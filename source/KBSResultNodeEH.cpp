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
//  out which: a hit row jumps, a chapter row shows its document, a FONT row shows the document it
//  sits in (it names no hit, so it falls to the same arm as its chapter), the book row activates
//  its book.
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
//  ***** AND THE FIRST CLICK'S MARKER WAITS TO SEE WHETHER IT WAS ONE. ***** Because the jump runs on
//  the first button-up, a double click used to jump (marker up) and then select (marker down), showing
//  a red flash of a marker that was never meant to be seen. The jump from here therefore BOOKS its
//  marker for the double-click interval instead of raising it - the move is immediate, only the
//  marker waits - and the ordinary ClearMarker at the end of a successful SelectHitText is what calls
//  the booking off. Nothing in this file cancels it, deliberately: a double click that is REFUSED
//  (overset, locked, hidden, stale) never reaches that ClearMarker, so its marker still appears,
//  which is the rule that a refusal is still pointed at. See KBSDrawEventHandler.h.
//
//  The row's "replace me" check box is a real widget of its own (kKBSResultCheckWidgetBoss) that
//  swallows its own clicks, so ticking a hit never arrives here and never jumps. Its observer is
//  KBSResultCheckObserver.
//
//  RIGHT-click (2026-08-01) pops the rows' context menu - Check All / Uncheck All, which moved here
//  off the panel flyout the same day. See RButtonDn at the foot of this file.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// the route from the app to IMenuManager (the right-click popup)
#include "IApplication.h"		// the app boss carries IKeyBoard
#include "IEvent.h"				// ShiftKeyDown / CmdKeyDown; GlobalWhere - where to pop the menu
#include "IEventHandler.h"		// the LIST's handler - the arrow keys' owner (see the hand-off below)
#include "IKeyBoard.h"			// AcquireKeyFocus - hand the arrows to the list after a click
#include "IMenuManager.h"		// HandlePopupMenu - pops kKBSResultRowMenuName at the cursor
#include "ISession.h"
#include "ITreeNodeIDData.h"	// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"		// QueryParentFor - the row -> the tree that owns the selection

// General includes:
#include "TreeNodeEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)

// Project includes:
#include "KBSID.h"
#include "KBSResultNodeID.h"
#include "KBSJump.h"
#include "KBSResultModel.h"		// SetContextMenuChapter - which row the menu is about to act on

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
		// It refused (overset, or the text has moved) and has said why. Fall through: the first
		// click's jump already happened, and the arrows below still want the tree.
	}
	else
	{
		// kTrue = this click may still turn out to be the first half of a double click, so the jump's
		// MARKER is booked rather than raised (KBSJump.h). The move itself is not deferred: the
		// document fronts and the view scrolls now, as it always did.
		KBSJump::ActivateNode(nodeID->GetChapter(), nodeID->GetHit(), /*deferMarkerUntilClickSettles*/ true);
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

// Right-click on a row: pop Check All / Uncheck All at the cursor. Same machinery as the real Links
// and Layers panel row menus (LinksUITreeRowPanelEH and friends) and as KESCL's own report rows,
// which this is copied from (KESCLResultNodeEH::RButtonDn): HandlePopupMenu pops the MenuDef subtree
// named kKBSResultRowMenuName, and the item the user picks fires through the ordinary action
// component. The clicked row is stashed FIRST - the action is handed no widget context of its own,
// so KBSResultModel::GetContextMenuChapter is how it learns what the menu was about.
//
// What the two commands then reach is exactly the row this was popped over: the BOOK row means every
// chapter (what the flyout used to do), a document row means that chapter alone. That question is the
// whole reason the commands moved here - a flyout has no row to ask about.
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

	// Hit rows carry no context menu (user's call, 2026-08-01): checking ONE hit is what the row's own
	// check box is for, so a menu there would only offer to act on something else. The click is
	// consumed all the same - no menu, and no stock handling either, so the row is not selected and
	// nothing jumps.
	if (nodeID->IsHitRow())
		return kTrue;

	const int32 target = nodeID->IsBookRow()
		? static_cast<int32>(KBSResultModel::kContextMenuBookRow)
		: nodeID->GetChapter();
	KBSResultModel::SetContextMenuChapter(target);

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return kTrue;
	InterfacePtr<IActionManager> actionMgr(app->QueryActionManager());
	if (actionMgr == nil)
		return kTrue;
	InterfacePtr<IMenuManager> menuMgr(actionMgr, UseDefaultIID());
	if (menuMgr == nil)
		return kTrue;

	menuMgr->HandlePopupMenu(kKBSResultRowMenuName, e->GlobalWhere(), e->GlobalWhere(), kTrue, this);
	return kTrue;
}

// End, KBSResultNodeEH.cpp.
