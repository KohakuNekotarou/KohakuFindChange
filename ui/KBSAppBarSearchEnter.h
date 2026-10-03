//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Link the Application Bar's Search Field to This Panel" - the flyout toggle (2026-10-02, the user's
//  design; named so on 2026-10-03). While it is ON, the search field of InDesign's APPLICATION BAR (the one
//  with the Adobe Stock / Adobe Help triangle) and Edit > Find/Change work as one:
//    . the field SHOWS the query of the tab the dialog is on, and follows it as it is typed (Text / GREP: the
//      find string; Glyph: the glyph; Transliterate: the character type; Object / Colour: left alone) - and puts
//      it back when InDesign writes its own "Adobe Stock" there (as it builds the field at startup, and when a
//      menu is used), unless the person is in the field (2026-10-03, O-1; a WH_CALLWNDPROCRET hook);
//    . Return in the field searches with this panel instead of Adobe Stock / Help - on the Text and GREP tabs
//      only, the field's text put into that tab first when it differs (KBSSearchEngine::SetQuery). On the
//      other tabs Return is still stopped but nothing is searched; the status line says so while the panel
//      is up (the user's call at the first live check, 2026-10-03 - the WIP searched the dialog's query there).
//  (The triangle no longer chooses the tab - that was the first design of 2026-10-03, dropped the same day.)
//  OFF by default; OFF = the field is not touched at all.
//
//  *Windows only. On Mac the calls below exist and do nothing.
//
//  ***WHY WIN32, AND WHY THIS WAY - MEASURED FIRST (two spikes, branch spike/2026-10-02-appbar-menu;
//  docs/ai-notes/appbar-search-field-2026-10-02.md).***
//    . The SDK has no handle on that field: the application bar is laid out by DVA's Eve
//      (Required/(Application UI Resources)/idrc_EVE_/8231.idrc holds a placeholder edit_text,
//      "DummySearchWidgetID"), and no header names it.
//    . The field IS a real Win32 Edit (OWL.ApplicationBarHostView -> ... -> Edit), and its keys come
//      through the message loop as ordinary posted messages: WM_KEYDOWN 0x0D, WM_CHAR, WM_KEYUP.
//    . InDesign starts its Stock / Help search while it processes the WM_KEYDOWN (measured: about a
//      second passed before the WM_CHAR, and the browser came up). A WH_GETMESSAGE hook that turns that
//      WM_KEYDOWN into WM_NULL stops it: no browser came up, twice, and the user saw nothing happen.
//    . The IME's confirming Return arrives as VK_PROCESSKEY (0xE5), so stopping 0x0D alone leaves
//      Japanese input alone.
//    . The triangle's menu is NOT a Win32 menu (a DroverLord 'OS_PopupWindow' made at each open), which
//      is why this is a toggle on this panel's flyout and not an item on that menu.
//
//========================================================================================

#ifndef __KBSAppBarSearchEnter_h__
#define __KBSAppBarSearchEnter_h__

#include "BaseType.h"

// The toggle's current state (*OFF by default).
bool16	KBSGetAppBarSearchEnter();

// Set the toggle - and with it the hook: ON puts it on the main thread, OFF takes it off. Unlike the
// window-appearance toggles there is no window to wait for: the hook watches the message loop, and the
// field is recognised when a key reaches it. *Called on the main thread (the flyout, and the settings
// file read back at startup) - a thread hook watches the thread that sets it.
void	KBSSetAppBarSearchEnter(bool16 on);

// For the flyout's status line: kTrue when the field is on screen now (or the toggle is OFF); kFalse when
// it is ON and the field cannot be seen - the application bar hides it when its menus need a second row
// (measured 2026-10-02), and Return can only be caught in a field that is there.
bool16	KBSApplyAppBarSearchEnter();

// Take the hook off, stop a search not yet started and release the timer. Called from the UI half's
// shutdown. *A hook's procedure and ICallbackTimer's callback are raw pointers into this .pln - neither
// may outlive it.
void	KBSShutdownAppBarSearchEnter();

#endif // __KBSAppBarSearchEnter_h__
