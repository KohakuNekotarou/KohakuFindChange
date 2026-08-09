//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  Saves and restores the flyout's SETTINGS toggles as a small JSON file of our own, in the
//  user's roaming preferences folder. Ported from KESCM's KESCMPanelState (2026-08-04), which
//  has carried this since 2026-07-12.
//  *Nothing is written into InDesign's own data (workspace SavedData, documents).
//
//  Where: FileUtils::GetAppRoamingDataFolder(.., "KBSPanelState.json"), in that folder itself
//    (Windows) %APPDATA%\Adobe\InDesign\Version XX.0\<locale>\KBSPanelState.json
//  *No sub-folder is created (the user's rule, set for KESCM on 2026-07-12). The folder is one
//   InDesign already makes for its preferences, but the file is ours alone and unrelated to
//   anything InDesign keeps there.
//
//  What is saved (SETTINGS only, not work state):
//    - Translucent Panel (*Windows only. Only the FLAG is restored; putting the alpha on the
//      window is done by the panel's AutoAttach and the palette-visibility observer, because at
//      startup there is no panel yet.)
//    - Translucent Find/Change (*Windows only, and the same again: the flag alone. InDesign's own
//      Find/Change dialog is certainly not open at startup, and the window-list observer puts the
//      alpha on the moment it is. **This line was missing from the list until 2026-08-04, when it
//      was already being saved and restored - the code was right and the note was not.)
//    - Hide Previous Chapter (the user's call, 2026-08-04). It closes chapter windows as a jump
//      lands, which is why it was left out at first - but a restored ON cannot act on its own:
//      the jump asks ShouldHidePreviousChapter, which ALSO requires the results to have come from
//      a book. In document scope the toggle is greyed out and the sweep never runs.
//
//  What is deliberately NOT saved:
//    - Book Scope: which scope the NEXT run uses. That is the question being worked on right
//      now, not a preference, and it is already written on the panel's own tab where it can be seen.
//
//========================================================================================

#ifndef __KBSPanelState_h__
#define __KBSPanelState_h__

// Called from "Save Panel Settings" on the flyout. Writes the current settings to the JSON file
// and puts the full path on the panel's status line (or says why it could not).
// Implemented in KBSPanelState.cpp.
void	KBSSavePanelState();

// Reads the JSON file if it is there and applies it (does nothing when there is none).
// *Called at startup (KBSStartupShutdown::Startup): every setting restored here lives in a module
//  flag, not on a widget, so there is no need to wait for the panel.
// *Guarded to run once per session, so calling it again from anywhere is a harmless no-op.
// Implemented in KBSPanelState.cpp.
void	KBSLoadPanelStateIfPresent();

#endif // __KBSPanelState_h__
