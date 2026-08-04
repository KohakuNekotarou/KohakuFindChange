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

private:
	// ***** WHERE THESE TWO NUMBERS COME FROM. *****
	//
	// WIDTH. The message block is laid out in KBS.fr as: 5px margin, the message box, 4px clear,
	// the 32px illustration, 8px margin. 250 leaves the message about 200px - three lines for the
	// opening hint and four for the longest status line, which is what its box holds. It is also
	// roughly the width the panel is usually left at (user's call, 2026-08-04: "about the size the
	// panel is now").
	//
	// HEIGHT. 160 is KESCL's, and it works out the same way here: the message block ends at 54 and
	// the tree starts at 57, so this leaves a little over five 19px rows - enough for a result set
	// to look like a list rather than a single row peeping out.
	//
	// Neither is a hard rule of the layout; they are the point past which the panel stops being
	// able to say anything. If the message block or the row height changes, these move with them.
	static const int kKBSMinimumPanelWidth  = 250;
	static const int kKBSMinimumPanelHeight = 160;
};

CREATE_PERSIST_PMINTERFACE(KBSPanelView, kKBSPanelViewImpl)

/* ConstrainDimensions
*/
PMPoint KBSPanelView::ConstrainDimensions(const PMPoint& desiredDimen) const
{
	PMPoint constrainedDim = desiredDimen;

	if (constrainedDim.X() < kKBSMinimumPanelWidth)
		constrainedDim.X(kKBSMinimumPanelWidth);

	if (constrainedDim.Y() < kKBSMinimumPanelHeight)
		constrainedDim.Y(kKBSMinimumPanelHeight);

	return constrainedDim;
}

// End, KBSPanelView.cpp.
