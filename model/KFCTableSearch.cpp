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
#include "KFCReplaceEngine.h"		// WriteFaceTables - the part's Change and Change All

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

// IN THE SEARCHED PART (Search: Story, To End of Story, Selection): its character in a stretch - the table in the text - or a stretch in one of its cells: a text cursor or a selection inside it, the
// table it stands in (each cell a story thread whose dictionary is the table).
bool TableInPart(const std::vector<KFCSearchEngine::ListedPlace>& part, const UIDRef& storyRef, ITextModel* model, ITableModel* table, TextIndex anchor)
{
	if (KFCSearchEngine::StretchesTouch(part, storyRef.GetUID(), anchor, anchor + 1))
		return true;
	const UID tableUID = ::GetUID(table);
	for (size_t p = 0; p < part.size(); ++p)
	{
		if (part[p].story != storyRef.GetUID())
			continue;
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(part[p].start, nil, nil));
		if (thread != nil && thread->GetDictUID() == tableUID)
			return true;
	}
	return false;
}

// The tables of one story, by the conditions, into places and what their rows draw.
void WalkStory(const UIDRef& storyRef, const PMString& findStyle, bool overridesOnly, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, std::vector<PMString>& ioWords, std::vector<UID>& ioTables, bool& outCapped)
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
		if (part != nil && !TableInPart(*part, storyRef, model, table, text->GetAnchorTextIndex()))
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
		ioTables.push_back(tableRef.GetUID());
	}
}

// ONE DOCUMENT'S TABLES AS ROWS - of its searched part alone, given one. False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const PMString& findStyle, bool overridesOnly, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
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
	std::vector<UID> tables;
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count && !outCapped; ++s)
		if (part == nil || KFCSearchEngine::StretchesTouch(*part, stories->GetNthTextModelUID(s).GetUID(), 0, kMaxInt32))
			WalkStory(stories->GetNthTextModelUID(s), findStyle, overridesOnly, part, limit, places, words, tables, outCapped);
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	for (size_t h = 0; h < outHits.size(); ++h)
	{
		outHits[h].runChain = words[h];
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].tableUID = tables[h];
	}
	return true;
}

// THE RANGE THE PART'S BUTTONS GO OVER (KFCTableSearch.h - Next): read when the selection has moved or Search: changed, kept while the selection is as the last landing left it. db is compared, never followed.
struct ButtonRange
{
	int32									scope;		// the Search: it was read for; -1 = not read
	const IDataBase*						db;			// the front document it was read in
	bool									fellBack;	// a Search: the selection did not offer: over the document, and said
	std::vector<KFCSearchEngine::ListedPlace>	part;

	ButtonRange() : scope(-1), db(nil), fellBack(false) {}
};
ButtonRange gButtonRange;

bool ScopeIsPartOfDocument(int32 scope)
{
	return scope == IWalkerScopeFactoryUtils::kStoryScope || scope == IWalkerScopeFactoryUtils::kToEndOfStoryScope || scope == IWalkerScopeFactoryUtils::kSelectionScope;
}

// The Text tab's sentence for a Search: the selection does not offer (KFCSearchEngine::ResolveRunScope's words).
void AppendFellBack(PMString& io, int32 scope)
{
	io.Append("Search: is ");
	io.Append(scope == IWalkerScopeFactoryUtils::kStoryScope ? "Story" : scope == IWalkerScopeFactoryUtils::kToEndOfStoryScope ? "To End of Story" : "Selection");
	io.Append(scope == IWalkerScopeFactoryUtils::kSelectionScope ? ", but no text is selected"
		: scope == IWalkerScopeFactoryUtils::kToEndOfStoryScope ? ", but there is no text cursor"
		: ", but no text or text frame is selected");
	io.Append(" - the whole document was searched, as Edit > Find/Change does.");
}

struct ButtonRow
{
	size_t					doc;
	KFCResultModel::Hit		hit;
};

// EVERY TABLE OF THE BUTTONS' RANGE IN THE LIST'S ORDER (KFCQRSearch::Next's walk): document by document, each in page order. False = it did not look: outRefusal says why.
bool WalkButtonRange(const KFCTableLook& look, const KFCTableConditions& asked, std::vector<KFCBookScope::ChapterDoc>& outDocs, std::vector<ButtonRow>& outRows, PMString& outRefusal)
{
	if (asked.scope == IWalkerScopeFactoryUtils::kAllDocumentScope)
		KFCBookScope::ListOpenDocuments(outDocs);
	else
	{
		IDocument* const doc = KFCBookScope::ActiveDocument();		// non-owning
		if (doc != nil)
			outDocs.push_back(KFCBookScope::DocAsChapter(doc));
	}
	if (outDocs.empty())
	{
		outRefusal.Append("No document is open.");
		return false;
	}
	IDataBase* const front = outDocs[0].docRef.GetDataBase();
	if (!look.onLast || gButtonRange.scope != asked.scope || (ScopeIsPartOfDocument(asked.scope) && gButtonRange.db != front))
	{
		gButtonRange = ButtonRange();
		gButtonRange.db = front;
		if (ScopeIsPartOfDocument(asked.scope))
		{
			if (KFCSearchEngine::SearchScopeForSelection(asked.scope) != asked.scope)
				gButtonRange.fellBack = true;
			else if (!KFCSearchEngine::PartStretches(asked.scope, front, gButtonRange.part))
			{
				outRefusal.Append("What Search: holds in the selection could not be read - nothing was searched.");
				return false;
			}
		}
		gButtonRange.scope = asked.scope;
	}
	const std::vector<KFCSearchEngine::ListedPlace>* const part = (ScopeIsPartOfDocument(asked.scope) && !gButtonRange.fellBack) ? &gButtonRange.part : nil;
	for (size_t d = 0; d < outDocs.size(); ++d)
	{
		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> versions;
		bool capped = false;
		if (!WalkDoc(outDocs[d].docRef, asked.findStyle, asked.overridesOnly, part, static_cast<size_t>(KFCResultModel::kKFCCollectObjectLimit), hits, versions, capped))
			continue;
		KFCResultModel::OrderHitsByPage(hits);
		for (size_t h = 0; h < hits.size(); ++h)
		{
			ButtonRow one;
			one.doc = d;
			one.hit = hits[h];
			outRows.push_back(one);
		}
	}
	return true;
}

// The row after the table found last - the first when that one is not among them.
size_t RowAfterLast(const KFCTableLook& look, const std::vector<KFCBookScope::ChapterDoc>& docs, const std::vector<ButtonRow>& rows)
{
	if (look.lastDoc == UIDRef::gNull)
		return 0;
	for (size_t k = 0; k < rows.size(); ++k)
		if (docs[rows[k].doc].docRef == look.lastDoc && rows[k].hit.storyUID == look.lastStory.GetUID() && rows[k].hit.textStart == look.lastAt)
			return (k + 1) % rows.size();
	return 0;
}

// A selection's place among a story's tables: its end - in a table's cell (each cell a story thread whose dictionary is the table, read after the body's text), just past that table's character.
TextIndex SelectionPlace(const UIDRef& story, TextIndex selEnd)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil || selEnd <= 0)
		return selEnd;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(selEnd - 1, nil, nil));
	InterfacePtr<ITableTextContainer> inTable(story.GetDataBase(), (thread != nil) ? thread->GetDictUID() : kInvalidUID, UseDefaultIID());
	return (inTable != nil) ? inTable->GetAnchorTextIndex() + 1 : selEnd;
}

// FIND NEXT'S ROW (KFCTableSearch.h).
size_t PickNextRow(const KFCTableLook& look, const std::vector<KFCBookScope::ChapterDoc>& docs, const std::vector<ButtonRow>& rows)
{
	if (look.onLast || look.selStory == UIDRef::gNull)
		return RowAfterLast(look, docs, rows);
	const TextIndex place = SelectionPlace(look.selStory, look.selEnd);
	bool inStory = false;
	size_t lastOfStory = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (docs[rows[k].doc].docRef.GetDataBase() != look.selStory.GetDataBase() || rows[k].hit.storyUID != look.selStory.GetUID())
			continue;
		if (rows[k].hit.textStart >= place)
			return k;
		inStory = true;
		lastOfStory = k;
	}
	return inStory ? (lastOfStory + 1) % rows.size() : RowAfterLast(look, docs, rows);
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
			else if (line == "scope=story")
				conditions.scope = IWalkerScopeFactoryUtils::kStoryScope;
			else if (line == "scope=toend")
				conditions.scope = IWalkerScopeFactoryUtils::kToEndOfStoryScope;
			else if (line == "scope=selection")
				conditions.scope = IWalkerScopeFactoryUtils::kSelectionScope;
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
	bool overridesOnly, const std::vector<KFCSearchEngine::ListedPlace>* part)
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
		const bool walked = WalkDoc(docRef, findStyle, overridesOnly, part, limit - static_cast<size_t>(out.total), hits, versions, capped);
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
	const bool walked = WalkDoc(docRef, findStyle, overridesOnly, nil, limit, outHits, outVersions, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kTableDocSpan);
	return walked && !outCancelled;
}

bool KFCTableSearch::WriteDocument(const UIDRef& docRef, const KFCTableConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount)
{
	outCount = 0;
	std::vector<KFCResultModel::Hit> hits;
	std::map<UID, uint32> versions;
	bool capped = false;
	if (!WalkDoc(docRef, asked.findStyle, asked.overridesOnly, part, static_cast<size_t>(kMaxInt32), hits, versions, capped))
		return false;
	IDataBase* const db = docRef.GetDataBase();
	const UID style = asked.changeStyle.IsEmpty() ? kInvalidUID : StyleNamed(db, asked.changeStyle);
	if (!asked.changeStyle.IsEmpty() && style == kInvalidUID)
		return true;		// this document has no style of that name: nothing of it to write (a row's Replace says so; a run writes on)
	for (size_t h = 0; h < hits.size(); ++h)
	{
		if (hits[h].isLocked)
			continue;
		InterfacePtr<ITableModel> table(QueryTableAt(UIDRef(db, hits[h].storyUID), hits[h].textStart));
		if (table == nil || !WouldChange(table, style, asked.clearOverrides))
			continue;
		if (Write(table, style, asked.clearOverrides) != kSuccess)
			return false;
		++outCount;
	}
	return true;
}

bool KFCTableSearch::Next(const KFCTableLook& look, KFCTableFound& out)
{
	out = KFCTableFound();
	out.words.SetTranslatable(kFalse);
	out.note.SetTranslatable(kFalse);
	KFCTableConditions asked;
	(void)IsTabCurrent(&asked);
	std::vector<KFCBookScope::ChapterDoc> docs;
	std::vector<ButtonRow> rows;
	if (!WalkButtonRange(look, asked, docs, rows, out.words) || rows.empty())
		return false;
	const ButtonRow& one = rows[PickNextRow(look, docs, rows)];
	const UIDRef docRef(docs[one.doc].docRef);
	out.doc = docRef;
	out.story = UIDRef(docRef.GetDataBase(), one.hit.storyUID);
	out.at = one.hit.textStart;
	out.end = one.hit.textEnd;
	out.table = one.hit.tableUID;
	if (one.hit.isLocked)
		out.reach |= kKFCQRReachLocked;
	if (one.hit.isHidden)
		out.reach |= kKFCQRReachHidden;
	if (one.hit.onHiddenSpread)
		out.reach |= kKFCQRReachHiddenSpread;
	if (one.hit.isOverset)
		out.reach |= kKFCQRReachOverset;
	// THE LINE: what its row draws, then where it stands, and its document over All Documents (KFCQRSearch::Next's).
	out.words.Append(one.hit.runChain);
	if (one.hit.isOverset)
		out.words.Append(" - overset text");
	else if (one.hit.frameOnPasteboard)
		out.words.Append(" - pasteboard");
	else
	{
		out.words.Append(" - page ");
		out.words.Append(one.hit.pageString);
	}
	if (asked.scope == IWalkerScopeFactoryUtils::kAllDocumentScope)
	{
		out.words.Append(" - ");
		out.words.Append(docs[one.doc].shortName);
	}
	if (gButtonRange.fellBack)
		AppendFellBack(out.note, asked.scope);
	KFC_DIAG_LOG("TBLBTN next story=%d at=%d reach=%d rows=%d words=%s", static_cast<int>(out.story.GetUID().Get()), static_cast<int>(out.at), static_cast<int>(out.reach),
		static_cast<int>(rows.size()), out.words.GetUTF8String().c_str());
	return true;
}

bool KFCTableSearch::ChangeFound(const KFCTableFound& found, PMString& outWords)
{
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	KFCTableConditions asked;
	(void)IsTabCurrent(&asked);
	// THE DOORS - nothing written yet. The document first: everything after it follows its database.
	if (asked.changeStyle.IsEmpty() && !asked.clearOverrides)
	{
		outWords.Append("Nothing to change - choose Change to Table Style or Clear Overrides.");
		return false;
	}
	if (found.doc == UIDRef::gNull || !KFCBookScope::IsDocStillOpen(found.doc))
	{
		outWords.Append("That table's document has been closed - press Find Next.");
		return false;
	}
	InterfacePtr<ITableModel> table(QueryTableAt(found.story, found.at));
	if (table == nil || ::GetUID(table) != found.table)
	{
		outWords.Append("That table is no longer there - press Find Next.");
		return false;
	}
	if (!KFCSearchEngine::IsFrameEditable(found.story, KFCSearchEngine::EditableFrameForMatch(found.story, found.at)))
	{
		outWords.Append("That table is locked - not changed.");
		return false;
	}
	const UID style = asked.changeStyle.IsEmpty() ? kInvalidUID : StyleNamed(found.doc.GetDataBase(), asked.changeStyle);
	if (!asked.changeStyle.IsEmpty() && style == kInvalidUID)
	{
		outWords.Append("This document has no table style of that name - not changed.");
		return false;
	}
	if (!WouldChange(table, style, asked.clearOverrides))
	{
		outWords.Append("That table already reads as the Change side asks.");
		return false;
	}
	KFCReplaceEngine::FaceTable one;
	one.doc = found.doc;
	one.story = found.story.GetUID();
	one.table = found.table;
	one.style = style;
	if (!KFCReplaceEngine::WriteFaceTables(std::vector<KFCReplaceEngine::FaceTable>(1, one), asked.clearOverrides, outWords))
		return false;
	outWords.Append("Changed: ");
	outWords.Append(TableWords(table));
	KFC_DIAG_LOG("TBLBTN change wrote words=%s", outWords.GetUTF8String().c_str());
	return true;
}

bool KFCTableSearch::ChangeAll(const KFCTableLook& look, int32& outChanged, int32& outMatched, PMString& outWords)
{
	outChanged = 0;
	outMatched = 0;
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	KFCTableConditions asked;
	(void)IsTabCurrent(&asked);
	if (asked.changeStyle.IsEmpty() && !asked.clearOverrides)
	{
		outWords.Append("Nothing to change - choose Change to Table Style or Clear Overrides.");
		return false;
	}
	std::vector<KFCBookScope::ChapterDoc> docs;
	std::vector<ButtonRow> rows;
	if (!WalkButtonRange(look, asked, docs, rows, outWords))
		return false;
	outMatched = static_cast<int32>(rows.size());
	std::vector<KFCReplaceEngine::FaceTable> targets;
	int32 locked = 0, noStyle = 0, asAsked = 0;
	std::map<size_t, UID> styleOfDoc;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		const KFCResultModel::Hit& hit = rows[k].hit;
		const UIDRef docRef(docs[rows[k].doc].docRef);
		if (hit.isLocked)
		{
			++locked;
			continue;
		}
		if (styleOfDoc.count(rows[k].doc) == 0)
			styleOfDoc[rows[k].doc] = asked.changeStyle.IsEmpty() ? kInvalidUID : StyleNamed(docRef.GetDataBase(), asked.changeStyle);
		const UID style = styleOfDoc[rows[k].doc];
		if (!asked.changeStyle.IsEmpty() && style == kInvalidUID)
		{
			++noStyle;
			continue;
		}
		InterfacePtr<ITableModel> table(docRef.GetDataBase(), hit.tableUID, UseDefaultIID());
		if (table == nil || !WouldChange(table, style, asked.clearOverrides))
		{
			++asAsked;
			continue;
		}
		KFCReplaceEngine::FaceTable one;
		one.doc = docRef;
		one.story = hit.storyUID;
		one.table = hit.tableUID;
		one.style = style;
		targets.push_back(one);
	}
	if (!targets.empty() && !KFCReplaceEngine::WriteFaceTables(targets, asked.clearOverrides, outWords))
		return false;
	outChanged = static_cast<int32>(targets.size());
	outWords.AppendNumber(outChanged);
	outWords.Append(" of ");
	outWords.AppendNumber(outMatched);
	outWords.Append(" table(s) changed.");
	if (locked + noStyle + asAsked > 0)
	{
		outWords.Append(" Left as they were:");
		const char* sep = " ";
		if (locked > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(locked);
			outWords.Append(" locked");
			sep = ", ";
		}
		if (noStyle > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(noStyle);
			outWords.Append(" no such table style");
			sep = ", ";
		}
		if (asAsked > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(asAsked);
			outWords.Append(" already as asked");
		}
		outWords.Append(".");
	}
	KFC_DIAG_LOG("TBLBTN changeall changed=%d matched=%d words=%s", static_cast<int>(outChanged), static_cast<int>(outMatched), outWords.GetUTF8String().c_str());
	return true;
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
