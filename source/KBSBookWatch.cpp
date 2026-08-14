//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result invalidation on book close - the book-scope half of the rule the document side
//  implements in KBSCloseDocResponder.cpp:
//
//      the book KBS opened chapters for goes away -> those windowless chapters are closed with it,
//      and if the panel is still showing that book's results, it goes back to empty.
//
//  The two halves are deliberately separate. Giving the chapters back is not optional and does not
//  depend on what the panel happens to be showing: a windowless chapter left open keeps its .indd
//  locked (a .idlk beside the file) for the rest of the session, so nobody else can open it. Emptying
//  the panel only makes sense while the panel is showing THAT book's results.
//
//  (This started as a temporary instrumentation build - a session observer that just reported what
//  a book close broadcasts. It answered the question and then became the feature; the notes below
//  are the measurements it was built to take.)
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
//      is the book our results came from still open?
//
//  That question is the whole design. It needs no notion of which book closed and no two-step
//  state: a spurious cue simply finds the book still open and does nothing, and a book that
//  disappears by any route at all is caught by the next cue.
//
//  It is asked about the SEARCHED BOOK (KBSBookScope::GetSearchedBookPath), NOT about the result
//  set. Asking it about the result set - which is what this did until 2026-07-30 - let two cases
//  through, and in both of them chapters stayed open with their files locked for the rest of the
//  session:
//    * a book search that found NOTHING - no result set, so an "are there results?" gate returned
//      early while the chapters it opened to look were still held;
//    * a book search followed by a DOCUMENT-scope search - IsFromBook() is false by then, so that
//      gate returned early as well.
//  The searched-book path survives both. It is set whenever a run resolves a book, whatever that
//  run goes on to find, and the only way to let it go is ReleaseSearchedBook - which hands the held
//  chapters back in the same breath. So when this gate is false, what is left open is only what
//  REFUSED to be handed back: ReleaseHeldDocs puts a chapter back on the held list when it holds
//  unsaved work or when its close is refused, and those two outlive the path that named their book.
//  (This said "there is nothing of ours left open", which was written before those two doors
//  existed - 2026-08-08 - and is the stronger claim of the two.) Nothing is stranded by it: the
//  next run of any kind calls ReleaseSearchedBook on its way in and tries them again, and a chapter
//  kept for unsaved work is one a replace has already reported to the user.
//
//  (Until 2026-08-02 the gate asked about the HELD CHAPTERS. That was honest while a book run kept
//  every chapter it opened; now a run closes each chapter as soon as it has walked it, so the held
//  list is empty almost always and a gate on it would never have armed the timer at all.)
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
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)
#include "BookID.h"				// kCloseBookCmdBoss, IID_IBOOKCONTENT
#include "ShuksanID.h"			// kCallbackTimerBoss, IID_ICALLBACKTIMER

// Project includes:
#include "KBSID.h"
#include "KBSBookScope.h"
#include "KBSBookWatch.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"
#include "KBSRunGuard.h"		// never retire results out from under ANY run of ours
#include "KBSSearchEngine.h"	// ForgetSearchedFindFormat - paired with every result Clear()

namespace
{

/** How long to wait before asking whether the book really went. Long enough for the close to
    finish unwinding, short enough that the panel clears while the user is still looking at it. */
const uint32 kKBSBookRetireDelayMs = 300;

/** The timer that asks the deferred question. ONE object for the whole life of the plug-in, made
    on first use and released only by DisarmRetireTimer - see the callback on why it is never
    released from inside itself. */
ICallbackTimer* gRetireTimer = nil;

/** Retire the results if the book they were searched in is no longer open. Does nothing - and
    says nothing - when the book is still there, because this runs on every book context change. */
void RetireBookResultsIfGone()
{
	// NEVER while ANY run of ours is going. This clears the result model and hands the held chapters
	// back, and every run is walking exactly those chapters - their progress bars pump events while
	// they are up, so an idle callback CAN land in the middle of one. (Reachable in practice: close
	// a book, then start a run inside the wait below.) The caller re-arms, so the cue is deferred
	// rather than dropped, and the book will still be gone when it is finally asked.
	//
	// This asked only about the SEARCH until 2026-08-02, which left the replace and both scans
	// unguarded - and the replace is the worst of them to interrupt, since it holds an open command
	// sequence over the documents this would close. See KBSRunGuard.
	if (KBSRunGuard::IsAnyRunning())
		return;

	// Which book are the results from? Read the path BEFORE releasing anything -
	// ReleaseSearchedBook clears it. No searched book = nothing of ours is open and nothing on the
	// panel names a book, which is the one honest early exit here (see the file header on why this
	// is asked about the searched book rather than about the result set or the held chapters).
	PMString searchedBookPath;
	if (!KBSBookScope::GetSearchedBookPath(searchedBookPath))
		return;

	// The ordinary case for most cues: our book is fine, this was about some other book.
	if (KBSBookScope::IsBookStillOpen(searchedBookPath))
		return;

	// Is the panel still showing THAT book's results? Decided before anything is released, because
	// releasing does not touch the model and clearing the model must not depend on the order.
	// Document-scope results belong to KBSCloseDocResponder instead.
	//
	// IsFromBook() ALONE - deliberately not "and it has at least one chapter". A book search that
	// found NOTHING still leaves a row on the tree: the hierarchy adapter gives the root one child
	// whenever the results came from a book, so the panel shows "book.indb  (0)". That row names a
	// book, so once the book is gone it has to go too. (Measured 2026-07-30 by the user: with a
	// chapter-count test in here, closing the book after a 0-hit search released the chapters but
	// left that row sitting there and the panel said nothing at all - which also made it impossible
	// to tell from the screen whether the close had even been noticed.)
	//
	// ! "(0)" is what that row says again as of 2026-08-11. It read "(0/0 checked)" in between: the
	//   rows started reading out "(N/M checked)" on 2026-08-05 and the fall-back to a plain total,
	//   added 35 minutes later, covered the lists with no boxes and not the list with no rows. This
	//   sentence is the only place in the plug-in that describes what that row shows, and it is the
	//   only reason the wording was noticed at all - so it is worth keeping exact.
	const bool showingThatBooksResults = KBSResultModel::IsFromBook();

	// The chapters go back FIRST, and unconditionally: leaving them open would strand hidden
	// documents - with their .indd files locked - belonging to a book nobody has open any more. The
	// closes are scheduled (IDocFileHandler::kSchedule), so doing this from a notification is safe.
	// ReleaseSearchedBook does the path as well, so the model's Clear below needs no second call.
	KBSBookScope::ReleaseSearchedBook();

	// The panel is only touched when it was showing this book's results. Anything else on screen
	// (a document-scope search, or nothing at all) is not ours to wipe.
	if (!showingThatBooksResults)
		return;

	// ForgetSearchedFindFormat alongside, as every Clear() has one: the format the replace's door
	// compares against belongs to the rows going away here.
	KBSResultModel::Clear();
	KBSSearchEngine::ForgetSearchedFindFormat();
	KBSResultTree::Rebuild();

	// A panel that empties itself without a word reads as a crash.
	PMString cleared("Results cleared - the book was closed.");
	cleared.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(cleared);
}

/** Timer callback. Raw function pointer - it must never outlive this plug-in (see the detach).

    NOTHING is released in here. The callback runs as the timer object's own idle task, so
    releasing it from inside drops the last reference to the very object whose RunTask is still on
    the stack - it would then read this function's return value out of a destroyed object. And
    nil-ing the global first throws away the only handle a teardown StopTimer could use to stop a
    callback that has gone wrong. KESCM hit exactly this and settled the rule the same way
    (KESCMTracker.cpp, KESCMHudTimerProc): the timer is released in ONE place, and it is not here. */
uint32 RetireTimerCallback(void* /*refPtr*/)
{
	// A run of ours is walking the very chapters this would hand back - wait it out by returning a
	// POSITIVE value, which re-arms without calling StartTimer from inside the callback.
	//
	// ***** THAT RE-ARM IS AN OBSERVED BEHAVIOUR, NOT A PROMISE - and this line said otherwise
	// ***** until 2026-08-12. ***** IIdleTask::RunTask documents its return as "the number of
	// milliseconds to sleep before running again" (IIdleTask.h:195) and ICallbackTimer derives from
	// IIdleTask, but the timer's OWN header describes what it registers as "a one time only
	// callback" (ICallbackTimer.h:42) and says nothing about what the callback's return value does
	// with it.
	//
	// ***** AND THE SDK'S ONE WORKED EXAMPLE NEVER TAKES THIS PATH. ***** ICallbackTimer has exactly
	// one caller in the whole SDK - publiclib/links/HTTPAssetLinkResourceHandler.cpp - and its
	// callback returns ~(uint32)0, which IS kEndOfTime, on every exit (:621). When Adobe wants that
	// timer to fire again they do it from OUTSIDE the callback: StopTimer, Release, build another and
	// StartTimer it (:645-658). So the one example that exists exercises the "remove me" return and
	// nothing else, and the positive-value re-arm below is used by nobody but this plug-in.
	// (CTracker's timers are ITrackerTimer, a different interface, and are not evidence either way.)
	//
	// KBSPanelAlpha's re-apply chain rests on the same inference and has always said so in as many
	// words; this side stated it as fact, which is one question answered two ways in one plug-in.
	//
	// ***** WHAT CATCHES IT IF THE RE-ARM DOES NOT HAPPEN. ***** The cue is dropped and this book's
	// chapters are not handed back HERE - but the next search or scan calls ReleaseSearchedBook on
	// its way in and hands them back then (KBSSearchEngine.cpp, KBSGlyphScanEngine.cpp,
	// KBSOversetScanEngine.cpp, each beside its own KBSResultModel::Clear()). A REPLACE does not:
	// it keeps the searched book on purpose, its results being still on the panel. So the worst case
	// is a book's results left on the panel, and any chapter that refused to close left locked,
	// until the next search or scan - not a permanent strand. Named rather than assumed, because a
	// fallback nobody has written down is one the next change can remove without noticing.
	if (KBSRunGuard::IsAnyRunning())
		return kKBSBookRetireDelayMs;

	RetireBookResultsIfGone();

	// NEVER return 0 here: to the idle task manager 0 means "call me again immediately", which
	// spins this callback forever and freezes InDesign. kEndOfTime is what retires the task.
	return IIdleTask::kEndOfTime;
}

/** Arm (or re-arm) the deferred question. The timer object is made once and then reused: an
    ICallbackTimer is a one-shot only in the sense that it fires once per StartTimer. */
void ArmRetireTimer()
{
	if (gRetireTimer == nil)
		gRetireTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);

	if (gRetireTimer == nil)
	{
		// No timer to be had - ask now. It will almost certainly answer "still open" and do
		// nothing, but that is better than dropping the cue entirely.
		RetireBookResultsIfGone();
		return;
	}

	// Another cue arrived while one was pending - restart the wait rather than stack timers.
	// Harmless when nothing is armed.
	gRetireTimer->StopTimer();
	gRetireTimer->StartTimer(RetireTimerCallback, kKBSBookRetireDelayMs, nil);
}

/** Drop a pending question without asking it, and give the timer back (plug-in teardown). The ONE
    place the object is released - never from inside the callback. */
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

	// No searched book? Then no book closing is any of our business - nothing of ours is open and
	// nothing on the panel names a book. This is the same gate the deferred question uses, kept here
	// so a cue that will do nothing does not even arm the timer. (See the file header for why the
	// question is not "are there results?" and no longer "do we hold chapters?".)
	PMString searchedBookPath;
	if (!KBSBookScope::GetSearchedBookPath(searchedBookPath))
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
