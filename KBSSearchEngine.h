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
}

#endif // __KBSSearchEngine_h__
