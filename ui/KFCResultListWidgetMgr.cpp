//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  ITreeViewWidgetMgr for the result tree. Two ROW SHAPES for the node kinds KFCResultNodeID.h names:
//
//    * BRANCH rows (from kKFCResultChapterNodeWidgetRsrcID): an expander arrow and a label. The
//      BOOK row, the DOCUMENT rows ("<name>  (N)"), the RUN rows and the STORY rows (the code's FONT
//      rows - "P3  first words...  (N)") are all this one shape at different indents, so no level
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
//  A fifth kind, the RUN row (Show Changes by KohakuFindChange): on a list rebuilt from the
//  Track Changes records, one replace's rows sit under a branch row between the document and its
//  stories - "<date> <time>  (N)", the branch shape again, one step right of the document row.
//
//  The visual indent is drawn by explicit frame offsets in ApplyDataToWidget, applied on top of
//  the framework's own indent rather than instead of it (see GetIndentForNode), as in KESCL. This
//  file also hosts KFCResultTree::Rebuild (the tree lives here). Ported from KESCL's
//  KESCLResultListWidgetMgr - two levels then (document and hit; the book, story and run levels
//  came later) - itself modelled on the paneltreeview sample's PnlTrvTVWidgetMgr.
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
#include "TextChar.h"		// kTextChar_Ellipse - the mark on a cut "Source Text:"
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

namespace
{
	// Visual layout of a row (px inside the 19px row). The chapter row's cells start after the
	// expander's zone; the hit row's cell steps one more zone right (drawn by us - the framework
	// indent is off, as in KESCL).
	const PMReal kRowInset = 2.0;
	const PMReal kExpanderZone = 16.0;
	// How much further right than its chapter row a hit row's content starts. ZERO: the check box
	// begins exactly where the chapter row's expander arrow ends, so the two line up down the left
	// edge (the author's call, from a screen shot - "put the check where the arrow is").
	//
	// A full expander zone, or half of one, only leaves a gap in front of the check box that buys
	// nothing. The hierarchy is still legible without it: the chapter row's LABEL and
	// the hit row's LOCATOR are what the eye compares, and the check box's width keeps those apart.
	const PMReal kHitExtraIndent = 0.0;
	// The hit row's check box occupies this much at the start of the row's content, and the
	// colour cell starts after it.
	const PMReal kCheckZone = 16.0;
	// What that same column shrinks to when NO row of the list carries a check box: a replace's report,
	// or a list rebuilt from the records - see everyRowLostBox in ApplyHitRow, which is the one place
	// that decides. Half a check zone, so the hit rows land 8px right of the row above them: the step
	// the book and story levels already use, which makes the whole tree one even staircase (the
	// author's call - "make it a nice staircase, shifted left by the check"). (The name is from the
	// scans, since removed, whose lists had no boxes either.)
	// Half rather than all of it: giving back the full 16px would line the hit rows up with their
	// story row's LABEL, leaving no step at all between a branch and the rows under it. The full
	// zone is still kept for a WORK LIST, where some rows have a box and some do not and the
	// locators have to stay in one column (see ApplyHitRow).
	const PMReal kScanCheckZone = 8.0;
	// A BOOK row sits above the documents when the results came from a book search, and its
	// children step right by this much. 8px, not a full expander zone: the horizontal room in this
	// panel was fought for once already (see kHitExtraIndent), and half a zone is enough to read
	// the hierarchy. A document search has no book row and no shift, so its tree is unchanged.
	const PMReal kBookLevelIndent = 8.0;
	// A STORY ("font") row sits between a document and its hits, and a RUN row above it on a list rebuilt
	// from the records; the children of each step right by this much. The same 8px the book level uses,
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

		// Built the way the layer panel builds its rows (LayerPanelTreeViewWidgetMgr.cpp), in three
		// steps rather than one CreateObject, because the ORDER is the point:
		//   1. CreateObjectNoInit - make the boss but do not build its child hierarchy yet.
		//   2. SetThemeForView(kIDPanelTheme) - tell it that it is going to live in a palette. The
		//      row is created here, long before the tree hands it to the panel's window, so nothing
		//      else is going to say which theme it draws in.
		//   3. DoPostCreate - NOW build the children, with the theme already settled.
		// Doing it in one CreateObject call leaves the children built first and themed never.
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
			else if (nodeID->IsRunRow())
				this->ApplyRunRow(nodeID, node, widget, rowData);
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
		if (nodeID != nil && (nodeID->IsFontRow() || nodeID->IsRunRow()))
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
	// row every hit sits under, and the run step where there is one.
	PMReal FontShift(int32 chapterIdx) const
	{
		return kFontLevelIndent + this->RunShift(chapterIdx);
	}

	// One more step for the story and hit rows of a chapter that has RUN rows above them (Show Changes)
	// - asked from the adapter's own count, like FontShift.
	PMReal RunShift(int32 chapterIdx) const
	{
		return (KFCResults()->GetDisplayRunCount(chapterIdx) > 0) ? kFontLevelIndent : PMReal(0.0);
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
	// "  - first N shown" when the tree is drawing fewer hit rows than it holds (kKFCDisplayHitLimit),
	// on the OUTERMOST row only - the book row, or a document's row when there is no book - and once,
	// because a sentence on the status line is replaced by the next click. !A GUARD NOW: the search stops
	// collecting at the same number (kKFCCollectHitLimit, the spec map's GEN-34), so the model holds no
	// row the tree does not draw and this does not fire; it stays for the day the two limits part.
	static void AppendDisplayCapNote(PMString& label)
	{
		if (KFCResults()->GetTotalHitCount() <= KFCResultModel::kKFCDisplayHitLimit)
			return;
		label.Append("  - first ");
		label.AppendNumber(KFCResultModel::kKFCDisplayHitLimit);
		label.Append(" shown");
	}

	void ApplyBookRow(const NodeID& node, IControlView* widget,
		IPanelControlData* rowData) const
	{
		// "<book>  (N/M checked)" - how many of this book's hits are ticked, out of all of them
		// (the author's wording). The row a Check All over the book acts on is this row, so what it
		// did is answered in the same place it was asked.
		//
		// Both numbers count every stored hit - what Check All ticks and what a replace would rewrite.
		//
		// ONLY ON A LIST THAT HAS BOXES, AND ONLY ON A LIST THAT HAS ROWS. A replace's report and a list
		// rebuilt from the records have no boxes at all, so "checked" is a word about nothing there and
		// the count is 0 by definition ("(0/120 checked)"). Those lists go back to the plain total, which
		// is what this row says when there is no work to offer.
		//
		// AND A BOOK SEARCH THAT FOUND NOTHING IS THE SAME SENTENCE ABOUT NOTHING. This row is drawn
		// even when the search found no hits at all - the hierarchy adapter gives the root one child
		// whenever the results came from a book, which is deliberate: it is how the panel goes on
		// naming the book it just searched. With no hits and a Find/Change kind NoRowHasCheckBox is
		// false, so without the hit-count test the row would read "book.indb  (0/0 checked)". It reads
		// "(0)" - which is also how KFCBookWatch describes it (the user's own measurement); keep the
		// two together.
		//
		// M counts LOCKED hits too, though Check All cannot tick them (RowHasCheckBox turns them
		// away) - so a fully checked chapter of locked-and-free hits reads short of its own total on
		// purpose. The alternative, a denominator that leaves them out, would disagree with the hit
		// count every other part of the panel reports.
		PMString label(KFCResults()->GetBookName());
		label.SetTranslatable(kFalse);
		label.Append("  (");
		if (KFCResults()->NoRowHasCheckBox() || KFCResults()->GetTotalHitCount() == 0)
		{
			label.AppendNumber(KFCResults()->GetTotalHitCount());
			label.Append(")");
		}
		else
		{
			label.AppendNumber(KFCResults()->GetCheckedCount());
			label.Append("/");
			label.AppendNumber(KFCResults()->GetTotalHitCount());
			label.Append(" checked)");
		}
		AppendDisplayCapNote(label);
		// No shift: the book row IS the outermost level.
		this->LayOutBranchRow(node, widget, rowData, PMReal(0.0), label);
	}

	// A document row: its expander and "<name>  (N)" after the zone.
	void ApplyChapterRow(const TreeNodePtr<KFCResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, IPanelControlData* rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KFCResults()->GetChapterDisplay(nodeID->GetChapter(), name, fullCount))
			return;

		// "<name>  (N/M checked)" - the same read-out the book row carries, for this chapter alone
		// (the author's wording). A Check All over a DOCUMENT row means that chapter, so this is
		// where its answer belongs.
		//
		// Both numbers are about the work, not the drawing - never "(shown / total)", which is the
		// panel talking about ITSELF. How many rows are drawn is one note on the OUTERMOST row,
		// outside the brackets (AppendDisplayCapNote).
		//
		// And, exactly as on the book row, only where there are boxes to count: a list with none falls
		// back to the plain total. See ApplyBookRow for the whole of it.
		//
		// The book row's OTHER fall-back - an empty result set - has no case here and gets no test:
		// a chapter is only ever appended with at least one hit (KFCResultModel::AppendChapter), so
		// a document row with fullCount 0 does not exist. A gate nothing can reach reads as a case
		// that happens.
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
		label.Append("  (");
		if (KFCResults()->NoRowHasCheckBox())
		{
			label.AppendNumber(fullCount);
			label.Append(")");
		}
		else
		{
			label.AppendNumber(KFCResults()->GetChapterCheckedCount(nodeID->GetChapter()));
			label.Append("/");
			label.AppendNumber(fullCount);
			label.Append(" checked)");
		}
		// A document's results have no book row above this one, so the note goes here instead.
		if (!KFCResults()->IsFromBook())
			AppendDisplayCapNote(label);

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

		// "P3  first words...  (N/M checked)", the way a document row reads out its count, like the rows
		// above it: what the row holds, not what the panel drew of it. (A group that answered
		// GetFontDisplay is in range - no further test needed.)
		PMString label(name);
		label.SetTranslatable(kFalse);
		label.Append("  (");
		if (!KFCResults()->NoRowHasCheckBox())
		{
			label.AppendNumber(KFCResults()->GetGroupCheckedCount(nodeID->GetChapter(), nodeID->GetFont()));
			label.Append("/");
			label.AppendNumber(fullCount);
			label.Append(" checked)");
		}
		else
		{
			label.AppendNumber(fullCount);
			label.Append(")");
		}
		this->LayOutBranchRow(node, widget, rowData,
			this->LevelShift() + kFontLevelIndent + this->RunShift(nodeID->GetChapter()), label);
	}

	// A RUN row (Show Changes by KohakuFindChange): one replace's rows, "<date> <time>  (N)" -
	// the branch shape one step right of its document row. No checked count: a list rebuilt from the
	// records has no boxes.
	void ApplyRunRow(const TreeNodePtr<KFCResultNodeID>& nodeID, const NodeID& node,
		IControlView* widget, IPanelControlData* rowData) const
	{
		PMString name;
		int32 fullCount = 0;
		if (!KFCResults()->GetRunDisplay(nodeID->GetChapter(), nodeID->GetRun(), name, fullCount))
			return;
		PMString label(name);
		label.SetTranslatable(kFalse);
		label.Append("  (");
		label.AppendNumber(fullCount);
		label.Append(")");
		this->LayOutBranchRow(node, widget, rowData, this->LevelShift() + kFontLevelIndent, label);
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

		// Reasons a row has NOTHING to select. THE MODEL ANSWERS ALL OF THEM IN ONE FIELD
		// (row.hasCheckBox = RowHasCheckBox), and what follows is the list of what that covers -
		// not a second copy of the rule (the model's is the one that decides).
		//   replaced - it has been changed already, and cannot be changed again
		//   locked   - InDesign gives no way to change locked content, so a box would offer an
		//              action that quietly does nothing (the locator says locked)
		//   outcome  - the row already carries a reason it was left alone (its accent word)
		//   report   - the panel is showing the aftermath of a replace, where only the rows taken back
		//              are selectable - or a list rebuilt from the records, where none is. This is the
		//              one that catches the rows carrying no reason at all: a chapter that could not be
		//              opened. It is a property of the list rather than of the row.
		//
		// Asked as its own question first because it says something the per-row tests cannot: NO row
		// in this list has a box, which is a property of the WHOLE list. That is what makes it safe
		// to narrow the column in front of the locators for every row at once (see the cell's frame
		// below) - nothing is left ragged, because there is nothing left to line up with. A work list
		// is the case that has to keep the full zone: it mixes rows that have a box with rows that do
		// not, and those locators have to stay in one column. The question lives in
		// KFCResultModel::NoRowHasCheckBox - the branch rows above ask it to decide whether "checked"
		// is a word their label may use at all, and three rows of one tree must not disagree about it.
		//
		// Asked HERE as well as through row.hasCheckBox because the two want different things: the
		// row wants to know whether IT has a box, and the cell's frame below wants to know whether
		// the WHOLE LIST has none (only then may the column move).
		const bool everyRowLostBox = KFCResults()->NoRowHasCheckBox();
		const bool noCheckBox = !row.hasCheckBox;

		// Draw our own indent: the check box sits where the hit row's content starts (one expander
		// zone right of the chapter row's text), and the colour cell follows it to the row's edge.
		const PMReal rowRight = widget->GetFrame().Width() - kRowInset;
		const PMReal xStart = kRowInset + kExpanderZone + kHitExtraIndent
			+ this->LevelShift() + this->FontShift(nodeID->GetChapter());

		// The check box. A row with nothing to select loses it completely; the space it would have
		// taken is left empty rather than reclaimed, so the locators stay in one column (see the
		// cell's frame below). Hiding alone would not be enough - a hidden widget still takes
		// clicks - so it is disabled as well.
		IControlView* checkView = rowData->FindWidget(kKFCResultCheckWidgetID);
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
			// through KFCResultCheckObserver as a phantom click and overwrite the model with
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
			// (A footnote's row is ticked by hand like any other - only its Reject Change and Accept Change
			// stay off, since Track Changes records nothing in a footnote.)
			checkView->Enable();
		}

		IControlView* cell = rowData->FindWidget(kKFCResultTextWidgetID);
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
			// Rows without a box reclaiming those 16px read as a ragged left edge once a search turns
			// up a lot of locked hits (the author's call, from a screen shot). A column that does not
			// move is worth more than the width.
			//
			// A list where NO row has a box is the case where the column can move, because it moves
			// for every row at once and nothing is left ragged: a replace's report (every row lost its
			// box together) or a list rebuilt from the records. There it keeps half the zone
			// (kScanCheckZone) instead of all of it, which steps the hit rows off the row above by the
			// same 8px the levels use rather than sinking them a full check box deeper than anything
			// else in the tree.
			frame.Left(xStart + (everyRowLostBox ? kScanCheckZone : kCheckZone));
			frame.Right(rowRight);
			cell->SetFrame(frame);

			// Hand the row's parts to the colour cell, and invalidate it HERE: the cell's data holder
			// does not ask for a redraw (a stock cell's ITextControlData::SetString does - its invalidate
			// defaults to kTrue), and a recycled row would keep the picture of the row it used to be.
			InterfacePtr<IKFCRowData> data(cell, UseDefaultIID());
			if (data != nil)
				data->SetSegments(row.locator, row.accentFlag, row.preText, row.matchText, row.postText);
			cell->Invalidate();
		}
	}
};

CREATE_PMINTERFACE(KFCResultListWidgetMgr, kKFCResultListWidgetMgrImpl)

namespace
{
// The panel's result tree, reached through the panel - nil when the panel is closed, which is an ordinary
// state: Rebuild, RefreshRows, RefreshCheckedCounts and BeforeChapterRowGoes then do nothing.
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
	// TEST BUILDS ONLY (2026-10-05, docs/ai-notes/kfc-speedup-ideas-2026-10-05.md): how long the rebuild takes and how
	// many rows it opens one by one (TREETIME) - and, with the fault switch tree-expand, the same rows opened another way:
	// mode 1 = one ExpandNode with all its descendants for a document row that opens; mode 2 = every row opened BEFORE
	// ChangeRoot (ITreeViewMgr.h: expansion is kept across ChangeRoot while the root is the same).
	KFC_CLOCK(cTree);
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
		// ...and the RUN rows above them the same (Show Changes): a grouping, not a hiding place.
		const int32 runs = KFCResults()->GetDisplayRunCount(c);
		for (int32 r = 0; r < runs; ++r)
			expand(KFCResultNodeID::CreateRun(c, r), kFalse);
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
		KFC_DIAG_LOG("TREETIME hits=%d chapters=%d expands=%d mode=%d %.0f ms", (int)KFCResults()->GetTotalHitCount(),
			(int)chapters, (int)expandCalls, expandMode, tTree);
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
	// children, so refreshing the chapters does NOT reach the row above them. It carries
	// "(N/M checked)", so it goes stale the moment anything is ticked -
	// which is exactly what this function is called for. Only drawn on a book search; NodeChanged
	// on a node the tree does not hold is harmless.
	if (KFCResults()->IsFromBook())
		treeMgr->NodeChanged(KFCResultNodeID::CreateBook(), kFalse /*children handled below*/);

	// The chapter AND each of its story rows, because childrenChangedAlso reaches a node's children -
	// and under the story level the hit rows are GRANDchildren. A chapter has a few stories with hits,
	// not a few thousand, so this stays a handful of calls.
	const int32 chapters = KFCResults()->GetDisplayChapterCount();
	for (int32 n = 0; n < chapters; ++n)
	{
		const int32 c = KFCResults()->GetShownChapter(n);	// (chapter n, but for an emptied one before it)
		treeMgr->NodeChanged(KFCResultNodeID::Create(c), kTrue /*childrenChangedAlso*/);
		// the run rows: the story rows' parents there, so the chapter's call stops at them
		const int32 runs = KFCResults()->GetDisplayRunCount(c);
		for (int32 r = 0; r < runs; ++r)
			treeMgr->NodeChanged(KFCResultNodeID::CreateRun(c, r), kTrue /*childrenChangedAlso*/);
		const int32 fonts = KFCResults()->GetDisplayFontCount(c);
		for (int32 f = 0; f < fonts; ++f)
			treeMgr->NodeChanged(KFCResultNodeID::CreateFont(c, f), kTrue /*childrenChangedAlso*/);
	}
}

//----------------------------------------------------------------------------------------
// KFCResultTree::RefreshCheckedCounts - repaint only the rows that read out a checked count
//----------------------------------------------------------------------------------------

void KFCResultTree::RefreshCheckedCounts(int32 chapterIdx)
{
	InterfacePtr<ITreeViewMgr> treeMgr(QueryResultTreeMgr());
	if (treeMgr == nil)
		return;

	// The rows that read out a count, and childrenChangedAlso is kFalse for each. Ticking one box changes
	// what the book row, that chapter's row and its story rows read out and NOTHING else: the box that
	// was clicked draws itself, and every other hit row is unaffected. RefreshRows would repaint every chapter and every story
	// row in the panel to say the same thing.
	if (KFCResults()->IsFromBook())
		treeMgr->NodeChanged(KFCResultNodeID::CreateBook(), kFalse);
	if (chapterIdx >= 0)
	{
		treeMgr->NodeChanged(KFCResultNodeID::Create(chapterIdx), kFalse);
		// ...and its STORY rows, which read out a checked count too
		const int32 groups = KFCResults()->GetDisplayFontCount(chapterIdx);
		for (int32 g = 0; g < groups; ++g)
			treeMgr->NodeChanged(KFCResultNodeID::CreateFont(chapterIdx, g), kFalse);
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

// THE "Source Text:" A SELECTED REPLACED ROW PUT UP (ShowRowsBefore). Its pieces, and
// whether it is standing - kept beside gLastStatus for the same reason: the panel can be closed and
// shown again while it stands. It stands OVER the last message rather than replacing it, so that
// selecting a row that has no "before" puts that message back (DropBefore); gLastStatus is not touched.
static bool gShowingBefore = false;
static PMString gBeforePre;
static PMString gBeforeOriginal;
static PMString gBeforePost;

// WHY A ROW'S RIGHT-CLICK MENU IS GREY (ShowRowMenuReason). One more layer, over
// the "Source Text:" or the last message: neither of those is touched while it stands, so the next right-click
// that has nothing to say - or a selection (DropBefore) - puts back exactly what it covered.
static bool gShowingReason = false;
static PMString gReason;

void KFCResultTree::ShutdownCleanup()
{
	// The statics this file keeps, emptied for the reason KFCResultModel empties its own: a PMString
	// still holding storage when the .pln unloads runs its destructor against an application that has
	// already torn itself down (the KESCL ShutdownCleanup rule). When a static is added above, it is
	// added here too.
	gLastStatus.Clear();
	gShowingBefore = false;
	gBeforePre.Clear();
	gBeforeOriginal.Clear();
	gBeforePost.Clear();
	gShowingReason = false;
	gReason.Clear();
}

namespace
{

/** Put these pieces on the panel's message area (IKFCStatusTextData.h says what each is). Does nothing
    when the panel is closed, which is an ordinary state. Shared by every writer below, so they all
    reach the box the same way; they decide only WHAT it says.

    NO '&' DOUBLING. The line names files the user chose; a stock StaticText takes a lone '&' as a
    keyboard accelerator ("A&B.indd" draws as "AB.indd" with the B underlined) and needs every message
    put through InsertAmpersandForDisplay. This box is drawn by hand, with convertAmpersand kFalse
    (KFCStatusTextView.cpp), so the text is the text - and a reader of the widget (KIDMCP's inspect_ui)
    reads "A&B.indd", not a doubled "A&&B.indd". !Do not double it again.

    @param forceRedraw kFalse while the panel is still being built (see RestoreStatusOnPanelShow) -
                       there is nothing on screen to force yet, and this runs mid-construction. */
void WriteStatusWidget(const PMString& label, const PMString& pre, const PMString& mid,
	const PMString& post, bool16 wantCaret, bool16 forceRedraw)
{
	// Reach the box through the panel; nil when the panel is closed (do nothing then) - the same reach
	// Rebuild uses, which is why this lives here rather than in the action component.
	InterfacePtr<IPanelControlData> panelData(Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	if (panelData == nil)
		return;
	IControlView* textView = panelData->FindWidget(kKFCStaticTextWidgetID);
	if (textView == nil)
		return;
	InterfacePtr<IKFCStatusTextData> textData(textView, UseDefaultIID());
	if (textData == nil)
		return;

	textData->SetSegments(label, pre, mid, post, wantCaret);

	// The pieces are not something the view watches, so it is told to repaint - and, for a report, made
	// to repaint NOW (ShowStatus says why): an invalidated view waits for the next event loop (memory
	// statictext-widget-immediate-update).
	// ONE call for each case: ForceRedraw with no region draws the whole view now ("Redraws the invalid
	// region directly", IControlView.h:281-286), so an Invalidate in front of it asks for nothing more.
	if (forceRedraw)
		textView->ForceRedraw();
	else
		textView->Invalidate();
}

/** An ordinary message: the sentence alone, in the theme's text colour - what the stock widget drew. */
void WriteMessage(const PMString& message, bool16 forceRedraw)
{
	const PMString kNothing;
	WriteStatusWidget(kNothing, kNothing, message, kNothing, kFalse, forceRedraw);
}

/** The "Source Text:" that is standing (ShowRowsBefore): the heading, then the row's line with the text the
    replace took in the middle - or the bar, when it took nothing (an insertion). */
void WriteBefore(bool16 forceRedraw)
{
	// "Source Text:", KCM's word (the author's call). KCM's
	// message area says the same thing in the same place: the row shows the newer side, the box the older
	// one. Here the older side is what Track Changes holds as the deletion - what Reject brings back.
	PMString label("Source Text:");
	label.SetTranslatable(kFalse);
	WriteStatusWidget(label, gBeforePre, gBeforeOriginal, gBeforePost,
		gBeforeOriginal.IsEmpty() ? kTrue : kFalse, forceRedraw);
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
	//
	// A right-click menu's reason (ShowRowMenuReason) does not come back with the panel: it was about the row
	// a menu was popped over, and that moment has passed. What it covered is written below as ever.
	gShowingReason = false;
	gReason.Clear();
	if (gShowingBefore)
	{
		// A selected replaced row's "Source Text:" was standing when the panel went away: it comes back
		// with the panel, the way the message under it would have.
		WriteBefore(kFalse /*still being built*/);
		return;
	}
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

	// A new message takes the place of a standing "Source Text:": it reports something that has happened
	// since, and a jump that fails says why through here - never under an old row's text. And of a standing
	// right-click reason, for the same reason.
	gShowingBefore = false;
	gShowingReason = false;
	gReason.Clear();

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
// KFCResultTree::ShowRowsBefore / DropBefore - a replaced row's text as it was before the replace
//----------------------------------------------------------------------------------------

void KFCResultTree::ShowRowsBefore(int32 chapterIdx, const std::vector<int32>& rows)
{
	PMString pre, original, post;
	if (!KFCResults()->GetRowsBefore(chapterIdx, rows, pre, original, post))
	{
		// Not a replaced row (or not a row): nothing to show before it, and an older row's "Source Text:"
		// must not stand beside this one.
		DropBefore();
		return;
	}

	// CUT LONG BEFORE THE BOX HAS TO MEASURE IT. The original text is the WHOLE match (a GREP
	// across paragraphs, a format-only search: a story's worth), where a row's own is capped at 50
	// characters for drawing (KFCSearchEngine's kKFCMaxLineChars) - and the box lays its text out by
	// measuring prefixes, again for every width it tries (KFCStatusTextView.cpp), on every repaint. It
	// holds four lines, about 120 characters on a Japanese UI; past that the view ends it in an ellipsis
	// anyway, so the tail is cut here, marked the same way.
	const int32 kBeforeMaxChars = 300;
	if (original.CharCount() > kBeforeMaxChars)
	{
		// Not through the middle of a surrogate pair (the doubt KFCStatusTextView's KFCSafeCut carries).
		int32 keep = kBeforeMaxChars;
		const uint32 at = original.GetChar(keep).GetValue();
		if (at >= 0xDC00 && at <= 0xDFFF)
			--keep;
		original.Truncate(original.CharCount() - keep);
		original.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}

	// The breaks as marks - the pilcrow and the return arrow a hit row draws (the same function). A raw
	// CR here would be taken by the box as a line break, and "which characters were replaced" would
	// lose the one that was a paragraph's end.
	KFCResults()->MarkUpBreaksForDisplay(pre);
	KFCResults()->MarkUpBreaksForDisplay(original);
	KFCResults()->MarkUpBreaksForDisplay(post);

	gBeforePre = pre;			gBeforePre.SetTranslatable(kFalse);
	gBeforeOriginal = original;	gBeforeOriginal.SetTranslatable(kFalse);
	gBeforePost = post;			gBeforePost.SetTranslatable(kFalse);
	gShowingBefore = true;
	gShowingReason = false;		// the newly selected row's text takes a right-click reason's place
	gReason.Clear();

	// The illustration is not settled here: nothing has run, and it follows what runs (ShowStatus).
	WriteBefore(kTrue /*force the redraw*/);
}

void KFCResultTree::DropBefore()
{
	if (!gShowingBefore)
	{
		// Nothing standing - but a right-click reason over the last message is about a row the user has
		// now moved away from by selecting another one: it goes too.
		DropRowMenuReason();
		return;
	}
	gShowingBefore = false;
	gBeforePre.Clear();
	gBeforeOriginal.Clear();
	gBeforePost.Clear();
	gShowingReason = false;		// a reason over the "Source Text:" goes with it
	gReason.Clear();

	// Back to what the panel said before the row was selected - the last message, untouched by the
	// "Source Text:" (or, with nothing run this session, the opening one).
	WriteMessage(gLastStatus.IsEmpty() ? InitialMessage() : gLastStatus, kTrue /*force the redraw*/);
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowRowMenuReason / DropRowMenuReason - why a row's right-click menu is grey
//----------------------------------------------------------------------------------------

void KFCResultTree::ShowRowMenuReason(const PMString& reason)
{
	gReason = reason;
	gReason.SetTranslatable(kFalse);
	gShowingReason = true;
	// Drawn now, before the menu (or, when every item is grey, instead of it). Neither the last message nor a
	// standing "Source Text:" is touched, and the illustration does not move: nothing has run.
	WriteMessage(gReason, kTrue /*force the redraw*/);
}

void KFCResultTree::DropRowMenuReason()
{
	if (!gShowingReason)
		return;		// nothing standing: what is under it is already what the box shows
	gShowingReason = false;
	gReason.Clear();
	if (gShowingBefore)
		WriteBefore(kTrue /*force the redraw*/);
	else
		WriteMessage(gLastStatus.IsEmpty() ? InitialMessage() : gLastStatus, kTrue /*force the redraw*/);
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowCheckAllStatus - what Check All / Uncheck All just did, and to which row
//----------------------------------------------------------------------------------------

void KFCResultTree::ShowCheckAllStatus(const PMString& targetName, bool nowChecked)
{
	if (KFCResults()->GetTotalHitCount() == 0)
		return;		// no results: leave whatever the search left on the line

	// "<name>  all checked" - the row's own name first, spaced the way the tree spaces its label
	// from its count, so the line reads as an echo of the row that was clicked.
	//
	// The NAME is what matters here and the counts are deliberately left out: the row itself reads
	// "(N/M checked)", and this line exists to answer "which one did I just do that to?" - the same
	// two commands mean one chapter or the whole book depending on where the menu was popped, and
	// that is the part the panel cannot show afterwards.
	PMString msg(targetName);
	msg.SetTranslatable(kFalse);
	msg.Append(nowChecked ? "  all checked" : "  all unchecked");
	KFCResultTree::ShowStatus(msg);
}

//----------------------------------------------------------------------------------------
// KFCResultTree::ShowHitCheckStatus - one box, named by the row's own locator
//----------------------------------------------------------------------------------------

void KFCResultTree::ShowHitCheckStatus(const PMString& locator, bool nowChecked)
{
	// Same shape as the Check All line above, one row narrower: what was clicked, then what it now
	// is. The locator is what the row LEADS with, so the two read as the same thing said twice -
	// which is the point, since the row that changed may be anywhere in a long list.
	PMString msg(locator);
	msg.SetTranslatable(kFalse);
	msg.Append(nowChecked ? "  checked" : "  unchecked");
	KFCResultTree::ShowStatus(msg);
}

// End, KFCResultListWidgetMgr.cpp.
