//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSModelNotify.h. Model side: not one UI header is included here, which is the whole reason the
//  file exists.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"
#include "ISubject.h"

// Project includes:
#include "KBSID.h"
#include "KBSModelNotify.h"
#include "KBSModelTypes.h"		// KBSNotifyPayload

namespace
{
// The session's subject - the one KBSBookWatch listens on. Nil during shutdown, when the session can no
// longer be resolved: the caller then says nothing, which is what an application going away wants.
void Emit(const KBSNotifyPayload& payload)
{
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<ISubject> subject(session, IID_ISUBJECT);
	if (subject == nil)
		return;
	// changedBy is a void*; the payload is only read while Change is delivering it.
	subject->Change(kKBSModelChangedMessage, IID_IKBSMODELOBSERVER, const_cast<KBSNotifyPayload*>(&payload));
}
}

void KBSNotifyRebuild()
{
	Emit(KBSNotifyPayload(KBSNotifyPayload::kRebuild));
}

void KBSNotifyRefreshRows()
{
	Emit(KBSNotifyPayload(KBSNotifyPayload::kRefreshRows));
}

void KBSNotifyChapterRowGoes(int32 chapterIdx)
{
	KBSNotifyPayload payload(KBSNotifyPayload::kChapterRowGoes);
	payload.chapterIdx = chapterIdx;
	Emit(payload);
}

void KBSNotifyStatus(const PMString& message)
{
	KBSNotifyPayload payload(KBSNotifyPayload::kStatus);
	payload.text = &message;
	Emit(payload);
}

// End, KBSModelNotify.cpp.
