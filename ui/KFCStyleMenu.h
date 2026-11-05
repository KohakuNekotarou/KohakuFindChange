//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A STYLE SUBMENU OF A ROW'S RIGHT-CLICK MENU (docs/superpowers/specs/2026-11-05-kfc-apply-object-style-design.md A2 / A3):
//  the front document's styles of a kind, put on a submenu as the menu pops - the Object Styles panel's order, the words a
//  person reads (translated, a group joined with ":" - KFCStylePathWords, as the Table part's style lists), up to
//  kKFCStyleItems items and a greyed note of the rest (or of
//  none). The items are the fixed ActionIDs kKFCStyleItem0ActionID ... (KFCUIID.h), named as they are drawn
//  (KFCActionComponent's UpdateActionStates) - Unify Reading's shape (KFCResultNodeEH's PopRowMenu).
//  ONE LIST AT A TIME: the list the last menu popped with says what an item gives (ItemStyle) - an object row's Apply Object
//  Style now; a table row list's cell styles are to share the items (the author's ask of 11-05 - Kind).
//  The UI half's main thread alone (menus).
//
//========================================================================================

#ifndef __KFCStyleMenu_h__
#define __KFCStyleMenu_h__

#include "PMString.h"

class IMenuManager;

namespace KFCStyleMenu
{
	/** Which styles a list holds. */
	enum Kind
	{
		kNone = 0,
		kObjectStyles		// an object row's Special Operations > Apply Object Style
	};

	/** The items and the note taken off `path` (put there by a menu before), the list of `kind` read again - the active
	    context's workspace (the front document's, the session's with none - KFCKohakuTab's FillStyles' source) - and put on
	    `path`: its first kKFCStyleItems at kKFCStyleItemFirstPosition + k, the note at kKFCStyleNotePosition when the list is
	    empty or longer. */
	void PutOnMenu(IMenuManager* menuMgr, const PMString& path, Kind kind);

	/** The name item k is drawn with - the style's words, an "&" doubled (a menu takes one for its mnemonic); untranslatable.
	    false = no item k on the menu now. */
	bool ItemName(int32 k, PMString& outName);

	/** The note's words ("No Object Styles", "<N> more - use the Object Styles panel"); untranslatable. false = no note now. */
	bool NoteName(PMString& outName);

	/** What item k gives: its list's kind, the style's untranslated full path (IStyleGroupHierarchy::GetFullPath(kFalse) - a
	    group's separator in it) and its words. false = no item k now. */
	bool ItemStyle(int32 k, Kind& outKind, PMString& outPath, PMString& outWords);

	/** Is `action` one of the style items? outK = its place (its ActionID less kKFCStyleItem0ActionID). */
	bool IsStyleItem(const ActionID& action, int32& outK);

	/** The list let go, its storage too (swapped with empty ones - KFCQueryOrder's way): its PMStrings destroyed while
	    InDesign stands, not by the unload. The UI half's Shutdown (KFCUIStartupShutdown). */
	void ShutdownCleanup();
}

#endif // __KFCStyleMenu_h__
