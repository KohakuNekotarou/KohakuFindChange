//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The hit row check box's observer: a click flips that hit's "replace me" flag in the result
//  model. Modelled on the layer panel's eyeball (source/open/components/layerpanel/
//  LayerPanelEyeballObserver.cpp), including the part the wlistboxcomposite sample left unsolved -
//  WHICH ROW was clicked. The check box carries no NodeID of its own; the row widget it sits in
//  does, so we ask upwards for it: IWidgetParent::QueryParentFor(IID_ITREENODEIDDATA), exactly
//  what LayerPanelUtils::GetLayerTreeNodeFromSubwidget does.
//
//  A toggle must watch BOTH kTrueStateMessage and kFalseStateMessage - the layer panel does;
//  wlistboxcomposite watches only the first and so would miss every un-click.
//
//  The widget manager pushes state the other way (model -> box) with notify = kFalse, so filling
//  a row in never comes back here as a phantom click.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISubject.h"
#include "ITreeNodeIDData.h"		// the row's NodeID, reached through the parent chain
#include "ITriStateControlData.h"	// the protocol a check box announces its state on
#include "IWidgetParent.h"			// QueryParentFor - the check box -> its row

// General includes:
#include "CObserver.h"
#include "widgetid.h"				// kTrueStateMessage / kFalseStateMessage

// Project includes:
#include "KFCUIID.h"
#include "KFCResultNodeID.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces
#include "KFCResultTree.h"

/** Watches one hit row's check box and mirrors the click into KFCResultModel. */
class KFCResultCheckObserver : public CObserver
{
public:
	KFCResultCheckObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KFCResultCheckObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KFCResultCheckObserver, kKFCResultCheckObserverImpl)

void KFCResultCheckObserver::AutoAttach()
{
	// A check box announces its state changes on IID_ITRISTATECONTROLDATA (not IID_IOBSERVER -
	// attaching to the wrong protocol is silently inert).
	InterfacePtr<ISubject> subject(this, IID_ISUBJECT);
	if (subject != nil)
		subject->AttachObserver(this, IID_ITRISTATECONTROLDATA);
}

void KFCResultCheckObserver::AutoDetach()
{
	InterfacePtr<ISubject> subject(this, IID_ISUBJECT);
	if (subject != nil)
		subject->DetachObserver(this, IID_ITRISTATECONTROLDATA);
}

void KFCResultCheckObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/,
	const PMIID& /*protocol*/, void* /*changedBy*/)
{
	// Both directions matter for a toggle: kTrue = just checked, kFalse = just unchecked.
	const bool nowChecked = (theChange == kTrueStateMessage);
	if (!nowChecked && theChange != kFalseStateMessage)
		return;

	// Which row is this box in? Walk up to the row widget, which carries the tree NodeID.
	InterfacePtr<IWidgetParent> widgetParent(this, UseDefaultIID());
	if (widgetParent == nil)
		return;
	InterfacePtr<ITreeNodeIDData> nodeData(
		static_cast<ITreeNodeIDData*>(widgetParent->QueryParentFor(IID_ITREENODEIDDATA)));
	if (nodeData == nil)
		return;
	TreeNodePtr<KFCResultNodeID> nodeID(nodeData->Get());
	if (nodeID == nil || !nodeID->IsHitRow())
		return;

	// One row, one box: touching matches do not go on and off together, and a footnote's row can be
	// taken off like any other - the replace writes only the ticked matches.
	KFCResults()->SetHitChecked(nodeID->GetChapter(), nodeID->GetHit(), nowChecked);

	// The book row and this chapter's row read out "(N/M checked)", so one box going
	// on or off changes what they say. Nothing else on the panel does - see RefreshCheckedCounts.
	KFCResultTree::RefreshCheckedCounts(nodeID->GetChapter());

	// ...and say WHICH row it was, by the locator the row leads with: "P1(2)  checked".
	//
	// The COUNT is deliberately not repeated here - that is what the two rows above read out (the
	// author's call).
	// What the line adds is the identity of the row that just changed, which is worth having when
	// the list is long enough that the row is nowhere near the pointer.
	PMString locator, pre, match, post;
	if (KFCResults()->GetHitDisplay(nodeID->GetChapter(), nodeID->GetHit(), locator, pre, match, post))
		KFCResultTree::ShowHitCheckStatus(locator, nowChecked);
}

// End, KFCResultCheckObserver.cpp.
