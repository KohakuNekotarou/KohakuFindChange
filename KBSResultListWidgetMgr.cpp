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
	// A BOOK row sits above the documents when the results came from a book search, and its
	// children step right by this much. 8px, not a full expander zone: the horizontal room in this
	// panel was fought for once already (see kHitExtraIndent), and half a zone is enough to read
	// the hierarchy. A document search has no book row and no shift, so its tree is unchanged.
	const PMReal kBookLevelIndent = 8.0;

	// Every row of this tree is this tall - the height both row resources declare in KBS.fr. Stated
	// here as well because the tree asks for it (GetNodeWidgetHeight), and because it is the fact
	// that lets Rebuild() promise ChangeRoot a constant widget height.
	const PMReal kRowHeight = 19.0;

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
		// ApplyDataToWidget. This panel switches the framework indent off and positions every row's
		// content itself, so our frames have to be applied ON TOP of whatever the base class did.
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
			else
				this->ApplyChapterRow(nodeID, node, widget, rowData);
		}
		return kTrue;
	}

	virtual PMReal GetIndentForNode(const NodeID& node) const
	{
		// PER-LEVEL indent (the base sums these up the ancestor chain). Unused in practice (the
		// framework indent is off, as in KESCL); the Apply*Row methods draw the hierarchy with
		// explicit offsets. Kept consistent in case a framework path ever consults it.
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID != nil && nodeID->IsHitRow())
			return PMReal(kHitExtraIndent);
		return 0.0;
	}

private:
	// How far right of the outermost level this tree's document and hit rows sit: one book-level
	// step when there is a book row above them, nothing when there is not.
	PMReal LevelShift() const
	{
		return KBSResultModel::IsFromBook() ? kBookLevelIndent : PMReal(0.0);
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
		PMString label(KBSResultModel::GetBookName());
		label.SetTranslatable(kFalse);
		label.Append("  (");
		label.AppendNumber(KBSResultModel::GetTotalHitCount());
		label.Append(")");
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
		const int32 shownCount = KBSResultModel::GetDisplayHitCount(nodeID->GetChapter());

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
		this->LayOutBranchRow(node, widget, rowData, this->LevelShift(), label);
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
		//              the safety ceiling cut short, or one that could not be opened. It is a
		//              property of the panel rather than of the row, which is why it is still a
		//              second question.
		const bool noCheckBox = row.replaced || row.locked
			|| row.outcome != KBSResultModel::kOutcomeNone
			|| KBSResultModel::IsShowingReplaceOutcome();

		// Draw our own indent: the check box sits where the hit row's content starts (one expander
		// zone right of the chapter row's text), and the colour cell follows it to the row's edge.
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;
		const PMReal xStart = kRowInset + kExpanderZone + kHitExtraIndent + this->LevelShift();

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
			frame.Left(xStart + kCheckZone);
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
