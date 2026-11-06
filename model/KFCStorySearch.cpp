//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Story kind, listed - see KFCStorySearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IFrameList.h"
#include "IFrameListComposer.h"		// RecomposeThruLastFrame - a story composed through its last frame before it is asked whether it is overset
#include "IMultiColumnTextFrame.h"
#include "IStoryList.h"
#include "ITableModel.h"
#include "ITableModelList.h"
#include "ITableTextContainer.h"
#include "ITextFrameColumn.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextUtils.h"				// IsOverset - InDesign's own "+": a final return alone overset is not overset
#include "ITOPFrameData.h"			// text on a path: its column says so
#include "IWalkerScopeFactoryUtils.h"
#include "IXMLUtils.h"				// GetBackingStore - the XML structure's internal story

// General includes:
#include "ConditionalTextID.h"		// kHiddenTextBoss
#include "InCopySharedID.h"			// kDeletedTextBoss, kNoteDataBoss
#include "TextID.h"					// kEndnoteStoryBoss, kFootnoteReferenceBoss
#include "TextIterator.h"
#include "Utils.h"

#include <set>
#include <string>

// Project includes:
#include "KFCStorySearch.h"
#include "IKFCUIServices.h"			// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCProgressBar.h"			// KFCSetCountedTask too - the bar's line

namespace
{

// A document's share of the bar (KFCWarichuSearch's number).
const int32 kStoryDocSpan = 100;

// A story's text the row stands for: its primary thread up to its last return - [0, end) (an empty story's [0, 0)).
TextIndex PrimaryEnd(ITextModel* model)
{
	const int32 span = model->GetPrimaryStoryThreadSpan();
	return (span > 1) ? static_cast<TextIndex>(span - 1) : 0;
}

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note (KFCTableSearch.cpp's rule): a table there is no table the Table kind lists.
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// The XML structure's backing store of the document (IXMLUtils::GetBackingStore - every document holds one from its making: measured 2026-11-05), kInvalidUID for none.
UID BackingStoreOf(IDataBase* db)
{
	Utils<IXMLUtils> xml;
	return xml ? xml->GetBackingStore(db).GetUID() : kInvalidUID;
}

// "1 frame" / "2 frames" - a count and its noun, as a person reads them.
void AppendCount(PMString& io, int32 n, const char* one, const char* many)
{
	io.AppendNumber(n);
	io.Append(" ");
	io.Append(n == 1 ? one : many);
}

// THE CHARACTERS A ROW COUNTS (ST5): the text a person reads in [0, end) - not InDesign's marks in it: a paragraph's or a line's end and every other control character but a tab (a
// footnote's reference U+0004, a table's U+0016 / U+0017 ...), U+FEFF (the XML structure's marks, the zero-width anchors), U+FFFC (an anchored object), U+FFFD (a special glyph) and
// the private-use characters InDesign gives its own (KFCWarichuSearch's StopsRow reads the same as no text).
bool IsReadText(uint32 c)
{
	if (c < 0x20)
		return c == 0x09;
	return c != 0xFEFF && c != 0xFFFC && c != 0xFFFD && !(c >= 0xE000 && c <= 0xF8FF);
}

int32 CountCharacters(ITextModel* model, TextIndex end)
{
	int32 count = 0;
	TextIterator it(model, 0);
	for (TextIndex at = 0; at < end && !it.IsNull(); ++at, ++it)
		if (IsReadText((*it).GetValue()))
			++count;
	return count;
}

// The story's tables the Table kind would list (none in set-aside text) and its footnotes (each a thread whose dictionary is its reference).
void CountTablesAndFootnotes(const UIDRef& storyRef, ITextModel* model, int32& outTables, int32& outFootnotes)
{
	outTables = 0;
	outFootnotes = 0;
	IDataBase* const db = storyRef.GetDataBase();
	InterfacePtr<ITableModelList> tables(storyRef, UseDefaultIID());
	if (tables != nil)
		for (int32 t = 0; t < tables->GetModelCount(); ++t)
		{
			InterfacePtr<ITableModel> table(tables->QueryNthModel(t));
			InterfacePtr<ITableTextContainer> text(table, UseDefaultIID());
			if (text != nil && !StandsInSetAsideText(model, text->GetAnchorTextIndex()))
				++outTables;
		}
	const TextIndex total = model->TotalLength();
	for (TextIndex at = model->GetPrimaryStoryThreadSpan(); at < total;)
	{
		TextIndex start = 0;
		int32 span = 0;
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &start, &span));
		if (thread == nil || span <= 0)
			break;
		const UID dict = thread->GetDictUID();
		if (dict != kInvalidUID && db->IsValidUID(dict) && db->GetClass(dict) == kFootnoteReferenceBoss)
			++outFootnotes;
		at = start + span;
	}
}

// WHAT A STORY'S ROW DRAWS (ST5): its facts, as a table row draws its size and style (no brackets: the row draws them in its match's place). Its frames (each text frame once - a frame's
// columns are one frame), composed through the last of them first when they are damaged (InDesign's own background composition is an idle task, and a windowless chapter has none:
// KFCSearchEngine's ComposeThrough note) - inside the walk's dirty guard.
PMString StoryWords(const UIDRef& storyRef, ITextModel* model, bool internal, bool backing, TextIndex end)
{
	IDataBase* const db = storyRef.GetDataBase();
	const int32 characters = CountCharacters(model, end);
	PMString words;
	words.SetTranslatable(kFalse);
	if (internal)
	{
		words.Append(backing ? "internal: XML structure, " : "internal, ");
		if (characters == 0)
			words.Append("empty");
		else
			AppendCount(words, characters, "character", "characters");
		return words;
	}
	InterfacePtr<IFrameList> frames(model->QueryFrameList());
	int32 frameCount = 0;
	bool onPath = false;
	bool overset = false;
	if (frames != nil)
	{
		if (frames->GetFirstDamagedFrameIndex() >= 0)
		{
			InterfacePtr<IFrameListComposer> composer(frames, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}
		std::set<UID> containers;
		for (int32 f = 0; f < frames->GetFrameCount(); ++f)
		{
			InterfacePtr<ITextFrameColumn> column(frames->QueryNthFrame(f));
			if (column == nil)
				continue;
			if (f == 0)
			{
				InterfacePtr<ITOPFrameData> top(column, UseDefaultIID());
				onPath = (top != nil);
			}
			InterfacePtr<IMultiColumnTextFrame> frame(column->QueryMultiColumnTextFrame());
			containers.insert(frame != nil ? ::GetUID(frame) : ::GetUID(column));
		}
		frameCount = static_cast<int32>(containers.size());
		Utils<ITextUtils> textUtils;
		overset = textUtils && textUtils->IsOverset(frames);
	}
	if (onPath)
		words.Append("text on path, ");
	else
	{
		if (db->GetClass(storyRef.GetUID()) == kEndnoteStoryBoss)
			words.Append("endnotes, ");
		if (frameCount == 0)
			words.Append("no frame");
		else
			AppendCount(words, frameCount, "frame", "frames");
		words.Append(", ");
	}
	if (characters == 0)
		words.Append("empty");
	else
		AppendCount(words, characters, "character", "characters");
	int32 tables = 0, footnotes = 0;
	CountTablesAndFootnotes(storyRef, model, tables, footnotes);
	if (tables > 0)
	{
		words.Append(", ");
		AppendCount(words, tables, "table", "tables");
	}
	if (footnotes > 0)
	{
		words.Append(", ");
		AppendCount(words, footnotes, "footnote", "footnotes");
	}
	if (overset)
		words.Append(", overset");
	return words;
}

// ONE DOCUMENT'S ROWS - every story of its list, the user's and then the internal ones (ST4). False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, size_t limit, std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped)
{
	outCapped = false;
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return false;
	// Composing to read a row's page and its overset dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	const UID backing = BackingStoreOf(db);
	std::vector<KFCSearchEngine::ListedPlace> places;
	std::vector<PMString> words;
	std::vector<int32> holders;
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;
		if (places.size() >= limit)
		{
			outCapped = true;
			break;
		}
		const bool internal = (stories->GetNthTextModelAccess(s) == kNotUserAccessible);
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = 0;
		place.end = PrimaryEnd(model);
		places.push_back(place);
		words.push_back(StoryWords(storyRef, model, internal, storyRef.GetUID() == backing, place.end));
		holders.push_back(internal ? static_cast<int32>(kKFCHolderInternalStory) : static_cast<int32>(kKFCHolderStory));
	}
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	for (size_t h = 0; h < outHits.size() && h < words.size(); ++h)
	{
		outHits[h].runChain = words[h];
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].holderKind = holders[h];
		// THE ROW READS ITS FACTS ALONE: no line around them (a story row's text is the story whole). Its "ID<n>: first words" go before them as it is
		// drawn (KFCResultModel::GetHitRow - 11-06: the row hangs off its document row, no story row above it to say them).
		outHits[h].preText = PMString();
		outHits[h].preText.SetTranslatable(kFalse);
		outHits[h].postText = PMString();
		outHits[h].postText.SetTranslatable(kFalse);
		// AN INTERNAL STORY IS NO OVERSET TEXT (ST5): it has no frame to be set in - its row says "internal" (KFCResultModel::BuildHitLocator), sorts to the end (no page) and
		// is landed on as nothing to select (ST6).
		if (holders[h] == kKFCHolderInternalStory)
			outHits[h].isOverset = false;
	}
	return true;
}

}	// anonymous namespace

bool KFCStorySearch::IsTabCurrent(KFCStoryConditions* outConditions)
{
	KFCStoryConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch story-tab (the model's cases, which need no dialog): on = the Story kind; its line scope=all the conditions.
	if (KFC_DIAG_FAULT("story-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("story-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
			if (lines[n] == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCWarichuSearch::IsTabCurrent's rule): no UI half is no Story kind.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->StoryTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCStorySearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCStoryConditions& /*asked*/)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kStoryDocSpan;
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
		if (fromBook && targets[i].docRef == UIDRef::gNull && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			continue;
		const UIDRef docRef = targets[i].docRef;
		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> versions;
		bool capped = false;
		const bool walked = WalkDoc(docRef, limit - static_cast<size_t>(out.total), hits, versions, capped);
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

bool KFCStorySearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCStoryConditions& /*asked*/,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kStoryDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, limit, outHits, outVersions, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kStoryDocSpan);
	return walked && !outCancelled;
}

bool KFCStorySearch::WalkForFace(const UIDRef& docRef, const KFCStoryConditions& /*asked*/, std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	std::map<UID, uint32> versions;
	bool capped = false;
	return WalkDoc(docRef, static_cast<size_t>(kMaxInt32), outHits, versions, capped);
}

bool KFCStorySearch::StoryRange(IDataBase* db, UID story, TextIndex& outEnd)
{
	outEnd = kInvalidTextIndex;
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return false;
	InterfacePtr<IStoryList> stories(db, db->GetRootUID(), UseDefaultIID());
	if (stories == nil || stories->GetTextModelIndex(story) < 0)
		return false;		// (deleted, or no longer the document's - its UID may name something else now)
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	if (model == nil)
		return false;
	outEnd = PrimaryEnd(model);
	return true;
}

// End, KFCStorySearch.cpp.
