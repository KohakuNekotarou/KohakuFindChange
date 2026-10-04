//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  Saves and restores the flyout's SETTINGS toggles as a small JSON file of our own, in the
//  user's roaming preferences folder. Ported from KCM's KCMPanelState.
//  *Nothing is written into InDesign's own data (workspace SavedData, documents).
//
//  Where: FileUtils::GetAppRoamingDataFolder(.., "KBSPanelState.json"), in that folder itself
//    (Windows) %APPDATA%\Adobe\InDesign\Version XX.0\<locale>\KBSPanelState.json
//  *No sub-folder is created (the author's rule, as for KCM). The folder is one InDesign already
//   makes for its preferences, but the file is ours alone and unrelated to anything InDesign keeps
//   there.
//
//  What is saved (SETTINGS only, not work state). !A setting added to KBSSavePanelState gets its line
//  here in the same change - this list has fallen behind the code more than once.
//    - Translucent Panel (*Windows only. Only the FLAG is restored; putting the alpha on the
//      window is done by the panel's AutoAttach and the palette-visibility observer.)
//    - Translucent Find/Change (*Windows only, and the same again: the flag alone. InDesign's own
//      Find/Change dialog is certainly not open at startup, and the window-list observer puts the
//      alpha on the moment it is.)
//    - Minimizable Find/Change (*Windows only, and the flag alone once more, for the reason given
//      on the line above: the dialog is not open at startup, and the same window-list observer puts
//      the style on the moment it is.)
//    - Link the Application Bar's Search Field to This Panel (key "appBarSearchEnter"; *Windows
//      only). Restoring ON is more than the flag: it puts up the toggle's two message hooks and its
//      observer on the Find/Change settings - on the main thread, where both callers of
//      KBSLoadPanelStateIfPresent run (KBSAppBarSearchEnter.h).
//    - Hide Previous Chapter (the author's call). It closes chapter windows as a jump lands - but a
//      restored ON cannot act on its own: the jump asks ShouldHidePreviousChapter, which ALSO
//      requires the results to have come from a book. In document scope the toggle is greyed out
//      and the sweep never runs.
//    - Remember Book Panel Placement, and the placement itself - where InDesign's OWN
//      Book panel was when it was last closed (floating: place, size, icon state and width; docked:
//      its neighbours). The keys are named in KBSBookPanelPlacement.cpp and ONLY there - this file
//      writes what that one hands over (AppendSaveKeys) and hands it the text to read
//      (LoadFromSettings), so a key added there needs nothing here.
//      THESE ARE THE ONLY KEYS WRITTEN WITHOUT "Save Panel Settings". The author's rules:
//      flipping the toggle writes the toggle's key, and while it is ticked, a book
//      closing - InDesign quitting included - writes the placement keys. Nothing else in the
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

#include "PMString.h"

#include <string>
#include <utility>
#include <vector>

// Called from "Save Panel Settings" on the flyout. Writes the current settings to the JSON file
// and puts the full path on the panel's status line (or says why it could not).
// Implemented in KBSPanelState.cpp.
void	KBSSavePanelState();

// Reads the JSON file if it is there and applies it (does nothing when there is none).
// *Called from TWO places, whichever comes first: KBSUIStartupShutdown::Startup, and
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
// *A file that cannot be read as the flat object "Save Panel Settings" writes is REPAIRED (the author's
//  call): every "key": value pair that stands complete in it is kept, the rest dropped, and the file is
//  written again with these keys. (Refusing the write instead would stop the book panel's placement being
//  kept at all once a crash had cut the file short.) outRepaired (when not nil) says it happened.
// *Every write goes through a side file (KBSPanelState.json.tmp) that is moved over the real one only
//  when written in full, so a crash part way leaves the old file whole.
// @return nil when the file was written; otherwise a short reason for the status line - "folder",
//         "read", "open", "write", "replace".
// Implemented in KBSPanelState.cpp.
const char*	KBSPanelStateWriteKeys(const std::vector<std::pair<std::string, std::string> >& keyValues,
	bool* outRepaired = nil);

// The settings file's full path, as "Save Panel Settings" shows it on the status line. false when the
// folder cannot be had. The toggle that writes its own key shows the same path (the author's call:
// "show where it was saved, the way Save Panel Settings does").
// Implemented in KBSPanelState.cpp.
bool	KBSPanelStateFilePath(PMString& outPath);

// The file's readers, for KBSBookPanelPlacement: it names its own keys (all of them, in one place)
// but reads them with the same lenient readers every other setting goes through. A key that is not
// there: ReadInt answers false and leaves out alone; ReadBool answers defVal.
// Implemented in KBSPanelState.cpp.
bool	KBSPanelStateReadInt(const std::string& text, const char* key, int32& out);
bool	KBSPanelStateReadBool(const std::string& text, const char* key, bool defVal);

#endif // __KBSPanelState_h__
