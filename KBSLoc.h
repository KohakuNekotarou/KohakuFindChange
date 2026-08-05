//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Runtime Japanese for the few strings KBS speaks in Japanese - the replace confirmations,
//  the Glyph-tab confirmation dialog's labels, and the About box.
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
			s.SetXString(reinterpret_cast<const UTF16TextChar*>(japanese),
				static_cast<int32>(std::char_traits<char16_t>::length(japanese)));
		}
		else
		{
			PMString k(englishKey);
			k.Translate();
			s = k;
		}
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
	const char16_t kConfirmEditedSince[]     = u"検索後にテキストが編集されていると意図していない場所が置換される場合が有ります。";
	const char16_t kConfirmUnsavedOne[]      = u"章は開かれたまま未保存の状態で残ります。";
	const char16_t kConfirmUnsavedMany[]     = u"^1 個の章が開かれたまま未保存の状態で残ります。";
	const char16_t kConfirmSeveralChapters[] = u"複数の章を置換する場合は、ご注意ください。";

	// ----- Glyph confirmation dialog chrome -----
	const char16_t kGlyphFindLabel[]   = u"検索";
	const char16_t kGlyphChangeLabel[] = u"置換後";
	const char16_t kGlyphArrow[]       = u"→";
	const char16_t kGlyphDontShow[]    = u"次回から表示しない";

	// ----- About box -----
	// Unprefixed macro pieces (the display name, the version, the repo URL) concatenate into
	// the u"" literal by the usual adjacent-literal rule.
	const char16_t kAboutBox[] =
		u"" kKBSDisplayName u"、version " kKBSVersion u"\n\n"
		u"Adobe InDesign C++ SDK プラグイン。\n"
		u"InDesign 標準の検索/置換ダイアログで設定したクエリを使い、前面のドキュメント、"
		u"またはアクティブなブック（.indb）の全章を一度に検索して、ヒットをツリーに一覧します。"
		u"行をクリックするとその場所へジャンプし、チェックした行だけを置換できます。\n\n"
		u"本プラグインは KohakuNekotarou が、Anthropic の AI Claude（Claude Code）と協働して"
		u"設計・実装しました。\n\n"
		u"ソース: " kKBSRepoURL;
}

#endif // __KBSLoc_h__

// End, KBSLoc.h.
