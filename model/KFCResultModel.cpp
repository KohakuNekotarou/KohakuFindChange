//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Result model implementation. See KFCResultModel.h for the contract. All state is file-static, in
//  the anonymous namespace below - the vector of chapters, the flags recorded beside it, the row
//  backup, the right-click targets and the result-set / layout ids; the getters are bounds-checked
//  so a repaint racing a rebuild (or a stale node id) reads "nothing" rather than crashing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "TextChar.h"	// kTextChar_CR / kTextChar_LF / kTextChar_PilchrowSign and the hidden markers - see MarkUpBreaksForDisplay

#include <algorithm>	// std::lower_bound - where the display cap falls inside one font group; std::partition_point - the groups it leaves shown; std::sort
#include <set>			// the rows already copied aside - see BackUpRow
#include <utility>		// std::move - the thinning below hands whole hits over instead of copying

// Project includes:
#include "KFCDiag.h"	// KFC_DIAG_LOG - a row's outcome as it is set, in a test build (compiled out of a shipping one)
#include "KFCResultModel.h"

namespace
{
	std::vector<KFCResultModel::Chapter> gChapters;

	// Were these results produced by a book search? Decides whether the tree opens its chapters.
	bool gFromBook = false;

	// The Search: they were searched with. See KFCResultModel::SetSearchScope.
	KFCResultModel::SearchScopeKind gSearchScope = KFCResultModel::kScopeDocument;

	// The book those results came from (file name only). Drawn on the tree's book row.
	PMString gBookName;

	// The Find/Change tab the search ran in (an IFindChangeOptions::SearchMode value; -1 = nothing
	// searched yet). See KFCResultModel::SetSearchMode for why the replace has to compare against it.
	int32 gSearchMode = -1;

	// The whole of what those results were WALKED by - query plus every switch that decides the match
	// set - as one opaque key. See KFCResultModel::SetWalkSignature.
	PMString gWalkSignature;

	// Is the panel showing a replace's aftermath rather than a search's results? See
	// KFCResultModel::IsShowingReplaceOutcome.
	bool gShowingOutcome = false;

	// Has a command been run since the results were last discarded? See KFCResultModel::NoteRun.
	// Deliberately NOT "are there any chapters": a search that found nothing has still been run.
	bool gHasRun = false;

	// The rows copied aside before a write of ours changed them - see KFCResultModel::BeginRowBackup.
	// RowCopy, the same struct the panel's following of Undo hands out.
	//
	// Only true while a write of ours is running; empty at every other moment.
	bool gBackingUpRows = false;
	std::vector<KFCResultModel::RowCopy> gRowBackup;
	std::set<std::pair<int32, int32> > gRowsBackedUp;	// (chapter, hit) already in gRowBackup
	// ...and the story versions it recorded (SetStoryVersion), the same way: an Undo of the
	// write puts the story back at the version before it, and the record has to follow or the next
	// write refuses the story as "changed since the search".
	std::vector<KFCResultModel::VersionCopy> gVersionBackup;
	std::set<std::pair<int32, UID> > gVersionsBackedUp;	// (chapter, story) already in gVersionBackup

	// Which result set and which layout of it the row indices name - KFCResultModel::GetResultSetId /
	// GetLayoutGeneration. Both are drawn from one counter, so no two values ever repeat.
	uint32 gIdCounter = 0;
	uint32 gResultSetId = 0;
	uint32 gLayoutGeneration = 0;

	// Copy a row aside before it is first written to, if a replace is running - ONCE per row: the copy
	// taken first is the row as the run found it, and it is the one a rollback has to put back. (A
	// replaced row is written four or five times over - MarkHitReplaced, SetHitRecord, SetHitChangeTexts,
	// SetHitRange, SetHitSegments - and a copy per change would only be overwritten unread.)
	void BackUpRow(int32 chapterIdx, int32 hitIdx, const KFCResultModel::Hit& row)
	{
		if (!gBackingUpRows || !gRowsBackedUp.insert(std::make_pair(chapterIdx, hitIdx)).second)
			return;
		KFCResultModel::RowCopy saved;
		saved.chapter = chapterIdx;
		saved.hit = hitIdx;
		saved.row = row;
		gRowBackup.push_back(saved);
	}


	// Which row the result tree's right-click menu was popped over (KFCResultNodeEH stashes it just
	// before HandlePopupMenu; Check All / Uncheck All read it back). See the header for the two
	// negative values it can hold.
	int32 gContextMenuChapter = KFCResultModel::kNoContextMenuChapter;
	// ...and which HIT row, for the hit row's own menu (Replace, Reject Change, Accept Change). -1 = none.
	int32 gContextMenuHitChapter = -1;
	int32 gContextMenuHit = -1;
	int32 gContextMenuGroupChapter = -1;	// the story row right-clicked
	int32 gContextMenuGroup = -1;

	// Every right-click target forgotten - the chapters and rows they index have just gone (Clear,
	// RestoreModelSnapshot).
	void ForgetContextMenus()
	{
		gContextMenuChapter = KFCResultModel::kNoContextMenuChapter;
		gContextMenuHitChapter = -1;
		gContextMenuHit = -1;
		gContextMenuGroupChapter = -1;
		gContextMenuGroup = -1;
	}

	// THE INDEXES, ASKED IN ONE PLACE. A chapter, a row, a story group - or nil when an index
	// is out of range, which is how a repaint racing a rebuild (or a stale node id) reads "nothing" rather
	// than crashing. Every getter and setter below starts here.
	KFCResultModel::Chapter* ChapterAt(int32 chapterIdx)
	{
		return (chapterIdx >= 0 && chapterIdx < static_cast<int32>(gChapters.size())) ? &gChapters[chapterIdx] : nil;
	}

	KFCResultModel::Hit* HitAt(int32 chapterIdx, int32 hitIdx)
	{
		KFCResultModel::Chapter* c = ChapterAt(chapterIdx);
		return (c != nil && hitIdx >= 0 && hitIdx < static_cast<int32>(c->hits.size())) ? &c->hits[hitIdx] : nil;
	}

	// A replaced row whose replace changed no character, so nothing was recorded for it - the one rule.
	// See KFCResultModel::GetHitTextUnchanged. A row taken back, accepted
	// or gone with its object has an outcome, and a footnote's row is said as such.
	bool TextUnchanged(const KFCResultModel::Hit& h)
	{
		return h.replaced && !h.inFootnote && h.recordTime == 0 && h.outcome == KFCResultModel::kOutcomeNone
			&& h.originalText == h.replacedText;
	}

	KFCResultModel::FontGroup* GroupAt(int32 chapterIdx, int32 groupIdx)
	{
		KFCResultModel::Chapter* c = ChapterAt(chapterIdx);
		return (c != nil && groupIdx >= 0 && groupIdx < static_cast<int32>(c->fontGroups.size())) ? &c->fontGroups[groupIdx] : nil;
	}

	// A chapter's recorded version of one story as it stands - `had` false when none is recorded.
	KFCResultModel::VersionCopy VersionNow(int32 chapterIdx, UID story)
	{
		KFCResultModel::VersionCopy v;
		v.chapter = chapterIdx;
		v.story = story;
		if (const KFCResultModel::Chapter* c = ChapterAt(chapterIdx))
		{
			const std::map<UID, uint32>::const_iterator it = c->storyVersions.find(story);
			if (it != c->storyVersions.end())
			{
				v.had = true;
				v.version = it->second;
			}
		}
		return v;
	}

	// Write a copied version back: recorded again, or taken off when there was none.
	void PutVersionBack(const KFCResultModel::VersionCopy& v)
	{
		KFCResultModel::Chapter* c = ChapterAt(v.chapter);
		if (c == nil)
			return;		// the result set changed underneath - nothing to put it back into
		if (v.had)
			c->storyVersions[v.story] = v.version;
		else
			c->storyVersions.erase(v.story);
	}

	// Does this row carry a check box? THE one definition of the question, so the commands that set
	// the boxes and the counts that decide whether to offer those commands cannot drift apart:
	// a row the model quietly checked but the panel drew no box for would be replaced without ever
	// having been asked for. Replaced = the text it matched is gone; locked = InDesign offers no way
	// to change it; an outcome already says why it was left alone.
	bool RowHasCheckBox(const KFCResultModel::Hit& hit)
	{
		// A replace's report offers work only on the rows Reject Change put back (the author's call). This
		// is the ROW's half of NoRowHasCheckBox, and it is the whole of it for a row: over a report, a row
		// taken back and still open is exactly what makes that question answer no, and every other row
		// is refused here. (NoRowHasCheckBox is not asked here as well - an answer that cannot change this
		// one, and a walk of every row per row: squared in the rows over a report. The callers that loop
		// ask it once, as their early exit.)
		if (KFCResultModel::IsShowingReplaceOutcome() && hit.outcome != KFCResultModel::kOutcomeRejected)
			return false;

		return !hit.replaced && !hit.isLocked && KFCResultModel::IsWorkOutcome(hit.outcome);
	}

	// Change Checked's work - see KFCResultModel::IsHitCheckedWork. Ticked and carrying a box: the box's own
	// question (RowHasCheckBox) - not "not replaced, and a work outcome", which says yes over a report to
	// the ticked rows of a chapter the run could not open, rows that have no box there.
	bool IsCheckedWork(const KFCResultModel::Hit& hit)
	{
		return hit.checked && RowHasCheckBox(hit);
	}

	// One chapter's rows, for the whole-model and the one-chapter versions of the same question
	// (SetAllChecked / SetChapterChecked, GetCheckedCount / GetChapterCheckedCount, GetCheckableCount /
	// GetChapterCheckableCount). The rows that carry no check box are not ticked by Check All either -
	// otherwise the model would hold checked hits the panel shows no box for.
	void SetBoxesIn(std::vector<KFCResultModel::Hit>& hits, bool checked)
	{
		for (size_t hi = 0; hi < hits.size(); ++hi)
			if (RowHasCheckBox(hits[hi]))
				hits[hi].checked = checked;
	}

	int32 CountBoxesIn(const std::vector<KFCResultModel::Hit>& hits)
	{
		int32 count = 0;
		for (size_t hi = 0; hi < hits.size(); ++hi)
			if (RowHasCheckBox(hits[hi]))
				++count;
		return count;
	}

	// Checked and still waiting: a REPLACED row does not count, because it is no longer waiting to be done.
	int32 CountCheckedWorkIn(const std::vector<KFCResultModel::Hit>& hits)
	{
		int32 count = 0;
		for (size_t hi = 0; hi < hits.size(); ++hi)
			if (IsCheckedWork(hits[hi]))
				++count;
		return count;
	}

	// GROUP A CHAPTER'S HITS BY STORY (the author's call) - the tree's middle level.
	// One group per story in first-appearance (page) order - so the stories read in the order their
	// first matches stand - and every hit given its group. The row reads "P<page of the story's first
	// match>  <the story's first words>". (The "font" in the names is the level's old name: it held the
	// fonts of Find Missing Glyphs, since removed; the level is the story's alone.)
	//
	// The groups are rebuilt from scratch, and every hit's fontGroup / fontGroupPos written, whatever
	// the hit held before - a search's new hits and the hits KeepCheckedRows carries over alike.
	void BuildFontGroups(KFCResultModel::Chapter& chapter)
	{
		chapter.fontGroups.clear();
		// Each story's group, looked up rather than searched for: a chapter of 5000 hits in as many stories
		// would otherwise compare every hit with every group made before it (2026-10-05, the speed-up study). The
		// groups are still made in first-appearance order - the map only finds them.
		std::map<UID, int32> groupOf;
		for (size_t i = 0; i < chapter.hits.size(); ++i)
		{
			KFCResultModel::Hit& hit = chapter.hits[i];
			int32 found = -1;
			const UID key = hit.storyUID;
			const std::map<UID, int32>::const_iterator known = groupOf.find(key);
			if (known != groupOf.end())
				found = known->second;
			if (found < 0)
			{
				KFCResultModel::FontGroup group;
				group.story = hit.storyUID;
				group.fontName = hit.pageString.IsEmpty() ? PMString("overset") : PMString("P");
				if (!hit.pageString.IsEmpty())
					group.fontName.Append(hit.pageString);
				group.fontName.Append("  ");
				// The story's first words keep their breaks (KFCSearchEngine's StoryLeadText):
				// drawn as the marks a hit row draws, by the same function.
				PMString lead(hit.storyLead);
				KFCResultModel::MarkUpBreaksForDisplay(lead);
				group.fontName.Append(lead);
				group.fontName.SetTranslatable(kFalse);
				chapter.fontGroups.push_back(group);
				found = static_cast<int32>(chapter.fontGroups.size()) - 1;
				groupOf[key] = found;
			}
			KFCResultModel::FontGroup& group = chapter.fontGroups[found];
			hit.fontGroup = found;
			hit.fontGroupPos = static_cast<int32>(group.hitIndices.size());
			group.hitIndices.push_back(static_cast<int32>(i));
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

void KFCResultModel::AppendChapter(Chapter&& chapter)
{
	// THE HITS ARE TAKEN, NOT COPIED. A chapter of a large search holds thousands of Hits and each Hit
	// holds its texts as PMStrings, so copying the vector in here would double the cost of filling the
	// model for nothing: every caller builds a Chapter, hands it over and drops it.
	gChapters.push_back(std::move(chapter));
	// Grouped on the way in, on the chapter the model now owns: the groups index the hits they are
	// built from, so they have to be built where those hits are going to live.
	BuildFontGroups(gChapters.back());

	// EVERY ROW STARTS UNTICKED (the author's call). The replace writes one match at a time, so the user
	// ticks what is to be replaced. (Hit::checked is false as a Hit is built - nothing to do here.)
}

void KFCResultModel::Clear()
{
	gChapters.clear();
	gShowingOutcome = false;
	gFromBook = false;
	gSearchScope = kScopeDocument;
	gBookName.Clear();
	gSearchMode = -1;
	gWalkSignature.Clear();
	// (Nothing outside this model describes these rows to forget here: the replace checks the stored
	//  positions against a fresh walk rather than fingerprinting each chapter.)
	// The right-click target is an index into the chapters that just went away - keeping it would let
	// the next search's Check All reach a chapter the user never right-clicked.
	ForgetContextMenus();
	// Discarding the results puts the panel back to the state it started in, illustration included.
	gHasRun = false;
	// A new result set, in its first layout: what KFCUndoFollow kept for the old one names
	// rows that are gone.
	gResultSetId = ++gIdCounter;
	gLayoutGeneration = ++gIdCounter;
}

uint32 KFCResultModel::GetResultSetId()
{
	return gResultSetId;
}

uint32 KFCResultModel::GetLayoutGeneration()
{
	return gLayoutGeneration;
}

void KFCResultModel::SetFromBook(bool fromBook)
{
	gFromBook = fromBook;
}

void KFCResultModel::NoteRun()
{
	gHasRun = true;
}

bool KFCResultModel::HasRun()
{
	return gHasRun;
}

bool KFCResultModel::IsFromBook()
{
	return gFromBook;
}

void KFCResultModel::SetSearchScope(SearchScopeKind scope)
{
	gSearchScope = scope;
}

KFCResultModel::SearchScopeKind KFCResultModel::GetSearchScope()
{
	return gSearchScope;
}

void KFCResultModel::CloseChapter(int32 chapterIdx)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return;
	// Emptied and unbound, in place (see the header for why the place is kept).
	EmptyChapter(*c);
	// A right-click target inside it names rows that are gone.
	if (gContextMenuChapter == chapterIdx)
		gContextMenuChapter = kNoContextMenuChapter;
	if (gContextMenuHitChapter == chapterIdx)
	{
		gContextMenuHitChapter = -1;
		gContextMenuHit = -1;
	}
	if (gContextMenuGroupChapter == chapterIdx)
	{
		gContextMenuGroupChapter = -1;
		gContextMenuGroup = -1;
	}
}

void KFCResultModel::EmptyChapter(Chapter& chapter)
{
	// Swapped with fresh vectors rather than cleared, so the rows' strings are released now rather than at
	// the next search.
	std::vector<Hit>().swap(chapter.hits);
	std::vector<FontGroup>().swap(chapter.fontGroups);
	chapter.storyVersions.clear();
	chapter.docRef = UIDRef(nil, kInvalidUID);
	chapter.file = IDFile();
}

int32 KFCResultModel::GetShownChapter(int32 nth)
{
	if (nth < 0)
		return -1;
	// GetDisplayChapterCount's walk, naming the chapter it reaches
	int32 before = 0;
	int32 shown = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
	{
		if (before >= kKFCDisplayHitLimit)
			break;
		if (gChapters[i].hits.empty())
			continue;
		if (shown == nth)
			return static_cast<int32>(i);
		++shown;
		before += static_cast<int32>(gChapters[i].hits.size());
	}
	return -1;
}

int32 KFCResultModel::GetShownChapterPos(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil || c->hits.empty())
		return -1;
	int32 pos = 0;
	for (int32 i = 0; i < chapterIdx; ++i)
		if (!gChapters[i].hits.empty())
			++pos;
	return (pos < GetDisplayChapterCount()) ? pos : -1;
}

bool KFCResultModel::NoRowHasCheckBox()
{
	// gShowingOutcome rather than IsShowingReplaceOutcome() only because this file owns the flag.
	// The two are the same question - see the header for why both halves have to be asked.
	// EXCEPT A REPORT HOLDING A ROW TAKEN BACK (the author's call): that row carries a box.
	return gShowingOutcome && !KFCResultModel::AnyRejectedRowOpen();
}

bool KFCResultModel::IsWorkOutcome(ChangeOutcome outcome)
{
	return outcome == kOutcomeNone || outcome == kOutcomeRejected;
}

bool KFCResultModel::IsHitCheckedWork(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return h != nil && IsCheckedWork(*h);
}

bool KFCResultModel::AnyRejectedRowOpen()
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

void KFCResultModel::SetSearchMode(int32 mode)
{
	gSearchMode = mode;
}

int32 KFCResultModel::GetSearchMode()
{
	return gSearchMode;
}

void KFCResultModel::SetWalkSignature(const PMString& signature)
{
	gWalkSignature = signature;
	gWalkSignature.SetTranslatable(kFalse);
}

PMString KFCResultModel::GetWalkSignature()
{
	PMString signature(gWalkSignature);
	signature.SetTranslatable(kFalse);
	return signature;
}

void KFCResultModel::SetBookName(const PMString& name)
{
	gBookName = name;
	gBookName.SetTranslatable(kFalse);
}

PMString KFCResultModel::GetBookName()
{
	PMString name(gBookName);
	name.SetTranslatable(kFalse);
	return name;
}

void KFCResultModel::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do (the KESCL ShutdownCleanup rule).
	gChapters = std::vector<Chapter>();

	// The static PMStrings, emptied for the same reason the vectors are: nothing of ours should
	// still be holding storage when the DLL unloads (the KESCL ShutdownCleanup rule).
	//
	// ALL of them. A static string added above without a line here is left holding storage at unload
	// (it has happened once). When a static is added above, it is added here too - that is what this
	// list is.
	gBookName.Clear();
	gWalkSignature.Clear();

	// Normally already empty - a replace clears it on both of its exits - but a shutdown during
	// one would leave copies behind, and these hold PMStrings like the chapters do.
	ForgetRowBackup();
}

int32 KFCResultModel::GetChapterCount()
{
	return static_cast<int32>(gChapters.size());
}

int32 KFCResultModel::GetHitCount(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	return (c != nil) ? static_cast<int32>(c->hits.size()) : 0;
}

int32 KFCResultModel::GetTotalHitCount()
{
	int32 total = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
		total += static_cast<int32>(gChapters[i].hits.size());
	return total;
}

int32 KFCResultModel::GetDisplayChapterCount()
{
	// The displayed chapters are the book-order prefix whose hits fit under the cap: a chapter is
	// shown when the hits before it have not already used up the whole budget.
	int32 before = 0;
	int32 shown = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
	{
		if (before >= kKFCDisplayHitLimit)
			break;
		if (gChapters[i].hits.empty())
			continue;	// a chapter CloseChapter emptied - kept in place, not shown
		++shown;
		before += static_cast<int32>(gChapters[i].hits.size());
	}
	return shown;
}

int32 KFCResultModel::GetDisplayHitCount(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return 0;
	const int32 before = HitsBeforeChapter(chapterIdx);
	if (before >= kKFCDisplayHitLimit)
		return 0;	// the cap ran out before this chapter
	const int32 remaining = kKFCDisplayHitLimit - before;
	const int32 full = static_cast<int32>(c->hits.size());
	return (full < remaining) ? full : remaining;
}

bool KFCResultModel::GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return false;
	outName = c->name;
	outName.SetTranslatable(kFalse);
	outHitCount = static_cast<int32>(c->hits.size());
	return true;
}

int32 KFCResultModel::GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx)
{
	const FontGroup* group = GroupAt(chapterIdx, fontIdx);
	if (group == nil)
		return 0;

	// The chapter's own share of the cap, then this group's share of that. hitIndices is ascending,
	// so the count is simply where the cap falls inside it.
	const int32 shown = GetDisplayHitCount(chapterIdx);
	const std::vector<int32>& idx = group->hitIndices;
	return static_cast<int32>(std::lower_bound(idx.begin(), idx.end(), shown) - idx.begin());
}

int32 KFCResultModel::GetDisplayFontCount(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return 0;

	// FOUND BY HALVING, NOT GROUP BY GROUP (2026-10-05, the speed-up study). The tree asks this for EVERY child of a
	// document row (KFCResultListAdapter::GetNthChild) and for every story row the rebuild opens, so counting group by
	// group cost the groups squared per rebuild - 25 million group counts for a document of 5000 one-hit stories.
	//
	// A group shows while its FIRST hit is under the cap (its hitIndices ascend - GetDisplayFontHitCount). BuildFontGroups
	// makes a group at the first hit of its story, scanning the hits in order, so every group holds a hit and the groups'
	// first hits ascend with the group index: the groups that show are a PREFIX, and its length is the first group whose
	// first hit is not under the cap.
	const int32 shown = GetDisplayHitCount(chapterIdx);
	const std::vector<FontGroup>& groups = c->fontGroups;
	const std::vector<FontGroup>::const_iterator end = std::partition_point(groups.begin(), groups.end(),
		[shown](const FontGroup& group) { return !group.hitIndices.empty() && group.hitIndices.front() < shown; });
	return static_cast<int32>(end - groups.begin());
}

bool KFCResultModel::IsStoryGroup(int32 chapterIdx, int32 groupIdx)
{
	// Every group is a story group - so this is the index's range.
	return GroupAt(chapterIdx, groupIdx) != nil;
}

void KFCResultModel::GetGroupHits(int32 chapterIdx, int32 groupIdx, std::vector<int32>& outHits)
{
	outHits.clear();
	if (const FontGroup* group = GroupAt(chapterIdx, groupIdx))
		outHits = group->hitIndices;
}

int32 KFCResultModel::GetGroupCheckedCount(int32 chapterIdx, int32 groupIdx)
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

void KFCResultModel::SetGroupChecked(int32 chapterIdx, int32 groupIdx, bool checked)
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

int32 KFCResultModel::GetGroupCheckableCount(int32 chapterIdx, int32 groupIdx)
{
	// The rows SetGroupChecked above would set, counted the same way.
	if (NoRowHasCheckBox())
		return 0;
	std::vector<int32> rows;
	GetGroupHits(chapterIdx, groupIdx, rows);
	int32 count = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (RowHasCheckBox(gChapters[chapterIdx].hits[rows[k]]))
			++count;
	}
	return count;
}

void KFCResultModel::SetContextMenuGroup(int32 chapterIdx, int32 groupIdx)
{
	gContextMenuGroupChapter = chapterIdx;
	gContextMenuGroup = groupIdx;
}

bool KFCResultModel::GetContextMenuGroup(int32& outChapterIdx, int32& outGroupIdx)
{
	if (!IsStoryGroup(gContextMenuGroupChapter, gContextMenuGroup))
		return false;
	outChapterIdx = gContextMenuGroupChapter;
	outGroupIdx = gContextMenuGroup;
	return true;
}


bool KFCResultModel::GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount)
{
	const FontGroup* group = GroupAt(chapterIdx, fontIdx);
	if (group == nil)
		return false;
	outName = group->fontName;			// the story row's text - see BuildFontGroups
	outName.SetTranslatable(kFalse);
	outHitCount = static_cast<int32>(group->hitIndices.size());
	return true;
}

int32 KFCResultModel::GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth)
{
	const FontGroup* group = GroupAt(chapterIdx, fontIdx);
	if (group == nil || nth < 0 || nth >= static_cast<int32>(group->hitIndices.size()))
		return -1;
	return group->hitIndices[nth];
}

int32 KFCResultModel::GetHitFontGroup(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->fontGroup : -1;
}

int32 KFCResultModel::GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->fontGroupPos : -1;
}

bool KFCResultModel::GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out)
{
	const Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return false;
	const Hit& h = *hp;
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
	// outside this file has to add them up into this answer.
	out.hasCheckBox = RowHasCheckBox(h);
	return true;
}

// U+21B5 DOWNWARDS ARROW WITH CORNER LEFTWARDS - the mark for a forced line break. TextChar.h names
// the pilcrow (kTextChar_PilchrowSign, :122) but carries no constant for this one, so it is named
// here rather than left as a bare number in the loop below.
static const UTF32TextChar kKFCReturnArrow = 0x21B5;

// THE CHARACTERS AN OBJECT STANDS ON ARE NOT SHOWN (the author's call). They have
// no glyph in the panel's font and drew as a box: a footnote / endnote reference (0x04 / 0x05), the
// marks around an endnote's text and other anchors (U+FEFF), a table's anchor and continuation
// (0x16 / 0x17), the page number and section markers (0x18 / 0x19), an anchored object (U+FFFC).
// Display only, like the break marks: the model keeps them as they are.
// EXCEPT THE TABLE'S ANCHOR - it is shown as kKFCTableSign (the author: "the way KCM does it, a table
// sign"). Its continuations (one per row after the first) are still dropped: a table is one sign however
// many rows it has.
// By TextChar.h's own names (0x18 is also kTextChar_AutoText there - the same code).
static bool IsHiddenMarker(UTF16TextChar c)
{
	return c == kTextChar_FootnoteMarker || c == kTextChar_EndnoteMarker || c == kTextChar_TableContinued
		|| c == kTextChar_PageNumber || c == kTextChar_SectionName
		|| c == kTextChar_ZeroSpaceNoBreak || c == kTextChar_ObjectReplacementCharacter;
}

// THE SIGN A TABLE LEAVES IN A ROW (the author: "can it be like KCM - a table mark between the two
// characters either side of the table"). U+25A6 SQUARE WITH ORTHOGONAL CROSSHATCH FILL,
// KCM's kKCMTableSign (KCMStoryList.cpp), and for KCM's reasons: NOT the kanji U+7530 the user remembered -
// KCM's user turned it down there ("that is Japanese": the sign has to read the same to an English
// reader) - and not U+229E, which in the palette font reads like InDesign's overset box. It stands where
// the anchor (kTextChar_Table) stands - a<sign>b, no gap of its own, the anchor IS the place. A code
// point, not a literal: this file is plain ASCII, and the sign is not in CP932 anyway.
static const UTF32TextChar kKFCTableSign = 0x25A6;

// See KFCResultModel.h for what this is for and why it is DISPLAY ONLY.
//
// The two marks are the ones InDesign itself draws with Show Hidden Characters on: a pilcrow for a
// paragraph end, a return arrow for a forced line break (Shift+Enter). They are two different things
// to a replace, so a row spells them differently.
//
// Whole runs are copied between the marks rather than one character at a time, so a surrogate pair
// is never split. Most strings hold no break at all, so the string is scanned once before anything
// is built: the common row pays one pass and no allocation.
void KFCResultModel::MarkUpBreaksForDisplay(PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* buf = s.GrabUTF16Buffer(&n);
	if (buf == nil || n <= 0)
		return;

	bool16 any = kFalse;
	for (int32 i = 0; i < n && !any; ++i)
		any = (buf[i] == kTextChar_CR || buf[i] == kTextChar_LF || buf[i] == kTextChar_Table
			|| IsHiddenMarker(buf[i]));
	if (!any)
		return;

	PMString out;
	out.SetTranslatable(kFalse);
	int32 runStart = 0;
	for (int32 i = 0; i < n; ++i)
	{
		const bool marker = IsHiddenMarker(buf[i]);
		if (!marker && buf[i] != kTextChar_CR && buf[i] != kTextChar_LF && buf[i] != kTextChar_Table)
			continue;
		if (i > runStart)
			out.AppendW(buf + runStart, i - runStart);
		if (!marker)
			out.AppendW(buf[i] == kTextChar_CR ? static_cast<UTF32TextChar>(kTextChar_PilchrowSign)
				: buf[i] == kTextChar_LF ? kKFCReturnArrow
				: kKFCTableSign);
		runStart = i + 1;
	}
	if (n > runStart)
		out.AppendW(buf + runStart, n - runStart);

	s = out;
	s.SetTranslatable(kFalse);
}


bool KFCResultModel::GetHitDisplay(int32 chapterIdx, int32 hitIdx,
	PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return false;
	outLocator = h->locator;
	outPre = h->preText;
	outMatch = h->matchText;
	outPost = h->postText;
	return true;
}

bool KFCResultModel::GetHitLocation(int32 chapterIdx, int32 hitIdx,
	UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return false;
	const Chapter& c = gChapters[chapterIdx];
	outDocRef = c.docRef;
	outFile = c.file;
	outStoryUID = h->storyUID;
	outStart = h->textStart;
	outEnd = h->textEnd;
	return true;
}

void KFCResultModel::RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef)
{
	if (Chapter* c = ChapterAt(chapterIdx))
		c->docRef = newDocRef;
}

bool KFCResultModel::GetStoryVersion(int32 chapterIdx, UID story, uint32& outVersion)
{
	const VersionCopy v = VersionNow(chapterIdx, story);
	if (!v.had)
		return false;
	outVersion = v.version;
	return true;
}

void KFCResultModel::SetStoryVersion(int32 chapterIdx, UID story, uint32 version)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return;
	// Copied aside like a row, once, as the write found it.
	if (gBackingUpRows && gVersionsBackedUp.insert(std::make_pair(chapterIdx, story)).second)
		gVersionBackup.push_back(VersionNow(chapterIdx, story));
	c->storyVersions[story] = version;
}

void KFCResultModel::GetChapterStories(int32 chapterIdx, std::set<UID>& outStories)
{
	outStories.clear();
	if (const Chapter* c = ChapterAt(chapterIdx))
		for (size_t hi = 0; hi < c->hits.size(); ++hi)
			if (c->hits[hi].storyUID != kInvalidUID)
				outStories.insert(c->hits[hi].storyUID);
}

void KFCResultModel::SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return;
	// The same question the panel asks before it draws a box, asked here so the model can never hold
	// a checked hit that no row offered - a replace's report above all, where only the rows taken back
	// carry a box.
	//
	// ONE ROW AT A TIME. The replace writes only the ticked matches, so every box is the row's own:
	// touching matches do not go on and off together, and a footnote's row can be left out. (Reject
	// Change still takes a touching group together - GetTouchingGroup.)
	if (RowHasCheckBox(*h))
		h->checked = checked;
}

uint64 KFCResultModel::GetHitRecordTime(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->recordTime : 0;
}

int32 KFCResultModel::GetHitRecordLead(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->recordLead : 0;
}

void KFCResultModel::SetHitRecord(int32 chapterIdx, int32 hitIdx, uint64 time, int32 lead)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->recordTime = time;
	h->recordLead = lead;
}

bool KFCResultModel::GetHitTextUnchanged(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return h != nil && TextUnchanged(*h);
}

void KFCResultModel::GetTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outHits)
{
	outHits.clear();
	const Hit* mep = HitAt(chapterIdx, hitIdx);
	if (mep == nil)
		return;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	const Hit& me = *mep;
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

bool KFCResultModel::GetHitInFootnote(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return h != nil && h->inFootnote;
}

bool KFCResultModel::GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	outChecked = h != nil && h->checked;
	outReplaced = h != nil && h->replaced;
	outLocked = h != nil && h->isLocked;
	return h != nil;
}

bool KFCResultModel::GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	outLocked = h != nil && h->isLocked;
	outHidden = h != nil && h->isHidden;
	return h != nil;
}

void KFCResultModel::SetAllChecked(bool checked)
{
	// Nothing on a list that offers no work is selectable. A short cut, not a second opinion: the
	// per-row test below asks the same question, and this only saves walking every hit to be told
	// so once per row.
	if (NoRowHasCheckBox())
		return;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
		SetBoxesIn(gChapters[ci].hits, checked);
}

void KFCResultModel::SetChapterChecked(int32 chapterIdx, bool checked)
{
	if (NoRowHasCheckBox())
		return;		// the same short cut SetAllChecked takes, over one chapter
	if (Chapter* c = ChapterAt(chapterIdx))
		SetBoxesIn(c->hits, checked);
}

int32 KFCResultModel::GetCheckedCount()
{
	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
		count += CountCheckedWorkIn(gChapters[ci].hits);
	return count;
}

int32 KFCResultModel::GetChapterCheckedCount(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	return (c != nil) ? CountCheckedWorkIn(c->hits) : 0;
}

int32 KFCResultModel::GetCheckableCount()
{
	if (NoRowHasCheckBox())
		return 0;	// no row of this list has a box, so Check All / Uncheck All grey out
	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
		count += CountBoxesIn(gChapters[ci].hits);
	return count;
}

int32 KFCResultModel::GetChapterCheckableCount(int32 chapterIdx)
{
	if (NoRowHasCheckBox())
		return 0;	// no row has a box, whichever chapter the menu was popped over
	const Chapter* c = ChapterAt(chapterIdx);
	return (c != nil) ? CountBoxesIn(c->hits) : 0;
}

void KFCResultModel::SetContextMenuChapter(int32 chapterIdx)
{
	gContextMenuChapter = chapterIdx;
}

int32 KFCResultModel::GetContextMenuChapter()
{
	return gContextMenuChapter;
}

void KFCResultModel::SetContextMenuHit(int32 chapterIdx, int32 hitIdx)
{
	gContextMenuHitChapter = chapterIdx;
	gContextMenuHit = hitIdx;
}

bool KFCResultModel::GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx)
{
	if (HitAt(gContextMenuHitChapter, gContextMenuHit) == nil)
		return false;
	outChapterIdx = gContextMenuHitChapter;
	outHitIdx = gContextMenuHit;
	return true;
}

KFCResultModel::ChangeOutcome KFCResultModel::GetHitOutcome(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->outcome : kOutcomeNone;
}

void KFCResultModel::SetHitChangeTexts(int32 chapterIdx, int32 hitIdx, const PMString& originalText,
	const PMString& replacedText)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->originalText = originalText;	h->originalText.SetTranslatable(kFalse);
	h->replacedText = replacedText;	h->replacedText.SetTranslatable(kFalse);
}

bool KFCResultModel::GetHitChangeTexts(int32 chapterIdx, int32 hitIdx, PMString& outOriginalText,
	PMString& outReplacedText)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || (!h->replaced && h->outcome != kOutcomeRejected))
		return false;
	outOriginalText = h->originalText;
	outReplacedText = h->replacedText;
	return true;
}

void KFCResultModel::SetHitRejected(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return;
	Hit& h = *hp;
	BackUpRow(chapterIdx, hitIdx, h);
	h.storyUID = storyUID;
	h.textStart = start;
	h.textEnd = end;
	h.replaced = false;
	h.checked = false;
	h.outcome = kOutcomeRejected;
	BuildHitLocator(h);
}

void KFCResultModel::SetHitDeleted(int32 chapterIdx, int32 hitIdx)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return;
	Hit& h = *hp;
	BackUpRow(chapterIdx, hitIdx, h);
	h.textStart = kInvalidTextIndex;
	h.textEnd = kInvalidTextIndex;
	h.replaced = true;
	h.checked = false;
	h.outcome = kOutcomeDeleted;
	BuildHitLocator(h);
}

bool KFCResultModel::GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return false;
	outDocRef = c->docRef;
	outFile = c->file;
	return true;
}

bool KFCResultModel::GetHitMatchIdentity(int32 chapterIdx, int32 hitIdx, UID& outStoryUID,
	TextIndex& outStart, TextIndex& outEnd, uint64& outHash)
{
	const Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return false;
	const Hit& h = *hp;
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outEnd = h.textEnd;
	outHash = h.matchHash;
	return true;
}

void KFCResultModel::MarkHitReplaced(int32 chapterIdx, int32 hitIdx, UID newStoryUID,
	TextIndex newStart, TextIndex newEnd)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return;
	Hit& h = *hp;
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
	// a row taken back and replaced again is an ordinary replaced row once more
	// The locator follows at once where the replace changes what it says: a row taken back reads as an
	// ordinary replaced one again, and a footnote's row says "no track" - a row's Replace leaves a work
	// list, which no pass numbers again afterwards (only a Change Checked's report is - KeepCheckedRows).
	// So does a row whose replace changed no character (TextUnchanged - the replace sets the row's record
	// and texts before this, for it). Every other row's locator reads the same before and after, so it is
	// not built again for nothing.
	const bool takenBack = (h.outcome == kOutcomeRejected);
	if (takenBack)
		h.outcome = kOutcomeNone;
	if (takenBack || h.inFootnote || TextUnchanged(h))
		BuildHitLocator(h);
}

// (No getter hands a replaced row's range back to the replace pass: the range the model holds is
//  where the text was WRITTEN, and a later replacement in the same story can move it, so the pass
//  keeps the range itself, carries it forward and hands the final one over - SetHitRange, below.)

void KFCResultModel::SetHitRange(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start,
	TextIndex end)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return;
	Hit& h = *hp;
	BackUpRow(chapterIdx, hitIdx, h);
	h.storyUID = storyUID;
	h.textStart = start;
	h.textEnd = end;
}

void KFCResultModel::SetHitSegments(int32 chapterIdx, int32 hitIdx, const PMString& newPre,
	const PMString& newMatch, const PMString& newPost, uint64 newMatchHash)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return;
	Hit& h = *hp;

	// Asked here too, whether or not MarkHitReplaced has already copied this row aside: BackUpRow
	// takes the first copy only, so nothing here relies on the earlier call having happened.
	BackUpRow(chapterIdx, hitIdx, h);

	h.preText = newPre;			h.preText.SetTranslatable(kFalse);
	h.matchText = newMatch;		h.matchText.SetTranslatable(kFalse);
	h.postText = newPost;		h.postText.SetTranslatable(kFalse);

	// AND the hash, in the same call. See the header for why the two cannot be set
	// apart from one another.
	h.matchHash = newMatchHash;
}

void KFCResultModel::BuildHitLocator(Hit& hit)
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
	// What the row cannot show any other way, each separated by a space. The tests below ARE the list -
	// two kinds of word, kept in two strings:
	//   on the locator, in the row's own colour = facts about the row: hidden (on a switched-off layer,
	//     so the page will look empty on arrival), locked (no check box; the replace will not touch it),
	//     and what has happened to it since: rejected (on a list rebuilt from the records only),
	//     deleted, no track;
	//   on accentFlag, drawn as a run of its own in the accent colour = why a row could not be acted
	//     on: missing (the text is not where the search left it), refused (InDesign's own replace
	//     would not run there), not replaced.
	// Only hidden and locked come from the search itself; the rest are put there later - by a replace,
	// a jump that finds the text gone, or the records (a query run's list). They stack on
	// either shape: "P1(2) overset hidden locked", "overset missing", "P7 hidden".
	//
	// A space, not a "+": InDesign's own overset marker IS a "+", so "P5+locked" reads as "page 5,
	// overset". EVERY word is spelled out in full (the author's call): these are what explain
	// a row the user cannot act on, so they are worth the characters. Clipped forms were tried and
	// dropped - "hid" / "lck" are hard to read, "loc" reads as "location" in English, and "ov" left
	// the one word a reader most needs to recognise as the least legible of the set.
	if (hit.isHidden)
		hit.locator.Append(" hidden");
	if (hit.isLocked || hit.outcome == kOutcomeLocked)
		hit.locator.Append(" locked");

	// NOT chained onto the test above. A locked row can be jumped to and found changed, and then it
	// has both things to say - "P4(1) locked missing" - where an else would leave it saying only that
	// it was locked, which is not why the jump landed on different text. Missing and refused do exclude
	// each other: outcome holds one value.
	//
	// These two go into their own string rather than onto the locator because the cell draws them
	// as a separate run in the theme's accent colour; the space in front of them belongs to that
	// run and is put there when it is drawn (KFCColorTextView).
	if (hit.outcome == kOutcomeMissing)
		hit.accentFlag.Append("missing");	// its own run, in the accent colour
	else if (hit.outcome == kOutcomeRefused)
		hit.accentFlag.Append("refused");	// same run, same colour: same kind of reason
	else if (hit.outcome == kOutcomeEndnoteLeft)
		hit.accentFlag.Append("not replaced");	// ticked and not written: the status line says why
	// A rejected row says nothing (the author: "no 'rejected' when I take one back") - it reads its
	// original text again, which is what the author asked for; the state is still there for the story
	// and document rows' Replace Again (Redo in the code); a reader of the panel sees the row's check
	// box come back.
	else if (hit.outcome == kOutcomeDeleted)
		hit.locator.Append(" deleted");		// gone with the object a ticked row deleted: what was asked for

	// "no track" - REPLACED INSIDE A FOOTNOTE (the user's request). Track Changes records nothing in a
	// footnote (measured), so the replace there left no change to take back or accept, and only the
	// row can keep saying so (the status line says it once, when it is replaced).
	// Normal colour, like "locked": a fact about the row, not a failure.
	// ...and a replace that changed no character (TextUnchanged): formatting only, if anything, which
	// Track Changes does not record either.
	if ((hit.replaced && hit.inFootnote && hit.outcome == kOutcomeNone) || TextUnchanged(hit))
		hit.locator.Append(" no track");
}

void KFCResultModel::NumberHitsWithinPages(std::vector<Hit>& hits)
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

void KFCResultModel::SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	KFC_DIAG_LOG("OUTCOME chapter=%d row=%d outcome=%d%s", chapterIdx, hitIdx, (int)outcome,
		(hp == nil) ? " (no such row)" : (hp->replaced ? " (replaced - not set)" : ""));
	if (hp == nil)
		return;
	Hit& h = *hp;
	if (h.replaced)
		return;		// it WAS replaced - nothing went wrong with it
	BackUpRow(chapterIdx, hitIdx, h);
	h.outcome = outcome;
	h.checked = false;
	BuildHitLocator(h);
}

bool KFCResultModel::IsShowingReplaceOutcome()
{
	return gShowingOutcome;
}

void KFCResultModel::BeginRowBackup()
{
	gRowBackup.clear();
	gRowsBackedUp.clear();
	gVersionBackup.clear();
	gVersionsBackedUp.clear();
	gBackingUpRows = true;
}

void KFCResultModel::RollBackRows()
{
	gBackingUpRows = false;

	// One copy per row (BackUpRow), so the direction of this loop decides nothing.
	for (size_t i = gRowBackup.size(); i > 0; --i)
	{
		const RowCopy& saved = gRowBackup[i - 1];
		if (Hit* h = HitAt(saved.chapter, saved.hit))	// nil: the result set changed underneath
			*h = saved.row;
	}
	// The recorded versions too - every caller records them only once its write has gone
	// through, so there is normally nothing here.
	for (size_t i = 0; i < gVersionBackup.size(); ++i)
		PutVersionBack(gVersionBackup[i]);

	// Swapping against a temporary releases the storage as well as the contents.
	std::vector<RowCopy>().swap(gRowBackup);
	gRowsBackedUp.clear();
	std::vector<VersionCopy>().swap(gVersionBackup);
	gVersionsBackedUp.clear();
}

void KFCResultModel::ForgetRowBackup()
{
	gBackingUpRows = false;
	std::vector<RowCopy>().swap(gRowBackup);
	gRowsBackedUp.clear();
	std::vector<VersionCopy>().swap(gVersionBackup);
	gVersionsBackedUp.clear();
}

void KFCResultModel::TakeRowBackup(RowStep& out)
{
	gBackingUpRows = false;
	out = RowStep();
	// "before" is the copy BackUpRow took as the write first changed the row; "after" is the row now.
	out.before.swap(gRowBackup);
	out.after.reserve(out.before.size());
	for (size_t i = 0; i < out.before.size(); ++i)
	{
		const RowCopy& was = out.before[i];
		const Hit* h = HitAt(was.chapter, was.hit);
		if (h == nil)
			continue;
		RowCopy now;
		now.chapter = was.chapter;
		now.hit = was.hit;
		now.row = *h;
		out.after.push_back(now);
	}
	out.versionsBefore.swap(gVersionBackup);
	for (size_t i = 0; i < out.versionsBefore.size(); ++i)
		out.versionsAfter.push_back(VersionNow(out.versionsBefore[i].chapter, out.versionsBefore[i].story));
	gRowsBackedUp.clear();
	gVersionsBackedUp.clear();
}

void KFCResultModel::ApplyRowStep(const RowStep& step, bool after)
{
	const std::vector<RowCopy>& rows = after ? step.after : step.before;
	for (size_t i = 0; i < rows.size(); ++i)
		if (Hit* h = HitAt(rows[i].chapter, rows[i].hit))
			*h = rows[i].row;
	const std::vector<VersionCopy>& versions = after ? step.versionsAfter : step.versionsBefore;
	for (size_t i = 0; i < versions.size(); ++i)
		PutVersionBack(versions[i]);
}

void KFCResultModel::TakeModelSnapshot(ModelSnapshot& out)
{
	out.chapters = gChapters;
	out.showingOutcome = gShowingOutcome;
	out.layout = gLayoutGeneration;
	out.fromBook = gFromBook;
	out.searchScope = gSearchScope;
	out.bookName = gBookName;
	out.searchMode = gSearchMode;
	out.walkSignature = gWalkSignature;
	out.hasRun = gHasRun;
}

void KFCResultModel::RestoreModelSnapshot(const ModelSnapshot& snapshot)
{
	gChapters = snapshot.chapters;
	gShowingOutcome = snapshot.showingOutcome;
	gLayoutGeneration = snapshot.layout;
	gFromBook = snapshot.fromBook;
	gSearchScope = snapshot.searchScope;
	gBookName = snapshot.bookName;
	gSearchMode = snapshot.searchMode;
	gWalkSignature = snapshot.walkSignature;
	gHasRun = snapshot.hasRun;
	// The right-click targets index the chapters and rows that were just replaced (Clear's reason).
	ForgetContextMenus();
	ForgetRowBackup();
}

int32 KFCResultModel::KeepCheckedRows()
{
	// A replace that was asked for nothing must not empty the panel, so check before touching
	// anything. A row counts as asked about when any of these hold:
	//   replaced - it was changed (its check was cleared when it was written)
	//   outcome  - it says something about itself: this run reached it and left it alone (its check was
	//              cleared then too), or something before the run did - Reject Change took it back, a
	//              jump found it missing - ticked or not
	//   checked  - still selected, so the run never reached it: a chapter that would not open. Those
	//              rows carry no reason, on purpose. (A cancel never gets here - the run's cancel exit
	//              rolls every row back and leaves the search's results; KFCReplaceEngine.cpp.)
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
			// than copied - a Hit carries its texts as PMStrings. (Its fontGroup / fontGroupPos are
			// left as they were: BuildFontGroups below writes every hit's pair.)
			keep.push_back(std::move(hits[hi]));
		}
		hits.swap(keep);

		// RENUMBER the within-page ordinals over what is left, so the rows read "the first
		// replacement on this page, the second, the third" (the author's call: the count follows the
		// REPLACEMENTS rather than the matches they came from).
		//
		// Neither of the other two: clearing the ordinal leaves every row on a page reading a bare
		// "P1", saying nothing about which of them it is; keeping the search's numbers leaves them
		// full of gaps. Both were tried.
		//
		// The search's own numbering, over what is left: thinning preserves the page order the search
		// sorted into. The locators are rebuilt here rather than as each row was kept - the flags may
		// have changed too, and this is the one pass that has the final ordinal to bake in.
		NumberHitsWithinPages(hits);

		// AND THE STORY GROUPS, because the thinning renumbered the hits they point AT.
		// A group holds POSITIONS in the chapter's hits vector (FontGroup::hitIndices), and every
		// hit holds the group it is in and its place inside it - all three of which were true of
		// the vector this pass has just replaced. Left alone, GetFontGroupHit would hand the tree
		// positions that name a different row or none at all, and KFCResultNodeID::Create(chapter,
		// hit) would stamp a stale group onto the node: two nodes naming one hit while carrying
		// different groups, which is the one thing that header says must never happen.
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

	// From here the panel is a report, not a work list: no row offers a check box but one taken back
	// (RowHasCheckBox).
	gShowingOutcome = true;
	// ...with its rows numbered again: an index taken before this names another row now.
	gLayoutGeneration = ++gIdCounter;
	return kept;
}

// End, KFCResultModel.cpp.
