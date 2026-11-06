//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The list a saved query run leaves - see KFCRunList.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IDataBase.h"
#include "IFindChangeCmdData.h"
#include "IFindChangeOptions.h"
#include "IFindChangeService.h"		// FindChangeResult
#include "ITextModel.h"
#include "ITextWalker.h"
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "TextChar.h"				// kTextChar_Ellipse
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss

#include <memory>		// std::shared_ptr - the list's query copy

// Project includes:
#include "KFCRunList.h"
#include "KFCChangeAll.h"			// InitWalk - the walk the write takes
#include "KFCDiag.h"
#include "KFCFindChangeCmds.h"		// ProcessFindChangeCmd
#include "KFCQueryCopy.h"			// CaptureRunList - what lets the list be kept as a tier and switched back to
#include "KFCResultModel.h"
#include "KFCRowFoci.h"				// AttachOpenChapters - the rows follow the text, as a search's do
#include "KFCSearchEngine.h"		// WalkerStops, ReadText, BuildListedHits, FileChapter
#include "KFCTextCut.h"				// KFCSafeCut - no cut in front of a mark that belongs to the character before it

namespace
{
using KFCRunListAlign::Match;
using KFCRunListAlign::Placed;
using KFCRunListAlign::Writes;
using KFCRunListChain::Place;

// THE ROWS A RUN'S LIST HOLDS (R9): the object lists' number (the narrow-down spec's N7 - what the panel draws and walks well).
// More are counted and said. (Fault switch run-list-cap, a test build's only - KFCDiag.h: a smaller number, so a test reaches it.)
int32 RowCap()
{
#ifdef KFC_DIAG
	return KFCDiagFaultValue("run-list-cap", 0, KFCResultModel::kKFCCollectObjectLimit);
#else
	return KFCResultModel::kKFCCollectObjectLimit;
#endif
}

// A CHAIN'S WORDS ON ITS ROW (the design's section 7): the line's budget, shared among its steps, no step left too short to read.
const int32 kChainChars = 50;
const int32 kChainStepFloor = 8;

// A STORY'S TEXT, WHOLE, one code point a position - what a TextIndex counts (memory textindex-counts-code-points).
bool ReadStoryText(const UIDRef& story, std::u32string& out)
{
	out.clear();
	IDataBase* const db = story.GetDataBase();
	if (db == nil || !db->IsValidUID(story.GetUID()))
		return false;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return false;
	const int32 total = model->TotalLength();
	const WideString text(KFCSearchEngine::ReadText(story, 0, total));
	out.reserve(static_cast<size_t>(total));
	for (WideString::const_iterator c = text.begin(); c != text.end(); ++c)
		out.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	// (A reading that came out another length has not told each position's character: said by the caller as "not placed".)
	return static_cast<int32>(out.size()) == total;
}

// WHAT THE QUERY FIND/CHANGE HOLDS NOW WRITES (KFCRunListAlign::Writes). A $ in Change To is found text - read so on any tab: the
// Text tab has no found text, and a literal $ there only costs an edge told from the text instead of from the length.
Writes WritesOfQuery()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return KFCRunListAlign::kAnyLength;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	if (mode == IFindChangeOptions::kGlyphSearch)
		return KFCRunListAlign::kOneLength;		// a glyph: one character, or nothing
	if (mode != IFindChangeOptions::kTextSearch && mode != IFindChangeOptions::kGrepSearch)
		return KFCRunListAlign::kAnyLength;		// Transliterate: as long as what it converts
	const PMString change(opts->GetReplaceString(mode));
	if (change.IsEmpty())
		return KFCRunListAlign::kEmptyChange;
	return (change.IndexOfWChar(UTF32TextChar('$'), 0) >= 0) ? KFCRunListAlign::kAnyLength : KFCRunListAlign::kOneLength;
}

// EVERY MATCH THE WALK SELECTS, by story - KFCSearchEngine's CollectHitsInDoc's loop, with nothing read of a match but its place: a
// find to kNotFound (a failure is false, its error cleared - a walk that broke off says nothing of the rest), the selections'
// critical section around it, the walker halted on the way out.
bool CollectMatches(const UIDRef& docRef, int32 writeScope, const WalkerScopeOptions& options, std::map<UID, std::vector<Match> >& out,
	int32& outFound)
{
	outFound = 0;
	InterfacePtr<ITextWalker> walker;
	InterfacePtr<ITextWalkerScope> scope;
	bool nothingToWalk = false;
	if (!KFCChangeAll::InitWalk(docRef, writeScope, options, walker, scope, nothingToWalk))
		return false;
	const KFCSearchEngine::WalkerStops stops(walker);
	if (nothingToWalk)
		return true;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	if (selUtils == nil)
		return false;
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);
	while (true)
	{
		InterfacePtr<ICommand> findCmd(CmdUtils::CreateCommand(kFindTextCmdBoss));
		InterfacePtr<IFindChangeCmdData> cmdData(findCmd, UseDefaultIID());
		if (findCmd == nil || cmdData == nil)
			return false;
		cmdData->SetTextWalker(walker);
		if (!KFCFindChangeCmds::ProcessFindChangeCmd(findCmd))
			return false;
		const IFindChangeService::FindChangeResult found = cmdData->GetFindChangeResult();
		if (found != IFindChangeService::kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			return found != IFindChangeService::kFailure;		// kNotFound, kFoundCompleted: the end of the walk
		}
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		const UIDRef story = cmdData->GetRange(start, end);
		out[story.GetUID()].push_back(Match{ static_cast<int32_t>(start), static_cast<int32_t>(end) });
		++outFound;
	}
}

PMString Say(const char* words)
{
	PMString s(words);
	s.SetTranslatable(kFalse);
	return s;
}

// One step's words as the row draws them: cut at `chars`, its mark where it was cut, as a match's cut is marked (the row's line).
void AppendStep(PMString& out, const std::u32string& words, int32 chars)
{
	PMString s;
	s.SetTranslatable(kFalse);
	for (size_t c = 0; c < words.size(); ++c)
		s.AppendW(UTF32TextChar(static_cast<uint32>(words[c])));
	// NOTHING THE ROW WOULD DRAW (an inline frame's anchor alone - KFCResultModel::MarkUpBreaksForDisplay drops the marks an object stands
	// on): said, or the step reads as nothing at all.
	PMString drawn(s);
	KFCResultModel::MarkUpBreaksForDisplay(drawn);
	if (drawn.IsEmpty() && !s.IsEmpty())
	{
		s = "(object)";
		s.SetTranslatable(kFalse);
	}
	if (s.CharCount() > chars)
	{
		int32 keep = KFCSafeCut(s, chars);
		if (keep <= 0)
			keep = chars;		// one character carrying the whole budget in marks: cut inside them rather than draw nothing
		s.Remove(keep, kMaxInt32);
		s.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	out.Append(s);
}

// IS THIS STEP A FIND'S (2026-11-06 - RF3): its query wrote nothing - the text it read is the step before it, no step of the chain.
bool FoundStep(const KFCRunListChain::Step& step, const std::vector<bool>& found)
{
	return step.query >= 0 && static_cast<size_t>(step.query) < found.size() && found[static_cast<size_t>(step.query)];
}

// DID A QUERY WRITE THIS PLACE - or only find it (RF3: a found place's row is no written row - no "Changed", no eye, no chain).
bool WrittenPlace(const Place& place, const std::vector<bool>& found)
{
	for (size_t s = 1; s < place.steps.size(); ++s)
		if (!FoundStep(place.steps[s], found))
			return true;
	return false;
}

// The chain as its row draws it (D1, D3): the steps joined by an arrow, the budget shared among them - a find's step left out (RF3).
PMString ChainWords(const Place& place, const std::vector<bool>& found)
{
	PMString out;
	out.SetTranslatable(kFalse);
	std::vector<size_t> shown;
	for (size_t s = 0; s < place.steps.size(); ++s)
		if (s == 0 || !FoundStep(place.steps[s], found))
			shown.push_back(s);
	const int32 steps = static_cast<int32>(shown.size());
	int32 each = (steps > 0) ? kChainChars / steps : kChainChars;
	if (each < kChainStepFloor)
		each = kChainStepFloor;
	for (int32 s = 0; s < steps; ++s)
	{
		if (s > 0)
			AppendStep(out, U" \u2192 ", 3);
		AppendStep(out, KFCRunListChain::StepWords(place, shown[static_cast<size_t>(s)]), each);
	}
	return out;
}

// A FOUND ROW THAT STANDS ON TEXT ALONE (RF3): a story's [start, end) and nothing else it names - a ruby, kenten, warichu or spelling row (an item's,
// a story's, a table's or a cell's row names what it stands for: kept as it is - RF4).
bool IsTextPlace(const KFCResultModel::Hit& hit)
{
	return hit.holderKind == kKFCHolderNone && hit.itemUID == kInvalidUID && hit.storyUID != kInvalidUID && hit.textEnd > hit.textStart;
}

// "a, b" - the run's names of the queries a row stands for, in the run's order.
PMString NamesOf(const std::vector<int32_t>& queries, const std::vector<PMString>& names)
{
	PMString out;
	out.SetTranslatable(kFalse);
	for (size_t q = 0; q < queries.size(); ++q)
	{
		if (queries[q] < 0 || static_cast<size_t>(queries[q]) >= names.size())
			continue;
		if (!out.IsEmpty())
			out.Append(", ");
		out.Append(names[static_cast<size_t>(queries[q])]);
	}
	return out;
}
}	// anonymous namespace

KFCRunList::Recorder::Recorder(const std::vector<PMString>& queryNames, const std::vector<KFCBookScope::ChapterDoc>& targets,
	bool byChangeAll)
	: fNames(queryNames), fTargets(targets), fObjectQuery(false), fKohakuQuery(false), fFound(queryNames.size(), false), fByChangeAll(byChangeAll)
{
	fDocOfTarget.reserve(targets.size());
	for (size_t t = 0; t < targets.size(); ++t)
	{
		size_t d = 0;
		while (d < fDocs.size() && fTargets[fDocs[d].target].docRef.GetDataBase() != targets[t].docRef.GetDataBase())
			++d;
		if (d == fDocs.size())
		{
			Doc doc;
			doc.target = t;
			fDocs.push_back(doc);
		}
		fDocOfTarget.push_back(d);
	}
}

KFCRunList::Recorder::Doc& KFCRunList::Recorder::DocOf(size_t target)
{
	return fDocs[fDocOfTarget[target]];
}

void KFCRunList::Recorder::LeaveOut(Doc& doc, const PMString& why)
{
	// (The first reason is the one that said: what came after it was not looked at.)
	if (doc.out)
		return;
	doc.out = true;
	doc.why = why;
	doc.stories.clear();
	KFC_DIAG_LOG("RUNLIST left out: %s", why.GetUTF8String().c_str());
}

void KFCRunList::Recorder::RecordBefore(size_t target, int32 query, int32 writeScope, const WalkerScopeOptions& options)
{
	Doc& doc = DocOf(target);
	doc.walked = false;
	if (doc.out)
		return;
	std::map<UID, std::vector<Match> > found;
	const auto notPlaced = [&]()
	{
		PMString why(" The list leaves out ");
		why.SetTranslatable(kFalse);
		why.Append(fTargets[doc.target].shortName);
		why.Append(" - the replaced text of ");
		why.Append(fNames[static_cast<size_t>(query)]);
		why.Append(" could not be placed.");
		LeaveOut(doc, why);
	};
	const UIDRef docRef(fTargets[target].docRef);
	if (!CollectMatches(docRef, writeScope, options, found, doc.found))
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// the list's trouble alone: the write goes on
		notPlaced();
		return;
	}
	doc.writes = WritesOfQuery();
	IDataBase* const db = docRef.GetDataBase();
	for (std::map<UID, std::vector<Match> >::iterator s = found.begin(); s != found.end(); ++s)
	{
		Story& story = doc.stories[s->first];
		// THE WALK GOES IN READING ORDER, NOT THE TEXT'S (a table's cells where the table stands, and "backward" as the dialog
		// says - memory walker-scope-options-and-hidden-layers): laid in place order.
		std::sort(s->second.begin(), s->second.end(), [](const Match& a, const Match& b) { return a.start != b.start ? a.start < b.start : a.end < b.end; });
		story.matches.swap(s->second);
		if (!ReadStoryText(UIDRef(db, s->first), story.before))
		{
			notPlaced();
			return;
		}
	}
	doc.walked = true;
}

void KFCRunList::Recorder::RecordAfter(size_t target, int32 query, int32 count)
{
	Doc& doc = DocOf(target);
	if (doc.out || !doc.walked)
		return;
	doc.walked = false;
	int32 selected = doc.found;
#ifdef KFC_DIAG
	// (Fault switch run-list-mismatch, a test build's only - KFCDiag.h: the find took as having selected one more than the write
	// wrote, in the run's first document - what R4 has to catch.)
	if (fDocOfTarget[target] == 0 && KFC_DIAG_FAULT("run-list-mismatch"))
		++selected;
#endif
	// THE FIND AND THE CHANGE ALL AGREE, OR THE ROWS WOULD SAY WHAT THE WRITE DID NOT (R4). (Measured to agree - the design's M2.)
	if (selected != count)
	{
		PMString why(" The list leaves out ");
		why.SetTranslatable(kFalse);
		why.Append(fTargets[doc.target].shortName);
		why.Append(" - ");
		why.Append(fNames[static_cast<size_t>(query)]);
		why.Append(" replaced ");
		why.AppendNumber(count);
		why.Append(" where a find selected ");
		why.AppendNumber(selected);
		why.Append(".");
		LeaveOut(doc, why);
		return;
	}
	IDataBase* const db = fTargets[target].docRef.GetDataBase();
	std::vector<UID> gone;
	for (std::map<UID, Story>::iterator s = doc.stories.begin(); s != doc.stories.end(); ++s)
	{
		Story& story = s->second;
		if (story.matches.empty())
			continue;		// no match of this query: what it holds stays, read again at the end of the run
		std::u32string after;
		if (db == nil || !db->IsValidUID(s->first))
		{
			// THE STORY WENT WITH THE WRITE (an inline frame's, with its anchor): its places are counted at the end as gone - and
			// this query's matches with them, one place each.
			for (size_t m = 0; m < story.matches.size(); ++m)
			{
				Place place;
				place.start = place.end = 0;
				place.steps.push_back(KFCRunListChain::Step{ -1, std::u32string() });
				story.places.push_back(place);
			}
			story.matches.clear();
			story.before.clear();
			continue;
		}
		std::vector<Placed> placed;
		if (!ReadStoryText(UIDRef(db, s->first), after) || !KFCRunListChain::ReadsAsKept(story.places, story.before)
			|| !KFCRunListAlign::Locate(story.before, after, story.matches, doc.writes, placed))
		{
			PMString why(" The list leaves out ");
			why.SetTranslatable(kFalse);
			why.Append(fTargets[doc.target].shortName);
			why.Append(" - the replaced text of ");
			why.Append(fNames[static_cast<size_t>(query)]);
			why.Append(" could not be placed.");
			LeaveOut(doc, why);
			return;
		}
		KFCRunListChain::Apply(story.places, query, story.before, after, story.matches, placed);
		story.matches.clear();
		story.before.clear();
		story.before.shrink_to_fit();
	}
}

void KFCRunList::Recorder::NoteKohakuQuery()
{
	fKohakuQuery = true;
}

void KFCRunList::Recorder::NoteObjectQuery()
{
	fObjectQuery = true;
}

void KFCRunList::Recorder::RecordFound(size_t target, int32 query, const std::vector<KFCResultModel::Hit>& hits)
{
	if (query >= 0 && static_cast<size_t>(query) < fFound.size())
		fFound[static_cast<size_t>(query)] = true;
	Doc& doc = DocOf(target);
	if (doc.out)
		return;
	IDataBase* const db = fTargets[target].docRef.GetDataBase();
	// THE TEXT MATCHES BY STORY (RF3); the other rows kept as they were found (RF4).
	std::map<UID, std::vector<size_t> > byStory;
	for (size_t h = 0; h < hits.size(); ++h)
	{
		if (IsTextPlace(hits[h]))
			byStory[hits[h].storyUID].push_back(h);
		else
			doc.foundItems.push_back(std::make_pair(query, hits[h]));
	}
	for (std::map<UID, std::vector<size_t> >::iterator s = byStory.begin(); s != byStory.end(); ++s)
	{
		Story& story = doc.stories[s->first];
		std::u32string now;
		// ITS TEXT READ, AND THE PLACES SO FAR READ IN IT AS THEY SAY THEY DO (R4 - a write the run did not follow moved it: no row of this document stands on anything).
		if (db == nil || !ReadStoryText(UIDRef(db, s->first), now) || !KFCRunListChain::ReadsAsKept(story.places, now))
		{
			PMString why(" The list leaves out ");
			why.SetTranslatable(kFalse);
			why.Append(fTargets[doc.target].shortName);
			why.Append(" - the found text of ");
			why.Append(fNames[static_cast<size_t>(query)]);
			why.Append(" could not be placed.");
			LeaveOut(doc, why);
			return;
		}
		// IN PLACE ORDER, OVERLAPS JOINED (Apply lays matches that do not overlap): a match alone keeps its row (its tag); one joined is the text it covers.
		std::vector<size_t>& at = s->second;
		std::sort(at.begin(), at.end(), [&](size_t a, size_t b)
			{ return hits[a].textStart != hits[b].textStart ? hits[a].textStart < hits[b].textStart : hits[a].textEnd < hits[b].textEnd; });
		const int32_t length = static_cast<int32_t>(now.size());
		std::vector<Match> matches;
		std::vector<int32_t> tags;
		for (size_t i = 0; i < at.size(); ++i)
		{
			const KFCResultModel::Hit& hit = hits[at[i]];
			const int32_t start = std::max<int32_t>(0, static_cast<int32_t>(hit.textStart));
			const int32_t end = std::min<int32_t>(length, static_cast<int32_t>(hit.textEnd));
			if (end <= start)
				continue;
			if (!matches.empty() && start < matches.back().end)
			{
				matches.back().end = std::max(matches.back().end, end);
				tags.back() = -1;
				continue;
			}
			matches.push_back(Match{ start, end });
			doc.foundText.push_back(hit);
			tags.push_back(static_cast<int32_t>(doc.foundText.size() - 1));
		}
		if (matches.empty())
			continue;
		// LAID AS A WRITE THAT LEFT THE TEXT AS IT WAS: each match where it stands, before and after the same.
		std::vector<Placed> placed;
		for (size_t m = 0; m < matches.size(); ++m)
			placed.push_back(Placed{ static_cast<int32_t>(m), static_cast<int32_t>(m), matches[m].start, matches[m].end });
		KFCRunListChain::Apply(story.places, query, now, now, matches, placed);
		// A MATCH OVER NO PLACE is a place of its own now, where it stands, of two steps - its row's tag on it (one laid over a place joined it: no tag).
		for (size_t m = 0; m < matches.size(); ++m)
		{
			if (tags[m] < 0)
				continue;
			for (size_t p = 0; p < story.places.size(); ++p)
			{
				Place& place = story.places[p];
				if (place.start == matches[m].start && place.end == matches[m].end && place.steps.size() == 2 && place.steps.back().query == query)
				{
					place.tag = tags[m];
					break;
				}
			}
		}
	}
	KFC_DIAG_LOG("RUNLIST found query=%d hits=%d text=%d items=%d", static_cast<int>(query), static_cast<int>(hits.size()),
		static_cast<int>(doc.foundText.size()), static_cast<int>(doc.foundItems.size()));
}

int32 KFCRunList::Recorder::FileList(bool fromBook, bool allDocuments, KFCResultModel::SearchScopeKind scope, const PMString& bookName,
	PMString& outWords)
{
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	const int32 cap = RowCap();
	int32 total = 0, listed = 0, gone = 0;
	struct Rows
	{
		size_t										doc;
		std::vector<KFCSearchEngine::ListedPlace>	places;
		std::vector<const Place*>					from;
		std::vector<const std::pair<int32, KFCResultModel::Hit>*>	items;		// the other rows a query that changes nothing found (RF4)
	};
	std::vector<Rows> rows;
	for (size_t d = 0; d < fDocs.size(); ++d)
	{
		Doc& doc = fDocs[d];
		if (doc.out)
			continue;
		const UIDRef docRef(fTargets[doc.target].docRef);
		IDataBase* const db = docRef.GetDataBase();
		// A DOCUMENT GONE SINCE IT WAS WRITTEN (KFCQuerySequence.cpp - InDesign can close a placed .indd's document by itself): nothing
		// of it is read through its address, and nothing is said of what cannot be listed - it is not open to go to.
		if (!KFCBookScope::IsDocStillOpen(docRef))
		{
			KFC_DIAG_LOG("RUNLIST %s is no longer open", fTargets[doc.target].shortName.GetUTF8String().c_str());
			continue;
		}
		Rows mine;
		mine.doc = d;
		int32 docTotal = 0, docGone = 0;
		bool readsAsKept = true;
		for (std::map<UID, Story>::iterator s = doc.stories.begin(); s != doc.stories.end() && readsAsKept; ++s)
		{
			const std::vector<Place>& places = s->second.places;
			if (places.empty())
				continue;
			std::u32string now;
			if (db == nil || !db->IsValidUID(s->first))
			{
				docGone += static_cast<int32>(places.size());
				continue;
			}
			// THE PLACES READ IN THE TEXT AS IT STANDS, OR NO ROW OF THIS DOCUMENT STANDS ON ANYTHING (R4): something the run did not
			// write moved the text between its writes.
			if (!ReadStoryText(UIDRef(db, s->first), now) || !KFCRunListChain::ReadsAsKept(places, now))
			{
				readsAsKept = false;
				break;
			}
			docTotal += static_cast<int32>(places.size());
			for (size_t p = 0; p < places.size(); ++p)
			{
				if (listed >= cap)
					continue;		// (counted above - the chains went on being followed: only a row is what the cap limits)
				++listed;
				KFCSearchEngine::ListedPlace place;
				place.story = s->first;
				place.start = places[p].start;
				place.end = places[p].end;
				mine.places.push_back(place);
				mine.from.push_back(&places[p]);
			}
		}
		if (!readsAsKept)
		{
			listed -= static_cast<int32>(mine.places.size());
			PMString why(" The list leaves out ");
			why.SetTranslatable(kFalse);
			why.Append(fTargets[doc.target].shortName);
			why.Append(" - its text moved under the run's writes.");
			LeaveOut(doc, why);
			continue;
		}
		// THE OTHER ROWS FOUND (RF4): each whose item - or, with none, its story - is still there; one gone is counted with the places gone.
		for (size_t f = 0; f < doc.foundItems.size(); ++f)
		{
			const KFCResultModel::Hit& hit = doc.foundItems[f].second;
			const bool there = (db != nil) && ((hit.itemUID != kInvalidUID) ? db->IsValidUID(hit.itemUID)
				: (hit.storyUID == kInvalidUID || db->IsValidUID(hit.storyUID)));
			if (!there)
			{
				++docGone;
				continue;
			}
			++docTotal;
			if (listed >= cap)
				continue;
			++listed;
			mine.items.push_back(&doc.foundItems[f]);
		}
		total += docTotal;
		gone += docGone;
		if (!mine.places.empty() || !mine.items.empty())
			rows.push_back(std::move(mine));
	}

	// THE ROWS, UNDER THE HEADER OF A QUERY RUN'S LIST: the header's order is WriteListHeader's; the rows are filed as a search files
	// them (page order, each its locator - FileChapter). None: nothing written, the panel left as a run has always left it (the
	// run's own words on the line, and no row reading "(0)").
	if (!rows.empty())
	{
		KFCResultModel::SetFromBook(fromBook);
		KFCResultModel::SetFromAllDocuments(allDocuments);
		KFCResultModel::SetSearchScope(scope);
		KFCResultModel::SetBookName(bookName);
		KFCResultModel::SetSearchMode(static_cast<int32>(kKFCRunListSearchMode));
		KFCQueryCopy copy;
		KFCQueryCopier::CaptureRunList(copy);
		KFCResultModel::SetQueryCopy(std::make_shared<KFCQueryCopy>(copy));
		// (The queries the rows name, in the run's order: what its heading names the list by - R7.)
		std::vector<bool> queryListed(fNames.size(), false);
		for (size_t r = 0; r < rows.size(); ++r)
		{
			for (size_t p = 0; p < rows[r].from.size(); ++p)
			{
				const std::vector<int32_t> queries(KFCRunListChain::QueriesOf(*rows[r].from[p]));
				for (size_t q = 0; q < queries.size(); ++q)
					queryListed[static_cast<size_t>(queries[q])] = true;
			}
			for (size_t i = 0; i < rows[r].items.size(); ++i)
				if (rows[r].items[i]->first >= 0 && static_cast<size_t>(rows[r].items[i]->first) < queryListed.size())
					queryListed[static_cast<size_t>(rows[r].items[i]->first)] = true;
		}
		PMString names;
		names.SetTranslatable(kFalse);
		for (size_t q = 0; q < fNames.size(); ++q)
			if (queryListed[q])
			{
				if (!names.IsEmpty())
					names.Append(", ");
				names.Append(fNames[q]);
			}
		KFCResultModel::SetRunNames(names);
		KFCResultModel::SetRunByChangeAll(fByChangeAll);		// (Change All in Book's list: its names are the query's words - 11-04)
		KFCSearchEngine::CollectTally tally;
		for (size_t r = 0; r < rows.size(); ++r)
		{
			const Doc& doc = fDocs[rows[r].doc];
			const KFCBookScope::ChapterDoc& target = fTargets[doc.target];
			std::vector<KFCResultModel::Hit> hits;
			std::map<UID, uint32> versions;
			KFCSearchEngine::BuildListedHits(target.docRef, rows[r].places, hits, versions);
			for (size_t h = 0; h < hits.size(); ++h)
			{
				const Place& place = *rows[r].from[h];
				// (Change All in Book's rows name no query: there is one, the heading's - 2026-11-04.)
				const std::vector<int32_t> queries(fByChangeAll ? std::vector<int32_t>() : KFCRunListChain::QueriesOf(place));
				if (!WrittenPlace(place, fFound))
				{
					// A PLACE ONLY FOUND (RF3, RF5): its row read from the text as it stands now - its line, its page, its match's hash: what a landing compares
					// the document with (RowReadsAsFoundAt reads the line around the match again: a row keeping the line the find read, before a later write,
					// read "Missing" - GREEN 11-06) - and, found alone, the words its kind drew it with: a ruby's or a kenten's reading (runChain, upper), a word's
					// spelling (spellKind), a reading's group. Found by two finds over one another: the text it stands on. No "Changed", no eye.
					if (place.tag >= 0 && static_cast<size_t>(place.tag) < doc.foundText.size())
					{
						const KFCResultModel::Hit& found = doc.foundText[static_cast<size_t>(place.tag)];
						hits[h].runChain = found.runChain;
						hits[h].upper = found.upper;
						hits[h].spellKind = found.spellKind;
						hits[h].groupKey = found.groupKey;
						hits[h].groupLabel = found.groupLabel;
						hits[h].otherReadings = found.otherReadings;
					}
					hits[h].replaced = false;
					hits[h].kept = false;
					hits[h].runNames = NamesOf(queries, fNames);
					continue;
				}
				KFCResultModel::Hit& hit = hits[h];
				// A ROW THE RUN WROTE (R6): what a Replace leaves on a row, with no copy - "Changed", its eye on, what it wrote, and what
				// its place has been (a find's step no step of it - RF3).
				hit.replaced = true;
				hit.kept = true;
				hit.replacedText = KFCSearchEngine::ReadText(UIDRef(target.docRef.GetDataBase(), hit.storyUID), hit.textStart, hit.textEnd - hit.textStart);
				hit.replacedText.SetTranslatable(kFalse);
				hit.runChain = ChainWords(place, fFound);
				hit.runNames = NamesOf(queries, fNames);
			}
			// THE OTHER ROWS FOUND (RF4): as they were found, named by their query - their stories' versions read now, as the places' are.
			IDataBase* const db = target.docRef.GetDataBase();
			for (size_t i = 0; i < rows[r].items.size(); ++i)
			{
				KFCResultModel::Hit hit(rows[r].items[i]->second);
				hit.replaced = false;
				hit.kept = false;
				hit.runNames = NamesOf(std::vector<int32_t>(1, rows[r].items[i]->first), fNames);
				uint32 version = 0;
				if (hit.storyUID != kInvalidUID && versions.find(hit.storyUID) == versions.end() && KFCSearchEngine::ReadStoryVersion(db, hit.storyUID, version))
					versions[hit.storyUID] = version;
				hits.push_back(hit);
			}
			KFCSearchEngine::FileChapter(target, hits, tally, &versions);
		}
		// The rows follow the text as a search's do (the text-focus spec's T6): an edit made since is followed to where it went.
		KFCRowFoci::AttachOpenChapters();
	}

	// WHAT THE MESSAGE LINE SAYS OF IT (R10): how many, then why any are missing.
	if (listed > 0)
	{
		outWords.Append(" Listed ");
		outWords.AppendNumber(listed);
		outWords.Append(" place(s).");
	}
	if (total > listed)
	{
		outWords.Append(" The list shows the first ");
		outWords.AppendNumber(listed);
		outWords.Append(" of ");
		outWords.AppendNumber(total);
		outWords.Append(" places.");
	}
	if (gone > 0)
	{
		outWords.Append(" ");
		outWords.AppendNumber(gone);
		outWords.Append(" place(s) whose story is gone are not listed.");
	}
	for (size_t d = 0; d < fDocs.size(); ++d)
		if (fDocs[d].out)
			outWords.Append(fDocs[d].why);
	if (fObjectQuery)
		outWords.Append(" Object queries are not listed.");
	if (fKohakuQuery)
		outWords.Append(" Kohaku+ queries are not listed.");
	return listed;
}

// End, KFCRunList.cpp.
