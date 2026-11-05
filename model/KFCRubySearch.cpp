//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Ruby tab, listed - see KFCRubySearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"
#include "IClassIDData.h"				// which strand kPrivateCreateStrandCmdBoss makes
#include "ICommand.h"
#include "IComposeScanner.h"			// QueryAttributeAt - a run's reading
#include "IDataBase.h"
#include "IDocument.h"					// Save List...: the front document
#include "IRubyStrand.h"				// IRubyAttrStrand - the file is IRubyStrand.h, the class is not
#include "IStoryList.h"
#include "ITextAttrBoolean.h"			// kTARubyAttrBoss, kTAMojiRubyBoss
#include "ITextAttrWideString.h"		// kTARubyStringBoss - the reading itself
#include "ITextModel.h"
#include "ITextModelCmds.h"				// ApplyCmd, ClearOverridesCmd
#include "ITextStoryThread.h"
#include "IUIDData.h"
#include "IWalkerScopeFactoryUtils.h"	// kAllDocumentScope - the tab's Search:

// General includes:
#include "AttributeBossList.h"
#include "CJKID.h"						// kRubyAttrStrandBoss and the ruby attributes
#include "CmdUtils.h"
#include "ConditionalTextID.h"			// kHiddenTextBoss
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "TextID.h"						// kCharAttrStrandBoss, kPrivateCreateStrandCmdBoss
#include "Utils.h"

#include <algorithm>		// a list's first occurrences - the page order; a folded text's places
#include <numeric>
#include <set>
#include <string>

// Project includes:
#include "KFCRubySearch.h"
#include "IKFCUIServices.h"				// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCFold.h"					// what Kana Sensitive and Width Sensitive take for the same
#include "KFCOwnScope.h"				// what the icons take in
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line
#include "KFCRubyList.h"				// a ruby list's words
#include "KFCRubyListFile.h"			// ...read from its file
#include "KFCRubyMarkText.h"			// the marks a manuscript writes
#include "KFCTextCut.h"					// KFCSafeCut
#include "KFCRunGuard.h"				// IsAnyRunning - Save List...'s door
#include "KFCReplaceEngine.h"			// WriteUnderWay - Save List... counted as a run while it reaches the book's chapters

namespace
{
// A document's share of the bar (KFCQRSearch's number: a walk with nothing to say about its inside).
const int32 kRubyDocSpan = 100;
// What a row draws for a run, at most: the base and the reading, each cut (a run list's row holds about as much - KFCRunList.cpp).
const int32 kRowWordsBase = 20;
const int32 kRowWordsReading = 28;

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note (KFCTableSearch.cpp's StandsInSetAsideText's rule).
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// Can a person see anything of the reading? (A footnote's marker inherits a control character as its reading - KIDMCPRuby.cpp's HasVisible, measured there.)
bool HasVisible(const WideString& reading)
{
	for (WideString::const_iterator c = reading.begin(); c != reading.end(); ++c)
	{
		const uint32 v = static_cast<uint32>(*c);
		if (v >= 0x20 && v != 0x7F)
			return true;
	}
	return false;
}

// The reading at `at`, read as the composer reads it - empty when it carries none a person can see - and how far the same value goes, up to `until` (outLength: never less than 1).
WideString ReadingAt(IComposeScanner* scanner, TextIndex at, TextIndex until, int32& outLength)
{
	WideString none;
	outLength = 0;
	InterfacePtr<const IAttrReport> attr(scanner->QueryAttributeAt(at, until, kTARubyStringBoss, &outLength));
	if (outLength <= 0)
		outLength = until - at;
	InterfacePtr<ITextAttrWideString> reading(attr, UseDefaultIID());
	if (reading == nil || !HasVisible(reading->Get()))
		return none;
	return reading->Get();
}

// ONE RUBY ON A STRETCH OF A STORY.
struct RubyRun
{
	TextIndex	start;
	TextIndex	end;
	WideString	reading;
};

// THE RUBIES A PERSON CAN SEE ON [from, to), as they stand: where the strand has the ruby on, each stretch of one reading (characters given their readings one by one are a ruby each - the Change
// boxes' way, and KIDMCP's), cut at `to`.
void RunsIn(IRubyAttrStrand* strand, IComposeScanner* scanner, TextIndex from, TextIndex to, std::vector<RubyRun>& out)
{
	TextIndex at = from;
	while (at < to)
	{
		int32 len = 0;
		TextIndex runBegin = at;
		const bool16 on = strand->GetRubyRun(at, &len, &runBegin);
		if (len <= 0)
			return;
		const TextIndex runEnd = at + len;
		TextIndex part = at;
		while (on != kFalse && part < runEnd && part < to)
		{
			int32 partLength = 0;
			const WideString reading(ReadingAt(scanner, part, runEnd, partLength));
			if (!reading.empty())
			{
				RubyRun run;
				run.start = part;
				run.end = (part + partLength < to) ? part + partLength : to;
				run.reading = reading;
				out.push_back(run);
			}
			part += partLength;
		}
		at = runEnd;
	}
}

// The same of a story, by its UIDRef. No strand: no ruby.
void RunsOn(const UIDRef& story, TextIndex from, TextIndex to, std::vector<RubyRun>& out)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(story, UseDefaultIID());
	if (model == nil || scanner == nil || to <= from)
		return;
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	if (strand != nil)
		RunsIn(strand, scanner, from, to, out);
}

PMString Plain(const WideString& words)
{
	PMString out(words);
	out.SetTranslatable(kFalse);
	return out;
}

// What a row's words hold of a stretch: its breaks and anchors marked as a row's match has them, cut.
void AppendCut(PMString& ioWords, const PMString& stretch, int32 most)
{
	PMString shown(stretch);
	shown.SetTranslatable(kFalse);
	KFCResultModel::MarkUpBreaksForDisplay(shown);
	if (shown.CharCount() > most)
	{
		const int32 keep = KFCSafeCut(shown, most);
		shown.Remove(keep > 0 ? keep : most, kMaxInt32);
		shown.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	ioWords.Append(shown);
}

// ----- THE MARKS OF A MANUSCRIPT (the spec's R12): KFCRubyMarkText.h.
using KFCRubyMarkText::kMarkOpen;
using KFCRubyMarkText::kMarkClose;
using KFCRubyMarkText::MarkPlace;
using KFCRubyMarkText::MarkAtOpen;

std::u32string U32Of(const PMString& words)
{
	std::u32string out;
	const WideString wide(words);
	for (WideString::const_iterator c = wide.begin(); c != wide.end(); ++c)
		out.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	return out;
}

WideString WideOf(const std::u32string& words)
{
	WideString out;
	for (size_t c = 0; c < words.size(); ++c)
		out.Append(UTF32TextChar(static_cast<uint32>(words[c])));
	return out;
}

// Where a folded text goes on from, given a place in what was folded: the first of its characters from there.
size_t FoldedPlaceOf(const KFCFold::Folded& folded, size_t textPlace)
{
	return static_cast<size_t>(std::lower_bound(folded.from.begin(), folded.from.end(), textPlace) - folded.from.begin());
}

// THE WORDS OF RUBY IN ONE STORY (Save List..., Inconsistent Ruby): the rubies a search of it would take - the icons, the part of the document - those that touch one another one word (characters given their readings one by
// one are a word's, as Text With Ruby's Ruby: reads them). each(first, end) for each - its first ruby and one past its last, in `all` - answering false to stop.
template <typename Each>
void ForEachRubyWord(const UIDRef& storyRef, ITextModel* model, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, std::vector<RubyRun>& all, Each each)
{
	all.clear();
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (strand == nil || scanner == nil)
		return;
	RunsIn(strand, scanner, 0, model->TotalLength(), all);
	const auto takes = [&](const RubyRun& run)
	{
		return !StandsInSetAsideText(model, run.start) && (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), run.start, run.end)) && taken.Takes(storyRef, model, run.start);
	};
	size_t r = 0;
	while (r < all.size())
	{
		if (!takes(all[r]))
		{
			++r;
			continue;
		}
		size_t end = r + 1;
		while (end < all.size() && all[end].start == all[end - 1].end && takes(all[end]))
			++end;
		if (!each(r, end))
			return;
		r = end;
	}
}

// A WORD AS INCONSISTENT RUBY COMPARES IT (KFCRubyList::Readings): its base folded, and its readings run together, folded, their spaces out - a group reading and the same words read a character at a time read alike.
std::u32string WordKey(const UIDRef& storyRef, TextIndex start, TextIndex end, const KFCFold::Flags& flags)
{
	return KFCFold::TextOf(U32Of(KFCSearchEngine::ReadText(storyRef, start, end - start)), flags);
}

std::u32string WordReading(const std::vector<RubyRun>& runs, size_t first, size_t end, const KFCFold::Flags& flags)
{
	std::u32string out;
	for (size_t g = first; g < end; ++g)
		out += KFCFold::TextOf(KFCRubyChangePlan::Compact(U32Of(Plain(runs[g].reading))), flags);
	return out;
}

// THE MARKS OF ONE STORY: every mark Search: and the icons take in. The boxes ask nothing of them: the face hides those with this Look For (the author, 11-02), and what is hidden must not narrow a list unseen.
void WalkMarks(const UIDRef& storyRef, ITextModel* model, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, 0, model->TotalLength())));
	if (static_cast<int32>(text.size()) != model->TotalLength())
		return;		// (a reading of another length has not told each position's character)
	for (size_t open = text.find(kMarkOpen); open != std::u32string::npos; open = text.find(kMarkOpen, open + 1))
	{
		MarkPlace mark;
		if (!MarkAtOpen(text, open, mark) || StandsInSetAsideText(model, static_cast<TextIndex>(open)))
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), static_cast<TextIndex>(mark.first), static_cast<TextIndex>(mark.close + 1)))
			continue;
		if (!taken.Takes(storyRef, model, static_cast<TextIndex>(mark.first)))
			continue;
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return;
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = static_cast<TextIndex>(mark.first);
		place.end = static_cast<TextIndex>(mark.close + 1);
		ioPlaces.push_back(place);
	}
}

// THE TEXT WITH RUBY IN ONE STORY, by the conditions. With no Base Text a row is a ruby - one the searched part touches: the reading is over the whole. WITH A BASE TEXT a row is an occurrence of it and
// every ruby it touches (the spec's F22): whatever ruby the Change boxes wrote - a character's each, or the kanji of a word's alone - the same conditions find it again. Ruby: is held by a row's readings
// together, their spaces aside (a per-character ruby's string has them).
void WalkWithRuby(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (strand == nil || scanner == nil)
		return;		// no strand: no ruby anywhere in this story (SnpPerformTextAttrRuby's rule)
	const TextIndex total = model->TotalLength();
	std::vector<RubyRun> runs;
	{
		std::vector<RubyRun> all;
		RunsIn(strand, scanner, 0, total, all);
		for (size_t r = 0; r < all.size(); ++r)
			if (!StandsInSetAsideText(model, all[r].start))
				runs.push_back(all[r]);
	}
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	const std::u32string wantedBase(KFCFold::TextOf(U32Of(asked.base), flags));
	const std::u32string wantedReading(KFCFold::TextOf(KFCRubyChangePlan::Compact(U32Of(asked.reading)), flags));
	const auto addRow = [&](TextIndex start, TextIndex end) -> bool
	{
		if (!taken.Takes(storyRef, model, start))
			return true;		// (on past it: the walk goes on)
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return false;
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = start;
		place.end = end;
		ioPlaces.push_back(place);
		return true;
	};
	if (wantedBase.empty() && wantedReading.empty())
	{
		for (size_t r = 0; r < runs.size(); ++r)
		{
			if (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), runs[r].start, runs[r].end))
				continue;
			if (!addRow(runs[r].start, runs[r].end))
				return;
		}
		return;
	}
	// RUBY: WITH NO BASE TEXT: the readings of the rubies that stand touching, read together - characters given their readings one by one (the Change boxes' way) are found by the word's whole reading, as a search with a Base Text finds them
	// and as a group ruby's is (the spec's F22 - measured by reading the code, 11-02: Ruby: and every box's word run together found no such text). A row is the rubies the reading stands on.
	if (wantedBase.empty())
	{
		size_t r = 0;
		while (r < runs.size())
		{
			size_t groupEnd = r + 1;
			while (groupEnd < runs.size() && runs[groupEnd].start == runs[groupEnd - 1].end)
				++groupEnd;
			std::u32string chain;
			std::vector<size_t> runOfChar;
			for (size_t g = r; g < groupEnd; ++g)
			{
				const std::u32string one(KFCFold::TextOf(KFCRubyChangePlan::Compact(U32Of(Plain(runs[g].reading))), flags));
				chain += one;
				runOfChar.insert(runOfChar.end(), one.size(), g);
			}
			size_t at = chain.find(wantedReading);
			while (at != std::u32string::npos)
			{
				const size_t firstRun = runOfChar[at];
				const size_t lastRun = runOfChar[at + wantedReading.size() - 1];
				if (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), runs[firstRun].start, runs[lastRun].end))
				{
					if (!addRow(runs[firstRun].start, runs[lastRun].end))
						return;
				}
				// (On from the ruby after it: a ruby is in one row, however often its reading holds the words.)
				size_t next = at + wantedReading.size();
				while (next < chain.size() && runOfChar[next] == lastRun)
					++next;
				at = chain.find(wantedReading, next);
			}
			r = groupEnd;
		}
		return;
	}
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, 0, total)));
	if (static_cast<int32>(text.size()) != total)
		return;		// (a reading of another length has not told each position's character)
	const KFCFold::Folded folded(KFCFold::Fold(text, flags));
	size_t first = 0;
	size_t found = folded.text.find(wantedBase);
	while (found != std::u32string::npos)
	{
		TextIndex rowStart = static_cast<TextIndex>(folded.from[found]);
		TextIndex rowEnd = static_cast<TextIndex>(folded.from[found + wantedBase.size()]);
		while (first < runs.size() && runs[first].end <= rowStart)
			++first;
		std::u32string readings;
		size_t r = first;
		for (; r < runs.size() && runs[r].start < rowEnd; ++r)
		{
			if (runs[r].start < rowStart)
				rowStart = runs[r].start;
			if (runs[r].end > rowEnd)
				rowEnd = runs[r].end;
			readings += KFCFold::TextOf(KFCRubyChangePlan::Compact(U32Of(Plain(runs[r].reading))), flags);
		}
		if (r > first && readings.find(wantedReading) != std::u32string::npos
			&& (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), rowStart, rowEnd)))
		{
			if (!addRow(rowStart, rowEnd))
				return;
		}
		found = folded.text.find(wantedBase, FoldedPlaceOf(folded, static_cast<size_t>(rowEnd)));
	}
}

// THE OCCURRENCES OF THE BASE TEXT IN ONE STORY WITH NO RUBY ON THEM.
void WalkWithoutRuby(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	const KFCFold::Folded folded(KFCFold::Fold(U32Of(KFCSearchEngine::ReadText(storyRef, 0, model->TotalLength())), flags));
	const std::u32string wanted(KFCFold::TextOf(U32Of(asked.base), flags));
	if (wanted.empty())
		return;
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	for (size_t at = folded.text.find(wanted); at != std::u32string::npos; at = folded.text.find(wanted, at + wanted.size()))
	{
		const TextIndex start = static_cast<TextIndex>(folded.from[at]);
		const TextIndex end = static_cast<TextIndex>(folded.from[at + wanted.size()]);
		if (StandsInSetAsideText(model, start))
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), start, end))
			continue;
		if (!taken.Takes(storyRef, model, start))
			continue;
		if (strand != nil && scanner != nil)
		{
			std::vector<RubyRun> on;
			RunsIn(strand, scanner, start, end, on);
			if (!on.empty())
				continue;
		}
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return;
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = start;
		place.end = end;
		ioPlaces.push_back(place);
	}
}

// A LIST'S WORD WITH RUBY ON IT, LISTED (Include Text With Other Ruby - the author's proposal 2, 11-02): when its ruby stands inside the word and reads otherwise than the list's line - the readings run together, folded, their
// spaces out, so a group reading and the same words read a character at a time read alike. Ruby that runs on past the word is no row: writing the word's reading would break a reading over more.
bool ReadsOtherwise(const UIDRef& storyRef, IRubyAttrStrand* strand, IComposeScanner* scanner, TextIndex total, const KFCRubyConditions& asked, TextIndex start, TextIndex end, const std::vector<RubyRun>& on)
{
	std::vector<RubyRun> around;
	RunsIn(strand, scanner, (start > 0) ? start - 1 : 0, (end < total) ? end + 1 : total, around);
	for (size_t r = 0; r < around.size(); ++r)
		if ((around[r].start < start && around[r].end > start) || (around[r].start < end && around[r].end > end))
			return false;
	std::vector<KFCRubyChangePlan::Run> plan;
	if (asked.listWords == nullptr || !KFCRubySearch::ListPlanForRow(*asked.listWords, asked.switches, storyRef, start, end, plan))
		return false;
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	std::u32string planned;
	for (size_t r = 0; r < plan.size(); ++r)
		planned += KFCFold::TextOf(KFCRubyChangePlan::Compact(plan[r].reading), flags);
	return WordReading(on, 0, on.size(), flags) != planned;
}

// THE WORDS OF A RUBY LIST IN ONE STORY (KFCRubyList.h): the longest at each place, left to right, each a row - but one a ruby stands on already: that is counted (ioHadRuby) and left, whatever it reads, for a list
// gives readings to text that has none. (A row whose ruby differs is Text With Ruby's to find, and its Replace's to write.) An occurrence a selection cuts is not found, as a text search finds none.
// EACH WORD'S FIRST ALONE (asked.listFirst - the author, 11-02): ioFirsts gets the first of each word in each THREAD of the story - a table's cells and a footnote's text are threads of their own, and stand after the story's text in the model whatever page they are on (memory
// table-cells-live-after-body-text), so which of them comes first is the page order's to say (KeepFirsts). One with ruby on it goes in too: it is that word's first, and the word is done with.
struct ListFound
{
	KFCSearchEngine::ListedPlace	place;
	size_t							line;		// the word: its line in the file
	bool							hadRuby;
};

void WalkList(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped, int32& ioHadRuby, std::vector<ListFound>* ioFirsts)
{
	const TextIndex total = model->TotalLength();
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, 0, total)));
	if (static_cast<int32>(text.size()) != total)
		return;		// (a reading of another length has not told each position's character)
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	const KFCRubyList::Words& words = KFCRubyList::ForFlags(*asked.listWords, flags);
	const KFCFold::Folded folded(KFCFold::Fold(text, flags));
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	TextIndex threadEnd = -1;
	std::set<size_t> seenInThread;
	size_t at = 0;
	while (at < folded.text.size())
	{
		const KFCRubyList::Entry* const entry = KFCRubyList::LongestAt(words, folded.text, at);
		if (entry == nullptr)
		{
			++at;
			continue;
		}
		const TextIndex start = static_cast<TextIndex>(folded.from[at]);
		const TextIndex end = static_cast<TextIndex>(folded.from[at + entry->word.size()]);
		at += entry->word.size();
		if (StandsInSetAsideText(model, start))
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), start, end))
			continue;
		if (!taken.Takes(storyRef, model, start))
			continue;
		if (ioFirsts != nil)
		{
			if (start >= threadEnd)
			{
				TextIndex threadStart = start;
				int32 threadSpan = 0;
				InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(start, &threadStart, &threadSpan));
				threadEnd = (thread != nil && threadStart + threadSpan > start) ? threadStart + threadSpan : total;
				seenInThread.clear();
			}
			if (!seenInThread.insert(entry->line).second)
				continue;
		}
		// A RUBY ON IT ALREADY keeps it from being a row - but with Include Text With Other Ruby (the author's proposal 2) one that reads otherwise than the list's line is a row too (ReadsOtherwise).
		bool hadRuby = false;
		if (strand != nil && scanner != nil)
		{
			std::vector<RubyRun> on;
			RunsIn(strand, scanner, start, end, on);
			hadRuby = !on.empty() && !(asked.listOtherRuby && ReadsOtherwise(storyRef, strand, scanner, total, asked, start, end, on));
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = start;
		place.end = end;
		if (ioFirsts != nil)
		{
			ListFound found;
			found.place = place;
			found.line = entry->line;
			found.hadRuby = hadRuby;
			ioFirsts->push_back(found);
			continue;
		}
		if (hadRuby)
		{
			++ioHadRuby;
			continue;
		}
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return;
		}
		ioPlaces.push_back(place);
	}
}

// INCONSISTENT RUBY IN ONE STORY (the author's proposal 3, 11-02): every word of ruby read two ways or more over what is searched (`readings`, counted first), each place of it a row - Text With Ruby's rows, written by its
// Change side; Base Text: narrows them as it narrows Text With Ruby's. RUBY: IS NOT READ (2026-11-03 - the author: "the Ruby part of the search is not needed, is it - grey, no typing in it"): a reading kept a
// place whose reading held it, so it could not pick out the odd reading - what the rows are for is each reading beside the others. The face greys it; what it held before is not a hidden condition.
void WalkVaries(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, const KFCRubyList::Readings& readings, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part,
	size_t limit, std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	const std::u32string wantedBase(KFCFold::TextOf(U32Of(asked.base), flags));
	std::vector<RubyRun> all;
	ForEachRubyWord(storyRef, model, taken, part, all, [&](size_t r, size_t end) -> bool
	{
		const std::u32string key(WordKey(storyRef, all[r].start, all[end - 1].end, flags));
		if (!readings.Varies(key) || (!wantedBase.empty() && key.find(wantedBase) == std::u32string::npos))
			return true;
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return false;
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = all[r].start;
		place.end = all[end - 1].end;
		ioPlaces.push_back(place);
		return true;
	});
}

// HOW A VARYING WORD IS READ, after its row's words: "(tarou 5, taro 1)" in the page's own characters, the most read first.
void AppendReadingsOf(PMString& ioWords, const KFCRubyList::Readings& readings, const std::u32string& key)
{
	const std::map<std::u32string, std::map<std::u32string, int32> >::const_iterator word = readings.byWord.find(key);
	if (word == readings.byWord.end())
		return;
	std::vector<std::pair<int32, std::u32string> > ways;
	for (std::map<std::u32string, int32>::const_iterator w = word->second.begin(); w != word->second.end(); ++w)
		ways.push_back(std::make_pair(-w->second, w->first));
	std::sort(ways.begin(), ways.end());
	ioWords.Append("  (");
	for (size_t w = 0; w < ways.size(); ++w)
	{
		if (w > 0)
			ioWords.Append(", ");
		AppendCut(ioWords, Plain(WideOf(ways[w].second)), kRowWordsReading);
		ioWords.Append(" ");
		ioWords.AppendNumber(-ways[w].first);
	}
	ioWords.Append(")");
}

// AN INCONSISTENT RUBY ROW'S HEADING (2026-11-03 - the author: "a heading per reading, the places found with it under it"; KFCResultModel::Hit::groupKey): its word and reading as the search compares
// them - what its rows are put together by -, the heading's words as the page has them ("<base><<reading>>", each cut as a row's words are), and the word's other readings over what was searched,
// the most read first, as the page first had them, tab-separated - what its menu offers to give it.
void SetReadingGroup(const UIDRef& storyRef, TextIndex start, TextIndex end, const std::u32string& word, const KFCFold::Flags& flags, const KFCRubyList::Readings& readings, KFCResultModel::Hit& ioHit)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IRubyAttrStrand> strand((model != nil) ? static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)) : nil);
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (strand == nil || scanner == nil)
		return;
	std::vector<RubyRun> runs;
	RunsIn(strand, scanner, start, end, runs);
	const std::u32string reading(WordReading(runs, 0, runs.size(), flags));
	std::u32string key(word);
	key.push_back(0x1F);
	key += reading;
	ioHit.groupKey = PMString(WideOf(key));
	ioHit.groupKey.SetTranslatable(kFalse);
	PMString label;
	AppendCut(label, PMString(KFCSearchEngine::ReadText(storyRef, start, end - start)), kRowWordsBase);
	label.AppendW(UTF32TextChar(0x300A));		// the book title marks, as a row's words write a reading
	AppendCut(label, PMString(WideOf(WordReading(runs, 0, runs.size(), KFCFold::Flags()))), kRowWordsReading);
	label.AppendW(UTF32TextChar(0x300B));
	label.SetTranslatable(kFalse);
	ioHit.groupLabel = label;
	PMString others;
	const std::map<std::u32string, std::map<std::u32string, int32> >::const_iterator ways = readings.byWord.find(word);
	if (ways != readings.byWord.end())
	{
		std::vector<std::pair<int32, std::u32string> > rest;
		for (std::map<std::u32string, int32>::const_iterator w = ways->second.begin(); w != ways->second.end(); ++w)
			if (w->first != reading)
				rest.push_back(std::make_pair(-w->second, w->first));
		std::stable_sort(rest.begin(), rest.end(), [](const std::pair<int32, std::u32string>& a, const std::pair<int32, std::u32string>& b) { return a.first < b.first; });
		for (size_t r = 0; r < rest.size(); ++r)
		{
			if (r > 0)
				others.Append("\t");
			others.Append(PMString(WideOf(readings.Shown(word, rest[r].second))));
		}
	}
	others.SetTranslatable(kFalse);
	ioHit.otherReadings = others;
}

// THE FIRST OF EACH WORD, AS THE ROWS WOULD STAND IN THE LIST: every candidate's row built, in page order as FileChapter files them (KFCResultModel::OrderHitsByPage: pages in order, on a page the walk's order, overset last), and the first of each word kept - of each story's, the document's, or
// across a book's (ioBookSeen: the words of the chapters before this one). A first with ruby on it keeps its word from a row, and is counted. Rows past `limit` go, the last in page order: they are said as a cut list is.
void KeepFirsts(const UIDRef& docRef, int32 listFirst, std::set<size_t>* ioBookSeen, const std::vector<ListFound>& found, size_t limit, std::vector<KFCSearchEngine::ListedPlace>& outPlaces, std::vector<KFCResultModel::Hit>& outHits,
	std::map<UID, uint32>& outVersions, bool& outCapped, int32& ioHadRuby)
{
	std::vector<KFCSearchEngine::ListedPlace> places;
	places.reserve(found.size());
	for (size_t f = 0; f < found.size(); ++f)
		places.push_back(found[f].place);
	std::vector<KFCResultModel::Hit> hits;
	KFCSearchEngine::BuildListedHits(docRef, places, hits, outVersions);
	std::vector<size_t> order(hits.size());
	std::iota(order.begin(), order.end(), static_cast<size_t>(0));
	std::stable_sort(order.begin(), order.end(), [&hits](size_t a, size_t b)
		{
			const int32 pa = (hits[a].pageIndex < 0) ? kMaxInt32 : hits[a].pageIndex;
			const int32 pb = (hits[b].pageIndex < 0) ? kMaxInt32 : hits[b].pageIndex;
			return pa < pb;
		});
	std::set<std::pair<size_t, UID> > seen;
	std::vector<size_t> kept;
	for (size_t o = 0; o < order.size(); ++o)
	{
		const size_t h = order[o];
		const bool first = (ioBookSeen != nil) ? ioBookSeen->insert(found[h].line).second
			: seen.insert(std::make_pair(found[h].line, (listFirst == kKFCRubyListFirstInStory) ? found[h].place.story : UID(kInvalidUID))).second;
		if (!first)
			continue;
		if (found[h].hadRuby)
		{
			++ioHadRuby;
			continue;
		}
		if (kept.size() >= limit)
		{
			outCapped = true;
			break;
		}
		kept.push_back(h);
	}
	std::sort(kept.begin(), kept.end());
	for (size_t k = 0; k < kept.size(); ++k)
	{
		outPlaces.push_back(places[kept[k]]);
		outHits.push_back(hits[kept[k]]);
	}
}

// What the row's words say and what stands over it, given the rubies on it (those it has, or those a list will give it).
PMString WordsOver(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<RubyRun>& on);
KFCRowUpper UpperOver(TextIndex start, const std::vector<RubyRun>& on);

// The rubies a list's word will write, as a stretch of the story reads them.
std::vector<RubyRun> RunsOfPlan(TextIndex start, const std::vector<KFCRubyChangePlan::Run>& plan)
{
	std::vector<RubyRun> out;
	for (size_t r = 0; r < plan.size(); ++r)
	{
		RubyRun run;
		run.start = start + static_cast<TextIndex>(plan[r].start);
		run.end = run.start + static_cast<int32>(plan[r].length);
		run.reading = WideOf(plan[r].reading);
		out.push_back(run);
	}
	return out;
}

// ONE DOCUMENT'S ROWS - of its searched part alone, given one. False = no story list to walk. ioBookSeen: a list's First in Book over a book's chapters, each word's first in the chapters before this one (nil = each document's own: First in Book reads as First in Document where no book is searched).
bool WalkDoc(const UIDRef& docRef, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
	std::map<UID, uint32>& outVersions, bool& outCapped, int32& ioHadRuby, std::set<size_t>* ioBookSeen)
{
	outCapped = false;
	if (asked.kind == kKFCRubyFindList && asked.listWords == nullptr)
		return false;		// (a list read before any walk: KFCSearchEngine's door, a run's WriteDocument)
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return false;
	// Composing to read a row's page dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	std::vector<KFCSearchEngine::ListedPlace> places;
	KFCOwnScope taken(asked.switches);
	const bool firsts = (asked.kind == kKFCRubyFindList && asked.listFirst != kKFCRubyListAll);
	// INCONSISTENT RUBY: each word's readings over all of what is searched, counted by the caller over every document it searches - given none, over this document (or the part Search: holds).
	std::shared_ptr<const KFCRubyList::Readings> readings(asked.varies);
	if (asked.kind == kKFCRubyFindVaries && readings == nullptr)
	{
		std::shared_ptr<KFCRubyList::Readings> own(new KFCRubyList::Readings);
		KFCRubySearch::CountReadings(docRef, asked.switches, part, *own);
		readings = own;
	}
	std::vector<ListFound> found;
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count && !outCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)) || !taken.TakesStory(storyRef))
			continue;
		if (asked.kind == kKFCRubyFindWithout)
			WalkWithoutRuby(storyRef, model, asked, taken, part, limit, places, outCapped);
		else if (asked.kind == kKFCRubyFindMarks)
			WalkMarks(storyRef, model, taken, part, limit, places, outCapped);
		else if (asked.kind == kKFCRubyFindList)
			WalkList(storyRef, model, asked, taken, part, limit, places, outCapped, ioHadRuby, firsts ? &found : nil);
		else if (asked.kind == kKFCRubyFindVaries)
			WalkVaries(storyRef, model, asked, *readings, taken, part, limit, places, outCapped);
		else
			WalkWithRuby(storyRef, model, asked, taken, part, limit, places, outCapped);
	}
	if (firsts)
		KeepFirsts(docRef, asked.listFirst, (asked.listFirst == kKFCRubyListFirstInBook) ? ioBookSeen : nil, found, limit, places, outHits, outVersions, outCapped, ioHadRuby);
	else
		KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	// What the rows with ruby draw there; a row without any draws its text. An inconsistent word's row says how the word is read, after it.
	const bool withRuby = (asked.kind == kKFCRubyFindWith || asked.kind == kKFCRubyFindVaries);
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	for (size_t h = 0; withRuby && h < outHits.size() && h < places.size(); ++h)
	{
		const UIDRef storyRef(db, places[h].story);
		outHits[h].runChain = KFCRubySearch::RowWords(storyRef, places[h].start, places[h].end);
		if (asked.kind == kKFCRubyFindVaries)
		{
			const std::u32string word(WordKey(storyRef, places[h].start, places[h].end, flags));
			AppendReadingsOf(outHits[h].runChain, *readings, word);
			SetReadingGroup(storyRef, places[h].start, places[h].end, word, flags, *readings, outHits[h]);		// (its heading - 11-03)
		}
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].upper = KFCRubySearch::UpperOn(storyRef, places[h].start, places[h].end);
	}
	// A LIST'S ROWS DRAW WHAT THEIR REPLACE WILL GIVE THEM, faint: what the person reads the list for, before a row is written.
	for (size_t h = 0; asked.kind == kKFCRubyFindList && h < outHits.size() && h < places.size(); ++h)
	{
		const UIDRef storyRef(db, places[h].story);
		std::vector<KFCRubyChangePlan::Run> runs;
		if (!KFCRubySearch::ListPlanForRow(*asked.listWords, asked.switches, storyRef, places[h].start, places[h].end, runs))
			continue;
		const std::vector<RubyRun> planned(RunsOfPlan(places[h].start, runs));
		// A WORD WITH OTHER RUBY (Include Text With Other Ruby): its ruby now, and what the list gives it - "now -> the list's".
		std::vector<RubyRun> now;
		RunsOn(storyRef, places[h].start, places[h].end, now);
		outHits[h].runChain = now.empty() ? PMString() : WordsOver(storyRef, places[h].start, places[h].end, now);
		if (!now.empty())
		{
			outHits[h].runChain.Append(" ");
			outHits[h].runChain.AppendW(UTF32TextChar(0x2192));		// a rightwards arrow
			outHits[h].runChain.Append(" ");
		}
		outHits[h].runChain.Append(WordsOver(storyRef, places[h].start, places[h].end, planned));
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].upper = UpperOver(places[h].start, planned);
		outHits[h].upper.planned = true;
	}
	return true;
}

// THE CHARACTER AFTER A WORD, for the line that tells its readings apart (KFCRubyList::ListMaker): 0 where there is none a line could use - the page does not set it, it is InDesign's own or a space (a line's are trimmed), or it belongs to the word's last character.
char32_t CharAfterWord(const UIDRef& storyRef, ITextModel* model, TextIndex at)
{
	if (at >= model->TotalLength() || StandsInSetAsideText(model, at))
		return 0;
	const std::u32string one(U32Of(KFCSearchEngine::ReadText(storyRef, at, 1)));
	if (one.size() != 1 || KFCRubyMarkText::StopsMark(one[0]) || KFCRubyChangePlan::IsSpace(one[0]) || KFCIsAttachedMark(UTF32TextChar(static_cast<uint32>(one[0]))))
		return 0;
	return one[0];
}

// THE WORDS OF ONE STORY FOR SAVE LIST...: the rubies a search of it would take (the icons, the part of the document), those that touch one another one word - characters given their readings one by one are a word's, as Text With Ruby's Ruby: reads them.
void CollectWords(const UIDRef& storyRef, ITextModel* model, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, KFCRubyList::ListMaker& ioMaker)
{
	std::vector<RubyRun> all;
	ForEachRubyWord(storyRef, model, taken, part, all, [&](size_t r, size_t end) -> bool
	{
		KFCRubyList::Entry word;
		word.word = U32Of(KFCSearchEngine::ReadText(storyRef, all[r].start, all[end - 1].end - all[r].start));
		for (size_t g = r; g < end; ++g)
		{
			KFCRubyChangePlan::Run run = { static_cast<size_t>(all[g].start - all[r].start), static_cast<size_t>(all[g].end - all[g].start), U32Of(Plain(all[g].reading)) };
			word.runs.push_back(run);
		}
		ioMaker.Add(word, CharAfterWord(storyRef, model, all[end - 1].end));
		return true;
	});
}

// ...OF ONE DOCUMENT. A function of its own: everything it holds of the document is let go when it returns - a book's chapter is closed straight after, and one reference held across the close is released into a database that has gone (it brought InDesign down - rb-save-book, 11-02).
void CollectDocumentWords(const UIDRef& docRef, const KFCOwnSwitches& switches, const std::vector<KFCSearchEngine::ListedPlace>* part, KFCRubyList::ListMaker& ioMaker)
{
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return;
	// Composing to read a place's frame dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	KFCOwnScope taken(switches);
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)) || !taken.TakesStory(storyRef))
			continue;
		CollectWords(storyRef, model, taken, part, ioMaker);
	}
}

ErrorCode CreateStrandIfNeeded(ITextModel* model)
{
	InterfacePtr<IRubyAttrStrand> existing(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	if (existing != nil)
		return kSuccess;
	// (SnpPerformTextAttrRuby: without it "Resulting ruby composition will be unstable!")
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPrivateCreateStrandCmdBoss));
	InterfacePtr<IUIDData> modelData(cmd, UseDefaultIID());
	InterfacePtr<IClassIDData> strandData(cmd, UseDefaultIID());
	if (cmd == nil || modelData == nil || strandData == nil)
		return kFailure;
	modelData->Set(model);
	strandData->Set(kRubyAttrStrandBoss);
	return CmdUtils::ProcessCommand(cmd);
}
}	// anonymous namespace

bool KFCRubySearch::IsTabCurrent(KFCRubyConditions* outConditions)
{
	KFCRubyConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch ruby-tab (the model's cases, which need no dialog): on = the Ruby tab, its lines its conditions.
	if (KFC_DIAG_FAULT("ruby-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("ruby-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
		{
			const std::string& line = lines[n];
			PMString value;
			value.SetTranslatable(kFalse);
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				value.SetUTF8String(line.substr(eq + 1));
			if (line == "kind=without")
				conditions.kind = kKFCRubyFindWithout;
			else if (line == "kind=marks")
				conditions.kind = kKFCRubyFindMarks;
			else if (line == "kind=list")
				conditions.kind = kKFCRubyFindList;
			else if (line == "kind=varies")
				conditions.kind = kKFCRubyFindVaries;
			else if (line == "other=1")
				conditions.listOtherRuby = true;
			else if (line.compare(0, 5, "list=") == 0)
				conditions.listPath = value;
			else if (line.compare(0, 5, "base=") == 0)
				conditions.base = value;
			else if (line.compare(0, 5, "ruby=") == 0)
				conditions.reading = value;
			else if (line.compare(0, 7, "change=") == 0)
			{
				KFCRubyChangePiece piece;
				piece.reading = value;
				conditions.change.assign(1, piece);
			}
			else if (line.compare(0, 6, "piece=") == 0)
			{
				const size_t sep = line.find('=', 6);
				KFCRubyChangePiece piece;
				piece.base.SetTranslatable(kFalse);
				piece.reading.SetTranslatable(kFalse);
				if (sep != std::string::npos)
				{
					piece.base.SetUTF8String(line.substr(6, sep - 6));
					piece.reading.SetUTF8String(line.substr(sep + 1));
				}
				conditions.change.push_back(piece);
			}
			else if (line == "remove=1")
				conditions.change.clear();
			else if (line == "findonly=1")
				conditions.findOnly = true;		// (2026-11-03 - Find Only)
			else if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
			else if (line == "scope=story")
				conditions.scope = IWalkerScopeFactoryUtils::kStoryScope;
			else if (line == "scope=toend")
				conditions.scope = IWalkerScopeFactoryUtils::kToEndOfStoryScope;
			else if (line == "scope=selection")
				conditions.scope = IWalkerScopeFactoryUtils::kSelectionScope;
			else if (line == "first=story")
				conditions.listFirst = kKFCRubyListFirstInStory;
			else if (line == "first=document")
				conditions.listFirst = kKFCRubyListFirstInDocument;
			else if (line == "first=book")
				conditions.listFirst = kKFCRubyListFirstInBook;
			else
				KFCOwnScopeDiagLine(line, conditions.switches);
		}
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCQRSearch::IsTabCurrent's rule): no UI half is no Ruby tab.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->RubyTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCRubySearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCRubyConditions& asked,
	const std::vector<KFCSearchEngine::ListedPlace>* part)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kRubyDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;		// the object lists' number (the spec's R5)
	const size_t limit = static_cast<size_t>(room > 0 ? room : 0);
	std::set<size_t> bookSeen;		// First in Book: the words of the chapters walked (a book's alone - fromBook; elsewhere each document is its own)
	// INCONSISTENT RUBY OVER SEVERAL DOCUMENTS (the spec's P3-4): every document's readings counted first - a word read one way in one chapter and another in the next is read two ways. A chapter opened for it goes
	// back straight after, as a walk's does, and is opened again for its rows.
	KFCRubyConditions withReadings(asked);
	if (asked.kind == kKFCRubyFindVaries && asked.varies == nullptr && targets.size() > 1)
	{
		std::shared_ptr<KFCRubyList::Readings> counted(new KFCRubyList::Readings);
		for (size_t i = 0; i < targets.size(); ++i)
		{
			KFCSetCountedTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
			if (bar.WasCancelled(kFalse))
			{
				out.cancelled = true;
				return;
			}
			const bool openedHere = fromBook && targets[i].docRef == UIDRef::gNull;
			if (openedHere && !KFCBookScope::OpenChapterDoc(targets[i], nil))
				continue;
			CountReadings(targets[i].docRef, asked.switches, part, *counted);
			if (openedHere)
			{
				(void)KFCBookScope::HandBackHeldDocNow(targets[i].docRef);
				targets[i].docRef = UIDRef::gNull;		// (opened again for its rows: a closed document's address is no address - memory uidref-reuse-after-close)
			}
		}
		withReadings.varies = counted;
	}
	for (size_t i = 0; i < targets.size(); ++i)
	{
		KFCSetCountedTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
		bar.SetPosition(shares[i]);
		if (bar.WasCancelled(kFalse))
		{
			out.cancelled = true;
			break;
		}
		if (static_cast<size_t>(out.total) >= limit)
		{
			out.truncated = true;
			break;
		}
		if (fromBook && targets[i].docRef == UIDRef::gNull && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			continue;
		const UIDRef docRef = targets[i].docRef;
		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> versions;
		bool capped = false;
		const bool walked = WalkDoc(docRef, withReadings, part, limit - static_cast<size_t>(out.total), hits, versions, capped, out.hadRuby, fromBook ? &bookSeen : nil);
		// A book's chapter goes back now, closed on the spot (one chapter of ours at a time - the other walks' rule).
		if (fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
			out.unclosed.push_back(targets[i].shortName);
		if (!walked)
		{
			PMString name(targets[i].shortName);
			name.SetTranslatable(kFalse);
			name.Append(": no story list");
			out.unsearchable.push_back(name);
			continue;
		}
		if (capped)
			out.truncated = true;
		if (!hits.empty())
			KFCSearchEngine::FileChapter(targets[i], hits, out, &versions);		// every walk's filing: page order, the tally
		if (capped)
			break;
	}
	if (!out.cancelled && bar.WasCancelled(kFalse))
		out.cancelled = true;
	bar.SetPosition(barEnd);
}

bool KFCRubySearch::SaveListText(PMString& outText, PMString& outName, PMString& outSaid)
{
	outText.Clear();
	outText.SetTranslatable(kFalse);
	outName.Clear();
	outName.SetTranslatable(kFalse);
	outSaid.Clear();
	outSaid.SetTranslatable(kFalse);
	// NOT UNDER A RUN OF OURS, AND COUNTED AS ONE FROM HERE TO THE HAND-BACK (KFCRunGuard.h - block 11's D11-1 rule): over a book it opens each closed chapter
	// windowless under its bar, and both run the message loop - what that dispatches must not start inside it, nor this inside another run. Until 2026-11-06
	// neither was asked (the since-2.1.0 re-inspection's D22-b - a test build's REOPEN lines read running=0). WriteUnderWay is the count of an operation that
	// reaches the chapters (IsReplacing - KFCRunGuard's one reader), a read as this one too.
	if (KFCRunGuard::IsAnyRunning())
	{
		outSaid.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const KFCReplaceEngine::WriteUnderWay counted;
	KFCRubyConditions asked;
	(void)IsTabCurrent(&asked);
	KFCSearchEngine::RunScope scope;
	if (!KFCSearchEngine::ResolveRunScope(scope, outSaid))
		return false;
	// THE SAME TARGETS AS THE TAB'S FIND (KFCSearchEngine's run): the book's chapters, opened when their turn comes and handed straight back; every open document; or the front one, whole or the part Search: holds.
	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	std::vector<KFCSearchEngine::ListedPlace> stretches;
	const IWalkerScopeFactoryUtils::WalkScopeType part = static_cast<IWalkerScopeFactoryUtils::WalkScopeType>(scope.selectionScope);
	const bool overPart = (part != IWalkerScopeFactoryUtils::kDocumentScope);
	KFCForwardSearchScope forward(overPart);		// (To End of Story, backwards, is the text before the caret: the run's rule)
	if (scope.fromBook)
	{
		if (!KFCBookScope::ListBookChapters(targets, bookName) || targets.empty())
		{
			outSaid.Append(KFCSearchEngine::kBookHasNoChapters);
			return false;
		}
		outName = bookName;
	}
	else if (scope.allDocuments)
	{
		KFCBookScope::ListOpenDocuments(targets);
		outName.Append("All Documents");
	}
	else
	{
		IDocument* const doc = KFCBookScope::ActiveDocument();
		if (doc != nil)
		{
			targets.push_back(KFCBookScope::DocAsChapter(doc));
			outName = targets[0].shortName;
		}
		if (overPart && (doc == nil || !KFCSearchEngine::PartStretches(static_cast<int32>(part), ::GetDataBase(doc), stretches)))
		{
			outSaid.Append("What Search: holds in the selection could not be read - nothing was saved.");
			return false;
		}
	}
	if (targets.empty())
	{
		outSaid.Append("There is no document to read the ruby of.");
		return false;
	}
	outName.SetTranslatable(kFalse);
	PMString title("Reading the ruby...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kRubyDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCRubyList::ListMaker maker;
	std::vector<PMString> unclosed;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		KFCSetCountedTask(bar, scope.fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
		bar.SetPosition(shares[i]);
		if (bar.WasCancelled(kFalse))
		{
			outSaid.Append("Cancelled - nothing was saved.");
			return false;
		}
		if (scope.fromBook && targets[i].docRef == UIDRef::gNull && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			continue;
		const UIDRef docRef = targets[i].docRef;
		CollectDocumentWords(docRef, asked.switches, overPart ? &stretches : nil, maker);
		// A book's chapter goes back now, closed on the spot (one chapter of ours at a time - the other walks' rule).
		if (scope.fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
			unclosed.push_back(targets[i].shortName);
	}
	bar.SetPosition(barEnd);
	if (bar.WasCancelled(kFalse))
	{
		outSaid.Append("Cancelled - nothing was saved.");
		return false;
	}
	PMString head("the ruby of ");
	head.SetTranslatable(kFalse);
	head.Append(outName);
	const std::u32string text(maker.Text(U32Of(head)));
	KFC_DIAG_LOG("RUBYTAB save words=%d apart=%d split=%d unwritable=%d", static_cast<int>(maker.Written()), static_cast<int>(maker.Apart()), static_cast<int>(maker.Split()), static_cast<int>(maker.Unwritable()));
	if (maker.Written() == 0)
	{
		outSaid.Append(maker.Empty() ? "There is no ruby in what Search: holds - nothing was saved." : "No ruby there can be written as a line of a list - nothing was saved.");
		KFCBookScope::AppendUnopenableNote(outSaid, unopenable);
		return false;
	}
	outText = Plain(WideOf(text));
	outSaid.AppendNumber(static_cast<int32>(maker.Written()));
	outSaid.Append((maker.Written() == 1) ? " word." : " words.");
	if (maker.Apart() > 0)
	{
		outSaid.Append(" ");
		outSaid.AppendNumber(static_cast<int32>(maker.Apart()));
		outSaid.Append((maker.Apart() == 1) ? " word read two ways or more has a line for each, with the character after it." : " words read two ways or more have a line for each, with the character after them.");
	}
	if (maker.Split() > 0)
	{
		outSaid.Append(" ");
		outSaid.AppendNumber(static_cast<int32>(maker.Split()));
		outSaid.Append((maker.Split() == 1) ? " is read two ways or more: its line is the way read most, and the others stand under it as # lines." : " are read two ways or more: each line is the way read most, and the others stand under it as # lines.");
	}
	if (maker.Unwritable() > 0)
	{
		outSaid.Append(" ");
		outSaid.AppendNumber(static_cast<int32>(maker.Unwritable()));
		outSaid.Append(" could not be written as a line: see the # lines.");
	}
	KFCBookScope::AppendUnopenableNote(outSaid, unopenable);
	KFCBookScope::AppendUnclosedNote(outSaid, unclosed);
	outSaid.SetTranslatable(kFalse);
	return true;
}

bool KFCRubySearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCRubyConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kRubyDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	int32 hadRuby = 0;
	const bool walked = WalkDoc(docRef, asked, nil, limit, outHits, outVersions, outCapped, hadRuby, nil);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kRubyDocSpan);
	return walked && !outCancelled;
}

bool KFCRubySearch::WriteDocument(const UIDRef& docRef, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount, std::set<size_t>* ioBookSeen)
{
	outCount = 0;
	if (asked.FindsOnly() || asked.kind == kKFCRubyFindVaries)
		return true;		// (Find Only writes nothing - 11-03, Text With Ruby's and Text Without Ruby's since 11-04; nor Inconsistent Ruby, which has no Change side since 11-03 - HasChangeSide keeps a run from asking either)
	std::vector<KFCResultModel::Hit> hits;
	std::map<UID, uint32> versions;
	bool capped = false;
	int32 hadRuby = 0;
	KFCRubyConditions conditions(asked);
	if (asked.kind == kKFCRubyFindList && conditions.listWords == nullptr)
	{
		// (A run hands it read, and turned the query away if it could not be - KFCQuerySequence. Anyone else's reads it here; one that cannot be read writes nothing.)
		int32 words = 0;
		PMString why;
		if (!KFCRubyListFile::Load(asked.listPath, conditions.listWords, words, why))
			return true;
	}
	if (!WalkDoc(docRef, conditions, part, static_cast<size_t>(kMaxInt32), hits, versions, capped, hadRuby, ioBookSeen))
		return false;
	IDataBase* const db = docRef.GetDataBase();
	const std::vector<KFCRubyChangePlan::Piece> pieces(PlanPieces(asked));
	if (asked.kind != kKFCRubyFindMarks && asked.kind != kKFCRubyFindList && KFCRubyChangePlan::Check(pieces) == KFCRubyChangePlan::kReadingOverNoBase)
		return true;		// no row could take it (a row's Replace says so; a run writes on)
	// FROM THE LAST TO THE FIRST: a mark's write takes characters out, and the places before it stand (each story's places are in its order).
	for (size_t h = hits.size(); h > 0; --h)
	{
		const KFCResultModel::Hit& hit = hits[h - 1];
		if (hit.isLocked)
			continue;
		const UIDRef story(db, hit.storyUID);
		if (asked.kind == kKFCRubyFindMarks)
		{
			int32 baseLength = 0;
			bool stillThere = false;
			if (WriteMark(story, hit.textStart, hit.textEnd - hit.textStart, baseLength, stillThere) != kSuccess)
				return false;
		}
		else if (asked.kind == kKFCRubyFindList)
		{
			std::vector<KFCRubyChangePlan::Run> runs;
			if (!ListPlanForRow(*conditions.listWords, conditions.switches, story, hit.textStart, hit.textEnd, runs) || ReadsAsPlanned(story, hit.textStart, hit.textEnd, runs))
				continue;
			if (WritePlan(story, hit.textStart, hit.textEnd, runs) != kSuccess)
				return false;
		}
		else
		{
			std::vector<KFCRubyChangePlan::Run> runs;
			const KFCRubyChangePlan::Result plan = PlanForRow(story, hit.textStart, hit.textEnd, pieces, runs, asked.switches);
			if (plan == KFCRubyChangePlan::kRemove)
			{
				if (!HasRubyOn(story, hit.textStart, hit.textEnd))
					continue;
				if (Remove(story, hit.textStart, hit.textEnd) != kSuccess)
					return false;
			}
			else if (plan == KFCRubyChangePlan::kWrite)
			{
				if (ReadsAsPlanned(story, hit.textStart, hit.textEnd, runs))
					continue;
				if (WritePlan(story, hit.textStart, hit.textEnd, runs) != kSuccess)
					return false;
			}
			else
				continue;		// (the boxes hold another text: a row's Replace says so; a run writes on)
		}
		++outCount;
	}
	return true;
}

std::vector<KFCRubyChangePlan::Piece> KFCRubySearch::PlanPieces(const KFCRubyConditions& asked)
{
	std::vector<KFCRubyChangePlan::Piece> pieces;
	for (size_t p = 0; p < asked.change.size(); ++p)
	{
		KFCRubyChangePlan::Piece piece;
		piece.base = U32Of(asked.change[p].base);
		piece.reading = U32Of(asked.change[p].reading);
		pieces.push_back(piece);
	}
	return pieces;
}

KFCRubyChangePlan::Result KFCRubySearch::PlanForRow(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Piece>& pieces, std::vector<KFCRubyChangePlan::Run>& outRuns, const KFCOwnSwitches& switches)
{
	return KFCRubyChangePlan::Make(pieces, U32Of(KFCSearchEngine::ReadText(story, start, end - start)), outRuns, KFCFoldFlagsOf(switches));
}

bool KFCRubySearch::ListPlanForRow(const KFCRubyList::Words& words, const KFCOwnSwitches& switches, const UIDRef& story, TextIndex start, TextIndex end, std::vector<KFCRubyChangePlan::Run>& outRuns)
{
	return KFCRubyList::RowPlan(words, KFCFoldFlagsOf(switches), U32Of(KFCSearchEngine::ReadText(story, start, end - start)), outRuns);
}

bool KFCRubySearch::ReadsAsPlanned(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Run>& runs)
{
	std::vector<RubyRun> on;
	RunsOn(story, start, end, on);
	if (on.size() != runs.size())
		return false;
	for (size_t r = 0; r < on.size(); ++r)
	{
		if (on[r].start != start + static_cast<TextIndex>(runs[r].start) || on[r].end != on[r].start + static_cast<int32>(runs[r].length)
			|| U32Of(Plain(on[r].reading)) != runs[r].reading)
			return false;
	}
	return true;
}

ErrorCode KFCRubySearch::WritePlan(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Run>& runs)
{
	TextIndex at = start;
	for (size_t r = 0; r <= runs.size(); ++r)
	{
		const TextIndex runStart = (r < runs.size()) ? start + static_cast<TextIndex>(runs[r].start) : end;
		if (at < runStart && HasRubyOn(story, at, runStart))
		{
			const ErrorCode off = Remove(story, at, runStart);
			if (off != kSuccess)
				return off;
		}
		if (r == runs.size())
			break;
		at = runStart + static_cast<int32>(runs[r].length);
		const ErrorCode wrote = Apply(story, runStart, at, WideOf(runs[r].reading));
		if (wrote != kSuccess)
			return wrote;
	}
	return kSuccess;
}

KFCRowUpper KFCRubySearch::UpperOn(const UIDRef& story, TextIndex start, TextIndex end)
{
	std::vector<RubyRun> on;
	RunsOn(story, start, end, on);
	return UpperOver(start, on);
}

namespace
{
KFCRowUpper UpperOver(TextIndex start, const std::vector<RubyRun>& on)
{
	KFCRowUpper upper;
	for (size_t r = 0; r < on.size(); ++r)
	{
		const int32 length = on[r].end - on[r].start;
		const std::vector<std::u32string> words(KFCRubyChangePlan::Tokens(U32Of(Plain(on[r].reading))));
		if (words.size() > 1 && static_cast<int32>(words.size()) == length)
		{
			// A PER-CHARACTER RUBY KEPT AS ONE STRING WITH SPACES (InDesign's own Ruby dialog's way): each word over its character.
			for (size_t w = 0; w < words.size(); ++w)
			{
				KFCRowUpperRun one;
				one.offset = on[r].start - start + static_cast<int32>(w);
				one.length = 1;
				one.reading = Plain(WideOf(words[w]));
				upper.runs.push_back(one);
			}
			continue;
		}
		KFCRowUpperRun one;
		one.offset = on[r].start - start;
		one.length = length;
		one.reading.SetTranslatable(kFalse);
		AppendCut(one.reading, Plain(on[r].reading), kRowWordsReading);
		upper.runs.push_back(one);
	}
	return upper;
}
}	// anonymous namespace

void KFCRubySearch::CountReadings(const UIDRef& docRef, const KFCOwnSwitches& switches, const std::vector<KFCSearchEngine::ListedPlace>* part, KFCRubyList::Readings& ioReadings)
{
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return;
	// Composing to read a place's frame dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	KFCOwnScope taken(switches);
	const KFCFold::Flags flags(KFCFoldFlagsOf(switches));
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)) || !taken.TakesStory(storyRef))
			continue;
		std::vector<RubyRun> all;
		ForEachRubyWord(storyRef, model, taken, part, all, [&](size_t r, size_t end) -> bool
		{
			const std::u32string word(WordKey(storyRef, all[r].start, all[end - 1].end, flags));
			const std::u32string reading(WordReading(all, r, end, flags));
			++ioReadings.byWord[word][reading];
			std::u32string& shown = ioReadings.shown[word][reading];
			if (shown.empty())
				shown = WordReading(all, r, end, KFCFold::Flags());		// (as the page has it: no folding - Flags' default)
			return true;
		});
	}
}

bool KFCRubySearch::WalkForFace(const UIDRef& docRef, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	std::map<UID, uint32> versions;
	bool capped = false;
	int32 hadRuby = 0;
	return WalkDoc(docRef, asked, part, static_cast<size_t>(kMaxInt32), outHits, versions, capped, hadRuby, nil);
}

bool KFCRubySearch::HasRubyOn(const UIDRef& story, TextIndex start, TextIndex end)
{
	std::vector<RubyRun> on;
	RunsOn(story, start, end, on);
	return !on.empty();
}

PMString KFCRubySearch::RowWords(const UIDRef& story, TextIndex start, TextIndex end)
{
	std::vector<RubyRun> on;
	RunsOn(story, start, end, on);
	return WordsOver(story, start, end, on);
}

namespace
{
PMString WordsOver(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<RubyRun>& on)
{
	PMString words;
	words.SetTranslatable(kFalse);
	if (on.empty())
		return words;		// (no reading: the row draws its own text)
	TextIndex at = start;
	for (size_t r = 0; r < on.size(); ++r)
	{
		if (on[r].start > at)
			AppendCut(words, KFCSearchEngine::ReadText(story, at, on[r].start - at), kRowWordsBase);
		AppendCut(words, KFCSearchEngine::ReadText(story, on[r].start, on[r].end - on[r].start), kRowWordsBase);
		words.AppendW(UTF32TextChar(0x300A));		// the book title marks: the page's own way of writing a reading beside its base
		AppendCut(words, Plain(on[r].reading), kRowWordsReading);
		words.AppendW(UTF32TextChar(0x300B));
		at = on[r].end;
	}
	if (at < end)
		AppendCut(words, KFCSearchEngine::ReadText(story, at, end - at), kRowWordsBase);
	return words;
}
}	// anonymous namespace

ErrorCode KFCRubySearch::Apply(const UIDRef& story, TextIndex start, TextIndex end, const WideString& reading)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (model == nil || cmds == nil)
		return kFailure;
	const ErrorCode made = CreateStrandIfNeeded(model);
	if (made != kSuccess)
		return made;
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	if (attrs.get() == nil)
		return kFailure;
	InterfacePtr<ITextAttrBoolean> on(::CreateObject2<ITextAttrBoolean>(kTARubyAttrBoss));
	InterfacePtr<ITextAttrWideString> said(::CreateObject2<ITextAttrWideString>(kTARubyStringBoss));
	if (on == nil || said == nil)
		return kFailure;
	on->Set(kTrue);
	said->Set(reading);
	InterfacePtr<IAttrReport> onReport(on, UseDefaultIID());
	InterfacePtr<IAttrReport> saidReport(said, UseDefaultIID());
	attrs->ApplyAttribute(onReport);
	attrs->ApplyAttribute(saidReport);
	// THE RUBY'S TYPE, ALWAYS WRITTEN. ONE READING OVER SEVERAL CHARACTERS: A GROUP (kTAMojiRubyBoss kFalse) - under InDesign's default mono, or a mono the characters had, all of it would
	// stand on the first character. OVER ONE CHARACTER: MONO (kTrue - 2026-11-04, the author: "one character, mono ruby"), whatever type the character had (a group's, from an earlier ruby
	// over it, stood till then): the marks' rule, "one character, mono" (KFCHowTo's Ruby page).
	InterfacePtr<ITextAttrBoolean> mono(::CreateObject2<ITextAttrBoolean>(kTAMojiRubyBoss));
	if (mono == nil)
		return kFailure;
	mono->Set((end - start > 1) ? kFalse : kTrue);
	InterfacePtr<IAttrReport> monoReport(mono, UseDefaultIID());
	attrs->ApplyAttribute(monoReport);
	InterfacePtr<ICommand> apply(cmds->ApplyCmd(RangeData(start, end), attrs, kCharAttrStrandBoss));
	return (apply != nil) ? CmdUtils::ProcessCommand(apply) : kFailure;
}

bool KFCRubySearch::ReadMark(const UIDRef& story, TextIndex start, TextIndex end, int32& outBaseOffset, int32& outBaseLength, WideString& outReading)
{
	outBaseOffset = 0;
	outBaseLength = 0;
	outReading.clear();
	if (end <= start)
		return false;
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(story, start, end - start)));
	const size_t open = text.rfind(kMarkOpen);
	MarkPlace mark;
	if (open == std::u32string::npos || !MarkAtOpen(text, open, mark) || mark.first != 0 || mark.close + 1 != text.size())
		return false;
	outBaseOffset = static_cast<int32>(mark.baseStart);
	outBaseLength = static_cast<int32>(mark.open - mark.baseStart);
	outReading = WideOf(std::u32string(text, mark.open + 1, mark.close - mark.open - 1));
	return true;
}

ErrorCode KFCRubySearch::WriteMark(const UIDRef& story, TextIndex at, int32 length, int32& outBaseLength, bool& outStillThere)
{
	outBaseLength = 0;
	outStillThere = false;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	int32 baseOffset = 0, baseLength = 0;
	WideString reading;
	if (model == nil || cmds == nil || !ReadMark(story, at, at + length, baseOffset, baseLength, reading))
		return kFailure;
	// THE READING AND ITS MARKS FIRST, THEN THE BAR: the later text first, so the earlier place stands.
	const TextIndex open = at + baseOffset + baseLength;
	const int32 readingLength = at + length - open;
	const int32 before = model->TotalLength();
	InterfacePtr<ICommand> outReadingCmd(cmds->DeleteCmd(open, readingLength));
	if (outReadingCmd == nil || CmdUtils::ProcessCommand(outReadingCmd) != kSuccess)
		return kFailure;
	if (model->TotalLength() != before - readingLength)
	{
		outStillThere = true;		// Track Changes keeps deleted text in the story
		return kFailure;
	}
	if (baseOffset > 0)
	{
		InterfacePtr<ICommand> outBarCmd(cmds->DeleteCmd(at, baseOffset));
		if (outBarCmd == nil || CmdUtils::ProcessCommand(outBarCmd) != kSuccess)
			return kFailure;
	}
	const ErrorCode applied = Apply(story, at, at + baseLength, reading);
	if (applied == kSuccess)
		outBaseLength = baseLength;
	return applied;
}

ErrorCode KFCRubySearch::Remove(const UIDRef& story, TextIndex start, TextIndex end)
{
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	// ALL THIRTY, NOT THE FEW A REPLACE WRITES: a person's font, size or colour for the ruby left on the text is "a set of instructions for ruby that is not there"
	// (KIDMCPRuby.cpp's ClearRubyOn). Only the class IDs matter in this list (ITextModelCmds.h, ClearOverridesCmd).
	static const ClassID kRubyAttrs[] =
	{
		kTARubyAttrBoss,			kTARubyStringBoss,			kTAMojiRubyBoss,
		kTARubyOTProBoss,			kTARubyPointSizeBoss,		kTARubyRelativeSizeBoss,
		kTARubyAlignmentBoss,		kTARubyFontUIDBoss,			kTARubyFontStyleBoss,
		kTARubyAdjustParentBoss,	kTARubyXScaleBoss,			kTARubyYScaleBoss,
		kTARubyXOffsetBoss,			kTARubyYOffsetBoss,			kTARubyPositionBoss,
		kTARubyEdgeSpaceBoss,		kTARubyOverhangBoss,		kTARubyOverhangFlagBoss,
		kTARubyAutoScalingBoss,		kTARubyAutoScaleMinBoss,	kTARubyColorBoss,
		kTARubyTintBoss,			kTARubyOverprintBoss,		kTARubyStrokeColorBoss,
		kTARubyStrokeTintBoss,		kTARubyStrokeOverprintBoss,	kTARubyOutlineBoss,
		kTARubyAutoTCYNumDigitsBoss,kTARubyAutoTCYIncludeRomanBoss,
		kTARubyAutoTCYAutoScaleBoss
	};
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	if (attrs.get() == nil)
		return kFailure;
	for (size_t i = 0; i < sizeof(kRubyAttrs) / sizeof(kRubyAttrs[0]); ++i)
	{
		InterfacePtr<IAttrReport> report(::CreateObject2<IAttrReport>(kRubyAttrs[i]));
		if (report == nil)
			return kFailure;		// all of them or none: a partial clear would be said as done (KIDMCPRuby.cpp's note)
		attrs->ApplyAttribute(report);
	}
	InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(start, end - start, attrs, kCharAttrStrandBoss));
	return (clear != nil) ? CmdUtils::ProcessCommand(clear) : kFailure;
}

// End, KFCRubySearch.cpp.
