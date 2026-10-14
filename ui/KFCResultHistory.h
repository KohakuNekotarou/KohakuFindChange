//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RESULTS KEPT IN TIERS (docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md - the author's calls (a) to
//  (e), and three more the same day: the tiers are always kept - no toggle -, at most 10 of them, each named after its
//  query). A search into the panel - the panel's Find and the query dialog's Find - puts the list it replaces above the new
//  one as a TIER: a copy of its rows as they were, drawn grey (never selected, no jump, no menu, not followed by Ctrl+Z).
//  The list the model holds is the LIVE tier, under them, and works as the list always has. Clear Results, a Run of the
//  query dialog and Change All in Book (No List) take the tiers away when they make a list anew; past 10 kept tiers the
//  oldest goes, and the status line says so.
//  A CLICK ON A GREY ROW SWITCHES TO ITS TIER (docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md - the author's
//  call): the model puts that tier's list back with its query (KFCTierStore::SwitchTo), and the row clicked is selected and
//  jumped to as a click on a live row is.
//
//  Kept here: the grey copies (each its tier's name, its row's text and a copy of its rows, made with the rows' own look -
//  KFCResultTree::GetRowLook - so a kept row reads as the row it was), each under its list's result set; the live tier's
//  name and the list it names (IKFCResults::GetResultSetId - a new number with every list made anew, which is also how a
//  search that went through is told from one turned away); and where the query dialog's Find is in the run order.
//  THE TIERS THEMSELVES - which lists are kept, their order, the cap - are the MODEL's (KFCTierStore, read through
//  IKFCRuns::GetKeptTierSets): the copies follow them. UI half only, main thread only.
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
	    (KFCRuns()->SearchBook). When it went through - the list made anew - the list it replaced is kept as a tier when the
	    model kept it (KFCTierStore::ParkCurrent), the copies follow the model's tiers (the oldest let go past the cap), and
	    `label` names the new list's tier.
	    Then the list drawn again (KFCResultTree::Rebuild) and the status line set to the search's words - with a word on
	    the tier let go, if one was. Returns the search's answer (the matches found); *outCommitted, when given, says whether
	    the search went through. */
	int32 Find(const PMString& label, PMString& outSummary, bool* outCommitted = nil);

	/** A tier's name - the query Find/Change holds now, in the author's words (10-14; F and C rather than Find and Change,
	    the same night): "[<tab>] F:"<find>" C:"<change>"",
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
	    them as it drew them, so each is matched to its copy by the result set it drew it with: a tier gone since (the cap, a
	    switch) is passed over, and the rest are found where they stand now. */
	void NoteOpenTiers(ITreeViewMgr* treeMgr);
	/** ...and after the tree is put back: those tiers opened, and in every kept tier the rows that were open when it was
	    kept. Returns how many rows it opened (a test build's TREETIME counts them). */
	int32 OpenKeptRows(ITreeViewMgr* treeMgr);

	// ----- the tier switch (docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md section 3-5) -----

	/** A CLICK ON KEPT TIER `tier`'s ROW `row` (KFCResultNodeEH): the switch booked to run once the double click's time is
	    over (an ICallbackTimer, ::GetDoubleClickTime) - not inside the row's own click handler: the switch draws the list
	    anew, and ITreeViewMgr::ClearTree removes every row's widget, this row's with it. Waiting the double click's time out
	    also keeps a double click one switch: its second click lands on the same grey row, not on the list drawn anew, and
	    books the same switch again. The tier is booked by its result set and the click by the list it was made on: a tier
	    gone by then, a list made anew since (a search, Clear Results), the panel closed or a landing under way drop the
	    booking (RunPendingSwitch) - the click is asked again, as a second click is (KFCActionComponent's Find). */
	void SwitchToKeptRowLater(int32 tier, int32 row);

	/** THE SWITCH: kept tier `tier` made the live list (IKFCRuns::SwitchToTier - the model puts its rows and its query back
	    and checks the query three times, the third asked of this half: QueryReadsAsTier). Refused before the model is
	    asked: a run of ours under way, a landing under way, a tier or row not there, or a tier no search here named (there
	    is no name to read its query against). Switched: the copies follow the model's tiers (the list before is the newest
	    grey tier, when the model kept it), the list is drawn again, and the status line says "Switched to: <name>".
	    `outLiveRow` = the row of the list now that the copy's row `row` was copied from (a kept row carries it), or an
	    invalid NodeID when it is not in the list now (its chapter emptied while it was grey - a document closed). False =
	    refused; the status line says why (nothing for a landing - dropped, as a second click is). */
	bool SwitchToKeptRow(int32 tier, int32 row, NodeID& outLiveRow);

	/** THE THIRD CHECK OF A LIST'S QUERY STATED FOR A ROW OPERATION, ANSWERED HERE (IKFCUIServices::QueryReadsAsTier - the
	    model's KFCQueryScope asks it after the query is stated): does the query Find/Change holds now read, word for word,
	    as list `set`'s name - the live list's, or a kept tier's - QueryLabel() against the name, the query dialog's place in
	    the run order ("<n>  ") taken off it? outName = that name as the list's row shows it. False = it does not, or there
	    is no list of that set, or no search here named it. */
	bool QueryReadsAsTier(uint32 set, PMString& outName);

	/** v2 R3, the UI half's side (IKFCUIServices::LiveQueryRecopied): the live list `set` named again from the query
	    Find/Change holds now - its place in the query dialog's run order kept - after a replace made with Find/Change as
	    it is took the list's copy again, so the heading says what a later replace under it would write. Nothing for a set
	    that is not the live list's, or a live list no search here named. */
	void RenameLiveFromQuery(uint32 set);

	/** The controlled shutdown (KFCUIStartupShutdown): every static here holding storage emptied by a fresh value
	    (KFCResultTree::ShutdownCleanup's rule - Clear() keeps a container's storage), and the switch's timer stopped and let
	    go (its callback is a raw pointer into this .pln). */
	void ShutdownCleanup();
}

#endif // __KFCResultHistory_h__

// End, KFCResultHistory.h.
