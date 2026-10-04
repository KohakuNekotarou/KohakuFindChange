//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  KBSUndoFollow.cpp -- see the header.
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
#include "textiterator.h"			// StoryTextHash
#include "UIDList.h"

#include <algorithm>		// std::find - the documents watched
#include <set>
#include <utility>			// std::move - a kept write carries its rows

// Project includes:
#include "KBSID.h"
#include "KBSBookScope.h"		// FindOpenChapterDoc - a chapter's document found again by its file
#include "KBSDiag.h"			// KBS_DIAG_LOG - the test build's trace (compiled out of a shipping build)
#include "KBSResultModel.h"
#include "KBSModelNotify.h"		// the panel drawn again, and its message line - told, never called
#include "KBSRunGuard.h"		// IsAnyRunning - nothing is followed while any run of ours (the replace among them) is up
#include "KBSSearchEngine.h"	// ReadStoryVersion - a story's version
#include "KBSUndoFollow.h"

namespace
{

// One story a write moved: its version before and after the write.
struct StoryMoved
{
	IDFile		file;		// its chapter's file - how the document is found again (KBSBookScope::FindOpenChapterDoc)
	UIDRef		doc;		// its document as the write found it - the only handle a document without a file has
	UID			story;
	uint32		before;
	uint32		after;
	uint64		afterText;	// its text right after the write, as one number (StoryTextHash) - what tells a Redo from
							// the same count of edits made after an Undo (the head of KBSUndoFollow.h)
	IDataBase*	now;		// the document as it was last found (ResolveDocs); nil = not open
	StoryMoved() : story(kInvalidUID), before(0), after(0), afterText(0), now(nil) {}
};

// One kept write.
struct Step
{
	KBSUndoFollow::StepKind			kind;
	bool							done;			// the write stands (false: an Undo took it back)
	uint32							resultSet;		// KBSResultModel::GetResultSetId when it was made
	bool							whole;			// it reshaped the result set (Change Checked)
	uint32							layoutBefore;	// the layout the row indices named before it...
	uint32							layoutAfter;	// ...and after it (the same, for a write of rows)
	std::vector<StoryMoved>			stories;
	KBSResultModel::RowStep			rows;			// a write of rows: the rows before and after
	KBSResultModel::ModelSnapshot	before;			// a write of the whole result set: the set before...
	KBSResultModel::ModelSnapshot	after;			// ...and after
	std::vector<size_t>				frozenBefore;	// a whole set's book chapters closed since, as indexes into before's
	std::vector<size_t>				frozenAfter;	// and after's chapters: kept as the panel has them when that set is
													// put back (KBSUndoFollow::ForgetBookChapter)
	Step() : kind(KBSUndoFollow::kStepReplace), done(true), resultSet(0), whole(false), layoutBefore(0), layoutAfter(0) {}
};

// The kept writes, oldest first.
std::vector<Step> gSteps;

// HOW MANY ARE KEPT. InDesign's own history is longer, but a write older than this is rarely undone through
// thirty of KBS's own; one that is simply stops being followed (its rows stay as they are). A whole result
// set is heavy (a large search holds thousands of rows),
// so only the last three of those are kept, with everything older than the oldest of them: a write older
// than a Change Checked names the rows in the layout before it, and could only be reached by undoing that
// Change Checked first.
const size_t kMaxSteps = 30;
const size_t kMaxWholeSteps = 3;

// The recorder standing now (at most one - the writes of KBS never nest): what it read at its start.
bool gRecording = false;
std::vector<StoryMoved> gPendingStories;
bool gPendingWhole = false;
uint32 gPendingResultSet = 0;
uint32 gPendingLayout = 0;
KBSResultModel::ModelSnapshot gPendingBefore;

void CloseRecording()
{
	gRecording = false;
	std::vector<StoryMoved>().swap(gPendingStories);
	gPendingBefore = KBSResultModel::ModelSnapshot();
}

// The document a story lives in, if it is open - by its chapter's file first, as every door of KBS finds a
// chapter's document (a UIDRef can outlive its document - KBSBookScope::ReachChapterDoc).
IDataBase* DocOf(const StoryMoved& s)
{
	UIDRef found = s.doc;
	if (!KBSBookScope::FindOpenChapterDoc(s.file, found))
		return nil;
	return found.GetDataBase();
}

// Every story holding a row of the chapter, with the version it has now. A chapter whose document is not
// open has none to read - and no write reaches it.
void ReadChapterStories(int32 chapterIdx, std::vector<StoryMoved>& out)
{
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file))
		return;
	UIDRef found = docRef;
	if (!KBSBookScope::FindOpenChapterDoc(file, found))
		return;
	IDataBase* const db = found.GetDataBase();
	std::set<UID> stories;
	KBSResultModel::GetChapterStories(chapterIdx, stories);
	for (std::set<UID>::const_iterator story = stories.begin(); story != stories.end(); ++story)
	{
		uint32 version = 0;
		if (!KBSSearchEngine::ReadStoryVersion(db, *story, version))
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
		if (!allOpen)
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	}
}

// The writes kept for another result set name rows that are gone (a search, Show Changes, a close).
void DropOtherResultSets()
{
	const uint32 current = KBSResultModel::GetResultSetId();
	for (size_t i = gSteps.size(); i-- > 0; )
		if (gSteps[i].resultSet != current)
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
}

// Do the two writes share a document? (InDesign keeps one history per document, so two that do are
// taken back and done again in order; two that do not, in any order.)
// A CHANGE CHECKED SHARES WITH EVERY WRITE. What it puts back is the WHOLE result set - every document's rows,
// the ones it never wrote to as well (ModelSnapshot) - so following its Undo or Redo in one document, unordered,
// takes back on the panel a Replace or a Reject made since in another: measured, the regression cases
// uf-alldocs-cc-undo-other-replace-redo and uf-alldocs-replace-cc-reject-undo (work/kbs-regress). Ordered against
// every write, it waits until the writes after it are undone, and a write made after its Undo throws its Redo
// away (KeepStep). One InDesign takes back or does again out of that order is not followed: the rows stay as
// they are, and the doors stand behind them as for any edit KBS did not make (the story's version, the
// records' times).
bool ShareDoc(const Step& a, const Step& b)
{
	if (a.whole || b.whole)
		return true;
	for (size_t x = 0; x < a.stories.size(); ++x)
		for (size_t y = 0; y < b.stories.size(); ++y)
			if (a.stories[x].now == b.stories[y].now)
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
		if (!KBSSearchEngine::ReadStoryVersion(s.now, s.story, version))
			return false;
		if (version != (after ? s.after : s.before))
			return false;
		if (after && StoryTextHash(s.now, s.story) != s.afterText)
			return false;
	}
	return !step.stories.empty();
}

// A whole result set to put back, its FROZEN chapters (indexes into it) - a book's, closed since
// (KBSUndoFollow::ForgetBookChapter) - given the rows the panel has for them now, found by the chapter's file
// (FileUtils::IsEqual, as KBSBookScope compares them): an Undo in another chapter does not reach their file. A
// chapter the panel no longer holds rows for is emptied in place (KBSResultModel::EmptyChapter - its place is kept).
void KeepFrozenAsTheyAre(KBSResultModel::ModelSnapshot& set, const std::vector<size_t>& frozen)
{
	if (frozen.empty())
		return;
	KBSResultModel::ModelSnapshot now;
	KBSResultModel::TakeModelSnapshot(now);
	for (size_t f = 0; f < frozen.size(); ++f)
	{
		if (frozen[f] >= set.chapters.size())
			continue;
		KBSResultModel::Chapter& chapter = set.chapters[frozen[f]];
		size_t n = 0;
		while (n < now.chapters.size() && FileUtils::IsEqual(now.chapters[n].file, chapter.file) == kFalse)
			++n;
		if (n < now.chapters.size())
			chapter = now.chapters[n];
		else
			KBSResultModel::EmptyChapter(chapter);
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

#ifdef KBS_DIAG
// The test build's picture of every kept write: its stories with the versions it waits for and the one each
// story has now - what decides a follow, laid out (KBSDiag.h).
void DiagSteps(const char* when)
{
	KBS_DIAG_LOG("  STEPS %s n=%u set(now)=%u layout(now)=%u", when, (unsigned)gSteps.size(),
		KBSResultModel::GetResultSetId(), KBSResultModel::GetLayoutGeneration());
	for (size_t i = 0; i < gSteps.size(); ++i)
	{
		const Step& s = gSteps[i];
		KBS_DIAG_LOG("    [%u] kind=%d done=%d whole=%d set=%u layout=%u->%u stories=%u", (unsigned)i, (int)s.kind,
			s.done ? 1 : 0, s.whole ? 1 : 0, s.resultSet, s.layoutBefore, s.layoutAfter, (unsigned)s.stories.size());
		for (size_t k = 0; k < s.stories.size(); ++k)
		{
			const StoryMoved& m = s.stories[k];
			uint32 v = 0;
			const bool read = (m.now != nil) && KBSSearchEngine::ReadStoryVersion(m.now, m.story, v);
			KBS_DIAG_LOG("      now=%p story=%u before=%u after=%u version=%s%u", (void*)m.now, m.story.Get(),
				m.before, m.after, read ? "" : "?", v);
		}
	}
}
#define KBS_DIAG_STEPS(when) DiagSteps(when)
#else
#define KBS_DIAG_STEPS(when) ((void)0)
#endif

// WHO TELLS US OF AN UNDO: THE DOCUMENT, NOT THE STORY.
// Every write leaves a mark in its own undo step (KBSUndoFollow::MarkWrite - kKBSUndoMarkCmdBoss raises a
// ModelChange on its document's subject), and a LAZY observer on that subject hears the mark again on the
// step's Undo and Redo ("At Undo, the runtimes queue up the same message ids that were queued up in Do",
// LazyNotificationData.h:50-58). KCM's ticks and paws ride the same road (KCMPageMarksCmd.cpp).
// NOT on each STORY a write moved, under IID_ITEXTMODEL. InDesign purges a story it is not using from memory -
// switching documents does it - and the story it builds again does not carry the attachment: measured in a test
// build (KBSDiag.h), the subject of the same story came back as another object and IsAttached said no, and the
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
	// Asked for by OUR IID: kDocBoss carries other people's IID_IOBSERVER (KBS.fr).
	InterfacePtr<IObserver> observer(db, root, IID_IKBSDOCUNDOOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (!subject->IsAttached(ISubject::kLazyAttachment, observer, IID_IKBSUNDOMARK, IID_IKBSDOCUNDOOBSERVER))
	{
		subject->AttachObserver(ISubject::kLazyAttachment, observer, IID_IKBSUNDOMARK, IID_IKBSDOCUNDOOBSERVER);
		KBS_DIAG_LOG("WATCH doc=%p subject=%p observer=%p", (void*)db, (void*)subject.get(), (void*)observer.get());
	}
	if (std::find(gWatchedDocs.begin(), gWatchedDocs.end(), db) == gWatchedDocs.end())
		gWatchedDocs.push_back(db);
}

// A write's name on the message line - the menu item that made it.
const char* KindName(KBSUndoFollow::StepKind kind)
{
	switch (kind)
	{
		case KBSUndoFollow::kStepChangeChecked:	return "Change Checked";
		case KBSUndoFollow::kStepReplace:		return "Replace";
		case KBSUndoFollow::kStepReplaceAgain:	return "Replace Again";
		case KBSUndoFollow::kStepReject:		return "Reject Change";
		case KBSUndoFollow::kStepAccept:		return "Accept Change";
		case KBSUndoFollow::kStepAcceptAll:		return "Accept All Changes by KohakuFindChange";
	}
	return "a change";
}

// Keep a write: the documents' histories it cut, and the old ones past the limit, go.
void KeepStep(Step& step)
{
	DropOtherResultSets();
	ResolveDocs();
	// A NEW STEP IN A DOCUMENT THROWS ITS REDO AWAY (InDesign's own rule): an undone write sharing a
	// document with this one can never be done again. (An undone Change Checked shares with every write -
	// ShareDoc - so any new write lets it go: InDesign may still do it again in its own document, and the panel
	// then does not follow.)
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
	KBS_DIAG_STEPS("kept");
}

}	// anonymous namespace

//========================================================================================
// The mark a write leaves, and the observer that hears it.
//========================================================================================

/** kKBSUndoMarkCmdBoss: changes nothing. Its work is DoNotify - the ModelChange its undo step will raise again
	on an Undo and a Redo. ItemList: the document's root (fItemList is also how the step knows its database). */
class KBSUndoMarkCmd : public Command
{
public:
	KBSUndoMarkCmd(IPMUnknown* boss) : Command(boss) {}
	virtual ~KBSUndoMarkCmd() {}

protected:
	/** Deliberately empty: the mark is the notification. */
	virtual void Do() {}
	virtual void DoNotify();
	virtual PMString* CreateName();
};

CREATE_PMINTERFACE(KBSUndoMarkCmd, kKBSUndoMarkCmdImpl)

void KBSUndoMarkCmd::DoNotify()
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
	subject->ModelChange(kKBSUndoMarkCmdBoss, IID_IKBSUNDOMARK, this);
}

PMString* KBSUndoMarkCmd::CreateName()
{
	// Never on the Edit menu: it runs inside a write's own named sequence, whose name is the step's.
	PMString* name = new PMString("Kohaku Find/Change");
	name->SetTranslatable(kFalse);
	return name;
}

/** The lazy observer AddIn'd on kDocBoss (KBS.fr): a write of KBS's was done, undone or redone in this document. */
class KBSDocUndoObserver : public CObserver
{
public:
	KBSDocUndoObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSDOCUNDOOBSERVER) {}
	virtual ~KBSDocUndoObserver() {}

	/** Deliberately empty: the work is in LazyUpdate, the only one of the two an Undo and a Redo reach. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy) {}

	/** The data is not read: it names neither what changed nor whether it was an Undo, and may be nil. The
		versions say (KBSUndoFollow::Follow). */
	virtual void LazyUpdate(ISubject* theSubject, const PMIID& protocol, const LazyNotificationData* data)
	{
		KBS_DIAG_LOG("LAZY doc subject=%p proto=0x%x data=%p main=%d", (void*)theSubject, protocol.Get(),
			(const void*)data, IDThreading::IsMainThreadDomain() ? 1 : 0);
		if (protocol != IID_IKBSUNDOMARK || theSubject == nil)
			return;
		// The main thread only (kModelPlugIn - the split's design, section 6): an Undo and a Redo
		// happen there, and the results it moves are the session's.
		if (!IDThreading::IsMainThreadDomain())
			return;
		(void)KBSUndoFollow::Follow();
	}
};

CREATE_PMINTERFACE(KBSDocUndoObserver, kKBSDocUndoObserverImpl)

void KBSUndoFollow::MarkWrite(IDataBase* db)
{
	if (db == nil)
		return;
	// The observer first: the step's Undo and Redo are heard by whatever is attached when they happen.
	WatchDoc(db);
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kKBSUndoMarkCmdBoss));
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
	KBS_DIAG_LOG("MARK doc=%p err=%d", (void*)db, (int)err);
}

//========================================================================================
// Recording.
//========================================================================================
KBSUndoFollow::StepRecorder::StepRecorder(const std::vector<int32>& chapters, bool wholeResultSet)
	: fOpen(true)
{
	CloseRecording();
	gRecording = true;
	gPendingWhole = wholeResultSet;
	gPendingResultSet = KBSResultModel::GetResultSetId();
	gPendingLayout = KBSResultModel::GetLayoutGeneration();
	for (size_t i = 0; i < chapters.size(); ++i)
		ReadChapterStories(chapters[i], gPendingStories);
	if (wholeResultSet)
		KBSResultModel::TakeModelSnapshot(gPendingBefore);
	KBSResultModel::BeginRowBackup();
}

KBSUndoFollow::StepRecorder::~StepRecorder()
{
	if (!fOpen)
		return;
	// Not kept: the write failed or was cancelled. The rows it changed go back (RollBackRows is harmless
	// where the caller has done it already: nothing is left).
	KBSResultModel::RollBackRows();
	CloseRecording();
}

void KBSUndoFollow::StepRecorder::Keep(StepKind kind)
{
	if (!fOpen)
		return;
	fOpen = false;

	Step step;		// done (the write stands)
	step.kind = kind;
	step.resultSet = gPendingResultSet;
	step.whole = gPendingWhole;
	step.layoutBefore = gPendingLayout;
	// the stories the write moved - the rest cannot tell an Undo of it from anything else
	for (size_t k = 0; k < gPendingStories.size(); ++k)
	{
		StoryMoved s = gPendingStories[k];
		s.now = DocOf(s);
		uint32 version = 0;
		if (s.now == nil || !KBSSearchEngine::ReadStoryVersion(s.now, s.story, version) || version == s.before)
			continue;
		s.after = version;
		s.afterText = StoryTextHash(s.now, s.story);
		step.stories.push_back(s);
	}
	if (step.whole)
	{
		KBSResultModel::ForgetRowBackup();		// the whole set is copied instead (Change Checked has ended it already)
		step.before = std::move(gPendingBefore);	// moved, not copied: a large search's set is not held twice
		KBSResultModel::TakeModelSnapshot(step.after);
		step.layoutAfter = KBSResultModel::GetLayoutGeneration();
	}
	else
	{
		KBSResultModel::TakeRowBackup(step.rows);
		step.layoutAfter = step.layoutBefore;
	}
	CloseRecording();

	KBS_DIAG_LOG("KEEP kind=%d whole=%d moved=%u set=%u/%u", (int)kind, step.whole ? 1 : 0,
		(unsigned)step.stories.size(), step.resultSet, KBSResultModel::GetResultSetId());
	// A write that moved no story has nothing an Undo could take back; one that threw the results away
	// (a refused Replace clears them - KBSReplaceEngine::RefuseChangedQuery) has no rows left to follow.
	if (step.stories.empty() || step.resultSet != KBSResultModel::GetResultSetId())
		return;
	KeepStep(step);
}

//========================================================================================
// Following.
//========================================================================================
bool KBSUndoFollow::Follow()
{
	KBS_DIAG_LOG("FOLLOW recording=%d steps=%u running=%d", gRecording ? 1 : 0, (unsigned)gSteps.size(),
		KBSRunGuard::IsAnyRunning() ? 1 : 0);
	// A write of ours is standing (its own mark is heard as its sequence ends), or a run is up (a search, a
	// replace or Show Changes pumps events behind its bar - KBSRunGuard counts all three).
	if (gRecording || gSteps.empty() || KBSRunGuard::IsAnyRunning())
		return false;
	// (Only KBS's own writes leave a mark, so nothing but their Do, Undo and Redo comes here - typing does not.)
	DropOtherResultSets();
	ResolveDocs();
	KBS_DIAG_STEPS("resolved");
	if (gSteps.empty())
		return false;

	// ONE WRITE AT A TIME, IN THE ORDER INDESIGN TAKES THEM. An Undo takes back the newest write
	// of a document, a Redo does the oldest undone one again - so a write is followed only when no later
	// standing write (for an Undo), or no earlier undone one (for a Redo), shares a document with it. The
	// loop goes on until nothing more moves: one notification can stand for several steps (a Change Checked
	// over three chapters, or a Redo that is not followed until the next notification).
	int32 undone = 0, redone = 0;
	bool reshaped = false;
	KBSUndoFollow::StepKind lastKind = kStepReplace;
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
		KBS_DIAG_LOG("  pick=%d undo=%d", (pick == gSteps.size()) ? -1 : (int)pick, undo ? 1 : 0);
		if (pick == gSteps.size())
			break;
		Step& step = gSteps[pick];
		// THE ROWS ARE NUMBERED IN ONE LAYOUT. A write's rows are put back only onto the layout they
		// were taken from; any other (which the order above should never leave) means the write can no
		// longer be followed, and it is dropped rather than written onto the wrong rows.
		if (KBSResultModel::GetLayoutGeneration() != (undo ? step.layoutAfter : step.layoutBefore))
		{
			KBS_DIAG_LOG("  layout mismatch - dropped %u", (unsigned)pick);
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(pick));
			continue;
		}
		if (step.whole)
		{
			const std::vector<size_t>& frozen = undo ? step.frozenBefore : step.frozenAfter;
			if (frozen.empty())
				KBSResultModel::RestoreModelSnapshot(undo ? step.before : step.after);
			else
			{
				// a book chapter closed since keeps what the panel has for it (KeepFrozenAsTheyAre)
				KBSResultModel::ModelSnapshot set(undo ? step.before : step.after);
				KeepFrozenAsTheyAre(set, frozen);
				KBSResultModel::RestoreModelSnapshot(set);
			}
			reshaped = true;
		}
		else
			KBSResultModel::ApplyRowStep(step.rows, !undo);
		step.done = !undo;
		if (undo)
			++undone;
		else
			++redone;
		lastKind = step.kind;
	}
	KBS_DIAG_LOG("  followed undone=%d redone=%d", undone, redone);
	if (undone + redone == 0)
		return false;

	// A reshaped result set is a new tree; rows put back are the same rows drawn again (RefreshRows keeps
	// what the user had opened and closed).
	if (reshaped)
		KBSNotifyRebuild();
	else
		KBSNotifyRefreshRows();
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
	KBSNotifyStatus(msg);
	return true;
}

void KBSUndoFollow::DocumentClosing(const UIDRef& docRef)
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
	InterfacePtr<IObserver> observer(db, root, IID_IKBSDOCUNDOOBSERVER);
	if (subject != nil && observer != nil
		&& subject->IsAttached(ISubject::kLazyAttachment, observer, IID_IKBSUNDOMARK, IID_IKBSDOCUNDOOBSERVER))
		subject->DetachObserver(ISubject::kLazyAttachment, observer, IID_IKBSUNDOMARK, IID_IKBSDOCUNDOOBSERVER);
	KBS_DIAG_LOG("DOCCLOSING detach doc=%p", (void*)db);
}

void KBSUndoFollow::ForgetDocument(const UIDRef& docRef)
{
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		Step& step = gSteps[i];
		// Its chapter in the kept whole result sets - found by the document, not by the index: a Change
		// Checked in between may have dropped chapters and renumbered the rest.
		KBSResultModel::ModelSnapshot* const sets[2] = { &step.before, &step.after };
		for (size_t s = 0; s < 2; ++s)
			for (size_t c = 0; c < sets[s]->chapters.size(); ++c)
				if (sets[s]->chapters[c].docRef == docRef)
					KBSResultModel::EmptyChapter(sets[s]->chapters[c]);
		// Its stories. A write of rows is one document's, so one of this document goes whole (and its rows
		// with it); a Change Checked keeps the stories of the documents still open.
		std::vector<StoryMoved>& stories = step.stories;
		for (size_t k = stories.size(); k-- > 0; )
			if (stories[k].doc == docRef)
				stories.erase(stories.begin() + static_cast<std::ptrdiff_t>(k));
		if (stories.empty())
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	}
}

void KBSUndoFollow::ForgetBookChapter(const UIDRef& docRef)
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
	std::vector<bool> wroteIt(gSteps.size(), false);
	for (size_t i = 0; i < gSteps.size(); ++i)
		for (size_t k = 0; k < gSteps[i].stories.size() && !wroteIt[i]; ++k)
			wroteIt[i] = inChapter(gSteps[i].stories[k]);
	// FROZEN in a whole result set only where that write, or one after it, wrote the chapter: the set's rows for it
	// then describe text its file may no longer hold (a write after it that goes now - its history goes with the
	// chapter - no longer even stands in the way of its Undo). A chapter nothing since wrote reads in the set as it
	// reads in its file, so it is put back with the set: frozen, it was EMPTIED whenever the list on screen had no
	// rows for it - a chapter with nothing ticked, which a Change Checked's report drops - and its rows were lost on
	// the Undo (measured - the regression case cb-book-close-unwritten-undo).
	// Found in the set by its file the same way: a set's docRef can be one the chapter had before it was closed and
	// opened again.
	bool laterWrote = false;
	for (size_t i = gSteps.size(); i-- > 0; )
	{
		laterWrote = laterWrote || wroteIt[i];
		Step& step = gSteps[i];
		if (!step.whole || !laterWrote)
			continue;
		const KBSResultModel::ModelSnapshot* const sets[2] = { &step.before, &step.after };
		std::vector<size_t>* const frozen[2] = { &step.frozenBefore, &step.frozenAfter };
		for (size_t s = 0; s < 2; ++s)
			for (size_t c = 0; c < sets[s]->chapters.size(); ++c)
			{
				const KBSResultModel::Chapter& chapter = sets[s]->chapters[c];
				UIDRef found = chapter.docRef;
				if (KBSBookScope::FindOpenChapterDoc(chapter.file, found) && found.GetDataBase() == db
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
		if (stories.empty())
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	}
	KBS_DIAG_STEPS("chapter closing");
}

void KBSUndoFollow::ShutdownCleanup()
{
	// Assigning fresh vectors releases the storage too (the KESCL ShutdownCleanup rule): the kept writes
	// hold rows, and rows hold PMStrings.
	std::vector<Step>().swap(gSteps);
	std::vector<IDataBase*>().swap(gWatchedDocs);	// (the documents are gone by now; nothing is detached here)
	CloseRecording();
}

// End, KBSUndoFollow.cpp.
