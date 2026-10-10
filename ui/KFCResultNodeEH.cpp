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
//  out which: a hit row jumps, a chapter row shows its document, a STORY ("font") row shows the
//  document it sits in (it names no hit, so it falls to the same arm as its chapter), the book row
//  activates its book.
//  Simplified from KESCL (which split fresh clicks onto a selection observer).
//
//  The shape of the hook is the layer panel's (LayerTreeRowPanelEH::LButtonUp): act on the button
//  going UP, only when the base handler did NOT claim the event, only without Shift / Cmd, and only
//  for a row that ended up SELECTED. What each of those buys here:
//    * UP, not DOWN  - a press that turns into a drag, or that the user rolls off before letting
//                      go, is not a request to go anywhere.
//    * !result       - the base returns kTrue for what it handled itself (a drag, an expander).
//                      Jumping on top of that would be a second action from one click.
//    * no Shift/Cmd  - those are selection modifiers, not "take me there". Since 1.4.0 they select hit
//                      rows together (O17 - KFCResultTreeController.cpp), and their button-up makes the
//                      page follow the selection (KFCResultTree::FollowModifiedClick).
//    * IsSelected    - the row the click actually landed on. The press already set the selection,
//                      so an ordinary click on a hit row still passes and still jumps.
//
//  A DOUBLE click is not told apart (1.4.0 - the author's call of 2026-10-10): the click selects the match with
//  the Type tool on (or marks it, when it cannot be selected - KFCJump.h), so the double click's own selection,
//  which gave the keyboard to the text, went. Each of its clicks is a click: the jump again, the keyboard left on
//  the list. THE MARKER COMES UP AT ONCE, on the button-up - the beat KCM's Story-mode jump keeps, which the user
//  asked for.
//
//  RIGHT-click on a hit row pops its menu (Replace), on a story row its menu (Search This Story Again); on any other
//  row it does nothing (spec F16). See RButtonDn
//  at the foot of this file.
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
#include "KFCDiagHiddenDocs.h"	// the hidden-document check after a click - test builds only
#include "KFCResultTree.h"		// TakeKeyboard - the hand-off after a click; DiagKeyFocus - who holds the keyboard, for that trace

namespace
{

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

	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 RButtonDn(IEvent* e);
#ifdef KFC_DIAG
	virtual bool16 LButtonDn(IEvent* e);	// (test builds only) the CLICKFOCUS trace of the press
#endif
};

CREATE_PMINTERFACE(KFCResultNodeEH, kKFCResultNodeEHImpl)

bool16 KFCResultNodeEH::LButtonUp(IEvent* e)
{
	KFC_DIAG_HIDDEN_DOCS_CHECK("click");		// (test builds only - a document left with no window when the click is over)
	// Let the stock handler finish the click (selection, expand / collapse, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);
	KFC_DIAG_LOG("CLICKFOCUS row LButtonUp stock=%d shift=%d cmd=%d focus=%s", static_cast<int>(result),
		static_cast<int>(e->ShiftKeyDown()), static_cast<int>(e->CmdKeyDown()), KFCResultTree::DiagKeyFocus().c_str());
	if (result)
		return result;
	if (e->ShiftKeyDown() || e->CmdKeyDown())
	{
		// SHIFT / CTRL - ROWS SELECTED TOGETHER (O17 - the author's calls of 2026-10-10, the Layers panel's way): the press
		// has made the selection (KFCResultTreeController); the page follows it - object rows' items selected, a text row
		// added jumped to - or the message area says why the row was not added (KFCResultTree::FollowModifiedClick). A
		// story, document or book row selected this way is selected alone, with no jump. The keyboard to the list after
		// it, as after every click (below), so Shift+Down goes on from here.
		InterfacePtr<ITreeNodeIDData> clickedData(this, UseDefaultIID());
		if (clickedData != nil)
			KFCResultTree::FollowModifiedClick(clickedData->Get());
		(void)KFCResultTree::TakeKeyboard();
		return result;
	}

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

	// The jump - its selection, or its marker - now, on every button-up: a double click's second click is a click
	// like the first (see the note at the head of this file).
	KFCJump::ActivateNode(nodeID->GetChapter(), nodeID->GetHit());

	// Hand the keyboard focus to the LIST, so the up / down arrows walk the tree from here on
	// (KFCResultTreeEH). Two things happen below, and BOTH are needed:
	//
	//   * The QUERY brings the list's IID_IEVENTHANDLER into existence. Interface implementations
	//     are created on first use, and nothing else in this plug-in ever asks the tree for its
	//     event handler - so without this line KFCResultTreeEH is never constructed at all and the
	//     arrows keep the stock behaviour (visible rows only). Measured: with the panel
	//     open and a book searched, a trace in that class's constructor never fired.
	//   * The hand-off makes it the key target. ActivateNode above brings a document window -
	//     or, on a book row, the Book panel - forward, and that takes the focus with it.
	//
	// AFTER the jump, deliberately: acquiring first and jumping second leaves the arrows stranded
	// in the document. KESCL hit exactly this and settled on the same order (KESCLResultNodeEH.cpp).
	//
	// THROUGH THE PANEL'S DOORS, EVERY CLICK (2026-10-10 - the author's report: Find/Change's title bar clicked, then a
	// row, and the down arrow moved the selected page item instead of walking the list). At that click the list still
	// held InDesign's keyboard ("focus=tree" in the trace), so the hand-off - IKeyBoard alone, and only when it named
	// another holder - did nothing, and the arrow never reached the list. KFCResultTree::TakeKeyboard makes the panel the
	// active one with the keyboard given to it (IPanelMgr::ShowPanelByWidgetID), then the list - what the arrows' walk
	// does after each landing.
	InterfacePtr<IEventHandler> treeEH(treeController, UseDefaultIID());
	bool acquireOnly = false;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch click-acquire-only: the hand-off as it was before 2026-10-10 - the case that shows
	// what the panel's doors add (click-arrow-after-title).
	acquireOnly = KFC_DIAG_FAULT("click-acquire-only");
#endif
	if (acquireOnly)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());	// IKeyBoard is the app's
		InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
		if (treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() != treeEH)
			keyBoard->AcquireKeyFocus(treeEH);
	}
	else if (treeEH != nil)
		(void)KFCResultTree::TakeKeyboard();
	KFC_DIAG_LOG("CLICKFOCUS row LButtonUp handed the keyboard to the list focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return result;
}

#ifdef KFC_DIAG
// (Test builds only) The press, for the CLICKFOCUS trace: who held the keyboard - InDesign's and the system's - as the
// click began (2026-10-10, Find/Change's title bar).
bool16 KFCResultNodeEH::LButtonDn(IEvent* e)
{
	KFC_DIAG_LOG("CLICKFOCUS row LButtonDn focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return TreeNodeEventHandler::LButtonDn(e);
}
#endif

// Right-click on a HIT row: pop its menu - Replace, about THIS row (the user's call; about all of them when it is one of
// several rows selected together - O18, KFCActionComponent) - at the cursor (PopRowMenu).
// Same machinery as the real Links and Layers panel row menus (LinksUITreeRowPanelEH and friends) and as
// KESCL's own report rows, which this is copied from (KESCLResultNodeEH::RButtonDn): HandlePopupMenu pops a
// MenuDef subtree by its name (KFCUI.fr), and the item the user picks fires through the ordinary action
// component. The clicked row is stashed FIRST - the action is handed no widget context of its own, so the
// model's context-menu row (KFCResultModel::GetContextMenuHit) is how it learns what the menu was about.
//
// A STORY row's right-click pops ITS menu - Search This Story Again (the author's call of 2026-10-09), stashed the same
// way (KFCResultModel::GetContextMenuStory). The book and document rows have no menu (spec F16) - their right-click is
// taken and does nothing.
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
	// BOTH stashes are cleared first, so a menu item fired later (a script invoking its ActionID - neither item is
	// offered for a shortcut) cannot act on a row nobody right-clicked this time. (Until 2026-10-09 only the story's
	// was: a hit row right-clicked, then a story row, and the Replace action invoked by a script wrote the hit row -
	// measured, case ctx-hit-stash-story-rclick - the final audit's D7-1.)
	KFCResults()->SetContextMenuHit(-1, -1);
	KFCResults()->SetContextMenuStory(-1, -1);
	if (nodeID->IsHitRow())
	{
		KFCResults()->SetContextMenuHit(nodeID->GetChapter(), nodeID->GetHit());
		PopRowMenu(kKFCResultHitMenuName, e, this);
	}
	else if (nodeID->IsFontRow() && KFCResults()->IsStoryGroup(nodeID->GetChapter(), nodeID->GetFont()))
	{
		// A STORY row: its own menu (Search This Story Again - the author's call of 2026-10-09).
		KFCResults()->SetContextMenuStory(nodeID->GetChapter(), nodeID->GetFont());
		PopRowMenu(kKFCResultStoryMenuName, e, this);
	}
	return kTrue;
}

// End, KFCResultNodeEH.cpp.
