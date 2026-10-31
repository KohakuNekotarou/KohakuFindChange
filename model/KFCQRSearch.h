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

namespace KFCQRSearch
{
	/** IS THE QR CODE TAB FIND/CHANGE'S TAB NOW (the design's D1), and the tab's Search: in outScope (when given) -
	    IWalkerScopeFactoryUtils::kDocumentScope or kAllDocumentScope. Asked of the UI half, which keeps it from the face's
	    own calls (IKFCUIServices::IsQRCodeTabCurrent - ui/KFCKohakuTab.h); no UI half = not the QR Code tab. A TEST BUILD asks
	    its fault switch qr-tab first (the model's cases, which need no dialog): on = the QR Code tab, its file's first line
	    "all" = All Documents, anything else Document. false = not the QR Code tab. */
	bool IsTabCurrent(int32* outScope = nil);

	/** A QR CODE LIST'S ROWS ARE ONLY GONE TO (the design's Q13 - the author: "no replace"): the words an operation that would
	    change something - Replace (a row's, the rows', Return's), Create Outlines, a Special Operation - refuses them with,
	    after its own name ("Replace: ..."). */
	const char* NotChangedWords();

	/** SEARCHBOOK'S WALK FOR THE QR CODE TAB: every target walked under one bar - a book's chapters opened and handed back
	    one at a time, as the Object tab's walk does (KFCObjectSearch::CollectTargets) - each target's rows filed as every
	    walk's are (KFCSearchEngine::FileChapter: page order, the tally). Cancel is heard between documents. reserved = rows
	    the list holds before the walk (a search again's kept rows); the object lists' limit counts the rest. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
		std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved);

	/** SEARCH THIS DOCUMENT AGAIN on a QR code list (KFCSearchEngine::SearchDocumentAgain): ONE open document walked whole,
	    under a bar of its own. Its rows in outHits (not in the model) - at most `limit` (outCapped when there were more).
	    true = walked to its end; false = no walker, or Cancel was pressed (outCancelled) - the rows are not to be used. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
		std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled);

	/** DOES THE ROW'S FRAME STILL HOLD A QR CODE (the design's Q15 - IKFCResults::IsHitItemGone asks it of a QR code list's
	    rows)? Read in its document as it stands (KFCQRCodes::Read): false = no QR code there now - the frame gone, or holding
	    a picture or nothing since. A document not open now cannot tell: true (the row's click opens it and asks again).
	    What the QR code holds is not compared - a row keeps the text it was found with (Q15). */
	bool RowHoldsQRCode(int32 chapterIdx, int32 hitIdx);

	/** FIND NEXT ON THE QR CODE TAB (the design's Q10 / D5 - the tab's button, ui/KFCKohakuTab): the QR code after `last` -
	    the one Find Next found before; an invalid UIDRef, or one of a document not in the range now or no longer holding it =
	    from the start - in the list's own order: the tab's Search: (the active document, or every open one in the order
	    All Documents lists them - KFCBookScope::ListOpenDocuments), each document's QR codes in page order
	    (KFCResultModel::OrderHitsByPage). After the last, the first again, silently (InDesign's own Find Next). The selection is
	    not asked (Q10). outReach = what keeps it from being selected (kKFCQRReach* bits, KFCModelTypes.h); outWords
	    = what the tab's first line says of it: its kind and content, and where. false = no QR code at all there
	    (outWords says so). Read only. */
	bool Next(const UIDRef& last, UIDRef& outFrame, int32& outReach, PMString& outWords);
}

#endif // __KFCQRSearch_h__

// End, KFCQRSearch.h.
