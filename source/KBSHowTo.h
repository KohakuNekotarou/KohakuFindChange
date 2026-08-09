//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  "How to Use..." on the panel's flyout menu: the operating reference for the whole plug-in,
//  shown in a ScriptUI dialog so it can be scrolled, selected and copied.
//
//  The text lives in the .cpp rather than in the string tables because odfrc caps a single string
//  at about 3.1KB and this reference is several times that - the same reason KESCL moved its own
//  reference into C++, and the reason KESCM's cannot be extended any further where it sits.
//
//========================================================================================

#pragma once
#ifndef __KBSHowTo_h__
#define __KBSHowTo_h__

namespace KBSHowTo
{
	/** Show the operating reference. Japanese on a Japanese InDesign, English otherwise - the same
	    split the enUS / jaJP string tables make for the replace prompts. */
	void Show();
}

#endif // __KBSHowTo_h__
