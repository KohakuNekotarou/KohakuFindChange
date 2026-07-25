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

	/** The walker scope options EVERY KBS walk uses (whole document, hidden layers excluded, all
	    else default). The replace pass must re-walk a chapter with exactly the options the search
	    that produced the hits used, or the walk order those hits were numbered by no longer lines
	    up - hence one definition, shared by both. */
	void GetKBSWalkerScopeOptions(WalkerScopeOptions& outOptions);

	/** The paragraph holding [start, end), split at those exact UTF-16 offsets into the three
	    segments a hit row paints: the text before the match, the matched text, and the text after.
	    Any of the three may come back empty; all three are empty when the position cannot be read.

	    Used by the search when a hit is collected, and again by the replace pass to rebuild a
	    row's text from the range the replace command reports back. */
	void SplitLineAroundMatch(const UIDRef& storyRef, TextIndex start, TextIndex end,
		PMString& outPre, PMString& outMatch, PMString& outPost);
}

#endif // __KBSSearchEngine_h__
