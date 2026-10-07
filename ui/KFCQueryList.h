//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QUERY DIALOG'S TWO LISTS, AS WIDGETS: flat tree-view lists of what KFCQueryOrder holds - the saved
//  queries (kKFCQuerySavedListWidgetID) and the run order (kKFCQueryOrderListWidgetID). One adapter and one row maker
//  serve both; each asks its own widget id which list it is. Drawing them, and the selection - the dialog's controller
//  and observer (KFCQueryDialog.cpp) use these three.
//
//========================================================================================

#ifndef __KFCQueryList_h__
#define __KFCQueryList_h__

class IPanelControlData;

/** Draw one of the dialog's lists again from KFCQueryOrder. Nothing for a panel without that list. */
void KFCQueryListRebuild(IPanelControlData* dialogPanel, const WidgetID& tree);

/** The selected row's index in that list, -1 when none is (both lists are single-selection). */
int32 KFCQueryListSelectedIndex(IPanelControlData* dialogPanel, const WidgetID& tree);

/** Select that row and only it, and scroll it into view; an index the list does not hold deselects every row. */
void KFCQueryListSelect(IPanelControlData* dialogPanel, const WidgetID& tree, int32 index);

#endif // __KFCQueryList_h__

// End, KFCQueryList.h.
