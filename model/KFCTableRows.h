//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A TABLE ROW LIST (the author, 11-05: "after searching tables, a list of their ROWs - then give those rows a cell style by
//  Special Operations" - docs/superpowers/specs/2026-11-05-kfc-table-rows-cell-style-design.md): a Table list's tier heading >
//  List Rows of These Tables puts a new tier on - one row a table row, each table's rows top to bottom (its header and footer
//  rows named), the tables in the list's order. A row is a TEXT row (T3): its story and the text of its cells whose top-left is
//  in that row - ITableTextContent::GetTextChunk, one stretch a row (measured 11-05: a merged cell is in its top row alone; a
//  table in a cell keeps its own cells' text out of the outer table's) - and Hit::tableUID. So a search's rows' ways go on
//  serving it: followed through edits, the eye, Copy Rows, Clear from Results, Undo's following. It draws its words
//  (Hit::runChain: "Row 2: a0 | a1"). No search made it - a query run's list's way (kKFCTableRowsSearchMode): not searched
//  again, not narrowed, not replaced; a row is written by Special Operations > Apply Cell Style (KFCReplaceEngine::CellStyleHits),
//  given to the row its text stands in when it is pressed (TableRowAt - T7).
//  The model half's main thread alone.
//
//========================================================================================

#ifndef __KFCTableRows_h__
#define __KFCTableRows_h__

#include "PMString.h"
#include "UIDRef.h"

class ITableModel;

namespace KFCTableRows
{
	/** LIST ROWS OF THESE TABLES: the live list - a Table list (KFCResultModel::ListIsTables) - read for its tables (the rows not cleared from it,
	    each table still in its document; a document not open named, not reopened), then parked as a search parks it (KFCTierStore::ParkCurrent),
	    and the tables' rows filed as a new list (kKFCTableRowsSearchMode) - at most KFCResultModel::kKFCCollectObjectLimit rows. A row whose every
	    column a merged cell from above covers holds no cell of its own: counted and said, not listed. outStatus: "Listed 7 rows of 2 tables." and
	    what it left out - or the refusal, the list untouched. True = a new list. */
	bool ListRows(PMString& outStatus);

	/** WHAT A TABLE ROW'S ROW DRAWS: "Row <n>[ (header)| (footer)]: <cell> | <cell>" - n counts from 1, a cell's text cut to a few words, the
	    whole kept short; its line breaks the panel draws as a hit row's (MarkUpBreaksForDisplay). Untranslatable. */
	PMString RowWords(ITableModel* table, int32 row);

	/** THE TABLE ROW A TEXT POSITION STANDS IN (T7): the story thread of `at` in `story` - a cell of a table - its table and the row of its cell
	    (ITextStoryThread::GetDictKey as a GridID - ITableModel::GetGridAddress). False = the position is in no table's cell. */
	bool TableRowAt(const UIDRef& story, TextIndex at, UIDRef& outTable, int32& outRow);
}

#endif // __KFCTableRows_h__
