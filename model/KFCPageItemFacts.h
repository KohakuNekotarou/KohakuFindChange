//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What a page item's PLACE says about it - its page, whether it is switched off, whether it may be edited - and a
//  story's first words. Moved out of KFCSearchEngine.cpp for 1.4.0, unchanged, so the object search
//  (KFCObjectSearch.cpp) asks the same questions the text search asks, by the same code: a text row and an object row
//  on the same frame say the same page, "hidden" and "locked".
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
}

#endif // __KFCPageItemFacts_h__
