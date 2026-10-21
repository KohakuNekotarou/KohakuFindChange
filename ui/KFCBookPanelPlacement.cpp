//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "Remember Book Panel Placement". What it does and what it leaves alone is in
//  KFCBookPanelPlacement.h. This file holds the four pieces that make it run:
//
//    1. a COMMAND INTERCEPTOR that sees kCloseBookCmdBoss BEFORE it runs, and measures the Book
//       panel while it still stands. It is the only moment there is. Measured on 21.0.2 (a temporary
//       log): closing a book - by script, by the Book panel's own "Close Book", and by quitting
//       InDesign alike - destroys its panel (kDestroyPanelCmdBoss, which runs AFTER kCloseBookCmdBoss)
//       without one notification on the panel manager's subject: neither kAboutToClosePaletteMsg nor
//       kPaletteVisibilityChangedMessage arrives. And a quit closes the books BEFORE
//       IPaletteMgrService::PaletteMgrAboutToShutdown - by then there is no Book panel left to measure.
//       Listening for kAboutToClosePaletteMsg and measuring at PaletteMgrAboutToShutdown records
//       nothing (the user's own test of that shape: "closed and reopened InDesign, it did not come
//       back").
//
//    2. the OBSERVER, AddIn'd onto kActiveContextBoss and attached to the panel manager's subject
//       (IID_IPANELMGR) - the same subject and arrangement as the "Translucent Panel" observer in
//       KFCPanelAlpha.cpp. It acts on kPaletteVisibilityChangedMessage only: a book OPENING does
//       announce itself there, after its panel is built (measured in the same log).
//
//    3. the PALETTE MANAGER SERVICE (IPaletteMgrService, kPaletteMgrService) - what the Book panel
//       ITSELF hangs off (kBookPanelStartupShutdownBoss is the product's only implementation,
//       Service_Registry_Memory_Dump.txt:2761). Its PaletteMgrStarted is where the observer and -
//       only while the toggle is ON - the interceptor go in (measured: it IS called for a third-party
//       provider; the toggle itself puts the interceptor in and out after that), because the startup
//       service is too early for the panel manager (KFCPanelAlpha.cpp says why); PaletteMgrAboutTo-
//       Shutdown is where they come out.
//
//    4. a one-shot ICallbackTimer that puts the placement back a moment after the Book panel
//       appears, not inside the notification: OWL lays palettes out asynchronously
//       (PaletteRefUtils.h:40-41). 100ms was enough on the machine it was measured on. (It also puts
//       it back again, later, when InDesign throws the panel out of the dock -
//       kKFCBookPanelReRestoreDelayMs - and a second timer looks at the Home screen - gHomeTimer.)
//
//  FLOATING: THE DOCK AND THE PANEL ARE MEASURED SEPARATELY, AND ON PURPOSE.
//    Position = the FLOATING DOCK's top-left (GetPalettePosition). SetPalettePosition only takes a
//      floating Dock, Toolbar or ControlBar (PaletteRefUtils.h:361-366), so the palette tree is
//      walked up from the panel's container to that dock - a walk KCM once used to move its own
//      panel to the cursor. Measured on 21.0.2: the
//      Book panel's container is three steps below its floating dock (tab group, tab pane, dock).
//    Size = the book PANEL's frame. A floating panel is resized by resizing the panel view - that is
//      what the product does (LinksUIUtils.cpp:650-653, :720-723); SetPaletteSize is its route for a
//      DOCKED palette only. Measured: moving the dock window itself with SetWindowPos to
//      360x420 left the panel inside at 302x224 - the size has to go through OWL, not the window.
//      *The Book panel rounds its own height: asked for 380x420, it came back 380x400
//       (ConstrainDimensions). A size the user dragged to already obeys that rule, so it round-trips.
//      *A size whose bottom would land below the screen at the new place is shortened before it is
//       put on (FitHeightToScreen - the product's ForceBottomOfPanelOnMonitor, LinksUIUtils.cpp:613-629,
//       asked of the place to come because OWL moves the dock asynchronously).
//    Collapsed to icons = the TAB PANE's mode, and the width of the icon strip is the tab pane's
//      "preferred iconic width" (PaletteRefUtils.h:300-312) - both put back last, on a panel that has
//      already been moved and sized expanded. (Confirmed working by the user.)
//    A FLOATING PALETTE CAN BE SHARED, AND THEN THE PLACE IS NOT THE ANSWER. The user tabbed the Book
//      panel into the floating KFC panel, closed InDesign and opened a book: with only the dock's
//      position kept, the Book panel came back as a palette of its own, laid exactly on top of KFC's
//      (KIDMCP's panels listing showed the two in different floating docks at one rectangle). The
//      dock's position alone is not enough, and a new book panel always arrives in a palette of its own. So a
//      floating book panel's neighbours are measured as a docked one's are - the panel in its tab
//      group with its tab's place, the nearest panels in the groups above and below - and put back
//      first; the place and size are what is used when no neighbour's palette is open.
//
//  DOCKED: KEPT AS NEIGHBOURS, PUT BACK WITH ReparentPalette. A dock is a list of columns
//    (tab panes), each a list of tab groups, each a list of tabs (PaletteRef.h:96-101), so a place in
//    it is "next to these", not an x and a y. What is recorded is the WidgetID of a panel that is NOT
//    a book panel (a book panel's WidgetID is made up per book at run time) in: the same tab group,
//    the nearest group below and above, and the nearest column after and before. Putting it back
//    tries them in that order - the closer the neighbour, the more exact the place.
//    Measured (the user's tests, read back from a temporary log): into the mate's group at tab 0 and
//    at tab 1, above the next group, below the previous one - each came back where it had been, and
//    the floating palette it was taken out of went away with it. A new column (NewTabPaneInDock) was
//    measured alone in the dock and beside the toolbox (which it does not take as "before" -
//    RestoreDocked), and beside another column by the user.
//    ! Seen twice (the user): the Book panel's icon drawn as KFC's own. KFC writes
//      no palette icon anywhere, so the suspicion is OWL's icon strip after a reparent - not confirmed,
//      and left there with the author's agreement (it is only how the icon looks).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActiveContext.h"		// where the observer implementation lives (kActiveContextBoss)
#include "IApplication.h"		// the application boss - IMonitorInfo sits on it
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
#include "DocumentID.h"			// kOpenDocCmdBoss, kNewDocCmdBoss - a document opening ends the launch
#include "CObserver.h"
#include "CPMUnknown.h"
#include "CreateObject.h"		// ::CreateObject2
#include "CServiceProvider.h"
#include "IDThreadingPrimitives.h"	// IsMainThreadDomain - the interceptor's gate (see KFCBookPanelCmdWatch)
#include "IDocumentList.h"		// GetDocCount - the Home screen only matters while no document is open
#include "IExtendScriptUtils.h"	// RunScriptInEngine - app.homeScreenVisible has no C++ door
#include "PaletteRef.h"
#include "PaletteRefUtils.h"	// the palette tree: walk it, measure it, move things about in it
#include "ShuksanID.h"			// kCallbackTimerBoss, IID_ICALLBACKTIMER
#include "WorkspaceID.h"		// kPaletteMgrService, IID_IPALETTEMGRSERVICE

#include <chrono>				// steady_clock - the Home screen is looked at only around launch

// Project includes:
#include "KFCUIID.h"
#include "KFCBookPanelPlacement.h"
#include "KFCBookPanelLookup.h"	// IsBookPanel - the one place that decides what a book panel is
#include "KFCPanelState.h"		// KFCPanelStateWriteKeys and the readers - the settings file, key by key
#include "KFCResultTree.h"		// ShowStatus - a write that failed is said, not swallowed
#include "KFCDiag.h"			// KFC_DIAG_LOG - a test build's trace of what was measured and put back
#include "KFCPanelAlpha.h"		// KFCDetachPanelVisibilityObserver - stops at the same palette manager moment

namespace
{

/** Where the Book panel was. One of the two halves is in force (docked says which); the other is kept
    as it last was, so a panel that goes from floating to docked and back comes back to its old
    floating place. */
struct Placement
{
	// Floating.
	bool	haveFloat = false;
	int32	left = 0;		// the floating dock's top-left, global coordinates
	int32	top = 0;
	int32	width = 0;		// the book PANEL's size (not the dock's, which adds the title band)
	int32	height = 0;

	// Floating in a palette SHARED with other panels: WidgetIDs of the nearest non-book panels in
	// that palette, 0 = none (alone in its palette). Measured with the floating half, and tried before
	// the place when it is put back (see the file header).
	int32	floatMate = 0;		// a panel in the same tab group ...
	int32	floatTabIndex = 0;	// ... and the book panel's tab position in that group
	int32	floatNextGroup = 0;	// the nearest panel in a tab group below, in the same palette
	int32	floatPrevGroup = 0;	// ... and above

	// Docked: WidgetIDs of the nearest non-book panels, 0 = none.
	bool	docked = false;
	int32	mate = 0;		// a panel in the same tab group ...
	int32	tabIndex = 0;	// ... and the book panel's tab position in that group
	int32	nextGroup = 0;	// the nearest panel in a tab group below, in the same column
	int32	prevGroup = 0;	// ... and above
	int32	nextColumn = 0;	// the nearest panel in a column after, in the same dock
	int32	prevColumn = 0;	// ... and before
	// AND THE DOCK ITSELF. A book panel alone in its column has no neighbour at
	// all - and its column goes with the book when it closes - so neighbours alone cannot find the place
	// again (the user: "alone in the dock, it comes back undocked"). Which dock (PaletteRefUtils::
	// DockLocation, 0 = not known) and which column in it, counted from the dock's first.
	int32	dockSide = 0;
	int32	columnIndex = 0;

	// Both: the tab pane's (floating palette's, or dock column's) icon state and icon-strip width.
	bool	iconic = false;
	int32	iconicWidth = 0;	// 0 = not known

	bool IsUsable() const { return docked || haveFloat; }
};

/** The toggle (session flag). */
bool gOn = false;

/** The last placement known this session - from the file at startup, or from a close. */
Placement gRemembered;

/** How many book panels there were at the last look. A restore is due when this goes from zero to
    anything, and ONLY then - a book opened beside another joins that palette as a tab.
    Nothing announces the drop to zero. A closing book says nothing to the panel manager
    (see the file header), so OnBookAboutToClose puts this to zero itself when the book about to
    close is the last one. Counted only while the toggle is ON; switching it on and Start() take a
    fresh count, so a stale number cannot make an already-open panel look new. */
int32 gBookPanelCount = 0;

/** The deferred restore. ONE object for the life of the plug-in, released only by
    GiveBackTimers - never from inside its own callback (KFCBookWatch.cpp explains why). */
ICallbackTimer* gRestoreTimer = nil;

/** How long after the book panel appears the placement is put back. A beat, so OWL's own layout of
    the new palette (asynchronous - PaletteRefUtils.h:40-41) has gone round first. */
const uint32 kKFCBookPanelRestoreDelayMs = 100;

/** INDESIGN CAN THROW IT OUT OF THE DOCK AGAIN (measured). A book opened with
    InDesign itself (a .indb double-clicked while InDesign is not running): the restore docks it, and
    a few seconds later - while InDesign is still settling its workspace, the Home screen up - InDesign
    takes the book panel out of the dock again and leaves it in a floating palette that is HIDDEN. The
    book is open and its panel nowhere to be seen (3 launches in 4; the other one kept it). The
    visibility message arrives as that happens, so the restore is simply done again once the messages
    have stopped: a longer wait than the first (the messages came up to 0.9 s apart over 1.5 s), and a
    few times at most per opening, so KFC never fights InDesign for good. */
const uint32 kKFCBookPanelReRestoreDelayMs = 1500;
const int32 kKFCBookPanelMaxReRestores = 3;
int32 gReRestores = 0;		// done since the book panel last appeared (RecountAndMaybeRestore resets it)

/** Has this opening's first restore run (RecountAndMaybeRestore puts it back to false)? Every restore after it is a
    RE-restore, armed because the panel looked thrown out of the dock, and it puts the panel back only when it still is
    (RestoreNow). The visibility messages come BEFORE OWL's asynchronous layout has done what they announce, so a panel
    the user floated reads "in a hidden floating dock" for a moment while the palettes come back on show after
    Show/Hide All Palettes - the Tab key (measured: armed three times as they came back, and the floated panel put back
    in the dock - work/note-scripts/2026-10-21-kfc-block16-defect/live_b16d.ps1, scene V). Only the look when the timer
    runs tells that moment from InDesign's throw-out, which stays. */
bool gRestoredThisOpening = false;

/** AN OPENING THAT COULD NOT PUT THE PANEL BACK RECORDS NOTHING while the panel still stands where InDesign put it
    (the author's call). The restore can leave it there - a dock the user has emptied by closing its panels
    (RestoreDocked), a remembered place whose title band would land off every screen (RestoreFloating) - and the close
    then measured InDesign's default floating place and wrote it over the placement remembered, which the next opening
    could no longer try. So the place is measured as the restore leaves it (RestoreNow), and the close keeps the
    remembered placement while the panel is still there (RecordPlacement); moved by the user since, the new place is
    recorded as ever. Put back to false at each opening (RecountAndMaybeRestore). */
bool gLeftUnplaced = false;
Placement gWhereInDesignPutIt;

/** THE HOME SCREEN HIDES THE DOCKS (measured). A book opened with InDesign
    itself (a .indb double-clicked while InDesign is not running) opens BEFORE InDesign puts up its
    Home screen, and the Home screen then stays - with every dock hidden under it, the book panel put
    back into one among them. Opened from a running InDesign, the same book takes the Home screen
    away (app.homeScreenVisible true -> false, the workspace reloaded, the docks shown). No command
    does it (kLoadUserWorkspaceCmdBoss is its result - app.applyWorkspace alone leaves the Home screen
    up); the opening of a book does. So when the placement is docked and the Home screen stands over
    an open book, KFC closes the books and opens them again, as the user would - once a session, and
    only with no document open and no book modified. (The author's call: "the docks visible, as when
    the book is opened from InDesign, is best".)
    ONLY AROUND LAUNCH (measured). The Home screen also comes back in the middle of a session: with a
    book open, closing the last document puts it up again. And the checks left over from an opening
    (one opening used 2 of the 6) do not stay unused - the Home screen's own switch raises visibility
    messages that recount the book panels "none, then one" and arm fresh ones - so without this limit
    KFC closed the user's book and opened it again, and its panel came back floating at InDesign's
    default place (a test build's trace). The case this was written for is a book opened WITH InDesign,
    so it is looked at only within kKFCHomeWindowMs of PaletteMgrStarted, and not at all once a document
    has been seen open (the session is past its launch then). */
ICallbackTimer* gHomeTimer = nil;
const uint32 kKFCHomeCheckDelayMs = 1500;
const int32 kKFCMaxHomeChecks = 6;	// per opening - the visibility messages come in bursts
int32 gHomeChecks = 0;
bool gLeftHomeScreen = false;	// once a session
const std::chrono::milliseconds kKFCHomeWindowMs(60000);	// from PaletteMgrStarted (Start)
std::chrono::steady_clock::time_point gStartedAt;
bool gHomeWindowOver = false;	// past launch: the window has run out, or a document has been seen open

/** How much of the title band has to be on a screen for a remembered placement to be used: enough
    to take hold of it and drag it back. */
const SysCoord kKFCBookPanelMinOnScreen = 40;

/** Every key this feature writes. Named here and nowhere else (KFCPanelState asks this file): the four
    written and read one by one, and each half's whole numbers in the lists below them. */
const char* const kKeyToggle      = "rememberBookPanelPlacement";
const char* const kKeyDocked      = "bookPanelDocked";
const char* const kKeyIconic      = "bookPanelIconic";
const char* const kKeyIconicWidth = "bookPanelIconicWidth";

/** Each half's whole numbers, each with the field it is kept in - listed ONCE, and walked by the write
    (AppendPlacementKeys), the read (LoadFromSettings) and the comparison (SamePlace) alike. A key is added
    to a half here and nowhere else (the docked half, the floating neighbours and the dock itself all came
    after the first version): with a list in each of them, a key can reach one side only - written and never
    read back, or the other way round. Templates over the placement, so the write walks a const one and the
    read the one it fills.
    The floating half's place and size - read back all four or none (LoadFromSettings): */
template <class P, class Visit>
void ForEachFloatPlaceKey(P& p, Visit visit)
{
	visit("bookPanelLeft",   p.left);
	visit("bookPanelTop",    p.top);
	visit("bookPanelWidth",  p.width);
	visit("bookPanelHeight", p.height);
}

/** ...its neighbours in a shared palette - written as zeros too, so a palette of its own CLEARS a
    neighbour an earlier close left, and read as 0 when missing (a file from before they were kept): */
template <class P, class Visit>
void ForEachFloatNeighbourKey(P& p, Visit visit)
{
	visit("bookPanelFloatMate",      p.floatMate);
	visit("bookPanelFloatTabIndex",  p.floatTabIndex);
	visit("bookPanelFloatNextGroup", p.floatNextGroup);
	visit("bookPanelFloatPrevGroup", p.floatPrevGroup);
}

/** ...and the docked half - a missing one reads as 0 = none (the dock's side is 0 in a file from an older
    build, which did not say which dock). */
template <class P, class Visit>
void ForEachDockKey(P& p, Visit visit)
{
	visit("bookPanelMate",        p.mate);
	visit("bookPanelTabIndex",    p.tabIndex);
	visit("bookPanelNextGroup",   p.nextGroup);
	visit("bookPanelPrevGroup",   p.prevGroup);
	visit("bookPanelNextColumn",  p.nextColumn);
	visit("bookPanelPrevColumn",  p.prevColumn);
	visit("bookPanelDockSide",    p.dockSide);
	visit("bookPanelColumnIndex", p.columnIndex);
}

/** The installed command interceptor, or nil. Held because the processor keeps a RAW pointer:
    releasing it while installed would leave InDesign calling into freed memory (KIDMCPCmdWatch.cpp
    says the same of its own). */
ICommandInterceptor* gCmdWatch = nil;

/** **Once ShutdownCleanup has run, nothing is installed or booked again - not the interceptor, not either timer.
    Each leaves InDesign holding a raw pointer into this .pln, and nothing promises that no menu press
    (ToggleAndSave) or notification comes between the clean-up and the unload: stopping them is half of it,
    never starting them again the other half (memory plugin-teardown-robustness). */
bool gShutDown = false;

//----------------------------------------------------------------------------------------
// The palette tree
//----------------------------------------------------------------------------------------

// (The panel manager is asked through KFCBookPanelLookup::QueryPanelManager, beside the walk of the book
//  panels it starts.)

/** How many book panels there are, and (when asked) the first of them, AddRef'd - through the one walk
    of them (KFCBookPanelLookup::ForEachBookPanel). */
int32 WalkBookPanels(IPanelMgr* panelMgr, IControlView** outFirst)
{
	if (outFirst != nil)
		*outFirst = nil;

	int32 count = 0;
	KFCBookPanelLookup::ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID&) -> bool
	{
		// Only a panel that has a palette counts: nothing PROMISES the list is pruned, and a stale entry would
		// keep the count at one. (Measured: a closed book's panel is off the list by the next look - but still on
		// it while the close runs, out of its palette: CountPlacedBookPanels.)
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid())
			return false;

		++count;
		if (outFirst != nil && *outFirst == nil)
		{
			panelView->AddRef();
			*outFirst = panelView;
		}
		return false;
	});
	return count;
}

/** The first book panel, AddRef'd (the caller takes the reference over) - or nil when there is none. */
IControlView* QueryFirstBookPanel(IPanelMgr* panelMgr)
{
	IControlView* first = nil;
	WalkBookPanels(panelMgr, &first);
	return first;
}

/** Never hand PaletteRefUtils an invalid PaletteRef. Nothing in PaletteRefUtils.h says
    what its functions do with one, and a dock that is not the shape this file expects - or a
    neighbour whose palette cannot be found - would otherwise walk an invalid ref straight into
    GetParentOfPalette / GetChildCountOfPalette. These two are the only doors to the tree the dock
    half uses: an invalid ref in, an invalid ref (or "no") out. */
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

/** HOW MANY BOOK PANELS STAND IN A DOCK OR A FLOATING PALETTE - what a book panel APPEARS by, from none
    (RecountAndMaybeRestore). A dying one is still on the list (measured - live_b16d.ps1, scene J2): the book panel was
    the front tab of Pages' tab group when its book closed, and the close raised a visibility message (Pages coming to
    the front) with that panel still listed, its container already out of every palette - no parent above it. Counted,
    it read "none, then one" with no book open: the count stood at one, the next opening was never put back, and its
    close wrote InDesign's default place over the one remembered. Only "from none" asks this. A panel KFC moves is out
    of every palette for a moment too (the message comes inside ReparentPalette): counted out there, the count would go
    to none and back, and arm a restore in the middle of one - so once a panel is there, the count is
    WalkBookPanels'. */
int32 CountPlacedBookPanels(IPanelMgr* panelMgr)
{
	int32 placed = 0;
	KFCBookPanelLookup::ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID&) -> bool
	{
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (FindAncestor(container, &PaletteRefUtils::IsDock).IsValid() || FindFloatingDock(container).IsValid())
			++placed;
		return false;
	});
	return placed;
}

/** Is the book panel where InDesign leaves it when it throws it out of the dock (see
    kKFCBookPanelReRestoreDelayMs) - floating, in a floating dock that is not on show? One question for its
    three askers: Measure records no placement there, MaybeRestoreAgain arms a restore on it, and RestoreNow
    counts that restore on it - the cap on re-restores holds only while those two ask the same thing. */
bool ThrownOutOfTheDock(const PaletteRef& container)
{
	if (!container.IsValid() || !PaletteRefUtils::IsPaletteFloating(container))
		return false;
	const PaletteRef dock = FindFloatingDock(container);
	return dock.IsValid() && !PaletteRefUtils::IsPaletteVisible(dock);
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

/** Does this palette already stand under this parent, just before this sibling (an invalid one = at
    the end) - or is the sibling it is to go before the palette itself? */
bool StandsThere(const PaletteRef& which, const PaletteRef& parent, const PaletteRef& before)
{
	if (!which.IsValid() || ParentOf(which) != parent)
		return false;
	const int32 at = IndexOfChild(parent, which);
	if (before == which)
		return true;
	if (!before.IsValid())
		return at + 1 == PaletteRefUtils::GetChildCountOfPalette(parent);
	return IndexOfChild(parent, before) == at + 1;
}

/** ReparentPalette - unless the palette already stands exactly there.
    A BOOK PANEL CAN BE "PUT BACK" WHERE IT ALREADY IS (measured). The count of
    book panels is put to zero BEFORE kCloseBookCmdBoss runs (OnBookAboutToClose), and the close can still
    be cancelled after that: a modified book's "save before closing?" comes up INSIDE the command - the
    settings file had been written while the prompt stood open. Cancelled, the panel stays, and the next
    visibility message reads "none, then one" and puts it back. Tabbed into a floating palette with a
    neighbour, that asks ReparentPalette to put the panel into its own group BEFORE ITSELF (the trace of a
    test build: beforeIsSelf=1). Nothing visible came of it on 21.0.2, but PaletteRefUtils.h:203-210 promises
    nothing for that call ("an invalid parent/child combination will trigger an assert in the debug
    build"), so it is not made: a panel already where it is to go is left alone. */
void ReparentUnlessThere(const PaletteRef& which, const PaletteRef& parent, const PaletteRef& before)
{
	if (StandsThere(which, parent, before))
	{
		KFC_DIAG_LOG("BOOKPANEL JOIN already there - left alone");
		return;
	}
	PaletteRefUtils::ReparentPalette(which, parent, before);
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
		if (panel != nil && !KFCBookPanelLookup::IsBookPanel(panel))
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
    panel or it is in no palette. (GetPanelFromWidgetID hands back hidden panels too, and a panel the user
    has CLOSED keeps its container where it was - NeighbourIsOpen.) */
PaletteRef ContainerOfPanel(IPanelMgr* panelMgr, int32 widgetID)
{
	if (widgetID == 0)
		return PaletteRef();
	IControlView* panel = panelMgr->GetPanelFromWidgetID(WidgetID(widgetID));
	if (panel == nil)
		return PaletteRef();
	return panelMgr->GetPaletteRefContainingPanel(panel);
}

#ifdef KFC_DIAG
/** A test build's look at one palette: its kind (c container, g tab group, p column, d dock, f floating dock), its own
    visible flag (GetVisibleState), its effective visibility (IsPaletteVisible), the close box's mark (GetIsClosed), and
    a column's mode ("icons"). */
std::string DescribePalette(const PaletteRef& pal)
{
	const bool column = PaletteRefUtils::IsTabPane(pal) != kFalse;
	const char* kind = PaletteRefUtils::IsFloatingTabbedPaletteDock(pal) ? "f" : PaletteRefUtils::IsDock(pal) ? "d"
		: column ? "p" : PaletteRefUtils::IsTabGroup(pal) ? "g" : PaletteRefUtils::IsTabPanelContainer(pal) ? "c" : "?";
	char one[80];
	_snprintf_s(one, sizeof(one), _TRUNCATE, " %s[own=%d eff=%d closed=%d%s]", kind,
		PaletteRefUtils::GetVisibleState(pal) ? 1 : 0, PaletteRefUtils::IsPaletteVisible(pal) ? 1 : 0,
		PaletteRefUtils::GetIsClosed(pal) ? 1 : 0,
		column && PaletteRefUtils::GetTabPaneMode(pal) == PaletteRefUtils::kIcon_TabPaneMode ? " icons" : "");
	return one;
}

/** A test build's look at every palette from a container up to its dock or floating dock (DescribePalette), or
    " (no container)". */
std::string DescribeLevels(const PaletteRef& from)
{
	std::string levels;
	PaletteRef pal = from;
	for (int32 guard = 0; guard < 16 && pal.IsValid(); ++guard)
	{
		levels += DescribePalette(pal);
		if (PaletteRefUtils::IsDock(pal) || PaletteRefUtils::IsFloatingTabbedPaletteDock(pal))
			break;
		pal = PaletteRefUtils::GetParentOfPalette(pal);
	}
	return levels.empty() ? std::string(" (no container)") : levels;
}

/** A test build's look at a neighbour before the book panel is put beside it (JoinNeighbours): each level up to its
    dock - what a neighbour the user closed, or one in a column folded into icons, looks like there. */
void LogNeighbour(IPanelMgr* panelMgr, const char* role, int32 widgetID)
{
	KFC_DIAG_LOG("BOOKPANEL NEIGHBOUR %s=%d%s", role, widgetID,
		DescribeLevels(ContainerOfPanel(panelMgr, widgetID)).c_str());
}

/** A test build's look at the book panel on each visibility message: floating or docked, its dock's effective and own
    visibility, its container's own flag, and whether the whole palette system is hidden (IsEntirePaletteSystemHidden) -
    what tells Show/Hide All Palettes from InDesign's throw-out. */
void LogBookPanelVisibility()
{
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	InterfacePtr<IControlView> bookPanel(QueryFirstBookPanel(panelMgr));
	if (bookPanel == nil)
		return;
	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	if (!container.IsValid())
		return;
	const bool floating = PaletteRefUtils::IsPaletteFloating(container) != kFalse;
	const PaletteRef dock = floating ? FindFloatingDock(container) : FindAncestor(container, &PaletteRefUtils::IsDock);
	KFC_DIAG_LOG("BOOKPANEL VIS floating=%d dockEffective=%d dockOwn=%d containerOwn=%d systemHidden=%d levels:%s",
		floating ? 1 : 0, Is(dock, &PaletteRefUtils::IsPaletteVisible) ? 1 : 0,
		Is(dock, &PaletteRefUtils::GetVisibleState) ? 1 : 0, PaletteRefUtils::GetVisibleState(container) ? 1 : 0,
		PaletteRefUtils::IsEntirePaletteSystemHidden() ? 1 : 0, DescribeLevels(container).c_str());
}
#endif

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
	out.dockSide = static_cast<int32>(PaletteRefUtils::GetDockLocation(dock));
	out.columnIndex = (ci >= 0) ? ci : 0;
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
		{
			KFC_DIAG_LOG("BOOKPANEL MEASURE docked: not a dock's shape - kept the last");
			return false;
		}
		KFC_DIAG_LOG("BOOKPANEL MEASURE docked side=%d column=%d mate=%d tab=%d next=%d prev=%d nextCol=%d prevCol=%d iconic=%d",
			measured.dockSide, measured.columnIndex, measured.mate, measured.tabIndex, measured.nextGroup,
			measured.prevGroup, measured.nextColumn, measured.prevColumn, measured.iconic ? 1 : 0);
		out = measured;
		return true;
	}

	// Floating. Minimised to its title bar, its frame is not the size to come back to.
	if (PaletteRefUtils::IsPaletteMinimized(container))
	{
		KFC_DIAG_LOG("BOOKPANEL MEASURE floating: minimised - kept the last");
		return false;
	}

	const PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
	{
		KFC_DIAG_LOG("BOOKPANEL MEASURE floating: no floating dock above it - kept the last");
		return false;
	}
	KFC_DIAG_LOG("BOOKPANEL MEASURE floating dock: visible=%d dockMode=%d location=%d",
		PaletteRefUtils::IsPaletteVisible(dock) ? 1 : 0, static_cast<int32>(PaletteRefUtils::GetDockMode(dock)),
		static_cast<int32>(PaletteRefUtils::GetDockLocation(dock)));

	// A floating palette that is not on show is not a place to come back to: it is where InDesign leaves
	// the book panel when it throws it out of the dock (ThrownOutOfTheDock) - recorded, it read "floating
	// at 0,0" and the next opening put the panel there. The last good placement is kept. (A floating dock
	// holding only hidden panels answers false - measured.)
	if (ThrownOutOfTheDock(container))
	{
		KFC_DIAG_LOG("BOOKPANEL MEASURE floating: the floating dock is not on show - kept the last");
		return false;
	}

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
		KFC_DIAG_LOG("BOOKPANEL MEASURE floating: an empty frame and no size from before - kept the last");
		return false;
	}

	KFC_DIAG_LOG("BOOKPANEL MEASURE floating at %d,%d %dx%d iconic=%d floatMate=%d tab=%d next=%d prev=%d",
		measured.left, measured.top, measured.width, measured.height, measured.iconic ? 1 : 0, measured.floatMate,
		measured.floatTabIndex, measured.floatNextGroup, measured.floatPrevGroup);
	out = measured;
	return true;
}

/** Measure the book panel that is open now, if any. */
bool MeasureOpenBookPanel(Placement& out)
{
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	InterfacePtr<IControlView> bookPanel(QueryFirstBookPanel(panelMgr));
	return Measure(panelMgr, bookPanel, out);
}

//----------------------------------------------------------------------------------------
// The keys
//----------------------------------------------------------------------------------------

std::string BoolValue(bool b) { return b ? "true" : "false"; }

/** The placement as keys and raw values - the half in force, and the other half as it was (so a
    panel that went from floating to docked still has a floating place to return to). The whole numbers
    go through the lists by the keys (ForEachFloatPlaceKey and the two after it), in their order. */
void AppendPlacementKeys(const Placement& p, std::vector<std::pair<std::string, std::string> >& keys)
{
	const auto number = [&keys](const char* key, int32 value)
	{
		keys.push_back(std::make_pair(std::string(key), std::to_string(value)));
	};
	keys.push_back(std::make_pair(std::string(kKeyDocked), BoolValue(p.docked)));
	if (p.haveFloat)
	{
		ForEachFloatPlaceKey(p, number);
		ForEachFloatNeighbourKey(p, number);
	}
	if (p.docked)
		ForEachDockKey(p, number);
	keys.push_back(std::make_pair(std::string(kKeyIconic), BoolValue(p.iconic)));
	if (p.iconicWidth > 0)
		number(kKeyIconicWidth, p.iconicWidth);
}

/** Say on the panel's status line that a write went wrong. Only failures are said: a write at every
    book close that reported success would be a line about something the user did not do. */
void SayWriteFailed(const char* reason)
{
	PMString msg("Book panel placement could not be saved (");
	msg.Append(reason);
	msg.Append(").");
	msg.SetTranslatable(kFalse);
	KFCResultTree::ShowStatus(msg);
}

/** Keep this placement for the session and write its keys - nothing else - to the settings file.
    Silent when it works (see SayWriteFailed) - except the once a broken file had to be repaired to
    write it, which is said when sayFailure is set. */
void RememberAndWrite(const Placement& p, bool sayFailure)
{
	if (!p.IsUsable())
		return;
	gRemembered = p;

	std::vector<std::pair<std::string, std::string> > keys;
	AppendPlacementKeys(p, keys);
	bool repaired = false;
	const char* failure = KFCPanelStateWriteKeys(keys, &repaired);
	if (failure != nil && sayFailure)
		SayWriteFailed(failure);
	else if (failure == nil && repaired && sayFailure)
	{
		// A broken settings file was repaired to keep the placement - said once, when it
		// happens: a setting the file had lost comes up at its default from the next start (this
		// session still holds it).
		KFCResultTree::ShowStatus("Book panel placement saved"
			" - the settings file was damaged and has been rewritten with what could be read.");
	}
}

/** The same place - the half in force, its whole numbers walked through the lists by the keys (so a key added to a
    half is compared as soon as it is written), and the icon state. */
bool SamePlace(const Placement& a, const Placement& b)
{
	if (a.docked != b.docked || a.iconic != b.iconic)
		return false;
	std::vector<int32> numbersA;
	std::vector<int32> numbersB;
	const auto intoA = [&numbersA](const char* /*key*/, int32 value) { numbersA.push_back(value); };
	const auto intoB = [&numbersB](const char* /*key*/, int32 value) { numbersB.push_back(value); };
	if (a.docked)
	{
		ForEachDockKey(a, intoA);
		ForEachDockKey(b, intoB);
	}
	else
	{
		ForEachFloatPlaceKey(a, intoA);
		ForEachFloatNeighbourKey(a, intoA);
		ForEachFloatPlaceKey(b, intoB);
		ForEachFloatNeighbourKey(b, intoB);
	}
	return numbersA == numbersB;
}

/** Measure the book panel open now and keep its placement (RememberAndWrite) - the one way a close records one: the
    interceptor's (OnBookAboutToClose) and Stop's backstop. Not while this opening's restore has left the panel where
    InDesign put it and it still stands there (gLeftUnplaced): the remembered placement is kept. */
void RecordPlacement(bool sayFailure)
{
	Placement now;
	if (!MeasureOpenBookPanel(now))
		return;
	if (gLeftUnplaced && SamePlace(now, gWhereInDesignPutIt))
	{
		KFC_DIAG_LOG("BOOKPANEL MEASURE not recorded: still where InDesign put it - the remembered placement is kept");
		return;
	}
	RememberAndWrite(now, sayFailure);
}

//----------------------------------------------------------------------------------------
// Putting it back
//----------------------------------------------------------------------------------------

/** The application's monitor information, AddRef'd - or nil (the session can be gone during
    shutdown). Asked by TitleBandIsOnScreen and FitHeightToScreen. */
const IMonitorInfo* QueryMonitorInfo()
{
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<const IMonitorInfo> monInfo(app, UseDefaultIID());	// nil in, nil out
	return monInfo.forget();
}

/** Would the palette's title band land on a screen if put here? Asked of the screen that holds most
    of the rectangle (IMonitorInfo::GetBestScreenRect handles more than one monitor - KCM's
    KeepPanelOnScreen asks it the same way). "Cannot tell" answers no: a palette left where it
    opened is a smaller fault than one put where nobody can reach it.
    *Only the TOP is judged here, and that is enough for a yes: a bottom edge that would land below
    the screen is not a reason to give up the whole placement - FitHeightToScreen shortens the panel
    instead. */
bool TitleBandIsOnScreen(const Placement& p)
{
	InterfacePtr<const IMonitorInfo> monInfo(QueryMonitorInfo());
	if (monInfo == nil)
		return false;

	SysRect wanted;
	::SetSysRectLTWH(wanted, p.left, p.top, p.width, p.height);
	const GSysRect screen = monInfo->GetBestScreenRect(wanted);

	// Across: this much of the width has to overlap the screen.
	const SysCoord overlapLeft  = (p.left > SysRectLeft(screen)) ? p.left : SysRectLeft(screen);
	const SysCoord wantedRight  = p.left + p.width;
	const SysCoord overlapRight = (wantedRight < SysRectRight(screen)) ? wantedRight : SysRectRight(screen);
	if (overlapRight - overlapLeft < kKFCBookPanelMinOnScreen)
		return false;

	// Down: the top edge - where the title band is - has to be on the screen, with room under it.
	if (p.top < SysRectTop(screen) || p.top > SysRectBottom(screen) - kKFCBookPanelMinOnScreen)
		return false;

	return true;
}

/** The size to give the book panel so that, at the place it is ABOUT TO BE MOVED TO, its bottom edge
    stays on the screen. panelLeft / panelTop are where the panel itself (not its dock) will be.

    The product's rule, and the sibling's. linksui's ForceBottomOfPanelOnMonitor
    (LinksUIUtils.cpp:613-629) runs right after a FLOATING panel is resized (:723 -> :735): ask which
    screen the panel is on (GetBestScreenRect), and if its bottom is below that screen's, shrink it by
    the difference plus 2. KCM copied it as KeepPanelOnScreen (KCMStorySection.cpp). The "which
    screen" question alone, without the shrink, brings a height remembered on a tall screen back - on
    a shorter one or after the monitors were rearranged - with the panel's bottom and its resize grip
    out of reach.

    WHY IT IS ASKED OF THE PLACE TO COME, NOT READ OFF THE VIEW AS THE PRODUCT DOES.
    linksui only RESIZES its panel where it stands; this file also MOVES it, and OWL applies a
    SetPalettePosition asynchronously (PaletteRefUtils.h:40-41). Measured with the product's
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
	if (panelView == nil)
		return size;
	InterfacePtr<const IMonitorInfo> monInfo(QueryMonitorInfo());
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

/** Floating: where, how big, and collapsed or not.
    @return false - nothing done, the panel left where InDesign put it - when no floating place is remembered, there is
    no floating dock to move, or the place's title band would land off every screen. */
bool RestoreFloating(IControlView* bookPanel, const PaletteRef& container, const Placement& p)
{
	if (!p.haveFloat)
		return false;

	const PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
		return false;

	if (!TitleBandIsOnScreen(p))
		return false;

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

	// Through ConstrainDimensions: putting the size through it is the CALLER'S job, it is not
	// something Resize does on the way in (IControlView.h:174-176) - and it is the book panel's own
	// view that knows its limits (it rounds its height - see the file header). The product calls it
	// before resizing a floating panel too (LinksUIUtils.cpp:626-627).
	// ...and then fitted to the screen at the NEW place (see FitHeightToScreen), so a height
	// remembered on a taller screen does not leave the bottom edge where it cannot be reached.
	PMPoint size(PMReal(p.width), PMReal(p.height));
	size = bookPanel->ConstrainDimensions(size);
	size = FitHeightToScreen(bookPanel, p.left + insetLeft, p.top + insetTop, size);
	bookPanel->Resize(size);

	// And last, collapsed to icons or not (the user's request). Last because the size is
	// what the panel expands to, and it is set on the expanded panel.
	ApplyIconState(FindAncestor(container, &PaletteRefUtils::IsTabPane), p);
	return true;
}

/** Is this dock or floating dock on show (IsPaletteVisible) and not closed (GetIsClosed)? false for an invalid
    one. NeighbourIsOpen says why its own flag alone does not decide. */
bool DockOnShow(const PaletteRef& dock)
{
	return dock.IsValid() && PaletteRefUtils::IsPaletteVisible(dock) && !PaletteRefUtils::GetIsClosed(dock);
}

/** IS A NEIGHBOUR STILL OPEN - asked before the book panel is put beside it, docked or floating
    (JoinNeighbours)? A panel the user has closed keeps its container where it was, and a book panel put beside
    it stands in what was closed, nowhere on screen. Measured, each palette from the neighbour's container up to
    its dock (block 16's defect re-check, work/note-scripts/2026-10-21-kfc-block16-defect/live_b16d.ps1):
      closed from the Window menu, alone in the right dock (scene K) - its TAB GROUP marked closed
        (GetIsClosed), the column and the dock hidden by their own flags, the container still shown by its own;
        the book panel put into that group stood in the hidden dock with the book open.
      open in a column folded into icons (scene I) - its tab group hidden by its own flag, not closed, the
        column and the dock on show (its container's own flag read 0 once too: KBSBookPanelDiag.log, 2026-09-25).
      open, every palette hidden with the Tab key (scene T) - the dock CONCEALED and still on show, its column
        and tab group hidden by their own flags, nothing closed.
    So an own flag below the dock does not say closed - a column of icons and the Tab key clear them too. What
    does is the close mark, at any level up to the dock, and the dock not on show (DockOnShow - a dock emptied by
    closing its panels); both are asked here. */
bool NeighbourIsOpen(IPanelMgr* panelMgr, int32 widgetID)
{
	PaletteRef pal = ContainerOfPanel(panelMgr, widgetID);
	for (int32 guard = 0; guard < 16 && pal.IsValid(); ++guard)
	{
		if (PaletteRefUtils::GetIsClosed(pal))
			return false;
		if (PaletteRefUtils::IsDock(pal) || PaletteRefUtils::IsFloatingTabbedPaletteDock(pal))
			return DockOnShow(pal);
		pal = PaletteRefUtils::GetParentOfPalette(pal);
	}
	return false;		// in no dock that can be found - nothing to vouch for
}

/** Back beside the same neighbours, the closest one that can still be found first: into a mate's
    tab group at the old tab place, else just above the group below, else just below the group above.
    The book panel arrives in a floating palette of its own; what is moved is its TAB (into a mate's
    group) or its TAB GROUP (into a column), which is the level each neighbour describes. One walk for
    a dock and for a shared floating palette - the tree has the same shape in both. Each step works out
    where it puts the panel once, and the test build's trace reports that same place (it is what the
    trace is read for - ReparentUnlessThere's beforeIsSelf). A neighbour the user has closed is passed
    over (NeighbourIsOpen).
    @return true when the book panel was moved. */
bool JoinNeighbours(IPanelMgr* panelMgr, const PaletteRef& container, int32 mate, int32 tabIndex,
	int32 nextGroupPanel, int32 prevGroupPanel)
{
	const PaletteRef ownGroup = ParentOf(container);
	if (!Is(ownGroup, &PaletteRefUtils::IsTabGroup))
		return false;
#ifdef KFC_DIAG
	if (mate != 0)
		LogNeighbour(panelMgr, "mate", mate);
	if (nextGroupPanel != 0)
		LogNeighbour(panelMgr, "nextGroup", nextGroupPanel);
	if (prevGroupPanel != 0)
		LogNeighbour(panelMgr, "prevGroup", prevGroupPanel);
#endif

	// 1. A panel it shared a tab group with: back into that group, at its tab's old place.
	//    (Measured: Pages as the mate, the tab back at 0 and at 1.)
	if (mate != 0 && NeighbourIsOpen(panelMgr, mate))
	{
		const PaletteRef mateGroup = ParentOf(ContainerOfPanel(panelMgr, mate));
		if (Is(mateGroup, &PaletteRefUtils::IsTabGroup))
		{
			const PaletteRef before = ChildAtOrEnd(mateGroup, tabIndex);
			KFC_DIAG_LOG("BOOKPANEL JOIN mate=%d tab=%d ownGroup=%d beforeIsSelf=%d standsThere=%d", mate, tabIndex,
				mateGroup == ownGroup ? 1 : 0, before == container ? 1 : 0,
				StandsThere(container, mateGroup, before) ? 1 : 0);
			ReparentUnlessThere(container, mateGroup, before);
			return true;
		}
	}

	// 2. The group below it: its own group goes back in just above that one. (Measured.)
	if (nextGroupPanel != 0 && NeighbourIsOpen(panelMgr, nextGroupPanel))
	{
		const PaletteRef nextGroup = ParentOf(ContainerOfPanel(panelMgr, nextGroupPanel));
		const PaletteRef column = Is(nextGroup, &PaletteRefUtils::IsTabGroup) ? ParentOf(nextGroup) : PaletteRef();
		if (Is(column, &PaletteRefUtils::IsTabPane))
		{
			KFC_DIAG_LOG("BOOKPANEL JOIN nextGroup=%d standsThere=%d", nextGroupPanel,
				StandsThere(ownGroup, column, nextGroup) ? 1 : 0);
			ReparentUnlessThere(ownGroup, column, nextGroup);
			return true;
		}
	}

	// 3. The group above it: just below that one. (Measured.)
	if (prevGroupPanel != 0 && NeighbourIsOpen(panelMgr, prevGroupPanel))
	{
		const PaletteRef prevGroup = ParentOf(ContainerOfPanel(panelMgr, prevGroupPanel));
		const PaletteRef column = Is(prevGroup, &PaletteRefUtils::IsTabGroup) ? ParentOf(prevGroup) : PaletteRef();
		if (Is(column, &PaletteRefUtils::IsTabPane))
		{
			const PaletteRef before = ChildAtOrEnd(column, IndexOfChild(column, prevGroup) + 1);
			KFC_DIAG_LOG("BOOKPANEL JOIN prevGroup=%d beforeIsSelf=%d standsThere=%d", prevGroupPanel,
				before == ownGroup ? 1 : 0, StandsThere(ownGroup, column, before) ? 1 : 0);
			ReparentUnlessThere(ownGroup, column, before);
			return true;
		}
	}

	return false;
}

/** Docked: into the dock next to the same neighbours (JoinNeighbours), and when none can be found, a
    new column beside the column that was next to it.
    @return false when the panel is left where InDesign put it. */
bool RestoreDocked(IPanelMgr* panelMgr, const PaletteRef& container, const Placement& p)
{
	const PaletteRef ownGroup = ParentOf(container);
	if (!Is(ownGroup, &PaletteRefUtils::IsTabGroup))
		return false;

	// 1-3. Beside the same neighbours - see JoinNeighbours (a 0 there = none).
	if (JoinNeighbours(panelMgr, container, p.mate, p.tabIndex, p.nextGroup, p.prevGroup))
		return true;

	// 4. It had a column to itself, so there is no column left to go back into: a new one, beside
	//    the column that was after it (or before it), in the same icon state and width.
	//    (Measured beside another column by the user; beside the toolbox, and alone in the dock, by a
	//    test run - see below and step 5.)
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
	// 5. No neighbouring column either - the book panel was alone in its dock: the dock it was in, by
	//    its side, and a new column at the place its own column had.
	if (!Is(dock, &PaletteRefUtils::IsDock) && p.dockSide != 0)
	{
		dock = PaletteRefUtils::FindDockByLocation(static_cast<PaletteRefUtils::DockLocation>(p.dockSide));
		if (Is(dock, &PaletteRefUtils::IsDock))
			before = ChildAtOrEnd(dock, p.columnIndex);
	}
	// ...and only into a dock on show: one the user emptied by closing its panels is not (measured -
	// NeighbourIsOpen), and a new column in it would stand hidden with the book panel in it.
	if (Is(dock, &PaletteRefUtils::IsDock) && !DockOnShow(dock))
	{
		KFC_DIAG_LOG("BOOKPANEL RESTORE docked: the dock is not on show (%s) - left where InDesign put it",
			DescribePalette(dock).c_str());
		return false;
	}
	if (!Is(dock, &PaletteRefUtils::IsDock))
	{
		KFC_DIAG_LOG("BOOKPANEL RESTORE docked: no neighbour and no dock found - left where InDesign put it");
		return false;		// no neighbour left to find the place by - it stays where InDesign put it
	}

	const PaletteRef newColumn = PaletteRefUtils::NewTabPaneInDock(dock,
		p.iconic ? PaletteRefUtils::kIcon_TabPaneMode : PaletteRefUtils::kExpanded_TabPaneMode, before);
	KFC_DIAG_LOG("BOOKPANEL RESTORE docked: a new column in the dock (side=%d visible=%d floatingDock=%d), before a %s, made=%d",
		static_cast<int32>(PaletteRefUtils::GetDockLocation(dock)), PaletteRefUtils::IsPaletteVisible(dock) ? 1 : 0,
		PaletteRefUtils::IsFloatingTabbedPaletteDock(dock) ? 1 : 0, before.IsValid() ? "sibling" : "nothing (the end)",
		Is(newColumn, &PaletteRefUtils::IsTabPane) ? 1 : 0);
	if (!Is(newColumn, &PaletteRefUtils::IsTabPane))
		return false;
	// NewTabPaneInDock DOES NOT TAKE THE TOOLBOX AS "BEFORE" (measured). A
	// column that sat between the document and the docked toolbox (the toolbox is a dock child too -
	// IsToolbar, not IsTabPane) was recorded as column 0 of 2; with the column gone, the child at 0 is
	// the toolbox, and a new tab pane asked to go before it was put at the END - the outer side of the
	// toolbox, left dock and right alike (the user). Then the next close recorded that place, so it
	// stayed there. ReparentPalette does put the new column before the toolbox (measured: child 0,
	// drawn inside the toolbox), so the column is moved there when it did not arrive there.
	if (before.IsValid() && IndexOfChild(dock, newColumn) + 1 != IndexOfChild(dock, before))
		PaletteRefUtils::ReparentPalette(newColumn, dock, before);
	PaletteRefUtils::ReparentPalette(ownGroup, newColumn, PaletteRef());
	ApplyIconState(newColumn, p);
	return true;
}

/** A BOOK PANEL PUT INTO A NEIGHBOUR'S TAB GROUP STAYS BEHIND the tab that was in front (measured: Pages, and the
    floating KFC panel, kept the front - live_b16d.ps1 scenes J and M), so the book just opened shows no panel. It is
    brought forward the way InDesign shows a panel - IPanelMgr::ShowPanelByWidgetID, what a panel's Window-menu item
    does (SnpShowPalette.cpp's ShowHideByWidget) - and without the keyboard. (The author's call: forward, as InDesign
    shows the panel of a book it opens.) Only when the restore moved it into another group (groupBefore): left in its
    own, it is in front already, and one put back where it already stood - after a cancelled close - is not moved. Not
    in a column folded into icons either: every panel there is on show as its icon, and showing one opens its drawer
    over the document. (Shown, the book panel widened the narrower Pages column to its own width, and InDesign kept that
    width after the book closed - measured 148 -> 240, scenes J and T. Not a fault of the restore, but a column grown
    under a floating palette makes Pages' Window-menu item bring it forward instead of closing it - measured.) */
void BringForwardIfJoined(IPanelMgr* panelMgr, IControlView* bookPanel, const PaletteRef& groupBefore)
{
	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	const PaletteRef group = ParentOf(container);
	if (group == groupBefore || !Is(group, &PaletteRefUtils::IsTabGroup)
			|| PaletteRefUtils::GetChildCountOfPalette(group) < 2)
		return;
	const PaletteRef column = FindAncestor(container, &PaletteRefUtils::IsTabPane);
	if (column.IsValid() && PaletteRefUtils::GetTabPaneMode(column) == PaletteRefUtils::kIcon_TabPaneMode)
		return;
	KFC_DIAG_LOG("BOOKPANEL RESTORE brought forward in the tab group it joined");
	panelMgr->ShowPanelByWidgetID(bookPanel->GetWidgetID(), kFalse);
}

/** Put the remembered placement on the book panel that is open now. Runs from the timer. */
void RestoreNow()
{
	if (!gOn || !gRemembered.IsUsable())
		return;

	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	InterfacePtr<IControlView> bookPanel(QueryFirstBookPanel(panelMgr));
	if (bookPanel == nil)
		return;		// closed again in the meantime

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	if (!container.IsValid())
		return;

	// This opening's first restore, or a re-restore (see gRestoredThisOpening)?
	const bool reRestore = gRestoredThisOpening;
	gRestoredThisOpening = true;

	// InDesign put it in a dock itself: that is InDesign's own memory at work - leave it.
	if (!PaletteRefUtils::IsPaletteFloating(container))
	{
		KFC_DIAG_LOG("BOOKPANEL RESTORE skipped: already docked");
		return;
	}

	// Put back from a hidden palette = InDesign threw it out of the dock again: counted, so it is only
	// done a few times per opening (MaybeRestoreAgain arms it on the same question). A re-restore that
	// finds the panel on show leaves it: the user's floating place (see gRestoredThisOpening).
	const bool thrownOut = ThrownOutOfTheDock(container);
	if (reRestore && !thrownOut)
	{
		KFC_DIAG_LOG("BOOKPANEL RESTORE-AGAIN dropped: the panel is floating on show now - left where it is");
		return;
	}
	if (thrownOut)
		++gReRestores;
	KFC_DIAG_LOG("BOOKPANEL RESTORE remembered docked=%d floatMate=%d reRestores=%d",
		gRemembered.docked ? 1 : 0, gRemembered.floatMate, gReRestores);

	const PaletteRef groupBefore = ParentOf(container);
	bool putBack = false;
	if (gRemembered.docked)
		putBack = RestoreDocked(panelMgr, container, gRemembered);
	else
		// Floating: back into the palette it shared, when that palette is open (see the file header);
		// its own place and size otherwise. Joined, the place and size are the palette's - nothing
		// more is put on it.
		putBack = JoinNeighbours(panelMgr, container, gRemembered.floatMate, gRemembered.floatTabIndex,
				gRemembered.floatNextGroup, gRemembered.floatPrevGroup)
			|| RestoreFloating(bookPanel, container, gRemembered);

	// Put back: forward in a tab group it joined (BringForwardIfJoined). Left where InDesign put it: that place is
	// measured, and the close keeps the remembered placement while the panel stays there (gLeftUnplaced).
	if (putBack)
	{
		gLeftUnplaced = false;
		BringForwardIfJoined(panelMgr, bookPanel, groupBefore);
	}
	else
	{
		gLeftUnplaced = Measure(panelMgr, bookPanel, gWhereInDesignPutIt);
		KFC_DIAG_LOG("BOOKPANEL RESTORE left where InDesign put it - nothing recorded while it stays (measured=%d)",
			gLeftUnplaced ? 1 : 0);
	}

#ifdef KFC_DIAG
	const PaletteRef after = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	const PaletteRef afterFloatingDock = FindFloatingDock(after);
	KFC_DIAG_LOG("BOOKPANEL RESTORE done: floating=%d floatingDockVisible=%d", (after.IsValid() && PaletteRefUtils::IsPaletteFloating(after)) ? 1 : 0,
		(afterFloatingDock.IsValid() && PaletteRefUtils::IsPaletteVisible(afterFloatingDock)) ? 1 : 0);
#endif
}

/** Book one of this file's two one-shot timers (gRestoreTimer, gHomeTimer): made on first use and kept for
    the life of the plug-in, and a booking still pending dropped before the new one - never a StartTimer on
    top of a live booking (memory icallbacktimer). false when no timer could be had. The one place a timer
    here is booked, as GiveBackTimer is the one place one is let go: the two timers cannot come to follow
    different rules. */
bool BookTimer(ICallbackTimer*& timer, TimerCallback callback, uint32 delayMs)
{
	if (timer == nil)
		timer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (timer == nil)
		return false;
	timer->StopTimer();
	timer->StartTimer(callback, delayMs, nil);
	return true;
}

/** Drop a timer's pending booking and let it go. Never from inside its own callback (KFCBookWatch.cpp
    explains why) - GiveBackTimers, at the end of the feature, is its caller. */
void GiveBackTimer(ICallbackTimer*& timer)
{
	if (timer == nil)
		return;
	timer->StopTimer();
	timer->Release();
	timer = nil;
}

/** Timer callback. A raw function pointer, so it must never outlive this plug-in (see
    ShutdownCleanup). NOTHING is released in here - the same rule and the same reason as
    KFCBookWatch's RetireTimerCallback. */
uint32 RestoreTimerCallback(void* /*refPtr*/)
{
	RestoreNow();

	// NEVER 0: to the idle task manager 0 means "call me again immediately".
	return IIdleTask::kEndOfTime;
}

/** Arm the deferred restore (or restart its wait if one is pending). */
void ArmRestoreTimer(uint32 delayMs = kKFCBookPanelRestoreDelayMs)
{
	if (gShutDown)
		return;		// past the clean-up (see gShutDown) - not even the restore done at once below

	if (!BookTimer(gRestoreTimer, RestoreTimerCallback, delayMs))
		RestoreNow();		// no timer to be had - do it now rather than not at all
}

/** How many documents are open. */
int32 OpenDocumentCount()
{
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docs(app != nil ? app->QueryDocumentList() : nil);
	return (docs != nil) ? docs->GetDocCount() : 0;
}

/** The Home screen standing over the open books (see gHomeTimer): close them and open them again.
    Everything is asked and done in ONE script - app.homeScreenVisible is scripting-only. The 4-argument
    RunScriptInEngine, as KESCL runs its script (the 9-argument one's ScriptRecordData needs a library
    KFC does not link): it hands no value back, so the script THROWS whenever it declines - the Home
    screen not up, a document open, no book, a book modified - and kSuccess means "reopened". */
void LeaveHomeScreen()
{
	if (gLeftHomeScreen || !gOn || !gRemembered.docked)
		return;
	Utils<IExtendScriptUtils> utils;
	if (!utils.Exists())
		return;
	PMString engine;
	engine.SetTranslatable(kFalse);
	engine.SetUTF8String(std::string("KohakuFindChangeBookPanel"));
	PMString text;
	text.SetTranslatable(kFalse);
	text.SetUTF8String(std::string(
		"(function () {"
		" if (!app.homeScreenVisible) throw new Error(\"no-home\");"
		" if (app.documents.length > 0) throw new Error(\"documents\");"
		" if (app.books.length == 0) throw new Error(\"no-book\");"
		" var files = [];"
		" for (var i = 0; i < app.books.length; i++) {"
		"  if (app.books[i].modified) throw new Error(\"modified\");"
		"  files.push(app.books[i].fullName);"
		" }"
		" for (var j = app.books.length - 1; j >= 0; j--) app.books[j].close(SaveOptions.NO);"
		" for (var k = 0; k < files.length; k++) app.open(files[k]);"
		"})();"));
	// No clear of the error state after it: a run leaves no error of its own, declined or not - the failure is the
	// return value (measured with KT's scripterr - kfc-block13-inspections-2026-10-20.md); a clear here could only
	// wipe an error somebody else had left standing.
	const ErrorCode ran = utils->RunScriptInEngine(engine, text, kFalse /*showErrorAlert*/, kFalse /*invokeDebugger*/);
	KFC_DIAG_LOG("BOOKPANEL HOME script %s (check %d)", ran == kSuccess ? "REOPENED THE BOOKS" : "declined", gHomeChecks);
	if (ran == kSuccess)
		gLeftHomeScreen = true;		// the reopening itself puts the book panel back (0 -> 1)
}

/** Home timer callback - the same rules as RestoreTimerCallback. */
uint32 HomeTimerCallback(void* /*refPtr*/)
{
	LeaveHomeScreen();
	return IIdleTask::kEndOfTime;
}

/** The session is past its launch: the Home screen is not looked at again (see gHomeTimer). */
void EndHomeWindow(const char* why)
{
	gHomeWindowOver = true;
	KFC_DIAG_LOG("BOOKPANEL HOME window over: %s", why);
	(void)why;
}

/** A visibility message with a book panel up, no document open and the placement docked, around launch:
    look at the Home screen once the messages stop. Its own timer, so the restore's short wait is
    untouched. */
void MaybeCheckHomeScreen()
{
	if (gShutDown || gLeftHomeScreen || gHomeWindowOver)
		return;
	// Past launch, for good (see gHomeTimer): the window has run out, or a document is open now (the
	// interceptor ends it as one opens - this is the backstop for a toggle switched on with one open).
	if (std::chrono::steady_clock::now() - gStartedAt > kKFCHomeWindowMs)
	{
		EndHomeWindow("the time since launch ran out");
		return;
	}
	if (OpenDocumentCount() > 0)
	{
		EndHomeWindow("a document is open");
		return;
	}
	if (!gRemembered.IsUsable() || !gRemembered.docked || gBookPanelCount == 0 || gHomeChecks >= kKFCMaxHomeChecks)
		return;
	if (!BookTimer(gHomeTimer, HomeTimerCallback, kKFCHomeCheckDelayMs))
		return;
	++gHomeChecks;		// counted and traced once it is booked (the check runs later, from the idle task)
	KFC_DIAG_LOG("BOOKPANEL HOME check %d armed", gHomeChecks);
}

/** The book panel thrown out of the dock by InDesign (see kKFCBookPanelReRestoreDelayMs): remembered
    docked, and now in a floating palette that is not on show. Then the restore is armed again, with
    the longer wait - every message restarts it, so it runs once they stop. */
void MaybeRestoreAgain()
{
	if (!gRemembered.IsUsable() || !gRemembered.docked || gReRestores >= kKFCBookPanelMaxReRestores)
		return;
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	InterfacePtr<IControlView> bookPanel(QueryFirstBookPanel(panelMgr));
	if (bookPanel == nil)
		return;
	if (!ThrownOutOfTheDock(panelMgr->GetPaletteRefContainingPanel(bookPanel)))
		return;		// docked, or floating and on show: the user's, or the first restore still to come
	KFC_DIAG_LOG("BOOKPANEL RESTORE-AGAIN armed: remembered docked, the panel in a hidden floating dock (done %d)", gReRestores);
	ArmRestoreTimer(kKFCBookPanelReRestoreDelayMs);
}

/** Count the book panels afresh, and arm a restore if there were none at the last look and there are
    some now. The one place the "zero to some" question is asked. */
void RecountAndMaybeRestore()
{
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	const int32 previous = gBookPanelCount;
	gBookPanelCount = WalkBookPanels(panelMgr, nil);

	// From none, only a book panel standing in a palette has appeared - not a dying one (CountPlacedBookPanels).
	if (previous == 0 && gBookPanelCount > 0 && CountPlacedBookPanels(panelMgr) == 0)
	{
		KFC_DIAG_LOG("BOOKPANEL RECOUNT 0->%d not taken: no book panel stands in a palette", gBookPanelCount);
		gBookPanelCount = 0;
	}

	if (previous == 0 && gBookPanelCount > 0 && gRemembered.IsUsable())
	{
		KFC_DIAG_LOG("BOOKPANEL RECOUNT 0->%d: restore armed", gBookPanelCount);
		gReRestores = 0;
		gRestoredThisOpening = false;
		gLeftUnplaced = false;
		gHomeChecks = 0;
		ArmRestoreTimer();
	}
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

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKFCBOOKPANELOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	if (panelMgr == nil)
		return;

	InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
	if (subject == nil)
		return;

	const bool attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKFCBOOKPANELOBSERVER) != kFalse;
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKFCBOOKPANELOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKFCBOOKPANELOBSERVER);
}

/** Drop the pending bookings and give both timers back - the Home screen's and the restore's. The ONE
    place either is released (through GiveBackTimer). */
void GiveBackTimers()
{
	GiveBackTimer(gHomeTimer);
	GiveBackTimer(gRestoreTimer);
}

ICommandProcessor* QueryCommandProcessor()
{
	ISession* session = GetExecutionContextSession();
	return (session != nil) ? session->QueryCommandProcessor() : nil;
}

/** Put the interceptor in place if it is not there already. The ONE place that installs - the
    interface's own InstallSelf is left empty, as KIDMCP's is: two ways to install one thing is how a
    pointer gets left behind in the command processor.
    ONLY WHILE THE TOGGLE IS ON. An interceptor sees every command InDesign processes, and its header
    asks third-party code to use it "with extreme caution" (ICommandInterceptor.h:37-40). Installed
    at PaletteMgrStarted whatever the toggle said, every user of KFC - the feature being OFF by
    default - would have every command pass through it for the whole session. So Start installs it
    only when the toggle is ON and ToggleAndSave puts it in and takes it out with the tick. (KIDMCP
    keeps its own resident, and that is right there: recording commands is what that tool is for.) */
void InstallCmdWatch()
{
	if (gShutDown || gCmdWatch != nil)
		return;

	InterfacePtr<ICommandProcessor> processor(QueryCommandProcessor());
	if (processor == nil)
		return;

	InterfacePtr<ICommandInterceptor> watch(::CreateObject2<ICommandInterceptor>(kKFCBookPanelCmdWatchBoss));
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
    And the count has to be told. Nothing will announce that the panel has gone, so when
    the book about to close is the last one open, the count is put to zero here - otherwise the next
    book to open would not look like "none, then one" and would never be put back.
    *With several books open their panels are normally tabs of ONE palette, so the first book panel
    found stands for all of them. A book panel dragged out into a palette of its own is not told
    apart - the first one found is what is measured.
    *A CLOSE CAN STILL BE CANCELLED after this (measured): a modified book asks whether to
    save from inside the command, after this has run. Cancelled, the panel stays with the count at zero,
    and the next visibility message puts it back where it already is - the place and size just measured,
    or its own tab group (ReparentUnlessThere leaves that alone). The count is not repaired here: a
    script can close and reopen a book within one call (LeaveHomeScreen), and only a zero count lets that
    reopening be put back. */
void OnBookAboutToClose()
{
	if (!gOn)
		return;

	RecordPlacement(true);

	// Still counted: the command has not run yet, so the closing book is one of these.
	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil || bookMgr->GetBookCount() <= 1)
		gBookPanelCount = 0;
	KFC_DIAG_LOG("BOOKPANEL CLOSE books=%d count now %d", bookMgr != nil ? bookMgr->GetBookCount() : -1, gBookPanelCount);
}

/** Stop following, and give back everything that holds a pointer into this .pln: the observer off the panel
    manager, the interceptor out of the command processor (above all - the processor holds a raw pointer into this
    .pln), the timers stopped and released. Both ends of the feature end with it - Stop at the palette manager's
    close, and ShutdownCleanup in case that never came. Safe to call twice. */
void LetGoOfEverything()
{
	AttachObserver(false);
	UninstallCmdWatch();
	GiveBackTimers();
}

/** The palette manager has laid its palettes out (IPaletteMgrService::PaletteMgrStarted - the service at the foot
    of this file): start following the panel manager, put the interceptor in if the toggle is ON (ToggleAndSave puts
    it in and takes it out after that - it sees every command, so it is only there while it is needed), and treat
    any book panel already open as "just appeared". */
void Start()
{
	// The saved settings are read by the startup service - but that one is LAZY
	// (kLazyStartupShutdownProviderImpl), and nothing promises it runs before the palette manager
	// has laid out. Reading here as well costs nothing: the read is guarded to happen once.
	KFCLoadPanelStateIfPresent();

	// The Home screen is looked at only for a while from here (see gHomeTimer).
	gStartedAt = std::chrono::steady_clock::now();
	gHomeWindowOver = false;
	KFC_DIAG_LOG("BOOKPANEL START on=%d remembered docked=%d", gOn ? 1 : 0, gRemembered.docked ? 1 : 0);

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

/** The palette manager is about to close its palettes (PaletteMgrAboutToShutdown, the same service): stop
    following. It also measures an open book panel if the toggle is ON - a backstop only: on 21.0.2 a quit closes
    the books BEFORE this, and the interceptor has already written the placement. */
void Stop()
{
	// NOT where a quit's placement is written. Measured on 21.0.2: a quit closes the books
	// BEFORE this is called, so the interceptor has already measured the Book panel and this finds
	// none. Kept as a backstop for a build that closes them later - it does nothing when there is no
	// Book panel. Nothing is said on failure: InDesign is quitting.
	if (gOn)
		RecordPlacement(false);

	LetGoOfEverything();
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KFCBookPanelPlacement
//----------------------------------------------------------------------------------------

bool KFCBookPanelPlacement::IsOn()
{
	return gOn;
}

void KFCBookPanelPlacement::ToggleAndSave(PMString& outStatus)
{
	gOn = !gOn;

	// A fresh count, so the next look compares against what is open NOW. Without it a count left
	// from before the toggle was switched off would make an already-open book panel look new, and
	// the first notification after ticking the box would move it.
	if (gOn)
	{
		InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
		gBookPanelCount = WalkBookPanels(panelMgr, nil);
	}

	// The interceptor follows the tick (see InstallCmdWatch). This runs from the menu action, never from
	// inside the interceptor's own InterceptProcessCommand, so UninstallCmdWatch never releases the
	// object while one of its own calls is on the stack.
	if (gOn)
		InstallCmdWatch();
	else
		UninstallCmdWatch();

	// The toggle's key, and nothing else (the author's rule).
	std::vector<std::pair<std::string, std::string> > keys;
	keys.push_back(std::make_pair(std::string(kKeyToggle), BoolValue(gOn)));
	bool repaired = false;
	const char* failure = KFCPanelStateWriteKeys(keys, &repaired);

	// What was set, then WHERE it was written - the full path on a line of its own, the way "Save
	// Panel Settings" shows it (the author's call), so the file can be found, backed up or
	// deleted. The first line stays because, unlike Save Panel Settings, this command also CHANGES
	// something, and the status line is where it says which way it went.
	outStatus = gOn ? "Remember book panel placement: on" : "Remember book panel placement: off";
	if (failure == nil && repaired)
		outStatus.Append(" (the settings file was damaged and has been rewritten with what could be read)");
	if (failure == nil)
	{
		PMString path;
		if (KFCPanelStateFilePath(path))
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

void KFCBookPanelPlacement::AppendSaveKeys(std::vector<std::pair<std::string, std::string> >& keys)
{
	keys.push_back(std::make_pair(std::string(kKeyToggle), BoolValue(gOn)));

	// An explicit save is a snapshot of the screen: the book panel as it stands now, if one is open.
	Placement now;
	if (MeasureOpenBookPanel(now))
		gRemembered = now;
	if (gRemembered.IsUsable())
		AppendPlacementKeys(gRemembered, keys);
}

void KFCBookPanelPlacement::LoadFromSettings(const std::string& text)
{
	gOn = KFCPanelStateReadBool(text, kKeyToggle, gOn);

	// A whole number into its field when its key is there (KFCPanelStateReadInt leaves the field alone when it
	// is not) - through the same lists the write walks (ForEachFloatPlaceKey and the two after it).
	const auto number = [&text](const char* key, int32& field) { KFCPanelStateReadInt(text, key, field); };

	Placement p;

	// Floating: all four numbers, or none - a placement with its height missing is not a place to put
	// anything back to. So they are read into a placement of their own, taken only when all four came.
	Placement place;
	bool allFour = true;
	ForEachFloatPlaceKey(place, [&text, &allFour](const char* key, int32& field)
	{
		if (!KFCPanelStateReadInt(text, key, field))
			allFour = false;
	});
	if (allFour && place.width > 0 && place.height > 0)
	{
		p = place;			// p holds nothing else yet: just the place and the size
		p.haveFloat = true;

		// Its neighbours in a shared palette; a file written before they were kept has none (0).
		ForEachFloatNeighbourKey(p, number);
	}

	// Docked: the neighbours (a missing one reads as 0 = none). A file written before docking was
	// remembered has no "bookPanelDocked" and is floating.
	p.docked = KFCPanelStateReadBool(text, kKeyDocked, false);
	if (p.docked)
		ForEachDockKey(p, number);

	p.iconic = KFCPanelStateReadBool(text, kKeyIconic, false);
	number(kKeyIconicWidth, p.iconicWidth);

	if (p.IsUsable())
		gRemembered = p;
}

void KFCBookPanelPlacement::ShutdownCleanup()
{
	gShutDown = true;		// **first: from here nothing is installed or booked again (see gShutDown)
	LetGoOfEverything();
}

//----------------------------------------------------------------------------------------
// The observer
//----------------------------------------------------------------------------------------

/** Follows the panel manager, and puts the placement back when a book panel appears where there was
    none. (It also listened for kAboutToClosePaletteMsg at first, to measure a closing book panel;
    that message never came for one - see the file header - and the interceptor does that job.) */
class KFCBookPanelObserver : public CObserver
{
public:
	KFCBookPanelObserver(IPMUnknown* boss) : CObserver(boss, IID_IKFCBOOKPANELOBSERVER) {}
	virtual ~KFCBookPanelObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KFCBookPanelObserver, kKFCBookPanelObserverImpl)

void KFCBookPanelObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	if (protocol != IID_IPANELMGR || theChange != kPaletteVisibilityChangedMessage)
		return;

	// Nothing while OFF: the visibility message arrives several times over merely opening a document
	// (measured - five times while one book opened), and people not using this should not pay for a
	// walk of the panel list each time.
	if (!gOn)
		return;

#ifdef KFC_DIAG
	LogBookPanelVisibility();
#endif
	RecountAndMaybeRestore();
	MaybeRestoreAgain();
	MaybeCheckHomeScreen();
}

//----------------------------------------------------------------------------------------
// The command interceptor
//----------------------------------------------------------------------------------------

/** Sees kCloseBookCmdBoss before it runs. "Use with extreme caution from third-party client code"
    (ICommandInterceptor.h:40), and the caution taken is KIDMCP's (KIDMCPCmdWatch.cpp):
    kCmdNotHandled ON EVERY PATH. This class is allowed to cancel any command in InDesign
    and never does: kCmdNotHandled is "pass it on", which is what an interceptor that is not there
    would produce. Everything is inside try/catch - an exception leaving here lands in the middle of
    InDesign's command processing. And the test that runs for every command is a class comparison -
    three more, and only until the session is past its launch, for the Home screen's window (see
    gHomeTimer). At file scope, not in the anonymous namespace, for the reason the other
    implementations here are.

    WHY NOT THE OBSERVER KFC ALREADY HAS (measured). KFCBookWatch
    hears the same close as kCloseBookCmdBoss @ kSessionBoss (IID_IBOOKCONTENT), and the official way
    to see a command before it is done is an observer that finds GetCommandState() == kNotDone
    (persistentlist/PstLstDocObserver.cpp:189-195). That notification does arrive as kNotDone - but
    kNotDone is "before OR WHILE the command is done" (ICommand.h:119), and it comes WHILE: in the log
    of all three closes (script, the Book panel's Close Book, quitting) the order was this class's
    kCloseBookCmdBoss, then kDestroyPanelCmdBoss, then KFCBookWatch - with no book panel left to
    measure by then. So the interceptor stays, the only door there is - installed only while ON (see
    InstallCmdWatch) and behind the main-thread gate below.

    MAIN THREAD ONLY. An interceptor is handed commands from whichever thread processes
    them - KIDMCP's tells the two sides apart for that reason (KIDMCPCmdWatch.cpp, IsMainThreadDomain)
    - and what this one does on a book close is UI work (the panel manager, the palette tree). A book
    close off the main thread has never been seen here, and nothing says one cannot happen; the gate
    is one call per book close, and it keeps this from ever doing UI work on another thread. */
class KFCBookPanelCmdWatch : public CPMUnknown<ICommandInterceptor>
{
public:
	KFCBookPanelCmdWatch(IPMUnknown* boss) : CPMUnknown<ICommandInterceptor>(boss) {}
	virtual ~KFCBookPanelCmdWatch() {}

	virtual InterceptResult InterceptProcessCommand(ICommand* cmd)
	{
		try
		{
			const ClassID cls = (cmd != nil) ? ::GetClass(cmd) : kInvalidClass;
			if (cls == kCloseBookCmdBoss)
			{
				if (gOn && IDThreading::IsMainThreadDomain())
					OnBookAboutToClose();
			}
			// A document opened or made: the session is past its launch, and the Home screen is looked at
			// no more (see gHomeTimer - measured, no visibility message comes while a document
			// is open, so this is the place that sees one). Three more comparisons, and only until then.
			// *Named, not asked of IDocumentCommands::IsOpenCommand (the framework's own question for code acting
			//  on a document command's type, IDocumentCommands.h:118-124): that says yes to
			//  kOpenFileWithWindowCmdBoss too - the open of ANY file with a window, a book's among them - and a
			//  launch with a .indb then ended this window before a single document (measured: the Home screen
			//  stayed over the docks).
			else if (!gHomeWindowOver && (cls == kOpenDocCmdBoss || cls == kNewDocCmdBoss || cls == kOpenDocFromBookCmdBoss)
				&& IDThreading::IsMainThreadDomain())
			{
				EndHomeWindow("a document was opened");
			}
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

CREATE_PMINTERFACE(KFCBookPanelCmdWatch, kKFCBookPanelCmdWatchImpl)

//----------------------------------------------------------------------------------------
// The palette manager service
//----------------------------------------------------------------------------------------

/** Registers the boss for kPaletteMgrService, the service the Book panel's own start-up hangs off. */
class KFCBookPanelServiceProvider : public CServiceProvider
{
public:
	KFCBookPanelServiceProvider(IPMUnknown* boss) : CServiceProvider(boss) {}
	virtual ~KFCBookPanelServiceProvider() {}

	virtual ServiceID GetServiceID() { return kPaletteMgrService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	// Per session and main thread only: what the product's own provider for this service is
	// registered as (Service_Registry_Memory_Dump.txt:2761, kBookPanelStartupShutdownBoss). The main
	// thread is the base class's answer for a provider in a UI plug-in (CServiceProvider.h:54-56), so
	// GetThreadingPolicy is not overridden.
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// An internal name, no string-table key: SetCString with an encoding, the form the header gives for a string
	// that is no key (PMString.h:819-826 - Translate does nothing on it). Not the one-argument SetCString: that is
	// SetKey under another name, among the APIs the header says not to use (PMString.h:952-1021).
	virtual void GetName(PMString* pName) { pName->SetCString("KFCBookPanelService", PMString::kEncodingASCII); }
};

CREATE_PMINTERFACE(KFCBookPanelServiceProvider, kKFCBookPanelServiceProviderImpl)

/** The two moments: the palettes are laid out, and the palettes are about to close. The second is also
    where the Translucent Panel observer stops listening (KFCPanelAlpha.cpp): the session and the panel
    manager are still up there, and by the plug-in's shutdown that observer can no longer be reached. */
class KFCBookPanelPaletteMgrService : public CPMUnknown<IPaletteMgrService>
{
public:
	KFCBookPanelPaletteMgrService(IPMUnknown* boss) : CPMUnknown<IPaletteMgrService>(boss) {}
	virtual ~KFCBookPanelPaletteMgrService() {}

	virtual void PaletteMgrStarted()		{ Start(); }
	virtual void PaletteMgrAboutToShutdown()
	{
		Stop();
		KFCDetachPanelVisibilityObserver();
	}
};

CREATE_PMINTERFACE(KFCBookPanelPaletteMgrService, kKFCBookPanelPaletteMgrServiceImpl)

// End, KFCBookPanelPlacement.cpp.
