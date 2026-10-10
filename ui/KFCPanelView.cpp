//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KFC)
//
//  The panel's IControlView: stock palette behaviour, plus a FLOOR under how small the user can
//  drag the panel, a HEIGHT THAT LANDS ON A WHOLE NUMBER OF ROWS, and THE KEYBOARD FRAME round the
//  result list while it holds the keyboard (DV_Draw, at the foot of the file).
//
//  Why the floor exists. Every widget on this panel is bound to the edges, so the panel narrows
//  happily past the point where it says anything: at about half its width the message wraps to five
//  lines in a box that holds four, and the tree's rows are ellipsized down to nothing. The message
//  box no longer shows a half-drawn line (KFCPanelMetrics sizes it to a whole number of the palette
//  font's lines whenever the panel is shown), but the text that no longer fits is text the user
//  cannot read at all.
//
//  KESCL measures the filter row it places at runtime, over a fixed floor; KCM's is a constant with
//  a maximum height too. Here the WIDTH is a constant, and the HEIGHT moves with the message block:
//  KFCPanelMetrics sizes that block from the palette font and re-places it, the tree and the pictures
//  every time the panel is shown, so the floor is asked of it rather than written here.
//
//  Why the rounding exists. The floor stops the panel getting too small; it says nothing about
//  where it stops in between. Dragged to any height the framework likes, the tree ends on a part
//  row - a strip of clipped letters along the bottom edge that reads as a result the panel is
//  hiding. Rounding the panel's own height down to a multiple of the row height means the last row
//  drawn is a whole one.
//
//  WHAT THAT DOES NOT COVER: ANY HEIGHT THE USER DID NOT DRAG TO. The framework asks
//  this only when it is about to resize, so a height that arrives another way is never rounded:
//
//    * the size the panel OPENS at (KFCUI.fr, 360). 360 - 61 of fixed part = 299, and 299 / 19 is
//      15.7 rows.
//    * the moment KFCPanelMetrics::Update moves the tree's top down for a UI whose palette font draws
//      a taller line than the .fr's 12px (a Japanese one: 72 - 48 = 24px): the tree loses that out
//      of a height rounded against the .fr's block, so a panel that WAS whole stops being so
//      (275 / 19 = 14.5 at the opening size).
//
//  One drag puts it right, and that is where this is left. The fix would be to resize the panel
//  from KFCPanelMetrics::Update - but a docked panel's height belongs to the dock, and a plug-in
//  that writes it back on every show is arguing with the thing that owns it. A part row at the
//  bottom edge until the user drags is the smaller of the two costs.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"				// GetFrame - this panel's and the tree's
#include "IInterfaceColors.h"			// RealAGMColor, kInterfaceHighLight - the keyboard frame's colour
#include "IPanelControlData.h"			// FindWidget - reaching the tree from the panel
#include "ISession.h"					// GetExecutionContextSession - the theme's colours
#include "ITreeViewHierarchyAdapter.h"	// GetRootNode - the node the row height is asked about
#include "ITreeViewWidgetMgr.h"			// GetNodeWidgetHeight - how tall one row is

// General includes:
#include "DVAPublicIncludes.h"			// dvaui::drawbot - what DV_Draw is handed
#include "DVPublicUtilities.h"			// dv_utils::DVFillRect - the keyboard frame (source/open/includes/widgets)
#include "PMUtils.h"					// maximum - the floor still wins after rounding
#include "PalettePanelView.h"

// Project includes:
#include "KFCUIID.h"				// kKFCResultListWidgetID / kKFCResultRowHeight (the fallback)
#include "KFCPanelMetrics.h"	// the floor, which moves with the message block's height
#include "KFCResultTree.h"		// ListHoldsKeyboard / kKeyboardFrameWidth - the keyboard frame
#include "KFCDiag.h"			// KFC_DIAG_LOG - the KEYFRAME trace, test builds only

/** The panel's view: PalettePanelView with a minimum size and row-height rounding.

	@ingroup kohakubooksearch
*/
class KFCPanelView : public PalettePanelView
{
public:
	/** Constructor.
		@param boss interface ptr from boss object on which this interface is aggregated.
	*/
	KFCPanelView(IPMUnknown* boss) : PalettePanelView(boss) {}

	/** Destructor. */
	virtual ~KFCPanelView() {}

	/** Clamp a requested panel size to the minimum, then round its height down so the result tree
		ends on a whole row. The framework asks before it resizes, so answering here is what stops
		the drag rather than snapping back after it.
		@param dimensions the requested size.
		@return the size to actually use.
	*/
	virtual PMPoint ConstrainDimensions(const PMPoint& dimensions) const;

	/** The panel as the stock view draws it - then THE KEYBOARD FRAME: while the result list holds InDesign's
		keyboard, a frame in the theme's selection colour around the list, in the strip the panel leaves round it (1.4.0
		- the author's call of 2026-10-10, after the down arrow moved a page item the user meant to walk the list with).
		Who holds the keyboard is asked as it is drawn (KFCResultTree::ListHoldsKeyboard); the list has the strip drawn
		again when it takes the keyboard or lets it go (KFCResultTree::RedrawKeyboardFrame).
		DV_Draw, not Draw: a palette panel is drawn through Drover's drawbot, and DVErasablePanelView keeps Draw private
		("no descendent should be calling draw on an erasable panel"). The shape is the Layers panel's row view
		(open/components/layerpanel/DVLayerElementView.cpp): the stock DV_Draw first, then the drawing over it at a child's
		frame, with dv_utils.
	*/
	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const;

	// THE TWO NUMBERS ARE NOT HERE. They are KFCPanelMetrics', because the height one
	// has to move with the message block, and how tall THAT is depends on the UI language. What they
	// mean and where they came from are written there.
};

CREATE_PERSIST_PMINTERFACE(KFCPanelView, kKFCPanelViewImpl)

/* ConstrainDimensions
*/
PMPoint KFCPanelView::ConstrainDimensions(const PMPoint& desiredDimen) const
{
	PMPoint constrainedDim = desiredDimen;

	const PMReal minWidth = KFCPanelMetrics::MinimumPanelWidth();
	const PMReal minHeight = KFCPanelMetrics::MinimumPanelHeight();

	if (constrainedDim.X() < minWidth)
		constrainedDim.X(minWidth);

	if (constrainedDim.Y() < minHeight)
		constrainedDim.Y(minHeight);

	// Round the height down to a whole number of result rows.
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
	// already owns it (KFCResultListWidgetMgr), which is also what KFC does everywhere else.
	InterfacePtr<const IPanelControlData> panelData(this, IID_IPANELCONTROLDATA);
	if (panelData == nil)
		return constrainedDim;

	IControlView* treeView = panelData->FindWidget(kKFCResultListWidgetID);
	if (treeView == nil)
		return constrainedDim;

	// How much of the panel is NOT the list.
	//
	// ! The product implementations ADD UP the fixed parts by name (control strip + indicators +
	//   sets area). This one SUBTRACTS instead - the panel's current height less the tree's - and
	//   the difference is deliberate. The fixed part of this panel is the message block, and its
	//   height DEPENDS ON THE UI LANGUAGE (four of the palette font's lines - 72px on a Japanese UI;
	//   KFCPanelMetrics asks the font and moves the tree to match). Adding it up here would state that fact a second time, in a
	//   second place, in a way that has to be kept in step by hand. The tree is the only widget
	//   that stretches, so what is left when it is taken away IS the fixed part, whatever the
	//   language made it.
	const PMReal nonListHeight = this->GetFrame().Height() - treeView->GetFrame().Height();
	if (nonListHeight <= 0)
		return constrainedDim;	// no room measured yet (or no tree); leave the size alone

	// How tall one row is. The tree is asked rather than told, so this cannot drift from what the
	// rows are actually drawn at. kKFCResultRowHeight is only the answer for the moment before the
	// tree can give one - the same fallback shape conditionaltextui uses with its own constant.
	PMReal rowHeight = kKFCResultRowHeight;
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

/* DV_Draw
*/
void KFCPanelView::DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const
{
	PalettePanelView::DV_Draw(drawbotP);
	InterfacePtr<const IPanelControlData> panelData(this, IID_IPANELCONTROLDATA);
	IControlView* const treeView = (panelData != nil) ? panelData->FindWidget(kKFCResultListWidgetID) : nil;
	const bool framed = treeView != nil && treeView->IsVisible() && KFCResultTree::ListHoldsKeyboard();
#ifdef KFC_DIAG
	// (Test builds only) The frame's state each time it changes - what a case reads (run.ps1's "frame" step).
	static int sLastFramed = -1;
	if ((framed ? 1 : 0) != sLastFramed)
	{
		sLastFramed = framed ? 1 : 0;
		KFC_DIAG_LOG("KEYFRAME %s focus=%s", framed ? "on" : "off", KFCResultTree::DiagKeyFocus().c_str());
	}
#endif
	if (!framed)
		return;

	// The theme's selection colour - the fill of the selected row - so the frame says "this list" in the colour the list
	// already uses for "this row", in the light UI and the dark one alike. (Not kInterfaceItemHighLight: a row's accent
	// words - "locked", "hidden" - are drawn in that.)
	RealAGMColor frameColor(0.2, 0.45, 0.9);	// a sane fallback if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
		colors->GetRealAGMColor(kInterfaceHighLight, frameColor);

	// Four strips just outside the list's edges, filled - not a stroke, whose width would straddle the edge. The list's
	// frame is in this panel's coordinates (IControlView::GetFrame - its parent's), the ones this view draws in, as the
	// Layers panel's row view draws at its children's frames.
	const PMRect list(treeView->GetFrame());
	const PMReal w(KFCResultTree::kKeyboardFrameWidth);
	dv_utils::DVFillRect(drawbotP, frameColor, PMRect(list.Left() - w, list.Top() - w, list.Right() + w, list.Top()));		// above
	dv_utils::DVFillRect(drawbotP, frameColor, PMRect(list.Left() - w, list.Bottom(), list.Right() + w, list.Bottom() + w));	// below
	dv_utils::DVFillRect(drawbotP, frameColor, PMRect(list.Left() - w, list.Top(), list.Left(), list.Bottom()));			// left
	dv_utils::DVFillRect(drawbotP, frameColor, PMRect(list.Right(), list.Top(), list.Right() + w, list.Bottom()));		// right
}

// End, KFCPanelView.cpp.
