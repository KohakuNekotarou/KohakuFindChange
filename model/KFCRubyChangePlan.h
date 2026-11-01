//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT THE RUBY PART'S CHANGE BOXES WRITE OVER A ROW'S TEXT (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md
//  F18 - F21). The face holds a row of reading boxes over a row of base boxes (the author: a box for the kanji and a box for the
//  ruby, more with a +, and they can be joined); each reading box is one Piece: its reading, and the base text under it - joined
//  boxes' together. A Run is one ruby to write, by code point (a TextIndex counts them) from the row's start.
//  ONE BOX, ONE RUBY - not one ruby with spaces in its string, as InDesign's own Ruby dialog keeps per-character ruby: read back,
//  each character holds its own reading, which says at once whether it is right (the 8 measured cases in
//  docs/superpowers/specs/2026-09-03-kidmcp-ruby-roundtrip-design.md - a per-character reading with no space puts all of it on the
//  first character, with no word from InDesign).
//  STD ONLY: its test runs outside InDesign (work/note-scripts/2026-11-01-kfc-kohaku-fixes/plan_test.ps1).
//
//========================================================================================

#ifndef __KFCRubyChangePlan_h__
#define __KFCRubyChangePlan_h__

#include <cstddef>
#include <string>
#include <vector>

namespace KFCRubyChangePlan
{
	/** One reading box and the base text under it. */
	struct Piece
	{
		std::u32string	base;
		std::u32string	reading;
	};

	/** One ruby to write: [start, start + length) of the row's text. */
	struct Run
	{
		size_t			start;
		size_t			length;
		std::u32string	reading;
	};

	enum Result
	{
		kWrite,					// the runs say what to write (the characters between them, none)
		kRemove,				// no box reads anything: the row's ruby comes off (the author)
		kBaseDiffers,			// the row's text is not what the base boxes hold
		kReadingOverNoBase,		// a box reads something over no base text, while others have some: nothing tells where it goes
		kCountDiffers			// no base boxes, and readings that are neither one nor one a character
	};

	/** A space that separates and is no text of a box (the full-width one too, as InDesign's Ruby dialog takes it). */
	inline bool IsSpace(char32_t c)
	{
		return c == 0x0020 || c == 0x3000 || c == 0x0009;
	}

	inline std::u32string Compact(const std::u32string& words)
	{
		std::u32string out;
		for (size_t k = 0; k < words.size(); ++k)
			if (!IsSpace(words[k]))
				out.push_back(words[k]);
		return out;
	}

	inline std::u32string Trimmed(const std::u32string& words)
	{
		size_t from = 0, to = words.size();
		while (from < to && IsSpace(words[from]))
			++from;
		while (to > from && IsSpace(words[to - 1]))
			--to;
		return words.substr(from, to - from);
	}

	/** The reading split where it has spaces. */
	inline std::vector<std::u32string> Tokens(const std::u32string& words)
	{
		std::vector<std::u32string> out;
		std::u32string one;
		for (size_t k = 0; k < words.size(); ++k)
		{
			if (!IsSpace(words[k]))
			{
				one.push_back(words[k]);
				continue;
			}
			if (!one.empty())
				out.push_back(one);
			one.clear();
		}
		if (!one.empty())
			out.push_back(one);
		return out;
	}

	/** WHAT THE BOXES ASK, WHATEVER THE ROW: kRemove, kReadingOverNoBase, or kWrite. */
	inline Result Check(const std::vector<Piece>& pieces)
	{
		bool anyReading = false, anyBase = false, readingOverNoBase = false;
		for (size_t p = 0; p < pieces.size(); ++p)
		{
			const bool reads = !Trimmed(pieces[p].reading).empty();
			const bool stands = !Compact(pieces[p].base).empty();
			anyReading = anyReading || reads;
			anyBase = anyBase || stands;
			readingOverNoBase = readingOverNoBase || (reads && !stands);
		}
		if (!anyReading)
			return kRemove;
		return (anyBase && readingOverNoBase) ? kReadingOverNoBase : kWrite;
	}

	/** One reading over `count` characters, the first at at[from]. A reading with spaces, one word a character, is one ruby a character: InDesign's own way of writing per-character ruby, pasted. */
	inline void AddRuns(const std::u32string& reading, const std::vector<size_t>& at, size_t from, size_t count, std::vector<Run>& out)
	{
		const std::vector<std::u32string> words(Tokens(reading));
		if (words.size() > 1 && words.size() == count)
		{
			for (size_t w = 0; w < words.size(); ++w)
			{
				Run run = { at[from + w], 1, words[w] };
				out.push_back(run);
			}
			return;
		}
		Run run = { at[from], at[from + count - 1] + 1 - at[from], reading };
		out.push_back(run);
	}

	/** THE RUNS OF ONE ROW. rowText = the row's whole text. */
	inline Result Make(const std::vector<Piece>& pieces, const std::u32string& rowText, std::vector<Run>& out)
	{
		out.clear();
		const Result asked = Check(pieces);
		if (asked != kWrite)
			return asked;
		// Where the row's characters stand, its spaces aside.
		std::vector<size_t> at;
		for (size_t k = 0; k < rowText.size(); ++k)
			if (!IsSpace(rowText[k]))
				at.push_back(k);
		std::u32string allBase;
		std::vector<std::u32string> readings;
		for (size_t p = 0; p < pieces.size(); ++p)
		{
			allBase += Compact(pieces[p].base);
			const std::u32string reading(Trimmed(pieces[p].reading));
			if (!reading.empty())
				readings.push_back(reading);
		}
		// NO BASE BOXES: the row's text is the base.
		if (allBase.empty())
		{
			if (at.empty())
				return kCountDiffers;
			if (readings.size() == 1)
			{
				const std::vector<std::u32string> words(Tokens(readings[0]));
				if (words.size() > 1 && words.size() == at.size())
					AddRuns(readings[0], at, 0, at.size(), out);
				else
				{
					Run run = { 0, rowText.size(), readings[0] };
					out.push_back(run);
				}
				return kWrite;
			}
			if (readings.size() != at.size())
				return kCountDiffers;
			for (size_t r = 0; r < readings.size(); ++r)
			{
				Run run = { at[r], 1, readings[r] };
				out.push_back(run);
			}
			return kWrite;
		}
		if (Compact(rowText) != allBase)
			return kBaseDiffers;
		size_t next = 0;
		for (size_t p = 0; p < pieces.size(); ++p)
		{
			const size_t count = Compact(pieces[p].base).size();
			if (count == 0)
				continue;
			const std::u32string reading(Trimmed(pieces[p].reading));
			if (!reading.empty())
				AddRuns(reading, at, next, count, out);
			next += count;
		}
		return kWrite;
	}
}

#endif // __KFCRubyChangePlan_h__

// End, KFCRubyChangePlan.h.
