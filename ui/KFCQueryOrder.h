//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QUERY DIALOG'S TWO LISTS (2026-10-07 - docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
//  sections 2-2 and 3): the saved queries the dialog offers (the left list, asked of the model at every open -
//  KFCSavedQueries) and the run order (the right list). What the dialog draws comes from here, and only from here: the
//  lists' row maker (KFCQueryList.cpp) reads these vectors by index.
//  THE RUN ORDER'S ONE SOURCE OF TRUTH IS ITS FILE, KFCQueryOrder.txt in InDesign's roaming folder: read at every open
//  (LoadOrder) and written at every change (SaveOrder), so however the dialog is closed - and across a restart - it opens
//  as it was left (G2, G3). One absolute path a line, UTF-8 without a BOM, CR LF; read with a BOM or LF too, a blank line
//  or a "#" line skipped.
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

	/** Read the run order from its file (every open). A file that is not there is an empty order. false = it is there
	    and could not be read (outWhy); the order is then empty. Each line is described by the model
	    (DescribeQueryFile): a line that is no query's file stays, "(not found)", and Run skips it. */
	bool LoadOrder(PMString& outWhy);

	/** Write the run order to its file (every change), through a side file (KFCWriteOwnFile). false = outWhy says which
	    step failed - "folder", "open", "write", "replace". */
	bool SaveOrder(PMString& outWhy);

	/** The four buttons' and Clear's changes. Indices outside the lists change nothing. */
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
