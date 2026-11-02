//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A RUBY LIST (docs/superpowers/specs/2026-11-02-kfc-ruby-list-design.md - the author, 11-02: many readings over text that is there, from a file, for the order of the characters says which: the ta
//  of Taro, the futo of futoru). ONE LINE, ONE WORD, in the marks Ruby Marks in Text reads (KFCRubyMarkText.h): the characters left when the marks come out are what is looked for, and a reading goes on
//  the characters its mark says - no columns, which could not tell a reading over the kanji of a word from one over the whole of it. A mark's marks may be written in three more ways here, where nothing
//  else stands to be taken for them. Blank lines and # lines pass; of a CSV or a tab-separated line, the first cell.
//  THE LONGEST WORD AT A PLACE WINS, whatever the file's order; a word with two readings is said, with both lines - nothing tells which.
//  STD ONLY: its test runs outside InDesign (work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1).
//
//========================================================================================

#ifndef __KFCRubyList_h__
#define __KFCRubyList_h__

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "KFCRubyChangePlan.h"		// Run - a list's word writes as the Change boxes do
#include "KFCRubyMarkText.h"

namespace KFCRubyList
{
	/** One word: what is looked for, and the rubies its characters take. */
	struct Entry
	{
		std::u32string							word;
		std::vector<KFCRubyChangePlan::Run>		runs;
		size_t									line;		// its line in the file, from 1
		Entry() : line(0) {}
	};

	enum Why
	{
		kOk = 0,
		kNoReading,		// a line with no mark in it
		kBadMark,		// a mark no reading can be read from: not closed, empty, a bar left over, kana before it with no bar
		kOtherReading	// a word an earlier line gives another reading
	};

	/** What is wrong with a line. */
	struct Problem
	{
		size_t	line;
		Why		why;
		size_t	otherLine;		// kOtherReading: the earlier line
		Problem() : line(0), why(kOk), otherLine(0) {}
	};

	/** A file's words. */
	struct Words
	{
		std::vector<Entry>						entries;
		std::map<char32_t, std::vector<size_t> >	byFirst;	// a word's first character: its entries, the longest first
		std::map<std::u32string, size_t>		byWord;
	};

	/** THE OTHER WAYS OF WRITING THE MARKS, AS THE MARKS: doubled full-width or ASCII less-than and greater-than (U+FF1C U+FF1E, < >), and the much-less-than signs U+226A U+226B, which an input method offers beside them. */
	inline std::u32string WithMarks(const std::u32string& cell)
	{
		std::u32string out;
		out.reserve(cell.size());
		for (size_t k = 0; k < cell.size(); ++k)
		{
			const char32_t c = cell[k];
			const char32_t next = (k + 1 < cell.size()) ? cell[k + 1] : char32_t(0);
			if (c == 0x226A)
				out.push_back(KFCRubyMarkText::kMarkOpen);
			else if (c == 0x226B)
				out.push_back(KFCRubyMarkText::kMarkClose);
			else if ((c == 0xFF1C || c == 0x3C) && next == c)
			{
				out.push_back(KFCRubyMarkText::kMarkOpen);
				++k;
			}
			else if ((c == 0xFF1E || c == 0x3E) && next == c)
			{
				out.push_back(KFCRubyMarkText::kMarkClose);
				++k;
			}
			else
				out.push_back(c);
		}
		return out;
	}

	/** ONE WORD FROM ITS CELL. False = outWhy. */
	inline bool ParseCell(const std::u32string& rawCell, Entry& out, Why& outWhy)
	{
		const std::u32string cell(WithMarks(rawCell));
		out.word.clear();
		out.runs.clear();
		outWhy = kOk;
		size_t at = 0;
		for (size_t open = cell.find(KFCRubyMarkText::kMarkOpen); open != std::u32string::npos; open = cell.find(KFCRubyMarkText::kMarkOpen, at))
		{
			KFCRubyMarkText::MarkPlace mark;
			if (!KFCRubyMarkText::MarkAtOpen(cell, open, mark))
			{
				outWhy = kBadMark;
				return false;
			}
			const std::u32string reading(KFCRubyChangePlan::Trimmed(cell.substr(mark.open + 1, mark.close - mark.open - 1)));
			if (reading.empty())
			{
				outWhy = kBadMark;
				return false;
			}
			out.word.append(cell, at, mark.first - at);
			// (A READING WITH SPACES, A WORD A CHARACTER: one ruby a character - InDesign's own way of writing per-character ruby, as the Change boxes take it: KFCRubyChangePlan's AddRuns.)
			const size_t start = out.word.size();
			const size_t length = mark.open - mark.baseStart;
			const std::vector<std::u32string> words(KFCRubyChangePlan::Tokens(reading));
			if (words.size() > 1 && words.size() == length)
			{
				for (size_t w = 0; w < words.size(); ++w)
				{
					KFCRubyChangePlan::Run each;
					each.start = start + w;
					each.length = 1;
					each.reading = words[w];
					out.runs.push_back(each);
				}
			}
			else
			{
				KFCRubyChangePlan::Run run;
				run.start = start;
				run.length = length;
				run.reading = reading;
				out.runs.push_back(run);
			}
			out.word.append(cell, mark.baseStart, length);
			at = mark.close + 1;
		}
		out.word.append(cell, at, std::u32string::npos);
		// (What is left of a mark in the word is one no reading came from.)
		for (size_t k = 0; k < out.word.size(); ++k)
		{
			const char32_t c = out.word[k];
			if (c == KFCRubyMarkText::kMarkOpen || c == KFCRubyMarkText::kMarkClose || KFCRubyMarkText::IsBar(c))
			{
				outWhy = kBadMark;
				return false;
			}
		}
		if (out.runs.empty())
		{
			outWhy = kNoReading;
			return false;
		}
		return true;
	}

	/** THE CELL A LINE HOLDS ITS WORD IN: the first of a CSV's or a tab-separated line's, its quotes off. */
	inline std::u32string FirstCell(const std::u32string& line)
	{
		size_t from = 0;
		while (from < line.size() && KFCRubyChangePlan::IsSpace(line[from]))
			++from;
		if (from < line.size() && line[from] == 0x22)
		{
			std::u32string quoted;
			for (size_t k = from + 1; k < line.size(); ++k)
			{
				if (line[k] != 0x22)
					quoted.push_back(line[k]);
				else if (k + 1 < line.size() && line[k + 1] == 0x22)
				{
					quoted.push_back(line[k]);
					++k;
				}
				else
					break;
			}
			return KFCRubyChangePlan::Trimmed(quoted);
		}
		size_t to = from;
		while (to < line.size() && line[to] != 0x2C && line[to] != 0x09)
			++to;
		return KFCRubyChangePlan::Trimmed(line.substr(from, to - from));
	}

	inline bool SameRuns(const std::vector<KFCRubyChangePlan::Run>& a, const std::vector<KFCRubyChangePlan::Run>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t r = 0; r < a.size(); ++r)
			if (a[r].start != b[r].start || a[r].length != b[r].length || a[r].reading != b[r].reading)
				return false;
		return true;
	}

	/** A FILE'S TEXT, READ. outProblem = the first line wrong, outProblems = how many are; the words of the lines that are right stand either way - the caller says the first and goes no further. */
	inline void Parse(const std::u32string& text, Words& out, Problem& outProblem, size_t& outProblems)
	{
		out = Words();
		outProblem = Problem();
		outProblems = 0;
		const auto say = [&](size_t line, Why why, size_t otherLine)
		{
			if (outProblems == 0)
			{
				outProblem.line = line;
				outProblem.why = why;
				outProblem.otherLine = otherLine;
			}
			++outProblems;
		};
		size_t from = (!text.empty() && text[0] == 0xFEFF) ? 1 : 0;
		size_t lineNumber = 0;
		while (from <= text.size())
		{
			size_t end = from;
			while (end < text.size() && text[end] != 0x0D && text[end] != 0x0A)
				++end;
			++lineNumber;
			const std::u32string cell(FirstCell(text.substr(from, end - from)));
			if (!cell.empty() && cell[0] != 0x23 && cell[0] != 0xFF03)
			{
				Entry entry;
				Why why = kOk;
				if (!ParseCell(cell, entry, why))
					say(lineNumber, why, 0);
				else
				{
					entry.line = lineNumber;
					const std::map<std::u32string, size_t>::const_iterator again = out.byWord.find(entry.word);
					if (again == out.byWord.end())
					{
						out.byWord[entry.word] = out.entries.size();
						out.entries.push_back(entry);
					}
					else if (!SameRuns(out.entries[again->second].runs, entry.runs))
						say(lineNumber, kOtherReading, out.entries[again->second].line);
				}
			}
			if (end >= text.size())
				break;
			from = end + ((text[end] == 0x0D && end + 1 < text.size() && text[end + 1] == 0x0A) ? 2 : 1);
		}
		for (size_t e = 0; e < out.entries.size(); ++e)
			out.byFirst[out.entries[e].word[0]].push_back(e);
		for (std::map<char32_t, std::vector<size_t> >::iterator first = out.byFirst.begin(); first != out.byFirst.end(); ++first)
		{
			const std::vector<Entry>& entries = out.entries;
			std::stable_sort(first->second.begin(), first->second.end(), [&entries](size_t a, size_t b) { return entries[a].word.size() > entries[b].word.size(); });
		}
	}

	/** THE LONGEST WORD STANDING AT `at` IN THE TEXT, or nullptr. */
	inline const Entry* LongestAt(const Words& words, const std::u32string& text, size_t at)
	{
		if (at >= text.size())
			return nullptr;
		const std::map<char32_t, std::vector<size_t> >::const_iterator first = words.byFirst.find(text[at]);
		if (first == words.byFirst.end())
			return nullptr;
		for (size_t c = 0; c < first->second.size(); ++c)
		{
			const Entry& entry = words.entries[first->second[c]];
			if (text.compare(at, entry.word.size(), entry.word) == 0)
				return &entry;
		}
		return nullptr;
	}

	/** The word, as a row's text reads, or nullptr. */
	inline const Entry* FindWord(const Words& words, const std::u32string& word)
	{
		const std::map<std::u32string, size_t>::const_iterator found = words.byWord.find(word);
		return (found != words.byWord.end()) ? &words.entries[found->second] : nullptr;
	}

	/** A FILE'S BYTES AS UNICODE, when they say so: a UTF-16 BOM, or whole UTF-8 (RFC 3629's table - with its BOM or not). False = neither: the caller's next guess is what a Japanese Windows writes, CP932. */
	inline bool DecodeUnicode(const std::string& bytes, std::u32string& out)
	{
		out.clear();
		const size_t size = bytes.size();
		const auto byteAt = [&bytes](size_t k) { return static_cast<unsigned int>(static_cast<unsigned char>(bytes[k])); };
		if (size >= 2 && ((byteAt(0) == 0xFF && byteAt(1) == 0xFE) || (byteAt(0) == 0xFE && byteAt(1) == 0xFF)))
		{
			const bool little = (byteAt(0) == 0xFF);
			if ((size % 2) != 0)
				return false;
			const auto unitAt = [&](size_t k) { return little ? (byteAt(k) | (byteAt(k + 1) << 8)) : ((byteAt(k) << 8) | byteAt(k + 1)); };
			for (size_t k = 2; k < size; k += 2)
			{
				const unsigned int unit = unitAt(k);
				if (unit >= 0xDC00 && unit <= 0xDFFF)
					return false;
				if (unit >= 0xD800 && unit <= 0xDBFF)
				{
					if (k + 3 >= size)
						return false;
					const unsigned int low = unitAt(k + 2);
					if (low < 0xDC00 || low > 0xDFFF)
						return false;
					out.push_back(static_cast<char32_t>(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00)));
					k += 2;
				}
				else
					out.push_back(static_cast<char32_t>(unit));
			}
			return true;
		}
		size_t k = (size >= 3 && byteAt(0) == 0xEF && byteAt(1) == 0xBB && byteAt(2) == 0xBF) ? 3 : 0;
		while (k < size)
		{
			const unsigned int lead = byteAt(k);
			size_t length = 1;
			unsigned int code = lead;
			unsigned int smallest = 0;
			if (lead >= 0x80)
			{
				if (lead >= 0xC2 && lead <= 0xDF)
				{
					length = 2;
					code = lead & 0x1F;
					smallest = 0x80;
				}
				else if (lead >= 0xE0 && lead <= 0xEF)
				{
					length = 3;
					code = lead & 0x0F;
					smallest = 0x800;
				}
				else if (lead >= 0xF0 && lead <= 0xF4)
				{
					length = 4;
					code = lead & 0x07;
					smallest = 0x10000;
				}
				else
					return false;
				if (size - k < length)
					return false;
				for (size_t c = 1; c < length; ++c)
				{
					const unsigned int next = byteAt(k + c);
					if ((next & 0xC0) != 0x80)
						return false;
					code = (code << 6) | (next & 0x3F);
				}
				if (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
					return false;
			}
			out.push_back(static_cast<char32_t>(code));
			k += length;
		}
		return true;
	}
}

#endif // __KFCRubyList_h__

// End, KFCRubyList.h.
