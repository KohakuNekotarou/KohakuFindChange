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
//  out which: a hit row jumps, a chapter row shows its document, the book row activates its book.
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

class KBSResultNodeEH : public TreeNodeEventHandler
{
public:
	KBSResultNodeEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KBSResultNodeEH() {}

	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 RButtonDn(IEvent* e);
};

CREATE_PMINTERFACE(KBSResultNodeEH, kKBSResultNodeEHImpl)

bool16 KBSResultNodeEH::LButtonUp(IEvent* e)
{
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

	KBSJump::ActivateNode(nodeID->GetChapter(), nodeID->GetHit());

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
