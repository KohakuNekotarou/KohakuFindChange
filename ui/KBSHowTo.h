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
	    split KBSLoc makes for the names Edit > Undo shows for KBS's writes and the stale-results
	    alerts, asked through the one function KBSLoc::JapaneseUI(). (Until 2026-10-03 this named the
	    replace prompts and the About box - gone on 2026-09-26 and the same in every language since
	    2026-08-09, KBSLoc.h.) */
	void Show();
}

#endif // __KBSHowTo_h__
