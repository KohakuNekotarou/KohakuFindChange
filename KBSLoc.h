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
}

// The Japanese the jaJP table used to carry, one constant per retired table entry. The keys
// these pair with live on in KBSID.h and the enUS table - they ARE the English path.
namespace KBSJa
{
	// ----- Change Checked confirmation (CAlert layout and glyph-dialog layout both) -----
	const char16_t kConfirmReplaceOne[]      = u"チェックした 1 件を置換しますか？";
	const char16_t kConfirmReplaceMany[]     = u"チェックした ^1 件を置換しますか？";
	const char16_t kConfirmFind[]            = u"検索文字列: ^1";
	const char16_t kConfirmChangeTo[]        = u"置換文字列: ^1";
	const char16_t kConfirmEmptyReplace[]    = u"（空欄：一致した箇所は削除されます）";
	const char16_t kConfirmFindFormat[]      = u"検索形式";
	const char16_t kConfirmChangeFormat[]    = u"置換形式";
	// ***** NOT PART OF THIS PROMPT - they are the replace's own alert. ***** An opening that names
	// what was edited, then what it can cost, then what Cancel does. Shown once per chapter, at the
	// moment the replace opens that chapter and finds its counters have moved (KBSEditStamp), which
	// is the only point where a chapter the user had CLOSED can be asked at all. They were a line on
	// the confirmation prompt for one afternoon on 2026-08-08; see KBSID.h for why they left it.
	// ...Many has no caller since that move: the alert names one chapter because it asks once per
	// chapter.
	const char16_t kConfirmEditedDoc[]       = u"検索後にテキストが編集されています。";
	const char16_t kConfirmEditedOne[]       = u"検索後に「^1」のテキストが編集されています。";
	const char16_t kConfirmEditedMany[]      = u"検索後に ^1 個の章のテキストが編集されています。";
	const char16_t kConfirmEditedTail[]      = u"意図していない場所が置換される場合が有ります。";
	const char16_t kConfirmEditedCancelAll[] = u"キャンセルすると、すべての置換を中止します。";
	// ONE string since 2026-08-07 (user's wording). It states the case rather than counting the
	// chapters, so the singular/plural pair it replaced is gone and no ^1 is left in it.
	const char16_t kConfirmUnsaved[]         = u"ブックで複数のドキュメントを置換する場合、置換されたドキュメントは未保存のまま開かれた状態になります。";
	// Just the warning since 2026-08-07 (user's wording): the line above states the condition it
	// used to name, so repeating it here only made the closing line the longest one on the prompt.
	const char16_t kConfirmCare[]            = u"ご注意下さい。";

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
