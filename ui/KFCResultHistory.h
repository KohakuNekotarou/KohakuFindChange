//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RESULTS KEPT IN TIERS (docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md - the author's calls (a) to
//  (e), and three more the same day: the tiers are always kept - no toggle -, at most 10 of them, each named after its
//  query). A search into the panel - the panel's Find and the query dialog's Find - puts the list it replaces above the new
//  one as a TIER: a copy of its rows as they were, drawn grey and nothing more (never selected, no jump, no menu, not
//  followed by Ctrl+Z). The list the model holds is the LIVE tier, under them, and works as the list always has. Clear
//  Results, a Run of the query dialog and Change All in Book (No List) take the tiers away when they make a list anew;
//  past 10 kept tiers the oldest goes, and the status line says so.
//
//  Kept here, and only here: the kept tiers (each its row's name and a copy of its rows, made with the rows' own look -
//  KFCResultTree::GetRowLook - so a kept row reads as the row it was); the live tier's name and the list it names
//  (IKFCResults::GetResultSetId - a new number with every list made anew, which is also how a search that went through is
//  told from one turned away); and where the query dialog's Find is in the run order.
//  UI half only, main thread only. Nothing of the model changes: the live tier is the model's list itself.
//
//========================================================================================

#ifndef __KFCResultHistory_h__
#define __KFCResultHistory_h__

#include "PMString.h"

#include "KFCResultTree.h"		// RowLook - what a kept row looks like

class ITreeViewMgr;

namespace KFCResultHistory
{
	// ----- the searches -----

	/** A SEARCH INTO THE PANEL - the one door the panel's Find (KFCActionComponent) and the query dialog's
	    (KFCQueryDialog.cpp) go through. The panel's tab name written first (KFCPanelTitle::Update), then the search
	    (KFCRuns()->SearchBook). When it went through - the list made anew - the list it replaced is kept as a tier (as long
	    as it has a row, or was a tier itself), the oldest tier let go past the cap, and `label` names the new list's tier.
	    Then the list drawn again (KFCResultTree::Rebuild) and the status line set to the search's words - with a word on
	    the tier let go, if one was. Returns the search's answer (the matches found); *outCommitted, when given, says whether
	    the search went through. */
	int32 Find(const PMString& label, PMString& outSummary, bool* outCommitted = nil);

	/** A tier's name - the query Find/Change holds now, in the author's words (10-14): "[<tab>] Find:"<find>" Change:"<change>"",
	    each side as the Application Bar's field would show it (KFCFindChangeQueryWords / KFCFindChangeChangeWords) and cut
	    at 200 characters; "[<tab>]" alone for a tab with nothing to say in words (Object). The panel's Find names its tier
	    by it; the query dialog's Find puts the query's place in the run order in front. */
	PMString QueryLabel();

	/** A LIST MADE ANEW ELSEWHERE - a Run of the query dialog, Change All in Book (No List): the tiers go when the list the
	    model holds is not the one it held before (`setBefore` = KFCResults()->GetResultSetId() before it ran). A run turned
	    away leaves them. */
	void DropStackIfRemade(uint32 setBefore);

	/** Clear Results (the spec's section 2-4) and the drops above: the kept tiers, the live tier's name and the query
	    dialog's place go. True = there was something to take away (IsStacking, before). */
	bool DropStack();

	/** Is the list drawn in tiers now - a tier kept, or the live list made by a search that keeps? The one question the
	    drawing, the hierarchy and Clear Results' grey ask. */
	bool IsStacking();

	/** Is there a live list to put under the kept tiers - a search's (HasRun), a book row, a document row? */
	bool HasLiveList();

	// ----- the query dialog's Find (the spec's section 2-1) -----

	/** The run order's query the next Find searches (0 = the first). */
	int32 NextQueryIndex();
	/** ...moved on by one: the query searched, or stepped over. */
	void StepQueryIndex();
	/** ...back to the first: the run order changed (KFCQueryDialog.cpp), or the tiers went (DropStack). */
	void RestartQueryFind();

	// ----- what the list draws (KFCResultListAdapter, KFCResultListWidgetMgr) -----

	/** How many tiers are kept. */
	int32 KeptTierCount();

	/** The live tier's name: the label of the search that made the list the model holds, or "Results" for a list KFC did not
	    name (one an Undo put back, say). */
	PMString LiveTierLabel();

	/** Kept tier `tier`'s copy as a tree: how many rows hang under `row` (-1 = under the tier's own row), the nth of them
	    (its row number, -1 when there is none), the row above `row` (-1 = the tier's row) and its place there. Each answers
	    "nothing" for a tier or a row that is not there - the tree can ask about a row of a tier just taken away before it
	    is drawn again. */
	int32 KeptChildCount(int32 tier, int32 row);
	int32 KeptChild(int32 tier, int32 row, int32 nth);
	int32 KeptParent(int32 tier, int32 row);
	int32 KeptChildPos(int32 tier, int32 row);

	/** The look of kept tier `tier`'s own row (`row` -1) or of a row of its copy. false when there is none. */
	bool GetKeptLook(int32 tier, int32 row, KFCResultTree::RowLook& out);
	/** Is that kept row a hit row - drawn in the colour cell? */
	bool IsKeptHit(int32 tier, int32 row);

	/** BEFORE THE TREE FORGETS ITS ROWS (KFCResultTree::Rebuild's ClearTree): which kept tiers are open - asked only of the
	    tiers the last rebuild drew (a tier kept since was the live one then, and comes up closed). The tree still numbers
	    them as it drew them, so the tiers the cap has let go since are passed over and the rest found where they moved. */
	void NoteOpenTiers(ITreeViewMgr* treeMgr);
	/** ...and after the tree is put back: those tiers opened, and in every kept tier the rows that were open when it was
	    kept. Returns how many rows it opened (a test build's TREETIME counts them). */
	int32 OpenKeptRows(ITreeViewMgr* treeMgr);

	/** The controlled shutdown (KFCUIStartupShutdown): every static here holding storage emptied by a fresh value
	    (KFCResultTree::ShutdownCleanup's rule - Clear() keeps a container's storage). */
	void ShutdownCleanup();
}

#endif // __KFCResultHistory_h__

// End, KFCResultHistory.h.
