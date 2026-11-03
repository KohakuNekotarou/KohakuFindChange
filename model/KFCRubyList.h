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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "KFCFold.h"				// what a word and the text take for the same
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
		// THE SAME WORDS AS THEY ARE COMPARED with Kana Sensitive or Width Sensitive off (KFCFold.h): each word folded, its runs over the folded characters - ForFlags. Made as the file is read: a file's words are read once for a search, and looked at once for every place in the text.
		std::shared_ptr<const Words>			kanaOff;
		std::shared_ptr<const Words>			widthOff;
		std::shared_ptr<const Words>			bothOff;
	};

	/** HOW EACH WORD IS READ IN WHAT IS SEARCHED (Inconsistent Ruby - the author's proposal 3, 11-02): a word - the base of rubies that touch one another, folded as the search compares - and how many places read it each way (the
	    reading folded, its spaces out: characters given their readings one by one and a group reading of the same words read alike). Counted over all of what is searched before a row is made (KFCRubySearch). */
	struct Readings
	{
		std::map<std::u32string, std::map<std::u32string, int32> >	byWord;
		// ...and each reading as the page has it at its first place (2026-11-03 - a heading's and an "unify" item's words: the folded reading is how the search compares, not what the person wrote - spaces
		// out, the characters as they stand).
		std::map<std::u32string, std::map<std::u32string, std::u32string> >	shown;

		/** Is the word read two ways or more? */
		bool Varies(const std::u32string& word) const
		{
			const std::map<std::u32string, std::map<std::u32string, int32> >::const_iterator it = byWord.find(word);
			return it != byWord.end() && it->second.size() >= 2;
		}

		/** A reading of the word as the page has it - the folded one itself when it was never counted. */
		std::u32string Shown(const std::u32string& word, const std::u32string& reading) const
		{
			const std::map<std::u32string, std::map<std::u32string, std::u32string> >::const_iterator w = shown.find(word);
			if (w == shown.end())
				return reading;
			const std::map<std::u32string, std::u32string>::const_iterator r = w->second.find(reading);
			return (r != w->second.end()) ? r->second : reading;
		}
	};

	/** The look-up tables of a set of entries. */
	inline void IndexWords(Words& words)
	{
		words.byFirst.clear();
		for (size_t e = 0; e < words.entries.size(); ++e)
			words.byFirst[words.entries[e].word[0]].push_back(e);
		for (std::map<char32_t, std::vector<size_t> >::iterator first = words.byFirst.begin(); first != words.byFirst.end(); ++first)
		{
			const std::vector<Entry>& entries = words.entries;
			std::stable_sort(first->second.begin(), first->second.end(), [&entries](size_t a, size_t b) { return entries[a].word.size() > entries[b].word.size(); });
		}
	}

	/** THE WORDS AS THEY ARE COMPARED. Two lines' words that fold as one are one word, the earlier line's: the file is right as it stands (a word with two readings is said as it is read), and which of two spellings a person meant is not asked here. */
	inline std::shared_ptr<const Words> FoldedWords(const Words& raw, const KFCFold::Flags& flags)
	{
		std::shared_ptr<Words> out(new Words());
		for (size_t e = 0; e < raw.entries.size(); ++e)
		{
			const Entry& entry = raw.entries[e];
			const KFCFold::Folded folded(KFCFold::Fold(entry.word, flags));
			if (folded.text.empty() || out->byWord.find(folded.text) != out->byWord.end())
				continue;
			Entry each;
			each.word = folded.text;
			each.line = entry.line;
			for (size_t r = 0; r < entry.runs.size(); ++r)
			{
				const KFCRubyChangePlan::Run& run = entry.runs[r];
				const size_t first = static_cast<size_t>(std::lower_bound(folded.from.begin(), folded.from.end(), run.start) - folded.from.begin());
				const size_t past = static_cast<size_t>(std::lower_bound(folded.from.begin(), folded.from.end(), run.start + run.length) - folded.from.begin());
				if (past > first)
				{
					KFCRubyChangePlan::Run foldedRun = { first, past - first, run.reading };
					each.runs.push_back(foldedRun);
				}
			}
			if (each.runs.empty())
				continue;
			out->byWord[each.word] = out->entries.size();
			out->entries.push_back(each);
		}
		IndexWords(*out);
		return out;
	}

	/** The words to look with, as the switches say - words' own with both on. */
	inline const Words& ForFlags(const Words& words, const KFCFold::Flags& flags)
	{
		if (flags.kanaSensitive && flags.widthSensitive)
			return words;
		const std::shared_ptr<const Words>& folded = (!flags.kanaSensitive && !flags.widthSensitive) ? words.bothOff : (!flags.kanaSensitive) ? words.kanaOff : words.widthOff;
		return (folded != nullptr) ? *folded : words;
	}

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
		IndexWords(out);
		out.kanaOff = FoldedWords(out, KFCFold::Flags(false, true));
		out.widthOff = FoldedWords(out, KFCFold::Flags(true, false));
		out.bothOff = FoldedWords(out, KFCFold::Flags(false, false));
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

	/** THE WORD A ROW'S TEXT IS, AS THE SWITCHES COMPARE IT, and the rubies it takes over the row's own characters: outRuns. False = no word of the list (the row's text has been edited). */
	inline bool RowPlan(const Words& words, const KFCFold::Flags& flags, const std::u32string& rowText, std::vector<KFCRubyChangePlan::Run>& outRuns)
	{
		outRuns.clear();
		const KFCFold::Folded row(KFCFold::Fold(rowText, flags));
		const Entry* const entry = FindWord(ForFlags(words, flags), row.text);
		if (entry == nullptr)
			return false;
		for (size_t r = 0; r < entry->runs.size(); ++r)
		{
			const KFCRubyChangePlan::Run& run = entry->runs[r];
			KFCRubyChangePlan::Run each = { row.from[run.start], row.from[run.start + run.length] - row.from[run.start], run.reading };
			outRuns.push_back(each);
		}
		return true;
	}

	/** A WORD AS A LINE OF A LIST - what ParseCell reads back (Save List...): each ruby's base and its reading, a bar where the base is more than kanji; a line the first-cell rule would cut, quoted. */
	inline std::u32string LineOf(const Entry& entry)
	{
		std::u32string line;
		size_t at = 0;
		for (size_t r = 0; r < entry.runs.size(); ++r)
		{
			const KFCRubyChangePlan::Run& run = entry.runs[r];
			line.append(entry.word, at, run.start - at);
			bool kanji = (run.start == at);		// (a character standing before a base could be taken for its base)
			for (size_t k = run.start; kanji && k < run.start + run.length; ++k)
				kanji = KFCRubyMarkText::IsKanji(entry.word[k]);
			if (!kanji)
				line.push_back(0xFF5C);
			line.append(entry.word, run.start, run.length);
			line.push_back(KFCRubyMarkText::kMarkOpen);
			line.append(run.reading);
			line.push_back(KFCRubyMarkText::kMarkClose);
			at = run.start + run.length;
		}
		line.append(entry.word, at, std::u32string::npos);
		if (line.find(char32_t(0x2C)) == std::u32string::npos && line.find(char32_t(0x09)) == std::u32string::npos && line.find(char32_t(0x22)) == std::u32string::npos)
			return line;
		std::u32string quoted(1, char32_t(0x22));
		for (size_t k = 0; k < line.size(); ++k)
		{
			if (line[k] == 0x22)
				quoted.push_back(line[k]);
			quoted.push_back(line[k]);
		}
		quoted.push_back(0x22);
		return quoted;
	}

	/** Does the line read back as the word, when the file is read? (A base or a reading that holds a mark of its own does not: `LineOf` has no way of writing it.) */
	inline bool LineReadsBack(const std::u32string& line, const Entry& entry)
	{
		if (line.find(char32_t(0x0D)) != std::u32string::npos || line.find(char32_t(0x0A)) != std::u32string::npos)
			return false;
		const std::u32string cell(FirstCell(line));
		Entry back;
		Why why = kOk;
		return !cell.empty() && cell[0] != 0x23 && cell[0] != 0xFF03 && ParseCell(cell, back, why) && back.word == entry.word;
	}

	/** WHAT A DOCUMENT'S RUBY COMES TO AS A LIST (Save List... - the author, 11-02: "the same word once"): each word once, as the rubies touching one another read. A WORD READ TWO WAYS gets a line for each way with the character after it (the author: "the next character, for the words whose reading splits" - futo-ru and ta-da, each read back as itself): the rest of a list reads as it is, the characters a word is followed by being many, and few of them saying anything. A place with no such character is the word's bare line. Two ways over the same characters (the word and the character after it the same, or one word given its readings as a group and as a character each - that is no other reading) are one line, the way most of its places read it - a list holds one reading for a word, so the rest stand under it as comments with how many places read so: what the person is to look at. Ties: the way met first. */
	class ListMaker
	{
	public:
		ListMaker() : fWritten(0), fSplit(0), fApart(0), fUnwritable(0) {}

		/** One place: the word its rubies make, and the character after it in the text (0 = none a line could use: the caller's to say what stands there is text). */
		void Add(const Entry& found, char32_t after = 0)
		{
			const std::map<std::u32string, size_t>::const_iterator known = fIndex.find(found.word);
			const size_t w = (known != fIndex.end()) ? known->second : fWords.size();
			if (known == fIndex.end())
			{
				fIndex[found.word] = w;
				fWords.emplace_back();
				fWords.back().word = found.word;
			}
			SeenWord& word = fWords[w];
			const std::u32string line(LineOf(found));
			if (!LineReadsBack(line, found))
			{
				++word.unwritable;
				return;
			}
			size_t v = 0;
			while (v < word.ways.size() && word.ways[v].line != line)
				++v;
			if (v == word.ways.size())
			{
				SeenWay way;
				way.entry = found;
				way.line = line;
				way.count = 0;
				word.ways.push_back(way);
			}
			SeenWay& way = word.ways[v];
			++way.count;
			size_t a = 0;
			while (a < way.after.size() && way.after[a].first != after)
				++a;
			if (a == way.after.size())
				way.after.push_back(std::make_pair(after, size_t(0)));
			++way.after[a].second;
		}

		/** The file's text, its lines ended as Windows ends them, under a head of a comment. */
		std::u32string Text(const std::u32string& headWords)
		{
			static const std::u32string kEol(U"\r\n");
			fSplit = 0;
			fApart = 0;
			fUnwritable = 0;
			// THE WORDS THE LINES WILL HOLD, in the order they came: one word of a line may be reached twice - a word read two ways, with its next character, and a longer word of its own that reads those same characters. A file holds one line for a word.
			std::vector<SavedWord> saved;
			std::map<std::u32string, size_t> savedAt;
			const auto savedWord = [&](const std::u32string& word) -> SavedWord&
			{
				const std::map<std::u32string, size_t>::const_iterator known = savedAt.find(word);
				if (known != savedAt.end())
					return saved[known->second];
				savedAt[word] = saved.size();
				saved.emplace_back();
				saved.back().word = word;
				return saved.back();
			};
			const auto put = [&](const std::u32string& word, const std::u32string& line, size_t count)
			{
				SavedWord& into = savedWord(word);
				for (size_t v = 0; v < into.ways.size(); ++v)
					if (into.ways[v].line == line)
					{
						into.ways[v].count += count;
						return;
					}
				Way way;
				way.line = line;
				way.count = count;
				into.ways.push_back(way);
			};
			for (size_t w = 0; w < fWords.size(); ++w)
			{
				SeenWord& seen = fWords[w];
				std::stable_sort(seen.ways.begin(), seen.ways.end(), [](const SeenWay& a, const SeenWay& b) { return a.count > b.count; });
				const bool split = ReadingsDiffer(seen);
				bool apart = false;
				for (size_t v = 0; v < seen.ways.size(); ++v)
				{
					SeenWay& way = seen.ways[v];
					if (!split)
					{
						put(seen.word, way.line, way.count);
						continue;
					}
					std::stable_sort(way.after.begin(), way.after.end(), [](const std::pair<char32_t, size_t>& a, const std::pair<char32_t, size_t>& b) { return a.second > b.second; });
					for (size_t a = 0; a < way.after.size(); ++a)
					{
						Entry longer(way.entry);
						if (way.after[a].first != 0)
							longer.word.push_back(way.after[a].first);
						const std::u32string line(LineOf(longer));
						// (A character no line can hold after the word - a mark's own, say: the word's bare line.)
						if (way.after[a].first != 0 && LineReadsBack(line, longer))
						{
							put(longer.word, line, way.after[a].second);
							apart = true;
						}
						else
							put(seen.word, way.line, way.after[a].second);
					}
				}
				if (apart)
					++fApart;
				if (seen.unwritable > 0)
					savedWord(seen.word).unwritable += seen.unwritable;
			}
			size_t written = 0;
			std::u32string body;
			for (size_t w = 0; w < saved.size(); ++w)
			{
				SavedWord& word = saved[w];
				std::stable_sort(word.ways.begin(), word.ways.end(), [](const Way& a, const Way& b) { return a.count > b.count; });
				if (!word.ways.empty())
				{
					++written;
					body += word.ways[0].line + kEol;
					if (word.ways.size() > 1)
						++fSplit;
				}
				for (size_t v = 1; v < word.ways.size(); ++v)
					body += U"# another reading, " + Number(word.ways[v].count) + U" of " + Number(Places(word)) + U" places: " + word.ways[v].line + kEol;
				if (word.unwritable > 0)
				{
					++fUnwritable;
					body += U"# no line can hold this, " + Number(word.unwritable) + U" places: " + Plain(word.word) + kEol;
				}
			}
			fWritten = written;
			return U"# " + Plain(headWords) + U": " + Number(written) + U" words" + kEol + body;
		}

		size_t Written() const { return fWritten; }
		/** Words of the file left with # lines of other readings. */
		size_t Split() const { return fSplit; }
		/** Words read two ways that the character after them told apart. */
		size_t Apart() const { return fApart; }
		size_t Unwritable() const { return fUnwritable; }
		bool Empty() const { return fWords.empty(); }

	private:
		// What the document held: each word, each way it was read, how many places, and what stood after them.
		struct SeenWay
		{
			Entry			entry;
			std::u32string	line;
			size_t			count;
			std::vector<std::pair<char32_t, size_t> >	after;
			SeenWay() : count(0) {}
		};
		struct SeenWord
		{
			std::u32string			word;
			std::vector<SeenWay>	ways;
			size_t					unwritable;
			SeenWord() : unwritable(0) {}
		};
		// What the file holds.
		struct Way
		{
			std::u32string	line;
			size_t			count;
		};
		struct SavedWord
		{
			std::u32string		word;
			std::vector<Way>	ways;
			size_t				unwritable;
			SavedWord() : unwritable(0) {}
		};

		// Is the word read two ways - in what it says, not in how the rubies stand over it?
		static bool ReadingsDiffer(const SeenWord& word)
		{
			std::u32string first;
			for (size_t v = 0; v < word.ways.size(); ++v)
			{
				std::u32string reading;
				for (size_t r = 0; r < word.ways[v].entry.runs.size(); ++r)
					reading += word.ways[v].entry.runs[r].reading;
				if (v == 0)
					first = reading;
				else if (reading != first)
					return true;
			}
			return false;
		}

		static size_t Places(const SavedWord& word)
		{
			size_t places = 0;
			for (size_t v = 0; v < word.ways.size(); ++v)
				places += word.ways[v].count;
			return places;
		}

		static std::u32string Number(size_t n)
		{
			std::u32string digits;
			do
			{
				digits.insert(digits.begin(), static_cast<char32_t>(0x30 + (n % 10)));
				n /= 10;
			} while (n > 0);
			return digits;
		}

		// A comment's words on one line.
		static std::u32string Plain(const std::u32string& words)
		{
			std::u32string out(words);
			for (size_t k = 0; k < out.size(); ++k)
				if (KFCRubyMarkText::StopsMark(out[k]))
					out[k] = 0x20;
			return out;
		}

		std::vector<SeenWord>				fWords;
		std::map<std::u32string, size_t>	fIndex;
		size_t								fWritten;
		size_t								fSplit;
		size_t								fApart;
		size_t								fUnwritable;
	};

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
