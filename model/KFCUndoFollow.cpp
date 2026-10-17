//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  KFCUndoFollow.cpp -- see the header.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IHierarchy.h"				// ItemIsThere - an item a write took away
#include "IDataBase.h"
#include "IObserver.h"
#include "ISubject.h"

// General includes:
#include "CmdUtils.h"
#include "CObserver.h"
#include "Command.h"
#include "ErrorUtils.h"
#include "IDFile.h"
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - the gate in LazyUpdate
#include "FileUtils.h"				// IsEqual - a frozen chapter found again by its file
#include "ITextModel.h"				// StoryTextHash - the story's text, read whole
#include "IStoryList.h"				// RunRecorder::ReadStories - every text model of a query run's documents
#include "textiterator.h"			// StoryTextHash
#include "UIDList.h"

#include <algorithm>		// std::find, std::remove_if, std::any_of - a closing document's part of the kept writes
#include <set>
#include <utility>			// std::move - a kept write carries its rows

// Project includes:
#include "KFCID.h"
#include "KFCBookScope.h"		// FindOpenChapterDoc - a chapter's document found again by its file
#include "KFCDiag.h"			// KFC_DIAG_LOG - the test build's trace (compiled out of a shipping build)
#include "KFCResultModel.h"
#include "KFCModelNotify.h"		// the panel drawn again, and its message line - told, never called
#include "KFCRunGuard.h"		// IsAnyRunning - nothing is followed while any run of ours (the replace among them) is up
#include "KFCSearchEngine.h"	// ReadStoryVersion - a story's version
#include "KFCObjectSearch.h"	// Fingerprint - an object row's item, before and after a write
#include "KFCPageItemFacts.h"	// StackIndex - an item's place in its stack, before and after an Arrange
#include "KFCTierStore.h"		// the kept tiers: their writes are kept, and followed onto their copies (the tier switch)
#include "KFCUndoFollow.h"

namespace
{

// One story a write moved: its version before and after the write.
struct StoryMoved
{
	IDFile		file;		// its chapter's file - how the document is found again (KFCBookScope::FindOpenChapterDoc)
	UIDRef		doc;		// its document as the write found it - the only handle a document without a file has
	UID			story;
	uint32		before;
	uint32		after;
	uint64		afterText;	// its text right after the write, as one number (StoryTextHash) - what tells a Redo from
							// the same count of edits made after an Undo (the head of KFCUndoFollow.h)
	IDataBase*	now;		// the document as it was last found (ResolveDocs); nil = not open
	StoryMoved() : story(kInvalidUID), before(0), after(0), afterText(0), now(nil) {}
};

// One page item a write changed - an object row's Replace: its fingerprint before and after the write
// (KFCObjectSearch::Fingerprint) - what an Undo and a Redo of it are told by, as a story's version is for text (measured:
// docs/superpowers/plans/2026-10-09-kfc-object-search.md, Task 1 M9 - an Undo brings the fingerprint back to "before", a
// Redo to "after").
struct ItemMoved
{
	IDFile		file;
	UIDRef		doc;
	UID			item;
	uint64		before;
	uint32		beforeLength;
	uint64		after;
	uint32		afterLength;
	IDataBase*	now;
	bool		afterGone;	// the write took the item away (Create Outlines): no print after it - gone is its "after"
	bool		withOrder;	// its place in its parent's stack is asked too (Arrange - RecordItem's withOrder)...
	int32		orderBefore;	// ...before the write (KFCPageItemFacts::StackIndex)
	int32		orderAfter;		// ...and after it
	ItemMoved() : item(kInvalidUID), before(0), beforeLength(0), after(0), afterLength(0), now(nil), afterGone(false),
		withOrder(false), orderBefore(-1), orderAfter(-1) {}
};

// One kept write.
struct Step
{
	bool							done;			// the write stands (false: an Undo took it back)
	uint32							resultSet;		// KFCResultModel::GetResultSetId when it was made
	uint32							resultSetBefore;	// a whole set's: the one it found - its Undo goes back to it
	bool							whole;			// it reshaped the result set: a query run (RunRecorder); else a row's
													// Replace (StepRecorder) - the name on the message line too
	const char*						name;			// a row write's own name (StepRecorder::SetName - Create Outlines); nil = Replace
	uint32							layoutBefore;	// the layout the row indices named before it...
	uint32							layoutAfter;	// ...and after it (the same, for a write of rows)
	std::vector<StoryMoved>			stories;
	std::vector<ItemMoved>			items;			// an object row's Replace: the item it changed
	KFCResultModel::RowStep			rows;			// a write of rows: the rows before and after
	KFCResultModel::ModelSnapshot	before;			// a write of the whole result set: the set before...
	KFCResultModel::ModelSnapshot	after;			// ...and after
	std::vector<size_t>				frozenBefore;	// a whole set's book chapters closed since, as indexes into before's
	std::vector<size_t>				frozenAfter;	// and after's chapters: kept as the panel has them when that set is
													// put back (KFCUndoFollow::ForgetBookChapter)
	Step() : done(true), resultSet(0), resultSetBefore(0), whole(false), name(nil), layoutBefore(0), layoutAfter(0) {}
};

// The kept writes, oldest first.
std::vector<Step> gSteps;

// HOW MANY ARE KEPT. InDesign's own history is longer, but a write older than this is rarely undone through
// thirty of KFC's own; one that is simply stops being followed (its rows stay as they are). A whole result
// set is heavy (a large search holds thousands of rows), so only the last three of those are kept, with
// everything older than the oldest of them dropped too: a write older than a whole result set's names the
// rows in the layout before it, and could only be reached by undoing that write first.
const size_t kMaxSteps = 30;
const size_t kMaxWholeSteps = 3;

// THE RECORDING STANDING NOW (at most one - the writes of KFC never nest): what its recorder read at its start. Opened and
// closed whole, so nothing one recorder read is left for the next.
struct Recording
{
	bool							open;		// a write is being made - nothing is followed until it is kept or let go
	uint32							resultSet;	// the list's result set at its start (KFCResultModel::GetResultSetId)...
	uint32							layout;		// ...and its layout generation
	std::vector<StoryMoved>			stories;	// the stories read, each at its version before the write
	std::vector<ItemMoved>			items;		// the items RecordItem read
	KFCResultModel::ModelSnapshot	before;		// a query run's: the whole list before it
	const char*						name;		// the write's name (StepRecorder::SetName)
	Recording() : open(false), resultSet(0), layout(0), name(nil) {}
};
Recording gRecording;

// (Assigning a fresh one releases the storage too.)
void CloseRecording()
{
	gRecording = Recording();
}

void OpenRecording()
{
	gRecording = Recording();
	gRecording.open = true;
	gRecording.resultSet = KFCResultModel::GetResultSetId();
	gRecording.layout = KFCResultModel::GetLayoutGeneration();
}

// The document a story (or an object row's item, or a chapter of a kept list) lives in, if it is open - by its chapter's
// file first, as every door of KFC finds a chapter's document (a UIDRef can outlive its document -
// KFCBookScope::ReachChapterDoc).
IDataBase* DocOfFile(const IDFile& file, const UIDRef& doc)
{
	UIDRef found = doc;
	if (!KFCBookScope::FindOpenChapterDoc(file, found))
		return nil;
	return found.GetDataBase();
}
IDataBase* DocOf(const StoryMoved& s) { return DocOfFile(s.file, s.doc); }
IDataBase* DocOf(const ItemMoved& m) { return DocOfFile(m.file, m.doc); }

// A chapter of the list as it stands: its file, and its document if that is open - found the same way.
bool ChapterDocNow(int32 chapterIdx, IDFile& outFile, UIDRef& outDoc)
{
	return KFCResultModel::GetChapterLocation(chapterIdx, outDoc, outFile)
		&& KFCBookScope::FindOpenChapterDoc(outFile, outDoc);
}

// A story's version now, noted as its "before" - a write about to be made may move it. A story that cannot be read (gone)
// is not noted.
void NoteStoryBefore(const IDFile& file, const UIDRef& doc, IDataBase* db, UID story, std::vector<StoryMoved>& out)
{
	uint32 version = 0;
	if (!KFCSearchEngine::ReadStoryVersion(db, story, version))
		return;
	StoryMoved s;
	s.file = file;
	s.doc = doc;
	s.story = story;
	s.before = version;
	s.now = db;
	out.push_back(s);
}

// Every story holding a row of the chapter, with the version it has now. A chapter whose document is not
// open has none to read - and no write reaches it.
void ReadChapterStories(int32 chapterIdx, std::vector<StoryMoved>& out)
{
	UIDRef found;
	IDFile file;
	if (!ChapterDocNow(chapterIdx, file, found))
		return;
	IDataBase* const db = found.GetDataBase();
	std::set<UID> stories;
	KFCResultModel::GetChapterStories(chapterIdx, stories);
	for (std::set<UID>::const_iterator story = stories.begin(); story != stories.end(); ++story)
		NoteStoryBefore(file, found, db, *story, out);
}

// Find every kept write's documents again. A write with a document that is not open any more is dropped:
// closing a document throws its undo history away, so nothing can take that write back or do it again.
void ResolveDocs()
{
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		bool allOpen = true;
		std::vector<StoryMoved>& stories = gSteps[i].stories;
		for (size_t k = 0; k < stories.size(); ++k)
		{
			stories[k].now = DocOf(stories[k]);
			if (stories[k].now == nil)
				allOpen = false;
		}
		std::vector<ItemMoved>& items = gSteps[i].items;
		for (size_t k = 0; k < items.size(); ++k)
		{
			items[k].now = DocOf(items[k]);
			if (items[k].now == nil)
				allOpen = false;
		}
		if (!allOpen)
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	}
}

// The writes kept for another result set name rows that are gone (a search, Clear Results, a close) - ALL BUT THE ONES A
// KEPT QUERY RUN LEADS BACK TO. A run's Undo puts back the list it found, as that list's
// result set (Follow), so the writes made on that list before the run are Ctrl+Z's to take back next, and an earlier run's
// the one after (UNDO-10: the last three runs are kept, with what came before them). Dropped here at the run's Keep, the
// Replace before it read "Changed" over its text after both Ctrl+Z's (case xq-done-undo), and two runs' second Ctrl+Z left
// the list empty (xq-two-runs-undo) - the cancelled run's twin, fixed by ReturnToResultSet (xq-cancel-undo).
// So a set reached from the one the panel shows through kept runs stays - each run joins the set it found and the set it
// left - and every other set's writes go. `incoming` = a write about to be kept (KeepStep): its sets count as reached.
void DropOtherResultSets(const Step* incoming = nil)
{
	std::set<uint32> reached;
	reached.insert(KFCResultModel::GetResultSetId());
	// ...and every kept tier's (the tier switch - KFCTierStore): a tier is a list that can be switched back to, so the writes
	// made on it while it was the live list stay its Undo's to follow - onto its copy while it is kept, onto the list when it
	// comes back (Follow).
	{
		std::vector<uint32> tiers;
		KFCTierStore::GetTierSets(tiers);
		reached.insert(tiers.begin(), tiers.end());
	}
	if (incoming != nil && incoming->whole)
		reached.insert(incoming->resultSetBefore);
	for (bool grew = true; grew; )
	{
		grew = false;
		for (size_t i = 0; i < gSteps.size(); ++i)
		{
			const Step& s = gSteps[i];
			if (!s.whole || (reached.count(s.resultSet) == 0 && reached.count(s.resultSetBefore) == 0))
				continue;
			if (reached.insert(s.resultSet).second)
				grew = true;
			if (reached.insert(s.resultSetBefore).second)
				grew = true;
		}
	}
	for (size_t i = gSteps.size(); i-- > 0; )
		if (reached.count(gSteps[i].resultSet) == 0)
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
}

// A CLOSING DOCUMENT'S CHAPTERS IN A LIST A KEPT QUERY RUN HOLDS: a write of rows kept for a list the panel does not
// show - the one a run found (its `before`) - names its rows in THAT list, so a chapter's index there is read from the
// run's copy of it, found by its file as the panel's chapters are (ForgetBookChapter). Nothing when no kept run holds it.
void ChaptersOfDocInKeptSet(uint32 set, IDataBase* db, std::vector<int32>& out)
{
	out.clear();
	for (size_t i = 0; i < gSteps.size(); ++i)
	{
		const Step& w = gSteps[i];
		if (!w.whole || (w.resultSetBefore != set && w.resultSet != set))
			continue;
		const KFCResultModel::ModelSnapshot& list = (w.resultSetBefore == set) ? w.before : w.after;
		for (size_t c = 0; c < list.chapters.size(); ++c)
			if (DocOfFile(list.chapters[c].file, list.chapters[c].docRef) == db)
				out.push_back(static_cast<int32>(c));
		return;		// one copy of a set is as good as another
	}
	// ...or a kept tier's copy (the tier switch - KFCTierStore), found the same way.
	KFCTierStore::ChaptersOfDoc(set, db, out);
}

// A CLOSING CHAPTER'S ROWS OFF A KEPT WRITE OF ROWS. A write of rows can be several documents' (rows selected together,
// one undo step - KFCReplaceEngine.cpp's ReplaceRowsNow, KFCObjectReplace::ReplaceRows). When one of them closes, the
// write is still followed in the others, and an Undo or a Redo there takes back or does again their part alone (measured:
// the case sel-book-close-undo - chapter 1 saved and closed, Ctrl+Z in chapter 2 put chapter 2's text back and left
// chapter 1's file as written). Put back with them, the closed chapter's rows said it held the text before the Replace
// while its file held the text after it - so its rows and its recorded versions leave the write, and they stay as the
// panel has them.
void DropChapterRows(Step& step, int32 chapterIdx)
{
	KFCResultModel::RowStep& rows = step.rows;
	const auto rowOf = [chapterIdx](const KFCResultModel::RowCopy& r) { return r.chapter == chapterIdx; };
	const auto versionOf = [chapterIdx](const KFCResultModel::VersionCopy& v) { return v.chapter == chapterIdx; };
	rows.before.erase(std::remove_if(rows.before.begin(), rows.before.end(), rowOf), rows.before.end());
	rows.after.erase(std::remove_if(rows.after.begin(), rows.after.end(), rowOf), rows.after.end());
	rows.versionsBefore.erase(std::remove_if(rows.versionsBefore.begin(), rows.versionsBefore.end(), versionOf),
		rows.versionsBefore.end());
	rows.versionsAfter.erase(std::remove_if(rows.versionsAfter.begin(), rows.versionsAfter.end(), versionOf),
		rows.versionsAfter.end());
}

// A CLOSING DOCUMENT'S STORIES AND ITEMS OFF A KEPT WRITE (`of` - is it that document's? - is asked of a StoryMoved and of
// an ItemMoved alike). True when the write is left with none: it goes whole.
template <class Of> bool TakeDocOff(Step& step, const Of& of)
{
	step.stories.erase(std::remove_if(step.stories.begin(), step.stories.end(), of), step.stories.end());
	step.items.erase(std::remove_if(step.items.begin(), step.items.end(), of), step.items.end());
	return step.stories.empty() && step.items.empty();
}

// Do the two writes share a document? (InDesign keeps one history per document, so two that do are
// taken back and done again in order; two that do not, in any order.)
// A WRITE OF A WHOLE RESULT SET SHARES WITH EVERY WRITE. What a query run's step puts back is the WHOLE result
// set - every document's rows, the ones it never wrote to as well (ModelSnapshot) - so following its Undo or Redo
// in one document, unordered, would take back on the panel a Replace made since in another (measured with Change
// Checked, the first whole-set write, gone since). Ordered against every write, it waits until
// the writes after it are undone, and a write made after its Undo throws its Redo away (KeepStep). One InDesign
// takes back or does again out of that order is not followed: the rows stay as they are, and the doors stand
// behind them as for any edit KFC did not make (the story's version, the row's text).
// (A write's documents are its stories' AND its items' - DocCount / DocAt, read in place: this is asked
//  for pairs of kept writes, up to kMaxSteps of them, so it makes no copies.)
size_t DocCount(const Step& s)
{
	return s.stories.size() + s.items.size();
}

IDataBase* DocAt(const Step& s, size_t k)
{
	return (k < s.stories.size()) ? s.stories[k].now : s.items[k - s.stories.size()].now;
}

bool ShareDoc(const Step& a, const Step& b)
{
	if (a.whole || b.whole)
		return true;
	for (size_t x = 0; x < DocCount(a); ++x)
		for (size_t y = 0; y < DocCount(b); ++y)
			if (DocAt(a, x) == DocAt(b, y))
				return true;
	return false;
}

// A story's whole text - every thread, the deleted text Track Changes keeps included - as one number: FNV-1a, 64-bit,
// over each character's value. 0 = it could not be read. What a Redo is asked besides the version (AllAt).
uint64 StoryTextHash(IDataBase* db, UID story)
{
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return 0;
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	if (model == nil)
		return 0;
	uint64 hash = 14695981039346656037ULL;
	const int32 total = model->TotalLength();
	TextIterator it(model, 0);
	for (int32 i = 0; i < total && !it.IsNull(); ++i, ++it)
	{
		const uint32 ch = static_cast<uint32>((*it).GetValue());
		for (int32 b = 0; b < 4; ++b)
		{
			hash ^= static_cast<uint64>((ch >> (b * 8)) & 0xFF);
			hash *= 1099511628211ULL;
		}
	}
	hash ^= static_cast<uint64>(total);		// the length too: a story cut short reads differently
	return (hash != 0) ? hash : 1;
}

// The stories the recording read that the write has moved, each with its version and text after it, onto the step - the
// rest cannot tell an Undo of it from anything else.
void TakeMovedStories(Step& step)
{
	for (size_t k = 0; k < gRecording.stories.size(); ++k)
	{
		StoryMoved s = gRecording.stories[k];
		s.now = DocOf(s);
		uint32 version = 0;
		if (s.now == nil || !KFCSearchEngine::ReadStoryVersion(s.now, s.story, version) || version == s.before)
			continue;
		s.after = version;
		s.afterText = StoryTextHash(s.now, s.story);
		step.stories.push_back(s);
	}
}

// The items the recording read that the write changed, each with its fingerprint after it, onto the step.
// IS THE ITEM IN ITS DOCUMENT - a write that takes an item away (Create Outlines) leaves its UID unused, an Undo puts it
// back.
bool ItemIsThere(IDataBase* db, UID item)
{
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return false;
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	return hier != nil;
}

void TakeMovedItems(Step& step)
{
	for (size_t k = 0; k < gRecording.items.size(); ++k)
	{
		ItemMoved m = gRecording.items[k];
		m.now = DocOf(m);
		if (m.now == nil)
			continue;
		if (!KFCObjectSearch::Fingerprint(UIDRef(m.now, m.item), m.after, m.afterLength))
		{
			// AN ITEM THE WRITE TOOK AWAY (Create Outlines - the frame replaced by its outlines, KFCObjectOutline): no print to
			// read after it - its Undo brings the item back at "before", its Redo takes it away again (AllAt).
			if (!ItemIsThere(m.now, m.item))
			{
				m.afterGone = true;
				m.after = 0;
				m.afterLength = 0;
				step.items.push_back(m);
			}
			continue;
		}
		// AN ITEM MOVED IN ITS STACK (Arrange - its print does not move: the stack is its parent's): its place after it, and
		// kept when that moved.
		if (m.withOrder)
			m.orderAfter = KFCPageItemFacts::StackIndex(m.now, m.item);
		if (m.after == m.before && m.afterLength == m.beforeLength && (!m.withOrder || m.orderAfter == m.orderBefore))
			continue;
		step.items.push_back(m);
	}
}

// Is every story the write moved at its version before it (after = false) or after it (after = true)?
// A REDO IS ASKED THE TEXT AS WELL: an edit after an Undo goes on from the version the Undo put back, so as many
// edits as the write moved the story by land on its "after" (measured - the regression case
// u1-false-redo-undo-other-type4); only a story that also reads as the write left it was redone. (An Undo needs no
// such question: a version comes back DOWN to "before" only by an Undo.)
bool AllAt(const Step& step, bool after)
{
	for (size_t k = 0; k < step.stories.size(); ++k)
	{
		const StoryMoved& s = step.stories[k];
		uint32 version = 0;
		if (!KFCSearchEngine::ReadStoryVersion(s.now, s.story, version))
			return false;
		if (version != (after ? s.after : s.before))
			return false;
		if (after && StoryTextHash(s.now, s.story) != s.afterText)
			return false;
	}
	// AN OBJECT ROW'S ITEM: its fingerprint back at "before" (an Undo) or at "after" (a Redo).
	for (size_t k = 0; k < step.items.size(); ++k)
	{
		const ItemMoved& m = step.items[k];
		// ...or, for an item the write took away (Create Outlines), gone again at "after" (a Redo).
		if (after && m.afterGone)
		{
			if (ItemIsThere(m.now, m.item))
				return false;
			continue;
		}
		uint64 print = 0;
		uint32 length = 0;
		if (!KFCObjectSearch::Fingerprint(UIDRef(m.now, m.item), print, length))
			return false;
		if (print != (after ? m.after : m.before) || length != (after ? m.afterLength : m.beforeLength))
			return false;
		// ...and, for an item a write moved in its stack (Arrange), its place there back at "before" or at "after".
		if (m.withOrder && KFCPageItemFacts::StackIndex(m.now, m.item) != (after ? m.orderAfter : m.orderBefore))
			return false;
	}
	return !step.stories.empty() || !step.items.empty();
}

// A whole result set to put back, its FROZEN chapters (indexes into it) - a book's, closed since
// (KFCUndoFollow::ForgetBookChapter) - given the rows the panel has for them now, found by the chapter's file
// (FileUtils::IsEqual, as KFCBookScope compares them): an Undo in another chapter does not reach their file. A
// chapter the panel no longer holds rows for is emptied in place (KFCResultModel::EmptyChapter - its place is kept).
void KeepFrozenAsTheyAre(KFCResultModel::ModelSnapshot& set, const std::vector<size_t>& frozen)
{
	if (frozen.empty())
		return;
	KFCResultModel::ModelSnapshot now;
	KFCResultModel::TakeModelSnapshot(now);
	for (size_t f = 0; f < frozen.size(); ++f)
	{
		if (frozen[f] >= set.chapters.size())
			continue;
		KFCResultModel::Chapter& chapter = set.chapters[frozen[f]];
		size_t n = 0;
		while (n < now.chapters.size() && FileUtils::IsEqual(now.chapters[n].file, chapter.file) == kFalse)
			++n;
		if (n < now.chapters.size())
			chapter = now.chapters[n];
		else
			KFCResultModel::EmptyChapter(chapter);
	}
}

// A write standing in a document a LATER standing write shares: the later one is undone first.
bool NewerDoneSharesDoc(size_t i)
{
	for (size_t j = i + 1; j < gSteps.size(); ++j)
		if (gSteps[j].done && ShareDoc(gSteps[i], gSteps[j]))
			return true;
	return false;
}

// A write undone in a document an EARLIER undone write shares: the earlier one is done again first.
bool OlderUndoneSharesDoc(size_t i)
{
	for (size_t j = 0; j < i; ++j)
		if (!gSteps[j].done && ShareDoc(gSteps[i], gSteps[j]))
			return true;
	return false;
}

#ifdef KFC_DIAG
// The test build's picture of every kept write: its stories with the versions it waits for and the one each
// story has now - what decides a follow, laid out (KFCDiag.h).
void DiagSteps(const char* when)
{
	KFC_DIAG_LOG("  STEPS %s n=%u set(now)=%u layout(now)=%u", when, (unsigned)gSteps.size(),
		KFCResultModel::GetResultSetId(), KFCResultModel::GetLayoutGeneration());
	for (size_t i = 0; i < gSteps.size(); ++i)
	{
		const Step& s = gSteps[i];
		KFC_DIAG_LOG("    [%u] done=%d whole=%d set=%u<-%u layout=%u->%u stories=%u items=%u", (unsigned)i,
			s.done ? 1 : 0, s.whole ? 1 : 0, s.resultSet, s.resultSetBefore, s.layoutBefore, s.layoutAfter,
			(unsigned)s.stories.size(), (unsigned)s.items.size());
		for (size_t k = 0; k < s.stories.size(); ++k)
		{
			const StoryMoved& m = s.stories[k];
			uint32 v = 0;
			const bool read = (m.now != nil) && KFCSearchEngine::ReadStoryVersion(m.now, m.story, v);
			KFC_DIAG_LOG("      now=%p story=%u before=%u after=%u version=%s%u", (void*)m.now, m.story.Get(),
				m.before, m.after, read ? "" : "?", v);
		}
	}
}
#define KFC_DIAG_STEPS(when) DiagSteps(when)
#else
#define KFC_DIAG_STEPS(when) ((void)0)
#endif

// WHO TELLS US OF AN UNDO: THE DOCUMENT, NOT THE STORY.
// Every write leaves a mark in its own undo step (KFCUndoFollow::MarkWrite - kKFCUndoMarkCmdBoss raises a
// ModelChange on its document's subject), and a LAZY observer on that subject hears the mark again on the
// step's Undo and Redo ("At Undo, the runtimes queue up the same message ids that were queued up in Do",
// LazyNotificationData.h:50-58). KCM's ticks and paws ride the same road (KCMPageMarksCmd.cpp).
// NOT on each STORY a write moved, under IID_ITEXTMODEL. InDesign purges a story it is not using from memory -
// switching documents does it - and the story it builds again does not carry the attachment: measured in a test
// build (KFCDiag.h), the subject of the same story came back as another object and IsAttached said no, and the
// second document's Ctrl+Z went unheard 12 times in 18. A reference held on the story stopped the purge and
// every Undo the panel had to follow was followed (5 of 5, in four runs) - which is what proved it - but holding
// the model's objects is not how the product listens; it watches the document, which is not purged while it is
// open (the layer, links and timing panels attach their lazy observers to the document's subject).
// Attached at run time, never written into the document; let go of when the document closes (DocumentClosing). Whether a
// document is watched is the subject's own answer (IsAttached), asked on both sides - no list of our own to keep with it.

// THE SUBJECT A WRITE'S MARK IS RAISED ON AND HEARD ON - the document's own, its root's (kDocBoss): KFCUndoMarkCmd's
// DoNotify raises the mark there, WatchDoc attaches the observer there and DocumentClosing takes it off. nil = no
// document. The caller owns the reference.
ISubject* QueryMarkSubject(IDataBase* db)
{
	if (db == nil)
		return nil;
	const UID root = db->GetRootUID();
	if (root == kInvalidUID)
		return nil;
	InterfacePtr<ISubject> subject(db, root, IID_ISUBJECT);
	return subject.forget();
}

void WatchDoc(IDataBase* db)
{
	InterfacePtr<ISubject> subject(QueryMarkSubject(db));
	// On the same boss, asked for by OUR IID: kDocBoss carries other people's IID_IOBSERVER (KFC.fr).
	InterfacePtr<IObserver> observer(subject, IID_IKFCDOCUNDOOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (!subject->IsAttached(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER))
	{
		subject->AttachObserver(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER);
		KFC_DIAG_LOG("WATCH doc=%p subject=%p observer=%p", (void*)db, (void*)subject.get(), (void*)observer.get());
	}
}

// A write's name on the message line - the menu item that made it: the query run is the one write that reshapes the whole
// list; every other is a row's Replace.
const char* WriteName(const Step& step)
{
	return step.whole ? "Run Queries" : (step.name != nil ? step.name : "Replace");
}

// Keep a write: the documents' histories it cut, and the old ones past the limit, go.
void KeepStep(Step& step)
{
	DropOtherResultSets(&step);		// (a run being kept joins the set it found - the writes on it stay)
	ResolveDocs();
	// A NEW STEP IN A DOCUMENT THROWS ITS REDO AWAY (InDesign's own rule): an undone write sharing a
	// document with this one can never be done again. (An undone query run shares with every write - ShareDoc -
	// so any new write lets it go: InDesign may still do it again in its own documents, and the panel then does
	// not follow.)
	for (size_t i = gSteps.size(); i-- > 0; )
		if (!gSteps[i].done && ShareDoc(gSteps[i], step))
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	// (No watch here: MarkWrite has put the observer on the write's document already.)
	gSteps.push_back(std::move(step));

	// The limits (see kMaxSteps): the oldest first; past the whole-result-set limit, the oldest of those
	// with everything older than it.
	while (gSteps.size() > kMaxSteps)
		gSteps.erase(gSteps.begin());
	size_t wholeCount = 0;
	for (size_t i = 0; i < gSteps.size(); ++i)
		if (gSteps[i].whole)
			++wholeCount;
	while (wholeCount > kMaxWholeSteps)
	{
		size_t oldest = 0;
		while (oldest < gSteps.size() && !gSteps[oldest].whole)
			++oldest;
		gSteps.erase(gSteps.begin(), gSteps.begin() + static_cast<std::ptrdiff_t>(oldest + 1));
		--wholeCount;
	}
	KFC_DIAG_STEPS("kept");
}

}	// anonymous namespace

//========================================================================================
// The mark a write leaves, and the observer that hears it.
//========================================================================================

/** kKFCUndoMarkCmdBoss: changes nothing. Its work is DoNotify - the ModelChange its undo step will raise again
	on an Undo and a Redo. ItemList: the document's root (fItemList is also how the step knows its database). */
class KFCUndoMarkCmd : public Command
{
public:
	KFCUndoMarkCmd(IPMUnknown* boss) : Command(boss) {}
	virtual ~KFCUndoMarkCmd() {}

	/** kFalse, as the SDK's own commands answer: InDesign checks for low memory before running it - freeing part of the
		undo history if it must - instead of skipping the check (Command's kTrue, which the guide calls rare). */
	virtual bool16 LowMemIsOK() const { return kFalse; }

protected:
	/** Deliberately empty: the mark is the notification. */
	virtual void Do() {}
	virtual void DoNotify();
	virtual PMString* CreateName();
};

CREATE_PMINTERFACE(KFCUndoMarkCmd, kKFCUndoMarkCmdImpl)

void KFCUndoMarkCmd::DoNotify()
{
	InterfacePtr<ISubject> subject(QueryMarkSubject(fItemList.GetDataBase()));
	if (subject == nil)
		return;
	// A ModelChange, not a Change: only that is replayed on an Undo and a Redo, and only from a subject inside a
	// database with undo support (ISubject.h:61-82) - the document's own. No lazy data: the observer reads the
	// story versions again, which is what it does with a nil one anyway (IObserver.h:101-106).
	subject->ModelChange(kKFCUndoMarkCmdBoss, IID_IKFCUNDOMARK, this);
}

PMString* KFCUndoMarkCmd::CreateName()
{
	// Never on the Edit menu: it runs inside a write's own named sequence, whose name is the step's.
	PMString* name = new PMString("Kohaku Find/Change");
	name->SetTranslatable(kFalse);
	return name;
}

/** THE WORK OF THE OBSERVER (defined under "Following"). Every kept write an Undo or a Redo has moved - the newest one
	first for an Undo, the oldest first for a Redo, never past a write in the same document that has not moved (a query
	run, which puts the whole list back, counts as in every document) - has its rows put back, and the panel is drawn
	again and says so on its message line. Called when a write's mark is heard (its Do, Undo or Redo) - the versions say
	which. */
static void Follow();

/** The lazy observer AddIn'd on kDocBoss (KFC.fr): a write of KFC's was done, undone or redone in this document. */
class KFCDocUndoObserver : public CObserver
{
public:
	KFCDocUndoObserver(IPMUnknown* boss) : CObserver(boss, IID_IKFCDOCUNDOOBSERVER) {}
	virtual ~KFCDocUndoObserver() {}

	/** Deliberately empty: the work is in LazyUpdate, the only one of the two an Undo and a Redo reach. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy) {}

	/** The data is not read: it names neither what changed nor whether it was an Undo, and may be nil. The
		versions say (Follow). */
	virtual void LazyUpdate(ISubject* theSubject, const PMIID& protocol, const LazyNotificationData* data)
	{
		KFC_DIAG_LOG("LAZY doc subject=%p proto=0x%x data=%p main=%d", (void*)theSubject, protocol.Get(),
			(const void*)data, IDThreading::IsMainThreadDomain() ? 1 : 0);
		if (protocol != IID_IKFCUNDOMARK || theSubject == nil)
			return;
		// The main thread only (kModelPlugIn - the split's design, section 6): an Undo and a Redo
		// happen there, and the results it moves are the session's.
		if (!IDThreading::IsMainThreadDomain())
			return;
		Follow();
	}
};

CREATE_PMINTERFACE(KFCDocUndoObserver, kKFCDocUndoObserverImpl)

void KFCUndoFollow::MarkWrite(IDataBase* db)
{
	if (db == nil)
		return;
	// The observer first: the step's Undo and Redo are heard by whatever is attached when they happen.
	WatchDoc(db);
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kKFCUndoMarkCmdBoss));
	if (cmd == nil)
		return;
	cmd->SetItemList(UIDList(db, db->GetRootUID()));
	// A MARK THAT FAILS MUST NOT TAKE THE WRITE WITH IT. The write's sequence decides by the global
	// error state as it ends, and the write has already gone through: a mark that could not be processed costs
	// the panel this step's following, never the user's replace.
	// NOR IS ONE PUT OVER AN ERROR ALREADY STANDING: a command processed while the
	// global error code is set brings on InDesign's protective shutdown (CmdUtils.h, ProcessCommand), and the write's
	// sequence ends by that error anyway - rolled back, with nothing left for an Undo to take back.
	if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		KFC_DIAG_LOG("MARK doc=%p skipped - an error stands", (void*)db);
		return;
	}
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	KFC_DIAG_LOG("MARK doc=%p err=%d", (void*)db, (int)err);
}

//========================================================================================
// Recording.
//========================================================================================
KFCUndoFollow::StepRecorder::StepRecorder(int32 chapterIdx)
	: fOpen(true)
{
	Open(std::vector<int32>(1, chapterIdx));
}

KFCUndoFollow::StepRecorder::StepRecorder(const std::vector<int32>& chapterIdxs)
	: fOpen(true)
{
	Open(chapterIdxs);
}

void KFCUndoFollow::StepRecorder::Open(const std::vector<int32>& chapterIdxs)
{
	OpenRecording();
	for (size_t k = 0; k < chapterIdxs.size(); ++k)
		ReadChapterStories(chapterIdxs[k], gRecording.stories);
	KFCResultModel::BeginRowBackup();
}

KFCUndoFollow::StepRecorder::~StepRecorder()
{
	if (!fOpen)
		return;
	// Not kept: the write failed, or was a preview. The rows it changed go back (RollBackRows is harmless
	// where the caller has done it already: nothing is left).
	KFCResultModel::RollBackRows();
	CloseRecording();
}

void KFCUndoFollow::StepRecorder::Keep()
{
	if (!fOpen)
		return;
	fOpen = false;

	Step step;		// done (the write stands) - a write of rows (whole = false)
	step.resultSet = gRecording.resultSet;
	step.layoutBefore = gRecording.layout;
	TakeMovedStories(step);
	TakeMovedItems(step);
	step.name = gRecording.name;
	KFCResultModel::TakeRowBackup(step.rows);
	step.layoutAfter = step.layoutBefore;
	CloseRecording();

	KFC_DIAG_LOG("KEEP whole=%d moved=%u items=%u set=%u/%u", step.whole ? 1 : 0,
		(unsigned)step.stories.size(), (unsigned)step.items.size(), step.resultSet, KFCResultModel::GetResultSetId());
	// A write that moved no story and changed no item has nothing an Undo could take back; one that threw the results
	// away (a refused Replace clears them - KFCReplaceEngine::RefuseChangedQuery) has no rows left to follow.
	if ((step.stories.empty() && step.items.empty()) || step.resultSet != KFCResultModel::GetResultSetId())
		return;
	KeepStep(step);
}

void KFCUndoFollow::StepRecorder::SetName(const char* name)
{
	if (fOpen)
		gRecording.name = name;
}

void KFCUndoFollow::StepRecorder::RecordItem(int32 chapterIdx, UID item, bool withOrder)
{
	if (!fOpen || item == kInvalidUID)
		return;
	UIDRef found;
	IDFile file;
	if (!ChapterDocNow(chapterIdx, file, found))
		return;
	ItemMoved m;
	m.file = file;
	m.doc = found;
	m.item = item;
	m.now = found.GetDataBase();
	if (!KFCObjectSearch::Fingerprint(UIDRef(m.now, item), m.before, m.beforeLength))
		return;
	m.withOrder = withOrder;
	if (withOrder)
		m.orderBefore = KFCPageItemFacts::StackIndex(m.now, item);
	gRecording.items.push_back(m);
}

KFCUndoFollow::RunRecorder::RunRecorder()
	: fOpen(true)
{
	OpenRecording();		// its result set: the list as it stands - RestoreBefore goes back to it
	KFCResultModel::TakeModelSnapshot(gRecording.before);
}

KFCUndoFollow::RunRecorder::~RunRecorder()
{
	if (fOpen)
		CloseRecording();
}

void KFCUndoFollow::RunRecorder::ReadStories(const std::vector<KFCBookScope::ChapterDoc>& docs)
{
	for (size_t d = 0; d < docs.size(); ++d)
	{
		IDataBase* const db = docs[d].docRef.GetDataBase();
		InterfacePtr<IStoryList> storyList(db, db != nil ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
		if (storyList == nil)
			continue;
		const int32 count = storyList->GetAllTextModelCount();
		for (int32 i = 0; i < count; ++i)
			NoteStoryBefore(docs[d].file, docs[d].docRef, db, storyList->GetNthTextModelUID(i).GetUID(), gRecording.stories);
	}
}

void KFCUndoFollow::RunRecorder::Keep()
{
	if (!fOpen)
		return;
	fOpen = false;
	Step step;		// done (the run stands)
	step.whole = true;
	step.resultSet = KFCResultModel::GetResultSetId();		// the list the run left - a new result set
	step.resultSetBefore = gRecording.resultSet;			// ...and the one it found, which its Undo puts back
	step.layoutBefore = gRecording.layout;
	// The stories it moved - and no items: a query run records none (an Object query's Change All names none of what it
	// changed), so a run of Object queries alone is not followed (the spec map's OBJ-11).
	TakeMovedStories(step);
	step.before = std::move(gRecording.before);	// the list before the run, header and all - moved, not copied
	KFCResultModel::TakeModelSnapshot(step.after);
	step.layoutAfter = KFCResultModel::GetLayoutGeneration();
	CloseRecording();
	KFC_DIAG_LOG("KEEP RUN moved=%u set=%u<-%u", (unsigned)step.stories.size(), step.resultSet, step.resultSetBefore);
	if (step.stories.empty())
		return;
	KeepStep(step);
}

void KFCUndoFollow::RunRecorder::RestoreBefore()
{
	if (!fOpen)
		return;
	fOpen = false;
	KFCResultModel::RestoreModelSnapshot(gRecording.before);
	// ...and as the same result set: the writes kept for the list are followed again (KFCResultModel::ReturnToResultSet).
	KFCResultModel::ReturnToResultSet(gRecording.resultSet);
	CloseRecording();
}

//========================================================================================
// Following.
//========================================================================================
static void Follow()
{
	KFC_DIAG_LOG("FOLLOW recording=%d steps=%u running=%d", gRecording.open ? 1 : 0, (unsigned)gSteps.size(),
		KFCRunGuard::IsAnyRunning() ? 1 : 0);
	// A write of ours is standing (its own mark is heard as its sequence ends), or a run is up (a search, Change
	// All in Book or a query run pumps events behind its bar, and a row's Replace runs the message loop as it opens a
	// chapter or a window - KFCRunGuard::IsAnyRunning asks each of them).
	if (gRecording.open || gSteps.empty() || KFCRunGuard::IsAnyRunning())
		return;
	// (Only KFC's own writes leave a mark, so nothing but their Do, Undo and Redo comes here - typing does not.)
	DropOtherResultSets();
	ResolveDocs();
	KFC_DIAG_STEPS("resolved");
	if (gSteps.empty())
		return;

	// ONE WRITE AT A TIME, IN THE ORDER INDESIGN TAKES THEM. An Undo takes back the newest write
	// of a document, a Redo does the oldest undone one again - so a write is followed only when no later
	// standing write (for an Undo), or no earlier undone one (for a Redo), shares a document with it. The
	// loop goes on until nothing more moves: one notification can stand for several steps (a query run over
	// three chapters, or a Redo that is not followed until the next notification).
	int32 undone = 0, redone = 0;
	bool reshaped = false;
	const char* lastName = "";		// the name of the write followed last (WriteName)
	const size_t guard = 2 * gSteps.size() + 2;
	for (size_t g = 0; g < guard; ++g)
	{
		bool undo = true;
		size_t pick = gSteps.size();
		for (size_t i = gSteps.size(); i-- > 0 && pick == gSteps.size(); )
			if (gSteps[i].done && !NewerDoneSharesDoc(i) && AllAt(gSteps[i], false))
				pick = i;
		if (pick == gSteps.size())
		{
			undo = false;
			for (size_t i = 0; i < gSteps.size() && pick == gSteps.size(); ++i)
				if (!gSteps[i].done && !OlderUndoneSharesDoc(i) && AllAt(gSteps[i], true))
					pick = i;
		}
		KFC_DIAG_LOG("  pick=%d undo=%d", (pick == gSteps.size()) ? -1 : (int)pick, undo ? 1 : 0);
		if (pick == gSteps.size())
			break;
		Step& step = gSteps[pick];
		// A WRITE MADE ON A KEPT TIER'S LIST (the tier switch - KFCTierStore): its rows are numbered in that tier's copy, not
		// in the live list, so they go onto the copy - checked against the copy's layout as the live list's writes are checked
		// against the live one below - and the list on the panel does not change (the tier is grey: it shows the copy as it
		// was kept). (A query run's whole step never names a kept tier: a run that goes through lets the tiers go.)
		if (!step.whole && step.resultSet != KFCResultModel::GetResultSetId() && KFCTierStore::HoldsSet(step.resultSet))
		{
			if (KFCTierStore::LayoutOf(step.resultSet) != (undo ? step.layoutAfter : step.layoutBefore))
			{
				KFC_DIAG_LOG("  kept tier's layout mismatch - dropped %u", (unsigned)pick);
				gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(pick));
				continue;
			}
			(void)KFCTierStore::ApplyRowStep(step.resultSet, step.rows, !undo);
			step.done = !undo;
			if (undo)
				++undone;
			else
				++redone;
			lastName = WriteName(step);
			KFC_DIAG_LOG("  onto kept tier set=%u", step.resultSet);
			continue;
		}
		// THE ROWS ARE NUMBERED IN ONE LAYOUT. A write's rows are put back only onto the layout they
		// were taken from; any other (which the order above should never leave) means the write can no
		// longer be followed, and it is dropped rather than written onto the wrong rows.
		if (KFCResultModel::GetLayoutGeneration() != (undo ? step.layoutAfter : step.layoutBefore))
		{
			KFC_DIAG_LOG("  layout mismatch - dropped %u", (unsigned)pick);
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(pick));
			continue;
		}
		if (step.whole)
		{
			const std::vector<size_t>& frozen = undo ? step.frozenBefore : step.frozenAfter;
			if (frozen.empty())
				KFCResultModel::RestoreModelSnapshot(undo ? step.before : step.after);
			else
			{
				// a book chapter closed since keeps what the panel has for it (KeepFrozenAsTheyAre)
				KFCResultModel::ModelSnapshot set(undo ? step.before : step.after);
				KeepFrozenAsTheyAre(set, frozen);
				KFCResultModel::RestoreModelSnapshot(set);
			}
			// ...AS THAT LIST'S RESULT SET (DropOtherResultSets): the writes kept for it are followed next.
			KFCResultModel::ReturnToResultSet(undo ? step.resultSetBefore : step.resultSet);
			reshaped = true;
		}
		else
			KFCResultModel::ApplyRowStep(step.rows, !undo);
		step.done = !undo;
		if (undo)
			++undone;
		else
			++redone;
		lastName = WriteName(step);
	}
	KFC_DIAG_LOG("  followed undone=%d redone=%d", undone, redone);
	if (undone + redone == 0)
		return;

	// A reshaped result set is a new tree; rows put back are the same rows drawn again (RefreshRows keeps
	// what the user had opened and closed).
	if (reshaped)
		KFCNotifyRebuild();
	else
		KFCNotifyRefreshRows();
	PMString msg;
	msg.SetTranslatable(kFalse);
	if (undone > 0 && redone > 0)
	{
		msg.Append("Undo / Redo: the rows follow the document (");
		msg.AppendNumber(undone);
		msg.Append(" undone, ");
		msg.AppendNumber(redone);
		msg.Append(" redone).");
	}
	else
	{
		// One way only: "Undo: Replace - the rows are back as they were before it.", "Redo: 3 changes of ..."
		const bool undo = (undone > 0);
		const int32 count = undo ? undone : redone;
		msg.Append(undo ? "Undo: " : "Redo: ");
		if (count == 1)
			msg.Append(lastName);
		else
		{
			msg.AppendNumber(count);
			msg.Append(" changes of Kohaku Find/Change");
		}
		msg.Append(undo ? " - the rows are back as they were before " : " - the rows are as they were after ");
		msg.Append(count == 1 ? "it." : "them.");
	}
	KFCNotifyStatus(msg);
}

void KFCUndoFollow::DocumentClosing(const UIDRef& docRef)
{
	// The document is still whole at this signal (BeforeCloseDoc), so its subject can be asked - one never watched answers
	// that the observer is not attached, and nothing happens.
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<ISubject> subject(QueryMarkSubject(db));
	InterfacePtr<IObserver> observer(subject, IID_IKFCDOCUNDOOBSERVER);
	if (subject != nil && observer != nil
		&& subject->IsAttached(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER))
	{
		subject->DetachObserver(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER);
		KFC_DIAG_LOG("DOCCLOSING detach doc=%p", (void*)db);
	}
}

void KFCUndoFollow::ForgetClosingDocument(const UIDRef& docRef)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return;
	// A chapter of a kept BOOK's list: a chapter of a whole result set a book's search made (its header's fromBook) - found
	// by its file, as ForgetBookChapter finds it (the document is still open at this signal). A write of rows keeps no set.
	bool bookChapter = false;
	for (size_t i = 0; i < gSteps.size() && !bookChapter; ++i)
	{
		const KFCResultModel::ModelSnapshot* const sets[2] = { &gSteps[i].before, &gSteps[i].after };
		for (size_t s = 0; s < 2 && !bookChapter; ++s)
			for (size_t c = 0; sets[s]->header.fromBook && c < sets[s]->chapters.size() && !bookChapter; ++c)
				bookChapter = DocOfFile(sets[s]->chapters[c].file, sets[s]->chapters[c].docRef) == db;
	}
	if (bookChapter)
		ForgetBookChapter(docRef);
	else
		ForgetDocument(docRef);
}

void KFCUndoFollow::ForgetDocument(const UIDRef& docRef)
{
	// Its chapter in the list - by its binding, as the close responder finds it (KFCCloseDocResponder).
	int32 closing = -1;
	for (int32 c = 0; c < KFCResultModel::GetChapterCount() && closing < 0; ++c)
	{
		UIDRef chapterDoc;
		IDFile chapterFile;
		if (KFCResultModel::GetChapterLocation(c, chapterDoc, chapterFile) && chapterDoc == docRef)
			closing = c;
	}
	const uint32 current = KFCResultModel::GetResultSetId();
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		Step& step = gSteps[i];
		// Its chapter in the kept whole result sets - found by the document, not by the index: a set put back
		// in between can number its chapters otherwise.
		KFCResultModel::ModelSnapshot* const sets[2] = { &step.before, &step.after };
		for (size_t s = 0; s < 2; ++s)
			for (size_t c = 0; c < sets[s]->chapters.size(); ++c)
				if (sets[s]->chapters[c].docRef == docRef)
					KFCResultModel::EmptyChapter(sets[s]->chapters[c],
						docRef.GetDataBase() != nil ? docRef.GetDataBase()->GetSysFile() : nil);	// its file kept
		// Its stories and items (an object row's Replace). A write of rows of this document alone goes whole (and its rows
		// with it); one of several documents (rows selected together) keeps the others' and lets this one's rows go
		// (DropChapterRows); a query run keeps the stories of the documents still open.
		if (TakeDocOff(step, [&](const auto& x) { return x.doc == docRef; }))
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
		else if (!step.whole && closing >= 0 && step.resultSet == current)	// (row indices name the current list's rows only)
			DropChapterRows(step, closing);
		else if (!step.whole && step.resultSet != current && KFCTierStore::HoldsSet(step.resultSet))
		{
			// A write of a kept tier's list (the tier switch): its rows are numbered in the tier's copy - the closing chapter
			// is looked for there (a tier's chapter already emptied by KFCTierStore::ForgetDocument is not found, and its
			// rows are out of range for an Undo's put-back anyway).
			std::vector<int32> inTier;
			KFCTierStore::ChaptersOfDoc(step.resultSet, docRef.GetDataBase(), inTier);
			for (size_t c = 0; c < inTier.size(); ++c)
				DropChapterRows(step, inTier[c]);
		}
	}
}

void KFCUndoFollow::ForgetBookChapter(const UIDRef& docRef)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return;
	// THE KEPT WRITES AS THEY STAND FIRST - the order Follow and KeepStep take: the writes of another result set
	// go, and every write's documents are found again (ResolveDocs drops one with a document closed since; the one
	// closing now is still open at this signal). Each story's `now` was otherwise the document as the LAST follow
	// found it, which can be one closed since: the test build's trace below read a story's version through such a
	// pointer and brought InDesign down (a regression case in which a script closes a document). Nothing below reads
	// `now`; the trace does.
	DropOtherResultSets();
	ResolveDocs();
	// Its stories and items in each kept write - found as every follow finds them (DocOf, by the chapter's file): the
	// document is still open at this signal.
	const auto inChapter = [&](const auto& x) { return x.doc == docRef || DocOf(x) == db; };
	std::vector<bool> wroteIt(gSteps.size(), false);
	for (size_t i = 0; i < gSteps.size(); ++i)
		wroteIt[i] = std::any_of(gSteps[i].stories.begin(), gSteps[i].stories.end(), inChapter)
			|| std::any_of(gSteps[i].items.begin(), gSteps[i].items.end(), inChapter);
	// FROZEN in a whole result set only where that write, or one after it, wrote the chapter: the set's rows for it
	// then describe text its file may no longer hold (a write after it that goes now - its history goes with the
	// chapter - no longer even stands in the way of its Undo). A chapter nothing since wrote reads in the set as it
	// reads in its file, so it is put back with the set: frozen, it was EMPTIED whenever the list on screen had no
	// rows for it, and its rows were lost on the Undo (measured with Change Checked's report, which dropped such a
	// chapter).
	// Found in the set by its file the same way: a set's docRef can be one the chapter had before it was closed and
	// opened again.
	bool laterWrote = false;
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		laterWrote = laterWrote || wroteIt[i];
		Step& step = gSteps[i];
		if (!step.whole || !laterWrote)
			continue;
		const KFCResultModel::ModelSnapshot* const sets[2] = { &step.before, &step.after };
		std::vector<size_t>* const frozen[2] = { &step.frozenBefore, &step.frozenAfter };
		for (size_t s = 0; s < 2; ++s)
			for (size_t c = 0; c < sets[s]->chapters.size(); ++c)
			{
				const KFCResultModel::Chapter& chapter = sets[s]->chapters[c];
				if (DocOfFile(chapter.file, chapter.docRef) == db
					&& std::find(frozen[s]->begin(), frozen[s]->end(), c) == frozen[s]->end())
					frozen[s]->push_back(c);
			}
	}
	// The chapter in the list, found by its file the same way (its binding can be a document closed and opened again since).
	std::vector<int32> closing;
	for (int32 c = 0; c < KFCResultModel::GetChapterCount(); ++c)
	{
		UIDRef found;
		IDFile file;
		if (ChapterDocNow(c, file, found) && found.GetDataBase() == db)
			closing.push_back(c);
	}
	// ...and, for a write of rows kept for a list a query run put away (DropOtherResultSets), the chapter in THAT list:
	// its rows are numbered there. Read before any write goes below - a run's step can go with its stories.
	const uint32 current = KFCResultModel::GetResultSetId();
	std::vector<std::vector<int32> > closingInSet(gSteps.size());
	for (size_t i = 0; i < gSteps.size(); ++i)
		if (!gSteps[i].whole && gSteps[i].resultSet != current)
			ChaptersOfDocInKeptSet(gSteps[i].resultSet, db, closingInSet[i]);
	// ...then its stories and items come off every write. A write of rows of this chapter alone goes whole; one of several
	// chapters (rows selected together) keeps the others' and lets this chapter's rows go (DropChapterRows).
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		if (TakeDocOff(gSteps[i], inChapter))
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
		else if (!gSteps[i].whole)
		{
			const std::vector<int32>& chapters = (gSteps[i].resultSet == current) ? closing : closingInSet[i];
			for (size_t c = 0; c < chapters.size(); ++c)
				DropChapterRows(gSteps[i], chapters[c]);
		}
	}
	KFC_DIAG_STEPS("chapter closing");
}

void KFCUndoFollow::RenameDocument(const UIDRef& docRef, const PMString& newName)
{
	// Its chapter in each kept whole result set - by the binding, as ForgetDocument finds them. A set of a book's list keeps
	// its names (KFCResultModel::RenameDocumentChapters' rule, asked of the set as that asks it of the list).
	for (size_t i = 0; i < gSteps.size(); ++i)
	{
		KFCResultModel::ModelSnapshot* const sets[2] = { &gSteps[i].before, &gSteps[i].after };
		for (size_t s = 0; s < 2; ++s)
		{
			if (sets[s]->header.fromBook)
				continue;
			for (size_t c = 0; c < sets[s]->chapters.size(); ++c)
			{
				KFCResultModel::Chapter& chapter = sets[s]->chapters[c];
				if (chapter.docRef != docRef)
					continue;
				chapter.name = newName;
				chapter.name.SetTranslatable(kFalse);
			}
		}
	}
}

void KFCUndoFollow::ShutdownCleanup()
{
	// Assigning fresh vectors releases the storage too (the KESCL ShutdownCleanup rule): the kept writes
	// hold rows, and rows hold PMStrings.
	std::vector<Step>().swap(gSteps);
	CloseRecording();
}

// End, KFCUndoFollow.cpp.
