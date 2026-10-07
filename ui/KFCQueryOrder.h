//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QUERY DIALOG'S TWO LISTS (docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
//  sections 2-2 and 3): the saved queries the dialog offers (the left list, asked of the model at every open -
//  KFCSavedQueries) and the run order (the right list). What the dialog draws comes from here, and only from here: the
//  lists' row maker (KFCQueryList.cpp) reads these vectors by index.
//  THE RUN ORDER IS THE SESSION'S (the author's call): kept here while InDesign runs - the dialog closed and
//  opened again shows it as it was left - and empty after a restart. An order is kept beyond that only as a file the
//  person saves and loads (SaveOrderTo / LoadOrderFrom - KFCQueryOrderFile.h). (No build that shipped wrote a run order
//  of its own anywhere: KFCQueryOrder.txt, which test builds kept in InDesign's roaming folder, is neither read nor
//  written.)
//  Main thread only, one dialog at a time - the state is this file's.
//
//========================================================================================

#ifndef __KFCQueryOrder_h__
#define __KFCQueryOrder_h__

#include "IDFile.h"
#include "PMString.h"

#include <vector>

#include "KFCModelTypes.h"	// KFCSavedQuery

namespace KFCQueryOrder
{
	/** The left list: every saved query of the four kinds, as the model lists them (LoadSaved fills it). */
	const std::vector<KFCSavedQuery>& Saved();

	/** Fill the left list again - at every open, so a query saved while the dialog was closed is there. */
	void LoadSaved();

	/** The right list: the run order. */
	const std::vector<KFCSavedQuery>& Order();

	/** The run order's files, in order - what Run hands the model. */
	std::vector<IDFile> OrderFiles();

	/** Read each row's file again (every open): a query whose file has gone since reads "(not found)", one that is there
	    reads as it is now. */
	void RefreshOrder();

	/** Write the run order to `file` as a KFC query order (KFCQueryOrderFile.h), through a side file beside it
	    (KFCWriteFileSafely). false = outWhy says which step failed - "open", "write", "replace". */
	bool SaveOrderTo(const IDFile& file, PMString& outWhy);

	/** Make the run order the one `file` holds. Each entry is its file when that is there; otherwise the saved query of
	    the same kind and name (the user's own before InDesign's - a query's folder moves with InDesign's version);
	    otherwise it stays, "(not found)", and Run refuses while it does (KFCQuerySequence). false = outWhy "read" (not
	    there, or not read whole) or "format" (not a KFC query order); the run order is then left as it was. */
	bool LoadOrderFrom(const IDFile& file, PMString& outWhy);

	/** The four buttons' and Clear's changes - the session's order alone (nothing is written). Indices outside the lists
	    change nothing. */
	void Add(int32 savedIndex);			// Saved()[savedIndex] at the end of the order
	void Remove(int32 orderIndex);
	void MoveUp(int32 orderIndex);
	void MoveDown(int32 orderIndex);
	void Clear();

	/** A left row's text: "<kind>  <name>". Empty for an index the list does not hold. */
	PMString SavedRowText(int32 index);

	/** A right row's text: "<n>  <kind>  <name>", and " (not found)" after a query whose file is not there. Empty for an
	    index the list does not hold. */
	PMString OrderRowText(int32 index);

	/** Empty both lists during the controlled shutdown (KFCUIStartupShutdown): each holds a PMString and an IDFile per
	    row, the kind of static whose destructor must not be left for the DLL's unload (KFCResultTree::ShutdownCleanup's
	    rule - the KESCL one). Safe to call twice. */
	void ShutdownCleanup();
}

#endif // __KFCQueryOrder_h__

// End, KFCQueryOrder.h.
