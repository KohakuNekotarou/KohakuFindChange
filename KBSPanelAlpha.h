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
//    so is every other dialog - so the class alone cannot identify it. The search adds: top-level,
//    owner is the main frame, and the title is one of the known Find/Change titles.
//----------------------------------------------------------------------------------------

// The toggle's current state (*OFF by default).
bool16	KBSGetFindChangeTranslucent();

// Set the toggle. Updates the flag; the window is left to KBSApplyFindChangeTranslucency below.
void	KBSSetFindChangeTranslucent(bool16 on);

// Write the current flag onto the Find/Change window.
//  - Returns kFalse when the dialog is not open (so the menu can say so rather than doing nothing
//    visible), and on Mac
//  - Turning it OFF also removes the WS_EX_LAYERED we added, but ONLY from the window we added it
//    to - a window that already had the style is left with it
bool16	KBSApplyFindChangeTranslucency();

// Start listening for the panel being shown, hidden, docked or floated.
// *Called once at startup (KBSStartupShutdown::Startup). After that, floating the panel again or
//   re-opening it re-applies the translucency by itself while the toggle is ON.
// *How: kPaletteVisibilityChangedMessage, broadcast from kPanelManagerBoss's IID_IPANELMGR subject
//   (identified on a debug build's Spy, 2026-07-29).
void	KBSAttachPanelVisibilityObserver();

// Tear down the one-shot timer and the Win32 event hook. Called from the plug-in's shutdown
// (KBSStartupShutdown::Shutdown). *ICallbackTimer's callback is a raw function pointer that is not
// reference counted, so leaving a booking live while this .pln goes down is a crash. Implemented in
// KBSPanelAlpha.cpp (empty on Mac).
void	KBSShutdownPanelAlpha();

#endif // __KBSPanelAlpha_h__
