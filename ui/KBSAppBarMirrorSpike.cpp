//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  ***** THROWAWAY SPIKE (2026-10-03, branch spike/2026-10-03-appbar-mirror) - NOT FOR SHIPPING. *****
//  The user's ask: what Edit > Find/Change holds in Find what should show in the application bar's search
//  field ("it would be very clear if the text the dialog holds showed in that place"). The user's own observation:
//  the dialog's text reaches the settings when Done (or Find) is pressed - typed and closed, it is gone.
//  Measures:
//    1. when a Find/Change settings change arrives here (Done / Find / the close box), and what Find what
//       reads then;
//    2. A - the text written INTO the field (WM_SETTEXT): does it stay?
//    3. B - the text as the field's cue banner (EM_SETCUEBANNER, the faint text an empty edit shows).
//  A while %TEMP%\kbs-appbar-mirror-A exists, B otherwise. Every step to %TEMP%\kbs-appbar-mirror-spike.txt.
//  Called from KBSPanelObserver::Update (KBSPanelTitle.cpp) - so only while this panel is shown.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "IFindChangeOptions.h"
#include "ISession.h"

#include "PreferenceUtils.h"
#include "WideString.h"

#include <windows.h>
#include <cstdio>
#include <string>

namespace
{

std::wstring SpikeClassOf(HWND w)
{
	wchar_t name[128] = { 0 };
	if (w == nullptr || ::GetClassNameW(w, name, 128) == 0)
		return std::wstring();
	return name;
}

bool SpikeIsAppBarField(HWND w)
{
	if (SpikeClassOf(w) != L"Edit")
		return false;
	for (HWND p = ::GetParent(w); p != nullptr; p = ::GetParent(p))
	{
		if (SpikeClassOf(p) == L"OWL.ApplicationBarHostView")
			return true;
	}
	return false;
}

BOOL CALLBACK SpikeFindField(HWND w, LPARAM lp)
{
	if (SpikeIsAppBarField(w))
	{
		*reinterpret_cast<HWND*>(lp) = w;
		return FALSE;
	}
	return TRUE;
}

BOOL CALLBACK SpikeFindMain(HWND w, LPARAM lp)
{
	DWORD pid = 0;
	::GetWindowThreadProcessId(w, &pid);
	if (pid == ::GetCurrentProcessId() && SpikeClassOf(w) == L"indesign")
	{
		*reinterpret_cast<HWND*>(lp) = w;
		return FALSE;
	}
	return TRUE;
}

std::string SpikeUtf8(const std::wstring& w)
{
	if (w.empty())
		return std::string();
	const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(n, '\0');
	::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}

std::wstring SpikeTempPath(const wchar_t* name)
{
	wchar_t dir[MAX_PATH] = { 0 };
	::GetTempPathW(MAX_PATH, dir);
	return std::wstring(dir) + name;
}

void SpikeLog(const std::string& line)
{
	FILE* f = _wfopen(SpikeTempPath(L"kbs-appbar-mirror-spike.txt").c_str(), L"ab");
	if (f == nullptr)
		return;
	SYSTEMTIME t;
	::GetLocalTime(&t);
	fprintf(f, "%02d:%02d:%02d.%03d %s\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line.c_str());
	fclose(f);
}

std::wstring SpikeFieldText(HWND field)
{
	wchar_t text[1024] = { 0 };
	::SendMessageW(field, WM_GETTEXT, 1024, reinterpret_cast<LPARAM>(text));
	return text;
}

}	// namespace

void KBSAppBarMirrorSpikeOnFindChange(const ClassID& theChange)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
	{
		SpikeLog("change: no IFindChangeOptions");
		return;
	}
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	const WideString& findW = opts->GetFindString(mode);
	int32 units = 0;
	const UTF16TextChar* buf = findW.GrabUTF16Buffer(&units);
	const std::wstring find(buf != nil ? reinterpret_cast<const wchar_t*>(buf) : L"", buf != nil ? units : 0);

	char head[160];
	sprintf_s(head, "change class=0x%x mode=%d find=[", theChange.Get(), static_cast<int>(mode));
	std::string line(head);
	line += SpikeUtf8(find);
	line += "]";

	HWND main = nullptr;
	::EnumWindows(SpikeFindMain, reinterpret_cast<LPARAM>(&main));
	HWND field = nullptr;
	if (main != nullptr)
		::EnumChildWindows(main, SpikeFindField, reinterpret_cast<LPARAM>(&field));
	if (field == nullptr)
	{
		SpikeLog(line + " field=NONE");
		return;
	}
	line += " field.before=[" + SpikeUtf8(SpikeFieldText(field)) + "]";
	line += ::IsWindowVisible(field) ? " vis=1" : " vis=0";

	const bool modeA = ::GetFileAttributesW(SpikeTempPath(L"kbs-appbar-mirror-A").c_str()) != INVALID_FILE_ATTRIBUTES;
	if (modeA)
	{
		const LRESULT r = ::SendMessageW(field, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(find.c_str()));
		char tail[64];
		sprintf_s(tail, " A:WM_SETTEXT=%d", static_cast<int>(r));
		line += tail;
		line += " field.after=[" + SpikeUtf8(SpikeFieldText(field)) + "]";
	}
	else
	{
		const UINT kSetCue = 0x1501, kGetCue = 0x1502;	// EM_SETCUEBANNER / EM_GETCUEBANNER (ECM_FIRST + 1 / + 2)
		const LRESULT r = ::SendMessageW(field, kSetCue, TRUE, reinterpret_cast<LPARAM>(find.c_str()));
		wchar_t cue[1024] = { 0 };
		const LRESULT g = ::SendMessageW(field, kGetCue, reinterpret_cast<WPARAM>(cue), 1024);
		char tail[96];
		sprintf_s(tail, " B:SETCUE=%d GETCUE=%d", static_cast<int>(r), static_cast<int>(g));
		line += tail;
		line += " cue=[" + SpikeUtf8(cue) + "]";
		::InvalidateRect(field, nullptr, TRUE);
	}
	SpikeLog(line);
}

// End, KBSAppBarMirrorSpike.cpp.
