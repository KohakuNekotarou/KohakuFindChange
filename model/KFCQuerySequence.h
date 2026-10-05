//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QUERY RUN (2026-10-04 - docs/superpowers/specs/2026-10-04-kfc-query-sequence-design.md). The saved
//  Find/Change queries a user lined up, run in that order: each one loaded into Edit > Find/Change
//  (kFCQueryXMLReaderCmdBoss), searched with KFC's own search and every match written through Change Checked's
//  writing loop, one match at a time (the spec's D13 - InDesign's Change All - was taken back on 2026-10-05), and
//  nothing signed until the run's end (D14 - KFCTrackChange::SignRunPlaces: one pair of records per place) -
//  ALL OF IT ONE UNDO STEP ("Run Queries"): one abortable command sequence around every
//  query, the run's documents opened and held from before it opens to after it ends (an open inside it
//  throws the undo history of what was written away - the resolve pass in KFCReplaceEngine::ReplaceChecked).
//  Then Edit > Find/Change is emptied (the spec's D7) and - when the panel's toggle asks for it (D12) - the
//  list is rebuilt from the run's own signed records: one row per place, the text before the run and after
//  it (KFCShowChanges::ListOwnRun). A query whose file is gone, or that has nothing to find, is skipped and
//  named; one that finds more than kKFCCollectHitLimit in ONE chapter (document), a failed write or Cancel
//  takes the whole run back (the spec's D8 as changed on 2026-10-05: each query is searched and written a
//  chapter at a time).
//
//========================================================================================

#ifndef __KFCQuerySequence_h__
#define __KFCQuerySequence_h__

#include "IDFile.h"
#include "PMString.h"

#include <vector>

namespace KFCQuerySequence
{
	/** One query of a run: its name as the list shows it, and its .xml. */
	struct QueryItem
	{
		PMString	name;
		IDFile		file;
	};

	/** Run these queries, in this order - each one chapter (document) at a time: searched and written before the next,
	    so the collect limit counts a chapter, not the book (the spec's D8, 2026-10-05). listResults = rebuild the list
	    from the run's records afterwards (the panel's toggle, on by default - the spec's D12); off = the list stays empty
	    and the message gives the counts (Show Changes by KohakuFindChange lists them later). outSummary = the panel's
	    message line. Returns how many rows were replaced (0 for a refusal, a cancel or a failure). */
	int32 Run(const std::vector<QueryItem>& queries, bool listResults, PMString& outSummary);

	/** Is a run going on (KFCRunGuard counts it)? */
	bool IsRunning();

#ifdef KFC_DIAG
	/** THE TEST BUILD'S WAY IN until the panel exists (plan A, Task 8): the fault switch queries-run's file holds one
	    query path per line (UTF-8) - a line "#nolist" runs it with the list off; SearchBook hands its run here while the
	    switch is on. */
	int32 RunFromDiagSwitch(PMString& outSummary);
#endif
}

#endif // __KFCQuerySequence_h__

// End, KFCQuerySequence.h.
