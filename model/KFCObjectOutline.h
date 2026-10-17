//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  AN OBJECT ROW'S SPECIAL OPERATIONS (docs/superpowers/specs/2026-10-17-kfc-object-outlines-design.md - the author,
//  2026-10-17: "do it on the selected rows' objects, so that objects across spreads can be operated on"): Create Outlines
//  over the rows selected together - their text frames outlined by InDesign's own command (Type > Create Outlines,
//  ICreateOutlinesUtils::CreateCOFrameCmd) wherever they stand, on every spread and in every document of the list, in ONE
//  undo step the panel follows; each row then reads Outlined, names its outlines, and keeps its eye. The object Replace's
//  shape (KFCObjectReplace.h): the doors row by row, one write, the rows marked, the documents left as a write leaves them.
//
//========================================================================================

#ifndef __KFCObjectOutline_h__
#define __KFCObjectOutline_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCRowsByChapter

namespace KFCObjectOutline
{
	/** CREATE OUTLINES over object rows - the right-clicked row, or the rows selected together when it is one of them (the
	    UI half's RowsOfRightClick). The doors, row by row (the spec's D6): an object row not outlined yet; its document
	    reached (a closed chapter reopened windowless, held - as a Replace reaches it); the item still there (else the row
	    reads Missing), on a spread, not locked; a text frame (InDesign's own filter, FilterUIDList). Not its fingerprint:
	    outlines go by the item the row names, whatever query found it. The rows that pass are written in ONE step
	    ("Create Outlines" in the Edit menu, KFC's undo mark in it - one document's a plain sequence, several documents'
	    one abortable sequence, KFCReplaceEngine::WriteStep), one command per frame, the frame replaced by its outlines (D4);
	    an error standing takes the whole step back. Each written row: Outlined, its item the outlines InDesign made, their
	    fingerprint, its eye on (KFCResultModel::MarkItemOutlined); a chapter of ours written given a window, one not
	    written handed back (KFCReplaceEngine::DocsAfterWrite).
	    outStatus: "Outlined 3 objects: ID:259, ID:260, ID:261." - "Outlined 2 of 3 objects: ... Left as they were: 1 not a
	    text frame." - "Outlined 3 objects in 2 documents: a.indd ID:259, ID:260; b.indd ID:12."; a refusal ("Create
	    Outlines: ...") when none could be written. True when something was written. Refused while a run of ours is up. */
	bool OutlineRows(const KFCRowsByChapter& rows, PMString& outStatus);

	/** WHETHER CREATE OUTLINES CAN BE OFFERED FOR A ROW (the menu's grey - the author, 2026-10-17: a menu item that cannot
	    run is greyed). OutlineRows's doors, READ ONLY: no document is reached or opened and no row is touched. An object
	    row not outlined yet; when its document is open now, its item there, on a spread, not locked, a text frame
	    (InDesign's own filter). A row of a document not open now answers true - only reaching it can tell, and pressing
	    does that (its doors say why when it cannot). */
	bool CanOutlineHit(int32 chapterIdx, int32 hitIdx);
}

#endif // __KFCObjectOutline_h__
