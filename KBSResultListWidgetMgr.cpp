//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ITreeViewWidgetMgr for the result tree. Two node kinds:
//
//    * CHAPTER rows (from kKBSResultChapterNodeWidgetRsrcID): the chapter's name and its hit
//      count, "<name>  (N)", in a stock InfoStaticText after the expander arrow's zone. The
//      expander is hidden on a chapter with no hits (never happens - only chapters with hits are
//      in the model - but the guard mirrors KESCL's).
//    * HIT rows (from kKBSResultHitNodeWidgetRsrcID): one match's line, drawn by the custom
//      colour cell (KBSColorTextView) with the matched part highlighted. No expander (a leaf);
//      indented one more zone than the chapter row.
//
//  The visual indent is drawn by explicit frame offsets in ApplyNodeIDToWidget (the framework
//  indent is unused, as in KESCL). This file also hosts KBSResultTree::Rebuild (the tree lives
//  here). Ported from KESCL's KESCLResultListWidgetMgr, simplified to two levels - itself
//  modelled on the paneltreeview sample's PnlTrvTVWidgetMgr.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPalettePanelUtils.h"			// QueryPanelByWidgetID (the rebuild reaches the tree)
#include "IPanelControlData.h"
#include "ITextControlData.h"
#include "ITriStateControlData.h"		// the hit row's check box state
#include "ITreeViewHierarchyAdapter.h"	// child count - decides the expander's visibility
#include "ITreeViewMgr.h"				// ClearTree / ChangeRoot / ExpandNode (the rebuild)

// General includes:
#include "CTreeViewWidgetMgr.h"
#include "CreateObject.h"
#include "CoreResTypes.h"
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"
#include "Utils.h"
#include "widgetid.h"		// kTreeNodeExpanderWidgetID

// Project includes:
#include "KBSID.h"
#include "KBSResultNodeID.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"
#include "KBSColorTextView.h"	// IKBSRowData (the hit cell)

namespace
{
	// Visual layout of a row (px inside the 19px row). The chapter row's cells start after the
	// expander's zone; the hit row's cell steps one more zone right (drawn by us - the framework
	// indent is off, as in KESCL).
	const PMReal kRowInset = 2.0;
	const PMReal kExpanderZone = 16.0;
	// The hit row's check box occupies this much at the start of the row's content, and the
	// colour cell starts after it.
	const PMReal kCheckZone = 16.0;

	// Put 'text' into a row's static-text cell. Row widgets are recycled as the tree scrolls, so
	// every cell is written on every apply. No manual repaint: the tree draws the row right after.
	void SetColumnText(const InterfacePtr<IPanelControlData>& rowData, const WidgetID& wid, const PMString& text)
	{
		if (rowData == nil)
			return;
		IControlView* cv = rowData->FindWidget(wid);
		if (cv == nil)
			return;
		InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
		if (tcd != nil)
			tcd->SetString(text, kTrue /*invalidate*/, kFalse /*don't notify*/);
	}
}

/** Builds and fills the result tree's row widgets (chapter rows and hit rows). */
class KBSResultListWidgetMgr : public CTreeViewWidgetMgr
{
public:
	// kHierarchical: the framework tracks expansion and asks the adapter for the child level.
	KBSResultListWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kHierarchical) {}
	virtual ~KBSResultListWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& node) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		RsrcID rsrcID = kKBSResultChapterNodeWidgetRsrcID;
		if (nodeID != nil && nodeID->IsHitRow())
			rsrcID = kKBSResultHitNodeWidgetRsrcID;
		IControlView* retval = (IControlView*)::CreateObject(
			::GetDataBase(this),
			RsrcSpec(LocaleSetting::GetLocale(), kKBSPluginID, kViewRsrcType, rsrcID),
			IID_ICONTROLVIEW);
		ASSERT(retval);
		return retval;
	}

	virtual WidgetID GetWidgetTypeForNode(const NodeID& node) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID != nil && nodeID->IsHitRow())
			return kKBSResultHitNodeWidgetID;
		return kKBSResultChapterNodeWidgetID;
	}

	virtual bool16 ApplyNodeIDToWidget(const NodeID& node, IControlView* widget, int32 message = 0) const
	{
		CTreeViewWidgetMgr::ApplyNodeIDToWidget(node, widget);

		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID == nil)
			return kTrue;

		InterfacePtr<IPanelControlData> rowData(widget, UseDefaultIID());
		if (rowData == nil)
			return kTrue;

		if (nodeID->IsHitRow())
			this->ApplyHitRow(nodeID, widget, rowData);
		else
			this->ApplyChapterRow(nodeID, node, widget, rowData);
		return kTrue;
	}

	virtual PMReal GetIndentForNode(const NodeID& node) const
	{
		// PER-LEVEL indent (the base sums these up the ancestor chain). Unused in practice (the
		// framework indent is off, as in KESCL); the Apply*Row methods draw the hierarchy with
		// explicit offsets. Kept consistent in case a framework path ever consults it.
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID != nil && nodeID->IsHitRow())
			return PMReal(kExpanderZone);
		return 0.0;
	}

private:
	// A chapter row: its expander (shown when it has hits) and "<name>  (N)" after the zone.
	void ApplyChapterRow(const TreeNodePtr<KBSResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, const InterfacePtr<IPanelControlData>& rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KBSResultModel::GetChapterDisplay(nodeID->GetChapter(), name, fullCount))
			return;
		const int32 shownCount = KBSResultModel::GetDisplayHitCount(nodeID->GetChapter());

		// The expander arrow: visible exactly when the row has children. Hide-only would still
		// leave a click target (the stacked-widget lesson), so the hidden arrow is disabled too.
		IControlView* expander = rowData->FindWidget(kTreeNodeExpanderWidgetID);
		if (expander != nil)
		{
			InterfacePtr<const ITreeViewHierarchyAdapter> adapter(this, UseDefaultIID());
			const bool16 hasChildren =
				(adapter != nil && adapter->GetNumChildren(node) > 0) ? kTrue : kFalse;
			if (hasChildren)
			{
				expander->ShowView();
				expander->Enable();
			}
			else
			{
				expander->HideView();
				expander->Disable();
			}
		}

		// "<name>  (N)", or "<name>  (shown / total)" for the one boundary chapter the display cap
		// splits. The panel shows the first kKBSDisplayHitLimit hits book-wide; the rest stay in the
		// model for a future export, so the boundary chapter shows how many of its hits are on screen.
		PMString label(name);
		label.SetTranslatable(kFalse);
		label.Append("  (");
		if (shownCount < fullCount)
		{
			label.AppendNumber(shownCount);
			label.Append(" / ");
			label.AppendNumber(fullCount);
		}
		else
		{
			label.AppendNumber(fullCount);
		}
		label.Append(")");
		SetColumnText(rowData, kKBSResultChapterLabelWidgetID, label);
	}

	// A hit row: the match's line into the custom colour cell (three segments), no expander,
	// indented one zone deeper than the chapter row.
	void ApplyHitRow(const TreeNodePtr<KBSResultNodeID>& nodeID, IControlView* widget,
		const InterfacePtr<IPanelControlData>& rowData) const
	{
		PMString locator, pre, match, post;
		if (!KBSResultModel::GetHitDisplay(nodeID->GetChapter(), nodeID->GetHit(), locator, pre, match, post))
			return;
		bool checked = false, replaced = false;
		KBSResultModel::GetHitFlags(nodeID->GetChapter(), nodeID->GetHit(), checked, replaced);

		// Draw our own indent: the check box sits where the hit row's content starts (one expander
		// zone right of the chapter row's text), and the colour cell follows it to the row's edge.
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;
		const PMReal xStart = kRowInset + 2 * kExpanderZone;

		// The check box. After a replace the panel lists only what CHANGED, so a replaced row has
		// nothing left to select: its box goes away completely and the text moves into that space.
		// Hiding alone would not be enough - a hidden widget still takes clicks - so it is disabled
		// as well.
		IControlView* checkView = rowData->FindWidget(kKBSResultCheckWidgetID);
		if (checkView != nil && replaced)
		{
			checkView->ShowView(kFalse);
			checkView->Disable();
		}
		else if (checkView != nil)
		{
			PMRect checkFrame = checkView->GetFrame();
			checkFrame.Left(xStart);
			checkFrame.Right(xStart + kCheckZone);
			checkView->SetFrame(checkFrame);

			// Push the model's state in WITHOUT notifying. A notify here would come straight back
			// through KBSResultCheckObserver as a phantom click and overwrite the model with
			// whatever this recycled row happened to be showing.
			InterfacePtr<ITriStateControlData> state(checkView, UseDefaultIID());
			if (state != nil)
			{
				state->SetState(checked ? ITriStateControlData::kSelected : ITriStateControlData::kUnselected,
					kTrue /*invalidate*/, kFalse /*do NOT notify*/);
			}

			// Rows are recycled as the tree scrolls, so a row that once showed a replaced hit has
			// to get its box back.
			checkView->ShowView(kTrue);
			checkView->Enable();
		}

		IControlView* cell = rowData->FindWidget(kKBSResultTextWidgetID);
		if (cell != nil)
		{
			PMRect frame = cell->GetFrame();
			// With no box in front of it, a replaced row's text starts where the box would be.
			frame.Left(replaced ? xStart : xStart + kCheckZone);
			frame.Right(rowRight);
			cell->SetFrame(frame);

			// Hand the three text segments to the colour cell; it invalidates itself as the tree
			// draws the row right after.
			InterfacePtr<IKBSRowData> data(cell, UseDefaultIID());
			if (data != nil)
				data->SetSegments(locator, pre, match, post);
			cell->Invalidate();
		}
	}
};

CREATE_PMINTERFACE(KBSResultListWidgetMgr, kKBSResultListWidgetMgrImpl)

//----------------------------------------------------------------------------------------
// KBSResultTree::Rebuild - reload the panel's tree from the model, expand every chapter
//----------------------------------------------------------------------------------------

void KBSResultTree::Rebuild()
{
	// Reach the tree widget through the panel; nil when the panel is closed (do nothing then).
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKBSPanelWidgetID));
	if (panelData == nil)
		return;
	IControlView* listView = panelData->FindWidget(kKBSResultListWidgetID);
	if (listView == nil)
		return;
	InterfacePtr<ITreeViewMgr> treeMgr(listView, UseDefaultIID());
	if (treeMgr == nil)
		return;

	// ClearTree(kTrue) forgets the old expansion state (rebuilt by the priming below);
	// ChangeRoot(kTrue) says every row widget has the same height, which they do (chapter and
	// hit rows are both 19px).
	treeMgr->ClearTree(kTrue);
	treeMgr->ChangeRoot(kTrue);

	// A BOOK's chapters come up CLOSED. A book-wide search can fill the panel with the first
	// chapter's hits, which buries the fact that other chapters matched at all; closed chapters show
	// the whole book's shape at a glance, and the arrow opens the one you want. ClearTree(kTrue)
	// above already forgot the expansion state, so leaving them alone is all it takes.
	//
	// No expand-then-collapse priming is needed to get the arrow drawn: THIS panel draws the
	// expander itself (see ApplyChapterRow - it shows the arrow whenever the hierarchy adapter
	// reports children, which does not depend on the node ever having been expanded). The
	// "expand to make the arrow appear" rule is the tree framework's own default, and this widget
	// manager overrides it.
	if (KBSResultModel::IsFromBook())
		return;

	// A single document has just the one chapter, so open it - otherwise the result is one closed
	// row and the hits take an extra click to reach.
	const int32 chapters = KBSResultModel::GetDisplayChapterCount();
	for (int32 c = 0; c < chapters; ++c)
		treeMgr->ExpandNode(KBSResultNodeID::Create(c), kFalse);
}

//----------------------------------------------------------------------------------------
// KBSResultTree::RefreshRows - repaint the rows in place, without rebuilding the tree
//----------------------------------------------------------------------------------------

void KBSResultTree::RefreshRows()
{
	// Same reach as Rebuild; nil when the panel is closed (do nothing then).
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKBSPanelWidgetID));
	if (panelData == nil)
		return;
	IControlView* listView = panelData->FindWidget(kKBSResultListWidgetID);
	if (listView == nil)
		return;
	InterfacePtr<ITreeViewMgr> treeMgr(listView, UseDefaultIID());
	if (treeMgr == nil)
		return;

	// One notification per CHAPTER with childrenChangedAlso = kTrue: the framework refreshes that
	// chapter's hit rows itself, so a 3000-row result costs a handful of calls rather than 3000.
	// Only the rows that actually have widgets (the visible ones) do any drawing; the rest pick the
	// model up when they scroll into view. The row heights do not change here, which is what
	// NodeChanged requires.
	const int32 chapters = KBSResultModel::GetDisplayChapterCount();
	for (int32 c = 0; c < chapters; ++c)
		treeMgr->NodeChanged(KBSResultNodeID::Create(c), kTrue /*childrenChangedAlso*/);
}

//----------------------------------------------------------------------------------------
// KBSResultTree::ShowStatus - write the panel's single-line status read-out
//----------------------------------------------------------------------------------------

void KBSResultTree::ShowStatus(const PMString& message)
{
	// Reach the status text through the panel; nil when the panel is closed (do nothing then) - the
	// same reach Rebuild uses, which is why this lives here rather than in the action component.
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKBSPanelWidgetID));
	if (panelData == nil)
		return;
	IControlView* textView = panelData->FindWidget(kKBSStaticTextWidgetID);
	if (textView == nil)
		return;
	InterfacePtr<ITextControlData> textData(textView, UseDefaultIID());
	if (textData == nil)
		return;
	// A single-line StaticText does not repaint on SetString alone, so invalidate + force a redraw
	// (the SDK immediate-StaticText-update rule).
	textData->SetString(message, kTrue /*invalidate*/, kFalse /*don't notify*/);
	textView->Invalidate();
	textView->ForceRedraw();
}

//----------------------------------------------------------------------------------------
// KBSResultTree::ShowCheckedStatus - how many hits are currently selected for replacement
//----------------------------------------------------------------------------------------

void KBSResultTree::ShowCheckedStatus()
{
	const int32 total = KBSResultModel::GetTotalHitCount();
	if (total == 0)
		return;		// no results: leave whatever the search left on the line

	// The count leads, so it survives the narrow status field's tail truncation.
	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.AppendNumber(KBSResultModel::GetCheckedCount());
	msg.Append(" / ");
	msg.AppendNumber(total);
	msg.Append(" checked.");
	if (total > KBSResultModel::kKBSDisplayHitLimit)
	{
		// The panel only shows the first N rows, but checking spans every stored hit - say so, so
		// "Check All" followed by a replace is never a surprise.
		msg.Append(" (");
		msg.AppendNumber(KBSResultModel::kKBSDisplayHitLimit);
		msg.Append(" shown)");
	}
	KBSResultTree::ShowStatus(msg);
}

// End, KBSResultListWidgetMgr.cpp.
