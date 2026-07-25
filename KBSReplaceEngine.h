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

	    Chapters are processed one at a time and each chapter's replacements are wrapped in one
	    command sequence, so a chapter undoes with a single Ctrl+Z (undo is per document, which
	    makes the chapter the largest grain available). A chapter whose re-walk does not reach
	    every checked hit is reported rather than replaced at guessed positions - that means the
	    document was edited, or the Find/Change query changed, since the search ran.

	    Chapters are left MODIFIED AND UNSAVED: overwriting the user's files is their decision.

	    @param outSummary OUT a ready-to-show status line (counts, chapters that did not line up).
	    @return the number of hits actually replaced (0 on any early exit). */
	int32 ReplaceChecked(PMString& outSummary);
}

#endif // __KBSReplaceEngine_h__
