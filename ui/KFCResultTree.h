//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The result list's doors for the rest of the UI half: the tree reloaded from KFCResultModel after a search or an
//  Undo (Rebuild) or its rows repainted, the panel's message area written, the rows' Replace and Search ... Again, the
//  rows selected together, and the keyboard the list keeps. Defined where each lives: KFCResultListWidgetMgr.cpp (the
//  tree, its rows - what a row looks like among them -, the message area, the rows' Replace and Search ... Again, the
//  keyboard handed to the list), KFCResultListAdapter.cpp (the live list's shape),
//  KFCResultTreeController.cpp (the rows selected together), KFCResultTreeEH.cpp (the Return filter and the keyboard
//  frame).
//
//========================================================================================

#ifndef __KFCResultTree_h__
#define __KFCResultTree_h__

#include "PMRect.h"
#include "PMString.h"
#include "KFCModelTypes.h"		// KFCRowsByChapter - the rows selected together, by document
#ifdef KFC_DIAG
#include <string>
#endif

class IControlView;
class ITreeViewController;
class NodeID;
class NodeID_rv;

namespace KFCResultTree
{
	/** WHAT A ROW OF THE LIST SAYS AND WHERE IT SITS (GetRowLook) - one answer for the rows the list draws
	    (KFCResultListWidgetMgr) and for the copies a kept tier holds of them (KFCResultHistory), so a kept row reads exactly
	    as the row it was. */
	struct RowLook
	{
		bool		hit;		// a hit row - the colour cell - rather than an expander and a label
		PMReal		shift;		// how far right of the list's outermost level; a tier's own level is not counted
		PMString	label;		// a book, document, story or tier row's "<name>  (R/N)"
		PMString	locator;	// a hit row's five parts, as IKFCRowData::SetSegments takes them ("Changed " at the head of
		PMString	flag;		//   the locator of a row this list has replaced)
		PMString	pre;
		PMString	match;
		PMString	post;

		RowLook() : hit(false), shift(0.0) {}
	};

	/** The look of a row of the LIVE list - a book, document, story or hit row, or the live tier's row - read off the
	    model. false for a row it cannot resolve, and for a kept tier's rows (KFCResultHistory::GetKeptLook answers those).
	    Defined in KFCResultListWidgetMgr.cpp, beside the drawing. */
	bool GetRowLook(const NodeID& node, RowLook& out);

	/** Does a rebuild bring this row of the live list up OPEN? A book row and a story row always do; a document row while the
	    results came from neither a book nor All Documents (Rebuild says why each). The one place the rule is kept: Rebuild
	    asks it, and so does a tier's copy made while the panel is closed (KFCResultHistory). Defined in
	    KFCResultListWidgetMgr.cpp. */
	bool OpensWhenDrawn(const NodeID& node);

	/** THE LIVE LIST'S SHAPE, as the list draws it (KFCResultListAdapter.cpp): how many rows hang under `node` - the hidden
	    root or the live tier's row for the list's top (its book row, or its documents), a book row's documents, a document
	    row's story rows (or its hits, an object search's), a story row's hits - and the nth of them. The adapter's answer for
	    the live rows, and what a tier's copy walks (KFCResultHistory). */
	int32 LiveRowCount(const NodeID& node);
	NodeID_rv LiveRow(const NodeID& node, int32 nth);

	/** (Re)load the panel's result tree from KFCResultModel. A book result comes up with the book
	    row open and its chapters closed; an All Documents result with its document rows closed; a
	    document result with its one chapter open. Story rows come up open in every case. While the results are kept in
	    tiers (KFCResultHistory), the kept tiers come up as they were left, and the live one open and scrolled to the top.
	    Safe to call when the panel is closed (does nothing then). */
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
	    BeforeNodeDeleted - so only that row and its children leave, and every other row keeps its place and
	    whether it is open (a Rebuild would close them all again).
	    BEFORE, AND IT HAS TO BE: ITreeViewMgr.h:78 asks for the call before the object is deleted (so the tree
	    can still get its size), and this tree's adapter reads the model, where an emptied chapter is no longer a
	    row. (The conditional text panel takes one row out with the same call, but after its list has changed,
	    from a lazy notification - its rows are named by a condition's UID, which names the row still.)
	    Safe when the panel is closed. */
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
	           "hidden layer" - the author's call), put in the heading: "Preview Text (not selected - no width):". nil
	           when it was selected. */
	bool ShowRowPreview(int32 chapterIdx, int32 hitIdx, const char* notSelectedWhy = nil);

	/** A ROW'S OWN NOTE on the message area: why a click did not select the row's match or item. Like a preview it is
	    the row's, not a report - not kept as the last message, and taken away by DropRowPreview when the next row is
	    landed on (else walking on with the arrows from a locked row would leave its reason standing beside a row that
	    was selected). Safe when the panel is closed. */
	void ShowRowNote(const PMString& note);

	/** The message area showing a preview or a row's note goes back to the last ordinary message - for a row that has
	    none, so the previous row's does not stand beside it. Nothing when it shows anything else. */
	void DropRowPreview();

	/** REPLACE THE ROWS (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F16 / F17 for one row;
	    the author's calls for rows selected together: hit rows of one kind, of one document or several). Return on the
	    selected rows and a hit row's right-click Replace (RowsOfRightClick) both come here. Writes nothing while a run of
	    ours is up (RefusedWhileRunning says so) or when none of them can be replaced (CanReplaceAny - the question the row
	    menu greys Replace by); then their Replace in one undo step, no prompt, the panel following Ctrl+Z and Redo -
	    object rows through KFCJump::ReplaceObjectRows (the document of the row selected last in front, its item left
	    selected - LastSelectedHit), text rows through the model (IKFCRuns::ReplaceRows) - the rows repainted and the
	    status line set. True = something written. outStatus, when given, gets the status line it set - Shift+Return adds
	    to it when there is no row below to go on to (KFCResultTreeEH). */
	bool ReplaceRows(const KFCRowsByChapter& rows, PMString* outStatus = nil);

	/** Can the Replace run on these rows - can any of them be replaced (KFCRuns()->CanReplaceHit)? The one question the
	    row menu greys Replace by (KFCActionComponent) and ReplaceRows asks first. */
	bool CanReplaceAny(const KFCRowsByChapter& rows);

	/** THE HIT ROWS SELECTED TOGETHER now: by document, in the order drawn; outObjects (optional) = object rows
	    rather than text rows. false when nothing is selected, or a story, document or book row is
	    (KFCResultTreeController.cpp). */
	bool GetSelectedHitRows(KFCRowsByChapter& outRows, bool* outObjects = nil);

	/** THE ROWS A RIGHT-CLICK'S REPLACE WRITES: every row selected together, by document in the order drawn, when
	    the hit row right-clicked is one of several selected; else that row alone. Asked by the menu's grey state and by
	    the Replace itself (KFCActionComponent) - one answer for both. */
	KFCRowsByChapter RowsOfRightClick(int32 chapterIdx, int32 hitIdx);

	/** SELECT ALL ROWS (a book, document or story row's right-click menu - the author's call): every hit row
	    under `row` selected - its rows expanded first, as a run walks what is drawn - for Return to replace them all
	    together. The page does not jump (the rows are the list's selection, not the page's); the message area says
	    how many were selected. false = none under it. */
	bool SelectAllRows(const NodeID& row);

	/** Is there a hit row under `row` for SelectAllRows to select - the rows it walks, the first of them enough? The one
	    question the row menu disables Select All Rows by (KFCActionComponent - the author's call on a row with none
	    under it, a document row Search This Document Again left at (0/0) or a book search's that found nothing:
	    disabled rather than doing nothing; InDesign leaves a disabled item out of a right-click menu). */
	bool CanSelectAllRows(const NodeID& row);

	/** A ROW CLOSED OVER SELECTED ROWS (KFCResultNodeObserver - the author's call O7-2b): the rows selected under
	    `closedRow` taken out of the selection - a selected row is a drawn row, as Select All Rows keeps it (Return and the
	    right-click Replace write the selection in the order drawn: they left the hidden rows selected and unwritten while
	    the message area still counted them). The message area says how many went and what is selected now. false = no
	    selected row was under it: nothing changed, the message area left as it is. */
	bool DeselectRowsUnder(const NodeID& closedRow);

	/** THE ROW SELECTED LAST among these rows - the one the page shows (FollowModifiedClick, ExtendSelection): where the
	    last click or Shift+arrow left the selection, while it is one of them; else the last of them. false for none. A
	    right-click does not move it (KFCResultNodeEH::RButtonDn takes no row). */
	bool LastSelectedHit(const KFCRowsByChapter& rows, int32& outChapter, int32& outHit);

	/** A SHIFT / CTRL CLICK'S BUTTON-UP on a row (KFCResultNodeEH): the page follows - the row added jumped to as a
	    click jumps (KFCJump::ActivateNode: the page shows the row selected last, a text row's match or an object row's
	    item, its document in front), a row taken away leaving the page as it is. Nothing for a story, document or book
	    row. */
	void FollowModifiedClick(const NodeID& clicked);

	/** SHIFT+DOWN / SHIFT+UP (KFCResultTreeEH - the author's calls): the run of hit rows grown or
	    shrunk by one row at its moving end - the run's other end stays where it began; story, document and book rows are
	    passed over, into the next story's and the next document's rows - and the page following (the moving end's jump,
	    text and object rows alike). false = not a run of hit
	    rows: the key is not this function's. */
	bool ExtendSelection(ITreeViewController* controller, bool down);

	/** SEARCH THIS STORY AGAIN (a story row's right-click menu - the author's call): nothing while a run of
	    ours is up (RefusedWhileRunning); then the story walked again (KFCRuns()->SearchStoryAgain), the tree rebuilt
	    when its rows were put back (their places in the list moved) and the status line set either way. True = done. */
	bool SearchStoryAgain(int32 chapterIdx, int32 groupIdx);

	/** SEARCH THIS DOCUMENT AGAIN (a document row's right-click menu - the author's call): as
	    SearchStoryAgain, over the whole document (KFCRuns()->SearchDocumentAgain). True = done. */
	bool SearchDocumentAgain(int32 chapterIdx);

	/** The result list's view, reached through its panel - nil while the panel is closed. Not counted (the panel holds
	    it): use it at once. Asked by whatever outside the panel wants the list alone - the tree's manager, its controller,
	    the keyboard's questions, the Return filter. (RedrawKeyboardFrame wants the panel's view as well, and asks the
	    panel for both.) */
	IControlView* ResultListView();

	/** THE LIST TAKES THE KEYBOARD, THE PANEL MADE ACTIVE: IPanelMgr::ShowPanelByWidgetID with giveKeyFocus,
	    then the panel's IPanelControlData::SetKeyboardFocus on the list - and IKeyBoard::AcquireKeyFocus if those did not
	    land it. After a row's click (KFCResultNodeEH), the arrows' walk and a Return's replace (KFCResultTreeEH) - a
	    document window has come forward - and after Select All Rows (a right-click's menu left the keyboard where it was).
	    Does nothing while the panel is closed. True = the list holds the keyboard. */
	bool TakeKeyboard();

	/** THE KEYBOARD FRAME (the author's call: "make it plain that the list has the keyboard"):
	    do the keys go to the result list now - IKeyBoard's focus is the list's own handler, AND the system sends the keys
	    to the panel's window (KFCPanelHasSystemKeyboard - a click on Find/Change's title bar moves the second and not the
	    first)? Asked when the panel draws the frame around the list (KFCPanelView::DV_Draw), and by the Return filter
	    before it takes a Return - one answer for both. false while the panel is closed. Defined in KFCResultTreeEH.cpp,
	    as the next two. */
	bool ListHoldsKeyboard();

	/** The strip around the list drawn again, so the keyboard frame comes or goes - called when the list takes the
	    keyboard or lets it go (KFCResultTreeEH's PostGetKeyFocus / PostGiveUpKeyFocus), when a window is made active
	    or inactive (its Return filter's Activate / Deactivate), and by TakeKeyboard (the panel's doors can bring the
	    system's keyboard back with InDesign's holder unchanged). Nothing while the panel is closed. */
	void RedrawKeyboardFrame();

	/** The keyboard frame's width in pixels, drawn just outside the list's edges - the panel leaves 3 or 4 px around the
	    list (KFCUI.fr). */
	const int32 kKeyboardFrameWidth = 2;

	/** The keyboard frame's four strips - above, below, left, right - kKeyboardFrameWidth wide just outside 'list' (the
	    list's frame, in the panel's coordinates). The one place they are worked out: the panel fills them
	    (KFCPanelView::DV_Draw) and RedrawKeyboardFrame has them drawn again, so the two cannot drift apart and leave a
	    strip behind. */
	void KeyboardFrameStrips(const PMRect& list, PMRect (&outStrips)[4]);

	/** The result list's Return filter off the application's event dispatcher and released, for good - the UI half's
	    shutdown (through ShutdownCleanup). Defined in KFCResultTreeEH.cpp, where the filter lives. */
	void ShutdownReturnFilter();

	/** A RUN OF OURS IS UP - its progress bar pumps events, so a key or a menu can arrive in the middle of it: the
	    status line says so and the caller turns it away. True = refused. The one place this is asked (ReplaceRows -
	    the rows' Replace and Return -, SearchStoryAgain, SearchDocumentAgain, Clear Results and Save Panel Settings).
	    A model half of another build answers here too: its stand-in is "a run standing", its words the reason. */
	bool RefusedWhileRunning();

	/** Put the status read-out back to what THIS session last had on it - the last message - or, when
	    nothing has run since launch, to the opening message (the string table's kKFCStaticTextKey). Called from the
	    panel's AutoAttach, and only from there.

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
	    trace says where in the Return it went (KFCResultTreeEH::KeyDown, ReplaceRows). */
	std::string DiagKeyFocus();

	/** (Test builds only.) The window the SYSTEM sends the keys to - the root of ::GetFocus(), by its title, and the
	    active window - for the trace beside DiagKeyFocus (made when, after Find/Change's title bar was clicked, the
	    list still held InDesign's keyboard, yet the down arrow moved the page item). Defined in KFCResultTreeEH.cpp. */
	std::string DiagSystemFocus();
#endif
}

#endif // __KFCResultTree_h__
