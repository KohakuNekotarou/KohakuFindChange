//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  "Is this plug-in in the middle of a long run?" - ONE definition, because three different things
//  can be running - a search, a replace and Show Changes by KohakuFindChange - and every guard has to
//  know about all of them.
//
//  WHY THIS IS NEEDED AT ALL
//
//  Every long run in KBS puts up a MODAL progress bar, and a modal progress bar PUMPS THE EVENT
//  QUEUE. That is what makes its Cancel button work at all - and it is also what lets a menu
//  command, or an idle task, be dispatched while a run is standing inside its own loop. Two things
//  then go wrong, and the second is not survivable:
//
//    * the result model is CLEARED and refilled underneath the outer run, so the two runs' findings
//      end up mixed in one list (each run clears the model and then appends chapter by chapter);
//    * the inner run hands the held chapters back - KBSBookScope::ReleaseHeldDocs, which every run
//      calls when it is cancelled - which CLOSES the very documents the outer run is still walking.
//      Its stored UIDRef then carries a dangling IDataBase*, and the next chapter dereferences it.
//
//  Guarding each run against ITSELF does not cover either case: both need two DIFFERENT runs. So
//  the question is asked HERE, about all of them at once, and every caller asks this instead of
//  naming the engines one at a time. By name, because a count goes stale where a list does not:
//
//    * the panel's actions (KBSActionComponent, the UI half - through IKBSRuns): UpdateActionStates
//      greys everything out, and RefusedWhileRunning turns away a command that arrives anyway;
//    * each run's own front door (KBSSearchEngine, KBSReplaceEngine, KBSShowChanges), for a caller
//      that never went through the menu - a script firing an action by ID reaches the engine
//      whatever the menu says;
//    * the book-close watcher (KBSBookWatch, twice: the deferred callback, and the question it asks -
//      which the no-timer fallback asks directly), whose question would otherwise release the
//      chapters a run is walking;
//    * the document-close responder (KBSCloseDocResponder), which would otherwise throw away the
//      result model a run is still filling;
//    * the Undo follow (KBSUndoFollow::Follow), which would otherwise put rows back under a run.
//
//  A run added later is one line in KBSRunGuard.cpp rather than a fault nobody notices in the callers.
//
//========================================================================================

#ifndef __KBSRunGuard_h__
#define __KBSRunGuard_h__

namespace KBSRunGuard
{
	/** Is a search, a replace or a Show Changes by KohakuFindChange running right now? */
	bool IsAnyRunning();

	/** What to put on the status line when a run is turned away because another one is up. Not
	    translatable - the panel's status line is English throughout, echoing the Find/Change
	    wording. Deliberately does not name WHICH run: the callers that use it are the ones whose own
	    re-entry message would be a guess, and the engines that DO know say so themselves before
	    asking this. */
	const char* BusyMessage();
}

#endif // __KBSRunGuard_h__
