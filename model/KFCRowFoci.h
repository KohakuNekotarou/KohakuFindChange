//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  ONE TEXT FOCUS PER RESULT ROW (docs/superpowers/specs/2026-10-08-kfc-text-focus-jump-design.md): InDesign moves a
//  focus when text is typed or deleted around it (ITextFocusManager::NewFocus - SnpManipulateTextModel.cpp's shape),
//  so a row's place survives the user's edits. A HINT FOR THE JUMP ONLY: a focus does not follow an Undo or a Redo
//  (measured, docs/ai-notes/kfc-text-focus-spike-2026-10-08.md), so nothing uses it without reading the text at it
//  first (KFCSearchEngine::LocateRow), and the row's Replace never reads it at all
//  (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F14).
//  Attached only where KFC still knows the story (its version unchanged); put back on the rows' places after every
//  locate and after KFC's own writes; released before its document closes. The main thread only.
//
//========================================================================================

#ifndef __KFCRowFoci_h__
#define __KFCRowFoci_h__

#include "BaseType.h"
#include "TextID.h"		// TextIndex
#include "UIDRef.h"		// UID

#include <memory>		// std::shared_ptr - a tier's held foci

class IDataBase;

namespace KFCRowFoci
{
	/** Every row of the chapter that has none gets a focus on its stored place - when the chapter's document is open
	    (asked BY FILE - KFCBookScope::FindOpenChapterDoc) and the row's story is at the version KFC recorded for it. */
	void AttachChapter(int32 chapterIdx);

	/** AttachChapter for every chapter of the results (after a search, after a list is put back by an Undo). */
	void AttachOpenChapters();

	/** The row's focus range now - false when it has none in that database, or it is not usable any more (its
	    document, its story's manager, its model) - then it is let go. */
	bool Current(int32 chapterIdx, int32 hitIdx, IDataBase* db, TextIndex& outStart, TextIndex& outEnd);

	/** The row's focus put on the row's STORED place (SetRange) - nothing when it has none or is there already. */
	void Reanchor(int32 chapterIdx, int32 hitIdx);

	/** The row's focus put on [start, end) - a place a jump found it at, which is not stored in the row (spec T3). */
	void MoveTo(int32 chapterIdx, int32 hitIdx, TextIndex start, TextIndex end);

	/** Reanchor for every row of that chapter and story that has a focus - and the story's searched part
	    (PlaceSearchedRange). */
	void ReanchorStory(int32 chapterIdx, UID story);

	// THE SEARCHED PART'S FOCUS (a search over part of a story - KFCResultModel::SearchedRange): one per story holding a
	// row, made and let go with the rows' foci (AttachChapter, DetachChapter, DetachDocument, DetachAll). A hint the same
	// way: Search This Story Again reads the text just outside it before believing it (KFCSearchEngine).

	/** The part's focus range now - false when it has none in that database, or it is not usable any more. */
	bool CurrentSearchedRange(int32 chapterIdx, UID story, IDataBase* db, TextIndex& outStart, TextIndex& outEnd);

	/** The part's focus made, or put back, on its recorded place - when the chapter's document is open and the story is
	    at the version that place was taken at; nothing otherwise. */
	void PlaceSearchedRange(int32 chapterIdx, UID story);

	/** The chapter's foci let go (RemoveFocus, Release). */
	void DetachChapter(int32 chapterIdx);

	/** Every focus on that document let go - before it closes. */
	void DetachDocument(IDataBase* db);

	/** Every focus let go (the rows are gone). */
	void DetachAll();

	// THE FOCI OF A LIST PUT ASIDE AS A TIER (the author's call, 10-14 night: "a grey tier's rows keep their foci too"):
	// held while the list is grey, so InDesign goes on moving them with every edit - KFC's own writes from another tier
	// among them - and given back when the tier is switched to (KFCTierStore). Let go when the last holder goes (the cap,
	// Clear Results, Clear These Results, the shutdown) and, for a closing document, before it closes (LetDocumentOf).
	// The same hint as the live list's: they do not follow an Undo or a Redo either.
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
