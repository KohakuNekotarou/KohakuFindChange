//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  ONE TEXT FOCUS PER RESULT ROW, OF KFC'S OWN CLASS (KFCRowFocus.h): InDesign moves it when text is typed or deleted
//  around it, and - unlike InDesign's own focus - the user's Undo and Redo put it back
//  (docs/superpowers/plans/2026-10-14-kfc-row-focus-undo.md, measured in KT: docs/ai-notes/text-focus-undo-spike-2026-10-14.md).
//  So a row's focus stands where its text is through any edit and any Undo, but one: a focus that met an Undo or a Redo
//  it could not follow (an edit older than the focus, undone) is LOST (KFCRowFocus::IsLost) and never trusted again.
//  KFCSearchEngine::FollowStoryFoci moves the rows onto their foci before a Replace, a preview and a jump - the author's
//  call of 2026-10-14 night, which overturned F14 of docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md
//  ("a row's Replace never reads the focus"). The focus was a hint for the jump only before (2026-10-08 -
//  docs/superpowers/specs/2026-10-08-kfc-text-focus-jump-design.md); the jump's reading of a focus that is not followed
//  (KFCSearchEngine::LocateRow) is still that hint's.
//  Attached only where KFC still knows the story (its version unchanged); put back on the rows' places after KFC's own
//  writes - inside the write's step, so its Undo and Redo hold it; released before its document closes, its target
//  database cleared first. The main thread only.
//
//========================================================================================

#ifndef __KFCRowFoci_h__
#define __KFCRowFoci_h__

#include "BaseType.h"	// TextIndex
#include "UIDRef.h"		// UID

#include <memory>		// std::shared_ptr - a tier's held foci

class IDataBase;

namespace KFCRowFoci
{
	/** Every row of the chapter that has none gets a focus on its stored place - when the chapter's document is open
	    (asked BY FILE - KFCSearchEngine::ChapterDocNow) and the row's story is at the version KFC recorded for it. */
	void AttachChapter(int32 chapterIdx);

	/** AttachChapter for every chapter of the results (after a search, after a list is put back by an Undo). */
	void AttachOpenChapters();

	/** The row's focus range now - false when it has none in that database, or it is not usable any more (its
	    document, its story's manager, its model) - then it is let go. A lost focus answers too: this is the jump's hint. */
	bool Current(int32 chapterIdx, int32 hitIdx, IDataBase* db, TextIndex& outStart, TextIndex& outEnd);

	/** What a row's focus can say (Where). */
	enum FocusState { kNoFocus, kLostFocus, kFocused };

	/** The row's focus in that database and its range now: kFocused = it has followed every edit and every Undo since it
	    was made - the row's text is there or was edited there; kLostFocus = it met an Undo it could not follow (the range
	    is no answer); kNoFocus = none there (or no longer usable - let go, as Current does). */
	FocusState Where(int32 chapterIdx, int32 hitIdx, IDataBase* db, TextIndex& outStart, TextIndex& outEnd);

	/** The row's focus put on the row's STORED place (SetRange) - nothing when it has none or is there already.
	    inStep = inside a write's step (KFCReplaceEngine): marked first (PreDirtyFocus), so the step holds the move. */
	void Reanchor(int32 chapterIdx, int32 hitIdx, bool inStep = false);

	/** The row's focus put on [start, end) - a place a jump found it at, which is not stored in the row (spec T3).
	    inStep as Reanchor's. Outside a step only a lost focus, or one already there, is moved in practice: an Undo after
	    a move made outside a step reads back the range of before it (KT, S7). */
	void MoveTo(int32 chapterIdx, int32 hitIdx, TextIndex start, TextIndex end, bool inStep = false);

	/** Reanchor for every row of that chapter and story that has a focus - and the story's searched part's focus made,
	    or put back, on its recorded place (when the chapter's document is open and the story is at the version that
	    place was taken at). inStep as Reanchor's. */
	void ReanchorStory(int32 chapterIdx, UID story, bool inStep = false);

	// THE SEARCHED PART'S FOCUS (a search over part of a story - KFCResultModel::SearchedRange): one per story holding a
	// row, made and let go with the rows' foci (AttachChapter, DetachChapter, DetachDocument, DetachAll), of the same
	// class. Search This Story Again reads the text just outside it before believing it (KFCSearchEngine).

	/** The part's focus range now - false when it has none in that database, or it is not usable any more. */
	bool CurrentSearchedRange(int32 chapterIdx, UID story, IDataBase* db, TextIndex& outStart, TextIndex& outEnd);

	/** The chapter's foci let go (target cleared, RemoveFocus, Release). */
	void DetachChapter(int32 chapterIdx);

	/** Every focus on that document let go - before it closes. */
	void DetachDocument(IDataBase* db);

	/** Every focus let go (the rows are gone). */
	void DetachAll();

	// THE FOCI OF A LIST PUT ASIDE AS A TIER (the author's call, 10-14 night: "a grey tier's rows keep their foci too"):
	// held while the list is grey, so InDesign goes on moving them with every edit - KFC's own writes from another tier
	// among them - and the user's Undo and Redo put them back, and given back when the tier is switched to (KFCTierStore).
	// Let go when the last holder goes (the cap, Clear Results, Clear These Results, the shutdown) and, for a closing
	// document, before it closes (LetDocumentOf).
	struct Held;
	typedef std::shared_ptr<Held> HeldPtr;

	/** The live list's foci - its rows' and its searched parts' - moved into a held set: nothing let go, and the live list
	    holds none after it. nil when it held none. */
	HeldPtr HandOver();

	/** A held set made the live list's again (the list itself put back first - KFCResultModel::RestoreModelSnapshot): each
	    held focus takes its row's place, one the live list attached for the same row let go first; `held` is emptied. */
	void TakeBack(HeldPtr& held);

	/** A closing document's foci in a held set let go - while the document still stands (the close responder). */
	void LetDocumentOf(const HeldPtr& held, IDataBase* db);
}

#endif // __KFCRowFoci_h__
