//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Search the Application Bar's Text with This Panel (Enter)" - the implementation. See
//  KBSAppBarSearchEnter.h for what was measured before any of this was written.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// PerformAction - this panel's Find, run the way the flyout runs it
#include "IActiveContext.h"
#include "IApplication.h"		// QueryActionManager
#include "IPanelMgr.h"			// IsPanelWithWidgetIDShown / ShowPanelByWidgetID - the panel up before it is filled
#include "ISession.h"

// The search runs once the key has gone through the loop - never inside the hook (see RunPendingSearch):
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// General includes:
#include "PMString.h"
#include "WideString.h"

// Project includes:
#include "KBSAppBarSearchEnter.h"
#include "KBSBookPanelLookup.h"	// QueryPanelManager
#include "KBSModelAccess.h"		// KBSRuns()->SetTextQuery - the Find/Change settings are the model half's
#include "KBSResultTree.h"		// ShowStatus - a query that could not be set is said
#include "KFCUIID.h"			// kKBSSearchBookActionID, kKBSPanelWidgetID

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names (KBSPanelAlpha.cpp's order).
#ifdef WINDOWS
#include <windows.h>
#include <string>
#endif

// The toggle, for this session. *OFF by default; remembered across restarts only through the settings file
// (KBSPanelState.cpp), like the toggles beside it.
static bool16 sAppBarSearchEnter = kFalse;

#ifdef WINDOWS

// The longest field text read. The field is a one-line search box; a query longer than this is cut, which a
// search box this narrow does not hold in practice.
static const int32 kKBSAppBarFieldMaxChars = 1024;

static HHOOK           sGetMsgHook = nullptr;
static ICallbackTimer* sRunTimer   = nil;
static std::wstring    sPendingText;		// the field's text at the Return, until the search runs
// **Once the clean-up has run, nothing is booked or hooked again - a key can still be in the loop while the
//   plug-in is going down (sMinimizeShutdown's rule, KBSFindChangeMinimize.cpp).
static bool            sShutdown = false;

static std::wstring ClassOf(HWND w)
{
	wchar_t name[128] = { 0 };
	if (w == nullptr || ::GetClassNameW(w, name, 128) == 0)
		return std::wstring();
	return name;
}

// Is `w` the application bar's search field? A Win32 Edit inside an OWL.ApplicationBarHostView (measured: the
// window tree, docs/ai-notes/indesign-win32-window-tree.md, and both spikes). Asked of a Return only, so it is
// asked fresh each time: a handle remembered from an earlier field could name another window by now.
static bool IsAppBarSearchField(HWND w)
{
	if (ClassOf(w) != L"Edit")
		return false;
	for (HWND p = ::GetParent(w); p != nullptr; p = ::GetParent(p))
	{
		if (ClassOf(p) == L"OWL.ApplicationBarHostView")
			return true;
	}
	return false;
}

static std::wstring FieldText(HWND field)
{
	wchar_t text[kKBSAppBarFieldMaxChars] = { 0 };
	::SendMessageW(field, WM_GETTEXT, kKBSAppBarFieldMaxChars, reinterpret_cast<LPARAM>(text));
	return text;
}

// The search, on the main thread at the next idle - once the Return has gone through the message loop. Not in
// the hook: a search puts up a modal progress bar and processes commands, and a hook procedure is the middle
// of somebody else's GetMessage.
static uint32 RunPendingSearch(void* /*refPtr*/)
{
	std::wstring text;
	text.swap(sPendingText);
	if (sShutdown || !sAppBarSearchEnter || text.empty())
		return IIdleTask::kEndOfTime;

	// The panel up first, so the rows the search draws are seen (kFalse = the key focus stays where it is).
	InterfacePtr<IPanelMgr> panelMgr(KBSBookPanelLookup::QueryPanelManager());
	if (panelMgr != nil && !panelMgr->IsPanelWithWidgetIDShown(kKBSPanelWidgetID))
		panelMgr->ShowPanelByWidgetID(kKBSPanelWidgetID, kFalse);

	PMString query(WideString(text.c_str(), static_cast<int32>(text.size())));
	query.SetTranslatable(kFalse);
	if (!KBSRuns()->SetTextQuery(query))
	{
		PMString msg("The Application Bar's text could not be put into Find/Change - nothing was searched.");
		msg.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(msg);
		return IIdleTask::kEndOfTime;
	}

	// This panel's Find, run as the flyout runs it - the same action, so the same doors, the same progress
	// bar, the same status line (KBSActionComponent, kKBSSearchBookActionID).
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	if (actionMgr != nil)
		actionMgr->PerformAction(session->GetActiveContext(), kKBSSearchBookActionID);
	return IIdleTask::kEndOfTime;
}

// The hook. ON only: a plain Return (no Shift / Ctrl / Alt) reaching the application bar's search field, with
// text in it, is turned into WM_NULL - so InDesign's own Stock / Help search does not start (it starts while it
// processes this WM_KEYDOWN, measured) and TranslateMessage makes no WM_CHAR of it - and the search is booked.
// *An auto-repeat (a held key) is stopped too, but searches only once. *An empty field is left to InDesign.
// *The IME's confirming Return is VK_PROCESSKEY, not VK_RETURN, and passes untouched (measured).
static LRESULT CALLBACK AppBarGetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION && wParam == PM_REMOVE && sAppBarSearchEnter && !sShutdown)
	{
		MSG* const m = reinterpret_cast<MSG*>(lParam);
		if (m->message == WM_KEYDOWN && m->wParam == VK_RETURN
			&& ::GetKeyState(VK_SHIFT) >= 0 && ::GetKeyState(VK_CONTROL) >= 0 && ::GetKeyState(VK_MENU) >= 0
			&& IsAppBarSearchField(m->hwnd))
		{
			const std::wstring text = FieldText(m->hwnd);
			if (!text.empty())
			{
				m->message = WM_NULL;
				const bool repeat = (m->lParam & 0x40000000) != 0;	// the previous key state: already down
				if (!repeat)
				{
					sPendingText = text;
					if (sRunTimer == nil)
						sRunTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
					if (sRunTimer != nil)
						sRunTimer->StartTimer(RunPendingSearch, 1, nil);
				}
			}
		}
	}
	return ::CallNextHookEx(sGetMsgHook, code, wParam, lParam);
}

static void UnhookAppBar()
{
	if (sGetMsgHook != nullptr)
	{
		::UnhookWindowsHookEx(sGetMsgHook);
		sGetMsgHook = nullptr;
	}
}

struct KBSAppBarFieldSearch { HWND found; };

static BOOL CALLBACK FindAppBarField(HWND w, LPARAM lp)
{
	if (IsAppBarSearchField(w))
	{
		reinterpret_cast<KBSAppBarFieldSearch*>(lp)->found = w;
		return FALSE;
	}
	return TRUE;
}

static BOOL CALLBACK FindMainFrame(HWND w, LPARAM lp)
{
	DWORD pid = 0;
	::GetWindowThreadProcessId(w, &pid);
	if (pid == ::GetCurrentProcessId() && ClassOf(w) == L"indesign")
	{
		*reinterpret_cast<HWND*>(lp) = w;
		return FALSE;
	}
	return TRUE;
}

#endif	// WINDOWS

bool16 KBSGetAppBarSearchEnter()
{
	return sAppBarSearchEnter;
}

void KBSSetAppBarSearchEnter(bool16 on)
{
	sAppBarSearchEnter = on;
#ifdef WINDOWS
	if (on && sGetMsgHook == nullptr && !sShutdown)
		sGetMsgHook = ::SetWindowsHookExW(WH_GETMESSAGE, AppBarGetMsgProc, nullptr, ::GetCurrentThreadId());
	else if (!on)
	{
		UnhookAppBar();
		sPendingText.clear();
		if (sRunTimer != nil)
			sRunTimer->StopTimer();
	}
#endif
}

bool16 KBSApplyAppBarSearchEnter()
{
#ifdef WINDOWS
	if (!sAppBarSearchEnter)
		return kTrue;
	HWND main = nullptr;
	::EnumWindows(FindMainFrame, reinterpret_cast<LPARAM>(&main));
	if (main == nullptr)
		return kFalse;
	KBSAppBarFieldSearch s = { nullptr };
	::EnumChildWindows(main, FindAppBarField, reinterpret_cast<LPARAM>(&s));
	return (s.found != nullptr && ::IsWindowVisible(s.found)) ? kTrue : kFalse;
#else
	return kFalse;
#endif
}

void KBSShutdownAppBarSearchEnter()
{
#ifdef WINDOWS
	// Order: nothing new from now on, then the hook off, then the booking stopped and the timer let go.
	sShutdown = true;
	UnhookAppBar();
	sPendingText.clear();
	if (sRunTimer != nil)
	{
		sRunTimer->StopTimer();
		sRunTimer->Release();
		sRunTimer = nil;
	}
#endif
	sAppBarSearchEnter = kFalse;
}

// End, KBSAppBarSearchEnter.cpp.
