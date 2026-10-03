//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS) - "Shorten Main Menu Names" (2026-10-03, the user's design; Japanese UI only).
//
//  Six columns of InDesign's own menu bar - File, Layout, Object, Plug-ins, Window, Help - get short
//  Japanese titles while the toggle is ON, and their official ones back the moment it is OFF.
//  Spec: docs/superpowers/specs/2026-10-03-kfc-shorten-main-menu-design.md. Measured basis (KT,
//  2026-10-03): memory official-menu-column-rename-at-runtime.
//
//  HOW. A column's title is the translation of its menu-path key and has no setter, so the column is
//  taken down (IMenuManager::RemoveSubmenuAndChildren) and every item put back (AddMenuItem) under a token
//  no string table holds, which shows as written. The items are known because the menu filter here sees
//  them as InDesign registers its menus - only on a start-up after the plug-in set changed; on any other
//  start-up the menus come back from the saved data - and they are kept in KFCMenuColumns.tsv beside
//  KBSPanelState.json. A rebuilt menu is kept by InDesign's saved data too, so it stays across restarts.
//
//========================================================================================

#pragma once
#ifndef __KBSMenuShorten_h__
#define __KBSMenuShorten_h__

class PMString;

namespace KBSMenuShorten
{
	/** The UI half's lazy start-up (the menus exist by then): write the record when this start-up
		registered the menus, then put the columns the way the settings ask. Japanese UI only. */
	void Startup();

	/** The panel menu's toggle: flip, rebuild, and write both keys to the settings file at once (the
		menus are kept by InDesign whether or not "Save Panel Settings" is pressed). outStatus = the
		status line; empty = say nothing. */
	void ToggleAndSave(PMString& outStatus);

	/** The toggle's check mark. */
	bool IsOn();

	/** Is there a record to rebuild from (this start-up's registration, or the file)? */
	bool HasRecord();

	/** The item's name in the panel menu (Japanese; not a key). */
	PMString MenuItemName();
}

#endif // __KBSMenuShorten_h__
