//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  How the UI half reaches the model half (2026-10-01, the model/UI split): the three session interfaces
//  the model puts on kSessionBoss, one line each. The UI half includes THIS and the shared types - never a
//  model header that declares the model's functions, which another plug-in cannot link to.
//
//      KBSResults()->GetChapterCount()        // was KBSResultModel::GetChapterCount()
//      KBSRuns()->SearchBook(summary)          // was KBSSearchEngine::SearchBook(summary)
//      KBSChapters()->IsBookScopeOn()          // was KBSBookScope::IsBookScopeOn()
//
//  The model plug-in is a dependency of the UI plug-in, so on the main thread - the only thread the UI
//  half runs on - these are always there.
//
//========================================================================================

#ifndef __KBSModelAccess_h__
#define __KBSModelAccess_h__

#include "ISession.h"

#include "IKBSChapters.h"
#include "IKBSResults.h"
#include "IKBSRuns.h"
#include "KBSModelTypes.h"

inline InterfacePtr<IKBSResults> KBSResults()
{
	return InterfacePtr<IKBSResults>(GetExecutionContextSession(), UseDefaultIID());
}

inline InterfacePtr<IKBSRuns> KBSRuns()
{
	return InterfacePtr<IKBSRuns>(GetExecutionContextSession(), UseDefaultIID());
}

inline InterfacePtr<IKBSChapters> KBSChapters()
{
	return InterfacePtr<IKBSChapters>(GetExecutionContextSession(), UseDefaultIID());
}

#endif // __KBSModelAccess_h__
