//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The "Minimizable Find/Change" flyout toggle: puts a MINIMIZE BOX on InDesign's OWN
//  Find/Change dialog, so it can be sent to the taskbar instead of being closed.
//
//  *Windows only. On Mac the calls below still exist and do nothing.
//
//  Why this needs Win32 at all. The SDK cannot do it, and says so:
//    . a window's decorations are fixed when it is created - IWindow::InitWindow(policyBits)
//    . a modeless dialog's standard controls are kCloseWindowControl ALONE (IWindow.h:125)
//    . IWindow::SetWindowPolicy states that for an existing window "only ... kSideTitlebarControl"
//      can be changed (IWindow.h:405), and InitWindow is never called anywhere in the SDK
//  Win32 can, and it takes exactly two bits (measured on the real application):
//    . WS_MINIMIZEBOX on its own changes NOTHING THAT CAN BE SEEN
//    . taking WS_EX_TOOLWINDOW OFF is what makes the button appear - Windows does not draw
//      minimize or maximize on a tool-window frame
//  The dialog's own style, for reference: STYLE 0x94C80000, EXSTYLE 0x00000180
//  (WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW). The title bar turned out to be the OS's own non-client
//  drawing, not something InDesign paints - which is why setting the bits is enough.
//  Full record = memory/window-minimize-presentation-system.md
//
//  WS_EX_APPWINDOW is added as well, so the minimised dialog lands on the TASKBAR (the user's
//  choice). Without it Windows leaves a 160x28 title-bar-only window at the bottom left of the
//  screen - the owned-popup behaviour, measured at L=0 T=700 R=160 B=728.
//
//  The window is found by KFCQueryFindChangeWindow (KFCPanelAlpha.h) - the SAME lookup the
//  translucency toggle uses, deliberately not a second copy of that judgement. And the chase that asks
//  again for a dialog not yet tellable (KFCChaseFindChangeWindow, here) serves both toggles alike.
//
//========================================================================================

#ifndef __KFCFindChangeMinimize_h__
#define __KFCFindChangeMinimize_h__

#include "BaseType.h"

// How many times, and how far apart, the dialog's window is asked for again after a cue that could
// not tell it yet - THE CHASE, which serves BOTH toggles on that window (this one and Translucent
// Find/Change).
//  WHY A CHASE IS NEEDED AT ALL. The only cue either feature gets is the application's window list
//    saying a window was added - and the Find/Change dialog opened for the FIRST time in a session can
//    be in the list at that moment with NO PANEL SET YET (GetDialogPanel nil - measured; not on every
//    first opening, both were seen), so the lookup, which knows the dialog by its panel, cannot tell
//    it. It records "not open" - its negative cache - and answers that until the window list next
//    changes. So SOMEBODY HAS TO FORGET AND ASK AGAIN once the panel is on.
//    !MEASURED twice, not theorised: opening the dialog from the same script that switched this
//     toggle on left it unstyled; and Translucent Find/Change ON alone left the session's first
//     dialog OPAQUE - its Win32 mouse hook does ask again on every move, but what it asks is the
//     lookup, and the lookup answers from that negative cache. (So the translucency side needs the
//     chase too, whatever its hook does.)
//  *8 x 50ms = about 400ms, the figure the panel side settled on for the same kind of settling
//   (kKFCPanelAlphaReapplyTries in KFCPanelAlpha.cpp). The count bounds it, so it always stops.
//   (The first run comes 0.5 to 1 second after the booking - see KFCFindChangeChaseProc - and found the
//    first-time dialog on that run in both traces taken.)
static const int32  kKFCFindChangeChaseTries       = 8;
static const uint32 kKFCFindChangeChaseDelayMillis = 50;

// The toggle's current state (*OFF by default).
bool16	KFCGetFindChangeMinimizable();

// Set the toggle. *This is the flag only - no window is touched. Applying is the call below, which
// the caller makes straight afterwards; the two are kept apart because the settings file restores
// the flag at startup, when there is certainly no dialog to touch.
void	KFCSetFindChangeMinimizable(bool16 on);

// Write the current flag onto the Find/Change dialog's window.
//  IT DOES NOT CHECK THE TOGGLE - IT READS IT. While OFF it puts back whatever we changed,
//    because that IS what switching off means here.
//  - While ON: returns kFalse when the dialog is not open (so the menu can say so rather than
//    leaving a click with no visible result unexplained), and on Mac.
//  - While OFF: always kTrue on Windows. What is undone is the style WE put on the window WE put it
//    on, which need not be a window that is open now, and may be no window at all.
bool16	KFCApplyFindChangeMinimizable();

// Put BOTH toggles that act on InDesign's Find/Change dialog - this one and Translucent Find/Change
// (KFCApplyFindChangeTranslucency) - on its window, each only if ON; and if the window cannot be told
// yet, keep asking for a short while (see the constants above). Does nothing when both are OFF.
// *This is what the window-list observer calls when the window just added may be the dialog
//  (KFCWindowMayBeFindChange); for every other window-list message it calls the two plain Apply
//  functions once. One chase for both toggles - the translucency side needs it as much (see the
//  constants above).
void	KFCChaseFindChangeWindow();

// If the Find/Change dialog is open and MINIMISED, restore it and return kTrue; otherwise kFalse and
// nothing is touched. For the panel's Open Find/Change... (the author's call): InDesign's own
// Edit > Find/Change is a TOGGLE that closes an open dialog, a minimised
// one included (measured), and from KFC's menu item that read as the dialog being thrown away. Not tied
// to the toggle: a minimised dialog is brought back whoever minimised it. Windows only; kFalse on Mac.
bool16	KFCRestoreMinimizedFindChange();

// Put the dialog back as it was, stop the chase and release the timer. Called from the plug-in's
// shutdown.
// *A style left on somebody else's window would outlive this plug-in - the same reason
//  KFCShutdownPanelAlpha takes its WS_EX_LAYERED back off. **And ICallbackTimer's callback is a raw
//  function pointer that is not reference counted, so a live booking as this .pln goes down is a
//  crash - the same hazard, and the same remedy, as the panel side's chase.
void	KFCShutdownFindChangeMinimize();

#endif // __KFCFindChangeMinimize_h__
