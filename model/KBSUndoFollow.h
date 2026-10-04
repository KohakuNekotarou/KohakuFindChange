//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ***** THE PANEL FOLLOWS AN UNDO AND A REDO (2026-09-29, the user: "after an Undo the row cannot be
//  ***** rejected again - the panel should come back with it, the way KCM's does"). *****
//
//  Every write of KBS's own - Change Checked, a row's / story's / document's Replace and Replace Again,
//  Reject Change, Accept Change, Accept All Changes by KohakuFindChange - is ONE undo step. What it did to
//  the panel's rows is kept beside it: the rows before it and after it (KBSResultModel::RowStep - or the
//  whole result set, for Change Checked, which turns the list into its report), and the VERSION of every
//  story it wrote to before it and after it (ITextModel::GetChangeCount, KBSSearchEngine::ReadStoryVersion).
//
//  An Undo puts a story back at EXACTLY the version it had (measured 2026-08-08, text-change-counters), a
//  Redo at exactly the one after; typing makes a new one. So when every story a write moved is back at its
//  "before", that write was undone, and its "before" rows go back on the panel; at its "after" again, it
//  was redone, and the "after" rows do. The row a Reject Change took back and an Undo put back reads
//  "replaced" again - and its Reject Change is offered again (the rows' own recorded versions come back
//  with them, so the replace's F-2 door finds the story as KBS left it).
//
//  ***** WHO TELLS US: THE MARK EACH WRITE LEAVES IN ITS UNDO STEP, HEARD ON THE DOCUMENT (2026-10-02). *****
//  Every write processes kKBSUndoMarkCmdBoss inside its own sequence (MarkWrite): a command that changes
//  nothing and raises a ModelChange on its document's subject. A LAZY observer on that subject
//  (IID_IKBSDOCUNDOOBSERVER, AddIn'd on kDocBoss, attached at run time) hears it again on the step's Undo and
//  Redo - lazy because lazy is the only notification an Undo and a Redo broadcast, the same message ids as
//  the Do (LazyNotificationData.h:50-58); a responder and a command interceptor are not called at all. KCM's
//  ticks and paws ride the same road (KCMPageMarksCmd.cpp).
//  (From 2026-09-29 to 2026-10-02 the observer sat on each STORY a write moved, under IID_ITEXTMODEL, the way
//   KCM's Story Edits list still does. A story InDesign purged from memory came back without it, and with
//   two documents in the list the second one's Ctrl+Z went unheard 12 times in 18 - S-1, KBSUndoFollow.cpp.)
//  ***** THE MODEL HALF'S SINCE THE SPLIT (2026-10-01) ***** - it keeps the results true and tells the panel
//  only through KBSModelNotify - and it answers on the main thread only (the gate in LazyUpdate): a
//  background task has neither an Undo nor a panel to follow it.
//
//  ***** WHAT IS NOT FOLLOWED: anything that is not a write of KBS's own ***** - typing, the Track Changes
//  panel, a script, a replace made before Show Changes rebuilt the list. Those leave the rows as they are,
//  as before; the doors that were there for them (the story's version, the row's text, the records' times)
//  still stand. ***** KEPT SO ON THE USER'S CALL (2026-10-04, the scenario cross-check 1: "A, as it is"). *****
//  Measured: Show Changes, then a Ctrl+Z of the replace it listed - the text came back, the list kept both rows
//  and "Found 2 change(s)", and said nothing until a row was touched (its right-click is greyed with the reason,
//  its jump says "undone, or edited since"). Saying so on the status line, or reading the records again, were
//  offered and not taken.
//
//========================================================================================

#ifndef __KBSUndoFollow_h__
#define __KBSUndoFollow_h__

#include "UIDRef.h"		// UIDRef

#include <vector>

class IDataBase;

namespace KBSUndoFollow
{
	/** Which write a step is - named on the status line when an Undo or a Redo takes it. */
	enum StepKind
	{
		kStepChangeChecked = 0,
		kStepReplace,
		kStepReplaceAgain,
		kStepReject,
		kStepAccept,
		kStepAcceptAll
	};

	/** ***** ONE WRITE, RECORDED ***** - made before a character is written (outside the write's command
	    sequence, or at its head), kept once the write has gone through AND the rows show it.

	    The constructor reads the version of every story holding a row in `chapters` (their documents must
	    be open) and starts the row backup (KBSResultModel::BeginRowBackup) - so a caller does not start one
	    of its own. `wholeResultSet` = the write reshapes the list (Change Checked): the whole result set is
	    copied now, and again at Keep.

	    Not kept - the write failed or was cancelled - the destructor puts back every row it changed
	    (KBSResultModel::RollBackRows), which is what the replace's own rollback did, and records nothing.
	    While one is standing, nothing is followed: the write's own notifications arrive as its sequence ends. */
	class StepRecorder
	{
	public:
		StepRecorder(const std::vector<int32>& chapters, bool wholeResultSet);
		~StepRecorder();

		/** The write went through and the rows show it: kept, if a story's version moved (a write that
		    moved none has nothing an Undo could take back). */
		void Keep(StepKind kind);

	private:
		bool fOpen;
		StepRecorder(const StepRecorder&);
		StepRecorder& operator=(const StepRecorder&);
	};

	/** ***** THE MARK (2026-10-02). ***** Processed by every write of KBS's own INSIDE its command sequence,
	    once it has written and before the sequence ends - so the mark is part of the write's one undo step -
	    for each document it wrote to. Puts the observer on the document first; a mark that cannot be
	    processed leaves the write's error state as it found it (the panel loses this step's following, the
	    user keeps the write). */
	void MarkWrite(IDataBase* db);

	/** ***** THE WORK OF THE OBSERVER. ***** Every kept write an Undo or a Redo has moved - the newest one
	    first for an Undo, the oldest first for a Redo, never past a write in the same document that has not
	    moved (a Change Checked, which puts every document's rows back, counts as in every document - since
	    2026-10-02) - has its rows put back, and the panel is drawn again and says so on its message line.
	    Called when a write's mark is heard (its Do, Undo or Redo) - the versions say which.
	    @return true when anything was put back. */
	bool Follow();

	/** ***** A DOCUMENT OF AN ALL DOCUMENTS LIST IS CLOSING (2026-09-29). ***** Only its rows leave the panel
	    (KBSResultModel::CloseChapter), and the kept writes let it go the same way: its stories come off every
	    write - a write left with none is dropped, as one of a closed document always was - and its chapter is
	    emptied in every kept whole result set, so an Undo of a Change Checked that also wrote it (it spans
	    documents; every write of rows is one document's) puts back the other documents' rows and not this
	    one's. Without it the whole write was dropped at the next follow (a document of it no longer open),
	    and an Undo in the documents still open went unfollowed.
	    Called by KBSCloseDocResponder, before the document goes (its UIDRef is still good). */
	void ForgetDocument(const UIDRef& docRef);

	/** ***** A DOCUMENT IS CLOSING, WHATEVER THE LIST SHOWS (2026-09-29). ***** The observer attached to it
	    (its stories' until 2026-10-02) is taken off while the document is still whole - what is attached is
	    detached. Called by KBSCloseDocResponder for every close, ahead of its other exits. */
	void DocumentClosing(const UIDRef& docRef);

	/** Application shutdown: the kept writes hold rows (PMStrings) - release them. */
	void ShutdownCleanup();
}

#endif // __KBSUndoFollow_h__

// End, KBSUndoFollow.h.
