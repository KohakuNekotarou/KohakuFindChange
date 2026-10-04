//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  How the UI half reaches the model half (the model/UI split): the three session interfaces the model
//  puts on kSessionBoss, one line each. The UI half includes THIS and the shared types - never a model
//  header that declares the model's functions, which another plug-in cannot link to.
//
//      KFCResults()->GetChapterCount()        // = KFCResultModel::GetChapterCount() in the model
//      KFCRuns()->SearchBook(summary)          // = KFCSearchEngine::SearchBook(summary)
//      KFCChapters()->IsBookScopeOn()          // = KFCBookScope::IsBookScopeOn()
//
//  The model plug-in is a dependency of the UI plug-in, so on the main thread - the only thread the UI
//  half runs on - these are always there.
//
//========================================================================================

#ifndef __KFCModelAccess_h__
#define __KFCModelAccess_h__

#include "ISession.h"

#include "IKFCChapters.h"
#include "IKFCResults.h"
#include "IKFCRuns.h"
#include "KFCModelTypes.h"

inline InterfacePtr<IKFCResults> KFCResults()
{
	return InterfacePtr<IKFCResults>(GetExecutionContextSession(), UseDefaultIID());
}

inline InterfacePtr<IKFCRuns> KFCRuns()
{
	return InterfacePtr<IKFCRuns>(GetExecutionContextSession(), UseDefaultIID());
}

inline InterfacePtr<IKFCChapters> KFCChapters()
{
	return InterfacePtr<IKFCChapters>(GetExecutionContextSession(), UseDefaultIID());
}

#endif // __KFCModelAccess_h__
