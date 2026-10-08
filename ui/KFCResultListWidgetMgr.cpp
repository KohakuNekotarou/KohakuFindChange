//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  ITreeViewWidgetMgr for the result tree. Two ROW SHAPES for the node kinds KFCResultNodeID.h names:
//
//    * BRANCH rows (from kKFCResultChapterNodeWidgetRsrcID): an expander arrow and a label. The
//      BOOK row, the DOCUMENT rows ("<name>  (R/N)") and the STORY rows (the code's FONT
//      rows - "P3  first words...  (R/N)") are all this one shape at different indents, so no level
//      needed a resource of its own.
//      The expander is hidden on a row with no children, which DOES happen: a book search that
//      finds nothing still draws its book row (the adapter gives the root one child whenever the
//      results came from a book), and that row has no chapters under it - measured by the user
//      (KFCBookWatch has the account). The guard also mirrors KESCL's.
//    * HIT rows (from kKFCResultHitNodeWidgetRsrcID): one match's line, drawn by the custom
//      colour cell (KFCColorTextView) with the matched part highlighted. No expander (a leaf);
//      indented past its branch row.
//
//  The STORY rows group each chapter's hits by story. (The code calls them FONT rows: the level once
//  held the fonts of the Find Missing Glyphs scan, since removed.)
//
//  The visual indent is drawn by explicit frame offsets in ApplyDataToWidget, applied on top of
//  the framework's own indent rather than instead of it (see GetIndentForNode), as in KESCL. This
//  file also hosts KFCResultTree::Rebuild (the tree lives here). Ported from KESCL's
//  KESCLResultListWidgetMgr - two levels then (document and hit; the book and story levels
//  came later) - itself modelled on the paneltreeview sample's PnlTrvTVWidgetMgr.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPalettePanelUtils.h"			// QueryPanelByWidgetID (the rebuild reaches the tree)
#include "IPanelControlData.h"
#include "ITextControlData.h"
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
#include "TextChar.h"		// kTextChar_Ellipse - the mark on a cut "Preview Text:"
#include "Utils.h"
#include "widgetid.h"		// kTreeNodeExpanderWidgetID

// Project includes:
#include "KFCUIID.h"
#include "KFCResultNodeID.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces
#include "KFCResultTree.h"
#include "KFCColorTextView.h"	// IKFCRowData (the hit cell)
#include "IKFCStatusTextData.h"	// the message area's pieces
#include "KFCPanelIcon.h"		// the illustration follows the status line
#include "KFCDiag.h"			// TREETIME and the tree-expand fault switch - test builds only
#include "KFCBookPanelLookup.h"	// QueryPanelManager - the panel made active (TakeKeyboard)
#include "IApplication.h"
#include "IEventHandler.h"		// the list's handler - the keyboard's holder (TakeKeyboard)
#include "IKeyBoard.h"			// GetKeyFocus / AcquireKeyFocus (TakeKeyboard)
#include "IPanelMgr.h"			// ShowPanelByWidgetID with giveKeyFocus (TakeKeyboard)
#include "ISession.h"
#include "IWidgetParent.h"		// the panel that holds the list (TakeKeyboard)
#ifdef KFC_DIAG
#include "PersistUtils.h"		// ::GetClass
#include <cstdio>
#endif

namespace
{
	// Visual layout of a row (px inside the 19px row). The chapter row's cells start after the
	// expander's zone; the hit row's cell steps one more zone right (drawn by us - the framework
	// indent is off, as in KESCL).
	const PMReal kRowInset = 2.0;
	const PMReal kExpanderZone = 16.0;
	// How much further right than its chapter row a hit row's content starts. ZERO: it begins exactly where
	// the chapter row's expander arrow ends, so the two line up down the left edge (the author's call, from
	// a screen shot - "put the check where the arrow is").
	const PMReal kHitExtraIndent = 0.0;
	// The hit row's colour cell starts this far right of the row's content: 8px, the step the book and story
	// levels already use, which makes the whole tree one even staircase (the author's call - "make it a nice
	// staircase").
	const PMReal kHitCellStep = 8.0;
	// A BOOK row sits above the documents when the results came from a book search, and its
	// children step right by this much. 8px, not a full expander zone: the horizontal room in this
	// panel was fought for once already (see kHitExtraIndent), and half a zone is enough to read
	// the hierarchy. A document search has no book row and no shift, so its tree is unchanged.
	const PMReal kBookLevelIndent = 8.0;
	// A STORY ("font") row sits between a document and its hits; its children step right by this much.
	// The same 8px the book level uses,
	// and for the same reason: half an expander zone is enough to read the hierarchy, and this panel's
	// width has been fought over once already (see kHitExtraIndent).
	const PMReal kFontLevelIndent = 8.0;

	// Every row of this tree is this tall. THE NUMBER IS NOT HERE: it is kKFCResultRowHeight in
	// KFCUIID.h, which KFCUI.fr reads as well - the row resources' frames and the tree's scroll
	// increments are the same fact, and the panel rounds its own height to a multiple of it
	// (KFCPanelView::ConstrainDimensions). This is where the tree asks for it (GetNodeWidgetHeight),
	// and it is the fact that lets Rebuild() promise ChangeRoot a constant widget height.
	const PMReal kRowHeight = kKFCResultRowHeight;

	// Put 'text' into a row's static-text cell. Row widgets are recycled as the tree scrolls, so
	// every cell is written on every apply. No manual repaint: the tree draws the row right after.
	void SetColumnText(IPanelControlData* rowData, const WidgetID& wid, const PMString& text)
	{
		if (rowData == nil)
			return;
		IControlView* cv = rowData->FindWidget(wid);
		if (cv == nil)
			return;

		// The label carries a name the user chose - a document's or a book's file name - and this
		// cell converts ampersands (KFCUI.fr sets that flag on it, exactly as the layer panel's row
		// resource does). A lone '&' is then taken as a keyboard accelerator: "A&B.indd" would show
		// as "AB.indd" with the B underlined. Doubling each one up is what the built-in panels do
		// before handing a user-entered name to a static text - see SetupLayerWidget in
		// LayerPanelTreeViewWidgetMgr.cpp, AddWidgetsIfNeeded in LinksUIPanelTreeViewWidgetMgr.cpp,
		// and the note on IMenuUtils::InsertAmpersandForDisplay itself ("style names, master names,
		// layer names ... or other user-entered strings").
		//
		// The hit rows do NOT come through here: their cell draws itself and asks for no ampersand
		// conversion at all (KFCColorTextView), which is right - the matched text has to appear
		// exactly as it stands in the document.
		PMString display(text);
		Utils<IMenuUtils>()->InsertAmpersandForDisplay(&display);

		InterfacePtr<ITextControlData> tcd(cv, UseDefaultIID());
		if (tcd != nil)
			tcd->SetString(display, kTrue /*invalidate*/, kFalse /*don't notify*/);
	}

	// A branch row's count: "  (R/N)" - R of its N hits replaced by this list (the author's call: the search's
	// count with the replaced one beside it, "1/3"). One shape for the book, document and story rows, so they read alike.
	void AppendCounts(PMString& label, int32 replaced, int32 hits)
	{
		label.Append("  (");
		label.AppendNumber(replaced);
		label.Append("/");
		label.AppendNumber(hits);
		label.Append(")");
	}
}

/** Builds and fills the result tree's row widgets (chapter rows and hit rows). */
class KFCResultListWidgetMgr : public CTreeViewWidgetMgr
{
public:
	// kHierarchical: the framework tracks expansion and asks the adapter for the child level.
	KFCResultListWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kHierarchical) {}
	virtual ~KFCResultListWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& node) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		RsrcID rsrcID = kKFCResultChapterNodeWidgetRsrcID;
		if (nodeID != nil && nodeID->IsHitRow())
			rsrcID = kKFCResultHitNodeWidgetRsrcID;

		// Built the way the layer panel builds its rows (LayerPanelTreeViewWidgetMgr.cpp:403-416), in
		// three steps rather than one CreateObject, so that the ORDER is spelled out:
		//   1. CreateObjectNoInit - make the boss but do not build its child hierarchy yet.
		//   2. SetThemeForView(kIDPanelTheme) - tell it that it is going to live in a palette. The
		//      row is created here, long before the tree hands it to the panel's window, so nothing
		//      else is going to say which theme it draws in.
		//   3. DoPostCreate - NOW build the children, with the theme already settled.
		// The layer panel is the only product tree built this way (linksui and conditionaltextui, among
		// others, make the row in one CreateObject and set no theme). The three steps stay as the safe
		// side - whether one call leaves the children unthemed was not measured.
		//
		// Both row resources are declared in KFCUIID.h and defined in KFCUI.fr, so nil here would mean
		// this plug-in's own resources did not load - not a case any handling on this side could
		// improve. It is handed straight back: the tree framework asked for the widget, so it is the
		// one that decides what to do without one.
		IPMUnknown* newObject = ::CreateObjectNoInit(
			::GetDataBase(this),
			RsrcSpec(LocaleSetting::GetLocale(), kKFCUIPluginID, kViewRsrcType, rsrcID),
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
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID != nil && nodeID->IsHitRow())
			return kKFCResultHitNodeWidgetID;
		return kKFCResultChapterNodeWidgetID;
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

	// THE ROW'S CONTENT GOES IN HERE, NOT IN AN ApplyNodeIDToWidget OVERRIDE. The two-argument
	// constructor above turns on the base class's V2 path, and on
	// that path the base's ApplyNodeIDToWidget runs, in this order: the selection highlight,
	// HideExpanderIfNotExpandable, ApplyIndentToWidget - which REWRITES the frame.Left of this row's
	// children - and then THIS (CTreeViewWidgetMgr.cpp:207-219). So the frames set below land on top of
	// the framework's indent by the framework's own order: the framework indent is NOT switched off here,
	// it is overwritten, and nothing in this file has to remember to call anything first.
	//
	// !Do not go back to an ApplyNodeIDToWidget override: there the base has to be called FIRST, and
	// moving the call last (as the layer and links panels have it) sends the hit rows' content back to
	// the left margin. Those panels are not a counter-example: they use the one-argument constructor, which the header
	// marks DEPRECATED and whose base call applies the highlight alone (CTreeViewWidgetMgr.cpp:59-74,
	// 207-221). And on the V2 path the base's own ApplyDataToWidget ran on every row before ours - the
	// sample default that numbers a ListIndexNodeID, asserting in a debug build that "you must override
	// CTreeViewWidgetMgr::ApplyDataToWidget()" (:93-113). This is that override: the shape the shipping
	// MSO panel (MSOPanelTreeViewWidgetMgr.cpp) and both of KCM's trees have.
	//
	// kTrue always, KCM's answer: kFalse asks the framework to build a new widget and apply again
	// (CTreeViewWidgetMgr.h:160-162), and a row the model cannot resolve would be missing from that one too.
	virtual bool16 ApplyDataToWidget(const NodeID& node, IPanelControlData* rowData, int32 /*message*/) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		InterfacePtr<IControlView> widget(rowData, UseDefaultIID());	// the row itself (the base queried rowData from it)
		if (nodeID != nil && widget != nil)
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
		// ApplyNodeIDToWidget calls it on every row (CTreeViewWidgetMgr.cpp:212-218) - and it moves
		// TWO kinds of child: the ones bound on BOTH sides (the hit row's colour cell, the chapter
		// row's label) and the ones bound on NEITHER (this row's expander arrow, kBindNone in
		// KFCUI.fr). Only a child bound on one side alone is left where it is - the hit row's check
		// box. What makes the framework indent invisible in this panel is NOT that it is switched
		// off: it is that the Apply*Row methods run AFTER it and set every one of those frames
		// themselves - they are called from ApplyDataToWidget, the base's last step (see there).
		//
		// ! These values do NOT add up to what the panel draws, and are not meant to. The book
		//   level's 8px step is not here at all (a document row answers 0 whether or not it hangs
		//   under a book row), because the Apply*Row methods carry it themselves in LevelShift().
		//   The two can only pull a row in different directions if one of those methods ever stops
		//   setting a frame - which is the invariant to keep, rather than "keep these numbers in
		//   step".
		TreeNodePtr<KFCResultNodeID> nodeID(node);
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
		return KFCResults()->IsFromBook() ? kBookLevelIndent : PMReal(0.0);
	}

	// How far right this chapter's HIT rows sit because of the levels above them: one step for the story
	// row every hit sits under.
	PMReal FontShift(int32 /*chapterIdx*/) const
	{
		return kFontLevelIndent;
	}

	// The shared shape of the two BRANCH rows (book and document): an expander arrow and a label
	// after it. The frames are set here rather than left to the resource, so ONE code path decides
	// every level's indent - the same reason the hit row has always drawn its own.
	//
	// The arrow is visible exactly when the row has children. Hiding it alone would leave a click
	// target behind (the stacked-widget lesson), so a hidden arrow is disabled too.
	void LayOutBranchRow(const NodeID& node, IControlView* widget,
		IPanelControlData* rowData, const PMReal& shift, const PMString& label) const
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

		IControlView* cell = rowData->FindWidget(kKFCResultChapterLabelWidgetID);
		if (cell != nil)
		{
			PMRect f = cell->GetFrame();
			f.Left(xLabel);
			f.Right(rowRight);
			cell->SetFrame(f);
		}
		SetColumnText(rowData, kKFCResultChapterLabelWidgetID, label);
	}

	// The BOOK row: which book these results came from. Only ever built for a book search, where it
	// is the root's single child - so it is the panel's standing answer to which book is the target,
	// which a status line cannot be (one line, truncated, overwritten by the next message).
	void ApplyBookRow(const NodeID& node, IControlView* widget,
		IPanelControlData* rowData) const
	{
		// "<book>  (R/N)" - how many of the hits the book's search holds this list has replaced, of how many (every one
		// drawn - one limit, F9; the R - AppendCounts). Drawn even when the search found
		// nothing - the hierarchy adapter gives the root one child whenever the results came from a book, which is how
		// the panel goes on naming the book it just searched - and then it reads "(0/0)", which is also how KFCBookWatch
		// describes it (the user's own measurement, of the "(0)" before the R); keep the two together.
		PMString label(KFCResults()->GetBookName());
		label.SetTranslatable(kFalse);
		AppendCounts(label, KFCResults()->GetTotalReplacedCount(), KFCResults()->GetTotalHitCount());
		// No shift: the book row IS the outermost level.
		this->LayOutBranchRow(node, widget, rowData, PMReal(0.0), label);
	}

	// A document row: its expander and "<name>  (R/N)" after the zone.
	void ApplyChapterRow(const TreeNodePtr<KFCResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, IPanelControlData* rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KFCResults()->GetChapterDisplay(nodeID->GetChapter(), name, fullCount))
			return;

		// "<name>  (R/N)" - of the chapter's hits, how many this list has replaced, the same read-out the book row
		// carries (AppendCounts). About the work, not the drawing - never "(shown / total)".
		PMString label(name);
		label.SetTranslatable(kFalse);
		// A DOCUMENT WITH NO WINDOW. Search: = All Documents searches those too, as
		// InDesign's own does; a replace there leaves it hidden (the user may hide a heavy one on purpose) and
		// only a jump opens a window - so its row says so. Asked as the row is drawn: a jump that opens one
		// takes the note away at the next repaint.
		if (KFCResults()->GetSearchScope() == KFCResultModel::kScopeAllDocuments)
		{
			UIDRef docRef;
			IDFile file;
			if (KFCResults()->GetChapterLocation(nodeID->GetChapter(), docRef, file)
				&& KFCChapters()->IsDocStillOpen(docRef) && !KFCChapters()->HasWindow(docRef))
				label.Append(" (no window)");
		}
		AppendCounts(label, KFCResults()->GetChapterReplacedCount(nodeID->GetChapter()), fullCount);
		// (No "cancelled" note: a cancel puts the WHOLE run back, so no chapter is left half-reached.)

		this->LayOutBranchRow(node, widget, rowData, this->LevelShift(), label);
	}

	// A STORY row (the code calls the level FONT - see the head of the file): the story's first words and how many rows sit under it. The same shape
	// as a document row - an expander and a label - so it shares the branch layout and the chapter row's
	// resource, one step further right.
	void ApplyFontRow(const TreeNodePtr<KFCResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, IPanelControlData* rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KFCResults()->GetFontDisplay(nodeID->GetChapter(), nodeID->GetFont(), name, fullCount))
			return;

		// "P3  first words...  (R/N)", the way a document row reads out its count, like the rows
		// above it: what the row holds, not what the panel drew of it. (A group that answered
		// GetFontDisplay is in range - no further test needed.)
		PMString label(name);
		label.SetTranslatable(kFalse);
		AppendCounts(label, KFCResults()->GetFontReplacedCount(nodeID->GetChapter(), nodeID->GetFont()), fullCount);
		this->LayOutBranchRow(node, widget, rowData,
			this->LevelShift() + kFontLevelIndent, label);
	}

	// A hit row: the match's line into the custom colour cell (IKFCRowData's parts), no expander,
	// indented one zone deeper than its story row.
	void ApplyHitRow(const TreeNodePtr<KFCResultNodeID>& nodeID, IControlView* widget,
		IPanelControlData* rowData) const
	{
		// One question, not four: the strings, the flags, the outcome and the accent word all come
		// from the same hit, and the row wants all of them.
		KFCResultModel::RowDisplay row;
		if (!KFCResults()->GetHitRow(nodeID->GetChapter(), nodeID->GetHit(), row))
			return;

		// Draw our own indent: the row's content starts one expander zone right of the chapter row's text,
		// and the colour cell follows it to the row's edge.
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;
		const PMReal xStart = kRowInset + kExpanderZone + kHitExtraIndent
			+ this->LevelShift() + this->FontShift(nodeID->GetChapter());

		IControlView* cell = rowData->FindWidget(kKFCResultTextWidgetID);
		if (cell != nil)
		{
			PMRect frame = cell->GetFrame();
			// One step right of the row above (kHitCellStep) - the staircase the levels make.
			frame.Left(xStart + kHitCellStep);
			frame.Right(rowRight);
			cell->SetFrame(frame);

			// Hand the row's parts to the colour cell, and invalidate it HERE: the cell's data holder
			// does not ask for a redraw (a stock cell's ITextControlData::SetString does - its invalidate
			// defaults to kTrue), and a recycled row would keep the picture of the row it used to be.
			InterfacePtr<IKFCRowData> data(cell, UseDefaultIID());
			if (data != nil)
			{
				// "Changed" FIRST ON A ROW THIS LIST HAS REPLACED (the author's call: nothing else on the row tells a
				// written row from one still as the search found it - without it a replaced row looks like the rest).
				// Part of the locator run - its colour, and the label a reader walks starts with it. An Undo of the
				// replace puts the row back as it was (UNDO-03, the row's copy has replaced == false), and the word goes.
				PMString locator(row.locator);
				if (row.replaced)
				{
					PMString changed("Changed ");
					changed.SetTranslatable(kFalse);
					changed.Append(locator);
					locator = changed;
				}
				data->SetSegments(locator, row.accentFlag, row.preText, row.matchText, row.postText);
			}
			cell->Invalidate();
		}
	}
};

CREATE_PMINTERFACE(KFCResultListWidgetMgr, kKFCResultListWidgetMgrImpl)

namespace
{
// The panel's result tree, reached through the panel - nil when the panel is closed, which is an ordinary
// state: Rebuild, RefreshRows and BeforeChapterRowGoes then do nothing.
ITreeViewMgr* QueryResultTreeMgr()
{
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	if (panelData == nil)
		return nil;
	IControlView* listView = panelData->FindWidget(kKFCResultListWidgetID);
	if (listView == nil)
		return nil;
	return InterfacePtr<ITreeViewMgr>(listView, UseDefaultIID()).forget();
}
}

//----------------------------------------------------------------------------------------
// KFCResultTree::Rebuild - reload the panel's tree from the model, open what the scope opens
//----------------------------------------------------------------------------------------

void KFCResultTree::Rebuild()
{
	InterfacePtr<ITreeViewMgr> treeMgr(QueryResultTreeMgr());
	if (treeMgr == nil)
		return;

#ifdef KFC_DIAG
	// TEST BUILDS ONLY (docs/ai-notes/kfc-speedup-ideas-2026-10-05.md): how long the rebuild takes and how
	// many rows it opens one by one (TREETIME) - and, with the fault switch tree-expand, the same rows opened another way:
	// mode 1 = one ExpandNode with all its descendants for a document row that opens; mode 2 = every row opened BEFORE
	// ChangeRoot (ITreeViewMgr.h: expansion is kept across ChangeRoot while the root is the same).
	KFC_CLOCK(cTree);
	const KFCDiagPerf treePerf;		// what the rebuild made InDesign do (KFCDiag.h)
	int32 expandCalls = 0;
	const int expandMode = KFCDiagFaultValue("tree-expand", 0, 0);
	const bool16 expandAllBelow = (expandMode == 1) ? kTrue : kFalse;
#else
	const bool16 expandAllBelow = kFalse;
#endif
	auto expand = [&](const NodeID& node, bool16 allDescendants)
	{
#ifdef KFC_DIAG
		++expandCalls;
#endif
		treeMgr->ExpandNode(node, allDescendants);
	};

	// ClearTree(kTrue) forgets the old expansion state (rebuilt by the priming below);
	// ChangeRoot(kTrue) says every row widget has the same height, which they do - both row
	// resources are kKFCResultRowHeight tall and GetNodeWidgetHeight answers that for every node.
	// (The number itself is not spelled out here, for the reason kRowHeight gives at the top.)
	treeMgr->ClearTree(kTrue);
#ifdef KFC_DIAG
	if (expandMode != 2)
#endif
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
	const int32 chapters = KFCResults()->GetDisplayChapterCount();
	// (A test build's tree-expand mode 1 opens such a document row with everything under it in one call, and the
	// loop below then leaves its rows alone.)
	bool chaptersOpenedWhole = false;
	if (KFCResults()->IsFromBook())
	{
		// The book row is the root's only child, so leaving it closed would show a panel with one
		// line on it and nothing else. Open it; the chapters underneath stay closed.
		expand(KFCResultNodeID::CreateBook(), kFalse);
	}
	else if (KFCResults()->GetSearchScope() != KFCResultModel::kScopeAllDocuments)
	{
		// A single document has just the one chapter, so open it - otherwise the result is one closed
		// row and the hits take an extra click to reach.
		for (int32 n = 0; n < chapters; ++n)
			expand(KFCResultNodeID::Create(KFCResults()->GetShownChapter(n)), expandAllBelow);
		chaptersOpenedWhole = (expandAllBelow != kFalse);
	}
	// (All Documents - the author's call - leaves its document rows CLOSED, for the book's reason above:
	//  one document's hits would bury the fact that the others matched at all.)
	//
	// THE STORY ROWS COME UP OPEN. The level is a grouping, not a place to hide rows: a story row closed
	// would put every hit one click further away. Opened in a closed chapter too (a book's), so the chapter's arrow shows
	// its hits at once.
	for (int32 n = 0; n < chapters && !chaptersOpenedWhole; ++n)
	{
		const int32 c = KFCResults()->GetShownChapter(n);
		const int32 groups = KFCResults()->GetDisplayFontCount(c);
		for (int32 g = 0; g < groups; ++g)
			expand(KFCResultNodeID::CreateFont(c, g), kFalse);
	}

#ifdef KFC_DIAG
	if (expandMode == 2)
		treeMgr->ChangeRoot(kTrue);		// the rows were opened above, before the tree was connected again
	{
		double tTree = 0;
		KFC_SPENT(tTree, cTree);
		char counters[300] = { 0 };
		treePerf.Since(counters, sizeof(counters));
		KFC_DIAG_LOG("TREETIME hits=%d chapters=%d expands=%d mode=%d %.0f ms %s", (int)KFCResults()->GetTotalHitCount(),
			(int)chapters, (int)expandCalls, expandMode, tTree, counters);
	}
	// THE COUNTS THE TREE IS BUILT FROM, CHECKED THE OLD WAY (test builds). GetDisplayFontCount finds
	// the shown groups by halving now; here every group is counted one by one, as they were before, after the timing
	// above. A difference is logged as DISPCOUNT MISMATCH - the regression runs grep for it.
	for (int32 n = 0; n < chapters; ++n)
	{
		const int32 c = KFCResults()->GetShownChapter(n);
		int32 fontsByGroup = 0;
		for (int32 g = 0; KFCResults()->IsStoryGroup(c, g); ++g)
			if (KFCResults()->GetDisplayFontHitCount(c, g) > 0)
				++fontsByGroup;
		const int32 fontsNow = KFCResults()->GetDisplayFontCount(c);
		if (fontsNow != fontsByGroup)
			KFC_DIAG_LOG("DISPCOUNT MISMATCH chapter=%d fonts now=%d by group=%d", (int)c, (int)fontsNow, (int)fontsByGroup);
	}
#endif
}

//----------------------------------------------------------------------------------------
// KFCResultTree::RefreshRows - repaint the rows in place, without rebuilding the tree
//----------------------------------------------------------------------------------------

void KFCResultTree::RefreshRows()
{
	InterfacePtr<ITreeViewMgr> treeMgr(QueryResultTreeMgr());
	if (treeMgr == nil)
		return;

	// One notification per BRANCH row with childrenChangedAlso = kTrue: the framework refreshes that
	// row's children itself, so a 3000-row result costs a handful of calls rather than 3000. Only
	// the rows that actually have widgets (the visible ones) do any drawing; the rest pick the model
	// up when they scroll into view. The row heights do not change here, which is what NodeChanged
	// requires.
	//
	// The BOOK row first, and it has to be asked for by name. childrenChangedAlso refreshes a node's
	// children, so refreshing the chapters does NOT reach the row above them. It carries the book's
	// count, so it goes stale the moment a row is replaced or taken out - which is what this function is
	// called for. Only drawn on a book search; NodeChanged
	// on a node the tree does not hold is harmless.
	if (KFCResults()->IsFromBook())
		treeMgr->NodeChanged(KFCResultNodeID::CreateBook(), kFalse /*children handled below*/);

	// The chapter AND each of its story rows, because childrenChangedAlso reaches a node's children -
	// and under the story level the hit rows are GRANDchildren. One call per story row: a handful for most
	// documents, one per hit for a document of one-hit stories (a frame per entry) - still one per row, never
	// more.
	const int32 chapters = KFCResults()->GetDisplayChapterCount();
	for (int32 n = 0; n < chapters; ++n)
	{
		const int32 c = KFCResults()->GetShownChapter(n);	// (chapter n, but for an emptied one before it)
		treeMgr->NodeChanged(KFCResultNodeID::Create(c), kTrue /*childrenChangedAlso*/);
		const int32 fonts = KFCResults()->GetDisplayFontCount(c);
		for (int32 f = 0; f < fonts; ++f)
			treeMgr->NodeChanged(KFCResultNodeID::CreateFont(c, f), kTrue /*childrenChangedAlso*/);
	}
}

//----------------------------------------------------------------------------------------
// KFCResultTree::BeforeChapterRowGoes - one document row out, the rest left as they are
//----------------------------------------------------------------------------------------

void KFCResultTree::BeforeChapterRowGoes(int32 chapterIdx)
{
	InterfacePtr<ITreeViewMgr> treeMgr(QueryResultTreeMgr());
	if (treeMgr == nil)
		return;
	// A row the tree never showed (past the display cap) is nothing to take out.
	if (KFCResults()->GetShownChapterPos(chapterIdx) < 0)
		return;
	treeMgr->BeforeNodeDeleted(KFCResultNodeID::Create(chapterIdx));
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowStatus - write the panel's message area
//----------------------------------------------------------------------------------------

// The last thing ShowStatus was given. Kept in the module rather than read back off the widget: the
// widget is gone whenever the panel is closed, and the line is written back when the panel is shown
// again (RestoreStatusOnPanelShow).
static PMString gLastStatus;

void KFCResultTree::ShutdownCleanup()
{
	// The statics this file keeps, emptied for the reason KFCResultModel empties its own: a PMString
	// still holding storage when the .pln unloads runs its destructor against an application that has
	// already torn itself down (the KESCL ShutdownCleanup rule). When a static is added above, it is
	// added here too.
	gLastStatus.Clear();
	// ...and the Return filter the list pushes on the application's event dispatcher (KFCResultTreeEH.cpp): off the
	// stack and released before the .pln goes.
	ShutdownReturnFilter();
}

namespace
{

// The message area's data, and its view - nil while the panel is closed (an ordinary state; do nothing then). The
// same reach Rebuild uses, which is why this lives here rather than in the action component.
IKFCStatusTextData* QueryStatusTextData(IControlView*& outView)
{
	outView = nil;
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	if (panelData == nil)
		return nil;
	IControlView* textView = panelData->FindWidget(kKFCStaticTextWidgetID);
	if (textView == nil)
		return nil;
	InterfacePtr<IKFCStatusTextData> textData(textView, UseDefaultIID());
	if (textData == nil)
		return nil;
	outView = textView;
	return textData.forget();
}

/** Put this message on the panel's message area (IKFCStatusTextData.h) - the sentence alone, in the theme's text
    colour. Does nothing when the panel is closed, which is an ordinary state. Every writer below reaches the box
    through here; they decide only WHAT it says.

    NO '&' DOUBLING. The line names files the user chose; a stock StaticText takes a lone '&' as a
    keyboard accelerator ("A&B.indd" draws as "AB.indd" with the B underlined) and needs every message
    put through InsertAmpersandForDisplay. This box is drawn by hand, with convertAmpersand kFalse
    (KFCStatusTextView.cpp), so the text is the text - and a reader of the widget (KIDMCP's inspect_ui)
    reads "A&B.indd", not a doubled "A&&B.indd". !Do not double it again.

    @param forceRedraw kFalse while the panel is still being built (see RestoreStatusOnPanelShow) -
                       there is nothing on screen to force yet, and this runs mid-construction. */
void WriteMessage(const PMString& message, bool16 forceRedraw)
{
	IControlView* textView = nil;
	InterfacePtr<IKFCStatusTextData> textData(QueryStatusTextData(textView));
	if (textData == nil)
		return;

	textData->SetText(message);

	// The text is not something the view watches, so it is told to repaint - and, for a report, made
	// to repaint NOW (ShowStatus says why): an invalidated view waits for the next event loop (memory
	// statictext-widget-immediate-update).
	// ONE call for each case: ForceRedraw with no region draws the whole view now ("Redraws the invalid
	// region directly", IControlView.h:281-286), so an Invalidate in front of it asks for nothing more.
	if (forceRedraw)
		textView->ForceRedraw();
	else
		textView->Invalidate();
}

/** What the box says when nothing has run since launch - the string table's, so it cannot drift from
    what a freshly installed panel says. */
PMString InitialMessage()
{
	PMString initial(kKFCStaticTextKey);
	initial.Translate();
	return initial;
}

}	// anonymous namespace

void KFCResultTree::RestoreStatusOnPanelShow()
{
	// Widget strings are PERSISTED IN THE WORKSPACE. A panel that is rebuilt - on every show, and
	// once more when InDesign is launched - comes back carrying whatever this line last said,
	// including a message from a session that ended days ago, while the results it described are
	// long gone ("the previous message is still there after a restart"). The box is drawn by hand from
	// pieces nothing persists, and its resource carries no text at all - so this is the only writer a
	// newly built panel meets.
	//
	// So the panel's show is where the line has to be written, exactly as the tab's name and the
	// illustration already are: whatever is written here outranks the persisted value.
	if (!gLastStatus.IsEmpty())
	{
		// Something ran in THIS session: put its message back. This also restores the line when the
		// panel is closed and reopened mid-session.
		WriteMessage(gLastStatus, kFalse /*still being built*/);
		return;
	}

	// Nothing has run since launch, so the line says what a freshly installed panel says.
	WriteMessage(InitialMessage(), kFalse /*still being built*/);
}

void KFCResultTree::ShowStatus(const PMString& message)
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
	KFCPanelIcon::Update();

	// The panel is on screen and this is a report of something that just happened, so it is drawn
	// immediately (the restore path above is the one that must not force a redraw).
	WriteMessage(message, kTrue /*force the redraw*/);
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowRowPreview - a GREP row's after-text on the message area
//----------------------------------------------------------------------------------------

// The heading of a preview - and how the area is known to be showing one (DropRowPreview).
static const char* const kPreviewLabel = "Preview Text:";

bool KFCResultTree::ShowRowPreview(int32 chapterIdx, int32 hitIdx)
{
	// What Return would write at this row (the author's call: an ordinary GREP search's row shows its after-text
	// when selected) - the model writes it inside a step it throws away (KFCReplaceEngine::PreviewHit) and says false
	// wherever Return would not write it now.
	PMString after;
	KFCResultModel::RowDisplay row;
	if (!KFCRuns()->PreviewHit(chapterIdx, hitIdx, after) || !KFCResults()->GetHitRow(chapterIdx, hitIdx, row))
	{
		DropRowPreview();
		return false;
	}
	IControlView* textView = nil;
	InterfacePtr<IKFCStatusTextData> textData(QueryStatusTextData(textView));
	if (textData == nil)
		return false;
	// KCM's "Source Text:" shape (the author's call): the heading on its own line, the row's own context faded, and
	// what would be written at full colour; nothing written at all is a PLACE, drawn as the bar.
	PMString shown(after);
	// CUT LONG BEFORE THE BOX HAS TO MEASURE IT: what a GREP replace
	// writes can be a story's worth ($0 over a match across paragraphs), where a row's own text is capped at 50
	// characters for drawing - and the box lays its text out by measuring prefixes, again for every width it tries
	// (KFCStatusTextView.cpp), on every repaint. It holds four lines, about 120 characters on a Japanese UI; past that
	// the view ends it in an ellipsis anyway, so the tail is cut here, marked the same way.
	const int32 kPreviewMaxChars = 300;
	if (shown.CharCount() > kPreviewMaxChars)
	{
		// Not through the middle of a surrogate pair (the doubt KFCStatusTextView's KFCSafeCut carries).
		int32 keep = kPreviewMaxChars;
		const uint32 at = shown.GetChar(keep).GetValue();
		if (at >= 0xDC00 && at <= 0xDFFF)
			--keep;
		shown.Truncate(shown.CharCount() - keep);
		shown.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	// The breaks as marks in ALL THREE pieces - the pilcrow and the return arrow the row itself draws (the same
	// function; the model keeps a row's context raw and the row marks it up as it draws). A raw CR here would be
	// taken by the box as a line break: the row's "sat" + pilcrow would come out as "sat" and an empty line.
	PMString pre(row.preText), post(row.postText);
	KFCResults()->MarkUpBreaksForDisplay(pre);
	KFCResults()->MarkUpBreaksForDisplay(shown);
	KFCResults()->MarkUpBreaksForDisplay(post);
	PMString label(kPreviewLabel);
	label.SetTranslatable(kFalse);
	textData->SetSegments(label, pre, shown, post, shown.IsEmpty() ? kTrue : kFalse);
	textView->ForceRedraw();
	return true;
}

void KFCResultTree::DropRowPreview()
{
	// A row with no preview (replaced, a Text search's, a branch row...) must not leave the last row's standing beside
	// it: the area goes back to the last ordinary message (gLastStatus - a preview is never kept there).
	IControlView* textView = nil;
	InterfacePtr<IKFCStatusTextData> textData(QueryStatusTextData(textView));
	if (textData == nil)
		return;
	PMString label, pre, mid, post;
	bool16 wantCaret = kFalse;
	textData->GetSegments(label, pre, mid, post, wantCaret);
	if (label != PMString(kPreviewLabel))
		return;
	WriteMessage(gLastStatus.IsEmpty() ? InitialMessage() : gLastStatus, kTrue /*force the redraw*/);
}

bool KFCResultTree::RefusedWhileRunning()
{
	if (!KFCRuns()->IsAnyRunning())
		return false;
	PMString busy(KFCRuns()->BusyMessage());
	busy.SetTranslatable(kFalse);
	ShowStatus(busy);
	return true;
}

#ifdef KFC_DIAG
std::string KFCResultTree::DiagKeyFocus()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (keyBoard == nil)
		return "no-keyboard";
	IEventHandler* const focus = keyBoard->GetKeyFocus();		// not counted (IKeyBoard.h) - used here only
	if (focus == nil)
		return "nobody";
	InterfacePtr<ITreeViewMgr> treeMgr(QueryResultTreeMgr());
	InterfacePtr<IEventHandler> treeEH(treeMgr, UseDefaultIID());
	if (treeEH != nil && treeEH.get() == focus)
		return "tree";
	InterfacePtr<IControlView> view(focus, UseDefaultIID());
	char text[96];
	::sprintf_s(text, sizeof(text), "class=0x%x widget=0x%x", static_cast<unsigned>(::GetClass(focus).Get()),
		view != nil ? static_cast<unsigned>(view->GetWidgetID().Get()) : 0u);
	return text;
}
#endif

bool KFCResultTree::TakeKeyboard()
{
	// THE PANEL MADE THE ACTIVE ONE AND ITS LIST GIVEN THE KEYBOARD, BY THE PANEL SYSTEM'S OWN DOORS (the author's
	// call: make the panel active and focus it, not an idle task or a timer). The arrows' walk fronts a document window
	// (KFCJump), and a Return pressed after it found the keyboard handed to the document by a second road the Return's
	// hold could refuse but not stop (KFCResultTreeEH, gHolding) - the list had kept InDesign's keyboard, not the
	// active panel. So, in order:
	//  1. IPanelMgr::ShowPanelByWidgetID with giveKeyFocus - "give key focus to panel" (IPanelMgr.h). A panel that is
	//     closed is not opened for this: the user closed it.
	//  2. the panel holding the list gives it the keyboard - IPanelControlData::SetKeyboardFocus, which wants "one of
	//     its children" (IPanelControlData.h), so the list's own parent panel; Adobe's Links panel hands its caption
	//     rows the keyboard the same way (open/components/linksui/AddDeleteCaptionRowButtonObserver.cpp).
	//  3. checked - and taken by IKeyBoard::AcquireKeyFocus, as before, if the panel's doors did not land it there.
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	if (panelMgr == nil || !panelMgr->IsPanelWithWidgetIDShown(kKFCPanelWidgetID))
		return false;
	panelMgr->ShowPanelByWidgetID(kKFCPanelWidgetID, kTrue);

	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	IControlView* listView = (panelData != nil) ? panelData->FindWidget(kKFCResultListWidgetID) : nil;
	if (listView == nil)
		return false;
	InterfacePtr<const IWidgetParent> listParent(listView, UseDefaultIID());
	InterfacePtr<IPanelControlData> holder(listParent != nil
		? static_cast<IPanelControlData*>(listParent->QueryParentFor(IID_IPANELCONTROLDATA)) : nil);
	if (holder != nil)
		holder->SetKeyboardFocus(listView);

	InterfacePtr<IEventHandler> listEH(listView, UseDefaultIID());
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	if (listEH == nil || keyBoard == nil)
		return false;
	KFC_DIAG_LOG("RETFOCUS TakeKeyboard after the panel's doors focus=%s", DiagKeyFocus().c_str());
	if (keyBoard->GetKeyFocus() != listEH)
		keyBoard->AcquireKeyFocus(listEH);
	return keyBoard->GetKeyFocus() == listEH;
}

bool KFCResultTree::ReplaceRow(int32 chapterIdx, int32 hitIdx, PMString* outStatus)
{
	if (RefusedWhileRunning())
		return false;
	if (!KFCRuns()->CanReplaceHit(chapterIdx, hitIdx))
		return false;
	PMString status;
	KFC_DIAG_LOG("RETFOCUS ReplaceRow begin focus=%s", DiagKeyFocus().c_str());
	const bool wrote = KFCRuns()->ReplaceHit(chapterIdx, hitIdx, status);	// no prompt (the author's call)
	KFC_DIAG_LOG("RETFOCUS ReplaceRow after the write focus=%s", DiagKeyFocus().c_str());
	// A WRITE INTO A DOCUMENT THAT HAS NO WINDOW (Search: = All Documents): it goes through and nothing opens one - the
	// user may keep a heavy document hidden on purpose - so the line says what the screen cannot show. Asked once the
	// write is over (a book chapter the Replace reopened has been given its window by then).
	UIDRef docRef;
	IDFile file;
	if (wrote && KFCResults()->GetChapterLocation(chapterIdx, docRef, file) && KFCChapters()->IsDocStillOpen(docRef)
		&& !KFCChapters()->HasWindow(docRef))
		status.Append(" The document has no window - still hidden.");
	// Repainted in place - or the tree rebuilt, when the write threw the results away: a refusal on a changed
	// Find/Change query clears them (KFCReplaceEngine::RefuseChangedQuery), and RefreshRows repaints only the chapters
	// the model still holds - none, so the old rows would stay drawn and answer nothing until the next search.
	if (KFCResults()->HasRun())
		RefreshRows();
	else
		Rebuild();
	KFC_DIAG_LOG("RETFOCUS ReplaceRow after the repaint focus=%s", DiagKeyFocus().c_str());
	ShowStatus(status);
	KFC_DIAG_LOG("RETFOCUS ReplaceRow after the status focus=%s", DiagKeyFocus().c_str());
	if (outStatus != nil)
		*outStatus = status;
	return wrote;
}

// End, KFCResultListWidgetMgr.cpp.
