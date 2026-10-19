//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE OBJECT TAB, LISTED (docs/superpowers/specs/2026-10-09-kfc-object-search-design.md). Edit > Find/Change's
//  Object tab - Find Object Format, its object style, its Type and its five Include switches - searched by InDesign's own
//  object search (IFindChangeService::SearchObject), one document at a time: the service takes no document and walks
//  whatever the SHARED object walker (kObjectWalkerService) was last initialised for, so the walker is aimed at each
//  document first (spec O2 - measured in KT: a windowless chapter is searched that way, nothing primed).
//  What it finds is read off the shared walker (GetCurrentItem): the service's own OUT argument stays empty (O5).
//
//  WHAT THE SEARCH COSTS THE USER, AND WHAT IS GIVEN BACK. The service SELECTS what it finds - in the front document
//  even when it searched a windowless one, where it selects the item of the same number (measured). So a listing takes
//  the text walker's selection snapshot first and gives it back at the end (O6) - the selections in the windows behind
//  the front view, and an empty selection, too (WalkAftercare, in the .cpp) - NEVER around a replace: given back after
//  ReplaceObject, the snapshot brought InDesign down (KT). And the shared walker is aimed at the front document again
//  when a run of ours is over (O7): it would go on naming a chapter the run has closed, for Edit > Find/Change's next
//  search.
//
//========================================================================================

#ifndef __KFCObjectSearch_h__
#define __KFCObjectSearch_h__

#include "IFindChangeService.h"	// FindChangeResult - ReadWalkStep
#include "PMString.h"
#include "UIDList.h"
#include "UIDRef.h"
#include "ObjectWalkerScopeOptions.h"

#include <set>
#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCResultModel.h"		// Hit - an object row
#include "KFCSearchEngine.h"	// RunScope
#include "KFCNarrow.h"			// Filter - a narrowed list's conditions (Narrow Down)

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
	    open dialog shows (measured - the object search plan's Task 1 M6, docs/superpowers/plans/2026-10-09-kfc-object-search.md).
	    An unset one reads as Document; Story / To End of Story read
	    as Document and say so in outFellBackNote (empty otherwise). Selection is answered whatever is selected - a run
	    with no page item selected is refused by ResolveObjectRunScope. -1 when the settings cannot be read. */
	int32 ObjectSearchScope(PMString& outFellBackNote);

	/** KFCSearchEngine::ResolveRunScope's doors and sentences, with ObjectSearchScope's reading of Search: - and one
	    door of its own: Search: = Selection with nothing selected - no page item, and no cursor in text - is refused, as
	    InDesign's own Find walks nothing there (and its object search fails a walk aimed at the document while Selection
	    is stored - measured). */
	bool ResolveObjectRunScope(KFCSearchEngine::RunScope& out, PMString& outRefusal);

	/** SEARCHBOOK'S WALK FOR THE OBJECT TAB: every target walked under one bar, a book's chapters opened and handed back
	    one at a time, each target's rows filed as the text tabs' are (KFCSearchEngine::FileChapter - a chapter in page
	    order, and the tally the text tabs' walk fills, KFCSearchEngine::CollectTally). The user's selection is given back at
	    the end (O6) and the shared walker aimed at the front document (O7). selectionScope = RunScope::selectionScope.
	    CANCEL IS HEARD INSIDE A DOCUMENT (the author's call): the bar is moved and asked at every item
	    the walk lists, not only between documents; a Cancel ends the run as one between documents does (out.cancelled). */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
		int32 selectionScope, std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out,
		int32 reserved,			// rows the list holds before the walk - a search again's kept rows (the row eye); 0 otherwise
		const KFCNarrow::Filter* narrow = nil);	// a narrowed list's conditions (Narrow Down): only the items that meet them,
								// dropped before the list's limit counts them; nil = everything found

	/** SEARCH THIS DOCUMENT AGAIN on the Object tab (KFCSearchEngine::SearchDocumentAgain): ONE open document
	    walked whole as CollectTargets walks each of its targets, under a bar of its own ("Document 1 / 1 - <name>"), the
	    user's selection given back and the shared walker aimed at the front after (O6 / O7). Its rows come back in
	    outHits (not in the model) - at most `limit` (outCapped when there were more). true = the walk ended cleanly;
	    false = it broke off, or Cancel was pressed (outCancelled) - the rows are not to be used then. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
		std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled,
		const KFCNarrow::Filter* narrow = nil);	// a narrowed list's conditions (CollectTargets'); nil = all

	/** AN OBJECT ROW FOR AN ITEM (its text, its place, its fingerprint): the row the object search lists for each item
	    it finds, and what Frames of These Matches makes of the frame holding a match (KFCSearchEngine's walk - the
	    frame KFCNarrow::FrameOwner names). */
	void BuildRow(const UIDRef& docRef, UID item, KFCResultModel::Hit& out);

	/** An item's FINGERPRINT (O12): its own persistent data as the database writes it (IPMPersist::SaveAll into memory),
	    with what it holds - a group's members, a graphic frame's image - its anchored settings when it hangs in text,
	    and a text frame's (or a path's) story's version (ITextModel::GetChangeCount): one 64-bit hash and a length.
	    Moving, restyling, relinking or typing into it changes it; recomposing, zooming or selecting does not, and an Undo
	    puts it back exactly (measured - the SaveAll probe, work/note-scripts/2026-10-10-kfc-saveall-probe/;
	    it took the place of the item's snippet, which was 20 to 100 times slower and crashed InDesign on an inline in
	    overset text). false = nothing of the item could be read. */
	bool Fingerprint(const UIDRef& item, uint64& outHash, uint32& outLength);

	/** WHAT A WRITE OF KFC'S OWN CAN MOVE BESIDES THE ITEMS IT WRITES (spec OBJ-12). A fingerprint holds more
	    than its item: a group's holds its members, a text frame's (and a path's) its story's version - and a change to an
	    inline, an anchored item or a graphic cell's item moves the version of the story it hangs in (its table's). So a
	    write can move the print of an item holding one it writes, of an item one it writes holds, and of a text frame or
	    path showing a story one it writes shows or hangs in. ReachOfWrite reads that around the items to be written,
	    before the write; WriteReaches asks it of one item. */
	struct WriteReach
	{
		std::set<UID>	written;	// the items to be written
		std::set<UID>	items;		// ...and every item holding one of them (IHierarchy, up to the top)
		std::set<UID>	stories;	// the stories they show, are composed in, or are placed in (in text, or in a table)
	};
	void ReachOfWrite(IDataBase* db, const std::set<UID>& toWrite, WriteReach& out);
	bool WriteReaches(IDataBase* db, const WriteReach& reach, UID item);

	/** Does a spread hold the item now - is it laid out (IPasteboardUtils::QuerySpread)? No for an item in text that is
	    not composed - an inline in OVERSET text: nothing of it can be shown or selected, and its row's Replace is refused
	    (found when InDesign's snippet export, the fingerprint of the time, crashed on one:
	    work/kfc-crash-2026-10-10-overset-inline.xml). */
	bool IsOnSpread(const UIDRef& item);

	/** WHERE AN OBJECT ROW'S ITEM STANDS, as its row reads it (O8): its page (Pages-panel style) and its place in the
	    document's page order, on the pasteboard or a parent page, in a group, inline or anchored, overset, hidden, locked -
	    io's fields of those, set whole. The search reads it as it builds a row; a special operation of KFC's own reads it
	    again after its write (KFCObjectSpecial - a move can take the item to another page, Lock and Hide change its words).
	    false = no such item (io as it was). */
	bool ReadItemPlace(const UIDRef& docRef, UID item, KFCResultModel::Hit& io);

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

	/** HOW ONE STEP OF A WALK ENDED - SearchObject's answer read with the global error state, the one reading every walk of
	    ours makes (the search's WalkDoc, a Replace's walk to its rows - KFCObjectReplace): kWalkStepOn = on an item
	    (kSuccess); kWalkStepEnd = InDesign has no more (kNotFound, kFoundCompleted); kWalkStepBroke = any other answer,
	    and ANY answer with the error state left raised - nothing more is processed over it (a command processed while it
	    stands brings InDesign's protective shutdown - CmdUtils.h, ProcessCommand). The error state is left as it is: the
	    search clears it, a Replace ends its step by it. (The text Replace tells its walk's error from "not found" the
	    same way - its walkFailed.) */
	enum WalkStep { kWalkStepOn = 0, kWalkStepEnd, kWalkStepBroke };
	WalkStep ReadWalkStep(IFindChangeService::FindChangeResult result);
}

#endif // __KFCObjectSearch_h__
