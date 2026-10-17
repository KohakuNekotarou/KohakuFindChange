//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RESULT LIST'S SELECTION (the author's calls: several rows selected at once, the way the Layers panel selects
//  several rows - Shift for a run of rows, Ctrl to add one or take it away, Shift+Up / Shift+Down to grow or shrink the
//  run. Object rows first - so InDesign's own Align, Group, Transform or an object style can be applied to all of their
//  items at once - then text rows as well; either kind's rows are replaced together - KFCResultTree::ReplaceRows).
//
//  The list's controller is the tree's own (CTreeViewController, public/includes) made a multiple-selection one, with
//  KFC's rule on top: ROWS ARE SELECTED TOGETHER WHEN THEY ARE HIT ROWS OF ONE KIND - text rows of any story, object rows
//  of any spread, of any document (the author's calls: NOT kept to one document, nor object rows to one spread, though
//  InDesign selects in one document and page items on one spread at a time - the rows selected together are replaced
//  together in one undo step, KFCResultTree::ReplaceRows).
//  THE PAGE SHOWS THE ROW SELECTED LAST, as a click does - its match, or its item, selected alone (FollowModifiedClick,
//  ExtendSelection): the rows selected together are the list's, not the page's.
//  A story, document or book row is always selected alone, and so is the live tier's own row (the author's call, 10-14
//  night); a grey tier's row and a kept row - the results kept in tiers, KFCResultHistory - never are (a click on one
//  switches to its tier, and the arrows stay out of them). A row closed over selected rows takes them out of the
//  selection - a selected row is a drawn row (DeselectRowsUnder, asked by the row's observer, KFCResultNodeObserver.cpp).
//  The shape is Adobe's: the Layers panel's controller runs the stock rules and then takes rows of the other kind out
//  (open/components/layerpanel/LayersPanelTreeViewController.cpp), and the Multi-State Object panel's changes the stock
//  rules where its panel needs it (open/components/buttonui/msopanel/MSOPanelTreeViewController.cpp).
//
//  What a selection does to the page is KFCJump's: the click's own jump (ActivateNode) on the row selected last. The
//  click that asks for it is KFCResultNodeEH's (FollowModifiedClick, below),
//  the keys KFCResultTreeEH's (ExtendSelection, below; Return replaces the rows selected together).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IEvent.h"
#include "ITreeNodeIDData.h"
#include "ITreeViewController.h"
#include "ITreeViewHierarchyAdapter.h"
#include "ITreeViewMgr.h"

// General includes:
#include "CTreeViewController.h"	// the stock controller (public/includes) - DV_WidgetBin
#include "CTreeViewMgr.h"			// AboveOrBelow - which way a run of rows goes
#include "TreeNodeTraverser.h"		// the rows in the order they are drawn

#include <algorithm>
#include <map>						// the rows' open state across Clear from Results' rebuild (LeftAs)
#include <vector>

// Project includes:
#include "KFCUIID.h"
#include "KFCJump.h"				// ActivateNode - the page follows the row selected last
#include "KFCModelAccess.h"			// GetHitItem - what a row is
#include "KFCResultNodeID.h"
#include "KFCResultTree.h"
#include "KFCResultHistory.h"		// ViewTopRow / HoldScrollForNextRebuild - Clear from Results keeps the view
#include "KFCDiag.h"				// the select-all-expand-once and list-landing-guard-off fault switches - test builds only

namespace
{

bool IsObjectRow(const KFCResultNodeID* node)
{
	return node != nil && node->IsHitRow() && KFCResults()->GetHitItem(node->GetChapter(), node->GetHit()) != kInvalidUID;
}

// CAN TWO ROWS BE SELECTED TOGETHER? Only as hit rows of one kind: a story, document or book row is always alone, and so
// is a row of the other kind. Any story, spread or document - the page shows the row selected last.
bool Joins(const NodeID& a, const NodeID& b)
{
	TreeNodePtr<KFCResultNodeID> first(a), second(b);
	if (first == nil || second == nil || !first->IsHitRow() || !second->IsHitRow())
		return false;
	return IsObjectRow(first) == IsObjectRow(second);		// (a list holds one kind - a search is on one tab; asked anyway)
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

// Is the row (chapter, hit) one of these rows?
bool RowsHold(const KFCRowsByChapter& rows, int32 chapter, int32 hit)
{
	for (size_t k = 0; k < rows.size(); ++k)
		if (rows[k].chapter == chapter && std::find(rows[k].hits.begin(), rows[k].hits.end(), hit) != rows[k].hits.end())
			return true;
	return false;
}

// THE MOVING END OF A RUN: the row the last click or Shift+arrow left the run at - Shift+Up / Shift+Down move it, while
// the run's other end stays where the run began (the controller's master item - CTreeViewController's fFirstSelected).
RowRef gRunEnd;

RowRef RowOf(const NodeID& node)
{
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	return (nodeID != nil && nodeID->IsHitRow()) ? RowRef(nodeID->GetChapter(), nodeID->GetHit()) : RowRef();
}

// The result list's controller - nil while the panel is closed.
ITreeViewController* QueryListController()
{
	IControlView* const listView = KFCResultTree::ResultListView();
	return (listView != nil) ? InterfacePtr<ITreeViewController>(listView, UseDefaultIID()).forget() : nil;
}

// A RUN OF ROWS selected: anchor to end, in the order the rows are drawn, each through the controller's Select - which
// turns away a row that cannot join (KFCResultTreeController::Select): a story row between two stories' text rows is
// passed over. The stock Shift click's own walk (CTreeViewController::ProcessSelectionRules), which announces every row
// it selects; this one announces the selection once, by the row that ends it - the Layers panel's shape (its rules run
// the stock ones in silence and notify once at the end - LayersPanelTreeViewController.cpp). A Select of a row already
// selected tells nobody (CTreeViewController::Select returns before it notifies).
void SelectRun(ITreeViewController* controller, const NodeID& anchor, const NodeID& end)
{
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	(void)controller->Select(anchor, anchor == end ? kTrue : kFalse, kTrue);
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
			(void)controller->Select(next, next == end ? kTrue : kFalse, kTrue);
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
	// off its story row (KFCResultListAdapter), and the rows of two stories - or of two documents - are selected
	// together; Joins keeps them to one kind.
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
	// a click asks this once, on its press (measured in a test build's trace: one call per click, kLButtonDn).
	// Anything else goes to the stock rules untouched, and KFC's record - the run's end - with it.
	InterfacePtr<IControlView> nodeView(::GetDataBase(this), nodeWidgetUID, UseDefaultIID());
	InterfacePtr<ITreeNodeIDData> data(nodeView, UseDefaultIID());
	if (event == nil || data == nil
		|| (event->GetType() != IEvent::kLButtonDn && event->GetType() != IEvent::kRButtonDn))
	{
		CTreeViewController::ProcessSelectionRules(event, nodeWidgetUID, notifyOfChange);
		return;
	}
	// NOT UNDER A LANDING OR A RUN OF OURS (KFCJump::IsLanding - a click's, its document opening; KFCRuns()->IsAnyRunning -
	// a search, a write, a row's Replace): their message loop can dispatch this press, and the landing it asks for is turned
	// away (KFCJump::ActivateNode) - the row would be selected while the page shows another, and the next Return would write
	// the one the page does not show. The press is dropped, as its landing is; the list's keys keep the same rule
	// (KFCResultTreeEH.cpp's MoveUnderWay).
	bool underWay = KFCJump::IsLanding() || KFCRuns()->IsAnyRunning();
#ifdef KFC_DIAG
	// (Test builds only) Fault switch list-landing-guard-off: nothing asked, the old way - the case that shows what this
	// stops (land-click-closed-armed).
	if (KFC_DIAG_FAULT("list-landing-guard-off"))
		underWay = false;
#endif
	if (underWay)
		return;
	const NodeID node(data->Get());
	// A PRESS ON A GREY TIER'S ROW OR A KEPT ROW leaves the selection as it is: the stock rules would take the rows selected
	// away for a row that Select then turns away (below) - its click switches to its tier (KFCResultNodeEH). The live tier's
	// own row is selected as a story row is - alone (Joins, below).
	TreeNodePtr<KFCResultNodeID> pressed(node);
	if (pressed != nil && pressed->IsInert() && !pressed->IsLiveTierRow())
		return;
	const bool modified = (event->GetType() == IEvent::kLButtonDn) && (event->ShiftKeyDown() || event->CmdKeyDown());
	if (modified && !IsSelected(node) && !fSelectedNodes.empty())
	{
		if (!Joins(fSelectedNodes[0], node))
		{
			// A ROW OF ANOTHER KIND - a story, document or book row clicked, or a hit row while one of those is selected:
			// that row alone (the Layers panel's rule - rows of one kind together; the newly clicked kind stays).
			DeselectAll(kFalse, kTrue);
			(void)Select(node, notifyOfChange);
			gRunEnd = RowOf(node);
			return;
		}
	}
	CTreeViewController::ProcessSelectionRules(event, nodeWidgetUID, notifyOfChange);
	gRunEnd = RowOf(node);		// the run's moving end is the row clicked
}

ITreeViewController::SelectCode KFCResultTreeController::Select(const NodeID& node, bool16 notifyOfChange,
	bool16 changeHilite)
{
	// A GREY TIER'S ROW OR A KEPT ROW IS NEVER SELECTED (docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md
	// section 3-5) - whoever asks: a click, a run of rows, a key. A kept row is a record (a click on it switches to its tier),
	// and the arrows stay out of a grey tier (the tier switch v2's R8). THE LIVE TIER'S OWN ROW CAN BE (the author's call,
	// 10-14 night) - alone, as a story row is: it joins no other row (Joins), and acts on nothing.
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	if (nodeID != nil && nodeID->IsInert() && !nodeID->IsLiveTierRow())
		return eNoSelectionAllowed;
	// NOT WITH THESE ROWS: a row that cannot join the selection is turned away whoever asks - a click, a run of
	// rows, a key. The stock answer for "not with the selection" (eSingleParentOnly) - nothing of KFC's reads it.
	if (!IsSelected(node) && !fSelectedNodes.empty() && !Joins(fSelectedNodes[0], node))
		return eSingleParentOnly;
	return CTreeViewController::Select(node, notifyOfChange, changeHilite);
}

//========================================================================================
// What the clicks and keys ask (KFCResultTree.h)
//========================================================================================
bool KFCResultTree::GetSelectedHitRows(KFCRowsByChapter& outRows, bool* outObjects)
{
	outRows.clear();
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
			outRows.clear();
			return false;		// a story, document or book row selected: not a selection of hit rows
		}
		const bool object = IsObjectRow(nodeID);
		if (k == 0)
			objects = object;
		if (object != objects)
		{
			outRows.clear();
			return false;		// (never - the rules keep one kind; asked anyway)
		}
		// A document's rows are drawn together, under its row: a new chapter starts a new group.
		if (outRows.empty() || outRows.back().chapter != nodeID->GetChapter())
			outRows.push_back(KFCChapterRows(nodeID->GetChapter()));
		outRows.back().hits.push_back(nodeID->GetHit());
	}
	if (outObjects != nil)
		*outObjects = objects;
	return !outRows.empty();
}

KFCRowsByChapter KFCResultTree::RowsOfRightClick(int32 chapterIdx, int32 hitIdx)
{
	KFCRowsByChapter rows;
	if (GetSelectedHitRows(rows) && (rows.size() >= 2 || rows[0].hits.size() >= 2) && RowsHold(rows, chapterIdx, hitIdx))
		return rows;		// one of several selected together: all of them
	return KFCRowsByChapter(1, KFCChapterRows(chapterIdx, hitIdx));
}

namespace
{
// Every hit row under `node`, in the order drawn - descending through the rows under it; it stops once `most` are in `out`
// (CanSelectAllRows asks for one).
void CollectHitRows(ITreeViewHierarchyAdapter* adapter, const NodeID& node, NodeIDList& out, int32 most = kMaxInt32)
{
	const int32 children = adapter->GetNumChildren(node);
	for (int32 i = 0; i < children && out.size() < static_cast<size_t>(most); ++i)
	{
		const NodeID child(adapter->GetNthChild(node, i));
		TreeNodePtr<KFCResultNodeID> childID(child);
		if (childID == nil)
			continue;
		if (childID->IsHitRow())
			out.push_back(child);
		else
			CollectHitRows(adapter, child, out, most);
	}
}

// EVERY BRANCH ROW FROM `node` DOWN OPENED, each before the rows under it - the way KIDMCP opens a tree, which found
// ITreeViewMgr::ExpandNode with expandAllDescendants leaving a book result's chapters closed when called on the tree's
// hidden root (memory paneltreeview-tree-panel). On a book row on screen, open with its chapters closed (the shape a book
// search comes up in - KFCResultTree::Rebuild), the one call does open them: measured 2026-10-13 night
// (sel-book-closed-return-armed, the fault switch select-all-expand-once - both rows drawn and written). Row by row stays:
// the same rows opened, whichever row the walk starts at. A row that is open is left as it is.
void OpenBranchRows(ITreeViewHierarchyAdapter* adapter, ITreeViewMgr* treeMgr, const NodeID& node)
{
	treeMgr->ExpandNode(node, kFalse /*expandAllDescendants*/);
	const int32 children = adapter->GetNumChildren(node);
	for (int32 i = 0; i < children; ++i)
	{
		const NodeID child(adapter->GetNthChild(node, i));
		TreeNodePtr<KFCResultNodeID> childID(child);
		if (childID != nil && !childID->IsHitRow())
			OpenBranchRows(adapter, treeMgr, child);
	}
}
}	// anonymous namespace

bool KFCResultTree::CanSelectAllRows(const NodeID& row)
{
	InterfacePtr<ITreeViewController> controller(QueryListController());
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	if (controller == nil || adapter == nil)
		return false;
	NodeIDList first;
	CollectHitRows(adapter, row, first, 1);
	return !first.empty();
}

namespace
{
// THE LIVE LIST AS THE PERSON LEFT IT, across the rebuild Clear from Results makes (KFCResultTree::ClearRows): the rows that go
// give every row after them in their story another place - a row's NodeID is its chapter, its story group and its hit, and a
// story row's is its group's place - so the tree is made anew, and this puts back what the rebuild's own rules would change.
// A document row by its chapter (its place stays); a story row, and the row at the top of the view, by the hit rows under it;
// the rows selected by their chapter and hit.
struct LeftAs
{
	bool									bookOpen;	// the book row (a book's list)
	std::map<int32, bool>					documents;	// chapter -> open
	std::map<std::pair<int32, int32>, bool>	stories;	// (chapter, a hit row under it) -> its story row open
	std::vector<RowRef>						selected;	// the hit rows selected
	NodeID									top;		// the row at the top of the view - as it is, for a row no clear moves
	int32									topChapter;	// ...a row of the live list's documents: its chapter (-1 = not one)
	int32									topHit;		// ...a hit row's hit (-1 = a story or document row)
	std::vector<int32>						topStory;	// ...the hit rows of the story it is of, or is (empty = none)
	LeftAs() : bookOpen(true), topChapter(-1), topHit(-1) {}
};

void NoteLeftAs(ITreeViewController* controller, ITreeViewMgr* treeMgr, LeftAs& out)
{
	if (KFCResults()->IsFromBook())
		out.bookOpen = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateBook()) != kFalse);
	const int32 chapters = KFCResults()->GetDisplayChapterCount();
	for (int32 n = 0; n < chapters; ++n)
	{
		const int32 c = KFCResults()->GetShownChapter(n);
		out.documents[c] = (treeMgr->IsNodeExpanded(KFCResultNodeID::Create(c)) != kFalse);
		const int32 groups = KFCResults()->GetDisplayFontCount(c);
		for (int32 g = 0; g < groups; ++g)
		{
			const bool open = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateFont(c, g)) != kFalse);
			const int32 rows = KFCResults()->GetDisplayFontHitCount(c, g);
			for (int32 k = 0; k < rows; ++k)
				out.stories[std::make_pair(c, KFCResults()->GetFontGroupHit(c, g, k))] = open;
		}
	}
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	for (size_t k = 0; k < selected.size(); ++k)
	{
		const RowRef row(RowOf(selected[k]));
		if (row.chapter >= 0)
			out.selected.push_back(row);
	}
	out.top = KFCResultHistory::ViewTopRow();
	TreeNodePtr<KFCResultNodeID> topID(out.top);
	if (topID == nil || topID->IsKeptRow() || topID->IsTierRow() || topID->GetChapter() < 0)
		return;		// a tier's own row, a grey tier's row, the book row: no clear moves them
	out.topChapter = topID->GetChapter();
	int32 group = topID->GetFont();
	if (topID->IsHitRow())
	{
		out.topHit = topID->GetHit();
		group = KFCResults()->GetHitFontGroup(out.topChapter, out.topHit);
	}
	const int32 rows = (group >= 0) ? KFCResults()->GetDisplayFontHitCount(out.topChapter, group) : 0;
	for (int32 k = 0; k < rows; ++k)
		out.topStory.push_back(KFCResults()->GetFontGroupHit(out.topChapter, group, k));
}

// A hit row the list still shows: its place under its story row, or among its document's object rows, is one (-1 for a row
// taken off - KFCResultModel::BuildFontGroups).
bool ShowsHit(int32 chapterIdx, int32 hitIdx)
{
	return KFCResults()->GetHitFontGroupPos(chapterIdx, hitIdx) >= 0;
}

// The row at the top of the view again: the same row while the list shows it; a hit row taken off - its story row, while a
// row of the story stays; then its document row; then the live list's own row - the nearest row above it that stands.
NodeID TopAgain(const LeftAs& left)
{
	if (left.topChapter < 0)
		return left.top;
	const int32 c = left.topChapter;
	if (left.topHit >= 0 && ShowsHit(c, left.topHit))
		return NodeID(KFCResultNodeID::Create(c, left.topHit));
	for (size_t k = 0; k < left.topStory.size(); ++k)
	{
		const int32 group = KFCResults()->GetHitFontGroup(c, left.topStory[k]);
		if (group >= 0 && ShowsHit(c, left.topStory[k]))
			return NodeID(KFCResultNodeID::CreateFont(c, group));
	}
	if (KFCResults()->GetShownChapterPos(c) >= 0)
		return NodeID(KFCResultNodeID::Create(c));
	return KFCResultHistory::IsStacking() ? NodeID(KFCResultNodeID::CreateLiveTier()) : NodeID();
}

// ...and put back after the rebuild: every row opened or closed as noted (each story row by a row of its that stays), the
// view's top row at the top again, and the rows selected that the list still shows selected again - announced once, by the
// last; the page is not moved (the rows were selected already).
void PutBackLeftAs(ITreeViewController* controller, ITreeViewMgr* treeMgr, const LeftAs& left)
{
	if (KFCResults()->IsFromBook() && !left.bookOpen)
		treeMgr->CollapseNode(KFCResultNodeID::CreateBook(), kFalse);
	const int32 chapters = KFCResults()->GetDisplayChapterCount();
	for (int32 n = 0; n < chapters; ++n)
	{
		const int32 c = KFCResults()->GetShownChapter(n);
		const int32 groups = KFCResults()->GetDisplayFontCount(c);
		for (int32 g = 0; g < groups; ++g)
		{
			const std::map<std::pair<int32, int32>, bool>::const_iterator s =
				left.stories.find(std::make_pair(c, KFCResults()->GetFontGroupHit(c, g, 0)));
			if (s == left.stories.end())
				continue;
			const NodeID row(KFCResultNodeID::CreateFont(c, g));
			if (s->second)
				treeMgr->ExpandNode(row, kFalse);
			else
				treeMgr->CollapseNode(row, kFalse);
		}
		const std::map<int32, bool>::const_iterator d = left.documents.find(c);
		if (d == left.documents.end())
			continue;
		const NodeID row(KFCResultNodeID::Create(c));
		if (d->second)
			treeMgr->ExpandNode(row, kFalse);
		else
			treeMgr->CollapseNode(row, kFalse);
	}
	const NodeID top(TopAgain(left));
	if (top.IsValid())
		treeMgr->ScrollToNode(top, ITreeViewMgr::eScrollToTop);
	std::vector<NodeID> again;
	for (size_t k = 0; k < left.selected.size(); ++k)
		if (ShowsHit(left.selected[k].chapter, left.selected[k].hit))
			again.push_back(NodeID(KFCResultNodeID::Create(left.selected[k].chapter, left.selected[k].hit)));
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	for (size_t k = 0; k < again.size(); ++k)
		(void)controller->Select(again[k], (k + 1 == again.size()) ? kTrue : kFalse /*announced once, by the last*/, kTrue);
	// The run's moving end, while it is still a row of the list (Shift+Up / Shift+Down go on from there).
	if (gRunEnd.chapter >= 0 && !ShowsHit(gRunEnd.chapter, gRunEnd.hit))
		gRunEnd = again.empty() ? RowRef() : RowOf(again.back());
}
}	// anonymous namespace

bool KFCResultTree::ClearRows(const KFCRowsByChapter& rows)
{
	if (KFCJump::IsLanding() || RefusedWhileRunning() || KFCRowCount(rows) == 0)
		return false;
	InterfacePtr<ITreeViewController> controller(QueryListController());
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	LeftAs left;
	if (controller != nil && treeMgr != nil)
		NoteLeftAs(controller, treeMgr, left);
	const int32 cleared = KFCResults()->ClearRows(rows);
	// The tree made anew - the rows after the ones that went have other places (LeftAs) - with the view left where it is
	// (the rebuild does not bring the live list's own row to the top), and then put back as the person left it.
	if (controller != nil && treeMgr != nil)
	{
		KFCResultHistory::HoldScrollForNextRebuild();
		Rebuild();
		PutBackLeftAs(controller, treeMgr, left);
	}
	PMString status("Cleared ");
	status.AppendNumber(cleared);
	status.Append(cleared == 1 ? " row from the results." : " rows from the results.");
	status.SetTranslatable(kFalse);
	ShowStatus(status);
	return cleared > 0;
}

bool KFCResultTree::RowsUnder(const NodeID& row, KFCRowsByChapter& out)
{
	out.clear();
	InterfacePtr<ITreeViewController> controller(QueryListController());
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	if (controller == nil || adapter == nil || !row.IsValid())
		return false;
	NodeIDList rows;
	CollectHitRows(adapter, row, rows);
	for (size_t k = 0; k < rows.size(); ++k)
	{
		TreeNodePtr<KFCResultNodeID> nodeID(rows[k]);
		if (nodeID == nil)
			continue;
		// A document's rows are drawn together, under its row: a new chapter starts a new group (GetSelectedHitRows' rule).
		if (out.empty() || out.back().chapter != nodeID->GetChapter())
			out.push_back(KFCChapterRows(nodeID->GetChapter()));
		out.back().hits.push_back(nodeID->GetHit());
	}
	return !out.empty();
}

bool KFCResultTree::SelectAllRows(const NodeID& row)
{
	InterfacePtr<ITreeViewController> controller(QueryListController());
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (controller == nil || adapter == nil || treeMgr == nil)
		return false;
	NodeIDList rows;
	CollectHitRows(adapter, row, rows);
	if (rows.empty())
		return false;
	// Drawn first, every branch row under it opened (OpenBranchRows): the list's runs and keys walk what is drawn - and
	// Return and the right-click Replace ask the selection in the order drawn (GetSelectedItemsDisplayOrder walks the open
	// rows alone - CTreeViewController.cpp), so a row left under a closed one would be selected and never written.
	bool expandOnce = false;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch select-all-expand-once: one ExpandNode with all its descendants, the way before -
	// the case that shows what the opening adds (sel-book-closed-return-armed).
	expandOnce = KFC_DIAG_FAULT("select-all-expand-once");
#endif
	if (expandOnce)
		treeMgr->ExpandNode(row, kTrue /*expandAllDescendants*/);
	else
		OpenBranchRows(adapter, treeMgr, row);
	// The rows selected, the first of them the run's anchor (the stock controller's master item: the first row selected
	// into an empty selection) and the last its moving end - Shift+Up / Shift+Down go on from there.
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	for (size_t k = 0; k < rows.size(); ++k)
		(void)controller->Select(rows[k], (k + 1 == rows.size()) ? kTrue : kFalse /*announced once, by the last*/, kTrue);
	gRunEnd = RowOf(rows.back());
	treeMgr->ScrollToNode(rows.front(), ITreeViewMgr::eScrollIntoView);
	// The keyboard to the list, as a row's click hands it (KFCResultNodeEH::LButtonUp): a right-click's menu leaves it
	// where it was - in the document's text after a jump - and the Return these rows were selected for would type there.
	(void)TakeKeyboard();
	PMString status("Selected ");
	status.AppendNumber(static_cast<int32>(rows.size()));
	status.Append(rows.size() == 1 ? " row. Return replaces it." : " rows. Return replaces them all.");
	status.SetTranslatable(kFalse);
	ShowStatus(status);
	return true;
}

bool KFCResultTree::DeselectRowsUnder(const NodeID& closedRow)
{
	InterfacePtr<ITreeViewController> controller(QueryListController());
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(controller, UseDefaultIID());
	if (controller == nil || adapter == nil || !closedRow.IsValid())
		return false;
	// The selected rows the closed row hides: those it is above - found through each one's rows above it, up to the hidden
	// root the list's top rows hang from.
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	NodeIDList hidden;
	for (size_t k = 0; k < selected.size(); ++k)
	{
		for (NodeID up(adapter->GetParentNode(selected[k])); up.IsValid(); up = NodeID(adapter->GetParentNode(up)))
		{
			if (up == closedRow)
			{
				hidden.push_back(selected[k]);
				break;
			}
			TreeNodePtr<KFCResultNodeID> upID(up);
			if (upID == nil || upID->IsRoot())
				break;
		}
	}
	if (hidden.empty())
		return false;
	// Out of the selection and unhilited, announced once - by the last (SelectAllRows' rule).
	for (size_t k = 0; k < hidden.size(); ++k)
		controller->Deselect(hidden[k], (k + 1 == hidden.size()) ? kTrue : kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	// The message area: how many went, and what Return writes now - "Selected 3 rows. Return replaces them all." would
	// otherwise stand over a selection of one.
	int32 left = 0;
	KFCRowsByChapter rows;
	if (GetSelectedHitRows(rows))
		for (size_t c = 0; c < rows.size(); ++c)
			left += static_cast<int32>(rows[c].hits.size());
	PMString status("Deselected ");
	status.AppendNumber(static_cast<int32>(hidden.size()));
	status.Append(hidden.size() == 1 ? " row under the closed row" : " rows under the closed row");
	if (left > 0)
	{
		status.Append(". Selected ");
		status.AppendNumber(left);
		status.Append(left == 1 ? " row. Return replaces it." : " rows. Return replaces them all.");
	}
	else
		status.Append(" - none is selected now.");
	status.SetTranslatable(kFalse);
	ShowStatus(status);
	return true;
}

bool KFCResultTree::LastSelectedHit(const KFCRowsByChapter& rows, int32& outChapter, int32& outHit)
{
	// Where the last click or Shift+arrow left the selection (the run's moving end), while it is one of these rows - else
	// the last of them in the list's order (a Ctrl+click took the row it was at away).
	if (RowsHold(rows, gRunEnd.chapter, gRunEnd.hit))
	{
		outChapter = gRunEnd.chapter;
		outHit = gRunEnd.hit;
		return true;
	}
	for (size_t k = rows.size(); k-- > 0; )
		if (!rows[k].hits.empty())
		{
			outChapter = rows[k].chapter;
			outHit = rows[k].hits.back();
			return true;
		}
	outChapter = -1;
	outHit = -1;
	return false;
}

void KFCResultTree::FollowModifiedClick(const NodeID& clicked)
{
	TreeNodePtr<KFCResultNodeID> nodeID(clicked);
	if (nodeID == nil || !nodeID->IsHitRow())
		return;		// a story, document or book row with Shift / Ctrl: selected alone, and no jump
	// A ROW ADDED: its jump, as a click makes it - a text row's match selected with the Type tool on, an object row's item
	// selected (or marked, and why it could not be selected), its document in front: THE PAGE SHOWS THE ROW SELECTED LAST
	// (the author's call - object rows as text rows, rows of any document). A row taken away leaves the page as it is.
	KFCRowsByChapter rows;
	if (GetSelectedHitRows(rows) && RowsHold(rows, nodeID->GetChapter(), nodeID->GetHit()))
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
	if (gRunEnd.IsValid())
	{
		const NodeID remembered(KFCResultNodeID::Create(gRunEnd.chapter, gRunEnd.hit));
		if (controller->IsSelected(remembered))
			end = remembered;
	}
	InterfacePtr<ITreeViewMgr> treeMgr(controller, UseDefaultIID());
	if (treeMgr == nil)
		return false;
	// The next HIT row the end can move to - story, document and book rows passed over, so a run goes on into the next
	// story's rows and the next document's (the author's call: rows of several documents together).
	// The rows drawn are the ones walked: a collapsed document's are not.
	TreeNodeTraverser walk(end, treeMgr, kTrue /*expandedNodesOnly*/,
		down ? TreeNodeTraverser::eForward : TreeNodeTraverser::eReverse);
	NodeID next;
	while (!walk.Completed())
	{
		next = walk.Next();
		TreeNodePtr<KFCResultNodeID> nextID(next);
		if (nextID != nil && nextID->IsHitRow())
			break;
		next = NodeID();
	}
	if (!next.IsValid())
		return true;		// the list's end: nothing to add
	if (!Joins(anchor, next))
		return true;		// (never - a list holds one kind; asked anyway)
	SelectRun(controller, anchor, next);
	gRunEnd = RowOf(next);
	treeMgr->ScrollToNode(next, ITreeViewMgr::eScrollIntoView);
	// The page shows the run's moving end, grown or shrunk - its jump, as a click makes it (text and object rows alike).
	TreeNodePtr<KFCResultNodeID> nextID(next);
	if (nextID != nil)
		KFCJump::ActivateNode(nextID->GetChapter(), nextID->GetHit());
	return true;
}

// End, KFCResultTreeController.cpp.
