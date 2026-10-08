//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A row of the query dialog's two lists (KFCQueryList.cpp). A DOUBLE CLICK on a saved query - a row of the left list -
//  puts it into Edit > Find/Change and brings that dialog up, so what the query holds can be seen at once (the author's
//  call: "double click", the dialog "opened to show"); KFCQueryDialogShowInFindChange is what happens. A single click
//  only selects, as before, and the run order's rows - the right list - keep the stock handling alone.
//
//  THE DOUBLE CLICK'S ONE BIT OF STATE - the result list's rows' (KFCResultNodeEH.cpp, the note of that name) and KCM's
//  book dialog rows' (KCMBookRowEH.cpp): a double click arrives as LButtonDn, LButtonUp, ButtonDblClk, LButtonUp, so
//  ButtonDblClk only raises a flag and the trailing up acts on it, after the stock handler has finished the click. The
//  flag is cleared on every down, so one never consumed cannot turn a later single click into a load.
//
//  The keyboard is left alone, as in KCM's dialog rows: the dialog's own machinery has it.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"			// the list a row is in - its widget id
#include "IEvent.h"					// ShiftKeyDown / CmdKeyDown
#include "ITreeNodeIDData.h"		// this node's NodeID
#include "ITreeViewController.h"	// IsSelected - is this the row the click landed on?
#include "IWidgetParent.h"			// QueryParentFor - the row -> the list that owns it

// General includes:
#include "ListIndexNodeID.h"		// the node class ListTreeViewAdapter hands out (KFCQueryList.cpp)
#include "TreeNodeEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)

// Project includes:
#include "KFCUIID.h"
#include "KFCQueryDialog.h"			// KFCQueryDialogShowInFindChange

namespace
{
	// "The click going on right now", not any one row's: the rows' widgets are recycled. One click happens at a time.
	bool gLoadOnNextButtonUp = false;
}

class KFCQueryRowEH : public TreeNodeEventHandler
{
public:
	KFCQueryRowEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KFCQueryRowEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);

private:
	/** The saved query this click is for - its index in the left list - or -1 to leave it alone: a click the stock
	    handler used, a modified click, a row that is not the selected one, or a row of the run order. */
	int32 SavedRowForClick(IEvent* e, bool16 baseHandled) const;
};

CREATE_PMINTERFACE(KFCQueryRowEH, kKFCQueryRowEHImpl)

int32 KFCQueryRowEH::SavedRowForClick(IEvent* e, bool16 baseHandled) const
{
	if (baseHandled || e == nil || e->ShiftKeyDown() || e->CmdKeyDown())
		return -1;
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData == nil)
		return -1;
	const NodeID& node = nodeData->Get();

	// The selection lives on the list, not on the row, so ask upwards for it - and the list says which of the two it is.
	InterfacePtr<const IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return -1;
	InterfacePtr<ITreeViewController> treeController(
		static_cast<ITreeViewController*>(widgetParent->QueryParentFor(ITreeViewController::kDefaultIID)));
	if (treeController == nil || !treeController->IsSelected(node))
		return -1;
	InterfacePtr<IControlView> list(treeController, UseDefaultIID());
	if (list == nil || list->GetWidgetID() != kKFCQuerySavedListWidgetID)
		return -1;

	TreeNodePtr<ListIndexNodeID> nodeID(node);
	return nodeID != nil ? nodeID->GetIndex() : -1;
}

// Nothing of this plug-in's own happens on the way DOWN: every click starts with the double-click flag down.
bool16 KFCQueryRowEH::LButtonDn(IEvent* e)
{
	gLoadOnNextButtonUp = false;
	return TreeNodeEventHandler::LButtonDn(e);
}

// The second click of a double click. Only raises the flag - see the note at the head of this file.
bool16 KFCQueryRowEH::ButtonDblClk(IEvent* e)
{
	const bool16 result = TreeNodeEventHandler::ButtonDblClk(e);
	if (this->SavedRowForClick(e, result) >= 0)
		gLoadOnNextButtonUp = true;
	return result;
}

bool16 KFCQueryRowEH::LButtonUp(IEvent* e)
{
	// Consumed here, on the way in, so that every path out of this function leaves it down.
	const bool loadIt = gLoadOnNextButtonUp;
	gLoadOnNextButtonUp = false;

	// The stock handler finishes the click first (the selection, the end of a drag).
	const bool16 result = TreeNodeEventHandler::LButtonUp(e);
	if (!loadIt)
		return result;		// an ordinary single click: the row is selected, and that is all

	const int32 savedIndex = this->SavedRowForClick(e, result);
	if (savedIndex >= 0)
		KFCQueryDialogShowInFindChange(savedIndex);
	return result;
}

// End, KFCQueryRowEH.cpp.
