//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RESULT LIST'S SELECTION (1.4.0 - docs/superpowers/specs/2026-10-09-kfc-object-search-design.md O17, the author's
//  calls of 2026-10-10: several rows selected at once, the way the Layers panel selects several rows - Shift for a run of
//  rows, Ctrl to add one or take it away, Shift+Up / Shift+Down to grow or shrink the run. Object rows first - so
//  InDesign's own Align, Group, Transform or an object style can be applied to all of their items at once - then text
//  rows as well; either kind's rows are replaced together - O18, KFCResultTree::ReplaceRows).
//
//  The list's controller is the tree's own (CTreeViewController, public/includes) made a multiple-selection one, with
//  KFC's rule on top: ROWS ARE SELECTED TOGETHER WHEN THEY ARE HIT ROWS OF ONE KIND IN ONE DOCUMENT -
//    * object rows whose items stood on ONE spread: InDesign selects page items on one spread at a time (measured
//      2026-10-10: the DOM refuses with "two objects on different spreads cannot be selected"; two items on one spread
//      were selected together);
//    * text rows of any of the document's stories (the author's call): InDesign selects one stretch of text, so the page
//      shows the row clicked last, as a click does (FollowModifiedClick, ExtendSelection).
//  A story, document or book row is always selected alone.
//  The shape is Adobe's: the Layers panel's controller runs the stock rules and then takes rows of the other kind out
//  (open/components/layerpanel/LayersPanelTreeViewController.cpp), and the Multi-State Object panel's changes the stock
//  rules where its panel needs it (open/components/buttonui/msopanel/MSOPanelTreeViewController.cpp).
//
//  What a selection does to the page is KFCJump's: SelectObjectRows for object rows (their items selected), the click's
//  own jump (ActivateNode) for a text row. The click that asks for it is KFCResultNodeEH's (FollowModifiedClick, below),
//  the keys KFCResultTreeEH's (ExtendSelection, below; Return replaces the rows selected together).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IEvent.h"
#include "IPalettePanelUtils.h"		// QueryPanelByWidgetID - the list, reached through its panel
#include "IPanelControlData.h"
#include "ITreeNodeIDData.h"
#include "ITreeViewController.h"
#include "ITreeViewHierarchyAdapter.h"
#include "ITreeViewMgr.h"

// General includes:
#include "CTreeViewController.h"	// the stock controller (public/includes) - DV_WidgetBin
#include "CTreeViewMgr.h"			// AboveOrBelow - which way a run of rows goes
#include "TreeNodeTraverser.h"		// the rows in the order they are drawn
#include "Utils.h"

#include <algorithm>
#include <vector>

// Project includes:
#include "KFCUIID.h"
#include "KFCJump.h"				// SelectObjectRows / ActivateNode - the page follows the rows
#include "KFCModelAccess.h"			// GetHitItem / GetHitSpread - what a row is and where its item stood
#include "KFCResultNodeID.h"
#include "KFCResultTree.h"

namespace
{

// Can two rows be selected together (O17)? The first answer is the only yes.
enum JoinAnswer
{
	kJoins = 0,
	kNotHitRows,		// either is a story, document or book row, or the two are of two kinds: always alone
	kOtherDocument,		// hit rows of two documents - InDesign selects in one document
	kOtherSpread,		// object rows whose items stood on two spreads
	kInOverset			// an object row whose item stood on no spread - in overset text: always alone (2026-10-10 night)
};

bool IsObjectRow(const KFCResultNodeID* node)
{
	return node != nil && node->IsHitRow() && KFCResults()->GetHitItem(node->GetChapter(), node->GetHit()) != kInvalidUID;
}

JoinAnswer Join(const NodeID& a, const NodeID& b)
{
	TreeNodePtr<KFCResultNodeID> first(a), second(b);
	if (first == nil || second == nil || !first->IsHitRow() || !second->IsHitRow())
		return kNotHitRows;
	const bool objects = IsObjectRow(first);
	if (objects != IsObjectRow(second))
		return kNotHitRows;		// (a list holds one kind - a search is on one tab; asked anyway)
	if (first->GetChapter() != second->GetChapter())
		return kOtherDocument;
	if (!objects)
		return kJoins;			// text rows: any story of the document
	const UID spreadA = KFCResults()->GetHitSpread(first->GetChapter(), first->GetHit());
	const UID spreadB = KFCResults()->GetHitSpread(second->GetChapter(), second->GetHit());
	if (spreadA == kInvalidUID || spreadB == kInvalidUID)
		return kInOverset;
	if (spreadA != spreadB)
		return kOtherSpread;
	return kJoins;
}

// What the message area says for a row that could not join the selection.
const char* WhyNotJoined(JoinAnswer answer)
{
	switch (answer)
	{
		case kOtherDocument:	return "Not added - that row is in another document. Rows are selected together in one document.";
		case kOtherSpread:		return "Not added - that object is on another spread. InDesign selects objects on one spread at a time.";
		case kInOverset:		return "Not added - an object in overset text cannot be selected: its row is selected on its own.";
		default:				return "";
	}
}

// A row named by its chapter and hit - kept as two numbers rather than a NodeID, which owns a heap object (a static
// NodeID would be destroyed at DLL unload, after the session it belongs to).
struct RowRef
{
	int32	chapter;
	int32	hit;
	RowRef() : chapter(-1), hit(-1) {}
	RowRef(int32 c, int32 h) : chapter(c), hit(h) {}
	bool IsValid() const { return chapter >= 0 && hit >= 0; }
};

// THE MOVING END OF A RUN: the row the last click or Shift+arrow left the run at - Shift+Up / Shift+Down move it, while
// the run's other end stays where the run began (the controller's master item - CTreeViewController's fFirstSelected).
RowRef gRunEnd;

// The hit row the last Shift / Ctrl click could not add (O17), and why - for the click's button-up to say
// (FollowModifiedClick). Cleared by every click.
RowRef gRefused;
JoinAnswer gRefusedWhy = kJoins;

RowRef RowOf(const NodeID& node)
{
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	return (nodeID != nil && nodeID->IsHitRow()) ? RowRef(nodeID->GetChapter(), nodeID->GetHit()) : RowRef();
}

// The result list's controller - nil while the panel is closed.
ITreeViewController* QueryListController()
{
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	IControlView* const listView = (panelData != nil) ? panelData->FindWidget(kKFCResultListWidgetID) : nil;
	return (listView != nil) ? InterfacePtr<ITreeViewController>(listView, UseDefaultIID()).forget() : nil;
}

// A RUN OF ROWS selected: anchor to end, in the order the rows are drawn, each through the controller's Select - which
// turns away a row that cannot join (KFCResultTreeController::Select): a story row between two stories' text rows is
// passed over, and a run that crosses into another spread's object rows stops taking them. The stock Shift click's own
// walk (CTreeViewController::ProcessSelectionRules).
void SelectRun(ITreeViewController* controller, const NodeID& anchor, const NodeID& end)
{
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	(void)controller->Select(anchor, kFalse, kTrue);
	if (anchor == end)
		return;
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (adapter == nil || treeMgr == nil)
		return;
	const int32 aboveOrBelow = CTreeViewMgr::AboveOrBelow(adapter, anchor, end);
	TreeNodeTraverser walk(anchor, treeMgr, kTrue /*expandedNodesOnly*/,
		aboveOrBelow < 0 ? TreeNodeTraverser::eForward : TreeNodeTraverser::eReverse);
	NodeID next;
	while (!walk.Completed() && next != end)
	{
		next = walk.Next();
		if (next.IsValid())
			(void)controller->Select(next, kFalse, kTrue);
	}
}

}	// anonymous namespace

//========================================================================================
// The controller
//========================================================================================
class KFCResultTreeController : public CTreeViewController
{
public:
	KFCResultTreeController(IPMUnknown* boss) : CTreeViewController(boss) { ForceMultipleSelection(); }
	virtual ~KFCResultTreeController() {}

	virtual void ProcessSelectionRules(IEvent* event, UID nodeWidgetUID, bool16 notifyOfChange);
	virtual SelectCode Select(const NodeID& node, bool16 notifyOfChange = kTrue, bool16 changeHilite = kTrue);

	/** The stock controller's own data, then the mode set again (ForceMultipleSelection). Called by the persistence
	    macro on this class (InterfaceFactory.h, PRIVATE_DEFINE_READWRITE), so it is the one that runs. */
	void ReadWrite(IPMStream* s, ImplementationID prop);

protected:
	virtual bool16 IgnoreIfNodeIsSelected() const;

private:
	// MULTIPLE SELECTION, SEVERAL PARENTS, GAPS ALLOWED (KFCUI.fr says the same). Set here as well because the stock
	// controller keeps its mode with the panel's saved state (CTreeViewController::ReadWrite): a panel saved by 1.3,
	// one row at a time, would otherwise come back that way whatever KFCUI.fr now says. Several parents: a text row hangs
	// off its story row (KFCResultListAdapter), and the text rows of two stories are selected together; Join keeps them
	// to one document.
	void ForceMultipleSelection()
	{
		fNumSelectableItems = eAllowMultipleSelection;
		fAllowMultipleParents = kTrue;
		fAllowDiscontiguousSelection = kTrue;
	}
};

CREATE_PERSIST_PMINTERFACE(KFCResultTreeController, kKFCResultTreeControllerImpl)

void KFCResultTreeController::ReadWrite(IPMStream* s, ImplementationID prop)
{
	CTreeViewController::ReadWrite(s, prop);
	ForceMultipleSelection();
}

// A PLAIN CLICK ON ONE OF SEVERAL SELECTED ROWS SELECTS THAT ROW ALONE. The stock rule leaves the selection as it is
// when the row clicked is already selected; with several selected, the click's jump (KFCResultNodeEH) would then act on
// one row under a list still showing several.
bool16 KFCResultTreeController::IgnoreIfNodeIsSelected() const
{
	return (fSelectedNodes.size() <= 1) ? kTrue : kFalse;
}

void KFCResultTreeController::ProcessSelectionRules(IEvent* event, UID nodeWidgetUID, bool16 notifyOfChange)
{
	// A PRESS ONLY. The stock rules act on a button going down and nothing else (CTreeViewController::ProcessSelectionRules);
	// a click asks this once, on its press (measured 2026-10-10 in a test build's trace: one call per click, kLButtonDn).
	// Anything else goes to the stock rules untouched, and KFC's records - the row refused, the run's end - with it.
	InterfacePtr<IControlView> nodeView(::GetDataBase(this), nodeWidgetUID, UseDefaultIID());
	InterfacePtr<ITreeNodeIDData> data(nodeView, UseDefaultIID());
	if (event == nil || data == nil
		|| (event->GetType() != IEvent::kLButtonDn && event->GetType() != IEvent::kRButtonDn))
	{
		CTreeViewController::ProcessSelectionRules(event, nodeWidgetUID, notifyOfChange);
		return;
	}
	gRefused = RowRef();
	gRefusedWhy = kJoins;
	const NodeID node(data->Get());
	const bool modified = (event->GetType() == IEvent::kLButtonDn) && (event->ShiftKeyDown() || event->CmdKeyDown());
	if (modified && !IsSelected(node) && !fSelectedNodes.empty())
	{
		const JoinAnswer answer = Join(fSelectedNodes[0], node);
		if (answer == kNotHitRows)
		{
			// A ROW OF ANOTHER KIND - a story, document or book row clicked, or a hit row while one of those is selected:
			// that row alone (the Layers panel's rule - rows of one kind together; the newly clicked kind stays).
			DeselectAll(kFalse, kTrue);
			(void)Select(node, notifyOfChange);
			gRunEnd = RowOf(node);
			return;
		}
		if (answer != kJoins)
		{
			// A HIT ROW OF ANOTHER DOCUMENT, OR AN OBJECT ROW OF ANOTHER SPREAD: not added. Ctrl leaves the selection as it
			// is; Shift still takes the run from where it began toward this row, cut where the rows that can join end
			// (Select turns the others away). The click's button-up says why (FollowModifiedClick).
			gRefused = RowOf(node);
			gRefusedWhy = answer;
			if (!event->ShiftKeyDown())
				return;
		}
	}
	CTreeViewController::ProcessSelectionRules(event, nodeWidgetUID, notifyOfChange);
	// The run's moving end is the row clicked - unless it was not added (ExtendSelection then goes on from the anchor).
	if (!gRefused.IsValid())
		gRunEnd = RowOf(node);
}

ITreeViewController::SelectCode KFCResultTreeController::Select(const NodeID& node, bool16 notifyOfChange,
	bool16 changeHilite)
{
	// NOT WITH THESE ROWS (O17): a row that cannot join the selection is turned away whoever asks - a click, a run of
	// rows, a key. The stock answer for "not with the selection" (eSingleParentOnly) - nothing of KFC's reads it.
	if (!IsSelected(node) && !fSelectedNodes.empty() && Join(fSelectedNodes[0], node) != kJoins)
		return eSingleParentOnly;
	return CTreeViewController::Select(node, notifyOfChange, changeHilite);
}

//========================================================================================
// What the clicks and keys ask (KFCResultTree.h)
//========================================================================================
bool KFCResultTree::GetSelectedHitRows(int32& outChapter, std::vector<int32>& outHits, bool* outObjects)
{
	outChapter = -1;
	outHits.clear();
	if (outObjects != nil)
		*outObjects = false;
	InterfacePtr<ITreeViewController> controller(QueryListController());
	if (controller == nil)
		return false;
	NodeIDList selected;
	controller->GetSelectedItemsDisplayOrder(selected);
	bool objects = false;
	for (size_t k = 0; k < selected.size(); ++k)
	{
		TreeNodePtr<KFCResultNodeID> nodeID(selected[k]);
		if (nodeID == nil || !nodeID->IsHitRow())
		{
			outHits.clear();
			return false;		// a story, document or book row selected: not a selection of hit rows
		}
		const bool object = IsObjectRow(nodeID);
		if (k == 0)
			objects = object;
		if (object != objects || (outChapter >= 0 && nodeID->GetChapter() != outChapter))
		{
			outHits.clear();
			return false;		// (never - the rules keep one kind in one document; asked anyway)
		}
		outChapter = nodeID->GetChapter();
		outHits.push_back(nodeID->GetHit());
	}
	if (outObjects != nil)
		*outObjects = objects;
	return !outHits.empty();
}

std::vector<int32> KFCResultTree::RowsOfRightClick(int32 chapterIdx, int32 hitIdx)
{
	int32 chapter = -1;
	std::vector<int32> rows;
	if (GetSelectedHitRows(chapter, rows) && rows.size() >= 2 && chapter == chapterIdx
		&& std::find(rows.begin(), rows.end(), hitIdx) != rows.end())
		return rows;		// one of several selected together: all of them
	return std::vector<int32>(1, hitIdx);
}

void KFCResultTree::FollowModifiedClick(const NodeID& clicked)
{
	TreeNodePtr<KFCResultNodeID> nodeID(clicked);
	if (nodeID == nil || !nodeID->IsHitRow())
		return;		// a story, document or book row with Shift / Ctrl: selected alone, and no jump (as before 1.4.0)
	const bool object = IsObjectRow(nodeID);
	if (gRefused.IsValid() && gRefused.chapter == nodeID->GetChapter() && gRefused.hit == nodeID->GetHit())
	{
		const JoinAnswer why = gRefusedWhy;
		gRefused = RowRef();
		gRefusedWhy = kJoins;
		// Shift's run toward it may still have changed the selection: object rows' items follow what is selected now -
		// FIRST, as SelectObjectRows takes a previous row's note away (measured 2026-10-10: the reason said before it
		// never showed). A text row's page is left as it is: the row clicked was not added.
		int32 chapter = -1;
		std::vector<int32> hits;
		if (object && GetSelectedHitRows(chapter, hits))
			KFCJump::SelectObjectRows(chapter, hits, -1, false /*sayCount*/);
		PMString note(WhyNotJoined(why));
		note.SetTranslatable(kFalse);
		ShowRowNote(note);
		return;
	}
	int32 chapter = -1;
	std::vector<int32> hits;
	const bool any = GetSelectedHitRows(chapter, hits);
	const bool added = any && std::find(hits.begin(), hits.end(), nodeID->GetHit()) != hits.end();
	if (object)
	{
		// The items selected as the rows are (none when Ctrl took the last one away); the row clicked shown when it was
		// added, one taken away leaving the view where it is.
		KFCJump::SelectObjectRows(any ? chapter : nodeID->GetChapter(), hits, added ? nodeID->GetHit() : -1, true);
		return;
	}
	// A TEXT ROW ADDED: its jump, as a click makes it - the match selected with the Type tool on (or marked, and why it
	// could not be selected); InDesign selects one stretch of text, so the page shows the row clicked last. A text row
	// taken away leaves the page as it is.
	if (added)
		KFCJump::ActivateNode(nodeID->GetChapter(), nodeID->GetHit());
}

bool KFCResultTree::ExtendSelection(ITreeViewController* controller, bool down)
{
	if (controller == nil)
		return false;
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.empty())
		return false;
	// Where the run began - the stock controller's master item (the first row selected into an empty selection).
	NodeID anchor(controller->GetMasterItem());
	if (!anchor.IsValid() || !controller->IsSelected(anchor))
		anchor = selected[0];
	TreeNodePtr<KFCResultNodeID> anchorID(anchor);
	if (anchorID == nil || !anchorID->IsHitRow())
		return false;		// Shift+arrow on a story, document or book row: not a run of hit rows
	// The moving end: where the last click or Shift+arrow left it, while it is still selected - else the anchor.
	NodeID end(anchor);
	if (gRunEnd.IsValid() && gRunEnd.chapter == anchorID->GetChapter())
	{
		const NodeID remembered(KFCResultNodeID::Create(gRunEnd.chapter, gRunEnd.hit));
		if (controller->IsSelected(remembered))
			end = remembered;
	}
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (treeMgr == nil)
		return false;
	// The next row the end can move to - a story row of the same document passed over, so a run of text rows goes on
	// into the next story's rows (an object row hangs off its document row: there is no story row among them).
	TreeNodeTraverser walk(end, treeMgr, kTrue /*expandedNodesOnly*/,
		down ? TreeNodeTraverser::eForward : TreeNodeTraverser::eReverse);
	NodeID next;
	while (!walk.Completed())
	{
		next = walk.Next();
		TreeNodePtr<KFCResultNodeID> nextID(next);
		if (nextID == nil || !nextID->IsFontRow() || nextID->GetChapter() != anchorID->GetChapter())
			break;
		next = NodeID();
	}
	if (!next.IsValid())
		return true;		// the list's end: nothing to add
	const JoinAnswer answer = Join(anchor, next);
	if (answer != kJoins)
	{
		// The next row cannot join: the run stays. Said only for a hit row of another document or an object row of
		// another spread or in overset text - a document row is the plain edge of the run.
		if (answer != kNotHitRows)
		{
			PMString note(WhyNotJoined(answer));
			note.SetTranslatable(kFalse);
			ShowRowNote(note);
		}
		return true;
	}
	const bool adds = !controller->IsSelected(next);
	SelectRun(controller, anchor, next);
	gRunEnd = RowOf(next);
	treeMgr->ScrollToNode(next, ITreeViewMgr::eScrollIntoView);
	TreeNodePtr<KFCResultNodeID> nextID(next);
	if (nextID == nil)
		return true;
	if (!IsObjectRow(nextID))
	{
		// TEXT ROWS: the page shows the run's moving end, grown or shrunk - its jump, as a click makes it.
		KFCJump::ActivateNode(nextID->GetChapter(), nextID->GetHit());
		return true;
	}
	int32 chapter = -1;
	std::vector<int32> hits;
	if (GetSelectedHitRows(chapter, hits))
		KFCJump::SelectObjectRows(chapter, hits, adds ? nextID->GetHit() : -1, true);
	return true;
}

// End, KFCResultTreeController.cpp.
