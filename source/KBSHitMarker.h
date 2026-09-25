//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The jump marker: the inversion laid over a jumped-to hit's characters for about a second.
//
//  ***** A GLOBAL TEXT ADORNMENT, NOT A DRAW EVENT (2026-09-26). ***** Until then the marker was a
//  rectangle in pasteboard coordinates, worked out by hand from the hit's first wax line and painted
//  on the spread front (KBSDrawEventHandler, now gone). That rectangle had to be right for vertical
//  text, rotated frames, threaded frames and table cells separately, and it only ever covered the
//  FIRST line of a match. An adornment is handed each wax run as the text engine draws it, so the
//  marker lands on the characters themselves in every one of those cases, and on every line of a
//  match that runs over several. Same as KCM's Story-mode marker (KCMStoryMarker.h), which was
//  built first (user's instruction, 2026-08-20).
//
//  ***** THE LOOK IS UNCHANGED: WHITE OVER DIFFERENCE, AN INVERSION. ***** KCM moved its marker to a
//  coloured wash because an inversion cannot be printed; this one is never printed, and on screen
//  the inversion is what keeps the marker visible on any ground (KCMStoryMarker.cpp records it as
//  "right for the screen").
//
//  Screen only: never when printing or exporting (IShape::kPrinting). Shown in every screen mode -
//  Overprint Preview included (user's call, 2026-09-26; the Draw Event marker hid itself there) and
//  the screen's Preview mode (IShape::kPreviewMode) - the user's call of
//  2026-07-31, kept on 2026-09-26 although IGlobalTextAdornment.h:78-83 asks adornments that do
//  not print to stay out of preview as well: the marker is a pointer for navigation, not artwork.
//
//========================================================================================

#ifndef __KBSHitMarker_h__
#define __KBSHitMarker_h__

#include "BaseType.h"
#include "TextID.h"		// TextIndex
#include "UIDRef.h"		// UID

class IDataBase;

namespace KBSHitMarker
{
	/** Put the marker on [start, end) of one story and start its countdown (about a second -
		KBSMarkerExpiryIdleTask). Replaces whatever marker was up, and repaints the document it was
		in when that is another one. start == end is a zero-width hit (GREP's ^, say), shown as a
		thin bar. Ignored after ShutdownCleanup. */
	void SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end);

	/** Take the marker down now, and repaint its document. Safe when there is none. */
	void ClearMarker();

	/** A document is closing: if the marker is in it, forget it WITHOUT repainting (the document is
		on its way out, and a repaint would reach into it). The address is compared, never read. */
	void ForgetDoc(IDataBase* db);

	/** Application shutdown: forget the marker without repainting anything, and refuse every call
		after this one. */
	void ShutdownCleanup();
}

#endif // __KBSHitMarker_h__
