//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCRunGuard.h for why this question is asked in one place.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Project includes:
#include "KFCRunGuard.h"
#include "KFCSearchEngine.h"
#include "KFCQuerySequence.h"
#include "KFCChangeAll.h"
#include "KFCReplaceEngine.h"

bool KFCRunGuard::IsAnyRunning()
{
	// Each engine keeps its own flag, raised by a guard object for the whole length of its run, so
	// this is four cheap reads and can be asked as often as a caller likes - including from
	// inside an action-enablement pass, which runs every time a menu is opened.
	return KFCSearchEngine::IsSearching()
		|| KFCChangeAll::IsRunning()			// Change All in Book (No List)
		|| KFCQuerySequence::IsRunning()		// the query run: every query's Change All under one sequence
		|| KFCReplaceEngine::IsReplacing();		// a row's Replace - no bar, but its opens run the message loop (the header)
}

const char* KFCRunGuard::BusyMessage()
{
	return "Another Kohaku Find/Change run is already in progress.";
}

// End, KFCRunGuard.cpp.
