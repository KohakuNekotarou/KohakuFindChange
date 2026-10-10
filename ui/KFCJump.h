//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Jump-to-hit navigation. A hit-row click (ActivateNode) jumps: it resolves the hit's stored
//  location (reopening the chapter windowless if the user closed it), brings that chapter's window
//  to the front, scrolls the view so the match is centred, and SELECTS the match with the Type tool
//  on (1.4.0 - the author's calls of 2026-10-09 and -10: the tree does what Edit > Find/Change's
//  Find Next does, so the selection is the pointer and no marker goes over it). The keyboard stays
//  on the list. A match that cannot be selected - locked, hidden, zero width, no longer found -
//  gets the marker on its characters instead (KFCHitMarker - a global text adornment), which takes
//  itself down after about a second, and nothing is left selected; until 1.4.0 every click did only
//  that, and changed nothing in the document (JMP-06). Why it was not selected is said (the author's
//  call of 2026-10-10): the row's note on the message area, or a GREP row's preview heading.
//  A double click is two clicks (1.4.0 - the author's call of 2026-10-10: its own selection, which
//  gave the keyboard to the text, went with the click's). THE MARKER COMES UP AT ONCE (the author's
//  call: on the same beat as KCM's Story-mode jump, which raises its flash straight away). With
//  "Hide Previous Chapter" ON, every other
//  displayed clean chapter of the searched book is closed as the jump lands (a document outside the
//  book stays - 2026-10-08). Ported from KESCL's jump machinery (KESCL
//  left untouched), simplified to a static snapshot (no match-list navigation, no edit-repair, no
//  reverse mode).
//
//========================================================================================

#ifndef __KFCJump_h__
#define __KFCJump_h__

#include "PMString.h"
#include <vector>

namespace KFCJump
{
	/** The single door every result row goes through: a hit row jumps (front its document, scroll to
	    the match, select the match, or raise the marker on one that cannot be selected - an overset
	    match scrolls to the frame's "+" and is neither), a
	    chapter row shows its document (no scroll, no marker), the book row activates its book (the
	    active book AND its tab in the book panel; a book closed since the search is not reopened).
	    Called by the row click and by the keyboard walk, which is why it exists - two callers must not
	    drift apart - and it holds the "one activation at a time" guard, which is why the three are in
	    KFCJump.cpp and not here (JumpToHit, ShowChapter, ShowBook).
	    @param chapterIdx the chapter index, or -1 for the book row.
	    @param hitIdx the hit index, or -1 when the row is not a hit row. */
	void ActivateNode(int32 chapterIdx, int32 hitIdx);

	/** A Find/Change setting changed (KFCPanelTitle's observer, IID_IFINDCHANGEOPTIONS). If a row was activated a
	    moment ago (ActivateNode - a click, the arrow walk) and Edit > Find/Change has re-picked Search: in answer to
	    the selection or the front document that changed, Search: is put back as it was before the activation, when
	    the selection offers it (1.4.0 - the author's call of 2026-10-10; the note above ScopeKeep in KFCJump.cpp). */
	void KeepSearchScopeAfterClick();

	/** AN OBJECT ROW'S REPLACE (1.4.0, spec O10): the model's checks first (IKFCRuns::CheckObjectReplace - a refused row
	    brings no window forward), then the row's document in front and the view on its item (InDesign's replace selects
	    what it writes in the FRONT document - O10 step 6), the write (IKFCRuns::ReplaceHit), and the row's item left
	    selected as a click selects it (step 9 - the author's call of 2026-10-10; nothing selected when the item is gone).
	    outStatus = what to say. True when it wrote. */
	bool ReplaceObjectRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** THE OBJECT ROWS SELECTED TOGETHER, REPLACED TOGETHER (O18): the model's checks first (IKFCRuns::
	    CheckObjectReplaceRows - none that could be written brings no window forward), then their document in front and
	    the view on the first row's item, the write in one undo step (IKFCRuns::ReplaceObjectRows), and their items left
	    selected as the rows are (quietly - the status says what was written). One row = ReplaceObjectRow. True when
	    something was written. */
	bool ReplaceObjectRows(int32 chapterIdx, const std::vector<int32>& hitIdxs, PMString& outStatus);

	/** THE PAGE FOLLOWS THE OBJECT ROWS SELECTED TOGETHER (O17 - the author's call of 2026-10-10, the Layers panel's
	    multiple selection): their document in front, and on the page exactly their items selected - the SDK's way
	    (SnpSelectShape.cpp: DeselectAll, then SelectPageItems with kReplace), the tool left as it is (a click's rule).
	    Left out, each counted: an item gone (its row reads Missing), a locked or hidden one (the row's flags, as a click
	    reads them), one now on another spread than the rest. shownHit: the row whose item the view is brought to (-1 =
	    the view stays - a row taken away). No rows: nothing selected in their document when it is in front.
	    sayCount: the message area says what was selected ("Selected 3 objects." / "Selected 3 of 5 objects - 1 locked,
	    1 hidden.") - false when the caller has its own sentence there. */
	void SelectObjectRows(int32 chapterIdx, const std::vector<int32>& hitIdxs, int32 shownHit, bool sayCount);

	/** Nothing selected in the front document - DeselectAll on the active selection, when there is one: the click's own
	    clearing (1.4.0), for the object search's "nothing was selected" given back (IKFCUIServices::ClearSelection). */
	void ClearFrontSelection();

	/** The "Hide Previous Chapter" flyout toggle (session state; starts ON). Read as a hit row or a
	    chapter row lands, to decide whether to close the other displayed chapters; the flyout drives it.

	    @note This is the TOGGLE, not the decision. The sweep also needs the results to have come from
	          a BOOK - it is about chapters, and the menu greys the toggle out in document scope for
	          exactly that reason - so the jump asks ShouldHidePreviousChapter, which tests both.
	          Reading this alone would close the user's other documents on a document-scope jump,
	          with a toggle they cannot reach to switch off. */
	bool IsHidePreviousChapterOn();
	void ToggleHidePreviousChapter();

	/** Set the toggle outright. For the saved settings (KFCPanelState.cpp), which restores a
	    remembered value - flipping would come out inverted whenever the saved state matches the
	    default. Nothing else should use it: the flyout toggles. */
	void SetHidePreviousChapter(bool on);
}

#endif // __KFCJump_h__
