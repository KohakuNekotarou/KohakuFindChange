//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "Minimizable Find/Change" - the implementation. See KFCFindChangeMinimize.h for why the SDK
//  cannot do this and Win32 can, and for why the chase at the foot of this file has to exist (it
//  serves Translucent Find/Change too).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// PerformAction - Edit > Find/Change, for a dialog that is closed (KFCShowFindChangeDialog)
#include "IApplication.h"
#include "ISession.h"

// General includes:
#include "FindChangeID.h"		// kFindDialogActionID

// Project includes:
#include "KFCFindChangeMinimize.h"
#include "KFCPanelAlpha.h"		// KFCQueryFindChangeWindow - the shared lookup of the dialog's window;
								// KFCCommitWindowStyle; the translucency toggle the chase applies as well
#include "KFCDiag.h"			// KFC_DIAG_LOG / the "fcmin-decoy" fault switch (test builds only)

// The dialog cannot always be told at the moment we are told about it (the first opening in a session
// can have no panel yet), so it is asked for again once the events have gone round:
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names.
//  (The same order as KFCPanelAlpha.cpp.)
#ifdef WINDOWS
#include <windows.h>
#endif

// The toggle, for this session. *OFF by default; remembered across restarts only through the
// settings file (KFCPanelState.cpp).
static bool16 sFindChangeMinimizable = kFalse;

#ifdef WINDOWS

// The window WE changed, and which of the three flags it already had.
//  *ONE set, not a list: a modeless dialog can only be open once at a time, whatever
//   allowMultipleCopies says (IDialogMgr.h:67).
//
//  ONLY THE BITS WE TOUCH ARE REMEMBERED - NEVER THE WHOLE STYLE WORD.
//   !Saving the whole GWL_STYLE and writing it back on the way out is WRONG in a way that is
//    invisible until it happens: a style word also carries WS_VISIBLE and WS_MINIMIZE, which are
//    STATE, not settings. Writing back a word captured while the dialog was open and restored took
//    the window straight back to "not visible" - MEASURED:
//    the dialog vanished, STYLE 0x94C80000 -> 0x84C80000, and nothing but the missing 0x10000000
//    said why. Microsoft states the same rule from the other side: WS_VISIBLE is changed with
//    ShowWindow, not with SetWindowLong.
//   *So each flag is a bool, and each is put back on top of the CURRENT word. This is also what
//    the translucency side does for WS_EX_LAYERED (KFCRestoreOurFindChangeStyle in
//    KFCPanelAlpha.cpp masks one bit off the value it has just read).
//   *Remembering "did it already have this?" rather than "clear what we set" still protects a
//    window that arrived carrying the flag: a future build might, and it must keep it.
static HWND sMinWnd     = nullptr;
static bool sHadMinBox  = false;	// did it already have WS_MINIMIZEBOX?
static bool sHadToolWin = false;	// did it already have WS_EX_TOOLWINDOW? (in practice it does)
static bool sHadAppWin  = false;	// did it already have WS_EX_APPWINDOW?

// The chase - for both toggles on the dialog's window (see the constants in the header).
static ICallbackTimer* sRetryTimer   = nil;
static int32           sRetriesLeft  = 0;
// **Once the clean-up has run, never build the timer again. The same guard, for the same reason, as
//   sPanelAlphaShutdown in KFCPanelAlpha.cpp: a notification can still be in flight while the
//   plug-in is going down, and it must not leave a booking live against code that is being unloaded.
static bool            sMinimizeShutdown = false;

static uint32 KFCFindChangeChaseProc(void* refPtr);

#ifdef KFC_DIAG
// Fault switch "fcmin-decoy" (test builds only): %TEMP%\kbs-diag-fault-fcmin-decoy holds a
// window handle in hex, and the record is pointed at it just before it is put back - what a handle the
// OS has since given to somebody else's window looks like. A test makes a window of its own for it.
static HWND KFCDiagDecoyWindow()
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return nullptr;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag-fault-fcmin-decoy", temp);
	free(temp);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "r") != 0 || f == nullptr)
		return nullptr;
	unsigned long long value = 0;
	const int read = fscanf_s(f, "%llx", &value);
	fclose(f);
	return (read == 1) ? reinterpret_cast<HWND>(static_cast<uintptr_t>(value)) : nullptr;
}
#endif

// Is the window on record still the Find/Change dialog WE styled?
// A HANDLE IS ONLY A NUMBER, AND THE RECORD OUTLIVES ITS WINDOW (measured). The dialog's window is
//   destroyed on every close (KFCPanelAlpha.cpp's window-list observer has the measurement), but the
//   record is kept until the toggle goes off, the next dialog is styled or the plug-in shuts down - and
//   by then the OS may have given that number to another window, of this process or of any other.
//   IsWindow alone says yes to such a window: with the record pointed at a window of ANOTHER PROCESS (a
//   test build's "fcmin-decoy" switch), switching the toggle off took that window's minimize box away
//   and made it a tool window, off the taskbar.
//   So, Win32 only (shutdown is a caller):
//     . it is still a window of the dialog's kind - THIS process, top level, "DroverLord - Window Class"
//       (KFCIsFindChangeShapedWindow in KFCPanelAlpha.cpp, which the translucency side's restore and
//       the cached handle ask as well)
//     . OUR marks are still on it - what we changed still reads the way we left it
static bool KFCStillOurFindChangeWindow(HWND h)
{
	if (!KFCIsFindChangeShapedWindow(h))
		return false;
	const LONG_PTR ex = ::GetWindowLongPtr(h, GWL_EXSTYLE);
	if (!sHadAppWin && (ex & WS_EX_APPWINDOW) == 0)
		return false;		// we put it on, and it is not there
	if (sHadToolWin && (ex & WS_EX_TOOLWINDOW) != 0)
		return false;		// we took it off, and it is back
	return true;
}

// Put the window we changed back as it was, and forget it. One place for it, because there are
// three callers: the toggle going OFF, a different dialog window turning up, and shutdown.
static void KFCRestoreFindChangeStyle()
{
	if (sMinWnd == nullptr)
		return;
#ifdef KFC_DIAG
	if (HWND decoy = KFCDiagDecoyWindow())
	{
		KFC_DIAG_LOG("FCMIN restore: the record (0x%llx) pointed at the decoy 0x%llx",
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(sMinWnd)),
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(decoy)));
		sMinWnd = decoy;
	}
#endif

	HWND h = sMinWnd;
	// *Forgotten first, and whether or not anything below succeeds. A handle the OS has recycled must
	//  never be written to on a later pass - it can name a different window by then
	//  (memory/panel-hwnd-from-paletteref.md records the same hazard on the panel side).
	sMinWnd = nullptr;

	if (!KFCStillOurFindChangeWindow(h))
	{
		KFC_DIAG_LOG("FCMIN restore: 0x%llx is no longer the dialog we styled - left alone",
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)));
		return;
	}

	// RESTORE IT FIRST IF IT IS MINIMISED. Putting WS_EX_TOOLWINDOW back while the window
	//   is iconic takes it off the taskbar - and the taskbar is the only way back to it. The user
	//   would be left with a Find/Change dialog that exists, is not on screen, and cannot be reached
	//   by any means this plug-in offers.
	if (::IsIconic(h))
		::ShowWindow(h, SW_RESTORE);

	// EACH FLAG ON ITS OWN, LAID ON TOP OF THE CURRENT WORD. Never a saved word written back
	//   wholesale - see the note over sMinWnd for what that costs.
	LONG_PTR st = ::GetWindowLongPtr(h, GWL_STYLE);
	if (!sHadMinBox)
		st &= ~WS_MINIMIZEBOX;
	::SetWindowLongPtr(h, GWL_STYLE, st);

	LONG_PTR ex = ::GetWindowLongPtr(h, GWL_EXSTYLE);
	if (sHadToolWin)
		ex |= WS_EX_TOOLWINDOW;
	if (!sHadAppWin)
		ex &= ~WS_EX_APPWINDOW;
	::SetWindowLongPtr(h, GWL_EXSTYLE, ex);

	KFCCommitWindowStyle(h);	// the frame put back, without pulling the dialog forward
	KFC_DIAG_LOG("FCMIN restore: the styles were put back on 0x%llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)));
}

#endif	// WINDOWS

bool16 KFCGetFindChangeMinimizable()
{
	return sFindChangeMinimizable;
}

void KFCSetFindChangeMinimizable(bool16 on)
{
	sFindChangeMinimizable = on;
#ifdef WINDOWS
	// AND DROP WHAT IS CACHED ABOUT WHERE THE DIALOG IS. A toggle press is exactly the moment when a
	//   "not open", established at some earlier moment, must not be allowed to answer - as the
	//   translucency setter does too. Without it the toggle does nothing whenever the lookup has
	//   already been asked and failed.
	KFCForgetFindChangeWindow();
#endif
}

bool16 KFCApplyFindChangeMinimizable()
{
#ifdef WINDOWS
	if (!sFindChangeMinimizable)
	{
		// OFF: undo ours, and report success - there may be no dialog open at all, and "off" has
		// still been carried out.
		KFCRestoreFindChangeStyle();
		return kTrue;
	}

	HWND h = KFCQueryFindChangeWindow();
	KFC_DIAG_LOG("FCMIN apply: dialog window 0x%llx%s", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)),
		(h != nullptr && h == sMinWnd) ? " (already ours)" : "");
	if (h == nullptr)
		return kFalse;		// not open (or not built yet); the chase below is what answers that

	if (h == sMinWnd)
		return kTrue;		// already done, to this very window

	// NOT WHILE IT IS MINIMISED. Rebuilding the frame of an iconic window with
	//   SWP_FRAMECHANGED was followed once by the window being destroyed outright, and
	//   there is nothing to gain by doing it: a minimised window shows no title bar, so a button
	//   added now could not be seen anyway. Reported as done, NOT as "no window" - "no window" would
	//   set the chase running against a state that will not change on its own.
	//   *The user's way out is the ordinary one: restore the dialog, and switch the toggle again.
	if (::IsIconic(h))
		return kTrue;

	// A different window from the one on record: put the old one back BEFORE taking a new record,
	// or the first one keeps our style with nobody left holding its original values.
	KFCRestoreFindChangeStyle();

	const LONG_PTR st = ::GetWindowLongPtr(h, GWL_STYLE);
	const LONG_PTR ex = ::GetWindowLongPtr(h, GWL_EXSTYLE);
	// Remember only whether each flag was ALREADY there - not the words themselves. See sMinWnd.
	sHadMinBox  = ((st & WS_MINIMIZEBOX)   != 0);
	sHadToolWin = ((ex & WS_EX_TOOLWINDOW) != 0);
	sHadAppWin  = ((ex & WS_EX_APPWINDOW)  != 0);
	sMinWnd     = h;

	::SetWindowLongPtr(h, GWL_STYLE,   st | WS_MINIMIZEBOX);
	::SetWindowLongPtr(h, GWL_EXSTYLE, (ex & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
	KFCCommitWindowStyle(h);
	return kTrue;
#else
	return kFalse;
#endif
}

#ifdef WINDOWS

// Is either toggle that acts on the dialog's window ON?
static bool KFCAnyFindChangeToggleOn()
{
	return sFindChangeMinimizable || KFCGetFindChangeTranslucent();
}

// Put each toggle that is ON on the dialog's window, if the lookup can tell the window. True when it
// could (the toggles are then applied - each one's own Apply decides what that means, "already done" and
// "minimised, not now" included); false when the dialog cannot be told, which is what the chase waits on.
// ONE LOOKUP FOR BOTH. The two features are set
//   going by the same cue and wait on the same window, so the question "is the dialog there yet" is
//   asked once here, not by each feature's own Apply.
static bool KFCApplyFindChangeToggles()
{
	if (KFCQueryFindChangeWindow() == nullptr)
		return false;
	if (KFCGetFindChangeTranslucent())
		KFCApplyFindChangeTranslucency();
	if (sFindChangeMinimizable)
		KFCApplyFindChangeMinimizable();
	return true;
}

// The chase. **There is deliberately no "already booked, do not stack" gate - a broken chain would
//   leave such a flag raised and the feature dead for the rest of the session (the panel side's note
//   on KFCScheduleReapply). ICallbackTimer holds one booking per instance, so
//   StartTimer over a live booking merely replaces it, and re-arming unconditionally debounces as a
//   side effect (the count goes back to the full number every time).
static uint32 KFCFindChangeChaseProc(void* /*refPtr*/)
{
	--sRetriesLeft;

	// *Do not Release here (releasing itself from inside RunTask is self-destruction). Releasing is
	//   in KFCShutdownFindChangeMinimize(), and nowhere else.
	if (!KFCAnyFindChangeToggleOn())
	{
		sRetriesLeft = 0;		// both turned OFF while we were waiting - stop
		return IIdleTask::kEndOfTime;
	}

	// WITHOUT THIS THE CHASE IS A NO-OP. The lookup answers a cached "not open" without
	//   looking again, and "not open" is precisely what it recorded on the try that sent us here.
	//   See KFCForgetFindChangeWindow in KFCPanelAlpha.h.
	KFCForgetFindChangeWindow();

	if (KFCApplyFindChangeToggles())
	{
		KFC_DIAG_LOG("FCCHASE found the dialog with %d tries left", sRetriesLeft);
		sRetriesLeft = 0;		// the window was found and the toggles put on - done
		return IIdleTask::kEndOfTime;
	}
	if (sRetriesLeft <= 0)
	{
		KFC_DIAG_LOG("FCCHASE gave up - no dialog window after %d tries", kKFCFindChangeChaseTries);
		return IIdleTask::kEndOfTime;	// bounded: the dialog is simply not open
	}

	// Ask again after another interval - by the RETURN VALUE, as KFCPanelAlpha's re-apply chain does.
	// NOT A PROMISE, BUT MEASURED. IIdleTask.h:195 reads the return as the delay before running again,
	// while ICallbackTimer.h:42 calls what it registers "a one time only callback", and its one SDK caller
	// re-arms from OUTSIDE the callback (KFCBookWatch.cpp has the whole account). A test build's trace of
	// this chase showed ONE booking and then all 8 runs - the first about 0.48s after the booking, the
	// rest about 50ms apart - so on InDesign 21.0 the return value does re-arm. (The other way - StartTimer
	// from inside, then kEndOfTime - made 8 runs into 2.) It stays an observation of this version, not a
	// contract; WHAT CATCHES IT if a later one stops re-arming:
	// the chase is then the one run the booking makes, and a dialog still not tellable by then gets
	// neither its button nor its alpha this time - the next cue does it: opening the dialog again (the
	// window-list observer calls the chase afresh, KFCPanelAlpha.cpp) or switching a toggle off and on.
	return kKFCFindChangeChaseDelayMillis;
}

#endif	// WINDOWS

void KFCChaseFindChangeWindow()
{
#ifdef WINDOWS
	if (!KFCAnyFindChangeToggleOn())
		return;		// nothing to put on the window

	if (KFCApplyFindChangeToggles())
		return;		// told on the spot - the common case, and no timer is created at all

	// The dialog cannot be told yet. **This is the case the whole chase exists for; see the header.
	if (sMinimizeShutdown)
		return;

	sRetriesLeft = kKFCFindChangeChaseTries;
	if (sRetriesLeft <= 0)
		return;		// the constant is 0 = the chase is turned off
	KFC_DIAG_LOG("FCCHASE no dialog window - %d tries %ums apart booked", kKFCFindChangeChaseTries,
		kKFCFindChangeChaseDelayMillis);

	if (sRetryTimer == nil)
		sRetryTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sRetryTimer == nil)
		return;

	sRetryTimer->StartTimer(KFCFindChangeChaseProc, kKFCFindChangeChaseDelayMillis, nil);
#endif
}

bool16 KFCRestoreMinimizedFindChange()
{
#ifdef WINDOWS
	// Asked afresh - the window list has not changed since the dialog was minimised, so the cache would
	// do, but a menu press is rare and the walk is cheap.
	KFCForgetFindChangeWindow();
	HWND h = KFCQueryFindChangeWindow();
	if (h == nullptr || !::IsIconic(h))
		return kFalse;
	// What the taskbar button does: restore, and with it bring the window forward.
	::ShowWindow(h, SW_RESTORE);
	KFC_DIAG_LOG("FCMIN open: 0x%llx was minimized - restored instead of closed",
		static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)));
	return kTrue;
#else
	return kFalse;
#endif
}

bool16 KFCShowFindChangeDialog()
{
#ifdef WINDOWS
	// Asked afresh, as above: a cached "not open" must not answer for a dialog opened since.
	KFCForgetFindChangeWindow();
	HWND h = KFCQueryFindChangeWindow();
	if (h != nullptr)
	{
		const bool wasMinimized = (::IsIconic(h) != FALSE);
		if (wasMinimized)
			::ShowWindow(h, SW_RESTORE);
		KFC_DIAG_LOG("FCSHOW 0x%llx was open%s", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(h)),
			wasMinimized ? " and minimized - restored" : "");
		return kTrue;
	}
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	if (actionMgr == nil)
		return kFalse;
	actionMgr->PerformAction(session->GetActiveContext(), kFindDialogActionID);
	KFC_DIAG_LOG("FCSHOW closed - opened through kFindDialogActionID");
	return kTrue;
#else
	return kFalse;
#endif
}

void KFCShutdownFindChangeMinimize()
{
#ifdef WINDOWS
	// Order: stop the booking first, then undo the window, then let go of the timer.
	sMinimizeShutdown = true;
	sRetriesLeft = 0;
	if (sRetryTimer != nil)
	{
		sRetryTimer->StopTimer();
		sRetryTimer->Release();
		sRetryTimer = nil;
	}
	KFCRestoreFindChangeStyle();
#endif
	sFindChangeMinimizable = kFalse;
}

// End, KFCFindChangeMinimize.cpp.
