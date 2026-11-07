//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Tree row event handler: a click on a HIT row jumps to that occurrence. Replaces IID_IEVENTHANDLER on the result
//  tree's node boss (kKFCResultNodeWidgetBoss). Derives from the stock TreeNodeEventHandler so ordinary tree behaviour
//  (select, expand/collapse, drag) is kept; only the button-UP is extended. EVERY row has somewhere to go -
//  KFCJump::ActivateNode sorts out which: a hit row jumps, a chapter row shows its document, a STORY ("font") row shows
//  the document it sits in (it names no hit, so it falls to the same arm as its chapter), the book row activates its
//  book. A GREY row - a row of a kept tier - switches to its tier, and that row is then selected and jumped to
//  (KFCResultHistory::SwitchToKeptRowLater); a grey tier's own row switches to it too (v2 R5); the live tier's own row
//  is selected alone and acts on nothing.
//  Simplified from KESCL (which split fresh clicks onto a selection observer).
//
//  The shape of the hook is the layer panel's (LayerTreeRowPanelEH::LButtonUp): act on the button
//  going UP, only when the base handler did NOT claim the event, only without Shift / Cmd, and only
//  for a row that ended up SELECTED. What each of those buys here:
//    * UP, not DOWN  - a press that turns into a drag, or that the user rolls off before letting
//                      go, is not a request to go anywhere.
//    * !result       - the base returns kTrue for what it handled itself (a drag, an expander).
//                      Jumping on top of that would be a second action from one click.
//    * no Shift/Cmd  - those are selection modifiers, not "take me there". They select hit rows
//                      together (KFCResultTreeController.cpp), and their button-up makes the page
//                      follow the selection (KFCResultTree::FollowModifiedClick).
//    * IsSelected    - the row the click actually landed on. The press already set the selection,
//                      so an ordinary click on a hit row still passes and still jumps.
//
//  A DOUBLE click is not told apart (the author's call): the click selects the match with
//  the Type tool on (or marks it, when it cannot be selected - KFCJump.h), so the double click's own selection,
//  which gave the keyboard to the text, went. Each of its clicks is a click: the jump again, the keyboard left on
//  the list. THE MARKER COMES UP AT ONCE, on the button-up - the beat KCM's Story-mode jump keeps, which the user
//  asked for.
//
//  RIGHT-click on a row pops that row's menu - each kind of row its own: a hit row's, an object row's, a story row's, a
//  document row's, the book row's and a tier's heading's (a kept row has none); what each holds is its MenuDef in
//  KFCUI.fr. See RButtonDn at the foot of this file.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// the route from the app to IMenuManager (the right-click popup)
#include "IApplication.h"		// the app: its action manager (the right-click popup), and the IKeyBoard of the old hand-off
#include "IEvent.h"				// ShiftKeyDown / CmdKeyDown; GlobalWhere - where to pop the menu
#include "IEventHandler.h"		// the list's handler - the old hand-off only (a test build's click-acquire-only)
#include "IKeyBoard.h"			// AcquireKeyFocus - the old hand-off only (KFCResultTree::TakeKeyboard hands it over now)
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
#include "KFCResultHistory.h"	// SwitchToKeptRowLater - a grey row's click
#include "KFCKohakuTab.h"		// IsJapaneseUI - a text row's Tate-chu-yoko, a Japanese InDesign's alone
#include "KFCStyleMenu.h"		// PutOnMenu - an object row's Apply Object Style > the front document's object styles; a table row's Apply Cell Style >, its cell styles;
								// a script label list's table row's Apply Table Style >, its table styles

#include <cstring>				// strcmp - which menu pops
#include <vector>				// a row's word's other readings (KFCResultTree::UnifyReadingsOf)

namespace
{

// What the hit row popping stands for beyond its text (IKFCResults::GetHitHolderKind - a script label list's table or cell row, a table row list's row);
// kKFCHolderNone for any other row.
int32 HolderOfRow(IPMUnknown* widget)
{
	InterfacePtr<const ITreeNodeIDData> nodeData(widget, UseDefaultIID());
	if (nodeData == nil)
		return kKFCHolderNone;
	TreeNodePtr<KFCResultNodeID> row(nodeData->Get());
	return (row != nil && row->IsHitRow()) ? KFCResults()->GetHitHolderKind(row->GetChapter(), row->GetHit()) : kKFCHolderNone;
}

// Pop a row's right-click menu (a MenuDef subtree, by its internal name) at the click. The item the user picks
// fires through the ordinary action component.
void PopRowMenu(const char* menuName, IEvent* e, IPMUnknown* widget)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	InterfacePtr<IMenuManager> menuMgr(actionMgr, UseDefaultIID());
	// GREY ITEMS STAY ON THE MENU (the author: "when it cannot be done, grey is fine" - SET-11): a row whose Replace
	// cannot write, a book row with no hit row under it, still open their menus, the item shown greyed out. kFalse,
	// where every caller in source/open passes kTrue: kTrue drops the grey items, and a menu of grey items alone does
	// not open at all - which looks like InDesign's own rule, and is this argument.
	bool16 removeDisabledItems = kFalse;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch menu-drop-grey: the grey items dropped again (kTrue) - the old way, for a test of the
	// greyed items to be armed against.
	if (KFC_DIAG_FAULT("menu-drop-grey"))
		removeDisabledItems = kTrue;
#endif
	if (menuMgr == nil)
		return;
	// TATE-CHU-YOKO, A JAPANESE INDESIGN'S ALONE (docs/superpowers/specs/2026-11-02-kfc-warichu-tcy-design.md B1): on a text row's Special Operations as its menu pops, or taken off - asked
	// each time, so a test build's fault switch not-japanese answers too. Taken off first, then put once (the SDK's AEMUIStartupShutdown.cpp's way: never twice on the menu).
	// NOT OVER A RUBY OR A KENTEN LIST'S ROWS (2026-11-07 - the author, waiting question 10: "the SP OP one,
	// not to show").
	if (std::strcmp(menuName, kKFCResultHitMenuName) == 0)
	{
		PMString path(kKFCHitSpecialOpsSubmenuPath);
		path.SetTranslatable(kFalse);
		menuMgr->RemoveMenuItem(path, kKFCTcyTextActionID);
		if (KFCKohakuTab::IsJapaneseUI() && !KFCResults()->ListIsRubyOrKenten())
			menuMgr->AddMenuItem(kKFCTcyTextActionID, path, kKFCTcyTextMenuItemPosition, kFalse);
		// EDIT IN STORY EDITOR (2026-11-06 - the author: "a document made hidden, its stories searched - opened in the Story Editor, only the story is edited"): over a
		// Story row - its own story's, not an internal one's (the XML structure's - no story a person writes) - taken off first, then put, as Tate-chu-yoko.
		menuMgr->RemoveMenuItem(path, kKFCEditInStoryEditorActionID);
		if (HolderOfRow(widget) == kKFCHolderStory)
			menuMgr->AddMenuItem(kKFCEditInStoryEditorActionID, path, kKFCEditInStoryEditorMenuItemPosition, kFalse);
		// A DOCUMENT LIST'S ROW'S THREE (2026-11-06 - W3: "Special Operations: Close", "Show, for a hidden document"; Save of the ideas - Revert taken out the
		// same night, the author: "no Revert"): over a Document row alone - taken off first, then put, as Tate-chu-yoko.
		static const ActionID kDocOps[] = { kKFCDocShowActionID, kKFCDocCloseActionID, kKFCDocSaveActionID };
		static const PMReal kDocOpPlaces[] = { PMReal(kKFCDocShowMenuItemPosition), PMReal(kKFCDocCloseMenuItemPosition), PMReal(kKFCDocSaveMenuItemPosition) };
		const bool documentRow = (HolderOfRow(widget) == kKFCHolderDocument);
		for (size_t k = 0; k < sizeof(kDocOps) / sizeof(kDocOps[0]); ++k)
		{
			menuMgr->RemoveMenuItem(path, kDocOps[k]);
			if (documentRow)
				menuMgr->AddMenuItem(kDocOps[k], path, kDocOpPlaces[k], kFalse);
		}
	}
	// UNIFY TO A READING (2026-11-03 - the author: an Inconsistent Ruby list's heading and rows offer their word's other readings, "<<tarou>> ni soroeru"): on a hit row's and a story row's menus,
	// under Replace, an item a reading the row's word has besides its own - taken off first, then put, as Tate-chu-yoko; named as the menu draws them (KFCActionComponent's UpdateActionStates).
	if (std::strcmp(menuName, kKFCResultHitMenuName) == 0 || std::strcmp(menuName, kKFCResultStoryMenuName) == 0)
	{
		static const ActionID kUnify[kKFCUnifyReadingItems] = { kKFCUnifyReading0ActionID, kKFCUnifyReading1ActionID, kKFCUnifyReading2ActionID,
			kKFCUnifyReading3ActionID, kKFCUnifyReading4ActionID, kKFCUnifyReading5ActionID };
		PMString path(menuName);
		path.SetTranslatable(kFalse);
		for (int32 k = 0; k < kKFCUnifyReadingItems; ++k)
			menuMgr->RemoveMenuItem(path, kUnify[k]);
		InterfacePtr<const ITreeNodeIDData> nodeData(widget, UseDefaultIID());
		PMString word;
		std::vector<PMString> readings;
		if (nodeData != nil && KFCResultTree::UnifyReadingsOf(nodeData->Get(), word, readings))
			for (int32 k = 0; k < kKFCUnifyReadingItems && k < static_cast<int32>(readings.size()); ++k)
				menuMgr->AddMenuItem(kUnify[k], path, PMReal(kKFCUnifyReadingMenuItemPosition + 0.01 * k), kFalse);
	}
	// APPLY OBJECT STYLE (docs/superpowers/specs/2026-11-05-kfc-apply-object-style-design.md A1 / A2): an object row's Special
	// Operations > Apply Object Style > - the front document's object styles, read again and put as the menu pops (KFCStyleMenu),
	// named as they are drawn (KFCActionComponent's UpdateActionStates). Its parent node is the MenuDef's.
	if (std::strcmp(menuName, kKFCResultObjectMenuName) == 0)
	{
		PMString path(kKFCApplyObjStyleSubmenuPath);
		path.SetTranslatable(kFalse);
		KFCStyleMenu::PutOnMenu(menuMgr, path, KFCStyleMenu::kObjectStyles);
	}
	// APPLY CELL STYLE (docs/superpowers/specs/2026-11-05-kfc-table-rows-cell-style-design.md T6): a text row's Special Operations > Apply Cell Style > - over
	// a row of a table row list, or a script label list's cell row (the script-label-tables-cells spec's L7): the submenu's entry put first (IMenuManager.h:
	// kInvalidActionID and the path with a delimiter - a node in the MenuDef stood empty on every text row's menu, measured 11-05), then the front document's
	// cell styles the same way as the object styles. APPLY TABLE STYLE > the same over a script label list's table row (L7 - its table styles). Over any other
	// text row both submenus go, entry and items (RemoveSubmenuAndChildren - taken off first every time, so neither is ever put twice).
	if (std::strcmp(menuName, kKFCResultHitMenuName) == 0)
	{
		PMString cellPath(kKFCApplyCellStyleSubmenuPath), tablePath(kKFCApplyTableStyleSubmenuPath);
		cellPath.SetTranslatable(kFalse);
		tablePath.SetTranslatable(kFalse);
		// (The entries TRANSLATABLE, as a MenuDef node's path is: put untranslatable, the submenu drew its key - "0x1EA600kKFCApplyCellStyleSubmenuKey", measured
		// 11-05. The items' path stays as the object styles' is - untranslatable, found by its keys.)
		PMString cellEntry(kKFCApplyCellStyleSubmenuEntry), tableEntry(kKFCApplyTableStyleSubmenuEntry);
		menuMgr->RemoveSubmenuAndChildren(cellEntry);
		menuMgr->RemoveSubmenuAndChildren(tableEntry);
		const int32 holder = HolderOfRow(widget);
		if (KFCResults()->ListIsTableRows() || holder == kKFCHolderCell)
		{
			menuMgr->AddMenuItem(kInvalidActionID, cellEntry, PMReal(kKFCApplyCellStyleSubmenuPosition), kFalse);
			KFCStyleMenu::PutOnMenu(menuMgr, cellPath, KFCStyleMenu::kCellStyles);
		}
		else if (holder == kKFCHolderTable)
		{
			menuMgr->AddMenuItem(kInvalidActionID, tableEntry, PMReal(kKFCApplyTableStyleSubmenuPosition), kFalse);
			KFCStyleMenu::PutOnMenu(menuMgr, tablePath, KFCStyleMenu::kTableStyles);
		}
		else
			KFCStyleMenu::PutOnMenu(menuMgr, cellPath, KFCStyleMenu::kNone);
	}
	// REMOVE LABEL (the script-label-tables-cells spec's L8b - common to every row of a script label list): on an object row's and a text row's Special Operations
	// over such a list - one action, taken off both paths first, then put on the path of the menu popping (the style items' rule: one action on one path).
	if (std::strcmp(menuName, kKFCResultHitMenuName) == 0 || std::strcmp(menuName, kKFCResultObjectMenuName) == 0)
	{
		PMString textPath(kKFCHitSpecialOpsSubmenuPath), objectPath(kKFCSpecialOpsSubmenuPath);
		textPath.SetTranslatable(kFalse);
		objectPath.SetTranslatable(kFalse);
		menuMgr->RemoveMenuItem(textPath, kKFCRemoveLabelActionID);
		menuMgr->RemoveMenuItem(objectPath, kKFCRemoveLabelActionID);
		if (KFCResults()->ListIsScriptLabels())
			menuMgr->AddMenuItem(kKFCRemoveLabelActionID, (std::strcmp(menuName, kKFCResultHitMenuName) == 0) ? textPath : objectPath,
				PMReal(kKFCRemoveLabelMenuItemPosition), kFalse);
	}
	// SPECIAL OPERATIONS > LABEL > (2026-11-06 - docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md LB2): over a row whose item can hold a label - an
	// object row (any list's), a Story row, a Kohaku+ Table row, a script label list's table or cell row (KFCRuns()->CanLabelHit) - on the path of the menu popping: the
	// submenu's entry put first, then its three items (Apply Cell Style's way: the entry translatable, the items' path not). Both paths' submenus taken off first every
	// time, so neither is ever put twice and an action stands on one path alone.
	if (std::strcmp(menuName, kKFCResultHitMenuName) == 0 || std::strcmp(menuName, kKFCResultObjectMenuName) == 0)
	{
		PMString hitEntry(kKFCHitLabelSubmenuEntry), objectEntry(kKFCObjLabelSubmenuEntry);
		menuMgr->RemoveSubmenuAndChildren(hitEntry);
		menuMgr->RemoveSubmenuAndChildren(objectEntry);
		InterfacePtr<const ITreeNodeIDData> nodeData(widget, UseDefaultIID());
		bool labels = false;
		if (nodeData != nil)
		{
			TreeNodePtr<KFCResultNodeID> row(nodeData->Get());
			labels = (row != nil && row->IsHitRow() && KFCRuns()->CanLabelHit(row->GetChapter(), row->GetHit()));
		}
		if (labels)
		{
			const bool objectMenu = (std::strcmp(menuName, kKFCResultObjectMenuName) == 0);
			PMString path(objectMenu ? kKFCObjLabelSubmenuPath : kKFCHitLabelSubmenuPath);
			path.SetTranslatable(kFalse);
			menuMgr->AddMenuItem(kInvalidActionID, objectMenu ? objectEntry : hitEntry, PMReal(kKFCLabelSubmenuPosition), kFalse);
			menuMgr->AddMenuItem(kKFCLabelAddKeyActionID, path, PMReal(kKFCLabelAddKeyMenuItemPosition), kFalse);
			menuMgr->AddMenuItem(kKFCLabelEditValueActionID, path, PMReal(kKFCLabelEditValueMenuItemPosition), kFalse);
			menuMgr->AddMenuItem(kKFCLabelRemoveKeyActionID, path, PMReal(kKFCLabelRemoveKeyMenuItemPosition), kFalse);
		}
	}
	menuMgr->HandlePopupMenu(menuName, e->GlobalWhere(), e->GlobalWhere(), removeDisabledItems, widget);
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
		// SHIFT / CTRL - ROWS SELECTED TOGETHER (the author's calls, the Layers panel's way): the press
		// has made the selection (KFCResultTreeController); the page follows it - the row added jumped to as a click jumps
		// (the page shows the row selected last), a row taken away leaving the page as it is
		// (KFCResultTree::FollowModifiedClick). A story, document or book row selected this way is selected alone, with no
		// jump. The keyboard to the list after it, as after every click (below), so Shift+Down goes on from here.
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
	// A GREY ROW - A ROW OF A KEPT TIER - SWITCHES TO ITS TIER
	// (docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md - the switch as v2 revised it, the author's
	// calls): booked, not made here - the switch draws the list anew, and this row's widget goes with the old tree
	// (KFCResultHistory::SwitchToKeptRowLater says when it runs, and why). Nothing of this row is read after. A GREY
	// TIER'S OWN ROW SWITCHES TO IT TOO (v2 R5 - the author's call), booked the same way with no row to land on
	// (GetKeptRow -1): a grey tier's row is never selected. (Its arrow opens and closes it, as every tier's does - the
	// arrow's own widget.)
	if (nodeID != nil && nodeID->IsGrey())
	{
		KFCResultHistory::SwitchToKeptRowLater(nodeID->GetTier(), nodeID->GetKeptRow());
		return result;
	}
	// THE LIVE TIER'S OWN ROW (the author's call: a heading row can be selected): the press selected it - alone,
	// as a story row is - and it acts on nothing (it names no document; Return writes nothing for it). The keyboard to the
	// list, so the arrows walk on from it, and the row's after-text left by the row before taken off the message line.
	if (nodeID != nil && nodeID->IsLiveTierRow())
	{
		KFCResultTree::DropRowPreview();
		(void)KFCResultTree::TakeKeyboard();
		return result;
	}
	if (nodeID == nil || nodeID->IsRoot() || nodeID->IsInert())
		return result;		// the hidden root has nowhere to go (a grey tier's rows switched above)

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
	// (KFCResultTreeEH). Two things happen in that hand-off (KFCResultTree::TakeKeyboard), and BOTH are needed:
	//
	//   * The QUERY of the list's IID_IEVENTHANDLER brings it into existence - interface implementations are created on
	//     first use; with none, KFCResultTreeEH is never constructed and the arrows keep the stock behaviour (visible
	//     rows only - measured: with the panel open and a book searched, a trace in that class's constructor never
	//     fired). The hand-off asks for it, and so does the keyboard frame the panel draws
	//     (KFCResultTree::ListHoldsKeyboard).
	//   * The hand-off makes it the key target. ActivateNode above brings a document window -
	//     or, on a book row, the Book panel - forward, and that takes the focus with it.
	//
	// AFTER the jump, deliberately: acquiring first and jumping second leaves the arrows stranded
	// in the document. KESCL hit exactly this and settled on the same order (KESCLResultNodeEH.cpp).
	//
	// THROUGH THE PANEL'S DOORS, EVERY CLICK (the user's report: Find/Change's title bar clicked, then a row, and the down
	// arrow moved the selected page item instead of walking the list). At that click the list still
	// held InDesign's keyboard ("focus=tree" in the trace), so the hand-off - IKeyBoard alone, and only when it named
	// another holder - did nothing, and the arrow never reached the list. KFCResultTree::TakeKeyboard makes the panel the
	// active one with the keyboard given to it (IPanelMgr::ShowPanelByWidgetID), then the list - what the arrows' walk
	// does after each landing.
	bool acquireOnly = false;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch click-acquire-only: the hand-off the old way, IKeyBoard alone - the case that shows
	// what the panel's doors add (click-arrow-after-title).
	acquireOnly = KFC_DIAG_FAULT("click-acquire-only");
#endif
	if (acquireOnly)
	{
		InterfacePtr<IEventHandler> treeEH(treeController, UseDefaultIID());
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());	// IKeyBoard is the app's
		InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
		if (treeEH != nil && keyBoard != nil && keyBoard->GetKeyFocus() != treeEH)
			keyBoard->AcquireKeyFocus(treeEH);
	}
	else
		(void)KFCResultTree::TakeKeyboard();
	KFC_DIAG_LOG("CLICKFOCUS row LButtonUp handed the keyboard to the list focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return result;
}

#ifdef KFC_DIAG
// (Test builds only) The press, for the CLICKFOCUS trace: who held the keyboard - InDesign's and the system's - as the
// click began (made for the case of Find/Change's title bar clicked first).
bool16 KFCResultNodeEH::LButtonDn(IEvent* e)
{
	KFC_DIAG_LOG("CLICKFOCUS row LButtonDn focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return TreeNodeEventHandler::LButtonDn(e);
}
#endif

// Right-click on a row: pop its menu - each kind of row its own (KFCUI.fr's MenuDefs) - at the cursor (PopRowMenu). A
// hit row's items are about THIS row (the author's call), or about all of them when it is one of several rows selected
// together (KFCActionComponent).
// Same machinery as the real Links and Layers panel row menus (LinksUITreeRowPanelEH and friends) and as
// KESCL's own report rows, which this is copied from (KESCLResultNodeEH::RButtonDn): HandlePopupMenu pops a
// MenuDef subtree by its name (KFCUI.fr), and the item the user picks fires through the ordinary action
// component. THIS ROW - the widget handed to HandlePopupMenu - is what the action reads to learn what the menu was
// about: the menu hands it on to the action's widget argument (KFCActionComponent's RightClickedRow), which is how the
// product's row menus read theirs (LayerPanelMenuComponent.cpp, ITreeNodeIDData). Nothing is stashed.
//
// Deliberately NOT calling the stock handler and NOT changing the selection: the selection is what
// the arrow keys walk from, and a right-click that is only asking for a menu should not move the
// user's place in the tree. (KESCL had a sharper version of the same rule - there a selection change
// drove the jump.) It keeps rows selected together as well: the stock press would collapse them to the row right-clicked
// (KFCResultTreeController's IgnoreIfNodeIsSelected says no with several selected), and the right-click Replace on one of
// them - meant for all of them - would write that row alone.
bool16 KFCResultNodeEH::RButtonDn(IEvent* e)
{
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return TreeNodeEventHandler::RButtonDn(e);
	TreeNodePtr<KFCResultNodeID> nodeID(nodeData->Get());
	if (nodeID == nil || nodeID->IsRoot())
		return TreeNodeEventHandler::RButtonDn(e);

	// Every row's right-click is consumed - no stock handling, so the row is not selected and nothing jumps. A kept row
	// has no menu (the stacked results' spec, section 3-5); a TIER's own row - a grey tier's and the live one's - has
	// its heading's menu.
	if (nodeID->IsTierRow())
		PopRowMenu(kKFCResultTierMenuName, e, this);
	else if (nodeID->IsHitRow())
	{
		// An OBJECT row (the Object tab's) pops a menu of its own - its Special Operations; a text row the hit row's.
		const bool objectRow = KFCResults()->GetHitItem(nodeID->GetChapter(), nodeID->GetHit()) != kInvalidUID;
		PopRowMenu(objectRow ? kKFCResultObjectMenuName : kKFCResultHitMenuName, e, this);
	}
	else if (nodeID->IsFontRow() && KFCResults()->IsStoryGroup(nodeID->GetChapter(), nodeID->GetFont()))
		PopRowMenu(kKFCResultStoryMenuName, e, this);		// a STORY row
	else if (nodeID->IsDocumentRow())
		PopRowMenu(kKFCResultDocumentMenuName, e, this);	// a DOCUMENT row
	else if (nodeID->IsBookRow())
		PopRowMenu(kKFCResultBookMenuName, e, this);		// the BOOK row
	return kTrue;
}

// End, KFCResultNodeEH.cpp.
