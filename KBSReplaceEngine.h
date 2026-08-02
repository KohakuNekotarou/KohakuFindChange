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

	    The WHOLE run is wrapped in ONE command sequence, across every chapter, so a book-wide
	    replace undoes with a single Ctrl+Z whichever chapter the user has in front. (Corrected
	    2026-07-28. It used to be one sequence per chapter, on the belief that "undo is per
	    document" made the chapter the largest grain available. Measured on the running
	    application, that arrangement was actively harmful: undoing in one document removed the
	    step from the other chapters' histories as well WITHOUT reverting their text, leaving them
	    replaced with no way back.) The price is that the run is all-or-nothing - an error left
	    standing when the sequence ends rolls back every chapter.

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

	    Chapters are left MODIFIED AND UNSAVED unless saveAfterReplace says otherwise: overwriting
	    the user's files is their decision, which is why it is asked for on the confirmation and
	    never remembered between runs.

	    Refuses to run at all while another replace is up (see IsReplacing), and while the panel is
	    showing a replace's report rather than a work list.

	    @param outSummary OUT a ready-to-show status line (counts, chapters that did not line up).
	    @param saveAfterReplace save every document a replacement actually landed in, once the whole
	           run is committed - whoever opened it. Documents that took no replacement are left
	           alone: nothing of ours is in them, so a dirty flag there is somebody else's edit.
	           With "Hide Previous Chapter" on, the chapters KBS opened windowless are then closed
	           again, but only if every save succeeded (see the release call).
	    @return the number of hits actually replaced (0 on any early exit). */
	int32 ReplaceChecked(PMString& outSummary, bool saveAfterReplace);

	/** Is a replace running right now? Its progress bar is modal but PUMPS EVENTS, so a menu
	    command can be dispatched while the run is standing in ReplaceChecked - the same hazard the
	    search guards against with KBSSearchEngine::IsSearching, and a worse one here: the run holds
	    an open command sequence, and a second walk started underneath it would Halt() the first
	    one's walker out from under it. The panel greys every action out while this is true. */
	bool IsReplacing();
}

#endif // __KBSReplaceEngine_h__
