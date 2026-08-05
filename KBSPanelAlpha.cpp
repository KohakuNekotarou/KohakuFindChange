//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  The "Translucent Panel" toggle. *Every Win32 dependency in this plug-in is in this one file.
//
//  Ported from KESCM's KESCMPanelAlpha.cpp (2026-08-04). Everything below is what that file
//  established on the real application on 2026-07-29 - see docs/ai-notes/win32-window-transparency.md
//  before changing any of it.
//
//  *The steps:
//    1. find the window with cls == "OWL.Palette" whose title is this panel's, in our own process
//    2. GetAncestor(GA_ROOT) for the top-level window it is on RIGHT NOW
//    3. if that is "indesign" (the main frame) the panel is docked and expanded -> do nothing
//    4. if it is "OWL.Dock" (floating) or "OWL.FrameDrawer" (pulled out of an icon as a drawer),
//       write the alpha with SetLayeredWindowAttributes
//
//  *The OWL.Dock HWND CHANGES when the panel is DOCKED and then floated again: the old window is
//    DESTROYED and a new one is made. Closing and re-opening the panel is different - the same Dock
//    survives, alpha and all. The OWL.Palette HWND does not change in either case. So no window
//    handle is held across calls - it is looked up again.
//    **That pair of facts is also why turning the toggle OFF can never leave a faint window behind,
//    which is the one thing this file's "do nothing while docked" rule looks like it should
//    (measured 2026-08-04 on Release 21.0.2.2, one step at a time, reading the alpha from another
//    process - see docs/ai-notes/win32-window-transparency.md):
//      . OFF while the panel is merely CLOSED reaches the Dock and writes 255 - it is still there
//      . OFF while DOCKED reaches nothing, but there is nothing left to reach: the Dock that
//        carried the 77 was destroyed by the docking, and floating again builds a fresh one at 255
//    *Nor can one panel's alpha reach another's: a Dock belongs to ONE panel (55-56 of them exist
//    from startup, waiting invisible, each naming its own panel), so nothing is ever handed round.
//  *InDesign itself puts WS_EX_LAYERED on OWL.Dock (EXSTYLE = 0x08080000). The style is neither
//    added nor removed here. *Removing it breaks the application's own drawing - to undo, write
//    alpha = 255 and nothing else.
//  *Do NOT put WS_EX_LAYERED on the child window (OWL.Palette): it does not go translucent, the
//    colours break instead (measured).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KBSPanelAlpha.h"		// kKBSPanelAlphaValue and the chase constants
#include "KBSID.h"				// kKBSDisplayName (the window title to look for) + our IIDs / ImplIDs

// For the observer that follows the panel being shown, hidden, docked or floated:
#include "CObserver.h"
#include "ISubject.h"			// AttachObserver / IsAttached
#include "ISession.h"			// GetExecutionContextSession (can be nil during shutdown)
#include "IApplication.h"		// QueryPanelManager
#include "IActiveContext.h"		// where the observer implementation lives (kActiveContextBoss)
#include "IPanelMgr.h"			// IID_IPANELMGR - the subject subscribed to
#include "AppUIID.h"			// *kPaletteVisibilityChangedMessage (AppUIID.h:325) / kWindowAddedMessage (:71)
#include "ShuksanID.h"			// kApplicationSuspendMsg (ShuksanID.h:1151 - another app came forward)

// For InDesign's own Find/Change dialog - found through the SDK, not by its title:
#include "IWindowList.h"		// the application's list of windows (IID_IWINDOWLIST on kAppBoss)
#include "IWindow.h"			// GetSysWindow - the platform window behind an IWindow
#include "IDialog.h"			// GetDialogPanel - what says WHICH dialog this window is
#include "IControlView.h"		// GetWidgetID on that panel
#include "FindChangeID.h"		// kFindChangeParentWidgetID - the panel the Find/Change dialog answers with

// The window is rebuilt AFTER the notification arrives, so the alpha is written again once the
// events have gone round:
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "CreateObject.h"		// ::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// To go opaque again while the pointer is on the panel:
#include "CPMUnknown.h"			// the implementation base
#include "IMouseRollOver.h"		// MouseEnter / MouseOver / MouseLeave (ui/IMouseRollOver.h)

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names.
#ifdef WINDOWS
#include <windows.h>
#endif

// The toggles, for this session. *Both OFF by default. They are remembered across restarts only
// when the user asks - "Save Panel Settings" writes them to KBSPanelState.json (KBSPanelState.cpp).
static bool16 sPanelTranslucent = kFalse;
static bool16 sFindChangeTranslucent = kFalse;

#ifdef WINDOWS

// Is the pointer over the target window (the top-level window the panel is on right now)?
//
// **No flag is kept - it is measured every time it is asked (KESCM's 2026-07-29 change). The
//   earlier version raised and lowered a static flag from IMouseRollOver, and that had two weak
//   points. Measuring has neither, structurally:
//     (a) MouseLeave does not fire when the panel is closed, docked, or another application is
//         switched to WITH the pointer still on it. Miss one and "the pointer is on it" sticks: the
//         toggle is ON and the panel never goes faint again.
//     (b) IMouseRollOver only sees the panel's own WIDGETS, so the tab strip (the band reading
//         "Kohaku Find/Change - Document") and the title band (the << / x band) never reach it -
//         which is exactly where the user grabs the panel.
//
// *The target window contains the tab band, the title band and the panel body, so this one test
//   answers "the pointer is somewhere on the panel" for all of it. **That the tab band cannot be
//   had from the SDK was settled on the real application (2026-07-29 in KESCM): the panel widget's
//   parent chain ends after one step, at kOWLHostedPanelWrapperBoss (0x1645a), whose bbox is the
//   panel body itself = the chrome is outside the widget tree, on the OWL side.
//
// *Do NOT decide this from the rectangle alone (GetWindowRect + PtInRect): a window of ANOTHER
//   APPLICATION on top would still count as "on the panel". The rectangle is a cheap rejection; the
//   answer comes from WindowFromPoint. *Thanks to that rejection, every pointer move away from the
//   panel costs nothing but integer comparisons.
//
// ***** BUT A WINDOW OF OUR OWN ON TOP OF THE PANEL IS STILL THE PANEL. ***** (2026-08-05, on the
//   user's report: "right-clicking a document row turns the opaque panel faint again".) The panel's
//   context menu opens over the panel and answers WindowFromPoint for as long as it is up, so with
//   the plain GA_ROOT == target test the answer became "the pointer is elsewhere" the moment the
//   menu appeared - and showing a window is EVENT_OBJECT_SHOW, which is inside the hook's range, so
//   the faint alpha was written immediately. A tooltip over the panel is the same shape.
//
//   The test is therefore "the pointer is inside the panel's rectangle, and what is under it belongs
//   to INDESIGN" rather than "...belongs to this very window". What that gives up is the case where
//   ANOTHER of InDesign's own windows covers the panel: the panel then stays opaque while hidden
//   behind it. Nothing is on screen to look wrong, and the next pointer move off the rectangle puts
//   it right - which is a far smaller price than the panel flickering every time its own menu opens.
static bool KBSCursorOverWindow(HWND target)
{
	if (target == nullptr)
		return false;

	POINT pt;
	if (!::GetCursorPos(&pt))
		return false;

	RECT rc;
	if (!::GetWindowRect(target, &rc) || !::PtInRect(&rc, pt))
		return false;		// outside the rectangle = certainly not on it (most mouse moves end here)

	HWND under = ::WindowFromPoint(pt);
	if (under == nullptr)
		return false;

	const HWND root = ::GetAncestor(under, GA_ROOT);
	if (root == target)
		return true;		// the panel itself - the ordinary answer

	// Ours as well? Then it is something the panel put up over itself (its context menu, a tooltip),
	// and the pointer has not left the panel. Asked of the TOP-LEVEL window: a menu is its own
	// top-level window, owned by the application, not a child of the panel.
	DWORD pid = 0;
	::GetWindowThreadProcessId(root, &pid);
	return (pid == ::GetCurrentProcessId());
}

// *The alpha that SHOULD be on the window: faint only while the toggle is ON and the pointer is
//   elsewhere. Keep this in one place - both the applying side and the hook's test read it.
//   *While OFF it does not even look at where the pointer is (this feature does nothing at all when
//   it is off - the user's rule, 2026-07-29).
//   Not defined on Mac, where nothing applies an alpha at all (an unused-function warning otherwise).
static uint8 KBSEffectiveAlpha(HWND target)
{
	if (!sPanelTranslucent)
		return 255;

	return KBSCursorOverWindow(target) ? 255 : kKBSPanelAlphaValue;
}

// *The Win32 event hook goes up and comes down with the toggle (bodies further down).
static void KBSInstallWinEventHook();
static void KBSRemoveWinEventHook();
// *Drops what was cached about where InDesign's own Find/Change dialog is (body further down).
static void KBSForgetFindChangeWindow();
#endif

bool16 KBSGetPanelTranslucent()
{
	return sPanelTranslucent;
}

#ifdef WINDOWS
// The hook serves BOTH toggles, so it goes up while either is ON and comes down when both are OFF -
// never left resident.
//   For the PANEL it is the only cue there is: neither "docked and expanded <-> floating" nor
//   "drawer -> floating" broadcasts a single SDK notification (confirmed on debug and release builds
//   of 2026).
//   For FIND/CHANGE the window itself is followed through the SDK (kWindowAddedMessage), so the hook
//   is needed only for the POINTER - going solid while the cursor is over the dialog.
static void KBSUpdateWinEventHook()
{
	if (sPanelTranslucent || sFindChangeTranslucent)
		KBSInstallWinEventHook();
	else
		KBSRemoveWinEventHook();
}
#endif

void KBSSetPanelTranslucent(bool16 on)
{
	sPanelTranslucent = on;

#ifdef WINDOWS
	KBSUpdateWinEventHook();
#endif
}

bool16 KBSGetFindChangeTranslucent()
{
	return sFindChangeTranslucent;
}

void KBSSetFindChangeTranslucent(bool16 on)
{
	sFindChangeTranslucent = on;

#ifdef WINDOWS
	// *Whatever was cached about the dialog is dropped as the toggle moves. The cache can be holding
	//  "not open", established at some earlier moment, and a toggle press is precisely when the user
	//  expects the state as it is NOW to be looked at - so the negative cache must not answer for it.
	//  One walk of the window list, once per press.
	KBSForgetFindChangeWindow();
	KBSUpdateWinEventHook();
#endif
}

#ifdef WINDOWS

// *The window title to look for. Built from kKBSDisplayName, which is THE definition of the panel's
//   name - both string tables put it under kKBSPanelTitleKey - so this is locale independent.
//   *If the panel's name in the .fr is ever changed without this following it, the feature stops
//   working silently.
#define KBS_WIDEN_(x)	L ## x
#define KBS_WIDEN(x)	KBS_WIDEN_(x)
static const wchar_t* const kKBSPaletteWindowTitle = KBS_WIDEN(kKBSDisplayName);

// **The ONE difference from KESCM's copy of this file: the title is matched as a PREFIX, not for
//   equality. KESCM's tab reads its display name and nothing else; KBS renames its own tab to carry
//   the scope - "Kohaku Find/Change - Book" / " - Document" (KBSPanelTitle::Update) - so an equality
//   test would match only while the tab happened to be back at its plain name.
//   Both forms start with kKBSDisplayName, so a prefix test catches the panel whether or not the
//   window title follows the tab label. Nothing else in this process is an OWL.Palette whose title
//   starts with this plug-in's display name.
static bool KBSTitleMatches(const wchar_t* title, const wchar_t* wanted)
{
	return ::wcsncmp(title, wanted, ::wcslen(wanted)) == 0;
}

// Passed through EnumWindows / EnumChildWindows.
struct KBSFindPaletteCtx
{
	const wchar_t*	fTitle;
	DWORD			fPid;
	HWND			fFound;
};

static bool KBSWindowMatches(HWND h, KBSFindPaletteCtx* ctx)
{
	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(h, cls, 64) == 0)
		return false;
	if (::wcscmp(cls, L"OWL.Palette") != 0)
		return false;

	wchar_t title[256] = { 0 };
	::GetWindowTextW(h, title, 256);
	return KBSTitleMatches(title, ctx->fTitle);
}

static BOOL CALLBACK KBSEnumChildProc(HWND h, LPARAM lp)
{
	KBSFindPaletteCtx* ctx = reinterpret_cast<KBSFindPaletteCtx*>(lp);
	if (KBSWindowMatches(h, ctx))
	{
		ctx->fFound = h;
		return FALSE;		// found it - stop
	}
	return TRUE;
}

static BOOL CALLBACK KBSEnumTopProc(HWND h, LPARAM lp)
{
	KBSFindPaletteCtx* ctx = reinterpret_cast<KBSFindPaletteCtx*>(lp);

	DWORD pid = 0;
	::GetWindowThreadProcessId(h, &pid);
	if (pid != ctx->fPid)
		return TRUE;		// other processes' windows are not ours to look at

	if (KBSWindowMatches(h, ctx))
	{
		ctx->fFound = h;
		return FALSE;
	}
	::EnumChildWindows(h, KBSEnumChildProc, lp);
	return (ctx->fFound == nullptr) ? TRUE : FALSE;
}

// Find the OWL.Palette HWND by the panel's name. nullptr when it is not there.
static HWND KBSFindPaletteWindow(const wchar_t* title)
{
	KBSFindPaletteCtx ctx;
	ctx.fTitle = title;
	ctx.fPid   = ::GetCurrentProcessId();
	ctx.fFound = nullptr;

	::EnumWindows(KBSEnumTopProc, reinterpret_cast<LPARAM>(&ctx));
	return ctx.fFound;
}

// *Remember the OWL.Palette that was found. Closing and re-opening the panel does not change its
//   HWND (what changes is the parent OWL.Dock), so the cache holds.
//   Why it is here: kPaletteVisibilityChangedMessage, which is subscribed to below, fires several
//   times over merely opening one document (measured 2026-07-29). Walking every window with
//   EnumWindows each time would be waste.
static HWND sPaletteWnd = nullptr;

// The panel window, cache first. *The OS reuses handles, so a live handle is not enough - the class
//   name and the title are checked too before it is used (still far cheaper than walking every window).
static HWND KBSQueryPaletteWindow()
{
	KBSFindPaletteCtx ctx;
	ctx.fTitle = kKBSPaletteWindowTitle;
	ctx.fPid   = ::GetCurrentProcessId();
	ctx.fFound = nullptr;

	if (sPaletteWnd != nullptr && ::IsWindow(sPaletteWnd) && KBSWindowMatches(sPaletteWnd, &ctx))
		return sPaletteWnd;

	sPaletteWnd = KBSFindPaletteWindow(kKBSPaletteWindowTitle);
	return sPaletteWnd;
}

// Is this window's class the one expected?
static bool KBSClassIs(HWND h, const wchar_t* wanted)
{
	if (h == nullptr)
		return false;
	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(h, cls, 64) == 0)
		return false;
	return ::wcscmp(cls, wanted) == 0;
}

// The top-level window the panel is on right now - but only when it is one that can be made
// translucent on its own. nullptr while the panel is docked and expanded (GA_ROOT is the main frame).
//
// *GA_ROOT has three answers (measured on the Pages panel in all three states, 2026-07-29; the
//   OWL.Palette HWND stays the same throughout):
//     "indesign"        docked and expanded (attached to the main window). EXSTYLE = 0x00000100
//                       = no WS_EX_LAYERED, and putting it there would take the whole application
//                       with it -> cannot be controlled on its own
//     "OWL.Dock"        floating. EXSTYLE = 0x08080000
//     "OWL.FrameDrawer" pulled out of an icon as a drawer. EXSTYLE = 0x08080000
//   InDesign itself put WS_EX_LAYERED on the latter two, so they are treated identically.
// !Testing for "OWL.Dock" alone silently leaves the drawer out (which is what KESCM did until
//   2026-07-29).
static HWND KBSQueryTranslucentTarget(HWND palette)
{
	if (palette == nullptr)
		return nullptr;

	HWND root = ::GetAncestor(palette, GA_ROOT);
	if (root == nullptr || root == palette)
		return nullptr;

	wchar_t cls[64] = { 0 };
	if (::GetClassNameW(root, cls, 64) == 0)
		return nullptr;

	if (::wcscmp(cls, L"OWL.Dock") == 0 || ::wcscmp(cls, L"OWL.FrameDrawer") == 0)
		return root;

	return nullptr;		// "indesign" = the main frame = docked and expanded
}

#endif // WINDOWS

bool16 KBSApplyPanelTranslucency()
{
#ifdef WINDOWS
	HWND palette = KBSQueryPaletteWindow();
	HWND target  = KBSQueryTranslucentTarget(palette);
	if (target == nullptr)
		return kFalse;		// no panel / docked and expanded -> nothing to do

	// *Opaque again while the pointer is on it (KBSEffectiveAlpha measures where it is). That counts
	//   the tab band and the title band too, because the target window contains them.
	const BYTE alpha = KBSEffectiveAlpha(target);

	// *WS_EX_LAYERED is InDesign's own doing here, so it is left alone.
	const BOOL ok = ::SetLayeredWindowAttributes(target, 0, alpha, LWA_ALPHA);

	// *The SHADOW (OWL.ShadowView) is dealt with as well. It is a separate top-level window - the
	//   Dock's owner - so making only the Dock faint leaves the shadow solid, and that band alone
	//   looks heavy (reported on KESCM, 2026-07-29: "it looks right while dragging, but the shadow
	//   is dark once I let go" = no shadow is drawn during a drag, only when the move is committed).
	//   While translucent, the shadow is hidden outright.
	//
	// **Do NOT use SetLayeredWindowAttributes for it (broken on the real application to find out,
	//   2026-07-29): the shadow is drawn with per-pixel alpha (that is what makes it soft), and Win32
	//   makes uniform alpha and per-pixel alpha exclusive - set the former once and it will not go
	//   back to per-pixel drawing even at 255, leaving an unnatural block of a shadow when the toggle
	//   goes OFF. Showing and hiding does not touch how it is drawn, so it is safe.
	//   ?SW_SHOWNA = show without activating (the shadow window is WS_EX_NOACTIVATE; it must not
	//     come forward).
	//   ?When a drawer's ("OWL.FrameDrawer") owner is not a ShadowView, the test below just skips.
	//   **The shadow follows the TOGGLE, not the alpha (KESCM's 2026-07-29 fix). While ON, the
	//     shadow stays hidden even when the pointer has made the panel opaque.
	//     !Why (a defect found on the real application): a panel is moved by its tab band or title
	//       band - which is where the pointer IS, so it is opaque there - and following the alpha
	//       would therefore SHOW THE SHADOW MID-DRAG. InDesign does not move a shadow during a drag
	//       (it draws it when the move is committed), so one forced out now is left behind at the
	//       old position.
	//     *Side benefit: no more flicker as the shadow blinks in and out with every pass of the
	//       pointer over the panel.
	const bool16 hideShadow = KBSGetPanelTranslucent();
	HWND shadow = ::GetWindow(target, GW_OWNER);
	if (KBSClassIs(shadow, L"OWL.ShadowView"))
		::ShowWindow(shadow, hideShadow ? SW_HIDE : SW_SHOWNA);

	return ok ? kTrue : kFalse;
#else
	return kFalse;		// Mac: there is no way to do this, so "it was not applied", always
#endif
}

//========================================================================================
// InDesign's OWN Find/Change dialog (2026-08-04, the user's request)
//
//   *The window is found through the SDK, NOT by its title. Two reasons the Win32 route that works
//    for the panel does not work here:
//      . the class "DroverLord - Window Class" is GENERIC - a document window's canvas is one, and
//        so is every other dialog
//      . the title is translated ("Suchen/Ersetzen" on a German build), so a list of candidate
//        titles would silently miss on any build nobody thought of
//
//   The route, established from a Spy trace on a debug build (2026-08-04):
//      IApplication -> IID_IWINDOWLIST -> every window
//        -> Query IID_IDIALOG on it        (IDialog.h: it sits on the same boss as the window)
//        -> GetDialogPanel()->GetWidgetID() == kFindChangeParentWidgetID
//           (FindChangeID.h:512 = kFindChangePrefix + 30, i.e. 18718. A NUMBER, so no language can
//            change it.)
//           *MEASURED, not assumed. The first attempt used kKillerFindDialogWidgetID (+56 = 18744),
//            which reads like the obvious candidate and is WRONG: the diagnostic below reported the
//            open dialog as wid=18718 while the search wanted 18744, so nothing was ever found
//            (2026-08-04). "Killer Find" is Find/Change's internal name and there are several widget
//            ids carrying it - the one the DIALOG PANEL answers with is this one.
//        -> IWindow::GetSysWindow()        = the HWND (SysWindow IS HWND on Windows, WSysType.h:69)
//
//   *Unlike the panel, WS_EX_LAYERED is NOT already set (measured: EXSTYLE 0x00000180), so it is
//    added here - and removed again on OFF, but only from the window WE added it to. Adding it
//    turned out to have no side effects: text, frame and every control stayed correct and usable
//    (user's check on the real application, 2026-08-04).
//========================================================================================

#ifdef WINDOWS

// The Find/Change dialog's IWindow, or nil when it is not open.
// *GetNthWindow hands back a pointer that is NOT addref'd, so nothing is released here.
static IWindow* KBSQueryFindChangeIWindow()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;

	InterfacePtr<IWindowList> windows(app, IID_IWINDOWLIST);
	if (windows == nil)
		return nil;

	const int32 count = windows->WindowCount();
	for (int32 i = 0; i < count; ++i)
	{
		IWindow* win = windows->GetNthWindow(i);
		if (win == nil)
			continue;

		// Only dialogs answer this - document windows and palettes drop out here.
		InterfacePtr<IDialog> dlg(win, IID_IDIALOG);
		if (dlg == nil)
			continue;

		IControlView* panel = dlg->GetDialogPanel();
		if (panel != nil && panel->GetWidgetID() == kFindChangeParentWidgetID)
			return win;
	}
	return nil;
}

// The HWND, cached. *The cache matters: the Win32 hook below asks on every mouse move (60-100 a
// second), and walking the window list with a QueryInterface per window each time would be waste.
// It is dropped whenever a window is added or removed - the observer at the foot of this file does
// that - so a handle the OS has recycled cannot be used against a different window.
static HWND sFcWnd = nullptr;

// **Has the window list already been walked since it last changed? (2026-08-04 audit.)
//   !Why this exists: the cache above only holds a window that WAS FOUND, so "the dialog is not
//     open" was re-established from scratch on every ask - and the ask comes from the Win32 hook,
//     which fires on every cursor move (60-100 a second) and on every window event (1477 measured
//     for one drag). A closed dialog therefore meant walking IWindowList with a QueryInterface per
//     window, thousands of times a second, FROM INSIDE A WIN32 CALLBACK - which is the one place in
//     this file that reached into the model at all (the panel side is pure Win32 by design). With a
//     restored "ON" it started during the application's own startup sequence.
//   *Cleared by KBSForgetFindChangeWindow, which the window-list observer calls the moment a window
//     is added or removed - so a dialog that opens is looked up again at once, not left unseen.
static bool16 sFcLookedUp = kFalse;

// nullptr when the dialog is not open, or when the SDK has no platform window for it (a document
// window answers nil to GetSysWindow, so this is not merely theoretical).
static HWND KBSQueryFindChangeWindow()
{
	if (sFcWnd != nullptr && ::IsWindow(sFcWnd))
		return sFcWnd;

	sFcWnd = nullptr;
	if (sFcLookedUp)
		return nullptr;		// looked already, found nothing, and nothing has changed since

	IWindow* win = KBSQueryFindChangeIWindow();
	if (win == nil)
	{
		sFcLookedUp = kTrue;	// the dialog is not open. Nothing to find until the list changes.
		return nullptr;
	}

	// **The dialog is open but has no platform window YET - which can be the state at the very
	//   moment kWindowAddedMessage arrives. The walk is deliberately NOT recorded as done here: the
	//   next ask has to look again, or the dialog would sit opaque until something else happened to
	//   change the window list. This is the case the negative cache above must not swallow.
	HWND h = win->GetSysWindow();
	if (h == nullptr || !::IsWindow(h))
		return nullptr;

	sFcLookedUp = kTrue;
	sFcWnd = h;
	return h;
}

// Called when the window list changes: the next ask looks the dialog up again.
static void KBSForgetFindChangeWindow()
{
	sFcWnd = nullptr;
	sFcLookedUp = kFalse;
}

// *The window WE put WS_EX_LAYERED on. Turning the toggle off removes the style only from this one:
//  a window that already carried it (a future build might) must keep it, or its own drawing breaks -
//  which is exactly the mistake the panel side is written to avoid.
static HWND sFcStyledWnd = nullptr;

// Put the window we styled back as it was, and forget it. One place for it, because there are three
// callers and all three used to be able to skip it (2026-08-04 audit):
//   . the toggle going OFF - which used to return early when the dialog was CLOSED, leaving the
//     record standing while the status line said "off."
//   . the toggle applying to a DIFFERENT window (the dialog closed and re-opened while ON) - which
//     used to overwrite the record and leave our style on a window nobody maintains any more
//   . plug-in shutdown
// **A stale record is not merely untidy: the OS re-uses HWNDs, so acting on one later can take
//   WS_EX_LAYERED off SOMEBODY ELSE'S window - and this file's own header says removing it breaks
//   the application's drawing.
//
// *The handle is therefore verified before it is touched - and verified with WIN32 ONLY, no
//   IWindowList and no IApplication, because shutdown is one of the callers and the model is the
//   last thing that should be reached into there. What the three tests rest on:
//     . WS_EX_LAYERED is still set = OUR OWN mark is still on it. A recycled handle belongs to a
//       window created since, whose EXSTYLE starts clean (this is the test that really carries it -
//       we only ever record a window that did NOT have the style, so its presence is our doing).
//     . the class is still "DroverLord - Window Class" (what a Find/Change dialog answers)
//     . it is still a top-level window (a document window's canvas shares that class but is a child)
static void KBSRestoreOurFindChangeStyle()
{
	if (sFcStyledWnd == nullptr)
		return;

	HWND h = sFcStyledWnd;
	sFcStyledWnd = nullptr;		// forgotten either way: a window that fails the tests is not ours

	if (!::IsWindow(h))
		return;
	if ((::GetWindowLongPtr(h, GWL_EXSTYLE) & WS_EX_LAYERED) == 0)
		return;
	if (!KBSClassIs(h, L"DroverLord - Window Class") || ::GetAncestor(h, GA_ROOT) != h)
		return;

	::SetLayeredWindowAttributes(h, 0, 255, LWA_ALPHA);
	::SetWindowLongPtr(h, GWL_EXSTYLE, ::GetWindowLongPtr(h, GWL_EXSTYLE) & ~WS_EX_LAYERED);
	::SetWindowPos(h, nullptr, 0, 0, 0, 0,
				   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	::RedrawWindow(h, nullptr, nullptr,
				   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

#endif // WINDOWS

bool16 KBSApplyFindChangeTranslucency()
{
#ifdef WINDOWS
	// OFF is dealt with first, because it has work to do whether or not the dialog is open.
	// **It used to return early on "no dialog", so switching the toggle off with the dialog CLOSED
	//   never ran the clean-up at all: the status line said "off." while our WS_EX_LAYERED stayed
	//   recorded against a window the OS could later hand to somebody else (2026-08-04 audit).
	if (!sFindChangeTranslucent)
	{
		// The dialog open NOW goes opaque. It need not be the window we styled - the dialog can have
		// been closed and re-opened while the toggle was on - so both are dealt with, separately.
		HWND open = KBSQueryFindChangeWindow();
		if (open != nullptr)
			::SetLayeredWindowAttributes(open, 0, 255, LWA_ALPHA);

		KBSRestoreOurFindChangeStyle();
		return kTrue;			// "off" always succeeds: nothing of ours is left set either way
	}

	HWND h = KBSQueryFindChangeWindow();
	if (h == nullptr)
		return kFalse;			// the dialog is not open - the menu says so rather than doing nothing

	const LONG_PTR ex = ::GetWindowLongPtr(h, GWL_EXSTYLE);
	if ((ex & WS_EX_LAYERED) == 0)
	{
		// *A record for a DIFFERENT window means the dialog was closed and re-opened while the toggle
		//  was on. Put that one back BEFORE taking this one: overwriting the record would leave our
		//  style on a window with nothing left that remembers it.
		if (sFcStyledWnd != h)
			KBSRestoreOurFindChangeStyle();

		::SetWindowLongPtr(h, GWL_EXSTYLE, ex | WS_EX_LAYERED);
		sFcStyledWnd = h;		// ours to undo
	}

	// Solid again while the pointer is on it - the same rule as the panel, and the same test.
	const BYTE alpha = KBSCursorOverWindow(h) ? 255 : kKBSPanelAlphaValue;
	return ::SetLayeredWindowAttributes(h, 0, alpha, LWA_ALPHA) ? kTrue : kFalse;
#else
	return kFalse;		// Mac: no way to do this
#endif
}

// (A diagnostic that listed every window with its dialog panel's WidgetID and its GetSysWindow
//  stood here while the search was being got right, and was removed once it had done its job
//  (2026-08-04). What it established is written into the comments above; if the window search ever
//  needs measuring again, it is one walk of IWindowList appending those two numbers per window.)

//========================================================================================
// The chase - not losing to the window being rebuilt
//
//   *Why it is needed (measured 2026-07-29): applying the alpha the moment the observer below
//     receives kPaletteVisibilityChangedMessage is not enough - InDesign can recreate the top-level
//     window right afterwards and throw the alpha away with it. The symptom is "pulling the panel
//     out of an icon comes back opaque, but toggling OFF then ON works".
//     *The diagnostic settled it - the read-back right after applying said rb=128 (a success) while
//     an external tool measured 255 afterwards, AND the HWND applied to (dk=0x5B0BF0) was not the
//     window that then existed (0x21656). So the value was not overwritten: the window was replaced.
//   *The fix: as well as applying on the notification, apply again to "whatever GA_ROOT is now"
//     once the events have gone round. A handful of tries while the windows settle
//     (kKBSPanelAlphaReapplyTries). Bounded by the count, so it always stops.
//   !ICallbackTimer's callback is a raw function pointer that is not reference counted, so leaving
//     a booking live while this .pln goes down is a crash -> KBSShutdownPanelAlpha() always stops
//     and releases it.
//========================================================================================

#ifdef WINDOWS

static ICallbackTimer* sReapplyTimer = nil;
static int32           sReapplyLeft  = 0;			// tries left (0 = stop; the runaway guard)

static uint32 KBSReapplyTimerProc(void* refPtr);

// Called from the notification side. (Re)starts the chase while the windows settle.
// **There is deliberately no "already booked, do not stack" gate (KESCM had one, sReapplyPending,
//   and removed it in its 2026-07-29 self-review): if the chain below ever broke, that flag stayed
//   raised and THIS FUNCTION became a permanent no-op - the translucency could never be re-applied
//   again for the rest of the session. ICallbackTimer holds one booking per instance, so calling
//   StartTimer over a live booking merely replaces it. Re-arming unconditionally on every
//   notification works whatever the timer does underneath (and since the count goes back to
//   kKBSPanelAlphaReapplyTries each time, it debounces as a side effect).
static void KBSScheduleReapply()
{
	sReapplyLeft = kKBSPanelAlphaReapplyTries;	// back to the full count on every notification
	if (sReapplyLeft <= 0)
		return;		// the constant is 0 = the chase is turned off

	if (sReapplyTimer == nil)
		sReapplyTimer = (ICallbackTimer*)::CreateObject(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sReapplyTimer == nil)
		return;

	sReapplyTimer->StartTimer(KBSReapplyTimerProc, kKBSPanelAlphaReapplyDelayMillis, nil);
}

static uint32 KBSReapplyTimerProc(void* /*refPtr*/)
{
	--sReapplyLeft;

	// *Do not Release here (releasing itself from inside RunTask is self-destruction). Releasing is
	//   in KBSShutdownPanelAlpha(), and nowhere else.
	if (!KBSGetPanelTranslucent())
	{
		sReapplyLeft = 0;		// turned OFF - stop chasing
		return IIdleTask::kEndOfTime;
	}

	KBSApplyPanelTranslucency();

	// **The chain is made with the RETURN VALUE (KESCM's 2026-07-29 fix). It used to call StartTimer
	//   again from in here and then return kEndOfTime, which cancelled the booking it had just made:
	//   8 runs became 2 (measured rp=2). The return value IS the reschedule, so returning the delay
	//   is how it continues.
	//   !ICallbackTimer's published contract is one-shot ("register a one time only callback"), so
	//     this chain rests on an observed behaviour rather than a promise. Even where it does not
	//     hold, the next notification re-arms unconditionally in KBSScheduleReapply, so it can never
	//     get stuck at "never runs again".
	if (sReapplyLeft > 0)
		return kKBSPanelAlphaReapplyDelayMillis;

	// **The return value is IIdleTask::RunTask's reschedule. **0 means "call me again immediately"**,
	//   not "stop" (returning 0 has frozen InDesign before, in KESCM's tracker). To stop: kEndOfTime.
	return IIdleTask::kEndOfTime;
}

//========================================================================================
// **The Win32 event hook - the only way to catch the transitions the SDK does not announce
//
//   Established by measurement (2026-07-29, agreeing on debug and release builds of 2026):
//     . kPaletteVisibilityChangedMessage fires when VISIBILITY changes, as its name says. Opening,
//       closing, collapsing to an icon and drawer-expanding all fire it; the transitions that only
//       change WHERE the panel is (docked-expanded <-> floating, drawer -> floating) do not.
//     . kDockedPaletteAreaChangedByUserMsg did fire on 2025 but does NOT on 2026 (and it goes to
//       kAppBoss, not kPanelManagerBoss).
//     . Riding on the view recalculation (kFitInViewCmdBoss and friends) was considered too, but
//       pulling out of a drawer does not change the dock's width, so nothing happens there either.
//   -> The only cue left is the Win32 fact that OWL.Palette's parent is being changed.
//
//   *Our own process only, plus WINEVENT_OUTOFCONTEXT (no DLL injected into anyone), so the blast
//     radius is closed.
//   *Up only while ON; always taken down on OFF and at shutdown (a leaked hook is a leaked resource).
//========================================================================================

static HWINEVENTHOOK sWinEventHook = nullptr;

static void CALLBACK KBSWinEventProc(HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND /*hwnd*/,
									 LONG idObject, LONG idChild,
									 DWORD /*thread*/, DWORD /*time*/)
{
	// Only two kinds are of interest:
	//   (1) events about a WINDOW (OBJID_WINDOW) = a window rebuilt or moved. The original quarry.
	//   *(2) the CURSOR moving (OBJID_CURSOR) = the mouse went somewhere. Taking this is what makes
	//     the panel opaque when the pointer is on the TAB BAND or the TITLE BAND, which are outside
	//     the widget tree and so out of IMouseRollOver's reach. No extra hook and no extra timer -
	//     this is something already arriving at this hook that used to be thrown away.
	//   !Child elements (idChild != CHILDID_SELF) are rejected for WINDOW events only: a cursor
	//     event can carry the cursor's state in idChild, and rejecting those would lose them.
	const bool isWindowEvent = (idObject == OBJID_WINDOW && idChild == CHILDID_SELF);
	const bool isCursorEvent = (idObject == OBJID_CURSOR);
	if (!isWindowEvent && !isCursorEvent)
		return;

	// ---- Find/Change: the pointer moving is all this hook is needed for here (the window itself is
	//      followed through kWindowAddedMessage). Only acts when what is on the window differs from
	//      what should be, so the volume of events does no harm.
	if (sFindChangeTranslucent)
	{
		HWND fc = KBSQueryFindChangeWindow();
		if (fc != nullptr)
		{
			const BYTE fcWant = KBSCursorOverWindow(fc) ? 255 : kKBSPanelAlphaValue;
			BYTE  fcCur = 0;
			DWORD fcKey = 0, fcFlags = 0;
			if (!::GetLayeredWindowAttributes(fc, &fcKey, &fcCur, &fcFlags) || fcCur != fcWant)
				KBSApplyFindChangeTranslucency();
		}
	}

	// ---- the panel. *This feature only does anything while the toggle is ON (the hook is not even
	//      up when both are OFF, but anything in flight as it comes down is rejected here).
	if (!KBSGetPanelTranslucent() || sPaletteWnd == nullptr)
		return;

	// **Even a cached handle is checked to still be OUR panel. The OS reuses HWNDs, so the same
	//   value can be handed to another window after this panel is closed. Following GA_ROOT without
	//   checking would make SOMEBODY ELSE'S PANEL translucent (and the hook stays up until the toggle
	//   goes OFF, so events keep arriving here after our panel is gone).
	//   !No full window walk (EnumWindows) here: if the cache is stale it is simply dropped, and
	//     looking it up again is left to the notification and Apply paths (KBSQueryPaletteWindow).
	//     This callback fires a great deal, so each pass is kept cheap.
	if (!::IsWindow(sPaletteWnd))
	{
		sPaletteWnd = nullptr;		// stale (panel closed etc). From here the line above returns at once
		return;
	}

	// *The class name / title check is done for WINDOW events only. Running GetClassNameW +
	//   GetWindowTextW on every cursor move (60-100 a second) would be waste.
	//   !Why that is safe: for an HWND to be reused by another window, that window has to be created
	//     and SHOWN. Being shown is EVENT_OBJECT_SHOW = a window event, so a swap always passes
	//     through this check at the moment it happens. A window that is never shown is rejected by
	//     KBSQueryTranslucentTarget anyway, as neither "OWL.Dock" nor "OWL.FrameDrawer".
	if (isWindowEvent)
	{
		KBSFindPaletteCtx ctx;
		ctx.fTitle = kKBSPaletteWindowTitle;
		ctx.fPid   = ::GetCurrentProcessId();
		ctx.fFound = nullptr;
		if (!KBSWindowMatches(sPaletteWnd, &ctx))
		{
			sPaletteWnd = nullptr;
			return;
		}
	}

	// **This started out filtered on "hwnd == sPaletteWnd", and **not one event got through**
	//   (measured hk=1/0): neither PARENTCHANGE nor LOCATIONCHANGE is ever sent for OWL.Palette,
	//   because the system does not always raise those for a child window being moved.
	//   -> So the sender is not looked at. "An event arrived, so look up our panel's current
	//     top-level window and write the alpha if it is not what it should be." The test is a
	//     GetAncestor and an attribute read, and it returns at once when nothing is out of place, so
	//     the volume does no harm.
	HWND target = KBSQueryTranslucentTarget(sPaletteWnd);
	if (target == nullptr)
		return;					// docked and expanded = not ours to touch

	// **This is called many times over during a move. Only go on to the real work when the state is
	//   NOT what it should be (measured: 1477 events, 1 actual write).

	// (1) is the alpha what it should be? (with the pointer on it, "what it should be" is opaque)
	const BYTE want = KBSEffectiveAlpha(target);
	BYTE  cur = 0;
	DWORD key = 0, flags = 0;
	const bool16 alphaOk = (::GetLayeredWindowAttributes(target, &key, &cur, &flags) && cur == want) ? kTrue : kFalse;

	// (2) is the shadow (OWL.ShadowView) shown or hidden as it should be?
	//   *InDesign puts the shadow back when the panel is dragged. Watching the alpha alone leaves
	//     "the shadow came back and it looks heavy" standing (found on the real application, 2026-07-29).
	bool16 shadowOk = kTrue;
	HWND   shadow   = ::GetWindow(target, GW_OWNER);
	if (KBSClassIs(shadow, L"OWL.ShadowView"))
	{
		const bool16 visible = ::IsWindowVisible(shadow) ? kTrue : kFalse;
		// *What it should be follows the TOGGLE, not the alpha (keep this in step with the applying
		//   side above). Only ON gets this far, so it should always be hidden.
		shadowOk = (visible == kFalse);
	}

	if (alphaOk && shadowOk)
		return;					// both as they should be = nothing to do

	KBSApplyPanelTranslucency();

	// *The chase (8 x 50ms) is for WINDOW events only. It exists to follow a window being rebuilt
	//   and taking the alpha with it, so running it for a mere cursor move is pure waste (and the
	//   cursor moves constantly, which would mean re-arming the chain over and over).
	if (isWindowEvent)
		KBSScheduleReapply();
}

static void KBSInstallWinEventHook()
{
	if (sWinEventHook != nullptr)
		return;		// already up

	// *The range is SHOW (0x8002) to LOCATIONCHANGE (0x800B). Hooking PARENTCHANGE (0x800F) alone
	//   was tried first and **never fired once** (measured hk=1/0) - OWL rearranges its windows by
	//   some means that is not SetParent. A panel that moves must raise LOCATIONCHANGE, so the range
	//   is taken out to there.
	//   !This range fires a great deal, for other windows too. The callback narrows it twice: our
	//     panel's window, AND the alpha not being what it should be.
	sWinEventHook = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_LOCATIONCHANGE,
									  nullptr,						// no hook DLL (a function in this process)
									  KBSWinEventProc,
									  ::GetCurrentProcessId(), 0,	// *our own process, all its threads
									  WINEVENT_OUTOFCONTEXT);		// *nothing injected
}

static void KBSRemoveWinEventHook()
{
	if (sWinEventHook != nullptr)
	{
		::UnhookWinEvent(sWinEventHook);
		sWinEventHook = nullptr;
	}
}

void KBSShutdownPanelAlpha()
{
	// The safety net at plug-in shutdown. Nothing is dereferenced - only stopped and released, which
	// is safe even mid-teardown.
	KBSRemoveWinEventHook();		// *a hook still up as the .pln goes down is dangerous

	// Put InDesign's own Find/Change dialog back. The WS_EX_LAYERED on it is OURS, and a style plus
	// an alpha left on a window nobody maintains any more would outlive this plug-in.
	// *Deliberately Win32 only, from the remembered handle - no IWindowList, no IApplication. During
	//  a controlled shutdown the model is the last thing that should be reached into, which is why
	//  KBSRestoreOurFindChangeStyle verifies the handle with Win32 alone.
	KBSRestoreOurFindChangeStyle();
	sFcWnd = nullptr;
	sFcLookedUp = kFalse;
	sPaletteWnd = nullptr;

	if (sReapplyTimer != nil)
	{
		sReapplyTimer->StopTimer();
		sReapplyTimer->Release();
		sReapplyTimer = nil;
	}
	sReapplyLeft = 0;
}

#else	// Mac: nothing is ever applied, so there is nothing to book and nothing to clean up

static void KBSScheduleReapply() {}
static void KBSForgetFindChangeWindow() {}
void        KBSShutdownPanelAlpha() {}

#endif // WINDOWS

//========================================================================================
// Following the panel being opened, closed, docked or floated
//
//   *The translucency is put on "the top-level window right now", so it is lost whenever that
//     window is rebuilt: (a) closing and re-opening the panel (b) switching between docked and
//     floating (c) pulling it out of an icon to float (d) drawer-expanding it from its icon.
//     **All four are caught by the one notification below (measured with Spy, 2026-07-29), so
//     while the toggle is ON it re-applies itself.
//
//   *Which notification was settled on a debug build's Spy (2026-07-29):
//       kPaletteVisibilityChangedMessage @ kPanelManagerBoss (IID_IPANELMGR)
//     It fires on every docking change. InDesign's own listeners (kLibraryPanelWindowObserverBoss,
//     kBookPaletteWindowObserverBoss) take it on a plain IID_IOBSERVER as well.
//     **Every one of (a)-(d) runs in the same order: the widgets are rebuilt (46 observers
//       re-attached) and THEN this message arrives (measured). So by the time Update is called the
//       widgets are back, and applying there is right.
//     !kDockedPaletteAreaChangedMsg, picked as a candidate beforehand, never fired once (= no use).
//     !kPanelChangedMessage (widgetid.h) is a different thing: CPanelControlData sends it for a
//       change of CHILD WIDGETS, nothing to do with a palette's visibility (measured: it does not
//       fire when a panel is opened or closed).
//     !There is NO notification dedicated to "a panel was opened". Nothing corresponds to
//       kAboutToClosePaletteMsg, which fires just before one closes - opening, docking changes and
//       icon restores are all in this single message.
//
//   *The observer implementation is aggregated onto kActiveContextBoss in the .fr - the proven
//     arrangement KESCM uses for its own three observers.
//   *It is never explicitly detached at shutdown (the application and the boss it lives on have the
//     session's lifetime, and detaching is itself a crash risk).
//========================================================================================

//========================================================================================
// Opaque again while the pointer is on the panel (IMouseRollOver)
//
//   *The point: translucency is for seeing through the panel while it is in the way, but reading
//     and working want it solid. The pointer arriving takes it off, the pointer leaving puts it back.
//   *How: IMouseRollOver (ui/IMouseRollOver.h) is the public interface for giving a widget roll-over
//     behaviour - MouseEnter / MouseOver / MouseLeave. It is aggregated onto the panel boss
//     (kKBSPanelWidgetBoss) as IID_IMOUSEROLLOVER in the .fr.
//     !**Leaving it out of the factory list (KBSFactoryList.h) means it is silently never called**
//       - CREATE_PMINTERFACE alone is not enough. Suspect that first if it stops working.
//   *The SDK contains no usage example of IMouseRollOver at all; KESCM established which bosses
//     implement it from a dump of the real object model (kRollOverIconButtonBoss family,
//     kPanelWithRolloverWidgetBoss, kClickableTextWidgetBoss family, kGIFPlayerWidgetBoss) =
//     **the side that CALLS MouseEnter is the widget's own implementation**, so moving this onto
//     another widget means checking there is a caller there first.
//   !Its reach ends at the panel's own widgets. It does not fire over the title bar or the tab band
//     (OWL chrome does not put mouse events through the app dispatcher).
//
//   **Which is why it holds no state: whether the pointer is on the panel is measured, every time,
//     by KBSCursorOverWindow. This class is only a supplementary TRIGGER saying "the mouse moved,
//     write the alpha again". Holding no flag, missing an event on either side cannot leave the
//     state stuck.
//========================================================================================

/** Re-applies the translucency when the pointer enters or leaves the panel (holds no state - it is
    a supplementary trigger). */
class KBSPanelRollOver : public CPMUnknown<IMouseRollOver>
{
public:
	KBSPanelRollOver(IPMUnknown* boss) : CPMUnknown<IMouseRollOver>(boss) {}
	~KBSPanelRollOver() {}

	virtual void	MouseEnter(const PMPoint& localMousePos);
	virtual void	MouseOver(const PMPoint& localMousePos);
	virtual void	MouseLeave();
	virtual bool8	IsMouseOver() const;
	virtual PMPoint	GetMouseOverPosition() const	{ return fLastPos; }

private:
	PMPoint	fLastPos;
};

CREATE_PMINTERFACE(KBSPanelRollOver, kKBSPanelRollOverImpl)

void KBSPanelRollOver::MouseEnter(const PMPoint& localMousePos)
{
	fLastPos = localMousePos;
	KBSApplyPanelTranslucency();		// -> measures, and goes opaque (rejected inside while OFF)
}

void KBSPanelRollOver::MouseOver(const PMPoint& localMousePos)
{
	// *Called on every move. Only note the position - do not write to the window (Enter has already
	//   made it opaque, so hitting SetLayeredWindowAttributes again each time buys nothing).
	fLastPos = localMousePos;
}

void KBSPanelRollOver::MouseLeave()
{
	KBSApplyPanelTranslucency();		// -> measures, and goes back to translucent
}

bool8 KBSPanelRollOver::IsMouseOver() const
{
	// *No flag is held, so it is measured on the spot - which is what this interface's contract asks
	//   for ("is the pointer on it NOW").
#ifdef WINDOWS
	// *Nothing is measured while the toggle is OFF. This AddIn exists for the translucency toggle,
	//   and while it is off nobody uses the answer - whereas KBSQueryPaletteWindow walks every window
	//   with EnumWindows when its cache is stale, and people not using the feature should not pay for
	//   it (the same rule KBSEffectiveAlpha follows in not even looking at the cursor while OFF).
	if (!sPanelTranslucent)
		return kFalse;
	return KBSCursorOverWindow(KBSQueryTranslucentTarget(KBSQueryPaletteWindow())) ? kTrue : kFalse;
#else
	return kFalse;
#endif
}

/** Re-applies the translucency when the panel's visibility changes. Subscribed to the panel
    manager's subject (and to the application's, for the two messages noted below). */
class KBSPanelVisibilityObserver : public CObserver
{
public:
	KBSPanelVisibilityObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSPANELVISIBILITYOBSERVER) {}
	~KBSPanelVisibilityObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KBSPanelVisibilityObserver, kKBSPanelVisibilityObserverImpl)

void KBSPanelVisibilityObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
{
	// **Two subjects are subscribed to (both established on a debug build's Spy, 2026-07-29):
	//   (1) kPaletteVisibilityChangedMessage @ kPanelManagerBoss / IID_IPANELMGR
	//     = the panel opened, closed, restored from an icon or drawer-expanded. Arrives just after
	//       the widgets are rebuilt.
	//     *As its name says, only when VISIBILITY changes - not for transitions that merely move it.
	//   (2) kDockedPaletteAreaChangedByUserMsg @ kAppBoss / IID_IAPPLICATION
	//     = dragging it out of the dock to float.
	//     !**It fires on 2025 but NOT on 2026** (measured). Nothing on 2026 may depend on it; it is
	//       kept for running on 2025. On 2026 the Win32 hook above is what does this job.
	//     !It goes to kAppBoss, not kPanelManagerBoss. Believing for a long time that it "never
	//       fires" was a matter of listening in the wrong place.
	//   !kApplicationResumeMsg / kApplicationSuspendMsg come through kAppBoss / IID_IAPPLICATION too,
	//     so theChange must always be tested.
	const bool16 isPaletteMsg = (protocol == IID_IPANELMGR    && theChange == kPaletteVisibilityChangedMessage);
	const bool16 isDockMsg    = (protocol == IID_IAPPLICATION && theChange == kDockedPaletteAreaChangedByUserMsg);
	// *(3) the application went to the back (kApplicationSuspendMsg).
	//   !Without it: leaving the pointer on the tab band or title band and moving the mouse out to
	//     another application means no more cursor events reach our own-process-only Win32 hook, so
	//     **it stays stuck opaque** (leaving the panel BODY is caught by IMouseRollOver's MouseLeave,
	//     but the chrome is not covered by that at all). Applying once here measures "not on it" and
	//     it goes faint again.
	//   *This writes one alpha and touches neither the model nor the UI = safe to run while the
	//     application is being deactivated.
	//   **It applies to InDesign's OWN Find/Change dialog just as much (2026-08-04): the pointer can
	//     be left anywhere on it and the window has no MouseLeave of ours at all, so the suspend is
	//     the only cue there is. See where it is acted on below.
	const bool16 isSuspendMsg = (protocol == IID_IAPPLICATION && theChange == kApplicationSuspendMsg);

	// *(4) The application's WINDOW LIST changed - a window was opened or closed (kAppBoss /
	//   IID_IWINDOWLIST). This is how InDesign's own Find/Change dialog is followed, and it replaces
	//   what would otherwise have to be guesswork on Win32 events: opening the dialog broadcasts
	//   kWindowAddedMessage, closing it broadcasts kRemoveWindowMessage (both read off a Spy trace on
	//   a debug build, 2026-08-04).
	//   The cached HWND is dropped either way - the dialog's window is gone, or a new one exists that
	//   might be it - and the alpha is put on the (new) dialog when the toggle is ON.
	if (protocol == IID_IWINDOWLIST && (theChange == kWindowAddedMessage || theChange == kRemoveWindowMessage))
	{
		KBSForgetFindChangeWindow();
		if (KBSGetFindChangeTranslucent())
			KBSApplyFindChangeTranslucency();
		return;		// nothing here concerns the panel
	}

	if (!isPaletteMsg && !isDockMsg && !isSuspendMsg)
		return;

	// **The suspend concerns BOTH windows, so it is answered here - in front of the panel's own guard
	//   below (2026-08-04 audit).
	//   !What was wrong: that guard ("nothing to do while the PANEL toggle is off") stood ahead of
	//     everything, so with only Translucent Find/Change switched on the suspend reached nothing at
	//     all - and the dialog was left STUCK OPAQUE in precisely the case the panel side documents
	//     for itself just above: the pointer is on the window, the mouse moves out to another
	//     application, and not one further cursor event reaches our own-process-only Win32 hook.
	//   *Nothing else here needs saying twice: opening and closing the dialog is followed through the
	//     window list (case 4 above), and the panel's own transitions cannot move the dialog.
	if (isSuspendMsg && KBSGetFindChangeTranslucent())
		KBSApplyFindChangeTranslucency();

	// *Nothing to do while OFF. This notification fires several times over merely opening one
	//   document (measured), so people not using the feature are not made to walk the window list.
	if (!KBSGetPanelTranslucent())
		return;

	KBSApplyPanelTranslucency();

	// *The alpha written just now is thrown away if InDesign rebuilds the window straight afterwards
	//   (measured). Apply again to "the window as it is then", once the events have gone round.
	//   *Not after a mere Suspend, though - no window changed there, so there is nothing to chase.
	if (!isSuspendMsg)
		KBSScheduleReapply();
}

void KBSAttachPanelVisibilityObserver()
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKBSPANELVISIBILITYOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	// *The panel manager comes up partway through the application's own startup sequence (there is a
	//   kPanelMgrHasStartedMsg for it), so this can be nil when called from a startup service.
	//   **That is no longer a reason to give up on the rest (2026-08-04 audit). Returning early here
	//     took the two APPLICATION subjects below with it - and one of them is what follows InDesign's
	//     own Find/Change dialog, which has nothing to do with the panel manager. The panel's
	//     AutoAttach calls this again (KBSPanelTitle.cpp), so the palette subject is picked up then;
	//     nothing was calling it again on behalf of the window list.
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr != nil)
	{
		InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
		if (subject != nil &&
			!subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSPANELVISIBILITYOBSERVER))
		{
			subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSPANELVISIBILITYOBSERVER);
		}
	}

	// *The second subject = kAppBoss / IID_IAPPLICATION.
	//   "Docked and expanded -> dragged out to float" does not reach the panel manager; on 2025 it
	//   arrives here as kDockedPaletteAreaChangedByUserMsg. !**It does not on 2026**, where the Win32
	//   hook is what covers it. This is the 2025 fallback.
	InterfacePtr<ISubject> appSubject(app, IID_ISUBJECT);
	if (appSubject != nil &&
		!appSubject->IsAttached(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKBSPANELVISIBILITYOBSERVER))
	{
		appSubject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IAPPLICATION, IID_IKBSPANELVISIBILITYOBSERVER);
	}

	// *The third subject = kAppBoss / IID_IWINDOWLIST, the application's list of windows. This is
	//   what says that InDesign's own Find/Change dialog has just been opened or closed
	//   (kWindowAddedMessage / kRemoveWindowMessage). Same boss as above, different protocol - so it
	//   is a second, separate attachment.
	if (appSubject != nil &&
		!appSubject->IsAttached(ISubject::kRegularAttachment, obs, IID_IWINDOWLIST, IID_IKBSPANELVISIBILITYOBSERVER))
	{
		appSubject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IWINDOWLIST, IID_IKBSPANELVISIBILITYOBSERVER);
	}
}

// End, KBSPanelAlpha.cpp.
