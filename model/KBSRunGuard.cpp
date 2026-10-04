//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSRunGuard.h for why this question is asked in one place.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KBSRunGuard.h"
#include "KBSSearchEngine.h"
#include "KBSReplaceEngine.h"
#include "KBSShowChanges.h"

bool KBSRunGuard::IsAnyRunning()
{
	// Each engine keeps its own flag, raised by a guard object for the whole length of its run, so
	// this is three reads of a bool and can be asked as often as a caller likes - including from
	// inside an action-enablement pass, which runs every time a menu is opened. (Show Changes by
	// KohakuFindChange is one of them: it opens and hands back chapters under a modal bar like the
	// search.)
	return KBSSearchEngine::IsSearching()
		|| KBSReplaceEngine::IsReplacing()
		|| KBSShowChanges::IsShowing();
}

const char* KBSRunGuard::BusyMessage()
{
	return "Another Kohaku Find/Change run is already in progress.";
}

// End, KBSRunGuard.cpp.
