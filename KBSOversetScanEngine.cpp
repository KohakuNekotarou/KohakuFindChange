//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The overset scanner. See KBSOversetScanEngine.h for the contract.
//
//  Two kinds of overflow are looked for, and they need different questions:
//
//    * a FRAME's thread - the red "+". ITextUtils::IsOverset answers it from the frame list.
//    * a TABLE CELL on its own - the red dot. A cell holds no frame list, so the frame's answer
//      says nothing about it; the cell's own thread has to be asked. Every table in the story is
//      walked, and any table found inside a cell is walked too, so a nested cell is reached.
//
//  Where the overflow starts and how much of it there is both come from ITextParcelList, which is
//  the only interface that knows: GetIsOverset fills in the first overset TextIndex, and
//  GetFirstOversetTextIndex hands back the thread's last index beside it.
//
//  Everything the ROW needs after that is borrowed from KBSSearchEngine - the page locator (which
//  names the page the "+" sits on when the range itself is overset, exactly what is wanted here),
//  the hidden and locked flags, the three drawn segments and the page ordering. Nothing in the
//  search or the glyph scan was changed to make this possible.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"			// GetName - the chapter row's display name
#include "IFrameList.h"			// the argument ITextUtils::IsOverset takes
#include "ILayoutUIUtils.h"		// GetFrontDocument - the same way KBSSearchEngine resolves scope
#include "IStoryList.h"			// the document's stories
#include "ITableModel.h"		// const_iterator / GetGridID - one visit per cell thread
#include "ITableModelList.h"	// every table in a story (deprecated but current - SnpIterTableStories)
#include "ITextModel.h"			// QueryFrameList / QueryTextParcelList
#include "ITextParcelList.h"	// GetIsOverset / GetFirstOversetTextIndex - the whole answer
#include "ITextStoryThread.h"	// GetTextStart - a cell thread's first TextIndex
#include "ITextStoryThreadDict.h"	// QueryThread(gridID) - the dictionary lives on the table model
#include "ITextUtils.h"			// IsOverset

// General includes:
#include "IDataBase.h"			// SaveRestoreModifiedState
#include "PMString.h"
#include "PersistUtils.h"		// ::GetUIDRef
#include "ProgressBar.h"		// RangeProgressBar - the scan's progress + cancel
#include "TableTypes.h"			// GridAddress / GridID
#include "TextID.h"				// kTextStoryBoss - the boss ITableModelList sits on
#include "Utils.h"

#include <vector>

// Project includes:
#include "KBSBookScope.h"		// the chapter list, and the windowless chapters it holds open
#include "KBSOversetScanEngine.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"		// Rebuild / ShowStatus - also what app.kbsStatus reads back
#include "KBSRunGuard.h"		// is anything else of ours running? (the modal bar pumps events)
#include "KBSSearchEngine.h"	// the borrowed hit builders

namespace
{

// A scan is running. Its progress bar pumps events, so without this a menu command could be
// dispatched INTO the running scan - and a second run clears the model this one is filling, or
// hands back the chapters it is walking. Read through KBSRunGuard::IsAnyRunning.
bool gScanning = false;

// Raise gScanning for the length of a scan, whichever way Run() returns. The glyph scan's own
// guard, and KBSSearchEngine's before it.
struct ScanningFlagGuard
{
	ScanningFlagGuard()		{ gScanning = true; }
	~ScanningFlagGuard()	{ gScanning = false; }
};

// How much of the overset text a row shows. Overset runs to hundreds of characters - the official
// preflight reported 370 for one frame in the test document - and handing the whole range to
// BuildHitForRange would put all of it in the model and in app.kbsResults, when the row only ever
// draws one line. 60 is comfortably more than a row can show.
const int32 kOversetPreviewChars = 60;

// How much of the progress bar one CHAPTER gets. Every chapter gets the same slice: chapters are
// opened one at a time now and closed again straight after, so there is no moment at which the scan
// could add up all their story counts. This scan only moves the bar at chapter boundaries anyway,
// so an equal slice costs nothing but the weighting between a long chapter and a short one.
const int32 kKBSChapterProgressSpan = 1;

/** One place where text did not fit. */
struct OversetPlace
{
	UIDRef		storyRef;
	TextIndex	start;		// the first character that did not fit
	int32		count;		// how many characters did not fit
	bool		inCell;		// true = a table cell overflowing on its own, false = a frame's thread

	OversetPlace() : start(0), count(0), inCell(false) {}
};

/** Ask the thread containing 'pos' whether it is overset, and if so where the overflow starts and
    how much of it there is.

    ***** GetIsOverset is the only judgement that works for a table cell. ***** The obvious test -
    GetParcelFrameUID(GetLastParcelKey()) == kInvalidUID - misses every one of them: a cell's parcel
    IS placed, so its frame UID is valid, while the cell still overflows (ITextParcelList.h:705-713,
    and KESCM measured exactly that on 2026-07-24). */
bool ThreadOverset(ITextModel* model, TextIndex pos, TextIndex& outStart, int32& outCount)
{
	if (model == nil)
		return false;

	InterfacePtr<ITextParcelList> tpl(model->QueryTextParcelList(pos));
	if (tpl == nil)
		return false;

	TextIndex first = kInvalidTextIndex;
	if (!tpl->GetIsOverset(&first) || first == kInvalidTextIndex)
		return false;

	TextIndex threadLast = kInvalidTextIndex;
	tpl->GetFirstOversetTextIndex(&threadLast);

	// The last CR is not overset - a thread counts as overset when anything OTHER than that final
	// CR failed to compose - so it is not counted either. A count of at least one, because getting
	// here means something did not fit.
	outStart = first;
	outCount = (threadLast > first) ? static_cast<int32>(threadLast - first) : 1;
	return true;
}

/** Collect every cell of every table in this story that is overset ON ITS OWN.

    ITableModelList is documented to list a story's tables without promising that a table inside a
    CELL is among them - so it was measured rather than assumed (2026-08-02,
    work/kbs-selftest/overset-shapes.indd): it hands back the NESTED tables too. The
    "Table cell (121)" finding that matches the official preflight comes from a table inside a cell,
    and no recursion is written here. ***** Do not add one. ***** Walking a cell's own tables as
    well would visit those threads a second time and report every nested cell twice. */
void CollectOversetCells(const UIDRef& storyRef, ITextModel* model, std::vector<OversetPlace>& out)
{
	InterfacePtr<ITableModelList> tableList(storyRef, UseDefaultIID());
	if (tableList == nil)
		return;

	const int32 tableCount = tableList->GetModelCount();
	for (int32 t = 0; t < tableCount; ++t)
	{
		InterfacePtr<ITableModel> table(tableList->QueryNthModel(t));
		if (table == nil)
			continue;

		// The dictionary that maps a cell to its own text thread lives on the table model itself.
		InterfacePtr<ITextStoryThreadDict> dict(table, UseDefaultIID());
		if (dict == nil)
			continue;

		// The model's own iterator visits ANCHOR cells - a merged cell comes past once, not once
		// per grid square it covers - which is exactly one visit per thread.
		for (ITableModel::const_iterator it(table->begin()), last(table->end()); it != last; ++it)
		{
			const GridAddress ga = *it;
			const GridID gridID = table->GetGridID(ga);

			InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));
			if (thread == nil)
				continue;

			int32 span = 0;
			const TextIndex cellPos = thread->GetTextStart(&span);

			TextIndex start = 0;
			int32 count = 0;
			if (!ThreadOverset(model, cellPos, start, count))
				continue;

			OversetPlace place;
			place.storyRef = storyRef;
			place.start = start;
			place.count = count;
			place.inCell = true;
			out.push_back(place);
		}
	}
}

/** Scan one document and turn every overset place into a result row.

    @param outOffPage  incremented for a place whose "+" sits on no page - a frame on the
                       pasteboard. Those are counted but not listed: a row that cannot be jumped to
                       is worse than a number, and the official preflight does not report them at
                       all (measured 2026-08-02), so saying how many there were is already more
                       than InDesign itself offers.
    @return how many ROWS the chapter got. */
int32 ScanOneDocument(const UIDRef& docRef, const PMString& chapterName,
	KBSResultModel::Chapter& outChapter, int32& outOffPage)
{
	outChapter.name = chapterName;
	outChapter.docRef = docRef;

	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return 0;

	// Reading must not dirty the document: asking a thread about its composition can make it
	// compose, and a document that came up clean - a chapter this scan opened windowless, say - has
	// to go back clean or the user is asked to save something they never touched.
	IDataBase::SaveRestoreModifiedState dirtyGuard(docRef.GetDataBase());

	std::vector<OversetPlace> places;

	// User accessible only - the population the search and the glyph scan walk. IStoryList.h:38-42
	// states internal stories are not subject to find/change, so reporting them would name places
	// the rest of the panel never looks at.
	const int32 storyCount = storyList->GetUserAccessibleStoryCount();
	for (int32 i = 0; i < storyCount; ++i)
	{
		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;

		// (1) the thread the frames carry - the red "+".
		InterfacePtr<IFrameList> frameList(model->QueryFrameList());
		if (frameList != nil && Utils<ITextUtils>()->IsOverset(frameList))
		{
			TextIndex start = 0;
			int32 count = 0;
			if (ThreadOverset(model, 0, start, count))
			{
				OversetPlace place;
				place.storyRef = storyRef;
				place.start = start;
				place.count = count;
				place.inCell = false;
				places.push_back(place);
			}
		}

		// (2) cells overflowing on their own - the red dot. Nested tables included.
		CollectOversetCells(storyRef, model, places);
	}

	KBSSearchEngine::HitCache* cache = KBSSearchEngine::NewHitCache();
	for (size_t p = 0; p < places.size(); ++p)
	{
		KBSResultModel::Hit hit;
		hit.checked = false;		// a scan is a report, not a work list: no row is selectable

		const TextIndex previewEnd = places[p].start + kOversetPreviewChars;
		KBSSearchEngine::BuildHitForRange(docRef, places[p].storyRef,
			places[p].start, previewEnd, cache, hit);

		// Is there a PAGE to point at? pageIndex is the page's plain document order, and it stays
		// -1 when there is none. The page STRING is no use for this: a frame on the pasteboard has
		// no page, but the lookup falls back to the spread and GetPageString spells that "PB", so
		// the string is non-empty for exactly the case being excluded here (measured 2026-08-02 -
		// the first run listed the pasteboard frame as "PPBov").
		if (hit.pageIndex < 0)
		{
			++outOffPage;
			continue;
		}

		// What KIND of overflow this is and HOW MUCH of it there is, put where the cell protects it.
		//
		// ***** It goes in matchText, not in preText. ***** The cell keeps the MATCH at full
		// strength and ellipsizes the context around it, and the leading context loses its HEAD
		// (KBSColorTextView: kEllipsizeBeginning, so the words just before a match survive). That is
		// right for a search hit and wrong here: written into preText, "Frame (370)" was the first
		// thing thrown away and the row read "...70)" (measured 2026-08-02). It is also the truer
		// place for it - an overset row has no "match", and the size IS the finding.
		//
		// The text that did not fit follows as trailing context, faded, which is what it is: a peek,
		// not the answer. Written afterwards for the same reason the glyph scan writes its font name
		// afterwards - so BuildHitForRange stays untouched.
		PMString kindAndSize(places[p].inCell ? "Table cell (" : "Frame (");
		kindAndSize.SetTranslatable(kFalse);
		kindAndSize.AppendNumber(places[p].count);
		kindAndSize.Append(")");

		PMString peek("  ");
		peek.SetTranslatable(kFalse);
		peek.Append(hit.matchText);		// what BuildHitForRange read out of the overset range

		hit.preText.Clear();
		hit.matchText = kindAndSize;
		hit.postText = peek;

		// LEFT EMPTY on purpose: the tree builds its font level out of this, and an overset finding
		// has no font to group by - so the tree stays the three levels it has always had.
		hit.fontName.Clear();

		outChapter.hits.push_back(hit);
	}
	KBSSearchEngine::DeleteHitCache(cache);

	// Page order, and the "ovP<page>(<n>)" locator on every row - the same pass the search runs.
	KBSSearchEngine::FinalizeHits(outChapter.hits);
	return static_cast<int32>(outChapter.hits.size());
}

/** The status line: how many places, over how many chapters, and how many could not be pointed at.

    The two numbers are separate questions and are NOT added up: the first is how many rows are in
    the list, the second is how many places were left out of it. A total would appear nowhere on
    screen and could not be checked against anything. */
void BuildSummary(int32 places, int32 chaptersWithHits, int32 chapterTotal, bool fromBook,
	const PMString& bookName, int32 offPage, PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	if (places == 0)
	{
		// ***** Which sentence depends on whether the one below it is coming. ***** "No overset
		// text." followed by "1 not on a page." is the panel contradicting itself in two sentences:
		// the scan DID find overflow, it just had no row to offer for it. Only the pasteboard case
		// can produce it, which is why the plain wording survived the first measurements.
		out.Append(offPage > 0 ? "No overset text on a page." : "No overset text.");
	}
	else
	{
		out.AppendNumber(places);
		out.Append(places == 1 ? " overset place" : " overset places");
		if (fromBook)
		{
			out.Append(" in ");
			out.AppendNumber(chaptersWithHits);
			out.Append(" of ");
			out.AppendNumber(chapterTotal);
			out.Append(chapterTotal == 1 ? " chapter" : " chapters");
			if (!bookName.IsEmpty())
			{
				PMString name(bookName);
				name.SetTranslatable(kFalse);
				out.Append(" - book \"");
				out.Append(name);
				out.Append("\"");
			}
		}
		out.Append(".");
	}

	if (offPage > 0)
	{
		out.Append("  ");
		out.AppendNumber(offPage);
		out.Append(" not on a page.");
	}
}

}	// anonymous namespace

void KBSOversetScanEngine::Run()
{
	PMString summary;
	summary.SetTranslatable(kFalse);

	// Last-resort re-entry stop, ahead of everything else - the same door the glyph scan has, and
	// for the same reason: every KBS run puts up a MODAL PROGRESS BAR THAT PUMPS EVENTS, so a
	// command can be dispatched into this one (a script firing the action by ID reaches here
	// whatever the menu says). Asked about EVERY run, because the damage needs two DIFFERENT ones -
	// the inner run hands back the chapters this loop is walking. See KBSRunGuard.
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

	if (fromBook)
	{
		if (!KBSBookScope::HasActiveBook())
		{
			summary.Append("Book Scope is on, but no book is open.");
			KBSResultTree::ShowStatus(summary);
			return;
		}
		// Listed, not opened: each chapter is opened when its turn comes in the loop below and
		// handed straight back once it has been scanned. Whether a chapter can actually be opened
		// is not known yet - the summary reports the ones that could not, after the scan.
		if (!KBSBookScope::ListBookChapters(targets, bookName) || targets.empty())
		{
			summary.Append("The active book has no chapters.");
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
	KBSBookScope::ReleaseSearchedBook();	// the two are one fact - see gSearchedBookPath

	// All four AFTER Clear(), which puts them back to their Find/Change defaults. The kind is what
	// takes the check boxes off every row and narrows the column they stood in - a scan is a
	// report, not a work list.
	KBSResultModel::SetResultKind(KBSResultModel::kResultOverset);
	KBSResultModel::SetFromBook(fromBook);
	KBSResultModel::SetBookName(bookName);
	KBSResultModel::NoteRun();		// the panel's illustration changes once anything has been run

	// ----- the progress bar: one step per chapter -----
	// It used to be sized in STORIES, to weight a hundred-story chapter above a two-story one. Two
	// things ended that: the scan only ever moves the bar at chapter boundaries (ScanOneDocument
	// does not report from inside), so the weighting bought nothing but a smoother-looking crawl,
	// and story counts are no longer knowable up front - chapters are opened one at a time now.
	const int32 progressTotal = static_cast<int32>(targets.size()) * kKBSChapterProgressSpan;

	PMString progressTitle(fromBook ? "Scanning book for overset text..."
									: "Scanning for overset text...");
	progressTitle.SetTranslatable(kFalse);
	// showImmediate = kTrue: put the bar up at once rather than waiting out its internal delay, or
	// the one thing it is really there for - Cancel - is never on screen for a fast scan.
	RangeProgressBar progressBar(progressTitle, 0, progressTotal, kTrue, kTrue);
	// Asking a thread whether it is overset can make it compose, and a compose is entitled to raise
	// a bar of its own - which would sit on top of this one and take the Cancel button with it. The
	// search and the replace both say this; the scans did not.
	progressBar.DisableChildProgressBars(kTrue);

	int32 progressBase = 0;
	int32 progressReported = 0;

	bool cancelled = false;
	int32 rowTotal = 0;
	int32 offPage = 0;
	int32 chaptersWithHits = 0;

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
		const int32 rows = ScanOneDocument(chapterDocRef, targets[i].shortName, chapter, offPage);

		progressBase += kKBSChapterProgressSpan;
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		if (rows > 0)
		{
			KBSResultModel::AppendChapter(chapter);		// only chapters with findings go in
			rowTotal += rows;
			++chaptersWithHits;
		}

		// ***** Hand the chapter back. ***** The scan is over and its rows are plain data - UIDs and
		// text indices, which survive the document being closed. A jump reopens what it needs
		// through ReopenChapterDoc. Only chapters KBS opened are closed: ReleaseHeldDoc checks the
		// held list itself, so one the user already had open passes through untouched.
		KBSBookScope::ReleaseHeldDoc(chapterDocRef);
	}

	// ***** Ask ONE more time, outside the loop. *****
	// Asking only inside it misses a cancel pressed during the LAST chapter, because no further
	// round comes to hear it - and on a one-chapter book that means Cancel never works at all.
	// (The same fault was found and fixed in the search, the replace and the glyph scan.)
	if (!cancelled && progressBar.WasCancelled(kFalse))
		cancelled = true;

	if (cancelled)
	{
		// Throw the half-finished list away rather than leave a partial one looking complete, and
		// give the chapters back. The closes are scheduled, so it is safe from in here.
		KBSResultModel::Clear();
		KBSBookScope::ReleaseSearchedBook();	// closes the chapters AND forgets the book
		KBSResultTree::Rebuild();
		summary.Append("Scan cancelled.");
		KBSResultTree::ShowStatus(summary);
		return;
	}

	KBSResultTree::Rebuild();

	BuildSummary(rowTotal, chaptersWithHits, static_cast<int32>(targets.size()), fromBook,
		bookName, offPage, summary);
	KBSBookScope::AppendUnopenableNote(summary, unopenable);
	KBSResultTree::ShowStatus(summary);
}

bool KBSOversetScanEngine::IsScanning()
{
	return gScanning;
}

// End, KBSOversetScanEngine.cpp.
