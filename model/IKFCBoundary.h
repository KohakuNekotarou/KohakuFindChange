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
//  knows nothing of it, and a nil Query is that answer.
//  NEVER CHANGES SHAPE. Every other door may move with kKFCBoundaryRevision; this one is what tells the two
//  halves they moved, so its one method stays as it is.
//
//========================================================================================

#ifndef __IKFCBoundary_h__
#define __IKFCBoundary_h__

#include "IPMUnknown.h"

#include "KFCBoundaryID.h"	// IID_IKFCBOUNDARY / IID_IKFCUIBOUNDARY, kKFCBoundaryStamp

class IKFCBoundary : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKFCBOUNDARY };	// the model half's; the UI half's is asked for by IID_IKFCUIBOUNDARY

	/** kKFCBoundaryStamp as this half was built ("<version>/<boundary revision>"). */
	virtual const char* GetBoundaryStamp() = 0;
};

#endif // __IKFCBoundary_h__
