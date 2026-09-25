//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Remember Book Panel Placement". What it does and what it leaves alone is in
//  KBSBookPanelPlacement.h. This file holds the four pieces that make it run:
//
//    1. a COMMAND INTERCEPTOR that sees kCloseBookCmdBoss BEFORE it runs, and measures the Book
//       panel while it still stands. ***** It is the only moment there is. ***** Measured on 21.0.2
//       (2026-09-25, a temporary log): closing a book - by script, by the Book panel's own "Close
//       Book", and by quitting InDesign alike - destroys its panel (kDestroyPanelCmdBoss, which runs
//       AFTER kCloseBookCmdBoss) without one notification on the panel manager's subject: neither
//       kAboutToClosePaletteMsg nor kPaletteVisibilityChangedMessage arrives. And a quit closes the
//       books BEFORE IPaletteMgrService::PaletteMgrAboutToShutdown - by then there is no Book panel
//       left to measure. The first version of this file listened for kAboutToClosePaletteMsg and
//       measured at PaletteMgrAboutToShutdown, and so never recorded anything (the user's own test:
//       "closed and reopened InDesign, it did not come back").
//
//    2. the OBSERVER, AddIn'd onto kActiveContextBoss and attached to the panel manager's subject
//       (IID_IPANELMGR) - the same subject and arrangement as the "Translucent Panel" observer in
//       KBSPanelAlpha.cpp. It acts on kPaletteVisibilityChangedMessage only: a book OPENING does
//       announce itself there, after its panel is built (measured in the same log).
//
//    3. the PALETTE MANAGER SERVICE (IPaletteMgrService, kPaletteMgrService) - what the Book panel
//       ITSELF hangs off (kBookPanelStartupShutdownBoss is the product's only implementation,
//       Service_Registry_Memory_Dump.txt:2761). Its PaletteMgrStarted is where the observer and -
//       only while the toggle is ON - the interceptor go in (measured: it IS called for a third-party
//       provider; the toggle itself puts the interceptor in and out after that), because the startup
//       service is too early for the panel manager (KBSPanelAlpha.cpp says why); PaletteMgrAboutTo-
//       Shutdown is where they come out.
//
//    4. a one-shot ICallbackTimer that puts the placement back a moment after the Book panel
//       appears, not inside the notification: OWL lays palettes out asynchronously
//       (PaletteRefUtils.h:40-41). 100ms was enough on the machine it was measured on.
//
//  ***** FLOATING: THE DOCK AND THE PANEL ARE MEASURED SEPARATELY, AND ON PURPOSE. *****
//    Position = the FLOATING DOCK's top-left (GetPalettePosition). SetPalettePosition only takes a
//      floating Dock, Toolbar or ControlBar (PaletteRefUtils.h:361-366), so the palette tree is
//      walked up from the panel's container to that dock - the walk KESCM used when it moved its own
//      panel to the cursor (2026-07-10, removed later with that feature). Measured on 21.0.2: the
//      Book panel's container is four steps below its floating dock (tab group, tab pane, dock).
//    Size = the book PANEL's frame. A floating panel is resized by resizing the panel view - that is
//      what the product does (LinksUIUtils.cpp:650-653, :720-723); SetPaletteSize is its route for a
//      DOCKED palette only. Measured 2026-09-25: moving the dock window itself with SetWindowPos to
//      360x420 left the panel inside at 302x224 - the size has to go through OWL, not the window.
//      *The Book panel rounds its own height: asked for 380x420, it came back 380x400
//       (ConstrainDimensions). A size the user dragged to already obeys that rule, so it round-trips.
//      *A size whose bottom would land below the screen at the new place is shortened before it is
//       put on (FitHeightToScreen - the product's ForceBottomOfPanelOnMonitor, LinksUIUtils.cpp:613-629,
//       asked of the place to come because OWL moves the dock asynchronously).
//    Collapsed to icons = the TAB PANE's mode, and the width of the icon strip is the tab pane's
//      "preferred iconic width" (PaletteRefUtils.h:300-312) - both put back last, on a panel that has
//      already been moved and sized expanded. (Confirmed working by the user, 2026-09-25.)
//    ***** A FLOATING PALETTE CAN BE SHARED, AND THEN THE PLACE IS NOT THE ANSWER. ***** The user
//      tabbed the Book panel into the floating KBS panel, closed InDesign and opened a book: the Book
//      panel came back as a palette of its own, laid exactly on top of KBS's (2026-09-25 - KIDMCP's
//      panels listing showed the two in different floating docks at one rectangle). Only the dock's
//      position had been kept, and a new book panel always arrives in a palette of its own. So a
//      floating book panel's neighbours are measured as a docked one's are - the panel in its tab
//      group with its tab's place, the nearest panels in the groups above and below - and put back
//      first; the place and size are what is used when no neighbour's palette is open.
//
//  ***** DOCKED: KEPT AS NEIGHBOURS, PUT BACK WITH ReparentPalette. ***** A dock is a list of columns
//    (tab panes), each a list of tab groups, each a list of tabs (PaletteRef.h:96-101), so a place in
//    it is "next to these", not an x and a y. What is recorded is the WidgetID of a panel that is NOT
//    a book panel (a book panel's WidgetID is made up per book at run time) in: the same tab group,
//    the nearest group below and above, and the nearest column after and before. Putting it back
//    tries them in that order - the closer the neighbour, the more exact the place.
//    ***** Measured 2026-09-25 (the user's tests, read back from a temporary log): into the mate's
//    group at tab 0 and at tab 1, above the next group, below the previous one - each came back where
//    it had been, and the floating palette it was taken out of went away with it. NOT measured: a
//    new column (NewTabPaneInDock), because no test had a second column in the dock.
//    ! Seen once and not again (the user, same day): the Book panel's icon drawn as KBS's own. KBS
//      writes no palette icon anywhere, so the suspicion is OWL's icon strip after a reparent - not
//      confirmed.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActiveContext.h"		// where the observer implementation lives (kActiveContextBoss)
#include "IApplication.h"		// QueryPanelManager
#include "IBookManager.h"		// GetBookCount - is the book about to close the last one?
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "ICommand.h"			// what the interceptor is handed (its class says which command)
#include "ICommandInterceptor.h"	// catching kCloseBookCmdBoss BEFORE it runs
#include "ICommandProcessor.h"	// InstallInterceptor / DeinstallInterceptor
#include "IControlView.h"		// GetFrame / ConstrainDimensions / Resize - the book panel itself
#include "IMonitorInfo.h"		// GetBestScreenRect - is the remembered place still on a screen?
#include "IObserver.h"
#include "IPaletteMgrService.h"	// the palette manager's start and shut-down
#include "IPanelMgr.h"			// the panel list, and the palette holding a panel
#include "ISession.h"			// GetExecutionContextSession (can be nil during shutdown)
#include "ISubject.h"			// AttachObserver / IsAttached / DetachObserver

// General includes:
#include "AppUIID.h"			// kPaletteVisibilityChangedMessage (:325)
#include "BookID.h"				// kCloseBookCmdBoss - the command every book close goes through
#include "CObserver.h"
#include "CPMUnknown.h"
#include "CreateObject.h"		// ::CreateObject / ::CreateObject2
#include "CServiceProvider.h"
#include "IDThreadingPrimitives.h"	// IsMainThreadDomain - the interceptor's gate (see KBSBookPanelCmdWatch)
#include "PaletteRef.h"
#include "PaletteRefUtils.h"	// the palette tree: walk it, measure it, move things about in it
#include "ShuksanID.h"			// kCallbackTimerBoss, IID_ICALLBACKTIMER
#include "WorkspaceID.h"		// kPaletteMgrService, IID_IPALETTEMGRSERVICE

// Project includes:
#include "KBSID.h"
#include "KBSBookPanelPlacement.h"
#include "KBSBookScope.h"		// IsBookPanel - the one place that decides what a book panel is
#include "KBSPanelState.h"		// KBSPanelStateWriteKeys and the readers - the settings file, key by key
#include "KBSResultTree.h"		// ShowStatus - a write that failed is said, not swallowed

namespace
{

/** Where the Book panel was. One of the two halves is in force (docked says which); the other is kept
    as it last was, so a panel that goes from floating to docked and back comes back to its old
    floating place. */
struct Placement
{
	// Floating.
	bool	haveFloat;
	int32	left;			// the floating dock's top-left, global coordinates
	int32	top;
	int32	width;			// the book PANEL's size (not the dock's, which adds the title band)
	int32	height;

	// Floating in a palette SHARED with other panels: WidgetIDs of the nearest non-book panels in
	// that palette, 0 = none (alone in its palette). Measured with the floating half, and tried before
	// the place when it is put back (see the file header).
	int32	floatMate;		// a panel in the same tab group ...
	int32	floatTabIndex;	// ... and the book panel's tab position in that group
	int32	floatNextGroup;	// the nearest panel in a tab group below, in the same palette
	int32	floatPrevGroup;	// ... and above

	// Docked: WidgetIDs of the nearest non-book panels, 0 = none.
	bool	docked;
	int32	mate;			// a panel in the same tab group ...
	int32	tabIndex;		// ... and the book panel's tab position in that group
	int32	nextGroup;		// the nearest panel in a tab group below, in the same column
	int32	prevGroup;		// ... and above
	int32	nextColumn;		// the nearest panel in a column after, in the same dock
	int32	prevColumn;		// ... and before

	// Both: the tab pane's (floating palette's, or dock column's) icon state and icon-strip width.
	bool	iconic;
	int32	iconicWidth;	// 0 = not known

	Placement() : haveFloat(false), left(0), top(0), width(0), height(0),
		floatMate(0), floatTabIndex(0), floatNextGroup(0), floatPrevGroup(0),
		docked(false), mate(0), tabIndex(0), nextGroup(0), prevGroup(0), nextColumn(0), prevColumn(0),
		iconic(false), iconicWidth(0) {}

	bool IsUsable() const { return docked || haveFloat; }
};

/** The toggle (session flag). */
bool gOn = false;

/** The last placement known this session - from the file at startup, or from a close. */
Placement gRemembered;

/** How many book panels there were at the last look. A restore is due when this goes from zero to
    anything, and ONLY then - a book opened beside another joins that palette as a tab.
    ***** Nothing announces the drop to zero. ***** A closing book says nothing to the panel manager
    (see the file header), so OnBookAboutToClose puts this to zero itself when the book about to
    close is the last one. Counted only while the toggle is ON; switching it on and Start() take a
    fresh count, so a stale number cannot make an already-open panel look new. */
int32 gBookPanelCount = 0;

/** The deferred restore. ONE object for the life of the plug-in, released only by
    DisarmRestoreTimer - never from inside its own callback (KBSBookWatch.cpp explains why). */
ICallbackTimer* gRestoreTimer = nil;

/** How long after the book panel appears the placement is put back. A beat, so OWL's own layout of
    the new palette (asynchronous - PaletteRefUtils.h:40-41) has gone round first. */
const uint32 kKBSBookPanelRestoreDelayMs = 100;

/** How much of the title band has to be on a screen for a remembered placement to be used: enough
    to take hold of it and drag it back. */
const SysCoord kKBSBookPanelMinOnScreen = 40;

/** Every key this feature writes. Named here and nowhere else (KBSPanelState asks this file). */
const char* const kKeyToggle      = "rememberBookPanelPlacement";
const char* const kKeyLeft        = "bookPanelLeft";
const char* const kKeyTop         = "bookPanelTop";
const char* const kKeyWidth       = "bookPanelWidth";
const char* const kKeyHeight      = "bookPanelHeight";
const char* const kKeyFloatMate      = "bookPanelFloatMate";
const char* const kKeyFloatTabIndex  = "bookPanelFloatTabIndex";
const char* const kKeyFloatNextGroup = "bookPanelFloatNextGroup";
const char* const kKeyFloatPrevGroup = "bookPanelFloatPrevGroup";
const char* const kKeyIconic      = "bookPanelIconic";
const char* const kKeyIconicWidth = "bookPanelIconicWidth";
const char* const kKeyDocked      = "bookPanelDocked";
const char* const kKeyMate        = "bookPanelMate";
const char* const kKeyTabIndex    = "bookPanelTabIndex";
const char* const kKeyNextGroup   = "bookPanelNextGroup";
const char* const kKeyPrevGroup   = "bookPanelPrevGroup";
const char* const kKeyNextColumn  = "bookPanelNextColumn";
const char* const kKeyPrevColumn  = "bookPanelPrevColumn";

/** The installed command interceptor, or nil. Held because the processor keeps a RAW pointer:
    releasing it while installed would leave InDesign calling into freed memory (KIDMCPCmdWatch.cpp
    says the same of its own). */
ICommandInterceptor* gCmdWatch = nil;

//----------------------------------------------------------------------------------------
// The palette tree
//----------------------------------------------------------------------------------------

/** The panel manager, AddRef'd - or nil: during startup it may not exist yet, and during teardown
    the session can already be gone. */
IPanelMgr* QueryPanelManager()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;
	return app->QueryPanelManager();
}

/** Walk the registered panels: how many are book panels, and (when asked) the first of them,
    AddRef'd. InDesign makes one book panel per open book and registers each with IPanelMgr; the
    WidgetID is numbered per book at run time, so walking is the only way to find them
    (docs/ai-notes/book-panel-active-tab.md - the same walk KBSBookScope uses). */
int32 WalkBookPanels(IPanelMgr* panelMgr, IControlView** outFirst)
{
	if (outFirst != nil)
		*outFirst = nil;
	if (panelMgr == nil)
		return 0;

	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return 0;

	int32 count = 0;
	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		if (!panelMgr->GetNthPanelInfo(i, panelUID))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!KBSBookScope::IsBookPanel(panelView))
			continue;

		// Only a panel that is in a palette counts. Measured 2026-09-25: a closed book's panel is off
		// the list by the next look, so this has never had to act - it is here because nothing
		// PROMISES the list is pruned, and a stale entry would keep the count at one.
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid())
			continue;

		++count;
		if (outFirst != nil && *outFirst == nil)
			*outFirst = panelView.forget();
	}
	return count;
}

/** ***** Never hand PaletteRefUtils an invalid PaletteRef. ***** Nothing in PaletteRefUtils.h says
    what its functions do with one, and a dock that is not the shape this file expects - or a
    neighbour whose palette cannot be found - would otherwise walk an invalid ref straight into
    GetParentOfPalette / GetChildCountOfPalette. These two are the only doors to the tree the dock
    half uses: an invalid ref in, an invalid ref (or "no") out. (Second re-check, 2026-09-25.) */
PaletteRef ParentOf(const PaletteRef& pal)
{
	return pal.IsValid() ? PaletteRefUtils::GetParentOfPalette(pal) : PaletteRef();
}

bool Is(const PaletteRef& pal, bool16 (*test)(const PaletteRef&))
{
	return pal.IsValid() && test(pal);
}

/** The nearest palette at or above this one that passes the test (one of PaletteRefUtils' Is...
    questions), walking up the palette tree - or an invalid PaletteRef when there is none. The guard
    only stops a malformed tree from looping; a real one is a handful of levels deep - measured on
    21.0.2 for the Book panel: container -> tab group -> tab pane -> floating dock. */
PaletteRef FindAncestor(const PaletteRef& from, bool16 (*test)(const PaletteRef&))
{
	PaletteRef pal = from;
	for (int32 guard = 0; guard < 16 && pal.IsValid(); ++guard)
	{
		if (test(pal))
			return pal;
		pal = PaletteRefUtils::GetParentOfPalette(pal);
	}
	return PaletteRef();
}

PaletteRef FindFloatingDock(const PaletteRef& container)
{
	return FindAncestor(container, &PaletteRefUtils::IsFloatingTabbedPaletteDock);
}

/** This child's place among its parent's children, or -1. */
int32 IndexOfChild(const PaletteRef& parent, const PaletteRef& child)
{
	if (!parent.IsValid())
		return -1;
	const uint16 n = PaletteRefUtils::GetChildCountOfPalette(parent);
	for (uint16 i = 0; i < n; ++i)
	{
		if (PaletteRefUtils::GetNthChildOfPalette(parent, i) == child)
			return i;
	}
	return -1;
}

/** The child at this place, or an invalid PaletteRef past the end - which is what ReparentPalette
    and NewTabPaneInDock take for "at the end" (PaletteRefUtils.h:208, :437). */
PaletteRef ChildAtOrEnd(const PaletteRef& parent, int32 index)
{
	if (!parent.IsValid() || index < 0 || index >= PaletteRefUtils::GetChildCountOfPalette(parent))
		return PaletteRef();
	return PaletteRefUtils::GetNthChildOfPalette(parent, static_cast<uint16>(index));
}

/** The WidgetID of the first panel in this tab group that is not a book panel, or 0. A book panel's
    WidgetID is numbered per book at run time, so it cannot be looked for again after a restart. */
int32 FirstNonBookPanelInGroup(IPanelMgr* panelMgr, const PaletteRef& group)
{
	if (!group.IsValid())
		return 0;
	const uint16 n = PaletteRefUtils::GetChildCountOfPalette(group);
	for (uint16 i = 0; i < n; ++i)
	{
		IControlView* panel = panelMgr->GetPanelFromPaletteContainer(PaletteRefUtils::GetNthChildOfPalette(group, i));
		if (panel != nil && !KBSBookScope::IsBookPanel(panel))
			return static_cast<int32>(panel->GetWidgetID().Get());
	}
	return 0;
}

/** ...and in a whole column (tab pane): the first such panel in any of its groups, or 0. */
int32 FirstNonBookPanelInColumn(IPanelMgr* panelMgr, const PaletteRef& column)
{
	if (!column.IsValid())
		return 0;
	const uint16 n = PaletteRefUtils::GetChildCountOfPalette(column);
	for (uint16 i = 0; i < n; ++i)
	{
		const int32 id = FirstNonBookPanelInGroup(panelMgr, PaletteRefUtils::GetNthChildOfPalette(column, i));
		if (id != 0)
			return id;
	}
	return 0;
}

/** The container of the panel with this WidgetID - or an invalid PaletteRef when there is no such
    panel or it is in no palette. (GetPanelFromWidgetID hands back hidden panels too; what the
    container of a panel the user has CLOSED is, is not measured.) */
PaletteRef ContainerOfPanel(IPanelMgr* panelMgr, int32 widgetID)
{
	if (widgetID == 0)
		return PaletteRef();
	IControlView* panel = panelMgr->GetPanelFromWidgetID(WidgetID(widgetID));
	if (panel == nil)
		return PaletteRef();
	return panelMgr->GetPaletteRefContainingPanel(panel);
}

//----------------------------------------------------------------------------------------
// Measuring
//----------------------------------------------------------------------------------------

/** The tab pane's icon state and icon-strip width, into out. */
void MeasureIconState(const PaletteRef& tabPane, Placement& out)
{
	if (!tabPane.IsValid())
		return;
	out.iconic = (PaletteRefUtils::GetTabPaneMode(tabPane) == PaletteRefUtils::kIcon_TabPaneMode);
	const int32 w = ::ToInt32(PMReal(PaletteRefUtils::GetTabPanePreferredIconicWidth(tabPane)));
	if (w > 0)
		out.iconicWidth = w;
}

/** The neighbours inside one column: a panel sharing the book panel's tab group (and the book
    panel's tab place in it), and the nearest panels in the groups below and above. The same
    question for a dock and for a floating palette - both hold tab groups in a tab pane - so it is
    asked in one place. Zeros when a neighbour is not there. */
void MeasureGroupNeighbours(IPanelMgr* panelMgr, const PaletteRef& container, const PaletteRef& group,
	const PaletteRef& column, int32& mate, int32& tabIndex, int32& nextGroup, int32& prevGroup)
{
	tabIndex = IndexOfChild(group, container);
	// A mate is any OTHER non-book panel in the group - the book panel itself is not a non-book
	// panel, so it is never picked.
	mate     = FirstNonBookPanelInGroup(panelMgr, group);

	const int32 gi = IndexOfChild(column, group);
	const int32 groups = PaletteRefUtils::GetChildCountOfPalette(column);
	nextGroup = 0;
	for (int32 i = gi + 1; i < groups && nextGroup == 0; ++i)
		nextGroup = FirstNonBookPanelInGroup(panelMgr, ChildAtOrEnd(column, i));
	prevGroup = 0;
	for (int32 i = gi - 1; i >= 0 && prevGroup == 0; --i)
		prevGroup = FirstNonBookPanelInGroup(panelMgr, ChildAtOrEnd(column, i));
}

/** A floating book panel: the other panels in its palette, if it shares one (see the file header).
    All zeros for a palette of its own - or a tree not shaped as tab group in a tab pane. */
void MeasureFloatNeighbours(IPanelMgr* panelMgr, const PaletteRef& container, Placement& out)
{
	out.floatMate = out.floatTabIndex = out.floatNextGroup = out.floatPrevGroup = 0;

	const PaletteRef group  = ParentOf(container);
	const PaletteRef column = ParentOf(group);
	if (!Is(group, &PaletteRefUtils::IsTabGroup) || !Is(column, &PaletteRefUtils::IsTabPane))
		return;

	MeasureGroupNeighbours(panelMgr, container, group, column,
		out.floatMate, out.floatTabIndex, out.floatNextGroup, out.floatPrevGroup);
}

/** A docked book panel: its neighbours. false when the tree is not the shape a dock is supposed to
    have (tab group in a tab pane in a dock). */
bool MeasureDocked(IPanelMgr* panelMgr, const PaletteRef& container, Placement& out)
{
	const PaletteRef group  = ParentOf(container);
	const PaletteRef column = ParentOf(group);
	const PaletteRef dock   = ParentOf(column);
	if (!Is(group, &PaletteRefUtils::IsTabGroup) || !Is(column, &PaletteRefUtils::IsTabPane) || !Is(dock, &PaletteRefUtils::IsDock))
		return false;

	out.docked = true;
	MeasureGroupNeighbours(panelMgr, container, group, column,
		out.mate, out.tabIndex, out.nextGroup, out.prevGroup);

	const int32 ci = IndexOfChild(dock, column);
	const int32 columns = PaletteRefUtils::GetChildCountOfPalette(dock);
	out.nextColumn = 0;
	for (int32 i = ci + 1; i < columns && out.nextColumn == 0; ++i)
	{
		const PaletteRef c = ChildAtOrEnd(dock, i);
		if (Is(c, &PaletteRefUtils::IsTabPane))
			out.nextColumn = FirstNonBookPanelInColumn(panelMgr, c);
	}
	out.prevColumn = 0;
	for (int32 i = ci - 1; i >= 0 && out.prevColumn == 0; --i)
	{
		const PaletteRef c = ChildAtOrEnd(dock, i);
		if (Is(c, &PaletteRefUtils::IsTabPane))
			out.prevColumn = FirstNonBookPanelInColumn(panelMgr, c);
	}

	MeasureIconState(column, out);
	return true;
}

/** Measure one book panel, starting from what is remembered so the half not in force is kept.
    false - and out untouched - when nothing whole could be measured. */
bool Measure(IPanelMgr* panelMgr, IControlView* bookPanel, Placement& out)
{
	if (panelMgr == nil || bookPanel == nil)
		return false;

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	if (!container.IsValid())
		return false;

	Placement measured = gRemembered;

	if (!PaletteRefUtils::IsPaletteFloating(container))
	{
		if (!MeasureDocked(panelMgr, container, measured))
			return false;
		out = measured;
		return true;
	}

	// Floating. Minimised to its title bar, its frame is not the size to come back to.
	if (PaletteRefUtils::IsPaletteMinimized(container))
		return false;

	const PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
		return false;

	const SysPoint pos = PaletteRefUtils::GetPalettePosition(dock);
	const PMRect frame = bookPanel->GetFrame();
	measured.docked = false;
	measured.left   = SysPointH(pos);
	measured.top    = SysPointV(pos);
	MeasureIconState(FindAncestor(container, &PaletteRefUtils::IsTabPane), measured);
	MeasureFloatNeighbours(panelMgr, container, measured);

	// Collapsed to icons, the panel is not on show, and what its frame says then is not a size to
	// come back to if it is empty. Keep the size known from before - the one it will expand to - and
	// take only the place and the icon state from now.
	const int32 w = ::ToInt32(frame.Width());
	const int32 h = ::ToInt32(frame.Height());
	if (w > 0 && h > 0)
	{
		measured.width  = w;
		measured.height = h;
		measured.haveFloat = true;
	}
	else if (!(measured.iconic && measured.haveFloat))
	{
		return false;
	}

	out = measured;
	return true;
}

/** Measure the book panel that is open now, if any. */
bool MeasureOpenBookPanel(Placement& out)
{
	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	IControlView* first = nil;
	WalkBookPanels(panelMgr, &first);
	InterfacePtr<IControlView> bookPanel(first);	// takes over the reference WalkBookPanels added
	return Measure(panelMgr, bookPanel, out);
}

//----------------------------------------------------------------------------------------
// The keys
//----------------------------------------------------------------------------------------

std::string BoolValue(bool b) { return b ? "true" : "false"; }

/** The placement as keys and raw values - the half in force, and the other half as it was (so a
    panel that went from floating to docked still has a floating place to return to). */
void AppendPlacementKeys(const Placement& p, std::vector<std::pair<std::string, std::string> >& keys)
{
	keys.push_back(std::make_pair(std::string(kKeyDocked), BoolValue(p.docked)));
	if (p.haveFloat)
	{
		keys.push_back(std::make_pair(std::string(kKeyLeft),   std::to_string(p.left)));
		keys.push_back(std::make_pair(std::string(kKeyTop),    std::to_string(p.top)));
		keys.push_back(std::make_pair(std::string(kKeyWidth),  std::to_string(p.width)));
		keys.push_back(std::make_pair(std::string(kKeyHeight), std::to_string(p.height)));
		// Written as zeros too: a palette of its own has to CLEAR a neighbour an earlier close left.
		keys.push_back(std::make_pair(std::string(kKeyFloatMate),      std::to_string(p.floatMate)));
		keys.push_back(std::make_pair(std::string(kKeyFloatTabIndex),  std::to_string(p.floatTabIndex)));
		keys.push_back(std::make_pair(std::string(kKeyFloatNextGroup), std::to_string(p.floatNextGroup)));
		keys.push_back(std::make_pair(std::string(kKeyFloatPrevGroup), std::to_string(p.floatPrevGroup)));
	}
	if (p.docked)
	{
		keys.push_back(std::make_pair(std::string(kKeyMate),       std::to_string(p.mate)));
		keys.push_back(std::make_pair(std::string(kKeyTabIndex),   std::to_string(p.tabIndex)));
		keys.push_back(std::make_pair(std::string(kKeyNextGroup),  std::to_string(p.nextGroup)));
		keys.push_back(std::make_pair(std::string(kKeyPrevGroup),  std::to_string(p.prevGroup)));
		keys.push_back(std::make_pair(std::string(kKeyNextColumn), std::to_string(p.nextColumn)));
		keys.push_back(std::make_pair(std::string(kKeyPrevColumn), std::to_string(p.prevColumn)));
	}
	keys.push_back(std::make_pair(std::string(kKeyIconic), BoolValue(p.iconic)));
	if (p.iconicWidth > 0)
		keys.push_back(std::make_pair(std::string(kKeyIconicWidth), std::to_string(p.iconicWidth)));
}

/** Say on the panel's status line that a write went wrong. Only failures are said: a write at every
    book close that reported success would be a line about something the user did not do. */
void SayWriteFailed(const char* reason)
{
	PMString msg("Book panel placement could not be saved (");
	msg.Append(reason);
	msg.Append(").");
	msg.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(msg);
}

/** Keep this placement for the session and write its keys - nothing else - to the settings file.
    Silent when it works (see SayWriteFailed). */
void RememberAndWrite(const Placement& p, bool sayFailure)
{
	if (!p.IsUsable())
		return;
	gRemembered = p;

	std::vector<std::pair<std::string, std::string> > keys;
	AppendPlacementKeys(p, keys);
	const char* failure = KBSPanelStateWriteKeys(keys);
	if (failure != nil && sayFailure)
		SayWriteFailed(failure);
}

//----------------------------------------------------------------------------------------
// Putting it back
//----------------------------------------------------------------------------------------

/** Would the palette's title band land on a screen if put here? Asked of the screen that holds most
    of the rectangle (IMonitorInfo::GetBestScreenRect handles more than one monitor - KCM's
    KeepPanelOnScreen asks it the same way). "Cannot tell" answers no: a palette left where it
    opened is a smaller fault than one put where nobody can reach it.
    *Only the TOP is judged here, and that is enough for a yes: a bottom edge that would land below
    the screen is not a reason to give up the whole placement - FitHeightToScreen shortens the panel
    instead. */
bool TitleBandIsOnScreen(const Placement& p)
{
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return false;
	InterfacePtr<const IMonitorInfo> monInfo(app, UseDefaultIID());
	if (monInfo == nil)
		return false;

	SysRect wanted;
	::SetSysRectLTWH(wanted, p.left, p.top, p.width, p.height);
	const GSysRect screen = monInfo->GetBestScreenRect(wanted);

	// Across: this much of the width has to overlap the screen.
	const SysCoord overlapLeft  = (p.left > SysRectLeft(screen)) ? p.left : SysRectLeft(screen);
	const SysCoord wantedRight  = p.left + p.width;
	const SysCoord overlapRight = (wantedRight < SysRectRight(screen)) ? wantedRight : SysRectRight(screen);
	if (overlapRight - overlapLeft < kKBSBookPanelMinOnScreen)
		return false;

	// Down: the top edge - where the title band is - has to be on the screen, with room under it.
	if (p.top < SysRectTop(screen) || p.top > SysRectBottom(screen) - kKBSBookPanelMinOnScreen)
		return false;

	return true;
}

/** The size to give the book panel so that, at the place it is ABOUT TO BE MOVED TO, its bottom edge
    stays on the screen. panelLeft / panelTop are where the panel itself (not its dock) will be.

    ***** The product's rule, and the sibling's. ***** linksui's ForceBottomOfPanelOnMonitor
    (LinksUIUtils.cpp:613-629) runs right after a FLOATING panel is resized (:723 -> :735): ask which
    screen the panel is on (GetBestScreenRect), and if its bottom is below that screen's, shrink it by
    the difference plus 2. KCM copied it as KeepPanelOnScreen (KCMStorySection.cpp). This file took
    only the "which screen" question from there at first and left the shrink behind (API audit,
    2026-09-25, A-1) - so a height remembered on a tall screen came back, on a shorter one or after
    the monitors were rearranged, with the panel's bottom and its resize grip out of reach.

    ***** WHY IT IS ASKED OF THE PLACE TO COME, NOT READ OFF THE VIEW AS THE PRODUCT DOES. *****
    linksui only RESIZES its panel where it stands; this file also MOVES it, and OWL applies a
    SetPalettePosition asynchronously (PaletteRefUtils.h:40-42). Measured 2026-09-25 with the product's
    shape (resize, then read GetBBox/WindowToGlobal): the view still stood at the old place - bottom
    694, on the 728 screen, so nothing was shrunk - and a moment later the dock arrived at the new
    place with its bottom at 971, well off the screen. Reading after the layout would need
    ForcePaletteSystemToPerformLayout, which the header reserves for test code ("use sparingly").
    So the rule is applied to the rectangle the panel is about to occupy, the way TitleBandIsOnScreen
    judges the top.
    The shrunk size goes through ConstrainDimensions like every size here: the Book panel rounds its
    own height and knows its own minimum. At that minimum the bottom can still be past the screen -
    the product accepts the same, and so does this. */
PMPoint FitHeightToScreen(IControlView* panelView, SysCoord panelLeft, SysCoord panelTop, const PMPoint& size)
{
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (panelView == nil || app == nil)
		return size;
	InterfacePtr<const IMonitorInfo> monInfo(app, UseDefaultIID());
	if (monInfo == nil)
		return size;

	SysRect wanted;
	::SetSysRectLTWH(wanted, panelLeft, panelTop, ::ToInt32(size.X()), ::ToInt32(size.Y()));
	const GSysRect screen = monInfo->GetBestScreenRect(wanted);
	if (SysRectBottom(screen) >= SysRectBottom(wanted))
		return size;		// on the screen - nothing to do

	const PMReal shrinkBy = PMReal(SysRectBottom(wanted) - SysRectBottom(screen) + 2);
	return panelView->ConstrainDimensions(PMPoint(size.X(), size.Y() - shrinkBy));
}

/** Put the icon state (and the icon strip's width) on a tab pane. Only switched when it differs: the
    mode belongs to the whole tab pane, so an unnecessary switch would be a flicker of every panel in
    it. The width goes first, so the strip opens at it. */
void ApplyIconState(const PaletteRef& tabPane, const Placement& p)
{
	if (!tabPane.IsValid())
		return;
	if (p.iconicWidth > 0)
		PaletteRefUtils::SetTabPanePreferredIconicWidth(tabPane, static_cast<float>(p.iconicWidth));
	const PaletteRefUtils::TabPaneMode want =
		p.iconic ? PaletteRefUtils::kIcon_TabPaneMode : PaletteRefUtils::kExpanded_TabPaneMode;
	if (PaletteRefUtils::GetTabPaneMode(tabPane) != want)
		PaletteRefUtils::SetTabPaneMode(tabPane, want);
}

/** Floating: where, how big, and collapsed or not. */
void RestoreFloating(IControlView* bookPanel, const PaletteRef& container, const Placement& p)
{
	if (!p.haveFloat)
		return;

	const PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
		return;

	if (!TitleBandIsOnScreen(p))
		return;

	// Where the panel sits inside its floating dock (the title band above it), read BEFORE anything
	// moves: the dock's position and the panel's global bounds then describe the same settled layout.
	// FitHeightToScreen needs it to know where the panel's bottom will be once the dock has moved.
	const SysPoint dockNow  = PaletteRefUtils::GetPalettePosition(dock);
	const SysRect  panelNow = ::ToSys(bookPanel->WindowToGlobal(bookPanel->GetBBox()));
	const SysCoord insetLeft = SysRectLeft(panelNow) - SysPointH(dockNow);
	const SysCoord insetTop  = SysRectTop(panelNow) - SysPointV(dockNow);

	// Where first, then how big: the panel grows from its top-left, so moving it after a resize
	// would first have grown it somewhere else.
	PaletteRefUtils::SetPalettePosition(dock, p.left, p.top);

	// ***** Through ConstrainDimensions: putting the size through it is the CALLER'S job, it is not
	// something Resize does on the way in (IControlView.h:174-176) - and it is the book panel's own
	// view that knows its limits (it rounds its height - see the file header). The product calls it
	// before resizing a floating panel too (LinksUIUtils.cpp:626-627).
	// ...and then fitted to the screen at the NEW place (see FitHeightToScreen), so a height
	// remembered on a taller screen does not leave the bottom edge where it cannot be reached.
	PMPoint size(PMReal(p.width), PMReal(p.height));
	size = bookPanel->ConstrainDimensions(size);
	size = FitHeightToScreen(bookPanel, p.left + insetLeft, p.top + insetTop, size);
	bookPanel->Resize(size);

	// And last, collapsed to icons or not (the user's request, 2026-09-25). Last because the size is
	// what the panel expands to, and it is set on the expanded panel.
	ApplyIconState(FindAncestor(container, &PaletteRefUtils::IsTabPane), p);
}

/** Is the palette a neighbour's panel sits in open - its dock, docked or floating, on show? Asked of
    a FLOATING neighbour before the book panel is put into its palette: a panel the user has closed
    can still hand back a container (ContainerOfPanel's own note), and a book panel put there would
    disappear with it. A docked neighbour is not asked this (the dock half is unchanged since it was
    measured, 2026-09-25). */
bool NeighbourPaletteIsOpen(IPanelMgr* panelMgr, int32 widgetID)
{
	const PaletteRef container = ContainerOfPanel(panelMgr, widgetID);
	if (!container.IsValid())
		return false;
	PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
		dock = FindAncestor(container, &PaletteRefUtils::IsDock);
	return dock.IsValid() && PaletteRefUtils::IsPaletteVisible(dock) != kFalse;
}

/** Back beside the same neighbours, the closest one that can still be found first: into a mate's
    tab group at the old tab place, else just above the group below, else just below the group above.
    The book panel arrives in a floating palette of its own; what is moved is its TAB (into a mate's
    group) or its TAB GROUP (into a column), which is the level each neighbour describes. One walk for
    a dock and for a shared floating palette - the tree has the same shape in both.
    @param askOpen IN whether a neighbour's palette has to be open to be used (NeighbourPaletteIsOpen).
    @return true when the book panel was moved. */
bool JoinNeighbours(IPanelMgr* panelMgr, const PaletteRef& container, int32 mate, int32 tabIndex,
	int32 nextGroupPanel, int32 prevGroupPanel, bool askOpen)
{
	const PaletteRef ownGroup = ParentOf(container);
	if (!Is(ownGroup, &PaletteRefUtils::IsTabGroup))
		return false;

	// 1. A panel it shared a tab group with: back into that group, at its tab's old place.
	//    (Measured 2026-09-25: Pages as the mate, the tab back at 0 and at 1.)
	if (mate != 0 && (!askOpen || NeighbourPaletteIsOpen(panelMgr, mate)))
	{
		const PaletteRef mateGroup = ParentOf(ContainerOfPanel(panelMgr, mate));
		if (Is(mateGroup, &PaletteRefUtils::IsTabGroup))
		{
			PaletteRefUtils::ReparentPalette(container, mateGroup, ChildAtOrEnd(mateGroup, tabIndex));
			return true;
		}
	}

	// 2. The group below it: its own group goes back in just above that one. (Measured.)
	if (nextGroupPanel != 0 && (!askOpen || NeighbourPaletteIsOpen(panelMgr, nextGroupPanel)))
	{
		const PaletteRef nextGroup = ParentOf(ContainerOfPanel(panelMgr, nextGroupPanel));
		const PaletteRef column = Is(nextGroup, &PaletteRefUtils::IsTabGroup) ? ParentOf(nextGroup) : PaletteRef();
		if (Is(column, &PaletteRefUtils::IsTabPane))
		{
			PaletteRefUtils::ReparentPalette(ownGroup, column, nextGroup);
			return true;
		}
	}

	// 3. The group above it: just below that one. (Measured.)
	if (prevGroupPanel != 0 && (!askOpen || NeighbourPaletteIsOpen(panelMgr, prevGroupPanel)))
	{
		const PaletteRef prevGroup = ParentOf(ContainerOfPanel(panelMgr, prevGroupPanel));
		const PaletteRef column = Is(prevGroup, &PaletteRefUtils::IsTabGroup) ? ParentOf(prevGroup) : PaletteRef();
		if (Is(column, &PaletteRefUtils::IsTabPane))
		{
			PaletteRefUtils::ReparentPalette(ownGroup, column, ChildAtOrEnd(column, IndexOfChild(column, prevGroup) + 1));
			return true;
		}
	}

	return false;
}

/** Docked: into the dock next to the same neighbours (JoinNeighbours), and when none can be found, a
    new column beside the column that was next to it. */
void RestoreDocked(IPanelMgr* panelMgr, const PaletteRef& container, const Placement& p)
{
	const PaletteRef ownGroup = ParentOf(container);
	if (!Is(ownGroup, &PaletteRefUtils::IsTabGroup))
		return;

	// 1-3. Beside the same neighbours - see JoinNeighbours. (0 = none, which ContainerOfPanel already
	//      answered with an invalid ref before this was a function; the explicit test is the same.)
	if (JoinNeighbours(panelMgr, container, p.mate, p.tabIndex, p.nextGroup, p.prevGroup, false))
		return;

	// 4. It had a column to itself, so there is no column left to go back into: a new one, beside
	//    the column that was after it (or before it), in the same icon state and width.
	//    ***** NOT MEASURED YET (2026-09-25): no test had a second column. *****
	PaletteRef dock;
	PaletteRef before;
	const PaletteRef nextCol = FindAncestor(ContainerOfPanel(panelMgr, p.nextColumn), &PaletteRefUtils::IsTabPane);
	if (nextCol.IsValid())
	{
		dock = ParentOf(nextCol);
		before = nextCol;
	}
	else
	{
		const PaletteRef prevCol = FindAncestor(ContainerOfPanel(panelMgr, p.prevColumn), &PaletteRefUtils::IsTabPane);
		if (prevCol.IsValid())
		{
			dock = ParentOf(prevCol);
			before = ChildAtOrEnd(dock, IndexOfChild(dock, prevCol) + 1);
		}
	}
	if (!Is(dock, &PaletteRefUtils::IsDock))
		return;		// no neighbour left to find the place by - it stays where InDesign put it

	const PaletteRef newColumn = PaletteRefUtils::NewTabPaneInDock(dock,
		p.iconic ? PaletteRefUtils::kIcon_TabPaneMode : PaletteRefUtils::kExpanded_TabPaneMode, before);
	if (!Is(newColumn, &PaletteRefUtils::IsTabPane))
		return;
	PaletteRefUtils::ReparentPalette(ownGroup, newColumn, PaletteRef());
	ApplyIconState(newColumn, p);
}

/** Put the remembered placement on the book panel that is open now. Runs from the timer. */
void RestoreNow()
{
	if (!gOn || !gRemembered.IsUsable())
		return;

	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	IControlView* first = nil;
	WalkBookPanels(panelMgr, &first);
	InterfacePtr<IControlView> bookPanel(first);	// takes over the reference WalkBookPanels added
	if (bookPanel == nil)
		return;		// closed again in the meantime

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	if (!container.IsValid())
		return;

	// InDesign put it in a dock itself: that is InDesign's own memory at work - leave it.
	if (!PaletteRefUtils::IsPaletteFloating(container))
		return;

	if (gRemembered.docked)
		RestoreDocked(panelMgr, container, gRemembered);
	else if (!JoinNeighbours(panelMgr, container, gRemembered.floatMate, gRemembered.floatTabIndex,
				gRemembered.floatNextGroup, gRemembered.floatPrevGroup, true))
		// Floating: back into the palette it shared, when that palette is open (see the file header);
		// its own place and size otherwise. Joined, the place and size are the palette's - nothing
		// more is put on it.
		RestoreFloating(bookPanel, container, gRemembered);
}

/** Timer callback. A raw function pointer, so it must never outlive this plug-in (see
    ShutdownCleanup). NOTHING is released in here - the same rule and the same reason as
    KBSBookWatch's RetireTimerCallback. */
uint32 RestoreTimerCallback(void* /*refPtr*/)
{
	RestoreNow();

	// NEVER 0: to the idle task manager 0 means "call me again immediately".
	return IIdleTask::kEndOfTime;
}

/** Arm the deferred restore (or restart its wait if one is pending). */
void ArmRestoreTimer()
{
	if (gRestoreTimer == nil)
		gRestoreTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);

	if (gRestoreTimer == nil)
	{
		// No timer to be had - do it now rather than not at all.
		RestoreNow();
		return;
	}

	gRestoreTimer->StopTimer();
	gRestoreTimer->StartTimer(RestoreTimerCallback, kKBSBookPanelRestoreDelayMs, nil);
}

/** Count the book panels afresh, and arm a restore if there were none at the last look and there are
    some now. The one place the "zero to some" question is asked. */
void RecountAndMaybeRestore()
{
	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	const int32 previous = gBookPanelCount;
	gBookPanelCount = WalkBookPanels(panelMgr, nil);

	if (previous == 0 && gBookPanelCount > 0 && gRemembered.IsUsable())
		ArmRestoreTimer();
}

//----------------------------------------------------------------------------------------
// Following: the observer's attach, the interceptor's install, the timer
//----------------------------------------------------------------------------------------

/** The observer's attach and detach. Regular attachment, the same as the "Translucent Panel"
    observer on the same subject, and undone with the same type (ISubject.h:288). */
void AttachObserver(bool attach)
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKBSBOOKPANELOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	if (panelMgr == nil)
		return;

	InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
	if (subject == nil)
		return;

	const bool attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSBOOKPANELOBSERVER) != kFalse;
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSBOOKPANELOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSBOOKPANELOBSERVER);
}

/** Drop a pending restore and give the timer back. The ONE place it is released. */
void DisarmRestoreTimer()
{
	if (gRestoreTimer == nil)
		return;
	gRestoreTimer->StopTimer();
	gRestoreTimer->Release();
	gRestoreTimer = nil;
}

ICommandProcessor* QueryCommandProcessor()
{
	ISession* session = GetExecutionContextSession();
	return (session != nil) ? session->QueryCommandProcessor() : nil;
}

/** Put the interceptor in place if it is not there already. The ONE place that installs - the
    interface's own InstallSelf is left empty, as KIDMCP's is: two ways to install one thing is how a
    pointer gets left behind in the command processor.
    ***** ONLY WHILE THE TOGGLE IS ON (API audit 2026-09-25, A-3). ***** An interceptor sees every
    command InDesign processes, and its header asks third-party code to use it "with extreme caution"
    (ICommandInterceptor.h:37-40). Until that audit it went in at PaletteMgrStarted whatever the
    toggle said - so every user of KBS, the feature being OFF by default, had every command pass
    through it for the whole session. Now Start installs it only when the toggle is ON and
    ToggleAndSave puts it in and takes it out with the tick. (KIDMCP keeps its own resident, and that
    is right there: recording commands is what that tool is for.) */
void InstallCmdWatch()
{
	if (gCmdWatch != nil)
		return;

	InterfacePtr<ICommandProcessor> processor(QueryCommandProcessor());
	if (processor == nil)
		return;

	InterfacePtr<ICommandInterceptor> watch(
		static_cast<ICommandInterceptor*>(::CreateObject(kKBSBookPanelCmdWatchBoss, IID_ICOMMANDINTERCEPTOR)));
	if (watch == nil)
		return;

	// The reference is handed over, not shared: the processor stores a raw pointer, so this file
	// keeps the object alive on its behalf until UninstallCmdWatch takes it back.
	watch->AddRef();
	gCmdWatch = watch;
	processor->InstallInterceptor(watch);
}

/** Take the interceptor back out and let it go. Safe to call twice. */
void UninstallCmdWatch()
{
	if (gCmdWatch == nil)
		return;

	InterfacePtr<ICommandProcessor> processor(QueryCommandProcessor());
	if (processor != nil)
		processor->DeinstallInterceptor(gCmdWatch);

	gCmdWatch->Release();
	gCmdWatch = nil;
}

/** A book is about to close (kCloseBookCmdBoss, before it runs): its Book panel is still standing,
    so this is the last moment it can be measured (see the file header).
    ***** And the count has to be told. ***** Nothing will announce that the panel has gone, so when
    the book about to close is the last one open, the count is put to zero here - otherwise the next
    book to open would not look like "none, then one" and would never be put back.
    *With several books open their panels are normally tabs of ONE palette, so the first book panel
    found stands for all of them. A book panel dragged out into a palette of its own is not told
    apart - the first one found is what is measured. */
void OnBookAboutToClose()
{
	if (!gOn)
		return;

	Placement now;
	if (MeasureOpenBookPanel(now))
		RememberAndWrite(now, true);

	// Still counted: the command has not run yet, so the closing book is one of these.
	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil || bookMgr->GetBookCount() <= 1)
		gBookPanelCount = 0;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KBSBookPanelPlacement
//----------------------------------------------------------------------------------------

bool KBSBookPanelPlacement::IsOn()
{
	return gOn;
}

void KBSBookPanelPlacement::ToggleAndSave(PMString& outStatus)
{
	gOn = !gOn;

	// A fresh count, so the next look compares against what is open NOW. Without it a count left
	// from before the toggle was switched off would make an already-open book panel look new, and
	// the first notification after ticking the box would move it.
	if (gOn)
	{
		InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
		gBookPanelCount = WalkBookPanels(panelMgr, nil);
	}

	// The interceptor follows the tick (see InstallCmdWatch). This runs from the menu action, never from
	// inside the interceptor's own InterceptProcessCommand, so UninstallCmdWatch never releases the
	// object while one of its own calls is on the stack.
	if (gOn)
		InstallCmdWatch();
	else
		UninstallCmdWatch();

	// The toggle's key, and nothing else (the user's rule, 2026-09-25).
	std::vector<std::pair<std::string, std::string> > keys;
	keys.push_back(std::make_pair(std::string(kKeyToggle), BoolValue(gOn)));
	const char* failure = KBSPanelStateWriteKeys(keys);

	// What was set, then WHERE it was written - the full path on a line of its own, the way "Save
	// Panel Settings" shows it (the user's call, 2026-09-25), so the file can be found, backed up or
	// deleted. The first line stays because, unlike Save Panel Settings, this command also CHANGES
	// something, and the status line is where it says which way it went.
	outStatus = gOn ? "Remember book panel placement: on" : "Remember book panel placement: off";
	if (failure == nil)
	{
		PMString path;
		if (KBSPanelStateFilePath(path))
		{
			outStatus.Append("\n");
			outStatus.Append(path);
		}
	}
	else
	{
		outStatus.Append(" - but it could not be saved (");
		outStatus.Append(failure);
		outStatus.Append(").");
	}
	outStatus.SetTranslatable(kFalse);
}

void KBSBookPanelPlacement::AppendSaveKeys(std::vector<std::pair<std::string, std::string> >& keys)
{
	keys.push_back(std::make_pair(std::string(kKeyToggle), BoolValue(gOn)));

	// An explicit save is a snapshot of the screen: the book panel as it stands now, if one is open.
	Placement now;
	if (MeasureOpenBookPanel(now))
		gRemembered = now;
	if (gRemembered.IsUsable())
		AppendPlacementKeys(gRemembered, keys);
}

void KBSBookPanelPlacement::LoadFromSettings(const std::string& text)
{
	gOn = KBSPanelStateReadBool(text, kKeyToggle, gOn);

	Placement p;

	// Floating: all four numbers, or none - a placement with its height missing is not a place to put
	// anything back to.
	int32 left = 0, top = 0, width = 0, height = 0;
	if (KBSPanelStateReadInt(text, kKeyLeft, left) && KBSPanelStateReadInt(text, kKeyTop, top) &&
		KBSPanelStateReadInt(text, kKeyWidth, width) && KBSPanelStateReadInt(text, kKeyHeight, height) &&
		width > 0 && height > 0)
	{
		p.haveFloat = true;
		p.left = left;
		p.top = top;
		p.width = width;
		p.height = height;

		// Its neighbours in a shared palette; a file written before they were kept has none (0).
		KBSPanelStateReadInt(text, kKeyFloatMate,      p.floatMate);
		KBSPanelStateReadInt(text, kKeyFloatTabIndex,  p.floatTabIndex);
		KBSPanelStateReadInt(text, kKeyFloatNextGroup, p.floatNextGroup);
		KBSPanelStateReadInt(text, kKeyFloatPrevGroup, p.floatPrevGroup);
	}

	// Docked: the neighbours (a missing one reads as 0 = none). A file written before docking was
	// remembered has no "bookPanelDocked" and is floating.
	p.docked = KBSPanelStateReadBool(text, kKeyDocked, false);
	if (p.docked)
	{
		KBSPanelStateReadInt(text, kKeyMate,       p.mate);
		KBSPanelStateReadInt(text, kKeyTabIndex,   p.tabIndex);
		KBSPanelStateReadInt(text, kKeyNextGroup,  p.nextGroup);
		KBSPanelStateReadInt(text, kKeyPrevGroup,  p.prevGroup);
		KBSPanelStateReadInt(text, kKeyNextColumn, p.nextColumn);
		KBSPanelStateReadInt(text, kKeyPrevColumn, p.prevColumn);
	}

	p.iconic = KBSPanelStateReadBool(text, kKeyIconic, false);
	KBSPanelStateReadInt(text, kKeyIconicWidth, p.iconicWidth);

	if (p.IsUsable())
		gRemembered = p;
}

void KBSBookPanelPlacement::Start()
{
	// The saved settings are read by the startup service - but that one is LAZY
	// (kLazyStartupShutdownProviderImpl), and nothing promises it runs before the palette manager
	// has laid out. Reading here as well costs nothing: the read is guarded to happen once.
	KBSLoadPanelStateIfPresent();

	AttachObserver(true);
	// Only while ON (see InstallCmdWatch) - ToggleAndSave puts it in when the box is ticked later.
	if (gOn)
		InstallCmdWatch();

	// Whatever book panel is open by now has "just appeared" as far as this feature is concerned -
	// before this call nothing was following - so the count starts from zero. (Measured on 21.0.2:
	// InDesign does NOT reopen books at launch, so in practice there is none; counting from zero is
	// right either way.)
	gBookPanelCount = 0;
	if (gOn)
		RecountAndMaybeRestore();
}

void KBSBookPanelPlacement::Stop()
{
	// ***** NOT where a quit's placement is written. ***** Measured on 21.0.2: a quit closes the books
	// BEFORE this is called, so the interceptor has already measured the Book panel and this finds
	// none. Kept as a backstop for a build that closes them later - it does nothing when there is no
	// Book panel. Nothing is said on failure: InDesign is quitting.
	if (gOn)
	{
		Placement now;
		if (MeasureOpenBookPanel(now))
			RememberAndWrite(now, false);
	}

	AttachObserver(false);
	UninstallCmdWatch();
	DisarmRestoreTimer();
}

void KBSBookPanelPlacement::ShutdownCleanup()
{
	AttachObserver(false);
	// The interceptor above all: the command processor holds a raw pointer into this .pln.
	UninstallCmdWatch();
	DisarmRestoreTimer();
}

//----------------------------------------------------------------------------------------
// The observer
//----------------------------------------------------------------------------------------

/** Follows the panel manager, and puts the placement back when a book panel appears where there was
    none. (It also listened for kAboutToClosePaletteMsg at first, to measure a closing book panel;
    that message never came for one - see the file header - and the interceptor does that job.) */
class KBSBookPanelObserver : public CObserver
{
public:
	KBSBookPanelObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSBOOKPANELOBSERVER) {}
	virtual ~KBSBookPanelObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KBSBookPanelObserver, kKBSBookPanelObserverImpl)

void KBSBookPanelObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	if (protocol != IID_IPANELMGR || theChange != kPaletteVisibilityChangedMessage)
		return;

	// Nothing while OFF: the visibility message arrives several times over merely opening a document
	// (measured - five times while one book opened), and people not using this should not pay for a
	// walk of the panel list each time.
	if (!gOn)
		return;

	RecountAndMaybeRestore();
}

//----------------------------------------------------------------------------------------
// The command interceptor
//----------------------------------------------------------------------------------------

/** Sees kCloseBookCmdBoss before it runs. "Use with extreme caution from third-party client code"
    (ICommandInterceptor.h:40), and the caution taken is KIDMCP's (KIDMCPCmdWatch.cpp):
    ***** kCmdNotHandled ON EVERY PATH. ***** This class is allowed to cancel any command in InDesign
    and never does: kCmdNotHandled is "pass it on", which is what an interceptor that is not there
    would produce. Everything is inside try/catch - an exception leaving here lands in the middle of
    InDesign's command processing. And the test that runs for every command is one flag and one class
    comparison. At file scope, not in the anonymous namespace, for the reason the other
    implementations here are.

    ***** WHY NOT THE OBSERVER KBS ALREADY HAS (API audit 2026-09-25, A-2 - measured). ***** KBSBookWatch
    hears the same close as kCloseBookCmdBoss @ kSessionBoss (IID_IBOOKCONTENT), and the official way
    to see a command before it is done is an observer that finds GetCommandState() == kNotDone
    (persistentlist/PstLstDocObserver.cpp:189-195). That notification does arrive as kNotDone - but
    kNotDone is "before OR WHILE the command is done" (ICommand.h:119), and it comes WHILE: in the log
    of all three closes (script, the Book panel's Close Book, quitting) the order was this class's
    kCloseBookCmdBoss, then kDestroyPanelCmdBoss, then KBSBookWatch - with no book panel left to
    measure by then. So the interceptor stays, the only door there is; A-3 (installed only while ON)
    and the main-thread gate below are what the audit asked of it instead.

    ***** MAIN THREAD ONLY. ***** An interceptor is handed commands from whichever thread processes
    them - KIDMCP's tells the two sides apart for that reason (KIDMCPCmdWatch.cpp, IsMainThreadDomain)
    - and what this one does on a book close is UI work (the panel manager, the palette tree). A book
    close off the main thread has never been seen here, and nothing says one cannot happen; the gate
    is one call per book close, and it keeps this from ever doing UI work on another thread. */
class KBSBookPanelCmdWatch : public CPMUnknown<ICommandInterceptor>
{
public:
	KBSBookPanelCmdWatch(IPMUnknown* boss) : CPMUnknown<ICommandInterceptor>(boss) {}
	virtual ~KBSBookPanelCmdWatch() {}

	virtual InterceptResult InterceptProcessCommand(ICommand* cmd)
	{
		try
		{
			if (gOn && cmd != nil && ::GetClass(cmd) == kCloseBookCmdBoss && IDThreading::IsMainThreadDomain())
				OnBookAboutToClose();
		}
		catch (...)
		{
		}
		return kCmdNotHandled;
	}

	// A scheduled command comes back through InterceptProcessCommand when it is processed, so it is
	// looked at there and only there (KIDMCP's reading of the same pair).
	virtual InterceptResult InterceptScheduleCommand(ICommand* /*cmd*/)	{ return kCmdNotHandled; }

	// The two remaining entry points are marked deprecated / "will eventually go away" in the header;
	// both are pure virtual, so they exist, and neither is a place to put behaviour.
	virtual InterceptResult InterceptExecuteDynamic(ICommand* /*cmd*/)		{ return kCmdNotHandled; }
	virtual InterceptResult InterceptExecuteImmediate(ICommand* /*cmd*/)	{ return kCmdNotHandled; }

	// Deliberately empty: InstallCmdWatch is the one way in (see there).
	virtual void InstallSelf()		{}
	virtual void DeinstallSelf()	{}
};

CREATE_PMINTERFACE(KBSBookPanelCmdWatch, kKBSBookPanelCmdWatchImpl)

//----------------------------------------------------------------------------------------
// The palette manager service
//----------------------------------------------------------------------------------------

/** Registers the boss for kPaletteMgrService, the service the Book panel's own start-up hangs off. */
class KBSBookPanelServiceProvider : public CServiceProvider
{
public:
	KBSBookPanelServiceProvider(IPMUnknown* boss) : CServiceProvider(boss) {}
	virtual ~KBSBookPanelServiceProvider() {}

	virtual ServiceID GetServiceID() { return kPaletteMgrService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	// Per session and main thread only: what the product's own provider for this service is
	// registered as (Service_Registry_Memory_Dump.txt:2761, kBookPanelStartupShutdownBoss).
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// SetCString, not SetKey: an internal name that never reaches the UI (KBSDrawEventSrvc's reason).
	virtual void GetName(PMString* pName) { pName->SetCString("KBSBookPanelService\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KBSBookPanelServiceProvider, kKBSBookPanelServiceProviderImpl)

/** The two moments: the palettes are laid out, and the palettes are about to close. */
class KBSBookPanelPaletteMgrService : public CPMUnknown<IPaletteMgrService>
{
public:
	KBSBookPanelPaletteMgrService(IPMUnknown* boss) : CPMUnknown<IPaletteMgrService>(boss) {}
	virtual ~KBSBookPanelPaletteMgrService() {}

	virtual void PaletteMgrStarted()		{ KBSBookPanelPlacement::Start(); }
	virtual void PaletteMgrAboutToShutdown()	{ KBSBookPanelPlacement::Stop(); }
};

CREATE_PMINTERFACE(KBSBookPanelPaletteMgrService, kKBSBookPanelPaletteMgrServiceImpl)

// End, KBSBookPanelPlacement.cpp.
