//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCModelObserver.h. UI side.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IObserver.h"
#include "ISession.h"
#include "ISubject.h"

// General includes:
#include "CObserver.h"

// Project includes:
#include "KFCUIID.h"
#include "KFCModelObserver.h"
#include "KFCModelTypes.h"		// KFCNotifyPayload
#include "KFCResultTree.h"

/** The session-attached observer that draws what the model half reports. */
class KFCModelObserver : public CObserver
{
public:
	KFCModelObserver(IPMUnknown* boss) : CObserver(boss, IID_IKFCMODELOBSERVER) {}
	virtual ~KFCModelObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KFCModelObserver, kKFCModelObserverImpl)

void KFCModelObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* changedBy)
{
	// Only what was sent under our own protocol: the session's subject carries InDesign's notifications too.
	if (theChange != kKFCModelChangedMessage || protocol != IID_IKFCMODELOBSERVER || changedBy == nil)
		return;
	const KFCNotifyPayload* payload = static_cast<const KFCNotifyPayload*>(changedBy);
	switch (payload->kind)
	{
		case KFCNotifyPayload::kRebuild:
			KFCResultTree::Rebuild();
			break;
		case KFCNotifyPayload::kRefreshRows:
			KFCResultTree::RefreshRows();
			break;
		case KFCNotifyPayload::kChapterRowGoes:
			KFCResultTree::BeforeChapterRowGoes(payload->chapterIdx);
			break;
		case KFCNotifyPayload::kStatus:
			if (payload->text != nil)
				KFCResultTree::ShowStatus(*payload->text);
			break;
		default:
			break;
	}
}

//----------------------------------------------------------------------------------------
// Attach / detach - KFCBookWatch's shape: ask before attaching and before detaching, so the two stay
// symmetric (linksui carries a live bug from attaching and detaching asymmetrically).
//----------------------------------------------------------------------------------------

void KFCModelObserverAttach()
{
	InterfacePtr<ISubject> subject(GetExecutionContextSession(), IID_ISUBJECT);
	InterfacePtr<IObserver> observer(GetExecutionContextSession(), IID_IKFCMODELOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, observer, IID_IKFCMODELOBSERVER, IID_IKFCMODELOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, observer, IID_IKFCMODELOBSERVER, IID_IKFCMODELOBSERVER);
}

void KFCModelObserverDetach()
{
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<ISubject> subject(session, IID_ISUBJECT);
	InterfacePtr<IObserver> observer(session, IID_IKFCMODELOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (subject->IsAttached(ISubject::kRegularAttachment, observer, IID_IKFCMODELOBSERVER, IID_IKFCMODELOBSERVER))
		subject->DetachObserver(ISubject::kRegularAttachment, observer, IID_IKFCMODELOBSERVER, IID_IKFCMODELOBSERVER);
}

// End, KFCModelObserver.cpp.
