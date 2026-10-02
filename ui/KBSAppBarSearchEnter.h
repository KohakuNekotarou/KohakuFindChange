//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Search the Application Bar's Text with This Panel (Enter)" - the flyout toggle (2026-10-02, the user's
//  design): while it is ON, Return in the search field of InDesign's APPLICATION BAR (the one with the
//  Adobe Stock / Adobe Help triangle) searches with this panel instead - the field's text put into
//  Edit > Find/Change as a Text search (KBSSearchEngine::SetTextQuery: the Text tab, its Find what), and
//  this panel's Find run on it. OFF by default; OFF = the field is not touched at all.
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
