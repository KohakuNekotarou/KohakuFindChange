//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The result list's row observer: a story, document or book row CLOSED takes the rows selected under it out of the
//  selection (the author's call, 2026-10-13 - O7-2b).
//
//  WHY. The stock tree keeps a row selected when a row above it closes over it (measured on this list: three rows
//  selected with Select All Rows, their first story row closed over two of them - all three still selected, and the two
//  shown hilited again when the row was opened). Return and the right-click Replace write the selection in the order
//  DRAWN (GetSelectedItemsDisplayOrder walks the open rows), so they wrote the one row on show and left the two hidden
//  ones selected and unwritten - while the message area still said "Selected 3 rows. Return replaces them all." A
//  selected row is a drawn row everywhere else on this list (Select All Rows opens the rows it selects); a row closed over
//  selected rows keeps that rule now (KFCResultTree::DeselectRowsUnder).
//
//  HOW: the official shape for "a row opened or closed" - an observer on the ROW widget's boss, attached to the row's own
//  subject along IID_ITREEVIEWMGR, where the tree manager broadcasts kTreeNodeExpandedMsg and kTreeNodeCollapsedMsg
//  (sdksamples/paneltreeview/PnlTrvNodeObserver.cpp; customdatalinkui/CusDtLnkUITreeNodeObserver.cpp the same). The row's
//  node is its ITreeNodeIDData. Attached while the row widget is shown (AutoAttach / AutoDetach - a row widget is
//  recycled as the list scrolls, and the one a closing row is drawn by is a shown one).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISubject.h"
#include "ITreeNodeIDData.h"
#include "ITreeViewMgr.h"		// IID_ITREEVIEWMGR - the protocol the tree manager tells a row's opening and closing on

// General includes:
#include "CObserver.h"
#include "widgetid.h"			// kTreeNodeCollapsedMsg

// Project includes:
#include "KFCUIID.h"
#include "KFCResultTree.h"		// DeselectRowsUnder

/** A result row's observer: hears the row closed (see the top). */
class KFCResultNodeObserver : public CObserver
{
public:
	KFCResultNodeObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KFCResultNodeObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KFCResultNodeObserver, kKFCResultNodeObserverImpl)

void KFCResultNodeObserver::AutoAttach()
{
	InterfacePtr<ISubject> subject(this, UseDefaultIID());
	if (subject != nil && !subject->IsAttached(this, IID_ITREEVIEWMGR))
		subject->AttachObserver(this, IID_ITREEVIEWMGR);
}

void KFCResultNodeObserver::AutoDetach()
{
	InterfacePtr<ISubject> subject(this, UseDefaultIID());
	if (subject != nil && subject->IsAttached(this, IID_ITREEVIEWMGR))
		subject->DetachObserver(this, IID_ITREEVIEWMGR);
}

void KFCResultNodeObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol,
								   void* /*changedBy*/)
{
	if (theChange != kTreeNodeCollapsedMsg || protocol != IID_ITREEVIEWMGR)
		return;		// an opened row hides nothing; the row's other news is not this observer's
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData != nil)
		(void)KFCResultTree::DeselectRowsUnder(nodeData->Get());
}

// End, KFCResultNodeObserver.cpp.
