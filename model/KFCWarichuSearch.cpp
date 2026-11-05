//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Warichu kind, listed - see KFCWarichuSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"
#include "IComposeScanner.h"			// QueryAttributeAt - a stretch's warichu, as the composer reads it
#include "IDataBase.h"
#include "IStoryList.h"
#include "ITextAttrBoolean.h"			// kTAWarichuAttrBoss's value
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "IWalkerScopeFactoryUtils.h"	// its Search:

// General includes:
#include "CJKID.h"						// kTAWarichuAttrBoss
#include "ConditionalTextID.h"			// kHiddenTextBoss
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "Utils.h"

#include <string>

// Project includes:
#include "KFCWarichuSearch.h"
#include "IKFCUIServices.h"				// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCOwnScope.h"				// what the icons take in
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line

namespace
{

// A document's share of the bar (KFCKentenSearch's number).
const int32 kWarichuDocSpan = 100;

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note (KFCKentenSearch.cpp's StandsInSetAsideText).
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// NO ROW RUNS OVER THESE: a paragraph's end, a table's or an anchor's character, anything of InDesign's in the text (KFCKentenSearch.cpp's StopsRow).
bool StopsRow(char32_t c)
{
	return c < 0x20 || c == 0xFEFF || (c >= 0xE000 && c <= 0xF8FF);
}

std::u32string U32Of(const PMString& words)
{
	std::u32string out;
	const WideString wide(words);
	for (WideString::const_iterator c = wide.begin(); c != wide.end(); ++c)
		out.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	return out;
}

// IS `at` SET AS WARICHU, AND HOW FAR THAT GOES, up to `until` (joined where it is unchanged, by the scanner): as the composer reads it, a character style's too.
bool WarichuAt(IComposeScanner* scanner, TextIndex at, TextIndex until, int32& outLength)
{
	outLength = 0;
	InterfacePtr<const IAttrReport> attr(scanner->QueryAttributeAt(at, until, kTAWarichuAttrBoss, &outLength));
	InterfacePtr<const ITextAttrBoolean> on(attr, UseDefaultIID());
	return on != nil && on->GetFlag();
}

// THE STRETCHES OF WARICHU OF ONE STORY: each thread's on its own (a table's cells are threads of their own, and the scanner would run on across them), cut where StopsRow stands.
void WalkWarichu(const UIDRef& storyRef, ITextModel* model, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;
	const TextIndex total = model->TotalLength();
	TextIndex at = 0;
	while (at < total)
	{
		TextIndex threadStart = at;
		int32 threadSpan = 0;
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, &threadSpan));
		const TextIndex until = (thread != nil && threadStart + threadSpan > at) ? threadStart + threadSpan : total;
		int32 len = 0;
		const bool warichu = WarichuAt(scanner, at, until, len);
		if (len <= 0)
			break;
		const TextIndex runEnd = at + len;
		if (warichu && !StandsInSetAsideText(model, at))
		{
			const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, at, len)));
			size_t from = 0;
			for (size_t k = 0; k <= text.size(); ++k)
			{
				if (k < text.size() && !StopsRow(text[k]))
					continue;
				const TextIndex start = at + static_cast<TextIndex>(from);
				const TextIndex end = at + static_cast<TextIndex>(k);
				if (k > from && (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), start, end)) && taken.Takes(storyRef, model, start))
				{
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
				from = k + 1;
			}
		}
		at = runEnd;
	}
}

// ONE DOCUMENT'S ROWS - of its searched part alone, given one. A row draws its own text (no runChain). False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const KFCWarichuConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
	std::map<UID, uint32>& outVersions, bool& outCapped)
{
	outCapped = false;
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return false;
	// Composing to read a row's page dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	std::vector<KFCSearchEngine::ListedPlace> places;
	KFCOwnScope taken(asked.switches);
	const int32 count = stories->GetUserAccessibleStoryCount();		// (the user's stories alone - KFCRubySearch.cpp's WalkDoc's note, D24-a)
	for (int32 s = 0; s < count && !outCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthUserAccessibleStoryUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)) || !taken.TakesStory(storyRef))
			continue;
		WalkWarichu(storyRef, model, taken, part, limit, places, outCapped);
	}
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	return true;
}

}	// anonymous namespace

bool KFCWarichuSearch::IsTabCurrent(KFCWarichuConditions* outConditions)
{
	KFCWarichuConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch warichu-tab (the model's cases, which need no dialog): on = the Warichu kind, its lines its conditions.
	if (KFC_DIAG_FAULT("warichu-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("warichu-tab", lines);
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
	// THE TAB ITSELF IS THE UI HALF'S (KFCKentenSearch::IsTabCurrent's rule): no UI half is no Warichu kind.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->WarichuTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCWarichuSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCWarichuConditions& asked,
	const std::vector<KFCSearchEngine::ListedPlace>* part)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kWarichuDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;		// the object lists' number, as a kenten list's
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
		const bool walked = WalkDoc(docRef, asked, part, limit - static_cast<size_t>(out.total), hits, versions, capped);
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

bool KFCWarichuSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCWarichuConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kWarichuDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, asked, nil, limit, outHits, outVersions, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kWarichuDocSpan);
	return walked && !outCancelled;
}

bool KFCWarichuSearch::WalkForFace(const UIDRef& docRef, const KFCWarichuConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	std::map<UID, uint32> versions;
	bool capped = false;
	return WalkDoc(docRef, asked, part, static_cast<size_t>(kMaxInt32), outHits, versions, capped);
}

// End, KFCWarichuSearch.cpp.
