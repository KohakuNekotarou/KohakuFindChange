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
#include "IDocument.h"			// GetName - the chapter row's display name
#include "IFindChangeUtils.h"	// kAnyNotDefGlyphID - RunWithWalker only
#include "IFrameList.h"			// GetWasOverset - "this story has text that did not fit"
#include "ILayoutUIUtils.h"		// GetFrontDocument - the same way KBSSearchEngine resolves scope
#include "IMenuUtils.h"			// InsertAmpersandForDisplay - '&' doubles in anything DRAWN
#include "IPMFont.h"			// GetNotDefinedGlyph / AppendFamilyName / AppendStyleName
#include "IStoryList.h"			// the document's stories
#include "ITextModel.h"			// QueryStrand
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
#include "TextID.h"				// kFrameListBoss / IID_IWAXSTRAND - which strand holds the wax
#include "Utils.h"

#include <algorithm>
#include <vector>

// Project includes:
#include "KBSBookScope.h"		// the chapter list, and the windowless chapters it holds open
#include "KBSGlyphScanEngine.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"		// Rebuild / ShowStatus - also what app.kbsStatus reads back
#include "KBSSearchEngine.h"	// the borrowed hit builders, and SearchBook for RunWithWalker

namespace
{

// How many wax lines to walk before giving up on a story. Nothing here is known to be able to
// loop; this is a backstop so a defect could never hang the application.
const int32 kMaxWaxLines = 200000;

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

	NotdefRun() : start(kInvalidTextIndex), end(kInvalidTextIndex) {}
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
void CollectNotdefsOnLine(IWaxLine* line, std::vector<NotdefGlyph>& out)
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

	// Asked of the FRAME LIST, which remembers the answer from its last composition
	// (IFrameList.h:146-157), because the wax cannot always say: an overset line carries no glyphs,
	// and a story placed nowhere at all has no lines to look at. GetWasOversetValid comes first -
	// the state is persisted, so it can be there without meaning anything yet (:149).
	InterfacePtr<IFrameList> frameList(waxStrand, UseDefaultIID());
	if (frameList != nil && frameList->GetWasOversetValid() && frameList->GetWasOverset())
		outHasOverset = true;

	K2::scoped_ptr<IWaxIterator> waxIter(waxStrand->NewWaxIterator());
	if (waxIter == nil)
		return;

	int32 lines = 0;
	int32 offset = 0;
	IWaxLine* line = waxIter->GetFirstWaxLine(0, &offset);
	while (line != nil && lines < kMaxWaxLines)
	{
		++lines;

		// An overset line (IWaxLine.h:68: no valid parcel key). Measured 2026-08-02: these carry no
		// glyphs whatsoever, and asking IFrameListComposer to compose the whole frame list adds
		// none - so there is nothing here to read, and nothing is guessed at either. The scan
		// records that it could not look, and the summary says so.
		if (!line->GetParcelKey().IsValid())
			outHasOverset = true;
		else
			CollectNotdefsOnLine(line, out);

		line = waxIter->GetNextWaxLine();
	}
}

/** Sort the collected glyphs and merge neighbours into the runs that become rows.

    ***** The sort is not tidiness. IWaxGlyphIterator.h:151-156 warns that the text indices "may not be
    monotonically increasing nor will they necessarily change for every call to Advance()", because
    one character can produce several glyphs and several characters one glyph. Adjacency can only be
    judged once the positions are in order, and the same index can arrive more than once.

    A run is broken by a gap OR by a change of font: two boxes side by side in two different fonts
    are two different problems, and the row names the font. */
void MergeIntoRuns(std::vector<NotdefGlyph>& found, std::vector<NotdefRun>& out)
{
	if (found.empty())
		return;

	std::sort(found.begin(), found.end(), ByPosition);

	for (size_t i = 0; i < found.size(); ++i)
	{
		const NotdefGlyph& g = found[i];

		if (!out.empty())
		{
			NotdefRun& last = out.back();
			if (g.pos < last.end)
				continue;		// this character was already taken (several glyphs, one character)
			if (g.pos == last.end && last.fontName.Compare(kTrue, g.fontName) == 0)
			{
				last.end = g.pos + 1;		// neighbour in the same font - keep it on one row
				continue;
			}
		}

		NotdefRun run;
		run.start = g.pos;
		run.end = g.pos + 1;
		run.fontName = g.fontName;
		out.push_back(run);
	}
}

/** Scan one document and build the chapter that goes into the result model.
    @return how many rows were produced. */
int32 ScanOneDocument(const UIDRef& docRef, const PMString& chapterName,
	KBSResultModel::Chapter& outChapter, bool& outHasOverset, int32& outGlyphCount)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return 0;

	// Read-only, but reading the wax can leave the database thinking it changed (a damaged frame
	// gets recomposed on the way). The search walk guards itself the same way, and for a chapter
	// opened windowless it is what lets the document be closed again without wanting a save.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	InterfacePtr<const IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
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
		MergeIntoRuns(found, runs);

		for (size_t r = 0; r < runs.size(); ++r)
		{
			KBSResultModel::Hit hit;
			hit.checked = false;		// a scan is a report, not a work list: no row is selectable
			KBSSearchEngine::BuildHitForRange(docRef, storyRef, runs[r].start, runs[r].end,
				cache, hit);
			outChapter.hits.push_back(hit);

			// Counted separately from the rows, because consecutive boxes are deliberately merged
			// into ONE row: a document with 45 boxes in two stretches makes two rows, and saying
			// "2 missing glyphs" about it would be a lie.
			outGlyphCount += static_cast<int32>(runs[r].end - runs[r].start);
		}
	}

	KBSSearchEngine::DeleteHitCache(cache);

	// Page order, and the "P<page>(<n>)" locator on every row - the same pass the search runs.
	KBSSearchEngine::FinalizeHits(outChapter.hits);
	return static_cast<int32>(outChapter.hits.size());
}

/** How many stories a scan of this document will visit - the progress bar's unit.

    Free to ask for: IStoryList keeps the count, so nothing is loaded here. USER-ACCESSIBLE only,
    which is exactly the population the scan walks. */
int32 CountScannableStories(const UIDRef& docRef)
{
	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return 0;
	return storyList->GetUserAccessibleStoryCount();
}

/** Name the chapters the book could not hand over as documents at all, and what the book says about
    them. Reported rather than dropped: a chapter missing from the list is indistinguishable from a
    chapter that simply held no boxes, and nothing on screen would let the user tell them apart.
    Appends nothing when every chapter opened. */
void AppendUnopenableNote(PMString& outSummary,
	const std::vector<KBSBookScope::SkippedChapter>& skipped)
{
	if (skipped.empty())
		return;

	outSummary.Append("  ");
	outSummary.AppendNumber(static_cast<int32>(skipped.size()));
	outSummary.Append(" chapter(s) could not be opened (");
	for (size_t i = 0; i < skipped.size(); ++i)
	{
		if (i > 0)
			outSummary.Append(", ");
		if (i >= 3)								// a status line is one line
		{
			outSummary.Append("...");
			break;
		}
		PMString name(skipped[i].name);
		name.SetTranslatable(kFalse);
		Utils<IMenuUtils>()->InsertAmpersandForDisplay(&name);	// this string gets DRAWN
		outSummary.Append(name);
		if (!skipped[i].reason.IsEmpty())
		{
			outSummary.Append(": ");
			PMString reason(skipped[i].reason);
			reason.SetTranslatable(kFalse);
			outSummary.Append(reason);
		}
	}
	outSummary.Append(").");
}

/** The status line: how many boxes, in how many places, and whether anything could not be looked at.

    The two numbers are different questions, and both matter. Consecutive boxes are deliberately
    merged into ONE row (design section 2), so a story with 45 solid boxes in two stretches makes
    two rows - and reporting "2 missing glyphs" about it would simply be wrong. */
void BuildSummary(int32 glyphs, int32 places, bool hasOverset, PMString& out)
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
}

}	// anonymous namespace

void KBSGlyphScanEngine::Run()
{
	PMString summary;
	summary.SetTranslatable(kFalse);

	// ----- the scope, resolved exactly the way a search resolves it -----
	// Book Scope ON means the whole book and nothing else; OFF means the front document and nothing
	// else. Never a silent fallback between them, so the status line can always say what was looked
	// at and a missing book is reported rather than quietly scanning one document instead.
	std::vector<KBSBookScope::ChapterDoc> targets;
	PMString bookName;
	std::vector<KBSBookScope::SkippedChapter> unopenable;
	const bool fromBook = KBSBookScope::IsBookScopeOn();

	if (fromBook)
	{
		if (!KBSBookScope::HasActiveBook())
		{
			summary.Append("Book Scope is on, but no book is open.");
			KBSResultTree::ShowStatus(summary);
			return;
		}
		if (!KBSBookScope::GetBookChapterDocs(targets, bookName, &unopenable) || targets.empty())
		{
			summary.Append("The active book has no openable chapters.");
			AppendUnopenableNote(summary, unopenable);
			KBSResultTree::ShowStatus(summary);
			return;
		}
	}
	else
	{
		IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
		if (doc == nil)
		{
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

	KBSResultModel::Clear();

	// All three AFTER Clear(), which puts them back to their Find/Change defaults. The kind is what
	// takes the check boxes off every row and greys Change Checked out - a scan is a report, not a
	// work list.
	KBSResultModel::SetResultKind(KBSResultModel::kResultMissingGlyph);
	KBSResultModel::SetFromBook(fromBook);
	KBSResultModel::SetBookName(bookName);

	// ----- the progress bar, sized in STORIES -----
	// A chapter is far too coarse a step: one chapter of a hundred stories and one of two would each
	// move the bar once, so it would stand still through the long one. Story counts are free to ask
	// for (IStoryList keeps the count). Shown for both scopes, since a single document with many
	// stories takes just as long as a short book and equally needs a way out.
	std::vector<int32> chapterSpans;
	chapterSpans.reserve(targets.size());
	int32 progressTotal = 0;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		int32 stories = CountScannableStories(targets[i].docRef);
		if (stories < 1)
			stories = 1;		// so a chapter with no stories still moves the bar
		chapterSpans.push_back(stories);
		progressTotal += stories;
	}

	PMString progressTitle(fromBook ? "Scanning book for missing glyphs..."
									: "Scanning for missing glyphs...");
	progressTitle.SetTranslatable(kFalse);
	// showImmediate = kTrue: put the bar up at once rather than waiting out its internal delay, or
	// the one thing it is really there for - Cancel - is never on screen for a fast scan.
	RangeProgressBar progressBar(progressTitle, 0, progressTotal, kTrue, kTrue);

	int32 progressBase = 0;
	int32 progressReported = 0;

	bool hasOverset = false;
	bool cancelled = false;
	int32 glyphTotal = 0;
	int32 rowTotal = 0;

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

		KBSResultModel::Chapter chapter;
		chapter.file = targets[i].file;		// so a jump can reopen a chapter that gets closed
		int32 chapterGlyphs = 0;
		const int32 rows = ScanOneDocument(targets[i].docRef, targets[i].shortName, chapter,
			hasOverset, chapterGlyphs);

		progressBase += chapterSpans[i];
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		if (rows > 0)
		{
			KBSResultModel::AppendChapter(chapter);		// only chapters with findings go in
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
		// give the chapters back. ReleaseHeldDocs schedules its closes, so it is safe from in here.
		KBSResultModel::Clear();
		KBSBookScope::ReleaseHeldDocs();
		KBSResultTree::Rebuild();
		summary.Append("Scan cancelled.");
		KBSResultTree::ShowStatus(summary);
		return;
	}

	KBSResultTree::Rebuild();

	BuildSummary(glyphTotal, rowTotal, hasOverset, summary);
	AppendUnopenableNote(summary, unopenable);
	KBSResultTree::ShowStatus(summary);
}

//========================================================================================
// The -2 route, kept only until the wax scan has been signed off on the real application.
//
// !! DO NOT SHIP THIS. glyphID = -2 takes InDesign down on any document holding overset text, by
//   every route there is (see the header comment). It is here so the two can be run over the same
//   document during testing, and it goes when the menu item that calls it goes.
//========================================================================================

void KBSGlyphScanEngine::RunWithWalker()
{
	PMString summary;
	summary.SetTranslatable(kFalse);
	KBSSearchEngine::SearchBook(summary, kAnyNotDefGlyphID);
	KBSResultTree::ShowStatus(summary);
}

// End, KBSGlyphScanEngine.cpp.
