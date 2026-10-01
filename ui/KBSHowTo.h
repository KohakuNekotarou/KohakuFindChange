//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  "How to Use..." on the panel's flyout menu: the operating reference for the whole plug-in,
//  shown in a ScriptUI dialog so it can be scrolled, selected and copied.
//
//  The text lives in the .cpp rather than in the string tables - see there for why.
//
//========================================================================================

#pragma once
#ifndef __KBSHowTo_h__
#define __KBSHowTo_h__

namespace KBSHowTo
{
	/** Show the operating reference. Japanese on a Japanese InDesign, English otherwise - the same
	    split KBSLoc makes for the replace prompts and the About box, asked through the one function
	    KBSLoc::JapaneseUI(). */
	void Show();
}

#endif // __KBSHowTo_h__
