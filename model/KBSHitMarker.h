//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The jump marker: the inversion laid over a jumped-to hit's characters for about a second.
//
//  A GLOBAL TEXT ADORNMENT, NOT A DRAW EVENT. A rectangle in pasteboard coordinates, worked out by
//  hand from the hit's first wax line and painted on the spread front, has to be right for vertical
//  text, rotated frames, threaded frames and table cells separately, and covers only the FIRST line
//  of a match. An adornment is handed each wax run as the text engine draws it, so the marker lands
//  on the characters themselves in every one of those cases, and on every line of a match that runs
//  over several. Same as KCM's Story-mode marker (KCMStoryMarker.h), which was built first (the
//  author's instruction).
//
//  THE LOOK: WHITE OVER DIFFERENCE, AN INVERSION. KCM moved its marker to a
//  coloured wash because an inversion cannot be printed; this one is never printed, and on screen
//  the inversion is what keeps the marker visible on any ground (KCMStoryMarker.cpp records it as
//  "right for the screen").
//
//  Screen only. Shown in every screen mode - Normal, Preview and Overprint Preview (the author's calls:
//  the marker is a pointer for navigation, not artwork).
//  Not on paper or in an export (IShape::kPrinting) - and NOT IN A PAGE DRAWN AS A PICTURE:
//  kPreviewMode WITH NO VIEW. Measured on the running
//  application with the marker's own trace (KBS_DIAG): the screen draws with a view in every mode -
//  Normal 0x800, Preview / Bleed / Slug 0x1000 (kPreviewMode), Overprint Preview 0x1100 - and a page
//  picture (SnapshotUtilsEx::Draw(IShape::kPreviewMode): KIDMCP's page pictures, KCM's comparison rasters;
//  the Pages panel's thumbnail is the same kind of draw - draw-event-pdf-export-experiment-2026-08-12.md)
//  comes as 0x1000 with NO view. Without that test such a picture taken in the second the marker is up
//  carries it (measured: 910 pixels at the hit, 72 dpi) - a thumbnail could keep it after it went, and
//  KCM could mark a change nobody made. kPreviewMode alone cannot tell the two apart, so the view does - the test KCM and
//  KIDMCP use for the thumbnail. IGlobalTextAdornment.h:78-83 asks an adornment that does not print to
//  stay out of kPreviewMode altogether; that would take the marker out of the screen's preview modes,
//  against the author's calls above, so this stays the one exception, narrowed to the screen.
//  (Measured wrong once on the way: a test script set the screen mode by a name that does not exist, the
//  assignment threw unseen, and "Preview" was measured in Normal - read the mode back before trusting it.)
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
	/** Put the marker on [start, end) of one story, replacing whatever marker was up. start == end is a
		zero-width hit (GREP's ^, say), shown as a thin bar.
		THE MODEL HALF DRAWS NOTHING (the model/UI split). Repainting the views and the countdown that
		takes the marker down again (about a second) are the UI half's - KBSHitMarkerView, which calls
		this.
		@param outPreviousDB the document the marker was in before when that is ANOTHER one (it has to be
			repainted too), nil otherwise.
		@return false when nothing was set - after ShutdownCleanup, or with no document or story - and then
			nothing is to be repainted or counted down. */
	bool SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB);

	/** Take the marker down. Safe when there is none.
		@param outDB the document it was in (to be repainted by the caller), nil when there was none.
		@return false after ShutdownCleanup, when nothing at all is to be done. */
	bool ClearMarker(IDataBase*& outDB);

	/** A document is closing: if the marker is in it, forget it WITHOUT repainting (the document is
		on its way out, and a repaint would reach into it). The address is compared, never read. */
	void ForgetDoc(IDataBase* db);

	/** Application shutdown: forget the marker without repainting anything, and refuse every call
		after this one. */
	void ShutdownCleanup();
}

#endif // __KBSHitMarker_h__
