//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  COPY RESULTS (docs/superpowers/specs/2026-10-18-kfc-copy-results-progress-share-design.md - the author's yes to my
//  proposal): the result rows as text on InDesign's clipboard, to paste into a spreadsheet or a text for a proofreading
//  report. INDESIGN'S OWN SHAPE FOR A PANEL'S ROWS AS TEXT - the Links panel's Copy Info for Selected Links
//  (open/components/linksui/LinksUIPanelMenuComponent.cpp: GenerateLinkReport, CopyStringToScrap): a line of column
//  names, a tab between columns, "\r" between lines, put on the clipboard through its text flavor's scrap. Here the
//  columns are Document, Page, Status and Text - each row as the list draws it, in the order it draws them (the
//  Document column the document's name alone). The documents are not touched: nothing to undo.
//
//========================================================================================

#pragma once
#ifndef __KFCCopyResults_h__
#define __KFCCopyResults_h__

#include "KFCModelTypes.h"	// KFCRowsByChapter

class PMString;

namespace KFCCopyResults
{
	/** EVERY ROW OF THE LIVE LIST (the panel menu's Copy Results; the book row's Copy Rows). outStatus = the status line's
	    words. false = no row to copy, or the clipboard could not be written (said in outStatus). */
	bool CopyLiveList(PMString& outStatus);

	/** THE LIVE LIST'S ROWS NAMED (a hit, object, story or document row's Copy Rows - the rows Clear from Results would take
	    off; Ctrl+C on the rows selected), in the order the list draws them. A row the list does not show is not copied. */
	bool CopyLiveRows(const KFCRowsByChapter& rows, PMString& outStatus);

	/** A TIER'S ROWS (its heading's Copy These Results): a grey tier's as its copy holds them, the live tier's (-1) as the
	    live list draws them. */
	bool CopyTier(int32 tier, PMString& outStatus);

	/** Would CopyTier(tier) copy a row - the menus' grey (-1 = the live list). */
	bool TierHasRows(int32 tier);
}

#endif // __KFCCopyResults_h__
