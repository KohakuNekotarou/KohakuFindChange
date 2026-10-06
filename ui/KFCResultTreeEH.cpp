//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Event handler for the result list ITSELF (the tree-view boss, not a row). It adds two things
//  to the stock up / down arrows, Return to a hit row, and leaves everything else alone:
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
//    3. RETURN / ENTER ON A HIT ROW REPLACES IT (2026-10-06, spec F17) - see KeyDown. With the arrows,
//       the keyboard alone walks the rows and replaces where the match is right.
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
//  NOTE: THIS CLASS ONLY EXISTS IF SOMETHING ASKS FOR IT. Interface implementations are created on
//  first QueryInterface, so naming it in KFCUI.fr is not enough - KFCResultNodeEH's key-focus
//  hand-off is what brings it into being (and what puts the arrows here at all). See the long
//  comment there before removing that call.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IEvent.h"
#include "IKeyBoard.h"				// taking the key focus back after a landing
#include "ISession.h"
#include "ITreeViewController.h"
#include "ITreeViewMgr.h"

// General includes:
#include "keyboarddefs.h"			// kVirtualUpArrowKey / kVirtualDownArrowKey / kVirtualReturnKey / kVirtualEnterKey
#include "TreeViewEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)

// Project includes:
#include "KFCUIID.h"
#include "KFCResultNodeID.h"
#include "KFCJump.h"
#include "KFCResultTree.h"		// ReplaceRow - Return on a hit row

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
};

CREATE_PMINTERFACE(KFCResultTreeEH, kKFCResultTreeEHImpl)

bool16 KFCResultTreeEH::HandleUpDownKey(IEvent* e, const VirtualKey& key)
{
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
	// walking on. IKeyBoard lives on the application boss.
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (keyBoard != nil && keyBoard->GetKeyFocus() != this)
		keyBoard->AcquireKeyFocus(this);
	return kTrue;
}

// RETURN REPLACES THE SELECTED ROW (2026-10-06, docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F17 - the
// author: walk the rows with the arrows and replace with Return where the match is right, the keyboard alone). Return or
// the keypad's Enter, no modifier, one HIT row selected: that row is replaced through the right-click Replace's own door
// (KFCResultTree::ReplaceRow - nothing happens on a row that cannot be replaced). Any other key, a modified Return, or
// Return on a story / document / book row goes to the stock handler. Only while the TREE holds the keyboard - typing
// in a document never comes here.
bool16 KFCResultTreeEH::KeyDown(IEvent* e)
{
	const VirtualKey key = e->GetVirtualKey();
	if ((!(key == kVirtualReturnKey) && !(key == kVirtualEnterKey)) || e->ShiftKeyDown() || e->CmdKeyDown() || e->OptionAltKeyDown())
		return TreeViewEventHandler::KeyDown(e);
	InterfacePtr<ITreeViewController> controller(this, UseDefaultIID());
	if (controller == nil)
		return TreeViewEventHandler::KeyDown(e);
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.size() != 1)
		return TreeViewEventHandler::KeyDown(e);
	TreeNodePtr<KFCResultNodeID> node(selected[0]);
	if (node == nil || node->IsRoot() || !node->IsHitRow())
		return TreeViewEventHandler::KeyDown(e);
	// A previous landing is still opening a document - see gWalking. The key is taken, and nothing is written from a
	// half-made selection.
	if (gWalking)
		return kTrue;
	(void)KFCResultTree::ReplaceRow(node->GetChapter(), node->GetHit());
	// The write may have opened a closed chapter and given it a window, which takes the key focus - take it back, or the
	// next arrow press lands in the document instead of walking on (as HandleUpDownKey does).
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (keyBoard != nil && keyBoard->GetKeyFocus() != this)
		keyBoard->AcquireKeyFocus(this);
	return kTrue;
}

// End, KFCResultTreeEH.cpp.
