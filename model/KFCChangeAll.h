//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  CHANGE ALL IN BOOK (NO LIST) AND CLEAR RESULTS (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md
//  F6, F18 and section 4). InDesign's own Change All (kReplaceAllTextCmdBoss) over the book's chapters - Book Scope on
//  only (F18: a document's Change All is InDesign's own dialog's) - once a chapter, in ONE undo step, with no list: the
//  panel says how many were replaced, and how many in each chapter. Track Changes is each story's own
//  setting, left as it is (F1). Runs only while the panel holds no list; Clear Results empties it
//  (docs/superpowers/specs/_done/2026-10-05-kfc-change-all-no-list-design.md C4, C8). What may stand then - a search that found
//  nothing, a book's "(0/0)" row - is thrown away at the run's commit point, as the query run throws its list away
//  (Run says why). The query run writes through WriteDocument too, in whatever scope Search: names, and leaves its
//  documents by the same rule (LeaveRunDocuments, PutRunDocumentsBack).
//
//========================================================================================

#ifndef __KFCChangeAll_h__
#define __KFCChangeAll_h__

#include "InterfacePtr.h"
#include "PMString.h"
#include "UIDRef.h"
#include "WalkerScopeOptions.h"
#include "KFCBookScope.h"		// ChapterDoc - a run's documents (LeaveRunDocuments, PutRunDocumentsBack)

#include <vector>

class ITextWalker;			// InitWalk
class ITextWalkerScope;

namespace KFCChangeAll
{
	/** Change All in Book (No List). outSummary = the panel's message line - "N replaced in M chapter(s)." (or the
	    selected documents' form), the notes, and last "  By chapter: <name> (<n>), ..." for the chapters written, in
	    the book's order, ten at most (the author's addition). Returns how many were replaced (0 for a
	    refusal, a cancel or a failure - outSummary says which). Refused with Book Scope off (the menu greys it then;
	    a caller that never went through the menu is told why). */
	int32 Run(PMString& outSummary);

	/** The command's name - "Change All in Book (No List)", or "Change All in Selected Documents (No List)" while
	    Find/Change Selected Documents (Book) narrows it: the menu's (IKFCRuns::ChangeAllCommandName) and
	    the one the search's row limit (KFCResultModel::ListLimit) names. */
	const char* CommandName();

	/** Is a Change All running (KFCRunGuard counts it)? */
	bool IsRunning();

	/** ONE DOCUMENT'S CHANGE ALL, inside the caller's command sequence - kReplaceAllTextCmdBoss run the way
	    SnpFindAndReplace runs it: the session's shared walker initialised on a scope, the find/change client and the
	    session's options, inside the selections' critical section. selectionScope = kDocumentScope: the whole document
	    (QueryDocumentWalkerScope); Story / To End of Story / Selection: that scope of the front document's selection
	    (QueryWalkerScope_UsingSelections - the query run's, which follows Search:). outCount = the command's own count
	    (IFindChangeCmdData::GetReplacementCount; 0 when it says nothing). False = the walk could not start or the
	    command failed - answered kFailure, or left the error state raised; the error state is left clear. */
	bool WriteDocument(const UIDRef& docRef, int32 selectionScope, const WalkerScopeOptions& scopeOptions, int32& outCount);

	/** THE WALK WRITEDOCUMENT TAKES, READY TO GO - the session's shared walker initialised on that scope, with the find/change
	    client and the session's options. Asked by WriteDocument and by what has to walk the same matches before it writes them
	    (KFCRunList - the query run's list): one place answers what a document's write covers. outNothingToWalk: the scope
	    holds nothing to walk (KFCSearchEngine::ScopeHasNothingToWalk). False = the walk could not be set up; nothing was
	    started. The caller stops the walker (KFCSearchEngine::WalkerStops). */
	bool InitWalk(const UIDRef& docRef, int32 selectionScope, const WalkerScopeOptions& scopeOptions,
		InterfacePtr<ITextWalker>& outWalker, InterfacePtr<ITextWalkerScope>& outScope, bool& outNothingToWalk);

	/** WHAT EACH DOCUMENT OF A RUN IS LEFT AS once the run's step is kept - the one rule for Change All in Book and the query
	    run (KFCQuerySequence). targets = the run's documents, opened (a closed chapter windowless and held) before its step;
	    wasModified = each one's "unsaved" flag then; wroteTo = whether the run wrote to it.
	      - WRITTEN: a held chapter gets its window, so the write can be seen and saved; one whose window could not be
	        opened stays held and windowless with the write in it, and is named in outUnclosed ("left open with no window").
	        A document another target wrote is left as that one leaves it (a book listing one file twice).
	      - NOT WRITTEN: it comes out as it went in - one that was clean is made clean again, since a walk can mark a
	        database modified without changing a character (handed back as the walk left it, such a chapter stays open
	        with no window and is named, and a document of the user's says "unsaved" with nothing in it changed - cases
	        xq-book-dirty-unwritten, xq-doc-dirty-unwritten, ca-book-dirty-unwritten) - and is handed back; one that could
	        not be is named.
	    (Test builds: each written document still open is noted first - KFCDiagWrittenDocs.h; one gone since is not read
	    through its address.) */
	void LeaveRunDocuments(const std::vector<KFCBookScope::ChapterDoc>& targets, const std::vector<bool>& wasModified,
		const std::vector<bool>& wroteTo, std::vector<PMString>& outUnclosed);

	/** A RUN TAKEN BACK (cancelled, or failed): the abort puts the text back and leaves the "unsaved" flag up, so every
	    document of the run that was clean is made clean again, and the held chapters are released
	    (KFCBookScope::ReleaseHeldDocs). */
	void PutRunDocumentsBack(const std::vector<KFCBookScope::ChapterDoc>& targets, const std::vector<bool>& wasModified);

	/** Clear Results: the panel's list emptied (KFCSearchEngine::DropResults - the rows, the searched book's held
	    chapters and the find format), so Change All can run. The documents are not touched; a Ctrl+Z afterwards does
	    not bring the list back (a new result set). False = refused (a run is up, or there is no list) - outStatus says why. */
	bool ClearResults(PMString& outStatus);
}

#endif // __KFCChangeAll_h__

// End, KFCChangeAll.h.
