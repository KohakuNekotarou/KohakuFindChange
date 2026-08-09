//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Event handler for the result list ITSELF (the tree-view boss, not a row). It adds two things
//  to the stock up / down arrows and leaves everything else alone:
//
//    1. A row that is CLOSED opens when the arrows land on it. The stock keys walk the VISIBLE
//       rows only, and a book search deliberately comes up with every chapter closed
//       (KBSResultTree::Rebuild), so without this the arrows would tour the chapter headings and
//       never once step inside a chapter. Opening on arrival means holding the down arrow tours
//       the whole book: land on a chapter, it opens, the next press is its first hit.
//    2. The landing runs the row's action - KBSJump::ActivateNode, exactly what a click on that
//       row would do: a hit row jumps, a chapter row shows its document, the book row activates
//       its book.
//
//  WHERE THE ROW ARITHMETIC WENT (2026-08-01)
//
//  An earlier version worked out the next / previous row itself (a full tree-order walk over the
//  model). It was replaced by "let the stock handler move, then open what it landed on", which
//  behaves the same and cannot go wrong the same way: the stock handler only ever selects rows the
//  tree actually has, while the hand-rolled walk counted chapters with GetChapterCount() where the
//  tree is built from GetDisplayChapterCount() - so a result set over the display cap sent it after
//  a node that does not exist.
//
//  TreeViewEventHandler is the stock base (source/open/includes/widgets; on the CPP.rsp path) and
//  HandleUpDownKey is virtual precisely for this. Home / End / PageUp / PageDown and the left /
//  right expand / collapse keys stay stock.
//
//  NOTE: THIS CLASS ONLY EXISTS IF SOMETHING ASKS FOR IT. Interface implementations are created on
//  first QueryInterface, so naming it in KBS.fr is not enough - KBSResultNodeEH's key-focus
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
#include "keyboarddefs.h"			// kVirtualUpArrowKey / kVirtualDownArrowKey
#include "TreeViewEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)

// Project includes:
#include "KBSID.h"
#include "KBSResultNodeID.h"
#include "KBSJump.h"

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

/** Up / down arrows that open what they land on, then run that row's action (see the top). */
class KBSResultTreeEH : public TreeViewEventHandler
{
public:
	KBSResultTreeEH(IPMUnknown* boss) : TreeViewEventHandler(boss) {}
	virtual ~KBSResultTreeEH() {}

	virtual bool16 HandleUpDownKey(IEvent* e, const VirtualKey& key);
};

CREATE_PMINTERFACE(KBSResultTreeEH, kKBSResultTreeEHImpl)

bool16 KBSResultTreeEH::HandleUpDownKey(IEvent* e, const VirtualKey& key)
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

	TreeNodePtr<KBSResultNodeID> node(selected[0]);
	if (node == nil || node->IsRoot())
		return handled;

	// A branch row opens on arrival, so the NEXT press steps inside it rather than over it. Hit
	// rows are leaves. ExpandNode on an already-open node is a harmless no-op, so the state is not
	// worth asking about first.
	if (!node->IsHitRow())
		treeMgr->ExpandNode(selected[0], kFalse /*expandAllDescendants*/);

	// The row's action - the same one a click on it would run.
	//
	// kFalse = raise the marker at once. That is the one thing this does NOT share with the click:
	// there the marker waits to find out whether a second click is coming, and there is no such thing
	// as a double arrow-key, so waiting here would only make every step of the walk point late.
	KBSJump::ActivateNode(node->GetChapter(), node->GetHit(), /*deferMarkerUntilClickSettles*/ false);

	// That action activated a document window - or, on a book row, the Book panel - which took the
	// key focus with it. Take it back, or the NEXT arrow press lands in the document instead of
	// walking on. IKeyBoard lives on the application boss.
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (keyBoard != nil && keyBoard->GetKeyFocus() != this)
		keyBoard->AcquireKeyFocus(this);
	return kTrue;
}

// End, KBSResultTreeEH.cpp.
