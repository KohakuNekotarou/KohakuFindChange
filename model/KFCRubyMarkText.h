//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  RUBY WRITTEN OUT IN TEXT, AS A MANUSCRIPT WRITES IT: a bar, a base, and its reading in book title marks - Aozora Bunko's way
//  (https://www.aozora.gr.jp/annotation/etc.html) - read one way for the text of a document (Ruby Marks in Text, KFCRubySearch) and a
//  ruby list's lines (KFCRubyList.h). WITH NO BAR, AOZORA BUNKO'S RULE (the author's call, 2026-11-07): the run of
//  one kind of character just before the reading is the base (BaseKindOf). Until then kanji alone were, narrower on
//  purpose: the marks of a book's title in a body text read as ruby over the kana before them now - such a row is
//  left unchosen (the How to Use page says so).
//  STD ONLY: its test runs outside InDesign (work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1).
//
//========================================================================================

#ifndef __KFCRubyMarkText_h__
#define __KFCRubyMarkText_h__

#include <cstddef>
#include <string>

namespace KFCRubyMarkText
{
	// U+300A and U+300B: the reading's marks. U+FF5C or U+007C: the bar.
	const char32_t kMarkOpen = 0x300A;
	const char32_t kMarkClose = 0x300B;

	inline bool IsBar(char32_t c)
	{
		return c == 0xFF5C || c == 0x007C;
	}

	/** WHAT IS TAKEN FOR A BASE WITH NO BAR: kanji, and the characters Aozora Bunko counts as kanji though JIS does not (U+3005, U+3006, U+3007, U+30F6; U+4EDD is one already). */
	inline bool IsKanji(char32_t c)
	{
		return (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0x20000 && c <= 0x2FFFF)
			|| c == 0x3005 || c == 0x3006 || c == 0x3007 || c == 0x30F6;
	}

	/** THE KINDS A BASE WITH NO BAR IS A RUN OF (Aozora Bunko's, 2026-11-07): kanji (IsKanji); hiragana with its
	    iteration marks; katakana with the prolonged sound mark and its iteration marks (U+30F6 is kanji's, above);
	    full-width letters and digits; ASCII letters and digits. Anything else - a bracket, a space, the middle dot,
	    half-width katakana - is no base: a bar says where such a base starts. */
	enum BaseKind { kNoBase = 0, kKanjiBase, kHiraganaBase, kKatakanaBase, kFullWidthBase, kAsciiBase };

	inline BaseKind BaseKindOf(char32_t c)
	{
		if (IsKanji(c))
			return kKanjiBase;
		if ((c >= 0x3041 && c <= 0x3096) || c == 0x309D || c == 0x309E)
			return kHiraganaBase;
		if ((c >= 0x30A1 && c <= 0x30FA) || c == 0x30FC || c == 0x30FD || c == 0x30FE)
			return kKatakanaBase;
		if ((c >= 0xFF10 && c <= 0xFF19) || (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A))
			return kFullWidthBase;
		if ((c >= 0x30 && c <= 0x39) || (c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A))
			return kAsciiBase;
		return kNoBase;
	}

	/** No mark runs over these: a paragraph's end, a table's or an anchor's character, anything of InDesign's in the text. */
	inline bool StopsMark(char32_t c)
	{
		return c < 0x20 || c == 0xFEFF || (c >= 0xE000 && c <= 0xF8FF);
	}

	struct MarkPlace
	{
		size_t	first;		// the bar, or the base when there is none
		size_t	baseStart;
		size_t	open;		// the opening mark; the base ends here
		size_t	close;		// the closing mark
	};

	/** THE MARK WHOSE READING OPENS AT `open`, if it is one: a reading of something, closed before the line ends, after a base - the text from a bar since the last mark (the bar's reach), or else the
	    run of one kind just before it (BaseKindOf). */
	inline bool MarkAtOpen(const std::u32string& text, size_t open, MarkPlace& out)
	{
		size_t close = open + 1;
		while (close < text.size() && text[close] != kMarkClose && text[close] != kMarkOpen && !StopsMark(text[close]))
			++close;
		if (close >= text.size() || text[close] != kMarkClose || close == open + 1)
			return false;
		size_t back = open;
		while (back > 0 && !IsBar(text[back - 1]) && text[back - 1] != kMarkClose && text[back - 1] != kMarkOpen && !StopsMark(text[back - 1]))
			--back;
		if (back > 0 && IsBar(text[back - 1]))
		{
			if (back == open)
				return false;		// a bar and no base
			out.first = back - 1;
			out.baseStart = back;
		}
		else
		{
			const BaseKind kind = (open > 0) ? BaseKindOf(text[open - 1]) : kNoBase;
			size_t base = open;
			while (kind != kNoBase && base > 0 && BaseKindOf(text[base - 1]) == kind)
				--base;
			if (base == open)
				return false;		// not the base of anything
			out.first = base;
			out.baseStart = base;
		}
		out.open = open;
		out.close = close;
		return true;
	}
}

#endif // __KFCRubyMarkText_h__

// End, KFCRubyMarkText.h.
