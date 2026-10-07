//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  RUN SAVED QUERIES... - THE QUERY DIALOG (2026-10-07 - docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
//  section 2). Modeless, with a minimize box (G4 as changed by the author the same day): the saved queries on the left,
//  the run order on the right, the buttons between them, a line saying what Run would run on, and Run / Close. The
//  order is the run order file's (KFCQueryOrder), read at every open and written at every change, so however the dialog
//  is closed it opens again as it was left (G2, G3).
//
//========================================================================================

#ifndef __KFCQueryDialog_h__
#define __KFCQueryDialog_h__

/** The flyout's Run Saved Queries...: the query dialog, modeless - this returns at once. Asked while it is open, it
    brings that one forward (back from the taskbar if minimized) and fills it again. */
void KFCQueryDialogOpen();

#endif // __KFCQueryDialog_h__

// End, KFCQueryDialog.h.
