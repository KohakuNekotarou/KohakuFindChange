//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A query run's list from its own signed records - see KFCShowChanges.h. (Until 2026-10-06 this was Show Changes
//  by KohakuFindChange as well; it went with Track Changes - docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md
//  F2. Task 6 of docs/superpowers/plans/2026-10-06-kfc-no-track-change-all.md replaces what is left.) The rows come
//  from the story's signed records (KFCTrackChange::CollectSignedRows), built into hits by the search's own BuildHit
//  (KFCSearchEngine::HitBuilder) so they read exactly like a search's.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"
#include "IStoryList.h"				// every text model - a record counts wherever it stands

// General includes:
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "KFCProgressBar.h"		// the read's progress + cancel - the bar is the UI half's

#include <algorithm>				// std::sort - the runs, newest first
#include <map>
#include <set>						// MergeRunRows - the merged rows' times
#include <utility>					// std::move - a finished chapter is handed to the model
#include <vector>

// Project includes:
#include "KFCShowChanges.h"
#include "KFCBookScope.h"
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"		// HitBuilder / FinalizeHits / ReadStoryVersion / KFCAdvanceProgress
#include "KFCTrackChange.h"

namespace
{

// One chapter's share of the progress bar - the search's slice (KFCSearchEngine.cpp,
// kKFCChapterProgressSpan), divided here by the chapter's text models.
const int32 kShowChapterProgressSpan = 10000;

// A record's run: its time with the row's number - the four decimal digits below the millisecond -
// dropped (KFCTrackChange::StampForRow).
uint64 RunStartOf(uint64 time)
{
	return time - time % 10000;
}

// A row's deleted text with the run's own characters taken out (ListOwnRun): masked where a write of the run put
// characters an earlier query had written into it (KFCTrackChange::RunMaskedText), otherwise as it stands.
PMString RunOriginalOfRow(const UIDRef& story, const KFCTrackChange::SignedRow& row)
{
	PMString text;
	text.SetTranslatable(kFalse);
	if (!row.hasDelete)
		return text;
	if (!KFCTrackChange::RunMaskedText(story, row.time, row.deletedText, text))
		text = row.deletedText;
	text.SetTranslatable(kFalse);
	return text;
}

bool RowBefore(const KFCTrackChange::SignedRow& a, const KFCTrackChange::SignedRow& b)
{
	return (a.at != b.at) ? (a.at < b.at) : (a.time < b.time);
}

// A QUERY RUN'S ROWS AT ONE PLACE, DRAWN AS ONE (ListOwnRun - the spec's section 4: "the same place written by two
// queries is one row"). Measured 2026-10-05 on the regression case qs-chain (" - " -> " - " with an en dash, then the
// en dash -> an em dash): the records came out as TWO rows - the first query's pieces either side of the gap the
// second wrote into, and the second's deletion holding the first one's en dash. Rows that overlap or touch, from two
// runs or more, become one: the range they cover, the text standing there now, and the range's text before the run
// read from their records (below - what the first query's dash was is in no record, so the run noted it when it
// wrote: KFCTrackChange::NoteRunDeletions). Rows of one run alone - a
// touching group of one query - stay as Show Changes draws them. The rows' times stay apart in the records, so the
// merged row takes the earliest; a query run's list takes nothing back by row (KFCResultModel::IsFromQueryRun).
void MergeRunRows(const UIDRef& story, std::vector<KFCTrackChange::SignedRow>& ioRows)
{
	std::vector<KFCTrackChange::SignedRow> rows(ioRows);
	std::sort(rows.begin(), rows.end(), RowBefore);
	ioRows.clear();
	size_t i = 0;
	while (i < rows.size())
	{
		TextIndex end = rows[i].at + rows[i].spanLen;
		bool twoRuns = false;
		size_t j = i + 1;
		for (; j < rows.size() && rows[j].at <= end; ++j)
		{
			if (rows[j].at + rows[j].spanLen > end)
				end = rows[j].at + rows[j].spanLen;
			if (RunStartOf(rows[j].time) != RunStartOf(rows[i].time))
				twoRuns = true;
		}
		if (!twoRuns)
		{
			for (size_t k = i; k < j; ++k)
			{
				KFCTrackChange::SignedRow row = rows[k];
				row.deletedText = RunOriginalOfRow(story, row);
				ioRows.push_back(row);
			}
			i = j;
			continue;
		}
		KFCTrackChange::SignedRow merged;
		merged.time = rows[i].time;
		merged.at = rows[i].at;
		merged.spanLen = end - rows[i].at;
		merged.insLen = merged.spanLen;
		merged.insertedText = KFCTrackChange::ReadText(story, merged.at, merged.spanLen);
		merged.insertedText.SetTranslatable(kFalse);
		merged.deletedText.SetTranslatable(kFalse);
		// THE TEXT BEFORE THE RUN, FROM THE RECORDS (2026-10-05, the deferred minor of the re-check): the range read
		// with every insertion of these rows left out and every deletion put back where it stands, its text as the
		// run noted it (KFCTrackChange::ApplyRunNotes) - so the characters a GREP $n kept, which no record holds, are
		// read where they stand. Joining the rows' deleted texts instead dropped those, and put a chained row's
		// characters out of place. When a record stands outside the range, the joined texts are the fallback.
		std::set<uint64> times;
		for (size_t k = i; k < j; ++k)
		{
			if (rows[k].time < merged.time)
				merged.time = rows[k].time;
			times.insert(rows[k].time);
		}
		std::vector<KFCTrackChange::Record> recs;
		KFCTrackChange::CollectRecordsOfTimes(story, times, recs);
		KFCTrackChange::ApplyRunNotes(story, recs);
		bool allInside = false;
		merged.deletedText = KFCTrackChange::OriginalFromRecords(story, merged.at, merged.spanLen, recs, allInside);
		if (!allInside)
		{
			merged.deletedText.Clear();
			for (size_t k = i; k < j; ++k)
				merged.deletedText.Append(RunOriginalOfRow(story, rows[k]));
		}
		merged.deletedText.SetTranslatable(kFalse);
		merged.hasDelete = !merged.deletedText.empty();
		merged.delAt = merged.at + merged.spanLen;
		ioRows.push_back(merged);
		i = j;
	}
}

// Read one open document's signed rows into outHits (in story order, each story's in position order),
// and each story's version where it was read. A row replaced with nothing is the zero-width row at its
// deletion. Stops at maxHits (outCapped). False = the document could not be read at all.
//
// Read-only, inside a SaveRestoreModifiedState guard like the search's walk: building a hit asks for
// the frame a position composes into, and composing marks a database modified - a chapter this run
// opened must come out as clean as it went in, or ReleaseHeldDoc will not close it.
bool ReadDocumentRows(const UIDRef& docRef, size_t maxHits, uint64 floor, std::vector<KFCResultModel::Hit>& outHits,
	std::map<UID, uint32>& outVersions, bool& outCapped, KFCProgressBar* bar, int32 progressBase,
	int32& ioProgressReported)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return false;
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return false;

	KFCSearchEngine::HitBuilder builder;
	// Every text model, not only the user-accessible ones - the question Accept All Changes by
	// KohakuFindChange asks (KFCTrackChange::DocumentHasSignedRecords): a record counts wherever it stands.
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
	{
		if (bar != nil)
			KFCAdvanceProgress(bar, ioProgressReported,
				progressBase + static_cast<int32>((static_cast<int64>(kShowChapterProgressSpan) * i) / count));
		if (bar != nil && bar->WasCancelled(kFalse))
			return true;		// the caller asks again and throws everything away

		const UIDRef story = storyList->GetNthTextModelUID(i);
		std::vector<KFCTrackChange::SignedRow> rows;
		KFCTrackChange::CollectSignedRows(story, rows);
		if (floor != 0)
		{
			// A query run's list (ListOwnRun): its own rows only, and those at one place drawn as one.
			std::vector<KFCTrackChange::SignedRow> own;
			for (size_t k = 0; k < rows.size(); ++k)
				if (rows[k].time >= floor)
					own.push_back(rows[k]);
			MergeRunRows(story, own);
			rows.swap(own);
		}
		if (rows.empty())
			continue;

		// The story's version as it is read: Reject / Accept write to it only
		// while it still stands there.
		uint32 version = 0;
		if (KFCSearchEngine::ReadStoryVersion(db, story.GetUID(), version))
			outVersions[story.GetUID()] = version;

		for (size_t k = 0; k < rows.size(); ++k)
		{
			if (rows[k].time < floor)
				continue;		// a record older than the run asked for (ListOwnRun) - Show Changes passes 0
			if (outHits.size() >= maxHits)
			{
				outCapped = true;
				return true;
			}
			const KFCTrackChange::SignedRow& row = rows[k];
			KFCResultModel::Hit hit;
			// (the pieces' span, not their sum: a GREP <$0>'s two pieces stand around the match)
			if (!builder.Build(docRef, story, row.at, row.at + row.spanLen, hit))
				return false;		// out of memory - nothing to show honestly
			// A replaced row, tied to its records by its time exactly - what Reject Change and Accept
			// Change look it up by (KFCTrackChange::FindRowChangeForHit).
			hit.replaced = true;
			hit.recordTime = row.time;
			// Its texts, as the replace writes them (KFCResultModel::SetHitChangeTexts): what its insertion
			// reads now, and what its deletion took. A touching group written front to back leaves ONE
			// deletion, on its LAST row - the others carry no original text, which is exactly what
			// RejectRowsNow's and AcceptRowsNow's check wants: the group's text with its records taken back
			// (KFCTrackChange::OriginalFromRecords) reads as its rows' originals joined (section 2 of the spec
			// named in KFCShowChanges.h).
			hit.originalText = row.deletedText;
			hit.originalText.SetTranslatable(kFalse);
			// The pieces' own text, not the range's: a row somebody typed inside reads their
			// characters in the range, and FindRowChangeForHit refuses it by that difference - the search's
			// rows are refused the same way (case signed-user-typed-then-reject).
			hit.replacedText = row.insertedText;
			hit.replacedText.SetTranslatable(kFalse);
			outHits.push_back(std::move(hit));
		}
	}
	return true;
}

} // anonymous namespace

int32 KFCShowChanges::ListOwnRun(const std::vector<KFCBookScope::ChapterDoc>& docs, uint64 floor, bool& outCapped)
{
	outCapped = false;
	int32 total = 0;
	int32 noBar = 0;
	for (size_t i = 0; i < docs.size(); ++i)
	{
		const int32 remaining = KFCResultModel::kKFCCollectHitLimit - total;
		if (remaining <= 0)
		{
			outCapped = true;
			break;
		}
		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> storyVersions;
		bool docCapped = false;
		if (!ReadDocumentRows(docs[i].docRef, static_cast<size_t>(remaining), floor, hits, storyVersions, docCapped,
				nil, 0, noBar))
			continue;
		if (docCapped)
			outCapped = true;
		if (hits.empty())
			continue;
		KFCSearchEngine::FinalizeHits(hits);		// page order and locators, as a search's rows
		KFCResultModel::Chapter chapter;
		chapter.name = docs[i].shortName;
		chapter.name.SetTranslatable(kFalse);
		chapter.docRef = docs[i].docRef;
		chapter.file = docs[i].file;
		chapter.hits.swap(hits);
		chapter.storyVersions.swap(storyVersions);
		total += static_cast<int32>(chapter.hits.size());
		KFCResultModel::AppendChapter(std::move(chapter));
	}
	return total;
}

// End, KFCShowChanges.cpp.
