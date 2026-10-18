//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A ROW OPERATION UNDER ITS OWN LIST'S QUERY (docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md section
//  3-2 - v2 R2, R3). The live list keeps a copy of the query it was searched with (ListHeader::query - KFCQueryCopy);
//  Find/Change can hold another one: a tier switched to (the switch leaves Find/Change alone - v2 R1), or the person's
//  edit since the search (REP-13 as changed by v2: such a Replace used to be refused and the list cleared). An operation
//  that reads the query - a row's Replace, the rows selected together, an object row's check and write, a GREP row's
//  after-text, Search This Story / Document / These Results Again, Narrow Down: the doors IKFCRuns.h says run under
//  the list's query - runs inside a scope:
//   - Find/Change's find side reads as the list's (KFCReplaceEngine::QueryUnchangedSinceSearch - the tab, the walk
//     signature, the Find Format): nothing is stated, and the operation runs as it always has - Change to as the dialog
//     holds it. A Replace that goes through so takes the list's copy again (R3 - the list remembers what it last
//     replaced with), and the UI half names the live list again (IKFCUIServices::LiveQueryRecopied).
//   - Otherwise the list's own query is stated (KFCQueryCopier::Apply) inside an abortable command sequence and checked
//     three times - field by field (Matches), by the walk signature the list was searched with, and in the words the
//     person reads (the list's name - IKFCUIServices::QueryReadsAsTier) - the operation runs, and Find/Change is put back
//     as it was: both tabs when the list's is not the one on screen (PutBack). A query that does not read back is refused
//     before the operation: the sequence aborted, Find/Change put back, nothing done.
//  Find/Change is not left on the list's query: what could not be put back is said - in the operation's words, or on the
//  panel's line where it has none (IKFCUIServices::ShowNotice). No file is written. The main thread only.
//
//========================================================================================

#ifndef __KFCQueryScope_h__
#define __KFCQueryScope_h__

#include "PMString.h"
#include "KFCQueryCopy.h"

class KFCQueryScope
{
public:
	/** Asks, and when Find/Change holds another query than the live list's, states the list's (above). Nothing while a
	    run of ours is under way - the operation's own door turns it away. */
	KFCQueryScope();
	/** A scope left without Finish (an operation that returned early, or one with no words to carry it - PreviewHit):
	    Find/Change put back, and said on the panel's line when it cannot be. */
	~KFCQueryScope();
	/** False = the list's query could not be stated: outWhy says why (Find/Change is already put back), and the operation
	    must not run. */
	bool Ready(PMString& outWhy) const;
	/** The operation is over. Stated: Find/Change put back, and ioStatus gains which query was used - or that Find/Change
	    could not be put back. Not stated, and `wrote` (a Replace that went through under Find/Change as it is): the list's
	    copy taken again when it reads otherwise now (R3). */
	void Finish(bool wrote, PMString& ioStatus);

	/** A GREP ROW'S AFTER-TEXT under its list's query (KFCReplaceEngine::PreviewHit - on every row landed on, the arrow
	    keys' walk included): asked first, with nothing stated, whether the row could have one at all - a list searched on
	    another tab, an object row, a row Return would not write, a run under way - so a walk over a Text tier's rows states
	    no query. */
	static bool PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter);

private:
	/** Find/Change as it was: the list's tab first when it is another (Apply turns to it), then the tab that was on screen
	    - each checked (Matches). False = outWhat names what did not read back. */
	bool PutBack(PMString& outWhat);

	bool			fStated;		// the list's query is in Find/Change now - put back by Finish or the destructor
	bool			fRefused;		// it could not be stated - Ready says why
	bool			fUnchanged;		// Find/Change read as the list's: nothing stated, and R3 may take the copy again
	bool			fFinished;
	KFCQueryCopy	fBeforeNow;		// the tab Find/Change was on, as it was
	KFCQueryCopy	fBeforeListTab;	// the list's tab as it was, when that is another tab (valid = it was read)
	PMString		fWhy;			// Ready's words
	PMString		fName;			// the list's name, as its row shows it

	KFCQueryScope(const KFCQueryScope&);
	KFCQueryScope& operator=(const KFCQueryScope&);
};

/** AN OPERATION RUN UNDER ITS LIST'S QUERY (the model's doors - KFCModelServices.cpp): the scope made, the operation
    refused when the list's query could not be stated, run otherwise, and Find/Change put back. `writes` = a Replace (R3
    takes the copy again after one alone). Returns the operation's answer; ioStatus carries its words and the scope's. */
template <typename Operation>
bool KFCUnderListQuery(PMString& ioStatus, bool writes, Operation operation)
{
	KFCQueryScope scope;
	if (!scope.Ready(ioStatus))
		return false;
	const bool done = operation();
	scope.Finish(writes && done, ioStatus);
	return done;
}

#endif // __KFCQueryScope_h__

// End, KFCQueryScope.h.
