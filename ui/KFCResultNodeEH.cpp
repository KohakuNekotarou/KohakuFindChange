//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Tree row event handler: a click on a HIT row jumps to that occurrence. Replaces
//  IID_IEVENTHANDLER on the result tree's node boss (kKFCResultNodeWidgetBoss). Derives from the
//  stock TreeNodeEventHandler so ordinary tree behaviour (select, expand/collapse, drag) is kept;
//  only the button-UP is extended. EVERY row has somewhere to go - KFCJump::ActivateNode sorts
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
//  DOUBLE-click on a hit row adds the other half: after the jump has pointed at the
//  match, it SELECTS it - Type tool, match highlighted - so the user can edit or copy without
//  hunting for it with the mouse. Single click still only points; that is the design
//  (KFCJump.h). Which of the two a button-up is doing rides on gSelectOnNextButtonUp below, whose
//  note explains why it cannot simply be done inside ButtonDblClk.
//
//  THE FIRST CLICK'S MARKER COMES UP AT ONCE, AND THE SECOND CLICK TAKES IT DOWN. The jump runs on
//  the first button-up and raises its marker there; a double click then selects, and the ordinary
//  KFCHitMarkerView::Hide at the end of a successful SelectHitText takes the marker down. A double
//  click that is REFUSED (KFCJump.h lists why it can be) never reaches that Hide, so its marker stays
//  up - the rule that a refusal is still pointed at. That is the beat KCM's Story-mode jump keeps, and
//  the user asked for it. (Booked for the double-click interval instead, so that a double click never
//  flashes one, every single click's marker arrives about half a second after the view has moved.)
//
//  RIGHT-click on a hit row pops its menu (Replace); on any other row it does nothing (2026-10-06, spec F16 -
//  the book, document and story rows' menus went with Change Checked). See RButtonDn at the foot of this file.
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
#include "KFCResultNodeID.h"
#include "KFCJump.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces (the model/UI split)
#include "KFCDiag.h"			// KFC_DIAG_LOG - the CLICKFOCUS trace, test builds only
#include "KFCResultTree.h"		// DiagKeyFocus - who holds the keyboard, for that trace

namespace
{

// THE DOUBLE-CLICK'S ONE BIT OF STATE, AND WHY IT IS NEEDED.
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

// Pop a row's right-click menu (a MenuDef subtree, by its internal name) at the click. The item the user picks
// fires through the ordinary action component.
void PopRowMenu(const char* menuName, IEvent* e, IPMUnknown* widget)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	InterfacePtr<IMenuManager> menuMgr(actionMgr, UseDefaultIID());
	if (menuMgr != nil)
		menuMgr->HandlePopupMenu(menuName, e->GlobalWhere(), e->GlobalWhere(), kTrue, widget);
}

}

class KFCResultNodeEH : public TreeNodeEventHandler
{
public:
	KFCResultNodeEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KFCResultNodeEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);
	virtual bool16 RButtonDn(IEvent* e);
};

CREATE_PMINTERFACE(KFCResultNodeEH, kKFCResultNodeEHImpl)

// Nothing of this plug-in's own happens on the way DOWN (see the note at the head of this file for
// why the jump rides the button coming up). The one job here is to start every click with the
// double-click flag down.
bool16 KFCResultNodeEH::LButtonDn(IEvent* e)
{
	gSelectOnNextButtonUp = false;
	const bool16 handled = TreeNodeEventHandler::LButtonDn(e);
	KFC_DIAG_LOG("CLICKFOCUS row LButtonDn handled=%d focus=%s", static_cast<int>(handled), KFCResultTree::DiagKeyFocus().c_str());
	return handled;
}

// The second click of a double click. Only a HIT row has anything extra to offer: a chapter or book
// row's double click is the tree's own expand / collapse, which the base handler does.
bool16 KFCResultNodeEH::ButtonDblClk(IEvent* e)
{
	const bool16 result = TreeNodeEventHandler::ButtonDblClk(e);
	if (result || e->ShiftKeyDown() || e->CmdKeyDown())
		return result;

	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return result;
	TreeNodePtr<KFCResultNodeID> nodeID(nodeData->Get());
	if (nodeID != nil && nodeID->IsHitRow())
		gSelectOnNextButtonUp = true;

	return result;
}

bool16 KFCResultNodeEH::LButtonUp(IEvent* e)
{
	// Consumed here, on the way in, so that every path out of this function leaves it down.
	const bool selectRatherThanJump = gSelectOnNextButtonUp;
	gSelectOnNextButtonUp = false;

	// Let the stock handler finish the click (selection, expand / collapse, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);
	KFC_DIAG_LOG("CLICKFOCUS row LButtonUp stock=%d shift=%d cmd=%d focus=%s", static_cast<int>(result),
		static_cast<int>(e->ShiftKeyDown()), static_cast<int>(e->CmdKeyDown()), KFCResultTree::DiagKeyFocus().c_str());
	if (result || e->ShiftKeyDown() || e->CmdKeyDown())
		return result;

	// The node's NodeID lives on this boss's ITreeNodeIDData (every TreeNode widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return result;
	const NodeID& node = nodeData->Get();
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	if (nodeID == nil || nodeID->IsRoot())
		return result;		// the hidden root has nowhere to go

	// The selection lives on the tree, not on the row, so ask upwards for it.
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return result;
	InterfacePtr<ITreeViewController> treeController(
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	if (treeController == nil || !treeController->IsSelected(node))
	{
		KFC_DIAG_LOG("CLICKFOCUS row LButtonUp: the row is not selected - no jump, no hand-off");
		return result;
	}

	// The second click of a double click SELECTS instead of jumping again.
	// The first click already did the jump (fronted the document, centred the match, raised the
	// marker), so repeating it would only re-do all of that. What is added is putting the user IN
	// the match - Type tool, match highlighted.
	if (selectRatherThanJump)
	{
		if (KFCJump::SelectHitText(nodeID->GetChapter(), nodeID->GetHit()))
		{
			// AND GIVE THE KEYBOARD BACK. The FIRST click of this double click ended in
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
		// the beat KCM's Story-mode jump keeps (not booked for the double-click interval - see
		// KFCJump.h).
		KFCJump::ActivateNode(nodeID->GetChapter(), nodeID->GetHit());
	}

	// Hand the keyboard focus to the LIST, so the up / down arrows walk the tree from here on
	// (KFCResultTreeEH). Two things happen in this one call, and BOTH are needed:
	//
	//   * The QUERY brings the list's IID_IEVENTHANDLER into existence. Interface implementations
	//     are created on first use, and nothing else in this plug-in ever asks the tree for its
	//     event handler - so without this line KFCResultTreeEH is never constructed at all and the
	//     arrows keep the stock behaviour (visible rows only). Measured: with the panel
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
	KFC_DIAG_LOG("CLICKFOCUS row LButtonUp handed the keyboard to the list focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return result;
}

// Right-click on a HIT row: pop its menu - Replace, about THIS row (the user's call) - at the cursor (PopRowMenu).
// Same machinery as the real Links and Layers panel row menus (LinksUITreeRowPanelEH and friends) and as
// KESCL's own report rows, which this is copied from (KESCLResultNodeEH::RButtonDn): HandlePopupMenu pops a
// MenuDef subtree by its name (KFCUI.fr), and the item the user picks fires through the ordinary action
// component. The clicked row is stashed FIRST - the action is handed no widget context of its own, so the
// model's context-menu row (KFCResultModel::GetContextMenuHit) is how it learns what the menu was about.
//
// The book, document and story rows have no menu since 2026-10-06 (spec F16: their Replace, Check All and
// Uncheck All went with Change Checked) - their right-click is taken and does nothing.
//
// Deliberately NOT calling the stock handler and NOT changing the selection: the selection is what
// the arrow keys walk from, and a right-click that is only asking for a menu should not move the
// user's place in the tree. (KESCL had a sharper version of the same rule - there a selection change
// drove the jump.)
bool16 KFCResultNodeEH::RButtonDn(IEvent* e)
{
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return TreeNodeEventHandler::RButtonDn(e);
	TreeNodePtr<KFCResultNodeID> nodeID(nodeData->Get());
	if (nodeID == nil || nodeID->IsRoot())
		return TreeNodeEventHandler::RButtonDn(e);

	// Every row's right-click is consumed - no stock handling, so the row is not selected and nothing jumps.
	if (nodeID->IsHitRow())
	{
		KFCResults()->SetContextMenuHit(nodeID->GetChapter(), nodeID->GetHit());
		PopRowMenu(kKFCResultHitMenuName, e, this);
	}
	return kTrue;
}

// End, KFCResultNodeEH.cpp.
