//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KEPT TIERS - THE MODEL'S SIDE (docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md section 3-1). A search
//  that goes through keeps the list it replaces as a TIER: the whole list (KFCResultModel::ModelSnapshot), its result set
//  (the tier's name - the UI half's grey copy of it carries the same number) and the book it was searched in; its query
//  copy rides in the list's header (ListHeader::query). The order and the cap are kept HERE, and only here - the UI half
//  reads them. Clear Results, a query run and Change All in Book (No List) that go through let every tier go (the UI half
//  asks); a closing document leaves its chapters empty in every tier, as a kept query run's list does
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

	/** AT A SEARCH'S COMMIT POINT, BEFORE THE LIST IS THROWN AWAY (KFCSearchEngine::SearchBook): the list kept as the newest
	    tier - when it has a row, or a search made it (its query was copied: a search that found nothing is kept too) - with
	    its result set and the book it was searched in. Past kMaxTiers the oldest goes. */
	void ParkCurrent();

	/** The kept tiers' result sets, oldest first. */
	void GetTierSets(std::vector<uint32>& out);

	/** Every tier let go (fresh storage, not Clear()). */
	void DropAll();

	/** A CLOSING DOCUMENT: in every tier but a book's, its chapters emptied (KFCResultModel::EmptyChapter) - found by their
	    file as every door of KFC finds a chapter's document (KFCBookScope::FindOpenChapterDoc), while the document is still
	    open (the close responder's kBeforeCloseDoc). A chapter keeps its place, so the rows of a write kept for that tier
	    stay numbered right. A BOOK'S tier keeps its chapters - book results survive a chapter closing, as the live book
	    list's do (KFC opens and hands back a book's chapters on every book search). */
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

	/** THE TIER SWITCH (the spec's section 3-2): the kept tier `set` made the live list - its rows back as they were kept
	    (no search), as its own result set again (the writes kept for it are followed again - KFCUndoFollow), its query
	    stated back into Find/Change (KFCQueryCopier::Apply) and checked three times: field by field (Matches), by the walk
	    signature it was searched with, and in the words the person reads - the tier's name, asked of the UI half
	    (IKFCUIServices::QueryReadsAsTier). The live list before it goes in as the newest tier (when it is one to keep).
	    REFUSED - nothing changes: a run of ours under way; no such tier; no query kept with it; a book's tier whose book is
	    not open; or the query not reading back as the copy - then the live list, its result set and the query Find/Change
	    held before are all put back (section 3-3-1: all or nothing). False = refused; outSummary says why. True = switched
	    (outSummary empty: the UI half names the tier it switched to). */
	bool SwitchTo(uint32 set, PMString& outSummary);

	/** The controlled shutdown: the tiers let go - their lists hold PMStrings, and their query copies hold attribute lists
	    (database work - KFCSearchEngine::ShutdownCleanup's reason). */
	void ShutdownCleanup();
}

#endif // __KFCTierStore_h__

// End, KFCTierStore.h.
