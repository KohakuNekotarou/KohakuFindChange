//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSModelObserver.h. UI side.
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
#include "KBSID.h"
#include "KBSModelObserver.h"
#include "KBSModelTypes.h"		// KBSNotifyPayload
#include "KBSResultTree.h"

/** The session-attached observer that draws what the model half reports. */
class KBSModelObserver : public CObserver
{
public:
	KBSModelObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSMODELOBSERVER) {}
	virtual ~KBSModelObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KBSModelObserver, kKBSModelObserverImpl)

void KBSModelObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* changedBy)
{
	// Only what was sent under our own protocol: the session's subject carries InDesign's notifications too.
	if (theChange != kKBSModelChangedMessage || protocol != IID_IKBSMODELOBSERVER || changedBy == nil)
		return;
	const KBSNotifyPayload* payload = static_cast<const KBSNotifyPayload*>(changedBy);
	switch (payload->kind)
	{
		case KBSNotifyPayload::kRebuild:
			KBSResultTree::Rebuild();
			break;
		case KBSNotifyPayload::kRefreshRows:
			KBSResultTree::RefreshRows();
			break;
		case KBSNotifyPayload::kChapterRowGoes:
			KBSResultTree::BeforeChapterRowGoes(payload->chapterIdx);
			break;
		case KBSNotifyPayload::kStatus:
			if (payload->text != nil)
				KBSResultTree::ShowStatus(*payload->text);
			break;
		default:
			break;
	}
}

//----------------------------------------------------------------------------------------
// Attach / detach - KBSBookWatch's shape: ask before attaching and before detaching, so the two stay
// symmetric (linksui carries a live bug from attaching and detaching asymmetrically).
//----------------------------------------------------------------------------------------

void KBSModelObserverAttach()
{
	InterfacePtr<ISubject> subject(GetExecutionContextSession(), IID_ISUBJECT);
	InterfacePtr<IObserver> observer(GetExecutionContextSession(), IID_IKBSMODELOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (!subject->IsAttached(ISubject::kRegularAttachment, observer, IID_IKBSMODELOBSERVER, IID_IKBSMODELOBSERVER))
		subject->AttachObserver(ISubject::kRegularAttachment, observer, IID_IKBSMODELOBSERVER, IID_IKBSMODELOBSERVER);
}

void KBSModelObserverDetach()
{
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<ISubject> subject(session, IID_ISUBJECT);
	InterfacePtr<IObserver> observer(session, IID_IKBSMODELOBSERVER);
	if (subject == nil || observer == nil)
		return;
	if (subject->IsAttached(ISubject::kRegularAttachment, observer, IID_IKBSMODELOBSERVER, IID_IKBSMODELOBSERVER))
		subject->DetachObserver(ISubject::kRegularAttachment, observer, IID_IKBSMODELOBSERVER, IID_IKBSMODELOBSERVER);
}

// End, KBSModelObserver.cpp.
