//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Locate the overset "+" indicator for an overset text position. InDesign exposes no public API
//  for the red "+" (frame) / red dot (table cell) overset locator, so we approximate it: the "+"
//  sits at the outport (bottom-right, horizontal text) of the LAST frame the thread is placed in.
//  Shared by three call sites so the geometry is computed one way only (named, so a new one has
//  somewhere to be added):
//    * KBSJump's JumpToHit (the UI half, through IKBSRuns::FindOversetLocator) - scrolls the view to
//      the "+" point (no marker) when a jumped-to hit is overset.
//    * KBSSearchEngine's BuildHit - names the page the "+" sits on so an overset hit lists as
//      "P<page>(n) overset" and sorts into that page instead of being pushed to the end.
//    * KBSSearchEngine::EditableFrameForMatch - answers WHICH FRAME speaks for an overset match, so
//      the editable / locked-layer test asks about the frame carrying the "+" - and with it whether
//      the row gets a check box and whether the replace refuses it.
//
//  When the position's own thread has nothing placed (a table or one of its rows is pushed out of
//  its frame, so the cell itself is gone; a footnote whose reference character is overset, so it was
//  never placed - 2026-10-02, B8-1), the walk climbs out - the table's anchor, the footnote's
//  reference - through any nesting until an ancestor thread IS placed: ultimately the main frame's "+".
//
//========================================================================================
#ifndef __KBSOversetLocator_h__
#define __KBSOversetLocator_h__

#include "BaseType.h"		// TextIndex
#include "UIDRef.h"			// UID / kInvalidUID
#include "PMPoint.h"		// PBPMPoint
#include "KBSModelTypes.h"	// KBSOversetLoc

// (KBSOversetLoc - where the "+" is: KBSModelTypes.h, since 2026-10-01 - the UI's jump reads it.)

/** Resolve the overset "+" locator for text position 'pos' in 'storyRef'. See file header. */
KBSOversetLoc KBSFindOversetLocator(const UIDRef& storyRef, TextIndex pos);

#endif // __KBSOversetLocator_h__
