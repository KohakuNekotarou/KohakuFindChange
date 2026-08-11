//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  "Is this plug-in in the middle of a long run?" - ONE definition, because four different things
//  can be running and every guard has to know about all four.
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
//  naming the engines one at a time. By name, because a count goes stale where a list does not
//  (this comment said "the three kinds of caller" while there were six call sites, the three it
//  omitted having arrived one at a time):
//
//    * the panel's action enablement (KBSActionComponent, twice) greys everything out;
//    * each engine's own front door (KBSSearchEngine / KBSReplaceEngine / KBSGlyphScanEngine /
//      KBSOversetScanEngine), for a caller that never went through the menu - a script firing an
//      action by ID reaches the engine whatever the menu says;
//    * the book-close watcher (KBSBookWatch, twice: at the cue and again in the deferred callback),
//      whose question would otherwise release the chapters a run is walking. It used to ask only
//      about the SEARCH, which left the replace and both scans unprotected;
//    * the document-close responder (KBSCloseDocResponder), which would otherwise throw away the
//      result model a run is still filling;
//    * the report writer (KBSReportSave), which would otherwise save a half-filled result set;
//    * the script provider (KBSScriptProvider), so app.kfcStatus / app.kfcResults answer "busy"
//      rather than a partial reading.
//
//  A fifth run added later is one line in this file rather than a fault nobody notices in four.
//
//========================================================================================

#ifndef __KBSRunGuard_h__
#define __KBSRunGuard_h__

namespace KBSRunGuard
{
	/** Is a search, a replace, a missing-glyph scan or an overset scan running right now? */
	bool IsAnyRunning();

	/** What to put on the status line when a run is turned away because another one is up. Not
	    translatable - the panel's status line is English throughout, echoing the Find/Change
	    wording. Deliberately does not name WHICH run: the runs that use it are the ones whose own
	    re-entry message would be a guess (a scan turned away by a replace, and so on), and the
	    engines that DO know say so themselves before asking this. */
	const char* BusyMessage();
}

#endif // __KBSRunGuard_h__
