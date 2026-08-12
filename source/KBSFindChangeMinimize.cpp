//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Minimizable Find/Change" - the implementation. See KBSFindChangeMinimize.h for why the SDK
//  cannot do this and Win32 can.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KBSFindChangeMinimize.h"
#include "KBSPanelAlpha.h"		// KBSQueryFindChangeWindow - the shared lookup of the dialog's window

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names.
//  (The same order as KBSPanelAlpha.cpp.)
#ifdef WINDOWS
#include <windows.h>
#endif

// The toggle, for this session. *OFF by default; remembered across restarts only through the
// settings file (KBSPanelState.cpp).
static bool16 sFindChangeMinimizable = kFalse;

#ifdef WINDOWS

// ***The window WE changed, and what it looked like before we did.***
//  *ONE set, not a list: a modeless dialog can only be open once at a time, whatever
//   allowMultipleCopies says (IDialogMgr.h:67).
//  *The ORIGINAL values are kept rather than reconstructed. Undoing by clearing the bits we set
//   would be wrong for a window that already carried them - a future build might - and would leave
//   it without styles that are its own.
static HWND     sMinWnd      = nullptr;
static LONG_PTR sMinOldStyle = 0;
static LONG_PTR sMinOldEx    = 0;

// Put the window we changed back as it was, and forget it. One place for it, because there are
// three callers: the toggle going OFF, a different dialog window turning up, and shutdown.
static void KBSRestoreFindChangeStyle()
{
	if (sMinWnd == nullptr)
		return;

	if (::IsWindow(sMinWnd))
	{
		// *****RESTORE IT FIRST IF IT IS MINIMISED.***** Putting WS_EX_TOOLWINDOW back while the
		//   window is iconic takes it off the taskbar - and the taskbar is the only way back to it.
		//   The user would be left with a Find/Change dialog that exists, is not on screen, and
		//   cannot be reached by any means this plug-in offers.
		if (::IsIconic(sMinWnd))
			::ShowWindow(sMinWnd, SW_RESTORE);

		::SetWindowLongPtr(sMinWnd, GWL_STYLE,   sMinOldStyle);
		::SetWindowLongPtr(sMinWnd, GWL_EXSTYLE, sMinOldEx);
		::SetWindowPos(sMinWnd, nullptr, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	}

	// *Dropped whether or not the window was still alive. A handle the OS has recycled must never be
	//  written to on a later pass - it can name a different window by then
	//  (memory/panel-hwnd-from-paletteref.md records the same hazard on the panel side).
	sMinWnd = nullptr;
}

#endif	// WINDOWS

bool16 KBSGetFindChangeMinimizable()
{
	return sFindChangeMinimizable;
}

void KBSSetFindChangeMinimizable(bool16 on)
{
	sFindChangeMinimizable = on;
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
		return kFalse;		// not open; the menu says it applies when the dialog is opened

	if (h == sMinWnd)
		return kTrue;		// already done, to this very window

	// A different window from the one on record: put the old one back BEFORE taking a new record,
	// or the first one keeps our style with nobody left holding its original values.
	KBSRestoreFindChangeStyle();

	sMinOldStyle = ::GetWindowLongPtr(h, GWL_STYLE);
	sMinOldEx    = ::GetWindowLongPtr(h, GWL_EXSTYLE);
	sMinWnd      = h;

	::SetWindowLongPtr(h, GWL_STYLE,   sMinOldStyle | WS_MINIMIZEBOX);
	::SetWindowLongPtr(h, GWL_EXSTYLE, (sMinOldEx & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
	::SetWindowPos(h, nullptr, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	return kTrue;
#else
	return kFalse;
#endif
}

void KBSShutdownFindChangeMinimize()
{
#ifdef WINDOWS
	KBSRestoreFindChangeStyle();
#endif
	sFindChangeMinimizable = kFalse;
}

// End, KBSFindChangeMinimize.cpp.
