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

	// Has a command been run since the results were last discarded? See KFCResultModel::NoteRun.
	// Deliberately NOT "are there any chapters": a search that found nothing has still been run.
	bool gHasRun = false;

	// Did the last command write with InDesign's Change All and change something? See KFCResultModel::NoteChangeAllWrote.
	bool gChangeAllWrote = false;

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
	// replaced row is written four times over - MarkHitReplaced, SetHitWrittenText, SetHitRange,
	// SetHitSegments - and a copy per change would only be overwritten unread.)
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

	// Which HIT row the result tree's right-click menu was popped over, for its menu (Replace) - KFCResultNodeEH
	// stashes it just before HandlePopupMenu. -1 = none. (The only row with a menu - spec F16.)
	int32 gContextMenuHitChapter = -1;
	int32 gContextMenuHit = -1;

	// Every right-click target forgotten - the chapters and rows they index have just gone (Clear,
	// RestoreModelSnapshot).
	void ForgetContextMenus()
	{
		gContextMenuHitChapter = -1;
		gContextMenuHit = -1;
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

	// GROUP A CHAPTER'S HITS BY STORY (the author's call) - the tree's middle level.
	// One group per story in first-appearance (page) order - so the stories read in the order their
	// first matches stand - and every hit given its group. The row reads "P<page of the story's first
	// match>  <the story's first words>". (The "font" in the names is the level's old name: it held the
	// fonts of Find Missing Glyphs, since removed; the level is the story's alone.)
	//
	// The groups are rebuilt from scratch, and every hit's fontGroup / fontGroupPos written, whatever
	// the hit held before.
	void BuildFontGroups(KFCResultModel::Chapter& chapter)
	{
		chapter.fontGroups.clear();
		// Each story's group, looked up rather than searched for: a chapter of 5000 hits in as many stories
		// would otherwise compare every hit with every group made before it (the speed-up study). The
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

	// Each story's hits numbered in the order their text stands (spec T14): a group's hits sorted by where they
	// started when the search found them. Not the hits' own order - that is page order, and the walk's order inside
	// a table is reading order (memory walker-scope-options-and-hidden-layers), neither of which is the text's.
	void NumberHitsWithinStories(KFCResultModel::Chapter& chapter)
	{
		for (size_t g = 0; g < chapter.fontGroups.size(); ++g)
		{
			std::vector<int32> order(chapter.fontGroups[g].hitIndices);
			std::stable_sort(order.begin(), order.end(), [&chapter](int32 a, int32 b)
				{ return chapter.hits[static_cast<size_t>(a)].textStart < chapter.hits[static_cast<size_t>(b)].textStart; });
			for (size_t k = 0; k < order.size(); ++k)
				chapter.hits[static_cast<size_t>(order[k])].storyOrdinal = static_cast<int32>(k);
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
	NumberHitsWithinStories(gChapters.back());
}

void KFCResultModel::Clear()
{
	gChapters.clear();
	gFromBook = false;
	gSearchScope = kScopeDocument;
	gBookName.Clear();
	gSearchMode = -1;
	gWalkSignature.Clear();
	// (Nothing outside this model describes these rows to forget here: the replace checks the stored
	//  positions against a fresh walk rather than fingerprinting each chapter.)
	// The right-click target is an index into the chapters that just went away - keeping it would let
	// the next list's Replace reach a row the user never right-clicked.
	ForgetContextMenus();
	// Discarding the results puts the panel back to the state it started in, illustration included.
	gHasRun = false;
	gChangeAllWrote = false;
	// A new result set, in its first layout: what KFCUndoFollow kept for the old one names
	// rows that are gone.
	gResultSetId = ++gIdCounter;
	gLayoutGeneration = ++gIdCounter;
}

uint32 KFCResultModel::GetResultSetId()
{
	return gResultSetId;
}

void KFCResultModel::ReturnToResultSet(uint32 resultSetId)
{
	gResultSetId = resultSetId;
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

void KFCResultModel::NoteChangeAllWrote(bool wrote)
{
	gChangeAllWrote = wrote;
}

bool KFCResultModel::HasChangeAllWritten()
{
	return gChangeAllWrote;
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
	if (gContextMenuHitChapter == chapterIdx)
	{
		gContextMenuHitChapter = -1;
		gContextMenuHit = -1;
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

bool KFCResultModel::IsWorkOutcome(ChangeOutcome outcome)
{
	return outcome == kOutcomeNone;
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

	// FOUND BY HALVING, NOT GROUP BY GROUP (the speed-up study). The tree asks this for EVERY child of a
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

int32 KFCResultModel::GetChapterReplacedCount(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return 0;
	int32 replaced = 0;
	for (size_t i = 0; i < c->hits.size(); ++i)
		if (c->hits[i].replaced)
			++replaced;
	return replaced;
}

int32 KFCResultModel::GetFontReplacedCount(int32 chapterIdx, int32 fontIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	const FontGroup* group = GroupAt(chapterIdx, fontIdx);
	if (c == nil || group == nil)
		return 0;
	int32 replaced = 0;
	for (size_t i = 0; i < group->hitIndices.size(); ++i)
	{
		const int32 h = group->hitIndices[i];
		if (h >= 0 && h < static_cast<int32>(c->hits.size()) && c->hits[static_cast<size_t>(h)].replaced)
			++replaced;
	}
	return replaced;
}

int32 KFCResultModel::GetTotalReplacedCount()
{
	int32 replaced = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
		replaced += GetChapterReplacedCount(static_cast<int32>(i));
	return replaced;
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
	out.replaced = h.replaced;
	out.locked = h.isLocked;
	out.outcome = h.outcome;
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

bool KFCResultModel::GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outReplaced, bool& outLocked)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	outReplaced = h != nil && h->replaced;
	outLocked = h != nil && h->isLocked;
	return h != nil;
}

int32 KFCResultModel::GetHitStoryOrdinal(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->storyOrdinal : -1;
}

bool KFCResultModel::GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	outLocked = h != nil && h->isLocked;
	outHidden = h != nil && h->isHidden;
	return h != nil;
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

void KFCResultModel::SetHitWrittenText(int32 chapterIdx, int32 hitIdx, const PMString& writtenText)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->replacedText = writtenText;
	h->replacedText.SetTranslatable(kFalse);
}

bool KFCResultModel::GetHitWrittenText(int32 chapterIdx, int32 hitIdx, PMString& outWrittenText)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || !h->replaced)
		return false;
	outWrittenText = h->replacedText;
	return true;
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
	// (The locator is not built again: nothing it says is changed by a replace - the page, the ordinal, overset,
	// hidden and locked come from the search, and an outcome is set where the replace sets one.)
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
	//     so the page will look empty on arrival), locked (its Replace is greyed; the replace will not touch it) -
	//     and, FIRST, Missing (the text is not where the search left it - see below);
	//   on accentFlag, drawn as a run of its own in the accent colour = refused (InDesign's own replace
	//     would not run there).
	// Only hidden and locked come from the search itself; the rest are put there later - by a replace
	// or a jump that finds the text gone. They stack on
	// either shape: "P1(2) overset hidden locked", "Missing overset", "P7 hidden".
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
	// has both things to say - "Missing P4(1) locked" - where an else would leave it saying only that
	// it was locked, which is not why the jump landed on different text. Missing and refused do exclude
	// each other: outcome holds one value.
	//
	// "Missing" FIRST, IN THE ROW'S OWN COLOUR (the author's call of 2026-10-08 - it was the last word, in the
	// accent colour): it stands where "Changed" stands on a replaced row (the UI half's ApplyHitRow puts that one
	// there), so a row's state is the first thing read on it, and the label a reader walks starts with it. The two
	// never meet: SetHitOutcome turns a replaced row away.
	// "refused" stays a run of its own in the theme's accent colour; the space in front of it belongs to that run
	// and is put there when it is drawn (KFCColorTextView).
	if (hit.outcome == kOutcomeMissing)
	{
		PMString lead("Missing ");
		lead.SetTranslatable(kFalse);
		lead.Append(hit.locator);
		hit.locator = lead;
	}
	else if (hit.outcome == kOutcomeRefused)
		hit.accentFlag.Append("refused");	// its own run, in the accent colour
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
	BuildHitLocator(h);
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
	out.layout = gLayoutGeneration;
	out.fromBook = gFromBook;
	out.searchScope = gSearchScope;
	out.bookName = gBookName;
	out.searchMode = gSearchMode;
	out.walkSignature = gWalkSignature;
	out.hasRun = gHasRun;
	out.changeAllWrote = gChangeAllWrote;
}

void KFCResultModel::RestoreModelSnapshot(const ModelSnapshot& snapshot)
{
	gChapters = snapshot.chapters;
	gLayoutGeneration = snapshot.layout;
	gFromBook = snapshot.fromBook;
	gSearchScope = snapshot.searchScope;
	gBookName = snapshot.bookName;
	gSearchMode = snapshot.searchMode;
	gWalkSignature = snapshot.walkSignature;
	gHasRun = snapshot.hasRun;
	gChangeAllWrote = snapshot.changeAllWrote;
	// The right-click targets index the chapters and rows that were just replaced (Clear's reason).
	ForgetContextMenus();
	ForgetRowBackup();
}

// End, KFCResultModel.cpp.
