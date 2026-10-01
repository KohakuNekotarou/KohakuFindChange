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
	    split KBSLoc makes for the replace prompts and the About box, asked through the one function
	    KBSLoc::JapaneseUI().
	    *This said "the same split the enUS / jaJP STRING TABLES make" until 2026-08-12. There has
	     been no jaJP table since 2026-08-05; the .cpp's own note was corrected on 2026-08-08 and
	     this one, in the other half of the same pair of files, was not. */
	void Show();
}

#endif // __KBSHowTo_h__
