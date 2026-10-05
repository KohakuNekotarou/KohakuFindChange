//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Show Changes by KohakuFindChange - see KFCShowChanges.h. The run's shape is the search's
//  (KFCSearchEngine::SearchBook): the refusals, then the commit point, then one chapter at a time -
//  opened, read inside a dirty guard, handed straight back - and the chapters appended to the model as
//  they finish. What differs is where the rows come from: the story's signed records
//  (KFCTrackChange::CollectSignedRows) instead of a walk, built into hits by the search's own
//  BuildHit (KFCSearchEngine::HitBuilder) so they read exactly like a search's.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"
#include "IStoryList.h"				// every text model - a record counts wherever it stands

// General includes:
#include "IDTime.h"					// a run's start as a local date and time
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "KFCProgressBar.h"		// the read's progress + cancel - the bar is the UI half's
#include "WideString.h"				// IDTime::DateToString

#include <algorithm>				// std::sort - the runs, newest first
#include <map>
#include <utility>					// std::move - a finished chapter is handed to the model
#include <vector>

// Project includes:
#include "KFCShowChanges.h"
#include "KFCBookScope.h"
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCSearchEngine.h"		// HitBuilder / FinalizeHits / ReadStoryVersion / KFCAdvanceProgress
#include "KFCTrackChange.h"

namespace
{

// Show Changes is reading. See KFCShowChanges::IsShowing.
bool gShowing = false;

struct ShowingFlagGuard
{
	ShowingFlagGuard()	{ gShowing = true; }
	~ShowingFlagGuard()	{ gShowing = false; }
};

// One chapter's share of the progress bar - the search's slice (KFCSearchEngine.cpp,
// kKFCChapterProgressSpan), divided here by the chapter's text models.
const int32 kShowChapterProgressSpan = 10000;

// A record's run: its time with the row's number - the four decimal digits below the millisecond -
// dropped (KFCTrackChange::StampForRow).
uint64 RunStartOf(uint64 time)
{
	return time - time % 10000;
}

// Two digits, for the minutes and seconds of RunLabel.
void AppendTwoDigits(PMString& s, int32 n)
{
	if (n < 10)
		s.Append("0");
	s.AppendNumber(n);
}

// The run row's text: the run's start as a local date and time, seconds included. The records hold UTC
// (100 ns since 1601 - an ATime, which IDTime takes as it is).
// IDTime, THE SDK'S OWN LOCAL TIME. Its header says "local" of both calls used here (IDTime.h:207-227) -
// GlobalTime's does not say either way. The date is the system's short form; the time is put together from
// the local hour, minute and second, because every SDK time string stops at the minute ("10:40", measured
// with GlobalTime::TimeToString) - and two runs made in one minute would then draw as the same row twice
// (case show-two-runs). (Not Win32 GetTimeFormatEx: the SDK's own call is the one to lean on.)
PMString RunLabel(uint64 t0)
{
	const IDTime when(t0);
	WideString date;
	int32 hour = 0, minute = 0, second = 0;
	PMString label;
	if (when.DateToString(date, true /*short*/) && when.GetTime(nil, nil, nil, &hour, &minute, &second))
	{
		label = PMString(date);
		label.Append(" ");
		label.AppendNumber(hour);
		label.Append(":");
		AppendTwoDigits(label, minute);
		label.Append(":");
		AppendTwoDigits(label, second);
	}
	label.SetTranslatable(kFalse);
	return label;
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

// Order one chapter's rows into runs - newest run first, each run's rows in page order with their
// locators (the search's own finishing pass) - and give each its run. The chapter's runs are filled in
// beside them; the model builds the groups (AppendChapter).
void GroupIntoRuns(std::vector<KFCResultModel::Hit>& ioHits, std::vector<KFCResultModel::RunGroup>& outRuns)
{
	std::vector<uint64> starts;
	for (size_t i = 0; i < ioHits.size(); ++i)
		starts.push_back(RunStartOf(ioHits[i].recordTime));
	std::sort(starts.begin(), starts.end());
	starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
	std::reverse(starts.begin(), starts.end());		// newest first

	std::map<uint64, size_t> runOfStart;
	std::vector<std::vector<KFCResultModel::Hit> > buckets(starts.size());
	outRuns.clear();
	for (size_t r = 0; r < starts.size(); ++r)
	{
		runOfStart[starts[r]] = r;
		KFCResultModel::RunGroup run;
		run.t0 = starts[r];
		run.label = RunLabel(starts[r]);
		outRuns.push_back(run);
	}
	for (size_t i = 0; i < ioHits.size(); ++i)
	{
		const size_t r = runOfStart[RunStartOf(ioHits[i].recordTime)];
		ioHits[i].run = static_cast<int32>(r);
		buckets[r].push_back(std::move(ioHits[i]));
	}
	ioHits.clear();
	for (size_t r = 0; r < buckets.size(); ++r)
	{
		KFCSearchEngine::FinalizeHits(buckets[r]);
		for (size_t k = 0; k < buckets[r].size(); ++k)
			ioHits.push_back(std::move(buckets[r][k]));
	}
}

} // anonymous namespace

bool KFCShowChanges::IsShowing()
{
	return gShowing;
}

int32 KFCShowChanges::Run(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// The search's doors, in its order: this run itself, then any other of ours (the bar pumps events,
	// and a run that hands back the chapters another is reading closes them under it - KFCRunGuard).
	if (gShowing)
	{
		outSummary.Append("Show Changes by KohakuFindChange is already running.");
		return 0;
	}
	if (KFCRunGuard::IsAnyRunning())
	{
		outSummary.Append(KFCRunGuard::BusyMessage());
		return 0;
	}
	const ShowingFlagGuard showingGuard;

	// EVERY REFUSAL COMES BEFORE THE MODEL IS TOUCHED (SearchBook's rule): a run turned away
	// leaves the panel as it found it.
	const bool fromBook = KFCBookScope::IsBookScopeOn();
	// An EMPTY target book is a refusal like the others, ahead of the commit point (the search's door, the
	// same one answer: KFCBookScope::GetTargetBook).
	const KFCBookScope::TargetBook targetBook = fromBook ? KFCBookScope::GetTargetBook() : KFCBookScope::kNoTargetBook;
	if (fromBook && targetBook == KFCBookScope::kNoTargetBook)
	{
		outSummary.Append("Book Scope is on, but no book is open.");
		return 0;
	}
	if (fromBook && targetBook == KFCBookScope::kTargetBookEmpty)
	{
		outSummary.Append("That book has no chapters.");
		return 0;
	}
	if (!fromBook && KFCBookScope::ActiveDocument() == nil)
	{
		outSummary.Append("No open document.");
		return 0;
	}

	// THE COMMIT POINT. The rows go, with the book and the find format that describe them.
	//
	// ...and the chapters the old results held go NOW, not on a schedule - the search's commit point, the same
	// two lines. DropResults closes them with kSchedule, and a scheduled close waits until this run is over:
	// until then the chapter is open and no longer held, so OpenChapterDoc below finds it open and reads it as
	// somebody else's - harmless while a scheduled close never runs under a modal bar (measured), but one rule
	// for both commit points is one fewer thing that has to stay true.
	KFCBookScope::ReleaseHeldDocs(true /*close now*/);
	KFCSearchEngine::DropResults();

	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	if (fromBook)
	{
		if (!KFCBookScope::ListBookChapters(targets, bookName) || targets.empty())
		{
			// (Refused at the front door - this is the safety net, as in the search.)
			outSummary.Append("That book has no chapters.");
			return 0;
		}
	}
	else
	{
		IDocument* doc = KFCBookScope::ActiveDocument();
		if (doc == nil)
		{
			outSummary.Append("No open document.");
			return 0;
		}
		targets.push_back(KFCBookScope::DocAsChapter(doc));
	}

	// On the results, as the search records them - and that these rows came from the records, which
	// takes every replace off them (KFCResultModel::SetFromRecords). No search mode, no walk signature:
	// nothing was searched.
	KFCResultModel::SetFromBook(fromBook);
	KFCResultModel::NoteRun();
	KFCResultModel::SetBookName(bookName);
	KFCResultModel::SetFromRecords(true);

	PMString progressTitle(fromBook ? "Reading the book's changes..." : "Reading changes...");
	progressTitle.SetTranslatable(kFalse);
	KFCProgressBar progressBar(progressTitle, 0,
		static_cast<int32>(targets.size()) * kShowChapterProgressSpan, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);
	int32 progressBase = 0;
	int32 progressReported = 0;

	int32 total = 0;
	bool capped = false;
	bool cancelled = false;
	std::vector<uint64> runStarts;		// every run met, across the chapters - the summary's count
	std::vector<PMString> unreadable;
	std::vector<PMString> unclosed;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		KFCSetChapterTask(progressBar, "Chapter", i, targets.size(), targets[i].shortName);
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
		if (progressBar.WasCancelled(kFalse))
		{
			cancelled = true;
			break;
		}
		const int32 remaining = KFCResultModel::kKFCCollectHitLimit - total;
		if (remaining <= 0)
		{
			capped = true;
			break;
		}
		if (fromBook && targets[i].docRef == UIDRef::gNull)
		{
			if (!KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			{
				progressBase += kShowChapterProgressSpan;
				KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
				continue;
			}
		}
		const UIDRef chapterDocRef = targets[i].docRef;

		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> storyVersions;
		bool docCapped = false;
		const bool read = ReadDocumentRows(chapterDocRef, static_cast<size_t>(remaining), 0, hits, storyVersions,
			docCapped, &progressBar, progressBase, progressReported);
		progressBase += kShowChapterProgressSpan;
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Handed back HERE, closed on the spot - the search's reasons (SearchBook): one chapter held at a
		// time, and what was read is plain data now.
		if (!KFCBookScope::HandBackHeldDocNow(chapterDocRef))
			unclosed.push_back(targets[i].shortName);

		if (docCapped)
			capped = true;
		if (!read)
		{
			unreadable.push_back(targets[i].shortName);
			continue;
		}
		if (hits.empty())
			continue;

		KFCResultModel::Chapter chapter;
		GroupIntoRuns(hits, chapter.runs);
		for (size_t r = 0; r < chapter.runs.size(); ++r)
			runStarts.push_back(chapter.runs[r].t0);
		chapter.name = targets[i].shortName;
		chapter.name.SetTranslatable(kFalse);
		chapter.docRef = targets[i].docRef;
		chapter.file = targets[i].file;
		chapter.hits.swap(hits);
		chapter.storyVersions.swap(storyVersions);
		total += static_cast<int32>(chapter.hits.size());
		KFCResultModel::AppendChapter(std::move(chapter));
	}

	// Asked once more after the loop, as the search does: a cancel pressed during the last chapter has no
	// next pass to be seen in.
	if (!cancelled && progressBar.WasCancelled(kFalse))
		cancelled = true;
	if (cancelled)
	{
		KFCSearchEngine::DropResults();
		outSummary.Clear();
		outSummary.SetTranslatable(kFalse);
		outSummary.Append("Show Changes cancelled.");
		return 0;
	}

	PMString chapterNotes;
	chapterNotes.SetTranslatable(kFalse);
	KFCBookScope::AppendChapterNote(chapterNotes, "could not be read", unreadable, ".");
	KFCBookScope::AppendUnopenableNote(chapterNotes, unopenable);
	KFCBookScope::AppendUnclosedNote(chapterNotes, unclosed);

	if (total == 0)
	{
		outSummary.Append(fromBook ? "No changes by KohakuFindChange in this book."
			: "No changes by KohakuFindChange in this document.");
		outSummary.Append(chapterNotes);
		return 0;
	}

	std::sort(runStarts.begin(), runStarts.end());
	const int32 runCount = static_cast<int32>(std::unique(runStarts.begin(), runStarts.end()) - runStarts.begin());
	outSummary.Append("Found ");
	outSummary.AppendNumber(total);
	outSummary.Append(" change(s) by KohakuFindChange in ");
	outSummary.AppendNumber(runCount);
	outSummary.Append(" run(s) - right-click a row to reject or accept them. To replace again, search again.");
	// One cap: the ceiling is the panel's display cap (kKFCCollectHitLimit), so a list that stops there has
	// every row it holds on the panel - no second "showing the first N" note.
	if (capped)
	{
		outSummary.Append(" Stopped at the ");
		outSummary.AppendNumber(KFCResultModel::kKFCCollectHitLimit);
		outSummary.Append(" safety limit.");
	}
	outSummary.Append(chapterNotes);
	return total;
}

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
		KFCSearchEngine::FinalizeHits(hits);		// page order and locators, as a search's rows (no runs: Hit::run stays -1)
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
