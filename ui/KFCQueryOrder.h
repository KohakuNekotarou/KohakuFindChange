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
//  Main thread only, one dialog at a time (a modal one) - the state is this file's.
//
//========================================================================================

#ifndef __KFCQueryOrder_h__
#define __KFCQueryOrder_h__

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

	/** A left row's text: "<kind>  <name>". Empty for an index the list does not hold. */
	PMString SavedRowText(int32 index);

	/** A right row's text: "<n>  <kind>  <name>", and " (not found)" after a query whose file is not there. Empty for an
	    index the list does not hold. */
	PMString OrderRowText(int32 index);
}

#endif // __KFCQueryOrder_h__

// End, KFCQueryOrder.h.
