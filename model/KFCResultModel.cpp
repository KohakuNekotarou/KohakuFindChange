//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Result model implementation. See KFCResultModel.h for the contract. All state is file-static, in
//  the anonymous namespace below - the vector of chapters, the list's header beside it, the row
//  backup and the result-set / layout ids; the getters are bounds-checked so a repaint racing a
//  rebuild (or a stale node id) reads "nothing" rather than crashing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "TextChar.h"	// kTextChar_CR / kTextChar_LF / kTextChar_PilchrowSign and the hidden markers - see MarkUpBreaksForDisplay

#include <algorithm>	// std::stable_sort - the page order and each story's order (OrderHitsByPage, NumberHitsWithinStories)
#include <set>			// the rows already copied aside - see BackUpRow
#include <iterator>		// std::make_move_iterator - ReplaceStoryRows hands the walked story's hits over
#include <utility>		// std::move - chapters and hits handed over instead of copied (AppendChapter, ReplaceStoryRows)

// Project includes:
#include "KFCDiag.h"	// KFC_DIAG_LOG / KFC_DIAG_FAULT - a row's outcome as it is set, and the fault switches, in a test
						// build (compiled out of a shipping one)
#include "KFCResultModel.h"
#include "KFCRowFoci.h"	// the rows' text foci - attached where a chapter is bound, let go with the rows

namespace
{
	std::vector<KFCResultModel::Chapter> gChapters;

	// What the list says about itself beside its chapters - book or not, the Search:, the tab, the walk's key, run or
	// not, Change All or not (KFCResultModel::ListHeader: each field's setter is named beside it). Taken whole.
	KFCResultModel::ListHeader gHeader;

	// The rows copied aside before a write of ours changed them - see KFCResultModel::BeginRowBackup.
	// RowCopy, the same struct the panel's following of Undo hands out.
	//
	// ONE STRUCT, as the list's header is (ListHeader): starting it and forgetting it each take all of it in one
	// statement (BeginRowBackup, ForgetRowBackup), and so does handing it over once its copies are out (TakeRowBackup) -
	// a container added here goes with them. On only while a write of ours is running; off and empty at every other
	// moment.
	struct RowBackup
	{
		bool										on;
		std::vector<KFCResultModel::RowCopy>		rows;
		std::set<std::pair<int32, int32> >			rowsCopied;		// (chapter, hit) already in rows
		// ...and the story versions it recorded (SetStoryVersion), the same way: an Undo of the
		// write puts the story back at the version before it, and the record has to follow or the next
		// write refuses the story as "changed since the search".
		std::vector<KFCResultModel::VersionCopy>	versions;
		std::set<std::pair<int32, UID> >			versionsCopied;	// (chapter, story) already in versions
		RowBackup() : on(false) {}
	};
	RowBackup gBackup;

	// Which result set and which layout of it the row indices name - KFCResultModel::GetResultSetId /
	// GetLayoutGeneration. Both are drawn from one counter, so no two values ever repeat.
	uint32 gIdCounter = 0;
	uint32 gResultSetId = 0;
	uint32 gLayoutGeneration = 0;

	// Copy a row aside before it is first written to, if a replace is running - ONCE per row: the copy
	// taken first is the row as the run found it, and it is the one a rollback has to put back. (A
	// replaced row is written several times over - MarkHitReplaced, SetHitWrittenText, SetHitRange,
	// SetHitSegments, SetStoryLead - and a copy per change would only be overwritten unread.)
	void BackUpRow(int32 chapterIdx, int32 hitIdx, const KFCResultModel::Hit& row)
	{
		if (!gBackup.on || !gBackup.rowsCopied.insert(std::make_pair(chapterIdx, hitIdx)).second)
			return;
		KFCResultModel::RowCopy saved;
		saved.chapter = chapterIdx;
		saved.hit = hitIdx;
		saved.row = row;
		gBackup.rows.push_back(saved);
	}

	// Stop remembering and release the copies - the rows they name have been put back, or are gone: a rollback's end
	// (RollBackRows), a list laid out again (ReplaceStoryRows, ReplaceChapterRows, RestoreModelSnapshot) and the
	// shutdown. Not public: a write that commits hands its copies to its Undo instead (TakeRowBackup), and one that
	// forgot them would leave its Undo nothing to put back.
	void ForgetRowBackup()
	{
		gBackup = RowBackup();	// a fresh one in its place: off, and the storage goes too, not just the contents
	}

	// Does the tree show this chapter? Rows, or searched again with none (Chapter::shownEmpty) - the one question
	// GetDisplayChapterCount, GetShownChapter and GetShownChapterPos ask.
	bool ChapterShown(const KFCResultModel::Chapter& c)
	{
		return !c.hits.empty() || c.shownEmpty;
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
	// first matches stand - and every hit given its group. The row reads "ID<the story's UID>: <the story's
	// first words>" (the author: a story can run through threaded frames, so the page of its first match - "P2"
	// for a story that begins on page 1, measured - does not name it). The UID is the number the scripting DOM
	// gives the story (Story.id). (The "font" in
	// the names is the level's old name: it held the fonts of Find Missing Glyphs, since removed; the level is
	// the story's alone.)
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
			// AN OBJECT ROW has no story: it hangs off its document row directly (the adapter's branch for a
			// chapter with no groups, KFCResultListAdapter.cpp).
			if (hit.itemUID != kInvalidUID)
			{
				hit.fontGroup = -1;
				hit.fontGroupPos = -1;
				continue;
			}
			int32 found = -1;
			const UID key = hit.storyUID;
			const std::map<UID, int32>::const_iterator known = groupOf.find(key);
			if (known != groupOf.end())
				found = known->second;
			if (found < 0)
			{
				KFCResultModel::FontGroup group;
				group.story = hit.storyUID;
				chapter.fontGroups.push_back(group);	// its row's text is made as it is read - GetFontDisplay
				found = static_cast<int32>(chapter.fontGroups.size()) - 1;
				groupOf[key] = found;
			}
			KFCResultModel::FontGroup& group = chapter.fontGroups[found];
			hit.fontGroup = found;
			hit.fontGroupPos = static_cast<int32>(group.hitIndices.size());
			group.hitIndices.push_back(static_cast<int32>(i));
		}
	}

	// Each story's hits numbered in the order their text stands (the text-focus spec's T14 - its path is at
	// Hit::storyOrdinal in the header): a group's hits sorted by where they started when the search found them. Not
	// the hits' own order - that is page order, and the walk's order inside a table is reading order (memory
	// walker-scope-options-and-hidden-layers), neither of which is the text's.
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

	// WHAT A CHAPTER'S ROWS ARE INDEXED BY, built from the rows as they stand: the story groups (each row's group and its
	// place in it) and each story's order. Both in one call, on every road that puts rows in or lays them out again
	// (AppendChapter, RowsLaidOutAgain), so no road builds the one and leaves the other naming rows that have moved.
	void IndexRows(KFCResultModel::Chapter& chapter)
	{
		BuildFontGroups(chapter);
		NumberHitsWithinStories(chapter);
	}

	// A CHAPTER'S ROWS LAID OUT AGAIN (ReplaceStoryRows, ReplaceChapterRows - each lets the chapter's foci go before its
	// rows move): indexed again, and whatever names the rows by their old places let go - a kept write (a new layout
	// generation: KFCUndoFollow drops a write whose layout is not the list's, UNDO-14) and the row backup - with the
	// chapter's foci back on the rows as they stand now. The one list of those, for both.
	void RowsLaidOutAgain(int32 chapterIdx, KFCResultModel::Chapter& chapter)
	{
		IndexRows(chapter);
		gLayoutGeneration = ++gIdCounter;
		ForgetRowBackup();
		KFCRowFoci::AttachChapter(chapterIdx);
	}
}

void KFCResultModel::AppendChapter(Chapter&& chapter)
{
	// THE HITS ARE TAKEN, NOT COPIED. A chapter of a large search holds thousands of Hits and each Hit
	// holds its texts as PMStrings, so copying the vector in here would double the cost of filling the
	// model for nothing: every caller builds a Chapter, hands it over and drops it.
	gChapters.push_back(std::move(chapter));
	// Indexed on the way in, on the chapter the model now owns: the groups index the hits they are
	// built from, so they have to be built where those hits are going to live.
	IndexRows(gChapters.back());
}

void KFCResultModel::Clear()
{
	gChapters.clear();
	// A fresh header: discarding the results puts the panel back to the state it started in, illustration included
	// (HasRun, HasChangeAllWritten).
	gHeader = ListHeader();
	// The rows' text foci go with them (KFCRowFoci - the one thing outside this model that describes these rows; the
	// replace still checks the stored positions against a fresh walk rather than fingerprinting each chapter).
	KFCRowFoci::DetachAll();
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
	gHeader.fromBook = fromBook;
}

void KFCResultModel::NoteRun()
{
	gHeader.hasRun = true;
}

bool KFCResultModel::HasRun()
{
	return gHeader.hasRun;
}

void KFCResultModel::NoteChangeAllWrote(bool wrote)
{
	gHeader.changeAllWrote = wrote;
}

bool KFCResultModel::HasChangeAllWritten()
{
	return gHeader.changeAllWrote;
}

bool KFCResultModel::IsFromBook()
{
	return gHeader.fromBook;
}

void KFCResultModel::SetSearchScope(SearchScopeKind scope)
{
	gHeader.searchScope = scope;
}

KFCResultModel::SearchScopeKind KFCResultModel::GetSearchScope()
{
	return gHeader.searchScope;
}

void KFCResultModel::SetSearchLayer(const PMString& layer)
{
	gHeader.searchLayer = layer;
	gHeader.searchLayer.SetTranslatable(kFalse);
}

PMString KFCResultModel::GetSearchLayer()
{
	PMString layer(gHeader.searchLayer);
	layer.SetTranslatable(kFalse);
	return layer;
}

void KFCResultModel::CloseChapter(int32 chapterIdx, const IDFile* closedFile)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return;
	// Its rows' text foci go first - their document is still open here (the close responder's kBeforeCloseDoc).
	KFCRowFoci::DetachChapter(chapterIdx);
	// Emptied and unbound, in place (see the header for why the place is kept).
	EmptyChapter(*c, closedFile);
}

void KFCResultModel::EmptyChapter(Chapter& chapter, const IDFile* closedFile)
{
	// A FRESH CHAPTER IN ITS PLACE, ITS NAME KEPT: rows, story groups, story versions, searched parts, binding (no docRef,
	// no file) and the "shown empty" mark - a closed document's chapter is not shown - all back to nothing in one
	// statement, so a field added to Chapter is emptied here without a line of its own. (Emptied field by field, the
	// searched parts were once left behind, describing rows that had gone.) The old storage goes now, rather than at
	// the next search.
	// ...AND THE FILE ITS DOCUMENT WAS CLOSED FROM (Chapter::closedFile - nil keeps what an earlier close left): the
	// heading's Search These Results Again finds the document open again by it, and nothing else reads it.
	const PMString name(chapter.name);
	const IDFile was(closedFile != nil ? *closedFile : chapter.closedFile);
	chapter = Chapter();
	chapter.name = name;
	chapter.closedFile = was;
}

int32 KFCResultModel::GetShownChapter(int32 nth)
{
	if (nth < 0)
		return -1;
	// GetDisplayChapterCount's walk, naming the chapter it reaches
	int32 shown = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
	{
		if (!ChapterShown(gChapters[i]))
			continue;
		if (shown == nth)
			return static_cast<int32>(i);
		++shown;
	}
	return -1;
}

int32 KFCResultModel::GetShownChapterPos(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil || !ChapterShown(*c))
		return -1;
	int32 pos = 0;
	for (int32 i = 0; i < chapterIdx; ++i)
		if (ChapterShown(gChapters[i]))
			++pos;
	return pos;
}

bool KFCResultModel::IsWorkOutcome(ChangeOutcome outcome)
{
	return outcome == kOutcomeNone;
}

void KFCResultModel::SetSearchMode(int32 mode)
{
	gHeader.searchMode = mode;
}

int32 KFCResultModel::GetSearchMode()
{
	return gHeader.searchMode;
}

void KFCResultModel::SetWalkSignature(const PMString& signature)
{
	gHeader.walkSignature = signature;
	gHeader.walkSignature.SetTranslatable(kFalse);
}

PMString KFCResultModel::GetWalkSignature()
{
	PMString signature(gHeader.walkSignature);
	signature.SetTranslatable(kFalse);
	return signature;
}

void KFCResultModel::SetQueryCopy(const std::shared_ptr<const KFCQueryCopy>& copy)
{
	gHeader.query = copy;
}

std::shared_ptr<const KFCQueryCopy> KFCResultModel::GetQueryCopy()
{
	return gHeader.query;
}

void KFCResultModel::SetBookName(const PMString& name)
{
	gHeader.bookName = name;
	gHeader.bookName.SetTranslatable(kFalse);
}

PMString KFCResultModel::GetBookName()
{
	PMString name(gHeader.bookName);
	name.SetTranslatable(kFalse);
	return name;
}

void KFCResultModel::ShutdownCleanup()
{
	// The rows' text foci first - nothing of ours may still hold a reference into a document at unload.
	KFCRowFoci::DetachAll();

	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do (the KESCL ShutdownCleanup rule).
	gChapters = std::vector<Chapter>();

	// The static strings, emptied for the same reason the vectors are: nothing of ours should still be holding
	// storage when the DLL unloads (the KESCL ShutdownCleanup rule). Every one of them is a field of the list's
	// header, so one statement empties them all - a field added to ListHeader goes with it (a string added beside
	// the header without a line here was once left holding storage at unload).
	gHeader = ListHeader();

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
	int32 shown = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
		if (ChapterShown(gChapters[i]))		// not a chapter CloseChapter emptied - kept in place, not shown
			++shown;
	return shown;
}

int32 KFCResultModel::GetDisplayHitCount(int32 chapterIdx)
{
	return GetHitCount(chapterIdx);
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
	return (group != nil) ? static_cast<int32>(group->hitIndices.size()) : 0;
}

int32 KFCResultModel::GetDisplayFontCount(int32 chapterIdx)
{
	// Every group: BuildFontGroups makes one at the first hit of its story, so each holds a row. One number, not a walk -
	// the tree asks it for every child of a document row (KFCResultListAdapter::GetNthChild) and for every story row a
	// rebuild opens.
	const Chapter* c = ChapterAt(chapterIdx);
	return (c != nil) ? static_cast<int32>(c->fontGroups.size()) : 0;
}

bool KFCResultModel::IsStoryGroup(int32 chapterIdx, int32 groupIdx)
{
	// Every group is a story group - so this is the index's range.
	return GroupAt(chapterIdx, groupIdx) != nil;
}

UID KFCResultModel::GetGroupStory(int32 chapterIdx, int32 groupIdx)
{
	const FontGroup* group = GroupAt(chapterIdx, groupIdx);
	return (group != nil) ? group->story : kInvalidUID;
}

bool KFCResultModel::GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount)
{
	const FontGroup* group = GroupAt(chapterIdx, fontIdx);
	if (group == nil)
		return false;
	// THE STORY ROW'S TEXT - "ID260: first words..." - made from its rows' storyLead NOW, not kept beside them (the
	// author's call): a Replace reads the story's first words into its rows again (SetStoryLead), and a rollback, an
	// Undo or a Redo puts the rows back with theirs, so the row says what the story says. No space after "ID" (the
	// author's call, as an object row reads "ID259:<name>").
	outName = PMString("ID");
	outName.AppendNumber(static_cast<int32>(group->story.Get()));
	outName.Append(": ");
	const Hit* first = group->hitIndices.empty() ? nil : HitAt(chapterIdx, group->hitIndices[0]);
	if (first != nil)
	{
		// The story's first words keep their breaks (KFCPageItemFacts::StoryLeadText): drawn as the marks a hit row
		// draws, by the same function.
		PMString lead(first->storyLead);
		MarkUpBreaksForDisplay(lead);
		outName.Append(lead);
	}
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
	const FontGroup* group = GroupAt(chapterIdx, fontIdx);
	if (group == nil)
		return 0;
	int32 replaced = 0;
	for (size_t i = 0; i < group->hitIndices.size(); ++i)
	{
		const Hit* h = HitAt(chapterIdx, group->hitIndices[i]);
		if (h != nil && h->replaced)
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
	out.outlined = h.outlined;
	out.kept = h.kept;
	out.special = h.special;
	return true;
}

bool KFCResultModel::SetHitKept(int32 chapterIdx, int32 hitIdx, bool kept)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || (kept && !h->replaced && h->special == 0))
		return false;		// an eye is a written row's (replaced, outlined, or given a special operation - Hit::kept)
	h->kept = kept;
	return true;
}

int32 KFCResultModel::CountKeptRows(int32 chapterIdx, UID story)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return 0;
	int32 count = 0;
	for (size_t i = 0; i < c->hits.size(); ++i)
		if (c->hits[i].kept && (story == kInvalidUID || c->hits[i].storyUID == story))
			++count;
	return count;
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
// ONE SET FOR EVERY ROW (the author's call): a hit row drops what a story row (StoryLeadText) drops - every
// control character - but the breaks and the tab (marks and white space, above), the table's anchor (its sign)
// and the non-Roman special glyph (0x1A): a glyph with no code of its own stands on it, and a Glyph search's
// match can be that very character - dropped, the match would read empty. (A list of the characters one by one
// let End Nested Style Here (0x03), Indent to Here (0x07), the Right Indent Tab (0x08) and an index marker
// (kTextChar_IndexMarker - in a story's text, though TextChar.h files it with the find-only codes: KIDMCP
// measured it there) through a hit row as boxes.)
// (StoryLeadText also drops UnicodeClass::IsIgnoredCharacter's characters - variation selectors, special glyphs
// - which a story row's first words can do without, and a hit row cannot: its match may be one of them.)
bool KFCResultModel::IsMarkerNotShown(UTF32TextChar c)
{
	const uint32 v = c.GetValue();
	if (v < kTextChar_Space)
		return v != kTextChar_CR && v != kTextChar_LF && v != kTextChar_Tab && v != kTextChar_Table
			&& v != kTextChar_NonRomanSpecialGlyph;
	return v == kTextChar_ZeroSpaceNoBreak || v == kTextChar_ObjectReplacementCharacter || v == kTextChar_IndexMarker;
}

static bool IsHiddenMarker(UTF16TextChar c)
{
	return KFCResultModel::IsMarkerNotShown(UTF32TextChar(c));
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
	{
		c->docRef = newDocRef;
		// THE ONE PLACE A CHAPTER'S ROWS GET THEIR FOCI AFTER A (RE)OPEN (the text-focus spec's T6): every road that
		// binds a chapter to an open document comes here - a jump's or a replace's reopen, and the user's own open
		// (KFCOpenDocResponder).
		KFCRowFoci::AttachChapter(chapterIdx);
	}
}

int32 KFCResultModel::RenameDocumentChapters(const UIDRef& docRef, const PMString& newName)
{
	if (IsFromBook())
		return 0;
	int32 renamed = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
	{
		Chapter& c = gChapters[i];
		if (c.docRef != docRef || c.name.IsEqual(newName))
			continue;
		c.name = newName;
		c.name.SetTranslatable(kFalse);
		++renamed;
	}
	return renamed;
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
	if (gBackup.on && gBackup.versionsCopied.insert(std::make_pair(chapterIdx, story)).second)
		gBackup.versions.push_back(VersionNow(chapterIdx, story));
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

bool KFCResultModel::GetSearchedRange(int32 chapterIdx, UID story, SearchedRange& outRange)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return false;
	const std::map<UID, SearchedRange>::const_iterator it = c->searchedRanges.find(story);
	if (it == c->searchedRanges.end())
		return false;
	outRange = it->second;
	return true;
}

void KFCResultModel::SetSearchedRange(int32 chapterIdx, UID story, const SearchedRange& range)
{
	if (Chapter* c = ChapterAt(chapterIdx))
		c->searchedRanges[story] = range;
}

void KFCResultModel::GetSearchedRangeStories(int32 chapterIdx, std::vector<UID>& outStories)
{
	outStories.clear();
	if (const Chapter* c = ChapterAt(chapterIdx))
		for (std::map<UID, SearchedRange>::const_iterator it = c->searchedRanges.begin(); it != c->searchedRanges.end(); ++it)
			outStories.push_back(it->first);
}

UID KFCResultModel::GetSearchedStory(int32 chapterIdx)
{
	const Chapter* c = ChapterAt(chapterIdx);
	return (c != nil) ? c->searchedStory : kInvalidUID;
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

bool KFCResultModel::SetStoryLead(int32 chapterIdx, UID storyUID, const PMString& lead)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil || storyUID == kInvalidUID)
		return false;
	bool changed = false;
	std::vector<Hit>& hits = c->hits;
	for (size_t i = 0; i < hits.size(); ++i)
	{
		Hit& hit = hits[i];
		if (hit.storyUID != storyUID || hit.storyLead == lead)
			continue;
		BackUpRow(chapterIdx, static_cast<int32>(i), hit);
		hit.storyLead = lead;
		hit.storyLead.SetTranslatable(kFalse);
		changed = true;
	}
	return changed;
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

bool KFCResultModel::GetChapterClosedFile(int32 chapterIdx, IDFile& outFile)
{
	const Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return false;
	outFile = c->closedFile;
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
	h.kept = true;		// ...and its eye on (the row eye, E2 - the author's call)
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

void KFCResultModel::SetHitCopy(int32 chapterIdx, int32 hitIdx, const HitCopy& copy)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->copy = copy;
}

bool KFCResultModel::GetHitCopy(int32 chapterIdx, int32 hitIdx, HitCopy& outCopy)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil)
		return false;
	outCopy = h->copy;
	return true;
}

void KFCResultModel::MarkHitRestored(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end)
{
	Hit* hp = HitAt(chapterIdx, hitIdx);
	if (hp == nil)
		return;
	Hit& h = *hp;
	BackUpRow(chapterIdx, hitIdx, h);
	// The row as it was before its Replace (the spec's C10): a match to replace again, its eye off (a row not written
	// keeps none - Hit::kept) - the line and the hash are read again from the text put back by the caller
	// (RereadRowText), the page words kept as the Replace kept them (MarkHitReplaced).
	h.replaced = false;
	h.outlined = false;
	h.kept = false;
	h.replacedText.Clear();
	h.copy = HitCopy();
	h.storyUID = storyUID;
	h.textStart = start;
	h.textEnd = end;
	h.outcome = kOutcomeNone;
	BuildHitLocator(h);
}

// The page words of a row that names its page - a text row's and an object row's alike (BuildHitLocator): "P<page>",
// "(<n>)" when the page holds more than one row (Hit::pageOrdinal), and " overset" after them for an overset match.
static void AppendPageWords(KFCResultModel::Hit& hit)
{
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

void KFCResultModel::BuildHitLocator(Hit& hit)
{
	hit.locator.Clear();
	hit.locator.SetTranslatable(kFalse);
	hit.accentFlag.Clear();
	hit.accentFlag.SetTranslatable(kFalse);

	if (hit.itemUID != kInvalidUID)
	{
		// AN OBJECT ROW (O8): "pasteboard" for an item on no page, else the page and its place on it; then where the
		// item stands - master, grouped, inline, anchored - each spelled out (ROW-14). hidden / locked follow, as for text.
		// An item no spread holds - an inline in overset text - reads "overset" as a text row's overset match does (with
		// its page when one is named), not "pasteboard".
		if (hit.isOverset && (hit.onPasteboard || hit.pageString.IsEmpty()))
			hit.locator.Append("overset");
		else if (hit.onPasteboard || hit.pageString.IsEmpty())
			hit.locator.Append("pasteboard");
		else
			AppendPageWords(hit);
		if (hit.isMaster)
			hit.locator.Append(" master");
		if (hit.isGrouped)
			hit.locator.Append(" grouped");
		if (hit.isInline)
			hit.locator.Append(" inline");
		if (hit.isAnchored)
			hit.locator.Append(" anchored");
	}
	else if (hit.pageString.IsEmpty())
	{
		hit.locator.Append("overset");	// overset with nothing placed anywhere: no page to name
	}
	else
	{
		// An overset hit carries the "+" indicator's page and sorts by it; a trailing " overset"
		// (after the page and ordinal) marks it as overset -> e.g. "P1(2) overset".
		AppendPageWords(hit);
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
	// "Missing" FIRST, IN THE ROW'S OWN COLOUR (the author's call - not the last word in the accent colour, as it
	// was): it stands where "Changed" stands on a replaced row (the UI half's ApplyHitRow puts that one
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

// Are two rows on one page, as a row names its page? Its place in the document's page order AND the page's name: a
// parent page, the pasteboard and text placed nowhere have no place in that order - IPageList::GetPageIndex answers a
// parent page and a spread (the pasteboard's) with a negative number, neither being a page of the publication, and text
// placed nowhere is given -1 - so the number alone does not tell them apart.
static bool OnOnePage(const KFCResultModel::Hit& a, const KFCResultModel::Hit& b)
{
	return a.pageIndex == b.pageIndex && a.pageString.IsEqual(b.pageString);
}

// Number one chapter's hits within their pages and rebuild each locator (BuildHitLocator): the ordinal is a row's place
// among its page's rows, and a page holding ONE row shows none, since there is nothing to tell apart (ROW-11). The hits
// must already stand in page order - OrderHitsByPage, its one caller, puts them there - so a page's rows stand together,
// all but the rows with no place in that order: those stand at the end in the order InDesign walked them, where two rows
// of one parent page can have another place's between them. So a row's page is looked for among every page met, not
// only the row before it's. (Counted as runs of one pageIndex, two parent pages' rows - and the pasteboard's - were
// numbered as one page's, and a parent page's rows with another's between them were not numbered: case
// row-ordinal-pageless.)
static void NumberHitsWithinPages(std::vector<KFCResultModel::Hit>& hits)
{
	// (A test build's fault switch: a run of one pageIndex is one page, the old way - case row-ordinal-pageless-armed.)
	const bool runs = KFC_DIAG_FAULT("ordinal-runs");
	std::vector<size_t> pageOf(hits.size(), 0);	// each row's page, as an index into the two below
	std::vector<size_t> firstRowOf;				// each page met: its first row
	std::vector<int32> rowsOn;					// ...and how many rows it holds
	for (size_t i = 0; i < hits.size(); ++i)
	{
		size_t page = firstRowOf.size();		// a page not met before, unless found below
		if (i > 0 && (runs ? hits[i - 1].pageIndex == hits[i].pageIndex : OnOnePage(hits[i - 1], hits[i])))
			page = pageOf[i - 1];
		else if (!runs)
		{
			for (size_t p = 0; p < firstRowOf.size(); ++p)
				if (OnOnePage(hits[firstRowOf[p]], hits[i]))
				{
					page = p;
					break;
				}
		}
		if (page == firstRowOf.size())
		{
			firstRowOf.push_back(i);
			rowsOn.push_back(0);
		}
		pageOf[i] = page;
		++rowsOn[page];
	}
	std::vector<int32> numbered(rowsOn.size(), 0);
	for (size_t i = 0; i < hits.size(); ++i)
	{
		const size_t page = pageOf[i];
		hits[i].pageOrdinal = (rowsOn[page] > 1) ? ++numbered[page] : 0;
		KFCResultModel::BuildHitLocator(hits[i]);
	}
}

void KFCResultModel::OrderHitsByPage(std::vector<Hit>& hits)
{
	// Page order, overset matches to the end (their pageIndex is -1). Stable, so hits on the
	// same page keep their document (walk) order.
	std::stable_sort(hits.begin(), hits.end(),
		[](const Hit& a, const Hit& b)
		{
			const int32 pa = (a.pageIndex < 0) ? kMaxInt32 : a.pageIndex;
			const int32 pb = (b.pageIndex < 0) ? kMaxInt32 : b.pageIndex;
			return pa < pb;
		});
	// The within-page ordinals and the locators (NumberHitsWithinPages - their one definition).
	NumberHitsWithinPages(hits);
}

int32 KFCResultModel::MergeKeptRows(std::vector<Hit>& found, std::vector<Hit>& kept)
{
	int32 stayed = 0;
	for (size_t k = 0; k < kept.size(); ++k)
	{
		Hit& row = kept[k];
		const bool objectRow = (row.itemUID != kInvalidUID);
		if (row.replaced)
		{
			// (7) WHAT IT WROTE, MATCHED AGAIN: not a second row over the same place - the replaced row is the record.
			found.erase(std::remove_if(found.begin(), found.end(), [&row, objectRow](const Hit& h)
				{
					if (objectRow)
						return h.itemUID == row.itemUID;
					return h.storyUID == row.storyUID && h.textStart < row.textEnd && row.textStart < h.textEnd;
				}), found.end());
		}
		else
		{
			// (6) FOUND AGAIN: one row - the found one, which the walk has just read - with the eye on.
			bool again = false;
			for (size_t f = 0; f < found.size() && !again; ++f)
			{
				Hit& h = found[f];
				if (objectRow ? (h.itemUID == row.itemUID)
					: (h.storyUID == row.storyUID && h.textStart == row.textStart && h.textEnd == row.textEnd))
				{
					h.kept = true;
					h.special = row.special;	// ...and what KFC last wrote it with (an object row's special operation)
					again = true;
				}
			}
			if (again)
				continue;
		}
		// (5) IT STAYS, beside its story's found rows in the order their text stands.
		size_t at = found.size();
		size_t afterLast = found.size();
		for (size_t f = 0; !objectRow && f < found.size(); ++f)
		{
			if (found[f].storyUID != row.storyUID)
				continue;
			if (found[f].textStart > row.textStart)
			{
				at = f;
				break;
			}
			afterLast = f + 1;
		}
		if (at == found.size() && afterLast < found.size())
			at = afterLast;
		found.insert(found.begin() + static_cast<std::ptrdiff_t>(at), std::move(row));
		++stayed;
	}
	kept.clear();
	return stayed;
}

int32 KFCResultModel::CarryKeptRows(int32 chapterIdx, std::vector<Hit>& kept, const std::map<UID, uint32>& versions)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil || kept.empty())
	{
		kept.clear();
		return 0;
	}
	for (size_t i = 0; i < kept.size(); ++i)
	{
		const UID s = kept[i].storyUID;
		const std::map<UID, uint32>::const_iterator v = versions.find(s);
		if (s != kInvalidUID && c->storyVersions.count(s) == 0 && v != versions.end())
			c->storyVersions[s] = v->second;
	}
	const int32 stayed = MergeKeptRows(c->hits, kept);
	OrderHitsByPage(c->hits);
	IndexRows(*c);
	return stayed;
}

bool KFCResultModel::OrderChapters(const std::vector<int32>& order)
{
	if (order.size() != gChapters.size())
		return false;
	std::vector<bool> seen(gChapters.size(), false);
	for (size_t i = 0; i < order.size(); ++i)
	{
		const int32 at = order[i];
		if (at < 0 || at >= static_cast<int32>(gChapters.size()) || seen[static_cast<size_t>(at)])
			return false;
		seen[static_cast<size_t>(at)] = true;
	}
	std::vector<Chapter> was;
	was.swap(gChapters);
	gChapters.reserve(was.size());
	for (size_t i = 0; i < order.size(); ++i)
		gChapters.push_back(std::move(was[static_cast<size_t>(order[i])]));
	return true;
}

int32 KFCResultModel::ReplaceStoryRows(int32 chapterIdx, UID story, std::vector<Hit>& storyHits, uint32 storyVersion,
	int32* outStayed)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return -1;
	// THE ROW EYE (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-2): the story's rows whose eye is on,
	// carried into the walk's rows (MergeKeptRows) - copied before the story's rows go out below.
	std::vector<Hit> keptRows;
	for (size_t i = 0; i < c->hits.size(); ++i)
		if (c->hits[i].storyUID == story && c->hits[i].kept)
			keptRows.push_back(c->hits[i]);
	const int32 stayed = MergeKeptRows(storyHits, keptRows);
	if (outStayed != nil)
		*outStayed = stayed;
	// The chapter's foci name its rows by their index, which is about to change (attached again at the foot).
	KFCRowFoci::DetachChapter(chapterIdx);
	// The story's rows out and the walk's in, where its first row stood - the other rows keep their order, so a
	// page the story shares with others reads as a whole search would list it.
	std::vector<Hit> hits;
	hits.reserve(c->hits.size() + storyHits.size());
	size_t at = c->hits.size() + 1;		// "not seen yet"
	for (size_t i = 0; i < c->hits.size(); ++i)
	{
		if (c->hits[i].storyUID == story)
		{
			if (at > c->hits.size())
				at = hits.size();
			continue;
		}
		hits.push_back(std::move(c->hits[i]));
	}
	if (at > hits.size())
		at = hits.size();
	hits.insert(hits.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(storyHits.begin()),
		std::make_move_iterator(storyHits.end()));
	storyHits.clear();
	OrderHitsByPage(hits);
	c->hits.swap(hits);
	c->storyVersions[story] = storyVersion;
	// ITS LAST STORY SEARCHED AGAIN AND NOTHING FOUND: the document's row stays, (0/0), as Search This Document Again leaves
	// one (the author's call: the same reason holds - it can be searched again after the next edit). The story's own row
	// goes with its rows (a story group is built from the rows).
	c->shownEmpty = c->hits.empty();
	// THE ROWS' PLACES IN THE LIST CHANGED: indexed again, and whatever names them by index let go, the chapter's foci
	// back on them (RowsLaidOutAgain).
	RowsLaidOutAgain(chapterIdx, *c);
	int32 count = 0;
	for (size_t i = 0; i < c->hits.size(); ++i)
		if (c->hits[i].storyUID == story)
			++count;
	return count;
}

int32 KFCResultModel::ReplaceChapterRows(int32 chapterIdx, std::vector<Hit>& docHits, std::map<UID, uint32>& storyVersions,
	int32* outStayed)
{
	Chapter* c = ChapterAt(chapterIdx);
	if (c == nil)
		return -1;
	// THE ROW EYE (2-2): the chapter's rows whose eye is on, carried into the walk's rows (MergeKeptRows); a story the
	// walk found nothing in keeps the version its foci were followed at (Search This Document Again follows them first).
	std::vector<Hit> keptRows;
	for (size_t i = 0; i < c->hits.size(); ++i)
	{
		if (!c->hits[i].kept)
			continue;
		keptRows.push_back(c->hits[i]);
		const UID s = c->hits[i].storyUID;
		const std::map<UID, uint32>::const_iterator v = c->storyVersions.find(s);
		if (s != kInvalidUID && storyVersions.count(s) == 0 && v != c->storyVersions.end())
			storyVersions[s] = v->second;
	}
	const int32 stayed = MergeKeptRows(docHits, keptRows);
	if (outStayed != nil)
		*outStayed = stayed;
	// ReplaceStoryRows' steps over the whole chapter: its foci let go (they name rows by index), the walk's rows in, in
	// page order, and the end ReplaceStoryRows has (RowsLaidOutAgain - indexed as the search's are, what names the rows
	// by index let go). WHAT A SEARCH OVER PART OF THE DOCUMENT SEARCHED STAYS - its searched parts (recorded again where
	// they stand by the caller, so their foci come back on them) and its story: the tier heading's search again walks
	// them again (the author, 2026-10-17 evening: "the same conditions as the first search - a Story search, that story
	// alone").
	KFCRowFoci::DetachChapter(chapterIdx);
	OrderHitsByPage(docHits);
	c->hits.swap(docHits);
	std::vector<Hit>().swap(docHits);
	c->storyVersions.swap(storyVersions);
	storyVersions.clear();
	c->shownEmpty = c->hits.empty();
	RowsLaidOutAgain(chapterIdx, *c);
	return static_cast<int32>(c->hits.size());
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
	gBackup = RowBackup();
	gBackup.on = true;
}

void KFCResultModel::RollBackRows()
{
	gBackup.on = false;

	// One copy per row (BackUpRow), so the direction of this loop decides nothing.
	for (size_t i = gBackup.rows.size(); i > 0; --i)
	{
		const RowCopy& saved = gBackup.rows[i - 1];
		if (Hit* h = HitAt(saved.chapter, saved.hit))	// nil: the result set changed underneath
			*h = saved.row;
	}
	// The recorded versions too - every caller records them only once its write has gone
	// through, so there is normally nothing here.
	for (size_t i = 0; i < gBackup.versions.size(); ++i)
		PutVersionBack(gBackup.versions[i]);

	ForgetRowBackup();
}

void KFCResultModel::TakeRowBackup(RowStep& out)
{
	out = RowStep();
	// "before" is the copy BackUpRow took as the write first changed the row; "after" is the row now.
	out.before.swap(gBackup.rows);
	out.versionsBefore.swap(gBackup.versions);
	gBackup = RowBackup();	// off and empty: the copies are the step's now
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
	for (size_t i = 0; i < out.versionsBefore.size(); ++i)
		out.versionsAfter.push_back(VersionNow(out.versionsBefore[i].chapter, out.versionsBefore[i].story));
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
	// The foci of the rows put back go back on their places (the text-focus spec's T7): an Undo or a Redo moves text
	// without moving a focus, and these rows' stored places are exact now - the step is followed only when the story
	// is at its version.
	for (size_t i = 0; i < rows.size(); ++i)
		KFCRowFoci::Reanchor(rows[i].chapter, rows[i].hit);
}

void KFCResultModel::TakeModelSnapshot(ModelSnapshot& out)
{
	out.chapters = gChapters;
	out.layout = gLayoutGeneration;
	out.header = gHeader;
}

void KFCResultModel::RestoreModelSnapshot(const ModelSnapshot& snapshot)
{
	// The rows going away take their foci (the rows put back get theirs at the foot - the text-focus spec's T7).
	KFCRowFoci::DetachAll();
	gChapters = snapshot.chapters;
	gLayoutGeneration = snapshot.layout;
	gHeader = snapshot.header;
	// The row backup names rows by index, and these rows were just replaced (ReplaceStoryRows forgets it the same way).
	ForgetRowBackup();
	// The list put back - its rows' foci where KFC knows the stories as they stand (the text-focus spec's T7).
	KFCRowFoci::AttachOpenChapters();
}

void KFCResultModel::ApplyRowStepTo(ModelSnapshot& list, const RowStep& step, bool after)
{
	// ApplyRowStep's rows and versions, onto a list that is not the model's: indexes checked here, as ChapterAt / HitAt
	// check them for the model's own; PutVersionBack's rule for a version (recorded again, or taken off).
	const std::vector<RowCopy>& rows = after ? step.after : step.before;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		const RowCopy& r = rows[i];
		if (r.chapter < 0 || r.chapter >= static_cast<int32>(list.chapters.size()))
			continue;
		std::vector<Hit>& hits = list.chapters[static_cast<size_t>(r.chapter)].hits;
		if (r.hit >= 0 && r.hit < static_cast<int32>(hits.size()))
			hits[static_cast<size_t>(r.hit)] = r.row;
	}
	const std::vector<VersionCopy>& versions = after ? step.versionsAfter : step.versionsBefore;
	for (size_t i = 0; i < versions.size(); ++i)
	{
		const VersionCopy& v = versions[i];
		if (v.chapter < 0 || v.chapter >= static_cast<int32>(list.chapters.size()))
			continue;
		std::map<UID, uint32>& recorded = list.chapters[static_cast<size_t>(v.chapter)].storyVersions;
		if (v.had)
			recorded[v.story] = v.version;
		else
			recorded.erase(v.story);
	}
}

void KFCResultModel::GetListCounts(const ModelSnapshot& list, int32& outReplaced, int32& outHits)
{
	// GetTotalHitCount's and GetTotalReplacedCount's count (GetChapterReplacedCount), over the list's own chapters.
	outReplaced = 0;
	outHits = 0;
	for (size_t c = 0; c < list.chapters.size(); ++c)
	{
		const std::vector<Hit>& hits = list.chapters[c].hits;
		outHits += static_cast<int32>(hits.size());
		for (size_t i = 0; i < hits.size(); ++i)
			if (hits[i].replaced)
				++outReplaced;
	}
}

UID KFCResultModel::GetHitItem(int32 chapterIdx, int32 hitIdx)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	return (h != nil) ? h->itemUID : kInvalidUID;
}

bool KFCResultModel::GetHitItemPrint(int32 chapterIdx, int32 hitIdx, uint64& outPrint, uint32& outLength)
{
	const Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || h->itemUID == kInvalidUID)
		return false;
	outPrint = h->itemPrint;
	outLength = h->itemPrintLength;
	return true;
}

void KFCResultModel::MarkItemReplaced(int32 chapterIdx, int32 hitIdx, uint64 newPrint, uint32 newLength)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || h->itemUID == kInvalidUID)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->replaced = true;
	h->special = 0;		// (the row reads "Changed" - its last write)
	h->kept = true;		// ...and its eye on (the row eye, E2)
	h->itemPrint = newPrint;
	h->itemPrintLength = newLength;
}

void KFCResultModel::MarkItemOutlined(int32 chapterIdx, int32 hitIdx, UID newItem, uint64 newPrint, uint32 newLength)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || h->itemUID == kInvalidUID)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->replaced = true;		// written by KFC: its Replace greyed, counted as written
	h->outlined = true;		// ...as outlines: the row reads "Outlined"
	h->special = 0;			// (its last write - no special operation's word over it)
	h->kept = true;			// ...and its eye on (the row eye - a row KFC wrote stays through a search again)
	h->itemUID = newItem;	// the outlines InDesign made: the row's jump selects them
	h->itemPrint = newPrint;
	h->itemPrintLength = newLength;
}

void KFCResultModel::MarkItemSpecial(int32 chapterIdx, int32 hitIdx, int32 op, uint64 newPrint, uint32 newLength,
	const Hit* place)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || h->itemUID == kInvalidUID)
		return;
	BackUpRow(chapterIdx, hitIdx, *h);
	h->special = op;			// the row reads the operation's word first (the UI half's KFCSpecialRowWord)
	h->kept = true;				// ...and its eye on (a row KFC wrote stays through a search again)
	h->outcome = kOutcomeNone;	// the write has just found the item there
	if (place == nil)
	{
		// THE ITEM TAKEN AWAY (Delete): nothing left to replace - its Replace greyed, the row counted as written. Its UID is
		// kept: an Undo brings the item back under it, and the row's copy (the backup) with it.
		h->replaced = true;
		BuildHitLocator(*h);
		return;
	}
	h->itemPrint = newPrint;
	h->itemPrintLength = newLength;
	// WHERE IT STANDS NOW - read after the write (KFCObjectSearch::ReadItemPlace). Its place among its page's rows is
	// dropped when the page changed: the rows are not ordered again (an Undo step names them by index).
	if (h->pageIndex != place->pageIndex || !h->pageString.IsEqual(place->pageString))
		h->pageOrdinal = 0;
	h->pageString = place->pageString;
	h->pageIndex = place->pageIndex;
	h->onPasteboard = place->onPasteboard;
	h->isMaster = place->isMaster;
	h->isGrouped = place->isGrouped;
	h->isInline = place->isInline;
	h->isAnchored = place->isAnchored;
	h->isOverset = place->isOverset;
	h->isHidden = place->isHidden;
	h->isLocked = place->isLocked;
	BuildHitLocator(*h);
}

void KFCResultModel::SetHitItemPrint(int32 chapterIdx, int32 hitIdx, uint64 newPrint, uint32 newLength)
{
	Hit* h = HitAt(chapterIdx, hitIdx);
	if (h == nil || h->itemUID == kInvalidUID)
		return;
	// (A test build's fault switch: the print set with no copy aside - an Undo then leaves the row at the write's print
	//  while the item goes back - case obj-f2-undo-frame-armed.)
	if (!KFC_DIAG_FAULT("object-prints-unbacked"))
		BackUpRow(chapterIdx, hitIdx, *h);
	h->itemPrint = newPrint;
	h->itemPrintLength = newLength;
}

// End, KFCResultModel.cpp.
