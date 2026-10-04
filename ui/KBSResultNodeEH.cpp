//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Tree row event handler (Task 3): a click on a HIT row jumps to that occurrence. Replaces
//  IID_IEVENTHANDLER on the result tree's node boss (kKBSResultNodeWidgetBoss). Derives from the
//  stock TreeNodeEventHandler so ordinary tree behaviour (select, expand/collapse, drag) is kept;
//  only the button-UP is extended. EVERY row now has somewhere to go - KBSJump::ActivateNode sorts
//  out which: a hit row jumps, a chapter row shows its document, a STORY ("font") or RUN row shows
//  the document it sits in (neither names a hit, so both fall to the same arm as their chapter), the
//  book row activates its book.
//  Simplified from KESCL (which split fresh clicks onto a selection observer).
//
//  The shape of the hook is the layer panel's (LayerTreeRowPanelEH::LButtonUp): act on the button
//  going UP, only when the base handler did NOT claim the event, only without Shift / Cmd, and only
//  for a row that ended up SELECTED. What each of those buys here:
//    * UP, not DOWN  - a press that turns into a drag, or that the user rolls off before letting
//                      go, is not a request to go anywhere.
//    * !result       - the base returns kTrue for what it handled itself (a drag, an expander).
//                      Jumping on top of that would be a second action from one click.
//    * no Shift/Cmd  - those are selection modifiers, not "take me there".
//    * IsSelected    - the row the click actually landed on. The press already set the selection,
//                      so an ordinary click on a hit row still passes and still jumps.
//
//  DOUBLE-click on a hit row (2026-08-09) adds the other half: after the jump has pointed at the
//  match, it SELECTS it - Type tool, match highlighted - so the user can edit or copy without
//  hunting for it with the mouse. Single click still only points; that was and remains the design
//  (KBSJump.h). Which of the two a button-up is doing rides on gSelectOnNextButtonUp below, whose
//  note explains why it cannot simply be done inside ButtonDblClk.
//
//  ***** THE FIRST CLICK'S MARKER COMES UP AT ONCE, AND THE SECOND CLICK TAKES IT DOWN. ***** The
//  jump runs on the first button-up and raises its marker there; a double click then selects, and
//  the ordinary KBSHitMarkerView::Hide at the end of a successful SelectHitText takes the marker down. A
//  double click that is REFUSED (KBSJump.h lists why it can be) never reaches that Hide, so its
//  marker stays up - the rule that a refusal is still pointed at. That is the beat KCM's Story-mode
//  jump keeps, and the user asked for it (2026-09-25). From 2026-08-09 until then the first click's
//  marker was BOOKED for the double-click interval instead, so a double click never flashed one - at
//  the price of every single click's marker arriving about half a second after the view had moved.
//
//  The row's "replace me" check box is a real widget of its own (kKBSResultCheckWidgetBoss) that
//  swallows its own clicks, so ticking a hit never arrives here and never jumps. Its observer is
//  KBSResultCheckObserver.
//
//  RIGHT-click (2026-08-01) pops the row's own context menu - the book / document rows', a story row's,
//  a run row's or a hit row's. See RButtonDn at the foot of this file.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// the route from the app to IMenuManager (the right-click popup)
#include "IApplication.h"		// the app boss carries IKeyBoard
#include "IEvent.h"				// ShiftKeyDown / CmdKeyDown; GlobalWhere - where to pop the menu
#include "IEventHandler.h"		// the LIST's handler - the arrow keys' owner (see the hand-off below)
#include "IKeyBoard.h"			// AcquireKeyFocus - hand the arrows to the list after a click
#include "IMenuManager.h"		// HandlePopupMenu - pops the rows' menus at the cursor
#include "ISession.h"
#include "ITreeNodeIDData.h"	// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"		// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "TreeNodeEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)

// Project includes:
#include "KFCUIID.h"
#include "KBSResultNodeID.h"
#include "KBSJump.h"
#include "KBSModelAccess.h"		// the model half, through its session interfaces (2026-10-01, the model/UI split)
#include "KBSResultTree.h"		// ShowRowMenuReason / DropRowMenuReason - why a row's menu has nothing to offer

namespace
{

// ***** THE DOUBLE-CLICK'S ONE BIT OF STATE, AND WHY IT IS NEEDED. *****
//
// A double click arrives as FOUR events, in this order:
//
//     LButtonDn   LButtonUp   ButtonDblClk   LButtonUp
//
// - so the FIRST up has already jumped by the time the double click is announced, and a SECOND up
// comes after it. Doing the selecting inside ButtonDblClk therefore does not work: the trailing up
// would run the jump a second time and take the keyboard focus back to the tree, undoing it.
//
// So ButtonDblClk only RAISES A FLAG, and the trailing up reads it and selects instead of jumping.
//
// ! The flag is cleared in LButtonDn, which is what makes it safe. Every click begins with a down,
//   so a flag that was set but never consumed (if a trailing up ever failed to arrive) cannot
//   survive into the next click and turn an ordinary single click into a selection.
//
// A file static, not a member: the rows' widgets are recycled as the tree scrolls, and this belongs
// to "the click going on right now" rather than to any one row. One click happens at a time.
bool gSelectOnNextButtonUp = false;

// Pop one of the rows' right-click menus (a MenuDef subtree, by its internal name) at the click. The item
// the user picks fires through the ordinary action component.
void PopRowMenu(const char* menuName, IEvent* e, IPMUnknown* widget)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	InterfacePtr<IMenuManager> menuMgr(actionMgr, UseDefaultIID());
	if (menuMgr != nil)
		menuMgr->HandlePopupMenu(menuName, e->GlobalWhere(), e->GlobalWhere(), kTrue, widget);
}

// Why a story row's Reject / Accept are grey, in the hit row's words (RButtonDn below) - asked once
// CanRejectStory has said no. "" = the story has no replaced row, so there is nothing to take back and nothing
// to say. The hit row's order: a footnote, a closed document, a hidden condition, then no change left.
// (2026-10-04, R-1: once a story's Check All stopped being offered over rows with no box - D-1 - such a story's
// menu can be grey from top to bottom, and then it does not open at all; the hidden condition was the only reason
// said until then.)
const char* StoryRejectGreyReason(int32 chapter, int32 group)
{
	PMString name;
	int32 rows = 0;
	if (!KBSResults()->GetFontDisplay(chapter, group, name, rows))
		return "";
	int32 replaced = 0, inFootnote = 0;
	for (int32 n = 0; n < rows; ++n)
	{
		const int32 hit = KBSResults()->GetFontGroupHit(chapter, group, n);
		bool checked = false, wasReplaced = false, locked = false;
		if (hit < 0 || !KBSResults()->GetHitFlags(chapter, hit, checked, wasReplaced, locked) || !wasReplaced)
			continue;
		++replaced;
		if (KBSResults()->GetHitInFootnote(chapter, hit))
			++inFootnote;
	}
	if (replaced == 0)
		return "";
	if (inFootnote == replaced)
		return "Reject / Accept Changes in This Story: not for matches inside a footnote - Track Changes records nothing there.";
	UIDRef storyDoc;
	IDFile storyFile;
	if (!(KBSResults()->GetChapterLocation(chapter, storyDoc, storyFile) && KBSChapters()->FindOpenChapterDoc(storyFile, storyDoc)))
		return "Reject / Accept Changes in This Story: its document is not open - open it to take the replace back.";
	if (KBSRuns()->StoryChangesHidden(chapter, group))
		return "Reject / Accept Changes in This Story: the replaced text is under a hidden condition - show the condition and try again.";
	// The guess names every cause measured (2026-10-04, scenario cross-check 1): a Ctrl+Z of the replace that the
	// list does not follow (a list Show Changes rebuilt - KBSUndoFollow.h), and a deletion with Track Changes on,
	// which InDesign folds into the deleter's own record (the [9b] re-read O9b-1). "Accepted or rejected" only, until then.
	return "Reject / Accept Changes in This Story: no tracked change of its replaces is left (undone, accepted, rejected or deleted?).";
}

}

class KBSResultNodeEH : public TreeNodeEventHandler
{
public:
	KBSResultNodeEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KBSResultNodeEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);
	virtual bool16 RButtonDn(IEvent* e);
};

CREATE_PMINTERFACE(KBSResultNodeEH, kKBSResultNodeEHImpl)

// Nothing of this plug-in's own happens on the way DOWN (see the note at the head of this file for
// why the jump rides the button coming up). The one job here is to start every click with the
// double-click flag down.
bool16 KBSResultNodeEH::LButtonDn(IEvent* e)
{
	gSelectOnNextButtonUp = false;
	return TreeNodeEventHandler::LButtonDn(e);
}

// The second click of a double click. Only a HIT row has anything extra to offer: a chapter or book
// row's double click is the tree's own expand / collapse, which the base handler does.
bool16 KBSResultNodeEH::ButtonDblClk(IEvent* e)
{
	const bool16 result = TreeNodeEventHandler::ButtonDblClk(e);
	if (result || e->ShiftKeyDown() || e->CmdKeyDown())
		return result;

	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return result;
	TreeNodePtr<KBSResultNodeID> nodeID(nodeData->Get());
	if (nodeID != nil && nodeID->IsHitRow())
		gSelectOnNextButtonUp = true;

	return result;
}

bool16 KBSResultNodeEH::LButtonUp(IEvent* e)
{
	// Consumed here, on the way in, so that every path out of this function leaves it down.
	const bool selectRatherThanJump = gSelectOnNextButtonUp;
	gSelectOnNextButtonUp = false;

	// Let the stock handler finish the click (selection, expand / collapse, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);
	if (result || e->ShiftKeyDown() || e->CmdKeyDown())
		return result;

	// The node's NodeID lives on this boss's ITreeNodeIDData (every TreeNode widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return result;
	const NodeID& node = nodeData->Get();
	TreeNodePtr<KBSResultNodeID> nodeID(node);
	if (nodeID == nil || nodeID->IsRoot())
		return result;		// the hidden root has nowhere to go

	// The selection lives on the tree, not on the row, so ask upwards for it.
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return result;
	InterfacePtr<ITreeViewController> treeController(
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	if (treeController == nil || !treeController->IsSelected(node))
		return result;

	// ***** The second click of a double click SELECTS instead of jumping again. *****
	// The first click already did the jump (fronted the document, centred the match, raised the
	// marker), so repeating it would only re-do all of that. What is added is putting the user IN
	// the match - Type tool, match highlighted.
	if (selectRatherThanJump)
	{
		if (KBSJump::SelectHitText(nodeID->GetChapter(), nodeID->GetHit()))
		{
			// ***** AND GIVE THE KEYBOARD BACK. ***** The FIRST click of this double click ended in
			// the AcquireKeyFocus at the foot of this function, so the TREE is holding the keyboard
			// at this moment. Left that way, the caret would sit in the text while the arrow keys
			// walked the panel and typing went nowhere - which is the one thing a user who asked
			// for this wants to do.
			//
			// ! WHERE IT GOES IS NOT CHOSEN HERE. IKeyBoard.h:49-53 says Relinquish "restores key
			//   focus to the PREVIOUS HOLDER" - it is a pop, not a hand-off to whoever should have
			//   it. What makes that the right holder is the ORDER of the first click: the jump
			//   fronted the document window and only then did the tree acquire, so the holder
			//   underneath is that window. If anything ever comes to hold the focus between those
			//   two - another palette, an edit box of ours - this hands the keyboard to THAT
			//   instead, and it will look like the double click stopped working.
			//   *The product does not lean on the pop when it cares where the focus lands: it
			//    remembers the handler itself and calls AcquireKeyFocus(saved) to put it back
			//    (spellpanel/SpellCheckWalker.cpp:95-139, SaveKeyboardEventHandler). That shape is
			//    available here if this ever needs to name the window it wants.
			//   *The bool16 both calls return (kFalse = the current holder would not let go) is
			//    ignored, as it is at every product call site.
			//
			// This is the deliberate difference between the two clicks: a single click LEAVES the
			// keyboard on the tree so the arrows keep walking the results, and a double click gives
			// it up because it is a request to stop reading and start editing.
			InterfacePtr<IEventHandler> treeEH(treeController, UseDefaultIID());
			InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
			InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
			if (treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() == treeEH)
				keyBoard->RelinquishKeyFocus();
			return result;
		}
		// It refused (SelectHitText's tests are the list) and has said why. Fall through: the first
		// click's jump already happened, and the arrows below still want the tree.
	}
	else
	{
		// The jump, and its marker, now - even though this click may yet turn out to be the first half
		// of a double click. Then the second click selects and SelectHitText takes the marker down,
		// the beat KCM's Story-mode jump keeps (2026-09-25; the marker used to be booked for the
		// double-click interval - see KBSJump.h).
		KBSJump::ActivateNode(nodeID->GetChapter(), nodeID->GetHit());
	}

	// Hand the keyboard focus to the LIST, so the up / down arrows walk the tree from here on
	// (KBSResultTreeEH). Two things happen in this one call, and BOTH are needed:
	//
	//   * The QUERY brings the list's IID_IEVENTHANDLER into existence. Interface implementations
	//     are created on first use, and nothing else in this plug-in ever asks the tree for its
	//     event handler - so without this line KBSResultTreeEH is never constructed at all and the
	//     arrows keep the stock behaviour (visible rows only). Measured 2026-08-01: with the panel
	//     open and a book searched, a trace in that class's constructor never fired.
	//   * AcquireKeyFocus makes it the key target. ActivateNode above brings a document window -
	//     or, on a book row, the Book panel - forward, and that takes the focus with it.
	//
	// AFTER the jump, deliberately: acquiring first and jumping second leaves the arrows stranded
	// in the document. KESCL hit exactly this and settled on the same order (KESCLResultNodeEH.cpp).
	// IKeyBoard lives on the application boss.
	InterfacePtr<IEventHandler> treeEH(treeController, UseDefaultIID());
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() != treeEH)
		keyBoard->AcquireKeyFocus(treeEH);
	return result;
}

// Right-click on a row: pop that kind of row's menu at the cursor (PopRowMenu). Same machinery as the
// real Links and Layers panel row menus (LinksUITreeRowPanelEH and friends) and as KESCL's own report
// rows, which this is copied from (KESCLResultNodeEH::RButtonDn): HandlePopupMenu pops a MenuDef
// subtree by its name (KFCUI.fr), and the item the user picks fires through the ordinary action
// component. The clicked row is stashed FIRST - the action is handed no widget context of its own,
// so the model's context-menu row (KBSResultModel::GetContextMenuChapter and its siblings) is how it
// learns what the menu was about.
//
// What the book / document rows' commands reach is exactly the row this was popped over: the BOOK row
// means every chapter (what the flyout used to do), a document row means that chapter alone. That
// question is the whole reason Check All / Uncheck All moved here (2026-08-01) - a flyout has no row to
// ask about.
//
// Deliberately NOT calling the stock handler and NOT changing the selection: the selection is what
// the arrow keys walk from, and a right-click that is only asking for a menu should not move the
// user's place in the tree. (KESCL had a sharper version of the same rule - there a selection change
// drove the jump.)
bool16 KBSResultNodeEH::RButtonDn(IEvent* e)
{
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return TreeNodeEventHandler::RButtonDn(e);
	TreeNodePtr<KBSResultNodeID> nodeID(nodeData->Get());
	if (nodeID == nil || nodeID->IsRoot())
		return TreeNodeEventHandler::RButtonDn(e);

	// Every menu below consumes the click - no stock handling, so the row is not selected and nothing jumps.
	//
	// ***** A STORY ROW (2026-09-27, the story level): its own menu, over that story's rows. *****
	// Every other right-click clears the story it named, so a story menu item fired later (a script, a
	// shortcut) cannot act on a story nobody right-clicked this time.
	if (nodeID->IsFontRow() && KBSResults()->IsStoryGroup(nodeID->GetChapter(), nodeID->GetFont()))
	{
		const int32 chapter = nodeID->GetChapter();
		const int32 group = nodeID->GetFont();
		KBSResults()->SetContextMenuGroup(chapter, group);
		KBSResults()->SetContextMenuRun(-1, -1);		// the run row's (2026-09-29): cleared like this one
		// ***** WHY ITS REJECT / ACCEPT ARE GREY (2026-10-02 a hidden condition, the user's call A; 2026-10-04 a
		// ***** footnote, a closed document and no change left as well, R-1). ***** The hit row's rule below: with
		// every item grey the popup does not open, so the status line is the only place to say it - and a story
		// whose replaced text is all under a hidden condition said nothing at all (case
		// reject-hidden-condition-story). StoryRejectGreyReason, above, says which.
		// ***** OVER THE LAST MESSAGE, AND TAKEN DOWN BY THE NEXT RIGHT-CLICK WITH NOTHING TO SAY (2026-10-04,
		// ***** O-3, the user's call). ***** Through ShowStatus until then: it became the last message and stayed,
		// reading as if it were about whatever row was right-clicked next (KBSResultTree::ShowRowMenuReason).
		PMString why;
		if (!KBSRuns()->CanRejectStory(chapter, group))
			why = StoryRejectGreyReason(chapter, group);
		if (!why.IsEmpty())
		{
			why.SetTranslatable(kFalse);
			KBSResultTree::ShowRowMenuReason(why);
		}
		else
			KBSResultTree::DropRowMenuReason();
		PopRowMenu(kKBSResultStoryMenuName, e, this);
		return kTrue;
	}
	KBSResults()->SetContextMenuGroup(-1, -1);

	// ***** A RUN ROW (2026-09-29, Show Changes by KohakuFindChange): its own menu, over that run's rows in
	// ***** this document. ***** Cleared by every other right-click, like the story row's.
	if (nodeID->IsRunRow())
	{
		KBSResults()->SetContextMenuRun(nodeID->GetChapter(), nodeID->GetRun());
		KBSResultTree::DropRowMenuReason();		// nothing to say over a run row (O-3)
		PopRowMenu(kKBSResultRunMenuName, e, this);
		return kTrue;
	}
	KBSResults()->SetContextMenuRun(-1, -1);

	// Hit rows carry their own menu since 2026-09-26 - Replace, Reject Change and Accept Change, about THIS
	// row (the user's call; until then a hit row had no menu, its check box being all a row needed).
	//
	// ***** WHY REJECT CHANGE IS GREY IS SAID HERE, BEFORE THE MENU. ***** When every item is disabled the
	// popup does not open at all (measured 2026-08-01 with Check All), so the status line is the only
	// place left to say it.
	if (nodeID->IsHitRow())
	{
		const int32 chapter = nodeID->GetChapter();
		const int32 hit = nodeID->GetHit();
		KBSResults()->SetContextMenuHit(chapter, hit);
		bool checked = false, replaced = false, locked = false;
		KBSResults()->GetHitFlags(chapter, hit, checked, replaced, locked);
		// (Over the last message since 2026-10-04 - the story row's note above, O-3.)
		if (KBSResults()->GetHitInFootnote(chapter, hit))
		{
			PMString why("Reject Change: not for a match inside a footnote - Track Changes records nothing there.");
			why.SetTranslatable(kFalse);
			KBSResultTree::ShowRowMenuReason(why);
		}
		else if (replaced && !KBSRuns()->RefreshRowFromRecords(chapter, hit))
		{
			// A closed document is said as such (2026-09-29, the defect re-check F-3): the records may all
			// be there, and the row reads "no tracked change" only because nothing is open to read them in.
			// ...and so is a hidden condition (2026-10-02, the user's call A): the records are there too, gone
			// out of the main text with the text the condition hides (case reject-hidden-condition).
			UIDRef rowDoc;
			IDFile rowFile;
			const bool open = KBSResults()->GetChapterLocation(chapter, rowDoc, rowFile)
				&& KBSChapters()->FindOpenChapterDoc(rowFile, rowDoc);
			PMString why(!open
				? "Reject Change: the document of this row is not open - open it to take the replace back."
				: KBSRuns()->RowChangeIsHidden(chapter, hit)
				? "Reject Change: this row's replaced text is under a hidden condition - show the condition and reject again."
				: "Reject Change: no tracked change of this replace is left for this row (undone, accepted or rejected in the Track Changes panel, or deleted?).");
			why.SetTranslatable(kFalse);
			KBSResultTree::ShowRowMenuReason(why);
		}
		else
			KBSResultTree::DropRowMenuReason();
		PopRowMenu(kKBSResultHitMenuName, e, this);
		return kTrue;
	}

	const int32 target = nodeID->IsBookRow()
		? static_cast<int32>(KBSResultModel::kContextMenuBookRow)
		: nodeID->GetChapter();
	KBSResults()->SetContextMenuChapter(target);
	KBSResultTree::DropRowMenuReason();		// nothing to say over a book or document row (O-3)
	PopRowMenu(kKBSResultRowMenuName, e, this);
	return kTrue;
}

// End, KBSResultNodeEH.cpp.
