//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
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
#include "KBSID.h"
#include "KBSResultNodeID.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"

/** Watches one hit row's check box and mirrors the click into KBSResultModel. */
class KBSResultCheckObserver : public CObserver
{
public:
	KBSResultCheckObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KBSResultCheckObserver() {}

	virtual void AutoAttach();
	virtual void AutoDetach();
	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KBSResultCheckObserver, kKBSResultCheckObserverImpl)

void KBSResultCheckObserver::AutoAttach()
{
	// A check box announces its state changes on IID_ITRISTATECONTROLDATA (not IID_IOBSERVER -
	// attaching to the wrong protocol is silently inert).
	InterfacePtr<ISubject> subject(this, IID_ISUBJECT);
	if (subject != nil)
		subject->AttachObserver(this, IID_ITRISTATECONTROLDATA);
}

void KBSResultCheckObserver::AutoDetach()
{
	InterfacePtr<ISubject> subject(this, IID_ISUBJECT);
	if (subject != nil)
		subject->DetachObserver(this, IID_ITRISTATECONTROLDATA);
}

void KBSResultCheckObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/,
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
	TreeNodePtr<KBSResultNodeID> nodeID(nodeData->Get());
	if (nodeID == nil || !nodeID->IsHitRow())
		return;

	KBSResultModel::SetHitChecked(nodeID->GetChapter(), nodeID->GetHit(), nowChecked);
	KBSResultTree::ShowCheckedStatus();
}

// End, KBSResultCheckObserver.cpp.
