//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "Is this plug-in in the middle of a long run?" - ONE definition, because four different things
//  can be running - a search, a Change All in Book (KFCChangeAll), a query run (KFCQuerySequence) and a row's Replace
//  (KFCReplaceEngine::IsReplacing) - and every guard has to know about all of them.
//  A ROW'S REPLACE COUNTS THOUGH IT HAS NO BAR (the 2026-10-12 re-check): reopening a closed chapter and giving a written one
//  its window run the message loop as surely as a bar does (KFCJump.cpp's gActivating says so of a landing's opens), and
//  what that loop dispatches - a click's landing and its Hide Previous Chapter sweep, a second Return, a run - must wait
//  for it like for any run. (Until then it was "one undo step with no bar, never a run".)
//
//  WHY THIS IS NEEDED AT ALL
//
//  Every long run in KFC puts up a MODAL progress bar, and a modal progress bar PUMPS THE EVENT
//  QUEUE. That is what makes its Cancel button work at all - and it is also what lets a menu
//  command, or an idle task, be dispatched while a run is standing inside its own loop. Two things
//  then go wrong, and the second is not survivable:
//
//    * the result model is CLEARED and refilled underneath the outer run, so the two runs' findings
//      end up mixed in one list (each run clears the model and then appends chapter by chapter);
//    * the inner run hands the held chapters back - KFCBookScope::ReleaseHeldDocs, which every run
//      calls when it is cancelled - which CLOSES the very documents the outer run is still walking.
//      Its stored UIDRef then carries a dangling IDataBase*, and the next chapter dereferences it.
//
//  Guarding each run against ITSELF does not cover either case: both need two DIFFERENT runs. So
//  the question is asked HERE, about all of them at once, and every caller asks this instead of
//  naming the engines one at a time. By name, because a count goes stale where a list does not:
//
//    * the panel's actions (KFCActionComponent, the UI half - through IKFCRuns): UpdateActionStates
//      greys out every kCustomEnabling one, and RefusedWhileRunning turns away a command that arrives anyway
//      (Save Panel Settings, which no grey reaches, asks it first);
//    * each engine's own front door (KFCSearchEngine, KFCReplaceEngine, KFCChangeAll - Clear Results
//      too - and KFCQuerySequence), for a caller that never went through the menu - a script firing an
//      action by ID reaches the engine whatever the menu says;
//    * the book-close watcher (KFCBookWatch, twice: the deferred callback, and the question it asks -
//      which the no-timer fallback asks directly), whose question would otherwise release the
//      chapters a run is walking;
//    * the document-close responder (KFCCloseDocResponder), which would otherwise throw away the
//      result model a run is still filling;
//    * the document-open responder (KFCOpenDocResponder), which leaves the chapters a run opens to the
//      run (it binds them itself);
//    * the Save As responder (KFCSaveAsDocResponder), which leaves the list's names to the run that owns the list;
//    * the Undo follow (KFCUndoFollow::Follow), which would otherwise put rows back under a run;
//    * the query dialog's double click (KFCQueryDialogShowInFindChange, the UI half - through IKFCRuns),
//      which would otherwise load a query into Find/Change while a query run is putting its own there;
//    * a row's landing (KFCJump::ActivateNode, the UI half - through IKFCRuns), whose Hide Previous Chapter sweep
//      would otherwise schedule the close of a chapter a Replace is about to write;
//    * a test build's hidden-document check (KFCDiagHiddenDocs, the UI half), which leaves the chapter a
//      run is walking to the run.
//
//  A run added later is one line in KFCRunGuard.cpp rather than a fault nobody notices in the callers.
//
//========================================================================================

#ifndef __KFCRunGuard_h__
#define __KFCRunGuard_h__

namespace KFCRunGuard
{
	/** Is a search, a Change All, a query run or a row's Replace running right now? */
	bool IsAnyRunning();

	/** IS A ROW'S LANDING UNDER WAY in the panel (the UI half's KFCJump::IsLanding, asked through IKFCUIServices)?
	    NOT part of IsAnyRunning, on purpose: the doors IsAnyRunning answers for - the GREP row's preview, an object row's
	    Replace - are themselves reached from inside a landing, and would turn themselves away. Asked by the model's own
	    deferred work that must not run under a landing - the book watcher's retire (KFCBookWatch, the 2026-10-12
	    proposal P-A): a landing opens documents and windows and runs the message loop, and a retire landing in it would
	    empty the results the landing is reading and hand back the chapter it is opening. No UI half = no landing. */
	bool IsLandingUnderWay();

	/** What to put on the status line when a run is turned away because another one is up. Not
	    translatable - the panel's status line is English throughout, echoing the Find/Change
	    wording. Deliberately does not name WHICH run: the callers that use it are the ones whose own
	    re-entry message would be a guess, and the engines that DO know say so themselves before
	    asking this. */
	const char* BusyMessage();
}

#endif // __KFCRunGuard_h__
