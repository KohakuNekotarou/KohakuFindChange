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
#include "IDataBase.h"
#include "IObserver.h"
#include "ISubject.h"

// General includes:
#include "CObserver.h"
#include "IDFile.h"
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - the gate in LazyUpdate
#include "PersistUtils.h"	// ::GetUIDRef - which story a notification is about
#include "TextID.h"			// IID_ITEXTMODEL - the protocol InDesign raises a story's change under

#include <set>
#include <utility>			// std::move - a kept write carries its rows

// Project includes:
#include "KBSID.h"
#include "KBSBookScope.h"		// FindOpenChapterDoc - a chapter's document found again by its file
#include "KBSResultModel.h"
#include "KBSModelNotify.h"		// the panel drawn again, and its message line - told, never called (2026-10-01)
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
	IDataBase*	now;		// the document as it was last found (ResolveDocs); nil = not open
	StoryMoved() : story(kInvalidUID), before(0), after(0), now(nil) {}
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
	Step() : kind(KBSUndoFollow::kStepReplace), done(true), resultSet(0), whole(false), layoutBefore(0), layoutAfter(0) {}
};

// The kept writes, oldest first.
std::vector<Step> gSteps;

// ***** HOW MANY ARE KEPT. ***** InDesign's own history is longer, but a write older than this is rarely
// undone through thirty of KBS's own; one that is simply stops being followed (its rows stay as they are -
// what they did before 2026-09-29). A whole result set is heavy (a large search holds thousands of rows),
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
bool ShareDoc(const Step& a, const Step& b)
{
	for (size_t x = 0; x < a.stories.size(); ++x)
		for (size_t y = 0; y < b.stories.size(); ++y)
			if (a.stories[x].now == b.stories[y].now)
				return true;
	return false;
}

// Is every story the write moved at its version before it (after = false) or after it (after = true)?
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
	}
	return !step.stories.empty();
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

// The stories being watched, so that a closing document's can be let go of (DocumentClosing).
std::vector<UIDRef> gWatched;

// Watch the story: a lazy observer, under IID_ITEXTMODEL (the header says why lazy).
// ***** DETACHED WHEN ITS DOCUMENT CLOSES (2026-09-29) - DocumentClosing. ***** Until then this said "NO
// DETACH, as KCM's has none: a document's stories go with the document". What is attached is taken off -
// the observer's side of the bargain, whatever becomes of the subject.
// !WHAT THIS DID NOT FIX, measured the same day: with All Documents (close one document, Ctrl+Z in the
//  other) about one Undo in ten was never heard in the regression run - no LazyUpdate at all, attachment in
//  place - with the detach as without it. Waiting 4 s after the Ctrl+Z before the run's next call heard all
//  of them: a lazy notification comes at idle, and a run that goes straight on (KIDMCP copies the document
//  around every script) can miss it. A person who presses Ctrl+Z and looks at the panel gives it that idle.
void Watch(IDataBase* db, UID story)
{
	if (db == nil || story == kInvalidUID)
		return;
	const UIDRef ref(db, story);
	InterfacePtr<ISubject> subject(ref, UseDefaultIID());
	// Asked for by OUR IID: kTextStoryBoss carries other people's IID_IOBSERVER (KBS.fr).
	InterfacePtr<IObserver> observer(ref, IID_IKBSSTORYUNDOOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (!subject->IsAttached(ISubject::kLazyAttachment, observer, IID_ITEXTMODEL, IID_IKBSSTORYUNDOOBSERVER))
	{
		subject->AttachObserver(ISubject::kLazyAttachment, observer, IID_ITEXTMODEL, IID_IKBSSTORYUNDOOBSERVER);
		gWatched.push_back(ref);
	}
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
	// ***** A NEW STEP IN A DOCUMENT THROWS ITS REDO AWAY ***** (InDesign's own rule): an undone write sharing a
	// document with this one can never be done again.
	for (size_t i = gSteps.size(); i-- > 0; )
		if (!gSteps[i].done && ShareDoc(gSteps[i], step))
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(i));
	for (size_t k = 0; k < step.stories.size(); ++k)
		Watch(step.stories[k].now, step.stories[k].story);
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
}

}	// anonymous namespace

//========================================================================================
// The observer.
//========================================================================================
class KBSStoryUndoObserver : public CObserver
{
public:
	KBSStoryUndoObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSSTORYUNDOOBSERVER) {}
	virtual ~KBSStoryUndoObserver() {}

	/** Deliberately empty: the work is in LazyUpdate, the only one of the two an Undo and a Redo reach. */
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy) {}

	/** The data is not read: it names neither what changed nor whether it was an Undo, and may be nil. The
		versions say (KBSUndoFollow::Follow). */
	virtual void LazyUpdate(ISubject* theSubject, const PMIID& protocol, const LazyNotificationData* data)
	{
		if (protocol != IID_ITEXTMODEL || theSubject == nil)
			return;
		// The main thread only (2026-10-01, kModelPlugIn - the split's design section 6): an Undo and a Redo
		// happen there, and the results it moves are the session's.
		if (!IDThreading::IsMainThreadDomain())
			return;
		(void)KBSUndoFollow::Follow(::GetUIDRef(theSubject).GetUID());
	}
};

CREATE_PMINTERFACE(KBSStoryUndoObserver, kKBSStoryUndoObserverImpl)

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
	// Not kept: the write failed or was cancelled. The rows it changed go back (the replace's own rollback
	// until 2026-09-29 - RollBackRows is harmless where the caller has done it already: nothing is left).
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

	// A write that moved no story has nothing an Undo could take back; one that threw the results away
	// (a refused Replace clears them - KBSReplaceEngine::RefuseChangedQuery) has no rows left to follow.
	if (step.stories.empty() || step.resultSet != KBSResultModel::GetResultSetId())
		return;
	KeepStep(step);
}

//========================================================================================
// Following.
//========================================================================================
bool KBSUndoFollow::Follow(UID story)
{
	// A write of ours is standing (its own notifications arrive as its sequence ends), or a run is up
	// (a search, a replace or Show Changes pumps events behind its bar - KBSRunGuard counts all three).
	if (gRecording || gSteps.empty() || KBSRunGuard::IsAnyRunning())
		return false;
	// ***** THE CHEAP QUESTION FIRST: does a kept write name this story? ***** Typing in a watched story
	// reaches here on every keystroke.
	if (story != kInvalidUID)
	{
		bool named = false;
		for (size_t i = 0; i < gSteps.size() && !named; ++i)
			for (size_t k = 0; k < gSteps[i].stories.size() && !named; ++k)
				named = (gSteps[i].stories[k].story == story);
		if (!named)
			return false;
	}
	DropOtherResultSets();
	ResolveDocs();
	if (gSteps.empty())
		return false;

	// ***** ONE WRITE AT A TIME, IN THE ORDER INDESIGN TAKES THEM. ***** An Undo takes back the newest write
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
		if (pick == gSteps.size())
			break;
		Step& step = gSteps[pick];
		// ***** THE ROWS ARE NUMBERED IN ONE LAYOUT. ***** A write's rows are put back only onto the layout they
		// were taken from; any other (which the order above should never leave) means the write can no
		// longer be followed, and it is dropped rather than written onto the wrong rows.
		if (KBSResultModel::GetLayoutGeneration() != (undo ? step.layoutAfter : step.layoutBefore))
		{
			gSteps.erase(gSteps.begin() + static_cast<std::ptrdiff_t>(pick));
			continue;
		}
		if (step.whole)
		{
			KBSResultModel::RestoreModelSnapshot(undo ? step.before : step.after);
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
	for (size_t i = gWatched.size(); i-- > 0; )
	{
		if (gWatched[i].GetDataBase() != db)
			continue;
		// The document is still whole at this signal (BeforeCloseDoc), so its stories can be asked.
		InterfacePtr<ISubject> subject(gWatched[i], UseDefaultIID());
		InterfacePtr<IObserver> observer(gWatched[i], IID_IKBSSTORYUNDOOBSERVER);
		if (subject != nil && observer != nil
			&& subject->IsAttached(ISubject::kLazyAttachment, observer, IID_ITEXTMODEL, IID_IKBSSTORYUNDOOBSERVER))
			subject->DetachObserver(ISubject::kLazyAttachment, observer, IID_ITEXTMODEL, IID_IKBSSTORYUNDOOBSERVER);
		gWatched.erase(gWatched.begin() + static_cast<std::ptrdiff_t>(i));
	}
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

void KBSUndoFollow::ShutdownCleanup()
{
	// Assigning fresh vectors releases the storage too (the KESCL ShutdownCleanup rule): the kept writes
	// hold rows, and rows hold PMStrings.
	std::vector<Step>().swap(gSteps);
	std::vector<UIDRef>().swap(gWatched);	// (the documents are gone by now; nothing is detached here)
	CloseRecording();
}

// End, KBSUndoFollow.cpp.
