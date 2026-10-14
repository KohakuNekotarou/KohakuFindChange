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
#include "IKFCUIServices.h"	// QueryReadsAsTier - the switch's third check, asked of the UI half (it names the tiers)
#include "KFCBookScope.h"	// GetSearchedBookPath - the book a list was searched in; FindOpenChapterDoc - a chapter, by its file
#include "KFCDiag.h"		// the TIER lines - test builds only
#include "KFCQueryCopy.h"	// the query stated back into Find/Change and checked (the switch)
#include "KFCRunGuard.h"	// no switch while a run of ours is under way
#include "KFCSearchEngine.h"	// BuildWalkSignature - the switch's second check; RememberFindFormatNow

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

	// KEPT OR NOT - the one answer, asked of the live list by a search's commit point (ParkCurrent) and by the switch (the
	// list it puts aside) - the stacked results' ROW-41, the model's side of it: a list with a row, or one a search made (its
	// query was copied - a search that found nothing is a tier too). A query run's list or Change All's is neither.
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
	}

	// A tier added as the newest; past the cap, the oldest go - from the front, where they stand.
	void AddNewest(TierRecord&& tier)
	{
		gTiers.push_back(std::move(tier));
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
	AddNewest(std::move(kept));
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
	if (target->list.header.query == nullptr || !target->list.header.query->valid)
	{
		outSummary.Append("Could not switch: the query that tier was searched with was not kept - search it again.");
		return false;
	}
	// A book's tier reaches its rows through the book (the jump brings its tab forward, Hide Previous Chapter closes its
	// other chapters): its book has to be open.
	if (target->list.header.fromBook && !KFCBookScope::IsBookStillOpen(target->bookPath))
	{
		outSummary.Append("Could not switch: the book that tier was searched in is not open - open it, or search again.");
		return false;
	}

	// 2. NOW, KEPT ASIDE: the live list (as a tier - whether it is one to keep is asked of it now) and the query Find/Change
	// holds now - what a refusal puts back. The tier wanted is copied out of the store before the store changes.
	const bool liveKeepable = LiveListIsKeepable();
	TierRecord live;
	TakeLive(live);
	KFCQueryCopy before;
	KFCQueryCopier::Capture(before);
	const TierRecord wanted = *target;

	// 3. THE TIER'S LIST BACK, AS ITS OWN RESULT SET (the writes kept for it are followed again - KFCUndoFollow).
	// RestoreModelSnapshot lets the live list's text foci go and attaches the tier's where its stories are as kept.
	KFCResultModel::RestoreModelSnapshot(wanted.list);
	KFCResultModel::ReturnToResultSet(wanted.set);

	// 4. ITS QUERY STATED BACK, AND CHECKED THREE TIMES (section 3-3-1: field by field - the change side too, which the
	// signature does not carry -, by the walk signature the list was searched with, and in the person's words below).
	PMString what;
	bool good = KFCQueryCopier::Apply(*wanted.list.header.query, what);
	if (good)
		good = KFCQueryCopier::Matches(*wanted.list.header.query, what);
	if (good)
	{
		PMString signature;
		KFCSearchEngine::BuildWalkSignature(signature);
		if (signature.IsEmpty() || signature != wanted.list.header.walkSignature)
		{
			good = false;
			what = PMString("the walk signature");
			what.SetTranslatable(kFalse);
		}
	}
	if (good)
	{
		// AND A THIRD TIME, IN THE WORDS THE PERSON READS (section 3-3-1, item 2 (b)): the tier's name made again from the query
		// Find/Change holds now must read, word for word, as the name the tier was given - asked of the UI half, which names
		// the tiers (IKFCUIServices::QueryReadsAsTier). Here, before the switch is kept, so a mismatch takes the one refusal
		// below. No UI half to ask = not checked = refused (only the panel asks for a switch in any case).
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		PMString name;
		name.SetTranslatable(kFalse);
		if (ui == nil || !ui->QueryReadsAsTier(wanted.set, name))
		{
			good = false;
			if (name.IsEmpty())
				what = PMString("there is no name of that tier to read it against");
			else
			{
				what = PMString("it does not read as the tier's name: ");
				what.Append(name);
			}
			what.SetTranslatable(kFalse);
		}
	}
	if (!good)
	{
		// 5. REFUSED - EVERYTHING PUT BACK AS IT WAS: the live list, its result set, and Find/Change's query before. If that
		// query does not come back either, say so plainly: Find/Change is not what the person left, and a replace reads it.
		KFCResultModel::RestoreModelSnapshot(live.list);
		KFCResultModel::ReturnToResultSet(live.set);
		PMString whatBack;
		bool back = KFCQueryCopier::Apply(before, whatBack);
		if (back)
			back = KFCQueryCopier::Matches(before, whatBack);
		KFC_DIAG_LOG("TIER switch REFUSED set=%u what=%s back=%d", set, what.GetUTF8String().c_str(), back ? 1 : 0);
		if (!back)
		{
			outSummary.Append("Could not switch, and Find/Change could not be put back as it was (");
			outSummary.Append(whatBack);
			outSummary.Append(") - check Edit > Find/Change before replacing.");
			return false;
		}
		outSummary.Append("Could not switch: the query of that tier could not be put back into Find/Change (");
		outSummary.Append(what);
		outSummary.Append(").");
		return false;
	}

	// 6. SWITCHED. The Find Format the list was searched with is the one the replace's door compares against again; the book
	// it was searched in is the one the panel shows (the held chapters of the list put aside go - they are reached by
	// their files when that tier comes back).
	KFCSearchEngine::RememberFindFormatNow();
	KFCBookScope::ReleaseSearchedBook();
	if (wanted.list.header.fromBook)
		KFCBookScope::SetSearchedBookPath(wanted.bookPath);

	// 7. THE ORDER: the tier switched to leaves the store; the list put aside comes in as the newest (when it is one to keep).
	for (size_t i = 0; i < gTiers.size(); ++i)
		if (gTiers[i].set == wanted.set)
		{
			gTiers.erase(gTiers.begin() + static_cast<std::ptrdiff_t>(i));
			break;
		}
	if (liveKeepable)
		AddNewest(std::move(live));
	KFC_DIAG_LOG("TIER switch set=%u tiers=%d", wanted.set, static_cast<int>(gTiers.size()));
	return true;
}

void KFCTierStore::ShutdownCleanup()
{
	gTiers = std::vector<TierRecord>();
}

// End, KFCTierStore.cpp.
