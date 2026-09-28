//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Replace engine: replaces the hits the user checked in the result panel, using the CHANGE
//  string of the official Find/Change dialog. KBS never writes to IFindChangeOptions - it only
//  reads - so GREP back-references and escapes are interpreted by InDesign's own engine and are
//  never parsed here.
//
//  ***** HOW IT WRITES, SINCE 2026-09-27: ONE TICKED MATCH AT A TIME, A STORY AT A TIME, UNDER TRACK
//  ***** CHANGES (the user's call). ***** ReplaceInChapterOneByOne in the .cpp walks each story that holds
//  a ticked row (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on the match it made current) and writes
//  only the ticked rows, each recognised by its thread, its offset into it and its length - so a match
//  a replacement made that the search never listed is stepped over, and its row is reported missing
//  rather than written (H-8). A GREP query holding ^ is walked backward (the direction is set before
//  anything is written, outside the sequence). The pending tracked changes a ticked match sits in or
//  next to are accepted first; the replaces' own records are LEFT in the document, told apart by their time stamp: they are
//  what Reject Change, Redo and the jump find a row by.
//  (From 2026-09-26 to 2026-09-27 each story got InDesign's Change All instead and the rows NOT ticked
//  were taken back; that went in the 2026-09-27 cleanup - git history, c876bc7 and before.)
//
//  ***** THE OLD CHAPTER WALK'S CHECK IS STILL HERE. ***** Until 2026-09-26 each chapter was re-walked
//  match by match (measured 2026-07-25, docs/superpowers/specs/_done/2026-07-25-kbs-replace-checked-
//  design.md section 10.1). What is left of it is the check that walk ran before anything was written
//  (ChapterMovedUnderRows in the .cpp): each chapter is walked as the search walked it, and every
//  ticked row must still begin where the search found it. (Its writing half went on 2026-09-28.)
//
//========================================================================================

#ifndef __KBSReplaceEngine_h__
#define __KBSReplaceEngine_h__

#include "PMString.h"

namespace KBSReplaceEngine
{
	/** Replace every checked hit in the current result set.

	    The WHOLE run is ONE command sequence, across every chapter, so a book-wide replace undoes
	    with a single Ctrl+Z whichever chapter the user has in front, and cancelling puts the whole
	    book back. Every chapter that has work is opened before the first character is written, and
	    every one stays open and unsaved afterwards. (Corrected 2026-07-28. It used to be one
	    sequence per chapter, on the belief that "undo is per document" made the chapter the largest
	    grain available. Measured on the running application, that arrangement was actively harmful:
	    undoing in one document removed the step from the other chapters' histories as well WITHOUT
	    reverting their text, leaving them replaced with no way back.) The price is that the run is
	    all-or-nothing - an error left standing when the sequence ends rolls back every chapter.

	    ***** A SECOND shape existed from 2026-08-03 to 2026-08-05 ***** - one chapter at a time,
	    for runs that SAVED, so that a book of twenty chapters was never all open at once. It was
	    removed with "save after replace" itself, and could not outlive it: a chapter that has not
	    been written to disk cannot be closed, because closing it would throw its replacements away.
	    A machine that cannot hold a whole book open is served by ticking fewer rows instead
	    (user's decision, 2026-08-05).

	    ***** THE RUN CHECKS THAT THE MATCHES ARE STILL WHERE THE SEARCH FOUND THEM, AND REFUSES TO
	    ***** START IF THEY ARE NOT. (User's design, 2026-08-10.)
	    The rows were found at positions the search recorded, and the run lines what it writes up with
	    them - so if the document has moved since the search in a way that adds, removes or shifts a
	    match, the rows no longer describe it. (This paragraph described the one-at-a-time walk, where
	    the Nth match was replaced for the Nth checked row, until the 2026-09-27 defect sweep; the
	    reason for the check is unchanged.)

	    So each chapter is walked before it is written. That walk writes nothing: for every ticked row
	    it asks whether the row's text is still the text that was ticked, and whether a match of the
	    walk still BEGINS where the row does. One that is not - or one ticked row the walk never
	    reaches - and the whole run stops, with an alert saying so and the results cleared
	    (TellResultsWentStale, and the verify pass in the resolve loop). Only if every chapter passes
	    does the second walk open a command sequence and write. (It lined the Nth match of the walk up
	    with the row numbered N, and did not read the text, until 2026-09-29 - see
	    ChapterMovedUnderRows in the .cpp.)

	    ***** IN THE RESOLVE PASS, AND NOWHERE ELSE. ***** That is the one moment where the question
	    is both answerable and free: the chapter has just been reopened, and NOT ONE CHARACTER has
	    been written yet - so the positions are still the ones the search recorded (no replacements
	    of this run's own to cancel out) and a refusal has nothing to roll back.

	    ***** WHY POSITIONS RATHER THAN A FINGERPRINT OF THE CHAPTER. ***** From 2026-08-08 to
	    2026-08-10 this was a per-chapter record of every story's change counter (KBSEditStamp),
	    warning rather than refusing. It answered "does this chapter look untouched?", which means
	    enumerating every way a document can move - text, stories added or deleted, layers hidden or
	    locked, conditions, master pages - and that list is never finished. Comparing the positions
	    asks about the thing itself: whatever the cause, if a ticked match is not where it was, the
	    row no longer describes it.

	    The same test used to stand INSIDE the replacing walk, per hit, until 2026-08-05. See the
	    note above the walk in KBSReplaceEngine.cpp for why it could not work there and what remains
	    of it (the JUMP still asks it, so a click on a row can still answer "the replacement is no
	    longer here").

	    A checked hit that does not get replaced is ALWAYS counted and named in the summary, never
	    allowed to make the total quietly come up short. The ways that happens since 2026-09-26:
	      - locked: on a locked layer or in a locked story. The Find/Change dialog can be told to
	        search those, but InDesign offers no way to change them ("Search Only"), so KBS follows.
	      - missing: the walk never met the row's match as the search listed it - this run's own
	        replaces moved the text around it (GREP's ^, $ or a lookahead now reads something else).
	      - endnote left: the match ends an endnote, where InDesign's replace breaks the endnote (see
	        MatchEndsAnEndnote in the .cpp); that row alone is left.
	      - refused: the replace command itself declined.
	      - deleted: the row went with a footnote, table or object another ticked row deleted.

	    ***** NOTHING IS EVER SAVED. ***** Every chapter a replacement lands in is left MODIFIED AND
	    UNSAVED, with a window open on it, and the summary says so: overwriting the user's files is
	    the user's own step to take. (The confirmation carried a "save after replace" box from
	    2026-08-02 to 2026-08-05, which is where the second shape above came from.)

	    ***** WHAT IS LEFT OPEN IS EXACTLY WHAT HAS SOMETHING IN IT. ***** A chapter a replacement
	    landed in stays open, gets a window, and is left unsaved - the replacements are in it and only
	    the user can decide about them. Every OTHER chapter this run opened is handed back
	    (KBSBookScope::ReleaseHeldDoc), because each one locks its .indd while it stands and a
	    windowless document cannot even be closed by hand - it is in no menu. Three cases:

	      - a run that is CANCELLED has put every character back, so no chapter holds anything of it
	        and they all go (ReleaseHeldDocs). The search has always done this on its own cancel; the
	        replace did not, between 2026-08-02 and 2026-08-05, because the only path that closed
	        anything was the one that SAVED and it went with "save after replace";
	      - a run that goes THROUGH hands back the chapters no replacement landed in - every checked
	        hit there came back locked, missing, refused or left at an endnote's end. Added 2026-08-05:
	        such a chapter used to stay open, windowless and locked for the rest of the session, and
	        WITH ITS MODIFIED FLAG SET, because a walk can mark a database changed without changing a
	        character (which is why the SEARCH guards its own walk with SaveRestoreModifiedState and
	        this one deliberately does not). That flag then stopped anything from ever closing it;
	      - a run that cannot start its command sequence at all writes nothing and hands back
	        everything, the same way.

	    A chapter the USER had open, or had already edited, is on none of these lists: it was never
	    held, so it is not this run's to close and stays exactly as it was.

	    Refuses to run at all while another replace is up (see IsReplacing), and while the panel is
	    showing a replace's report rather than a work list.

	    @param outSummary OUT a ready-to-show status line (counts, chapters that did not line up).
	    @return the number of hits actually replaced (0 on any early exit). */
	int32 ReplaceChecked(PMString& outSummary);

	/** Do the current Find/Change settings still describe the search the panel's results came from?

	    Two questions, most specific first:
	      1. the TAB - clicking another one returns another set of matches;
	      2. the QUERY and every switch that decides which occurrences come back - the find string,
	         case / whole word / kana / width, the five scope switches, and FIND FORMAT (a paragraph
	         style, a font, a colour). See KBSSearchEngine::BuildWalkSignature.
	    (A third - is that tab one this panel walks at all - stood between them until 2026-09-29. The
	    tab on the results is one the search could state, so it never answered no.)

	    Why it has to be asked at all: the replace walks every story that holds a ticked row with the
	    LIVE IFindChangeOptions and writes the matches it meets at the rows' places - so a query edited
	    between the search and the replace walks a different set of matches from the one the rows
	    list. The verify walk looks for the rows' matches the same way, and the reason is the same for
	    it.

	    ***** ONE OF TWO DOORS, and they divide the ways a run can be wrong between them. *****

	      - the QUERY changing between the search and the replace is caught HERE, before a chapter
	        is even opened, and the run is refused;
	      - the DOCUMENT moving between the two is caught by the verify walk in the resolve pass
	        (see ReplaceChecked above), which is the only place the document itself can be asked.

	    Both refuse before a character is written. This one is first because it is far cheaper - the
	    dialog's own settings are readable and BuildWalkSignature turns them into something
	    comparable, where the other has to walk every chapter.

	    From 2026-08-05 to 2026-08-10 this was the ONLY door: the document side was first left
	    entirely to the user, then merely detected and warned about (KBSEditStamp).

	    ***** TWO SIDE EFFECTS, both deliberate. ***** It STATES the tab (KBSSearchEngine::
	    CommitSearchMode - the walk needs that whatever the answer is, and the comparison has to be
	    taken on the same side of that command as the search took it), and on answer 2 it CLEARS THE
	    RESULTS: they describe a query the dialog no longer holds, so keeping them up would only
	    invite another attempt. Answer 1 leaves them alone - a tab is one click to put back - and so
	    does a tab that could not be stated. A caller that gets true back should redraw the tree.

	    Call it OUTSIDE any command sequence: it processes a command of its own.

	    @param outSummary OUT the reason, ready for the status line. Cleared first.
	    @return true when the run must NOT go ahead. */
	bool RefuseChangedQuery(PMString& outSummary);

	/** RefuseChangedQuery's question WITHOUT ITS CONSEQUENCES (2026-09-29): true when the Find/Change
	    settings still describe the search the results came from. The tab is stated all the same - a
	    walk after this runs in it - and NOTHING IS CLEARED whatever the answer. For a caller that only
	    has to know: the jump's look for a row Undo moved (KBSJump RelocateStaleRow), which asked
	    RefuseChangedQuery until then and on a changed query cleared the whole result set in the middle
	    of a jump. Same rule as RefuseChangedQuery: outside any command sequence. */
	bool QueryUnchangedSinceSearch();

	/** Reject Change on a replaced hit row (2026-09-26): its tracked change - found by
	    KBSTrackChange::FindRowChangeForHit - is rejected, deletion and insertion, in ONE undo step, and
	    the row shows its original text again ("rejected"). All the way back or not at all: when the
	    original text does not stand where the change stood afterwards, the step is rolled back.
	    False = nothing changed; outStatus says why either way. */
	bool RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** Replace on a hit row's right-click menu (2026-09-27, the user's call): that one row, ticked or
	    not, with no prompt, in ONE undo step ("Replace"); the Track Changes note goes in outStatus. The
	    list stays a work list: the row reads "replaced", every other row is moved to where its text now
	    stands. Refused - nothing changed, outStatus says why - when
	    the query changed since the search, the row's text is not the one the search found, the chapter
	    cannot be opened, or the row would not be replaced (locked since, missing, an endnote's end). */
	bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** Can the row be replaced from its menu: a Find/Change match not replaced, not locked, with no
	    outcome, and no replace running. */
	bool CanReplaceHit(int32 chapterIdx, int32 hitIdx);

	/** ***** A STORY ROW'S MENU (2026-09-27, the story level). ***** Replace = the story's TICKED rows (the
	    user's call), no prompt, one undo step, the list stays a work list (as ReplaceHit). Reject Change =
	    every replaced row of the story whose tracked change is still there, one undo step. Redo = the
	    rows taken back, below (RedoStory - a ROW's own Redo went on 2026-09-27: a row taken back is
	    replaced again with Replace). Each says what it did - or why nothing - in outStatus. */
	bool ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	/** Replace on a DOCUMENT row (2026-09-27): that document's ticked rows, as ReplaceStory. */
	bool ReplaceChapter(int32 chapterIdx, PMString& outStatus);
	bool CanReplaceChapter(int32 chapterIdx);
	/** Reject Change on a DOCUMENT row (2026-09-27): every replaced row of the document outside a footnote
	    whose tracked change is still there, one undo step (as RejectStory, over the whole document). */
	bool RejectChapter(int32 chapterIdx, PMString& outStatus);
	bool CanRejectChapter(int32 chapterIdx);
	/** Redo on a DOCUMENT row (2026-09-27): as RedoStory, over the whole document. */
	bool RedoChapter(int32 chapterIdx, PMString& outStatus);
	bool CanRedoChapter(int32 chapterIdx);
	bool RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	/** Redo on a story row (2026-09-27, the user's call C): every row of the story taken back with Reject
	    Change and still holding its original text, replaced again with what Find/Change holds now, ticked
	    or not; the rest are skipped and counted. One undo step, the list stays as it is. */
	bool RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	bool CanRedoStory(int32 chapterIdx, int32 groupIdx);
	/** Is there anything for the story row's Replace / Reject Change to do (the menu's greying). */
	bool CanReplaceStory(int32 chapterIdx, int32 groupIdx);
	bool CanRejectStory(int32 chapterIdx, int32 groupIdx);

	// (RedoHit - Redo on a row taken back - went on 2026-09-27, the user's call: a row taken back carries a
	//  box again and is replaced again with Replace or Change Checked.)

	/** Accept All Changes by KohakuFindChange in This Document on a document row (2026-09-27, the user's
	    call): the tracked changes signed "KohakuFindChange" in that chapter's document are accepted and
	    everybody else's are left (2026-09-29, the user's call - until then every change, as InDesign's
	    own Accept All), in ONE undo step (KBSTrackChange::AcceptSignedInDocument). All or nothing - a
	    story that will not go rolls the step back. Ours in hidden conditional text are left, and the
	    status says how many. Rows keep what they show; a replaced row's Reject Change then greys out
	    (its records are gone), as after an accept in the Track Changes panel. False = nothing changed;
	    outStatus says why either way. */
	bool AcceptAllInChapter(int32 chapterIdx, PMString& outStatus);

	/** Is there anything for Accept All Changes by KohakuFindChange to do: the chapter's document open
	    and holding at least one record signed "KohakuFindChange". */
	bool CanAcceptAllInChapter(int32 chapterIdx);

	/** Is a replace running right now? Its progress bar is modal but PUMPS EVENTS, so a menu
	    command can be dispatched while the run is standing in ReplaceChecked - the same hazard the
	    search guards against with KBSSearchEngine::IsSearching, and a worse one here: the run holds
	    an open command sequence, and a second walk started underneath it would Halt() the first
	    one's walker out from under it. The panel greys every action out while this is true. */
	bool IsReplacing();

}

#endif // __KBSReplaceEngine_h__
