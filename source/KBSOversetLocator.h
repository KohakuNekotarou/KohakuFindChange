//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Locate the overset "+" indicator for an overset text position. InDesign exposes no public API
//  for the red "+" (frame) / red dot (table cell) overset locator, so we approximate it: the "+"
//  sits at the outport (bottom-right, horizontal text) of the LAST frame the thread is placed in.
//  Shared by two callers so the geometry is computed one way only:
//    * KBSJump  - scrolls the view to the "+" point (no marker) when a jumped-to hit is overset.
//    * KBSSearchEngine - names the page the "+" sits on so an overset hit lists as "P<page>(n) overset"
//      and sorts into that page instead of being pushed to the end.
//
//  When the position's own thread has nothing placed (a table or one of its rows is pushed out of
//  its frame, so the cell itself is gone), the walk climbs the table-anchor chain out of any nested
//  tables until an ancestor thread IS placed - ultimately the main frame's "+".
//
//========================================================================================
#ifndef __KBSOversetLocator_h__
#define __KBSOversetLocator_h__

#include "BaseType.h"		// TextIndex
#include "UIDRef.h"			// UID / kInvalidUID
#include "PMPoint.h"		// PBPMPoint

/** Where the overset "+" locator for a text position is. 'found' is false when nothing in the
    thread (or any enclosing thread) is placed, so there is no on-page location to point at. */
struct KBSOversetLoc
{
	bool		found;		// true if an outport location was resolved
	UID			frameUID;	// the frame carrying the "+" (for naming its page)
	PBPMPoint	outportPb;	// the "+" point in pasteboard coordinates (for scrolling)

	KBSOversetLoc() : found(false), frameUID(kInvalidUID), outportPb(0.0, 0.0) {}
};

/** Resolve the overset "+" locator for text position 'pos' in 'storyRef'. See file header. */
KBSOversetLoc KBSFindOversetLocator(const UIDRef& storyRef, TextIndex pos);

#endif // __KBSOversetLocator_h__
