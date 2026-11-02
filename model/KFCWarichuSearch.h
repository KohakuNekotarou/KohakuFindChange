//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ TAB'S WARICHU KIND, LISTED (the author, 11-02: a warichu search tab too, "as the QR Code tab, Japanese only" - docs/superpowers/specs/2026-11-02-kfc-warichu-tcy-design.md A1 - A6; a
//  Japanese InDesign alone, as Ruby and Kenten; KFCKentenSearch's shape). While Find: on Find/Change's Kohaku+ tab says Warichu (the tab itself is the UI half's - IKFCUIServices::WarichuTabConditions),
//  the panel's Find lists the text set as warichu: a row a stretch of it (kTAWarichuAttrBoss as the composer reads it - IComposeScanner::QueryAttributeAt, which joins the characters that share it: a
//  character style's warichu too - the SDK's SnpPerformTextAttrWarichu.cpp reads it the same), cut at a paragraph's end and at anything of InDesign's in the text, and at a story thread's end - a table's
//  cells are threads of their own. Text a hidden condition holds, deleted text and notes are not listed. It changes nothing, as QR Code: no Change side, no Replace. A row is a text row in everything
//  else - its click lands on its text, and its Special Operations are a text row's.
//
//========================================================================================

#ifndef __KFCWarichuSearch_h__
#define __KFCWarichuSearch_h__

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCWarichuConditions
#include "KFCResultModel.h"		// Hit
#include "KFCSearchEngine.h"	// CollectTally, ListedPlace

#include <map>
#include <vector>

namespace KFCWarichuSearch
{
	/** IS THE WARICHU KIND FIND/CHANGE'S NOW, and what it asks (either way). Asked of the UI half (IKFCUIServices::WarichuTabConditions - ui/KFCKohakuTab.h); no UI half = not it. A TEST BUILD asks the
	    fault switch warichu-tab first: on = the Warichu kind, its file's lines scope=all|story|toend|selection and the icons' on=... / off=... the conditions. */
	bool IsTabCurrent(KFCWarichuConditions* outConditions = nil);

	/** THE KIND'S WALK OVER A RUN'S TARGETS (KFCKentenSearch::CollectTargets' shape). part (KFCSearchEngine::PartStretches): a stretch of warichu they touch - a row is the stretch whole. nil = the whole
	    of each target. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCWarichuConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part = nil);

	/** One document's rows again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCWarichuConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** THE FACE BUTTONS' WALK OF ONE DOCUMENT (KFCFaceText - Find Next): every row a search of the conditions would list there, no list's limit. False = no story list to walk. */
	bool WalkForFace(const UIDRef& docRef, const KFCWarichuConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, std::vector<KFCResultModel::Hit>& outHits);
}

#endif // __KFCWarichuSearch_h__

// End, KFCWarichuSearch.h.
