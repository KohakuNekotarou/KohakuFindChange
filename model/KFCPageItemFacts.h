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
	    spread name can be read for it. */
	bool GetFramePageString(const UIDRef& docRef, UID frameUID, PMString& outPage, int32& outPageIndex);

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

	/** THE DOCUMENT LAYER AN ITEM IS ON - the Layers panel's row: its spread layer's (a group's member, the group's), and
	    for an item in text its anchor frame's (the hidden / locked tests above read it the same way). kInvalidUID when it
	    cannot be told (an inline in overset text). */
	UID DocLayerOf(IDataBase* db, UID item);

	/** A document's layer of that name - the one ILayerList::FindByName finds (Move to Active Layer's, and Find Objects on
	    Active Layer's in each document). kInvalidUID = none, or no name. */
	UID DocLayerNamed(const UIDRef& docRef, const PMString& name);
}

#endif // __KFCPageItemFacts_h__
