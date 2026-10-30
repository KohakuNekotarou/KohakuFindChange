//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The results kept in tiers - see KFCResultHistory.h. "v2 R<n>" below names an item of the tier switch's
//  revised spec, docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICallbackTimer.h"				// StartTimer / StopTimer - a click's switch, run at the next idle
#include "IControlView.h"
#include "IPanelControlData.h"			// the tree's row widgets - the row at the top of the view (ShownTop)
#include "ITreeNodeIDData.h"			// ...each row widget's NodeID
#include "ITreeViewController.h"		// DeselectAll / Select - the row switched to, selected
#include "ITreeViewHierarchyAdapter.h"	// the rows above the row switched to, opened (SelectAloneInView)
#include "ITreeViewMgr.h"				// IsNodeExpanded / ExpandNode - the kept tiers opened as they were

// General includes:
#include "CreateObject.h"				// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)
#include <utility>
#include <vector>

// Project includes:
#include "KFCResultHistory.h"
#include "KFCAppBarSearchEnter.h"	// KFCFindChangeQueryWords / ChangeWords - a tier's name
#include "KFCDiag.h"				// the STACK line and the UIOBS counters - test builds only
#include "KFCJump.h"				// IsLanding - the switch's door; ActivateNode - the row switched to, landed on
#include "KFCModelAccess.h"			// the model half, through its session interfaces
#include "KFCPanelTitle.h"			// Update - the tab's scope written before the search
#include "KFCResultNodeID.h"
#include "KFCTextCut.h"				// KFCSafeCut - where the tier name's words may be cut
#include "FileUtils.h"				// IsEqual - the query the dialog's Find searched last, by its file

namespace
{
	// A tier's name keeps this many characters of each side of the query: a GREP pattern can run to thousands, and the row
	// ends in "..." where the panel's width runs out in any case.
	const int32 kLabelWordsMax = 200;

	/** A ROW OF A KEPT TIER: its look as it was drawn, where it hangs, whether it was open - and the live row it was copied
	    from. */
	struct KeptRow
	{
		KFCResultTree::RowLook	look;
		int32					parent;		// the row above it in the copy; -1 = the tier's own row
		int32					pos;		// its place under that row
		bool					open;		// a branch row the list had open when it was kept
		std::vector<int32>		children;	// the rows under it, in order
		NodeID					live;		// the row of the live list it was copied from (KFCResultNodeID - the model's chapter,
											// story group and hit numbers, which the tier's list keeps as they were): the row a
											// click on this copy is switched to (SwitchToKeptRow)

		KeptRow() : parent(-1), pos(0), open(false) {}
	};

	/** A KEPT TIER: the list it copies, its name, what its row reads, its copy, and whether the person has it open. */
	struct KeptTier
	{
		uint32					set;		// the model's result set of the list it copies - the number the model keeps the tier
											// under (KFCTierStore): what the copies follow, and what a switch asks for
		PMString				name;		// the name a search here gave that list (Find's label) - empty when none
											// did (a query run's): the switch's third check reads the query against it
		PMString				label;		// "<name>  (R/N)" as the tier's row read when it was kept
		PMString				head;		// ...its "<name>" alone (LiveTierLabel then): KeptChapterGone writes
											// the counts after it again when a closed document's rows go
		std::vector<KeptRow>	rows;
		std::vector<int32>		top;		// the rows under the tier's own row, in order
		bool					open;		// the tier's row open - noted before each rebuild (NoteOpenTiers)
		bool					holdsObjects;	// its rows are object rows (IKFCResults::ListHoldsObjects when it was kept) -
												// its heading's Narrow Down greys a text-only condition and
												// Search Text in These Objects is grey over text rows
												// (TierHoldsObjects)
		bool					holdsQRCodes;	// ...QR codes (IKFCResults::ListIsQRCodes when it was kept) - its heading's
												// Narrow Down and Search Text in These Objects grey (TierHoldsQRCodes -
												// QR-18)
		bool					holdsRunList;	// ...a query run's list (IKFCResults::ListIsRunList when it was kept) - its heading's
												// Narrow Down and Search These Results Again grey (TierHoldsRunList)

		KeptTier() : set(0), open(false), holdsObjects(false), holdsQRCodes(false), holdsRunList(false) {}
	};

	/** THE LIVE TIER'S NAME and the list it names: the model's result set it was given to (0 = none - the model counts its
	    lists from 1) and whether a search that keeps made it - a tier itself. */
	struct LiveTier
	{
		PMString	label;
		uint32		set;
		bool		stacked;

		LiveTier() : set(0), stacked(false) {}
	};

	std::vector<KeptTier> gKept;		// the copies, in the model's order - the oldest first (FollowModelTiers)
	LiveTier gLive;
	IDFile gLastFound;					// the query dialog's Find: the query it searched last (QueryJustFound)
	uint32 gLastFoundSet = 0;			// ...and the list that search made (0 = none)
	std::vector<uint32> gDrawnSets;		// the kept tiers the last rebuild drew, by result set, in the tree's order:
										// NoteOpenTiers finds each where it stands now (a tier gone since is passed over)

	// A CLICK'S SWITCH, BOOKED (SwitchToKeptRowLater) and run by gSwitchTimer at the next idle.
	ICallbackTimer* gSwitchTimer = nil;
	uint32 gBookedTier = 0;		// the tier clicked, by its result set (0 = nothing booked - the model counts its lists from 1)
	int32 gBookedRow = -1;		// the row clicked, in that tier's copy
	uint32 gBookedOn = 0;		// the live list's result set at the click: a list made anew since lets the booking go
	bool gShutDown = false;		// once ShutdownCleanup has run, nothing is booked again

	/** THE LIVE TIER'S OPEN STATE, NOTED FOR THE NEXT REBUILD (v2 R6 - nothing opens or closes by itself): its own row, while
	    the list drawn last is the live one still (NoteOpenTiers); after a switch, its rows too - as the tier's copy had them
	    (SwitchToKeptRow). Used once (OpenLiveTier). */
	struct LiveOpen
	{
		uint32				set;		// the list it was noted for (0 = nothing noted)
		bool				tierOpen;	// the live tier's own row
		bool				rowsNoted;	// its rows too - otherwise they open by Rebuild's rules
		std::vector<NodeID>	open;		// the rows open, when rowsNoted (each a book, document or story row of the list)

		LiveOpen() : set(0), tierOpen(true), rowsNoted(false) {}
	};
	LiveOpen gLiveOpen;
	uint32 gDrawnLive = 0;		// the live list the last rebuild drew as the live tier (0 = none) - NoteOpenTiers' question
	bool gScrollHold = false;	// a switch's rebuild leaves the view where it is (TakeScrollHold)

	// Is the list the model holds now the one a search that keeps made?
	bool LiveIsStacked()
	{
		// A QUERY RUN'S LIST IS A TIER BY ITSELF, ITS NAME THE LIST'S OWN (LiveTierLabel - the run list spec's R7): gLive goes on naming the
		// list the run replaced, whose name its Ctrl+Z needs (DropStackIfRemade).
		return KFCResults()->ListIsRunList() || (gLive.stacked && gLive.set != 0 && gLive.set == KFCResults()->GetResultSetId());
	}

	// The rows under `row` of kept tier `tier` (-1 = the tier's own row); nil when either is not there.
	const std::vector<int32>* ChildrenOf(int32 tier, int32 row)
	{
		if (tier < 0 || tier >= static_cast<int32>(gKept.size()))
			return nil;
		const KeptTier& kept = gKept[static_cast<size_t>(tier)];
		if (row < 0)
			return &kept.top;
		if (row >= static_cast<int32>(kept.rows.size()))
			return nil;
		return &kept.rows[static_cast<size_t>(row)].children;
	}

	// Row `row` of kept tier `tier`; nil when either is not there.
	const KeptRow* RowOf(int32 tier, int32 row)
	{
		if (tier < 0 || tier >= static_cast<int32>(gKept.size()) || row < 0)
			return nil;
		const KeptTier& kept = gKept[static_cast<size_t>(tier)];
		return (row < static_cast<int32>(kept.rows.size())) ? &kept.rows[static_cast<size_t>(row)] : nil;
	}

	// The copy of list `set` (a result set): its place among the kept tiers, -1 when no copy has it - a tier gone
	// since (the cap, a switch, Clear These Results). Each list has one copy at most: the copies follow the model's
	// tiers, each of them taken once (FollowModelTiers).
	int32 TierOfSet(uint32 set)
	{
		for (size_t k = 0; k < gKept.size(); ++k)
			if (gKept[k].set == set)
				return static_cast<int32>(k);
		return -1;
	}

	// THE ROWS UNDER `from`, COPIED into `kept` under its row `parent` (-1 = the tier's own row): each row, then the rows under
	// it - the walk the list draws (KFCResultTree::LiveRowCount / LiveRow), each row's look the list's own (GetRowLook) - and
	// whether it is open: asked of the tree while the panel shows it (`treeMgr`), by the rebuild's rule while it does not
	// (OpensWhenDrawn). Each copy keeps the live row it was made from.
	void CopyRows(ITreeViewMgr* treeMgr, const NodeID& from, int32 parent, KeptTier& kept)
	{
		const int32 count = KFCResultTree::LiveRowCount(from);
		for (int32 i = 0; i < count; ++i)
		{
			const NodeID child(KFCResultTree::LiveRow(from, i));
			if (!child.IsValid())
				continue;
			KeptRow row;
			if (!KFCResultTree::GetRowLook(child, row.look))
				continue;
			row.parent = parent;
			std::vector<int32>& siblings = (parent >= 0) ? kept.rows[static_cast<size_t>(parent)].children : kept.top;
			row.pos = static_cast<int32>(siblings.size());
			if (!row.look.hit)
				row.open = (treeMgr != nil) ? (treeMgr->IsNodeExpanded(child) != kFalse) : KFCResultTree::OpensWhenDrawn(child);
			row.live = child;
			const int32 index = static_cast<int32>(kept.rows.size());
			// Into its parent's list BEFORE the row goes into kept.rows: `siblings` may be a row of kept.rows, which the push
			// below can move.
			siblings.push_back(index);
			const bool branch = !row.look.hit;
			kept.rows.push_back(std::move(row));
			if (branch)
				CopyRows(treeMgr, child, index, kept);
		}
	}

	// THE LIVE LIST COPIED AS A TIER - before a search can throw it away (Find) or a switch puts it aside (SwitchToKeptRow):
	// its row's text and its rows, under its result set and the name a search here gave it. false = no live list to copy.
	// WHETHER THE COPY IS KEPT is the model's answer, not this one's (FollowModelTiers - the spec's section 3-1: a list with
	// no row that no search made is not a tier, and the model is the one place that says so).
	bool TakeSnapshot(KeptTier& out)
	{
		if (!KFCResultHistory::HasLiveList())
			return false;
		KFCResultTree::RowLook head;
		if (!KFCResultTree::GetRowLook(KFCResultNodeID::CreateLiveTier(), head))
			return false;
		out.set = KFCResults()->GetResultSetId();
		// (A query run's list has no search's name: nothing to read a query against - QueryReadsAsTier.)
		out.name = (LiveIsStacked() && !KFCResults()->ListIsRunList()) ? gLive.label : PMString();
		out.name.SetTranslatable(kFalse);
		out.label = head.label;
		out.head = KFCResultHistory::LiveTierLabel();	// the name GetRowLook wrote it with, before the counts
		out.head.SetTranslatable(kFalse);
		InterfacePtr<ITreeViewMgr> treeMgr(KFCResultTree::QueryResultTreeMgr());
		// The tier's own row open or closed as the person left it (v2 R6) - open when the tree does not show it as a tier (a
		// list drawn with no tiers shows all its rows) or the panel is closed.
		out.open = (treeMgr != nil && KFCResultHistory::IsStacking())
			? (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateLiveTier()) != kFalse) : true;
		out.holdsObjects = KFCResults()->ListHoldsObjects();
		out.holdsQRCodes = KFCResults()->ListIsQRCodes();
		out.holdsRunList = KFCResults()->ListIsRunList();
		CopyRows(treeMgr, KFCResultNodeID::CreateLiveTier(), -1, out);
		return true;
	}

	// THE COPIES FOLLOW THE MODEL'S TIERS (the spec's section 3-1: which lists are kept, in what order, and the cap are the
	// model's - KFCTierStore - and this half draws what the model keeps): the copies put in the model's order, and a copy
	// whose list the model does not keep let go. Returns how many copies went.
	int32 FollowModelTiers()
	{
		std::vector<uint32> sets;
		KFCRuns()->GetKeptTierSets(sets);
		std::vector<KeptTier> kept;
		kept.reserve(sets.size());
		for (size_t s = 0; s < sets.size(); ++s)
			for (size_t k = 0; k < gKept.size(); ++k)
				if (gKept[k].set != 0 && gKept[k].set == sets[s])
				{
					kept.push_back(std::move(gKept[k]));
					gKept[k].set = 0;		// moved from: not matched again
					break;
				}
		const int32 went = static_cast<int32>(gKept.size()) - static_cast<int32>(kept.size());
		gKept = std::move(kept);
		return went;
	}

	// Is `wanted` a row of the live list under `from` - walked as the list draws (KFCResultTree::LiveRowCount / LiveRow, the
	// walk CopyRows makes)? The tree's own search (ITreeViewMgr::Search) answers only for the rows it has made widgets for.
	bool FindLiveRow(const NodeID& from, const NodeID& wanted)
	{
		const int32 count = KFCResultTree::LiveRowCount(from);
		for (int32 i = 0; i < count; ++i)
		{
			const NodeID child(KFCResultTree::LiveRow(from, i));
			if (!child.IsValid())
				continue;
			if (child == wanted)
				return true;
			TreeNodePtr<KFCResultNodeID> childID(child);
			if (childID != nil && !childID->IsHitRow() && FindLiveRow(child, wanted))
				return true;
		}
		return false;
	}

	// Is `row` - a book, document or story row of the live list, as a copy noted it - a row of the list now? Asked of the
	// model by the row's own numbers, not by a walk of the list for each row: the book searched, the document shown, the
	// story's group there.
	bool LiveBranchRowStands(const NodeID& row)
	{
		TreeNodePtr<KFCResultNodeID> id(row);
		if (id == nil || id->IsRoot() || id->IsInert() || id->IsHitRow())
			return false;
		if (id->IsBookRow())
			return KFCResults()->IsFromBook();
		if (KFCResults()->GetShownChapterPos(id->GetChapter()) < 0)
			return false;
		return !id->IsFontRow() || id->GetFont() < KFCResults()->GetDisplayFontCount(id->GetChapter());
	}

	// THE ROW AT THE TOP OF THE LIST'S VIEW: the tree makes widgets for the rows in view alone, each a widget of a panel of
	// the tree's with its NodeID (ITreeNodeIDData) - the one standing highest is the top row. Invalid when none is made (an
	// empty list, the panel closed).
	NodeID ShownTop()
	{
		NodeID top;
		InterfacePtr<IPanelControlData> treePanel(KFCResultTree::ResultListView(), UseDefaultIID());
		if (treePanel == nil)
			return top;
		bool found = false;
		PMReal highest = 0;
		for (int32 i = 0; i < treePanel->Length(); ++i)
		{
			InterfacePtr<IPanelControlData> rows(treePanel->GetWidget(i), UseDefaultIID());
			if (rows == nil)
				continue;
			for (int32 j = 0; j < rows->Length(); ++j)
			{
				IControlView* const rowView = rows->GetWidget(j);
				InterfacePtr<const ITreeNodeIDData> data(rowView, UseDefaultIID());
				if (rowView == nil || data == nil || !rowView->IsVisible())
					continue;
				const PMReal y = rowView->GetFrame().Top();
				if (!found || y < highest)
				{
					highest = y;
					top = data->Get();
					found = true;
				}
			}
		}
		return top;
	}

	/** WHERE THE VIEW STANDS, BY WHAT ITS TOP ROW IS (a switch keeps the view - the row clicked stays where it was): the list
	    the row is of and its place in it - a tier's own row, a copy's row by its number, a live row by its NodeID - so it is
	    found again after the switch has made a grey tier live and the live list grey, which gives each of those rows
	    another NodeID. */
	struct ViewTop
	{
		uint32	set;		// the list (0 = nothing noted)
		bool	heading;	// the tier's own row
		int32	keptRow;	// a copy's row, by its number in the copy (-1 = none)
		NodeID	live;		// a live row - or the live row a copy's row was made from

		ViewTop() : set(0), heading(false), keptRow(-1) {}
	};

	// The top row noted - before a switch moves anything (the tree numbers the grey tiers as the last rebuild drew them).
	ViewTop NoteViewTop()
	{
		ViewTop at;
		const NodeID top(ShownTop());
		if (!top.IsValid())
			return at;
		TreeNodePtr<KFCResultNodeID> id(top);
		if (id == nil || id->IsRoot())
			return at;
		if (!id->IsInert())
		{
			at.set = KFCResults()->GetResultSetId();		// a row of the live list
			at.live = top;
			return at;
		}
		const int32 drawn = id->GetTier();
		if (drawn < 0)
		{
			at.set = KFCResults()->GetResultSetId();		// the live tier's own row
			at.heading = true;
			return at;
		}
		if (drawn >= static_cast<int32>(gDrawnSets.size()))
			return at;
		at.set = gDrawnSets[static_cast<size_t>(drawn)];
		at.heading = (id->IsTierRow() != kFalse);
		if (!at.heading)
		{
			at.keptRow = id->GetKeptRow();
			const KeptRow* const row = RowOf(TierOfSet(at.set), at.keptRow);
			if (row != nil)
				at.live = row->live;
		}
		return at;
	}

	// ...and found again once the switch has drawn the list anew. Invalid when its list is not drawn now.
	NodeID FindViewTop(const ViewTop& at)
	{
		if (at.set == 0)
			return NodeID();
		if (at.set == KFCResults()->GetResultSetId() && KFCResultHistory::HasLiveList())
		{
			// The list is the live one now: its own row, or the row the copy's row was made from while it is still one.
			if (at.heading || !at.live.IsValid() || !FindLiveRow(KFCResultNodeID::CreateLiveTier(), at.live))
				return NodeID(KFCResultNodeID::CreateLiveTier());
			return at.live;
		}
		const int32 t = TierOfSet(at.set);
		if (t < 0)
			return NodeID();
		const KeptTier& kept = gKept[static_cast<size_t>(t)];
		if (at.heading)
			return NodeID(KFCResultNodeID::CreateTier(t));
		if (at.keptRow >= 0 && at.keptRow < static_cast<int32>(kept.rows.size()))
			return NodeID(KFCResultNodeID::CreateKept(t, at.keptRow));
		// A row of the list that was live: the copy's row made from it.
		for (size_t r = 0; r < kept.rows.size(); ++r)
			if (at.live.IsValid() && kept.rows[r].live == at.live)
				return NodeID(KFCResultNodeID::CreateKept(t, static_cast<int32>(r)));
		return NodeID(KFCResultNodeID::CreateTier(t));
	}

	// A LIST MADE ANEW BY A SEARCH INTO THE PANEL (Find, Narrow - `committed`: it went through), what follows it, in
	// one place: the copy of the list it replaced offered as a tier; THE COPIES FOLLOW THE MODEL'S TIERS - after
	// every search, whether it went through or not (the plan's Review Focus 5: the copies and the model's tiers must
	// not drift apart): the list replaced is kept or not as the model kept it (KFCTierStore::ParkCurrent - one
	// answer, in one place), and past the cap the model let the oldest go; and the new list named `label`. outKept =
	// the copy offered was kept. Returns how many copies the cap let go: the copies that went are the oldest - all of
	// them but the one offered, when the model did not keep it.
	int32 FollowNewList(bool committed, bool havePending, KeptTier& pending, uint32 setBefore, const PMString& label,
		bool& outKept)
	{
		if (committed && havePending)
			gKept.push_back(std::move(pending));
		const int32 went = FollowModelTiers();
		outKept = committed && havePending && !gKept.empty() && gKept.back().set == setBefore;
		const int32 letGo = went - ((committed && havePending && !outKept) ? 1 : 0);
		if (committed)
		{
			gLive.label = label;
			gLive.label.SetTranslatable(kFalse);
			gLive.set = KFCResults()->GetResultSetId();
			gLive.stacked = true;
		}
		return letGo;
	}

	// The status line after a search into the panel: the search's words - and, when the cap let a tier go, a word on it
	// after them.
	void SayFound(const PMString& summary, int32 letGo)
	{
		PMString status(summary);
		status.SetTranslatable(kFalse);
		if (letGo > 0)
		{
			status.Append(" The oldest kept results were let go - ");
			status.AppendNumber(KFCResultModel::kMaxKeptTiers);
			status.Append(" are kept at most.");
		}
		KFCResultTree::ShowStatus(status);
	}
}	// anonymous namespace

int32 KFCResultHistory::Find(const PMString& label, PMString& outSummary, bool* outCommitted)
{
	if (outCommitted != nil)
		*outCommitted = false;
	// Before the search rather than after: the progress bar is modal, and the tab stays in view behind it. This is also
	// what puts the name back when the panel has been closed and reopened since the last toggle.
	KFCPanelTitle::Update();

	// The list on the panel now, copied before the search can throw it away (its commit point -
	// KFCSearchEngine::SearchBook) and offered as a tier only when the search goes through.
	const uint32 setBefore = KFCResults()->GetResultSetId();
	KeptTier pending;
	const bool havePending = TakeSnapshot(pending);

#ifdef KFC_DIAG
	// how often the UI half's observers ran while the search did (guide vol2-15: not per match, one hopes)
	const int titleBefore = KFCDiagCounter(kKFCDiagTitleRecomputes);
	const int mirrorBefore = KFCDiagCounter(kKFCDiagFieldMirrors);
#endif
	const int32 found = KFCRuns()->SearchBook(outSummary);
#ifdef KFC_DIAG
	KFC_DIAG_LOG("UIOBS search title=%d mirror=%d", KFCDiagCounter(kKFCDiagTitleRecomputes) - titleBefore,
		KFCDiagCounter(kKFCDiagFieldMirrors) - mirrorBefore);
#endif

	// WENT THROUGH = A NEW LIST: past its commit point the search drops the results (KFCSearchEngine::DropResults - a new
	// result set), and every refusal comes before it and touches nothing (SearchBook's own rule).
	const bool committed = KFCResults()->GetResultSetId() != setBefore;
	bool keptPending = false;
	const int32 letGo = FollowNewList(committed, havePending, pending, setBefore, label, keptPending);
	KFC_DIAG_LOG("STACK find went=%d kept=%d letgo=%d tiers=%d", committed ? 1 : 0, keptPending ? 1 : 0,
		static_cast<int>(letGo), static_cast<int>(gKept.size()));
	KFCResultTree::Rebuild();
	// The status line is the panel's alone: outSummary goes back to the caller as the search's own words, which the
	// query dialog quotes.
	SayFound(outSummary, letGo);
	if (outCommitted != nil)
		*outCommitted = committed;
	return found;
}

bool KFCResultHistory::Narrow(const KFCNarrowStep& step, PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);
	KFCPanelTitle::Update();
	// The list narrowed: its copy taken before the model can park it (Find's rule), and its name - the new list's name.
	const uint32 setBefore = KFCResults()->GetResultSetId();
	PMString label;
	label.SetTranslatable(kFalse);
	if (gLive.set != 0 && gLive.set == setBefore)
		label = gLive.label;
	KeptTier pending;
	const bool havePending = TakeSnapshot(pending);
	(void)KFCRuns()->NarrowList(step, outSummary);
	// A NEW LIST = narrowed (a new result set - the model's commit point, as a search's); refused, nothing has changed.
	const bool committed = KFCResults()->GetResultSetId() != setBefore;
	bool keptPending = false;
	const int32 letGo = FollowNewList(committed, havePending, pending, setBefore, label, keptPending);
	KFC_DIAG_LOG("STACK narrow kind=%d went=%d kept=%d letgo=%d tiers=%d", static_cast<int>(step.kind), committed ? 1 : 0,
		keptPending ? 1 : 0, static_cast<int>(letGo), static_cast<int>(gKept.size()));
	// Drawn again either way: a new list rebuilt; the list a refusal or a Cancel left repainted - a walk over part of a
	// document has its rows follow their foci before it can be refused (KFCResultListWidgetMgr's SearchAgain says why).
	if (committed)
		KFCResultTree::Rebuild();
	else
	{
#ifdef KFC_DIAG
		// (Test builds only) Fault switch again-no-repaint: the rows left as drawn, the old way.
		if (!KFC_DIAG_FAULT("again-no-repaint"))
#endif
			KFCResultTree::RefreshRows();
	}
	SayFound(outSummary, letGo);
	return committed;
}

bool KFCResultHistory::SearchTextInObjects(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);
	KFCPanelTitle::Update();
	// The list of objects: its copy taken before the model can park it (Find's rule). The new list is named as Find
	// names one - the query Find/Change holds now, which it is searched with (the text-in-objects spec's T4) - read
	// before the search, as Find's.
	const uint32 setBefore = KFCResults()->GetResultSetId();
	const PMString label(QueryLabel());
	KeptTier pending;
	const bool havePending = TakeSnapshot(pending);
	(void)KFCRuns()->SearchTextInObjects(outSummary);
	// A NEW LIST = searched (a new result set - the model's commit point, as a search's); refused, nothing has changed.
	const bool committed = KFCResults()->GetResultSetId() != setBefore;
	bool keptPending = false;
	const int32 letGo = FollowNewList(committed, havePending, pending, setBefore, label, keptPending);
	KFC_DIAG_LOG("STACK textinobjects went=%d kept=%d letgo=%d tiers=%d", committed ? 1 : 0, keptPending ? 1 : 0,
		static_cast<int>(letGo), static_cast<int>(gKept.size()));
	if (committed)
		KFCResultTree::Rebuild();
	SayFound(outSummary, letGo);
	return committed;
}

bool KFCResultHistory::TierHoldsObjects(int32 tier)
{
	if (tier < 0)
		return KFCResults()->ListHoldsObjects();
	return tier < static_cast<int32>(gKept.size()) && gKept[static_cast<size_t>(tier)].holdsObjects;
}

bool KFCResultHistory::TierHoldsQRCodes(int32 tier)
{
	if (tier < 0)
		return KFCResults()->ListIsQRCodes();
	return tier < static_cast<int32>(gKept.size()) && gKept[static_cast<size_t>(tier)].holdsQRCodes;
}

bool KFCResultHistory::TierHoldsRunList(int32 tier)
{
	if (tier < 0)
		return KFCResults()->ListIsRunList();
	return tier < static_cast<int32>(gKept.size()) && gKept[static_cast<size_t>(tier)].holdsRunList;
}

namespace
{
	// One side of a tier's name: the words, cut at kLabelWordsMax where a character may be cut (KFCSafeCut).
	void AppendQueryWords(PMString& label, PMString words)
	{
		if (words.CharCount() > kLabelWordsMax)
		{
			words.Truncate(words.CharCount() - KFCSafeCut(words, kLabelWordsMax));	// Truncate takes off that many from the end
			words.Append("...");
		}
		label.Append(words);
	}

	// A tier's name without the query dialog's place in the run order - "<n>  " in front of the query's words (the dialog's
	// Find names its tier so, KFCQueryDialog.cpp): the digits, then the two spaces. A name without them is its own.
	PMString WithoutQueryPlace(const PMString& name)
	{
		const int32 count = name.CharCount();
		int32 digits = 0;
		while (digits < count && name.GetWChar(digits).GetValue() >= '0' && name.GetWChar(digits).GetValue() <= '9')
			++digits;
		PMString words(name);
		words.SetTranslatable(kFalse);
		if (digits > 0 && digits + 2 <= count && name.GetWChar(digits).GetValue() == ' '
			&& name.GetWChar(digits + 1).GetValue() == ' ')
			words.Remove(0, digits + 2);
		return words;
	}

	// THE SCOPE IN A TIER'S HEADING (the author: "[Text/Story] - easy to read"): the Search: the list was
	// searched with, in Find/Change's own words, after the tab inside the name's first brackets - "[Text]" drawn
	// "[Text/Story]", the query dialog's "1  [GREP]" "1  [GREP/Book]". DRAWN, NOT KEPT IN THE NAME: the name is what a row
	// operation reads the query against (QueryReadsAsTier - QueryLabel made again from Find/Change), and Search: is not part
	// of the query stated back. A name with no brackets ("Results") is drawn as it is.
	PMString WithScope(const PMString& name, KFCResultModel::SearchScopeKind scope)
	{
		PMString out(name);
		out.SetTranslatable(kFalse);
		const char* words = nil;
		switch (scope)
		{
			case KFCResultModel::kScopeDocument:		words = "Document"; break;
			case KFCResultModel::kScopeBook:			words = "Book"; break;
			case KFCResultModel::kScopeAllDocuments:	words = "All Documents"; break;
			case KFCResultModel::kScopeStory:			words = "Story"; break;
			case KFCResultModel::kScopeToEndOfStory:	words = "To End of Story"; break;
			case KFCResultModel::kScopeSelection:		words = "Selection"; break;
			case KFCResultModel::kScopeInObjects:		words = "Objects"; break;		// Search Text in These Objects
			default:									break;
		}
		const CharCounter open = out.IndexOfWChar(UTF32TextChar('['), 0);
		const CharCounter close = (open >= 0) ? out.IndexOfWChar(UTF32TextChar(']'), open + 1) : -1;
		if (words == nil || close < 0)
			return out;
		PMString scoped("/");
		scoped.SetTranslatable(kFalse);
		scoped.Append(words);
		out.Insert(scoped, close);
		return out;
	}
}

namespace
{
	// A tier's name for tab `mode` - QueryLabel's and HeldQueryLabel's words.
	PMString LabelForTab(int32 mode)
	{
		PMString label("[");
		label.SetTranslatable(kFalse);
		label.Append(KFCRuns()->TabName(mode));
		label.Append("]");
		// THE QR CODE TAB HAS NO QUERY (it lists every QR code - KFCModelTypes.h): "[QR Code]" alone - Find/Change's words
		// are another tab's, the one InDesign's mode still is.
		if (mode == kKFCQRCodeSearchMode)
			return label;
		// (The words are InDesign's own tab's - KFCFindChangeQueryWords reads the mode Find/Change is in, which `mode` is for
		// every tab but the QR Code tab.)
		PMString findWords, changeWords;
		if (KFCFindChangeQueryWords(findWords))
		{
			// F and C, not Find and Change (the author's call): the row has the panel's width to fill.
			label.Append(" F:\"");
			AppendQueryWords(label, findWords);
			label.Append("\"");
			(void)KFCFindChangeChangeWords(changeWords);		// the same tab, so it answers as the find side did
			label.Append(" C:\"");
			AppendQueryWords(label, changeWords);
			label.Append("\"");
		}
		return label;
	}
}	// anonymous namespace

PMString KFCResultHistory::QueryLabel()
{
	return LabelForTab(KFCRuns()->CurrentSearchMode());
}

PMString KFCResultHistory::HeldQueryLabel()
{
	// InDesign's own tab - also while KFC's QR Code tab is shown, whose mode is still the tab shown before it.
	int32 mode = -1, scope = -1;
	KFCRuns()->FindChangeTabAndScope(mode, scope);
	return LabelForTab(mode);
}

void KFCResultHistory::DropStackIfRemade(uint32 setBefore)
{
	if (KFCResults()->GetResultSetId() == setBefore)
		return;
	// THE LIST IT REPLACED KEEPS ITS NAME (the author's call): a query run's Ctrl+Z puts that list back as its own
	// result set (KFCUndoFollow - the run's step names the set it found), and it comes back as it was - its tier
	// named, a row's Replace reading the list's query against that name (QueryReadsAsTier), and the query dialog's Find
	// refusing the query that made it (QueryJustFound). The name and that note are kept by their set and read only
	// while it is the live list's (LiveIsStacked), so they wait unread until it comes back - after a second run too,
	// whose Ctrl+Z comes first. Change All in Book's Ctrl+Z puts no list back: there they are never read. The tiers
	// go (cases qd-run-undo-name, qd-find-run-undo-refused, qd-run-undo-replace).
	const LiveTier live = gLive;
	const IDFile lastFound = gLastFound;
	const uint32 lastFoundSet = gLastFoundSet;
	(void)DropStack();
	gLive = live;
	gLastFound = lastFound;
	gLastFoundSet = lastFoundSet;
	KFC_DIAG_LOG("STACK remade - the name kept for set=%u", gLive.set);
}

bool KFCResultHistory::DropStack()
{
	const bool was = IsStacking();
	// The model's tiers go with the copies - the lists, their query copies and the writes kept for them (the spec's section
	// 3-1: Clear Results, a Run and Change All in Book (No List) let every tier go).
	KFCRuns()->DropKeptTiers();
	// Fresh values, not Clear(): the storage goes with them.
	gKept = std::vector<KeptTier>();
	gLive = LiveTier();
	gLastFound = IDFile();
	gLastFoundSet = 0;
	gDrawnSets = std::vector<uint32>();
	KFC_DIAG_LOG("STACK drop was=%d", was ? 1 : 0);
	return was;
}

bool KFCResultHistory::IsStacking()
{
	return !gKept.empty() || LiveIsStacked();
}

bool KFCResultHistory::HasLiveList()
{
	return KFCResults()->HasRun() || KFCResults()->IsFromBook() || KFCResults()->GetDisplayChapterCount() > 0;
}

bool KFCResultHistory::QueryJustFound(const IDFile& file)
{
	return gLastFoundSet != 0 && gLastFoundSet == KFCResults()->GetResultSetId() && FileUtils::IsEqual(file, gLastFound) != kFalse;
}

void KFCResultHistory::NoteQueryFound(const IDFile& file)
{
	gLastFound = file;
	gLastFoundSet = KFCResults()->GetResultSetId();
}

int32 KFCResultHistory::KeptTierCount()
{
	return static_cast<int32>(gKept.size());
}

PMString KFCResultHistory::LiveTierLabel()
{
	PMString label("Results");
	label.SetTranslatable(kFalse);
	if (KFCResults()->ListIsRunList())
	{
		// A QUERY RUN'S LIST: "[Run/Document] a, b" - its queries that wrote what it lists (the run list spec's R7), kept with it.
		PMString names;
		KFCResults()->GetRunNames(names);
		PMString run("[Run] ");
		run.SetTranslatable(kFalse);
		run.Append(names);
		label = WithScope(run, KFCResults()->GetSearchScope());
	}
	else if (gLive.set != 0 && gLive.set == KFCResults()->GetResultSetId() && !gLive.label.IsEmpty())
		label = WithScope(gLive.label, KFCResults()->GetSearchScope());	// "[Text/Story] ..." - the list's own Search:
	// A NARROWED LIST'S CONDITIONS, after the name - drawn for Search:'s reason (the narrow-down spec's N4: "[Text/Book]
	// F:"cat" C:"dog" / In Tables / Pages 1-5").
	PMString narrow;
	KFCResults()->GetNarrowWords(narrow);
	if (!narrow.IsEmpty())
	{
		label.Append(" / ");
		label.Append(narrow);
	}
	label.SetTranslatable(kFalse);
	return label;
}

int32 KFCResultHistory::LiveTierPosition()
{
	const uint32 live = KFCResults()->GetResultSetId();
	int32 before = 0;
	for (size_t k = 0; k < gKept.size(); ++k)
		if (gKept[k].set < live)
			++before;
	return before;
}

int32 KFCResultHistory::KeptChildCount(int32 tier, int32 row)
{
	const std::vector<int32>* children = ChildrenOf(tier, row);
	return (children != nil) ? static_cast<int32>(children->size()) : 0;
}

int32 KFCResultHistory::KeptChild(int32 tier, int32 row, int32 nth)
{
	const std::vector<int32>* children = ChildrenOf(tier, row);
	if (children == nil || nth < 0 || nth >= static_cast<int32>(children->size()))
		return -1;
	return (*children)[static_cast<size_t>(nth)];
}

int32 KFCResultHistory::KeptParent(int32 tier, int32 row)
{
	const KeptRow* kept = RowOf(tier, row);
	return (kept != nil) ? kept->parent : -1;
}

int32 KFCResultHistory::KeptChildPos(int32 tier, int32 row)
{
	const KeptRow* kept = RowOf(tier, row);
	return (kept != nil) ? kept->pos : -1;
}

bool KFCResultHistory::GetKeptLook(int32 tier, int32 row, KFCResultTree::RowLook& out)
{
	out = KFCResultTree::RowLook();
	if (tier < 0 || tier >= static_cast<int32>(gKept.size()))
		return false;
	if (row < 0)
	{
		out.label = gKept[static_cast<size_t>(tier)].label;		// the tier's own row: its name and counts, at the left edge
		out.label.SetTranslatable(kFalse);
		out.name = gKept[static_cast<size_t>(tier)].head;
		out.name.SetTranslatable(kFalse);
		return true;
	}
	const KeptRow* kept = RowOf(tier, row);
	if (kept == nil)
		return false;
	out = kept->look;
	return true;
}

bool KFCResultHistory::IsKeptHit(int32 tier, int32 row)
{
	const KeptRow* kept = RowOf(tier, row);
	return kept != nil && kept->look.hit;
}

void KFCResultHistory::NoteOpenTiers(ITreeViewMgr* treeMgr)
{
	if (treeMgr == nil)
		return;
	// The tree's tier t is the copy that has the result set the last rebuild drew it with (gDrawnSets), wherever that copy
	// stands now: the cap lets tiers go from the front, and a switch takes one out from anywhere. A tier gone since is
	// passed over.
	for (size_t t = 0; t < gDrawnSets.size(); ++t)
	{
		const int32 k = TierOfSet(gDrawnSets[t]);
		if (k < 0)
			continue;
		KeptTier& kept = gKept[static_cast<size_t>(k)];
		kept.open = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateTier(static_cast<int32>(t))) != kFalse);
		// Its rows too (v2 R6 - a row the person opened or closed in a grey tier stays so): the branch rows, a hit row
		// being a leaf.
		for (size_t r = 0; r < kept.rows.size(); ++r)
			if (!kept.rows[r].look.hit)
				kept.rows[r].open = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateKept(static_cast<int32>(t),
					static_cast<int32>(r))) != kFalse);
	}
	// The live tier's own row, while the list drawn last is the live one still - a switch notes what it opens itself.
	if (gLiveOpen.set == 0 && gDrawnLive != 0 && gDrawnLive == KFCResults()->GetResultSetId())
	{
		gLiveOpen.set = gDrawnLive;
		gLiveOpen.tierOpen = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateLiveTier()) != kFalse);
		gLiveOpen.rowsNoted = false;
	}
}

int32 KFCResultHistory::OpenKeptRows(ITreeViewMgr* treeMgr)
{
	int32 opened = 0;
	if (treeMgr != nil)
	{
		for (int32 t = 0; t < KeptTierCount(); ++t)
		{
			const KeptTier& kept = gKept[static_cast<size_t>(t)];
			if (kept.open)
			{
				treeMgr->ExpandNode(KFCResultNodeID::CreateTier(t), kFalse /*expandAllDescendants*/);
				++opened;
			}
			// A row under a closed tier is opened too - the tree keeps each row's state - so the tier opens as it was kept.
			for (int32 r = 0; r < static_cast<int32>(kept.rows.size()); ++r)
				if (kept.rows[static_cast<size_t>(r)].open)
				{
					treeMgr->ExpandNode(KFCResultNodeID::CreateKept(t, r), kFalse /*expandAllDescendants*/);
					++opened;
				}
		}
	}
	// What this rebuild drew, by result set - NoteOpenTiers' map at the next - and the list it draws as the live tier.
	std::vector<uint32> drawn;
	drawn.reserve(gKept.size());
	for (size_t k = 0; k < gKept.size(); ++k)
		drawn.push_back(gKept[k].set);
	gDrawnSets.swap(drawn);
	gDrawnLive = (IsStacking() && HasLiveList()) ? KFCResults()->GetResultSetId() : 0;
	return opened;
}

bool KFCResultHistory::OpenLiveTier(ITreeViewMgr* treeMgr, int32& ioExpands)
{
	const LiveOpen noted(gLiveOpen);
	gLiveOpen = LiveOpen();		// used once
	if (treeMgr == nil || !HasLiveList())
		return false;
	const bool forThis = (noted.set != 0 && noted.set == KFCResults()->GetResultSetId());
	if (!forThis || noted.tierOpen)
	{
		treeMgr->ExpandNode(KFCResultNodeID::CreateLiveTier(), kFalse /*expandAllDescendants*/);
		++ioExpands;
	}
	if (!forThis || !noted.rowsNoted)
		return false;
	// A switch's: the rows the tier's copy had open, while they are rows of the list (a document closed while it was grey
	// took its rows) - in the copy's order, a row's parent before it.
	for (size_t i = 0; i < noted.open.size(); ++i)
		if (LiveBranchRowStands(noted.open[i]))
		{
			treeMgr->ExpandNode(noted.open[i], kFalse /*expandAllDescendants*/);
			++ioExpands;
		}
	return true;
}

bool KFCResultHistory::TakeScrollHold()
{
	const bool hold = gScrollHold;
	gScrollHold = false;
	return hold;
}

void KFCResultHistory::HoldScrollForNextRebuild()
{
	gScrollHold = true;
}

NodeID_rv KFCResultHistory::ViewTopRow()
{
	return ShownTop();
}

bool KFCResultHistory::QueryReadsAsTier(uint32 set, PMString& outName)
{
	outName.Clear();
	outName.SetTranslatable(kFalse);
	// THE LIVE LIST (v2: a row operation on it states its query when Find/Change is on another one - KFCQueryScope): its
	// name, as LiveTierLabel draws it - a list no search here named has none to read the query against.
	// (outName as the row draws it - its scope after the tab, WithScope; the query is read against the name as it is kept.)
	if (set != 0 && set == gLive.set && gLive.set == KFCResults()->GetResultSetId())
	{
		outName = LiveTierLabel();
		outName.SetTranslatable(kFalse);
		if (gLive.label.IsEmpty())
			return false;
		return (WithoutQueryPlace(gLive.label) == HeldQueryLabel()) != kFalse;
	}
	const int32 tier = TierOfSet(set);
	if (tier < 0)
		return false;
	const KeptTier& kept = gKept[static_cast<size_t>(tier)];
	outName = kept.head.IsEmpty() ? kept.name : kept.head;
	outName.SetTranslatable(kFalse);
	if (kept.name.IsEmpty())
		return false;		// no search here named that list: nothing to read the query against
	// Word for word - PMString's == compares the buffers -: the name as the search made it, the dialog's place in the
	// run order taken off, against the name made again from the query Find/Change holds now (HeldQueryLabel: InDesign's own
	// tab's - the query stated for the operation is one of its tabs', KFC's QR Code tab shown or not).
	return (WithoutQueryPlace(kept.name) == HeldQueryLabel()) != kFalse;
}

bool KFCResultHistory::ClearTier(int32 tier)
{
	// The tier and its name, before anything goes.
	uint32 set = 0;
	PMString name;
	if (tier >= 0)
	{
		if (tier >= KeptTierCount())
			return false;
		set = gKept[static_cast<size_t>(tier)].set;
		name = gKept[static_cast<size_t>(tier)].head;		// as its row draws it - its scope after the tab (WithScope)
		if (name.IsEmpty())
			name = gKept[static_cast<size_t>(tier)].name;
		if (name.IsEmpty())
			name = PMString("Results");
	}
	else
	{
		if (!HasLiveList())
			return false;
		set = KFCResults()->GetResultSetId();
		name = LiveTierLabel();
	}
	name.SetTranslatable(kFalse);
	if (!KFCRuns()->DropTier(set))
		return false;
	// Its copy goes with it - or, for the live list, its name (the list it named is gone; a later search names its own).
	if (tier >= 0)
		gKept.erase(gKept.begin() + tier);
	else
		gLive = LiveTier();
	(void)FollowModelTiers();
	KFCResultTree::Rebuild();
	PMString status("Cleared: ");
	status.SetTranslatable(kFalse);
	status.Append(name);
	KFCResultTree::ShowStatus(status);
	KFC_DIAG_LOG("STACK clear tier=%d set=%u tiers=%d", static_cast<int>(tier), set, static_cast<int>(gKept.size()));
	return true;
}

void KFCResultHistory::KeptChapterGone(uint32 set, int32 chapterIdx, int32 replaced, int32 hits)
{
	const int32 tier = TierOfSet(set);
	if (tier < 0)
		return;
	KeptTier& kept = gKept[static_cast<size_t>(tier)];
	// THE TREE IS TOLD ONLY WHERE THE LAST REBUILD DREW THIS TIER, AT THIS PLACE (gDrawnSets - the tree numbers the tiers
	// as it drew them): a copy drawn elsewhere or not at all - the panel closed - is drawn from the copy at the next
	// rebuild.
	InterfacePtr<ITreeViewMgr> treeMgr(KFCResultTree::QueryResultTreeMgr());
	const bool drawn = treeMgr != nil && tier < static_cast<int32>(gDrawnSets.size())
		&& gDrawnSets[static_cast<size_t>(tier)] == set;
	// THE DOCUMENT ROW: a row under the tier's own row, copied from that chapter's row (a book's tier keeps its chapters -
	// the model sends nothing for one).
	bool found = false;
	for (size_t i = 0; i < kept.top.size() && !found; ++i)
	{
		const int32 row = kept.top[i];
		TreeNodePtr<KFCResultNodeID> id(kept.rows[static_cast<size_t>(row)].live);
		if (id == nil || !id->IsDocumentRow() || id->GetChapter() != chapterIdx)
			continue;
		found = true;
		// Out of the tree first, while the copy still holds it (KFCResultTree::BeforeChapterRowGoes' order for the live
		// list).
		if (drawn)
			treeMgr->BeforeNodeDeleted(KFCResultNodeID::CreateKept(tier, row));
		// Out of the copy: off the tier's own row, the rows after it one place up. The rows themselves stay in `rows` - the
		// others are numbered by their place there, as the tree knows them - closed, the row and the rows under it, so
		// OpenKeptRows never opens one of them again.
		kept.top.erase(kept.top.begin() + static_cast<std::ptrdiff_t>(i));
		for (size_t j = i; j < kept.top.size(); ++j)
			kept.rows[static_cast<size_t>(kept.top[j])].pos = static_cast<int32>(j);
		std::vector<int32> under(1, row);
		while (!under.empty())
		{
			KeptRow& gone = kept.rows[static_cast<size_t>(under.back())];
			under.pop_back();
			gone.open = false;
			under.insert(under.end(), gone.children.begin(), gone.children.end());
		}
	}
	// ITS HEADING READS THE KEPT LIST'S COUNTS NOW - the tier stays, "(0/0)" once its last document is gone.
	kept.label = kept.head;
	kept.label.SetTranslatable(kFalse);
	KFCResultTree::AppendCounts(kept.label, replaced, hits);
	if (drawn)
		treeMgr->NodeChanged(KFCResultNodeID::CreateTier(tier));
	KFC_DIAG_LOG("STACK kept chapter gone set=%u chapter=%d found=%d drawn=%d counts=%d/%d", set,
		static_cast<int>(chapterIdx), found ? 1 : 0, drawn ? 1 : 0, static_cast<int>(replaced), static_cast<int>(hits));
}

void KFCResultHistory::KeptChapterRenamed(uint32 set, int32 chapterIdx, const PMString& name)
{
	const int32 tier = TierOfSet(set);
	if (tier < 0)
		return;
	KeptTier& kept = gKept[static_cast<size_t>(tier)];
	for (size_t i = 0; i < kept.top.size(); ++i)
	{
		const int32 row = kept.top[i];
		KeptRow& doc = kept.rows[static_cast<size_t>(row)];
		TreeNodePtr<KFCResultNodeID> id(doc.live);
		if (id == nil || !id->IsDocumentRow() || id->GetChapter() != chapterIdx)
			continue;
		// "<name>  (R/N)", a note between as it was drawn (" (no window)" - KFCResultTree::GetRowLook): the name in
		// front is the one given anew, the rest kept. A label that does not begin with the row's name is left as it was
		// drawn.
		if (doc.look.label.IndexOfString(doc.look.name) != 0)
		{
			KFC_DIAG_LOG("STACK kept chapter renamed set=%u chapter=%d - its label does not begin with its name", set,
				static_cast<int>(chapterIdx));
			return;
		}
		PMString rest(doc.look.label);
		rest.Remove(0, doc.look.name.CharCount());
		doc.look.label = name;
		doc.look.label.Append(rest);
		doc.look.label.SetTranslatable(kFalse);
		doc.look.name = name;
		doc.look.name.SetTranslatable(kFalse);
		// THE TREE IS TOLD ONLY WHERE THE LAST REBUILD DREW THIS TIER, AT THIS PLACE (KeptChapterGone says why).
		InterfacePtr<ITreeViewMgr> treeMgr(KFCResultTree::QueryResultTreeMgr());
		const bool drawn = treeMgr != nil && tier < static_cast<int32>(gDrawnSets.size())
			&& gDrawnSets[static_cast<size_t>(tier)] == set;
		if (drawn)
			treeMgr->NodeChanged(KFCResultNodeID::CreateKept(tier, row));
		KFC_DIAG_LOG("STACK kept chapter renamed set=%u chapter=%d row=%d", set, static_cast<int>(chapterIdx),
			static_cast<int>(row));
		return;
	}
}

void KFCResultHistory::RenameLiveFromQuery(uint32 set)
{
	if (set == 0 || set != gLive.set || gLive.set != KFCResults()->GetResultSetId() || gLive.label.IsEmpty())
		return;
	// The query dialog's place in the run order ("<n>  " - its Find names a list so) stays in front; the words after it are
	// made again from the query Find/Change holds now - the one the replace just used.
	const PMString words(WithoutQueryPlace(gLive.label));
	PMString renamed(gLive.label);
	renamed.Truncate(words.CharCount());	// Truncate takes off that many from the end: the place alone is left
	renamed.Append(HeldQueryLabel());		// (the replace's query - InDesign's own tab's, the QR Code tab shown or not)
	renamed.SetTranslatable(kFalse);
	KFC_DIAG_LOG("STACK live renamed set=%u", set);
	gLive.label = renamed;
}

bool KFCResultHistory::SwitchToKeptRow(int32 tier, int32 row, NodeID& outLiveRow)
{
	outLiveRow = NodeID();
	// THE DOORS, before anything moves: a run of ours (the status line says so), a landing under way (dropped, as a second
	// click is - KFCActionComponent's Find), a tier or a row not there (row -1 = the tier's own row - v2 R5).
	if (KFCResultTree::RefusedWhileRunning() || KFCJump::IsLanding())
		return false;
	if (tier < 0 || tier >= KeptTierCount() || row < -1)
		return false;
	const KeptRow* const clicked = (row >= 0) ? RowOf(tier, row) : nil;
	if (row >= 0 && clicked == nil)
		return false;
	const uint32 set = gKept[static_cast<size_t>(tier)].set;
	const NodeID clickedLive(clicked != nil ? clicked->live : NodeID());
	// (A tier no search here named is switched to as any other - v2: the switch reads no query. Its rows' Replace states its
	//  query only when Find/Change is on another one, and is refused then: there is no name to read it against.)

	// AS IT STANDS, BEFORE ANYTHING MOVES: which rows are open - the grey tiers' as the person left them, the tier switched
	// to's among them (v2 R6) - and the row at the top of the view (the view stays where it is).
	InterfacePtr<ITreeViewMgr> treeMgr(KFCResultTree::QueryResultTreeMgr());
	NoteOpenTiers(treeMgr);
	const ViewTop top(NoteViewTop());

	// THE LIVE LIST'S COPY, made before the switch puts the list aside - as Find makes it before a search.
	KeptTier pending;
	const bool havePending = TakeSnapshot(pending);
	PMString why;
	if (!KFCRuns()->SwitchToTier(set, why))
	{
		// Refused: the model put back all it had moved (KFCTierStore::SwitchTo), and nothing here has changed.
		why.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(why);
		KFC_DIAG_LOG("STACK switch refused set=%u", set);
		return false;
	}

	// SWITCHED. The tier is the live list now, under its own name, opened as its copy was - its own row, and each row the
	// copy had open, by the live row the copy's row was made from (OpenLiveTier, in the rebuild below).
	const KeptTier& switched = gKept[static_cast<size_t>(tier)];
	gLive.label = switched.name;
	gLive.label.SetTranslatable(kFalse);
	gLive.set = set;
	gLive.stacked = true;
	gLiveOpen = LiveOpen();
	gLiveOpen.set = set;
	gLiveOpen.tierOpen = switched.open;
	gLiveOpen.rowsNoted = true;
	for (size_t r = 0; r < switched.rows.size(); ++r)
		if (switched.rows[r].open && !switched.rows[r].look.hit)
			gLiveOpen.open.push_back(switched.rows[r].live);
	// The list before it is a copy now, and the copies follow the model's tiers - in search order, the list before at its
	// place among them (KFCTierStore::SwitchTo), or let go when the model did not keep it.
	gKept.erase(gKept.begin() + tier);
	if (havePending)
		gKept.push_back(std::move(pending));
	(void)FollowModelTiers();
	// DRAWN AGAIN WITH NOTHING MOVED: the rebuild leaves the view alone (TakeScrollHold), and the row that was at its top is
	// put back there - the tiers stand where they stood (v2 R4), so every row is where it was.
	gScrollHold = true;
	KFCResultTree::Rebuild();
	gScrollHold = false;		// (taken by the rebuild - and let go here when the panel is closed and nothing was drawn)
	const NodeID topNow(FindViewTop(top));
	if (treeMgr != nil && topNow.IsValid())
		treeMgr->ScrollToNode(topNow, ITreeViewMgr::eScrollToTop);
	// THE ROW CLICKED, in the list now: the live row its copy was made from, while it is still a row of the list (FindLiveRow).
	// Not there = its chapter emptied while the tier was grey (a document closed), or the tier's own row was clicked:
	// switched, nothing selected.
	if (clickedLive.IsValid() && FindLiveRow(KFCResultNodeID::CreateLiveTier(), clickedLive))
		outLiveRow = clickedLive;
	// The status line names the tier switched to (Find/Change is left as it is - a row's Replace says whose query it used) -
	// and the model's words after it, when it has any.
	PMString status("Switched to: ");
	status.SetTranslatable(kFalse);
	status.Append(LiveTierLabel());
	if (!why.IsEmpty())
	{
		status.Append(" ");
		status.Append(why);
	}
	KFCResultTree::ShowStatus(status);
	KFC_DIAG_LOG("STACK switch set=%u row=%d found=%d tiers=%d", set, static_cast<int>(row), outLiveRow.IsValid() ? 1 : 0,
		static_cast<int>(gKept.size()));
	return true;
}

namespace
{
	// THE BOOKED SWITCH, RUN (SwitchToKeptRowLater says when): the switch, then the row switched to selected and landed on as
	// a click on a live row lands (KFCResultNodeEH's button-up) - its jump, then the keyboard to the list. One time only.
	uint32 RunPendingSwitch(void* /*refPtr*/)
	{
		const uint32 tierSet = gBookedTier;
		const int32 row = gBookedRow;
		const uint32 bookedOn = gBookedOn;
		gBookedTier = 0;
		gBookedRow = -1;
		gBookedOn = 0;
		// LET GO, as a second click is (KFCActionComponent's Find): the plug-in going down; a landing under way (it runs the
		// message loop, so this can come in the middle of one - and the switch draws the list anew); a list made anew since
		// the click (a search, Clear Results - the click was about the list it was made on); the panel closed.
		if (tierSet == 0 || gShutDown || KFCJump::IsLanding() || KFCResults()->GetResultSetId() != bookedOn
			|| KFCResultTree::ResultListView() == nil)
		{
			KFC_DIAG_LOG("STACK switch booking let go set=%u", tierSet);
			return IIdleTask::kEndOfTime;
		}
		const int32 tier = TierOfSet(tierSet);
		NodeID liveRow;
		if (tier < 0 || !KFCResultHistory::SwitchToKeptRow(tier, row, liveRow))
			return IIdleTask::kEndOfTime;

		// THE ROW SWITCHED TO, SELECTED AND LANDED ON - the arrows' landing (KFCResultTreeEH's GoOnToNextReplaceableRow): every
		// row above it opened, top down, so the tree draws it; the selection taken off and put on it (the official
		// re-selection - Select alone does not move a selection); brought into view (KFCResultTree::SelectAloneInView);
		// then the jump, and the keyboard to the list - a click on a live row's two (KFCResultNodeEH).
		IControlView* const listView = KFCResultTree::ResultListView();
		InterfacePtr<ITreeViewController> controller(listView, UseDefaultIID());
		InterfacePtr<const ITreeViewHierarchyAdapter> adapter(listView, UseDefaultIID());
		InterfacePtr<ITreeViewMgr> treeMgr(listView, UseDefaultIID());
		if (controller == nil || adapter == nil || treeMgr == nil)
			return IIdleTask::kEndOfTime;
		// SWITCHED WITH NO ROW TO LAND ON - the tier's own row was clicked (v2 R5), or the row's chapter went while it was
		// grey: nothing of the list before stays selected (its rows' numbers name other rows of this list), and the tier's own
		// row is - the one row of a tier that can be (the author's call) - with the keyboard on the list.
		if (!liveRow.IsValid())
		{
			controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
			(void)controller->Select(KFCResultNodeID::CreateLiveTier(), kTrue /*notifyOfChange*/, kTrue /*changeHilite*/);
			KFCResultTree::DropRowPreview();
			(void)KFCResultTree::TakeKeyboard();
			return IIdleTask::kEndOfTime;
		}
		(void)KFCResultTree::SelectAloneInView(controller, adapter, treeMgr, liveRow);
		TreeNodePtr<KFCResultNodeID> rowID(liveRow);
		if (rowID != nil)
			KFCJump::ActivateNode(rowID->GetChapter(), rowID->GetHit());
		(void)KFCResultTree::TakeKeyboard();
		return IIdleTask::kEndOfTime;
	}
}

void KFCResultHistory::SwitchToKeptRowLater(int32 tier, int32 row)
{
	if (gShutDown || tier < 0 || tier >= KeptTierCount() || row < -1 || (row >= 0 && RowOf(tier, row) == nil))
		return;
	gBookedTier = gKept[static_cast<size_t>(tier)].set;
	gBookedRow = row;
	gBookedOn = KFCResults()->GetResultSetId();
	if (gSwitchTimer == nil)
		gSwitchTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	// At the next idle, once the click is over (v2 R7 - no double click's wait: KFC has no double click of its own, and the
	// tiers not moving, a second click lands on the row the first switched to). A click booked before it ran books
	// anew: its booking taken off first, then started again - the official re-arm stops the running timer before it
	// starts one (HTTPAssetLinkResourceHandler.cpp), and ICallbackTimer.h promises nothing for a StartTimer over a
	// live booking.
	if (gSwitchTimer != nil)
	{
		gSwitchTimer->StopTimer();
		gSwitchTimer->StartTimer(RunPendingSwitch, 1, nil);
	}
	KFC_DIAG_LOG("STACK switch booked set=%u row=%d", gBookedTier, static_cast<int>(row));
}

void KFCResultHistory::ShutdownCleanup()
{
	// Nothing booked from now on, and the booking's timer stopped and let go: its callback is a raw pointer into this .pln
	// (ICallbackTimer.h - the plug-in must not be unloaded while it is in the timer).
	gShutDown = true;
	if (gSwitchTimer != nil)
	{
		gSwitchTimer->StopTimer();
		gSwitchTimer->Release();
		gSwitchTimer = nil;
	}
	gBookedTier = 0;
	gBookedRow = -1;
	gBookedOn = 0;
	gKept = std::vector<KeptTier>();
	gLive = LiveTier();
	gLastFound = IDFile();
	gLastFoundSet = 0;
	gDrawnSets = std::vector<uint32>();
	gLiveOpen = LiveOpen();
	gDrawnLive = 0;
	gScrollHold = false;
}

// End, KFCResultHistory.cpp.
