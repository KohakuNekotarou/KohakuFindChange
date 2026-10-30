//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Next Result / Previous Result - see KFCResultStep.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "ITreeViewController.h"
#include "ITreeViewHierarchyAdapter.h"	// the tree's own order - the rows a step walks over
#include "ITreeViewMgr.h"

#include <vector>

// Project includes:
#include "KFCResultStep.h"
#include "KFCResultNodeID.h"
#include "KFCResultTree.h"		// ResultListView, SelectAloneInView, ListHoldsKeyboard / TakeKeyboard, RefusedWhileRunning
#include "KFCResultHistory.h"	// IsStacking / HasLiveList - the live tier's rows alone
#include "KFCJump.h"			// ActivateNode - the landing; IsLanding - nothing moves under one
#include "KFCDiag.h"			// KFC_DIAG_LOG - the RESULTSTEP trace, test builds only

namespace
{

// ONE STEP AT A TIME: a landing opens a document, and opening a document runs the message loop - a key held down could
// come back here while the step before is still landing (the arrows' gWalking, KFCResultTreeEH).
bool gStepping = false;

class StepGuard
{
public:
	StepGuard() { gStepping = true; }
	~StepGuard() { gStepping = false; }
};

// THE TOP OF THE LIVE TIER: its own row while the results are kept in tiers (KFCResultHistory - the grey tiers' rows hang
// from their own), the hidden root otherwise. false = no live list to step in.
bool LiveTop(const ITreeViewHierarchyAdapter* adapter, NodeID& outTop)
{
	if (KFCResultHistory::IsStacking())
	{
		if (!KFCResultHistory::HasLiveList())
			return false;
		outTop = NodeID(KFCResultNodeID::CreateLiveTier());
		return true;
	}
	outTop = NodeID(adapter->GetRootNode());
	return outTop.IsValid();
}

// A row a step lands on: a hit row (a text or an object row) of the live list - not a grey tier's kept row.
bool IsStepRow(const NodeID& row)
{
	TreeNodePtr<KFCResultNodeID> node(row);
	return node != nil && node->IsHitRow() && !node->IsInert();
}

// EVERY ROW UNDER `top`, IN THE ORDER THE TREE DRAWS THEM - depth first, open or closed: the adapter's own order, the one an
// arrow key walks (a closed row's rows are the next ones down).
void RowsInOrder(const ITreeViewHierarchyAdapter* adapter, const NodeID& top, std::vector<NodeID>& out)
{
	const int32 count = adapter->GetNumChildren(top);
	for (int32 i = 0; i < count; ++i)
	{
		NodeID row(adapter->GetNthChild(top, i));
		if (!row.IsValid())
			continue;
		out.push_back(row);
		RowsInOrder(adapter, row, out);
	}
}

// Is there a hit row under `top` - the first one found ends the walk.
bool AnyStepRow(const ITreeViewHierarchyAdapter* adapter, const NodeID& top)
{
	const int32 count = adapter->GetNumChildren(top);
	for (int32 i = 0; i < count; ++i)
	{
		NodeID row(adapter->GetNthChild(top, i));
		if (row.IsValid() && (IsStepRow(row) || AnyStepRow(adapter, row)))
			return true;
	}
	return false;
}

}	// anonymous namespace

bool KFCResultStep::CanStep()
{
	IControlView* const view = KFCResultTree::ResultListView();
	if (view == nil)
		return false;
	InterfacePtr<const ITreeViewHierarchyAdapter> adapter(view, UseDefaultIID());
	NodeID top;
	return adapter != nil && LiveTop(adapter, top) && AnyStepRow(adapter, top);
}

bool KFCResultStep::Step(bool forward)
{
	// NOTHING MOVES UNDER SOMETHING STILL UNDER WAY - a step still landing, a click's landing, a run of ours (the arrows'
	// MoveUnderWay): each runs the message loop, and a step from inside it would move the selection under its landing.
	if (gStepping || KFCJump::IsLanding() || KFCResultTree::RefusedWhileRunning())
		return false;
	IControlView* const view = KFCResultTree::ResultListView();
	if (view == nil)
		return false;
	InterfacePtr<ITreeViewController> controller(view, UseDefaultIID());
	InterfacePtr<const ITreeViewHierarchyAdapter> adapter(view, UseDefaultIID());
	InterfacePtr<ITreeViewMgr> treeMgr(view, UseDefaultIID());
	NodeID top;
	if (controller == nil || adapter == nil || treeMgr == nil || !LiveTop(adapter, top))
		return false;
	std::vector<NodeID> rows;
	RowsInOrder(adapter, top, rows);
	const int32 count = static_cast<int32>(rows.size());

	// WHERE THE SELECTION STANDS, in the tree's order: its highest and its lowest row (a story or document row selected
	// counts as well - the step goes on from it).
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	int32 highest = -1, lowest = -1;
	for (int32 i = 0; i < count; ++i)
		for (size_t s = 0; s < selected.size(); ++s)
			if (rows[i] == selected[s])
			{
				if (highest < 0)
					highest = i;
				lowest = i;
			}

	// THE HIT ROW TO GO TO - and round to the other end past the last (or the first) one (the spec's A4).
	int32 target = -1;
	if (forward)
	{
		for (int32 i = (lowest < 0) ? 0 : lowest + 1; i < count && target < 0; ++i)
			if (IsStepRow(rows[i]))
				target = i;
		for (int32 i = 0; i < count && target < 0; ++i)
			if (IsStepRow(rows[i]))
				target = i;
	}
	else
	{
		for (int32 i = (highest < 0) ? count - 1 : highest - 1; i >= 0 && target < 0; --i)
			if (IsStepRow(rows[i]))
				target = i;
		for (int32 i = count - 1; i >= 0 && target < 0; --i)
			if (IsStepRow(rows[i]))
				target = i;
	}
	// (Test builds only) The step taken - where it began, where it went, and who held the keyboard.
	const bool listHadKeyboard = KFCResultTree::ListHoldsKeyboard();
	KFC_DIAG_LOG("RESULTSTEP forward=%d rows=%d selected=%d..%d target=%d listKeyboard=%d", forward ? 1 : 0,
		static_cast<int>(count), static_cast<int>(highest), static_cast<int>(lowest), static_cast<int>(target),
		listHadKeyboard ? 1 : 0);
	if (target < 0)
		return false;

	const StepGuard guard;
	(void)KFCResultTree::SelectAloneInView(controller, adapter, treeMgr, rows[target]);
	TreeNodePtr<KFCResultNodeID> node(rows[target]);
	if (node == nil)
		return false;
	// The row's action - the one its click or an arrow runs (KFCJump.h): its match selected, or marked when it cannot be.
	KFCJump::ActivateNode(node->GetChapter(), node->GetHit());
	// THE KEYBOARD WHERE IT WAS (the spec's A5): the landing brought a document window forward, which took the keyboard -
	// given back to the list when the list held it (as an arrow takes it back); left in the text when the text held it, so
	// the match it selected can be typed over.
	if (listHadKeyboard)
		(void)KFCResultTree::TakeKeyboard();
	return true;
}

// End, KFCResultStep.cpp.
