//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  RUN SAVED QUERIES... - THE QUERY DIALOG (docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
//  section 2). Modeless, with a minimize box (G4 as the author changed it): the saved queries on the left,
//  the run order on the right, the buttons between them, a line saying what Run would run on, and Run / Close. The
//  order is the session's (KFCQueryOrder - kept while InDesign runs; a file of its own only through Save Order... /
//  Load Order...), so however the dialog is closed it opens again as it was left.
//
//========================================================================================

#ifndef __KFCQueryDialog_h__
#define __KFCQueryDialog_h__

/** The flyout's Run Saved Queries...: the query dialog, modeless - this returns at once. Asked while it is open, it
    brings that one forward (back from the taskbar if minimized) and fills it again. */
void KFCQueryDialogOpen();

/** The open dialog's Runs on: line asked again (nothing when it is not open) - KFCPanelTitle::Update calls it, so the line
    follows Book Scope, Search: and the selection as the panel's tab does (the spec's section 8, item 8). */
void KFCQueryDialogRefreshScope();

/** A saved query double-clicked in the left list (KFCQueryRowEH): put into Edit > Find/Change, whole, and that dialog
    brought up - opened when closed, restored when minimised, left as it is when open (KFCShowFindChangeDialog) - so what
    the query holds can be read at once (the author's call). The dialog's message line says what was loaded, or why
    nothing was: a run going on, a query file gone since the list was read, the command failing. */
void KFCQueryDialogShowInFindChange(int32 savedIndex);

#endif // __KFCQueryDialog_h__

// End, KFCQueryDialog.h.
