//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Records what each chapter looked like when the search walked it, so the replace can tell
//  the user - as it opens that chapter - whether it has changed since.
//
//  A same-occurrence test stood in the replace engine until 2026-08-05 and was removed on the
//  user's decision - it could not be made to work across a BOOK, where a chapter may be closed
//  between the search and the replace. Since then nothing has caught a document being edited
//  between the two, and the confirmation carried a standing disclaimer instead. This is the
//  second attempt, and it does work across a book: the counters below live in the FILE, so a
//  chapter that was closed and reopened - even across a restart of InDesign - still answers.
//
//  What is recorded, per chapter, is every story's (UID, ITextModel::GetChangeCount()) - and,
//  when the search's scope switches make them part of the walk's universe (see below), every
//  story's insert lock and, frame by frame, the two states that decide whether the walk visits
//  a frame's text at all: hidden, and sitting on a locked layer.
//
//    - GetChangeCount, not GetTextChangeCount: the latter does not move for an edit that only
//      changes attributes, and KBS searches can carry formatting criteria (Find Format), so an
//      attribute-only edit can change what matches. Measured 2026-08-08.
//    - EVERY story, not only the ones a hit landed in: the replace rewrites "the Nth match" of a
//      walk over the whole chapter, so a match appearing in a story that had no hits still shifts
//      the numbering. A story's counter only ever moves for its own edits - adding or deleting a
//      story leaves every other counter untouched - so the increase has to be seen in the LIST.
//    - The comparison walks both lists in step and also checks that they end together, so a story
//      added or removed is caught as well as one edited. New stories are inserted part-way
//      through the enumeration rather than appended, which the order check catches.
//
//  ***** WHY THE WALK GATES ARE PART OF THE STAMP (2026-08-09, the pre-submission re-check). *****
//  The replace rewrites "the Nth match of a re-walk", so anything that changes WHICH matches the
//  walk visits shifts the numbering - and the universe is shaped not only by the text but by the
//  states the scope switches exclude: hidden content (Include Hidden Layers off), locked layers
//  (Include Locked Layers off), locked stories (Include Locked Stories off). None of those states
//  moves a story counter - hiding a layer is not an edit - so the counters alone let a user hide
//  a layer between the search and the replace and have unticked occurrences rewritten with no
//  warning at all.
//
//  The states are taken PER FRAME, as the two effective answers KBSSearchEngine gives about every
//  hit (hidden: the layer's eye or the item's own Object > Hide; locked layer), not as a list of
//  the layers themselves. That is what makes a frame MOVED onto a hidden or locked layer - which
//  changes no layer and no counter - read as a change, while a frame moved between two ordinary
//  layers reads as nothing at all.
//
//  Only the states an OFF switch excludes are compared: a switch that is ON walks the state
//  regardless, so its changing cannot move the numbering, and comparing it would raise alarms
//  about runs that are in fact safe. The switches themselves cannot differ at compare time - the
//  walk signature covers all five, and the replace refuses on a signature mismatch before it ever
//  asks this file anything.
//
//  There is no official recipe for any of this: InDesign's own Find/Change has no stored plan to
//  protect (Change All finds and changes in ONE walk, so it always walks the current universe),
//  and its answer to locks is to read them at write time (spellpanel, SpellReplaceWalker.cpp:436),
//  which KBS also does (EditableFrameForMatch). A stored plan replayed later is this plug-in's
//  own shape, so guarding it is too.
//
//  Two properties measured on 2026-08-08 are what make this cheap:
//    - the counters are persisted in the document, so no observer has to be attached to anything
//      (and none has to be detached at shutdown);
//    - Undo winds them back to the recorded value, so "edited and then undone" reads as unchanged
//      and no false warning appears. Adding and then deleting a story does the same.
//
//  Reading counters composes nothing, so no SaveRestoreModifiedState guard is needed here - the
//  same note KESCL's CaptureDocStamp carries (KESCLFindInDoc.cpp:375-397), which this follows.
//
//========================================================================================

#ifndef __KBSEditStamp_h__
#define __KBSEditStamp_h__

#include "UIDRef.h"
#include "WalkerScopeOptions.h"

/** Per-chapter record of every story's change counter and the walk gates the search's scope
	switches watch. Lives beside the results and dies with them - KBSResultModel::Clear() calls
	Forget() from inside itself.
*/
namespace KBSEditStamp
{
	/** What, if anything, has changed about a chapter since its stamp was taken. The two changed
		answers exist so the prompt can name the axis: "the text has been edited" and "a layer or
		a lock has changed" call for different second looks from the user. */
	enum ChapterChange
	{
		/** Reads exactly as the stamp left it (or was never stamped - see QueryChapterChange). */
		kUnchanged = 0,
		/** A story's counter moved, or a story was added or removed: the text itself. */
		kTextEdited,
		/** The walk's universe moved instead: something was hidden or shown, locked or unlocked,
			or moved onto a differently-gated layer, while the search's scope switches EXCLUDE
			that state. The text may be untouched, but a re-walk would visit a different set of
			matches - which shifts the numbering exactly as an edit does. */
		kScopeStateChanged
	};

	/** Read every story's change counter - and the walk gates the scope switches watch - for the
		chapter just walked, and hold the reading.

		***** THIS MUST RUN BEFORE THE CHAPTER IS HANDED BACK. ***** A book search closes each
		chapter the moment its walk ends (KBSSearchEngine, at the ReleaseHeldDoc call), and the
		comment there states the rule this obeys: everything read AFTER that point is plain values,
		not database work. Reading the counters IS database work, so it happens before.

		That is why capturing is split in two. The index the chapter will occupy is not known yet
		at that moment - chapters with no hits are skipped further down - so the reading is held
		here and CommitPending files it once the chapter has actually been appended.

		A chapter whose document cannot be read is left UNSTAMPED rather than stamped empty: an
		empty stamp would compare equal to nothing and report every chapter as edited.
		@param docRef the chapter's document, still open.
		@param scopeOptions the switches THIS search walked with - the same ones the caller hands
		       to every chapter's walk. An OFF switch makes its state part of the universe and so
		       part of the stamp; an ON one walks the state regardless, so it is not recorded and
		       cannot raise a false alarm.
	*/
	void CapturePending(const UIDRef& docRef, const WalkerScopeOptions& scopeOptions);

	/** File what CapturePending read under the index the chapter was given. Discards the pending
		reading either way, so a chapter that ends up not appended leaves nothing behind.
		@param chapterIdx the chapter's index in KBSResultModel.
	*/
	void CommitPending(int32 chapterIdx);

	/** Does this chapter read exactly as its stamp left it - and if not, what kind of change?

		***** THE CALLER MUST HAVE ESTABLISHED THAT THE DOCUMENT IS STILL OPEN. ***** This reads
		the document, so a UIDRef whose database has been closed is undefined behaviour here, not
		a nil check - use KBSBookScope::IsDocStillOpen first (it compares the database pointer
		against the session's list without ever dereferencing it). A book search closes each
		chapter as it finishes with it, so this is the ordinary case, not a corner one.

		The one caller reopens the chapter immediately before asking (KBSReplaceEngine's resolve
		pass), which settles the question rather than testing it.

		An UNSTAMPED chapter answers kUnchanged. The warning it feeds does not refuse anything -
		it states the fact and offers a Cancel - so "not known to have changed" is the answer that
		keeps it quiet, and the alternative would raise an alert for every scan-built result set
		that was never stamped.

		When both kinds of change are present, kTextEdited wins: it is the more specific statement
		and the likelier thing the user did.
		@param chapterIdx the chapter's index in KBSResultModel.
		@param docRef the chapter's document, freshly resolved by the caller. Deliberately passed
		       in rather than held here: a UIDRef kept across a close cannot be trusted for
		       identity, so only UIDs and plain values are stored.
		@return kTextEdited when a counter moved, a story was added or removed, or the document
		        cannot be read now (it could be read at capture time, so something has changed);
		        kScopeStateChanged when only a watched gate moved (see ChapterChange).
	*/
	ChapterChange QueryChapterChange(int32 chapterIdx, const UIDRef& docRef);

	/** Drop every stamp. Called from INSIDE KBSResultModel::Clear(), not beside it. */
	void Forget();

	/** Release the storage at shutdown, beside the model's own statics. */
	void ShutdownCleanup();
}

#endif // __KBSEditStamp_h__

// End, KBSEditStamp.h.
