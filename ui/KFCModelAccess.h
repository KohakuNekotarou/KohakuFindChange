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
//      KFCResults()->GetTotalHitCount()        // = KFCResultModel::GetTotalHitCount() in the model
//      KFCRuns()->SearchBook(summary)          // = KFCSearchEngine::SearchBook(summary)
//      KFCChapters()->IsBookScopeOn()          // = KFCBookScope::IsBookScopeOn()
//
//  The model plug-in is a dependency of the UI plug-in, so on the main thread - the only thread the UI
//  half runs on - these are always there.
//
//  ONLY A MODEL HALF OF THIS BUILD IS CALLED. The first time a door is asked for, the model half's boundary
//  stamp (IKFCBoundary.h) is compared with this half's own (KFCBoundaryID.h, kKFCBoundaryStamp). When they
//  differ - or the model half is too old to have one - each door answers from a stand-in that calls nothing
//  (KFCModelAccess.cpp), because the real door would take every call to whatever method sits in that slot in
//  the other build. A stand-in has a run always "already in progress", with the reason as its message: every
//  command of the panel is grey and the message line says what to do.
//  These three are the only way in: the doors have no kDefaultIID, so a Query of its own does not compile, and
//  each door's IID is spelled here alone.
//
//========================================================================================

#ifndef __KFCModelAccess_h__
#define __KFCModelAccess_h__

#include "ISession.h"

#include "IKFCChapters.h"
#include "IKFCResults.h"
#include "IKFCRuns.h"
#include "KFCModelTypes.h"

/** Is the model half that is loaded the one this UI half was built with? Asked once a session (KFCModelAccess.cpp). */
bool			KFCBoundaryMatches();
/** Why the doors are stood in for, as the message line says it (empty while KFCBoundaryMatches() is true). */
const char*		KFCBoundaryMismatchText();
/** The doors' stand-ins: statics that count no references, so an InterfacePtr may hold one and Release it. */
IKFCResults*	KFCResultsStandIn();
IKFCRuns*		KFCRunsStandIn();
IKFCChapters*	KFCChaptersStandIn();

inline InterfacePtr<IKFCResults> KFCResults()
{
	if (!KFCBoundaryMatches())
		return InterfacePtr<IKFCResults>(KFCResultsStandIn());
	return InterfacePtr<IKFCResults>(GetExecutionContextSession(), IID_IKFCRESULTS);
}

inline InterfacePtr<IKFCRuns> KFCRuns()
{
	if (!KFCBoundaryMatches())
		return InterfacePtr<IKFCRuns>(KFCRunsStandIn());
	return InterfacePtr<IKFCRuns>(GetExecutionContextSession(), IID_IKFCRUNS);
}

inline InterfacePtr<IKFCChapters> KFCChapters()
{
	if (!KFCBoundaryMatches())
		return InterfacePtr<IKFCChapters>(KFCChaptersStandIn());
	return InterfacePtr<IKFCChapters>(GetExecutionContextSession(), IID_IKFCCHAPTERS);
}

#endif // __KFCModelAccess_h__
