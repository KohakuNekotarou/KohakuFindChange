//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Link the Application Bar's Search Field to This Panel" - the implementation. See
//  KBSAppBarSearchEnter.h for what was measured before any of this was written.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// PerformAction - this panel's Find, run the way the flyout runs it
#include "IActiveContext.h"
#include "IApplication.h"		// QueryActionManager
#include "IFindChangeOptions.h"	// the dialog's tab and its query - what the field shows, where its text goes
#include "IObserver.h"
#include "IPanelMgr.h"			// IsPanelWithWidgetIDShown / ShowPanelByWidgetID - the panel up before it is filled
#include "ISession.h"
#include "ISubject.h"			// the Find/Change settings' subject - where their changes arrive
#include "TextWalkerServiceProviderID.h"	// IID_IFINDCHANGEOPTIONS - the protocol those changes arrive on
#include "IDialog.h"			// AppBarField: dialogs are passed over on the window list
#include "IWindow.h"			// GetSysWindow - the application frame's HWND
#include "IWindowList.h"		// the application's windows (KBSPanelAlpha.cpp walks the same list)

// The Glyph tab's query shown as a character (GlyphDescription): its font is two attributes on that tab's list.
#include "AttributeBossList.h"
#include "IFontFamily.h"		// QueryFace - the font the glyph id belongs to
#include "IGlyphUtils.h"		// GetUnicodeForGlyphID
#include "IPMFont.h"
#include "ITextAttrFont.h"		// kTextAttrFontStyleBoss's value - the style name
#include "ITextAttrUID.h"		// kTextAttrFontUIDBoss's value - the font family
#include "TextAttrID.h"

// The search runs once the key has gone through the loop - never inside the hook (see RunPendingSearch):
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)

// General includes:
#include "CObserver.h"
#include "PreferenceUtils.h"	// QuerySessionPreferences - the Find/Change settings, as the engine reads them
#include "Utils.h"

#include "PMString.h"
#include "WideString.h"

// Project includes:
#include "KBSAppBarSearchEnter.h"
#include "KBSBookPanelLookup.h"	// QueryPanelManager
#include "KBSModelAccess.h"		// KBSRuns()->SetQuery - the Find/Change settings are the model half's
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

// INDESIGN'S OWN TEXT, WRITTEN OVER AGAIN - THE WRITE-BACK (the author's call).
// InDesign puts its own "Adobe Stock" into the field when it builds the field - measured: about 24 s into a launch
// the field is made anew, already holding it - and when a menu is used (the user's observation). Find/Change does
// not change then, so without this the field goes on saying "Adobe Stock" until its next change, and a Return there
// searches for "Adobe Stock". A second thread hook, WH_CALLWNDPROCRET, sees the field being created (WM_CREATE) or
// written (WM_SETTEXT) - both are SENT messages, which the WH_GETMESSAGE hook never sees - and books the query to
// be written back one idle later. Typing is not a WM_SETTEXT (WM_CHAR, the IME and a paste edit the text from
// inside the Edit - how a Win32 Edit works; NOT YET MEASURED with a person typing into the field, which is hidden
// on the machine these were measured on), so what the person types is not written over. Two guards:
//   . the field has the keyboard focus -> nothing is written: that is the person's editing (InDesign may clear its
//     own text as the field takes the focus);
//   . InDesign writes again within kKBSAppBarRewriteGuardMs of a WRITE-BACK of ours -> it is answering us, so the
//     field is left to it until Find/Change next changes: a back-and-forth must not run for ever (none was measured).
//     *Only a write-back counts (found with KIDMCP's win32_controls): timed from ANY write of ours, a write by
//      InDesign within half a second of an ordinary change of the query - a menu used right after typing in Find
//      what - is taken for an answer, and the field is left saying "Adobe Stock" (a Return there then searches for
//      it). A back-and-forth can only start from a write-back.
static HHOOK           sCallWndRetHook = nullptr;
static ICallbackTimer* sRemirrorTimer  = nil;
static bool            sOwnWrite       = false;		// our own WM_SETTEXT is going through the field right now
static DWORD           sWriteBackTick  = 0;			// when we last wrote BACK over InDesign's text (GetTickCount); 0 = not yet
static bool            sRemirrorHeld   = false;		// InDesign answered a write-back of ours: hands off until the dialog changes
static const DWORD     kKBSAppBarRewriteGuardMs = 500;

static std::wstring ClassOf(HWND w)
{
	wchar_t name[128] = { 0 };
	if (w == nullptr || ::GetClassNameW(w, name, 128) == 0)
		return std::wstring();
	return name;
}

// Is `w` the application bar's search field? A Win32 Edit inside an OWL.ApplicationBarHostView (measured: the
// window tree, docs/ai-notes/indesign-win32-window-tree.md, and both spikes). The hook asks it of every Return's
// own window; AppBarField asks it again of the handle it remembers before each use, since a handle the field
// once had could name another window by now.
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

// THE FIELD SHOWS WHAT EDIT > FIND/CHANGE HOLDS (the author's design).
// While the toggle is ON, the field carries the query of the tab the dialog is on - the find string of Text and
// GREP, the glyph of Glyph, the character type of Transliterate - and follows it as it changes (Object and
// Colour, which this panel does not search, leave the field alone). Measured in a spike (branch
// spike/2026-10-03-appbar-mirror): a settings change arrives on IID_IFINDCHANGEOPTIONS for every keystroke in
// Find what, not only at Done; the field's usual "Adobe Stock" is real text in the Edit (so a cue banner shows
// only while the field is focused and empty), and WM_SETTEXT shows at once; InDesign puts its own text back when
// a menu is used - written over again at once (the write-back - see the second hook, above).
// (A design that took the tab from the field's triangle - Adobe Stock = Text, Adobe Help = GREP - was dropped
//  once the field showed the dialog's own query. What it measured stays in the note:
//  kSessionBoss's IID_IBOOLDATA, kStockSearchPrefImpl, is true for Adobe HELP.)

// What the field is given: the UTF-16 of a PMString, which is UTF-16 already. One converter: the Text and GREP
// tabs' WideString arrives here through PMString's own constructor from it.
static std::wstring WideOf(const PMString& s)
{
	int32 units = 0;
	const UTF16TextChar* buf = s.GrabUTF16Buffer(&units);
	return (buf != nil && units > 0) ? std::wstring(reinterpret_cast<const wchar_t*>(buf), units) : std::wstring();
}

// The Glyph tab's query as the field can show it: the character the glyph stands for (when the font says),
// then the glyph id and the font - "<char> (GID 8123, Kozuka Mincho Pr6N R)". The font is the two attributes the
// dialog keeps on that tab's list, kTextAttrFontUIDBoss (the family) and kTextAttrFontStyleBoss (the style),
// read the way SnpInsertGlyph.cpp reads them off text (and the way KBSSearchEngine's query signature counts
// them: "the FONT is two attributes in the list"). Built as a PMString, so a character past U+FFFF becomes its
// surrogate pair in PMString::AppendW rather than by hand.
static PMString GlyphDescription(const IFindChangeOptions* opts)
{
	PMString out;
	const Text::GlyphID gid = opts->GetFindGlyphID();
	if (gid == kInvalidGlyphID)
		return out;
	PMString font;
	UTF32TextChar character(0);
	IDataBase* const db = opts->GetUIDAttrDB();
	const AttributeBossList* list = (db != nil) ? opts->GetFindAttributeBossList(db, IFindChangeOptions::kGlyphSearch) : nil;
	if (list != nil)
	{
		InterfacePtr<const ITextAttrUID> familyAttr(
			static_cast<const ITextAttrUID*>(list->QueryByClassID(kTextAttrFontUIDBoss, IID_ITEXTATTRUID)));
		InterfacePtr<const ITextAttrFont> styleAttr(
			static_cast<const ITextAttrFont*>(list->QueryByClassID(kTextAttrFontStyleBoss, IID_ITEXTATTRFONT)));
		InterfacePtr<IFontFamily> family(familyAttr != nil ? db : nil, familyAttr != nil ? familyAttr->Get() : kInvalidUID, UseDefaultIID());
		if (family != nil)
		{
			const PMString style = (styleAttr != nil) ? styleAttr->GetFontName() : PMString();
			font = family->GetDisplayFamilyName();
			if (!style.IsEmpty())
			{
				font.Append(" ");
				font.Append(style);
			}
			InterfacePtr<IPMFont> face(family->QueryFace(style));
			Utils<IGlyphUtils> glyphUtils;
			if (face != nil && glyphUtils)
				character = glyphUtils->GetUnicodeForGlyphID(face, gid);
		}
	}
	if (character.GetValue() != 0)
	{
		out.AppendW(character);
		out.Append(" ");
	}
	out.Append("(GID ");
	out.AppendNumber(static_cast<int32>(gid));
	if (!font.IsEmpty())
	{
		out.Append(", ");
		out.Append(font);
	}
	out.Append(")");
	return out;
}

// The Transliterate tab's query - the character type it finds - in InDesign's UI language, in the very words its
// Transliterate tab shows (the author's call: English names of our own are not the UI's).
// The keys are InDesign's own string keys, read off its string tables on disk (idrc_PMST - the Find and Change
// Panel's for all but Kanji, which the dialog does not offer; that one is CompFontMgr's, a Required plug-in):
// e.g. "Half-width Katakana" = jaJP "hankaku katakana", "kWesternArabicDigits" = "Arabic Digits (0, 1, ...)".
// kTranslateDuringCall looks the key up in the string tables of every loaded plug-in; a key not found stays as
// it is - English-like words, never an empty field.
static PMString CharacterTypeName(IFindChangeOptions::CharacterType type)
{
	ConstCString key = nil;
	switch (type)
	{
		case IFindChangeOptions::kKanji:				key = "Kanji"; break;
		case IFindChangeOptions::kHalfWidthKatakana:	key = "Half-width Katakana"; break;
		case IFindChangeOptions::kHalfWidthRoman:		key = "Half-width Roman Symbols"; break;
		case IFindChangeOptions::kFullWidthHiragana:	key = "Full-width Hiragana"; break;
		case IFindChangeOptions::kFullWidthKatakana:	key = "Full-width Katakana"; break;
		case IFindChangeOptions::kFullWidthRoman:		key = "Full-width Roman Symbols"; break;
		case IFindChangeOptions::kWesternArabicDigits:	key = "kWesternArabicDigits"; break;
		case IFindChangeOptions::kArabicIndicDigits:	key = "kArabicIndicDigits"; break;
		case IFindChangeOptions::kFarsiDigits:			key = "kFarsiDigits"; break;
		default:										return PMString();
	}
	return PMString(key, PMString::kTranslateDuringCall);
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

// The field, remembered: it is asked for on every keystroke typed into Find what,
// and finding it walks every child window of the application frame. Re-checked before each use with the test
// the hook uses - a Win32 Edit inside the application bar - so a handle the system has since given to another
// window is never written to; it is looked for again.
static HWND sField = nullptr;

// The application bar's search field, or nullptr. The application frame is reached through the SDK - the window
// list, the way KBSPanelAlpha.cpp finds the Find/Change dialog - not by its Win32 class name: the windows on it
// that have a platform window and are not dialogs (measured: the frame alone - palettes and document windows
// answer nil to GetSysWindow; docs/ai-notes/indesign-win32-window-tree.md). Each such window is searched, so
// which of them carries the field is not assumed either. *GetNthWindow does not addref; nothing is released.
// (Not EnumWindows and the class name "indesign": that walks every top-level window on the desktop, on every
// keystroke.)
static HWND AppBarField()
{
	if (sField != nullptr && IsAppBarSearchField(sField))
		return sField;
	sField = nullptr;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IWindowList> windows(app, IID_IWINDOWLIST);
	if (windows == nil)
		return nullptr;
	const int32 count = windows->WindowCount();
	for (int32 i = 0; i < count && sField == nullptr; ++i)
	{
		IWindow* win = windows->GetNthWindow(i);
		if (win == nil)
			continue;
		InterfacePtr<IDialog> dlg(win, IID_IDIALOG);
		if (dlg != nil)
			continue;
		const HWND frame = win->GetSysWindow();
		if (frame == nullptr)
			continue;
		KBSAppBarFieldSearch s = { nullptr };
		::EnumChildWindows(frame, FindAppBarField, reinterpret_cast<LPARAM>(&s));
		sField = s.found;
	}
	return sField;
}

// Write the dialog's query into the field. Only what changed is written: an unchanged write would still move
// the caret of a field the user is in. True = it wrote.
static bool MirrorFindChangeIntoField()
{
	if (sShutdown || !sAppBarSearchEnter)
		return false;
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	PMString query;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	switch (mode)
	{
		case IFindChangeOptions::kTextSearch:
		case IFindChangeOptions::kGrepSearch:			query = PMString(opts->GetFindString(mode)); break;
		case IFindChangeOptions::kGlyphSearch:			query = GlyphDescription(opts); break;
		case IFindChangeOptions::kTransliterateSearch:	query = CharacterTypeName(opts->GetFindCharacterType()); break;
		default:										return false;	// Object, Colour: not this panel's
	}
	const std::wstring text = WideOf(query);
	const HWND field = AppBarField();
	if (field == nullptr || FieldText(field) == text)
		return false;
	// Marked as ours while it goes through, so the WH_CALLWNDPROCRET hook does not take it for InDesign's.
	sOwnWrite = true;
	::SendMessageW(field, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
	sOwnWrite = false;
	return true;
}

// The query written back over InDesign's own text (the write-back, see the statics) - one idle after InDesign
// wrote, so it has finished whatever it was doing with the field. Never while the person is in the field.
static uint32 RemirrorAfterInDesign(void* /*refPtr*/)
{
	if (sShutdown || !sAppBarSearchEnter || sRemirrorHeld)
		return IIdleTask::kEndOfTime;
	const HWND field = AppBarField();
	if (field != nullptr && ::GetFocus() == field)
		return IIdleTask::kEndOfTime;
	if (MirrorFindChangeIntoField())
		sWriteBackTick = ::GetTickCount();		// the one kind of write InDesign could be answering (see the statics)
	return IIdleTask::kEndOfTime;
}

// The second hook (the write-back): after a SENT message has been handled on the main thread. Only two messages to
// one window are looked at - the application bar's search field being created or written - and only to book the
// write-back.
// *A field made anew is remembered at once (sField): during the rebuild measured at startup the old field can
// still exist, and writing to it would leave the new one saying "Adobe Stock".
static LRESULT CALLBACK AppBarCallWndRetProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION && sAppBarSearchEnter && !sShutdown && !sOwnWrite && !sRemirrorHeld)
	{
		const CWPRETSTRUCT* const m = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
		if ((m->message == WM_SETTEXT || m->message == WM_CREATE) && IsAppBarSearchField(m->hwnd))
		{
			if (m->message == WM_CREATE)
				sField = m->hwnd;
			if (sWriteBackTick != 0 && ::GetTickCount() - sWriteBackTick < kKBSAppBarRewriteGuardMs)
				sRemirrorHeld = true;	// InDesign answered a write-back of ours: stop here (see the statics)
			else
			{
				if (sRemirrorTimer == nil)
					sRemirrorTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
				if (sRemirrorTimer != nil)
					sRemirrorTimer->StartTimer(RemirrorAfterInDesign, 1, nil);
			}
		}
	}
	return ::CallNextHookEx(sCallWndRetHook, code, wParam, lParam);
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

	// Return searches on the Text and GREP tabs only - the author's call at the first live check (not the dialog's
	// own query searched on every other tab). On the others the Return is still stopped (no
	// browser) and nothing runs; the status line says why - only while the panel is up: ShowStatus also keeps
	// the line for the panel's next show, where it would stand in place of the last search's report. The panel
	// is not opened for it.
	InterfacePtr<IPanelMgr> panelMgr(KBSBookPanelLookup::QueryPanelManager());
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	const IFindChangeOptions::SearchMode mode = (opts != nil) ? opts->GetSearchMode() : IFindChangeOptions::kTextSearch;
	if (mode != IFindChangeOptions::kTextSearch && mode != IFindChangeOptions::kGrepSearch)
	{
		if (panelMgr != nil && panelMgr->IsPanelWithWidgetIDShown(kKBSPanelWidgetID))
		{
			PMString msg("Application Bar link: Return searches on the Text and GREP tabs only - nothing was searched.");
			msg.SetTranslatable(kFalse);
			KBSResultTree::ShowStatus(msg);
		}
		return IIdleTask::kEndOfTime;
	}

	// The panel up first, so the rows the search draws are seen (kFalse = the key focus stays where it is).
	if (panelMgr != nil && !panelMgr->IsPanelWithWidgetIDShown(kKBSPanelWidgetID))
		panelMgr->ShowPanelByWidgetID(kKBSPanelWidgetID, kFalse);

	// The field's text goes into the dialog only when it differs from what the tab already holds (the field
	// shows that, so an untouched field changes nothing).
	if (opts == nil || WideOf(opts->GetFindString(mode)) != text)
	{
		PMString query(WideString(text.c_str(), static_cast<int32>(text.size())));
		query.SetTranslatable(kFalse);
		if (!KBSRuns()->SetQuery(query, mode))
		{
			PMString msg("The Application Bar's text could not be put into Find/Change - nothing was searched.");
			msg.SetTranslatable(kFalse);
			KBSResultTree::ShowStatus(msg);
			return IIdleTask::kEndOfTime;
		}
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

// Both hooks off. (The two always go on and off together: KBSSetAppBarSearchEnter, KBSShutdownAppBarSearchEnter.)
static void UnhookAppBar()
{
	if (sGetMsgHook != nullptr)
	{
		::UnhookWindowsHookEx(sGetMsgHook);
		sGetMsgHook = nullptr;
	}
	if (sCallWndRetHook != nullptr)
	{
		::UnhookWindowsHookEx(sCallWndRetHook);
		sCallWndRetHook = nullptr;
	}
}

#endif	// WINDOWS

// The observer that keeps the field following the dialog. An AddIn onto kActiveContextBoss under its own IID
// (KFCUI.fr), as the panel-visibility and Book-panel observers are: that boss carries observers that are not
// ours, and it is there for the whole session - this one is attached while the toggle is ON, panel or no panel.
class KBSAppBarMirrorObserver : public CObserver
{
public:
	KBSAppBarMirrorObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSAPPBARMIRROROBSERVER) {}
	virtual ~KBSAppBarMirrorObserver() {}

	virtual void Update(const ClassID& /*theChange*/, ISubject* /*theSubject*/, const PMIID& protocol, void* /*changedBy*/)
	{
#ifdef WINDOWS
		if (protocol == IID_IFINDCHANGEOPTIONS)
		{
			sRemirrorHeld = false;		// the dialog changed: writing back over InDesign's own text is allowed again
			MirrorFindChangeIntoField();
		}
#else
		(void)protocol;
#endif
	}
};

CREATE_PMINTERFACE(KBSAppBarMirrorObserver, kKBSAppBarMirrorObserverImpl)

// Attach (or detach) that observer to the Find/Change settings - the subject KBSPanelTitle's tab name listens
// to, on the same protocol. Nothing when the session is gone (teardown).
static void AttachMirrorObserver(bool attach)
{
	ISession* const session = GetExecutionContextSession();
	IActiveContext* const ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;
	InterfacePtr<IObserver> obs(static_cast<IObserver*>(ctx->QueryInterface(IID_IKBSAPPBARMIRROROBSERVER)));
	InterfacePtr<IFindChangeOptions> settings(QuerySessionPreferences<IFindChangeOptions>());
	InterfacePtr<ISubject> subject(settings, UseDefaultIID());
	if (obs == nil || subject == nil)
		return;
	const bool attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IFINDCHANGEOPTIONS,
		IID_IKBSAPPBARMIRROROBSERVER) != kFalse;
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IFINDCHANGEOPTIONS, IID_IKBSAPPBARMIRROROBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IFINDCHANGEOPTIONS, IID_IKBSAPPBARMIRROROBSERVER);
}

bool16 KBSGetAppBarSearchEnter()
{
	return sAppBarSearchEnter;
}

void KBSSetAppBarSearchEnter(bool16 on)
{
	sAppBarSearchEnter = on;
#ifdef WINDOWS
	if (on && !sShutdown)
	{
		// Both on the main thread, the thread that sets them (the flyout, and the settings read back at startup).
		if (sGetMsgHook == nullptr)
			sGetMsgHook = ::SetWindowsHookExW(WH_GETMESSAGE, AppBarGetMsgProc, nullptr, ::GetCurrentThreadId());
		if (sCallWndRetHook == nullptr)
			sCallWndRetHook = ::SetWindowsHookExW(WH_CALLWNDPROCRET, AppBarCallWndRetProc, nullptr, ::GetCurrentThreadId());
		// A fresh start for the guard: a write-back made before an OFF must not make an
		// InDesign write just after this ON look like an answer.
		sRemirrorHeld = false;
		sWriteBackTick = 0;
	}
	else if (!on)
	{
		UnhookAppBar();
		sPendingText.clear();
		if (sRunTimer != nil)
			sRunTimer->StopTimer();
		if (sRemirrorTimer != nil)
			sRemirrorTimer->StopTimer();
	}
	// The field follows the dialog while ON - and shows its query at once, not only at the next change.
	if (!sShutdown)
		AttachMirrorObserver(on != kFalse);
	if (on)
		MirrorFindChangeIntoField();
#endif
}

bool16 KBSApplyAppBarSearchEnter()
{
#ifdef WINDOWS
	if (!sAppBarSearchEnter)
		return kTrue;
	const HWND field = AppBarField();
	return (field != nullptr && ::IsWindowVisible(field)) ? kTrue : kFalse;
#else
	return kFalse;
#endif
}

void KBSShutdownAppBarSearchEnter()
{
#ifdef WINDOWS
	// Order: nothing new from now on, then the observer and both hooks off, then the bookings stopped and the timers
	// let go.
	sShutdown = true;
	AttachMirrorObserver(false);
	UnhookAppBar();
	sPendingText.clear();
	if (sRunTimer != nil)
	{
		sRunTimer->StopTimer();
		sRunTimer->Release();
		sRunTimer = nil;
	}
	if (sRemirrorTimer != nil)
	{
		sRemirrorTimer->StopTimer();
		sRemirrorTimer->Release();
		sRemirrorTimer = nil;
	}
#endif
	sAppBarSearchEnter = kFalse;
}

// End, KBSAppBarSearchEnter.cpp.
