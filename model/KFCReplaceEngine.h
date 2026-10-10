//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Replace engine: replaces the row the user asks for in the result panel (its right-click menu, or Return),
//  using the CHANGE string of the official Find/Change dialog. KFC never writes to IFindChangeOptions - it only
//  reads - so GREP back-references and escapes are interpreted by InDesign's own engine and are
//  never parsed here.
//
//  HOW IT WRITES: ONE MATCH AT A TIME, A STORY AT A TIME (the author's call). ReplaceInChapterOneByOne in
//  the .cpp walks each story that holds a row asked for (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on
//  the match it made current) and writes only those rows, each recognised by its thread, its offset into
//  it and its length - so a match a replacement made that the search never listed is stepped over, and
//  its row is reported missing rather than written. A GREP query holding ^ is walked backward (the
//  direction is set before anything is written, outside the sequence). Track Changes is left as each
//  story has it: KFC records nothing of its own
//  (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F1). (Not InDesign's Change All
//  over each story with the rows NOT asked for taken back afterwards: that was tried and dropped - git
//  history, c876bc7 and before. Change All itself is KFCChangeAll's.)
//
//  THE CHECK BEFORE THE WRITE. What is left of the old chapter-by-chapter re-walk (measured,
//  docs/superpowers/specs/_done/2026-07-25-kbs-replace-checked-design.md section 10.1) is the check
//  it ran before anything was written (ChapterMovedUnderRows in the .cpp): the row's story is walked as
//  the search walked it, and the row asked for must still begin where the search found it.
//
//========================================================================================

#ifndef __KFCReplaceEngine_h__
#define __KFCReplaceEngine_h__

#include "PMString.h"
#include "UIDRef.h"
#include "KFCModelTypes.h"		// KFCRowsByChapter

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

	    ONE OF TWO DOORS, and they divide the ways a write can be wrong between them.

	      - the QUERY changing between the search and the replace is caught HERE, before a chapter
	        is even opened, and the write is refused;
	      - the DOCUMENT moving between the two is caught by the verify walk (ChapterMovedUnderRows in
	        the .cpp), which is the only place the document itself can be asked.

	    Both refuse before a character is written. This one is first because it is far cheaper - the
	    dialog's own settings are readable and BuildWalkSignature turns them into something
	    comparable, where the other has to walk the row's story.

	    TWO SIDE EFFECTS, both deliberate. Once the tab is the searched one it STATES it
	    (KFCSearchEngine::CommitSearchMode - every walk after this needs that, and the query has to
	    be compared on the same side of that command as the search took it), and on answer 2 it
	    CLEARS THE RESULTS: they describe a query the dialog no longer holds, so keeping them up
	    would only invite another attempt. Answer 1 leaves them alone - a tab is one click to put
	    back - and so does a tab that could not be stated. A caller that gets true back should
	    redraw the tree.

	    Call it OUTSIDE any command sequence: it processes a command of its own.

	    @param outSummary OUT the reason, ready for the status line. Cleared first.
	    @return true when the write must NOT go ahead. */
	bool RefuseChangedQuery(PMString& outSummary);

	/** RefuseChangedQuery's question WITHOUT ITS CONSEQUENCES: true when the Find/Change settings
	    still describe the search the results came from. The tab is stated as RefuseChangedQuery
	    states it - a walk after this runs in it - and NOTHING IS CLEARED whatever the answer. For a
	    caller that only has to know: the jump's look for a row Undo moved
	    (KFCSearchEngine::RelocateStaleRow), where RefuseChangedQuery would clear the whole result set
	    in the middle of a jump on a changed query. Same rule as RefuseChangedQuery: outside any
	    command sequence. */
	bool QueryUnchangedSinceSearch();

	/** Replace on a hit row's right-click menu, or Return (the author's call): that one row, with no prompt, in ONE
	    undo step ("Replace"). The list stays a work list: the row shows its new text, every other row is
	    moved to where its text now stands. Refused - nothing changed, outStatus says why - when the query
	    changed since the search, the row's text is not the one the search found, the chapter cannot be
	    opened, or the row would not be replaced (locked since, missing, refused). */
	bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** THE ROWS SELECTED TOGETHER (O18 - the author's calls of 2026-10-10), all of one kind, of one document or several
	    (as Change Checked's ticked rows were), in ONE undo step: object rows by KFCObjectReplace::ReplaceRows (row by row -
	    a row that cannot be written is left and counted); text rows whose Replace is greyed are left and counted, and the
	    others are written all or nothing, as ReplaceHit writes one ("Replaced 3 rows: ID:262 #1, ID:262 #3, ID:270 #1." /
	    "Replaced 2 of 3 rows: ... Left as they were: 1 locked." / "Replaced 3 rows in 2 documents: a.indd ID:262 #1;
	    b.indd ID:270 #1."; a refusal names "a selected row"). Several documents are one abortable step around every
	    document's write (ReplaceRowsNow's note). One row = ReplaceHit. */
	bool ReplaceHits(const KFCRowsByChapter& rows, PMString& outStatus);

	/** Can the row be replaced from its menu: a Find/Change match not replaced, not locked, with no
	    outcome - WhyGreyed answering kGreyedNot. */
	bool CanReplaceHit(int32 chapterIdx, int32 hitIdx);

	/** WHY A ROW'S REPLACE IS GREYED - the one place the reasons are asked (CanReplaceHit is this answering kGreyedNot) -
	    as its row shows it - Changed, Missing, locked, refused -
	    for the rows left by a Replace of rows selected together, counted by reason (O18 - the author's call of
	    2026-10-10: "1 already replaced, 1 locked" rather than one "already replaced or locked" for all of them, which
	    named a row reading Missing as one of those). One reason a row: replaced, then missing, then locked, then refused
	    (a locked row a jump found changed reads "Missing ... locked" - missing is what keeps it from being written).
	    kGreyedNot when the row can be replaced. Text and object rows alike (KFCObjectReplace counts its rows by it). */
	enum GreyedReason { kGreyedNot = 0, kGreyedReplaced, kGreyedMissing, kGreyedLocked, kGreyedRefused, kGreyedReasonCount };
	GreyedReason WhyGreyed(int32 chapterIdx, int32 hitIdx);

	/** A GREP row's AFTER-TEXT: what the row's Replace would write at its place, read from a write that is
	    then thrown away (an aborted sequence - no undo step, the text, the story's version, the rows and the modified
	    flag as they were). false (outAfter empty) wherever Return would not write it now - not a GREP search, the query
	    changed since it, the row replaced / locked / not a work row, its document closed, its story changed, a run
	    going - or the write did not land. The UI shows it on the message area when the row is selected. */
	bool PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter);
}

#endif // __KFCReplaceEngine_h__
