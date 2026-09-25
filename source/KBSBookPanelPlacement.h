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
//    * the moment a book panel is about to close (kAboutToClosePaletteMsg), and when InDesign quits
//      (IPaletteMgrService::PaletteMgrAboutToShutdown), the FLOATING book panel's position and size
//      are measured and written to the settings file - those four keys and nothing else;
//    * the moment a book panel appears where there was none (kPaletteVisibilityChangedMessage, zero
//      book panels before, one or more now), that position and size are put back.
//
//  What it deliberately leaves alone:
//    * a DOCKED book panel. Where it sits in a dock belongs to the dock, and putting it back would
//      mean re-parenting palettes - nothing is measured and nothing is restored then;
//    * a book panel appearing BESIDE another one. It joins that palette as a tab, and moving the
//      palette would move the book panel the user already has open;
//    * a placement whose title band would land off every screen (a monitor that has since gone).
//
//  The file: KBSPanelState.json, the one "Save Panel Settings" writes. The toggle itself is written
//  to it the moment it is flipped (one key), and "Save Panel Settings" writes all of it. See
//  KBSPanelState.h.
//
//  *UI code (IPanelMgr, PaletteRefUtils, IControlView): it belongs to the UI half when KBS is split
//   into model and UI plug-ins.
//
//========================================================================================

#ifndef __KBSBookPanelPlacement_h__
#define __KBSBookPanelPlacement_h__

#include "PMString.h"

namespace KBSBookPanelPlacement
{
	/** Where a floating book panel was: the floating dock's top-left in global coordinates
	    (PaletteRefUtils::GetPalettePosition) and the book PANEL's own width and height
	    (IControlView::GetFrame) - the size the product's own code resizes a floating panel by
	    (LinksUIUtils.cpp, IControlView::Resize), not the dock's, which adds the title band. */
	struct Placement
	{
		int32	left;
		int32	top;
		int32	width;
		int32	height;

		Placement() : left(0), top(0), width(0), height(0) {}
	};

	/** The toggle. Session flag; OFF until the settings file says otherwise. */
	bool IsOn();

	/** Set the flag and nothing else - the startup read uses this. The flyout goes through
	    ToggleAndSave, which also writes the file and starts following the panels afresh. */
	void SetOn(bool on);

	/** The flyout's "Remember Book Panel Placement": flip the flag, write THAT ONE KEY to the settings
	    file (the user's rule, 2026-09-25), and hand back the line for the panel's status line. */
	void ToggleAndSave(PMString& outStatus);

	/** The placement known this session: read from the file at startup, or measured when a book panel
	    closed. false when there is none. */
	bool GetRemembered(Placement& out);

	/** Record a placement (the startup read, and a measurement). Refused when the size is not
	    positive - a panel with no size is not a place to put anything back to. */
	void SetRemembered(const Placement& placement);

	/** Measure the book panel that is open right now, if it floats. false when no book panel is open,
	    when it is docked, or when its palette cannot be reached. "Save Panel Settings" asks this so the
	    file gets the panel as it is on screen, not as it was at the last close. */
	bool MeasureOpenBookPanel(Placement& out);

	/** The palette manager has laid its palettes out (IPaletteMgrService::PaletteMgrStarted): start
	    following the panel manager, and treat any book panel already open as "just appeared". */
	void Start();

	/** The palette manager is about to close its palettes (PaletteMgrAboutToShutdown): write the open
	    book panel's placement if the toggle is ON, then stop following. */
	void Stop();

	/** Application shutdown (KBSStartupShutdown::Shutdown): stop following and give the timer back,
	    in case Stop was never reached. Safe to call twice. */
	void ShutdownCleanup();
}

#endif // __KBSBookPanelPlacement_h__
