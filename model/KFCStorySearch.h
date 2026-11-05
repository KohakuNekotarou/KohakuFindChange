//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU+ TAB'S STORY KIND, LISTED (the author, 11-05: "a story tab on the search - internal stories too. internal, internal" - and, read back to them as
//  "every story listed as a row, a label or not", "yes - internal stories in the Story tab's search too"; docs/superpowers/specs/2026-11-06-kfc-story-kind-design.md
//  ST1 - ST14, every call the author left to me: "the plan as you recommend"). While Find: on Find/Change's Kohaku+ tab says Story (the tab itself is the UI half's -
//  IKFCUIServices::StoryTabConditions), the panel's Find lists EVERY story of a document - IStoryList's whole list (GetAllTextModelCount: "use this function when
//  iterating over all stories"), the user's and then the INTERNAL ones (kNotUserAccessible - "not subject to search through find change, spell checking", IStoryList.h;
//  measured 2026-11-05 - work/note-scripts/2026-11-05-kfc-story-tab/story_census.cpp: every document holds one, the XML structure's backing store, made with the document,
//  with no frame list, holding the words of the XML elements that are not placed). Nothing is left out (ST4): a parent page's, the pasteboard's, a hidden or locked
//  layer's, a locked story, an overset one, an empty one - an inventory, as the Script Label kind lists every labelled item.
//  A ROW IS A TEXT ROW STANDING FOR ITS STORY (ST5 - KFCResultModel::Hit::holderKind kKFCHolderStory, kKFCHolderInternalStory): its range the story's own text, its
//  primary thread up to its last return - [0, E) -, under the story's branch as every text row; what it draws (Hit::runChain - the Table kind's way) the story's facts:
//  "2 frames, 1010 characters, overset", "text on path, 10 characters", "endnotes, 1 frame, 7 characters", "1 frame, empty", "internal: XML structure, 2 characters" - the
//  characters the text a person reads (no paragraph return, no mark or anchor of InDesign's). A row FOLLOWS ITS STORY, NOT ITS WORDS (ST6 - KFCSearchEngine::LocateRow asks StoryRange): its text edited since, its click still selects the story's
//  text as it is now. It writes nothing (ST8): no Change side, no Replace. The main thread only.
//
//========================================================================================

#ifndef __KFCStorySearch_h__
#define __KFCStorySearch_h__

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCStoryConditions
#include "KFCResultModel.h"		// Hit
#include "KFCSearchEngine.h"	// CollectTally

#include <map>
#include <vector>

class IDataBase;

namespace KFCStorySearch
{
	/** IS THE STORY KIND FIND/CHANGE'S NOW, and what it asks (either way - its Search: alone). Asked of the UI half (IKFCUIServices::StoryTabConditions - ui/KFCKohakuTab.h);
	    no UI half = not it. A TEST BUILD asks the fault switch story-tab first: on = the Story kind, its file's line scope=all the conditions. */
	bool IsTabCurrent(KFCStoryConditions* outConditions = nil);

	/** THE KIND'S WALK OVER A RUN'S TARGETS (KFCWarichuSearch::CollectTargets' shape): each target's every story, a row each, filed a chapter each (page order, the tally); a
	    book's chapter opened and handed straight back. Cancel is heard between documents. reserved = rows the list holds before the walk; the object lists' limit counts the rest. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCStoryConditions& asked);

	/** One document's rows again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCStoryConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** THE FACE'S FIND NEXT'S WALK OF ONE DOCUMENT (KFCFaceText): every row a search lists there, no list's limit. False = no story list to walk. */
	bool WalkForFace(const UIDRef& docRef, const KFCStoryConditions& asked, std::vector<KFCResultModel::Hit>& outHits);

	/** WHERE A STORY ROW STANDS NOW (ST6): the story still in its document's story list - its text [0, outEnd), the primary thread up to its last return. False = it is gone
	    (deleted - an Undo of the step that made it, its frames deleted), or no story. Read only. */
	bool StoryRange(IDataBase* db, UID story, TextIndex& outEnd);
}

#endif // __KFCStorySearch_h__

// End, KFCStorySearch.h.
