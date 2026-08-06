//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  The panel's IControlView (2026-08-04): stock palette behaviour, plus a FLOOR under how small
//  the user can drag the panel.
//
//  Why it exists. Every widget on this panel is bound to the edges, so the panel narrows happily
//  past the point where it says anything: at about half its width the message wraps to five lines
//  in a box that holds four, and the tree's rows are ellipsized down to nothing. The message box
//  no longer shows a half-drawn line (KBS.fr sizes it to a whole number of lines), but the text
//  that no longer fits is text the user cannot read at all.
//
//  This is KESCL's KESCLReportPanelView, which has had the same floor since 2026-07-17 and is the
//  only Kohaku panel that did (KESCM has none). The one difference is where the width comes from:
//  KESCL measures a widget it moves at runtime, whereas every widget here sits where the .fr put
//  it, so a constant says it and names its parts.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "PalettePanelView.h"

// Project includes:
#include "KBSID.h"
#include "KBSPanelMetrics.h"	// the floor, which moves with the message block's height

/** The panel's view: PalettePanelView with a minimum size.

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

	/** Clamp a requested panel size to the minimum. The framework asks before it resizes, so
		refusing here is what stops the drag rather than snapping back after it.
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

	const int32 minWidth = KBSPanelMetrics::MinimumPanelWidth();
	const int32 minHeight = KBSPanelMetrics::MinimumPanelHeight();

	if (constrainedDim.X() < minWidth)
		constrainedDim.X(minWidth);

	if (constrainedDim.Y() < minHeight)
		constrainedDim.Y(minHeight);

	return constrainedDim;
}

// End, KBSPanelView.cpp.
