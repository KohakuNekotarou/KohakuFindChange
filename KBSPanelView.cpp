//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  The panel's IControlView (2026-08-04): stock palette behaviour, plus a FLOOR under how small
//  the user can drag the panel, and (2026-08-09) a HEIGHT THAT LANDS ON A WHOLE NUMBER OF ROWS.
//
//  Why the floor exists. Every widget on this panel is bound to the edges, so the panel narrows
//  happily past the point where it says anything: at about half its width the message wraps to five
//  lines in a box that holds four, and the tree's rows are ellipsized down to nothing. The message
//  box no longer shows a half-drawn line (KBS.fr sizes it to a whole number of lines), but the text
//  that no longer fits is text the user cannot read at all.
//
//  This is KESCL's KESCLReportPanelView, which has had the same floor since 2026-07-17 and is the
//  only Kohaku panel that did (KESCM has none). The one difference is where the width comes from:
//  KESCL measures a widget it moves at runtime, whereas every widget here sits where the .fr put
//  it, so a constant says it and names its parts.
//
//  Why the rounding exists. The floor stops the panel getting too small; it says nothing about
//  where it stops in between. Dragged to any height the framework likes, the tree ends on a part
//  row - a strip of clipped letters along the bottom edge that reads as a result the panel is
//  hiding. Rounding the panel's own height down to a multiple of the row height means the last row
//  drawn is always a whole one.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"				// GetFrame - this panel's and the tree's
#include "IPanelControlData.h"			// FindWidget - reaching the tree from the panel
#include "ITreeViewHierarchyAdapter.h"	// GetRootNode - the node the row height is asked about
#include "ITreeViewWidgetMgr.h"			// GetNodeWidgetHeight - how tall one row is

// General includes:
#include "PMUtils.h"					// maximum - the floor still wins after rounding
#include "PalettePanelView.h"

// Project includes:
#include "KBSID.h"				// kKBSResultListWidgetID / kKBSResultRowHeight (the fallback)
#include "KBSPanelMetrics.h"	// the floor, which moves with the message block's height

/** The panel's view: PalettePanelView with a minimum size and row-height rounding.

	@ingroup kohakubooksearch
*/
class KBSPanelView : public PalettePanelView
{
public:
	/** Constructor.
		@param boss interface ptr from boss object on which this interface is aggregated.
	*/
	KBSPanelView(IPMUnknown* boss) : PalettePanelView(boss) {}

	/** Destructor. */
	virtual ~KBSPanelView() {}

	/** Clamp a requested panel size to the minimum, then round its height down so the result tree
		ends on a whole row. The framework asks before it resizes, so answering here is what stops
		the drag rather than snapping back after it.
		@param dimensions the requested size.
		@return the size to actually use.
	*/
	virtual PMPoint ConstrainDimensions(const PMPoint& dimensions) const;

	// ***** THE TWO NUMBERS ARE NOT HERE. ***** They are KBSPanelMetrics', because the height one
	// has to move with the message block, and how tall THAT is depends on the UI language (a
	// Japanese palette font draws 18px lines where an English one draws 12px). Keeping the floor
	// and the block in one place is what stops a taller block from quietly eating the result rows
	// the floor exists to protect.
	//
	// What they mean has not changed. The WIDTH is the point past which the panel stops being able
	// to say anything: every widget is bound to the edges, so it narrows happily until the message
	// wraps past what its box holds and the tree's rows are ellipsized down to nothing. It is now
	// what the panel actually measures at the size it is usually left at (user's call, 2026-08-06;
	// it was 250 from 2026-08-04, measured by eye rather than off the running panel). The HEIGHT
	// leaves about five 19px rows under the message block - enough for a result set to look like a
	// list rather than a single row peeping out.
};

CREATE_PERSIST_PMINTERFACE(KBSPanelView, kKBSPanelViewImpl)

/* ConstrainDimensions
*/
PMPoint KBSPanelView::ConstrainDimensions(const PMPoint& desiredDimen) const
{
	PMPoint constrainedDim = desiredDimen;

	const PMReal minWidth = KBSPanelMetrics::MinimumPanelWidth();
	const PMReal minHeight = KBSPanelMetrics::MinimumPanelHeight();

	if (constrainedDim.X() < minWidth)
		constrainedDim.X(minWidth);

	if (constrainedDim.Y() < minHeight)
		constrainedDim.Y(minHeight);

	// ***** Round the height down to a whole number of result rows. *****
	//
	// The shape is ConditionalTextUIPanelView::ConstrainDimensions (:61-99): work out how much of
	// the panel is NOT the list, ask the tree how tall one row is, floor the rest to a multiple of
	// it, and never go under the floor. There are four product implementations of this and they do
	// not agree, so the choice is worth recording:
	//
	//   LayerPanelView.cpp:57-88            rounds itself, row height from a CONSTANT
	//   MSOPanelView.cpp:68-104             rounds itself, row height from a constant per detail level
	//   ConditionalTextUIPanelView.cpp:61-99  rounds itself, row height ASKED OF THE TREE   <- this one
	//   TimingPanelView.cpp:53-71           DELEGATES the rounding to the tree widget
	//
	// Not the delegating form, for a reason particular to this panel: its rows are 19px, and the
	// stock tree's own idea of a row is kCC2016PanelTreeNodeHeight = 22. Delegating puts the
	// rounding inside DVTreeWidgetControlView::ConstrainDimensions, which is declared in
	// open/includes/widgets/DVTreeWidgetControlView.h:59 but IMPLEMENTED IN DV_WidgetBin.lib - a
	// binary whose source is not in the SDK. Whether it would honour 19 rather than 22 cannot be
	// read anywhere, and a wrong answer here shows up as exactly the clipped row this is meant to
	// remove. Asking GetNodeWidgetHeight keeps the row height coming from the one place that
	// already owns it (KBSResultListWidgetMgr), which is also what KBS does everywhere else.
	InterfacePtr<const IPanelControlData> panelData(this, IID_IPANELCONTROLDATA);
	if (panelData == nil)
		return constrainedDim;

	IControlView* treeView = panelData->FindWidget(kKBSResultListWidgetID);
	if (treeView == nil)
		return constrainedDim;

	// How much of the panel is NOT the list.
	//
	// ! The product implementations ADD UP the fixed parts by name (control strip + indicators +
	//   sets area). This one SUBTRACTS instead - the panel's current height less the tree's - and
	//   the difference is deliberate. The fixed part of this panel is the message block, and its
	//   height DEPENDS ON THE UI LANGUAGE (48px Roman, 54px Japanese; KBSPanelMetrics decides and
	//   moves the tree to match). Adding it up here would state that fact a second time, in a
	//   second place, in a way that has to be kept in step by hand. The tree is the only widget
	//   that stretches, so what is left when it is taken away IS the fixed part, whatever the
	//   language made it.
	const PMReal nonListHeight = this->GetFrame().Height() - treeView->GetFrame().Height();
	if (nonListHeight <= 0)
		return constrainedDim;	// no room measured yet (or no tree); leave the size alone

	// How tall one row is. The tree is asked rather than told, so this cannot drift from what the
	// rows are actually drawn at. kKBSResultRowHeight is only the answer for the moment before the
	// tree can give one - the same fallback shape conditionaltextui uses with its own constant.
	PMReal rowHeight = kKBSResultRowHeight;
	InterfacePtr<ITreeViewWidgetMgr> treeViewMgr(treeView, IID_ITREEVIEWWIDGETMGR);
	InterfacePtr<ITreeViewHierarchyAdapter> hierAdapter(treeView, IID_ITREEVIEWHIERARCHYADAPTER);
	if (treeViewMgr != nil && hierAdapter != nil)
	{
		NodeID rootNode = hierAdapter->GetRootNode();
		if (rootNode != kInvalidNodeID)
			rowHeight = treeViewMgr->GetNodeWidgetHeight(rootNode);
	}
	if (rowHeight <= 0)
		return constrainedDim;

	PMReal listHeight = constrainedDim.Y() - nonListHeight;
	listHeight = ::Floor(listHeight / rowHeight) * rowHeight;

	// The floor still wins: flooring can only take height away, and the panel is not allowed to
	// end up under the minimum that keeps the message readable.
	constrainedDim.Y(::maximum(listHeight + nonListHeight, minHeight));

	return constrainedDim;
}

// End, KBSPanelView.cpp.
