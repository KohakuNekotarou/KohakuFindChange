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
//      rows - "ID260: first words...  (R/N)") are all this one shape at different indents, so no level
//      needed a resource of its own.
//      The expander is hidden on a row with no children, which DOES happen: a book search that
//      finds nothing still draws its book row (the adapter gives the root one child whenever the
//      results came from a book), and that row has no chapters under it - measured by the user
//      (KFCBookWatch has the account). The guard also mirrors KESCL's.
//    * HIT rows (from kKFCResultHitNodeWidgetRsrcID): one match's line, drawn by the custom
//      colour cell (KFCColorTextView) with the matched part highlighted. No expander (a leaf);
//      indented past its branch row.
//
//  WHILE THE RESULTS ARE KEPT IN TIERS (KFCResultHistory - docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md
//  section 3-4) a tier's own row is a branch row at the left edge, the live list's rows sit one step right of it, and a kept
//  tier's rows are drawn from its copy in the same two shapes - disabled, which greys them. What every row says, live or
//  copied, is worked out in one place (KFCResultTree::GetRowLook), so a kept row reads as the row it was.
//
//  The STORY rows group each chapter's hits by story. (The code calls them FONT rows: the level once
//  held the fonts of the Find Missing Glyphs scan, since removed.)
//
//  The visual indent is drawn by explicit frame offsets in ApplyDataToWidget, applied on top of
//  the framework's own indent rather than instead of it (see GetIndentForNode), as in KESCL. This
//  file also hosts the KFCResultTree doors that reach the tree and the message area (the head of KFCResultTree.h says
//  which file holds which). Ported from KESCL's
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
#include "KFCResultHistory.h"	// the tiers kept above the live list - their rows' copies, and whether there are any
#include "KFCJump.h"			// ReplaceObjectRows - object rows' Replace
#include "KFCResultEye.h"		// Draw - the row eye, drawn as its click and its trace draw it
#include "KFCColorTextView.h"	// IKFCRowData (the hit cell)
#include "IKFCStatusTextData.h"	// the message area's pieces
#include "KFCTextCut.h"			// KFCSafeCut - where the box's text may be cut (the rows' own rule, model/)
#include "KFCPanelIcon.h"		// the illustration follows the status line
#include "KFCDiag.h"			// TREETIME, RETFOCUS and the fault switches - test builds only
#include "KFCBookPanelLookup.h"	// QueryPanelManager - the panel made active (TakeKeyboard)
#include "IApplication.h"
#include "IEventHandler.h"		// the list's handler - the keyboard's holder (TakeKeyboard)
#include "IKeyBoard.h"			// GetKeyFocus / AcquireKeyFocus (TakeKeyboard)
#include "IPanelMgr.h"			// ShowPanelByWidgetID with giveKeyFocus (TakeKeyboard)
#include "ISession.h"
#include "IWidgetParent.h"		// the panel that holds the list (TakeKeyboard)
#include <algorithm>			// std::find - the documents a Replace of rows wrote to (ReplaceRows)
#include <utility>
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
	// the chapter row's expander arrow ends, so the two line up down the left edge (the author's call; the colour
	// cell starts kHitCellStep further right).
	const PMReal kHitExtraIndent = 0.0;
	// The hit row's colour cell starts this far right of the row's content: 8px, the step the book and story
	// levels already use, which makes the whole tree one even staircase (the author's call - "make it a nice
	// staircase").
	const PMReal kHitCellStep = 8.0;
	// (THE ROW EYE has no column of its own: it stands in the hit row's expander zone, under the arrow of the row above -
	//  LayOutHitRow says why.)
	// A BOOK row sits above the documents when the results came from a book search, and its
	// children step right by this much. 8px, not a full expander zone: the horizontal room in this
	// panel is short, and half a zone is enough to read the hierarchy. A document search has no book row
	// and no shift, so its tree is unchanged.
	const PMReal kBookLevelIndent = 8.0;
	// A STORY ("font") row sits between a document and its hits; its children step right by this much.
	// The same 8px the book level uses, and for the same reason.
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

	// A DOCUMENT WITH NO WINDOW: the chapter's document is open and no window shows it. The one question the document
	// row's "(no window)" and the status line after a Replace ("still hidden") both ask.
	bool ChapterHasNoWindow(int32 chapterIdx)
	{
		UIDRef docRef;
		IDFile file;
		return KFCResults()->GetChapterLocation(chapterIdx, docRef, file) && KFCChapters()->IsDocStillOpen(docRef)
			&& !KFCChapters()->HasWindow(docRef);
	}

	// How far right of the outermost level this list's document and hit rows sit: one book-level step when there is a book
	// row above them, nothing when there is not. (A tier's own level is added where a row is drawn - TierShift.)
	PMReal LevelShift()
	{
		return KFCResults()->IsFromBook() ? kBookLevelIndent : PMReal(0.0);
	}

	// How far right this chapter's HIT rows sit because of the levels above them: one step for the story row every TEXT
	// hit sits under; none for an OBJECT chapter, whose rows hang off the document row directly.
	PMReal FontShift(int32 chapterIdx)
	{
		return (KFCResults()->GetDisplayFontCount(chapterIdx) > 0) ? kFontLevelIndent : PMReal(0.0);
	}

	// THE TIERS' OWN LEVEL (the stacked results' spec, section 2-2): while the results are kept in tiers, the live list's
	// rows sit one step right of the tier's row above them - and a kept tier's rows of theirs. The book level's 8px.
	const PMReal kTierLevelIndent = 8.0;
	PMReal TierShift()
	{
		return KFCResultHistory::IsStacking() ? kTierLevelIndent : PMReal(0.0);
	}

	// A DOCUMENT ROW COMES UP OPEN when the results came from neither a book nor All Documents: one document, its rows at
	// once (Rebuild says why each). Asked by Rebuild and by OpensWhenDrawn - one rule. (Both as the list recorded them, not
	// its scope: a text list Search Text in These Objects made from an All Documents one is kScopeInObjects.)
	bool DocumentRowsOpen()
	{
		return !KFCResults()->IsFromBook() && !KFCResults()->IsFromAllDocuments();
	}

	// A ROW DRAWN IN THE COLOUR CELL: a live hit row, or a kept tier's copy of one (KFCResultHistory).
	bool DrawsAsHit(const KFCResultNodeID* nodeID)
	{
		if (nodeID == nil)
			return false;
		if (nodeID->IsHitRow())
			return true;
		return nodeID->IsKeptRow() && KFCResultHistory::IsKeptHit(nodeID->GetTier(), nodeID->GetKeptRow());
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
		if (DrawsAsHit(nodeID))
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
		if (DrawsAsHit(nodeID))
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
		if (nodeID == nil || widget == nil)
			return kTrue;
		// WHAT THE ROW SAYS - KFCResultTree::GetRowLook for the live list's rows and the live tier's, KFCResultHistory's copy
		// for a kept tier's - and HOW FAR RIGHT: a kept row one tier level in, the live list's rows one tier level in while the
		// results are kept in tiers (TierShift), a tier's own row at the left edge.
		KFCResultTree::RowLook look;
		const bool kept = nodeID->IsGrey();
		bool known = false;
		PMReal levelIn(0.0);
		if (kept)
		{
			known = KFCResultHistory::GetKeptLook(nodeID->GetTier(), nodeID->GetKeptRow(), look);
			if (nodeID->IsKeptRow())
				levelIn = kTierLevelIndent;
		}
		else
		{
			known = KFCResultTree::GetRowLook(node, look);
			if (!nodeID->IsTierRow())
				levelIn = TierShift();
		}
		if (!known)
			return kTrue;
		if (look.hit)
			this->LayOutHitRow(widget, rowData, look.shift + levelIn, look, kept);
		else
			this->LayOutBranchRow(node, widget, rowData, look.shift + levelIn, look.label, kept);
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
		// KFCUI.fr). Only a child bound on one side alone is left where it is - no row has one. What makes
		// the framework indent invisible in this panel is NOT that it is switched
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
	// The shared shape of the BRANCH rows (book, document, story, and a tier's own row): an expander arrow and a label
	// after it. The frames are set here rather than left to the resource, so ONE code path decides
	// every level's indent - the same reason the hit row has always drawn its own.
	//
	// The arrow is visible exactly when the row has children. Hiding it alone would leave a click
	// target behind (the stacked-widget lesson), so a hidden arrow is disabled too.
	//
	// A KEPT ROW'S LABEL IS DISABLED - grey (the stacked results' spec, section 2-2) - and every other row's enabled: set on
	// every apply, a row widget being recycled between the two. (The arrow keeps working on a kept row: it is what opens it.)
	void LayOutBranchRow(const NodeID& node, IControlView* widget,
		IPanelControlData* rowData, const PMReal& shift, const PMString& label, bool kept) const
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
			if (kept)
				cell->Disable();
			else
				cell->Enable();
		}
		SetColumnText(rowData, kKFCResultChapterLabelWidgetID, label);
	}

	// A hit row - a live one, or a kept tier's copy: the match's line into the custom colour cell (IKFCRowData's parts), no
	// expander, indented one zone deeper than its story row. `kept`: the cell disabled, which it draws one step fainter
	// (KFCColorTextView).
	void LayOutHitRow(IControlView* widget, IPanelControlData* rowData, const PMReal& shift,
		const KFCResultTree::RowLook& look, bool kept) const
	{
		// Draw our own indent: the row's content starts one expander zone right of the chapter row's text,
		// and the colour cell follows it to the row's edge.
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;
		const PMReal xStart = kRowInset + kExpanderZone + kHitExtraIndent + shift;

		// THE ROW EYE (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md) - IN THE ROW'S EXPANDER ZONE (the
		// author: the rows "sit too far right when searching - put them where they were", and the eye "in line with the
		// arrow" - a column of its own after the zone moves every row's text right by it, kept or not): a hit row has
		// no arrow of its own, and that zone is the very one its document or story row draws its arrow in (the same
		// shift, one level up), so the eye stands under that arrow and takes no room from the text. The eyeball while
		// the row is kept, nothing while it is not. A kept tier's eye is disabled: drawn as it was kept, its clicks the
		// row's (a switch to its tier - ROW-42).
		IControlView* eye = rowData->FindWidget(kKFCResultEyeWidgetID);
		if (eye != nil)
		{
			PMRect eyeFrame = eye->GetFrame();
			eyeFrame.Left(xStart - kExpanderZone);
			eyeFrame.Right(xStart);
			eye->SetFrame(eyeFrame);
			if (kept)
				eye->Disable();
			else
				eye->Enable();
			KFCResultEye::Draw(eye, look.kept, false /*invalidated: the tree draws the row right after*/);
		}

		IControlView* cell = rowData->FindWidget(kKFCResultTextWidgetID);
		if (cell == nil)
			return;
		PMRect frame = cell->GetFrame();
		// One step right of the row above (kHitCellStep) - the staircase the levels make; the eye is in the zone before it.
		frame.Left(xStart + kHitCellStep);
		frame.Right(rowRight);
		cell->SetFrame(frame);

		// Hand the row's parts to the colour cell, and invalidate it HERE: the cell's data holder
		// does not ask for a redraw (a stock cell's ITextControlData::SetString does - its invalidate
		// defaults to kTrue), and a recycled row would keep the picture of the row it used to be.
		InterfacePtr<IKFCRowData> data(cell, UseDefaultIID());
		if (data != nil)
			data->SetSegments(look.locator, look.flag, look.pre, look.match, look.post);
		// Disabled for a kept row, enabled for every other - set on every apply, as the label's is (LayOutBranchRow).
		if (kept)
			cell->Disable();
		else
			cell->Enable();
		cell->Invalidate();
	}
};

CREATE_PMINTERFACE(KFCResultListWidgetMgr, kKFCResultListWidgetMgrImpl)

void KFCResultTree::AppendCounts(PMString& label, int32 replaced, int32 hits)
{
	label.Append("  (");
	label.AppendNumber(replaced);
	label.Append("/");
	label.AppendNumber(hits);
	label.Append(")");
}

bool KFCResultTree::GetRowLook(const NodeID& node, RowLook& out)
{
	out = RowLook();
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	if (nodeID == nil || nodeID->IsRoot() || nodeID->IsGrey())
		return false;		// a grey row - a kept tier's own row or a row of its copy: KFCResultHistory::GetKeptLook
	if (nodeID->IsTierRow())
	{
		// THE LIVE TIER'S ROW (the stacked results' spec, section 2-2): the search that made the list, and the list's
		// counts as the book row reads them. At the left edge.
		out.label = KFCResultHistory::LiveTierLabel();
		out.label.SetTranslatable(kFalse);
		out.name = out.label;
		AppendCounts(out.label, KFCResults()->GetTotalReplacedCount(), KFCResults()->GetTotalHitCount());
		return true;
	}
	if (nodeID->IsHitRow())
	{
		// A HIT row: the match's line, its five parts for the colour cell. One question, not four: the strings, the
		// flags, the outcome and the accent word all come from the same hit, and the row wants all of them.
		KFCResultModel::RowDisplay row;
		if (!KFCResults()->GetHitRow(nodeID->GetChapter(), nodeID->GetHit(), row))
			return false;
		out.hit = true;
		out.shift = LevelShift() + FontShift(nodeID->GetChapter());
		// "Changed" FIRST ON A ROW THIS LIST HAS REPLACED (the author's call: nothing else on the row tells a
		// written row from one still as the search found it - without it a replaced row looks like the rest).
		// Part of the locator run - its colour, and the label a reader walks starts with it. An Undo of the
		// replace puts the row back as it was (UNDO-03, the row's copy has replaced == false), and the word goes.
		// (A row whose text is gone starts with "Missing" in the same place - the model's BuildHitLocator puts
		// that one on the locator itself; the two never meet.)
		out.locator = row.locator;
		if (row.replaced || row.special != kKFCSpecialNone)
		{
			// ("Outlined" for a row written as outlines - an object row's Create Outlines, KFCObjectOutline; a special
			// operation's own word for a row one wrote last - "Fitted", "Locked" ... - SpecialRowWord, KFCObjectSpecial.)
			PMString changed(row.special != kKFCSpecialNone ? SpecialRowWord(row.special)
				: (row.outlined ? "Outlined" : "Changed"));
			changed.SetTranslatable(kFalse);
			changed.Append(" ");
			changed.Append(row.locator);
			out.locator = changed;
		}
		out.flag = row.accentFlag;
		out.pre = row.preText;
		out.match = row.matchText;
		// A TEXT ROW WRITTEN AS OUTLINES (docs/superpowers/specs/2026-10-18-kfc-text-outlines-design.md X6): its match
		// is inline outlines now, which draw nothing in a line of text - the characters it outlined are drawn in their
		// place, so the row still says what it was.
		if (row.outlined && !row.outlinedText.IsEmpty())
			out.match = row.outlinedText;
		out.post = row.postText;
		out.kept = row.kept;
		return true;
	}
	if (nodeID->IsBookRow())
	{
		// The BOOK row: which book these results came from. Only ever built for a book search, where it
		// is the top's single child - so it is the panel's standing answer to which book is the target,
		// which a status line cannot be (one line, truncated, overwritten by the next message).
		// "<book>  (R/N)" - how many of the hits the book's search holds this list has replaced, of how many (every one
		// drawn - the search's limit is the only one; the R - AppendCounts). Drawn even when the search found
		// nothing - the hierarchy adapter gives the top one child whenever the results came from a book, which is how
		// the panel goes on naming the book it just searched - and then it reads "(0/0)", which is also how KFCBookWatch
		// describes it (the user's own measurement, of the "(0)" before the R); keep the two together.
		// No shift: the book row IS the outermost level of the list.
		out.label = KFCResults()->GetBookName();
		out.label.SetTranslatable(kFalse);
		out.name = out.label;
		AppendCounts(out.label, KFCResults()->GetTotalReplacedCount(), KFCResults()->GetTotalHitCount());
		return true;
	}
	PMString name;
	int32 fullCount = 0;
	if (nodeID->IsFontRow())
	{
		// A STORY row (the code calls the level FONT - see the head of the file): the story's first words and how
		// many rows sit under it. The same shape as a document row - an expander and a label - so it shares the
		// branch layout and the chapter row's resource, one step further right.
		if (!KFCResults()->GetFontDisplay(nodeID->GetChapter(), nodeID->GetFont(), name, fullCount))
			return false;
		// "ID260: first words...  (R/N)", the way a document row reads out its count, like the rows
		// above it: what the row holds, not what the panel drew of it. (A group that answered
		// GetFontDisplay is in range - no further test needed.)
		out.label = name;
		out.label.SetTranslatable(kFalse);
		out.name = out.label;
		AppendCounts(out.label, KFCResults()->GetFontReplacedCount(nodeID->GetChapter(), nodeID->GetFont()), fullCount);
		out.shift = LevelShift() + kFontLevelIndent;
		return true;
	}
	// A document row: its expander and "<name>  (R/N)" after the zone.
	if (!KFCResults()->GetChapterDisplay(nodeID->GetChapter(), name, fullCount))
		return false;
	// "<name>  (R/N)" - of the chapter's hits, how many this list has replaced, the same read-out the book row
	// carries (AppendCounts). About the work, not the drawing - never "(shown / total)".
	out.label = name;
	out.label.SetTranslatable(kFalse);
	out.name = out.label;		// (before the note below - the name is the document's, the note the drawing's)
	// A DOCUMENT WITH NO WINDOW. Search: = All Documents searches those too, as
	// InDesign's own does; a replace there leaves it hidden (the user may hide a heavy one on purpose) and
	// only a jump opens a window - so its row says so. Asked as the row is drawn: a jump that opens one
	// takes the note away at the next repaint. (A kept tier's copy keeps the note as it was then.) All Documents as the
	// list recorded it (IsFromAllDocuments - a text list Search Text in These Objects made from one too).
	if (KFCResults()->IsFromAllDocuments() && ChapterHasNoWindow(nodeID->GetChapter()))
		out.label.Append(" (no window)");
	AppendCounts(out.label, KFCResults()->GetChapterReplacedCount(nodeID->GetChapter()), fullCount);
	out.shift = LevelShift();
	return true;
}

bool KFCResultTree::OpensWhenDrawn(const NodeID& node)
{
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	if (nodeID == nil)
		return false;
	if (nodeID->IsBookRow() || nodeID->IsFontRow())
		return true;		// Rebuild opens the book row and every story row (it says why)
	if (nodeID->IsDocumentRow())
		return DocumentRowsOpen();
	return false;
}

IControlView* KFCResultTree::ResultListView()
{
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	return (panelData != nil) ? panelData->FindWidget(kKFCResultListWidgetID) : nil;
}

ITreeViewMgr* KFCResultTree::QueryResultTreeMgr()
{
	// Nil when the panel is closed, an ordinary state: Rebuild, RefreshRows and BeforeChapterRowGoes then do nothing.
	IControlView* const listView = ResultListView();
	return (listView != nil) ? InterfacePtr<ITreeViewMgr>(listView, UseDefaultIID()).forget() : nil;
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

	// THE KEPT TIERS THE PERSON HAS OPEN, noted before ClearTree forgets them (KFCResultHistory - the stacked results'
	// spec, section 3-4).
	KFCResultHistory::NoteOpenTiers(treeMgr);

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
	//
	// THE TIERS, while the results are kept in tiers: the kept ones as they were left - those the person had open opened,
	// and in each the rows that were open when it was kept (KFCResultHistory::OpenKeptRows) - and the live one open, its
	// rows then opened by the rules below exactly as a list with no tiers.
	const int32 keptOpened = KFCResultHistory::OpenKeptRows(treeMgr);
#ifdef KFC_DIAG
	expandCalls += keptOpened;
#else
	(void)keptOpened;
#endif
	const bool liveTier = KFCResultHistory::IsStacking() && KFCResultHistory::HasLiveList();
	// THE LIVE TIER AS IT WAS LEFT (nothing opens or closes by itself - KFCResultHistory::OpenLiveTier): its own row
	// open or closed as noted, open when nothing was; after a switch its rows too, as the tier's copy had them - and
	// then the rules below are not run over them.
	int32 liveExpands = 0;
	const bool liveRowsOpened = liveTier && KFCResultHistory::OpenLiveTier(treeMgr, liveExpands);
#ifdef KFC_DIAG
	expandCalls += liveExpands;
#endif

	const int32 chapters = KFCResults()->GetDisplayChapterCount();
	// (A test build's tree-expand mode 1 opens such a document row with everything under it in one call, and the
	// loop below then leaves its rows alone - as do a switch's rows, opened above.)
	bool chaptersOpenedWhole = liveRowsOpened;
	if (!liveRowsOpened && KFCResults()->IsFromBook())
	{
		// The book row is the root's only child, so leaving it closed would show a panel with one
		// line on it and nothing else. Open it; the chapters underneath stay closed.
		expand(KFCResultNodeID::CreateBook(), kFalse);
	}
	else if (!liveRowsOpened && DocumentRowsOpen())		// (no book here - so: not All Documents)
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
#endif

	// THE NEW RESULTS IN VIEW: a rebuild leaves the list at its top, which is the OLDEST tier while there are tiers - so the
	// live tier's row goes to the top instead, its rows under it, the kept tiers a scroll above (the stacked results' spec,
	// section 3-4). NOT AFTER A SWITCH (v2): it puts the view back where it was itself (KFCResultHistory::SwitchToKeptRow).
	const bool holdView = KFCResultHistory::TakeScrollHold();
	if (liveTier && !holdView)
		treeMgr->ScrollToNode(KFCResultNodeID::CreateLiveTier(), ITreeViewMgr::eScrollToTop);
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
	// The LIVE TIER's row the same, while the results are kept in tiers: it carries the list's counts, as the book row does.
	if (KFCResultHistory::IsStacking() && KFCResultHistory::HasLiveList())
		treeMgr->NodeChanged(KFCResultNodeID::CreateLiveTier(), kFalse /*children handled below*/);
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
	// A row the tree does not show is nothing to take out.
	if (KFCResults()->GetShownChapterPos(chapterIdx) < 0)
		return;
	treeMgr->BeforeNodeDeleted(KFCResultNodeID::Create(chapterIdx));
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowStatus - write the panel's message area
//----------------------------------------------------------------------------------------

namespace
{
// THE MESSAGE AREA'S OWN STATE. Kept in the module rather than read back off the widget: the widget is gone whenever
// the panel is closed, and the last message is written back when the panel is shown again (RestoreStatusOnPanelShow).
// One struct, emptied by one assignment at shutdown (KFCResultTree::ShutdownCleanup).
struct MessageArea
{
	// What the area shows now: the last ordinary message, or a row's own - its preview (ShowRowPreview) or its note
	// (ShowRowNote) - which DropRowPreview takes away. Set by the write that puts it there.
	enum Shown { kMessage, kRowPreview, kRowNote };

	PMString	lastStatus;		// the last thing ShowStatus was given - a row's preview or note is never kept here
	Shown		shown;

	MessageArea() : shown(kMessage) {}
};

MessageArea gArea;
}	// anonymous namespace

void KFCResultTree::ShutdownCleanup()
{
	// The statics this file keeps, emptied for the reason KFCResultModel empties its own: a PMString
	// still holding storage when the .pln unloads runs its destructor against an application that has
	// already torn itself down (the KESCL ShutdownCleanup rule). A FRESH VALUE, not Clear(): Clear() keeps the
	// string's storage (it is clear() - "Capacity stays the same", UnicodeSavvyString.h), while assigning a fresh one
	// swaps it away (PMString's move assignment), to be freed now. A field added to MessageArea goes with it.
	gArea = MessageArea();
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
    through here and decides only WHAT it says - except a preview, which writes its pieces itself (ShowRowPreview).

    NO '&' DOUBLING. The line names files the user chose; a stock StaticText takes a lone '&' as a
    keyboard accelerator ("A&B.indd" draws as "AB.indd" with the B underlined) and needs every message
    put through InsertAmpersandForDisplay. This box is drawn by hand, with convertAmpersand kFalse
    (KFCStatusTextView.cpp), so the text is the text - and a reader of the widget (KIDMCP's inspect_ui)
    reads "A&B.indd", not a doubled "A&&B.indd". !Do not double it again.

    @param forceRedraw kFalse while the panel is still being built (see RestoreStatusOnPanelShow) -
                       there is nothing on screen to force yet, and this runs mid-construction.
    @param shown       what the message is: an ordinary one, or a row's note (ShowRowNote). */
void WriteMessage(const PMString& message, bool16 forceRedraw, MessageArea::Shown shown = MessageArea::kMessage)
{
	gArea.shown = shown;		// whatever stood there is replaced by this
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
	// The two plug-in files are not one build's: the doors are stood in for (KFCModelAccess.h), so what a freshly
	// installed panel would say is untrue - say why nothing will run instead.
	if (!KFCBoundaryMatches())
	{
		PMString why(KFCBoundaryMismatchText());
		why.SetTranslatable(kFalse);
		return why;
	}
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
	if (!gArea.lastStatus.IsEmpty())
	{
		// Something ran in THIS session: put its message back. This also restores the line when the
		// panel is closed and reopened mid-session.
		WriteMessage(gArea.lastStatus, kFalse /*still being built*/);
		return;
	}

	// Nothing has run since launch, so the line says what a freshly installed panel says.
	WriteMessage(InitialMessage(), kFalse /*still being built*/);
}

void KFCResultTree::ShowStatus(const PMString& message)
{
	// Remembered FIRST, before the panel is even looked for: this has to hold whether or not there
	// is a panel to draw it on.
	gArea.lastStatus = message;
	gArea.lastStatus.SetTranslatable(kFalse);

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

void KFCResultTree::ShowStatus(const char* message)
{
	PMString sentence(message);
	sentence.SetTranslatable(kFalse);
	ShowStatus(sentence);
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowRowPreview - a GREP row's after-text on the message area
//----------------------------------------------------------------------------------------

// The heading of a preview: "Preview Text:", or with a word on how the click left the match - "Preview Text (not
// selected - locked):", "Preview Text (Overset Text):" (KFCJump's NotSelectedWords).
static const char* const kPreviewLabel = "Preview Text:";
static const char* const kPreviewLabelStem = "Preview Text";

void KFCResultTree::ShowRowNote(const PMString& note)
{
	// Not remembered as the last message (the row's, not a report - ShowRowPreview's rule) and no panel icon to settle: a
	// note reports nothing that ran. Written at once, as a report is - the click that made it is the user's.
	WriteMessage(note, kTrue /*force the redraw*/, MessageArea::kRowNote);
}

bool KFCResultTree::ShowRowPreview(int32 chapterIdx, int32 hitIdx, const char* notSelectedWhy)
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
		// (Not in front of a mark that belongs to the character before it - KFCSafeCut, the rule every shown text is cut by.)
		const int32 keep = KFCSafeCut(shown, kPreviewMaxChars);
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
	// HOW THE CLICK LEFT THE MATCH, in the heading (the author's call: the reason and the preview both): "Preview Text (not
	// selected - locked):", or what cannot be seen of one it selected - "Preview Text (Overset Text):". The words come
	// whole from the caller (KFCJump's NotSelectedWords). The heading owns its line(s) and wraps like the rest.
	PMString label(kPreviewLabel);
	if (notSelectedWhy != nil && *notSelectedWhy != '\0')
	{
		label = kPreviewLabelStem;
		label.Append(" (");
		label.Append(notSelectedWhy);
		label.Append("):");
	}
	label.SetTranslatable(kFalse);
	textData->SetSegments(label, pre, shown, post, shown.IsEmpty() ? kTrue : kFalse);
	gArea.shown = MessageArea::kRowPreview;		// what DropRowPreview takes away
	textView->ForceRedraw();
	return true;
}

void KFCResultTree::DropRowPreview()
{
	// A row with no preview (replaced, a Text search's, a branch row...) must not leave the last row's standing beside
	// it: the area goes back to the last ordinary message (a preview is never kept as that). Nor the last row's note
	// (ShowRowNote): its reason is about that row alone.
	if (gArea.shown == MessageArea::kMessage)
		return;
	WriteMessage(gArea.lastStatus.IsEmpty() ? InitialMessage() : gArea.lastStatus, kTrue /*force the redraw*/);
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
	// InDesign's holder, then the system's keyboard window beside it (DiagSystemFocus).
	std::string holder;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IKeyBoard> keyBoard(app, UseDefaultIID());
	IEventHandler* const focus = (keyBoard != nil) ? keyBoard->GetKeyFocus() : nil;	// not counted (IKeyBoard.h)
	if (keyBoard == nil)
		holder = "no-keyboard";
	else if (focus == nil)
		holder = "nobody";
	else
	{
		InterfacePtr<ITreeViewMgr> treeMgr(QueryResultTreeMgr());
		InterfacePtr<IEventHandler> treeEH(treeMgr, UseDefaultIID());
		if (treeEH != nil && treeEH.get() == focus)
			holder = "tree";
		else
		{
			InterfacePtr<IControlView> view(focus, UseDefaultIID());
			char text[96];
			::sprintf_s(text, sizeof(text), "class=0x%x widget=0x%x", static_cast<unsigned>(::GetClass(focus).Get()),
				view != nil ? static_cast<unsigned>(view->GetWidgetID().Get()) : 0u);
			holder = text;
		}
	}
	return holder + " " + DiagSystemFocus();
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
	//  3. checked - and taken by IKeyBoard::AcquireKeyFocus if the panel's doors did not land it there.
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	if (panelMgr == nil || !panelMgr->IsPanelWithWidgetIDShown(kKFCPanelWidgetID))
		return false;
	panelMgr->ShowPanelByWidgetID(kKFCPanelWidgetID, kTrue);

	IControlView* const listView = ResultListView();
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
	// The keyboard frame drawn again: the panel's doors can bring the system's keyboard back to the panel's window with
	// InDesign's holder unchanged (measured: a row clicked after Find/Change's title bar), and then nothing
	// else asks the frame again (KFCResultTreeEH's PostGetKeyFocus does it when the holder changes).
	RedrawKeyboardFrame();
	return keyBoard->GetKeyFocus() == listEH;
}

namespace
{
// IS ANY OF THESE ROWS ONE `can` SAYS YES FOR - the one walk of the questions the row menus grey their items by
// (CanReplaceAny, CanRestoreAny, CanOutlineAny, CanSpecialAny): the first yes is enough.
template <typename Can>
bool AnyRowCan(const KFCRowsByChapter& rows, Can can)
{
	for (size_t c = 0; c < rows.size(); ++c)
		for (size_t k = 0; k < rows[c].hits.size(); ++k)
			if (can(rows[c].chapter, rows[c].hits[k]))
				return true;
	return false;
}
}	// anonymous namespace

bool KFCResultTree::CanReplaceAny(const KFCRowsByChapter& rows)
{
	return AnyRowCan(rows, [](int32 chapter, int32 hit) { return KFCRuns()->CanReplaceHit(chapter, hit); });
}

bool KFCResultTree::ReplaceRows(const KFCRowsByChapter& rows, PMString* outStatus)
{
	// NOT WHILE A LANDING IS UNDER WAY (KFCJump::IsLanding - its message loop can dispatch a Return or a menu item): a write
	// inside the landing's dirty guard is marked clean when the landing ends, and the next Hide Previous Chapter sweep then
	// closes the document unsaved. Dropped without a word, as an object row's Replace always was (ReplaceObjectRow(s)).
	if (KFCJump::IsLanding())
		return false;
	int32 shownChapter = -1, shownHit = -1;
	if (!LastSelectedHit(rows, shownChapter, shownHit) || RefusedWhileRunning())
		return false;
	// Nothing to write when none can be replaced - the menu greys Replace by the same question; Return does nothing then.
	if (!CanReplaceAny(rows))
		return false;
	PMString status;
	KFC_DIAG_LOG("RETFOCUS ReplaceRows begin focus=%s", DiagKeyFocus().c_str());
	// The rows not replaced yet, before the write: the ones of them that read replaced after it are what it wrote, and their
	// documents the ones written to (the note on a hidden document below names those alone - a row left greyed is no write).
	std::vector<std::pair<int32, int32> > notYet;
	for (size_t c = 0; c < rows.size(); ++c)
		for (size_t k = 0; k < rows[c].hits.size(); ++k)
		{
			bool replaced = false, locked = false;
			if (KFCResults()->GetHitFlags(rows[c].chapter, rows[c].hits[k], replaced, locked) && !replaced)
				notYet.push_back(std::make_pair(rows[c].chapter, rows[c].hits[k]));
		}
	// OBJECT ROWS are replaced through the jump's machinery - their document fronted around the write (KFCJump);
	// text rows go straight to the model. One row takes each side's one-row door (ReplaceObjectRows hands it to
	// ReplaceObjectRow, the model's ReplaceRows to ReplaceHit).
	const bool wrote = (KFCResults()->GetHitItem(shownChapter, shownHit) != kInvalidUID)
		? KFCJump::ReplaceObjectRows(rows, shownChapter, shownHit, status)
		: KFCRuns()->ReplaceRows(rows, status);	// no prompt (the author's call)
	KFC_DIAG_LOG("RETFOCUS ReplaceRows after the write focus=%s", DiagKeyFocus().c_str());
	// A WRITE INTO A DOCUMENT THAT HAS NO WINDOW (Search: = All Documents): it goes through and nothing opens one - the
	// user may keep a heavy document hidden on purpose - so the line says what the screen cannot show. Asked once the
	// write is over (a book chapter the Replace reopened has been given its window by then).
	std::vector<int32> written;
	for (size_t r = 0; r < notYet.size() && wrote; ++r)
	{
		bool replaced = false, locked = false;
		if (KFCResults()->GetHitFlags(notYet[r].first, notYet[r].second, replaced, locked) && replaced
			&& std::find(written.begin(), written.end(), notYet[r].first) == written.end())
			written.push_back(notYet[r].first);
	}
	bool hidden = false;
	for (size_t c = 0; c < written.size() && !hidden; ++c)
		hidden = ChapterHasNoWindow(written[c]);
	if (hidden)
		status.Append(written.size() == 1 ? " The document has no window - still hidden." : " A document written has no window - still hidden.");
	// Repainted in place - or the tree rebuilt, when the write threw the results away: a refusal on a changed
	// Find/Change query clears them (KFCReplaceEngine::RefuseChangedQuery), and RefreshRows repaints only the chapters
	// the model still holds - none, so the old rows would stay drawn and answer nothing until the next search.
	if (KFCResults()->HasRun())
		RefreshRows();
	else
		Rebuild();
	KFC_DIAG_LOG("RETFOCUS ReplaceRows after the repaint focus=%s", DiagKeyFocus().c_str());
	if (!status.IsEmpty())		// (empty only from a door that refused without a word - a landing under way is turned away above)
		ShowStatus(status);
	KFC_DIAG_LOG("RETFOCUS ReplaceRows after the status focus=%s", DiagKeyFocus().c_str());
	if (outStatus != nil)
		*outStatus = status;
	return wrote;
}

namespace
{
// SEARCH ... AGAIN, THE STEPS AROUND THE WALK - one place for the story's, the document's and the list's (a tier's
// heading's). Not while a landing is under
// way (KFCJump::IsLanding): the walk reopens the chapter and hands it back - closed on the spot - which can be the very
// document the landing is giving a window; nor while a run of ours is up (RefusedWhileRunning). Then the walk; the tree
// REBUILT, not repainted, when its rows were put back - every row after them has another place, the document's another
// number of them and another order; REPAINTED when they were not: a walk turned away or cancelled once it began has had
// the rows follow their foci (KFCSearchEngine::FollowStoryFoci - a line read again, Missing put on a row whose match
// was edited), which nothing else draws; the status line either way.
template <typename Walk>
bool SearchAgain(Walk walk)
{
	if (KFCJump::IsLanding() || KFCResultTree::RefusedWhileRunning())
		return false;
	PMString status;
	const bool done = walk(status);
	if (done)
		KFCResultTree::Rebuild();
	else
	{
#ifdef KFC_DIAG
		// (Test builds only) Fault switch again-no-repaint: the rows left as drawn, the old way - the case that shows
		// what the repaint adds (sr-toend-edge-repaint-armed).
		if (!KFC_DIAG_FAULT("again-no-repaint"))
#endif
			KFCResultTree::RefreshRows();
	}
	KFCResultTree::ShowStatus(status);
	return done;
}

// A WRITE FROM THE LIST WITH NO LANDING, THE STEPS AROUND IT - one place for Create Outlines, Restore from Copy and
// the other Special Operations: not under a landing or a run (KFCJump::IsLanding, RefusedWhileRunning), nothing for
// no rows; then the write; the rows drawn again - repainted in place, or the tree rebuilt when the write threw the
// results away (ReplaceRows says when) - and the status line, when the write has words for it. True = written.
template <typename Write>
bool WriteRowsInPlace(const KFCRowsByChapter& rows, Write write)
{
	if (KFCJump::IsLanding() || KFCResultTree::RefusedWhileRunning() || KFCRowCount(rows) == 0)
		return false;
	PMString status;
	const bool wrote = write(status);
	if (KFCResults()->HasRun())
		KFCResultTree::RefreshRows();
	else
		KFCResultTree::Rebuild();
	if (!status.IsEmpty())
		KFCResultTree::ShowStatus(status);
	return wrote;
}
}	// anonymous namespace

bool KFCResultTree::SearchStoryAgain(int32 chapterIdx, int32 groupIdx)
{
	return SearchAgain([&](PMString& status) { return KFCRuns()->SearchStoryAgain(chapterIdx, groupIdx, status); });
}

bool KFCResultTree::SearchDocumentAgain(int32 chapterIdx)
{
	return SearchAgain([&](PMString& status) { return KFCRuns()->SearchDocumentAgain(chapterIdx, status); });
}

bool KFCResultTree::SearchListAgain()
{
	return SearchAgain([&](PMString& status) { return KFCRuns()->SearchListAgain(status); });
}

bool KFCResultTree::OutlineRows(const KFCRowsByChapter& rows)
{
	// Drawn again after it (WriteRowsInPlace): each written row reads Outlined and names its outlines, RefreshRows
	// keeping what the person had opened and closed.
	return WriteRowsInPlace(rows, [&](PMString& status) { return KFCRuns()->OutlineRows(rows, status); });
}

bool KFCResultTree::RestoreRows(const KFCRowsByChapter& rows)
{
	// Drawn again after it (WriteRowsInPlace): each row put back reads as before its Replace, RefreshRows keeping
	// what the person had opened and closed.
	return WriteRowsInPlace(rows, [&](PMString& status) { return KFCRuns()->RestoreRows(rows, status); });
}

bool KFCResultTree::CanRestoreAny(const KFCRowsByChapter& rows)
{
	return AnyRowCan(rows, [](int32 chapter, int32 hit) { return KFCRuns()->CanRestoreHit(chapter, hit); });
}

bool KFCResultTree::CanOutlineAny(const KFCRowsByChapter& rows)
{
	return AnyRowCan(rows, [](int32 chapter, int32 hit) { return KFCRuns()->CanOutlineHit(chapter, hit); });
}

bool KFCResultTree::SpecialRows(int32 op, const KFCRowsByChapter& rows, const KFCSpecialParams& params)
{
	// Drawn again after it (WriteRowsInPlace): each written row reads the operation's word, its place read again,
	// RefreshRows keeping what the person had opened and closed.
	return WriteRowsInPlace(rows, [&](PMString& status) { return KFCRuns()->SpecialRows(op, rows, params, status); });
}

bool KFCResultTree::CanSpecialAny(int32 op, const KFCRowsByChapter& rows)
{
	return AnyRowCan(rows, [op](int32 chapter, int32 hit) { return KFCRuns()->CanSpecialHit(op, chapter, hit); });
}

const char* KFCResultTree::SpecialRowWord(int32 op)
{
	switch (op)
	{
		case kKFCSpecialFillFrameProp:
		case kKFCSpecialFitContentProp:
		case kKFCSpecialContentAwareFit:
		case kKFCSpecialFitFrameToContent:
		case kKFCSpecialFitContentToFrame:
		case kKFCSpecialCenterContent:			return "Fitted";
		case kKFCSpecialBringToFront:
		case kKFCSpecialBringForward:
		case kKFCSpecialSendBackward:
		case kKFCSpecialSendToBack:				return "Arranged";
		case kKFCSpecialRotate90CW:
		case kKFCSpecialRotate90CCW:
		case kKFCSpecialRotate180:
		case kKFCSpecialRotate:					return "Rotated";
		case kKFCSpecialFlipHorizontal:
		case kKFCSpecialFlipVertical:			return "Flipped";
		case kKFCSpecialRedefineScaling:		return "Redefined";
		case kKFCSpecialClearTransformations:	return "Reset";
		case kKFCSpecialLock:					return "Locked";
		case kKFCSpecialUnlock:					return "Unlocked";
		case kKFCSpecialHide:					return "Hidden";
		case kKFCSpecialShow:					return "Shown";
		case kKFCSpecialMove:
		case kKFCSpecialMoveToLayer:			return "Moved";
		case kKFCSpecialScale:					return "Scaled";
		case kKFCSpecialShear:					return "Sheared";
		case kKFCSpecialClearStyleOverrides:	return "Cleared";
		case kKFCSpecialDelete:					return "Deleted";
		default:								return "";
	}
}

// End, KFCResultListWidgetMgr.cpp.
