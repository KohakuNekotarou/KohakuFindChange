//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE PLACES A QUERY RUN WROTE IN ONE STORY, AND WHAT EACH HAS BEEN (docs/superpowers/specs/2026-10-30-kfc-run-result-list-design.md
//  R5 and the author's D1-D4): a place's range in the story's text as it stands, and its chain - the text it started as and
//  the text after each query that wrote it. Each query's write is laid over the places: a place before the write stays,
//  one after it moves by what the write added, and a match over a place joins them into one place, read from the original
//  text (D2). A place stays when its text is gone (D3) and when it has come back to what it was (D4).
//  std only, no InDesign type: built and run on its own (work/note-scripts/2026-10-30-kfc-run-list/align_test.ps1).
//
//========================================================================================

#ifndef __KFCRunListChain_h__
#define __KFCRunListChain_h__

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "KFCRunListAlign.h"

namespace KFCRunListChain
{
	/** What a place read after a query wrote it. query -1 = what it started as: always the first step. */
	struct Step
	{
		int32_t			query;
		std::u32string	text;
	};

	/** [start, end) of the story's text as it stands, in place order among its story's places, none overlapping. */
	struct Place
	{
		int32_t				start;
		int32_t				end;
		std::vector<Step>	steps;
	};

	/** What a place read once query `query` had gone by: its last step at or before it. */
	inline const std::u32string& TextAfter(const Place& place, int32_t query)
	{
		size_t at = 0;
		for (size_t s = 1; s < place.steps.size() && place.steps[s].query <= query; ++s)
			at = s;
		return place.steps[at].text;
	}

	/** Do the places read in `text` as they say they do? Asked of the story's text before each write and at the end of the
	    run: what moved the text under them (a text variable, a cross-reference) was no write of the run's, and their ranges
	    no longer mean anything then. */
	inline bool ReadsAsKept(const std::vector<Place>& places, const std::u32string& text)
	{
		const int32_t length = static_cast<int32_t>(text.size());
		for (size_t i = 0; i < places.size(); ++i)
		{
			const Place& p = places[i];
			const std::u32string& kept = p.steps.back().text;
			if (p.start < 0 || p.end < p.start || p.end > length || static_cast<size_t>(p.end - p.start) != kept.size()
				|| text.compare(static_cast<size_t>(p.start), kept.size(), kept) != 0)
				return false;
		}
		return true;
	}

	/** One query's write over a story's places (`before` = the story's text before it, `after` = after it, `matches` and
	    `placed` = KFCRunListAlign::Locate's). Places and writes are compared in the text before the write; one touching
	    another without overlapping stays apart. */
	inline void Apply(std::vector<Place>& places, int32_t query, const std::u32string& before, const std::u32string& after,
		const std::vector<KFCRunListAlign::Match>& matches, const std::vector<KFCRunListAlign::Placed>& placed)
	{
		// One write: what it covered before, what it became.
		struct Write { int32_t start, end, newStart, newEnd; };
		const auto writeOf = [&](size_t g)
		{
			const KFCRunListAlign::Placed& w = placed[g];
			return Write{ matches[static_cast<size_t>(w.first)].start, matches[static_cast<size_t>(w.last)].end, w.start, w.end };
		};
		const auto over = [](const Place& p, const Write& w) { return p.start < w.end && w.start < p.end; };
		const auto sub = [&](int32_t from, int32_t to) { return before.substr(static_cast<size_t>(from), static_cast<size_t>(to - from)); };

		std::vector<Place> made;
		made.reserve(places.size() + placed.size());
		size_t next = 0;		// the next place not dealt with
		int32_t delta = 0;		// what the writes so far have added: a position between them, after minus before
		const auto keepBefore = [&](const Write& w)
		{
			while (next < places.size() && !over(places[next], w) && places[next].end <= w.start)
			{
				Place p(std::move(places[next++]));
				p.start += delta;
				p.end += delta;
				made.push_back(std::move(p));
			}
		};
		for (size_t g = 0; g < placed.size(); )
		{
			Write w = writeOf(g);
			keepBefore(w);
			if (next >= places.size() || !over(places[next], w))
			{
				// A new place of this write alone.
				Place p;
				p.start = w.newStart;
				p.end = w.newEnd;
				p.steps.push_back(Step{ -1, sub(w.start, w.end) });
				p.steps.push_back(Step{ query, after.substr(static_cast<size_t>(w.newStart), static_cast<size_t>(w.newEnd - w.newStart)) });
				delta = w.newEnd - w.end;
				made.push_back(std::move(p));
				++g;
				continue;
			}
			// A MATCH OVER A PLACE: every place and write joined by overlapping is one place, and each step of it reads each of
			// those places as it stood then and the text between them, never written (D2).
			const size_t firstPlace = next;
			Write last = w;
			size_t afterGroup = g + 1;
			size_t afterPlaces = next + 1;
			for (bool grew = true; grew; )
			{
				grew = false;
				if (afterPlaces < places.size() && over(places[afterPlaces], last))
				{
					++afterPlaces;
					grew = true;
				}
				if (afterGroup < placed.size() && over(places[afterPlaces - 1], writeOf(afterGroup)))
				{
					last = writeOf(afterGroup);
					++afterGroup;
					grew = true;
				}
			}
			const Place& head = places[firstPlace];
			const Place& tail = places[afterPlaces - 1];
			const int32_t start = std::min(head.start, w.start), end = std::max(tail.end, last.end);
			const int32_t newStart = (w.start <= head.start) ? w.newStart : head.start + delta;
			const int32_t newEnd = (last.end >= tail.end) ? last.newEnd : tail.end + (last.newEnd - last.end);
			std::vector<int32_t> queries(1, -1);
			for (size_t i = firstPlace; i < afterPlaces; ++i)
				for (size_t s = 1; s < places[i].steps.size(); ++s)
					queries.push_back(places[i].steps[s].query);
			std::sort(queries.begin(), queries.end());
			queries.erase(std::unique(queries.begin(), queries.end()), queries.end());
			Place joined;
			joined.start = newStart;
			joined.end = newEnd;
			for (size_t q = 0; q < queries.size(); ++q)
			{
				std::u32string text;
				int32_t at = start;
				for (size_t i = firstPlace; i < afterPlaces; ++i)
				{
					text += sub(at, places[i].start);
					text += TextAfter(places[i], queries[q]);
					at = places[i].end;
				}
				text += sub(at, end);
				joined.steps.push_back(Step{ queries[q], std::move(text) });
			}
			joined.steps.push_back(Step{ query, after.substr(static_cast<size_t>(newStart), static_cast<size_t>(newEnd - newStart)) });
			delta = newEnd - end;
			made.push_back(std::move(joined));
			next = afterPlaces;
			g = afterGroup;
		}
		while (next < places.size())
		{
			Place p(std::move(places[next++]));
			p.start += delta;
			p.end += delta;
			made.push_back(std::move(p));
		}
		places.swap(made);
	}

	/** One step in the row's words: its text, or for no text "(empty)" where a place started (a zero-width match's - nothing was
	    deleted there) and "(deleted)" after a query (D3). */
	inline std::u32string StepWords(const Place& place, size_t step)
	{
		const std::u32string& text = place.steps[step].text;
		if (!text.empty())
			return text;
		return (step == 0) ? U"(empty)" : U"(deleted)";
	}

	/** The chain as a row reads it, whole: its steps joined by an arrow (D1). */
	inline std::u32string ChainText(const Place& place)
	{
		std::u32string out;
		for (size_t s = 0; s < place.steps.size(); ++s)
		{
			if (s > 0)
				out += U" → ";
			out += StepWords(place, s);
		}
		return out;
	}

	/** The queries that wrote a place, in the run's order. */
	inline std::vector<int32_t> QueriesOf(const Place& place)
	{
		std::vector<int32_t> out;
		for (size_t s = 1; s < place.steps.size(); ++s)
			if (out.empty() || out.back() != place.steps[s].query)
				out.push_back(place.steps[s].query);
		return out;
	}
}

#endif // __KFCRunListChain_h__

// End, KFCRunListChain.h.
