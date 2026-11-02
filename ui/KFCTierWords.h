//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT OF A TIER'S NAME FADES (the author, 11-02: on a results heading, "Find:" and the like "in the fainter colour", as a row's context beside its match, and "the quotes too"): the words a name says what it asked in - KFCResultHistory's [<tab>] Find:"<find>" Change:"<change>" -
//  so that what was asked reads at full strength. Only those it put there: "Find:" in a searched text, or in a run's or a file's name, stays. STD ONLY: its test runs outside InDesign (work/note-scripts/2026-11-02-kfc-ruby-list/list_test.ps1).
//
//========================================================================================

#ifndef __KFCTierWords_h__
#define __KFCTierWords_h__

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace KFCTierWords
{
	struct Run
	{
		size_t	start;
		size_t	length;
		bool	faded;
	};

	/** The name, whole, in runs left to right. A name with none of these words is one run. */
	inline std::vector<Run> RunsOf(const std::u32string& name)
	{
		std::vector<std::pair<size_t, size_t> > fades;
		const std::u32string kFind(U"] Find:\"");
		const std::u32string kChange(U"\" Change:\"");
		const size_t find = name.find(kFind);
		if (find != std::u32string::npos)
		{
			const size_t findWord = find + 2;
			const size_t findValue = find + kFind.size();
			fades.push_back(std::make_pair(findWord, kFind.size() - 2));
			// (What a narrowed list adds after its name may quote a layer's name: KFCNarrow.cpp's On Active Layer. The name's own quotes stand before it.)
			const size_t narrowed = name.find(U" / On Active Layer (\"", findValue);
			const size_t last = (narrowed != std::u32string::npos) ? narrowed : name.size();
			const size_t change = (last >= kChange.size()) ? name.rfind(kChange, last - kChange.size()) : std::u32string::npos;
			const bool hasChange = (change != std::u32string::npos && change >= findValue);
			const size_t lastValue = hasChange ? change + kChange.size() : findValue;
			if (hasChange)
			{
				fades.push_back(std::make_pair(change, size_t(1)));
				fades.push_back(std::make_pair(change + 2, kChange.size() - 2));
			}
			const size_t close = (last > lastValue) ? name.rfind(char32_t(0x22), last - 1) : std::u32string::npos;
			if (close != std::u32string::npos && close >= lastValue)
				fades.push_back(std::make_pair(close, size_t(1)));
		}
		std::vector<Run> runs;
		size_t at = 0;
		for (size_t f = 0; f < fades.size(); ++f)
		{
			if (fades[f].first > at)
			{
				const Run plain = { at, fades[f].first - at, false };
				runs.push_back(plain);
			}
			const Run faded = { fades[f].first, fades[f].second, true };
			runs.push_back(faded);
			at = fades[f].first + fades[f].second;
		}
		if (at < name.size() || runs.empty())
		{
			const Run plain = { at, name.size() - at, false };
			runs.push_back(plain);
		}
		return runs;
	}
}

#endif // __KFCTierWords_h__

// End, KFCTierWords.h.
