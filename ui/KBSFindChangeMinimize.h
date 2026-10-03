//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  The "Minimizable Find/Change" flyout toggle: puts a MINIMIZE BOX on InDesign's OWN
//  Find/Change dialog, so it can be sent to the taskbar instead of being closed.
//
//  *Windows only. On Mac the calls below still exist and do nothing.
//
//  ***Why this needs Win32 at all.*** The SDK cannot do it, and says so:
//    . a window's decorations are fixed when it is created - IWindow::InitWindow(policyBits)
//    . a modeless dialog's standard controls are kCloseWindowControl ALONE (IWindow.h:125)
//    . IWindow::SetWindowPolicy states that for an existing window "only ... kSideTitlebarControl"
//      can be changed (IWindow.h:405), and InitWindow is never called anywhere in the SDK
//  Win32 can, and it takes exactly two bits (measured on the real application, 2026-08-12):
//    . WS_MINIMIZEBOX on its own changes NOTHING THAT CAN BE SEEN
//    . ***taking WS_EX_TOOLWINDOW OFF is what makes the button appear*** - Windows does not draw
//      minimize or maximize on a tool-window frame
//  The dialog's own style, for reference: STYLE 0x94C80000, EXSTYLE 0x00000180
//  (WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW). The title bar turned out to be the OS's own non-client
//  drawing, not something InDesign paints - which is why setting the bits is enough.
//  Full record = memory/window-minimize-presentation-system.md
//
//  WS_EX_APPWINDOW is added as well, so the minimised dialog lands on the TASKBAR (the user's
//  choice, 2026-08-12). Without it Windows leaves a 160x28 title-bar-only window at the bottom
//  left of the screen - the old owned-popup behaviour, measured at L=0 T=700 R=160 B=728.
//
//  The window is found by KBSQueryFindChangeWindow (KBSPanelAlpha.h) - the SAME lookup the
//  translucency toggle uses, deliberately not a second copy of that judgement. And the chase that asks
//  again for a dialog not yet tellable (KBSChaseFindChangeWindow, here) serves both toggles alike.
//
//========================================================================================

#ifndef __KBSFindChangeMinimize_h__
#define __KBSFindChangeMinimize_h__

#include "BaseType.h"

// How many times, and how far apart, the dialog's window is asked for again after a cue that could
// not tell it yet - THE CHASE, which serves BOTH toggles on that window (this one and Translucent
// Find/Change) since 2026-10-03.
//  *****WHY A CHASE IS NEEDED AT ALL.***** The only cue either feature gets is the application's
//    window list saying a window was added - and the Find/Change dialog opened for the FIRST time in a
//    session can be in the list at that moment with NO PANEL SET YET (GetDialogPanel nil - measured
//    2026-10-03, the block 15 recheck; not every first opening, the block 13/14 recheck saw both), so
//    the lookup, which knows the dialog by its panel, cannot tell it. It records "not open" - its
//    negative cache - and answers that until the window list next changes. So SOMEBODY HAS TO FORGET
//    AND ASK AGAIN once the panel is on.
//    !MEASURED twice, not theorised: 2026-08-12, opening the dialog from the same script that switched
//     this toggle on left it unstyled; 2026-10-03 (the block 13/14 recheck T-1), Translucent
//     Find/Change ON alone left the session's first dialog OPAQUE - its Win32 mouse hook does ask
//     again on every move, but what it asks is the lookup, and the lookup answered from that negative
//     cache. Until then this note said the translucency side needed no chase for that very reason.
//  *8 x 50ms = about 400ms, the figure the panel side settled on for the same kind of settling
//   (kKBSPanelAlphaReapplyTries in KBSPanelAlpha.cpp). The count bounds it, so it always stops.
//   (The first run comes 0.5 to 1 second after the booking - see KBSFindChangeChaseProc - and found the
//    first-time dialog on that run in both traces: block 15, and the block 13/14 recheck's GREEN.)
static const int32  kKBSFindChangeChaseTries       = 8;
static const uint32 kKBSFindChangeChaseDelayMillis = 50;

// The toggle's current state (*OFF by default).
bool16	KBSGetFindChangeMinimizable();

// Set the toggle. *This is the flag only - no window is touched. Applying is the call below, which
// the caller makes straight afterwards; the two are kept apart because the settings file restores
// the flag at startup, when there is certainly no dialog to touch.
void	KBSSetFindChangeMinimizable(bool16 on);

// Write the current flag onto the Find/Change dialog's window.
//  ***IT DOES NOT CHECK THE TOGGLE - IT READS IT.*** While OFF it puts back whatever we changed,
//    because that IS what switching off means here.
//  - While ON: returns kFalse when the dialog is not open (so the menu can say so rather than
//    leaving a click with no visible result unexplained), and on Mac.
//  - While OFF: always kTrue on Windows. What is undone is the style WE put on the window WE put it
//    on, which need not be a window that is open now, and may be no window at all.
bool16	KBSApplyFindChangeMinimizable();

// Put BOTH toggles that act on InDesign's Find/Change dialog - this one and Translucent Find/Change
// (KBSApplyFindChangeTranslucency) - on its window, each only if ON; and if the window cannot be told
// yet, keep asking for a short while (see the constants above). Does nothing when both are OFF.
// *This is what the window-list observer calls when the window just added may be the dialog
//  (KBSWindowMayBeFindChange, 2026-10-03); for every other window-list message it calls the two plain
//  Apply functions once. It was KBSApplyFindChangeMinimizableWithRetry, for this toggle alone, until
//  the block 13/14 recheck T-1 (2026-10-03) found the translucency side needing the same chase.
void	KBSChaseFindChangeWindow();

// If the Find/Change dialog is open and MINIMISED, restore it and return kTrue; otherwise kFalse and
// nothing is touched. For the panel's Open Find/Change... (2026-10-03, the user's call - the block 15
// recheck F-2): InDesign's own Edit > Find/Change is a TOGGLE that closes an open dialog, a minimised
// one included (measured), and from KFC's menu item that read as the dialog being thrown away. Not tied
// to the toggle: a minimised dialog is brought back whoever minimised it. Windows only; kFalse on Mac.
bool16	KBSRestoreMinimizedFindChange();

// Put the dialog back as it was, stop the chase and release the timer. Called from the plug-in's
// shutdown.
// *A style left on somebody else's window would outlive this plug-in - the same reason
//  KBSShutdownPanelAlpha takes its WS_EX_LAYERED back off. **And ICallbackTimer's callback is a raw
//  function pointer that is not reference counted, so a live booking as this .pln goes down is a
//  crash - the same hazard, and the same remedy, as the panel side's chase.
void	KBSShutdownFindChangeMinimize();

#endif // __KBSFindChangeMinimize_h__
