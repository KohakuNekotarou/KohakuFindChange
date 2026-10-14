//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The results kept in tiers - see KFCResultHistory.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICallbackTimer.h"				// StartTimer / StopTimer - a click's switch, run once the double click's time is over
#include "IControlView.h"
#include "ITreeViewController.h"		// DeselectAll / Select - the row switched to, selected
#include "ITreeViewHierarchyAdapter.h"	// GetParentNode - the rows above it opened, so the tree draws it
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
		PMString				name;		// the name a search here gave that list (Find's label) - empty when none did (a list
											// an Undo put back): the switch's third check reads the query against it
		PMString				label;		// "<name>  (R/N)" as the tier's row read when it was kept
		std::vector<KeptRow>	rows;
		std::vector<int32>		top;		// the rows under the tier's own row, in order
		bool					open;		// the tier's row open - noted before each rebuild (NoteOpenTiers)

		KeptTier() : set(0), open(false) {}
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
	int32 gNextQuery = 0;				// the query dialog's Find: the run order's query it searches next
	std::vector<uint32> gDrawnSets;		// the kept tiers the last rebuild drew, by result set, in the tree's order:
										// NoteOpenTiers finds each where it stands now (a tier gone since is passed over)

	// A CLICK'S SWITCH, BOOKED (SwitchToKeptRowLater) and run by gSwitchTimer once the double click's time is over.
	ICallbackTimer* gSwitchTimer = nil;
	uint32 gBookedTier = 0;		// the tier clicked, by its result set (0 = nothing booked - the model counts its lists from 1)
	int32 gBookedRow = -1;		// the row clicked, in that tier's copy
	uint32 gBookedOn = 0;		// the live list's result set at the click: a list made anew since lets the booking go
	bool gShutDown = false;		// once ShutdownCleanup has run, nothing is booked again

	// Is the list the model holds now the one a search that keeps made?
	bool LiveIsStacked()
	{
		return gLive.stacked && gLive.set != 0 && gLive.set == KFCResults()->GetResultSetId();
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

	// The result list's tree - nil while the panel is closed (KFCResultTree::ResultListView).
	ITreeViewMgr* QueryListTreeMgr()
	{
		IControlView* const listView = KFCResultTree::ResultListView();
		return (listView != nil) ? InterfacePtr<ITreeViewMgr>(listView, UseDefaultIID()).forget() : nil;
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
		out.name = LiveIsStacked() ? gLive.label : PMString();
		out.name.SetTranslatable(kFalse);
		out.label = head.label;
		InterfacePtr<ITreeViewMgr> treeMgr(QueryListTreeMgr());
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
	if (committed && havePending)
		gKept.push_back(std::move(pending));
	// THE COPIES FOLLOW THE MODEL'S TIERS - after every search, whether it went through or not (the plan's Review Focus 5:
	// the copies and the model's tiers must not drift apart): the list replaced is kept or not as the model kept it
	// (KFCTierStore::ParkCurrent - one answer, in one place), and past the cap the model let the oldest go.
	const int32 went = FollowModelTiers();
	const bool keptPending = committed && havePending && !gKept.empty() && gKept.back().set == setBefore;
	// The copies that went are the oldest, let go by the cap - all of them but the one offered, when the model did not keep
	// it.
	const int32 letGo = went - ((committed && havePending && !keptPending) ? 1 : 0);
	if (committed)
	{
		gLive.label = label;
		gLive.label.SetTranslatable(kFalse);
		gLive.set = KFCResults()->GetResultSetId();
		gLive.stacked = true;
	}
	KFC_DIAG_LOG("STACK find went=%d kept=%d letgo=%d tiers=%d", committed ? 1 : 0, keptPending ? 1 : 0,
		static_cast<int>(letGo), static_cast<int>(gKept.size()));
	KFCResultTree::Rebuild();
	// The status line: the search's words - and, when the cap let a tier go, a word on it after them (the panel's line
	// only: outSummary goes back to the caller as the search's own words, which the query dialog quotes).
	PMString status(outSummary);
	status.SetTranslatable(kFalse);
	if (letGo > 0)
	{
		status.Append(" The oldest kept results were let go - ");
		status.AppendNumber(KFCResultModel::kMaxKeptTiers);
		status.Append(" are kept at most.");
	}
	KFCResultTree::ShowStatus(status);
	if (outCommitted != nil)
		*outCommitted = committed;
	return found;
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
}

PMString KFCResultHistory::QueryLabel()
{
	PMString label("[");
	label.SetTranslatable(kFalse);
	label.Append(KFCRuns()->TabName(KFCRuns()->CurrentSearchMode()));
	label.Append("]");
	PMString findWords, changeWords;
	if (KFCFindChangeQueryWords(findWords))
	{
		label.Append(" Find:\"");
		AppendQueryWords(label, findWords);
		label.Append("\"");
		(void)KFCFindChangeChangeWords(changeWords);		// the same tab, so it answers as the find side did
		label.Append(" Change:\"");
		AppendQueryWords(label, changeWords);
		label.Append("\"");
	}
	return label;
}

void KFCResultHistory::DropStackIfRemade(uint32 setBefore)
{
	if (KFCResults()->GetResultSetId() != setBefore)
		(void)DropStack();
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
	gNextQuery = 0;
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

int32 KFCResultHistory::NextQueryIndex()
{
	return gNextQuery;
}

void KFCResultHistory::StepQueryIndex()
{
	++gNextQuery;
}

void KFCResultHistory::RestartQueryFind()
{
	gNextQuery = 0;
}

int32 KFCResultHistory::KeptTierCount()
{
	return static_cast<int32>(gKept.size());
}

PMString KFCResultHistory::LiveTierLabel()
{
	if (gLive.set != 0 && gLive.set == KFCResults()->GetResultSetId())
	{
		PMString named(gLive.label);
		named.SetTranslatable(kFalse);
		return named;
	}
	PMString unnamed("Results");
	unnamed.SetTranslatable(kFalse);
	return unnamed;
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
		for (size_t k = 0; k < gKept.size(); ++k)
			if (gKept[k].set == gDrawnSets[t])
			{
				gKept[k].open = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateTier(static_cast<int32>(t))) != kFalse);
				break;
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
	// What this rebuild drew, by result set - NoteOpenTiers' map at the next.
	std::vector<uint32> drawn;
	drawn.reserve(gKept.size());
	for (size_t k = 0; k < gKept.size(); ++k)
		drawn.push_back(gKept[k].set);
	gDrawnSets.swap(drawn);
	return opened;
}

bool KFCResultHistory::QueryReadsAsTier(uint32 set, PMString& outName)
{
	outName.Clear();
	outName.SetTranslatable(kFalse);
	for (size_t k = 0; k < gKept.size(); ++k)
		if (gKept[k].set == set)
		{
			outName = gKept[k].name;
			outName.SetTranslatable(kFalse);
			if (gKept[k].name.IsEmpty())
				return false;		// no search here named that list: nothing to read the query against
			// Word for word - PMString's == compares the buffers -: the name as the search made it, the dialog's place in the
			// run order taken off, against the name made again from the query Find/Change holds now.
			return (WithoutQueryPlace(gKept[k].name) == QueryLabel()) != kFalse;
		}
	return false;
}

bool KFCResultHistory::SwitchToKeptRow(int32 tier, int32 row, NodeID& outLiveRow)
{
	outLiveRow = NodeID();
	// THE DOORS, before anything moves: a run of ours (the status line says so), a landing under way (dropped, as a second
	// click is - KFCActionComponent's Find), a tier or a row not there.
	if (KFCResultTree::RefusedWhileRunning() || KFCJump::IsLanding())
		return false;
	const KeptRow* const clicked = RowOf(tier, row);
	if (clicked == nil)
		return false;
	const uint32 set = gKept[static_cast<size_t>(tier)].set;
	PMString name(gKept[static_cast<size_t>(tier)].name);
	name.SetTranslatable(kFalse);
	const NodeID clickedLive(clicked->live);
	// A TIER NO SEARCH HERE NAMED (a list an Undo put back, kept by the next search): there is no name to read its query back
	// against - the switch's third check (QueryReadsAsTier) - so the model is not asked.
	if (name.IsEmpty())
	{
		PMString why("Could not switch: that tier was not named by a search, so its query cannot be checked - search it again.");
		why.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(why);
		KFC_DIAG_LOG("STACK switch refused set=%u (no name)", set);
		return false;
	}

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

	// SWITCHED. The tier is the live list now, under its own name; the list before it is the newest copy (the model's order:
	// KFCTierStore::SwitchTo takes the tier out and puts the list before it in last); then the copies follow the model's
	// tiers - the list before it goes too, when the model did not keep it.
	gLive.label = name;
	gLive.set = set;
	gLive.stacked = true;
	gKept.erase(gKept.begin() + tier);
	if (havePending)
		gKept.push_back(std::move(pending));
	(void)FollowModelTiers();
	KFCResultTree::Rebuild();
	// THE ROW CLICKED, in the list now: the live row its copy was made from, while it is still a row of the list (FindLiveRow).
	// Not there = its chapter emptied while the tier was grey (a document closed): switched, nothing selected.
	if (clickedLive.IsValid() && FindLiveRow(KFCResultNodeID::CreateLiveTier(), clickedLive))
		outLiveRow = clickedLive;
	// The status line names the tier switched to - the query Find/Change holds now, read before a replace (the spec's
	// section 3-3-1, item 7) - and the model's words after it, when it has any.
	PMString status("Switched to: ");
	status.SetTranslatable(kFalse);
	status.Append(name);
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
		int32 tier = -1;
		for (size_t k = 0; k < gKept.size(); ++k)
			if (gKept[k].set == tierSet)
			{
				tier = static_cast<int32>(k);
				break;
			}
		NodeID liveRow;
		if (tier < 0 || !KFCResultHistory::SwitchToKeptRow(tier, row, liveRow) || !liveRow.IsValid())
			return IIdleTask::kEndOfTime;

		// THE ROW SWITCHED TO, SELECTED AND LANDED ON - the arrows' landing (KFCResultTreeEH's GoOnToNextReplaceableRow): every
		// row above it opened, top down, so the tree draws it; the selection taken off and put on it (the official
		// re-selection, docs/ai-notes/api-official-examples.md - Select alone does not move a selection); brought into view;
		// then the jump, and the keyboard to the list - a click on a live row's two (KFCResultNodeEH).
		IControlView* const listView = KFCResultTree::ResultListView();
		InterfacePtr<ITreeViewController> controller(listView, UseDefaultIID());
		InterfacePtr<const ITreeViewHierarchyAdapter> adapter(listView, UseDefaultIID());
		InterfacePtr<ITreeViewMgr> treeMgr(listView, UseDefaultIID());
		if (controller == nil || adapter == nil || treeMgr == nil)
			return IIdleTask::kEndOfTime;
		std::vector<NodeID> above;
		for (NodeID up(adapter->GetParentNode(liveRow)); up.IsValid(); up = NodeID(adapter->GetParentNode(up)))
		{
			TreeNodePtr<KFCResultNodeID> upID(up);
			if (upID == nil || upID->IsRoot())
				break;		// the hidden root is never drawn
			above.push_back(up);
		}
		for (auto it = above.rbegin(); it != above.rend(); ++it)
			treeMgr->ExpandNode(*it, kFalse /*expandAllDescendants*/);
		controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
		(void)controller->Select(liveRow, kTrue /*notifyOfChange*/, kTrue /*changeHilite*/);
		treeMgr->ScrollToNode(liveRow, ITreeViewMgr::eScrollIntoView);
		TreeNodePtr<KFCResultNodeID> rowID(liveRow);
		if (rowID != nil)
			KFCJump::ActivateNode(rowID->GetChapter(), rowID->GetHit());
		(void)KFCResultTree::TakeKeyboard();
		return IIdleTask::kEndOfTime;
	}
}

void KFCResultHistory::SwitchToKeptRowLater(int32 tier, int32 row)
{
	if (gShutDown || RowOf(tier, row) == nil)
		return;
	gBookedTier = gKept[static_cast<size_t>(tier)].set;
	gBookedRow = row;
	gBookedOn = KFCResults()->GetResultSetId();
	if (gSwitchTimer == nil)
		gSwitchTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	// Once the double click's time is over (the declaration says why): a double click's second click books the same switch
	// again, and the timer starts anew.
	if (gSwitchTimer != nil)
		gSwitchTimer->StartTimer(RunPendingSwitch, static_cast<uint32>(::GetDoubleClickTime()), nil);
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
	gNextQuery = 0;
	gDrawnSets = std::vector<uint32>();
}

// End, KFCResultHistory.cpp.
