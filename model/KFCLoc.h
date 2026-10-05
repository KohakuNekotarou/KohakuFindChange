//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Runtime Japanese for the few strings KFC speaks in Japanese - the replace's own alert and what
//  Edit > Undo calls KFC's writes - and JapaneseUI() for the How to Use page (KFCHowTo.cpp).
//
//  There is no jaJP string TABLE (the plug-in author's call). Every locale reads the enUS table,
//  and the Japanese is switched in HERE at run time instead, so no CP932 resource file has to be
//  maintained and no LocaleIndex row can strand a locale on raw keys. What is asked is the UI
//  LANGUAGE, not the featureset: a Roman-engine install running a Japanese UI gets Japanese, which
//  is what "speak the user's language" means.
//
//  THIS FILE IS UTF-8 WITH BOM so the L"..." literals can stay readable Japanese.
//  (An ASCII file would force \u escapes; a BOM-less one would be read as CP932 by MSVC.)
//
//========================================================================================

#ifndef __KFCLoc_h__
#define __KFCLoc_h__

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

namespace KFCLoc
{
	/** Is the UI language Japanese? PMLocaleId keeps the featureset and the UI language as
	    separate axes (PMLocaleId.h:39-43); this reads the language one. */
	inline bool JapaneseUI()
	{
		return LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
	}

	/** The Japanese text when the UI is Japanese, the enUS string-table entry otherwise.
	    Either way the result is FINISHED text that will not be translated again - parameters (^1)
	    are still replaced by ::ReplaceStringParameters afterwards, exactly as before. */
	inline PMString Text(const char* englishKey, const wchar_t* japanese)
	{
		if (JapaneseUI())
		{
			// THE HEADER'S OWN CONSTRUCTOR FOR "NOT A KEY". PMString(const wchar_t*) "should be used to
			// set strings that are not keys. Calling Translate on this string will do nothing"
			// (PMString.h:96-102) - which is exactly what these are, and is what the header recommends in
			// place of SetTranslatable, which it files under DISCOURAGED (:698-721). It relies on wchar_t
			// being UTF-16, which it is on Windows - KFC is Windows alone (the plug-in author's call). On
			// the Mac wchar_t is UTF-32 (:96-97): these would have to be char16_t literals put in through
			// SetXString, then SetTranslatable(kFalse).
			return PMString(japanese);
		}
		// The official one-liner for "here is a string-table key, give me its translation"
		// (PMString.h:80-83), written exactly this way by the localization sample itself -
		// basiclocalization/BscL10NDialogController.cpp:115.
		PMString s(englishKey, PMString::kTranslateDuringCall);
		// ...and the translation marked as finished. This one stays: the header's alternatives
		// (WideString, a kNoTranslate constructor, SetCString with an encoding) cannot take a key to its
		// translation, and an alert translates what it is given "unless the string has been translated
		// already or isn't translatable" (CAlert.h:84).
		s.SetTranslatable(kFalse);
		return s;
	}
}

// The Japanese for each enUS entry that has one. The keys these pair with are in KFCID.h and the
// enUS table (KFC_enUS.fr) - they ARE the English path.
namespace KFCJa
{
	// ----- The replace's own alert, shown INSTEAD of running. -----
	// The run stopped before writing anything: the verify walk found a ticked match that no longer
	// begins where the search left it (KFCReplaceEngine::TellResultsWentStale). An opening that
	// names the chapter where there is one to name, then what it means for the user.
	// See KFCID.h for why this is a statement rather than a question.
	const wchar_t kStaleResultsDoc[]        = L"検索結果に変化を確認しましたので、置換を中止しました。";
	const wchar_t kStaleResultsOne[]        = L"「^1」の検索結果に変化を確認しましたので、置換を中止しました。";
	// What Edit > Undo calls a Change Checked run (the plug-in author's call).
	const wchar_t kReplaceStep[]            = L"置換";
	const wchar_t kRunQueriesStep[]         = L"クエリの連続実行";
	// ...and a Reject Change and an Accept All Changes by KohakuFindChange. InDesign's own Track
	// Changes words, the change's author named as the Track Changes panel shows it (the plug-in
	// author's call: the name says only KohakuFindChange's changes are accepted).
	const wchar_t kRejectStep[]             = L"変更を却下";
	const wchar_t kAcceptAllStep[]          = L"KohakuFindChange によるすべての変更を承認";
	// ...and an Accept Change by KohakuFindChange on a row, a story or a run (Show Changes).
	const wchar_t kAcceptStep[]             = L"変更を承認";
	// (No closing "please search again" line, on purpose: the alert states the outcome and the status
	//  line carries what to do next - the plug-in author's call.)
}

#endif // __KFCLoc_h__

// End, KFCLoc.h.
