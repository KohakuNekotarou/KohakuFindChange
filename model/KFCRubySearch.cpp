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

#include <string>

// Project includes:
#include "KFCRubySearch.h"
#include "IKFCUIServices.h"				// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line
#include "KFCRubyList.h"				// a ruby list's words
#include "KFCRubyListFile.h"			// ...read from its file
#include "KFCRubyMarkText.h"			// the marks a manuscript writes
#include "KFCTextCut.h"					// KFCSafeCut

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

// THE MARKS OF ONE STORY, by the conditions.
void WalkMarks(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, 0, model->TotalLength())));
	if (static_cast<int32>(text.size()) != model->TotalLength())
		return;		// (a reading of another length has not told each position's character)
	const std::u32string wantedBase(U32Of(asked.base)), wantedReading(U32Of(asked.reading));
	for (size_t open = text.find(kMarkOpen); open != std::u32string::npos; open = text.find(kMarkOpen, open + 1))
	{
		MarkPlace mark;
		if (!MarkAtOpen(text, open, mark) || StandsInSetAsideText(model, static_cast<TextIndex>(open)))
			continue;
		const std::u32string base(text, mark.baseStart, mark.open - mark.baseStart);
		const std::u32string reading(text, mark.open + 1, mark.close - mark.open - 1);
		if (base.find(wantedBase) == std::u32string::npos || reading.find(wantedReading) == std::u32string::npos)
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), static_cast<TextIndex>(mark.first), static_cast<TextIndex>(mark.close + 1)))
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
void WalkWithRuby(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
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
	const std::u32string wantedBase(U32Of(asked.base));
	const std::u32string wantedReading(KFCRubyChangePlan::Compact(U32Of(asked.reading)));
	const auto addRow = [&](TextIndex start, TextIndex end) -> bool
	{
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
				const std::u32string one(KFCRubyChangePlan::Compact(U32Of(Plain(runs[g].reading))));
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
	size_t first = 0;
	size_t found = text.find(wantedBase);
	while (found != std::u32string::npos)
	{
		TextIndex rowStart = static_cast<TextIndex>(found);
		TextIndex rowEnd = rowStart + static_cast<int32>(wantedBase.size());
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
			readings += KFCRubyChangePlan::Compact(U32Of(Plain(runs[r].reading)));
		}
		if (r > first && readings.find(wantedReading) != std::u32string::npos
			&& (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), rowStart, rowEnd)))
		{
			if (!addRow(rowStart, rowEnd))
				return;
		}
		found = text.find(wantedBase, static_cast<size_t>(rowEnd));
	}
}

// THE OCCURRENCES OF THE BASE TEXT IN ONE STORY WITH NO RUBY ON THEM.
void WalkWithoutRuby(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const std::u32string text([&] {
		std::u32string out;
		const WideString all(KFCSearchEngine::ReadText(storyRef, 0, model->TotalLength()));
		out.reserve(static_cast<size_t>(model->TotalLength()));
		for (WideString::const_iterator c = all.begin(); c != all.end(); ++c)
			out.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
		return out; }());
	std::u32string wanted;
	const WideString wantedWide(asked.base);
	for (WideString::const_iterator c = wantedWide.begin(); c != wantedWide.end(); ++c)
		wanted.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	if (wanted.empty())
		return;
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	for (size_t at = text.find(wanted); at != std::u32string::npos; at = text.find(wanted, at + wanted.size()))
	{
		const TextIndex start = static_cast<TextIndex>(at);
		const TextIndex end = start + static_cast<int32>(wanted.size());
		if (StandsInSetAsideText(model, start))
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), start, end))
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

// THE WORDS OF A RUBY LIST IN ONE STORY (KFCRubyList.h): the longest at each place, left to right, each a row - but one a ruby stands on already: that is counted (ioHadRuby) and left, whatever it reads, for a list
// gives readings to text that has none. (A row whose ruby differs is Text With Ruby's to find, and its Replace's to write.) An occurrence a selection cuts is not found, as a text search finds none.
void WalkList(const UIDRef& storyRef, ITextModel* model, const KFCRubyList::Words& words, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped, int32& ioHadRuby)
{
	const TextIndex total = model->TotalLength();
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, 0, total)));
	if (static_cast<int32>(text.size()) != total)
		return;		// (a reading of another length has not told each position's character)
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	size_t at = 0;
	while (at < text.size())
	{
		const KFCRubyList::Entry* const entry = KFCRubyList::LongestAt(words, text, at);
		if (entry == nullptr)
		{
			++at;
			continue;
		}
		const TextIndex start = static_cast<TextIndex>(at);
		const TextIndex end = start + static_cast<int32>(entry->word.size());
		at += entry->word.size();
		if (StandsInSetAsideText(model, start))
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), start, end))
			continue;
		if (strand != nil && scanner != nil)
		{
			std::vector<RubyRun> on;
			RunsIn(strand, scanner, start, end, on);
			if (!on.empty())
			{
				++ioHadRuby;
				continue;
			}
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

// ONE DOCUMENT'S ROWS - of its searched part alone, given one. False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
	std::map<UID, uint32>& outVersions, bool& outCapped, int32& ioHadRuby)
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
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count && !outCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)))
			continue;
		if (asked.kind == kKFCRubyFindWithout)
			WalkWithoutRuby(storyRef, model, asked, part, limit, places, outCapped);
		else if (asked.kind == kKFCRubyFindMarks)
			WalkMarks(storyRef, model, asked, part, limit, places, outCapped);
		else if (asked.kind == kKFCRubyFindList)
			WalkList(storyRef, model, *asked.listWords, part, limit, places, outCapped, ioHadRuby);
		else
			WalkWithRuby(storyRef, model, asked, part, limit, places, outCapped);
	}
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	// What the rows with ruby draw there; a row without any draws its text.
	for (size_t h = 0; asked.kind == kKFCRubyFindWith && h < outHits.size() && h < places.size(); ++h)
	{
		const UIDRef storyRef(db, places[h].story);
		outHits[h].runChain = KFCRubySearch::RowWords(storyRef, places[h].start, places[h].end);
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].upper = KFCRubySearch::UpperOn(storyRef, places[h].start, places[h].end);
	}
	// A LIST'S ROWS DRAW WHAT THEIR REPLACE WILL GIVE THEM, faint: what the person reads the list for, before a row is written.
	for (size_t h = 0; asked.kind == kKFCRubyFindList && h < outHits.size() && h < places.size(); ++h)
	{
		const UIDRef storyRef(db, places[h].story);
		const KFCRubyList::Entry* const entry = KFCRubyList::FindWord(*asked.listWords, U32Of(KFCSearchEngine::ReadText(storyRef, places[h].start, places[h].end - places[h].start)));
		if (entry == nullptr)
			continue;
		const std::vector<RubyRun> planned(RunsOfPlan(places[h].start, entry->runs));
		outHits[h].runChain = WordsOver(storyRef, places[h].start, places[h].end, planned);
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].upper = UpperOver(places[h].start, planned);
		outHits[h].upper.planned = true;
	}
	return true;
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
			else if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
			else if (line == "scope=story")
				conditions.scope = IWalkerScopeFactoryUtils::kStoryScope;
			else if (line == "scope=toend")
				conditions.scope = IWalkerScopeFactoryUtils::kToEndOfStoryScope;
			else if (line == "scope=selection")
				conditions.scope = IWalkerScopeFactoryUtils::kSelectionScope;
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
		const bool walked = WalkDoc(docRef, asked, part, limit - static_cast<size_t>(out.total), hits, versions, capped, out.hadRuby);
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
	const bool walked = WalkDoc(docRef, asked, nil, limit, outHits, outVersions, outCapped, hadRuby);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kRubyDocSpan);
	return walked && !outCancelled;
}

bool KFCRubySearch::WriteDocument(const UIDRef& docRef, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount)
{
	outCount = 0;
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
	if (!WalkDoc(docRef, conditions, part, static_cast<size_t>(kMaxInt32), hits, versions, capped, hadRuby))
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
			if (!ListPlanForRow(*conditions.listWords, story, hit.textStart, hit.textEnd, runs) || ReadsAsPlanned(story, hit.textStart, hit.textEnd, runs))
				continue;
			if (WritePlan(story, hit.textStart, hit.textEnd, runs) != kSuccess)
				return false;
		}
		else
		{
			std::vector<KFCRubyChangePlan::Run> runs;
			const KFCRubyChangePlan::Result plan = PlanForRow(story, hit.textStart, hit.textEnd, pieces, runs);
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

KFCRubyChangePlan::Result KFCRubySearch::PlanForRow(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Piece>& pieces, std::vector<KFCRubyChangePlan::Run>& outRuns)
{
	return KFCRubyChangePlan::Make(pieces, U32Of(KFCSearchEngine::ReadText(story, start, end - start)), outRuns);
}

bool KFCRubySearch::ListPlanForRow(const KFCRubyList::Words& words, const UIDRef& story, TextIndex start, TextIndex end, std::vector<KFCRubyChangePlan::Run>& outRuns)
{
	outRuns.clear();
	const KFCRubyList::Entry* const entry = KFCRubyList::FindWord(words, U32Of(KFCSearchEngine::ReadText(story, start, end - start)));
	if (entry == nullptr)
		return false;
	outRuns = entry->runs;
	return true;
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
	if (end - start > 1)
	{
		// ONE READING OVER SEVERAL CHARACTERS: A GROUP (kTAMojiRubyBoss kFalse): under InDesign's default mono, or a mono the characters had, all of it stands on the first character.
		InterfacePtr<ITextAttrBoolean> mono(::CreateObject2<ITextAttrBoolean>(kTAMojiRubyBoss));
		if (mono == nil)
			return kFailure;
		mono->Set(kFalse);
		InterfacePtr<IAttrReport> monoReport(mono, UseDefaultIID());
		attrs->ApplyAttribute(monoReport);
	}
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
