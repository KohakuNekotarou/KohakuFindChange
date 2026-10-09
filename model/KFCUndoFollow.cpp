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

#include <algorithm>		// std::find - the documents watched
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
#include "KFCObjectSearch.h"	// Fingerprint - an object row's item, before and after a write (1.4.0)
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

// One page item a write changed - an object row's Replace (1.4.0, spec O11): its fingerprint before and after the write
// (KFCObjectSearch::Fingerprint) - what an Undo and a Redo of it are told by, as a story's version is for text (measured:
// the plan's Task 1 M9 - an Undo brings the fingerprint back to "before", a Redo to "after").
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
	ItemMoved() : item(kInvalidUID), before(0), beforeLength(0), after(0), afterLength(0), now(nil) {}
};

// One kept write.
struct Step
{
	KFCUndoFollow::StepKind			kind;
	bool							done;			// the write stands (false: an Undo took it back)
	uint32							resultSet;		// KFCResultModel::GetResultSetId when it was made
	bool							whole;			// it reshaped the result set (a query run - RunRecorder)
	uint32							layoutBefore;	// the layout the row indices named before it...
	uint32							layoutAfter;	// ...and after it (the same, for a write of rows)
	std::vector<StoryMoved>			stories;
	std::vector<ItemMoved>			items;			// an object row's Replace: the item it changed (1.4.0)
	KFCResultModel::RowStep			rows;			// a write of rows: the rows before and after
	KFCResultModel::ModelSnapshot	before;			// a write of the whole result set: the set before...
	KFCResultModel::ModelSnapshot	after;			// ...and after
	std::vector<size_t>				frozenBefore;	// a whole set's book chapters closed since, as indexes into before's
	std::vector<size_t>				frozenAfter;	// and after's chapters: kept as the panel has them when that set is
													// put back (KFCUndoFollow::ForgetBookChapter)
	Step() : kind(KFCUndoFollow::kStepReplace), done(true), resultSet(0), whole(false), layoutBefore(0), layoutAfter(0) {}
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

// The recorder standing now (at most one - the writes of KFC never nest): what it read at its start.
bool gRecording = false;
std::vector<StoryMoved> gPendingStories;
std::vector<ItemMoved> gPendingItems;		// the items RecordItem read (1.4.0)
uint32 gPendingResultSet = 0;
uint32 gPendingLayout = 0;
KFCResultModel::ModelSnapshot gPendingBefore;

void CloseRecording()
{
	gRecording = false;
	std::vector<StoryMoved>().swap(gPendingStories);
	std::vector<ItemMoved>().swap(gPendingItems);
	gPendingBefore = KFCResultModel::ModelSnapshot();
}

// The document a story (or an object row's item) lives in, if it is open - by its chapter's file first, as every door
// of KFC finds a chapter's document (a UIDRef can outlive its document - KFCBookScope::ReachChapterDoc).
IDataBase* DocOfFile(const IDFile& file, const UIDRef& doc)
{
	UIDRef found = doc;
	if (!KFCBookScope::FindOpenChapterDoc(file, found))
		return nil;
	return found.GetDataBase();
}
IDataBase* DocOf(const StoryMoved& s) { return DocOfFile(s.file, s.doc); }
IDataBase* DocOf(const ItemMoved& m) { return DocOfFile(m.file, m.doc); }

// Every story holding a row of the chapter, with the version it has now. A chapter whose document is not
// open has none to read - and no write reaches it.
void ReadChapterStories(int32 chapterIdx, std::vector<StoryMoved>& out)
{
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file))
		return;
	UIDRef found = docRef;
	if (!KFCBookScope::FindOpenChapterDoc(file, found))
		return;
	IDataBase* const db = found.GetDataBase();
	std::set<UID> stories;
	KFCResultModel::GetChapterStories(chapterIdx, stories);
	for (std::set<UID>::const_iterator story = stories.begin(); story != stories.end(); ++story)
	{
		uint32 version = 0;
		if (!KFCSearchEngine::ReadStoryVersion(db, *story, version))
			continue;
		StoryMoved s;
		s.file = file;
		s.doc = found;
		s.story = *story;
		s.before = version;
		s.now = db;
		out.push_back(s);
	}
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

// The writes kept for another result set name rows that are gone (a search, a query run, Clear Results, a close).
void DropOtherResultSets()
{
	const uint32 current = KFCResultModel::GetResultSetId();
	for (size_t i = gSteps.size(); i-- > 0; )
		if (gSteps[i].resultSet != current)
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
}

// Do the two writes share a document? (InDesign keeps one history per document, so two that do are
// taken back and done again in order; two that do not, in any order.)
// A WRITE OF A WHOLE RESULT SET SHARES WITH EVERY WRITE. What a query run's step puts back is the WHOLE result
// set - every document's rows, the ones it never wrote to as well (ModelSnapshot) - so following its Undo or Redo
// in one document, unordered, would take back on the panel a Replace made since in another (measured with Change
// Checked, the first whole-set write: work/kbs-regress, uf-alldocs-*). Ordered against every write, it waits until
// the writes after it are undone, and a write made after its Undo throws its Redo away (KeepStep). One InDesign
// takes back or does again out of that order is not followed: the rows stay as they are, and the doors stand
// behind them as for any edit KFC did not make (the story's version, the row's text).
// (Since 1.4.0 a write's documents are its stories' AND its items' - DocsOf.)
void DocsOf(const Step& s, std::vector<IDataBase*>& out)
{
	for (size_t k = 0; k < s.stories.size(); ++k)
		out.push_back(s.stories[k].now);
	for (size_t k = 0; k < s.items.size(); ++k)
		out.push_back(s.items[k].now);
}

bool ShareDoc(const Step& a, const Step& b)
{
	if (a.whole || b.whole)
		return true;
	std::vector<IDataBase*> da, db;
	DocsOf(a, da);
	DocsOf(b, db);
	for (size_t x = 0; x < da.size(); ++x)
		for (size_t y = 0; y < db.size(); ++y)
			if (da[x] == db[y])
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
	for (size_t k = 0; k < gPendingStories.size(); ++k)
	{
		StoryMoved s = gPendingStories[k];
		s.now = DocOf(s);
		uint32 version = 0;
		if (s.now == nil || !KFCSearchEngine::ReadStoryVersion(s.now, s.story, version) || version == s.before)
			continue;
		s.after = version;
		s.afterText = StoryTextHash(s.now, s.story);
		step.stories.push_back(s);
	}
}

// The items the recording read that the write changed, each with its fingerprint after it, onto the step (1.4.0).
void TakeMovedItems(Step& step)
{
	for (size_t k = 0; k < gPendingItems.size(); ++k)
	{
		ItemMoved m = gPendingItems[k];
		m.now = DocOf(m);
		if (m.now == nil || !KFCObjectSearch::Fingerprint(UIDRef(m.now, m.item), m.after, m.afterLength))
			continue;
		if (m.after == m.before && m.afterLength == m.beforeLength)
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
	// AN OBJECT ROW'S ITEM (1.4.0): its fingerprint back at "before" (an Undo) or at "after" (a Redo).
	for (size_t k = 0; k < step.items.size(); ++k)
	{
		const ItemMoved& m = step.items[k];
		uint64 print = 0;
		uint32 length = 0;
		if (!KFCObjectSearch::Fingerprint(UIDRef(m.now, m.item), print, length))
			return false;
		if (print != (after ? m.after : m.before) || length != (after ? m.afterLength : m.beforeLength))
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
		KFC_DIAG_LOG("    [%u] kind=%d done=%d whole=%d set=%u layout=%u->%u stories=%u items=%u", (unsigned)i, (int)s.kind,
			s.done ? 1 : 0, s.whole ? 1 : 0, s.resultSet, s.layoutBefore, s.layoutAfter, (unsigned)s.stories.size(),
			(unsigned)s.items.size());
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
// Attached at run time, never written into the document; let go of when the document closes (DocumentClosing).
std::vector<IDataBase*> gWatchedDocs;	// compared, never dereferenced once their document has closed

void WatchDoc(IDataBase* db)
{
	if (db == nil)
		return;
	const UID root = db->GetRootUID();
	if (root == kInvalidUID)
		return;
	InterfacePtr<ISubject> subject(db, root, IID_ISUBJECT);
	// Asked for by OUR IID: kDocBoss carries other people's IID_IOBSERVER (KFC.fr).
	InterfacePtr<IObserver> observer(db, root, IID_IKFCDOCUNDOOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (!subject->IsAttached(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER))
	{
		subject->AttachObserver(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER);
		KFC_DIAG_LOG("WATCH doc=%p subject=%p observer=%p", (void*)db, (void*)subject.get(), (void*)observer.get());
	}
	if (std::find(gWatchedDocs.begin(), gWatchedDocs.end(), db) == gWatchedDocs.end())
		gWatchedDocs.push_back(db);
}

// A write's name on the message line - the menu item that made it.
const char* KindName(KFCUndoFollow::StepKind kind)
{
	switch (kind)
	{
		case KFCUndoFollow::kStepReplace:		return "Replace";
		case KFCUndoFollow::kStepRunQueries:	return "Run Queries";
	}
	return "a change";
}

// Keep a write: the documents' histories it cut, and the old ones past the limit, go.
void KeepStep(Step& step)
{
	DropOtherResultSets();
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

protected:
	/** Deliberately empty: the mark is the notification. */
	virtual void Do() {}
	virtual void DoNotify();
	virtual PMString* CreateName();
};

CREATE_PMINTERFACE(KFCUndoMarkCmd, kKFCUndoMarkCmdImpl)

void KFCUndoMarkCmd::DoNotify()
{
	IDataBase* const db = fItemList.GetDataBase();
	if (db == nil)
		return;
	const UID root = db->GetRootUID();
	if (root == kInvalidUID)
		return;
	InterfacePtr<ISubject> subject(db, root, IID_ISUBJECT);
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

/** The lazy observer AddIn'd on kDocBoss (KFC.fr): a write of KFC's was done, undone or redone in this document. */
class KFCDocUndoObserver : public CObserver
{
public:
	KFCDocUndoObserver(IPMUnknown* boss) : CObserver(boss, IID_IKFCDOCUNDOOBSERVER) {}
	virtual ~KFCDocUndoObserver() {}

	/** Deliberately empty: the work is in LazyUpdate, the only one of the two an Undo and a Redo reach. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy) {}

	/** The data is not read: it names neither what changed nor whether it was an Undo, and may be nil. The
		versions say (KFCUndoFollow::Follow). */
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
		(void)KFCUndoFollow::Follow();
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
	const ErrorCode before = ErrorUtils::PMGetGlobalErrorCode();
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess && before == kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	KFC_DIAG_LOG("MARK doc=%p err=%d", (void*)db, (int)err);
}

//========================================================================================
// Recording.
//========================================================================================
KFCUndoFollow::StepRecorder::StepRecorder(int32 chapterIdx)
	: fOpen(true)
{
	CloseRecording();
	gRecording = true;
	gPendingResultSet = KFCResultModel::GetResultSetId();
	gPendingLayout = KFCResultModel::GetLayoutGeneration();
	ReadChapterStories(chapterIdx, gPendingStories);
	KFCResultModel::BeginRowBackup();
}

KFCUndoFollow::StepRecorder::~StepRecorder()
{
	if (!fOpen)
		return;
	// Not kept: the write failed or was cancelled. The rows it changed go back (RollBackRows is harmless
	// where the caller has done it already: nothing is left).
	KFCResultModel::RollBackRows();
	CloseRecording();
}

void KFCUndoFollow::StepRecorder::Keep(StepKind kind)
{
	if (!fOpen)
		return;
	fOpen = false;

	Step step;		// done (the write stands) - a write of rows (whole = false)
	step.kind = kind;
	step.resultSet = gPendingResultSet;
	step.layoutBefore = gPendingLayout;
	TakeMovedStories(step);
	TakeMovedItems(step);
	KFCResultModel::TakeRowBackup(step.rows);
	step.layoutAfter = step.layoutBefore;
	CloseRecording();

	KFC_DIAG_LOG("KEEP kind=%d whole=%d moved=%u items=%u set=%u/%u", (int)kind, step.whole ? 1 : 0,
		(unsigned)step.stories.size(), (unsigned)step.items.size(), step.resultSet, KFCResultModel::GetResultSetId());
	// A write that moved no story and changed no item has nothing an Undo could take back; one that threw the results
	// away (a refused Replace clears them - KFCReplaceEngine::RefuseChangedQuery) has no rows left to follow.
	if ((step.stories.empty() && step.items.empty()) || step.resultSet != KFCResultModel::GetResultSetId())
		return;
	KeepStep(step);
}

void KFCUndoFollow::StepRecorder::RecordItem(int32 chapterIdx, UID item)
{
	if (!fOpen || item == kInvalidUID)
		return;
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file))
		return;
	UIDRef found = docRef;
	if (!KFCBookScope::FindOpenChapterDoc(file, found))
		return;
	ItemMoved m;
	m.file = file;
	m.doc = found;
	m.item = item;
	m.now = found.GetDataBase();
	if (!KFCObjectSearch::Fingerprint(UIDRef(m.now, item), m.before, m.beforeLength))
		return;
	gPendingItems.push_back(m);
}

KFCUndoFollow::RunRecorder::RunRecorder()
	: fOpen(true)
{
	CloseRecording();
	gRecording = true;
	gPendingResultSet = KFCResultModel::GetResultSetId();		// the list as it stands - RestoreBefore goes back to it
	gPendingLayout = KFCResultModel::GetLayoutGeneration();
	KFCResultModel::TakeModelSnapshot(gPendingBefore);
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
		{
			const UID story = storyList->GetNthTextModelUID(i).GetUID();
			uint32 version = 0;
			if (!KFCSearchEngine::ReadStoryVersion(db, story, version))
				continue;
			StoryMoved s;
			s.file = docs[d].file;
			s.doc = docs[d].docRef;
			s.story = story;
			s.before = version;
			s.now = db;
			gPendingStories.push_back(s);
		}
	}
}

void KFCUndoFollow::RunRecorder::Keep()
{
	if (!fOpen)
		return;
	fOpen = false;
	Step step;		// done (the run stands)
	step.kind = kStepRunQueries;
	step.whole = true;
	step.resultSet = KFCResultModel::GetResultSetId();		// the list the run left - a new result set
	step.layoutBefore = gPendingLayout;
	TakeMovedStories(step);
	TakeMovedItems(step);
	step.before = std::move(gPendingBefore);	// the list before the run, header and all - moved, not copied
	KFCResultModel::TakeModelSnapshot(step.after);
	step.layoutAfter = KFCResultModel::GetLayoutGeneration();
	CloseRecording();
	KFC_DIAG_LOG("KEEP RUN moved=%u items=%u set=%u", (unsigned)step.stories.size(), (unsigned)step.items.size(), step.resultSet);
	if (step.stories.empty() && step.items.empty())
		return;
	KeepStep(step);
}

void KFCUndoFollow::RunRecorder::RestoreBefore()
{
	if (!fOpen)
		return;
	fOpen = false;
	KFCResultModel::RestoreModelSnapshot(gPendingBefore);
	// ...and as the same result set: the writes kept for the list are followed again (KFCResultModel::ReturnToResultSet).
	KFCResultModel::ReturnToResultSet(gPendingResultSet);
	CloseRecording();
}

//========================================================================================
// Following.
//========================================================================================
bool KFCUndoFollow::Follow()
{
	KFC_DIAG_LOG("FOLLOW recording=%d steps=%u running=%d", gRecording ? 1 : 0, (unsigned)gSteps.size(),
		KFCRunGuard::IsAnyRunning() ? 1 : 0);
	// A write of ours is standing (its own mark is heard as its sequence ends), or a run is up (a search, Change
	// All in Book or a query run pumps events behind its bar - KFCRunGuard counts all three).
	if (gRecording || gSteps.empty() || KFCRunGuard::IsAnyRunning())
		return false;
	// (Only KFC's own writes leave a mark, so nothing but their Do, Undo and Redo comes here - typing does not.)
	DropOtherResultSets();
	ResolveDocs();
	KFC_DIAG_STEPS("resolved");
	if (gSteps.empty())
		return false;

	// ONE WRITE AT A TIME, IN THE ORDER INDESIGN TAKES THEM. An Undo takes back the newest write
	// of a document, a Redo does the oldest undone one again - so a write is followed only when no later
	// standing write (for an Undo), or no earlier undone one (for a Redo), shares a document with it. The
	// loop goes on until nothing more moves: one notification can stand for several steps (a query run over
	// three chapters, or a Redo that is not followed until the next notification).
	int32 undone = 0, redone = 0;
	bool reshaped = false;
	KFCUndoFollow::StepKind lastKind = kStepReplace;
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
			reshaped = true;
		}
		else
			KFCResultModel::ApplyRowStep(step.rows, !undo);
		step.done = !undo;
		if (undo)
			++undone;
		else
			++redone;
		lastKind = step.kind;
	}
	KFC_DIAG_LOG("  followed undone=%d redone=%d", undone, redone);
	if (undone + redone == 0)
		return false;

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
			msg.Append(KindName(lastKind));
		else
		{
			msg.AppendNumber(count);
			msg.Append(" changes of Kohaku Find/Change");
		}
		msg.Append(undo ? " - the rows are back as they were before " : " - the rows are as they were after ");
		msg.Append(count == 1 ? "it." : "them.");
	}
	KFCNotifyStatus(msg);
	return true;
}

void KFCUndoFollow::DocumentClosing(const UIDRef& docRef)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return;
	const std::vector<IDataBase*>::iterator watched = std::find(gWatchedDocs.begin(), gWatchedDocs.end(), db);
	if (watched == gWatchedDocs.end())
		return;
	gWatchedDocs.erase(watched);
	// The document is still whole at this signal (BeforeCloseDoc), so its subject can be asked.
	const UID root = db->GetRootUID();
	InterfacePtr<ISubject> subject(db, root, IID_ISUBJECT);
	InterfacePtr<IObserver> observer(db, root, IID_IKFCDOCUNDOOBSERVER);
	if (subject != nil && observer != nil
		&& subject->IsAttached(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER))
		subject->DetachObserver(ISubject::kLazyAttachment, observer, IID_IKFCUNDOMARK, IID_IKFCDOCUNDOOBSERVER);
	KFC_DIAG_LOG("DOCCLOSING detach doc=%p", (void*)db);
}

void KFCUndoFollow::ForgetDocument(const UIDRef& docRef)
{
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		Step& step = gSteps[i];
		// Its chapter in the kept whole result sets - found by the document, not by the index: a set put back
		// in between can number its chapters otherwise.
		KFCResultModel::ModelSnapshot* const sets[2] = { &step.before, &step.after };
		for (size_t s = 0; s < 2; ++s)
			for (size_t c = 0; c < sets[s]->chapters.size(); ++c)
				if (sets[s]->chapters[c].docRef == docRef)
					KFCResultModel::EmptyChapter(sets[s]->chapters[c]);
		// Its stories. A write of rows is one document's, so one of this document goes whole (and its rows
		// with it); a query run keeps the stories of the documents still open.
		std::vector<StoryMoved>& stories = step.stories;
		for (size_t k = stories.size(); k-- > 0; )
			if (stories[k].doc == docRef)
				stories.erase(stories.begin() + static_cast<std::ptrdiff_t>(k));
		// ...and its items (an object row's Replace - 1.4.0), the same way.
		std::vector<ItemMoved>& items = step.items;
		for (size_t k = items.size(); k-- > 0; )
			if (items[k].doc == docRef)
				items.erase(items.begin() + static_cast<std::ptrdiff_t>(k));
		if (stories.empty() && items.empty())
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
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
	// pointer and brought InDesign down (the regression case book-empty-keeps-results - a script
	// closing a document). Nothing below reads `now`; the trace does.
	DropOtherResultSets();
	ResolveDocs();
	// Its stories in each kept write - found as every follow finds them (DocOf, by the chapter's file): the document
	// is still open at this signal.
	auto inChapter = [&](const StoryMoved& story) { return story.doc == docRef || DocOf(story) == db; };
	auto itemInChapter = [&](const ItemMoved& m) { return m.doc == docRef || DocOf(m) == db; };	// (1.4.0)
	std::vector<bool> wroteIt(gSteps.size(), false);
	for (size_t i = 0; i < gSteps.size(); ++i)
	{
		for (size_t k = 0; k < gSteps[i].stories.size() && !wroteIt[i]; ++k)
			wroteIt[i] = inChapter(gSteps[i].stories[k]);
		for (size_t k = 0; k < gSteps[i].items.size() && !wroteIt[i]; ++k)
			wroteIt[i] = itemInChapter(gSteps[i].items[k]);
	}
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
				UIDRef found = chapter.docRef;
				if (KFCBookScope::FindOpenChapterDoc(chapter.file, found) && found.GetDataBase() == db
					&& std::find(frozen[s]->begin(), frozen[s]->end(), c) == frozen[s]->end())
					frozen[s]->push_back(c);
			}
	}
	// ...then its stories come off every write; a write of rows is one document's, so one of this chapter goes whole.
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		std::vector<StoryMoved>& stories = gSteps[i].stories;
		for (size_t k = stories.size(); k-- > 0; )
			if (inChapter(stories[k]))
				stories.erase(stories.begin() + static_cast<std::ptrdiff_t>(k));
		std::vector<ItemMoved>& items = gSteps[i].items;
		for (size_t k = items.size(); k-- > 0; )
			if (itemInChapter(items[k]))
				items.erase(items.begin() + static_cast<std::ptrdiff_t>(k));
		if (stories.empty() && items.empty())
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	}
	KFC_DIAG_STEPS("chapter closing");
}

void KFCUndoFollow::ShutdownCleanup()
{
	// Assigning fresh vectors releases the storage too (the KESCL ShutdownCleanup rule): the kept writes
	// hold rows, and rows hold PMStrings.
	std::vector<Step>().swap(gSteps);
	std::vector<IDataBase*>().swap(gWatchedDocs);	// (the documents are gone by now; nothing is detached here)
	CloseRecording();
}

// End, KFCUndoFollow.cpp.
