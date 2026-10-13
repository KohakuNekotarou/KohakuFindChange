//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT A SAVED QUERY DOES, READ OFF ITS FILE (the author's call: a row picked in the query dialog says what its query
//  finds and changes to, and nothing of Find/Change changes - the double click is what loads one): the query's own
//  XML, as InDesign wrote it when the query was saved, read with InDesign's own parser (KFCQueryContents.cpp says
//  how). The dialog's message line takes the short form (KFCQueryContentsLine - the dialog's observer,
//  KFCQueryDialog.cpp), the row's tooltip the whole of it (KFCQueryContentsTip - KFCQueryRowTip.cpp).
//
//========================================================================================

#ifndef __KFCQueryContents_h__
#define __KFCQueryContents_h__

#include "PMString.h"

#include "KFCModelTypes.h"		// KFCSavedQuery

/** For the dialog's message line: `<kind>  <name>:  Find what: "<text>"  Change to: "<text>"  + Find Format
    + Change Format` - the parts the query has (an Object query has no text to find; a Glyph or a Transliterate query's
    own settings are not written out, only its formats). One line: the message line cuts what does not fit at its end.
    A query whose file is not there, or cannot be read, says that instead. */
PMString KFCQueryContentsLine(const KFCSavedQuery& query);

/** For the row's tooltip: the same, whole - one part to a line, a format said "set" or "none", and the file it was read
    from last. Each text is cut at a length a tooltip holds (the product's own tips cap theirs). */
PMString KFCQueryContentsTip(const KFCSavedQuery& query);

#endif // __KFCQueryContents_h__

// End, KFCQueryContents.h.
