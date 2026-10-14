//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The kept tiers, the model's side - see KFCTierStore.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include <utility>			// std::move - a kept list is handed over, not copied

// Project includes:
#include "KFCTierStore.h"
#include "KFCBookScope.h"	// GetSearchedBookPath - the book a list was searched in; FindOpenChapterDoc - a chapter, by its file
#include "KFCDiag.h"		// the TIER lines - test builds only
#include "KFCRowFoci.h"	// a tier's rows keep their text foci while it is grey (HandOver / TakeBack)
#include "KFCRunGuard.h"	// no switch while a run of ours is under way
#include "KFCSearchEngine.h"	// RememberFindFormatOf - the switched list's own Find Format, for the replace's door

namespace
{
	/** ONE KEPT TIER: the whole list as it was kept, its result set, and the book it was searched in. */
	struct TierRecord
	{
		KFCResultModel::ModelSnapshot	list;
		uint32							set;
		PMString						bookPath;	// empty = not a book's list
		KFCRowFoci::HeldPtr				foci;		// its rows' text foci, still moved by InDesign while it is grey (shared:
													// a copy of the record shares them - the last holder lets them go)
		TierRecord() : set(0) {}
	};

	std::vector<TierRecord> gTiers;		// in search order - the oldest first (AddInOrder)

	TierRecord* TierOf(uint32 set)
	{
		for (size_t i = 0; i < gTiers.size(); ++i)
			if (gTiers[i].set == set)
				return &gTiers[i];
		return nil;
	}

	// KEPT OR NOT - the one answer, asked of the live list by a search's commit point (ParkCurrent) and by the switch (the
	// list it puts aside) - the stacked results' ROW-41, the model's side of it: a list with a row, or one a search made (its
	// query was copied - a search that found nothing is a tier too). A query run's list or Change All's is neither, nor a
	// list the tiers were let go over (DropAll).
	bool LiveListIsKeepable()
	{
		return KFCResultModel::GetDisplayChapterCount() > 0 || KFCResultModel::GetQueryCopy() != nullptr;
	}

	// The live list as a tier record - its rows, its result set, its book.
	void TakeLive(TierRecord& out)
	{
		KFCResultModel::TakeModelSnapshot(out.list);
		out.set = KFCResultModel::GetResultSetId();
		out.bookPath = PMString();
		out.bookPath.SetTranslatable(kFalse);
		(void)KFCBookScope::GetSearchedBookPath(out.bookPath);
		// ITS ROWS' TEXT FOCI GO WITH IT (the author's call, 10-14 night): handed over, not let go, so they go on following
		// the edits made while the list is grey - another tier's replace among them (KFCRowFoci::HandOver).
		out.foci = KFCRowFoci::HandOver();
	}

	// A TIER PUT INTO THE STORE IN SEARCH ORDER - by its result set, which every list made anew numbers upward
	// (KFCResultModel::GetResultSetId) and a switch keeps (ReturnToResultSet) - so a list switched to and away again goes
	// back to the place it had: the tiers stay where they are (v2 R4 - the author: "the same position"). Past the cap the
	// oldest go - from the front, where they stand.
	void AddInOrder(TierRecord&& tier)
	{
		size_t at = 0;
		while (at < gTiers.size() && gTiers[at].set < tier.set)
			++at;
		gTiers.insert(gTiers.begin() + static_cast<std::ptrdiff_t>(at), std::move(tier));
		while (gTiers.size() > static_cast<size_t>(KFCTierStore::kMaxTiers))
			gTiers.erase(gTiers.begin());
	}
}	// anonymous namespace

void KFCTierStore::ParkCurrent()
{
	// (A query run's list or Change All's goes with the commit point as it always has.)
	if (!LiveListIsKeepable())
	{
		KFC_DIAG_LOG("TIER park skipped (no row, no search) set=%u", KFCResultModel::GetResultSetId());
		return;
	}
	TierRecord kept;
	TakeLive(kept);
	AddInOrder(std::move(kept));
	KFC_DIAG_LOG("TIER park set=%u tiers=%d", KFCResultModel::GetResultSetId(), static_cast<int>(gTiers.size()));
}

void KFCTierStore::GetTierSets(std::vector<uint32>& out)
{
	out.clear();
	for (size_t i = 0; i < gTiers.size(); ++i)
		out.push_back(gTiers[i].set);
}

void KFCTierStore::DropAll()
{
	KFC_DIAG_LOG("TIER drop all tiers=%d", static_cast<int>(gTiers.size()));
	// Fresh storage, not clear(): the lists and their query copies go with it.
	gTiers = std::vector<TierRecord>();
	// ...AND THE LIVE LIST IS NO TIER-TO-BE ANY MORE: its query copy goes too. Clear Results leaves a list that found nothing
	// as it is (KFCActionComponent's Clear Results - only the tiers go), and with its copy kept it was what the next search
	// put aside as a tier - one with no name, since the tiers' names went with the tiers ("Results (0/0)", the author's
	// live test, 10-14). A list made anew - a Run's, Change All's - has no copy to lose.
	KFCResultModel::SetQueryCopy(nullptr);
}

bool KFCTierStore::DropTier(uint32 set)
{
	if (KFCRunGuard::IsAnyRunning() || set == 0)
		return false;
	// THE LIVE LIST'S OWN TIER: the list dropped as Clear Results drops it - the kept tiers stay, to be switched to.
	if (set == KFCResultModel::GetResultSetId())
	{
		KFCSearchEngine::DropResults();
		KFC_DIAG_LOG("TIER drop live set=%u tiers=%d", set, static_cast<int>(gTiers.size()));
		return true;
	}
	for (size_t i = 0; i < gTiers.size(); ++i)
		if (gTiers[i].set == set)
		{
			gTiers.erase(gTiers.begin() + static_cast<std::ptrdiff_t>(i));
			KFC_DIAG_LOG("TIER drop set=%u tiers=%d", set, static_cast<int>(gTiers.size()));
			return true;
		}
	return false;
}

void KFCTierStore::ForgetDocument(IDataBase* db)
{
	if (db == nil)
		return;
	for (size_t t = 0; t < gTiers.size(); ++t)
	{
		// ITS FOCI ON THAT DOCUMENT GO FIRST, every tier's - a book's too: no focus outlives its document (KFCRowFoci's rule
		// for the live list - DetachDocument).
		KFCRowFoci::LetDocumentOf(gTiers[t].foci, db);
		// A BOOK'S TIER KEEPS ITS CHAPTERS, as the live book list does (KFCCloseDocResponder: "book results survive a chapter
		// closing") - its chapters are reached by their files, and KFC itself opens and hands back a book's chapters on every
		// book search: emptied here, a kept book tier would lose its rows to the next search.
		if (gTiers[t].list.header.fromBook)
			continue;
		std::vector<KFCResultModel::Chapter>& chapters = gTiers[t].list.chapters;
		for (size_t c = 0; c < chapters.size(); ++c)
		{
			// By the chapter's file first, then its docRef (a chapter of an All Documents list has no file) - a UIDRef can
			// outlive its document and name another one (memory uidref-reuse-after-close).
			UIDRef found = chapters[c].docRef;
			if (KFCBookScope::FindOpenChapterDoc(chapters[c].file, found) && found.GetDataBase() == db)
			{
				KFCResultModel::EmptyChapter(chapters[c]);
				KFC_DIAG_LOG("TIER forget chapter=%d of set=%u", static_cast<int>(c), gTiers[t].set);
			}
		}
	}
}

void KFCTierStore::ChaptersOfDoc(uint32 set, IDataBase* db, std::vector<int32>& out)
{
	const TierRecord* const tier = TierOf(set);
	if (tier == nil || db == nil)
		return;
	for (size_t c = 0; c < tier->list.chapters.size(); ++c)
	{
		// ForgetDocument's lookup - by file first, then by docRef.
		UIDRef found = tier->list.chapters[c].docRef;
		if (KFCBookScope::FindOpenChapterDoc(tier->list.chapters[c].file, found) && found.GetDataBase() == db)
			out.push_back(static_cast<int32>(c));
	}
}

bool KFCTierStore::HoldsSet(uint32 set)
{
	return TierOf(set) != nil;
}

uint32 KFCTierStore::LayoutOf(uint32 set)
{
	const TierRecord* const tier = TierOf(set);
	return (tier != nil) ? tier->list.layout : 0;
}

bool KFCTierStore::ApplyRowStep(uint32 set, const KFCResultModel::RowStep& step, bool after)
{
	TierRecord* const tier = TierOf(set);
	if (tier == nil)
		return false;
	KFCResultModel::ApplyRowStepTo(tier->list, step, after);
	return true;
}

bool KFCTierStore::SwitchTo(uint32 set, PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// 1. THE DOORS - each turns the switch away before anything moves.
	if (KFCRunGuard::IsAnyRunning())
	{
		outSummary.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const TierRecord* const target = TierOf(set);
	if (target == nil)
	{
		outSummary.Append("Could not switch: that tier is no longer kept - search again.");
		return false;
	}
	// A book's tier reaches its rows through the book (the jump brings its tab forward, Hide Previous Chapter closes its
	// other chapters): its book has to be open.
	if (target->list.header.fromBook && !KFCBookScope::IsBookStillOpen(target->bookPath))
	{
		outSummary.Append("Could not switch: the book that tier was searched in is not open - open it, or search again.");
		return false;
	}

	// 2. THE LIVE LIST PUT ASIDE (as a tier, when it is one to keep - asked of it now), and the tier wanted copied out of
	// the store before the store changes.
	const bool liveKeepable = LiveListIsKeepable();
	TierRecord live;
	TakeLive(live);
	TierRecord wanted = *target;

	// 3. THE TIER'S LIST BACK, AS ITS OWN RESULT SET (the writes kept for it are followed again - KFCUndoFollow).
	// RestoreModelSnapshot lets the live list's text foci go and attaches the tier's where its stories are as kept.
	// FIND/CHANGE IS LEFT AS IT IS (v2 R1 - the author: "there is no need to change it when a row is selected"): a row
	// operation states this list's query only when Find/Change is on another one, and puts it back after (KFCQueryScope).
	KFCResultModel::RestoreModelSnapshot(wanted.list);
	// ITS ROWS' FOCI BACK - the ones held while it was grey, which followed the edits since (the list put back attached
	// new ones only where its stories are as kept - KFCRowFoci::TakeBack lets those go for the held).
	KFCRowFoci::TakeBack(wanted.foci);
	KFCResultModel::ReturnToResultSet(wanted.set);
	// ITS ROWS FOLLOW THOSE FOCI AT ONCE (KFCSearchEngine::FollowChapterFoci - the author, 10-14 night: "the Story part's
	// text is getting different"): the edits made while it was grey moved its foci, not its rows; the list is drawn
	// with the text as it is now - the story rows' words too.
	for (int32 c = 0; c < KFCResultModel::GetChapterCount(); ++c)
		KFCSearchEngine::FollowChapterFoci(c);
	// The Find Format this list was searched with is the one the replace's door compares against (FindFormatHasChanged) -
	// read off the list's own copy, as Find/Change does not hold it.
	KFCSearchEngine::RememberFindFormatOf(wanted.list.header.query.get());
	// The book it was searched in is the one the panel shows (the held chapters of the list put aside go - they are reached
	// by their files when that tier comes back).
	KFCBookScope::ReleaseSearchedBook();
	if (wanted.list.header.fromBook)
		KFCBookScope::SetSearchedBookPath(wanted.bookPath);

	// 4. THE STORE: the tier switched to leaves it; the list put aside goes in at its place in search order (when it is one
	// to keep) - every tier stays where it stood (v2 R4).
	for (size_t i = 0; i < gTiers.size(); ++i)
		if (gTiers[i].set == wanted.set)
		{
			gTiers.erase(gTiers.begin() + static_cast<std::ptrdiff_t>(i));
			break;
		}
	if (liveKeepable)
		AddInOrder(std::move(live));
	KFC_DIAG_LOG("TIER switch set=%u tiers=%d", wanted.set, static_cast<int>(gTiers.size()));
	return true;
}

void KFCTierStore::ShutdownCleanup()
{
	gTiers = std::vector<TierRecord>();
}

// End, KFCTierStore.cpp.
