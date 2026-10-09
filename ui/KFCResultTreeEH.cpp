//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Event handler for the result list ITSELF (the tree-view boss, not a row). It adds two things
//  to the stock up / down arrows and one key of its own - Return on a hit row - and leaves
//  everything else alone:
//
//    1. A row that is CLOSED opens when the arrows land on it. The stock keys walk the VISIBLE
//       rows only, and a book search deliberately comes up with every chapter closed
//       (KFCResultTree::Rebuild), so without this the arrows would tour the chapter headings and
//       never once step inside a chapter. Opening on arrival means holding the down arrow tours
//       the whole book: land on a chapter, it opens, the next press is its first hit.
//    2. The landing runs the row's action - KFCJump::ActivateNode, exactly what a click on that
//       row would do: a hit row jumps, a chapter row shows its document, a STORY ("font") row
//       shows the document it sits in (it names no hit, so it takes the chapter's arm), the book
//       row activates its book.
//    3. RETURN / ENTER ON A HIT ROW REPLACES IT (spec F17) - see KeyDown. With the arrows,
//       the keyboard alone walks the rows and replaces where the match is right. SHIFT+RETURN
//       replaces it and goes on to the next row below that can be replaced (the author's call of
//       2026-10-08 - InDesign's own Change/Find for the list) - see GoOnToNextReplaceableRow.
//
//  WHY THE STOCK HANDLER MOVES, NOT A WALK OF OUR OWN
//
//  "Let the stock handler move, then open what it landed on" - not a tree-order walk over the model
//  working out the next / previous row itself. The stock handler only ever selects rows the tree
//  actually has; a hand-rolled walk once counted chapters with GetChapterCount() where the tree is
//  built from GetDisplayChapterCount(), so a result set over the display cap sent it after a node
//  that does not exist.
//
//  TreeViewEventHandler is the stock base (source/open/includes/widgets; on the CPP.rsp path) and
//  HandleUpDownKey is virtual precisely for this. Home / End / PageUp / PageDown and the left /
//  right expand / collapse keys stay stock.
//
//  Shift+Return has no stock move to lean on - it skips rows - so it walks, but through the tree's
//  OWN hierarchy adapter (KFCResultListAdapter, what the tree view builds its rows from), never the
//  model's counts: it can only reach a row the tree has.
//
//  NOTE: THIS CLASS ONLY EXISTS IF SOMETHING ASKS FOR IT. Interface implementations are created on
//  first QueryInterface, so naming it in KFCUI.fr is not enough - KFCResultNodeEH's key-focus
//  hand-off is what brings it into being (and what puts the arrows here at all). See the long
//  comment there before removing that call.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IControlView.h"
#include "IEvent.h"
#include "IEventDispatcher.h"		// Push / Remove - the Return filter on the application's stack (KFCReturnFilterEH)
#include "IEventHandler.h"
#include "IEventUtils.h"			// RemoveNextKeyCmd - a Return taken whole (TakeReturn)
#include "IKeyBoard.h"				// who holds the keyboard - the filter acts only while the list does
#include "IPalettePanelUtils.h"		// QueryPanelByWidgetID - the list, reached through its panel (the filter)
#include "IPanelControlData.h"
#include "ISession.h"
#include "ITreeViewController.h"
#include "ITreeViewHierarchyAdapter.h"	// the tree's own order - the row Shift+Return goes on to
#include "ITreeViewMgr.h"

// General includes:
#include "CEventHandler.h"			// the Return filter's base: every event passed on unless it is taken
#include "CreateObject.h"			// ::CreateObject2 - the Return filter's boss
#include "keyboarddefs.h"			// kVirtualUpArrowKey / kVirtualDownArrowKey / kVirtualReturnKey / kVirtualEnterKey
#include "TreeViewEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)
#include "Utils.h"					// Utils<IEventUtils>

#include <chrono>					// a Return's hold on the keyboard - how long at most (gHolding)
#include <vector>					// the rows above the one Shift+Return goes on to, opened top down

// Project includes:
#include "KFCUIID.h"
#include "KFCResultNodeID.h"
#include "KFCJump.h"
#include "KFCResultTree.h"		// ReplaceRow - Return on a hit row
#include "KFCModelAccess.h"		// KFCRuns()->CanReplaceHit - which row Shift+Return goes on to
#include "KFCDiag.h"			// KFC_DIAG_LOG - the RETFOCUS trace, test builds only
#ifdef KFC_DIAG
#include <windows.h>			// CaptureStackBackTrace - who takes the keyboard (DiagCallers), test builds only
#include <cstdio>
#include <cstring>
#include <string>
#endif

namespace
{

// One walk at a time. Landing on a row opens a document, and opening a document RUNS THE MESSAGE
// LOOP - so a held-down arrow key can arrive back here while the previous landing is still opening
// a chapter, and the second walk would step from a selection the first one has not finished making.
bool gWalking = false;

class WalkGuard
{
public:
	WalkGuard() { gWalking = true; }
	~WalkGuard() { gWalking = false; }
};

// THE KEYBOARD KEPT THROUGH A RETURN. A Return on a hit row replaces it (KeyDown) and the list must keep the
// keyboard, so the arrows walk on - but InDesign gives it away the moment KeyDown returns: the widget layer's own event
// dispatch, with no code of ours on the stack, asks the holder to let go and hands the keyboard to the document - a
// panel's Return going back to the layout, the convention for a palette. The arrows then moved the page item selected
// in the layout (the author's find). Measured in the test build's trace (RETFOCUS, the callers' modules): the request
// comes from DV_WidgetBin's dispatch straight after this handler's KeyDown, and goes through WillingToGiveUpKeyFocus -
// which IEventHandler.h offers for exactly this ("Return kFalse to hold onto the keyboard focus").
// So a handled Return HOLDS: give-up requests are refused until that Return's own KeyUp arrives - the keyboard is the
// user's to move again from then on - and for kHoldAfterReturnMs at most, so a hold whose KeyUp never comes (the key
// taken from us some other way) cannot keep refusing a click into the document later.
const int32 kHoldAfterReturnMs = 500;
bool gHolding = false;
std::chrono::steady_clock::time_point gHoldUntil;

void HoldAfterReturn()
{
	gHolding = true;
	gHoldUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHoldAfterReturnMs);
}

bool StillHolding()
{
	if (gHolding && std::chrono::steady_clock::now() > gHoldUntil)
		gHolding = false;
	return gHolding;
}

// The row after `from` in the order the rows are drawn, top to bottom - asked of the tree's own hierarchy adapter (see the
// head of the file): a row's first child, else its next sibling, else the next sibling of the nearest row above it that has
// one. An invalid NodeID after the last row. Kept in a NodeID before anything reads it: TreeNodePtr reads a NodeID_rv's
// class without asking whether there is one (NodeID.h), and the adapter answers kInvalidNodeID at the ends.
NodeID NextRowInTreeOrder(const ITreeViewHierarchyAdapter* adapter, const NodeID& from)
{
	if (adapter->GetNumChildren(from) > 0)
		return NodeID(adapter->GetNthChild(from, 0));
	NodeID node = from;
	for (;;)
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsRoot())
			return NodeID();
		const NodeID parent(adapter->GetParentNode(node));
		if (!parent.IsValid())
			return NodeID();
		const int32 next = adapter->GetChildIndex(parent, node) + 1;
		if (next > 0 && next < adapter->GetNumChildren(parent))
			return NodeID(adapter->GetNthChild(parent, next));
		node = parent;
	}
}

// SHIFT+RETURN GOES ON (the author's call of 2026-10-08 - InDesign's own Change/Find, "replace, then find the next", for the
// list). After the row it wrote, the next HIT row below it that can be replaced is selected, brought into view and landed
// on the way the arrows land on a row (HandleUpDownKey): its action - the jump, and a GREP row's Preview Text - then the
// keyboard taken back. Rows that cannot be replaced are stepped over (already replaced, locked, not a match of Find/Change:
// KFCRuns()->CanReplaceHit, the question the row menu greys Replace by), and a closed row above the one landed on is opened
// - a book search brings its chapters up closed. false = no such row below: the selection stays where it is.
bool GoOnToNextReplaceableRow(ITreeViewController* controller, const NodeID& from)
{
	InterfacePtr<const ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (adapter == nil || treeMgr == nil)
		return false;
	NodeID next = from;
	int32 chapterIdx = -1, hitIdx = -1;
	for (;;)
	{
		next = NextRowInTreeOrder(adapter, next);
		TreeNodePtr<KFCResultNodeID> nodeID(next);
		if (nodeID == nil)
			return false;
		if (nodeID->IsHitRow() && KFCRuns()->CanReplaceHit(nodeID->GetChapter(), nodeID->GetHit()))
		{
			chapterIdx = nodeID->GetChapter();
			hitIdx = nodeID->GetHit();
			break;
		}
	}
	// Every row above it open, top down, so the tree draws it. (ExpandNode on an open row is a harmless no-op -
	// HandleUpDownKey leans on the same.)
	std::vector<NodeID> above;
	for (NodeID up(adapter->GetParentNode(next)); up.IsValid(); up = NodeID(adapter->GetParentNode(up)))
	{
		TreeNodePtr<KFCResultNodeID> upID(up);
		if (upID == nil || upID->IsRoot())
			break;		// the hidden root is never drawn
		above.push_back(up);
	}
	for (auto it = above.rbegin(); it != above.rend(); ++it)
		treeMgr->ExpandNode(*it, kFalse /*expandAllDescendants*/);
	// THE WRITTEN ROW'S SELECTION OFF FIRST. The list selects one row at a time ("Items selectable: 1", KFCUI.fr), and in
	// such a tree Select does not move a selection - it refuses while a row is selected (ITreeViewController::SelectCode
	// eSingleItemAlreadySelected; measured: the next row was jumped to while the written one stayed selected).
	// The official re-selection (docs/ai-notes/api-official-examples.md, "select a tree row again"): the deselect tells
	// nobody - only the row that ends selected is announced - and both repaint.
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	const ITreeViewController::SelectCode selected = controller->Select(next, kTrue /*notifyOfChange*/, kTrue /*changeHilite*/);
	KFC_DIAG_LOG("GOON select code=%d chapter=%d hit=%d", static_cast<int>(selected), static_cast<int>(chapterIdx), static_cast<int>(hitIdx));
	(void)selected;
	treeMgr->ScrollToNode(next, ITreeViewMgr::eScrollIntoView);
	{
		// One walk at a time, as for the arrows (gWalking): the landing may open a chapter, and that runs the message loop.
		WalkGuard walkGuard;
		KFCJump::ActivateNode(chapterIdx, hitIdx);
	}
	// The landing fronted a document window, which took the keyboard with it - as after the arrows.
	(void)KFCResultTree::TakeKeyboard();
	return true;
}

// RETURN ON THE SELECTED HIT ROW (spec F17), one place for the two that take it: the list's own KeyDown and the Return
// filter below. Return or the keypad's Enter, no modifier, one HIT row selected: that row is replaced through the
// right-click Replace's own door (KFCResultTree::ReplaceRow - nothing happens on a row that cannot be replaced), the list
// keeps the keyboard, and the key is taken whole. kFalse = not a Return this list acts on: the caller passes it on.
// WITH SHIFT (2026-10-08), the same, and then - only when the row was written - on to the next row below that can be
// replaced (GoOnToNextReplaceableRow); with none below, the status line says so after its own sentence. Ctrl and Alt
// still make it not a Return of ours.
//  - The KeyCmd a Return's character makes next is removed, first - the official shape for a list that acts on Return
//    (open/components/spellpanel/SpellListBoxEH.cpp, KeyDown), through IEventUtils (EventUtilities.h asks to be replaced
//    by it, and the interface's own note is this case: "when we handle the KeyDown and want to remove the following
//    KeyCmd"). That KeyDown also sets the event's SystemHandledState to kDontCall; IEvent.h now marks the call
//    DEPRECATED and a no-op, so it is not copied.
//  - The keyboard is taken back with the panel made the active one (KFCResultTree::TakeKeyboard), and held through the
//    Return (gHolding) - which is all a Return taken by the LIST's KeyDown can do: InDesign's widget layer gives the
//    keyboard back to the document after it (DVEventHandler::RestoreFocusOnDocument), on one road a hold refuses and
//    on another it cannot. Hence the filter: a Return taken there never reaches the widget layer at all.
bool16 TakeReturn(IEvent* e, ITreeViewController* controller)
{
	const VirtualKey key = e->GetVirtualKey();
	if ((!(key == kVirtualReturnKey) && !(key == kVirtualEnterKey)) || e->CmdKeyDown() || e->OptionAltKeyDown())
		return kFalse;
	const bool goOn = e->ShiftKeyDown() ? true : false;
	if (controller == nil)
		return kFalse;
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.size() != 1)
		return kFalse;
	TreeNodePtr<KFCResultNodeID> node(selected[0]);
	if (node == nil || node->IsRoot() || !node->IsHitRow())
		return kFalse;
	Utils<IEventUtils>()->RemoveNextKeyCmd(e);
	KFC_DIAG_LOG("RETFOCUS Return taken focus=%s repeat=%d", KFCResultTree::DiagKeyFocus().c_str(), e->IsRepeatKey() ? 1 : 0);
	// A RETURN HELD DOWN WRITES ONCE (2026-10-09 - the header re-read 52's proposal, the author's yes). Windows repeats a key
	// held down, and each repeat was one more replace and, with Shift, one more row on (one second of Shift+Return: a dozen
	// rows). A repeat (IEvent::IsRepeatKey - IEvent.h) is taken and writes nothing, as the application bar's search field
	// takes a held Return once (KFCAppBarSearchEnter.cpp); many rows at once are Change All's. (Fault switch
	// return-repeat, a test build's only - KFCDiag.h: the repeat goes through as before, the case that shows what this
	// stops.)
	bool repeatGoesThrough = false;
#ifdef KFC_DIAG
	repeatGoesThrough = KFC_DIAG_FAULT("return-repeat");
#endif
	if (e->IsRepeatKey() && !repeatGoesThrough)
	{
		HoldAfterReturn();
		return kTrue;
	}
	// A previous landing is still opening a document - see gWalking. The key is taken, and nothing is written from a
	// half-made selection.
	if (gWalking)
	{
		HoldAfterReturn();
		return kTrue;
	}
	PMString status;
	const bool wrote = KFCResultTree::ReplaceRow(node->GetChapter(), node->GetHit(), &status);
	// The write may have opened a closed chapter and given it a window, which takes the key focus - take it back, with the
	// panel made the active one again, or the next arrow press lands in the document instead of walking on.
	(void)KFCResultTree::TakeKeyboard();
	// Shift: on to the next row that can be replaced - from the row as it was selected (a NodeID is its chapter, story
	// and hit, which the write's repaint leaves standing).
	if (goOn && wrote && !GoOnToNextReplaceableRow(controller, selected[0]))
	{
		status.Append(" No row below can be replaced.");
		KFCResultTree::ShowStatus(status);
	}
	// Held from here, not before: the write itself lends the keyboard out and takes it back (InDesign's Find/Change code
	// the walk goes through - measured, two round trips that always came home), and those are not to be refused.
	HoldAfterReturn();
	KFC_DIAG_LOG("RETFOCUS Return done focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return kTrue;
}

// THE RETURN FILTER. Its own boss (kKFCReturnFilterBoss, KFCUI.fr), created once and released at shutdown,
// pushed on the application's IEventDispatcher while the list holds the keyboard - the stack the dispatcher offers an
// event to from the top down before anything below it (IEventDispatcher.h). Adobe's Layers panel pushes a handler the
// same way for the length of a drag (open/components/layerpanel/LayerProxyDragDropSourceEH.cpp).
IEventHandler* gReturnFilter = nil;
bool gFilterPushed = false;
bool gFilterShutDown = false;

// The result list's view, reached through its panel - nil while the panel is closed.
IControlView* ResultListView()
{
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	return (panelData != nil) ? panelData->FindWidget(kKFCResultListWidgetID) : nil;
}

IEventDispatcher* QueryDispatcher()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	return (app != nil) ? InterfacePtr<IEventDispatcher>(app, UseDefaultIID()).forget() : nil;
}

void PushReturnFilter()
{
	if (gFilterShutDown || gFilterPushed)
		return;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch no-return-filter: never pushed - to tell what the filter's being on the
	// stack changes from what the rest of the Return work does (a jumped-to replaced row drew hilited).
	if (KFC_DIAG_FAULT("no-return-filter"))
	{
		KFC_DIAG_LOG("RETFOCUS filter NOT pushed (fault switch no-return-filter)");
		return;
	}
#endif
	if (gReturnFilter == nil)
		gReturnFilter = ::CreateObject2<IEventHandler>(kKFCReturnFilterBoss, IID_IEVENTHANDLER);
	InterfacePtr<IEventDispatcher> dispatcher(QueryDispatcher());
	if (gReturnFilter == nil || dispatcher == nil)
		return;
	dispatcher->Push(gReturnFilter, IEventDispatcher::kPushOnTop);
	gFilterPushed = true;
	KFC_DIAG_LOG("RETFOCUS filter pushed depth=%d", static_cast<int>(dispatcher->Depth()));
}

void RemoveReturnFilter()
{
	if (!gFilterPushed)
		return;
	InterfacePtr<IEventDispatcher> dispatcher(QueryDispatcher());
	if (dispatcher != nil)
		dispatcher->Remove(gReturnFilter);
	gFilterPushed = false;
	KFC_DIAG_LOG("RETFOCUS filter removed");
}

}

/** Up / down arrows that open what they land on, then run that row's action; Return / Enter that replaces a
    selected hit row (see the top). */
class KFCResultTreeEH : public TreeViewEventHandler
{
public:
	KFCResultTreeEH(IPMUnknown* boss) : TreeViewEventHandler(boss) {}
	virtual ~KFCResultTreeEH() {}

	virtual bool16 HandleUpDownKey(IEvent* e, const VirtualKey& key);
	virtual bool16 KeyDown(IEvent* e);
	virtual bool16 KeyUp(IEvent* e);					// ends a Return's hold (gHolding)
	virtual bool16 WillingToGiveUpKeyFocus();		// refuses while a Return holds
	virtual void PostGetKeyFocus();					// pushes the Return filter
	virtual void PostGiveUpKeyFocus();				// takes it off again

#ifdef KFC_DIAG
	// (Test builds only) The trace of where a Return-replace let the keyboard go: the key events that reach
	// the list after the KeyDown, and the moments the list's keyboard is suspended and resumed. Each passes on to the
	// stock one.
	virtual bool16 KeyCmd(IEvent* e);
	virtual bool16 SuspendKeyFocus();
	virtual bool16 ResumeKeyFocus();
#endif
};

CREATE_PMINTERFACE(KFCResultTreeEH, kKFCResultTreeEHImpl)

#ifdef KFC_DIAG
namespace
{
// The callers above this point as "module+offset < module+offset ...", nearest first - which plug-in asked the list to
// let the keyboard go. Module and offset only: the names need symbols this build does not have.
std::string DiagCallers()
{
	void* frames[20];
	const USHORT count = ::CaptureStackBackTrace(1, 20, frames, nil);
	std::string out;
	for (USHORT i = 0; i < count; ++i)
	{
		HMODULE module = nil;
		char name[MAX_PATH] = "?";
		if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCSTR>(frames[i]), &module) && module != nil)
		{
			char path[MAX_PATH] = "";
			if (::GetModuleFileNameA(module, path, MAX_PATH) > 0)
			{
				const char* base = ::strrchr(path, '\\');
				::strcpy_s(name, sizeof(name), base != nil ? base + 1 : path);
			}
		}
		char one[MAX_PATH + 32];
		::sprintf_s(one, sizeof(one), "%s+0x%llx", name,
			static_cast<unsigned long long>(static_cast<char*>(frames[i]) - reinterpret_cast<char*>(module)));
		if (!out.empty())
			out += " < ";
		out += one;
	}
	return out;
}
}

bool16 KFCResultTreeEH::KeyCmd(IEvent* e)
{
	KFC_DIAG_LOG("RETFOCUS list KeyCmd vk=%d focus=%s", static_cast<int>(e->GetVirtualKey().GetDVKeyCode()),
		KFCResultTree::DiagKeyFocus().c_str());
	const bool16 handled = TreeViewEventHandler::KeyCmd(e);
	KFC_DIAG_LOG("RETFOCUS list KeyCmd done handled=%d focus=%s", static_cast<int>(handled), KFCResultTree::DiagKeyFocus().c_str());
	return handled;
}

bool16 KFCResultTreeEH::SuspendKeyFocus()
{
	const bool16 suspended = TreeViewEventHandler::SuspendKeyFocus();
	KFC_DIAG_LOG("RETFOCUS list SUSPENDED suspended=%d callers: %s", static_cast<int>(suspended), DiagCallers().c_str());
	return suspended;
}

bool16 KFCResultTreeEH::ResumeKeyFocus()
{
	const bool16 resumed = TreeViewEventHandler::ResumeKeyFocus();
	KFC_DIAG_LOG("RETFOCUS list RESUMED resumed=%d", static_cast<int>(resumed));
	return resumed;
}

#endif

// The list has the keyboard: the Return filter goes on the dispatcher's stack (see gReturnFilter).
void KFCResultTreeEH::PostGetKeyFocus()
{
	TreeViewEventHandler::PostGetKeyFocus();
	KFC_DIAG_LOG("RETFOCUS list GOT the keyboard");
	PushReturnFilter();
}

// ...and has let it go: the filter comes off. (One that stays - the keyboard taken from the list without this call -
// does nothing: it acts only while the list holds the keyboard.)
void KFCResultTreeEH::PostGiveUpKeyFocus()
{
	TreeViewEventHandler::PostGiveUpKeyFocus();
	KFC_DIAG_LOG("RETFOCUS list GAVE UP the keyboard focus=%s callers: %s", KFCResultTree::DiagKeyFocus().c_str(),
		DiagCallers().c_str());
	RemoveReturnFilter();
}

// See gHolding: while a handled Return holds, the request to let the keyboard go - InDesign's own, straight after
// KeyDown - is refused. Anything else asks the stock handler as before.
bool16 KFCResultTreeEH::WillingToGiveUpKeyFocus()
{
	if (StillHolding())
	{
		KFC_DIAG_LOG("RETFOCUS list REFUSED to give up the keyboard (a Return holds) callers: %s", DiagCallers().c_str());
		return kFalse;
	}
	const bool16 willing = TreeViewEventHandler::WillingToGiveUpKeyFocus();
	KFC_DIAG_LOG("RETFOCUS list asked to give up the keyboard willing=%d callers: %s", static_cast<int>(willing),
		DiagCallers().c_str());
	return willing;
}

// The Return's own KeyUp ends its hold (gHolding): from here the keyboard is the user's to move again.
bool16 KFCResultTreeEH::KeyUp(IEvent* e)
{
	const VirtualKey key = e->GetVirtualKey();
	if (key == kVirtualReturnKey || key == kVirtualEnterKey)
		gHolding = false;
	KFC_DIAG_LOG("RETFOCUS list KeyUp vk=%d focus=%s", static_cast<int>(key.GetDVKeyCode()),
		KFCResultTree::DiagKeyFocus().c_str());
	return TreeViewEventHandler::KeyUp(e);
}

bool16 KFCResultTreeEH::HandleUpDownKey(IEvent* e, const VirtualKey& key)
{
	KFC_DIAG_LOG("RETFOCUS list arrow vk=%d walking=%d", static_cast<int>(key.GetDVKeyCode()), gWalking ? 1 : 0);
	if (!(key == kVirtualDownArrowKey) && !(key == kVirtualUpArrowKey))
		return TreeViewEventHandler::HandleUpDownKey(e, key);

	// A previous landing is still opening a document - see gWalking. Swallow the key rather than
	// stepping from a half-made selection.
	if (gWalking)
		return kTrue;
	WalkGuard walkGuard;

	// The stock handler owns the movement: it knows which rows are on screen, how the selection
	// scrolls, and it can only ever land on a row the tree really has.
	const bool16 handled = TreeViewEventHandler::HandleUpDownKey(e, key);

	InterfacePtr<ITreeViewController> controller(this, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(this, UseDefaultIID());
	if (controller == nil || treeMgr == nil)
		return handled;

	// Where it landed. The list is single-selection, so anything else means the move did not
	// happen (an empty list, or already at the end) and there is nothing to open or run.
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.size() != 1)
		return handled;

	TreeNodePtr<KFCResultNodeID> node(selected[0]);
	if (node == nil || node->IsRoot())
		return handled;

	// A branch row opens on arrival, so the NEXT press steps inside it rather than over it. Hit
	// rows are leaves. ExpandNode on an already-open node is a harmless no-op, so the state is not
	// worth asking about first.
	if (!node->IsHitRow())
		treeMgr->ExpandNode(selected[0], kFalse /*expandAllDescendants*/);

	// The row's action - the same one a click on it would run, marker and all (the two come up at
	// once - see KFCJump.h).
	KFCJump::ActivateNode(node->GetChapter(), node->GetHit());

	// That action activated a document window - or, on a book row, the Book panel - which took the
	// key focus with it. Take it back, or the NEXT arrow press lands in the document instead of
	// walking on - and take it back with the panel made the active one again (KFCResultTree::TakeKeyboard):
	// a list holding InDesign's keyboard inside a panel that is no longer active lost it to the next
	// Return by a road the Return's hold could not stop.
	(void)KFCResultTree::TakeKeyboard();
	return kTrue;
}

// RETURN REPLACES THE SELECTED ROW (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F17 - the
// author's call: walk the rows with the arrows and replace with Return where the match is right, the keyboard alone). Return or
// the keypad's Enter, no modifier, one HIT row selected: that row is replaced through the right-click Replace's own door
// (KFCResultTree::ReplaceRow - nothing happens on a row that cannot be replaced). Any other key, a modified Return, or
// Return on a story / document / book row goes to the stock handler. Only while the TREE holds the keyboard - typing
// in a document never comes here.
bool16 KFCResultTreeEH::KeyDown(IEvent* e)
{
	gHolding = false;		// any key ends a previous Return's hold; this one may start a new one (TakeReturn)
	// THE RETURN, WHEN THE FILTER DID NOT GET IT FIRST (found by the author: after a Return had replaced a
	// row, the down arrow moved the page item selected in the layout instead of walking the list, while the right-click
	// Replace, which runs the same ReplaceRow, left the arrows walking). Normally the Return filter on the dispatcher's
	// stack takes it before it gets here (gReturnFilter); this is the same Return through the same door for when it
	// does not - the filter off the stack - with only the hold to keep the keyboard (TakeReturn).
	InterfacePtr<ITreeViewController> controller(this, UseDefaultIID());
	if (TakeReturn(e, controller))
		return kTrue;
	return TreeViewEventHandler::KeyDown(e);
}

/** THE RETURN FILTER - see gReturnFilter. Takes one thing: a Return or Enter on a selected hit row while the result
    list holds the keyboard - and that Return's KeyCmd and KeyUp with it, so no part of it reaches the widget layer,
    whose DVEventHandler gives the keyboard back to the document after a key it delivered (RestoreFocusOnDocument -
    what moved the page item). Everything else is passed on untouched: CEventHandler answers kFalse to every event. */
class KFCReturnFilterEH : public CEventHandler
{
public:
	KFCReturnFilterEH(IPMUnknown* boss) : CEventHandler(boss), fTook(false) {}
	virtual ~KFCReturnFilterEH() {}

	virtual bool16 KeyDown(IEvent* e);
	virtual bool16 KeyCmd(IEvent* e);
	virtual bool16 KeyUp(IEvent* e);

private:
	bool fTook;		// this filter took the Return now going through - its KeyCmd and KeyUp are taken too
};

CREATE_PMINTERFACE(KFCReturnFilterEH, kKFCReturnFilterEHImpl)

bool16 KFCReturnFilterEH::KeyDown(IEvent* e)
{
	fTook = false;
	// Every key the application dispatches passes here while the filter is on the stack, so the cheap question first.
	const VirtualKey key = e->GetVirtualKey();
	if (!(key == kVirtualReturnKey) && !(key == kVirtualEnterKey))
		return kFalse;
	// Only while the result list holds the keyboard - a Return anywhere else (a document, a dialog) is not ours.
	IControlView* listView = ResultListView();
	if (listView == nil)
		return kFalse;
	InterfacePtr<IEventHandler> listEH(listView, UseDefaultIID());
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (listEH == nil || keyBoard == nil || keyBoard->GetKeyFocus() != listEH)
		return kFalse;
	InterfacePtr<ITreeViewController> controller(listView, UseDefaultIID());
	if (!TakeReturn(e, controller))
		return kFalse;
	fTook = true;
	KFC_DIAG_LOG("RETFOCUS filter took the Return focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return kTrue;
}

bool16 KFCReturnFilterEH::KeyCmd(IEvent* e)
{
	const VirtualKey key = e->GetVirtualKey();
	return (fTook && (key == kVirtualReturnKey || key == kVirtualEnterKey)) ? kTrue : kFalse;
}

bool16 KFCReturnFilterEH::KeyUp(IEvent* e)
{
	const VirtualKey key = e->GetVirtualKey();
	if (!fTook || !(key == kVirtualReturnKey || key == kVirtualEnterKey))
		return kFalse;
	fTook = false;
	gHolding = false;		// the Return is over; the keyboard is the user's to move again (gHolding)
	KFC_DIAG_LOG("RETFOCUS filter took the Return's KeyUp focus=%s", KFCResultTree::DiagKeyFocus().c_str());
	return kTrue;
}

void KFCResultTree::ShutdownReturnFilter()
{
	// Off the stack and let go, and nothing pushed again: a handler left on the application's dispatcher when the .pln
	// unloads would be called into nothing.
	gFilterShutDown = true;
	RemoveReturnFilter();
	if (gReturnFilter != nil)
	{
		gReturnFilter->Release();
		gReturnFilter = nil;
	}
}

// End, KFCResultTreeEH.cpp.
