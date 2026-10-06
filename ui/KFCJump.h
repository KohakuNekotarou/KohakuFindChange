//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Jump-to-hit navigation. A hit-row click (ActivateNode) jumps: it resolves the hit's stored
//  location (reopening the chapter windowless if the user closed it), brings that chapter's window
//  to the front, scrolls the view so the match is centred, and raises the marker on the match's
//  characters (KFCHitMarker - a global text adornment) which takes itself down after about a
//  second. It does NOT select the text - it points at the match, VS-style.
//  A DOUBLE click does: SelectHitText switches to the Type tool and highlights the match, for when
//  pointing is not what was wanted. The two are deliberately different - a single click can be spent
//  freely because it changes nothing in the document, and that is only true while it does not
//  select. THE MARKER COMES UP AT ONCE, FROM BOTH DOORS (the author's call: on the same beat as
//  KCM's Story-mode jump, which raises its flash straight away) - not booked for the double-click
//  interval; a double click's selection takes it back down (SelectHitText ends by taking the
//  marker down). With "Hide Previous Chapter" ON, every other
//  displayed clean document is closed as the jump lands. Ported from KESCL's jump machinery (KESCL
//  left untouched), simplified to a static snapshot (no match-list navigation, no edit-repair, no
//  reverse mode).
//
//========================================================================================

#ifndef __KFCJump_h__
#define __KFCJump_h__

namespace KFCJump
{
	/** The single door every result row goes through: a hit row jumps (front its document, scroll to
	    the match, raise the marker - an overset match scrolls to the frame's "+" and is not marked), a
	    chapter row shows its document (no scroll, no marker), the book row activates its book (the
	    active book AND its tab in the book panel; a book closed since the search is not reopened).
	    Called by the row click and by the keyboard walk, which is why it exists - two callers must not
	    drift apart - and it holds the "one activation at a time" guard, which is why the three are in
	    KFCJump.cpp and not here (JumpToHit, ShowChapter, ShowBook).
	    @param chapterIdx the chapter index, or -1 for the book row.
	    @param hitIdx the hit index, or -1 when the row is not a hit row. */
	void ActivateNode(int32 chapterIdx, int32 hitIdx);

	/** SELECT the match in the document: switch to the Type tool and highlight the hit's own range,
	    so the user can edit or copy it straight away. The DOUBLE-CLICK half of a hit row - a single
	    click still only POINTS at the match (KFCJump's whole design; see the note at the head of this
	    header), and this is the extra step that says "and put me in it".

	    Assumes the jump has already run for this row - the double-click sequence TRIES it on the first
	    click, and this refuses, without a word, when the hit's layout window is not the one in front
	    (the jump could not bring it forward, or was dropped while an earlier landing was still opening
	    a chapter). It does NOT scroll (Selection::kDontScrollSelection), because the
	    jump's own centring is better than what scroll-into-view would do, and it does not front the
	    window again. On success it TAKES THE JUMP'S MARKER BACK DOWN - the inverted rectangle and the
	    selection say the same thing, and together they make the text unreadable.

	    Refuses when there is nothing honest to select - a row with no place left ("deleted"), a LOCKED
	    or HIDDEN match, a zero-width one, an OVERSET one, and one whose text is no longer what the
	    search recorded (a stale range would highlight text the user never searched for). Each says why
	    on the status line, except the stale one: the jump has already said it. The tests in
	    SelectHitText are the list.

	    @param chapterIdx the chapter index.
	    @param hitIdx the hit index.
	    @return kTrue if a text selection was actually made. */
	bool SelectHitText(int32 chapterIdx, int32 hitIdx);

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
