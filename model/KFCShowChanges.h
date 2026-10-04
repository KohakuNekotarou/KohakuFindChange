//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "Show Changes by KohakuFindChange" (the author's design; spec
//  docs/superpowers/specs/2026-09-29-kbs-show-changes-design.md): the panel's list rebuilt from the
//  Track Changes records KFC signed, after the results that listed them are gone - a new search, a
//  document closed and opened again, InDesign started again.
//
//  THE RECORDS ARE THE WHOLE OF IT. Every replace KFC makes is signed "KohakuFindChange" at a time KFC
//  hands out - the run's start plus the row's number (KFCTrackChange.h, the head) - so a record says
//  which run wrote it and which row it was. Nothing of the search is kept anywhere (the author's call:
//  no script label, no settings file, no saved query): the rows come back under their runs -
//  document -> run -> story -> row - and can be taken back (Reject Change) or accepted (Accept Change),
//  but not replaced again. To replace again, search again.
//
//  The scope is the Book Scope toggle's: ON = the book the book panel shows, chapter by chapter, each
//  opened and handed straight back, as the search does; OFF = the active document (Edit > Find/Change's
//  Search:, which the search follows, is not read here). Read-only, like the search: a
//  chapter never comes out modified.
//
//========================================================================================

#ifndef __KFCShowChanges_h__
#define __KFCShowChanges_h__

#include "PMString.h"

namespace KFCShowChanges
{
	/** Replace the panel's list with the rows the scope's signed records make, and say what was found in
	    outSummary. Every refusal (another run up, nothing to read) leaves the list as it was. Returns the
	    number of rows listed. */
	int32 Run(PMString& outSummary);

	/** Is Show Changes reading right now? Its progress bar pumps events, so a menu command can arrive
	    in the middle of it - KFCRunGuard asks this beside the search and the replace. */
	bool IsShowing();
}

#endif // __KFCShowChanges_h__
