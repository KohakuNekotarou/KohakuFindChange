//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Replace engine: replaces the row the user asks for in the result panel (its right-click menu), using
//  the CHANGE string of the official Find/Change dialog. KFC never writes to IFindChangeOptions - it only
//  reads - so GREP back-references and escapes are interpreted by InDesign's own engine and are
//  never parsed here.
//
//  HOW IT WRITES: ONE MATCH AT A TIME, A STORY AT A TIME (the user's call). ReplaceInChapterOneByOne in
//  the .cpp walks each story that holds a row asked for (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on
//  the match it made current) and writes only those rows, each recognised by its thread, its offset into
//  it and its length - so a match a replacement made that the search never listed is stepped over, and
//  its row is reported missing rather than written. A GREP query holding ^ is walked backward (the
//  direction is set before anything is written, outside the sequence). Track Changes is left as each
//  story has it: KFC records nothing of its own (2026-10-06 -
//  docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F1). (Not InDesign's Change All
//  over each story with the rows NOT asked for taken back afterwards: that was tried and dropped - git
//  history, c876bc7 and before. Change All itself is KFCChangeAll's.)
//
//  THE CHECK BEFORE THE RUN. What is left of the old chapter-by-chapter re-walk (measured,
//  docs/superpowers/specs/_done/2026-07-25-kbs-replace-checked-design.md section 10.1) is the check
//  it ran before anything was written (ChapterMovedUnderRows in the .cpp): each chapter is walked as
//  the search walked it, and every row asked for must still begin where the search found it.
//
//========================================================================================

#ifndef __KFCReplaceEngine_h__
#define __KFCReplaceEngine_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

namespace KFCReplaceEngine
{

	/** Do the current Find/Change settings still describe the search the panel's results came from?

	    Two questions, most specific first:
	      1. the TAB - clicking another one returns another set of matches;
	      2. the QUERY and every switch that decides which occurrences come back - the find string,
	         case / whole word / kana / width, the five scope switches, and FIND FORMAT (a paragraph
	         style, a font, a colour). See KFCSearchEngine::BuildWalkSignature.
	    (Not "is that tab one this panel walks at all": the tab on the results is one the search could
	    state, so that would never answer no.)

	    Why it has to be asked at all: the replace walks every story that holds a row it writes with the
	    LIVE IFindChangeOptions and writes the matches it meets at the rows' places - so a query edited
	    between the search and the replace walks a different set of matches from the one the rows
	    list. The verify walk looks for the rows' matches the same way, and the reason is the same for
	    it.

	    ONE OF TWO DOORS, and they divide the ways a run can be wrong between them.

	      - the QUERY changing between the search and the replace is caught HERE, before a chapter
	        is even opened, and the run is refused;
	      - the DOCUMENT moving between the two is caught by the verify walk in the resolve pass
	        (ChapterMovedUnderRows in the .cpp), which is the only place the document itself can be asked.

	    Both refuse before a character is written. This one is first because it is far cheaper - the
	    dialog's own settings are readable and BuildWalkSignature turns them into something
	    comparable, where the other has to walk every chapter.

	    TWO SIDE EFFECTS, both deliberate. Once the tab is the searched one it STATES it
	    (KFCSearchEngine::CommitSearchMode - every walk after this needs that, and the query has to
	    be compared on the same side of that command as the search took it), and on answer 2 it
	    CLEARS THE RESULTS: they describe a query the dialog no longer holds, so keeping them up
	    would only invite another attempt. Answer 1 leaves them alone - a tab is one click to put
	    back - and so does a tab that could not be stated. A caller that gets true back should
	    redraw the tree.

	    Call it OUTSIDE any command sequence: it processes a command of its own.

	    @param outSummary OUT the reason, ready for the status line. Cleared first.
	    @return true when the run must NOT go ahead. */
	bool RefuseChangedQuery(PMString& outSummary);

	/** RefuseChangedQuery's question WITHOUT ITS CONSEQUENCES: true when the Find/Change settings
	    still describe the search the results came from. The tab is stated as RefuseChangedQuery
	    states it - a walk after this runs in it - and NOTHING IS CLEARED whatever the answer. For a
	    caller that only has to know: the jump's look for a row Undo moved
	    (KFCSearchEngine::RelocateStaleRow), where RefuseChangedQuery would clear the whole result set
	    in the middle of a jump on a changed query. Same rule as RefuseChangedQuery: outside any
	    command sequence. */
	bool QueryUnchangedSinceSearch();

	/** Replace on a hit row's right-click menu (the author's call): that one row, with no prompt, in ONE
	    undo step ("Replace"). The list stays a work list: the row shows its new text, every other row is
	    moved to where its text now stands. Refused - nothing changed, outStatus says why - when the query
	    changed since the search, the row's text is not the one the search found, the chapter cannot be
	    opened, or the row would not be replaced (locked since, missing, an endnote's end). */
	bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** Can the row be replaced from its menu: a Find/Change match not replaced, not locked, with no
	    outcome. */
	bool CanReplaceHit(int32 chapterIdx, int32 hitIdx);



}

#endif // __KFCReplaceEngine_h__
