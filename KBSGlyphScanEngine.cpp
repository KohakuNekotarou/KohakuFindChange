//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The missing-glyph (notdef) scanner: finds where InDesign is drawing a box instead of a
//  character, because the font in force has no glyph for it.
//
//  ***** WHY THIS READS THE COMPOSED RESULT INSTEAD OF SEARCHING FOR THE GLYPH *****
//
//  There is a search for this - IFindChangeUtils::SearchForGlyph with kAnyNotDefGlyphID (-2), and
//  the same thing through the DOM as findGlyphPreferences.glyphID = -2 - and it was built and
//  measured first. It cannot be used:
//
//    - a document holding OVERSET text takes InDesign down with it, through EVERY route: the
//      official Find/Change UI by hand, the DOM, and a plug-in walking under a text walker. That
//      makes it a defect in the application, not a way of calling it wrongly
//      (report: work/notdef-search-crash-report.md)
//    - the header even states that a notdef search reads the wax and therefore skips overset text
//      (IFindChangeUtils.h:59-60) - so it dies in the region it says it does not look at
//    - Adobe itself never calls it: there is not one use of SearchForGlyph anywhere in the SDK
//
//  Its safe cousin - glyph id 0 with a font stated - does not crash, but it needs a font for every
//  call (so every font in the document has to be enumerated and walked separately) and it assumes
//  notdef is glyph 0, which IFindChangeUtils.h:40 says outright is not true across fonts.
//
//  ***** SO IT DOES WHAT THE APPLICATION ITSELF DOES *****
//
//  InDesign's own missing-glyph checks read the composed result:
//    - the preflight rule visits WAX RUNS               (IPreflightWaxInfo.h:30-41)
//    - the on-screen highlight is handed IWaxGlyphs      (IGlobalTextAdornment.h:111-113, and
//      :174 carries a kTAPriMissingGlyphs drawing priority of its own)
//
//  Reading the wax gives the answer the composer actually reached - no font enumeration, the right
//  notdef id per font, and the font's name for free, since a run already knows its font.
//
//  Measured against the official preflight profile on 2026-08-02: same 7 findings, same stories,
//  same positions, table cells and footnotes included. Records:
//  docs/ai-notes/kbs-glyph-scan-searchforglyph-behavior.md (section 10).
//
//  ***** OVERSET IS NOT CHECKED, AND SAYS SO *****
//
//  Overset text has no glyphs to read. Its wax LINES exist (IWaxLine.h:68 defines an overset line
//  as one with no valid parcel key) but they carry no glyphs at all, and composing the entire
//  frame list adds none - both measured 2026-08-02. Predicting from the font instead was
//  considered and dropped (user, 2026-08-02): the official preflight rule reads wax too, so it
//  does not check overset either, and a prediction cannot see font fallback, OpenType substitution
//  or composite fonts, so it would report boxes that are not there. Reporting nothing silently
//  would be worse still, so a scan that met overset text says so on the status line.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"	// CopyText - reading the character between two boxes
#include "IDocument.h"			// GetName - the chapter row's display name
#include "IFrameList.h"			// GetFirstDamagedFrameIndex - is this story's wax out of date?
#include "IFrameListComposer.h"	// RecomposeThruLastFrame - compose what is stale before reading it
#include "ILayoutUIUtils.h"		// GetFrontDocument - the same way KBSSearchEngine resolves scope
#include "IPMFont.h"			// GetNotDefinedGlyph / AppendFamilyName / AppendStyleName
#include "IStoryList.h"			// the document's stories
#include "ITextModel.h"			// QueryStrand, and GetStoryThreadSpan - which thread a position is in
#include "IWaxGlyphIterator.h"	// one glyph at a time: its id, its text index, its run
#include "IWaxGlyphs.h"			// the container an iterator hands back (nil == end of line)
#include "IWaxIterator.h"		// line by line through a story's wax
#include "IWaxLine.h"			// GetParcelKey - IWaxLine.h:68 "overset == !ParcelKey.IsValid()"
#include "IWaxRenderData.h"		// the run's font, and whether that font is missing entirely
#include "IWaxRun.h"
#include "IWaxStrand.h"			// NewWaxIterator

// General includes:
#include "IDataBase.h"			// SaveRestoreModifiedState
#include "K2SmartPtr.h"			// K2::scoped_ptr - the two wax iterators are NOT reference counted
#include "PMString.h"
#include "ParcelKey.h"			// ParcelKey::IsValid - the overset test on a wax line
#include "PersistUtils.h"		// ::GetUIDRef
#include "ProgressBar.h"		// RangeProgressBar - the scan's progress + cancel
#include "TextChar.h"			// kTextChar_CR / kTextChar_LF - the only breaks a row may cross
#include "TextID.h"				// kFrameListBoss / IID_IWAXSTRAND - which strand holds the wax
#include "Utils.h"
#include "WideString.h"			// the gap's characters come back in one

#include <algorithm>
#include <vector>
#include <utility>				// std::move - a finished chapter is handed to the model, not copied

// Project includes:
#include "KBSBookScope.h"		// the chapter list, and the windowless chapters it holds open
#include "KBSGlyphScanEngine.h"
#include "KBSOversetScanEngine.h"	// StoryHasAnyOverset - the ONE answer to "is this overset right now?"
#include "KBSResultModel.h"
#include "KBSResultTree.h"		// Rebuild / ShowStatus - also what app.kfcStatus reads back
#include "KBSRunGuard.h"		// is anything else of ours running? (the modal bar pumps events)
#include "KBSSearchEngine.h"	// the borrowed hit builders (BuildHitForRange / FinalizeHits)

namespace
{

// A scan is running. Its progress bar pumps events, so without this a menu command could be
// dispatched INTO the running scan - and a second run clears the model this one is filling, or
// hands back the chapters it is walking. Read through KBSRunGuard::IsAnyRunning.
bool gScanning = false;

// Raise gScanning for the length of a scan, whichever way Run() returns - and it returns from
// several places. Modelled on KBSSearchEngine's SearchingFlagGuard.
struct ScanningFlagGuard
{
	ScanningFlagGuard()		{ gScanning = true; }
	~ScanningFlagGuard()	{ gScanning = false; }
};

// How many wax lines to walk before giving up on a story. Nothing here is known to be able to
// loop; this is a backstop so a defect could never hang the application.
const int32 kMaxWaxLines = 200000;

// How much of the progress bar one CHAPTER gets. Every chapter gets the same slice, because the run
// no longer knows how big a chapter is before it opens it: chapters are opened one at a time and
// closed again straight after, so there is no all-chapters-open moment in which to add up their
// story counts.
//
// Within a chapter the slice is divided by STORIES, which a document that IS open can be asked for
// for free (IStoryList keeps the count). The same shape and the same number the search uses -
// KBSSearchEngine's kKBSChapterProgressSpan, whose note explains the size.
//
// ***** It was 1 until 2026-08-05, and that was the whole reason a one-document scan showed nothing.
// ***** With a span of one there was no room to move the bar inside a chapter, so a document-scope
// scan sat at 0% and finished - and the Cancel button, which needs the bar to have been moved to
// answer at all, never got the chance. The reasoning recorded here for the old value ("story counts
// are no longer knowable up front") does not survive comparison with the search, which counts them
// AFTER opening each chapter and has always divided its slice that way.
const int32 kKBSChapterProgressSpan = 10000;

/** One notdef glyph: where it sits, and the font that had no glyph for it. */
struct NotdefGlyph
{
	TextIndex	pos;
	PMString	fontName;

	NotdefGlyph() : pos(kInvalidTextIndex) {}
	NotdefGlyph(TextIndex p, const PMString& font) : pos(p), fontName(font) {}
};

/** A stretch of consecutive notdef glyphs sharing one font - one row in the result tree. */
struct NotdefRun
{
	TextIndex	start;
	TextIndex	end;		// EXCLUSIVE
	PMString	fontName;
	int32		glyphs;		// how many BOXES this run holds - not the same as end - start, because a
							// run may swallow one line break (see MergeIntoRuns), and that character
							// is not a box. The summary counts these.

	NotdefRun() : start(kInvalidTextIndex), end(kInvalidTextIndex), glyphs(0) {}
};

bool ByPosition(const NotdefGlyph& a, const NotdefGlyph& b)
{
	return a.pos < b.pos;
}

/** The run's font, named the way the user sees it in the font menu.

    Family + style is how the OFFICIAL preflight rule names a font
    (preflightrule/PreflightFontRuleVisitor.cpp:262-263). IWaxRenderData::GetFontName would answer
    too, but it gives the PostScript name ("KozMinPr6N-Regular") - correct, and no help at all to
    someone about to go and fix the box. It is kept as the fallback for a run whose font object
    cannot be had. */
void AppendFontDisplayName(IPMFont* font, IWaxRenderData* render, PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	if (font == nil)
	{
		if (render != nil)
		{
			out = render->GetFontName();
			out.SetTranslatable(kFalse);
		}
		return;
	}

	font->AppendFamilyName(out);

	PMString style;
	style.SetTranslatable(kFalse);
	font->AppendStyleName(style);
	if (!style.IsEmpty())
	{
		out.Append(" ");
		out.Append(style);
	}
	out.SetTranslatable(kFalse);
}

/** Visit every glyph on one wax line and collect the notdefs.

    The notdef id belongs to the FONT, not to the text: IFindChangeUtils.h:40 states that
    "GetNotDefinedGlyph is different across different fonts", which is the whole reason a
    font-independent sentinel had to exist in the search API. So it is re-read whenever the run
    changes - a run being the longest stretch of a line sharing one font.

    A run whose FONT IS MISSING is skipped rather than reported. That text is drawn in a substitute
    face, which InDesign marks with a pink highlight; it is a different problem, and this scan is
    about boxes (design section 2). */
void CollectNotdefsOnLine(const IWaxLine* line, std::vector<NotdefGlyph>& out)
{
	K2::scoped_ptr<IWaxGlyphIterator> git(line->QueryWaxGlyphIterator(kFalse));
	if (git == nil)
		return;
	git->Reset();

	IWaxRun* lastRun = nil;
	Text::GlyphID notdef = 0;
	bool runUsable = false;
	PMString fontName;
	fontName.SetTranslatable(kFalse);

	// Reset() leaves the iterator ON the first glyph - KESCMPageNumberMarker.cpp:194-195 reads the
	// container straight after one - so the container is tested first and Advance() moves on.
	for (IWaxGlyphs* glyphs = git->GetWaxGlyphsContainer(); glyphs != nil; glyphs = git->Advance())
	{
		IWaxRun* run = git->GetWaxRun();	// NOT reference counted - IWaxGlyphIterator.h:116-124
		if (run != lastRun)
		{
			lastRun = run;
			runUsable = false;
			notdef = 0;
			fontName.Clear();

			InterfacePtr<IWaxRenderData> render(run, UseDefaultIID());
			if (render != nil && !render->FontFaceMissing())
			{
				InterfacePtr<IPMFont> font(render->QueryFont());		// Query - owned, released here
				if (font != nil)
				{
					notdef = font->GetNotDefinedGlyph();
					runUsable = true;
					AppendFontDisplayName(font, render, fontName);
				}
			}
		}

		if (!runUsable)
			continue;

		const Text::GlyphID gid = git->GetGlyphID();
		if (gid != 0 && gid != notdef)
			continue;

		out.push_back(NotdefGlyph(git->GetGlyphTextIndex(), fontName));
	}
}

/** Walk one story's wax and collect every notdef in the text that is actually composed.

    @param outHasOverset  RAISED (never lowered) when this story holds text that did not fit. */
void ScanStoryWax(ITextModel* model, std::vector<NotdefGlyph>& out, bool& outHasOverset)
{
	InterfacePtr<IWaxStrand> waxStrand(
		static_cast<IWaxStrand*>(model->QueryStrand(kFrameListBoss, IID_IWAXSTRAND)));
	if (waxStrand == nil)
		return;

	InterfacePtr<IFrameList> frameList(waxStrand, UseDefaultIID());
	if (frameList != nil)
	{
		// ***** Compose what is out of date BEFORE reading it. *****
		// The wax is the composer's last answer. A story edited since it last composed can hand
		// back glyphs that are no longer on the page - a box the user has already fixed, or none
		// where one has just appeared - so the scan would report the document as it used to be.
		// This is the official route (SnpInspectTextModel.cpp:724-733: ask the frame list for a
		// damaged index, then RecomposeThruLastFrame) and the one KBSJump.cpp:143-149 already takes
		// before reading this very wax. The two scans were the only readers that skipped it.
		if (frameList->GetFirstDamagedFrameIndex() != -1)
		{
			InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}
	}

	// ***** Does this story hold text that did not fit? ***** Asked AFTER the recompose above, and of the
	// same interface the overset scan uses, so this plug-in has ONE answer to the question rather
	// than two (KBSOversetScanEngine::StoryHasAnyOverset -> ITextParcelList::GetIsOverset). It covers
	// the whole story, cells included: text that did not compose carries no glyphs to read wherever
	// it sits, so a cell overflowing on its own has to be admitted to just as a pushed-out frame is.
	//
	// It used to be IFrameList::GetWasOverset(), asked BEFORE the recompose, and both halves of that
	// were wrong (2026-08-04):
	//   * WHAT it asked - the frame list remembers what the last composition found, and that answer
	//     is PERSISTED (IFrameList.h:146-157). A document whose overset had since been fixed still
	//     said yes, and kept saying yes after a close and reopen (measured on adv-index2.indd). The
	//     status line then claimed text it could not check while the overset scan, on the same
	//     document, correctly reported none - two scans of one plug-in disagreeing.
	//   * WHEN it asked - before the recompose that this very function performs, so even a fresh
	//     answer would have been the stale one.
	//
	// Not asked again once the answer is yes. The flag is raised for the whole SCAN, not per story,
	// and one sentence on the status line is all it can produce - while the question itself walks
	// every table and every cell of this story to answer it. (It is stated as a short-circuit rather
	// than left to the assignment because the cost is on the CALL, not on the store.)
	if (!outHasOverset && KBSOversetScanEngine::StoryHasAnyOverset(model))
		outHasOverset = true;

	// READ-ONLY iterator: this walk never changes a wax line nor applies one, which is exactly the
	// case IWaxStrand.h:100-106 describes ("code that draws") and offers an optimisation for. The
	// product code reads the wax this way (PrivateSpellingUtils.cpp:371, 579). The sample
	// SnpEstimateTextDepth.cpp:208 uses the plain iterator instead - two ways of spelling it - and
	// the product code is the one followed here.
	K2::scoped_ptr<const IWaxIterator> waxIter(waxStrand->NewReadOnlyWaxIterator());
	if (waxIter == nil)
		return;

	int32 lines = 0;
	int32 offset = 0;
	const IWaxLine* line = waxIter->GetFirstWaxLine(0, &offset);
	while (line != nil && lines < kMaxWaxLines)
	{
		++lines;

		// A line the composer has thrown away, which the strand can still hand over. Reading one is
		// what bug fix 538392 was about, and the product code tests for it before touching a line
		// (PrivateSpellingUtils.cpp:387-389).
		//
		// Its companion test there - IsDamaged - is deliberately NOT copied. That code is DRAWING:
		// a damaged line will be redrawn anyway, so skipping it costs nothing. This scan has just
		// composed the frame list above and wants every line it can get; skipping damaged ones
		// would silently drop text from the report.
		if (line->IsDestroyed())
		{
			line = waxIter->GetNextWaxLine();
			continue;
		}

		// An overset line (IWaxLine.h:68: no valid parcel key). Measured 2026-08-02: these carry no
		// glyphs whatsoever, and asking IFrameListComposer to compose the whole frame list adds
		// none - so there is nothing here to read, and nothing is guessed at either. Skipped.
		//
		// ***** Skipped, but NOT reported as overset from here. ***** Whether the STORY holds
		// overset is decided once, above, by the same question the overset scan asks. A line's
		// parcel key on its own is not that question: a story that fits perfectly still ends with a
		// line whose key is invalid, so raising the flag here made the scan announce
		// "Text in overset cannot be checked." about documents holding no overset at all - while
		// the overset scan, asked about the very same document, correctly answered none
		// (2026-08-04, measured on adv-index2.indd and adv-index3.indd).
		if (line->GetParcelKey().IsValid())
			CollectNotdefsOnLine(line, out);

		line = waxIter->GetNextWaxLine();
	}
}

/** Is the single character at 'pos' a line break that a row is allowed to swallow?

    Exactly two characters qualify, because exactly two are STORED in the text:
      kTextChar_CR (0x000D) - the paragraph terminator
      kTextChar_LF (0x000A) - a forced line break (Shift+Enter)

    A column, frame or page break is NOT a character of its own: it is a paragraph attribute
    (kStartParagraphPropertyScriptElement), so the story holds a plain CR for it and this covers it
    without naming it. TextChar.h:496 states that the 0xE00B-series break characters "are used only
    by Find/Change, and are not stored in a document", so there is nothing else to test for.

    Everything else ends the run and must: a space has a glyph of its own, a table anchor (0x0016)
    is a boundary a row must never cross, and an inline graphic (0xFFFC) is not text at all. */
bool IsCrossableBreak(IComposeScanner* scanner, TextIndex pos)
{
	if (scanner == nil)
		return false;

	WideString gap;
	scanner->CopyText(pos, 1, &gap);

	int32 n = 0;
	const UTF16TextChar* buf = gap.GrabUTF16Buffer(&n);
	if (buf == nil || n != 1)
		return false;
	return buf[0] == kTextChar_CR || buf[0] == kTextChar_LF;
}

/** Are these two positions in the SAME story thread?

    A story is not one run of text. The body, every table cell and every footnote are separate
    THREADS of the same ITextModel, laid end to end - which is why the wax iterator hands their
    lines over together, and why this scan finds boxes in all of them without asking.

    ***** A ROW MUST NOT CROSS A THREAD BOUNDARY, AND NOTHING ELSE STOPS IT. ***** Every story
    thread is terminated by a kTextChar_CR - stated as a rule to anyone writing one in
    hiddentext/HidTxtModel.cpp:243-244 - and IsCrossableBreak accepts a CR. So the last box of one
    thread and the first box of the next are exactly the "one line break apart" case MergeIntoRuns
    joins, and they were being joined.

    The table ANCHOR was never what needed guarding: 0x0016 is not a break, so it was already
    refused, and a row that stops at one looks correct. It is the CR that ENDS a thread that let a
    row escape into the next one.

    Measured 2026-08-05 on work/kbs-selftest/glyphscan-threadboundary.indd before this existed:
    "10 missing glyphs in 7 places" where 9 places is the truth. One row read "c1 [box][CR][box] c2"
    - one row naming TWO DIFFERENT CELLS - and the second box had no row of its own at all, so
    nothing on the panel could take the user to it. The glyph COUNT was right either way; what was
    lost was where they are.

    ***** ASKED OF THE MODEL, POSITION BY POSITION, RATHER THAN BY LISTING THE BOUNDARIES. ***** The
    first attempt walked ITextStoryThreadDictHier and collected each dictionary's
    GetThreadBlockTextRange().GetStart(). That fixed the body-to-cell case and left CELL-TO-CELL
    exactly as it was, because a dictionary is per TABLE, not per cell: its thread block covers the
    whole table, so the boundaries between its cells are not in that list at all (measured - p5 of
    the document above stayed merged). ITextModel answers about the thread ITSELF (ITextModel.h:226),
    which is the finest unit there is, so cells, footnotes and the body all come out of one question. */
bool SameStoryThread(ITextModel* model, TextIndex a, TextIndex b)
{
	if (model == nil)
		return true;		// cannot tell - behave as before rather than start splitting rows

	TextIndex startA = kInvalidTextIndex;
	TextIndex startB = kInvalidTextIndex;
	model->GetStoryThreadSpan(a, &startA);
	model->GetStoryThreadSpan(b, &startB);
	return startA == startB;
}

/** Sort the collected glyphs and merge neighbours into the runs that become rows.

    ***** The sort is not tidiness. IWaxGlyphIterator.h:151-156 warns that the text indices "may not be
    monotonically increasing nor will they necessarily change for every call to Advance()", because
    one character can produce several glyphs and several characters one glyph. Adjacency can only be
    judged once the positions are in order, and the same index can arrive more than once.

    A run is broken by a gap, by a change of font, OR by a THREAD BOUNDARY. Two boxes side by side
    in two different fonts are two different problems, and the row names the font; two boxes in
    different threads are in different PLACES - one in the body and one inside a table cell, or one
    in each of two cells - however few characters lie between them (see SameStoryThread).

    ONE line break may sit inside a run (2026-08-02, user's call): a stretch of text that a font
    cannot set does not stop being one problem because it wrapped onto a new paragraph, and the fix
    - applying a font to the range - wants to reach all of it in one go. Only a SINGLE break is
    crossed: two in a row mean an empty paragraph between the boxes, which is a different place.

    ***** The row DISPLAYS the whole run, break and all (2026-08-04). KBSSearchEngine::
    SplitLineAroundMatch no longer stops at the paragraph the range starts in, and the break itself
    is drawn as a mark - a pilcrow, or a return arrow for a forced line break - so a run that crosses
    one shows the boxes on BOTH sides of it. The COUNT and the RANGE always covered the whole run, so
    neither the summary nor any fix is changed by this; only what the row shows grew. */
void MergeIntoRuns(ITextModel* model, std::vector<NotdefGlyph>& found, std::vector<NotdefRun>& out)
{
	if (found.empty())
		return;

	std::sort(found.begin(), found.end(), ByPosition);

	// One scanner for the whole story rather than one per gap - it is only asked anything when two
	// boxes are exactly one character apart, which is rare, but the query itself is not free.
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());

	for (size_t i = 0; i < found.size(); ++i)
	{
		const NotdefGlyph& g = found[i];

		if (!out.empty())
		{
			NotdefRun& last = out.back();
			if (g.pos < last.end)
				continue;		// this character was already taken (several glyphs, one character)
			// The thread test is asked LAST, and deliberately: it is the only one of the three that
			// costs a call into the model, and by here the cheap two have already ruled out every box
			// that is not a neighbour of this run. last.end - 1 is the run's last BOX (a swallowed
			// break sits before that index, never on it), which is the position the new one has to
			// share a thread with. See SameStoryThread.
			if (last.fontName.Compare(kTrue, g.fontName) == 0
				&& (g.pos == last.end
					|| (g.pos == last.end + 1 && IsCrossableBreak(scanner, last.end)))
				&& SameStoryThread(model, last.end - 1, g.pos))
			{
				// Neighbour in the same font, or one line break away from it - keep it on one row.
				// The break itself is taken INTO the range, which is what lets a font be applied to
				// the whole stretch in one call. It is NOT counted as a box.
				last.end = g.pos + 1;
				++last.glyphs;
				continue;
			}
		}

		NotdefRun run;
		run.start = g.pos;
		run.end = g.pos + 1;
		run.fontName = g.fontName;
		run.glyphs = 1;
		out.push_back(run);
	}
}

/** Scan one document and build the chapter that goes into the result model.

    @param maxRows        stop collecting once this chapter holds this many rows - what is left of
                          the run's whole-run ceiling (KBSResultModel::kKBSCollectHitLimit).
    @param outHitCeiling  set when collecting stopped because of maxRows rather than because the
                          document ran out. The summary has to say so: a report that comes back
                          short without a word reads as "this is everything wrong with the book".
    @param progressBar    the run's bar, or nil. progressBase is where this chapter's slice starts
                          and chapterSpan is how wide it is; the slice is divided between this
                          document's stories, so the bar moves WITHIN a chapter and a one-document
                          scan is not a motionless 0%.
    @param outCancelled   set when the user pressed Cancel while this chapter was being walked, at
                          which point the scan stops where it stands and the caller throws the whole
                          list away.

    ***** THE CANCEL IS ASKED FROM INSIDE THE CHAPTER, WHICH THE SEARCH CANNOT DO. ***** WasCancelled
    pumps the event queue, and the search's walk holds the text walker's critical section while it
    runs - so there it can only be asked between chapters (see KBSSearchEngine's CollectHitsInDoc).
    This scan runs no walker: it reads IStoryList itself, holds no critical section, and processes no
    command, so the question is safe here. Asked once per story, which is far less often than the
    search moves its own bar.

    @return how many rows were produced. */
int32 ScanOneDocument(const UIDRef& docRef, const PMString& chapterName,
	KBSResultModel::Chapter& outChapter, bool& outHasOverset, int32& outGlyphCount,
	int32 maxRows, bool& outHitCeiling,
	RangeProgressBar* progressBar, int32 progressBase, int32 chapterSpan,
	int32& ioProgressReported, bool& outCancelled)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return 0;

	// Read-only, but reading the wax can leave the database thinking it changed (a damaged frame
	// gets recomposed on the way). The search walk guards itself the same way, and for a chapter
	// opened windowless it is what lets the document be closed again without wanting a save.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// Off the document's own UIDRef, which is how the search and the overset scan ask for this
	// (KBSSearchEngine::CountSearchableStories, KBSOversetScanEngine::ScanOneDocument). It used to be
	// spelled (db, db->GetRootUID()) here - the same object by a longer route, and a second spelling
	// of one question in the third of three loops that are meant to read alike.
	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return 0;

	outChapter.name = chapterName;
	outChapter.name.SetTranslatable(kFalse);
	outChapter.docRef = docRef;

	// Per-frame answers (page, layer switched off, locked) cost a structure walk each and a frame
	// usually holds several boxes, so they are remembered for the length of this document.
	KBSSearchEngine::HitCache* cache = KBSSearchEngine::NewHitCache();

	// User accessible only. IStoryList.h:38-42 states that internal stories are not subject to
	// find/change or spell checking, so scanning them would report boxes in places the rest of the
	// panel - and InDesign itself - never looks at.
	const int32 storyCount = storyList->GetUserAccessibleStoryCount();

	// This chapter's slice of the bar, cut into one piece per story. At least one step apiece, so a
	// document with more stories than the slice has steps still crawls forward rather than standing
	// still - the overshoot that allows is clamped at the chapter's own end below.
	int32 stepsPerStory = (storyCount > 0) ? (chapterSpan / storyCount) : chapterSpan;
	if (stepsPerStory < 1)
		stepsPerStory = 1;

	for (int32 i = 0; i < storyCount; ++i)
	{
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;

		// Table cells and footnotes are thread blocks of THIS model, not stories of their own, and
		// the wax iterator hands their lines over along with the body's - measured against the
		// official preflight, which finds exactly the same seven.
		std::vector<NotdefGlyph> found;
		ScanStoryWax(model, found, outHasOverset);

		std::vector<NotdefRun> runs;
		MergeIntoRuns(model, found, runs);

		for (size_t r = 0; r < runs.size(); ++r)
		{
			// The whole-run ceiling, asked here because this is where a row comes into existence.
			// Stopping mid-story is deliberate: the alternative is to finish "just this story",
			// which on the very document the ceiling exists for (one whose every character is a
			// box) is exactly the unbounded case being guarded against.
			if (static_cast<int32>(outChapter.hits.size()) >= maxRows)
			{
				outHitCeiling = true;
				break;
			}

			KBSResultModel::Hit hit;
			hit.checked = false;		// a scan is a report, not a work list: no row is selectable
			KBSSearchEngine::BuildHitForRange(docRef, storyRef, runs[r].start, runs[r].end,
				cache, hit);

			// The font that had no glyph for this text - the answer to "why is this a box", and the
			// thing the fix has to change. Kept RAW: the row is drawn with convertAmpersand off, so
			// an '&' in a font name has to survive verbatim, and app.kfcResults reports it as it is.
			hit.fontName = runs[r].fontName;
			hit.fontName.SetTranslatable(kFalse);

			outChapter.hits.push_back(hit);

			// Counted separately from the rows, because consecutive boxes are deliberately merged
			// into ONE row: a document with 45 boxes in two stretches makes two rows, and saying
			// "2 missing glyphs" about it would be a lie.
			//
			// The run's own tally, NOT its length: a run may swallow one line break, and that
			// character is not a box.
			outGlyphCount += runs[r].glyphs;
		}

		// Full: the remaining stories are not walked either. Reading them would cost the time of a
		// full scan to produce rows that are thrown away.
		if (outHitCeiling)
			break;

		// ----- the bar, and the button on it -----
		// Moving the bar is what makes Cancel answer at all: the button's state is set by the message
		// loop, and SetPosition is what runs it (see KBSAdvanceProgress). Not forced - the 8-step
		// threshold in there keeps a document of many tiny stories from repainting once per story.
		if (progressBar != nil)
		{
			int32 position = progressBase + (i + 1) * stepsPerStory;
			const int32 chapterEnd = progressBase + chapterSpan;
			if (position > chapterEnd)
				position = chapterEnd;		// never into the next chapter's slice - the bar would jump back
			KBSAdvanceProgress(progressBar, ioProgressReported, position);

			// kFalse = do not raise the global error state, which would outlive the scan and fail the
			// commands that come after it.
			if (progressBar->WasCancelled(kFalse))
			{
				outCancelled = true;
				break;
			}
		}
	}

	KBSSearchEngine::DeleteHitCache(cache);

	// Page order, and the "P<page>(<n>)" locator on every row - the same pass the search runs.
	KBSSearchEngine::FinalizeHits(outChapter.hits);
	return static_cast<int32>(outChapter.hits.size());
}

/** The status line: how many boxes, in how many places, and whether anything could not be looked at.

    The two numbers are different questions, and both matter. Consecutive boxes are deliberately
    merged into ONE row (design section 2), so a story with 45 solid boxes in two stretches makes
    two rows - and reporting "2 missing glyphs" about it would simply be wrong. */
void BuildSummary(int32 glyphs, int32 places, bool hasOverset, bool truncated, PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	if (glyphs == 0)
		out.Append("No missing glyphs.");
	else
	{
		out.AppendNumber(glyphs);
		out.Append(glyphs == 1 ? " missing glyph" : " missing glyphs");
		if (places > 1)
		{
			out.Append(" in ");
			out.AppendNumber(places);
			out.Append(" places");
		}
		out.Append(".");
	}

	// Said only when there IS overset text, so that it means something when it appears. The
	// official preflight rule reads wax as well and therefore checks no more than this does; what
	// it does not do is admit it, and a scan that quietly skipped part of the document would be
	// worse than one that found nothing.
	if (hasOverset)
		out.Append("  Text in overset cannot be checked.");

	// ***** THE LIST IS NOT THE WHOLE STORY - SAY SO. ***** A scan reads as "here is everything
	// wrong with this book", so one that stopped early has to admit it or it misreports the
	// document. The search says "narrow your search" at this point; a scan has no query to narrow,
	// so it only states the fact.
	if (truncated)
	{
		out.Append("  Stopped at the ");
		out.AppendNumber(KBSResultModel::kKBSCollectHitLimit);
		out.Append(" safety limit.");
	}
	// The display cap is a separate number and a separate sentence: the rows beyond it ARE collected
	// and DO reach Save Results, they are simply not drawn. The search has said this since it was
	// written; the scans never did, so a scan of more than 5000 places quietly showed 5000.
	if (places > KBSResultModel::kKBSDisplayHitLimit)
	{
		out.Append("  Showing first ");
		out.AppendNumber(KBSResultModel::kKBSDisplayHitLimit);
		out.Append(" in the panel.");
	}
}

}	// anonymous namespace

void KBSGlyphScanEngine::Run()
{
	PMString summary;
	summary.SetTranslatable(kFalse);

	// Last-resort re-entry stop, ahead of everything else. The panel's actions grey themselves out
	// while any run is up, but every one of those runs puts up a MODAL PROGRESS BAR THAT PUMPS
	// EVENTS, so a command can still find its way in here - through a script firing the action by
	// ID, if nothing else. Asked about EVERY run, not just another scan: what makes this dangerous
	// is two DIFFERENT runs, one of which hands back the chapters the other is walking. See
	// KBSRunGuard.
	if (KBSRunGuard::IsAnyRunning())
	{
		summary.Append(KBSRunGuard::BusyMessage());
		KBSResultTree::ShowStatus(summary);
		return;
	}
	const ScanningFlagGuard scanningGuard;

	// ----- the scope, resolved exactly the way a search resolves it -----
	// Book Scope ON means the whole book and nothing else; OFF means the front document and nothing
	// else. Never a silent fallback between them, so the status line can always say what was looked
	// at and a missing book is reported rather than quietly scanning one document instead.
	std::vector<KBSBookScope::ChapterDoc> targets;
	PMString bookName;
	std::vector<KBSBookScope::SkippedChapter> unopenable;
	const bool fromBook = KBSBookScope::IsBookScopeOn();

	// ***** IS THERE ANYTHING TO SCAN? ASKED BEFORE THE MODEL IS TOUCHED. *****
	// A run that is turned away has to leave the panel exactly as it found it. These two tests used
	// to sit BELOW the Clear() just under them, so "Book Scope is on, but no book is open." also
	// threw away whatever the panel was showing (found 2026-08-03 in the defect audit).
	if (fromBook && !KBSBookScope::HasActiveBook())
	{
		summary.Append("Book Scope is on, but no book is open.");
		KBSResultTree::ShowStatus(summary);
		return;
	}
	if (!fromBook && Utils<ILayoutUIUtils>()->GetFrontDocument() == nil)
	{
		summary.Append("No document to scan.");
		KBSResultTree::ShowStatus(summary);
		return;
	}

	// ***** THE COMMIT POINT. ***** The previous run's results go here, before the book is resolved -
	// the order the search uses.
	//
	// ***** It HAS to be before that. ***** ListBookChapters below records which book this run is
	// against (gSearchedBookPath) and ReleaseSearchedBook is what forgets it, so doing this
	// afterwards wipes the record the run has just made. The book watcher then has no book to ask
	// about, and closing that book leaves its results sitting on the panel. Measured 2026-08-02, with
	// these two lines below the scope block: the scan worked, and closing the book did nothing at all.
	//
	// ForgetSearchedFindFormat with them: a scan's rows are not a Find/Change query, so nothing here
	// will ever put a format back - which is exactly why the memory of the LAST search must not be
	// left standing over them (the rule has no exceptions; see KBSSearchEngine.h).
	KBSResultModel::Clear();
	KBSBookScope::ReleaseSearchedBook();	// the two are one fact - see gSearchedBookPath
	KBSSearchEngine::ForgetSearchedFindFormat();

	if (fromBook)
	{
		// Listed, not opened: each chapter is opened when its turn comes in the loop below and
		// handed straight back once it has been scanned. Whether a chapter can actually be opened
		// is not known yet - the summary reports the ones that could not, after the scan.
		if (!KBSBookScope::ListBookChapters(targets, bookName) || targets.empty())
		{
			// ***** THE MODEL IS ALREADY EMPTY BY HERE - DRAW IT. ***** This exit is PAST the commit
			// point above, so the previous run's rows are gone from the model; without this the tree
			// goes on showing them while the status line announces that nothing was scanned, and a
			// row the model no longer holds is a row that answers nothing when it is clicked.
			// ShowStatus writes the status widget and nothing else (see KBSResultTree.h).
			//
			// The search never had this fault, for a reason that is easy to miss: it returns its
			// summary to KBSActionComponent, which rebuilds the tree unconditionally afterwards. A
			// scan puts its own summary up, so every one of its exits has to say this for itself.
			KBSResultTree::Rebuild();
			summary.Append("The active book has no chapters.");
			KBSResultTree::ShowStatus(summary);
			return;
		}
	}
	else
	{
		// Re-read rather than carried down from the test above: nothing between them is meant to
		// disturb it, but a front-document pointer is not ours to assume survived anything.
		IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
		if (doc == nil)
		{
			KBSResultTree::Rebuild();	// past the commit point - see the sibling exit above
			summary.Append("No document to scan.");
			KBSResultTree::ShowStatus(summary);
			return;
		}
		KBSBookScope::ChapterDoc single;
		single.docRef = ::GetUIDRef(doc);
		doc->GetName(single.shortName);
		single.shortName.SetTranslatable(kFalse);
		targets.push_back(single);
	}

	// All three AFTER Clear(), which puts them back to their Find/Change defaults. The kind is what
	// takes the check boxes off every row and greys Change Checked out - a scan is a report, not a
	// work list.
	KBSResultModel::SetResultKind(KBSResultModel::kResultMissingGlyph);
	KBSResultModel::SetFromBook(fromBook);
	KBSResultModel::NoteRun();		// the panel's illustration changes once anything has been run
	KBSResultModel::SetBookName(bookName);

	// ----- the progress bar: one equal slice per chapter, divided by stories inside -----
	// Every chapter gets the same slice because a chapter's size is not knowable before it is opened,
	// and ScanOneDocument then divides that slice between the stories it finds - so the bar moves
	// through a single document too, which is what gives the Cancel button a chance to be pressed and
	// answered. Shown for both scopes, since a single document takes just as long as a short book and
	// equally needs a way out. See kKBSChapterProgressSpan.
	const int32 progressTotal = static_cast<int32>(targets.size()) * kKBSChapterProgressSpan;

	PMString progressTitle(fromBook ? "Scanning book for missing glyphs..."
									: "Scanning for missing glyphs...");
	progressTitle.SetTranslatable(kFalse);
	// showImmediate = kTrue: put the bar up at once rather than waiting out its internal delay, or
	// the one thing it is really there for - Cancel - is never on screen for a fast scan.
	RangeProgressBar progressBar(progressTitle, 0, progressTotal, kTrue, kTrue);
	// Reading the wax can make a damaged frame recompose, and a recompose is entitled to raise a bar
	// of its own - which would sit on top of this one and take the Cancel button with it. The search
	// and the replace both say this; the scans did not.
	progressBar.DisableChildProgressBars(kTrue);

	int32 progressBase = 0;
	int32 progressReported = 0;

	bool hasOverset = false;
	bool cancelled = false;
	// Set when the run stopped collecting at the whole-run ceiling rather than at the end of the
	// book. NOT the same as cancelled: what was collected is kept and reported, with a line saying
	// the list is not the whole story.
	bool collectionTruncated = false;
	int32 glyphTotal = 0;
	int32 rowTotal = 0;
	// Chapters this run OPENED and could not hand back. Named in the summary: they have no window,
	// so the user can neither see them nor close them, and they hold their .indd locked - see
	// KBSBookScope::AppendUnclosedNote.
	std::vector<PMString> unclosed;

	for (size_t i = 0; i < targets.size(); ++i)
	{
		PMString taskLine;
		taskLine.SetTranslatable(kFalse);
		taskLine.Append("Chapter ");
		taskLine.AppendNumber(static_cast<int32>(i) + 1);
		taskLine.Append(" / ");
		taskLine.AppendNumber(static_cast<int32>(targets.size()));
		taskLine.Append(" - ");
		taskLine.Append(targets[i].shortName);
		progressBar.SetTaskText(taskLine);
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// kFalse = do not raise the global error state, which would outlive the scan and fail the
		// commands that come after it.
		if (progressBar.WasCancelled(kFalse))
		{
			cancelled = true;
			break;
		}

		// ***** Room left under the whole-run ceiling. ***** Asked BEFORE the chapter is opened:
		// with the list already full, opening one more chapter would cost a document load to
		// produce rows that are thrown away.
		const int32 remaining = KBSResultModel::kKBSCollectHitLimit - rowTotal;
		if (remaining <= 0)
		{
			collectionTruncated = true;
			break;
		}

		// Open THIS chapter now. Book scope only - a document-scope target is the front document,
		// which is already open and never ours to close.
		if (fromBook && targets[i].docRef == UIDRef::gNull)
		{
			if (!KBSBookScope::OpenChapterDoc(targets[i], &unopenable))
			{
				// The reason is recorded; AppendUnopenableNote names it in the summary. Hand the
				// bar on all the same, so a book whose chapters will not open still fills it.
				progressBase += kKBSChapterProgressSpan;
				KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
				continue;
			}
		}

		const UIDRef chapterDocRef = targets[i].docRef;

		KBSResultModel::Chapter chapter;
		chapter.file = targets[i].file;		// so a jump can reopen a chapter that gets closed
		int32 chapterGlyphs = 0;
		const int32 rows = ScanOneDocument(chapterDocRef, targets[i].shortName, chapter,
			hasOverset, chapterGlyphs, remaining, collectionTruncated,
			&progressBar, progressBase, kKBSChapterProgressSpan, progressReported, cancelled);

		// This chapter is done, whatever it found: put the bar exactly where the next one starts, so a
		// chapter the walk left early still hands it on at the right place.
		progressBase += kKBSChapterProgressSpan;
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// ***** Hand the chapter back HERE, before its rows go into the model. ***** The scan is over
		// and its rows are plain data - UIDs and text indices, which survive the document being
		// closed. A jump reopens what it needs through ReopenChapterDoc. Only chapters KBS opened are
		// closed: ReleaseHeldDoc checks the held list itself, so one the user already had open passes
		// through untouched.
		//
		// ***** CLOSED ON THE SPOT, not scheduled. ***** A scheduled close waits for this run to
		// unwind, so every chapter handed back here would still be open, and still locking its
		// .indd, when the scan ended - the one thing the chapter-at-a-time shape exists to avoid
		// (measured 2026-08-04 on the replace, which walks chapters the same way). Safe at this
		// point: ScanOneDocument has returned, so its dirty guard has restored the flag and nothing
		// of ours is still holding the document.
		//
		// BEFORE AppendChapter, which is the order the search uses (KBSSearchEngine::SearchBook).
		// The two orders were both correct - appending touches no database - but three loops of one
		// shape that differ in a detail are three loops somebody has to compare line by line before
		// changing any of them.
		//
		// ***** ASKED FIRST WHETHER IT IS OURS, BECAUSE THE RELEASE'S false CANNOT BE READ ALONE.
		// ***** It answers false for four different things, two of them perfectly ordinary (the
		// chapter was the user's own copy, or it is no longer open) and two of them a real failure (it
		// came out modified, or the close was refused). Only the pair of questions tells them apart.
		// A chapter left behind by a failure is WINDOWLESS: nothing on screen shows it, nothing the
		// user can do closes it, and it holds its .indd locked for the rest of the session - so it is
		// worth a line in the summary.
		const bool wasOurs = KBSBookScope::IsHeldDoc(chapterDocRef);
		if (!KBSBookScope::ReleaseHeldDoc(chapterDocRef, true /*close now*/) && wasOurs)
			unclosed.push_back(targets[i].shortName);

		// ***** Cancelled INSIDE this chapter - and only now may the loop end. ***** The release above
		// has to run first whatever happened: a chapter abandoned half-walked is still a chapter this
		// run opened windowless, and leaving it standing would lock its .indd with no window to close
		// it by. Its rows are dropped rather than appended - the cancel throws the whole list away a
		// few lines below, and a partial chapter has no business reaching the model on the way.
		if (cancelled)
			break;

		if (rows > 0)
		{
			// Handed over, not copied: the model takes the rows and leaves this Chapter empty, which
			// is safe because it is a fresh one per pass of this loop and nothing below reads it.
			KBSResultModel::AppendChapter(std::move(chapter));	// only chapters with findings go in
			rowTotal += rows;
			glyphTotal += chapterGlyphs;
		}
	}

	// ***** Ask ONE more time, outside the loop. *****
	// Asking only inside it misses a cancel pressed during the LAST chapter, because no further
	// round comes to hear it - and on a one-chapter book that meant Cancel never worked at all.
	// (The same fault was found and fixed in the search and the replace on 2026-07-31.)
	if (!cancelled && progressBar.WasCancelled(kFalse))
		cancelled = true;

	if (cancelled)
	{
		// Throw the half-finished list away rather than leave a partial one looking complete, and
		// give the chapters back. The closes are scheduled, so it is safe from in here.
		KBSResultModel::Clear();
		KBSBookScope::ReleaseSearchedBook();	// closes the chapters AND forgets the book
		KBSSearchEngine::ForgetSearchedFindFormat();	// ...and the format, as every Clear() does
		KBSResultTree::Rebuild();
		summary.Append("Scan cancelled.");
		KBSResultTree::ShowStatus(summary);
		return;
	}

	KBSResultTree::Rebuild();

	BuildSummary(glyphTotal, rowTotal, hasOverset, collectionTruncated, summary);
	// The two ends of the run, in the order they happened: what could not be opened, then what could
	// not be closed again. Both append nothing in the ordinary case.
	KBSBookScope::AppendUnopenableNote(summary, unopenable);
	KBSBookScope::AppendUnclosedNote(summary, unclosed);
	KBSResultTree::ShowStatus(summary);
}

bool KBSGlyphScanEngine::IsScanning()
{
	return gScanning;
}

// End, KBSGlyphScanEngine.cpp.
