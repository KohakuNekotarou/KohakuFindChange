//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The search engine: walks the user's CURRENT Find/Change query across the scope the Book Scope
//  toggle selects - every chapter of the active book when it is ON, just the front document when it
//  is OFF, never a silent fallback between them - and collects the matches into
//  KBSResultModel, grouped by chapter. Unlike KESCL - which supplied its own literal text and
//  pinned the mode to plain text - KBS touches nothing on the Find/Change panel: it walks with
//  whatever the user set there, MODE INCLUDED (Text or GREP). The walk is read-only (a
//  SaveRestoreModifiedState dirty guard per document).
//
//  Each collected hit carries the line's text pre-split into the three segments the colour cell
//  paints (before / matched / after) and the jump anchors (story UID + text range) for Task 3.
//
//========================================================================================

#ifndef __KBSSearchEngine_h__
#define __KBSSearchEngine_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WalkerScopeOptions.h"

class RangeProgressBar;

/** Move the run's progress bar to an absolute position. ioReported is the position already sent, so
    that advances too small to be worth a repaint can be swallowed (see the .cpp); pass force = true
    where the bar must land exactly, such as a chapter boundary.

    NOTE: this does NOT make the run cancellable, and neither does any other way of moving the bar - that
    was measured both ways on 2026-07-31. WasCancelled has to be ASKED, and asking it only inside the
    chapter loop misses a cancel pressed during the last chapter. See the ask-once-more test that
    follows the loop in SearchBook and ReplaceChecked. */
void KBSAdvanceProgress(RangeProgressBar* bar, int32& ioReported, int32 target, bool force = false);

namespace KBSSearchEngine
{
	/** Resolve the scope from the Book Scope toggle (the active book's chapters when it is ON, the
	    front document when it is OFF - never a silent fallback between them), walk the user's
	    current Find/Change query across it, fill KBSResultModel with the hits (grouped by
	    chapter, only chapters with >=1 hit), and build a one-line status summary. Releases any
	    windowless chapters opened for the search (Task 2: the hits' display text is already
	    extracted, so nothing needs them held; Task 3 will hold them for the row jumps instead).

	    @param outSummary  a ready-to-show status line for the panel.
	    @return the total number of matches across the scope. */
	int32 SearchBook(PMString& outSummary);

	/** Is a search running right now? The progress bar pumps events while it is up, so a menu
	    command could otherwise be dispatched INTO a running search. The panel's actions ask this
	    and grey themselves out; SearchBook itself turns a re-entrant call away as a last resort. */
	bool IsSearching();

	/** State which Find/Change TAB to work in, by re-committing the mode the user already has
	    selected. Call this immediately before a find or a replace walk.

	    This is the ONE thing KBS sets on the Find/Change side, and it sets it to the value that is
	    already there. It is needed because the engine does not pick the mode up from
	    IFindChangeOptions when a command runs - the mode has to have been COMMITTED through
	    kFindSearchModeCmdBoss, which is what the dialog itself does when a tab is clicked. Without
	    it, a walk driven from outside the dialog runs as plain TEXT whatever tab is on screen:
	      * a Glyph-tab search was matching its find string as literal text (it looked like it worked,
	        because the glyph's character was in that string), and
	      * a replace then wrote the TEXT tab's change string over what the Glyph tab had found.
	    Both reported by the user on 2026-07-30. Every one of the four entry points in the SDK's own
	    SnpFindAndReplace (find/replace text, find/replace glyph) does this right before it runs -
	    including the glyph pair, which use the same kTWReplaceTextCmdBoss this does.

	    Writes the value it just read, so the user's own settings never change: this STATES the mode,
	    it does not choose one.

	    On the Glyph tab it states the FIND GLYPH too, for the same reason and by the same rule: the
	    dialog commits the glyph through kFindChangeGlyphIDCmdBoss when one is picked, so a walk driven
	    from outside the dialog has to commit it again. Stating the mode alone left the engine in glyph
	    mode with no glyph and the panel found nothing at all (user, 2026-07-30). The CHANGE glyph is
	    deliberately NOT stated here - see CommitReplaceGlyph.

	    @note Call it OUTSIDE any command sequence. It processes a command, and a session-setting
	          command inside the replace sequence would become part of that undo step. */
	void CommitSearchMode();

	/** State the glyph a Glyph-tab replace will WRITE, by re-committing the one the user already has
	    in the dialog's Change To box. Call it immediately after CommitSearchMode on the replace path
	    only - a search must never leave a change glyph standing, since nothing on screen would say it
	    had been set.

	    Does nothing unless the Glyph tab is the mode in force. Returns false when that tab has no
	    change glyph chosen: the caller must then refuse the replace rather than walk, because the
	    command would otherwise write whatever glyph was committed last - a glyph the user never chose
	    on this run, and the exact failure this whole mechanism exists to prevent.

	    @return true when it is safe to replace: either not a glyph replace at all, or a change glyph
	            is set and has been stated.
	    @note Same as CommitSearchMode - call it OUTSIDE any command sequence. */
	bool CommitReplaceGlyph();

	/** The walker scope options EVERY KBS walk uses: the five switches read straight off the
	    Find/Change dialog, exactly as the query itself is. The replace pass must re-walk a chapter
	    with exactly the options the search that produced the hits used, or the walk order those
	    hits were numbered by no longer lines up - hence one definition, shared by both.

	    @note Two of the five ("include locked layers" / "include locked stories") are FIND-only in
	          InDesign - the header states there is no option to change in locked content. They stay
	          in the shared scope so both walks visit the same matches in the same order, and the
	          replace refuses the locked ones one at a time instead (see IsMatchEditable). */
	void GetKBSWalkerScopeOptions(WalkerScopeOptions& outOptions);

	/** May the text at this position be REWRITTEN? The Find/Change dialog can be told to search
	    locked layers and locked stories, but InDesign offers no way to change what it finds there
	    ("Search Only"), so the replace has to make the same distinction itself: those matches are
	    listed and can be jumped to, and are then left untouched.

	    Two locks are asked about, which is the pair the dialog names:
	      - the STORY's insert lock (IItemLockData on the text story, which also answers for an
	        inline by way of its parent), and
	      - the LAYER the match's frame sits on.

	    @return false ONLY when one of those two locks is positively found. Anything that cannot be
	            resolved - a story without the lock interface, an overset match placed in no frame,
	            an item on no layer - reads as editable, because that is what it was before this
	            test existed and a "cannot tell" must not start refusing ordinary replacements. */
	bool IsMatchEditable(const UIDRef& storyRef, TextIndex pos);

	/** The frame that decides whether the match at 'pos' may be edited: the frame it composes into,
	    or - for an overset match, composed but placed nowhere - the frame carrying the "+"
	    indicator. kInvalidUID when neither can be resolved, which IsFrameEditable then reads as
	    editable (see IsMatchEditable's @return).

	    This and IsFrameEditable are IsMatchEditable taken apart, so a pass asking about many hits
	    can ask the expensive half ONCE PER FRAME instead of once per hit: a chapter's hits usually
	    share a handful of frames, and no lock can change while a replace pass is running. Callers
	    with a single hit to ask about should keep using IsMatchEditable. */
	UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos);

	/** Are the story and that frame both unlocked? The expensive half: it climbs the page-item
	    hierarchy and asks four separate locks, which is why it is worth remembering per frame.
	    @see IsMatchEditable for what the answer means, and what "cannot tell" resolves to. */
	bool IsFrameEditable(const UIDRef& storyRef, UID frameUID);

	/** Just the matched text at [start, end), cut where SplitLineAroundMatch would cut its match
	    segment - clipped to the end of the paragraph holding 'start' - but WITHOUT building the
	    line around it.

	    SplitLineAroundMatch copies the whole paragraph two or three times over to produce three
	    strings; the same-occurrence test throws two of them away. This reads the matched characters
	    and nothing else, which is what makes it affordable once per checked hit. */
	void CopyMatchText(const UIDRef& storyRef, TextIndex start, TextIndex end, PMString& outMatch);

	/** The paragraph holding [start, end), split at those exact UTF-16 offsets into the three
	    segments a hit row paints: the text before the match, the matched text, and the text after.
	    Any of the three may come back empty; all three are empty when the position cannot be read.

	    Used by the search when a hit is collected, and again by the replace pass to rebuild a
	    row's text from the range the replace command reports back. */
	void SplitLineAroundMatch(const UIDRef& storyRef, TextIndex start, TextIndex end,
		PMString& outPre, PMString& outMatch, PMString& outPost);

	/** Is the match at [start, end) the SAME occurrence a stored hit describes? Three questions,
	    all of which must answer yes:

	      - same story          (a match in another story is never the one the row means)
	      - same position       (start == expectStart + posDelta)
	      - same matched text   (what is there now reads the way the row says it did)

	    posDelta is how far THIS pass has already moved the text ahead of this point in this story -
	    our own replacements, cancelled out - so whatever difference is left is the USER's editing,
	    which is exactly the case that must not be written over. A caller that has changed nothing
	    (the jump) passes 0.

	    An unreadable or out-of-range position comes back with empty text and therefore answers
	    false, which is the safe answer: when in doubt, do not write. */
	bool MatchIsSameOccurrence(const UIDRef& storyRef, TextIndex start, TextIndex end,
		UID expectStoryUID, TextIndex expectStart, const PMString& expectMatch, int32 posDelta);
}

#endif // __KBSSearchEngine_h__
