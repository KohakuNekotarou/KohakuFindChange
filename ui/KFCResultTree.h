//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Result tree rebuild entry point. Called after KFCResultModel has been filled by a search (or its
//  shape changed by an Undo): reloads the panel's tree widget from the model. What it opens depends on the scope - a
//  BOOK result opens the book row and leaves the chapters closed (a book-wide run can fill the
//  panel with one chapter's hits and bury the fact that others matched), All Documents leaves its
//  document rows closed for the same reason, a single document opens its one chapter - and the story
//  rows always come up open. No priming is needed to get the expander arrows drawn: this
//  panel draws them itself, from the hierarchy adapter's child count. No-op when the panel is closed.
//  Implemented in KFCResultListWidgetMgr.cpp (it lives with the tree).
//
//========================================================================================

#ifndef __KFCResultTree_h__
#define __KFCResultTree_h__

#include "PMString.h"
#include <vector>
#ifdef KFC_DIAG
#include <string>
#endif

class ITreeViewController;
class NodeID;

namespace KFCResultTree
{
	/** (Re)load the panel's result tree from KFCResultModel. A book result comes up with the book
	    row open and its chapters closed; an All Documents result with its document rows closed; a
	    document result with its one chapter open. Story rows come up open in every case. Safe
	    to call when the panel is closed (does nothing then). */
	void Rebuild();

	/** Repaint the existing rows from the model WITHOUT rebuilding the tree. For changes that touch
	    only what a row DRAWS - a row's text, place or word after a replace, an Undo or a jump - where the
	    tree's shape (which chapters, how many hits) is untouched.

	    Costs one notification per BRANCH row, not per hit: NodeChanged carries childrenChangedAlso,
	    so the framework refreshes a node's children itself. That is the book row (when there is
	    one), each chapter and its story rows - the hit rows under a story are
	    grandchildren (or deeper), so the chapter's own call does not reach them. A chapter has a
	    handful of those, not a few thousand, so it stays a handful of calls. Rebuild() by contrast
	    tears the whole tree down and re-expands it, which is what made a large result set expensive.

	    It also KEEPS the expansion state, so a chapter the user collapsed stays collapsed (Rebuild
	    re-expands everything). Safe to call when the panel is closed (does nothing then). */
	void RefreshRows();


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

	/** A selected GREP row's AFTER-TEXT on the message area: "Preview Text:", the row's context faded and
	    what its Replace would write at full colour (KFCReplaceEngine::PreviewHit - written in a step thrown away).
	    false = no preview for this row (not a GREP search, replaced, the query or the story changed, its document
	    closed...) - then DropRowPreview. Not kept as the last message: a preview is the row's, not a report.
	    @param notSelectedWhy  a word or two on why the click did not select the row's match ("no width", "overset",
	           "hidden layer" - 1.4.0, the author's call of 2026-10-10), put in the heading: "Preview Text (not
	           selected - no width):". nil when it was selected. */
	bool ShowRowPreview(int32 chapterIdx, int32 hitIdx, const char* notSelectedWhy = nil);

	/** A ROW'S OWN NOTE on the message area (1.4.0): why a click did not select the row's match or item - and, for rows
	    selected together, what was selected (KFCJump::SelectObjectRows) or why a row was not added. Like a preview
	    it is the row's, not a report - not kept as the last message, and taken away by DropRowPreview when the next row
	    is landed on (else walking on with the arrows from a locked row would leave its reason standing beside a row
	    that was selected). Safe when the panel is closed. */
	void ShowRowNote(const PMString& note);

	/** The message area showing a preview or a row's note goes back to the last ordinary message - for a row that has
	    none, so the previous row's does not stand beside it. Nothing when it shows anything else. */
	void DropRowPreview();

	/** REPLACE ONE ROW (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F16 / F17): the
	    hit row's right-click Replace and Return on a selected row both come here. Writes nothing while a run of ours is
	    up (RefusedWhileRunning says so) or when the row cannot be replaced (KFCRuns()->CanReplaceHit - the question the
	    row menu greys Replace by); then the row's one replace (KFCRuns()->ReplaceHit, no prompt - one undo step, the
	    panel following Ctrl+Z and Redo), the rows repainted and the status line set. True = written. outStatus, when
	    given, gets the status line it set - Shift+Return adds to it when there is no row below to go on to
	    (KFCResultTreeEH). */
	bool ReplaceRow(int32 chapterIdx, int32 hitIdx, PMString* outStatus = nil);

	/** REPLACE THE ROWS SELECTED TOGETHER (O18 - the author's call of 2026-10-10): object rows of one document, by Return
	    or by the Replace of a right-click on one of them. One row = ReplaceRow. Nothing while a run of ours is up, nor
	    when none of them can be replaced (the menu is grey then); otherwise their Replace in one undo step
	    (KFCJump::ReplaceObjectRows), the rows repainted and the status line set. True = something written. */
	bool ReplaceRows(int32 chapterIdx, const std::vector<int32>& hitIdxs, PMString* outStatus = nil);

	/** THE OBJECT ROWS SELECTED TOGETHER now (O17): their document and their rows in the order drawn. false when nothing
	    is selected, or a text or branch row is (KFCResultTreeController.cpp). */
	bool GetSelectedObjectRows(int32& outChapter, std::vector<int32>& outHits);

	/** Is this object row one of SEVERAL selected together? outHits = them all, in the order drawn - what a right-click
	    Replace on it writes (O18). false (and outHits empty) when it is not, or only it is selected. */
	bool SelectionHoldsRow(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outHits);

	/** A SHIFT / CTRL CLICK'S BUTTON-UP on a row (KFCResultNodeEH, O17): the page's selection follows the object rows now
	    selected (KFCJump::SelectObjectRows - the row clicked shown when it was added), or the message area says why the
	    row could not be added (another document, another spread). Nothing for a text or branch row. */
	void FollowModifiedClick(const NodeID& clicked);

	/** SHIFT+DOWN / SHIFT+UP (KFCResultTreeEH, O17 - the author's call of 2026-10-10): the run of object rows grown or
	    shrunk by one row at its moving end - the run's other end stays where it began - and the page's selection
	    following. A row that cannot join stops it (the message area says why for another spread or document). false =
	    not a run of object rows: the key is not this function's. */
	bool ExtendSelection(ITreeViewController* controller, bool down);

	/** SEARCH THIS STORY AGAIN (a story row's right-click menu - the author's call of 2026-10-09): nothing while a run of
	    ours is up (RefusedWhileRunning); then the story walked again (KFCRuns()->SearchStoryAgain), the tree rebuilt
	    when its rows were put back (their places in the list moved) and the status line set either way. True = done. */
	bool SearchStoryAgain(int32 chapterIdx, int32 groupIdx);

	/** THE LIST TAKES THE KEYBOARD, THE PANEL MADE ACTIVE: IPanelMgr::ShowPanelByWidgetID with giveKeyFocus,
	    then the panel's IPanelControlData::SetKeyboardFocus on the list - and IKeyBoard::AcquireKeyFocus if those did not
	    land it. For the arrows' walk and a Return's replace (KFCResultTreeEH), after a document window has come forward.
	    Does nothing while the panel is closed. True = the list holds the keyboard. */
	bool TakeKeyboard();

	/** THE KEYBOARD FRAME (1.4.0 - the author's call of 2026-10-10: "make it plain that the list has the keyboard"):
	    does the result list hold InDesign's keyboard now - IKeyBoard's focus is the list's own handler? Asked when the
	    panel draws the frame around the list (KFCPanelView::Draw). false while the panel is closed. Defined in
	    KFCResultTreeEH.cpp, as the next one. */
	bool ListHoldsKeyboard();

	/** The strip around the list drawn again, so the keyboard frame comes or goes - called when the list takes the
	    keyboard or lets it go (KFCResultTreeEH's PostGetKeyFocus / PostGiveUpKeyFocus). Nothing while the panel is
	    closed. */
	void RedrawKeyboardFrame();

	/** The keyboard frame's width in pixels, drawn just outside the list's edges - the panel leaves 3 or 4 px around the
	    list (KFCUI.fr). */
	const int32 kKeyboardFrameWidth = 2;

	/** The result list's Return filter off the application's event dispatcher and released, for good - the UI half's
	    shutdown (through ShutdownCleanup). Defined in KFCResultTreeEH.cpp, where the filter lives. */
	void ShutdownReturnFilter();

	/** A RUN OF OURS IS UP - its progress bar pumps events, so a key or a menu can arrive in the middle of it: the
	    status line says so and the caller turns it away. True = refused. The one place this is asked (ReplaceRow -
	    the row's Replace and Return -, SearchStoryAgain and Clear Results). */
	bool RefusedWhileRunning();

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

#ifdef KFC_DIAG
	/** (Test builds only.) Who holds the keyboard, in a few words for the trace: "tree" (this list), "nobody", or the
	    holder's boss and widget. Made when a Return that replaced a row let the keyboard go to the document: the
	    trace says where in the Return it went (KFCResultTreeEH::KeyDown, ReplaceRow). */
	std::string DiagKeyFocus();

	/** (Test builds only.) The window the SYSTEM sends the keys to - the root of ::GetFocus(), by its title, and the
	    active window - for the trace beside DiagKeyFocus (2026-10-10: after Find/Change's title bar was clicked the
	    list still held InDesign's keyboard, yet the down arrow moved the page item). Defined in KFCResultTreeEH.cpp. */
	std::string DiagSystemFocus();
#endif
}

#endif // __KFCResultTree_h__
