//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Minimizable Find/Change" - the implementation. See KBSFindChangeMinimize.h for why the SDK
//  cannot do this and Win32 can, and for why the chase at the foot of this file has to exist.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KBSFindChangeMinimize.h"
#include "KBSPanelAlpha.h"		// KBSQueryFindChangeWindow - the shared lookup of the dialog's window

// The dialog's window can be absent at the moment we are told about it, so the style is written
// again once the events have gone round:
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names.
//  (The same order as KBSPanelAlpha.cpp.)
#ifdef WINDOWS
#include <windows.h>
#endif

// The toggle, for this session. *OFF by default; remembered across restarts only through the
// settings file (KBSPanelState.cpp).
static bool16 sFindChangeMinimizable = kFalse;

#ifdef WINDOWS

// ***The window WE changed, and which of the three flags it already had.***
//  *ONE set, not a list: a modeless dialog can only be open once at a time, whatever
//   allowMultipleCopies says (IDialogMgr.h:67).
//
//  *****ONLY THE BITS WE TOUCH ARE REMEMBERED - NEVER THE WHOLE STYLE WORD.*****
//   !This file saved the whole GWL_STYLE and wrote it back on the way out until 2026-08-12, and
//    that is WRONG in a way that is invisible until it happens: a style word also carries
//    WS_VISIBLE and WS_MINIMIZE, which are STATE, not settings. Writing back a word captured while
//    the dialog was open and restored took the window straight back to "not visible" - MEASURED:
//    the dialog vanished, STYLE 0x94C80000 -> 0x84C80000, and nothing but the missing 0x10000000
//    said why. Microsoft states the same rule from the other side: WS_VISIBLE is changed with
//    ShowWindow, not with SetWindowLong.
//   *So each flag is a bool, and each is put back on top of the CURRENT word. This is also what
//    the translucency side has always done for WS_EX_LAYERED (KBSRestoreOurFindChangeStyle in
//    KBSPanelAlpha.cpp masks one bit off the value it has just read).
//   *Remembering "did it already have this?" rather than "clear what we set" still protects a
//    window that arrived carrying the flag: a future build might, and it must keep it.
static HWND sMinWnd     = nullptr;
static bool sHadMinBox  = false;	// did it already have WS_MINIMIZEBOX?
static bool sHadToolWin = false;	// did it already have WS_EX_TOOLWINDOW? (in practice it does)
static bool sHadAppWin  = false;	// did it already have WS_EX_APPWINDOW?

// The chase (see the constants in the header).
static ICallbackTimer* sRetryTimer   = nil;
static int32           sRetriesLeft  = 0;
// **Once the clean-up has run, never build the timer again. The same guard, for the same reason, as
//   sPanelAlphaShutdown in KBSPanelAlpha.cpp: a notification can still be in flight while the
//   plug-in is going down, and it must not leave a booking live against code that is being unloaded.
static bool            sMinimizeShutdown = false;

static uint32 KBSMinimizeRetryProc(void* refPtr);

// Put the window we changed back as it was, and forget it. One place for it, because there are
// three callers: the toggle going OFF, a different dialog window turning up, and shutdown.
static void KBSRestoreFindChangeStyle()
{
	if (sMinWnd == nullptr)
		return;

	HWND h = sMinWnd;
	// *Forgotten first, and whether or not anything below succeeds. A handle the OS has recycled must
	//  never be written to on a later pass - it can name a different window by then
	//  (memory/panel-hwnd-from-paletteref.md records the same hazard on the panel side).
	sMinWnd = nullptr;

	if (!::IsWindow(h))
		return;

	// *****RESTORE IT FIRST IF IT IS MINIMISED.***** Putting WS_EX_TOOLWINDOW back while the window
	//   is iconic takes it off the taskbar - and the taskbar is the only way back to it. The user
	//   would be left with a Find/Change dialog that exists, is not on screen, and cannot be reached
	//   by any means this plug-in offers.
	if (::IsIconic(h))
		::ShowWindow(h, SW_RESTORE);

	// *****EACH FLAG ON ITS OWN, LAID ON TOP OF THE CURRENT WORD.***** Never a saved word written
	//   back wholesale - see the note over sMinWnd for what that cost when this file did it.
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

	// *SWP_NOACTIVATE is ours, so putting the frame back cannot pull the dialog forward; the other
	//  four are the combination Microsoft's SetWindowPos Remarks prescribe for making a
	//  SetWindowLongPtr style change take effect. Taken from KBSRestoreOurFindChangeStyle, which has
	//  carried the same set since 2026-08-04.
	::SetWindowPos(h, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	::RedrawWindow(h, nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
}

#endif	// WINDOWS

bool16 KBSGetFindChangeMinimizable()
{
	return sFindChangeMinimizable;
}

void KBSSetFindChangeMinimizable(bool16 on)
{
	sFindChangeMinimizable = on;
#ifdef WINDOWS
	// *****AND DROP WHAT IS CACHED ABOUT WHERE THE DIALOG IS.***** A toggle press is exactly the
	//   moment when a "not open", established at some earlier moment, must not be allowed to answer.
	//   The translucency setter has always done this; this one did not until 2026-08-12, and the
	//   result was a toggle that did nothing whenever the lookup had already been asked and failed.
	KBSForgetFindChangeWindow();
#endif
}

bool16 KBSApplyFindChangeMinimizable()
{
#ifdef WINDOWS
	if (!sFindChangeMinimizable)
	{
		// OFF: undo ours, and report success - there may be no dialog open at all, and "off" has
		// still been carried out.
		KBSRestoreFindChangeStyle();
		return kTrue;
	}

	HWND h = KBSQueryFindChangeWindow();
	if (h == nullptr)
		return kFalse;		// not open (or not built yet); the chase below is what answers that

	if (h == sMinWnd)
		return kTrue;		// already done, to this very window

	// *****NOT WHILE IT IS MINIMISED.***** Rebuilding the frame of an iconic window with
	//   SWP_FRAMECHANGED was followed once by the window being destroyed outright (2026-08-12), and
	//   there is nothing to gain by doing it: a minimised window shows no title bar, so a button
	//   added now could not be seen anyway. Reported as done, NOT as "no window" - "no window" would
	//   set the chase running against a state that will not change on its own.
	//   *The user's way out is the ordinary one: restore the dialog, and switch the toggle again.
	if (::IsIconic(h))
		return kTrue;

	// A different window from the one on record: put the old one back BEFORE taking a new record,
	// or the first one keeps our style with nobody left holding its original values.
	KBSRestoreFindChangeStyle();

	const LONG_PTR st = ::GetWindowLongPtr(h, GWL_STYLE);
	const LONG_PTR ex = ::GetWindowLongPtr(h, GWL_EXSTYLE);
	// Remember only whether each flag was ALREADY there - not the words themselves. See sMinWnd.
	sHadMinBox  = ((st & WS_MINIMIZEBOX)   != 0);
	sHadToolWin = ((ex & WS_EX_TOOLWINDOW) != 0);
	sHadAppWin  = ((ex & WS_EX_APPWINDOW)  != 0);
	sMinWnd     = h;

	::SetWindowLongPtr(h, GWL_STYLE,   st | WS_MINIMIZEBOX);
	::SetWindowLongPtr(h, GWL_EXSTYLE, (ex & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
	::SetWindowPos(h, nullptr, 0, 0, 0, 0,
		SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	::RedrawWindow(h, nullptr, nullptr,
		RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME | RDW_UPDATENOW);
	return kTrue;
#else
	return kFalse;
#endif
}

#ifdef WINDOWS

// The chase. **There is deliberately no "already booked, do not stack" gate - the panel side
//   removed one in its 2026-07-29 self-review because a broken chain left the flag raised and the
//   feature dead for the rest of the session. ICallbackTimer holds one booking per instance, so
//   StartTimer over a live booking merely replaces it, and re-arming unconditionally debounces as a
//   side effect (the count goes back to the full number every time).
static uint32 KBSMinimizeRetryProc(void* /*refPtr*/)
{
	--sRetriesLeft;

	// *Do not Release here (releasing itself from inside RunTask is self-destruction). Releasing is
	//   in KBSShutdownFindChangeMinimize(), and nowhere else.
	if (!sFindChangeMinimizable)
	{
		sRetriesLeft = 0;		// turned OFF while we were waiting - stop
		return IIdleTask::kEndOfTime;
	}

	// *****WITHOUT THIS THE CHASE IS A NO-OP.***** The lookup answers a cached "not open" without
	//   looking again, and "not open" is precisely what it recorded on the try that sent us here.
	//   See KBSForgetFindChangeWindow in KBSPanelAlpha.h.
	KBSForgetFindChangeWindow();

	if (KBSApplyFindChangeMinimizable())
	{
		sRetriesLeft = 0;		// the window was found and styled (or is minimised) - done
		return IIdleTask::kEndOfTime;
	}
	if (sRetriesLeft <= 0)
		return IIdleTask::kEndOfTime;	// bounded: the dialog is simply not open

	return kKBSMinimizeRetryDelayMillis;	// ask again after another interval
}

#endif	// WINDOWS

void KBSApplyFindChangeMinimizableWithRetry()
{
#ifdef WINDOWS
	if (KBSApplyFindChangeMinimizable())
		return;		// styled on the spot - the common case, and no timer is created at all

	// No window yet. **This is the case the whole chase exists for; see the header.
	if (sMinimizeShutdown)
		return;

	sRetriesLeft = kKBSMinimizeRetryTries;
	if (sRetriesLeft <= 0)
		return;		// the constant is 0 = the chase is turned off

	if (sRetryTimer == nil)
		sRetryTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sRetryTimer == nil)
		return;

	sRetryTimer->StartTimer(KBSMinimizeRetryProc, kKBSMinimizeRetryDelayMillis, nil);
#endif
}

void KBSShutdownFindChangeMinimize()
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
	KBSRestoreFindChangeStyle();
#endif
	sFindChangeMinimizable = kFalse;
}

// End, KBSFindChangeMinimize.cpp.
