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

namespace
{
	/** ONE KEPT TIER: the whole list as it was kept, its result set, and the book it was searched in. */
	struct TierRecord
	{
		KFCResultModel::ModelSnapshot	list;
		uint32							set;
		PMString						bookPath;	// empty = not a book's list
		TierRecord() : set(0) {}
	};

	std::vector<TierRecord> gTiers;		// oldest first

	TierRecord* TierOf(uint32 set)
	{
		for (size_t i = 0; i < gTiers.size(); ++i)
			if (gTiers[i].set == set)
				return &gTiers[i];
		return nil;
	}
}	// anonymous namespace

void KFCTierStore::ParkCurrent()
{
	// KEPT OR NOT - the one answer (the stacked results' ROW-41, the model's side of it): a list with a row, or one a search
	// made (its query was copied - a search that found nothing is a tier too). A query run's list or Change All's is
	// neither, and goes with the commit point as it always has.
	const bool searched = KFCResultModel::GetQueryCopy() != nullptr;
	if (KFCResultModel::GetDisplayChapterCount() == 0 && !searched)
	{
		KFC_DIAG_LOG("TIER park skipped (no row, no search) set=%u", KFCResultModel::GetResultSetId());
		return;
	}
	TierRecord kept;
	KFCResultModel::TakeModelSnapshot(kept.list);
	kept.set = KFCResultModel::GetResultSetId();
	kept.bookPath.SetTranslatable(kFalse);
	(void)KFCBookScope::GetSearchedBookPath(kept.bookPath);
	gTiers.push_back(std::move(kept));
	// PAST THE CAP, THE OLDEST GO - from the front, where they stand.
	while (gTiers.size() > static_cast<size_t>(kMaxTiers))
		gTiers.erase(gTiers.begin());
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
}

void KFCTierStore::ForgetDocument(IDataBase* db)
{
	if (db == nil)
		return;
	for (size_t t = 0; t < gTiers.size(); ++t)
	{
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

void KFCTierStore::ShutdownCleanup()
{
	gTiers = std::vector<TierRecord>();
}

// End, KFCTierStore.cpp.
