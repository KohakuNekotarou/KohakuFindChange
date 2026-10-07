//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QUERY RUN (2026-10-04 - docs/superpowers/specs/2026-10-04-kfc-query-sequence-design.md; since 2026-10-06
//  docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F7 and section 5). The saved Find/Change
//  queries a user lined up, run in that order: each one loaded into Edit > Find/Change (kFCQueryXMLReaderCmdBoss) and
//  written with InDesign's own Change All, a document at a time (KFCChangeAll::WriteDocument) - no rows collected, no
//  list, no limit - ALL OF IT ONE UNDO STEP ("Run Queries"): one abortable command sequence around every query, the
//  run's documents opened and held from before it opens to after it ends (an open inside it throws the undo history of
//  what was written away - the resolve pass in KFCReplaceEngine::ReplaceChecked). The panel's list is cleared at the
//  start (a Ctrl+Z of the run puts it back - KFCUndoFollow::RunRecorder), and Edit > Find/Change is emptied at the end
//  (the spec's D7). A query whose file is gone, or that has nothing to find, is skipped and named; a failed write or
//  Cancel takes the whole run back.
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

	/** Run these queries, in this order - each one with InDesign's own Change All, a document at a time. outSummary =
	    the panel's message line. Returns how many were replaced (0 for a refusal, a cancel or a failure). */
	int32 Run(const std::vector<QueryItem>& queries, PMString& outSummary);

	/** THE QUERY DIALOG'S RUN (2026-10-07 - docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md):
	    these query files, in this order, each named as the dialog names it (KFCSavedQueries::Describe - the file's
	    name without .xml), then Run. A file that is not there is Run's to skip and name. */
	int32 RunFiles(const std::vector<IDFile>& files, PMString& outSummary);

	/** Is a run going on (KFCRunGuard counts it)? */
	bool IsRunning();

#ifdef KFC_DIAG
	/** THE TEST BUILD'S WAY IN until the panel exists (plan A, Task 8): the fault switch queries-run's file holds one
	    query path per line (UTF-8) - a line starting "#" is skipped; SearchBook hands its run here while the switch is
	    on. */
	int32 RunFromDiagSwitch(PMString& outSummary);
#endif
}

#endif // __KFCQuerySequence_h__

// End, KFCQuerySequence.h.
