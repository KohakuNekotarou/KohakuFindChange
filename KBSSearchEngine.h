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
