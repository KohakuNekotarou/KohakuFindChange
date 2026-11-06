//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The query dialog's two lists - see KFCQueryList.h. The shape is KCM's book comparison dialog list
//  (KCMBookTreeAdapter.cpp / KCMBookTreeWidgetMgr.cpp): ListTreeViewAdapter answers every question a flat list raises
//  except how many rows there are, and the row maker builds a row the way the product's panels do.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "ITreeViewController.h"	// the selection
#include "ITreeViewMgr.h"			// ClearTree / ChangeRoot - the rebuild; ScrollToNode

// General includes:
#include "CTreeViewWidgetMgr.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "CreateObject.h"			// CreateObjectNoInit
#include "DVPublicUtilities.h"		// dv_utils::SetThemeForView - the theme a new row draws in
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out
#include "ListTreeViewAdapter.h"
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"

// Project includes:
#include "KFCQueryList.h"
#include "KFCQueryOrder.h"
#include "KFCLabelDialogs.h"		// Special Operations > Label's keys - the third list (2026-11-06)
#include "KFCUIID.h"

namespace
{
	/** WHICH LIST THIS IS - the query dialog's saved queries (the left) or run order (the right), or Special Operations > Label's keys (KFCLabelDialogs -
	    2026-11-06): asked of the list's own widget id, the one thing they differ in. */
	enum WhichList { kSavedList, kOrderList, kLabelKeyList };

	WhichList ListOf(const WidgetID& id)
	{
		return (id == kKFCQueryOrderListWidgetID) ? kOrderList : (id == kKFCLabelKeyListWidgetID) ? kLabelKeyList : kSavedList;
	}

	WhichList ListOf(const IPMUnknown* boss)
	{
		InterfacePtr<IControlView> view(boss, UseDefaultIID());
		return (view != nil) ? ListOf(view->GetWidgetID()) : kSavedList;
	}

	/** How many rows the list has. */
	int32 CountOf(WhichList list)
	{
		switch (list)
		{
			case kOrderList:		return static_cast<int32>(KFCQueryOrder::Order().size());
			case kLabelKeyList:		return KFCLabelDialogs::KeyRowCount();
			default:				return static_cast<int32>(KFCQueryOrder::Saved().size());
		}
	}

	/** A row's words. */
	PMString TextOf(WhichList list, int32 index)
	{
		switch (list)
		{
			case kOrderList:		return KFCQueryOrder::OrderRowText(index);
			case kLabelKeyList:		return KFCLabelDialogs::KeyRowText(index);
			default:				return KFCQueryOrder::SavedRowText(index);
		}
	}
}

/** The lists' hierarchy: flat, as long as the list it is. ListTreeViewAdapter's own count is a placeholder that
    answers 10 (ListTreeViewAdapter.cpp), so this is the one method a flat list has to say. */
class KFCQueryListAdapter : public ListTreeViewAdapter
{
public:
	KFCQueryListAdapter(IPMUnknown* boss) : ListTreeViewAdapter(boss) {}
	virtual ~KFCQueryListAdapter() {}

protected:
	virtual int32 GetNumListItems() const
	{
		return CountOf(ListOf(this));
	}
};

CREATE_PMINTERFACE(KFCQueryListAdapter, kKFCQueryListAdapterImpl)

/** Builds and fills one row of either list: one cell, its text from KFCQueryOrder. */
class KFCQueryListWidgetMgr : public CTreeViewWidgetMgr
{
public:
	// kList: no levels - the base class then gives every row an indent of zero (CTreeViewWidgetMgr.cpp's
	// GetIndentForNode), but its ApplyIndentToWidget still moves the cell (below).
	KFCQueryListWidgetMgr(IPMUnknown* boss) : CTreeViewWidgetMgr(boss, kList) {}
	virtual ~KFCQueryListWidgetMgr() {}

	virtual IControlView* CreateWidgetForNode(const NodeID& /*node*/) const
	{
		// Three steps, as the product's panels build a row: make the boss without its cells, say which theme it draws
		// in - a DIALOG's (it is made long before the tree hands it to the dialog's window, so nothing else would say
		// it) - and only then build the cells. One CreateObject would build the cells first and theme them never.
		IPMUnknown* newObject = ::CreateObjectNoInit(
			::GetDataBase(this),
			RsrcSpec(LocaleSetting::GetLocale(), kKFCUIPluginID, kViewRsrcType, kKFCQueryRowRsrcID),
			IID_ICONTROLVIEW);
		InterfacePtr<IControlView> view(newObject, UseDefaultIID());
		if (view != nil)
		{
			dv_utils::SetThemeForView(view, dv_utils::kIDDialogTheme);
			view->DoPostCreate();
		}
		// The reference CreateObjectNoInit handed over is the one the caller gets; the InterfacePtr holds a second.
		return view;
	}

	virtual WidgetID GetWidgetTypeForNode(const NodeID& /*node*/) const
	{
		return kKFCQueryRowWidgetID;
	}

	// One fixed height (the row resource and the lists' scroll increments read the same number), the list's width.
	virtual PMReal GetNodeWidgetHeight(const NodeID& /*node*/) const
	{
		return PMReal(kKFCQueryRowHeight);
	}

	virtual PMReal GetNodeWidgetWidth(const NodeID& /*node*/) const
	{
		return this->GetTreeViewWidth();
	}

	// A flat list has nothing to indent - and the base's indent would throw away the cell's left edge as the row
	// resource writes it: it sets the left edge of a cell bound left and right to its base offset plus the indent, and
	// the base offset is only ever read from a registered style's row (CTreeViewWidgetMgr.cpp) - this class makes its
	// rows itself (CreateWidgetForNode), so the offset stays 0 (measured on KCM's panel list -
	// memory treeview-indent-overwrites-cell-frames).
	virtual void ApplyIndentToWidget(const NodeID& /*node*/, IPanelControlData* /*widgetList*/, int32 /*message*/) const
	{
	}

	virtual bool16 ApplyDataToWidget(const NodeID& node, IPanelControlData* widgetList, int32 /*message*/) const
	{
		if (widgetList == nil)
			return kTrue;
		TreeNodePtr<ListIndexNodeID> nodeID(node);
		const int32 index = nodeID != nil ? nodeID->GetIndex() : -1;
		// Written on EVERY apply, an unknown row included: rows are recycled as the list scrolls, and a cell left alone
		// keeps what the row it used to be said. (kTrue for an unknown row too - a new widget would not know it either.)
		const PMString text = TextOf(ListOf(this), index);
		this->SetNodeName(widgetList, text, kKFCQueryRowTextWidgetID);
		return kTrue;
	}
};

CREATE_PMINTERFACE(KFCQueryListWidgetMgr, kKFCQueryListWidgetMgrImpl)

void KFCQueryListRebuild(IPanelControlData* dialogPanel, const WidgetID& tree)
{
	if (dialogPanel == nil)
		return;
	InterfacePtr<ITreeViewMgr> treeMgr(dialogPanel->FindWidget(tree), UseDefaultIID());
	if (treeMgr == nil)
		return;
	// ClearTree(kTrue) drops what the tree held; ChangeRoot(kTrue) reloads it - kTrue promising every row is one height,
	// which GetNodeWidgetHeight keeps.
	treeMgr->ClearTree(kTrue);
	treeMgr->ChangeRoot(kTrue);
}

int32 KFCQueryListSelectedIndex(IPanelControlData* dialogPanel, const WidgetID& tree)
{
	if (dialogPanel == nil)
		return -1;
	InterfacePtr<ITreeViewController> controller(dialogPanel->FindWidget(tree), UseDefaultIID());
	if (controller == nil)
		return -1;
	NodeIDList selected;
	controller->GetSelectedItems(selected);
	if (selected.size() != 1)
		return -1;
	TreeNodePtr<ListIndexNodeID> nodeID(selected[0]);
	return nodeID != nil ? nodeID->GetIndex() : -1;
}

void KFCQueryListSelect(IPanelControlData* dialogPanel, const WidgetID& tree, int32 index)
{
	if (dialogPanel == nil)
		return;
	IControlView* treeView = dialogPanel->FindWidget(tree);
	InterfacePtr<ITreeViewController> controller(treeView, UseDefaultIID());
	if (controller == nil)
		return;
	const int32 count = CountOf(ListOf(tree));
	if (index < 0 || index >= count)
	{
		controller->DeselectAll();
		return;
	}
	const NodeID node = ListIndexNodeID::Create(index);
	// MOVED, not added to: the official re-selection - DeselectAll, then Select (docs/ai-notes/api-official-examples.md;
	// Select alone leaves a picked row picked - found live when the query dialog's Find picked the next row). Told once,
	// by the Select (the dialog's observer says what the row's query does).
	controller->DeselectAll(kFalse /*notifyOfChange*/, kTrue /*changeHilite*/);
	controller->Select(node);
	InterfacePtr<ITreeViewMgr> treeMgr(treeView, UseDefaultIID());
	if (treeMgr != nil)
		treeMgr->ScrollToNode(node, ITreeViewMgr::eScrollIntoView);
}

// End, KFCQueryList.cpp.
