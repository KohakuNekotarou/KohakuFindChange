//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Runtime Japanese for the few strings KBS speaks in Japanese - the replace's own alert and what
//  Edit > Undo calls KBS's writes - and JapaneseUI() for the How to Use page (KBSHowTo.cpp).
//  (The About box was one of these until 2026-08-09; it now reads the same in every language.)
//
//  There is no jaJP string TABLE any more (2026-08-05, user's call). Every locale reads the
//  enUS table, and the Japanese is switched in HERE at run time instead, so no CP932 resource
//  file has to be maintained and no LocaleIndex row can strand a locale on raw keys. What is
//  asked is the UI LANGUAGE, not the featureset: a Roman-engine install running a Japanese UI
//  (this machine) gets Japanese, which is what "speak the user's language" means.
//
//  ***** THIS FILE IS UTF-8 WITH BOM ***** so the L"..." literals can stay readable Japanese.
//  (An ASCII file would force \u escapes; a BOM-less one would be read as CP932 by MSVC.)
//
//========================================================================================

#ifndef __KBSLoc_h__
#define __KBSLoc_h__

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

namespace KBSLoc
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
			// ***** THE HEADER'S OWN CONSTRUCTOR FOR "NOT A KEY" (2026-10-02, the API re-audit). *****
			// PMString(const wchar_t*) "should be used to set strings that are not keys. Calling
			// Translate on this string will do nothing" (PMString.h:96-102) - which is exactly what these
			// are, and is what the header recommends in place of SetTranslatable, which it files under
			// DISCOURAGED (:698-721). These were char16_t literals put in through SetXString and then
			// SetTranslatable(kFalse) until then, because wchar_t is UTF-32 on the Mac (:96-97); KBS is
			// Windows alone (the user's call, 2026-09-28), where wchar_t is UTF-16 and L"" is that.
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

// The Japanese the jaJP table used to carry, one constant per retired table entry. The keys
// these pair with live on in KBSID.h and the enUS table - they ARE the English path.
namespace KBSJa
{
	// ----- The replace's own alert, shown INSTEAD of running. -----
	// (The Change Checked confirmation's Japanese stood above it until 2026-09-26, and the prompt
	//  itself went on 2026-09-27.)
	// The run stopped before writing anything: the verify walk found a ticked match that no longer
	// begins where the search left it (KBSReplaceEngine::TellResultsWentStale). An opening that
	// names the chapter where there is one to name, then what it means for the user.
	// See KBSID.h for how this came to be a statement rather than a question.
	const wchar_t kStaleResultsDoc[]        = L"検索結果に変化を確認しましたので、置換を中止しました。";
	const wchar_t kStaleResultsOne[]        = L"「^1」の検索結果に変化を確認しましたので、置換を中止しました。";
	// What Edit > Undo calls a Change Checked run (2026-09-26, the user's call).
	const wchar_t kReplaceStep[]            = L"置換";
	// ...and a Reject Change and an Accept All Changes by KohakuFindChange (2026-09-29: English on every
	// UI until then). InDesign's own Track Changes words, the author named as the Track Changes panel
	// shows it (the user's call: the name says only KohakuFindChange's changes are accepted).
	const wchar_t kRejectStep[]             = L"変更を却下";
	const wchar_t kAcceptAllStep[]          = L"KohakuFindChange によるすべての変更を承認";
	// ...and an Accept Change by KohakuFindChange on a row, a story or a run (2026-09-29, Show Changes).
	const wchar_t kAcceptStep[]             = L"変更を承認";
	// (A closing line, u"検索し直してください。", stood here until 2026-08-10. It opened as "Nothing was
	//  replaced - please search again" and lost its first half that morning for saying what the
	//  sentence above already said; the user's call the same day took the rest, leaving the alert
	//  to state the outcome and the status line to carry what to do next.)
}

#endif // __KBSLoc_h__

// End, KBSLoc.h.
