//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHERE EACH MATCH'S REPLACEMENT STANDS IN THE TEXT AFTER A CHANGE ALL (docs/superpowers/specs/2026-10-30-kfc-run-result-list-design.md
//  R3). InDesign's Change All says how many it wrote and nothing of where (no per-match command to watch - the design's M1),
//  so a query run takes the matches with a find just before it, and places them here: the text outside the matches is
//  what the write left alone, so it is laid on the text after the write and what lies between is what was written.
//  Answered only as far as the text can tell - nothing guessed: an edge that the text leaves open joins the matches either
//  side of it into one place, and text that cannot be laid at all is false (KFCRunList leaves the document out and says so).
//  std only, no InDesign type: built and run on its own (work/note-scripts/2026-10-30-kfc-run-list/align_test.ps1).
//
//========================================================================================

#ifndef __KFCRunListAlign_h__
#define __KFCRunListAlign_h__

#include <cstdint>
#include <string>
#include <vector>

namespace KFCRunListAlign
{
	/** A match in the text before the write: [start, end). A list of them is in place order and does not overlap. A text
	    position counts a code point as one (ITextModel's TextIndex), as a u32string does. */
	struct Match
	{
		int32_t	start;
		int32_t	end;
	};

	/** matches[first..last] and what they became: [start, end) of the text after the write. first == last unless the edge
	    between them could not be told. */
	struct Placed
	{
		int32_t	first;
		int32_t	last;
		int32_t	start;
		int32_t	end;
	};

	/** WHAT THE QUERY'S CHANGE TO WRITES, as far as the query itself can say. */
	enum Writes
	{
		kAnyLength,		// a length of its own for each match: found text in it ($0, $1 ...), or a character type's conversion
		kOneLength,		// one length whatever it matches: a Change To with no found text in it - what lets adjacent matches stay
						// apart with no text between them ("inu inu" to "neko neko" each is two places)
		kEmptyChange	// an empty Change To: nothing written (one length, 0) - or, with a Change Format, the text left as it is
	};

	/** after = before with each match written over. False = the text after is not that (the edge of an unchanged stretch is not
	    where it has to be, or the matches are not in order). The answer, tried in this order:
	    - the text as it was, under an empty Change To (a Change Format query): each match where it was;
	    - one width for every match, every unchanged stretch at the place that makes: taken when the query has one length,
	      or when every match reads alike and so does everything written (the same text written the same way);
	    - else as far as the unchanged stretches tell. An unchanged stretch laid as far left and as far right as the text
	      allows: if the two are one place, that is where it stands; if not, the matches either side of it are one place.
	      (Every way of laying them lies between those two, each stretch's choice bounded only by its neighbours - so the
	      two layings answer what counting every way would.) */
	inline bool Locate(const std::u32string& before, const std::u32string& after, const std::vector<Match>& matches,
		Writes writes, std::vector<Placed>& out)
	{
		out.clear();
		const bool oneLength = (writes != kAnyLength);
		const int32_t k = static_cast<int32_t>(matches.size());
		const int32_t beforeLength = static_cast<int32_t>(before.size());
		const int32_t afterLength = static_cast<int32_t>(after.size());
		if (k == 0)
			return before == after;
		// The unchanged stretches: stretch i stands before match i, stretch k after the last.
		std::vector<int32_t> from(static_cast<size_t>(k) + 1), length(static_cast<size_t>(k) + 1);
		int32_t unchanged = 0, at = 0;
		for (int32_t i = 0; i < k; ++i)
		{
			const Match& m = matches[static_cast<size_t>(i)];
			if (m.start < at || m.end < m.start || m.end > beforeLength)
				return false;
			from[static_cast<size_t>(i)] = at;
			length[static_cast<size_t>(i)] = m.start - at;
			unchanged += m.start - at;
			at = m.end;
		}
		from[static_cast<size_t>(k)] = at;
		length[static_cast<size_t>(k)] = beforeLength - at;
		unchanged += beforeLength - at;
		if (unchanged > afterLength)
			return false;
		const auto stretchAt = [&](int32_t stretch, int32_t place)
		{
			const int32_t n = length[static_cast<size_t>(stretch)];
			return place >= 0 && place + n <= afterLength
				&& after.compare(static_cast<size_t>(place), static_cast<size_t>(n), before, static_cast<size_t>(from[static_cast<size_t>(stretch)]), static_cast<size_t>(n)) == 0;
		};
		// The head and the tail have one place each.
		if (!stretchAt(0, 0) || !stretchAt(k, afterLength - length[static_cast<size_t>(k)]))
			return false;

		// NOTHING WRITTEN: a Change Format query's text. No length to know: each match stands where it was.
		if (writes == kEmptyChange && before == after)
		{
			for (int32_t i = 0; i < k; ++i)
				out.push_back(Placed{ i, i, matches[static_cast<size_t>(i)].start, matches[static_cast<size_t>(i)].end });
			return true;
		}

		// ONE WIDTH FOR EVERY MATCH.
		if ((afterLength - unchanged) % k == 0)
		{
			const int32_t width = (afterLength - unchanged) / k;
			bool fits = true;
			int32_t place = length[0];
			std::vector<Placed> even;
			for (int32_t i = 0; i < k && fits; ++i)
			{
				even.push_back(Placed{ i, i, place, place + width });
				place += width;
				fits = stretchAt(i + 1, place);
				place += length[static_cast<size_t>(i) + 1];
			}
			if (fits)
			{
				bool alike = true;
				const Match& one = matches[0];
				for (int32_t i = 1; i < k && alike && !oneLength; ++i)
				{
					const Match& m = matches[static_cast<size_t>(i)];
					alike = (m.end - m.start) == (one.end - one.start)
						&& before.compare(static_cast<size_t>(m.start), static_cast<size_t>(m.end - m.start), before, static_cast<size_t>(one.start), static_cast<size_t>(one.end - one.start)) == 0
						&& after.compare(static_cast<size_t>(even[static_cast<size_t>(i)].start), static_cast<size_t>(width), after, static_cast<size_t>(even[0].start), static_cast<size_t>(width)) == 0;
				}
				if (oneLength || alike)
				{
					out.swap(even);
					return true;
				}
			}
		}

		// AS FAR AS THE UNCHANGED STRETCHES TELL: each laid as far left, then as far right, as the text allows.
		std::vector<int32_t> left(static_cast<size_t>(k) + 1), right(static_cast<size_t>(k) + 1);
		left[0] = 0;
		for (int32_t i = 1; i < k; ++i)
		{
			const size_t found = after.find(before.data() + from[static_cast<size_t>(i)],
				static_cast<size_t>(left[static_cast<size_t>(i) - 1] + length[static_cast<size_t>(i) - 1]), static_cast<size_t>(length[static_cast<size_t>(i)]));
			if (found == std::u32string::npos)
				return false;
			left[static_cast<size_t>(i)] = static_cast<int32_t>(found);
		}
		left[static_cast<size_t>(k)] = afterLength - length[static_cast<size_t>(k)];
		if (left[static_cast<size_t>(k)] < left[static_cast<size_t>(k) - 1] + length[static_cast<size_t>(k) - 1])
			return false;
		right[static_cast<size_t>(k)] = left[static_cast<size_t>(k)];
		for (int32_t i = k - 1; i >= 1; --i)
		{
			const int32_t last = right[static_cast<size_t>(i) + 1] - length[static_cast<size_t>(i)];
			if (last < 0)
				return false;
			const size_t found = after.rfind(before.data() + from[static_cast<size_t>(i)], static_cast<size_t>(last), static_cast<size_t>(length[static_cast<size_t>(i)]));
			if (found == std::u32string::npos)
				return false;
			right[static_cast<size_t>(i)] = static_cast<int32_t>(found);
		}
		right[0] = 0;
		int32_t first = 0;
		for (int32_t i = 1; i <= k; ++i)
		{
			if (left[static_cast<size_t>(i)] != right[static_cast<size_t>(i)])
				continue;		// an edge not told: match i - 1 and match i are one place
			out.push_back(Placed{ first, i - 1, left[static_cast<size_t>(first)] + length[static_cast<size_t>(first)], left[static_cast<size_t>(i)] });
			first = i;
		}
		return true;
	}
}

#endif // __KFCRunListAlign_h__

// End, KFCRunListAlign.h.
