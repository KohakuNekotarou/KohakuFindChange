//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Table tab, listed - see KFCTableSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocument.h"
#include "IStoryList.h"
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"
#include "ITableModel.h"
#include "ITableModelList.h"
#include "ITableStylesFacade.h"
#include "ITableTextContainer.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "IWalkerScopeFactoryUtils.h"	// kAllDocumentScope - the tab's Search:
#include "IWorkspace.h"

// General includes:
#include "ConditionalTextID.h"			// kHiddenTextBoss
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "TableTypes.h"
#include "UIDList.h"
#include "Utils.h"

#include <string>

// Project includes:
#include "KFCTableSearch.h"
#include "IKFCUIServices.h"			// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCProgressBar.h"			// KFCSetCountedTask too - the bar's line

namespace
{
// A document's share of the bar (KFCQRSearch's number: a walk with nothing to say about its inside).
const int32 kTableDocSpan = 100;

// A style's full path as the conditions name it (not translated: "[Basic Table]" whatever the UI's language), or as a row draws it.
PMString StylePath(IDataBase* db, UID style, bool translated)
{
	PMString path;
	path.SetTranslatable(kFalse);
	InterfacePtr<IStyleGroupHierarchy> hierarchy(db, style, UseDefaultIID());
	if (hierarchy != nil)
		path = hierarchy->GetFullPath(translated ? kTrue : kFalse);
	path.SetTranslatable(kFalse);
	return path;
}

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note: each a story thread of its own hanging off the body
// (KCMTextRead.cpp's rule). Not a table of the document as InDesign's Find has it ("Hidden text is not searchable" - IConditionalTextFacade.h)
// or as the DOM lists a story's tables (measured: a table in hidden text was listed "overset" here, at a character nothing shows).
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// The tables of one story, by the conditions, into places and what their rows draw.
void WalkStory(const UIDRef& storyRef, const PMString& findStyle, bool overridesOnly, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, std::vector<PMString>& ioWords, bool& outCapped)
{
	InterfacePtr<ITableModelList> tables(storyRef, UseDefaultIID());
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	Utils<Facade::ITableStylesFacade> facade;
	if (tables == nil || model == nil || !facade)
		return;
	IDataBase* const db = storyRef.GetDataBase();
	const int32 count = tables->GetModelCount();
	for (int32 t = 0; t < count; ++t)
	{
		InterfacePtr<ITableModel> table(tables->QueryNthModel(t));
		InterfacePtr<ITableTextContainer> text(table, UseDefaultIID());
		if (table == nil || text == nil || StandsInSetAsideText(model, text->GetAnchorTextIndex()))
			continue;
		const UIDRef tableRef(::GetUIDRef(table));
		UID style = kInvalidUID;
		if (facade->GetTableStyle(tableRef, style) != kSuccess)
			continue;
		if (!findStyle.IsEmpty() && StylePath(db, style, false) != findStyle)
			continue;
		if (overridesOnly && facade->HasLocalOverrides(tableRef) == kFalse)
			continue;
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return;
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = text->GetAnchorTextIndex();
		place.end = place.start + text->GetAnchorSpan();
		ioPlaces.push_back(place);
		ioWords.push_back(KFCTableSearch::TableWords(table));
	}
}

// ONE DOCUMENT'S TABLES AS ROWS. False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const PMString& findStyle, bool overridesOnly, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
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
	std::vector<PMString> words;
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count && !outCapped; ++s)
		WalkStory(stories->GetNthTextModelUID(s), findStyle, overridesOnly, limit, places, words, outCapped);
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	for (size_t h = 0; h < outHits.size(); ++h)
	{
		outHits[h].runChain = words[h];
		outHits[h].runChain.SetTranslatable(kFalse);
	}
	return true;
}
}	// anonymous namespace

bool KFCTableSearch::IsTabCurrent(KFCTableConditions* outConditions)
{
	KFCTableConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch table-tab (the model's cases, which need no dialog): on = the Table tab, its lines its conditions.
	if (KFC_DIAG_FAULT("table-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("table-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
		{
			const std::string& line = lines[n];
			PMString value;
			value.SetTranslatable(kFalse);
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				value.SetUTF8String(line.substr(eq + 1));
			// ("G:TS3" in the file: the path's own separator is no character a script writes.)
			if (line.compare(0, 5, "find=") == 0)
				conditions.findStyle = KFCStylePathSwapped(value, kTextChar_Colon, kTextChar_StyleGroupPathSeparator);
			else if (line.compare(0, 7, "change=") == 0)
				conditions.changeStyle = KFCStylePathSwapped(value, kTextChar_Colon, kTextChar_StyleGroupPathSeparator);
			else if (line == "overrides=1")
				conditions.overridesOnly = true;
			else if (line == "clear=1")
				conditions.clearOverrides = true;
			else if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
		}
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCQRSearch::IsTabCurrent's rule): no UI half is no Table tab.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->TableTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCTableSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const PMString& findStyle,
	bool overridesOnly)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kTableDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;		// the object lists' number (the spec's T7)
	size_t limit = static_cast<size_t>(room > 0 ? room : 0);
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch table-cap: the limit is 2 (a run of a thousand tables' sake - the Stopped at sentence).
	if (KFC_DIAG_FAULT("table-cap"))
		limit = 2;
#endif
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
		const bool walked = WalkDoc(docRef, findStyle, overridesOnly, limit - static_cast<size_t>(out.total), hits, versions, capped);
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

bool KFCTableSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const PMString& findStyle, bool overridesOnly,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kTableDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, findStyle, overridesOnly, limit, outHits, outVersions, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kTableDocSpan);
	return walked && !outCancelled;
}

ITableModel* KFCTableSearch::QueryTableAt(const UIDRef& story, TextIndex at)
{
	InterfacePtr<ITableModelList> tables(story, UseDefaultIID());
	if (tables == nil)
		return nil;
	const int32 count = tables->GetModelCount();
	for (int32 t = 0; t < count; ++t)
	{
		InterfacePtr<ITableModel> table(tables->QueryNthModel(t));
		InterfacePtr<ITableTextContainer> text(table, UseDefaultIID());
		if (text != nil && text->GetAnchorTextIndex() == at)
			return table.forget();
	}
	return nil;
}

PMString KFCTableSearch::TableWords(ITableModel* table)
{
	PMString words;
	words.SetTranslatable(kFalse);
	Utils<Facade::ITableStylesFacade> facade;
	if (table == nil || !facade)
		return words;
	const UIDRef tableRef(::GetUIDRef(table));
	words.AppendNumber(table->GetTotalRows().count);
	words.Append("x");
	words.AppendNumber(table->GetTotalCols().count);
	UID style = kInvalidUID;
	if (facade->GetTableStyle(tableRef, style) == kSuccess)
	{
		words.Append(" ");
		words.Append(KFCStylePathWords(StylePath(tableRef.GetDataBase(), style, true)));
	}
	if (facade->HasLocalOverrides(tableRef) != kFalse)
		words.Append(" +");
	return words;
}

UID KFCTableSearch::StyleNamed(IDataBase* db, const PMString& path)
{
	if (db == nil)
		return kInvalidUID;
	InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
	InterfacePtr<IWorkspace> workspace(db, (doc != nil) ? doc->GetDocWorkSpace().GetUID() : kInvalidUID, UseDefaultIID());
	InterfacePtr<IStyleGroupManager> styles(workspace, IID_ITABLESTYLEGROUPMANAGER);
	return (styles != nil) ? styles->FindByName(path) : kInvalidUID;
}

bool KFCTableSearch::WouldChange(ITableModel* table, UID style, bool clearOverrides)
{
	Utils<Facade::ITableStylesFacade> facade;
	if (table == nil || !facade)
		return false;
	const UIDRef tableRef(::GetUIDRef(table));
	UID now = kInvalidUID;
	if (style != kInvalidUID && (facade->GetTableStyle(tableRef, now) != kSuccess || now != style))
		return true;
	return clearOverrides && facade->HasLocalOverrides(tableRef) != kFalse;
}

ErrorCode KFCTableSearch::Write(ITableModel* table, UID style, bool clearOverrides)
{
	Utils<Facade::ITableStylesFacade> facade;
	if (table == nil || !facade)
		return kFailure;
	const UIDRef tableRef(::GetUIDRef(table));
	if (style != kInvalidUID)
		return facade->ApplyTableStyle(UIDRef(tableRef.GetDataBase(), style), tableRef, clearOverrides ? kTrue : kFalse, kFalse);
	return clearOverrides ? facade->ClearLocalOverrides(tableRef) : kSuccess;
}

// End, KFCTableSearch.cpp.
