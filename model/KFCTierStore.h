//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KEPT TIERS - THE MODEL'S SIDE (docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md section 3-1). A search
//  that goes through keeps the list it replaces as a TIER: the whole list (KFCResultModel::ModelSnapshot), its result set
//  (the tier's name - the UI half's grey copy of it carries the same number) and the book it was searched in; its query
//  copy rides in the list's header (ListHeader::query). The order (search order - v2: no tier moves when one is switched
//  to) and the cap are kept HERE, and only here - the UI half reads them. Clear Results, a query run and Change All in
//  Book (No List) that go through let every tier go (the UI half asks), and a tier heading's Clear These Results one
//  (v2 R9); a closing document leaves its chapters empty in every tier, as a kept query run's list does
//  (KFCUndoFollow::ForgetDocument). The main thread only.
//
//========================================================================================

#ifndef __KFCTierStore_h__
#define __KFCTierStore_h__

#include "KFCResultModel.h"		// ModelSnapshot, RowStep

#include <vector>

class IDataBase;

namespace KFCTierStore
{
	/** The most tiers kept - past it the oldest goes. The one number both halves read (KFCModelTypes.h). */
	enum { kMaxTiers = KFCResultModel::kMaxKeptTiers };

	/** AT A SEARCH'S COMMIT POINT, BEFORE THE LIST IS THROWN AWAY (KFCSearchEngine::SearchBook): the list kept as a tier -
	    when it has a row, or a search made it (its query was copied: a search that found nothing is kept too) - with its
	    result set and the book it was searched in, at its place in SEARCH ORDER (its result set's). Past kMaxTiers the
	    oldest goes. */
	void ParkCurrent();

	/** The kept tiers' result sets, in search order - the oldest first. */
	void GetTierSets(std::vector<uint32>& out);

	/** KEPT OR NOT - the one answer, asked of the live list: is it a tier - a list with a row, or one a search made (its
	    query was copied: a search that found nothing is a tier too)? A query run's list or Change All's is neither, nor a
	    list the tiers were let go over (DropAll). Asked by a search's commit point (ParkCurrent), by the switch (the list
	    it puts aside) and by the close responder (a list whose last document closed keeps its heading while it is one -
	    the author, 10-14 night). The stacked results' ROW-41, the model's side of it. */
	bool LiveListIsKeepable();

	/** Every tier let go (fresh storage, not Clear()) - and the live list's query copy with them, so a list the tiers were
	    let go over is no tier-to-be (a list that found nothing, which Clear Results leaves as it is, became a nameless
	    "Results (0/0)" tier at the next search - the author's live test, 10-14). */
	void DropAll();

	/** ONE TIER LET GO (a tier heading's Clear These Results - v2 R9): a kept tier leaves the store; the live list's set
	    drops the live list as Clear Results drops it (KFCSearchEngine::DropResults) - the kept tiers stay. False = nothing
	    let go: a run of ours under way, or no tier of that set. */
	bool DropTier(uint32 set);

	/** A CLOSING DOCUMENT: in every tier but a book's, its chapters emptied (KFCResultModel::EmptyChapter) - found by their
	    file as every door of KFC finds a chapter's document (KFCBookScope::FindOpenChapterDoc), while the document is still
	    open (the close responder's kBeforeCloseDoc). A chapter keeps its place, so the rows of a write kept for that tier
	    stay numbered right. A BOOK'S tier keeps its chapters - book results survive a chapter closing, as the live book
	    list's do (KFC opens and hands back a book's chapters on every book search). Each chapter emptied is told to the
	    panel (KFCNotifyKeptChapterGone): the tier's grey copy lets that document's rows go and its heading reads the
	    counts left - the tier stays, its heading with it (the author, 10-14 night: "only the closed document's part
	    goes"). */
	void ForgetDocument(IDataBase* db);

	/** The chapters of kept tier `set`'s list that are the document `db` - by file first, as ForgetDocument finds them (the
	    document is still open at the close signal). Appended to `out`; nothing when there is no such tier. For the Undo
	    follow: a write of rows kept for that tier names its rows in the tier's list (KFCUndoFollow's closing chapter). */
	void ChaptersOfDoc(uint32 set, IDataBase* db, std::vector<int32>& out);

	/** Is `set` a kept tier's result set? (KFCUndoFollow: the writes kept for it are not let go.) */
	bool HoldsSet(uint32 set);

	/** The layout generation the kept tier's rows are numbered in - 0 when there is no such tier. */
	uint32 LayoutOf(uint32 set);

	/** An Undo or a Redo of a write made while that tier was the live list, followed while it is kept: its rows put onto the
	    kept copy (KFCResultModel::ApplyRowStepTo). False = no such tier. */
	bool ApplyRowStep(uint32 set, const KFCResultModel::RowStep& step, bool after);

	/** THE TIER SWITCH (docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md section 3-1): the kept tier `set`
	    made the live list - its rows back as they were kept (no search), as its own result set again (the writes kept for
	    it are followed again - KFCUndoFollow), its Find Format remembered for the replace's door (RememberFindFormatOf). The
	    list before it goes into the store at its place in search order (when it is one to keep) - no tier moves.
	    FIND/CHANGE IS NOT TOUCHED (v2 R1): a row operation states this list's query only when Find/Change is on another
	    one, and puts it back after (KFCQueryScope). REFUSED - nothing changes: a run of ours under way; no such tier; a
	    book's tier whose book is not open. False = refused; outSummary says why. True = switched (outSummary empty: the UI
	    half names the tier it switched to). */
	bool SwitchTo(uint32 set, PMString& outSummary);

	/** The controlled shutdown: the tiers let go - their lists hold PMStrings, and their query copies hold attribute lists
	    (database work - KFCSearchEngine::ShutdownCleanup's reason). */
	void ShutdownCleanup();
}

#endif // __KFCTierStore_h__

// End, KFCTierStore.h.
