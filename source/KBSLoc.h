//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Runtime Japanese for the few strings KBS speaks in Japanese - the replace confirmations and
//  the Glyph-tab confirmation dialog's labels. (The About box was one of these until 2026-08-09;
//  it now reads the same in every language, so it needs nothing from this file.)
//
//  There is no jaJP string TABLE any more (2026-08-05, user's call). Every locale reads the
//  enUS table, and the Japanese is switched in HERE at run time instead, so no CP932 resource
//  file has to be maintained and no LocaleIndex row can strand a locale on raw keys. What is
//  asked is the UI LANGUAGE, not the featureset: a Roman-engine install running a Japanese UI
//  (this machine) gets Japanese, which is what "speak the user's language" means.
//
//  ***** THIS FILE IS UTF-8 WITH BOM ***** so the u"..." literals can stay readable Japanese.
//  (An ASCII file would force \u escapes; a BOM-less one would be read as CP932 by MSVC.)
//
//========================================================================================

#ifndef __KBSLoc_h__
#define __KBSLoc_h__

#include <string>

#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMString.h"

#include "KBSID.h"

namespace KBSLoc
{
	/** Is the UI language Japanese? PMLocaleId keeps the featureset and the UI language as
	    separate axes (PMLocaleId.h:39-43); this reads the language one. */
	inline bool JapaneseUI()
	{
		return LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
	}

	/** The Japanese text when the UI is Japanese, the enUS string-table entry otherwise.
	    Either way the result is FINISHED text, marked untranslatable - parameters (^1) are
	    still replaced by ::ReplaceStringParameters afterwards, exactly as before. */
	inline PMString Text(const char* englishKey, const char16_t* japanese)
	{
		PMString s;
		if (JapaneseUI())
		{
			// PMString's own way in from UTF-16 (PMString.h:164-170). The cast is the one
			// the SDK itself makes for wide literals (PMString.h:1071-1073). char16_t
			// rather than wchar_t on purpose: PMString(const wchar_t*) would say outright
			// that it is not a key - which is what is wanted here - but wchar_t is UTF-32
			// on the Mac (PMString.h:96-97), and these literals are UTF-16.
			s.SetXString(reinterpret_cast<const UTF16TextChar*>(japanese),
				static_cast<int32>(std::char_traits<char16_t>::length(japanese)));
		}
		else
		{
			// The official one-liner for "here is a string-table key, give me its
			// translation" (PMString.h:80-83), written exactly this way by the
			// localization sample itself - basiclocalization/BscL10NDialogController.cpp:115.
			s = PMString(englishKey, PMString::kTranslateDuringCall);
		}
		// PMString.h files SetTranslatable under DISCOURAGED (:698-721) and points at
		// WideString, a kNoTranslate constructor or SetCString-with-encoding instead. None of
		// those three can carry a UTF-16 literal out as a PMString, and this is the very
		// means the alert's own contract names: CAlert.h:84 says a string is translated
		// "unless the string has been translated already or isn't translatable".
		s.SetTranslatable(kFalse);
		return s;
	}

	/** The enUS string-table entry, whatever the UI language - for the strings that are English on
		purpose. The replace confirmation became one of them on 2026-09-26 (user's call: "Find:" and
		"Change:", in English). Same finishing as Text(): translated from its key, then marked
		untranslatable so nothing downstream takes the finished text for a key. */
	inline PMString English(const char* englishKey)
	{
		PMString s(englishKey, PMString::kTranslateDuringCall);
		s.SetTranslatable(kFalse);
		return s;
	}
}

// The Japanese the jaJP table used to carry, one constant per retired table entry. The keys
// these pair with live on in KBSID.h and the enUS table - they ARE the English path.
namespace KBSJa
{
	// ----- Change Checked confirmation: NO JAPANESE ANY MORE (2026-09-26, user's call). -----
	// The prompt is two English lines on every UI - "Find:" and "Change:" - through KBSLoc::English,
	// and its opening question and closing lines are gone. The Japanese that stood here (the count
	// question, 検索文字列 / 置換文字列, the empty-box note, 検索形式 / 置換形式, and the unsaved-book
	// and ご注意下さい lines below) went with it.
	// ***** NOT PART OF THIS PROMPT - the replace's own alert, shown INSTEAD of running. *****
	// The run stopped before writing anything: the verify walk found a ticked match that no longer
	// begins where the search left it (KBSReplaceEngine::TellResultsWentStale). An opening that
	// names the chapter where there is one to name, then what it means for the user.
	// See KBSID.h for how this came to be a statement rather than a question.
	const char16_t kStaleResultsDoc[]        = u"検索結果に変化を確認しましたので、置換を中止しました。";
	const char16_t kStaleResultsOne[]        = u"「^1」の検索結果に変化を確認しましたので、置換を中止しました。";
	// (A closing line, u"検索し直してください。", stood here until 2026-08-10. It opened as "Nothing was
	//  replaced - please search again" and lost its first half that morning for saying what the
	//  sentence above already said; the user's call the same day took the rest, leaving the alert
	//  to state the outcome and the status line to carry what to do next.)

	// ----- Glyph confirmation dialog chrome -----
	const char16_t kGlyphFindLabel[]   = u"検索";
	const char16_t kGlyphChangeLabel[] = u"置換後";
	const char16_t kGlyphArrow[]       = u"→";
	// No caller since 2026-08-06 - the box this labels came off the dialog on 2026-08-01, and the
	// controller stopped stamping it into a widget that no longer exists. Kept for the same reason
	// KBSID.h keeps the key and the id: putting the box back should not need a translation round.
	const char16_t kGlyphDontShow[]    = u"次回から表示しない";

	// ----- About box -----
	// GONE on 2026-08-09 (user's call): the About box now reads the same in every UI language -
	// the plug-in's name and version, and nothing else - so there is no Japanese wording of it to
	// switch in. KBSActionComponent::DoAbout asks the string table directly instead of coming
	// through Text() above. The English entry is KBS_enUS.fr's kKBSAboutBoxStringKey.
}

#endif // __KBSLoc_h__

// End, KBSLoc.h.
