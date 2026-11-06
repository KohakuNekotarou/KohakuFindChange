//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ TAB'S SPELLING KIND, LISTED (the author, 11-06: "a list of the spelling mistakes - the words with the wavy line in English text - can it be one of
//  the search tab's?"; docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md SP1 - SP6; KFCWarichuSearch's shape). While Find: on Find/Change's
//  Kohaku+ tab says Spelling (the tab itself is the UI half's - IKFCUIServices::SpellingTabConditions), the panel's Find lists the words Dynamic Spelling
//  underlines - red and green alike (misspelled, repeated, uncapitalized, an uncapitalized sentence: the author, "as you recommend"), a row a line, the
//  locator naming which line it has. WHAT THE WAVY LINE READS: InDesign keeps what Dynamic Spelling found in a strand of the story's text model
//  (ISpellingDataStrand - a public header: kSpellingStrandBoss) and draws its lines from it, a run in error a line (open/components/spellpanel/
//  DynamicSpellCheckAdornment.cpp's CoreDraw). KFC reads the same strand - the list is the lines. MEASURED (2026-11-06 - docs/ai-notes/
//  kfc-label-spelling-measure-2026-11-06.md): InDesign checks every open document's stories while it is idle - pages out of view, overset text, parent
//  pages, a document behind, one with no window - and the strand is SAVED with the document: a closed chapter KFC opens reads as it was saved (a chapter
//  saved before InDesign checked it reads unchecked). What has not been checked (kSpellingUnchecked) has no line and no row, and its stories are counted -
//  CollectTally::notChecked, a closed chapter's notCheckedClosed (KFC opens it and gives it back at once: no idle comes between). Dynamic Spelling off draws no line: the kind says so
//  and lists nothing (SP4). Text a hidden condition holds, deleted text and notes are not listed. It changes nothing, as Warichu: no Change side, no
//  Replace. A row is a text row in everything else - its click lands on its word. The main thread only.
//
//========================================================================================

#ifndef __KFCSpellingSearch_h__
#define __KFCSpellingSearch_h__

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCSpellingConditions
#include "KFCResultModel.h"		// Hit
#include "KFCSearchEngine.h"	// CollectTally, ListedPlace

#include <map>
#include <vector>

namespace KFCSpellingSearch
{
	/** IS THE SPELLING KIND FIND/CHANGE'S NOW, and what it asks (either way). Asked of the UI half (IKFCUIServices::SpellingTabConditions - ui/KFCKohakuTab.h); no UI half = not it. A
	    TEST BUILD asks the fault switch spelling-tab first: on = the Spelling kind, its file's lines scope=all|story|toend|selection and the icons' on=... / off=... the conditions. */
	bool IsTabCurrent(KFCSpellingConditions* outConditions = nil);

	/** IS DYNAMIC SPELLING ON (Edit > Spelling > Dynamic Spelling - ISpellingPrefs::GetDynamicSpellCheck, the session's)? Off, InDesign draws no wavy line and checks nothing
	    for one: the kind lists nothing and says so (SP4). A TEST BUILD's fault switch spelling-off answers no. */
	bool DynamicSpellingIsOn();

	/** THE WORD A ROW'S LOCATOR SAYS FOR A SPELLING STATE (Hit::spellKind) - Preferences > Spelling's words: "misspelled", "repeated", "uncapitalized", "uncapitalized sentence";
	    "" for any other. */
	const char* KindWord(int32 spellKind);

	/** THE KIND'S WALK OVER A RUN'S TARGETS (KFCWarichuSearch::CollectTargets' shape). part (KFCSearchEngine::PartStretches): the lines that touch them. nil = the whole of
	    each target. The stories not checked yet are counted - out.notChecked, out.notCheckedClosed for the closed chapters it opened. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCSpellingConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part = nil);

	/** One document's rows again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCSpellingConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** THE FACE BUTTONS' WALK OF ONE DOCUMENT (KFCFaceText - Find Next): every row a search of the conditions would list there, no list's limit. False = no story list to walk. */
	bool WalkForFace(const UIDRef& docRef, const KFCSpellingConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, std::vector<KFCResultModel::Hit>& outHits);
}

#endif // __KFCSpellingSearch_h__

// End, KFCSpellingSearch.h.
