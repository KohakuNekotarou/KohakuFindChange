//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ITreeViewWidgetMgr for the result tree. Two ROW SHAPES, three node kinds:
//
//    * BRANCH rows (from kKBSResultChapterNodeWidgetRsrcID): an expander arrow and a label. The
//      BOOK row, the CHAPTER rows ("<name>  (N)") and the FONT rows ("<font>  (N)") are all this
//      one shape, at three different indents - so the font level needed no resource of its own.
//      The expander is hidden on a row with no children (never happens - only chapters with hits
//      are in the model - but the guard mirrors KESCL's).
//    * HIT rows (from kKBSResultHitNodeWidgetRsrcID): one match's line, drawn by the custom
//      colour cell (KBSColorTextView) with the matched part highlighted. No expander (a leaf);
//      indented past its branch row.
//
//  The FONT rows (2026-08-02) appear only under a chapter whose hits name fonts - a missing-glyph
//  scan. A Find/Change chapter has no groups and its hits hang off it directly, which is the tree
//  KBS has always drawn.
//
//  The visual indent is drawn by explicit frame offsets in ApplyNodeIDToWidget, applied on top of
//  the framework's own indent rather than instead of it (see GetIndentForNode), as in KESCL. This
//  file also hosts KBSResultTree::Rebuild (the tree lives here). Ported from KESCL's
//  KESCLResultListWidgetMgr, simplified to two levels - itself
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

// Interface includes (cont.):
#include "IMenuUtils.h"					// InsertAmpersandForDisplay - a file name may contain '&'

// General includes:
#include "CTreeViewWidgetMgr.h"
#include "CreateObject.h"
#include "CoreResTypes.h"
#include "DVPublicUtilities.h"			// dv_utils::SetThemeForView - the theme a new row widget draws in
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
#include "KBSPanelIcon.h"		// the illustration follows the status line
#include "KBSEditStamp.h"		// its statics are released beside this file's own

namespace
{
	// Visual layout of a row (px inside the 19px row). The chapter row's cells start after the
	// expander's zone; the hit row's cell steps one more zone right (drawn by us - the framework
	// indent is off, as in KESCL).
	const PMReal kRowInset = 2.0;
	const PMReal kExpanderZone = 16.0;
	// How much further right than its chapter row a hit row's content starts. ZERO: the check box
	// begins exactly where the chapter row's expander arrow ends, so the two line up down the left
	// edge (user's call 2026-07-28, from a screen shot - "put the check where the arrow is").
	//
	// It was a full expander zone, then half of one, and both left a gap in front of the check box
	// that bought nothing. The hierarchy is still legible without it: the chapter row's LABEL and
	// the hit row's LOCATOR are what the eye compares, and the check box's width keeps those apart.
	const PMReal kHitExtraIndent = 0.0;
	// The hit row's check box occupies this much at the start of the row's content, and the
	// colour cell starts after it.
	const PMReal kCheckZone = 16.0;
	// What that same column shrinks to when NO row of the list carries a check box: either scan
	// (missing-glyph, overset) and a replace's report alike - see everyRowLostBox in ApplyHitRow,
	// which is the one place that decides. Half a check zone, so the hit rows land 8px right of the
	// row above them: the step the book and font levels already use, which makes the whole tree one
	// even staircase (user's call 2026-08-02 - "make it a nice staircase, shifted left by the
	// check"; the report was brought into line the same day, for the same reason).
	// Half rather than all of it: giving back the full 16px would line the hit rows up with their
	// font row's LABEL, leaving no step at all between a branch and the rows under it. The full
	// zone is still kept for a Find/Change WORK LIST, where some rows have a box and some do not
	// and the locators have to stay in one column (see ApplyHitRow).
	const PMReal kScanCheckZone = 8.0;
	// A BOOK row sits above the documents when the results came from a book search, and its
	// children step right by this much. 8px, not a full expander zone: the horizontal room in this
	// panel was fought for once already (see kHitExtraIndent), and half a zone is enough to read
	// the hierarchy. A document search has no book row and no shift, so its tree is unchanged.
	const PMReal kBookLevelIndent = 8.0;
	// A FONT row sits between a document and its hits when the results name fonts, and its children
	// step right by this much. The same 8px the book level uses, and for the same reason: half an
	// expander zone is enough to read the hierarchy, and this panel's width has been fought over
	// once already (see kHitExtraIndent). A chapter with no font rows gets no shift at all, so a
	// Find/Change result is laid out exactly as it was.
	const PMReal kFontLevelIndent = 8.0;

	// Every row of this tree is this tall. THE NUMBER IS NOT HERE: it is kKBSResultRowHeight in
	// KBSID.h, which KBS.fr reads as well - the row resources' frames and the tree's scroll
	// increments are the same fact, and the panel rounds its own height to a multiple of it
	// (KBSPanelView::ConstrainDimensions). This is where the tree asks for it (GetNodeWidgetHeight),
	// and it is the fact that lets Rebuild() promise ChangeRoot a constant widget height.
	const PMReal kRowHeight = kKBSResultRowHeight;

	// Put 'text' into a row's static-text cell. Row widgets are recycled as the tree scrolls, so
	// every cell is written on every apply. No manual repaint: the tree draws the row right after.
	void SetColumnText(const InterfacePtr<IPanelControlData>& rowData, const WidgetID& wid, const PMString& text)
	{
		if (rowData == nil)
			return;
		IControlView* cv = rowData->FindWidget(wid);
		if (cv == nil)
			return;

		// The label carries a name the user chose - a document's or a book's file name - and this
		// cell converts ampersands (KBS.fr sets that flag on it, exactly as the layer panel's row
		// resource does). A lone '&' is then taken as a keyboard accelerator: "A&B.indd" would show
		// as "AB.indd" with the B underlined. Doubling each one up is what the built-in panels do
		// before handing a user-entered name to a static text - see SetupLayerWidget in
		// LayerPanelTreeViewWidgetMgr.cpp, AddWidgetsIfNeeded in LinksUIPanelTreeViewWidgetMgr.cpp,
		// and the note on IMenuUtils::InsertAmpersandForDisplay itself ("style names, master names,
		// layer names ... or other user-entered strings").
		//
		// The hit rows do NOT come through here: their cell draws itself and asks for no ampersand
		// conversion at all (KBSColorTextView), which is right - the matched text has to appear
		// exactly as it stands in the document.
		PMString display(text);
		Utils<IMenuUtils>()->InsertAmpersandForDisplay(&display);

		InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
		if (tcd != nil)
			tcd->SetString(display, kTrue /*invalidate*/, kFalse /*don't notify*/);
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

		// Built the way the layer panel builds its rows (LayerPanelTreeViewWidgetMgr.cpp), in three
		// steps rather than one CreateObject, because the ORDER is the point:
		//   1. CreateObjectNoInit - make the boss but do not build its child hierarchy yet.
		//   2. SetThemeForView(kIDPanelTheme) - tell it that it is going to live in a palette. The
		//      row is created here, long before the tree hands it to the panel's window, so nothing
		//      else is going to say which theme it draws in.
		//   3. DoPostCreate - NOW build the children, with the theme already settled.
		// Doing it in one CreateObject call leaves the children built first and themed never.
		//
		// Both row resources are declared in KBSID.h and defined in KBS.fr, so nil here would mean
		// this plug-in's own resources did not load - not a case any handling on this side could
		// improve. It is handed straight back: the tree framework asked for the widget, so it is the
		// one that decides what to do without one.
		IPMUnknown* newObject = ::CreateObjectNoInit(
			::GetDataBase(this),
			RsrcSpec(LocaleSetting::GetLocale(), kKBSPluginID, kViewRsrcType, rsrcID),
			IID_ICONTROLVIEW);
		InterfacePtr<IControlView> view(newObject, UseDefaultIID());
		if (view != nil)
		{
			dv_utils::SetThemeForView(view, dv_utils::kIDPanelTheme);
			view->DoPostCreate();
		}
		// The reference CreateObjectNoInit handed over is the one the caller gets (the InterfacePtr
		// above holds a second one and releases it here) - the layer panel's ownership exactly.
		return view;
	}

	virtual WidgetID GetWidgetTypeForNode(const NodeID& node) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID != nil && nodeID->IsHitRow())
			return kKBSResultHitNodeWidgetID;
		return kKBSResultChapterNodeWidgetID;
	}

	// The tree asks how big a row is rather than measuring one, so answer both questions the way
	// the built-in panels do (LayerPanelTreeViewWidgetMgr / LinksUIPanelTreeViewWidgetMgr each
	// implement both). Every row here is one fixed height, and every row is as wide as the tree -
	// this list has no horizontal scroll bar and no columns to add up.
	virtual PMReal GetNodeWidgetHeight(const NodeID& /*node*/) const
	{
		return kRowHeight;
	}

	virtual PMReal GetNodeWidgetWidth(const NodeID& /*node*/) const
	{
		return this->GetTreeViewWidth();
	}

	virtual bool16 ApplyNodeIDToWidget(const NodeID& node, IControlView* widget, int32 message = 0) const
	{
		// The base class FIRST, and it MUST stay first for this panel.
		//
		// It is not only the selection highlight. With the V2 option flags set it also runs
		// ApplyIndentToWidget, which REWRITES the frame.Left of this row's child widgets
		// (CTreeViewWidgetMgr.cpp:207-221 and :234-252), plus HideExpanderIfNotExpandable and
		// ApplyDataToWidget. Those all run - the framework indent is NOT switched off here, it is
		// simply overwritten: this panel positions every row's content itself, so our frames have to
		// be applied ON TOP of whatever the base class just did.
		//
		// The shipping panels (LayerPanelTreeViewWidgetMgr, LinksUIPanelTreeViewWidgetMgr) call it
		// last, and that was copied here on 2026-07-31 - it silently undid this file's indent and
		// pulled the hit rows' content back to the left margin (seen on screen the same day, then
		// reverted). Their order works because they let the framework place the row content; ours
		// does not. The paneltreeview sample - which this tree is modelled on - calls it first.
		CTreeViewWidgetMgr::ApplyNodeIDToWidget(node, widget);

		TreeNodePtr<KBSResultNodeID> nodeID(node);
		InterfacePtr<IPanelControlData> rowData(widget, UseDefaultIID());
		if (nodeID != nil && rowData != nil)
		{
			if (nodeID->IsHitRow())
				this->ApplyHitRow(nodeID, widget, rowData);
			else if (nodeID->IsBookRow())
				this->ApplyBookRow(node, widget, rowData);
			else if (nodeID->IsFontRow())
				this->ApplyFontRow(nodeID, node, widget, rowData);
			else
				this->ApplyChapterRow(nodeID, node, widget, rowData);
		}
		return kTrue;
	}

	virtual PMReal GetIndentForNode(const NodeID& node) const
	{
		// PER-LEVEL indent. The base class sums these up the ancestor chain (GetIndent) and
		// ApplyIndentToWidget moves this row's children by the total.
		//
		// That DOES run here - the kHierarchical constructor sets the V2 option flag, so the base's
		// ApplyNodeIDToWidget calls it on every row (CTreeViewWidgetMgr.cpp:212-218) - and it
		// rewrites the frame.Left of every child bound on BOTH sides, which is the hit row's colour
		// cell and the chapter row's label. What makes the framework indent invisible in this panel
		// is NOT that it is switched off: it is that the Apply*Row methods run AFTER it and set
		// every frame themselves. That is the whole reason the base call has to stay FIRST (see
		// ApplyNodeIDToWidget). These values are kept in step with what those methods draw, so the
		// two can never pull a row in different directions.
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID != nil && nodeID->IsHitRow())
			return PMReal(kHitExtraIndent);
		if (nodeID != nil && nodeID->IsFontRow())
			return PMReal(kFontLevelIndent);
		return 0.0;
	}

private:
	// How far right of the outermost level this tree's document and hit rows sit: one book-level
	// step when there is a book row above them, nothing when there is not.
	PMReal LevelShift() const
	{
		return KBSResultModel::IsFromBook() ? kBookLevelIndent : PMReal(0.0);
	}

	// How far right this chapter's HIT rows sit because of the font level: one step when the chapter
	// has font rows above them, nothing when it has not. Asked per chapter, from the same count the
	// hierarchy adapter uses to decide whether to build those rows at all, so the indent can never
	// disagree with the tree.
	PMReal FontShift(int32 chapterIdx) const
	{
		return (KBSResultModel::GetDisplayFontCount(chapterIdx) > 0) ? kFontLevelIndent : PMReal(0.0);
	}

	// The shared shape of the two BRANCH rows (book and document): an expander arrow and a label
	// after it. The frames are set here rather than left to the resource, so ONE code path decides
	// every level's indent - the same reason the hit row has always drawn its own.
	//
	// The arrow is visible exactly when the row has children. Hiding it alone would leave a click
	// target behind (the stacked-widget lesson), so a hidden arrow is disabled too.
	void LayOutBranchRow(const NodeID& node, IControlView* widget,
		const InterfacePtr<IPanelControlData>& rowData, const PMReal& shift, const PMString& label) const
	{
		const PMReal xExpander = kRowInset + shift;
		const PMReal xLabel = kRowInset + kExpanderZone + shift;
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;

		IControlView* expander = rowData->FindWidget(kTreeNodeExpanderWidgetID);
		if (expander != nil)
		{
			PMRect f = expander->GetFrame();
			f.Left(xExpander);
			f.Right(xExpander + kExpanderZone);
			expander->SetFrame(f);

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

		IControlView* cell = rowData->FindWidget(kKBSResultChapterLabelWidgetID);
		if (cell != nil)
		{
			PMRect f = cell->GetFrame();
			f.Left(xLabel);
			f.Right(rowRight);
			cell->SetFrame(f);
		}
		SetColumnText(rowData, kKBSResultChapterLabelWidgetID, label);
	}

	// The BOOK row: which book these results came from. Only ever built for a book search, where it
	// is the root's single child - so it is the panel's standing answer to which book is the target,
	// which a status line cannot be (one line, truncated, overwritten by the next message).
	void ApplyBookRow(const NodeID& node, IControlView* widget,
		const InterfacePtr<IPanelControlData>& rowData) const
	{
		// "<book>  (N/M checked)" - how many of this book's hits are ticked, out of all of them
		// (user's wording, 2026-08-05; it used to read just "(M)"). The row a Check All over the
		// book acts on is this row, so what it did is answered in the same place it was asked.
		//
		// Both numbers are UNCAPPED - every stored hit, not the rows on screen. That is what Check
		// All ticks and what a replace would rewrite.
		//
		// ***** ONLY ON A LIST THAT HAS BOXES. ***** A scan and a replace's report have none at all,
		// so "checked" is a word about nothing there and the count is 0 by definition: this row read
		// "(0/120 checked)" over a missing-glyph scan from 2026-08-05 until NoRowHasCheckBox was
		// given a home in the model. Those lists go back to the plain total, which is what this row
		// has always said when there was no work to offer.
		//
		// M counts LOCKED hits too, though Check All cannot tick them (RowHasCheckBox turns them
		// away) - so a fully checked chapter of locked-and-free hits reads short of its own total on
		// purpose. The alternative, a denominator that leaves them out, would disagree with the hit
		// count every other part of the panel reports.
		PMString label(KBSResultModel::GetBookName());
		label.SetTranslatable(kFalse);
		label.Append("  (");
		if (KBSResultModel::NoRowHasCheckBox())
		{
			label.AppendNumber(KBSResultModel::GetTotalHitCount());
			label.Append(")");
		}
		else
		{
			label.AppendNumber(KBSResultModel::GetCheckedCount());
			label.Append("/");
			label.AppendNumber(KBSResultModel::GetTotalHitCount());
			label.Append(" checked)");
		}
		// No shift: the book row IS the outermost level.
		this->LayOutBranchRow(node, widget, rowData, PMReal(0.0), label);
	}

	// A document row: its expander and "<name>  (N)" after the zone.
	void ApplyChapterRow(const TreeNodePtr<KBSResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, const InterfacePtr<IPanelControlData>& rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KBSResultModel::GetChapterDisplay(nodeID->GetChapter(), name, fullCount))
			return;

		// "<name>  (N/M checked)" - the same read-out the book row carries, for this chapter alone
		// (user's wording, 2026-08-05). A Check All over a DOCUMENT row means that chapter, so this
		// is where its answer belongs.
		//
		// Both numbers are UNCAPPED. The label used to read "(shown / total)" on the one boundary
		// chapter the display cap falls inside, which was the panel talking about ITSELF rather than
		// about the work: the hits past the cap are still stored, still ticked by Check All and
		// still rewritten by a replace. Saying how many are drawn was dropped with the matching
		// "(N shown)" on the status line.
		//
		// And, exactly as on the book row, only where there are boxes to count: a scan and a
		// replace's report fall back to the plain total. See ApplyBookRow for the whole of it.
		PMString label(name);
		label.SetTranslatable(kFalse);
		label.Append("  (");
		if (KBSResultModel::NoRowHasCheckBox())
		{
			label.AppendNumber(fullCount);
			label.Append(")");
		}
		else
		{
			label.AppendNumber(KBSResultModel::GetChapterCheckedCount(nodeID->GetChapter()));
			label.Append("/");
			label.AppendNumber(fullCount);
			label.Append(" checked)");
		}

		// A chapter a cancelled replace never reached used to say "cancelled" here (2026-08-03). Only
		// the chapter-at-a-time path could leave one: it saved as it went, so a cancel stopped the run
		// with some chapters done and the rest untouched. That path went with "save after replace" on
		// 2026-08-05, and a cancel now puts the WHOLE run back - there is no such chapter any more.

		this->LayOutBranchRow(node, widget, rowData, this->LevelShift(), label);
	}

	// A FONT row: which font had no glyph for this text, and how many rows sit under it. The same
	// shape as a document row - an expander and a label - so it shares the branch layout and the
	// chapter row's resource, one step further right.
	void ApplyFontRow(const TreeNodePtr<KBSResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, const InterfacePtr<IPanelControlData>& rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KBSResultModel::GetFontDisplay(nodeID->GetChapter(), nodeID->GetFont(), name, fullCount))
			return;

		// "<font>  (N)". The count is ROWS, not glyphs: a run of boxes side by side is one row, so
		// the two numbers differ, and every number in this tree means "how many rows are under me"
		// (user's call 2026-08-02). The status line is where the glyph count is said.
		//
		// UNCAPPED, like the two rows above it. This one read "(shown / total)" on the single group
		// the display cap falls inside until 2026-08-05, which was the document row's old rule - and
		// it outlived it there by a day, leaving one tree speaking two dialects: a chapter saying
		// how much WORK it holds and a font under it saying how much of itself is DRAWN. The hits
		// past the cap are still stored and still counted everywhere else, so the drawn number was
		// the odd one out and it went the same way.
		PMString label(name);
		label.SetTranslatable(kFalse);
		label.Append("  (");
		label.AppendNumber(fullCount);
		label.Append(")");
		this->LayOutBranchRow(node, widget, rowData, this->LevelShift() + kFontLevelIndent, label);
	}

	// A hit row: the match's line into the custom colour cell (three segments), no expander,
	// indented one zone deeper than the chapter row.
	void ApplyHitRow(const TreeNodePtr<KBSResultNodeID>& nodeID, IControlView* widget,
		const InterfacePtr<IPanelControlData>& rowData) const
	{
		// One question, not four: the strings, the flags, the outcome and the accent word all come
		// from the same hit, and the row wants all of them.
		KBSResultModel::RowDisplay row;
		if (!KBSResultModel::GetHitRow(nodeID->GetChapter(), nodeID->GetHit(), row))
			return;

		// Reasons a row has NOTHING to select, all handled identically from here on:
		//   replaced - it has been changed already, and cannot be changed again
		//   locked   - InDesign gives no way to change locked content, so a box would offer an
		//              action that quietly does nothing (the locator says lock)
		//   outcome  - the row already carries a reason it was left alone (missing / refused)
		//   report   - the panel is showing the aftermath of a replace, where NO row is selectable.
		//              This is the one that catches the rows carrying no reason at all: a chapter
		//              that could not be opened. It is a property of the panel rather than of the
		//              row, which is why it is still a second question.
		//   scan     - a scan REPORTS; there is nothing on it to replace (the missing-glyph one by
		//              the user's decision of 2026-08-02, after weighing a font-only replace and
		//              turning it down; the overset one because a frame's size is not something a
		//              panel can guess at). The model has always said so - RowHasCheckBox turns the
		//              whole result kind away, so Check All and Change Checked have never reached
		//              these rows - but the box was still being DRAWN here and could still be
		//              clicked, which promised an action nothing was going to carry out. Same
		//              question, asked in both places now.
		//
		// Asked as its own question first because it says something the per-row tests cannot: NO row
		// in this list has a box, which is a property of the WHOLE list. That is what makes it safe
		// to narrow the column in front of the locators for every row at once (see the cell's frame
		// below) - nothing is left ragged, because there is nothing left to line up with.
		//
		// Two ways it happens, and they are different facts: a SCAN reports rather than offers work
		// (no row of that kind ever had a box), and a replace's REPORT is what is left after every
		// row lost its box at once. Both are asked, together, inside NoRowHasCheckBox. A Find/Change
		// work list is the case that has to keep the full zone: it mixes rows that have a box with
		// rows that do not, and those locators have to stay in one column.
		// Both halves live in KBSResultModel::NoRowHasCheckBox now - the branch rows above ask the
		// same question to decide whether "checked" is a word their label may use at all, and three
		// rows of one tree must not be able to disagree about it.
		const bool everyRowLostBox = KBSResultModel::NoRowHasCheckBox();
		const bool noCheckBox = row.replaced || row.locked
			|| row.outcome != KBSResultModel::kOutcomeNone
			|| everyRowLostBox;

		// Draw our own indent: the check box sits where the hit row's content starts (one expander
		// zone right of the chapter row's text), and the colour cell follows it to the row's edge.
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;
		const PMReal xStart = kRowInset + kExpanderZone + kHitExtraIndent
			+ this->LevelShift() + this->FontShift(nodeID->GetChapter());

		// The check box. A row with nothing to select loses it completely; the space it would have
		// taken is left empty rather than reclaimed, so the locators stay in one column (see the
		// cell's frame below). Hiding alone would not be enough - a hidden widget still takes
		// clicks - so it is disabled as well.
		IControlView* checkView = rowData->FindWidget(kKBSResultCheckWidgetID);
		if (checkView != nil && noCheckBox)
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
				state->SetState(row.checked ? ITriStateControlData::kSelected : ITriStateControlData::kUnselected,
					kTrue /*invalidate*/, kFalse /*do NOT notify*/);
			}

			// Rows are recycled as the tree scrolls, so a row that once showed a replaced or locked
			// hit has to get its box back.
			checkView->ShowView(kTrue);
			checkView->Enable();
		}

		IControlView* cell = rowData->FindWidget(kKBSResultTextWidgetID);
		if (cell != nil)
		{
			PMRect frame = cell->GetFrame();
			// ALWAYS past the check zone, box or no box: the locators line up in one column down
			// the whole list and the check box sits in the margin to their left.
			//
			//     [v] P1(1)
			//         P1(2) lock
			//         P1(3) lock
			//
			// The rows without a box used to reclaim those 16px, which read as a ragged left edge
			// once a search turned up a lot of locked hits (user's call 2026-07-28, from a screen
			// shot). A column that does not move is worth more than the width.
			//
			// A list where NO row has a box is the case where the column can move, because it moves
			// for every row at once and nothing is left ragged: a scan (no row of that kind has ever
			// had one) and a replace's report (every row lost its box together). There it keeps half
			// the zone (kScanCheckZone) instead of all of it, which steps the hit rows off the row
			// above by the same 8px the levels use rather than sinking them a full check box deeper
			// than anything else in the tree.
			frame.Left(xStart + (everyRowLostBox ? kScanCheckZone : kCheckZone));
			frame.Right(rowRight);
			cell->SetFrame(frame);

			// Hand the three text segments to the colour cell; it invalidates itself as the tree
			// draws the row right after.
			InterfacePtr<IKBSRowData> data(cell, UseDefaultIID());
			if (data != nil)
				data->SetSegments(row.locator, row.accentFlag, row.preText, row.matchText, row.postText);
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
	// expander itself (see LayOutBranchRow - it shows the arrow whenever the hierarchy adapter
	// reports children, which does not depend on the node ever having been expanded). The
	// "expand to make the arrow appear" rule is the tree framework's own default, and this widget
	// manager overrides it.
	if (KBSResultModel::IsFromBook())
	{
		// The book row is the root's only child, so leaving it closed would show a panel with one
		// line on it and nothing else. Open it; the chapters underneath stay closed.
		treeMgr->ExpandNode(KBSResultNodeID::CreateBook(), kFalse);
		return;
	}

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

	// One notification per BRANCH row with childrenChangedAlso = kTrue: the framework refreshes that
	// row's children itself, so a 3000-row result costs a handful of calls rather than 3000. Only
	// the rows that actually have widgets (the visible ones) do any drawing; the rest pick the model
	// up when they scroll into view. The row heights do not change here, which is what NodeChanged
	// requires.
	//
	// ***** The BOOK row first, and it has to be asked for by name. ***** childrenChangedAlso
	// refreshes a node's children, so refreshing the chapters does NOT reach the row above them.
	// It carries "(N/M checked)" now (2026-08-05), so it goes stale the moment anything is ticked -
	// which is exactly what this function is called for. Only drawn on a book search; NodeChanged
	// on a node the tree does not hold is harmless.
	if (KBSResultModel::IsFromBook())
		treeMgr->NodeChanged(KBSResultNodeID::CreateBook(), kFalse /*children handled below*/);

	// The chapter AND each of its font rows, because childrenChangedAlso reaches a node's children -
	// and with the font level the hit rows are GRANDchildren. A chapter has a few fonts, not a few
	// thousand, so this stays a handful of calls.
	const int32 chapters = KBSResultModel::GetDisplayChapterCount();
	for (int32 c = 0; c < chapters; ++c)
	{
		treeMgr->NodeChanged(KBSResultNodeID::Create(c), kTrue /*childrenChangedAlso*/);
		const int32 fonts = KBSResultModel::GetDisplayFontCount(c);
		for (int32 f = 0; f < fonts; ++f)
			treeMgr->NodeChanged(KBSResultNodeID::CreateFont(c, f), kTrue /*childrenChangedAlso*/);
	}
}

//----------------------------------------------------------------------------------------
// KBSResultTree::RefreshCheckedCounts - repaint only the rows that read out a checked count
//----------------------------------------------------------------------------------------

void KBSResultTree::RefreshCheckedCounts(int32 chapterIdx)
{
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKBSPanelWidgetID));
	if (panelData == nil)
		return;
	IControlView* listView = panelData->FindWidget(kKBSResultListWidgetID);
	if (listView == nil)
		return;
	InterfacePtr<ITreeViewMgr> treeMgr(listView, UseDefaultIID());
	if (treeMgr == nil)
		return;

	// TWO rows, and childrenChangedAlso is kFalse for both. Ticking one box changes what the book
	// row and that chapter's row read out and NOTHING else: the box that was clicked draws itself,
	// and every other hit row is unaffected. RefreshRows would repaint every chapter and every font
	// row in the panel to say the same thing.
	if (KBSResultModel::IsFromBook())
		treeMgr->NodeChanged(KBSResultNodeID::CreateBook(), kFalse);
	if (chapterIdx >= 0)
		treeMgr->NodeChanged(KBSResultNodeID::Create(chapterIdx), kFalse);
}

//----------------------------------------------------------------------------------------
// KBSResultTree::ShowStatus - write the panel's single-line status read-out
//----------------------------------------------------------------------------------------

// The last thing ShowStatus was given. Kept in the module rather than read back off the widget:
// the widget is gone whenever the panel is closed, and app.kfcStatus has to answer regardless (a
// script can run a search with no panel on screen). Also, a StaticText cannot be read back
// reliably from outside anyway - see the panel-title work.
static PMString gLastStatus;

void KBSResultTree::GetLastStatus(PMString& outMessage)
{
	outMessage = gLastStatus;
	outMessage.SetTranslatable(kFalse);
}

void KBSResultTree::ShutdownCleanup()
{
	// The one static this file keeps, emptied for the reason KBSResultModel empties its four: a
	// PMString still holding storage when the .pln unloads runs its destructor against an application
	// that has already torn itself down (the KESCL ShutdownCleanup rule).
	//
	// Added on 2026-08-08. It had been standing since the status line was first kept in the module,
	// through both of the earlier sweeps that wrote that rule down - KBSResultModel's list even says
	// "when a static is added above, it is added here too", and KBSSearchEngine's names "a static
	// PMString" as the example. Neither of them was looking in this file.
	gLastStatus.Clear();

	// Added 2026-08-08 with the stamps themselves, rather than after a fourth sweep found them
	// standing: KBSEditStamp keeps statics of its own, and the rule this comment block describes
	// covers them exactly.
	//
	// It keeps TWO, and this line said "a static vector" until 2026-08-09 - the file's own
	// ShutdownCleanup had been written to match that count and released only the first of them.
	// Corrected in both places rather than one: an undercount here is what let the miss stand.
	KBSEditStamp::ShutdownCleanup();
}

namespace
{

/** Draw 'message' on the panel's status read-out. Does nothing when the panel is closed, which is an
    ordinary state. Shared by ShowStatus and by the restore below, so both spell the message the same
    way; only ShowStatus decides what the message IS.

    @param forceRedraw kFalse while the panel is still being built (see RestoreStatusOnPanelShow) -
                       there is nothing on screen to force yet, and this runs mid-construction. */
void WriteStatusWidget(const PMString& message, bool16 forceRedraw)
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

	// This line names files the user chose - a document's or a book's - and a StaticText takes a
	// lone '&' as a keyboard accelerator, so "A&B.indd" drew as "AB.indd" with the B underlined
	// (reported from the panel, 2026-07-31). Doubling each one up is the same thing SetColumnText
	// above already does for the tree's rows, and what the shipping panels do before handing a
	// user-entered name to a static text.
	//
	// ONLY what is drawn is doubled. gLastStatus keeps the message exactly as it was written:
	// app.kfcStatus exists to hand back what the panel said, not how a widget had to spell it, and
	// a test comparing against a file name must not have to know about this.
	PMString display(message);
	Utils<IMenuUtils>()->InsertAmpersandForDisplay(&display);

	// A single-line StaticText does not repaint on SetString alone, so invalidate + force a redraw
	// (the SDK immediate-StaticText-update rule).
	textData->SetString(display, kTrue /*invalidate*/, kFalse /*don't notify*/);
	textView->Invalidate();
	if (forceRedraw)
		textView->ForceRedraw();
}

}	// anonymous namespace

void KBSResultTree::RestoreStatusOnPanelShow()
{
	// Widget strings are PERSISTED IN THE WORKSPACE. A panel that is rebuilt - on every show, and
	// once more when InDesign is launched - comes back carrying whatever this line last said,
	// including a message from a session that ended days ago, while the results it described are
	// long gone (reported 2026-08-02: "the previous message is still there after a restart"). The
	// .fr's initial text is only ever used the very first time the panel is built.
	//
	// So the panel's show is where the line has to be written, exactly as the tab's name and the
	// illustration already are: whatever is written here outranks the persisted value.
	if (!gLastStatus.IsEmpty())
	{
		// Something ran in THIS session: put its message back. This also restores the line when the
		// panel is closed and reopened mid-session, which used to lose it.
		WriteStatusWidget(gLastStatus, kFalse /*still being built*/);
		return;
	}

	// Nothing has run since launch, so the line says what a freshly installed panel says. Taken from
	// the string table rather than spelled out here, so it cannot drift from the .fr's own initial
	// text - which would show as the old wording flashing up for the instant before this runs.
	PMString initial(kKBSStaticTextKey);
	initial.Translate();
	WriteStatusWidget(initial, kFalse /*still being built*/);
}

void KBSResultTree::ShowStatus(const PMString& message)
{
	// Remembered FIRST, before the panel is even looked for: this has to hold whether or not there
	// is a panel to draw it on.
	gLastStatus = message;
	gLastStatus.SetTranslatable(kFalse);

	// The illustration follows the same moments this line does, so it is settled here rather than at
	// every call site. Both directions run through here: an engine reports what it found (the model
	// says a run happened, so the searching cat), and a close responder reports that the results
	// went (it cleared the model first, so the plain one comes back). Does nothing when the panel is
	// closed, like everything else below.
	KBSPanelIcon::Update();

	// The panel is on screen and this is a report of something that just happened, so it is drawn
	// immediately (the restore path above is the one that must not force a redraw).
	WriteStatusWidget(message, kTrue /*force the redraw*/);
}

//----------------------------------------------------------------------------------------
// KBSResultTree::ShowCheckAllStatus - what Check All / Uncheck All just did, and to which row
//----------------------------------------------------------------------------------------

void KBSResultTree::ShowCheckAllStatus(const PMString& targetName, bool nowChecked)
{
	if (KBSResultModel::GetTotalHitCount() == 0)
		return;		// no results: leave whatever the search left on the line

	// "<name>  all checked" - the row's own name first, spaced the way the tree spaces its label
	// from its count, so the line reads as an echo of the row that was clicked.
	//
	// The NAME is what matters here and the counts are deliberately left out: the row itself now
	// reads "(N/M checked)", and this line exists to answer "which one did I just do that to?" -
	// the same two commands mean one chapter or the whole book depending on where the menu was
	// popped, and that is the part the panel cannot show afterwards.
	//
	// (Until 2026-08-05 this was ShowCheckedStatus, which put "<checked> / <total> checked." here
	// after ANY change of any box. The count moved onto the rows; what was left worth saying is
	// this.)
	PMString msg(targetName);
	msg.SetTranslatable(kFalse);
	msg.Append(nowChecked ? "  all checked" : "  all unchecked");
	KBSResultTree::ShowStatus(msg);
}

//----------------------------------------------------------------------------------------
// KBSResultTree::ShowHitCheckStatus - one box, named by the row's own locator
//----------------------------------------------------------------------------------------

void KBSResultTree::ShowHitCheckStatus(const PMString& locator, bool nowChecked)
{
	// Same shape as the Check All line above, one row narrower: what was clicked, then what it now
	// is. The locator is what the row LEADS with, so the two read as the same thing said twice -
	// which is the point, since the row that changed may be anywhere in a long list.
	PMString msg(locator);
	msg.SetTranslatable(kFalse);
	msg.Append(nowChecked ? "  checked" : "  unchecked");
	KBSResultTree::ShowStatus(msg);
}

// End, KBSResultListWidgetMgr.cpp.
