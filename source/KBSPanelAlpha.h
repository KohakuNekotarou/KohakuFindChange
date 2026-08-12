//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  The "Translucent Panel" flyout toggle. Ported from KESCM's KESCMPanelAlpha (2026-08-04),
//  where it shipped in 1.2.0 - the mechanism is not specific to which panel it is pointed at.
//
//  *Windows only. The alpha is put on the panel's window with Win32's
//    SetLayeredWindowAttributes; on Mac the three calls below still exist but
//    KBSApplyPanelTranslucency does nothing.
//  *It only takes effect while the panel is FLOATING (or pulled out of an icon as a drawer).
//    A docked panel is a child of the main frame and cannot be made translucent on its own -
//    the flag is still set, and the moment the panel floats again it applies.
//
//  The measurements this rests on = memory/win32-window-alpha-transparency.md
//                                   docs/ai-notes/win32-window-transparency.md
//
//========================================================================================

#ifndef __KBSPanelAlpha_h__
#define __KBSPanelAlpha_h__

#include "BaseType.h"

// The alpha the panel is drawn with while the toggle is ON (0 = invisible, 255 = opaque).
// *77 is about 30% - the user's figure, settled on in KESCM after 128 (50%) read as too solid.
// Putting the pointer on the panel brings it back to opaque, so the resting state can afford to be
// faint. There is no slider and no steps: changing how faint it is means changing this one line.
// **...but not to 0, and not to anything near it (Microsoft's contract, read 2026-08-11): "hit
//   testing of a layered window is based on the shape and transparency of the window ... the areas
//   of the window ... whose alpha value is zero will let the mouse messages through". At 0 the panel
//   would stop receiving the pointer altogether - and this feature relies on the pointer arriving to
//   put the panel back to opaque, so it would also have no way back. "0 = invisible" above describes
//   what the parameter means, not a value to put here.
static const uint8 kKBSPanelAlphaValue = 77;

// How many times, and how far apart, the alpha is written again AFTER the notification that asked
// for it (measured 2026-07-29 for KESCM).
//   Writing the alpha when kPaletteVisibilityChangedMessage arrives is not enough: InDesign can
//   recreate the top-level window immediately afterwards and the value goes with it (the diagnostic
//   that settled it: the read-back said 128 while an external tool measured 255 - and the window
//   written to was a different HWND from the one that then existed).
//   So the window is chased for a short while after the event. *0 disables the chase entirely.
//   The count and the interval come from a measurement, not a guess: collapsing to an icon settled
//   within 3ms, but dragging a panel out to float did not settle at all inside that - a drag puts a
//   far larger gap between the notification and the final window - so about 400ms is covered.
// (KESCM keeps these in KESCMConstants.h; KBS has no constants header, so they live here with the
//  code that reads them.)
static const int32  kKBSPanelAlphaReapplyTries       = 8;
static const uint32 kKBSPanelAlphaReapplyDelayMillis = 50;	// 50ms x 8 = about 400ms of chasing

// (The panel's SHADOW (OWL.ShadowView) is handled by hiding and showing it, not with an alpha: it
//  is drawn with per-pixel alpha, which Win32 makes exclusive with the uniform kind, so writing an
//  alpha to it once means it never returns to the shadow it was - confirmed by breaking it on the
//  real application, 2026-07-29. Hence no constant for how faint the shadow is.)

// The toggle's current state (*OFF by default).
bool16	KBSGetPanelTranslucent();

// Set the toggle. *No WINDOW is touched here (the two are kept apart because there is a caller, at
// startup, with no panel to touch yet) - but this is not a plain setter either: it puts the Win32
// event hook up, or takes it down when both toggles end up off. Applying is KBSApplyPanelTranslucency.
void	KBSSetPanelTranslucent(bool16 on);

// Write the current flag onto the panel's window.
//  ***IT DOES NOT CHECK THE TOGGLE - THE CALLER MUST.*** While OFF this writes alpha 255 and shows
//    the shadow again, because that IS the restoring the OFF menu item does. Anything that calls it
//    on some other cue (the pointer arriving, the widgets being rebuilt) has to ask
//    KBSGetPanelTranslucent first, or it will cancel the translucency of ANY OTHER panel grouped
//    with this one - a floating group shares one OWL.Dock - and force out a shadow nobody asked for.
//    !Three callers did not, until 2026-08-11, each saying in a comment that OFF was "rejected
//     inside". See the note over KBSPanelRollOver::MouseEnter in the .cpp for the whole account.
//  - Does nothing (and does NOT report an error) when the panel is absent or docked
//  - Callers, all seven: the menu item (KBSActionComponent.cpp), the panel's AutoAttach
//    (KBSPanelTitle.cpp), and five in KBSPanelAlpha.cpp itself - the palette-visibility observer,
//    the chase timer, the Win32 event hook, and KBSPanelRollOver's MouseEnter and MouseLeave.
//    (Named rather than counted: this list said "the menu item, the AutoAttach and the observer"
//     while the .cpp's own account of the same set correctly named six of them.)
//  - Returns kTrue when an alpha actually reached a window; kFalse when there is no panel, when it
//    is docked, or on Mac. The menu uses that to say "it is on" or "it is on but the panel is
//    docked", rather than leaving a click with no visible result unexplained.
bool16	KBSApplyPanelTranslucency();

//----------------------------------------------------------------------------------------
// The same treatment for InDesign's OWN Find/Change dialog (2026-08-04, the user's request).
//
//   Measured on the real application before it was built (work/findchange-window-probe.ps1):
//     class   = "DroverLord - Window Class"   top-level, owner = the main frame ("indesign")
//     EXSTYLE = 0x00000180  = *WS_EX_LAYERED is NOT set, unlike a floating panel's OWL.Dock
//   So the style has to be added by us - and taken off again when the toggle goes OFF, which is
//   the opposite of the panel side, where InDesign's own style must never be touched.
//   Adding it turned out to have no side effects at all: text, frame and every control stayed
//   correct and usable (user's check, 2026-08-04).
//
//   *"DroverLord - Window Class" is a GENERIC class - a document window's canvas is one too, and
//    so is every other dialog - so the class alone cannot identify it. That is why this window is
//    NOT looked for from Win32 at all: the lookup walks the SDK's IWindowList and takes the dialog
//    whose panel answers kFindChangeParentWidgetID - a NUMBER, so no UI language can change it.
//    See the block comment over KBSQueryFindChangeIWindow in KBSPanelAlpha.cpp for the full route.
//
//    !This said "the search adds: top-level, owner is the main frame, and the title is one of the
//     known Find/Change titles" until 2026-08-11. That was the plan the probe above was run for,
//     never the code that followed it: the .cpp rejects a title list by name ("the title is
//     translated, so a list of candidate titles would silently miss on any build nobody thought
//     of"), and the menu command that calls this has always said "never by its title, which is
//     translated" (KBSActionComponent.cpp). A reader of this header alone would have concluded the
//     feature cannot work on a translated build - which is the build this is developed on.
//----------------------------------------------------------------------------------------

// The toggle's current state (*OFF by default).
bool16	KBSGetFindChangeTranslucent();

// Set the toggle. The window is left to KBSApplyFindChangeTranslucency below; what this does do
// besides the flag is drop whatever was cached about where the dialog is (a toggle press is exactly
// when "not open", established at some earlier moment, must not be allowed to answer), and put the
// Win32 event hook up or take it down.
void	KBSSetFindChangeTranslucent(bool16 on);

// Write the current flag onto the Find/Change window.
//  - While ON: returns kFalse when the dialog is not open (so the menu can say so rather than
//    leaving a click with no visible result unexplained), and on Mac
//  - While OFF: always kTrue on Windows. Two separate things happen - the dialog open NOW goes back
//    to opaque, and the WS_EX_LAYERED we added comes off the window WE ADDED IT TO, which need not
//    be the one open now and may be no open window at all. A window that already carried the style
//    is left with it.
//    *Until 2026-08-04 both hung off "is a dialog open", so switching OFF with the dialog closed ran
//     no clean-up at all and left the record standing against a handle the OS can recycle.
bool16	KBSApplyFindChangeTranslucency();

// InDesign's OWN Find/Change dialog's platform window, or nullptr when it is not open.
// *Shared with KBSFindChangeMinimize.cpp (2026-08-12) so that "which window is the Find/Change
//  dialog" is decided in ONE place. It is not a trivial question - the window class is generic and
//  the title is translated - and the answer walks the SDK's window list for the dialog whose panel
//  answers kFindChangeParentWidgetID, a NUMBER. See the block comment over KBSQueryFindChangeIWindow
//  in the .cpp. (KBSQueryFindChangeIWindow itself stays private: nothing outside needs the IWindow.)
// The contract:
//   . the result is CACHED, and the cache is dropped by the window-list observer whenever a window
//     is added or removed - so ask again rather than keeping the handle
//   . ***do not hold the returned HWND across events.*** The OS recycles handles, and a stale one
//     can name somebody else's window (memory/panel-hwnd-from-paletteref.md)
//   . nullptr means "not open", and also "open, but the platform window does not exist yet"
#ifdef WINDOWS
// *HWND is named here WITHOUT pulling windows.h into this header, which five .cpp files include and
//  only two of which have any business with Win32. This is the declaration windows.h itself makes
//  (DECLARE_HANDLE expands to exactly this), so the two can appear in either order.
struct HWND__;
typedef struct HWND__* HWND;

HWND	KBSQueryFindChangeWindow();
#endif

// Start listening for the panel being shown, hidden, docked or floated.
// *Called from TWO places, and safe to call again: KBSStartupShutdown::Startup, and the panel's own
//   AutoAttach (KBSPanelTitle.cpp). !The second one is not belt and braces - the panel manager comes
//   up partway through the application's startup sequence, so at Startup it can still be nil, and
//   that subscription is picked up on the AutoAttach pass instead. Each attachment asks IsAttached
//   first, so repeating the call attaches nothing twice.
//   (Corrected 2026-08-08: this said "called once at startup". The .cpp had it right all along -
//    see the note at KBSAttachPanelVisibilityObserver's panel-manager branch.)
// *How: kPaletteVisibilityChangedMessage, broadcast from kPanelManagerBoss's IID_IPANELMGR subject
//   (identified on a debug build's Spy, 2026-07-29). Two further subjects hang off kAppBoss - see
//   the function itself.
void	KBSAttachPanelVisibilityObserver();

// Undo every attachment the above makes. Called from the plug-in's shutdown, BEFORE
// KBSShutdownPanelAlpha, so that notifications stop before the timer and the hook are torn down.
// *Why it exists (2026-08-08): while attached, the session holds a pointer into this .pln, and a
//   notification arriving during teardown would run the observer in code that is going away. This is
//   the same reasoning - and the same shape - as KBSBookWatchDetach, which this plug-in has always
//   had; until now the two subjects were treated in opposite ways.
void	KBSDetachPanelVisibilityObserver();

// Tear down everything this file has put anywhere. Called from the plug-in's shutdown
// (KBSStartupShutdown::Shutdown). *ICallbackTimer's callback is a raw function pointer that is not
// reference counted, so leaving a booking live while this .pln goes down is a crash. Implemented in
// KBSPanelAlpha.cpp (empty on Mac). In order:
//   . the one-shot timer, and the flag that stops another timer or hook being made afterwards
//   . the Win32 event hook
//   . ***InDesign's own Find/Change dialog, put back as it was*** - the WS_EX_LAYERED on it is OURS,
//     and a style plus an alpha left on a window nobody maintains any more would outlive this
//     plug-in. This one is easy to overlook, being the only thing here that touches somebody else's
//     window (this comment did overlook it until 2026-08-12).
//   . the remembered window handles
void	KBSShutdownPanelAlpha();

#endif // __KBSPanelAlpha_h__
