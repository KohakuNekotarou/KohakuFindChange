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
	/** Show the operating reference. Japanese on a Japanese InDesign, English otherwise - the one
	    thing KFC switches by UI language, asked through KFCLoc::JapaneseUI() (everything from the
	    string table is English in every UI language). */
	void Show();
}

#endif // __KFCHowTo_h__
