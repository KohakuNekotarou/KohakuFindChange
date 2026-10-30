//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RESULTS KEPT IN TIERS (docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md - the author's calls (a) to
//  (e), and three more: the tiers are always kept - no toggle -, at most 10 of them, each named after its
//  query). A search into the panel - the panel's Find and the query dialog's Find - puts the list it replaces above the new
//  one as a TIER: a copy of its rows as they were, drawn grey (never selected, no jump, no menu, not followed by Ctrl+Z).
//  The list the model holds is the LIVE tier, under them, and works as the list always has. Clear Results, a Run of the
//  query dialog and Change All in Book (No List) take the tiers away when they make a list anew; past 10 kept tiers the
//  oldest goes, and the status line says so.
//  A CLICK ON A GREY ROW - OR ON A GREY TIER'S OWN ROW - SWITCHES TO ITS TIER
//  (docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md - the author's calls): the model puts that tier's
//  list back (KFCTierStore::SwitchTo - Find/Change is left alone), the row clicked is selected and jumped to as a click
//  on a live row is, and nothing moves: the tiers stand in search order, the live one among them (LiveTierPosition),
//  each row open or closed as it was, the view where it was.
//
//  Kept here: the grey copies (each its tier's name, its row's text and a copy of its rows, made with the rows' own look -
//  KFCResultTree::GetRowLook - so a kept row reads as the row it was), each under its list's result set; the live tier's
//  name and the list it names (IKFCResults::GetResultSetId - a new number with every list made anew, which is also how a
//  search that went through is told from one turned away); and the query the query dialog's Find searched last, with
//  the list it made (the same query is not searched twice in a row - QueryJustFound).
//  THE TIERS THEMSELVES - which lists are kept, their order, the cap - are the MODEL's (KFCTierStore, read through
//  IKFCRuns::GetKeptTierSets): the copies follow them. UI half only, main thread only.
//
//========================================================================================

#ifndef __KFCResultHistory_h__
#define __KFCResultHistory_h__

#include "PMString.h"
#include "IDFile.h"				// QueryJustFound / NoteQueryFound - a saved query, by its file

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

	/** NARROW DOWN (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md - a tier heading's submenu): the live list's
	    search run once more with its conditions and `step` (KFCRuns()->NarrowList) into a NEW list, carried as Find carries a
	    search's: the list it narrowed kept as a tier when the model kept it, the copies following the model's tiers, the new
	    list named as the one it narrowed (its conditions are drawn after the name - LiveTierLabel - not kept in it: the name
	    is what QueryReadsAsTier reads the query against). The list drawn again, the status line set. True = a new list. */
	bool Narrow(const KFCNarrowStep& step, PMString& outSummary);

	/** SEARCH TEXT IN THESE OBJECTS (docs/superpowers/specs/2026-10-21-kfc-text-in-object-results-design.md - a tier heading's
	    right-click menu): the text inside the live list's objects searched with the query Find/Change holds now
	    (KFCRuns()->SearchTextInObjects) into a NEW list, carried as Find carries a search's: the list of objects kept as a tier
	    when the model kept it, the copies following the model's tiers, the new list named as Find names one (QueryLabel - its
	    heading draws "Objects" in the scope's place, WithScope). The list drawn again, the status line set. True = a new list. */
	bool SearchTextInObjects(PMString& outSummary);

	/** Are tier `tier`'s rows object rows - an Object tab search's, a QR Code tab search's, a Frames of These Matches
	    list's (-1 = the live list, asked of the model; a grey one as its copy noted it)? The heading's Narrow Down
	    greys a text-only condition by it, and its Search Text in These Objects is grey over text rows (and over
	    QR codes - TierHoldsQRCodes). */
	bool TierHoldsObjects(int32 tier);

	/** Are tier `tier`'s rows QR codes - a QR Code tab search's (-1 = the live list, asked of the model; a grey one as its
	    copy noted it)? Its heading's Narrow Down and Search Text in These Objects are grey over it (QR-18: its
	    rows are only gone to). */
	bool TierHoldsQRCodes(int32 tier);

	/** Is tier `tier` a query run's list (-1 = the live list, asked of the model; a grey one as its copy noted it)? Its heading's
	    Narrow Down and Search These Results Again are grey over it: no search made it (the run list spec's R12). */
	bool TierHoldsRunList(int32 tier);

	/** Does the tier (-1 = the live list) hold a list of a tab of KFC's with a Change side (IKFCResults::ListHasOwnChangeTab when it was kept)? Its heading's Narrow Down greys. */
	bool TierHoldsOwnChangeTab(int32 tier);

	/** A tier's name - the query Find/Change holds now, in the author's words (F and C rather than Find and Change):
	    "[<tab>] F:"<find>" C:"<change>"", each side as the Application Bar's field would show it
	    (KFCFindChangeQueryWords / KFCFindChangeChangeWords) and cut at 200 characters; "[<tab>]" alone for a tab with
	    nothing to say in words (Object) and for KFC's QR Code tab, which has no query ("[QR Code]"). The panel's Find
	    names its tier by it; the query dialog's Find puts the query's place in the run order in front. */
	PMString QueryLabel();

	/** The same name for the query Find/Change HOLDS now, on InDesign's own tab - also while KFC's QR Code tab is shown,
	    whose mode is still the tab shown before it (IKFCRuns::FindChangeTabAndScope): what a list's name is read against once
	    its query is stated for a row operation (QueryReadsAsTier), and what the live tier is renamed to after a replace
	    (RenameLiveFromQuery). QueryLabel's own answer whenever the QR Code tab is not shown. */
	PMString HeldQueryLabel();

	/** A LIST MADE ANEW ELSEWHERE - a Run of the query dialog, Change All in Book (No List): the tiers go when the list the
	    model holds is not the one it held before (`setBefore` = KFCResults()->GetResultSetId() before it ran). A run turned
	    away leaves them. THE LIST IT REPLACED KEEPS ITS NAME (the author's call): the live tier's name and the query
	    dialog's last Find stay, each under its result set, unread until that list is the live one again - a query run's
	    Ctrl+Z puts it back (KFCUndoFollow), named as it was. */
	void DropStackIfRemade(uint32 setBefore);

	/** Clear Results (the spec's section 2-4), and the drops above: the kept tiers, the live tier's name and the query
	    dialog's place go (DropStackIfRemade puts the name and the place back after it). True = there was something to
	    take away (IsStacking, before). */
	bool DropStack();

	/** Is the list drawn in tiers now - a tier kept, or the live list made by a search that keeps? The one question the
	    drawing, the hierarchy and Clear Results' grey ask. */
	bool IsStacking();

	/** Is there a live list to put under the kept tiers - a search's (HasRun), a book row, a document row? */
	bool HasLiveList();

	// ----- the query dialog's Find (the author's call: the Run Order's selected row) -----

	/** THE SAME QUERY TWICE IN A ROW (the author's call): did the query dialog's last Find search `file` (FileUtils::IsEqual
	    - a saved query is its file), and is the list it made the panel's live list still? True = the Find is refused - its
	    list would be the one on the panel again. A search since (the panel's, another query's), a Run, Clear Results or a
	    switch to another tier makes another list live, and the query can be searched again - until a Run's Ctrl+Z makes
	    its list the live one again (DropStackIfRemade kept the note). */
	bool QueryJustFound(const IDFile& file);
	/** ...noted after a Find of `file` that went through: the query, and the list it made (the live list's set). */
	void NoteQueryFound(const IDFile& file);

	// ----- what the list draws (KFCResultListAdapter, KFCResultListWidgetMgr) -----

	/** How many tiers are kept. */
	int32 KeptTierCount();

	/** The live tier's name as its row draws it: the label of the search that made the list the model holds, with the
	    Search: it was searched with after the tab ("[Text/Story] F:..." - the author: "easy to read"; drawn, not kept
	    in the name, which QueryReadsAsTier reads the query against), or "Results" for a list KFC did not name (the
	    one a query run leaves, say) - and a narrowed list's conditions after it, drawn so too ("... / In Tables"). */
	PMString LiveTierLabel();

	/** WHERE THE LIVE TIER STANDS AMONG THE TIERS (v2 R4 - the tiers stay in search order, the live one in its place: the
	    author's "the same position"): the number of kept tiers searched before it - their result sets are below the live
	    list's, as every list made anew is numbered upward (KFCResultModel::GetResultSetId). The root's children are the
	    kept tiers in the model's order with the live tier at this place (KFCResultListAdapter). */
	int32 LiveTierPosition();

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

	/** BEFORE THE TREE FORGETS ITS ROWS (KFCResultTree::Rebuild's ClearTree; a switch, before anything moves): which kept
	    tiers are open, and which of their rows - asked only of the tiers the last rebuild drew (a tier kept since was the
	    live one then, and keeps what its copy was made with). The tree still numbers them as it drew them, so each is
	    matched to its copy by the result set it drew it with: a tier gone since (the cap, a switch) is passed over, and the
	    rest are found where they stand now. And the live tier's own row, while the list drawn last is the live one still
	    (OpenLiveTier) - v2 R6: nothing opens or closes by itself. */
	void NoteOpenTiers(ITreeViewMgr* treeMgr);
	/** ...and after the tree is put back: those tiers opened, and in every kept tier the rows that were open as noted.
	    Returns how many rows it opened (a test build's TREETIME counts them). */
	int32 OpenKeptRows(ITreeViewMgr* treeMgr);
	/** ...and the live tier (v2 R6): its own row open or closed as noted (open when nothing was noted - a list drawn for the
	    first time), and - after a switch - its rows open as the tier's copy had them (each copy row knows the live row it
	    was made from). True = the rows were opened so, and Rebuild leaves them; false = Rebuild opens them by its rules (a
	    new search's list, and every other rebuild). ioExpands counts the rows it opened (TREETIME). */
	bool OpenLiveTier(ITreeViewMgr* treeMgr, int32& ioExpands);
	/** Did a switch ask the rebuild to leave the view where it is (v2 - the row clicked stays where it was)? Asked once:
	    the answer is taken. False = the rebuild brings the live tier to the top, as after a search. */
	bool TakeScrollHold();
	/** ...and a rebuild asked to leave the view where it is by anything else that draws the list anew with nothing of it
	    moved but the rows it took away (Clear from Results - KFCResultTree::ClearRows): the next rebuild's TakeScrollHold answers
	    true. The caller puts the view back itself - and calls this only while the panel shows the list (else no rebuild draws
	    it and takes the answer). */
	void HoldScrollForNextRebuild();
	/** THE ROW AT THE TOP OF THE LIST'S VIEW (the row widget standing highest) - invalid when no row is drawn (an empty list,
	    the panel closed). What a rebuild that keeps the view puts back at the top (a switch, Clear from Results). */
	NodeID_rv ViewTopRow();

	// ----- the tier switch (docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md section 3-4) -----

	/** A CLICK ON KEPT TIER `tier`'s ROW `row` - `row` -1 for the tier's own row (v2 R5) - (KFCResultNodeEH): the switch
	    booked to run at the next idle (an ICallbackTimer of 1 ms - v2 R7: no double click's wait, KFC has no double click
	    of its own), not inside the row's own click handler: the switch draws the list anew, and ITreeViewMgr::ClearTree
	    removes every row's widget, this row's with it. A double click's second click lands where the first did - the
	    tiers do not move (R4) - on the row it switched to, and lands on it again. The tier is booked by its result set
	    and the click by the list it was made on: a tier gone by then, a list made anew since (a search, Clear Results), the
	    panel closed or a landing under way drop the booking (RunPendingSwitch) - the click is asked again, as a second
	    click is (KFCActionComponent's Find). */
	void SwitchToKeptRowLater(int32 tier, int32 row);

	/** THE SWITCH: kept tier `tier` made the live list (IKFCRuns::SwitchToTier - the model puts its rows back; Find/Change
	    is left alone, v2 R1). Refused before the model is asked: a run of ours under way, a landing under way, a tier or
	    row not there. Switched: the copies follow the model's tiers (the list before goes in as a grey tier at its place in
	    search order, when the model kept it), the list is drawn again with every row open or closed as it was and the view
	    where it was, and the status line says "Switched to: <name>". `row` -1 = the tier's own row was clicked: nothing to
	    land on (the booked switch selects the tier's own row then). `outLiveRow` = the row of the list now that the copy's
	    row `row` was copied from (a kept row carries it),
	    or an invalid NodeID when there is none (its chapter emptied while it was grey - a document closed - or the tier's
	    own row). False = refused; the status line says why (nothing for a landing - dropped, as a second click is). */
	bool SwitchToKeptRow(int32 tier, int32 row, NodeID& outLiveRow);

	/** THE THIRD CHECK OF A LIST'S QUERY STATED FOR A ROW OPERATION, ANSWERED HERE (IKFCUIServices::QueryReadsAsTier - the
	    model's KFCQueryScope asks it after the query is stated): does the query Find/Change holds now read, word for word,
	    as list `set`'s name - the live list's, or a kept tier's - HeldQueryLabel() against the name, the query dialog's place in
	    the run order ("<n>  ") taken off it? outName = that name as the list's row shows it. False = it does not, or there
	    is no list of that set, or no search here named it. */
	bool QueryReadsAsTier(uint32 set, PMString& outName);

	/** v2 R3, the UI half's side (IKFCUIServices::LiveQueryRecopied): the live list `set` named again from the query
	    Find/Change holds now - its place in the query dialog's run order kept - after a replace made with Find/Change as
	    it is took the list's copy again, so the heading says what a later replace under it would write. Nothing for a set
	    that is not the live list's, or a live list no search here named. */
	void RenameLiveFromQuery(uint32 set);

	/** CLEAR THESE RESULTS (v2 R9 - a tier's own row's right-click menu): tier `tier` alone let go - a grey one (its copy and
	    the model's tier: IKFCRuns::DropTier), or -1, the live list (the model drops it as Clear Results drops a list; the
	    grey tiers stay, to be switched to). The list drawn again, the rest open or closed as they were, and the status line
	    says "Cleared: <name>". False = nothing let go (a tier not there, a run of ours under way - the model refuses). */
	bool ClearTier(int32 tier);

	/** A CLOSED DOCUMENT'S ROWS OUT OF A GREY TIER (the author: "the title part stays - only the closed document's part
	    goes"; the model emptied the chapter - KFCTierStore::ForgetDocument): kept tier `set`'s copy lets go of chapter
	    `chapterIdx`'s document row and the rows under it - the tree told first, where the last rebuild drew that tier -
	    and its heading reads `replaced` of `hits`, the kept list's counts now. The tier stays. Nothing but the counts
	    when the copy has no such row; nothing at all when no copy has that result set. */
	void KeptChapterGone(uint32 set, int32 chapterIdx, int32 replaced, int32 hits);

	/** A DOCUMENT SAVED AS ANOTHER FILE, IN A GREY TIER (KFCTierStore::RenameDocument - the author's call: the copy
	    reads the new name at once, not at the next switch): kept tier `set`'s copy gives chapter `chapterIdx`'s
	    document row `name` - its counts and note as they were drawn - and the tree redraws that row where the last
	    rebuild drew the tier. Nothing when no copy has that result set, or the copy has no such row. */
	void KeptChapterRenamed(uint32 set, int32 chapterIdx, const PMString& name);

	/** The controlled shutdown (KFCUIStartupShutdown): every static here holding storage emptied by a fresh value
	    (KFCResultTree::ShutdownCleanup's rule - Clear() keeps a container's storage), and the switch's timer stopped and let
	    go (its callback is a raw pointer into this .pln). */
	void ShutdownCleanup();
}

#endif // __KFCResultHistory_h__

// End, KFCResultHistory.h.
