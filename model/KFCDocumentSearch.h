//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ TAB'S DOCUMENT KIND, LISTED (the author, 2026-11-06: "a Document tab on the search" - "to find the documents with no window" - "open a document
//  without a window, in that tab" - "Special Operations: Close" - "Show, for a hidden document; grey for a shown one"; docs/superpowers/specs/
//  2026-11-06-kfc-windowless-documents-design.md W3). While Find: on Find/Change's Kohaku+ tab says Document (the tab itself is the UI half's -
//  IKFCUIServices::DocumentTabCurrent), the panel's Find lists EVERY OPEN DOCUMENT, window or not (All Documents alone: KFCBookScope::ListOpenDocuments -
//  the chapters a run of ours holds left out), or, with Book Scope on, the book's chapters that are open (a closed one is not opened to be listed: what is
//  looked for is a document open and not seen).
//  A ROW IS THE DOCUMENT (KFCResultModel::Hit::holderKind kKFCHolderDocument): no story, no item - its words its name, then its facts (what Hit::runChain
//  draws, the Story kind's way): "no window" / "1 window" / "2 windows" (layout windows), ", 1 Story Editor", ", not saved" / ", modified", and the file's
//  path after it. It hangs off the list's top (KFCResultTree - no document row above it says the name again). It writes nothing (no Change side, no
//  Replace); its Special Operations (Show, Close, Save, Revert - KFCDocumentOps) are the UI half's. The main thread only.
//
//========================================================================================

#ifndef __KFCDocumentSearch_h__
#define __KFCDocumentSearch_h__

#include "KFCBookScope.h"		// ChapterDoc - a run's targets
#include "KFCResultModel.h"		// Hit
#include "KFCSearchEngine.h"	// CollectTally

#include <vector>

namespace KFCDocumentSearch
{
	/** IS THE DOCUMENT KIND FIND/CHANGE'S NOW. Asked of the UI half (IKFCUIServices::DocumentTabCurrent); no UI half = not it. A TEST BUILD asks the fault
	    switch document-tab first: on = the Document kind. */
	bool IsTabCurrent();

	/** THE KIND'S WALK OVER A RUN'S TARGETS: one row for each target that is open - a book's chapter that is closed is passed by (not opened, not counted
	    as unopenable) - filed a chapter each (KFCSearchEngine::FileChapter). Nothing is opened, composed or written. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, KFCSearchEngine::CollectTally& out);

	/** ONE DOCUMENT'S ROW (a run's, and Search This Document Again's): false = it is not open. */
	bool RowOf(const UIDRef& docRef, const PMString& name, KFCResultModel::Hit& outHit);
}

#endif // __KFCDocumentSearch_h__

// End, KFCDocumentSearch.h.
