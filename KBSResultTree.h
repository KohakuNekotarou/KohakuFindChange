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
	    Kept for the app.kbsStatus script property (KBSScriptProvider.cpp), which is how a script -
	    or PowerShell over COM - reads what the panel just reported. Empty until the first message. */
	void GetLastStatus(PMString& outMessage);

	/** Write "<checked> / <total> checked." to the panel's status line (and note the display cap
	    when the result set is bigger than the panel shows). Called after any check change. */
	void ShowCheckedStatus();

	/** "Save Results..." on the panel's flyout: write the current result set to a tab-separated text
	    file the user picks. Implemented in KBSReportSave.cpp.

	    Lives with the tree for the same reason ShowStatus does - it is the panel reporting what it is
	    showing - and it reads the status line back to put it in the file's heading, which is the one
	    thing about the report that only the panel knows.

	    Says nothing when it works: the file is where the user put it. Only the failures reach the
	    status line, and a cancelled chooser does nothing at all. */
	void SaveResultsAsText();
}

#endif // __KBSResultTree_h__
