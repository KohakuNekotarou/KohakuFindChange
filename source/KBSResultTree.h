//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result tree rebuild entry point. Called after KBSResultModel has been filled by a search or a
//  scan: reloads the panel's tree widget from the model. What it opens depends on the scope - a
//  BOOK result opens the book row and leaves the chapters closed (a book-wide run can fill the
//  panel with one chapter's hits and bury the fact that others matched), a single document opens
//  its one chapter. No priming is needed to get the expander arrows drawn: this panel draws them
//  itself, from the hierarchy adapter's child count. No-op when the panel is closed. Implemented
//  in KBSResultListWidgetMgr.cpp (it lives with the tree).
//
//========================================================================================

#ifndef __KBSResultTree_h__
#define __KBSResultTree_h__

#include "PMString.h"

namespace KBSResultTree
{
	/** (Re)load the panel's result tree from KBSResultModel. A book result comes up with the book
	    row open and its chapters closed; a document result comes up with its one chapter open. Safe
	    to call when the panel is closed (does nothing then). */
	void Rebuild();

	/** Repaint the existing rows from the model WITHOUT rebuilding the tree. For changes that touch
	    only what a row DRAWS - the check boxes behind Check All / Uncheck All - where the tree's
	    shape (which chapters, how many hits) is untouched.

	    Costs one notification per CHAPTER, not per hit: NodeChanged carries childrenChangedAlso, so
	    the framework refreshes each chapter's hit rows itself. Rebuild() by contrast tears the whole
	    tree down and re-expands it, which is what made a large result set expensive.

	    It also KEEPS the expansion state, so a chapter the user collapsed stays collapsed (Rebuild
	    re-expands everything). Safe to call when the panel is closed (does nothing then). */
	void RefreshRows();

	/** Repaint ONLY the rows that read out a checked count: the book row, and the one chapter row
	    named here. What a single check box changes and nothing more - the box draws itself, and no
	    other hit row is affected - so this is what a box's observer calls instead of RefreshRows.
	    Pass -1 for the chapter to refresh the book row alone. Safe when the panel is closed. */
	void RefreshCheckedCounts(int32 chapterIdx);

	/** Write a one-line message to the panel's status read-out (its single-line StaticText). Safe
	    to call when the panel is closed (does nothing then). Lives with the tree because it reaches
	    the panel exactly the way Rebuild does. */
	void ShowStatus(const PMString& message);

	/** Put the status read-out back to what THIS session last had on it - or, when nothing has run
	    since launch, to the .fr's own initial text. Called from the panel's AutoAttach, and only
	    from there.

	    Why it is needed: a widget's string is persisted in the WORKSPACE, so a rebuilt panel comes
	    back carrying the last message of whatever session wrote it, including yesterday's - while
	    the results it describes are gone. The panel's show is the one moment that can outrank the
	    persisted value, the same moment the tab's name and the illustration are written at.

	    Does NOT touch what GetLastStatus answers: restoring a line is not the panel reporting
	    something. Safe to call when the panel is closed (does nothing then). */
	void RestoreStatusOnPanelShow();

	/** The last message ShowStatus was given, whether or not the panel was open to display it.
	    Kept for the app.kfcStatus script property (KBSScriptProvider.cpp), which is how a script -
	    or PowerShell over COM - reads what the panel just reported. Empty until the first message. */
	void GetLastStatus(PMString& outMessage);

	/** Release this module's static storage during the controlled shutdown
	    (KBSStartupShutdown::Shutdown), so no static destructor at DLL unload finds work left to do.

	    One string - the status line above - but it is a PMString, which is exactly the kind of static
	    the rule was written for: KBSResultModel::ShutdownCleanup and KBSSearchEngine::ShutdownCleanup
	    both name "a static PMString" as the thing that must be let go here. This one was missed by
	    both of them until 2026-08-08 (it is the third static to be added without a line in the list;
	    see KBSResultModel::ShutdownCleanup for the first two). */
	void ShutdownCleanup();

	/** Say on the status line WHAT was just ticked or cleared, and over WHICH row:

	        ch1.indd  all checked
	        selftest.indb  all unchecked

	    ***** Check All / Uncheck All only. ***** Those two reach every hit of a book or of a
	    document, most of them scrolled out of sight, so what they did has to be said somewhere the
	    user is looking - and WHICH row they were asked over is the whole question, since the same
	    two commands mean "this chapter" or "the whole book" depending on it.

	    Ticking a single box says nothing here (2026-08-05). The book and document rows read out
	    "(N/M checked)" themselves, so the line would be overwriting the search's own summary to
	    repeat what is already on screen.

	    @param targetName the row the menu was popped over - a chapter's name, or the book's.
	    @param nowChecked true = Check All, false = Uncheck All. */
	void ShowCheckAllStatus(const PMString& targetName, bool nowChecked);

	/** The same, for ONE box:

	        P1(2)  checked
	        P4  unchecked

	    Named by its LOCATOR, which is what the row itself leads with - so the line reads as an echo
	    of the row that was clicked, the way the Check All line echoes a chapter's name.

	    @param locator the hit row's page locator (KBSResultModel::GetHitDisplay's first field).
	    @param nowChecked the state the box was just put into. */
	void ShowHitCheckStatus(const PMString& locator, bool nowChecked);

	/** "Save Results..." on the panel's flyout: write the current result set to a tab-separated text
	    file the user picks. Implemented in KBSReportSave.cpp.

	    Lives with the tree for the same reason ShowStatus does - it is the panel reporting what it is
	    showing.

	    ***** THE HEADING'S "Summary:" LINE COMES FROM THE RUN, NOT FROM THIS STATUS LINE. ***** It
	    did read the line back until 2026-08-09, and a tick between the run and the save then put
	    "P1(2)  checked" at the head of the file - so every run records its own sentence
	    (KBSResultModel::NoteRunSummary) and the report asks for that. GetLastStatus stands in only
	    when nothing recorded one (KBSReportSave.cpp:203-210).

	    Says nothing when it works: the file is where the user put it. Only the failures reach the
	    status line, and a cancelled chooser does nothing at all. */
	void SaveResultsAsText();
}

#endif // __KBSResultTree_h__
