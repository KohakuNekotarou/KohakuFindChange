//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ TAB'S QUERIES IN FIND/CHANGE'S OWN QUERY MENU (the author, 10-31: "pick it in the official dialog, and its tab opens and is set" - docs/superpowers/specs/2026-10-31-kfc-kohaku-tab-design.md section 5;
//  how InDesign's menu works, read from its panel plug-in and measured: docs/ai-notes/kfc-native-query-menu-2026-10-31.md).
//  LISTED: the file is a query file where InDesign's menu lists them (KFCKohakuQuery.h) - nothing here adds an entry.
//  PICKED: InDesign's menu hands the entry's file to kFCQueryXMLReaderCmdBoss, and KFC's watch on the command processor takes that command when the file is one of KFC's: InDesign's readers do not run - nothing of its own
//  tabs' is touched - the query is stated on the tab, and the tab is shown. Answered -1, "could not load": what keeps InDesign from turning to its own tab after the command - and it says [Custom] and greys Delete Query
//  for such a load, so the entry is chosen again once its handler has returned (the next idle).
//  DELETED: InDesign's own button: the file is one it listed.
//  SAVED: InDesign's own Save Query, on the tab: its dialog would take a name and write nothing (no writer for a tab of KFC's), so the press is taken before it - an event filter pushed while the face is shown - and KFC's
//  dialog asks the name.
//  CHANGED: what the tab asks changed by hand: InDesign's own message says [Custom], as a change on its own tabs does.
//
//========================================================================================

#ifndef __KFCKohakuQueryMenu_h__
#define __KFCKohakuQueryMenu_h__

#include "IDFile.h"
#include "KFCModelTypes.h"		// KFCKohakuQueryData

namespace KFCKohakuQueryMenu
{
	/** The watch on the command processor, for the session. */
	void Startup();
	/** Everything let go. Safe to call twice. */
	void Shutdown();

	/** The face is shown / hidden: the filter that takes Save Query's press on it. Nothing is walked (the face's hazard - KFCKohakuTab.h). */
	void FaceShown();
	void FaceHidden();

	/** What the face asks was changed by hand: the menu reads [Custom]. */
	void FaceChanged();

	/** A query of the tab double-clicked in KFC's query dialog: stated, the tab shown, and the menu says its entry - what its pick in the menu does. */
	void ShowQuery(const KFCKohakuQueryData& query, const IDFile& file);
}

#endif // __KFCKohakuQueryMenu_h__

// End, KFCKohakuQueryMenu.h.
