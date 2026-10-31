//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ TAB'S KENTEN KIND, LISTED (the author, 10-31: "add kenten too" - in a Japanese InDesign alone, as Ruby; KFCRubySearch's shape). While Find: on Find/Change's Kohaku+ tab says Kenten (the
//  tab itself is the UI half's - IKFCUIServices::KentenTabConditions), the panel's Find lists the text with emphasis marks: a row a stretch of one kind of mark (kTAKentenKindBoss as the composer reads it
//  - IComposeScanner::QueryAttributeAt, which joins the characters that share it: a character style's marks too), cut at a paragraph's end and at anything of InDesign's in the text, and at a story
//  thread's end - a table's cells are threads of its own. Base Text and Kenten narrow it. Text Without Kenten lists each occurrence of Base Text with no mark on it. A row draws its base and the mark
//  (Hit::runChain), and is a text row in everything else. Replace on rows gives them Change Kenten To, or takes the marks off with Remove Kenten (the kind Kenten_None, as InDesign's own menu does - a
//  character style's marks go too): the look of the marks is left as it stands. All the rows in one step.
//
//========================================================================================

#ifndef __KFCKentenSearch_h__
#define __KFCKentenSearch_h__

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCKentenConditions
#include "KFCResultModel.h"		// Hit
#include "KFCSearchEngine.h"	// CollectTally, ListedPlace

#include <map>
#include <vector>

namespace KFCKentenSearch
{
	/** IS THE KENTEN KIND FIND/CHANGE'S NOW, and what it asks (either way). Asked of the UI half (IKFCUIServices::KentenTabConditions - ui/KFCKohakuTab.h); no UI half = not it. A TEST BUILD asks the fault
	    switch kenten-tab first: on = the Kenten kind, its file's lines kind=without, base=, mark=<n>, change=<n>, remove=1, scope=all|story|toend|selection the conditions. */
	bool IsTabCurrent(KFCKentenConditions* outConditions = nil);

	/** THE KIND'S WALK OVER A RUN'S TARGETS (KFCRubySearch::CollectTargets' shape). part (KFCSearchEngine::PartStretches): a stretch of marks they touch - a row is the stretch whole, as
	    a ruby's is; an occurrence (Text Without Kenten) they hold whole, as a text search finds none a selection cuts. nil = the whole of each target. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCKentenConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part = nil);

	/** One document's rows again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCKentenConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** The kind of mark on [start, end) of the story, when one kind runs over the whole of it (Kenten_None = none on it); -1 when it changes inside. */
	int32 MarkOn(const UIDRef& story, TextIndex start, TextIndex end);

	/** What a row draws: its base (cut when long) and the mark, as the page's own marks read - empty for no mark (the row draws its own text). */
	PMString RowWords(const UIDRef& story, TextIndex start, TextIndex end, int32 mark);

	/** Replace on a row: the kind on [start, end) (IKentenStyle::Kenten_None takes the marks off). */
	ErrorCode Write(const UIDRef& story, TextIndex start, TextIndex end, int32 mark);

	/** A QUERY RUN'S WRITE OF ONE DOCUMENT (KFCKohakuQuery::WriteDocument): what the conditions list, given their Change side, with no rows. outCount = the places written. False = InDesign would not write one. */
	bool WriteDocument(const UIDRef& docRef, const KFCKentenConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount);

	/** REMOVE KENTEN (the author, 10-31: "does it come off properly?"): every kenten attribute off the range, not its kind alone (InDesign's own menu and the SDK's RemoveKenten write the kind None and leave the size, font,
	    colour and the rest on the text) - and, where a character or paragraph style still gives the text a mark, its kind turned off on the text (the style stays: other text has it). kSuccess = done. */
	ErrorCode Remove(const UIDRef& story, TextIndex start, TextIndex end);
}

#endif // __KFCKentenSearch_h__

// End, KFCKentenSearch.h.
