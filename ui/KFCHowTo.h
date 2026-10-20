//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KFC)
//
//  "How to Use..." on the panel's flyout menu: the operating reference for the whole plug-in,
//  shown in a ScriptUI dialog so it can be scrolled, selected and copied.
//
//  The text lives in the .cpp rather than in the string tables - see there for why.
//
//========================================================================================

#pragma once
#ifndef __KFCHowTo_h__
#define __KFCHowTo_h__

namespace KFCHowTo
{
	/** Show the operating reference: Japanese when InDesign's UI language is Japanese, English
	    otherwise - the one text of KFC's own that follows the UI language (everything from the
	    string tables is English in every UI language), asked in Show() itself. */
	void Show();
}

#endif // __KFCHowTo_h__
