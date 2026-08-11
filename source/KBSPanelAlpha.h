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

// Set the toggle. *Updates the flag only - the window is not touched here (the two are kept apart
// because there is a caller, at startup, with no panel to touch yet).
void	KBSSetPanelTranslucent(bool16 on);

// Write the current flag onto the panel's window.
//  - Does nothing (and does NOT report an error) when the panel is absent or docked
//  - Callers: the menu item (KBSActionComponent.cpp), and the panel's AutoAttach and the
//    palette-visibility observer below
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

// Set the toggle. Updates the flag; the window is left to KBSApplyFindChangeTranslucency below.
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

// Tear down the one-shot timer and the Win32 event hook. Called from the plug-in's shutdown
// (KBSStartupShutdown::Shutdown). *ICallbackTimer's callback is a raw function pointer that is not
// reference counted, so leaving a booking live while this .pln goes down is a crash. Implemented in
// KBSPanelAlpha.cpp (empty on Mac).
void	KBSShutdownPanelAlpha();

#endif // __KBSPanelAlpha_h__
