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

// The chain as its row draws it (D1, D3): the steps joined by an arrow, the budget shared among them.
PMString ChainWords(const Place& place)
{
	PMString out;
	out.SetTranslatable(kFalse);
	const int32 steps = static_cast<int32>(place.steps.size());
	int32 each = (steps > 0) ? kChainChars / steps : kChainChars;
	if (each < kChainStepFloor)
		each = kChainStepFloor;
	for (int32 s = 0; s < steps; ++s)
	{
		if (s > 0)
			AppendStep(out, U" \u2192 ", 3);
		AppendStep(out, KFCRunListChain::StepWords(place, static_cast<size_t>(s)), each);
	}
	return out;
}
}	// anonymous namespace

KFCRunList::Recorder::Recorder(const std::vector<PMString>& queryNames, const std::vector<KFCBookScope::ChapterDoc>& targets)
	: fNames(queryNames), fTargets(targets), fObjectQuery(false)
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

void KFCRunList::Recorder::NoteObjectQuery()
{
	fObjectQuery = true;
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
		total += docTotal;
		gone += docGone;
		if (!mine.places.empty())
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
			for (size_t p = 0; p < rows[r].from.size(); ++p)
			{
				const std::vector<int32_t> queries(KFCRunListChain::QueriesOf(*rows[r].from[p]));
				for (size_t q = 0; q < queries.size(); ++q)
					queryListed[static_cast<size_t>(queries[q])] = true;
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
				KFCResultModel::Hit& hit = hits[h];
				const Place& place = *rows[r].from[h];
				// A ROW THE RUN WROTE (R6): what a Replace leaves on a row, with no copy - "Changed", its eye on, what it wrote, and what
				// its place has been.
				hit.replaced = true;
				hit.kept = true;
				hit.replacedText = KFCSearchEngine::ReadText(UIDRef(target.docRef.GetDataBase(), hit.storyUID), hit.textStart, hit.textEnd - hit.textStart);
				hit.replacedText.SetTranslatable(kFalse);
				hit.runChain = ChainWords(place);
				hit.runNames.Clear();
				hit.runNames.SetTranslatable(kFalse);
				const std::vector<int32_t> queries(KFCRunListChain::QueriesOf(place));
				for (size_t q = 0; q < queries.size(); ++q)
				{
					if (q > 0)
						hit.runNames.Append(", ");
					hit.runNames.Append(fNames[static_cast<size_t>(queries[q])]);
				}
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
	return listed;
}

// End, KFCRunList.cpp.
