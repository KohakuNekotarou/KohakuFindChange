//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  AN OBJECT ROW'S OTHER SPECIAL OPERATIONS (docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md -
//  the author, 2026-10-17: Fitting, Arrange, Transform, Lock / Unlock, Hide / Show, Move to Active Layer, Clear Object
//  Style Overrides, Delete, Run Script over the selected rows' objects). InDesign's own Object menu commands work on a
//  selection, one spread; the object list spans spreads and documents, so the rows selected together are written
//  wherever they stand - by InDesign's own Facades, each asked first whether it can (its Can... - E4), in ONE undo step
//  the panel follows. Create Outlines' shape (KFCObjectOutline.h): the doors row by row, one write, the rows marked, the
//  documents left as a write leaves them - for every operation of one table (KFCSpecialOp, KFCModelTypes.h).
//
//========================================================================================

#ifndef __KFCObjectSpecial_h__
#define __KFCObjectSpecial_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCRowsByChapter, KFCSpecialOp, KFCSpecialParams

namespace KFCObjectSpecial
{
	/** A SPECIAL OPERATION (op, a KFCSpecialOp) over object rows - the right-clicked row, or the rows selected together
	    when it is one of them (the UI half's RowsOfRightClick). The doors, row by row (the spec's E4): an object row; its
	    document reached (a closed chapter reopened windowless, held - as a Replace reaches it); the item still there (else
	    the row reads Missing), on a spread; a locked layer refuses, and Object > Lock refuses all but Unlock (which asks
	    for it); then InDesign's own question for the operation (its Can...). Not the row's fingerprint: a special operation
	    goes by the item the row names, whatever query found it. The rows that pass are written in ONE step (the operation's
	    name in the Edit menu, KFC's undo mark in it - one document's a plain sequence, several documents' one abortable
	    sequence, KFCReplaceEngine::WriteStep): one Facade call over each document's items, a transform one call per item
	    about its own centre (E5); an error standing takes the whole step back. A row whose item the write CHANGED - its
	    fingerprint, or its place in its stack for Arrange - is marked (KFCResultModel::MarkItemSpecial: its word, its eye,
	    its place read again, its fingerprint; Delete: gone, its Replace greyed); one it left as it was is counted
	    "unchanged" (E7); the other rows the write moved take the prints it left (KFCObjectReplace::FollowRowsTheWriteMoved);
	    a chapter of ours written is given a window, one not written handed back (KFCReplaceEngine::DocsAfterWrite).
	    params: Move to Active Layer's layer name (text - found by its name in each document); Move, Scale, Rotate,
	    Shear's values (KFCSpecialParams).
	    outStatus: "<operation>: 3 objects - ID:259, ID:260, ID:261." - "...: 2 of 3 objects - ... Left as they were: 1
	    locked." - "...: 3 objects in 2 documents - a.indd ID:259, ID:260; b.indd ID:12."; a refusal ("<operation>: ...")
	    when none could be written. True when something was written. Refused while a run of ours is up. */
	bool SpecialRows(int32 op, const KFCRowsByChapter& rows, const KFCSpecialParams& params, PMString& outStatus);

	/** WHETHER A SPECIAL OPERATION CAN BE OFFERED FOR A ROW (the menu's grey - the spec's E10): SpecialRows's doors, READ
	    ONLY - no document is reached or opened and no row is touched. A row of a document not open now answers true: only
	    reaching it can tell, and pressing does that (its doors say why when it cannot). Move to Active Layer's layer is
	    not asked here (it is read when the item is pressed). */
	bool CanSpecialHit(int32 op, int32 chapterIdx, int32 hitIdx);
}

#endif // __KFCObjectSpecial_h__
