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
#include "IFrameListComposer.h"	// RecomposeThruLastFrame - compose what is stale before asking
#include "ILayoutUIUtils.h"		// GetFrontDocument - the same way KBSSearchEngine resolves scope
#include "IParcelList.h"		// GetLastParcelKey / GetParcelFrameUID - was this thread placed at all?
#include "IStoryList.h"			// the document's stories
#include "ITableModel.h"		// const_iterator / GetGridID - one visit per cell thread
#include "ITextModel.h"			// QueryFrameList / QueryTextParcelList
#include "ITextParcelList.h"	// GetIsOverset / GetFirstOversetTextIndex - the whole answer
#include "ITextStoryThread.h"	// GetTextStart - a cell thread's first TextIndex
#include "ITextStoryThreadDict.h"		// QueryThread(gridID) - one dictionary per table
#include "ITextStoryThreadDictHier.h"	// NextUID - every dictionary in the story, flattened
#include "ITextUtils.h"			// IsOverset

// General includes:
#include "IDataBase.h"			// SaveRestoreModifiedState
#include "PMString.h"
#include "ParcelKey.h"			// ParcelKey::IsValid - the placed-parcel walk's stop condition
#include "PersistUtils.h"		// ::GetUIDRef
#include "ProgressBar.h"		// RangeProgressBar - the scan's progress + cancel
#include "TableTypes.h"			// GridID - the key a cell's thread is filed under
#include "TextID.h"				// kTextStoryBoss - the boss ITableModelList sits on
#include "Utils.h"

#include <algorithm>			// std::min - the preview stops at the end of the thread
#include <vector>
#include <utility>				// std::move - a finished chapter is handed to the model, not copied

// Project includes:
#include "KBSBookScope.h"		// the chapter list, and the windowless chapters it holds open
#include "KBSOversetScanEngine.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"		// Rebuild / ShowStatus - also what app.kfcStatus reads back
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

// How much of the overset text a row's preview READS. Overset runs to hundreds of characters - the
// official preflight reported 370 for one frame in the test document - and handing the whole range
// to BuildHitForRange would put all of it in the model and in app.kfcResults, when the row only
// ever draws one line.
//
// ***** IT BOUNDS THE READ, NOT WHAT IS DRAWN - AND IT IS NO LONGER THE SMALLER OF THE TWO. *****
// This said "60 is comfortably more than a row can show", which was true when it was written. Since
// 2026-08-10 SplitLineWithScanner budgets all three drawn segments together at kKBSMaxLineChars = 50
// and KBSCapMatchEnd cuts the match at that very number, so what a row shows is decided over there
// and this constant no longer decides anything about it. What it still does is keep the read short.
//
// The overflow's OWN length wins whenever it is shorter - see the preview range below.
const int32 kOversetPreviewChars = 60;

// How much of the progress bar one CHAPTER gets. Every chapter gets the same slice, because the run
// no longer knows how big a chapter is before it opens it: chapters are opened one at a time and
// closed again straight after, so there is no all-chapters-open moment in which to add up their
// story counts.
//
// Within a chapter the slice is divided by STORIES, which a document that IS open can be asked for
// for free (IStoryList keeps the count). The same shape and the same number the search uses -
// KBSSearchEngine's kKBSChapterProgressSpan, whose note explains the size: large enough that a
// 500-story chapter still gets whole steps apiece, small enough that a 100-chapter book stays far
// inside int32.
//
// ***** It was 1 until 2026-08-05, and that was the whole reason a one-document scan showed nothing.
// ***** With a span of one there was no room to move the bar inside a chapter, so a document-scope
// scan sat at 0% and finished - and the Cancel button, which needs the bar to have been moved to
// answer at all, never got the chance. The reasoning recorded here for the old value ("story counts
// are no longer knowable up front") does not survive comparison with the search, which counts them
// AFTER opening each chapter and has always divided its slice that way.
const int32 kKBSChapterProgressSpan = 10000;

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

/** Is ANY of this thread actually placed - does it hold at least one parcel that sits in a frame?

    ***** This is what tells "this cell overflowed" from "this cell never got the chance". *****
    ThreadOverset answers the same yes for both: a cell whose own text ran past its bottom edge, and
    a cell in a table that was pushed out of its frame entirely, where nothing was composed because
    there was nowhere to compose it. The second is not a finding of its own - the FRAME is the
    finding, and the official preflight says exactly that: measured 2026-08-05 on
    work/kbs-selftest/pushed-table.indd, InDesign reported two "Text Frame / Overset text" errors and
    not one word about the ten cells inside them, while this scan reported all twelve.

    The distinction is in the parcels. A cell that overflowed IS on the page - its first lines are
    drawn, so its parcel list holds a parcel with a real frame UID - whereas a pushed-out cell has a
    parcel list in which every parcel answers kInvalidUID.

    Written as the walk KBSOversetLocator's LocateInThread already runs, and for the same reason it
    runs it: that one climbs to the frame the "+" is drawn in, this one only asks whether there was
    one. (Kept here rather than exported from there because the two questions are different - a
    caller of this one does not want the geometry, and a caller of that one has already decided to
    point at something.) */
bool ThreadHasPlacedParcel(ITextModel* model, TextIndex pos)
{
	if (model == nil)
		return false;

	InterfacePtr<ITextParcelList> tpl(model->QueryTextParcelList(pos));
	if (tpl == nil)
		return false;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return false;

	// From the LAST parcel backwards: an overflowing thread has its overset parcels at the end, so
	// the placed one it is looking for is nearer that end than the start on the failing path, and on
	// the succeeding path the very first step answers.
	for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
	{
		if (pl->GetParcelFrameUID(k) != kInvalidUID)
			return true;
	}
	return false;
}

/** Collect every cell of every table in this story that is overset ON ITS OWN.

    ***** The tables are reached through the thread-dictionary hierarchy, not through
    ITableModelList. ***** Both work, and the SDK is explicit about which is which: the snippet that
    uses ITableModelList calls it "an older way" and points at this one -
    "See SnpIterTableUseDictHier::IterateAllTablesInDocument() for a better technique"
    (SnpIterTableStories.cpp:68-70, :151-154). The walk below is SnpIterTableUseDictHier.cpp:147-199.

    What that buys here is the nested table. ITextStoryThreadDictHier::NextUID FLATTENS the
    hierarchy (ITextStoryThreadDictHier.h:63-66), so a table anchored inside a CELL arrives in the
    same sequence as a top-level one - by contract, where the old route left it as something this
    plug-in had measured (2026-08-02, work/kbs-selftest/overset-shapes.indd) and had to trust. The
    "Table cell (121)" finding that matches the official preflight is one of those nested cells.

    ***** Still no recursion. ***** The sequence is already flat; walking a cell's own tables as
    well would visit those threads a second time and report every nested cell twice.

    @param out  where the places are collected - or nil for a caller that only wants to know WHETHER
                any cell overflowed (StoryHasAnyOverset). With nil the walk stops at the first one,
                since nothing further can change the answer.
    @param maxPlaces  stop once 'out' holds this many places - what is left of the run's whole-run
                ceiling. Ignored when out is nil (that caller stops at the first one anyway).
                ***** The bound belongs HERE, not on the rows built afterwards. ***** A ceiling that
                only counts ROWS lets this walk collect without bound first and throw the surplus
                away second, which is the opposite of what a safety ceiling is for: the document it
                exists for - a book of overset table cells - is exactly the one where the collecting
                itself is the cost.
                ***** THE GLYPH SCAN DOES NOT DO THIS, AND THIS LINE SAID IT DID UNTIL 2026-08-11.
                ***** That one collects every notdef of a story into one vector and applies its
                ceiling to the ROWS afterwards (KBSGlyphScanEngine's ScanOneDocument), so a story
                whose every character is a box is held whole before the surplus is dropped. It is
                bounded per story, and its walk has to visit every glyph anyway to know which ones
                are boxes - so the shape has never been worth changing. It is simply not this one.
    @return whether any overset cell was found at all. */
bool CollectOversetCells(const UIDRef& storyRef, ITextModel* model, std::vector<OversetPlace>* out,
	size_t maxPlaces)
{
	bool found = false;

	// Aggregated on kTextStoryBoss, and it owns one ITextStoryThreadDict per table
	// (SnpIterTableUseDictHier.cpp:154-163).
	InterfacePtr<ITextStoryThreadDictHier> dictHier(model, UseDefaultIID());
	if (dictHier == nil)
		return false;

	IDataBase* const db = ::GetDataBase(dictHier);

	// Starts at the story's own UID - kTextStoryBoss carries a dictionary of its own, the primary
	// story thread - which then falls out below for having no ITableModel.
	for (UID nextUID = ::GetUIDRef(dictHier).GetUID();
		 nextUID != kInvalidUID;
		 nextUID = dictHier->NextUID(nextUID))
	{
		InterfacePtr<ITextStoryThreadDict> dict(db, nextUID, UseDefaultIID());
		if (dict == nil)
			continue;

		// Is this dictionary a TABLE's? kTableModelBoss carries the dictionary and an ITableModel
		// together; kTextStoryBoss carries the dictionary without one, and that is how the primary
		// story thread is told apart (SnpIterTableUseDictHier.cpp:219-225).
		InterfacePtr<ITableModel> table(dict, UseDefaultIID());
		if (table == nil)
			continue;

		// ***** ONE VISIT PER CELL THREAD - and it is the nil test below that makes it so, not the
		// iterator. ***** begin()/end() traverse GRID ADDRESSES (ITableModel.h:391-406) and every
		// element of the grid has a GridID of its own (:188-196), while a cell's thread is filed
		// under its ANCHOR's GridID alone - QueryThread being a keyed lookup, "Private unique key
		// that identifies which ITextStoryThread to instantiate" (ITextStoryThreadDict.h:78-83). So
		// an element that is not an anchor answers nil and falls out, and a merged cell is reported
		// once however many squares it covers. The official walk is these same three lines without
		// the test (SnpIterTableUseDictHier.cpp:247-262).
		// (This note read "the iterator visits ANCHOR cells" until 2026-08-11. Nothing in the
		//  headers says that, and IsAnchor() exists precisely because an address may not be one.)
		for (ITableModel::const_iterator it(table->begin()), last(table->end()); it != last; ++it)
		{
			const GridID gridID = table->GetGridID(*it);

			InterfacePtr<ITextStoryThread> thread(dict->QueryThread(gridID));
			if (thread == nil)
				continue;

			int32 span = 0;
			const TextIndex cellPos = thread->GetTextStart(&span);

			TextIndex start = 0;
			int32 count = 0;
			if (!ThreadOverset(model, cellPos, start, count))
				continue;

			// ***** Did this cell overflow, or was it never placed at all? ***** A table pushed out of
			// its frame leaves every one of its cells composing nothing, and ThreadOverset says yes to
			// all of them - which put ten rows on the panel for a document the official preflight
			// reports two frames for (2026-08-05, work/kbs-selftest/pushed-table.indd). The frame IS
			// the finding there, and it is already collected by the caller's frame pass; a row per
			// pushed-out cell only buries it. See ThreadHasPlacedParcel.
			if (!ThreadHasPlacedParcel(model, cellPos))
				continue;

			// A caller that only asked WHETHER anything overflowed has its answer now, and the
			// remaining tables cannot change it.
			if (out == nil)
				return true;

			// Full. Asked BEFORE the push, so the list never goes over - the caller reads its size
			// to decide whether the walk was cut short, and a list one entry past the bound would
			// make that test read a size it never set.
			if (out->size() >= maxPlaces)
				return found;

			OversetPlace place;
			place.storyRef = storyRef;
			place.start = start;
			place.count = count;
			place.inCell = true;
			out->push_back(place);
			found = true;
		}
	}

	return found;
}

//----------------------------------------------------------------------------------------
// ***** ITextParcelList::GetParcelContainsOversetContent WAS MEASURED AGAINST THE WALK ABOVE AND
// IS NOT USED. *****
//
// It answers "does this parcel hold complex content - Tables and Footnotes - that is overset?" in
// one call (ITextParcelList.h:705-713), and the SDK declares it without ever calling it: one line,
// no use anywhere. A probe build ran both over every story of five documents, 2026-08-10:
//
//     overset-shapes      4 stories   cells 2   parcels 2   agree
//     pushed-table        2 stories   cells 0   parcels 0   agree
//     overset-bigtable    1 story     cells 1   parcels 1   agree
//     many-overset      500 stories   cells 0   parcels 0   agree
//     footnote-overset    1 story     cells 0   parcels 1   DIFFER
//
// ***** The one disagreement is the interesting one, and it changes no answer. ***** The API does
// see an overflowing FOOTNOTE, which the walk above cannot - that one only visits dictionaries
// carrying an ITableModel, so a footnote thread is passed over. But a footnote that does not fit
// makes the FRAME LIST overset: measured with 4,183 characters of footnote under 64 characters of
// body, the last frame answered overflows=true and this scan reported the place. So question (1)
// of StoryHasAnyOverset - ITextUtils::IsOverset - has already said yes before the cell walk is
// reached, exactly as it has for a pushed-out table.
//
// Nor is it faster where it would matter. Microseconds either way, and the ordering flips with the
// document: 500 stories with no table in them cost 70 us for the walk against 1,540 us for the
// parcels (the walk leaves at once when the hierarchy holds no ITableModel, while every parcel has
// to be asked), and one story holding a table of 861 overset cells cost 53 us for the walk against
// 24 us for the parcels. Against a scan that takes 700 ms, neither is visible.
//----------------------------------------------------------------------------------------

/** Scan one document and turn every overset place into a result row.

    @param outOffPage  incremented for a place with NOTHING PLACED ANYWHERE to point at - no page,
                       no spread, no ancestor frame carrying the "+". Those are counted but not
                       listed: a row that cannot be jumped to is worse than a number. Master pages
                       and the pasteboard used to land here too and no longer do (2026-08-11) - see
                       the test at the head of the row loop.
    @param maxRows       stop once this chapter has collected this many PLACES - what is left of the
                         run's whole-run ceiling (KBSResultModel::kKBSCollectHitLimit). Every row
                         comes from a place, so bounding the places bounds the rows as well, and it
                         bounds the WALK that finds them, which counting rows alone did not.
    @param outHitCeiling set when the walk stopped because of maxRows rather than because the
                         document ran out, so the summary can say the list is not the whole story.
    @param progressBar   the run's bar, or nil. progressBase is where this chapter's slice starts and
                         chapterSpan is how wide it is; the slice is divided between this document's
                         stories, so the bar moves WITHIN a chapter and a one-document scan is not a
                         motionless 0%.
    @param outCancelled  set when the user pressed Cancel while this chapter was being walked, at
                         which point the scan stops where it stands and the caller throws the whole
                         list away.

    ***** THE CANCEL IS ASKED FROM INSIDE THE CHAPTER, WHICH THE SEARCH CANNOT DO. ***** WasCancelled
    pumps the event queue, and the search's walk holds the text walker's critical section while it
    runs - so there it can only be asked between chapters (see KBSSearchEngine's CollectHitsInDoc).
    This scan runs no walker: it reads IStoryList itself, holds no critical section, and processes no
    command, so the question is safe here. Asked once per story, which is far less often than the
    search moves its own bar.

    @return how many ROWS the chapter got. */
int32 ScanOneDocument(const UIDRef& docRef, const PMString& chapterName,
	KBSResultModel::Chapter& outChapter, int32& outOffPage,
	int32 maxRows, bool& outHitCeiling,
	RangeProgressBar* progressBar, int32 progressBase, int32 chapterSpan,
	int32& ioProgressReported, bool& outCancelled)
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

	// The bound the walk below stops at. Negative or zero cannot arrive - the caller breaks out of
	// its chapter loop when nothing is left - but it is clamped rather than trusted, because the
	// value it would take on is size_t and an unsigned cast of a negative number is enormous.
	const size_t maxPlaces = (maxRows > 0) ? static_cast<size_t>(maxRows) : 0;

	// User accessible only - the population the search and the glyph scan walk. IStoryList.h:38-42
	// states internal stories are not subject to find/change, so reporting them would name places
	// the rest of the panel never looks at.
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

		// ***** (0) Compose what is out of date BEFORE asking. *****
		// Overset is a COMPOSITION result - both answers below are the composer's last word - so a
		// story edited since it last composed can report overflow the user has already fixed, or
		// stay silent about overflow that has just appeared. Official route:
		// SnpInspectTextModel.cpp:724-733 (damaged index, then RecomposeThruLastFrame); the same
		// three lines KBSJump's RecomposeIfDamaged runs before reading this story's wax (cited by
		// line number until 2026-08-11, by which time they had moved). Composing the frame
		// list settles the tables in it too, so the cell pass below reads a settled story as well.
		InterfacePtr<IFrameList> frameList(model->QueryFrameList());
		if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
		{
			InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}

		// (1) the thread the frames carry - the red "+".
		if (places.size() < maxPlaces
			&& frameList != nil && Utils<ITextUtils>()->IsOverset(frameList))
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
		CollectOversetCells(storyRef, model, &places, maxPlaces);

		// ***** Full: the remaining stories are not walked either. ***** Walking them would cost the
		// time of a full scan - every story, every table, every cell of every table - to produce
		// places that are thrown away unread. The glyph scan stops at the same point for the same
		// reason (KBSGlyphScanEngine's ScanOneDocument).
		//
		// It is stated as a ceiling here rather than left for the row loop to notice, because the row
		// loop cannot: a place that lands on no page builds no row, so the rows can come back short
		// of the bound on a walk that was cut short all the same.
		if (places.size() >= maxPlaces)
		{
			outHitCeiling = true;
			break;
		}

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

	// Cancelled: none of the work below is worth doing - the caller throws the whole list away, rows
	// and all. Returned from HERE, above the hit cache, so there is nothing yet to hand back.
	if (outCancelled)
		return 0;

	// Every place gets its turn from here on - no second ceiling. The walk above already stopped at
	// maxPlaces, and a row can only come from a place, so the rows cannot exceed the bound however
	// this loop goes. That also lets every place be ASKED about its page: the row ceiling used to sit
	// here and break out of the loop, which left the places past it uncounted in outOffPage as well -
	// so a truncated scan under-reported how many findings it could not point at.
	KBSSearchEngine::HitCache* cache = KBSSearchEngine::NewHitCache();
	for (size_t p = 0; p < places.size(); ++p)
	{
		KBSResultModel::Hit hit;
		hit.checked = false;		// a scan is a report, not a work list: no row is selectable

		// ***** NEVER PAST THE END OF THE THREAD. ***** The overflow's own length is already in hand -
		// ThreadOverset measured it, and the row states it a few lines below as "Table cell (3)" - so
		// a thread that overflowed by three characters has exactly three to preview. Reading a flat
		// sixty from there walks out of the thread and into whatever the model holds next: the
		// following cell, a footnote, the body. This is ONE ITextModel, so those characters read
		// perfectly well and land in the row and in app.kfcResults as though the cell had overflowed
		// by sixty (found 2026-08-10, the block 10 re-check; the range fed the drawn text only - the
		// stale test short-circuits on an overset row, MatchTextIsLiveText, so nothing was misjudged
		// by it).
		const TextIndex previewEnd = places[p].start
			+ std::min(places[p].count, kOversetPreviewChars);
		KBSSearchEngine::BuildHitForRange(docRef, places[p].storyRef,
			places[p].start, previewEnd, cache, hit);

		// ***** IS THERE ANYWHERE AT ALL TO POINT AT? ***** Asked of the page STRING, which BuildHit
		// leaves empty only when the locator found nothing placed - not of pageIndex.
		//
		// ***** IT USED TO ASK pageIndex < 0, AND THAT DROPPED MASTER PAGES. ***** The note here said
		// the index "is non-empty for exactly the case being excluded" - the pasteboard - and it was
		// not: IPageList::GetPageIndex counts pages within the pub and a MASTER page is not one of
		// them, so it answers negative for those too (KESCM measured that and wrote it into
		// KESCMStoryList.cpp:304-307). Measured here 2026-08-11 on work/kbs-selftest/
		// masterpage-overset.indd - two frames overset, one on page 1 and one on master A:
		//    Find Overset  "1 overset place.  1 not on a page."   master frame NOT listed
		//    the search    the same frame's hit listed as "PA"
		//    the glyph scan a notdef box in that frame listed as "PA"
		// - three parts of one plug-in answering differently about one frame, which is the fault
		// this file's own history calls "two scans of one plug-in disagreeing" twice over.
		//
		// So the filter is gone (user's call, 2026-08-11) and a place is listed wherever it sits: on
		// a page, on a master page, out on the pasteboard. It is what the shared hit builder already
		// argues for in so many words - "a match out on the pasteboard should say where it is rather
		// than drop out of the list" (KBSSearchEngine.cpp, GetFramePageString) - and what the other
		// two commands have always done. It reports MORE than InDesign's own preflight, which was
		// measured on the same document the same day and named only the page-1 frame; the glyph scan
		// has taken that line since it was written.
		//
		// What is still counted rather than listed: a place with nothing placed ANYWHERE - no page,
		// no spread, no ancestor frame - which no row could point at. Rare by construction, since
		// the locator climbs out of a pushed-out table to the frame that holds it.
		if (hit.pageString.IsEmpty())
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

	// Page order, and the "P<page>(<n>) overset" locator on every row - the same pass the search runs.
	KBSSearchEngine::FinalizeHits(outChapter.hits);
	return static_cast<int32>(outChapter.hits.size());
}

/** The status line: how many places, over how many chapters, and how many could not be pointed at.

    The two numbers are separate questions and are NOT added up: the first is how many rows are in
    the list, the second is how many places were left out of it. A total would appear nowhere on
    screen and could not be checked against anything. */
void BuildSummary(int32 places, int32 chaptersWithHits, int32 chapterTotal, bool fromBook,
	const PMString& bookName, int32 offPage, bool truncated, PMString& out)
{
	out.Clear();
	out.SetTranslatable(kFalse);

	if (places == 0)
	{
		// ***** Which sentence depends on whether the one below it is coming. ***** "No overset
		// text." followed by "1 place has nowhere to point at." is the panel contradicting itself in
		// two sentences: the scan DID find overflow, it just had no row to offer for it.
		out.Append(offPage > 0 ? "No overset text that can be shown." : "No overset text.");
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

	// Places with nothing placed ANYWHERE - no page, no spread, no ancestor frame - so no row could
	// be clicked. It read "N not on a page." while master pages and the pasteboard were dropped here
	// as well (until 2026-08-11); both are listed now, so this counts only what genuinely cannot be
	// pointed at, and the sentence had to stop saying "page".
	if (offPage > 0)
	{
		out.Append("  ");
		out.AppendNumber(offPage);
		out.Append(offPage == 1 ? " place has nowhere to point at." : " places have nowhere to point at.");
	}

	// ***** THE LIST IS NOT THE WHOLE STORY - SAY SO. ***** A scan reads as "here is every place
	// text did not fit", so one that stopped early has to admit it. The search says "narrow your
	// search" here; a scan has no query to narrow, so it only states the fact.
	if (truncated)
	{
		out.Append("  Stopped at the ");
		out.AppendNumber(KBSResultModel::kKBSCollectHitLimit);
		out.Append(" safety limit.");
	}
	// A separate number and a separate sentence: rows past the display cap ARE collected and DO
	// reach Save Results, they are simply not drawn.
	if (places > KBSResultModel::kKBSDisplayHitLimit)
	{
		out.Append("  Showing first ");
		out.AppendNumber(KBSResultModel::kKBSDisplayHitLimit);
		out.Append(" in the panel.");
	}
}

}	// anonymous namespace

// The shared answer to "does this story hold overset text right now?" - see KBSOversetScanEngine.h
// for why the glyph scan asks this rather than the frame list's REMEMBERED one. BOTH of the scan's
// own questions are asked, in the scan's own order: the thread the frames carry, then the cells that
// overflow on their own.
bool KBSOversetScanEngine::StoryHasAnyOverset(ITextModel* model)
{
	if (model == nil)
		return false;

	// ***** (1) The thread the frames carry - the red "+". *****
	//
	// ITextParcelList ALONE says yes about a story that fits: it counts the thread's final CR as an
	// overset parcel, which is why ThreadOverset has to correct the COUNT (see its comment). The
	// frame pass of the scan itself never sees that, because it asks ITextUtils::IsOverset first and
	// only then reads the detail - so this asks in the same order.
	//
	// Measured 2026-08-04: without this gate a document with nothing overset reported overset here,
	// which is precisely the false "Text in overset cannot be checked." this function exists to
	// remove.
	InterfacePtr<IFrameList> frameList(model->QueryFrameList());
	if (frameList != nil && Utils<ITextUtils>()->IsOverset(frameList))
	{
		TextIndex whereItStarts = kInvalidTextIndex;
		int32 howMuch = 0;
		if (ThreadOverset(model, 0, whereItStarts, howMuch))
			return true;
	}

	// ***** (2) Cells overflowing on their own - the red dot, nested tables included. *****
	//
	// The glyph scan cannot read what did not compose WHEREVER it sits: a cell overflowing on its own
	// hides missing glyphs exactly as a pushed-out frame does. Asking only (1) left the scan silent
	// about a document Find Overset had findings for - the same two-scans-disagree fault that the
	// main-thread half of this function was written to remove, one level further down (2026-08-04).
	//
	// ***** A pushed-out cell is not counted here either, and that changes nothing. ***** Since
	// 2026-08-05 CollectOversetCells passes over a cell that was never placed (ThreadHasPlacedParcel),
	// and this function asks it the same way the scan does - on purpose, so the two cannot drift. It
	// costs no answer: a cell can only be pushed out by its table being pushed out, which means the
	// frame holding that table is overset, which (1) above has already said yes to. Nothing is lost
	// from the glyph scan's "part of this document could not be looked at" either, for the same
	// reason - that text is inside the overflow (1) reported.
	//
	// ***** A FOOTNOTE that overflowed is not counted here either, and it costs no answer for the
	// same reason (measured 2026-08-10). ***** CollectOversetCells only visits dictionaries that
	// carry an ITableModel, so a footnote thread never reaches it at all. But a footnote that does
	// not fit makes the FRAME LIST overset: with 4,183 characters of footnote under 64 characters of
	// body the last frame answered overflows=true and the scan reported the place, so (1) above has
	// already said yes. The probe that established this ran
	// ITextParcelList::GetParcelContainsOversetContent beside the cell walk - that API DOES see the
	// footnote, and the note over ScanOneDocument records why it is still not used.
	//
	// nil = collect nothing and stop at the first one; only the yes or no is wanted here. The place
	// bound goes unread on that path for the same reason - there is no list to bound.
	return CollectOversetCells(::GetUIDRef(model), model, nil, 0 /*maxPlaces: unused with nil*/);
}

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

	// ***** IS THERE ANYTHING TO SCAN? ASKED BEFORE THE MODEL IS TOUCHED. *****
	// A run that is turned away has to leave the panel exactly as it found it. These two tests used
	// to sit BELOW the Clear() just under them, so "Book Scope is on, but no book is open." also
	// threw away whatever the panel was showing (found 2026-08-03 in the defect audit).
	if (fromBook && !KBSBookScope::HasTargetBook())
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

	// The KIND, the SCOPE, the BOOK NAME and the RUN FLAG - every one of them AFTER Clear(), which
	// puts them back to their Find/Change defaults. The kind is what takes the check boxes off every
	// row and narrows the column they stood in - a scan is a report, not a work list.
	//
	// Named rather than counted, though the count here happens to be right: the glyph scan's copy of
	// this sentence said "All three" for nine days after a fourth line joined it (2026-08-11).
	KBSResultModel::SetResultKind(KBSResultModel::kResultOverset);
	KBSResultModel::SetFromBook(fromBook);
	KBSResultModel::SetBookName(bookName);
	KBSResultModel::NoteRun();		// the panel's illustration changes once anything has been run

	// ----- the progress bar: one equal slice per chapter, divided by stories inside -----
	// Every chapter gets the same slice because a chapter's size is not knowable before it is opened,
	// and ScanOneDocument then divides that slice between the stories it finds - so the bar moves
	// through a single document too, which is what gives the Cancel button a chance to be pressed and
	// answered. See kKBSChapterProgressSpan.
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
	// Set when the run stopped collecting at the whole-run ceiling rather than at the end of the
	// book. NOT the same as cancelled: what was collected is kept and reported.
	bool collectionTruncated = false;
	int32 rowTotal = 0;
	int32 offPage = 0;
	int32 chaptersWithHits = 0;
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
		const int32 rows = ScanOneDocument(chapterDocRef, targets[i].shortName, chapter, offPage,
			remaining, collectionTruncated,
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
		// came out modified, or the close was refused). A chapter left behind by a failure is
		// WINDOWLESS: nothing on screen shows it, nothing the user can do closes it, and it holds its
		// .indd locked for the rest of the session - so it is worth a line in the summary. (The same
		// shape as the replace's ShowChapterWindow, whose return value was being discarded until
		// 2026-08-05.)
		//
		// THREE questions, not two (2026-08-08): IsHeldDoc before, IsDocStillOpen after. The pair
		// alone cannot tell "the user closed it under the run" - their own doing, with nothing left
		// behind - from the real failures, and so counted it as "left open with no window" about a
		// chapter that is not open at all.
		const bool wasOurs = KBSBookScope::IsHeldDoc(chapterDocRef);
		if (!KBSBookScope::ReleaseHeldDoc(chapterDocRef, true /*close now*/)
			&& wasOurs && KBSBookScope::IsDocStillOpen(chapterDocRef))
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
			++chaptersWithHits;
		}
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
		KBSSearchEngine::ForgetSearchedFindFormat();	// ...and the format, as every Clear() does
		KBSResultTree::Rebuild();
		summary.Append("Scan cancelled.");
		KBSResultTree::ShowStatus(summary);
		return;
	}

	KBSResultTree::Rebuild();

	BuildSummary(rowTotal, chaptersWithHits, static_cast<int32>(targets.size()), fromBook,
		bookName, offPage, collectionTruncated, summary);
	// The two ends of the run, in the order they happened: what could not be opened, then what could
	// not be closed again. Both append nothing in the ordinary case.
	KBSBookScope::AppendUnopenableNote(summary, unopenable);
	KBSBookScope::AppendUnclosedNote(summary, unclosed);
	// The saved report's heading - recorded here, where the results were committed, so a status
	// line overwritten later (a jump's message, a panel toggle) cannot become the report's summary.
	KBSResultModel::NoteRunSummary(summary);
	KBSResultTree::ShowStatus(summary);
}

bool KBSOversetScanEngine::IsScanning()
{
	return gScanning;
}

// End, KBSOversetScanEngine.cpp.
