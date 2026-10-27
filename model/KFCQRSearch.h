//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QR CODE TAB, LISTED (docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md). While KFC's own tab in
//  Edit > Find/Change - "QR Code" - is the tab Find/Change is on (IsTabCurrent; KFCSearchEngine::CurrentSearchMode answers
//  kKFCQRCodeSearchMode then, and CurrentSearchScope the tab's own Search:), the panel's Find lists every QR code of what it
//  searches, a document at a time: no condition to match (the author: "list every QR code"). Its run scope is the text
//  tabs' (KFCSearchEngine::ResolveRunScope - Document or All Documents, the book with Book Scope, FIND-13's refusal: the same
//  doors and words). Read by KFCQRCodes - an object walker of KFC's own, so nothing is selected, scrolled or written, and no
//  Find/Change setting is touched. A row is an OBJECT row (KFCResultModel::Hit's itemUID - the QR code's frame), its text
//  "ID<uid>:<kind>  <content>" (Q7 / Q14), its place read as the object rows' is (KFCObjectSearch::ReadItemPlace). A QR
//  code list's rows are only gone to (Q13): every operation that would change something refuses them (NotChangedWords).
//
//========================================================================================

#ifndef __KFCQRSearch_h__
#define __KFCQRSearch_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCResultModel.h"		// Hit - a row
#include "KFCSearchEngine.h"	// CollectTally - a walk's count
#include "KFCQRCodes.h"			// Kind

namespace KFCQRSearch
{
	/** IS THE QR CODE TAB FIND/CHANGE'S TAB NOW (the design's D1), and the tab's Search: in outScope (when given) -
	    IWalkerScopeFactoryUtils::kDocumentScope or kAllDocumentScope. A TEST BUILD asks its fault switch qr-tab first (the
	    plan's Task 3 cases, before the tab itself exists): on = the QR Code tab, its file's first line "all" = All
	    Documents, anything else Document. false = not the QR Code tab. */
	bool IsTabCurrent(int32* outScope = nil);

	/** A QR CODE LIST'S ROWS ARE ONLY GONE TO (the design's Q13 - the author: "no replace"): the words an operation that would
	    change something - Replace (a row's, the rows', Return's), Create Outlines, a Special Operation - refuses them with,
	    after its own name ("Replace: ..."). */
	const char* NotChangedWords();

	/** SEARCHBOOK'S WALK FOR THE QR CODE TAB: every target walked under one bar - a book's chapters opened and handed back
	    one at a time, as the Object tab's walk does (KFCObjectSearch::CollectTargets) - each target's rows filed as every
	    walk's are (KFCSearchEngine::FileChapter: page order, the tally). Cancel is heard between documents. reserved = rows
	    the list holds before the walk (a search again's kept rows); the object lists' limit counts the rest. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
		std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved);

	/** SEARCH THIS DOCUMENT AGAIN on a QR code list (KFCSearchEngine::SearchDocumentAgain): ONE open document walked whole,
	    under a bar of its own. Its rows in outHits (not in the model) - at most `limit` (outCapped when there were more).
	    true = walked to its end; false = no walker, or Cancel was pressed (outCancelled) - the rows are not to be used. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
		std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled);

	/** A QR CODE'S ROW: itemUID = the frame, matchText = "ID<uid>:<kind>  <content>" (the content cut short where a row's
	    text is - the panel cuts it again to its width), its page and words as an object row's (ReadItemPlace). */
	void BuildRow(const UIDRef& docRef, UID frame, KFCQRCodes::Kind kind, const PMString& content, KFCResultModel::Hit& out);
}

#endif // __KFCQRSearch_h__

// End, KFCQRSearch.h.
