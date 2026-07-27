//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result invalidation on book close - the book-scope half of the rule the document side
//  implements in KBSCloseDocResponder.cpp:
//
//      the book a result set was searched in goes away -> the panel goes back to empty, and the
//      chapters KBS opened windowless for that search are closed with it.
//
//  A book close cannot be caught with a responder: the Programming Guide states responders may
//  only register for "a set of events the application predefines", and not one of the SDK's 105
//  signal ServiceIDs comes from BookID.h. So it has to be an observer, and what to listen to was
//  read off a debug build (Test > Spy, SpyOnBroadcast + SpyOnObserver).
//
//  WHAT THIS LISTENS TO
//
//      kCloseBookCmdBoss @ kSessionBoss (IID_IBOOKCONTENT)
//
//  It does not say WHICH book closed (the subject is the session; theChange and protocol are
//  identical every time), and it does not need to, because the only question ever asked is:
//
//      is the book this result set was searched in still open?
//
//  That question is the whole design. It needs no notion of which book closed and no two-step
//  state: a spurious cue simply finds the book still open and does nothing, and a book that
//  disappears by any route at all is caught by the next cue.
//
//  THE ONE THAT DID NOT WORK - kept here so it is not tried again (2026-07-28)
//
//  This first listened to IID_IDX_ACTIVE_BOOKCONTEXT_CHANGED_MSG @ kSessionBoss
//  (IID_IACTIVETOPICLISTCONTEXT) instead, on the belief that it is "broadcast when a book closes
//  (always)". That belief was wrong: it fires when the ACTIVE book changes, and closing a book does
//  NOT necessarily change it. Reproduced by the user - open two books, search one, make the other
//  active (that cue DOES arrive, the diagnostic line appears), then close both books. Neither close
//  moved the active book, so no cue ever arrived and the result set was never retired.
//
//  kCloseBookCmdBoss had been written off on 2026-07-27 as "useless on its own" for two reasons,
//  and neither survives contact with the design above: it does not identify the book (irrelevant -
//  one question, asked about OUR book) and at broadcast time the closing book still reports
//  IsOpen() (handled - the question is deferred, see below). Measured on a debug build to fire once
//  per close, for the last remaining book as much as for any other, and confirmed on the release
//  build. Listening to both messages was tried first and also worked; it was trimmed back to this
//  one because this is the cue that actually means "a book closed".
//
//  WHY THE ANSWER IS NOT ASKED IMMEDIATELY
//
//  Because at cue time nothing about the book has changed yet. Measured on the release build
//  (2026-07-27), immediately inside this notification the closing book still reports:
//
//      books=1, ours listed, IsOpen=1, db=1
//
//  - still on IBookManager's list, still IsOpen(), and its database is still alive. Every possible
//  test therefore answers "still open" for the very book that is closing. No later broadcast was
//  found that fires after the teardown, so the question is asked one beat later instead, through a
//  one-shot ICallbackTimer. This is the "delay is structurally necessary" case that timer is kept
//  for: it is not polling - exactly one deferred question per cue.
//
//  Note on the save prompt: a book that needs saving puts its alert up BEFORE any of this - by the
//  time these messages are broadcast the window teardown is already done - so a cancelled close
//  never reaches the point of clearing anything. The "is it still open" test covers that too.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICallbackTimer.h"
#include "IIdleTask.h"
#include "IObserver.h"
#include "ISubject.h"

// General includes:
#include "CObserver.h"

// ID.h files:
#include "BookID.h"				// kCloseBookCmdBoss, IID_IBOOKCONTENT
#include "ShuksanID.h"			// kCallbackTimerBoss, IID_ICALLBACKTIMER

// Project includes:
#include "KBSID.h"
#include "KBSBookScope.h"
#include "KBSBookWatch.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"

namespace
{

/** How long to wait before asking whether the book really went. Long enough for the close to
    finish unwinding, short enough that the panel clears while the user is still looking at it. */
const uint32 kKBSBookRetireDelayMs = 300;

/** The pending deferred question, or nil when none is armed. */
ICallbackTimer* gRetireTimer = nil;

/** Retire the results if the book they were searched in is no longer open. Does nothing - and
    says nothing - when the book is still there, because this runs on every book context change. */
void RetireBookResultsIfGone()
{
	// Document-scope results are KBSCloseDocResponder's business, not this one's.
	if (!KBSResultModel::IsFromBook())
		return;
	if (KBSResultModel::GetChapterCount() <= 0)
		return;

	// Read the path BEFORE releasing anything - ReleaseHeldDocs clears it.
	PMString heldBookPath;
	if (!KBSBookScope::GetHeldBookPath(heldBookPath))
		return;

	// The ordinary case for most cues: our book is fine, this was about some other book.
	if (KBSBookScope::IsBookStillOpen(heldBookPath))
	{
		// DIAGNOSTIC: report exactly what the book manager says right now, so we can see which
		// of these actually changes when a book is closed.
		PMString state("after wait: ");
		state.SetTranslatable(kFalse);
		KBSBookScope::DescribeBookState(heldBookPath, state);
		KBSResultTree::ShowStatus(state);
		return;
	}

	// Back to empty, and the chapters this search opened windowless go with it: leaving them open
	// would strand hidden documents belonging to a book nobody has open any more. The closes are
	// scheduled (IDocFileHandler::kSchedule), so doing this from a notification is safe.
	KBSResultModel::Clear();
	KBSBookScope::ReleaseHeldDocs();
	KBSResultTree::Rebuild();

	// A panel that empties itself without a word reads as a crash.
	PMString cleared("Results cleared - the book was closed.");
	cleared.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(cleared);
}

/** Timer callback. Raw function pointer - it must never outlive this plug-in (see the detach). */
uint32 RetireTimerCallback(void* /*refPtr*/)
{
	RetireBookResultsIfGone();

	// Release only here, where returning kEndOfTime guarantees this callback cannot run again.
	// (Releasing at the TOP would leave nothing for a teardown StopTimer to hold on to.)
	if (gRetireTimer != nil)
	{
		gRetireTimer->StopTimer();
		gRetireTimer->Release();
		gRetireTimer = nil;
	}

	// NEVER return 0 here: to the idle task manager 0 means "call me again immediately", which
	// spins this callback forever and freezes InDesign. kEndOfTime is what retires the task.
	return IIdleTask::kEndOfTime;
}

/** Arm (or re-arm) the deferred question. */
void ArmRetireTimer()
{
	if (gRetireTimer != nil)
	{
		// Another cue arrived while one was pending - restart the wait rather than stack timers.
		gRetireTimer->StopTimer();
		gRetireTimer->Release();
		gRetireTimer = nil;
	}

	gRetireTimer = static_cast<ICallbackTimer*>(::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER));
	if (gRetireTimer == nil)
	{
		// No timer to be had - ask now. It will almost certainly answer "still open" and do
		// nothing, but that is better than dropping the cue entirely.
		RetireBookResultsIfGone();
		return;
	}
	gRetireTimer->StartTimer(RetireTimerCallback, kKBSBookRetireDelayMs, nil);
}

/** Drop a pending question without asking it (plug-in teardown). */
void DisarmRetireTimer()
{
	if (gRetireTimer == nil)
		return;
	gRetireTimer->StopTimer();
	gRetireTimer->Release();
	gRetireTimer = nil;
}

}	// anonymous namespace

/** Session-attached observer that retires a book-scope result set when its book goes away. */
class KBSBookWatch : public CObserver
{
public:
	KBSBookWatch(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KBSBookWatch() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KBSBookWatch, kKBSBookWatchImpl)

void KBSBookWatch::Update(const ClassID& theChange, ISubject* /*theSubject*/,
	const PMIID& /*protocol*/, void* /*changedBy*/)
{
	// One cue, one question - see the file header.
	if (theChange != kCloseBookCmdBoss)
		return;

	// Nothing to retire? Then do not even arm the timer.
	if (!KBSResultModel::IsFromBook())
		return;
	if (KBSResultModel::GetChapterCount() <= 0)
		return;

	// Ask one beat later: at this instant the closing book still looks completely open.
	ArmRetireTimer();
}

//----------------------------------------------------------------------------------------
// Attach / detach. Called from KBSStartupShutdown - the session outlives every book, so one
// attach at startup covers every book that will ever be opened, with no per-book bookkeeping.
//----------------------------------------------------------------------------------------

void KBSBookWatchAttach()
{
	InterfacePtr<ISubject> subject(GetExecutionContextSession(), IID_ISUBJECT);
	if (subject == nil)
		return;
	InterfacePtr<IObserver> observer(GetExecutionContextSession(), IID_IKBSBOOKWATCH);
	if (observer == nil)
		return;
	// Ask before attaching: the layer panel's tree observer does the same, and linksui carries a
	// live bug from attaching and detaching asymmetrically.
	if (!subject->IsAttached(observer, IID_IBOOKCONTENT, IID_IKBSBOOKWATCH))
		subject->AttachObserver(observer, IID_IBOOKCONTENT, IID_IKBSBOOKWATCH);
}

void KBSBookWatchDetach()
{
	// The timer holds a raw pointer to a function in this plug-in: if it were left armed while the
	// plug-in unloads, the idle task manager would call into freed code.
	DisarmRetireTimer();

	InterfacePtr<ISubject> subject(GetExecutionContextSession(), IID_ISUBJECT);
	if (subject == nil)
		return;
	InterfacePtr<IObserver> observer(GetExecutionContextSession(), IID_IKBSBOOKWATCH);
	if (observer == nil)
		return;
	// Symmetric with the attach, or the session keeps a pointer into a plug-in being unloaded.
	if (subject->IsAttached(observer, IID_IBOOKCONTENT, IID_IKBSBOOKWATCH))
		subject->DetachObserver(observer, IID_IBOOKCONTENT, IID_IKBSBOOKWATCH);
}

// End, KBSBookWatch.cpp.
