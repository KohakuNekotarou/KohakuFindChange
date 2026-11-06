//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Spelling kind, listed - see KFCSpellingSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "ISpellingDataStrand.h"		// GetSpellingDataRun - what Dynamic Spelling found of the story's words; SpellingState
#include "ISpellingPrefs.h"			// GetDynamicSpellCheck - is it on
#include "IStoryList.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "IWalkerScopeFactoryUtils.h"	// its Search:

// General includes:
#include "ConditionalTextID.h"			// kHiddenTextBoss
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "LinguisticID.h"				// kSpellingStrandBoss, IID_ISPELLINGDATASTRAND
#include "PreferenceUtils.h"			// QuerySessionPreferences
#include "Utils.h"

#include <string>

// Project includes:
#include "KFCSpellingSearch.h"
#include "IKFCUIServices.h"				// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCOwnScope.h"				// what the icons take in
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line

namespace
{

// A document's share of the bar (KFCWarichuSearch's number).
const int32 kSpellingDocSpan = 100;

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note (KFCWarichuSearch.cpp's StandsInSetAsideText).
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// THE STATES A WAVY LINE IS DRAWN FOR, and listed (SP2): red - misspelled; green - repeated, uncapitalized, an uncapitalized sentence. kSpellError ("denotes the
// beginning of spelling errors" - ISpellingService.h) read as misspelled; the grammar error's start, note anchors and deleted text are none.
bool IsListedState(int32 state)
{
	return state == kSpellingError || state == kIncorrectWord || state == kUncapitalizedWord || state == kRepeatedWord || state == kUncapitalizedStartOfSentence;
}

// ONE STORY'S WAVY LINES, walked by the strand's runs (ISpellingDataStrand::GetSpellingDataRun): each run in error is one row - the line InDesign draws under it (its
// CoreDraw draws a run as one line). MEASURED (2026-11-06, work/cpp-probe/examples/spelling-strand.cpp - docs/ai-notes/kfc-label-spelling-measure-2026-11-06.md):
// count is what is left of the run FROM THE POSITION ASKED (asked one character in, the same begin and count less one) - the run ends at at + count; a run in error
// is its word exactly ("teh", "london"), a repeated word's run holds both words ("the the"). A story with a run not checked yet counts once in ioUnchecked.
void WalkSpelling(const UIDRef& storyRef, ITextModel* model, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, std::vector<int32>& ioKinds, int32& ioUnchecked, bool& outCapped)
{
	InterfacePtr<ISpellingDataStrand> strand(static_cast<ISpellingDataStrand*>(model->QueryStrand(kSpellingStrandBoss, IID_ISPELLINGDATASTRAND)));
	if (strand == nil)
		return;
	const TextIndex total = model->TotalLength();
	bool unchecked = false;
	TextIndex at = 0;
	while (at < total)
	{
		int32 count = 0;
		TextIndex runBegin = at;
		const SpellingState state = strand->GetSpellingDataRun(at, &count, &runBegin);
		if (count <= 0)
			break;
		const TextIndex runEnd = at + count;
		const TextIndex start = (runBegin >= 0 && runBegin <= at) ? runBegin : at;
		if (state == kSpellingUnchecked)
			unchecked = true;
		else if (IsListedState(state) && (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), start, runEnd))
			&& !StandsInSetAsideText(model, start) && taken.Takes(storyRef, model, start))
		{
			if (ioPlaces.size() >= limit)
			{
				outCapped = true;
				break;
			}
			KFCSearchEngine::ListedPlace place;
			place.story = storyRef.GetUID();
			place.start = start;
			place.end = runEnd;
			ioPlaces.push_back(place);
			ioKinds.push_back(static_cast<int32>(state));
		}
		at = runEnd;
	}
	if (unchecked)
		++ioUnchecked;
}

// ONE DOCUMENT'S ROWS - of its searched part alone, given one; each row's word on its locator (Hit::spellKind). False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const KFCSpellingConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, int32& ioUnchecked)
{
	outCapped = false;
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return false;
	// Composing to read a row's page dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	std::vector<KFCSearchEngine::ListedPlace> places;
	std::vector<int32> kinds;
	KFCOwnScope taken(asked.switches);
	const int32 count = stories->GetUserAccessibleStoryCount();		// (the user's stories alone - InDesign's own Check Spelling's and Find/Change's: D24-a)
	for (int32 s = 0; s < count && !outCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthUserAccessibleStoryUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)) || !taken.TakesStory(storyRef))
			continue;
		WalkSpelling(storyRef, model, taken, part, limit, places, kinds, ioUnchecked, outCapped);
	}
	const size_t first = outHits.size();
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	for (size_t i = 0; i < kinds.size() && first + i < outHits.size(); ++i)
	{
		outHits[first + i].spellKind = kinds[i];
		KFCResultModel::BuildHitLocator(outHits[first + i]);
	}
	return true;
}

}	// anonymous namespace

const char* KFCSpellingSearch::KindWord(int32 spellKind)
{
	switch (spellKind)
	{
		case kSpellingError:
		case kIncorrectWord:					return "misspelled";
		case kUncapitalizedWord:				return "uncapitalized";
		case kRepeatedWord:						return "repeated";
		case kUncapitalizedStartOfSentence:		return "uncapitalized sentence";
		default:								return "";
	}
}

bool KFCSpellingSearch::DynamicSpellingIsOn()
{
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch spelling-off: as Dynamic Spelling off (SP4's case), with no setting of the person's touched.
	if (KFC_DIAG_FAULT("spelling-off"))
		return false;
#endif
	InterfacePtr<ISpellingPrefs> prefs(static_cast<ISpellingPrefs*>(::QuerySessionPreferences(IID_ISPELLINGPREFS)));
	return prefs != nil && prefs->GetDynamicSpellCheck() != kFalse;
}

bool KFCSpellingSearch::IsTabCurrent(KFCSpellingConditions* outConditions)
{
	KFCSpellingConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch spelling-tab (the model's cases, which need no dialog): on = the Spelling kind, its lines its conditions.
	if (KFC_DIAG_FAULT("spelling-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("spelling-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
		{
			const std::string& line = lines[n];
			if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
			else if (line == "scope=story")
				conditions.scope = IWalkerScopeFactoryUtils::kStoryScope;
			else if (line == "scope=toend")
				conditions.scope = IWalkerScopeFactoryUtils::kToEndOfStoryScope;
			else if (line == "scope=selection")
				conditions.scope = IWalkerScopeFactoryUtils::kSelectionScope;
			else
				KFCOwnScopeDiagLine(line, conditions.switches);
		}
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCWarichuSearch::IsTabCurrent's rule): no UI half is no Spelling kind.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->SpellingTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCSpellingSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCSpellingConditions& asked,
	const std::vector<KFCSearchEngine::ListedPlace>* part)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kSpellingDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;		// the object lists' number, as a warichu list's
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
		const bool opened = fromBook && targets[i].docRef == UIDRef::gNull;		// (a closed chapter: it reads as it was saved - notCheckedClosed)
		if (opened && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			continue;
		const UIDRef docRef = targets[i].docRef;
		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> versions;
		bool capped = false;
		const bool walked = WalkDoc(docRef, asked, part, limit - static_cast<size_t>(out.total), hits, versions, capped, opened ? out.notCheckedClosed : out.notChecked);
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
	KFC_DIAG_LOG("SPELLING targets=%d rows=%d notchecked=%d closed=%d", static_cast<int>(targets.size()), static_cast<int>(out.total), static_cast<int>(out.notChecked),
		static_cast<int>(out.notCheckedClosed));
}

bool KFCSpellingSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCSpellingConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kSpellingDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	int32 unchecked = 0;
	const bool walked = WalkDoc(docRef, asked, nil, limit, outHits, outVersions, outCapped, unchecked);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kSpellingDocSpan);
	return walked && !outCancelled;
}

bool KFCSpellingSearch::WalkForFace(const UIDRef& docRef, const KFCSpellingConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part,
	std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	std::map<UID, uint32> versions;
	bool capped = false;
	int32 unchecked = 0;
	return WalkDoc(docRef, asked, part, static_cast<size_t>(kMaxInt32), outHits, versions, capped, unchecked);
}

// End, KFCSpellingSearch.cpp.
