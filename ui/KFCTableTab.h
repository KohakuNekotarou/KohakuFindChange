//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE TABLE TAB'S FACE IN INDESIGN'S FIND/CHANGE DIALOG (docs/superpowers/specs/2026-10-31-kfc-table-tab-design.md - the QR Code tab's
//  shape, KFCQRTab.h, and its hazard: no window is walked while the dialog closes). A face of KFC's own among the dialog's tabs -
//  "Table" - offered through kFindChangeMiniTabbedDialogService (KFCUI.fr's IDList / IDListPair): Search: (All Documents / Document),
//  Find Table Style: ([Any Table Style] and the front document's table styles), With Overrides Only, Change to Table Style: ([Leave As
//  It Is] and the styles), Clear Overrides. No Find Next - the panel's Find lists the tables and its Next Result walks them (T13 as
//  ruled in work/sdd/2026-10-31-kfc-table-tab/progress.md) - two lines say so. A style is held by its full path, untranslated: a
//  list's search and a row's Replace read it by name in each document (KFCTableSearch). The lists follow the front document and its
//  styles while the face is shown, and a style the front document does not have stays on the list while it is asked for.
//  While the face is the dialog's tab - or was when the dialog closed - KFC's Find lists tables (KFCTableSearch in the model half, which
//  asks through IKFCUIServices::TableTabConditions). The main thread only.
//
//========================================================================================

#ifndef __KFCTableTab_h__
#define __KFCTableTab_h__

#include "KFCModelTypes.h"		// KFCTableConditions

namespace KFCTableTab
{
	/** IS THE TABLE TAB FIND/CHANGE'S TAB NOW (IKFCUIServices::TableTabConditions): its face shown, or the dialog closed with it shown
	    last - but not while a saved query loaded since stands (YieldToSavedQuery). outConditions = the face's as they stand, either way. */
	bool IsCurrent(KFCTableConditions& outConditions);

	/** A SAVED QUERY STATES ITS OWN TAB (the query dialog's Find - KFCQueryDialog, once the query is loaded): the Table tab steps aside
	    until the person shows its face again, so the Find searches what the query says. */
	void YieldToSavedQuery();

	/** Application shutdown: what the face keeps holds PMStrings - released while the session stands (the plug-in's other statics' rule). */
	void ShutdownCleanup();
}

#endif // __KFCTableTab_h__

// End, KFCTableTab.h.
