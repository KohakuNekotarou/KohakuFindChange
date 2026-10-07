//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE SAVED FIND/CHANGE QUERIES (2026-10-07 - docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
//  section 2-2). What the query dialog's left list offers, and what a path in its run order is called: the user's
//  queries (FileUtils::GetAppRoamingDataFolder(.., "Find-Change Queries")\<kind>\*.xml) and InDesign's own
//  (FileUtils::GetPresetsFolder(.., "Find-Change Queries\<kind>", the UI language)\*.xml - InDesign's put the language's
//  folder UNDER the kind's). Four kinds only - Text, GREP, Glyph, Transliterate: the query run skips Object and Color
//  (KFCQuerySequence), so the dialog does not offer them. The one place that knows how those folders are laid out.
//
//========================================================================================

#ifndef __KFCSavedQueries_h__
#define __KFCSavedQueries_h__

#include "IDFile.h"

#include <vector>

#include "KFCModelTypes.h"	// KFCSavedQuery

namespace KFCSavedQueries
{
	/** Every saved query of the four kinds, the user's and InDesign's: kind order (Text, GREP, Glyph, Transliterate),
	    then name (case-insensitive), then the user's before InDesign's. */
	void List(std::vector<KFCSavedQuery>& out);

	/** One query file as the dialog names it: its name (the file name without .xml), its kind (the folder it is in -
	    the kind's folder is the parent of the user's and the grandparent of InDesign's), whether it is InDesign's own,
	    and whether it is there now. A path in no kind's folder answers mode -1. */
	void Describe(const IDFile& file, KFCSavedQuery& out);

	/** "Text", "GREP", "Glyph", "Transliterate" - the kinds' folder names - or "?" for any other mode. */
	const char* KindName(int32 mode);
}

#endif // __KFCSavedQueries_h__

// End, KFCSavedQueries.h.
