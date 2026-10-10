//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE OBJECT TAB'S WRITES (1.4.0 - docs/superpowers/specs/2026-10-09-kfc-object-search-design.md O10, O13, O14):
//  InDesign's own object replace, aimed by the shared object walker as the search is (KFCObjectSearch.h).
//    A ROW'S REPLACE - the walker aimed at the row's document, InDesign's SearchObject asked until it stands on the row's
//    item (StartWithItem does not move where a search begins - measured), then ReplaceObject: that item alone, in one undo
//    step. Its doors - the settings, the change side, the item there, unlocked and as the search found it - are asked
//    first and can be asked alone (CheckRowNow), so the UI half refuses before it brings a window forward.
//    SEVERAL ROWS' REPLACE (O18) - the rows selected together: the same doors row by row, then one walk replacing each
//    row's item as the walk stands on it, in one undo step (ReplaceRows / CheckRowsNow).
//    A DOCUMENT'S CHANGE ALL - ReplaceAllObject over the document (it selects nothing - measured).
//
//========================================================================================

#ifndef __KFCObjectReplace_h__
#define __KFCObjectReplace_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

namespace KFCObjectReplace
{
	/** O10 steps 1-5 for an object row, with nothing written: the Find/Change settings still the search's (the tab, the
	    Type, the five switches, Find Object Format and its style - a changed one CLEARS the results, as for text), something
	    to change to, the item still in its document (else the row reads Missing), not locked, and its fingerprint the one
	    the row holds. A chapter reopened to ask is handed back when the answer is no. false = refused, outStatus says why
	    ("Replace: ..."). Call it OUTSIDE any command sequence. */
	bool CheckRowNow(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** An object row's Replace (O10 steps 1-5, 7, 8, 10, 11): the doors again, then the walk to the item and ReplaceObject,
	    one undo step ("Replace") with KFC's undo mark in it; the row Changed with the item's new fingerprint;
	    "Replaced ID:<uid>." on success. The selection InDesign leaves (step 9) is the UI half's to settle: the row's
	    item selected (the author's call of 2026-10-10). */
	bool ReplaceRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** SEVERAL OBJECT ROWS OF ONE DOCUMENT REPLACED TOGETHER (O18 - the author's call of 2026-10-10: the rows selected
	    together are replaced together). Steps 1-2 asked once; 3-5 row by row - a row that fails is left as it is and
	    counted by its reason, the others written in ONE walk (InDesign's own matching, its replace on each row's item as
	    the walk stands on it) and ONE undo step with KFC's undo mark in it; each written row Changed with its item's new
	    fingerprint. outStatus: "Replaced 3 objects: ID:259, ID:260, ID:261." - or "Replaced 3 of 5 objects: ... Left as
	    they were: 1 locked, 1 changed since the search."; a refusal when none could be written. One row = ReplaceRow.
	    True when something was written. */
	bool ReplaceRows(int32 chapterIdx, const std::vector<int32>& hitIdxs, PMString& outStatus);

	/** ReplaceRows asked before its window is brought forward, nothing written: true at the first row that would be
	    written; false = none would, outStatus says why (the reasons counted). One row = CheckRowNow. */
	bool CheckRowsNow(int32 chapterIdx, const std::vector<int32>& hitIdxs, PMString& outStatus);

	/** InDesign's Change All on the Object tab over one document (O13, O14): ReplaceAllObject with the shared walker aimed
	    at it. outReplaced = what InDesign changed fully or in part; outPartially = in part. false = InDesign failed. The
	    caller runs it inside its own sequence and aims the walker at the front again at the end. */
	bool ReplaceAllInDoc(const UIDRef& docRef, int32& outReplaced, int32& outPartially);
}

#endif // __KFCObjectReplace_h__
