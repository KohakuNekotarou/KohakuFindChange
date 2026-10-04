//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The jump marker as the UI half shows it (the model/UI split). The marker itself - where it
//  is, and the text adornment that draws it - is the model half's (KFCHitMarker.h): an adornment is drawn
//  by the text engine wherever text is drawn, which is what a mark meant for print and PDF will need. What
//  is left here is the user interface around it: repainting the views so it appears and disappears at
//  once, and the countdown (KFCMarkerExpiryIdleTask) that takes it down after about a second - a pointer
//  for navigation, not a highlight.
//
//========================================================================================

#ifndef __KFCHitMarkerView_h__
#define __KFCHitMarkerView_h__

#include "BaseType.h"
#include "TextID.h"		// TextIndex
#include "UIDRef.h"		// UID

class IDataBase;

namespace KFCHitMarkerView
{
	/** Put the marker on [start, end) of one story (KFCHitMarker::SetMarker), repaint the document it is
		in - and the one it was in before, when that is another - and start its countdown. */
	void Show(IDataBase* db, UID storyUID, TextIndex start, TextIndex end);

	/** Take the marker down now (KFCHitMarker::ClearMarker), stop its countdown and repaint its document.
		Safe when there is none. */
	void Hide();
}

#endif // __KFCHitMarkerView_h__
