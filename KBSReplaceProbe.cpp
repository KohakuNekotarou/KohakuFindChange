//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  TEMPORARY measurement probe - see KBSReplaceProbe.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KBSReplaceProbe.h"

void KBSReplaceProbe::Run(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);
	outSummary.Append("Probe: wired, measurement not implemented yet.");
}

// End, KBSReplaceProbe.cpp.
