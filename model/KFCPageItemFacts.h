//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What a page item's PLACE says about it - its page, whether it is switched off, whether it may be edited - and a
//  story's first words. Shared by the text search (KFCSearchEngine.cpp) and the object search (KFCObjectSearch.cpp), so
//  the two ask the same questions by the same code: a text row and an object row on the same frame say the same page,
//  "hidden" and "locked".
//
//========================================================================================

#ifndef __KFCPageItemFacts_h__
#define __KFCPageItemFacts_h__

#include "PMString.h"
#include "UIDRef.h"

class IDataBase;

namespace KFCPageItemFacts
{
	/** A frame's page, named the way the Pages panel names it (section prefix and all), and its plain document order
	    (for sorting). A frame on no page reads as its spread, which GetPageString spells "PB". false when no page or
	    spread name can be read for it. outOnPasteboard (optional): the UID named is not a page (kPageBoss) - the frame is
	    out on the pasteboard (Narrow Down's On Pasteboard: told by the class, never by the "PB" spelling). */
	bool GetFramePageString(const UIDRef& docRef, UID frameUID, PMString& outPage, int32& outPageIndex,
		bool* outOnPasteboard = nil, UID* outPageUID = nil);	// outPageUID: the page (or spread) named - Narrow Down's Pages

	/** Is this item switched off - its layer hidden, or the item itself (Object > Hide)? */
	bool IsFrameHidden(IDataBase* db, UID frameUID);

	/** Is this item on a locked layer? */
	bool IsFrameOnLockedLayer(IDataBase* db, UID frameUID);

	/** Do this item's own lock flags - or its outermost parent's - refuse an edit (Object > Lock, an insert lock)? */
	bool IsPageItemLockedForEdit(IDataBase* db, UID frameUID);

	/** A story's first 24 visible characters, its breaks kept - what a story row reads. */
	PMString StoryLeadText(const UIDRef& storyRef);

	/** An item's place in its parent's stack - its index among the parent's children, the back first (Object > Arrange
	    moves it; nothing of the item's own data does - KFCObjectSearch::Fingerprint leaves it out). -1 = no item, or no
	    parent. */
	int32 StackIndex(IDataBase* db, UID item);

	/** IS THE ITEM IN ITS DOCUMENT - its UID valid there and a page item (it has a hierarchy). A write that takes an
	    item away (Delete, Create Outlines) leaves its UID unused; an Undo puts it back. */
	bool ItemIsThere(IDataBase* db, UID item);

	/** THE DOCUMENT LAYER AN ITEM IS ON - the Layers panel's row: its spread layer's (a group's member, the group's), and
	    for an item in text its anchor frame's (the hidden / locked tests above read it the same way). kInvalidUID when it
	    cannot be told (an inline in overset text). */
	UID DocLayerOf(IDataBase* db, UID item);

	/** A document's layer of that name - the one ILayerList::FindByName finds (Move to Active Layer's, and Narrow Down's
	    On Active Layer's in each document). kInvalidUID = none, or no name. */
	UID DocLayerNamed(const UIDRef& docRef, const PMString& name);

	/** THE SPREAD AN ITEM STANDS ON NOW, ASKED OF THE ITEM (IPasteboardUtils::QuerySpread - IsOnSpread asks it too):
	    kInvalidUID for an item on none - an inline in overset text. A text row's frame is asked it, an object row's
	    item, and a row's click the same (the UI half's SpreadOfItem in KFCJump.cpp). NOT THE SHARED OBJECT WALKER'S
	    IObjectWalker::GetCurrentSpread - IT BROUGHT InDesign DOWN (the author's Find Objects on Active Layer;
	    reproduced): a walker initialized for Search: = Selection walks the items it was given and puts no document in
	    its document list, while GetCurrentSpread reads that list's first entry whatever the scope (TEXT WALKER.RPLN -
	    Initialize +0xb9cd0, GetCurrentSpread +0xb9110, read in the disassembly). The list keeps the storage a document
	    walk gave it, so a selection walk read a stale entry - until the session's FIRST object walk was a selection's,
	    with no storage yet: a read of address 0 (+0xb9151), at the first item it listed. The header promises "the
	    spread being walked" and says nothing of the scope. */
	UID SpreadOf(const UIDRef& item);

	/** IS THE SPREAD HIDDEN (the Pages panel's Hide Spread)? An IBoolData on kSpreadBoss, IID_IHIDESPREADBOOLDATA, true
	    = hidden (SpreadID.h - KCM reads it so, KCMIsPageOnHiddenSpread; the UI half's SpreadIsHidden in KFCJump.cpp the
	    same). A parent spread answers no: InDesign has no way to hide one. False for kInvalidUID. */
	bool IsSpreadHidden(IDataBase* db, UID spread);

	/** Does the document hold a hidden spread now (IsSpreadHidden, over its spread list)? */
	bool HasHiddenSpread(const UIDRef& docRef);
}

#endif // __KFCPageItemFacts_h__
