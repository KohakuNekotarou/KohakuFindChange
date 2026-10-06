//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Result tree rebuild entry point. Called after KFCResultModel has been filled by a search or by Show
//  Changes: reloads the panel's tree widget from the model. What it opens depends on the scope - a
//  BOOK result opens the book row and leaves the chapters closed (a book-wide run can fill the
//  panel with one chapter's hits and bury the fact that others matched), All Documents leaves its
//  document rows closed for the same reason, a single document opens its one chapter - and the story
//  and run rows always come up open. No priming is needed to get the expander arrows drawn: this
//  panel draws them itself, from the hierarchy adapter's child count. No-op when the panel is closed.
//  Implemented in KFCResultListWidgetMgr.cpp (it lives with the tree).
//
//========================================================================================

#ifndef __KFCResultTree_h__
#define __KFCResultTree_h__

#include "PMString.h"


namespace KFCResultTree
{
	/** (Re)load the panel's result tree from KFCResultModel. A book result comes up with the book
	    row open and its chapters closed; an All Documents result with its document rows closed; a
	    document result with its one chapter open. Story and run rows come up open in every case. Safe
	    to call when the panel is closed (does nothing then). */
	void Rebuild();

	/** Repaint the existing rows from the model WITHOUT rebuilding the tree. For changes that touch
	    only what a row DRAWS - the check boxes behind Check All / Uncheck All - where the tree's
	    shape (which chapters, how many hits) is untouched.

	    Costs one notification per BRANCH row, not per hit: NodeChanged carries childrenChangedAlso,
	    so the framework refreshes a node's children itself. That is the book row (when there is
	    one), each chapter, its run rows and its story rows - the hit rows under a story are
	    grandchildren (or deeper), so the chapter's own call does not reach them. A chapter has a
	    handful of those, not a few thousand, so it stays a handful of calls. Rebuild() by contrast
	    tears the whole tree down and re-expands it, which is what made a large result set expensive.

	    It also KEEPS the expansion state, so a chapter the user collapsed stays collapsed (Rebuild
	    re-expands everything). Safe to call when the panel is closed (does nothing then). */
	void RefreshRows();

	/** Repaint ONLY the rows that read out a checked count: the book row, the one chapter row named
	    here and its story rows. What a single check box changes and nothing more - the box draws
	    itself, and no other hit row is affected - so this is what a box's observer calls instead of
	    RefreshRows.
	    Pass -1 for the chapter to refresh the book row alone. Safe when the panel is closed. */
	void RefreshCheckedCounts(int32 chapterIdx);

	/** ONE DOCUMENT ROW IS ABOUT TO GO (All Documents: a document was closed). Tells
	    the tree BEFORE the model empties the chapter (KFCResultModel::CloseChapter) - ITreeViewMgr's
	    BeforeNodeDeleted, the way the conditional text panel takes a row out - so only that row and its
	    children leave, and every other row keeps its place and whether it is open (a Rebuild would close
	    them all again). Safe when the panel is closed. */
	void BeforeChapterRowGoes(int32 chapterIdx);

	/** Write a message to the panel's message area (drawn by hand - KFCStatusTextView). Safe
	    to call when the panel is closed (does nothing then). Lives with the tree because it reaches
	    the panel exactly the way Rebuild does. */
	void ShowStatus(const PMString& message);

	/** Put the status read-out back to what THIS session last had on it - the last message - or, when
	    nothing has run since launch, to the opening message (the string table's kKFCStaticTextKey). Called from the panel's AutoAttach, and only
	    from there.

	    Why it is needed: a widget's string is persisted in the WORKSPACE, so a rebuilt panel comes
	    back carrying the last message of whatever session wrote it, including yesterday's - while
	    the results it describes are gone. The panel's show is the one moment that can outrank the
	    persisted value, the same moment the tab's name and the illustration are written at.

	    Does NOT change the kept line: restoring a line is not the panel reporting something. Safe
	    to call when the panel is closed (does nothing then). */
	void RestoreStatusOnPanelShow();

	/** Release this module's static storage during the controlled shutdown (the UI half's,
	    KFCUIStartupShutdown), so no static destructor at DLL unload finds work left to do: the kept
	    status line - a PMString, exactly the kind of static the rule was written for
	    (KFCResultModel::ShutdownCleanup). */
	void ShutdownCleanup();

	/** Say on the status line WHAT was just ticked or cleared, and over WHICH row:

	        ch1.indd  all checked
	        selftest.indb  all unchecked

	    Check All / Uncheck All only. Those two reach every hit of a book or of a
	    document, most of them scrolled out of sight, so what they did has to be said somewhere the
	    user is looking - and WHICH row they were asked over is the whole question, since the same
	    two commands mean "this chapter" or "the whole book" depending on it.

	    Ticking a single box does not come through HERE - it has a line of its own, one row narrower:
	    ShowHitCheckStatus below. It says WHICH row it was ("P1(2)  checked") and no count: the book
	    and document rows read the "(n/m checked)" out themselves.

	    @param targetName the row the menu was popped over - a chapter's name, or the book's.
	    @param nowChecked true = Check All, false = Uncheck All. */
	void ShowCheckAllStatus(const PMString& targetName, bool nowChecked);

	/** The same, for ONE box:

	        P1(2)  checked
	        P4  unchecked

	    Named by its LOCATOR, which is what the row itself leads with - so the line reads as an echo
	    of the row that was clicked, the way the Check All line echoes a chapter's name.

	    @param locator the hit row's page locator (KFCResultModel::GetHitDisplay's first field).
	    @param nowChecked the state the box was just put into. */
	void ShowHitCheckStatus(const PMString& locator, bool nowChecked);
}

#endif // __KFCResultTree_h__
