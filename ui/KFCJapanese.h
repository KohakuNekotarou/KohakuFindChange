//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE JAPANESE WORDS KFC WRITES ITSELF - in a Japanese InDesign's Kohaku+ Ruby and Kenten parts (their labels, their lists' entries), on the panel's tiers' names and on the rows'
//  menus (2026-11-03 - the author: "kenten is Japanese, so Japanese words"; "the Look For list in Japanese"; "Change Ruby To and the rest in Japanese"; "the Find"" on the panel in
//  Japanese"). The parts are a Japanese InDesign's alone (KOHAKU-03), so their words are not the string table's (English, the one table - memory plugin-ui-localization-fr-encoding).
//
//  AS CODE POINTS: the project's .cpp and .h files have no BOM - a character typed in one is read as CP932 - and a \u escape written through a tool can come back as the character itself
//  (2026-11-03: it did, and a list said mojibake - memory file-edit-escape-and-newline-traps). Numbers cannot. Each word's romanization stands beside it.
//
//========================================================================================

#ifndef __KFCJapanese_h__
#define __KFCJapanese_h__

#include "PMString.h"

namespace KFCJa
{
	/** The characters of `codes`, untranslatable. */
	inline PMString Words(const uint32* codes, size_t count)
	{
		PMString words;
		for (size_t i = 0; i < count; ++i)
			words.AppendW(UTF32TextChar(codes[i]));
		words.SetTranslatable(kFalse);
		return words;
	}
}

// One word: KFCJa::<name>() - its characters (the macro keeps the array and its count together).
#define KFC_JA_WORD(name, ...) \
	namespace KFCJa { inline PMString name() { static const uint32 kCodes[] = { __VA_ARGS__ }; return Words(kCodes, sizeof(kCodes) / sizeof(kCodes[0])); } }

// ----- the Ruby part's Look For (the author's names, 11-03), in kKFCRubyFindWith ... kKFCRubyFindVaries's order - and the tiers' names the same
KFC_JA_WORD(RubyWith,			0x30EB, 0x30D3, 0x6709)									// ruby ari (Text With Ruby)
KFC_JA_WORD(RubyWithout,		0x30EB, 0x30D3, 0x7121)									// ruby nashi (Text Without Ruby)
KFC_JA_WORD(RubyMarks,			0x30EB, 0x30D3, 0x5370)									// ruby shirushi (Ruby Marks in Text)
KFC_JA_WORD(RubyList,			0x30EB, 0x30D3, 0x4E00, 0x89A7)							// ruby ichiran (Words in a Ruby List)
KFC_JA_WORD(RubyVaries,			0x30EB, 0x30D3, 0x63FA, 0x308C)							// ruby yure (Inconsistent Ruby)

// ----- the Ruby part's labels and its list's
KFC_JA_WORD(LabelLookFor,		0x691C, 0x7D22, 0x5BFE, 0x8C61, 0xFF1A)					// kensaku taishou: (Look For:)
KFC_JA_WORD(LabelRuby,			0x30EB, 0x30D3, 0xFF1A)									// ruby: (Ruby:)
KFC_JA_WORD(LabelBase,			0x89AA, 0x6587, 0x5B57, 0xFF1A)							// oyamoji: (Base Text: - Kenten's too)
KFC_JA_WORD(LabelChangeRuby,	0x7F6E, 0x63DB, 0x30EB, 0x30D3, 0xFF1A)					// chikan ruby: (Change Ruby To:)
KFC_JA_WORD(FindOnly,			0x691C, 0x7D22, 0x306E, 0x307F)							// kensaku nomi (Find Only)
KFC_JA_WORD(LabelList,			0x4E00, 0x89A7, 0xFF1A)									// ichiran: (Ruby List:)
KFC_JA_WORD(LabelWhich,			0x5BFE, 0x8C61, 0xFF1A)									// taishou: (Occurrences:)
KFC_JA_WORD(ListAll,			0x3059, 0x3079, 0x3066)									// subete (All)
KFC_JA_WORD(FirstInStory,		0x30B9, 0x30C8, 0x30FC, 0x30EA, 0x30FC, 0x3067, 0x6700, 0x521D)			// sutoorii de saisho (First in Story)
KFC_JA_WORD(FirstInDocument,	0x30C9, 0x30AD, 0x30E5, 0x30E1, 0x30F3, 0x30C8, 0x3067, 0x6700, 0x521D)	// dokyumento de saisho (First in Document)
KFC_JA_WORD(FirstInBook,		0x30D6, 0x30C3, 0x30AF, 0x3067, 0x6700, 0x521D)							// bukku de saisho (First in Book)
KFC_JA_WORD(OtherRuby,			0x9055, 0x3046, 0x8AAD, 0x307F, 0x306E, 0x30EB, 0x30D3, 0x3082, 0x542B, 0x3081, 0x308B)	// chigau yomi no ruby mo fukumeru (Include Text With Other Ruby)

// ----- the Kenten part's labels and its lists' first entries (J2)
KFC_JA_WORD(LabelKenten,		0x570F, 0x70B9, 0xFF1A)									// kenten: (Kenten:)
KFC_JA_WORD(LabelChangeKenten,	0x7F6E, 0x63DB, 0x570F, 0x70B9, 0xFF1A)					// chikan kenten: (Change Kenten To:)
KFC_JA_WORD(AnyKenten,			0x5B, 0x3059, 0x3079, 0x3066, 0x306E, 0x570F, 0x70B9, 0x5D)		// [subete no kenten] ([Any Kenten] - InDesign's own words for it)
KFC_JA_WORD(LeaveKenten,		0x5B, 0x5909, 0x66F4, 0x3057, 0x306A, 0x3044, 0x5D)				// [henkou shinai] ([Leave As It Is])
KFC_JA_WORD(KentenNone,			0x570F, 0x70B9, 0x306A, 0x3057)							// kenten nashi (a tier's Text Without Kenten)

// ----- the rows' menus
KFC_JA_WORD(NiSoroeru,			0x306B, 0x305D, 0x308D, 0x3048, 0x308B)					// ni soroeru ("<<reading>> ni soroeru" - unify to)

namespace KFCJa
{
	/** "[<words>]" - a kind's name on a tier, as the English ones stood ("[Inconsistent Ruby]"). */
	inline PMString Bracketed(const PMString& words)
	{
		PMString out("[");
		out.Append(words);
		out.Append("]");
		out.SetTranslatable(kFalse);
		return out;
	}

	/** "(<words>)" in full-width parentheses - a tier's qualifier ("(First in Story)"). */
	inline PMString Parenthesized(const PMString& words)
	{
		PMString out;
		out.AppendW(UTF32TextChar(0xFF08));
		out.Append(words);
		out.AppendW(UTF32TextChar(0xFF09));
		out.SetTranslatable(kFalse);
		return out;
	}
}

#endif // __KFCJapanese_h__

// End, KFCJapanese.h.
