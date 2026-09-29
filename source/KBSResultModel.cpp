//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result model implementation. See KBSResultModel.h for the contract. All state is a single
//  file-static vector of chapters; the getters are bounds-checked so a repaint racing a rebuild
//  (or a stale node id) reads "nothing" rather than crashing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "TextChar.h"	// kTextChar_CR / kTextChar_LF / kTextChar_PilchrowSign - see MarkUpBreaksForDisplay
#include "Utils.h"

#include <algorithm>	// std::lower_bound - where the display cap falls inside one font group
#include <set>			// the rows already copied aside - see BackUpRow
#include <utility>		// std::move - the thinning below hands whole hits over instead of copying

// Project includes:
#include "KBSResultModel.h"

namespace
{
	std::vector<KBSResultModel::Chapter> gChapters;

	// Were these results produced by a book search? Decides whether the tree opens its chapters.
	bool gFromBook = false;

	// Were these rows rebuilt from the Track Changes records (2026-09-29)? See KBSResultModel::SetFromRecords.
	bool gFromRecords = false;

	// The book those results came from (file name only). Drawn on the tree's book row.
	PMString gBookName;

	// The Find/Change tab the search ran in (an IFindChangeOptions::SearchMode value; -1 = nothing
	// searched yet). See KBSResultModel::SetSearchMode for why the replace has to compare against it.
	int32 gSearchMode = -1;

	// The whole of what those results were WALKED by - query plus every switch that decides the match
	// set - as one opaque key. See KBSResultModel::SetWalkSignature.
	PMString gWalkSignature;

	// Is the panel showing a replace's aftermath rather than a search's results? See
	// KBSResultModel::IsShowingReplaceOutcome.
	bool gShowingOutcome = false;

	// Has a command been run since the results were last discarded? See KBSResultModel::NoteRun.
	// Deliberately NOT "are there any chapters": a search that found nothing has still been run.
	bool gHasRun = false;

	// (gStoppedShort - did the search stop short of its scope - stood here until 2026-09-28. Nothing had
	// read it since the replace became one match at a time on 2026-09-27; see KBSResultModel.h.)

	// One row copied aside before a replace changed it. See KBSResultModel::BeginRowBackup.
	struct BackedUpRow
	{
		int32					chapter;
		int32					hit;
		KBSResultModel::Hit		row;
	};

	// Only true while a replace is running; empty at every other moment.
	bool gBackingUpRows = false;
	std::vector<BackedUpRow> gRowBackup;
	std::set<std::pair<int32, int32> > gRowsBackedUp;	// (chapter, hit) already in gRowBackup

	// Copy a row aside before it is first written to, if a replace is running - ONCE per row: the copy
	// taken first is the row as the run found it, and it is the one a rollback has to put back. (Every
	// change was kept until 2026-09-28, a replaced row four or five times over - MarkHitReplaced,
	// SetHitRecordTime, SetHitChangeTexts, SetHitRange, SetHitSegments - and RollBackRows walked the
	// copies backwards so the oldest won; every later copy was overwritten unread.)
	void BackUpRow(int32 chapterIdx, int32 hitIdx, const KBSResultModel::Hit& row)
	{
		if (!gBackingUpRows || !gRowsBackedUp.insert(std::make_pair(chapterIdx, hitIdx)).second)
			return;
		BackedUpRow saved;
		saved.chapter = chapterIdx;
		saved.hit = hitIdx;
		saved.row = row;
		gRowBackup.push_back(saved);
	}

	// Which row the result tree's right-click menu was popped over (KBSResultNodeEH stashes it just
	// before HandlePopupMenu; Check All / Uncheck All read it back). See the header for the two
	// negative values it can hold.
	int32 gContextMenuChapter = KBSResultModel::kNoContextMenuChapter;
	// ...and which HIT row, for the hit row's own menu (Reject Change / Redo, 2026-09-26). -1 = none.
	int32 gContextMenuHitChapter = -1;
	int32 gContextMenuHit = -1;
	int32 gContextMenuGroupChapter = -1;	// the story row right-clicked (2026-09-27)
	int32 gContextMenuGroup = -1;
	int32 gContextMenuRunChapter = -1;		// the run row right-clicked (2026-09-29)
	int32 gContextMenuRun = -1;

	// Does this row carry a check box? THE one definition of the question, so the commands that set
	// the boxes and the counts that decide whether to offer those commands can no longer drift apart:
	// a row the model quietly checked but the panel drew no box for would be replaced without ever
	// having been asked for. Replaced = the text it matched is gone; locked = InDesign offers no way
	// to change it; an outcome already says why it was left alone.
	bool RowHasCheckBox(const KBSResultModel::Hit& hit)
	{
		// A list rebuilt from the records offers no replace at all (2026-09-29, the user's call B).
		if (gFromRecords)
			return false;

		// A replace's report offers work only on the rows Reject Change put back (2026-09-27, B). This is
		// the ROW's half of NoRowHasCheckBox, and it is the whole of it for a row: over a report, a row
		// taken back and still open is exactly what makes that question answer no, and every other row
		// is refused here. (NoRowHasCheckBox was asked first as well until 2026-09-29 - an answer that
		// could not change this one, and a walk of every row per row: squared in the rows over a report.
		// The callers that loop still ask it once, as their early exit.)
		if (KBSResultModel::IsShowingReplaceOutcome() && hit.outcome != KBSResultModel::kOutcomeRejected)
			return false;

		return !hit.replaced && !hit.isLocked && KBSResultModel::IsWorkOutcome(hit.outcome);
	}

	// Change Checked's work - see KBSResultModel::IsHitCheckedWork. Ticked and carrying a box: the box's own
	// question (RowHasCheckBox) since 2026-09-29 (the defect re-check F-4) - it was "not replaced, and a work
	// outcome" until then, which said yes over a report to the ticked rows of a chapter the run could not
	// open, rows that have no box there.
	bool IsCheckedWork(const KBSResultModel::Hit& hit)
	{
		return hit.checked && RowHasCheckBox(hit);
	}

	// ***** GROUP A CHAPTER'S HITS BY STORY (2026-09-27, the user's call) - the tree's middle level. *****
	// One group per story in first-appearance (page) order - so the stories read in the order their
	// first matches stand - and every hit given its group. The row reads "P<page of the story's first
	// match>  <the story's first words>". (The level held FONTS for Find Missing Glyphs from 2026-08-02;
	// that scan was removed on 2026-09-27, and the level is the story's alone.)
	//
	// The groups are rebuilt from scratch, and every hit's fontGroup / fontGroupPos written, whatever
	// the hit held before - a search's new hits and the hits KeepCheckedRows carries over alike.
	//
	// ***** KEYED BY (RUN, STORY) SINCE 2026-09-29 (Show Changes by KohakuFindChange). ***** On a list rebuilt
	// from the records a story two runs changed stands under each run, so a group is one story of one run;
	// every other list has run -1 throughout, where the key is the story alone, as before. Each run's own
	// list of groups is rebuilt at the end, from the groups (the runs themselves - start and label - are
	// the builder's and stay).
	void BuildFontGroups(KBSResultModel::Chapter& chapter)
	{
		chapter.fontGroups.clear();
		for (size_t i = 0; i < chapter.hits.size(); ++i)
		{
			KBSResultModel::Hit& hit = chapter.hits[i];
			int32 found = -1;
			for (size_t g = 0; g < chapter.fontGroups.size(); ++g)
			{
				if (chapter.fontGroups[g].story == hit.storyUID && chapter.fontGroups[g].run == hit.run)
				{
					found = static_cast<int32>(g);
					break;
				}
			}
			if (found < 0)
			{
				KBSResultModel::FontGroup group;
				group.isStory = true;
				group.story = hit.storyUID;
				group.run = hit.run;
				group.fontName = hit.pageString.IsEmpty() ? PMString("overset") : PMString("P");
				if (!hit.pageString.IsEmpty())
					group.fontName.Append(hit.pageString);
				group.fontName.Append("  ");
				group.fontName.Append(hit.storyLead);
				group.fontName.SetTranslatable(kFalse);
				chapter.fontGroups.push_back(group);
				found = static_cast<int32>(chapter.fontGroups.size()) - 1;
			}
			KBSResultModel::FontGroup& group = chapter.fontGroups[found];
			hit.fontGroup = found;
			hit.fontGroupPos = static_cast<int32>(group.hitIndices.size());
			group.hitIndices.push_back(static_cast<int32>(i));
		}

		// Each run's groups, ascending (a group of a run the chapter does not hold is left out).
		for (size_t r = 0; r < chapter.runs.size(); ++r)
			chapter.runs[r].groups.clear();
		for (size_t g = 0; g < chapter.fontGroups.size(); ++g)
		{
			const int32 run = chapter.fontGroups[g].run;
			if (run >= 0 && run < static_cast<int32>(chapter.runs.size()))
				chapter.runs[run].groups.push_back(static_cast<int32>(g));
		}
	}

	// Hits stored in the chapters BEFORE 'chapterIdx' (book order). The display cap is applied in
	// book order, and every chapter before the boundary chapter is shown in full, so counting full
	// hits here is the budget consumed before this chapter.
	int32 HitsBeforeChapter(int32 chapterIdx)
	{
		int32 sum = 0;
		const int32 n = static_cast<int32>(gChapters.size());
		for (int32 i = 0; i < chapterIdx && i < n; ++i)
			sum += static_cast<int32>(gChapters[i].hits.size());
		return sum;
	}
}

void KBSResultModel::AppendChapter(Chapter&& chapter)
{
	// ***** THE HITS ARE TAKEN, NOT COPIED. ***** A chapter of a large search holds thousands of
	// Hits and each Hit holds its texts as PMStrings, so copying the vector in here doubled the cost of
	// filling the model for nothing: every caller builds a Chapter, hands it over and drops it.
	// Copied until 2026-08-08 - and the search had used swap() to keep the same hits from being
	// copied into that Chapter one line earlier, which this then undid.
	gChapters.push_back(std::move(chapter));
	// Grouped on the way in, on the chapter the model now owns: the groups index the hits they are
	// built from, so they have to be built where those hits are going to live.
	BuildFontGroups(gChapters.back());

	// ***** EVERY ROW STARTS UNTICKED (2026-09-27, the user's call). ***** From 2026-09-26 to 2026-09-27
	// every row came in ticked, because the replace was Change All over whole stories and the rows left
	// out had to be taken back; the replace writes one match at a time again, so the user ticks what is
	// to be replaced. (Hit::checked is false as a Hit is built - nothing to do here.)
}

void KBSResultModel::Clear()
{
	gChapters.clear();
	gShowingOutcome = false;
	gFromBook = false;
	gFromRecords = false;
	gBookName.Clear();
	gSearchMode = -1;
	gWalkSignature.Clear();
	// (KBSEditStamp::Forget was called from here, and the file is gone: the replace checks the
	//  stored positions against a fresh walk rather than fingerprinting each chapter, so nothing
	//  outside this model describes these rows any more.)
	// The right-click target is an index into the chapters that just went away - keeping it would let
	// the next search's Check All reach a chapter the user never right-clicked.
	gContextMenuChapter = kNoContextMenuChapter;
	gContextMenuHitChapter = -1;
	gContextMenuHit = -1;
	gContextMenuGroupChapter = -1;
	gContextMenuGroup = -1;
	gContextMenuRunChapter = -1;
	gContextMenuRun = -1;
	// Discarding the results puts the panel back to the state it started in, illustration included.
	gHasRun = false;
}

void KBSResultModel::SetFromBook(bool fromBook)
{
	gFromBook = fromBook;
}

void KBSResultModel::NoteRun()
{
	gHasRun = true;
}

bool KBSResultModel::HasRun()
{
	return gHasRun;
}

bool KBSResultModel::IsFromBook()
{
	return gFromBook;
}

void KBSResultModel::SetFromRecords(bool fromRecords)
{
	gFromRecords = fromRecords;
}

bool KBSResultModel::IsFromRecords()
{
	return gFromRecords;
}

bool KBSResultModel::NoRowHasCheckBox()
{
	// A list rebuilt from the records has no box anywhere (2026-09-29) - RowHasCheckBox's first answer.
	if (gFromRecords)
		return true;
	// gShowingOutcome rather than IsShowingReplaceOutcome() only because this file owns the flag.
	// The two are the same question - see the header for why both halves have to be asked.
	// ***** EXCEPT A REPORT HOLDING A ROW TAKEN BACK (2026-09-27, B): that row carries a box. *****
	// (A scan's report-only kind was the other half until the two scans were removed, 2026-09-27.)
	return gShowingOutcome && !KBSResultModel::AnyRejectedRowOpen();
}

bool KBSResultModel::IsWorkOutcome(ChangeOutcome outcome)
{
	return outcome == kOutcomeNone || outcome == kOutcomeRejected;
}

bool KBSResultModel::IsHitCheckedWork(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	return hitIdx >= 0 && hitIdx < static_cast<int32>(hits.size()) && IsCheckedWork(hits[hitIdx]);
}

bool KBSResultModel::AnyRejectedRowOpen()
{
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
		for (size_t hi = 0; hi < gChapters[ci].hits.size(); ++hi)
		{
			const Hit& h = gChapters[ci].hits[hi];
			if (h.outcome == kOutcomeRejected && !h.replaced && !h.isLocked)
				return true;
		}
	return false;
}

void KBSResultModel::SetSearchMode(int32 mode)
{
	gSearchMode = mode;
}

int32 KBSResultModel::GetSearchMode()
{
	return gSearchMode;
}

void KBSResultModel::SetWalkSignature(const PMString& signature)
{
	gWalkSignature = signature;
	gWalkSignature.SetTranslatable(kFalse);
}

PMString KBSResultModel::GetWalkSignature()
{
	PMString signature(gWalkSignature);
	signature.SetTranslatable(kFalse);
	return signature;
}

void KBSResultModel::SetBookName(const PMString& name)
{
	gBookName = name;
	gBookName.SetTranslatable(kFalse);
}

PMString KBSResultModel::GetBookName()
{
	PMString name(gBookName);
	name.SetTranslatable(kFalse);
	return name;
}

void KBSResultModel::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do (the KESCL ShutdownCleanup rule).
	gChapters = std::vector<Chapter>();

	// The static PMStrings, emptied for the same reason the vectors are: nothing of ours should
	// still be holding storage when the DLL unloads (the KESCL ShutdownCleanup rule).
	//
	// ALL of them. gChangeText was added on 2026-08-04 and did not get a line here, so the one string
	// that is only ever filled by a replace was the one left holding storage at unload. When a static
	// is added above, it is added here too - that is what this list is. (gQueryText, gChangeText and
	// gRunSummary went with Save Results... on 2026-09-27.)
	gBookName.Clear();
	gWalkSignature.Clear();

	// Normally already empty - a replace clears it on both of its exits - but a shutdown during
	// one would leave copies behind, and these hold PMStrings like the chapters do.
	ForgetRowBackup();
}

int32 KBSResultModel::GetChapterCount()
{
	return static_cast<int32>(gChapters.size());
}

int32 KBSResultModel::GetHitCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	return static_cast<int32>(gChapters[chapterIdx].hits.size());
}

int32 KBSResultModel::GetTotalHitCount()
{
	int32 total = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
		total += static_cast<int32>(gChapters[i].hits.size());
	return total;
}

int32 KBSResultModel::GetDisplayChapterCount()
{
	// The displayed chapters are the book-order prefix whose hits fit under the cap: a chapter is
	// shown when the hits before it have not already used up the whole budget.
	int32 before = 0;
	int32 shown = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
	{
		if (before >= kKBSDisplayHitLimit)
			break;
		++shown;
		before += static_cast<int32>(gChapters[i].hits.size());
	}
	return shown;
}

int32 KBSResultModel::GetDisplayHitCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const int32 before = HitsBeforeChapter(chapterIdx);
	if (before >= kKBSDisplayHitLimit)
		return 0;	// the cap ran out before this chapter
	const int32 remaining = kKBSDisplayHitLimit - before;
	const int32 full = static_cast<int32>(gChapters[chapterIdx].hits.size());
	return (full < remaining) ? full : remaining;
}

bool KBSResultModel::GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	outName = c.name;
	outName.SetTranslatable(kFalse);
	outHitCount = static_cast<int32>(c.hits.size());
	return true;
}

int32 KBSResultModel::GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const Chapter& c = gChapters[chapterIdx];
	if (fontIdx < 0 || fontIdx >= static_cast<int32>(c.fontGroups.size()))
		return 0;

	// The chapter's own share of the cap, then this group's share of that. hitIndices is ascending,
	// so the count is simply where the cap falls inside it.
	const int32 shown = GetDisplayHitCount(chapterIdx);
	const std::vector<int32>& idx = c.fontGroups[fontIdx].hitIndices;
	return static_cast<int32>(std::lower_bound(idx.begin(), idx.end(), shown) - idx.begin());
}

int32 KBSResultModel::GetDisplayFontCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const Chapter& c = gChapters[chapterIdx];

	int32 shownGroups = 0;
	for (int32 g = 0; g < static_cast<int32>(c.fontGroups.size()); ++g)
	{
		if (GetDisplayFontHitCount(chapterIdx, g) > 0)
			++shownGroups;
	}
	return shownGroups;
}

bool KBSResultModel::IsStoryGroup(int32 chapterIdx, int32 groupIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	return groupIdx >= 0 && groupIdx < static_cast<int32>(c.fontGroups.size()) && c.fontGroups[groupIdx].isStory;
}

void KBSResultModel::GetGroupHits(int32 chapterIdx, int32 groupIdx, std::vector<int32>& outHits)
{
	outHits.clear();
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	const Chapter& c = gChapters[chapterIdx];
	if (groupIdx < 0 || groupIdx >= static_cast<int32>(c.fontGroups.size()))
		return;
	outHits = c.fontGroups[groupIdx].hitIndices;
}

int32 KBSResultModel::GetGroupCheckedCount(int32 chapterIdx, int32 groupIdx)
{
	std::vector<int32> rows;
	GetGroupHits(chapterIdx, groupIdx, rows);
	int32 count = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (IsCheckedWork(gChapters[chapterIdx].hits[rows[k]]))
			++count;
	}
	return count;
}

void KBSResultModel::SetGroupChecked(int32 chapterIdx, int32 groupIdx, bool checked)
{
	if (NoRowHasCheckBox())
		return;
	std::vector<int32> rows;
	GetGroupHits(chapterIdx, groupIdx, rows);
	for (size_t k = 0; k < rows.size(); ++k)
	{
		Hit& h = gChapters[chapterIdx].hits[rows[k]];
		if (RowHasCheckBox(h))
			h.checked = checked;
	}
}

void KBSResultModel::SetContextMenuGroup(int32 chapterIdx, int32 groupIdx)
{
	gContextMenuGroupChapter = chapterIdx;
	gContextMenuGroup = groupIdx;
}

bool KBSResultModel::GetContextMenuGroup(int32& outChapterIdx, int32& outGroupIdx)
{
	if (!IsStoryGroup(gContextMenuGroupChapter, gContextMenuGroup))
		return false;
	outChapterIdx = gContextMenuGroupChapter;
	outGroupIdx = gContextMenuGroup;
	return true;
}

// ---- The run level (2026-09-29, Show Changes by KohakuFindChange) - see the header. ----

namespace
{
	// The run, or nil for an index out of range (either index).
	const KBSResultModel::RunGroup* RunAt(int32 chapterIdx, int32 runIdx)
	{
		if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
			return nil;
		const std::vector<KBSResultModel::RunGroup>& runs = gChapters[chapterIdx].runs;
		if (runIdx < 0 || runIdx >= static_cast<int32>(runs.size()))
			return nil;
		return &runs[runIdx];
	}
}

int32 KBSResultModel::GetDisplayRunGroupCount(int32 chapterIdx, int32 runIdx)
{
	const RunGroup* run = RunAt(chapterIdx, runIdx);
	if (run == nil)
		return 0;
	// The groups the cap left a hit to: the first N of the run's (they stand in hit order).
	int32 shown = 0;
	for (size_t k = 0; k < run->groups.size(); ++k)
		if (GetDisplayFontHitCount(chapterIdx, run->groups[k]) > 0)
			++shown;
	return shown;
}

int32 KBSResultModel::GetDisplayRunCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	int32 shown = 0;
	for (int32 r = 0; r < static_cast<int32>(gChapters[chapterIdx].runs.size()); ++r)
		if (GetDisplayRunGroupCount(chapterIdx, r) > 0)
			++shown;
	return shown;
}

bool KBSResultModel::GetRunDisplay(int32 chapterIdx, int32 runIdx, PMString& outLabel, int32& outHitCount)
{
	const RunGroup* run = RunAt(chapterIdx, runIdx);
	if (run == nil)
		return false;
	outLabel = run->label;
	outLabel.SetTranslatable(kFalse);
	// The FULL count, like every other number the tree reads out (GetFontDisplay).
	outHitCount = 0;
	for (size_t k = 0; k < run->groups.size(); ++k)
		outHitCount += static_cast<int32>(gChapters[chapterIdx].fontGroups[run->groups[k]].hitIndices.size());
	return true;
}

int32 KBSResultModel::GetRunGroup(int32 chapterIdx, int32 runIdx, int32 nth)
{
	const RunGroup* run = RunAt(chapterIdx, runIdx);
	if (run == nil || nth < 0 || nth >= static_cast<int32>(run->groups.size()))
		return -1;
	return run->groups[nth];
}

int32 KBSResultModel::GetGroupRun(int32 chapterIdx, int32 groupIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (groupIdx < 0 || groupIdx >= static_cast<int32>(c.fontGroups.size()))
		return -1;
	return c.fontGroups[groupIdx].run;
}

int32 KBSResultModel::GetGroupPosInRun(int32 chapterIdx, int32 groupIdx)
{
	const RunGroup* run = RunAt(chapterIdx, GetGroupRun(chapterIdx, groupIdx));
	if (run == nil)
		return -1;
	for (size_t k = 0; k < run->groups.size(); ++k)
		if (run->groups[k] == groupIdx)
			return static_cast<int32>(k);
	return -1;
}

void KBSResultModel::GetRunHits(int32 chapterIdx, int32 runIdx, std::vector<int32>& outHits)
{
	outHits.clear();
	const RunGroup* run = RunAt(chapterIdx, runIdx);
	if (run == nil)
		return;
	for (size_t k = 0; k < run->groups.size(); ++k)
	{
		const std::vector<int32>& idx = gChapters[chapterIdx].fontGroups[run->groups[k]].hitIndices;
		outHits.insert(outHits.end(), idx.begin(), idx.end());
	}
	// The stories' rows interleave in page order: back into the chapter's order.
	std::sort(outHits.begin(), outHits.end());
}

void KBSResultModel::SetContextMenuRun(int32 chapterIdx, int32 runIdx)
{
	gContextMenuRunChapter = chapterIdx;
	gContextMenuRun = runIdx;
}

bool KBSResultModel::GetContextMenuRun(int32& outChapterIdx, int32& outRunIdx)
{
	if (RunAt(gContextMenuRunChapter, gContextMenuRun) == nil)
		return false;
	outChapterIdx = gContextMenuRunChapter;
	outRunIdx = gContextMenuRun;
	return true;
}

bool KBSResultModel::GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (fontIdx < 0 || fontIdx >= static_cast<int32>(c.fontGroups.size()))
		return false;

	const FontGroup& group = c.fontGroups[fontIdx];
	outName = group.fontName;			// the story row's text - see BuildFontGroups
	outName.SetTranslatable(kFalse);
	outHitCount = static_cast<int32>(group.hitIndices.size());
	return true;
}

int32 KBSResultModel::GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (fontIdx < 0 || fontIdx >= static_cast<int32>(c.fontGroups.size()))
		return -1;
	const std::vector<int32>& idx = c.fontGroups[fontIdx].hitIndices;
	if (nth < 0 || nth >= static_cast<int32>(idx.size()))
		return -1;
	return idx[nth];
}

int32 KBSResultModel::GetHitFontGroup(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return -1;
	return c.hits[hitIdx].fontGroup;
}

int32 KBSResultModel::GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return -1;
	return c.hits[hitIdx].fontGroupPos;
}

bool KBSResultModel::GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	out.locator = h.locator;
	out.accentFlag = h.accentFlag;
	out.preText = h.preText;
	out.matchText = h.matchText;
	out.postText = h.postText;
	out.checked = h.checked;
	out.replaced = h.replaced;
	out.locked = h.isLocked;
	out.outcome = h.outcome;
	// The same call SetHitChecked makes before it accepts a tick, so the panel cannot draw a box the
	// model would refuse. The flags above are handed over as well - the row still says WHY it was
	// left alone ("locked" in the locator, "missing" / "refused" in the accent word) - but nothing
	// outside this file has to add them up into this answer any more.
	out.hasCheckBox = RowHasCheckBox(h);
	return true;
}

// (DescribeAllRows and its three helpers - the app.kfcResults block - went with that property on
//  2026-09-27.)

// U+21B5 DOWNWARDS ARROW WITH CORNER LEFTWARDS - the mark for a forced line break. TextChar.h names
// the pilcrow (kTextChar_PilchrowSign, :122) but carries no constant for this one, so it is named
// here rather than left as a bare number in the loop below.
static const UTF32TextChar kKBSReturnArrow = 0x21B5;

// ***** THE CHARACTERS AN OBJECT STANDS ON ARE NOT SHOWN (2026-09-26, the user's call). ***** They have
// no glyph in the panel's font and drew as a box: a footnote / endnote reference (0x04 / 0x05), the
// marks around an endnote's text and other anchors (U+FEFF), a table's anchor and continuation
// (0x16 / 0x17), the page number and section markers (0x18 / 0x19), an anchored object (U+FFFC).
// Display only, like the break marks: the model keeps them as they are.
static bool IsHiddenMarker(UTF16TextChar c)
{
	return c == 0x04 || c == 0x05 || c == 0x16 || c == 0x17 || c == 0x18 || c == 0x19
		|| c == 0xFEFF || c == 0xFFFC;
}

// See KBSResultModel.h for what this is for and why it is DISPLAY ONLY.
//
// The two marks are the ones InDesign itself draws with Show Hidden Characters on: a pilcrow for a
// paragraph end, a return arrow for a forced line break (Shift+Enter). They are two different things
// to a replace, so a row spells them differently.
//
// Whole runs are copied between the marks rather than one character at a time, so a surrogate pair
// is never split. Most strings hold no break at all, so the string is scanned once before anything
// is built: the common row pays one pass and no allocation.
void KBSResultModel::MarkUpBreaksForDisplay(PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* buf = s.GrabUTF16Buffer(&n);
	if (buf == nil || n <= 0)
		return;

	bool16 any = kFalse;
	for (int32 i = 0; i < n && !any; ++i)
		any = (buf[i] == kTextChar_CR || buf[i] == kTextChar_LF || IsHiddenMarker(buf[i]));
	if (!any)
		return;

	PMString out;
	out.SetTranslatable(kFalse);
	int32 runStart = 0;
	for (int32 i = 0; i < n; ++i)
	{
		const bool marker = IsHiddenMarker(buf[i]);
		if (!marker && buf[i] != kTextChar_CR && buf[i] != kTextChar_LF)
			continue;
		if (i > runStart)
			out.AppendW(buf + runStart, i - runStart);
		if (!marker)
			out.AppendW(buf[i] == kTextChar_CR
				? static_cast<UTF32TextChar>(kTextChar_PilchrowSign)
				: kKBSReturnArrow);
		runStart = i + 1;
	}
	if (n > runStart)
		out.AppendW(buf + runStart, n - runStart);

	s = out;
	s.SetTranslatable(kFalse);
}


bool KBSResultModel::GetHitDisplay(int32 chapterIdx, int32 hitIdx,
	PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outLocator = h.locator;
	outPre = h.preText;
	outMatch = h.matchText;
	outPost = h.postText;
	return true;
}

bool KBSResultModel::GetHitLocation(int32 chapterIdx, int32 hitIdx,
	UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outDocRef = c.docRef;
	outFile = c.file;
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outEnd = h.textEnd;
	return true;
}

void KBSResultModel::RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	gChapters[chapterIdx].docRef = newDocRef;
}

bool KBSResultModel::GetStoryVersion(int32 chapterIdx, UID story, uint32& outVersion)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const std::map<UID, uint32>& versions = gChapters[chapterIdx].storyVersions;
	const std::map<UID, uint32>::const_iterator it = versions.find(story);
	if (it == versions.end())
		return false;
	outVersion = it->second;
	return true;
}

void KBSResultModel::SetStoryVersion(int32 chapterIdx, UID story, uint32 version)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	gChapters[chapterIdx].storyVersions[story] = version;
}

void KBSResultModel::GetTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outHits)
{
	outHits.clear();
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(hits.size()))
		return;
	const Hit& me = hits[hitIdx];
	// the story's rows with a place, in text order
	std::vector<std::pair<TextIndex, int32> > order;
	for (size_t i = 0; i < hits.size(); ++i)
		if (hits[i].storyUID == me.storyUID && hits[i].textStart != kInvalidTextIndex)
			order.push_back(std::make_pair(hits[i].textStart, static_cast<int32>(i)));
	std::sort(order.begin(), order.end());
	size_t at = 0;
	while (at < order.size() && order[at].second != hitIdx)
		++at;
	if (at == order.size())
	{
		outHits.push_back(hitIdx);
		return;
	}
	size_t from = at, to = at;
	while (from > 0 && hits[order[from - 1].second].textEnd >= hits[order[from].second].textStart)
		--from;
	while (to + 1 < order.size() && hits[order[to].second].textEnd >= hits[order[to + 1].second].textStart)
		++to;
	for (size_t k = from; k <= to; ++k)
		outHits.push_back(order[k].second);
}

void KBSResultModel::SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	// The same question the panel asks before it draws a box, asked here so the model can never hold
	// a checked hit that no row offered - a replace's report above all, where only the rows taken back
	// carry a box.
	//
	// ***** ONE ROW AT A TIME AGAIN (2026-09-27). ***** Touching matches went on and off together, and a
	// footnote's row could not go off, while the replace was Change All (2026-09-26 to 2026-09-27): a row
	// left out had to be taken back, and neither a deletion shared by touching matches nor anything in a
	// footnote can be. The replace writes only the ticked matches now, so every box is the row's own.
	// (Reject Change still takes a touching group together - GetTouchingGroup.)
	if (RowHasCheckBox(c.hits[hitIdx]))
		c.hits[hitIdx].checked = checked;
}

uint64 KBSResultModel::GetHitRecordTime(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return 0;
	return c.hits[hitIdx].recordTime;
}

void KBSResultModel::SetHitRecordTime(int32 chapterIdx, int32 hitIdx, uint64 time)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	BackUpRow(chapterIdx, hitIdx, c.hits[hitIdx]);
	c.hits[hitIdx].recordTime = time;
}

bool KBSResultModel::GetHitInFootnote(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	return c.hits[hitIdx].inFootnote;
}

bool KBSResultModel::GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked)
{
	outChecked = false;
	outReplaced = false;
	outLocked = false;
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	outChecked = c.hits[hitIdx].checked;
	outReplaced = c.hits[hitIdx].replaced;
	outLocked = c.hits[hitIdx].isLocked;
	return true;
}

bool KBSResultModel::GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden)
{
	outLocked = false;
	outHidden = false;
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	outLocked = c.hits[hitIdx].isLocked;
	outHidden = c.hits[hitIdx].isHidden;
	return true;
}

void KBSResultModel::SetAllChecked(bool checked)
{
	// Nothing on a list that offers no work is selectable. A short cut, not a second opinion: the
	// per-row test below asks the same question, and this only saves walking every hit to be told
	// so once per row.
	if (NoRowHasCheckBox())
		return;

	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			// The rows that carry no check box are not touched by Check All either - otherwise
			// the model would hold checked hits the panel shows no box for.
			if (!RowHasCheckBox(hits[hi]))
				continue;
			hits[hi].checked = checked;
		}
	}
}

void KBSResultModel::SetChapterChecked(int32 chapterIdx, bool checked)
{
	if (NoRowHasCheckBox())
		return;		// the same short cut SetAllChecked takes, over one chapter
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;

	std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	for (size_t hi = 0; hi < hits.size(); ++hi)
	{
		if (!RowHasCheckBox(hits[hi]))
			continue;
		hits[hi].checked = checked;
	}
}

int32 KBSResultModel::GetCheckedCount()
{
	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (IsCheckedWork(hits[hi]))
				++count;
		}
	}
	return count;
}

int32 KBSResultModel::GetChapterCheckedCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;

	// Same rule as GetCheckedCount, applied to one chapter: a REPLACED row does not count, because
	// it is no longer waiting to be done.
	int32 count = 0;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	for (size_t hi = 0; hi < hits.size(); ++hi)
	{
		if (IsCheckedWork(hits[hi]))
			++count;
	}
	return count;
}

// (GetCheckedChapterCount was defined here until 2026-08-10 - see the note where it was declared
// in KBSResultModel.h.)

int32 KBSResultModel::GetCheckableCount()
{
	if (NoRowHasCheckBox())
		return 0;	// no row of this list has a box, so Check All / Uncheck All grey out

	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (RowHasCheckBox(hits[hi]))
				++count;
		}
	}
	return count;
}

int32 KBSResultModel::GetChapterCheckableCount(int32 chapterIdx)
{
	if (NoRowHasCheckBox())
		return 0;	// no row has a box, whichever chapter the menu was popped over
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;

	int32 count = 0;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	for (size_t hi = 0; hi < hits.size(); ++hi)
	{
		if (RowHasCheckBox(hits[hi]))
			++count;
	}
	return count;
}

void KBSResultModel::SetContextMenuChapter(int32 chapterIdx)
{
	gContextMenuChapter = chapterIdx;
}

int32 KBSResultModel::GetContextMenuChapter()
{
	return gContextMenuChapter;
}

void KBSResultModel::SetContextMenuHit(int32 chapterIdx, int32 hitIdx)
{
	gContextMenuHitChapter = chapterIdx;
	gContextMenuHit = hitIdx;
}

bool KBSResultModel::GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx)
{
	if (gContextMenuHitChapter < 0 || gContextMenuHitChapter >= static_cast<int32>(gChapters.size()))
		return false;
	if (gContextMenuHit < 0 || gContextMenuHit >= static_cast<int32>(gChapters[gContextMenuHitChapter].hits.size()))
		return false;
	outChapterIdx = gContextMenuHitChapter;
	outHitIdx = gContextMenuHit;
	return true;
}

KBSResultModel::ChangeOutcome KBSResultModel::GetHitOutcome(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return kOutcomeNone;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return kOutcomeNone;
	return c.hits[hitIdx].outcome;
}

void KBSResultModel::SetHitChangeTexts(int32 chapterIdx, int32 hitIdx, const PMString& originalText,
	const PMString& replacedText)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);
	h.originalText = originalText;	h.originalText.SetTranslatable(kFalse);
	h.replacedText = replacedText;	h.replacedText.SetTranslatable(kFalse);
}

bool KBSResultModel::GetHitChangeTexts(int32 chapterIdx, int32 hitIdx, PMString& outOriginalText,
	PMString& outReplacedText)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	if (!h.replaced && h.outcome != kOutcomeRejected)
		return false;
	outOriginalText = h.originalText;
	outReplacedText = h.replacedText;
	return true;
}

void KBSResultModel::SetHitRejected(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);
	h.storyUID = storyUID;
	h.textStart = start;
	h.textEnd = end;
	h.replaced = false;
	h.checked = false;
	h.outcome = kOutcomeRejected;
	BuildHitLocator(h);
}

void KBSResultModel::SetHitAccepted(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);
	// Still replaced - the text it wrote stays - but its records are gone: nothing is left to find by
	// that time, so it offers neither Reject Change nor Accept Change again (both look the row up by it).
	h.replaced = true;
	h.checked = false;
	h.recordTime = 0;
	h.outcome = kOutcomeAccepted;
	BuildHitLocator(h);
}

void KBSResultModel::SetHitDeleted(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);
	h.textStart = kInvalidTextIndex;
	h.textEnd = kInvalidTextIndex;
	h.replaced = true;
	h.checked = false;
	h.outcome = kOutcomeDeleted;
	BuildHitLocator(h);
}

bool KBSResultModel::GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	outDocRef = c.docRef;
	outFile = c.file;
	return true;
}

bool KBSResultModel::GetHitMatchIdentity(int32 chapterIdx, int32 hitIdx, UID& outStoryUID,
	TextIndex& outStart, TextIndex& outEnd, uint64& outHash)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outEnd = h.textEnd;
	outHash = h.matchHash;
	return true;
}

// (GetHitAnchor and GetHitStoryStamp were defined here until 2026-08-03 - see the note where they
// were declared in KBSResultModel.h.)

void KBSResultModel::MarkHitReplaced(int32 chapterIdx, int32 hitIdx, UID newStoryUID,
	TextIndex newStart, TextIndex newEnd)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);

	// The locator (page) is kept: a replacement does not move the line to another page in any
	// case worth chasing here - if the text reflowed that far, the result set is stale anyway and
	// the user is told to search again.
	//
	// The displayed segments are left as the search wrote them, on purpose: they are read back
	// when the chapter is done, by which time no later replacement can still change them.
	//
	// The STORY comes from the command as well - see the header. It is normally the story the row
	// already named; when it is not, the row is describing a match somewhere else and the range
	// alone would be read against the wrong text.
	h.storyUID = newStoryUID;
	h.textStart = newStart;
	h.textEnd = newEnd;
	h.replaced = true;
	h.checked = false;
	// a row taken back and replaced again (2026-09-27, A/B) is an ordinary replaced row once more
	// The locator follows at once where the replace changes what it says: a row taken back reads as an
	// ordinary replaced one again, and a footnote's row says "no track" (2026-09-29) - a row's Replace leaves a
	// work list, which no pass numbers again afterwards (only a Change Checked's report is - KeepCheckedRows).
	// Every other row's locator reads the same before and after, so it is not built again for nothing.
	const bool takenBack = (h.outcome == kOutcomeRejected);
	if (takenBack)
		h.outcome = kOutcomeNone;
	if (takenBack || h.inFootnote)
		BuildHitLocator(h);
}

// (GetHitReplacedRange stood here until 2026-09-25: the replace pass read a replaced row's range
//  back from the model to fetch its line. The range the model holds is where the text was WRITTEN,
//  and a later replacement in the same story can move it, so the pass now keeps the range itself,
//  carries it forward and hands the final one over - SetHitRange, below.)

void KBSResultModel::SetHitRange(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start,
	TextIndex end)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);
	h.storyUID = storyUID;
	h.textStart = start;
	h.textEnd = end;
}

void KBSResultModel::SetHitSegments(int32 chapterIdx, int32 hitIdx, const PMString& newPre,
	const PMString& newMatch, const PMString& newPost, uint64 newMatchHash)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];

	// Asked here too, whether or not MarkHitReplaced has already copied this row aside: BackUpRow
	// takes the first copy only, so nothing here relies on the earlier call having happened.
	BackUpRow(chapterIdx, hitIdx, h);

	h.preText = newPre;			h.preText.SetTranslatable(kFalse);
	h.matchText = newMatch;		h.matchText.SetTranslatable(kFalse);
	h.postText = newPost;		h.postText.SetTranslatable(kFalse);

	// ***** AND the hash, in the same call. ***** See the header for why the two cannot be set
	// apart from one another.
	h.matchHash = newMatchHash;
}

void KBSResultModel::BuildHitLocator(Hit& hit)
{
	hit.locator.Clear();
	hit.locator.SetTranslatable(kFalse);
	hit.accentFlag.Clear();
	hit.accentFlag.SetTranslatable(kFalse);

	if (hit.pageString.IsEmpty())
	{
		hit.locator.Append("overset");	// overset with nothing placed anywhere: no page to name
	}
	else
	{
		// An overset hit carries the "+" indicator's page and sorts by it; a trailing " overset"
		// (after the page and ordinal) marks it as overset -> e.g. "P1(2) overset".
		hit.locator.Append("P");
		hit.locator.Append(hit.pageString);
		if (hit.pageOrdinal > 0)
		{
			hit.locator.Append("(");
			hit.locator.AppendNumber(hit.pageOrdinal);
			hit.locator.Append(")");
		}
		if (hit.isOverset)
			hit.locator.Append(" overset");
	}

	// What the row cannot show any other way, each separated by a space, in this order:
	//   hidden  - on a switched-off layer, so the page will look empty on arrival
	//   locked  - locked, so the row carries no check box and the replace will not touch it
	//   missing - the same text could not be found where the search left it
	//   refused - InDesign's own replace command would not run there
	// The last two are put there by a replace, or by a jump that finds the text gone, so they never
	// appear on a fresh search's rows. They stack on either shape: "P1(2) overset hidden locked",
	// "overset missing", "P7 hidden".
	//
	// A space, not a "+": InDesign's own overset marker IS a "+", so "P5+locked" reads as "page 5,
	// overset". EVERY word is spelled out in full (user's call, 2026-08-04): these are what explain
	// a row the user cannot act on, so they are worth the characters. Clipped forms were tried and
	// dropped - "hid" / "lck" are hard to read, "loc" reads as "location" in English, and "ov" left
	// the one word a reader most needs to recognise as the least legible of the set.
	if (hit.isHidden)
		hit.locator.Append(" hidden");
	if (hit.isLocked || hit.outcome == kOutcomeLocked)
		hit.locator.Append(" locked");

	// NOT chained onto the test above. A locked row can be jumped to and found changed, and then it
	// has both things to say - "P4(1) locked missing" - where an else left it saying only that it
	// was locked, which is not why the jump landed on different text. Missing and refused do exclude
	// each other: outcome holds one value.
	//
	// These two go into their own string rather than onto the locator because the cell draws them
	// as a separate run in the theme's accent colour; the space in front of them belongs to that
	// run and is put there when it is drawn (KBSColorTextView).
	if (hit.outcome == kOutcomeMissing)
		hit.accentFlag.Append("missing");	// its own run, in the accent colour
	else if (hit.outcome == kOutcomeRefused)
		hit.accentFlag.Append("refused");	// same run, same colour: same kind of reason
	else if (hit.outcome == kOutcomeEndnoteLeft)
		hit.accentFlag.Append("not replaced");	// ticked and not written: the status line says why
	// A rejected row says nothing (the user, 2026-09-27: "no 'rejected' when I take one back") - it
	// reads its original text again, which is what the user asked for; the state is still there for
	// the menu (Redo); a reader of the panel sees the row's check box come back.
	// ***** EXCEPT ON A LIST REBUILT FROM THE RECORDS (2026-09-29, the design's section 4). ***** No row
	// there carries a box, so nothing else would tell a row taken back from one still replaced.
	else if (hit.outcome == kOutcomeRejected && gFromRecords)
		hit.locator.Append(" rejected");
	else if (hit.outcome == kOutcomeDeleted)
		hit.locator.Append(" deleted");		// gone with the object a ticked row deleted: what was asked for
	else if (hit.outcome == kOutcomeAccepted)
		hit.locator.Append(" accepted");	// its change accepted (2026-09-29): final, nothing left to act on

	// ***** "no track" - REPLACED INSIDE A FOOTNOTE (2026-09-29, the user's request). ***** Track Changes
	// records nothing in a footnote (measured 2026-09-26), so the replace there left no change to take back
	// or accept, and nothing on the row said so until now (the status line did, once, when it was replaced).
	// Normal colour, like "locked": a fact about the row, not a failure.
	if (hit.replaced && hit.inFootnote && hit.outcome == kOutcomeNone)
		hit.locator.Append(" no track");
}

void KBSResultModel::NumberHitsWithinPages(std::vector<Hit>& hits)
{
	size_t runStart = 0;
	while (runStart < hits.size())
	{
		size_t runEnd = runStart;
		while (runEnd < hits.size() && hits[runEnd].pageIndex == hits[runStart].pageIndex)
			++runEnd;
		const int32 runCount = static_cast<int32>(runEnd - runStart);
		for (size_t k = runStart; k < runEnd; ++k)
		{
			hits[k].pageOrdinal = (runCount > 1) ? (static_cast<int32>(k - runStart) + 1) : 0;
			BuildHitLocator(hits[k]);
		}
		runStart = runEnd;
	}
}

void KBSResultModel::SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	if (h.replaced)
		return;		// it WAS replaced - nothing went wrong with it
	BackUpRow(chapterIdx, hitIdx, h);
	h.outcome = outcome;
	h.checked = false;
	BuildHitLocator(h);
}

bool KBSResultModel::IsShowingReplaceOutcome()
{
	return gShowingOutcome;
}

void KBSResultModel::BeginRowBackup()
{
	gRowBackup.clear();
	gRowsBackedUp.clear();
	gBackingUpRows = true;
}

void KBSResultModel::RollBackRows()
{
	gBackingUpRows = false;

	// One copy per row (BackUpRow), so the order decides nothing now. (Backwards since the copies were
	// several per row, when the one taken FIRST had to be applied last.)
	for (size_t i = gRowBackup.size(); i > 0; --i)
	{
		const BackedUpRow& saved = gRowBackup[i - 1];
		if (saved.chapter < 0 || saved.chapter >= static_cast<int32>(gChapters.size()))
			continue;	// the result set changed underneath - nothing to put the row back into
		std::vector<Hit>& hits = gChapters[saved.chapter].hits;
		if (saved.hit < 0 || saved.hit >= static_cast<int32>(hits.size()))
			continue;
		hits[saved.hit] = saved.row;
	}

	// Swapping against a temporary releases the storage as well as the contents.
	std::vector<BackedUpRow>().swap(gRowBackup);
	gRowsBackedUp.clear();
}

void KBSResultModel::ForgetRowBackup()
{
	gBackingUpRows = false;
	std::vector<BackedUpRow>().swap(gRowBackup);
	gRowsBackedUp.clear();
}

// (DropChapter - erase one chapter and leave the others - was defined here until 2026-08-07. See
// the note where it was declared in KBSResultModel.h.)

int32 KBSResultModel::KeepCheckedRows()
{
	// A replace that was asked for nothing must not empty the panel, so check before touching
	// anything. A row counts as asked about when any of these hold:
	//   replaced - it was changed (its check was cleared when it was written)
	//   outcome  - it was reached and left alone, and says why (its check was cleared then too)
	//   checked  - still selected, so the run never reached it: a chapter that would not open, or
	//              a cancel. Those rows carry no reason, on purpose.
	//   isLocked - found by the search and never offerable. Kept so the list can account for a
	//              search that turned up more than the replace was allowed to touch.
	bool anyAsked = false;
	for (size_t ci = 0; ci < gChapters.size() && !anyAsked; ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (hits[hi].replaced || hits[hi].checked || hits[hi].isLocked
				|| hits[hi].outcome != kOutcomeNone)
			{
				anyAsked = true;
				break;
			}
		}
	}
	if (!anyAsked)
		return GetTotalHitCount();

	// Thin each chapter down to the rows the replace was asked about...
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		std::vector<Hit>& hits = gChapters[ci].hits;
		std::vector<Hit> keep;
		keep.reserve(hits.size());
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (!hits[hi].replaced && !hits[hi].checked && !hits[hi].isLocked
				&& hits[hi].outcome == kOutcomeNone)
				continue;
			// The source vector is thrown away at the swap below, so the hit is moved out rather
			// than copied - a Hit carries its texts as PMStrings.
			// (Its fontGroup / fontGroupPos were put back to -1 here until 2026-09-29, for a regroup
			// that left an ungrouped chapter's pair as it came. BuildFontGroups below writes every
			// hit's pair since every chapter is grouped by story, 2026-09-27.)
			keep.push_back(std::move(hits[hi]));
		}
		hits.swap(keep);

		// RENUMBER the within-page ordinals over what is left, so the rows read "the first
		// replacement on this page, the second, the third" (2026-08-03, user's call).
		//
		// The ordinal was CLEARED here until then, on the reasoning that thinning the list leaves the
		// search's numbers full of gaps - which left every row on a page reading a bare "P1", saying
		// nothing at all about which of them it was. Keeping the search's numbers was tried in
		// between; the user asked for the count to follow the REPLACEMENTS rather than the matches
		// they came from, which is this.
		//
		// The search's own numbering, over what is left: thinning preserves the page order the search
		// sorted into. The locators are rebuilt here rather than as each row was kept - the flags may
		// have changed too, and this is the one pass that has the final ordinal to bake in.
		NumberHitsWithinPages(hits);

		// ***** AND THE FONT GROUPS, because the thinning renumbered the hits they point AT. *****
		// A group holds POSITIONS in the chapter's hits vector (FontGroup::hitIndices), and every
		// hit holds the group it is in and its place inside it - all three of which were true of
		// the vector this pass has just replaced. Left alone, GetFontGroupHit would hand the tree
		// positions that name a different row or none at all, and KBSResultNodeID::Create(chapter,
		// hit) would stamp a stale group onto the node: two nodes naming one hit while carrying
		// different fonts, which is the one thing that header says must never happen.
		//
		// Reached on every Find/Change chapter since the story groups came in (2026-09-27).
		BuildFontGroups(gChapters[ci]);
	}

	// ...then drop the chapters left with nothing.
	std::vector<Chapter> remaining;
	remaining.reserve(gChapters.size());
	int32 kept = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		if (gChapters[ci].hits.empty())
			continue;
		kept += static_cast<int32>(gChapters[ci].hits.size());	// counted BEFORE the move
		remaining.push_back(std::move(gChapters[ci]));
	}
	gChapters.swap(remaining);

	// From here the panel is a report, not a work list: no row offers a check box.
	gShowingOutcome = true;
	return kept;
}

// End, KBSResultModel.cpp.
