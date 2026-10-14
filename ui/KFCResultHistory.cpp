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
#include "IControlView.h"
#include "ITreeViewMgr.h"			// IsNodeExpanded / ExpandNode - the kept tiers opened as they were

// General includes:
#include <utility>
#include <vector>

// Project includes:
#include "KFCResultHistory.h"
#include "KFCAppBarSearchEnter.h"	// KFCFindChangeQueryWords / ChangeWords - a tier's name
#include "KFCDiag.h"				// the STACK line and the UIOBS counters - test builds only
#include "KFCModelAccess.h"			// the model half, through its session interfaces
#include "KFCPanelTitle.h"			// Update - the tab's scope written before the search
#include "KFCResultNodeID.h"
#include "KFCTextCut.h"				// KFCSafeCut - where the tier name's words may be cut

namespace
{
	// A tier's name keeps this many characters of each side of the query: a GREP pattern can run to thousands, and the row
	// ends in "..." where the panel's width runs out in any case.
	const int32 kLabelWordsMax = 200;

	// THE MOST TIERS KEPT (the author's calls, 10-14: the oldest let go past a cap, a word on the status line when one goes -
	// and the cap 10, the 50 first proposed being too many searches to keep). A tier is a copy of one list, and a list holds
	// at most kKFCCollectHitLimit rows (300) and the rows above them, so the cap bounds what the copies can hold - and the
	// tiers are kept by every search now, with no toggle.
	const int32 kKeptTiersMax = 10;

	/** A ROW OF A KEPT TIER: its look as it was drawn, where it hangs, and whether it was open. */
	struct KeptRow
	{
		KFCResultTree::RowLook	look;
		int32					parent;		// the row above it in the copy; -1 = the tier's own row
		int32					pos;		// its place under that row
		bool					open;		// a branch row the list had open when it was kept
		std::vector<int32>		children;	// the rows under it, in order

		KeptRow() : parent(-1), pos(0), open(false) {}
	};

	/** A KEPT TIER: what its row reads, its copy, and whether the person has it open. */
	struct KeptTier
	{
		PMString				label;		// "<name>  (R/N)" as the tier's row read when it was kept
		std::vector<KeptRow>	rows;
		std::vector<int32>		top;		// the rows under the tier's own row, in order
		bool					open;		// the tier's row open - noted before each rebuild (NoteOpenTiers)

		KeptTier() : open(false) {}
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

	std::vector<KeptTier> gKept;
	LiveTier gLive;
	int32 gNextQuery = 0;		// the query dialog's Find: the run order's query it searches next
	int32 gDrawnTiers = 0;		// how many kept tiers the last rebuild drew (NoteOpenTiers asks those alone)
	int32 gDroppedSinceDrawn = 0;	// how many of those the cap has let go since - the front ones (NoteOpenTiers skips them)

	// PAST THE CAP, THE OLDEST TIERS GO - from the front, where the oldest stand. Counted, because the tree still numbers the
	// tiers as the last rebuild drew them (NoteOpenTiers). Returns how many went.
	int32 LetGoOldest()
	{
		const int32 over = static_cast<int32>(gKept.size()) - kKeptTiersMax;
		if (over <= 0)
			return 0;
		gKept.erase(gKept.begin(), gKept.begin() + over);
		gDroppedSinceDrawn += over;
		return over;
	}

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
	// (OpensWhenDrawn).
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

	// THE LIVE LIST COPIED AS A TIER, before a search that keeps throws it away (Find). false = nothing to keep: no live list,
	// or one with no row that was not a tier itself (a Run's empty list) - a tier with no row is kept, for a query that
	// found nothing says so.
	bool TakeSnapshot(KeptTier& out)
	{
		if (!KFCResultHistory::HasLiveList())
			return false;
		KFCResultTree::RowLook head;
		if (!KFCResultTree::GetRowLook(KFCResultNodeID::CreateLiveTier(), head))
			return false;
		out.label = head.label;
		InterfacePtr<ITreeViewMgr> treeMgr(QueryListTreeMgr());
		CopyRows(treeMgr, KFCResultNodeID::CreateLiveTier(), -1, out);
		return !out.rows.empty() || LiveIsStacked();
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
	// KFCSearchEngine::SearchBook) and kept only when the search goes through.
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
	int32 letGo = 0;
	if (committed)
	{
		if (havePending)
		{
			gKept.push_back(std::move(pending));
			letGo = LetGoOldest();
		}
		gLive.label = label;
		gLive.label.SetTranslatable(kFalse);
		gLive.set = KFCResults()->GetResultSetId();
		gLive.stacked = true;
	}
	KFC_DIAG_LOG("STACK find went=%d kept=%d letgo=%d tiers=%d", committed ? 1 : 0, (committed && havePending) ? 1 : 0,
		static_cast<int>(letGo), static_cast<int>(gKept.size()));
	KFCResultTree::Rebuild();
	// The status line: the search's words - and, when the cap let a tier go, a word on it after them (the panel's line
	// only: outSummary goes back to the caller as the search's own words, which the query dialog quotes).
	PMString status(outSummary);
	status.SetTranslatable(kFalse);
	if (letGo > 0)
	{
		status.Append(" The oldest kept results were let go - ");
		status.AppendNumber(kKeptTiersMax);
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
	// Fresh values, not Clear(): the storage goes with them.
	gKept = std::vector<KeptTier>();
	gLive = LiveTier();
	gNextQuery = 0;
	gDrawnTiers = 0;
	gDroppedSinceDrawn = 0;
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
	// The tree's tier t is the kept tier t - gDroppedSinceDrawn now: the cap let the front ones go since it was drawn.
	for (int32 t = 0; t < gDrawnTiers; ++t)
	{
		const int32 kept = t - gDroppedSinceDrawn;
		if (kept >= 0 && kept < KeptTierCount())
			gKept[static_cast<size_t>(kept)].open = (treeMgr->IsNodeExpanded(KFCResultNodeID::CreateTier(t)) != kFalse);
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
	gDrawnTiers = KeptTierCount();
	gDroppedSinceDrawn = 0;
	return opened;
}

void KFCResultHistory::ShutdownCleanup()
{
	gKept = std::vector<KeptTier>();
	gLive = LiveTier();
	gNextQuery = 0;
	gDrawnTiers = 0;
	gDroppedSinceDrawn = 0;
}

// End, KFCResultHistory.cpp.
