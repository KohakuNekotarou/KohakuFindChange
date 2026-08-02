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

	/** The last message ShowStatus was given, whether or not the panel was open to display it.
	    Kept for the app.kbsStatus script property (KBSScriptProvider.cpp), which is how a script -
	    or PowerShell over COM - reads what the panel just reported. Empty until the first message. */
	void GetLastStatus(PMString& outMessage);

	/** Write "<checked> / <total> checked." to the panel's status line (and note the display cap
	    when the result set is bigger than the panel shows). Called after any check change. */
	void ShowCheckedStatus();
}

#endif // __KBSResultTree_h__
