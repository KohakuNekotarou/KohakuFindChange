//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A table row list - see KFCTableRows.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "ITableModel.h"
#include "ITableTextContainer.h"
#include "ITableTextContent.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextStoryThreadDict.h"

// General includes:
#include "TableTypes.h"
#include "TablesID.h"				// kTableModelBoss - a list's table still a table
#include "TextChar.h"				// kTextChar_Ellipse
#include "Utils.h"

#include <algorithm>
#include <map>
#include <memory>					// std::shared_ptr - the list's query copy
#include <vector>

// Project includes:
#include "KFCTableRows.h"
#include "KFCBookScope.h"			// ChapterDoc; ReachChapterDoc - a book's closed chapter; ReleaseHeldDocs - handed back after
#include "KFCDiag.h"
#include "KFCQueryCopy.h"			// CaptureTableRows - what lets the list be kept as a tier and switched back to
#include "KFCResultModel.h"
#include "KFCRowFoci.h"				// AttachOpenChapters - the rows follow the text, as a search's do
#include "KFCRunGuard.h"
#include "KFCSearchEngine.h"		// ChapterDocNow, ReadText, BuildListedHits, FileChapter, ForgetSearchedFindFormat
#include "KFCTextCut.h"				// KFCSafeCut - no cut in front of a mark that belongs to the character before it
#include "KFCTierStore.h"			// ParkCurrent - the list of tables kept as a tier

namespace
{
// What a row's words hold (T2): a cell's text cut to this many characters, and no further cell once the row's words are this long.
const int32 kCellChars = 24;
const int32 kRowChars = 100;

// A TABLE OF THE LIST STILL A TABLE OF ITS DOCUMENT: its UID may name another object by now - the class asked, as a gone item's.
bool IsTableNow(IDataBase* db, UID table)
{
	return db != nil && table != kInvalidUID && db->IsValidUID(table) && db->GetClass(table) == kTableModelBoss;
}

// `s` cut to `chars` characters with an ellipsis (KFCRunList's way - KFCSafeCut keeps a character with its marks).
void Cut(PMString& s, int32 chars)
{
	if (s.CharCount() <= chars)
		return;
	int32 keep = KFCSafeCut(s, chars);
	if (keep <= 0)
		keep = chars;
	s.Remove(keep, kMaxInt32);
	s.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
}

// A CELL'S WORDS: its story thread's text, the character that ends the cell left out (the thread's span counts it - measured 11-05: 4 characters
// of text, a span of 5), cut to kCellChars.
PMString CellWords(ITableModel* table, const GridAddress& anchor, const UIDRef& storyRef)
{
	PMString words;
	words.SetTranslatable(kFalse);
	InterfacePtr<ITextStoryThreadDict> threads(table, UseDefaultIID());
	InterfacePtr<ITextStoryThread> thread((threads != nil) ? threads->QueryThread(table->GetGridID(anchor)) : nil);
	if (thread == nil)
		return words;
	int32 span = 0;
	const TextIndex start = thread->GetTextStart(&span);
	const int32 length = span - 1;
	if (length <= 0)
		return words;
	// (Read no further than the cut needs, and a character past it, so the cut says so.)
	words = KFCSearchEngine::ReadText(storyRef, start, std::min(length, kCellChars + 1));
	words.SetTranslatable(kFalse);
	Cut(words, kCellChars);
	words.SetTranslatable(kFalse);
	return words;
}

// ONE ROW'S STRETCH OF TEXT (T3): GetTextChunk over the row's every column - one stretch, measured; should it ever hand more than one, the
// row's text from the first of them to the last. False = the row holds no cell of its own (every column covered from above).
bool RowStretch(ITableModel* table, ITableTextContent* content, int32 row, TextIndex& outStart, TextIndex& outEnd)
{
	GridArea area(RowRange(row, 1), table->GetTotalCols());
	bool any = false;
	for (int32 guard = 0; guard < 64; ++guard)		// (the area is the cookie GetTextChunk moves on: zero = no stretch left)
	{
		TextIndex at = 0;
		const int32 n = content->GetTextChunk(area, at);
		if (n <= 0)
			break;
		if (!any || at < outStart)
			outStart = at;
		if (!any || at + n > outEnd)
			outEnd = at + n;
		any = true;
	}
	return any;
}

// One document's share of the list.
struct DocRows
{
	KFCBookScope::ChapterDoc					target;
	std::vector<UID>							tables;		// its tables, in the list's order
	std::vector<KFCSearchEngine::ListedPlace>	places;		// ...their rows' stretches
	std::vector<PMString>						words;		// ...what the rows draw
	std::vector<UID>							rowTables;	// ...and the table of each
	std::vector<KFCResultModel::Hit>			hits;
	std::map<UID, uint32>						versions;
};
}	// anonymous namespace

PMString KFCTableRows::RowWords(ITableModel* table, int32 row)
{
	PMString words;
	words.SetTranslatable(kFalse);
	InterfacePtr<ITableTextContainer> container(table, UseDefaultIID());
	InterfacePtr<ITextModel> model((container != nil) ? container->QueryTextModel() : nil);
	if (table == nil || model == nil)
		return words;
	const UIDRef storyRef(::GetUIDRef(model));
	words.Append("Row ");
	words.AppendNumber(row + 1);
	const RowRange header(table->GetHeaderRows());
	const RowRange footer(table->GetFooterRows());
	if (header.count > 0 && row >= header.start && row < header.start + header.count)
		words.Append(" (header)");
	else if (footer.count > 0 && row >= footer.start && row < footer.start + footer.count)
		words.Append(" (footer)");
	words.Append(":");
	const ColRange cols(table->GetTotalCols());
	bool first = true;
	for (int32 c = cols.start; c < cols.start + cols.count; ++c)
	{
		// THE ROW'S OWN CELLS (T3): a cell whose top-left is here - not a merged cell's other columns, not one from the row above.
		const GridAddress anchor(table->FindCellAnchor(GridAddress(row, c)));
		if (anchor.row != row || anchor.col != c)
			continue;
		if (words.CharCount() >= kRowChars)
		{
			words.Append(" |");
			words.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
			break;
		}
		words.Append(first ? " " : " | ");
		words.Append(CellWords(table, anchor, storyRef));
		first = false;
	}
	words.SetTranslatable(kFalse);
	return words;
}

bool KFCTableRows::TableRowAt(const UIDRef& story, TextIndex at, UIDRef& outTable, int32& outRow)
{
	outRow = -1;
	GridAddress cell;
	if (!TableCellAt(story, at, outTable, cell))
		return false;
	outRow = cell.row;
	return true;
}

bool KFCTableRows::TableCellAt(const UIDRef& story, TextIndex at, UIDRef& outTable, GridAddress& outCell)
{
	outTable = UIDRef::gNull;
	outCell = GridAddress();
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil || at < 0 || at >= model->TotalLength())
		return false;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	IDataBase* const db = story.GetDataBase();
	const UID dict = (thread != nil) ? thread->GetDictUID() : kInvalidUID;
	if (!IsTableNow(db, dict))
		return false;		// (the story's own text, a footnote's, a note's: a thread of no table)
	InterfacePtr<ITableModel> table(db, dict, UseDefaultIID());
	if (table == nil)
		return false;
	const GridAddress cell(table->GetGridAddress(GridID(thread->GetDictKey())));
	if (cell.row < 0 || cell.row >= table->GetTotalRows().count)
		return false;
	outTable = UIDRef(db, dict);
	outCell = table->FindCellAnchor(cell);
	return true;
}

bool KFCTableRows::ListRows(PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	PMString lead("List Rows of These Tables: ");
	lead.SetTranslatable(kFalse);
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	// A TABLE LIST'S ALONE (T1 - the menu greys it over any other; this stays for a caller that never opened the menu).
	if (!KFCResultModel::ListIsTables())
	{
		outStatus.Append(lead);
		outStatus.Append("this is not a list of tables - use it on a tier of the Table kind.");
		return false;
	}

	// ITS TABLES, read off the live list before anything is parked: each document's, in the list's order, a table once - the rows cleared from
	// the list left out. A document found open (by its file - ChapterDocNow); a book's chapter not open reached by its file, windowless, as a
	// Replace reaches it (handed back below); any other list's document not open named, never reopened (LIFE-01).
	const bool fromBook = KFCResultModel::IsFromBook();
	std::vector<DocRows> docs;
	PMString notOpen;
	notOpen.SetTranslatable(kFalse);
	int32 gone = 0;
	for (int32 c = 0; c < KFCResultModel::GetChapterCount(); ++c)
	{
		DocRows one;
		for (int32 h = 0; h < KFCResultModel::GetHitCount(c); ++h)
		{
			const UID table = KFCResultModel::GetHitTable(c, h);
			if (table == kInvalidUID || KFCResultModel::IsHitCleared(c, h))
				continue;
			if (std::find(one.tables.begin(), one.tables.end(), table) == one.tables.end())
				one.tables.push_back(table);
		}
		if (one.tables.empty())
			continue;
		int32 rowsNow = 0;
		UIDRef bound;
		(void)KFCResultModel::GetChapterLocation(c, bound, one.target.file);
		(void)KFCResultModel::GetChapterDisplay(c, one.target.shortName, rowsNow);
		one.target.shortName.SetTranslatable(kFalse);
		one.target.docRef = KFCSearchEngine::ChapterDocNow(c);
		if (one.target.docRef.GetDataBase() == nil && fromBook && KFCBookScope::ChapterHasFile(one.target.file))
		{
			UIDRef reached(bound);
			if (KFCBookScope::ReachChapterDoc(one.target.file, reached))
				one.target.docRef = reached;
		}
		IDataBase* const db = one.target.docRef.GetDataBase();
		if (db == nil)
		{
			if (!notOpen.IsEmpty())
				notOpen.Append(", ");
			notOpen.Append(one.target.shortName);
			continue;
		}
		std::vector<UID> standing;
		for (size_t t = 0; t < one.tables.size(); ++t)
		{
			if (IsTableNow(db, one.tables[t]))
				standing.push_back(one.tables[t]);
			else
				++gone;
		}
		one.tables.swap(standing);
		if (!one.tables.empty())
			docs.push_back(one);
	}

	// THE ROWS (T2 - T4): every table's, top to bottom, up to the object lists' number; a row with no cell of its own counted.
	const int32 cap = KFCResultModel::kKFCCollectObjectLimit;
	int32 listed = 0, total = 0, covered = 0, tablesListed = 0;
	for (size_t d = 0; d < docs.size(); ++d)
	{
		DocRows& one = docs[d];
		IDataBase* const db = one.target.docRef.GetDataBase();
		for (size_t t = 0; t < one.tables.size(); ++t)
		{
			InterfacePtr<ITableModel> table(db, one.tables[t], UseDefaultIID());
			InterfacePtr<ITableTextContent> content(table, UseDefaultIID());
			InterfacePtr<ITableTextContainer> container(table, UseDefaultIID());
			InterfacePtr<ITextModel> model((container != nil) ? container->QueryTextModel() : nil);
			if (table == nil || content == nil || model == nil)
			{
				++gone;
				continue;
			}
			const UID story = ::GetUID(model);
			const RowRange rows(table->GetTotalRows());
			bool any = false;
			for (int32 r = rows.start; r < rows.start + rows.count; ++r)
			{
				KFCSearchEngine::ListedPlace place;
				place.story = story;
				place.start = 0;
				place.end = 0;
				if (!RowStretch(table, content, r, place.start, place.end))
				{
					++covered;
					continue;
				}
				++total;
				if (listed >= cap)
					continue;		// (counted - only a row is what the cap limits)
				++listed;
				any = true;
				one.places.push_back(place);
				one.words.push_back(RowWords(table, r));
				one.rowTables.push_back(one.tables[t]);
			}
			if (any)
				++tablesListed;
		}
		// What a search's row holds, for each - its line and hash, its page and flags (BuildListedHits composes, as a search does: a clean
		// document left clean).
		IDataBase::SaveRestoreModifiedState keepClean(db);
		KFCSearchEngine::BuildListedHits(one.target.docRef, one.places, one.hits, one.versions);
		for (size_t h = 0; h < one.hits.size() && h < one.words.size(); ++h)
		{
			one.hits[h].runChain = one.words[h];
			one.hits[h].runChain.SetTranslatable(kFalse);
			one.hits[h].tableUID = one.rowTables[h];
			one.hits[h].holderKind = kKFCHolderTableRow;		// (its cells take Apply Cell Style, a click selects them - the UI half asks GetHitHolderKind)
		}
	}
	if (listed == 0)
	{
		KFCBookScope::ReleaseHeldDocs();		// (a chapter reached for nothing handed back)
		outStatus.Append(lead);
		if (!notOpen.IsEmpty())
		{
			outStatus.Append("the documents of these tables are not open - ");
			outStatus.Append(notOpen);
			outStatus.Append(".");
		}
		else if (gone > 0)
			outStatus.Append("these tables are no longer in their documents.");
		else
			outStatus.Append("these tables have no rows to list.");
		return false;
	}

	// A NEW LIST (T1): the list of tables kept as a tier (a search's commit point's way - its rows' foci go with it), the rows thrown away as a
	// query run's end throws them away - the model cleared and the Find Format forgotten, the book kept (KFCSearchEngine::ForgetSearchedFindFormat
	// says why) - and the header written as the list's: its book, All Documents and Search: as the list of tables recorded them, its mode KFC's
	// own (kKFCTableRowsSearchMode).
	const bool allDocuments = KFCResultModel::IsFromAllDocuments();
	const KFCResultModel::SearchScopeKind scope = KFCResultModel::GetSearchScope();
	const PMString bookName(KFCResultModel::GetBookName());
	KFCTierStore::ParkCurrent();
	KFCResultModel::Clear();
	KFCSearchEngine::ForgetSearchedFindFormat();
	KFCResultModel::SetFromBook(fromBook);
	KFCResultModel::SetFromAllDocuments(allDocuments);
	KFCResultModel::SetSearchScope(scope);
	KFCResultModel::SetBookName(bookName);
	KFCResultModel::NoteRun();
	KFCResultModel::SetSearchMode(static_cast<int32>(kKFCTableRowsSearchMode));
	KFCQueryCopy copy;
	KFCQueryCopier::CaptureTableRows(copy);
	KFCResultModel::SetQueryCopy(std::make_shared<KFCQueryCopy>(copy));
	KFCSearchEngine::CollectTally tally;
	for (size_t d = 0; d < docs.size(); ++d)
		if (!docs[d].hits.empty())
			KFCSearchEngine::FileChapter(docs[d].target, docs[d].hits, tally, &docs[d].versions);
	// The rows follow the text as a search's do (the text-focus spec's T6): an edit made since is followed to where it went.
	KFCRowFoci::AttachOpenChapters();
	// A book's chapter the listing reached handed back, as a run hands back its own (a scheduled close: the list's chapter is filed first, so the
	// close is followed as any chapter's).
	KFCBookScope::ReleaseHeldDocs();
	KFC_DIAG_LOG("TABLEROWS listed=%d total=%d covered=%d tables=%d gone=%d docs=%d", static_cast<int>(listed), static_cast<int>(total),
		static_cast<int>(covered), static_cast<int>(tablesListed), static_cast<int>(gone), static_cast<int>(docs.size()));

	// WHAT THE MESSAGE LINE SAYS: "Listed 7 rows of 2 tables." - then what was left out.
	outStatus.Append("Listed ");
	outStatus.AppendNumber(listed);
	outStatus.Append(listed == 1 ? " row of " : " rows of ");
	outStatus.AppendNumber(tablesListed);
	outStatus.Append(tablesListed == 1 ? " table." : " tables.");
	if (total > listed)
	{
		outStatus.Append(" The list shows the first ");
		outStatus.AppendNumber(listed);
		outStatus.Append(" of ");
		outStatus.AppendNumber(total);
		outStatus.Append(" rows.");
	}
	if (covered > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(covered);
		outStatus.Append(" row(s) covered whole by merged cells from above are not listed.");
	}
	if (gone > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(gone);
		outStatus.Append(" table(s) no longer in their documents are not listed.");
	}
	if (!notOpen.IsEmpty())
	{
		outStatus.Append(" Not open: ");
		outStatus.Append(notOpen);
		outStatus.Append(".");
	}
	return true;
}

// End, KFCTableRows.cpp.
