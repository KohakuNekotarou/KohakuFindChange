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
//  in docs/superpowers/specs/2026-07-25-kbs-replace-checked-design.md section 10.1):
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

	    A chapter whose re-walk does not reach every checked hit is reported rather than replaced
	    at guessed positions - that means the document was edited, or the Find/Change query
	    changed, since the search ran.

	    A checked hit that does not get replaced is ALWAYS counted and named in the summary, never
	    allowed to make the total quietly come up short. Four ways that happens:
	      - locked: on a locked layer or in a locked story. The Find/Change dialog can be told to
	        search those, but InDesign offers no way to change them ("Search Only"), so KBS follows.
	      - missing: the text could not be found where the row said it was.
	      - refused: the replace command was asked and would not run. The only one of the four that
	        is a failure rather than a decision.
	      - not reached: the safety ceiling cut the re-walk short before the hit came up. Those rows
	        are the only ones carrying NO word on their locator, because nothing was found out about
	        them; the summary names the chapter instead. A walk that simply ran to the end of the
	        chapter without the hit coming up is a different thing - those rows say missing.

	    ***** NOTHING IS EVER SAVED, AND NOTHING IS EVER CLOSED. ***** Every chapter a replacement
	    lands in is left MODIFIED AND UNSAVED, with a window open on it, and the summary says so:
	    overwriting the user's files is the user's own step to take. (The confirmation carried a
	    "save after replace" box from 2026-08-02 to 2026-08-05, which is where the second shape
	    above came from.)

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

	    ***** IT IS NOT WHAT KEEPS THE REPLACE CORRECT. ***** That is the per-hit same-occurrence test,
	    which runs for every row with nothing allowed past it. This door exists so the user is told
	    WHY, instead of watching a whole run come back "not found".

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
