//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Jump-to-hit navigation (Task 3). A hit-row click asks JumpToHit(chapter, hit) to: resolve the
//  hit's stored location (reopening the chapter windowless if the user closed it), bring that
//  chapter's window to the front, scroll the view so the match is centred, and raise the red
//  marker (KBSDrawEventHandler) which fades itself out. It does NOT select the text - it points at
//  the match, VS-style. With "Hide Previous Chapter" ON, every other displayed clean document is
//  closed as the jump lands. Ported from KESCL's jump machinery (KESCL left untouched), simplified
//  to a static snapshot (no match-list navigation, no edit-repair, no reverse mode).
//
//========================================================================================

#ifndef __KBSJump_h__
#define __KBSJump_h__

namespace KBSJump
{
	/** Jump to hit 'hitIdx' of chapter 'chapterIdx' in KBSResultModel: front its document, scroll
	    to the match, raise the marker. Does not select. No-op on a bad index; an unreachable chapter
	    (missing / locked file) reports through the status line. An overset match has no on-page
	    location of its own, so the view scrolls to the frame's overset "+" instead and NO marker is
	    raised - those pixels belong to the indicator, not to the text. */
	void JumpToHit(int32 chapterIdx, int32 hitIdx);

	/** Show chapter 'chapterIdx': bring its document to the front, reopening it windowless first if
	    the user closed it since the search. Does NOT scroll and raises no marker - a chapter row
	    names a document, not a place inside one. Honours "Hide Previous Chapter". No-op on a bad
	    index; an unreachable chapter reports through the status line. */
	void ShowChapter(int32 chapterIdx);

	/** Activate the book the results came from: make it IBookManager's current active book AND
	    bring its tab to the front in the book panel - two separate things that do not follow each
	    other. A book that has been closed since the search is NOT reopened; the status line says
	    so. No-op for a document-scope result, which has no book row. */
	void ShowBook();

	/** The single door every result row goes through: a hit row jumps, a chapter row shows its
	    document, the book row activates its book. Called by the row click and by the keyboard
	    walk, which is why it exists - two callers must not drift apart.
	    @param chapterIdx the chapter index, or -1 for the book row.
	    @param hitIdx the hit index, or -1 when the row is not a hit row. */
	void ActivateNode(int32 chapterIdx, int32 hitIdx);

	/** The "Hide Previous Chapter" flyout toggle (session state; starts ON). Read by JumpToHit to
	    decide whether to close other displayed chapters as a jump lands; the flyout drives it. */
	bool IsHidePreviousChapterOn();
	void ToggleHidePreviousChapter();
}

#endif // __KBSJump_h__
