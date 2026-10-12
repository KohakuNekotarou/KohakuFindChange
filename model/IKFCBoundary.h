//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCBoundary - which build of the boundary a half was compiled with (KFCBoundaryID.h, kKFCBoundaryStamp).
//  Both halves put one on kSessionBoss, each under its own IID: the model half's under IID_IKFCBOUNDARY
//  (KFCModelServices.cpp, from KFC.fr), the UI half's under IID_IKFCUIBOUNDARY (ui/KFCUIServices.cpp, from
//  KFCUI.fr). Each half asks the OTHER's before it calls the other's door, and calls none when the answer is
//  not its own stamp - or when there is no answer at all, which is what a half older than the stamp gives:
//  the UI half then stands in for the model's doors (ui/KFCModelAccess.cpp), the model half takes the UI half
//  for absent (KFCQueryUIServices, IKFCUIServices.h).
//
//  Their own IIDs rather than a method at the end of a door: the question has to be answerable by a half that
//  knows nothing of it - a nil Query is that answer, where a slot past the end of an older half's vtable would be a
//  call into nothing.
//  NEVER CHANGES SHAPE. Every other door may move with kKFCBoundaryRevision; this one is what tells the two
//  halves they moved, so its one method stays as it is.
//  Asked for by its IID only - there is no kDefaultIID: a default would name one of the two stamps, and in the model
//  half the wrong one (its own, which always matches).
//
//========================================================================================

#ifndef __IKFCBoundary_h__
#define __IKFCBoundary_h__

#include "IPMUnknown.h"
#include "ISession.h"

#include <cstring>			// strcmp - KFCOtherHalfIsThisBuild

#include "KFCBoundaryID.h"	// IID_IKFCBOUNDARY / IID_IKFCUIBOUNDARY, kKFCBoundaryStamp
#include "KFCDiag.h"		// KFC_DIAG_FAULT - the test build's boundary-mismatch

class IKFCBoundary : public IPMUnknown
{
public:
	/** kKFCBoundaryStamp as this half was built ("<version>/<boundary revision>"). */
	virtual const char* GetBoundaryStamp() = 0;
};

/** IS THE OTHER HALF OF THIS BUILD? - the one question both halves ask before they call the other's door: the UI half
    once a session (KFCBoundaryMatches, ui/KFCModelAccess.cpp), the model half every time (KFCQueryUIServices,
    IKFCUIServices.h). otherStampIID = the other half's stamp: IID_IKFCBOUNDARY for the model half's, IID_IKFCUIBOUNDARY
    for the UI half's. A half with no stamp - one older than it - is not of this build. outOtherStamp, when asked for,
    gets the other half's stamp or nil; it points into the other .pln, so it is copied, not kept. The test build's
    boundary-mismatch switch (KFCDiag.h) answers no on both sides. */
inline bool KFCOtherHalfIsThisBuild(ISession* session, const PMIID& otherStampIID, const char** outOtherStamp = nil)
{
	InterfacePtr<IKFCBoundary> stamp(session, otherStampIID);
	const char* const theirs = (stamp != nil) ? stamp->GetBoundaryStamp() : nil;
	if (outOtherStamp != nil)
		*outOtherStamp = theirs;
	return theirs != nil && std::strcmp(theirs, kKFCBoundaryStamp) == 0 && !KFC_DIAG_FAULT("boundary-mismatch");
}

#endif // __IKFCBoundary_h__
