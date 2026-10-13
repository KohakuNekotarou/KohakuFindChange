//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  JapaneseUI() - is the UI language Japanese? Asked by the How to Use page (KFCHowTo.cpp), which carries its text
//  in both languages and picks one at run time - the one thing KFC switches to Japanese.
//
//  There is no jaJP string TABLE (the plug-in author's call). Every locale reads the enUS table, so the menus, the
//  panel and Edit > Undo's names for KFC's writes are English in every UI language, and no LocaleIndex row can
//  strand a locale on raw keys. What is asked is the UI LANGUAGE, not the featureset: a Roman-engine install
//  running a Japanese UI gets the Japanese page.
//
//========================================================================================

#ifndef __KFCLoc_h__
#define __KFCLoc_h__

#include "LocaleSetting.h"
#include "PMLocaleIds.h"

namespace KFCLoc
{
	/** Is the UI language Japanese? PMLocaleId keeps the featureset and the UI language as
	    separate axes (PMLocaleId.h:39-43); this reads the language one. */
	inline bool JapaneseUI()
	{
		return LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
	}
}

#endif // __KFCLoc_h__

// End, KFCLoc.h.
