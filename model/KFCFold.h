//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT THE RUBY AND KENTEN KINDS TAKE FOR THE SAME WITH KANA SENSITIVE OR WIDTH SENSITIVE OFF: what InDesign's own Text tab takes, measured character by character
//  (work/note-scripts/2026-11-02-kfc-ruby-list/measure_fold*.jsx with app.findChangeTextOptions - the whole of each table, not its ends):
//    KANA OFF: a hiragana and its katakana, U+3041..U+3094. Not the small ka and ke (U+3095, U+3096), not the iteration marks.
//    WIDTH OFF: an ASCII character and its full-width form but the two straight quotes and the grave (U+0022, U+0027, U+0060); a space and the ideographic space; the six currency and sign forms (the yen's is
//    U+00A5, not the backslash, whose own full-width form is U+FF3C); a half-width kana or punctuation mark and its full-width form. A half-width voiced or semi-voiced mark goes with the kana before it: one
//    character where one exists (ka+mark and ga, wa+mark and U+30F7) and, where none does, the kana alone - so that kana's character alone does not stand in the voiced one (ha+mark is not ha) and the mark
//    is no character of its own (a+mark reads as a; i+mark and e+mark too: InDesign does not take them for U+30F8, U+30F9). A mark with no kana before it stands as itself.
//  Case is compared as it stands: these kinds have no Case Sensitive of their own.
//  A FOLD KEEPS WHERE EACH CHARACTER CAME FROM, for a match's place in the text it is found in: a voiced half-width pair is two characters there, and one here.
//  STD ONLY: its test runs outside InDesign (work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1).
//
//========================================================================================

#ifndef __KFCFold_h__
#define __KFCFold_h__

#include <cstddef>
#include <string>
#include <vector>

namespace KFCFold
{
	/** The two switches: on = compared as they stand. */
	struct Flags
	{
		bool	kanaSensitive;
		bool	widthSensitive;

		Flags() : kanaSensitive(true), widthSensitive(true) {}
		Flags(bool kana, bool width) : kanaSensitive(kana), widthSensitive(width) {}
	};

	/** A text folded: `from[k]` = where character k of `text` starts in what was folded, and one more at the end, that text's size. */
	struct Folded
	{
		std::u32string			text;
		std::vector<size_t>		from;
	};

	/** The full-width form of a half-width kana or mark of U+FF61..U+FF9D. */
	inline char32_t FullOfHalf(char32_t half)
	{
		static const char16_t kFull[] =
		{
			0x3002, 0x300C, 0x300D, 0x3001, 0x30FB, 0x30F2, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3, 0x30FC,
			0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF, 0x30B1, 0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30BF,
			0x30C1, 0x30C4, 0x30C6, 0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8, 0x30DB, 0x30DE, 0x30DF,
			0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9, 0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3
		};
		return static_cast<char32_t>(kFull[half - 0xFF61]);
	}

	/** A full-width katakana with a voiced mark: the one character there is, or 0. */
	inline char32_t VoicedOf(char32_t kana)
	{
		const bool oneUp = (kana >= 0x30AB && kana <= 0x30C1 && (kana % 2) == 1)		// KA..TI, each a code before its voiced one
			|| (kana >= 0x30C4 && kana <= 0x30C8 && (kana % 2) == 0)					// TU, TE, TO
			|| (kana >= 0x30CF && kana <= 0x30DB && ((kana - 0x30CF) % 3) == 0);		// HA..HO
		if (oneUp)
			return kana + 1;
		if (kana == 0x30A6)
			return 0x30F4;		// U
		if (kana == 0x30EF)
			return 0x30F7;		// WA
		if (kana == 0x30F2)
			return 0x30FA;		// WO
		return 0;
	}

	/** A full-width katakana with a semi-voiced mark: the one character there is, or 0. */
	inline char32_t SemiVoicedOf(char32_t kana)
	{
		return (kana >= 0x30CF && kana <= 0x30DB && ((kana - 0x30CF) % 3) == 0) ? kana + 2 : 0;
	}

	/** The form a full-width character is compared in with Width Sensitive off, or itself. */
	inline char32_t NarrowOf(char32_t c)
	{
		if (c >= 0xFF01 && c <= 0xFF5E)
			return (c == 0xFF02 || c == 0xFF07 || c == 0xFF40) ? c : c - 0xFEE0;
		switch (c)
		{
		case 0x3000: return 0x20;
		case 0xFFE0: return 0xA2;
		case 0xFFE1: return 0xA3;
		case 0xFFE2: return 0xAC;
		case 0xFFE3: return 0xAF;
		case 0xFFE4: return 0xA6;
		case 0xFFE5: return 0xA5;
		case 0xFFE6: return 0x20A9;
		default: return c;
		}
	}

	/** THE TEXT AS IT IS COMPARED. Both switches on: itself, character for character. */
	inline Folded Fold(const std::u32string& in, const Flags& flags)
	{
		Folded out;
		out.text.reserve(in.size());
		out.from.reserve(in.size() + 1);
		for (size_t k = 0; k < in.size(); ++k)
		{
			out.from.push_back(k);
			char32_t c = in[k];
			if (!flags.widthSensitive)
			{
				c = NarrowOf(c);
				if (c >= 0xFF61 && c <= 0xFF9D)
				{
					// (From WO on, U+FF66, a kana: the five before it are marks of punctuation, which take no voiced mark.)
					const bool kana = (c >= 0xFF66);
					c = FullOfHalf(c);
					if (kana && k + 1 < in.size() && (in[k + 1] == 0xFF9E || in[k + 1] == 0xFF9F))
					{
						const char32_t one = (in[k + 1] == 0xFF9E) ? VoicedOf(c) : SemiVoicedOf(c);
						if (one != 0)
							c = one;
						++k;
					}
				}
			}
			if (!flags.kanaSensitive && c >= 0x3041 && c <= 0x3094)
				c += 0x60;
			out.text.push_back(c);
		}
		out.from.push_back(in.size());
		return out;
	}

	/** The compared form alone: of what is looked for, which has no place to go back to. */
	inline std::u32string TextOf(const std::u32string& in, const Flags& flags)
	{
		return (flags.kanaSensitive && flags.widthSensitive) ? in : Fold(in, flags).text;
	}
}

#endif // __KFCFold_h__

// End, KFCFold.h.
