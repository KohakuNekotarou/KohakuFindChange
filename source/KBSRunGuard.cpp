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
#include "KBSGlyphScanEngine.h"
#include "KBSOversetScanEngine.h"

bool KBSRunGuard::IsAnyRunning()
{
	// Each engine keeps its own flag, raised by a guard object for the whole length of its run, so
	// this is four reads of a bool and can be asked as often as a caller likes - including from
	// inside an action-enablement pass, which runs every time a menu is opened.
	return KBSSearchEngine::IsSearching()
		|| KBSReplaceEngine::IsReplacing()
		|| KBSGlyphScanEngine::IsScanning()
		|| KBSOversetScanEngine::IsScanning();
}

const char* KBSRunGuard::BusyMessage()
{
	return "Another Kohaku Find/Change run is already in progress.";
}

// End, KBSRunGuard.cpp.
