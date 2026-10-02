//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ***** SPIKE - THROWAWAY, ON BRANCH spike/2026-10-02-appbar-menu ONLY (2026-10-02, the user's request:
//  ***** "try the menu thing in KBS"). ***** The question: can KBS add an item to the application bar's
//  search field menu (the triangle: Adobe Stock / Adobe Help), learn that it was chosen, and read the
//  field's text? The field is a real Win32 Edit (OWL.ApplicationBarHostView -> ... -> Edit) and the menu's
//  nature is not known. Two thread hooks on the main thread log what a menu does, to
//  %TEMP%\kbs-appbar-spike.txt: every popup's items when it opens, the highlight, and any command.
//  A popup holding an item with "Stock" in it gets one item of ours appended; choosing it logs the field.
//  Nothing here is meant to ship.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

namespace
{
	const UINT kSpikeItemID = 0x7F4B;		// an id nobody else in a 16-bit menu id space is likely to use
	HHOOK gCallWndHook = nullptr;
	HHOOK gGetMsgHook = nullptr;

	void SpikeLog(const wchar_t* fmt, ...)
	{
		wchar_t path[MAX_PATH] = { 0 };
		if (::GetTempPathW(MAX_PATH, path) == 0)
			return;
		wcscat_s(path, L"kbs-appbar-spike.txt");
		FILE* f = nullptr;
		if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || f == nullptr)
			return;
		SYSTEMTIME t;
		::GetLocalTime(&t);
		fwprintf(f, L"%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		va_list ap;
		va_start(ap, fmt);
		vfwprintf(f, fmt, ap);
		va_end(ap);
		fputwc(L'\n', f);
		fclose(f);
	}

	std::wstring ClassOf(HWND w)
	{
		wchar_t name[128] = { 0 };
		if (w == nullptr || ::GetClassNameW(w, name, 128) == 0)
			return L"?";
		return name;
	}

	bool HasAncestorOfClass(HWND w, const wchar_t* cls)
	{
		for (HWND p = ::GetParent(w); p != nullptr; p = ::GetParent(p))
			if (ClassOf(p) == cls)
				return true;
		return false;
	}

	struct EditSearch { HWND found; };

	BOOL CALLBACK FindAppBarEdit(HWND w, LPARAM lp)
	{
		EditSearch* s = reinterpret_cast<EditSearch*>(lp);
		if (ClassOf(w) == L"Edit" && HasAncestorOfClass(w, L"OWL.ApplicationBarHostView"))
		{
			s->found = w;
			return FALSE;
		}
		return TRUE;
	}

	BOOL CALLBACK FindMainFrame(HWND w, LPARAM lp)
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

	std::wstring AppBarFieldText()
	{
		HWND main = nullptr;
		::EnumWindows(FindMainFrame, reinterpret_cast<LPARAM>(&main));
		if (main == nullptr)
			return L"(no main frame)";
		EditSearch s = { nullptr };
		::EnumChildWindows(main, FindAppBarEdit, reinterpret_cast<LPARAM>(&s));
		if (s.found == nullptr)
			return L"(no app bar Edit)";
		wchar_t text[512] = { 0 };
		::SendMessageW(s.found, WM_GETTEXT, 512, reinterpret_cast<LPARAM>(text));
		return std::wstring(L"[") + text + L"] edit=" + std::to_wstring(reinterpret_cast<uintptr_t>(s.found));
	}

	void LogMenuAndMaybeAdd(HMENU menu, HWND owner)
	{
		const int count = ::GetMenuItemCount(menu);
		SpikeLog(L"INITMENUPOPUP hmenu=%p owner=%p class=%s items=%d", menu, owner, ClassOf(owner).c_str(), count);
		bool stock = false;
		for (int i = 0; i < count; ++i)
		{
			wchar_t text[256] = { 0 };
			::GetMenuStringW(menu, i, text, 256, MF_BYPOSITION);
			const UINT id = ::GetMenuItemID(menu, i);
			SpikeLog(L"  [%d] id=%u text=[%s]", i, id, text);
			if (wcsstr(text, L"Stock") != nullptr)
				stock = true;
		}
		if (stock && ::GetMenuState(menu, kSpikeItemID, MF_BYCOMMAND) == static_cast<UINT>(-1))
		{
			const BOOL ok = ::AppendMenuW(menu, MF_STRING, kSpikeItemID, L"Search with Kohaku Find/Change");
			SpikeLog(L"  APPENDED ours ok=%d err=%lu items now=%d", ok, ok ? 0UL : ::GetLastError(), ::GetMenuItemCount(menu));
		}
	}

	void LogCommand(const wchar_t* what, HWND w, WPARAM wp, LPARAM lp)
	{
		const UINT id = LOWORD(wp);
		SpikeLog(L"%s hwnd=%p class=%s id=%u hi=%u lp=%p%s", what, w, ClassOf(w).c_str(), id, HIWORD(wp),
			reinterpret_cast<void*>(lp), id == kSpikeItemID ? L"  <== OURS" : L"");
		if (id == kSpikeItemID)
			SpikeLog(L"  FIELD %s", AppBarFieldText().c_str());
	}

	LRESULT CALLBACK CallWndProc(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HC_ACTION)
		{
			const CWPSTRUCT* m = reinterpret_cast<const CWPSTRUCT*>(lParam);
			switch (m->message)
			{
				case WM_INITMENUPOPUP:
					LogMenuAndMaybeAdd(reinterpret_cast<HMENU>(m->wParam), m->hwnd);
					break;
				case WM_MENUSELECT:
					SpikeLog(L"MENUSELECT id/pos=%u flags=%#x hmenu=%p%s", LOWORD(m->wParam), HIWORD(m->wParam),
						reinterpret_cast<void*>(m->lParam), LOWORD(m->wParam) == kSpikeItemID ? L"  <== OURS" : L"");
					break;
				case WM_COMMAND:
					LogCommand(L"SENT WM_COMMAND", m->hwnd, m->wParam, m->lParam);
					break;
				case WM_MENUCOMMAND:
					SpikeLog(L"SENT WM_MENUCOMMAND pos=%u hmenu=%p", static_cast<UINT>(m->wParam), reinterpret_cast<void*>(m->lParam));
					break;
				case WM_UNINITMENUPOPUP:
					SpikeLog(L"UNINITMENUPOPUP hmenu=%p", reinterpret_cast<void*>(m->wParam));
					break;
				case WM_ENTERMENULOOP:
					SpikeLog(L"ENTERMENULOOP hwnd=%p class=%s popup=%u", m->hwnd, ClassOf(m->hwnd).c_str(), static_cast<UINT>(m->wParam));
					break;
				case WM_EXITMENULOOP:
					SpikeLog(L"EXITMENULOOP hwnd=%p popup=%u", m->hwnd, static_cast<UINT>(m->wParam));
					break;
				default:
					break;
			}
		}
		return ::CallNextHookEx(gCallWndHook, code, wParam, lParam);
	}

	LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam)
	{
		if (code == HC_ACTION && wParam == PM_REMOVE)
		{
			const MSG* m = reinterpret_cast<const MSG*>(lParam);
			if (m->message == WM_COMMAND)
				LogCommand(L"POSTED WM_COMMAND", m->hwnd, m->wParam, m->lParam);
			else if (m->message == WM_MENUCOMMAND)
				SpikeLog(L"POSTED WM_MENUCOMMAND pos=%u", static_cast<UINT>(m->wParam));
		}
		return ::CallNextHookEx(gGetMsgHook, code, wParam, lParam);
	}
}

void KBSAppBarMenuSpikeStart()
{
	if (gCallWndHook != nullptr)
		return;
	const DWORD thread = ::GetCurrentThreadId();
	gCallWndHook = ::SetWindowsHookExW(WH_CALLWNDPROC, CallWndProc, nullptr, thread);
	gGetMsgHook = ::SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, nullptr, thread);
	SpikeLog(L"START thread=%lu callwnd=%p getmsg=%p", thread, gCallWndHook, gGetMsgHook);
}

void KBSAppBarMenuSpikeStop()
{
	if (gCallWndHook != nullptr)
		::UnhookWindowsHookEx(gCallWndHook);
	if (gGetMsgHook != nullptr)
		::UnhookWindowsHookEx(gGetMsgHook);
	gCallWndHook = nullptr;
	gGetMsgHook = nullptr;
}

// End, KBSAppBarMenuSpike.cpp.
