//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  CHANGE ALL IN BOOK (NO LIST) AND CLEAR RESULTS (2026-10-06 - docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md
//  F6, F18 and section 4). InDesign's own Change All (kReplaceAllTextCmdBoss) over the book's chapters - Book Scope on
//  only (F18: a document's Change All is InDesign's own dialog's) - once a chapter, in ONE undo step, with no list: the
//  panel says how many were replaced. Track Changes is each story's own setting, left as it is (F1). Runs only while
//  the panel holds no list (the old design's C4); Clear Results empties it (C8). The query run writes through
//  WriteDocument too, in whatever scope Search: names.
//
//========================================================================================

#ifndef __KFCChangeAll_h__
#define __KFCChangeAll_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WalkerScopeOptions.h"

namespace KFCChangeAll
{
	/** Change All in Book (No List). outSummary = the panel's message line. Returns how many were replaced (0 for a
	    refusal, a cancel or a failure - outSummary says which). Refused with Book Scope off (the menu greys it then;
	    a caller that never went through the menu is told why). */
	int32 Run(PMString& outSummary);

	/** The command's name - "Change All in Book (No List)", or "Change All in Selected Documents (No List)" while
	    Find/Change Selected Documents (Book) narrows it (2026-10-07): the menu's (IKFCRuns::ChangeAllCommandName) and
	    the one the search's 300 limit names. */
	const char* CommandName();

	/** Is a Change All running (KFCRunGuard counts it)? */
	bool IsRunning();

	/** ONE DOCUMENT'S CHANGE ALL, inside the caller's command sequence - kReplaceAllTextCmdBoss run the way
	    SnpFindAndReplace runs it: the session's shared walker initialised on a scope, the find/change client and the
	    session's options, inside the selections' critical section. selectionScope = kDocumentScope: the whole document
	    (QueryDocumentWalkerScope); Story / To End of Story / Selection: that scope of the front document's selection
	    (QueryWalkerScope_UsingSelections - the query run's, which follows Search:). outCount = the command's own count
	    (IFindChangeCmdData::GetReplacementCount; 0 when it says nothing). False = the walk could not start or the
	    command failed; the error state is left clear. */
	bool WriteDocument(const UIDRef& docRef, int32 selectionScope, const WalkerScopeOptions& scopeOptions, int32& outCount);

	/** Clear Results: the panel's list emptied (KFCSearchEngine::DropResults - the rows, the searched book's held
	    chapters and the find format), so Change All can run. The documents are not touched; a Ctrl+Z afterwards does
	    not bring the list back (a new result set). False = refused (a run is up, or there is no list) - outStatus says why. */
	bool ClearResults(PMString& outStatus);
}

#endif // __KFCChangeAll_h__

// End, KFCChangeAll.h.
