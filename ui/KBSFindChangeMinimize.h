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
//  translucency toggle uses, deliberately not a second copy of that judgement.
//
//========================================================================================

#ifndef __KBSFindChangeMinimize_h__
#define __KBSFindChangeMinimize_h__

#include "BaseType.h"

// How many times, and how far apart, the style is written again after a cue that found NO WINDOW.
//  *****WHY A CHASE IS NEEDED AT ALL.***** The only cue this feature gets is the application's
//    window list saying a window was added - and the dialog can be "open" at that moment while its
//    platform window does not exist yet. KBSQueryFindChangeWindow is written for exactly that state
//    (it does not record the walk as done, so the next ask looks again) - but SOMEBODY HAS TO ASK
//    AGAIN. The translucency side is asked again constantly by its Win32 mouse hook; this side has
//    no such traffic, so without a chase the button simply never appears.
//    !MEASURED, not theorised (2026-08-12): opening the dialog from the same script that switched
//     the toggle on left it unstyled, while doing the two as separate steps applied it in 15ms.
//  *8 x 50ms = about 400ms, the figure the panel side settled on for the same kind of settling
//   (kKBSPanelAlphaReapplyTries in KBSPanelAlpha.h). The count bounds it, so it always stops.
static const int32  kKBSMinimizeRetryTries       = 8;
static const uint32 kKBSMinimizeRetryDelayMillis = 50;

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

// Apply, and if there is no window yet, keep trying for a short while (see the constants above).
// *This is what the window-list observer calls, not KBSApplyFindChangeMinimizable directly.
void	KBSApplyFindChangeMinimizableWithRetry();

// Put the dialog back as it was, stop the chase and release the timer. Called from the plug-in's
// shutdown.
// *A style left on somebody else's window would outlive this plug-in - the same reason
//  KBSShutdownPanelAlpha takes its WS_EX_LAYERED back off. **And ICallbackTimer's callback is a raw
//  function pointer that is not reference counted, so a live booking as this .pln goes down is a
//  crash - the same hazard, and the same remedy, as the panel side's chase.
void	KBSShutdownFindChangeMinimize();

#endif // __KBSFindChangeMinimize_h__
