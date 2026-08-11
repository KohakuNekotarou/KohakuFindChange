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
//  How the checked hits are found again: a hit's TextIndex shifts the moment an earlier hit in
//  the same story is replaced, so stored positions cannot be the key. Instead each chapter is
//  RE-WALKED with exactly the search's scope and options, and the Nth match of that walk is
//  lined up with the hit whose walkOrder is N (KBSResultModel::Hit::walkOrder).
//
//  The command behaviour below was measured on the real application (2026-07-25 probe, recorded
//  in docs/superpowers/specs/_done/2026-07-25-kbs-replace-checked-design.md section 10.1):
//    * kTWReplaceTextCmdBoss does NOT search on its own. Fired without a preceding
//      kFindTextCmdBoss it returns kFailure with an invalid range. So every step here is
//      find-then-maybe-replace, exactly like the application's own Find / Change button pair.
//    * After a replace, IFindChangeCmdData::GetRange describes the REPLACED text and follows a
//      change of length (1 char -> 3 chars came back as a 3-char range), so a replaced row's new
//      text is read straight out of it.
//    * A replace does not advance the walker: the following find still moves on by exactly one
//      match, absorbing the shift the replacement caused.
//    * GetReplacementCount is NOT updated. Success is GetFindChangeResult() == kSuccess.
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
	    The chapter is walked again with the same query and the Nth match is replaced for the Nth
	    checked row - so if the document has moved since the search in a way that adds, removes or
	    shifts a match, a replacement would land somewhere the user never ticked. The count alone
	    cannot see that: it still comes out right, and every checked row still finds a match.

	    So each chapter is walked TWICE. The first walk writes nothing: at every ticked walk order it
	    asks whether the match still BEGINS in the same story at the same index, which is what the
	    row recorded when the search found it. One mismatch - or one ticked row the walk never
	    reaches - and the whole run stops, with an alert saying so and the results cleared
	    (TellResultsWentStale, and the verify pass in the resolve loop). Only if every chapter passes
	    does the second walk open a command sequence and write.

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
	    walk order no longer means what the rows say it means.

	    The same test used to stand INSIDE the replacing walk, per hit, until 2026-08-05. See the
	    note above the walk in KBSReplaceEngine.cpp for why it could not work there and what remains
	    of it (the JUMP still asks it, so a click on a row can still answer "the replacement is no
	    longer here").

	    A checked hit that does not get replaced is ALWAYS counted and named in the summary, never
	    allowed to make the total quietly come up short. Three ways that happens:
	      - locked: on a locked layer or in a locked story. The Find/Change dialog can be told to
	        search those, but InDesign offers no way to change them ("Search Only"), so KBS follows.
	      - missing: the re-walk ran to the end of the chapter without that hit's turn coming up.
	      - refused: the replace command was asked and would not run. The only one of the three that
	        is a failure rather than a decision.

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
	        and they all go (ReleaseHeldDocs). The search and the two scans have always done this on
	        their own cancel; the replace did not, between 2026-08-02 and 2026-08-05, because the only
	        path that closed anything was the one that SAVED and it went with "save after replace";
	      - a run that goes THROUGH hands back the chapters no replacement landed in - every checked
	        hit there came back locked, missing or refused, or the walk never ran. Added 2026-08-05:
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

	    Three questions, most specific first:
	      1. the TAB - clicking another one returns another set of matches;
	      2. whether that tab is one this panel walks at all (Object and Colour are not);
	      3. the QUERY and every switch that decides which occurrences come back - the find string,
	         case / whole word / kana / width, the five scope switches, and FIND FORMAT (a paragraph
	         style, a font, a colour). See KBSSearchEngine::BuildWalkSignature.

	    Why it has to be asked at all: Change Checked RE-WALKS each chapter and lines the Nth match of
	    that walk up with the hit whose walkOrder is N, and the walker is handed the LIVE
	    IFindChangeOptions - so a query edited between the search and the replace makes the Nth match a
	    different occurrence entirely.

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
	    taken on the same side of that command as the search took it), and on answer 3 it CLEARS THE
	    RESULTS: they describe a query the dialog no longer holds, so keeping them up would only
	    invite another attempt. Answers 1 and 2 leave them alone - a tab is one click to put back.
	    A caller that gets true back should redraw the tree.

	    Call it OUTSIDE any command sequence: it processes a command of its own.

	    @param outSummary OUT the reason, ready for the status line. Cleared first.
	    @return true when the run must NOT go ahead. */
	bool RefuseChangedQuery(PMString& outSummary);

	/** Is a replace running right now? Its progress bar is modal but PUMPS EVENTS, so a menu
	    command can be dispatched while the run is standing in ReplaceChecked - the same hazard the
	    search guards against with KBSSearchEngine::IsSearching, and a worse one here: the run holds
	    an open command sequence, and a second walk started underneath it would Halt() the first
	    one's walker out from under it. The panel greys every action out while this is true. */
	bool IsReplacing();
}

#endif // __KBSReplaceEngine_h__
