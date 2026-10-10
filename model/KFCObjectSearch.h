//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE OBJECT TAB, LISTED (1.4.0 - docs/superpowers/specs/2026-10-09-kfc-object-search-design.md). Edit > Find/Change's
//  Object tab - Find Object Format, its object style, its Type and its five Include switches - searched by InDesign's own
//  object search (IFindChangeService::SearchObject), one document at a time: the service takes no document and walks
//  whatever the SHARED object walker (kObjectWalkerService) was last initialised for, so the walker is aimed at each
//  document first (spec O2 - measured in KT on 2026-10-09: a windowless chapter is searched that way, nothing primed).
//  What it finds is read off the shared walker (GetCurrentItem): the service's own OUT argument stays empty (O5).
//
//  WHAT THE SEARCH COSTS THE USER, AND WHAT IS GIVEN BACK. The service SELECTS what it finds - in the front document even
//  when it searched a windowless one, where it selects the item of the same number (measured). So a listing takes the
//  text walker's selection snapshot first and gives it back at the end (O6) - NEVER around a replace: given back after
//  ReplaceObject, the snapshot brought InDesign down (KT, 2026-10-09). And the shared walker is aimed at the front document
//  again when a run of ours is over (O7): it would go on naming a chapter the run has closed, for Edit > Find/Change's
//  next search.
//
//========================================================================================

#ifndef __KFCObjectSearch_h__
#define __KFCObjectSearch_h__

#include "PMString.h"
#include "UIDList.h"
#include "UIDRef.h"
#include "ObjectWalkerScopeOptions.h"

#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCResultModel.h"		// Hit - an object row
#include "KFCSearchEngine.h"	// RunScope

class IFindChangeService;
class IObjectWalker;

namespace KFCObjectSearch
{
	/** Is the Find Object Format set - an attribute in its list, or an object style? What Change All in Book and a query
	    run need before they write (O13: an empty one would rewrite every frame of the Type). */
	bool HasFindObjectFormat();

	/** ...and the Change side - Change Object Format, or an object style to apply (O10 step 2). */
	bool HasChangeObjectFormat();

	/** THE OBJECT TAB'S Search:, as the object search reads it (O3): kDocumentScope, kAllDocumentScope or kSelectionScope
	    (an IWalkerScopeFactoryUtils::WalkScopeType), as STORED - InDesign's own walk follows the stored value, not what an
	    open dialog shows (measured - the plan's Task 1 M6). An unset one reads as Document; Story / To End of Story read
	    as Document and say so in outFellBackNote (empty otherwise). Selection is answered whatever is selected - a run
	    with no page item selected is refused by ResolveObjectRunScope. -1 when the settings cannot be read. */
	int32 ObjectSearchScope(PMString& outFellBackNote);

	/** KFCSearchEngine::ResolveRunScope's doors and sentences, with ObjectSearchScope's reading of Search: - and one
	    door of its own: Search: = Selection with nothing selected - no page item, and no cursor in text - is refused, as
	    InDesign's own Find walks nothing there (and its object search fails a walk aimed at the document while Selection
	    is stored - measured 2026-10-10). */
	bool ResolveObjectRunScope(KFCSearchEngine::RunScope& out, PMString& outRefusal);

	/** The page items selected now - what Search: = Selection searches. Empty when none, or no UI half. */
	void SelectedPageItems(UIDList& out);

	/** Is the cursor in text now - a caret or a range? With no page item selected (the callers have just read the page
	    items - SelectedPageItems), Search: = Selection then searches the frame the cursor stands in, as InDesign's own
	    Find does (the author's call of 2026-10-10; the plan's Task 1 M6 measured it: a caret -> that frame only). false
	    with no UI half. */
	bool CursorInText();

	/** What one listing over a run's targets found (KFCSearchEngine.cpp's CollectTally, for page items). */
	struct Tally
	{
		int32					total;
		int32					chaptersWithHits;
		bool					truncated;
		bool					cancelled;
		std::vector<PMString>	unsearchable;
		std::vector<PMString>	brokeOff;
		std::vector<PMString>	unclosed;
		Tally() : total(0), chaptersWithHits(0), truncated(false), cancelled(false) {}
	};

	/** SEARCHBOOK'S WALK FOR THE OBJECT TAB: every target walked under one bar, a book's chapters opened and handed back
	    one at a time, each target's rows into the model as a chapter (page order). The user's selection is given back at
	    the end (O6) and the shared walker aimed at the front document (O7). selectionScope = RunScope::selectionScope.
	    CANCEL IS HEARD INSIDE A DOCUMENT (O16 - the author's call of 2026-10-10): the bar is moved and asked at every item
	    the walk lists, not only between documents; a Cancel ends the run as one between documents does (out.cancelled). */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
		int32 selectionScope, std::vector<KFCBookScope::SkippedChapter>& unopenable, Tally& out);

	/** SEARCH THIS DOCUMENT AGAIN on the Object tab (2026-10-10 - KFCSearchEngine::SearchDocumentAgain): ONE open document
	    walked whole as CollectTargets walks each of its targets, under a bar of its own ("Document 1 / 1 - <name>"), the
	    user's selection given back and the shared walker aimed at the front after (O6 / O7). Its rows come back in
	    outHits (not in the model) - at most `limit` (outCapped when there were more). true = the walk ended cleanly;
	    false = it broke off, or Cancel was pressed (outCancelled) - the rows are not to be used then. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
		std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled);

	/** An item's FINGERPRINT (O12): its own persistent data as the database writes it (IPMPersist::SaveAll into memory),
	    with what it holds - a group's members, a graphic frame's image - its anchored settings when it hangs in text,
	    and a text frame's (or a path's) story's version (ITextModel::GetChangeCount): one 64-bit hash and a length.
	    Moving, restyling, relinking or typing into it changes it; recomposing, zooming or selecting does not, and an Undo
	    puts it back exactly (measured 2026-10-10 night - the SaveAll probe, work/note-scripts/2026-10-10-kfc-saveall-probe/;
	    it took the place of the item's snippet, which was 20 to 100 times slower and crashed InDesign on an inline in
	    overset text). false = nothing of the item could be read. */
	bool Fingerprint(const UIDRef& item, uint64& outHash, uint32& outLength);

	/** Does a spread hold the item now - is it laid out (IPasteboardUtils::QuerySpread)? No for an item in text that is
	    not composed - an inline in OVERSET text: nothing of it can be shown or selected, and its row's Replace is refused
	    (2026-10-10 night - found when InDesign's snippet export, the fingerprint of the time, crashed on one:
	    work/kfc-crash-2026-10-10-overset-inline.xml). */
	bool IsOnSpread(const UIDRef& item);

	/** The shared object walker aimed at the front document again (O7) - after every run of ours that aimed it elsewhere.
	    No front document: nothing is done. */
	void AimSharedWalkerAtFront();

	/** The walker options the Object tab's settings make for one document (O4): its five Include switches and its Type;
	    kSelection over `items` when there are any, kDocument otherwise. */
	ObjectWalkerScopeOptions WalkerOptionsFor(const UIDRef& docRef, const UIDList* items);

	/** The session's SHARED object walker (kObjectWalkerServiceProviderBoss) - the one InDesign's object search walks. */
	IObjectWalker* QuerySharedWalker();

	/** InDesign's find/change service (kFindChangeServiceBoss - IFindChangeService has no kDefaultIID). */
	IFindChangeService* CreateFindChangeService();

	/** One object row for one page item (O8): its kind and what it holds, its page and where it stands, hidden / locked,
	    its fingerprint. The locator is built later, with the chapter's page order (KFCResultModel::OrderHitsByPage). */
	void BuildObjectHit(const UIDRef& docRef, UID item, KFCResultModel::Hit& out);
}

#endif // __KFCObjectSearch_h__
