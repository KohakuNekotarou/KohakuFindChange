//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The tooltip on a row of the query dialog's two lists: the whole of what the row's query does
//  (KFCQueryContentsTip) - the message line takes one line of it when the row is picked (KFCQueryDialog.cpp).
//
//  ON THE ROW'S TEXT CELL, NOT ON THE ROW. The cell covers the row, and it is the widget the pointer is over: a stock
//  InfoStaticTextWidget, which carries a tooltip of its own (kInfoStaticTextWidgetBoss's IID_ITIP - with
//  IID_ITRUNCATEDSTATICNAME, the cell's whole text when it is cut short). The cell's boss is
//  kKFCQueryRowTextWidgetBoss - the stock cell with this one interface replaced. Replacing it reads nothing into
//  this class: the cell's resource type holds no tooltip's data (InfoStaticTextWidget, Widgets.fh - StaticTextWidget's
//  and a font), and a resource hands its data only to the interfaces it names (memory
//  widget-boss-inherited-persistent-interface - the result list's text cell, kKFCColorTextWidgetBoss, once had its
//  resource's "Panel name" read into a class with no ReadWrite, and InDesign went down). The product's own tree-row tooltip is the same kind of class (conditionaltextui's
//  ConditionTagTVNodeTip.cpp, on its row boss).
//
//  Derived from AbstractTip, as every product tooltip is (KFCIconTip.cpp says why).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "ITreeNodeIDData.h"		// the row's node - which row of the list
#include "ITreeViewMgr.h"			// the list above the row - which of the two lists
#include "IWidgetParent.h"			// QueryParentFor - the cell -> its row -> its list

// General includes:
#include "AbstractTip.h"
#include "ListIndexNodeID.h"		// the node class the lists' adapter hands out (KFCQueryList.cpp)
#include "PMString.h"

#include <vector>

// Project includes:
#include "KFCQueryContents.h"
#include "KFCQueryOrder.h"
#include "KFCUIID.h"

/** The row cell's tooltip: what the row's saved query does, read off its file. */
class KFCQueryRowTip : public AbstractTip
{
public:
	KFCQueryRowTip(IPMUnknown* boss) : AbstractTip(boss) {}
	virtual ~KFCQueryRowTip() {}

	virtual PMString GetTipText(const PMPoint& mouseLocation);
};

CREATE_PMINTERFACE(KFCQueryRowTip, kKFCQueryRowTipImpl)

PMString KFCQueryRowTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	PMString none;
	none.SetTranslatable(kFalse);
	// Asked upwards: the row holds the node (ITreeNodeIDData), the list the rows (ITreeViewMgr) - and the list's widget
	// id says which of the two it is, as KFCQueryList.cpp's adapter asks.
	InterfacePtr<const IWidgetParent> parent(this, UseDefaultIID());
	if (parent == nil)
		return none;
	InterfacePtr<ITreeNodeIDData> nodeData(static_cast<ITreeNodeIDData*>(parent->QueryParentFor(ITreeNodeIDData::kDefaultIID)));
	InterfacePtr<ITreeViewMgr> listMgr(static_cast<ITreeViewMgr*>(parent->QueryParentFor(ITreeViewMgr::kDefaultIID)));
	InterfacePtr<IControlView> list(listMgr, UseDefaultIID());
	if (nodeData == nil || list == nil)
		return none;
	TreeNodePtr<ListIndexNodeID> node(nodeData->Get());
	const int32 index = (node != nil) ? node->GetIndex() : -1;
	const std::vector<KFCSavedQuery>& rows = (list->GetWidgetID() == kKFCQueryOrderListWidgetID)
		? KFCQueryOrder::Order() : KFCQueryOrder::Saved();
	if (index < 0 || index >= static_cast<int32>(rows.size()))
		return none;
	return KFCQueryContentsTip(rows[static_cast<size_t>(index)]);
}

// End, KFCQueryRowTip.cpp.
