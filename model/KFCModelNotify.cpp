//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCModelNotify.h. Model side: not one UI header is included here, which is the whole reason the
//  file exists.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"
#include "ISubject.h"

// Project includes:
#include "KFCID.h"
#include "KFCModelNotify.h"
#include "KFCModelTypes.h"		// KFCNotifyPayload

namespace
{
// The session's subject - the one KFCBookWatch listens on. Nil during shutdown, when the session can no
// longer be resolved: the caller then says nothing, which is what an application going away wants.
void Emit(const KFCNotifyPayload& payload)
{
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<ISubject> subject(session, IID_ISUBJECT);
	if (subject == nil)
		return;
	// changedBy is a void*; the payload is only read while Change is delivering it.
	subject->Change(kKFCModelChangedMessage, IID_IKFCMODELOBSERVER, const_cast<KFCNotifyPayload*>(&payload));
}
}

void KFCNotifyRebuild()
{
	Emit(KFCNotifyPayload(KFCNotifyPayload::kRebuild));
}

void KFCNotifyRefreshRows()
{
	Emit(KFCNotifyPayload(KFCNotifyPayload::kRefreshRows));
}

void KFCNotifyChapterRowGoes(int32 chapterIdx)
{
	KFCNotifyPayload payload(KFCNotifyPayload::kChapterRowGoes);
	payload.chapterIdx = chapterIdx;
	Emit(payload);
}

void KFCNotifyStatus(const PMString& message)
{
	KFCNotifyPayload payload(KFCNotifyPayload::kStatus);
	payload.text = &message;
	Emit(payload);
}

// End, KFCModelNotify.cpp.
