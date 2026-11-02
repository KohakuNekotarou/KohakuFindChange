//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  RUBY WRITTEN OUT IN TEXT, AS A MANUSCRIPT WRITES IT: a bar, a base, and its reading in book title marks - Aozora Bunko's way
//  (https://www.aozora.gr.jp/annotation/etc.html) - read one way for the text of a document (Ruby Marks in Text, KFCRubySearch) and a
//  ruby list's lines (KFCRubyList.h). NARROWER THAN AOZORA BUNKO ON PURPOSE: with no bar it takes one script's run for a base, and here
//  kanji alone are a base with no bar - the marks of a book's title in a body text would read as ruby over the kana before them.
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
	    kanji just before it. */
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
			size_t kanji = open;
			while (kanji > 0 && IsKanji(text[kanji - 1]))
				--kanji;
			if (kanji == open)
				return false;		// not the base of anything
			out.first = kanji;
			out.baseStart = kanji;
		}
		out.open = open;
		out.close = close;
		return true;
	}
}

#endif // __KFCRubyMarkText_h__

// End, KFCRubyMarkText.h.
