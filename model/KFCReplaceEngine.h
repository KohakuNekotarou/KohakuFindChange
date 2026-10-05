//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Replace engine: replaces the hits the user checked in the result panel, using the CHANGE
//  string of the official Find/Change dialog. KFC never writes to IFindChangeOptions - it only
//  reads - so GREP back-references and escapes are interpreted by InDesign's own engine and are
//  never parsed here.
//
//  HOW IT WRITES: ONE TICKED MATCH AT A TIME, A STORY AT A TIME, UNDER TRACK CHANGES (the user's
//  call). ReplaceInChapterOneByOne in the .cpp walks each story that holds a ticked row
//  (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on the match it made current) and writes only the
//  ticked rows, each recognised by its thread, its offset into it and its length - so a match a
//  replacement made that the search never listed is stepped over, and its row is reported missing
//  rather than written. A GREP query holding ^ is walked backward (the direction is set before
//  anything is written, outside the sequence). The pending tracked changes a ticked match sits in or
//  next to are accepted first - not around a match the walk leaves, one at an endnote's end; the
//  replaces' own records are LEFT in the document, told apart by their time stamp: they are what
//  Reject Change, Replace Again (Redo in the code) and the jump find a row by.
//  (Not InDesign's Change All over each story with the rows NOT ticked taken back afterwards: that
//  was tried and dropped - git history, c876bc7 and before.)
//
//  THE CHECK BEFORE THE RUN. What is left of the old chapter-by-chapter re-walk (measured,
//  docs/superpowers/specs/_done/2026-07-25-kbs-replace-checked-design.md section 10.1) is the check
//  it ran before anything was written (ChapterMovedUnderRows in the .cpp): each chapter is walked as
//  the search walked it, and every ticked row must still begin where the search found it.
//
//========================================================================================

#ifndef __KFCReplaceEngine_h__
#define __KFCReplaceEngine_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

namespace KFCReplaceEngine
{
	/** Replace every checked hit in the current result set.

	    The WHOLE run is ONE command sequence, across every chapter, so a book-wide replace undoes
	    with a single Ctrl+Z whichever chapter the user has in front, and cancelling puts the whole
	    book back. Every chapter that has work is opened before the first character is written, and
	    every one a replacement lands in stays open and unsaved afterwards (below). NOT one sequence
	    per chapter, though "undo is per document" makes the chapter look like the largest grain
	    available: measured on the running application, that arrangement is actively harmful -
	    undoing in one document removes the step from the other chapters' histories as well WITHOUT
	    reverting their text, leaving them replaced with no way back. The price is that the run is
	    all-or-nothing - an error left standing when the sequence ends rolls back every chapter.

	    NO CHAPTER-AT-A-TIME SHAPE, so a book of twenty chapters is all open at once. Such a shape can
	    only serve runs that SAVE: a chapter that has not been written to disk cannot be closed,
	    because closing it would throw its replacements away - and nothing is ever saved (below). A
	    machine that cannot hold a whole book open is served by ticking fewer rows instead (the user's
	    decision).

	    THE RUN CHECKS THAT THE MATCHES ARE STILL WHERE THE SEARCH FOUND THEM, AND REFUSES TO START
	    IF THEY ARE NOT (the author's design).
	    The rows were found at positions the search recorded, and the run lines what it writes up with
	    them - so if the document has moved since the search in a way that adds, removes or shifts a
	    match, the rows no longer describe it.

	    So each chapter is walked before it is written. That walk writes nothing: for every ticked row
	    it asks whether the row's story is at the version KFC left it at, whether the row still reads
	    as it was found, and whether a match of the walk still stands at the row's start and length
	    (the three questions at ChapterMovedUnderRows in the .cpp). One that does not - or one ticked
	    row the walk never reaches - and the whole run stops, with an alert saying so and the results cleared
	    (TellResultsWentStale, and the verify pass in the resolve loop). Only if every chapter passes
	    does the second walk open a command sequence and write. (The walk does not line the Nth match
	    up with the row numbered N: it finds each row by its place and reads its text - see
	    ChapterMovedUnderRows in the .cpp.)

	    IN THE RESOLVE PASS, AND NOWHERE ELSE. That is the one moment where the question is both
	    answerable and free: the chapter has just been reopened, and NOT ONE CHARACTER has been
	    written yet - so the positions are still the ones the search recorded (no replacements of
	    this run's own to cancel out) and a refusal has nothing to roll back.

	    WHY POSITIONS RATHER THAN A FINGERPRINT OF THE CHAPTER. A record of every story's change
	    counter, warning rather than refusing, answers "does this chapter look untouched?", which
	    means enumerating every way a document can move - text, stories added or deleted, layers
	    hidden or locked, conditions, master pages - and that list is never finished. Comparing the
	    positions asks about the thing itself: whatever the cause, if a ticked match is not where it
	    was, the row no longer describes it. (The story version asked first is not that fingerprint:
	    it covers only the stories holding a ticked row, and it refuses rather than warns - A STORY'S
	    VERSION in the .cpp.)

	    The same test does not stand INSIDE the replacing walk, per hit. See the note on the
	    SAME-OCCURRENCE TEST in KFCReplaceEngine.cpp for why it cannot work there and what remains of
	    it (KFCSearchEngine::RowReadsAsFound, which the jump and the row menus' doors ask).

	    A checked hit that does not get replaced is ALWAYS counted and named in the summary, never
	    allowed to make the total quietly come up short. The ways that happens:
	      - locked: on a locked layer or in a locked story. The Find/Change dialog can be told to
	        search those, but InDesign offers no way to change them ("Search Only"), so KFC follows.
	      - missing: the walk never met the row's match as the search listed it - this run's own
	        replaces moved the text around it (GREP's ^, $ or a lookahead now reads something else).
	      - endnote left: the match ends an endnote, where InDesign's replace breaks the endnote (see
	        MatchEndsAnEndnote in the .cpp); that row alone is left.
	      - refused: the replace command itself declined.
	      - deleted: the row went with a footnote, table or object another ticked row deleted.

	    NOTHING IS EVER SAVED. Every chapter a replacement lands in is left MODIFIED AND UNSAVED, with
	    a window open on it, and the summary says so: overwriting the user's files is the user's own
	    step to take.

	    WHAT IS LEFT OPEN IS EXACTLY WHAT HAS SOMETHING IN IT. A chapter a replacement
	    landed in stays open, gets a window, and is left unsaved - the replacements are in it and only
	    the user can decide about them. Every OTHER chapter this run opened is handed back
	    (KFCBookScope::ReleaseHeldDoc), because each one locks its .indd while it stands and a
	    windowless document cannot even be closed by hand - it is in no menu. Three cases:

	      - a run that is CANCELLED has put every character back, so no chapter holds anything of it
	        and they all go (ReleaseHeldDocs), as the search's do on its own cancel;
	      - a run that goes THROUGH hands back the chapters no replacement landed in - every checked
	        hit there came back locked, missing, refused or left at an endnote's end. Otherwise such a
	        chapter stays open, windowless and locked for the rest of the session, and WITH ITS
	        MODIFIED FLAG SET, because a walk can mark a database changed without changing a
	        character (which is why the SEARCH guards its own walk with SaveRestoreModifiedState and
	        this one deliberately does not). That flag then stops anything from ever closing it;
	      - a run that cannot start its command sequence at all writes nothing and hands back
	        everything, the same way.

	    A chapter the USER had open, or had already edited, is on none of these lists: it was never
	    held, so it is not this run's to close and stays exactly as it was.

	    Refuses to run at all while another replace is up (see IsReplacing), and while no row of the
	    list carries a box (KFCResultModel::NoRowHasCheckBox - a replace's report with no row taken
	    back in it, or a list rebuilt from the records).

	    @param outSummary OUT a ready-to-show status line (counts, chapters that did not line up).
	    @return the number of hits actually replaced (0 on any early exit). */
	int32 ReplaceChecked(PMString& outSummary);

	/** What a query run's write did (WriteCheckedInHeldSequence). */
	struct WriteOutcome
	{
		int32		replaced;		// rows rewritten
		int32		missing;		// checked rows whose text was not where the row says
		int32		locked;			// checked rows on a locked layer or in a locked story
		int32		refused;		// rows InDesign's replace command said no to
		int32		endnoteLeft;	// rows at an endnote's end, left alone
		int32		unrecorded;		// rows written that left no tracked change (a footnote's)
		int32		acceptedFirst;	// pending changes of somebody else's accepted before a write
		bool		cancelled;		// Cancel pressed on the bar
		bool		failed;			// the write could not go on - `why` says so
		PMString	why;
		std::vector<UIDRef>	touchedDocs;	// documents a replacement (or an accept) landed in
		WriteOutcome() : replaced(0), missing(0), locked(0), refused(0), endnoteLeft(0), unrecorded(0),
			acceptedFirst(0), cancelled(false), failed(false) { why.SetTranslatable(kFalse); }
	};

	/** A QUERY RUN'S WRITE (KFCQuerySequence, 2026-10-04): every checked row of the results, inside the command
	    sequence the CALLER holds open, through Change Checked's own writing loop (WriteCheckedChapters). Every
	    chapter with a checked row must be OPEN - the caller opened and holds them; nothing is opened or closed
	    here, and a chapter that is not open fails the write before a character is written. States the change side
	    (KFCSearchEngine::CommitReplaceSide), turns the session's direction for a GREP ^ query as Change Checked
	    does, and starts a signed run of its own (KFCTrackChange::BeginSignedRun). No verify pass: the rows come
	    from a search made a moment ago inside the same sequence.
	    `changeAllScope` (2026-10-05, the spec's D13): -1 = one match at a time; otherwise the run's Search: scope (an
	    IWalkerScopeFactoryUtils::WalkScopeType - kDocumentScope for a document, all documents or a book's chapter),
	    and each story with rows is written with InDesign's Change All in that scope (ChangeAllInStory) - a match at
	    an endnote's end too, as InDesign's own Change All writes it (the author's call: InDesign's fault, ignored).
	    The query run's message keeps the write's endnote-left count, which then comes only from a story written
	    one at a time (none, while every story goes through Change All). Nothing is signed while a query run writes
	    (KFCTrackChange::QueryRunWriting): the run signs at its end (KFCTrackChange::SignRunPlaces).
	    @return true when it went through (not cancelled, not failed). */
	bool WriteCheckedInHeldSequence(const PMString& barTitle, WriteOutcome& out, int32 changeAllScope = -1);

	/** Do the current Find/Change settings still describe the search the panel's results came from?

	    Two questions, most specific first:
	      1. the TAB - clicking another one returns another set of matches;
	      2. the QUERY and every switch that decides which occurrences come back - the find string,
	         case / whole word / kana / width, the five scope switches, and FIND FORMAT (a paragraph
	         style, a font, a colour). See KFCSearchEngine::BuildWalkSignature.
	    (Not "is that tab one this panel walks at all": the tab on the results is one the search could
	    state, so that would never answer no.)

	    Why it has to be asked at all: the replace walks every story that holds a ticked row with the
	    LIVE IFindChangeOptions and writes the matches it meets at the rows' places - so a query edited
	    between the search and the replace walks a different set of matches from the one the rows
	    list. The verify walk looks for the rows' matches the same way, and the reason is the same for
	    it.

	    ONE OF TWO DOORS, and they divide the ways a run can be wrong between them.

	      - the QUERY changing between the search and the replace is caught HERE, before a chapter
	        is even opened, and the run is refused;
	      - the DOCUMENT moving between the two is caught by the verify walk in the resolve pass
	        (see ReplaceChecked above), which is the only place the document itself can be asked.

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
	    command sequence. False on a list rebuilt from the records: nothing was searched, so there is
	    nothing it can be unchanged from. */
	bool QueryUnchangedSinceSearch();

	/** Reject Change on a replaced hit row: its tracked change - found by
	    KFCTrackChange::FindRowChangeForHit - is rejected, deletion and insertion, in ONE undo step, and
	    the row shows its original text again (the word "rejected" only on a list rebuilt from the
	    records, where no check box comes back to say it). All the way back or not at all: when the
	    original text does not stand where the change stood afterwards, the step is rolled back.
	    False = nothing changed; outStatus says why either way. */
	bool RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** Replace on a hit row's right-click menu (the author's call): that one row, ticked or
	    not, with no prompt, in ONE undo step ("Replace"); the Track Changes note goes in outStatus. The
	    list stays a work list: the row shows its new text and loses its box, every other row is moved to
	    where its text now stands. Refused - nothing changed, outStatus says why - when the query changed
	    since the search, the row's text is not the one the search found, the chapter cannot be opened,
	    or the row would not be replaced (locked since, missing, an endnote's end). */
	bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** Can the row be replaced from its menu: a Find/Change match not replaced, not locked, with no
	    outcome, and no replace running. */
	bool CanReplaceHit(int32 chapterIdx, int32 hitIdx);

	/** A STORY ROW'S MENU. Replace = the story's TICKED rows (the author's call), no prompt, one undo
	    step, the list stays a work list (as ReplaceHit) - and all or none: one row that cannot be
	    written (an endnote's end, locked, missing) leaves every row as it was, and outStatus says so and
	    how many (the author's call - Change Checked writes the rest). Reject Change = every replaced row of
	    the story whose tracked change is still there, one undo step. Redo = the rows taken back, below
	    (RedoStory - a hit row has no Redo of its own: a row taken back is replaced again with Replace).
	    Each says what it did - or why nothing - in outStatus. */
	bool ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	/** Replace on a DOCUMENT row: that document's ticked rows, as ReplaceStory. */
	bool ReplaceChapter(int32 chapterIdx, PMString& outStatus);
	bool CanReplaceChapter(int32 chapterIdx);
	/** Reject Change on a DOCUMENT row: every replaced row of the document outside a footnote
	    whose tracked change is still there, one undo step (as RejectStory, over the whole document). */
	bool RejectChapter(int32 chapterIdx, PMString& outStatus);
	bool CanRejectChapter(int32 chapterIdx);
	/** Redo on a DOCUMENT row: as RedoStory, over the whole document. */
	bool RedoChapter(int32 chapterIdx, PMString& outStatus);
	bool CanRedoChapter(int32 chapterIdx);
	bool RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	/** Redo on a story row (the author's call): every row of the story taken back with Reject Change and
	    still holding its original text, replaced again with what Find/Change holds now, ticked or not;
	    the rest are skipped and counted. One undo step, the list stays as it is. The menu item and
	    every status line it writes say "Replace Again (Current Find/Change Settings)" (the user's
	    call); the function names keep Redo. */
	bool RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	bool CanRedoStory(int32 chapterIdx, int32 groupIdx);
	/** Is there anything for the story row's Replace / Reject Change to do (the menu's greying). */
	bool CanReplaceStory(int32 chapterIdx, int32 groupIdx);
	bool CanRejectStory(int32 chapterIdx, int32 groupIdx);
	/** Is a replaced row of this story under a hidden condition right now (KFCTrackChange::RowChangeIsHidden) -
	    the reason its Reject / Accept Changes are grey when nothing else is left to take back (case
	    reject-hidden-condition-story). The story row's right-click says it: with every item grey the
	    popup does not open at all. */
	bool StoryChangesHidden(int32 chapterIdx, int32 groupIdx);

	/** Accept All Changes by KohakuFindChange in This Document on a document row (the author's call):
	    the tracked changes signed "KohakuFindChange" in that chapter's document are accepted and
	    everybody else's are left (the author's call - not every change, as InDesign's own Accept All
	    takes), in ONE undo step (KFCTrackChange::AcceptSignedInDocument). All or nothing - a
	    story that will not go rolls the step back. Ours in hidden conditional text are left, and the
	    status says how many. Rows keep what they show; a replaced row's Reject Change then greys out
	    (its records are gone), as after an accept in the Track Changes panel. False = nothing changed;
	    outStatus says why either way. */
	bool AcceptAllInChapter(int32 chapterIdx, PMString& outStatus);

	/** Is there anything for Accept All Changes by KohakuFindChange to do: the chapter's document open
	    and holding at least one record signed "KohakuFindChange". */
	bool CanAcceptAllInChapter(int32 chapterIdx);

	/** ACCEPT CHANGE BY KohakuFindChange (Show Changes) - Reject Change's twin. The
	    row's tracked change accepted - with every replaced row touching it whose change is still there, as a
	    reject takes them (touching replaces written front to back share ONE deletion: accepting part of it
	    would leave the rest unable to come back) - in ONE undo step ("Accept Change"). The row reads
	    "accepted" and offers neither Reject nor Accept again. On any row of any list whose change is still
	    there. All or nothing; false = nothing changed, outStatus says why either way. */
	bool AcceptHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);
	/** Is the row's tracked change still there (the Reject / Accept items' greying)? */
	bool CanAcceptOrRejectHit(int32 chapterIdx, int32 hitIdx);
	/** Accept Change on a story row: every replaced row of the story with a change left, one undo step - the
	    rows Reject Change takes there, so its greying is CanRejectStory's. */
	bool AcceptStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus);
	/** A RUN ROW'S MENU (a list rebuilt from the records only). The run's rows in
	    that document with a change left - taken back (RejectRun) or accepted (AcceptRun), one undo step. */
	bool RejectRun(int32 chapterIdx, int32 runIdx, PMString& outStatus);
	bool AcceptRun(int32 chapterIdx, int32 runIdx, PMString& outStatus);
	bool CanRejectOrAcceptRun(int32 chapterIdx, int32 runIdx);

	/** Is a replace running right now? Its progress bar is modal but PUMPS EVENTS, so a menu
	    command can be dispatched while the run is standing in ReplaceChecked - the same hazard the
	    search guards against with KFCSearchEngine::IsSearching, and a worse one here: the run holds
	    an open command sequence, and a second walk started underneath it would Halt() the first
	    one's walker out from under it. The panel greys every action out while this is true. */
	bool IsReplacing();

}

#endif // __KFCReplaceEngine_h__
