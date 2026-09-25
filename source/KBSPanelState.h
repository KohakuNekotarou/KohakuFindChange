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
//    - Minimizable Find/Change (*Windows only, and the flag alone once more, for the reason given
//      on the line above: the dialog is not open at startup, and the same window-list observer puts
//      the style on the moment it is. Added 2026-08-12 with the feature - and added HERE at the same
//      time, because the line above records what it costs to leave this list behind.)
//    - Hide Previous Chapter (the user's call, 2026-08-04). It closes chapter windows as a jump
//      lands, which is why it was left out at first - but a restored ON cannot act on its own:
//      the jump asks ShouldHidePreviousChapter, which ALSO requires the results to have come from
//      a book. In document scope the toggle is greyed out and the sweep never runs.
//    - Remember Book Panel Placement (2026-09-25), and the placement itself: four integers, the
//      position and size InDesign's OWN Book panel had when it was last closed.
//      *****THESE ARE THE ONLY KEYS WRITTEN WITHOUT "Save Panel Settings".***** The user's rules
//      (2026-09-25): flipping the toggle writes the toggle's key, and while it is ticked, a book
//      panel closing - or InDesign quitting - writes the four placement keys. Nothing else in the
//      file is touched by either: KBSPanelStateWriteKeys below rewrites the named keys and leaves
//      every other key as the FILE has it, not as the flyout currently has it. See
//      KBSBookPanelPlacement.h.
//
//  What is deliberately NOT saved:
//    - Book Scope: which scope the NEXT run uses. That is the question being worked on right
//      now, not a preference, and it is already written on the panel's own tab where it can be seen.
//
//========================================================================================

#ifndef __KBSPanelState_h__
#define __KBSPanelState_h__

#include <string>
#include <utility>
#include <vector>

// Called from "Save Panel Settings" on the flyout. Writes the current settings to the JSON file
// and puts the full path on the panel's status line (or says why it could not).
// Implemented in KBSPanelState.cpp.
void	KBSSavePanelState();

// Reads the JSON file if it is there and applies it (does nothing when there is none).
// *Called from TWO places, whichever comes first: KBSStartupShutdown::Startup, and
//  KBSBookPanelPlacement::Start (the palette manager's PaletteMgrStarted - the startup service is a
//  LAZY one, and nothing promises it runs before the Book panel needs the placement). Every setting
//  restored here lives in a module flag, not on a widget, so there is no need to wait for the panel.
// *Guarded to run once per session, so the second call - and any other - is a harmless no-op.
// Implemented in KBSPanelState.cpp.
void	KBSLoadPanelStateIfPresent();

// Rewrite ONLY the given keys of the settings file and leave every other key exactly as the file has
// it - its value, and the key itself when this version does not know it. A key not yet in the file is
// added at the end. With no file yet, one is made holding "version" and these keys alone.
// *Values are written RAW (already JSON: "true", "-12"), so a string value would need its quotes.
// *A file that cannot be read as the flat object "Save Panel Settings" writes is NOT overwritten:
//  rewriting it would throw away whatever the user had saved in it. The caller is told instead.
// @return nil when the file was written; otherwise a short reason for the status line - "folder",
//         "read", "unreadable file", "open", "write".
// Implemented in KBSPanelState.cpp.
const char*	KBSPanelStateWriteKeys(const std::vector<std::pair<std::string, std::string> >& keyValues);

#endif // __KBSPanelState_h__
