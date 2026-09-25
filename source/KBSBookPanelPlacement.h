//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Remember Book Panel Placement" (2026-09-25, the user's request): InDesign's OWN Book panel is
//  built afresh for every book, so once the last book is closed the next one opens at the default
//  place and size again - wherever the user had put it. Measured on 21.0 before this was written
//  (the palette moved with SetWindowPos, not dragged by hand): moved to (900,300), book closed and
//  reopened -> back at (532,252) 304x275, exactly where an untouched one reopens.
//
//  What this does, only while the flyout toggle is ticked:
//    * the moment a book is about to close - by the Book panel, by a script, or by InDesign quitting
//      (a quit closes the books first) - where the Book panel is, is measured and written to the
//      settings file - the Book panel's keys and nothing else. Caught with a command interceptor on
//      kCloseBookCmdBoss: a closing book says nothing to the panel manager (measured - the .cpp's
//      header has the story). "Where" is one of two things:
//        FLOATING  the floating dock's top-left, the panel's size, and whether it is collapsed to
//                  icons;
//        DOCKED    which panels it sits next to - a panel sharing its tab group (and its tab's place
//                  there), the nearest panels in the tab groups above and below it, and in the
//                  columns (tab panes) either side - and whether its column is collapsed to icons.
//                  A dock position is kept as NEIGHBOURS rather than coordinates because that is
//                  what a dock is made of, and a built-in panel's WidgetID does not change between
//                  launches (the user's request, 2026-09-25: "in the dock, and its order there").
//    * the moment a book panel appears where there was none (kPaletteVisibilityChangedMessage, zero
//      book panels before, one or more now), it is put back: moved and sized, or moved into its dock
//      next to the same neighbours (PaletteRefUtils::ReparentPalette).
//
//  What it deliberately leaves alone:
//    * a book panel appearing BESIDE another one. It joins that palette as a tab, and moving the
//      palette would move the book panel the user already has open;
//    * a book panel InDesign itself put in a dock: that is InDesign's own memory at work;
//    * a floating placement whose title band would land off every screen (a monitor that has gone),
//      and a docked one whose neighbours are all gone.
//
//  The file: KBSPanelState.json, the one "Save Panel Settings" writes. The toggle itself is written
//  to it the moment it is flipped (one key), and "Save Panel Settings" writes all of it. EVERY key of
//  this feature is named in the .cpp and only there - KBSPanelState asks this file for them.
//
//  *UI code (IPanelMgr, PaletteRefUtils, IControlView): it belongs to the UI half when KBS is split
//   into model and UI plug-ins.
//
//========================================================================================

#ifndef __KBSBookPanelPlacement_h__
#define __KBSBookPanelPlacement_h__

#include "PMString.h"

#include <string>
#include <utility>
#include <vector>

namespace KBSBookPanelPlacement
{
	/** The toggle. Session flag; OFF until the settings file says otherwise. */
	bool IsOn();

	/** The flyout's "Remember Book Panel Placement": flip the flag, put the command interceptor in or
	    take it out with it, write THAT ONE KEY to the settings file (the user's rule, 2026-09-25), and
	    hand back the line for the panel's status line - the new state and, like "Save Panel
	    Settings", where the file is. */
	void ToggleAndSave(PMString& outStatus);

	/** For "Save Panel Settings": the toggle and the placement, as keys and RAW JSON values, appended
	    in the order they are to be written. The book panel is measured as it stands NOW if one is
	    open - an explicit save is a snapshot of the screen - otherwise the placement last measured
	    (or read at startup) is written again. */
	void AppendSaveKeys(std::vector<std::pair<std::string, std::string> >& keys);

	/** At startup: the toggle and the placement from the settings file's text. Nothing is moved here -
	    what puts the placement on a book panel is this file, when one appears. */
	void LoadFromSettings(const std::string& text);

	/** The palette manager has laid its palettes out (IPaletteMgrService::PaletteMgrStarted): start
	    following the panel manager, put the interceptor in if the toggle is ON (ToggleAndSave puts it in
	    and takes it out after that - it sees every command, so it is only there while it is needed), and
	    treat any book panel already open as "just appeared". */
	void Start();

	/** The palette manager is about to close its palettes (PaletteMgrAboutToShutdown): stop following.
	    It also measures an open book panel if the toggle is ON - a backstop only: on 21.0.2 a quit
	    closes the books BEFORE this, and the interceptor has already written the placement. */
	void Stop();

	/** Application shutdown (KBSStartupShutdown::Shutdown): stop following and give the timer and the
	    interceptor back, in case Stop was never reached. Safe to call twice. */
	void ShutdownCleanup();
}

#endif // __KBSBookPanelPlacement_h__
