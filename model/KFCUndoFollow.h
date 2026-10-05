//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE PANEL FOLLOWS AN UNDO AND A REDO (the author: "after an Undo the row cannot be rejected again -
//  the panel should come back with it, the way KCM's does").
//
//  Every write of KFC's own - Change Checked, a row's / story's / document's Replace and Replace Again,
//  Reject Change, Accept Change, Accept All Changes by KohakuFindChange - is ONE undo step. What it did to
//  the panel's rows is kept beside it: the rows before it and after it (KFCResultModel::RowStep - or the
//  whole result set, for Change Checked, which turns the list into its report), and the VERSION of every
//  story it wrote to before it and after it (ITextModel::GetChangeCount, KFCSearchEngine::ReadStoryVersion).
//
//  An Undo puts a story back at EXACTLY the version it had (measured - memory text-change-counters), a
//  Redo at exactly the one after. So when every story a write moved is back at its "before", that write was
//  undone, and its "before" rows go back on the panel; at its "after" again, it was redone, and the "after"
//  rows do.
//  ! AN EDIT AFTER AN UNDO GOES ON FROM THE NUMBER THE UNDO PUT BACK, ONE PER CHANGE - so as many keystrokes
//    as the undone write had moved the story by come back to its "after" (measured: a row's Replace moved a
//    story 6 -> 10; Ctrl+Z, four characters typed -> 10). The version alone cannot tell that from a Redo - a
//    mark heard from another document then took it for one, measured (the regression case
//    u1-false-redo-undo-other-type4) - so a Redo is asked the story's TEXT as well: each story's text right after
//    the write is kept as one number, and only a story that reads that way again was redone (AllAt in the .cpp).
//    An Undo needs no such question: a version comes back DOWN to "before" only by an Undo.
//  The row a Reject Change took back and an Undo put back reads
//  "replaced" again - and its Reject Change is offered again (the rows' own recorded versions come back
//  with them, so the replace's story-version door - KFCReplaceEngine.cpp, "A STORY'S VERSION" - finds the
//  story as KFC left it).
//
//  WHO TELLS US: THE MARK EACH WRITE LEAVES IN ITS UNDO STEP, HEARD ON THE DOCUMENT.
//  Every write processes kKFCUndoMarkCmdBoss inside its own sequence (MarkWrite): a command that changes
//  nothing and raises a ModelChange on its document's subject. A LAZY observer on that subject
//  (IID_IKFCDOCUNDOOBSERVER, AddIn'd on kDocBoss, attached at run time) hears it again on the step's Undo and
//  Redo - lazy because lazy is the only notification an Undo and a Redo broadcast, the same message ids as
//  the Do (LazyNotificationData.h:50-58); a responder and a command interceptor are not called at all. KCM's
//  ticks and paws ride the same road (KCMPageMarksCmd.cpp).
//  (NOT on each STORY a write moved, under IID_ITEXTMODEL, the way KCM's Story Edits list does: a story
//   InDesign purged from memory comes back without its observer, and with two documents in the list the
//   second one's Ctrl+Z went unheard 12 times in 18.)
//  THE MODEL HALF'S - it keeps the results true and tells the panel only through KFCModelNotify - and it
//  answers on the main thread only (the gate in LazyUpdate): a background task has neither an Undo nor a
//  panel to follow it.
//
//  WHAT IS NOT FOLLOWED: anything that is not a write of KFC's own - typing, the Track Changes panel, a
//  script, a replace made before Show Changes rebuilt the list. Those leave the rows as they are; the
//  doors that are there for them (the story's version, the row's text, the records' times) still stand.
//  KEPT SO ON THE AUTHOR'S CALL ("A, as it is") - do not offer it again. Measured: Show Changes, then a
//  Ctrl+Z of the replace it listed - the text came back, the list kept both rows and "Found 2 change(s)",
//  and said nothing until a row was touched (its right-click is greyed with the reason, its jump says
//  "undone, or edited since"). Saying so on the status line, or reading the records again, were offered
//  and not taken.
//
//========================================================================================

#ifndef __KFCUndoFollow_h__
#define __KFCUndoFollow_h__

#include "UIDRef.h"		// UIDRef
#include "KFCBookScope.h"	// ChapterDoc - a query run's documents (RunRecorder)

#include <vector>

class IDataBase;

namespace KFCUndoFollow
{
	/** Which write a step is - named on the status line when an Undo or a Redo takes it. */
	enum StepKind
	{
		kStepChangeChecked = 0,
		kStepReplace,
		kStepReplaceAgain,
		kStepReject,
		kStepAccept,
		kStepAcceptAll,
		kStepRunQueries		// the query run (KFCQuerySequence) - RunRecorder's
	};

	/** ONE WRITE, RECORDED - made before a character is written (outside the write's command
	    sequence, or at its head), kept once the write has gone through AND the rows show it.

	    The constructor reads the version of every story holding a row in `chapters` (their documents must
	    be open) and starts the row backup (KFCResultModel::BeginRowBackup) - so a caller does not start one
	    of its own. `wholeResultSet` = the write reshapes the list (Change Checked): the whole result set is
	    copied now, and again at Keep.

	    Not kept - the write failed or was cancelled - the destructor puts back every row it changed
	    (KFCResultModel::RollBackRows), which is what the replace's own rollback did, and records nothing.
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

	/** A QUERY RUN, RECORDED (KFCQuerySequence, 2026-10-04) - not a StepRecorder: the stories it writes are known only
	    once each query has searched, and the list it leaves is a new one. Made at the run's commit point, BEFORE the
	    list is cleared: the list as it is then is copied whole (what an Undo puts back). ReadStories = every text model's
	    version in the run's documents, before a character is written. Keep = the run went through and the list shows it:
	    kept as kStepRunQueries, in the result set the list is now. RestoreBefore = the run was undone (cancelled, failed):
	    the list goes back as it was and nothing is kept. While one stands nothing is followed. */
	class RunRecorder
	{
	public:
		RunRecorder();
		~RunRecorder();
		void ReadStories(const std::vector<KFCBookScope::ChapterDoc>& docs);
		void Keep();
		void RestoreBefore();
	private:
		bool fOpen;
		RunRecorder(const RunRecorder&);
		RunRecorder& operator=(const RunRecorder&);
	};

	/** THE MARK. Processed by every write of KFC's own INSIDE its command sequence,
	    once it has written and before the sequence ends - so the mark is part of the write's one undo step -
	    for each document it wrote to. Puts the observer on the document first; a mark that cannot be
	    processed leaves the write's error state as it found it (the panel loses this step's following, the
	    user keeps the write). */
	void MarkWrite(IDataBase* db);

	/** THE WORK OF THE OBSERVER. Every kept write an Undo or a Redo has moved - the newest one first for an
	    Undo, the oldest first for a Redo, never past a write in the same document that has not moved (a
	    Change Checked, which puts every document's rows back, counts as in every document) - has its rows
	    put back, and the panel is drawn again and says so on its message line.
	    Called when a write's mark is heard (its Do, Undo or Redo) - the versions say which.
	    @return true when anything was put back. */
	bool Follow();

	/** A DOCUMENT OF AN ALL DOCUMENTS LIST IS CLOSING. Only its rows leave the panel
	    (KFCResultModel::CloseChapter), and the kept writes let it go the same way: its stories come off every
	    write - a write left with none is dropped, as one of a closed document always was - and its chapter is
	    emptied in every kept whole result set, so an Undo of a Change Checked that also wrote it (it spans
	    documents; every write of rows is one document's) puts back the other documents' rows and not this
	    one's. Without it the whole write would be dropped at the next follow (a document of it no longer
	    open), and an Undo in the documents still open would go unfollowed.
	    Called by KFCCloseDocResponder, before the document goes (its UIDRef is still good). */
	void ForgetDocument(const UIDRef& docRef);

	/** A CHAPTER OF A BOOK'S LIST IS CLOSING. A book's rows stay when a chapter closes (KFCCloseDocResponder),
	    so its chapter is not emptied as ForgetDocument's is: the writes of rows in it go (its history goes with
	    it), its stories come off every Change Checked - which is then still followed in the chapters left open
	    (an Undo there goes through: the same as All Documents, case sc-alldocs-close-undo) - and a kept whole
	    result set holding it marks it FROZEN where that write, or one after it, wrote the chapter: when that set
	    is put back, the chapter keeps the rows the panel has for it at that moment, since nothing an Undo in
	    another chapter does reaches its file. A chapter nothing wrote is put back with the set, as its file reads
	    the same (case cb-book-close-unwritten-undo). Without this the whole Change Checked was dropped at the next
	    follow and an Undo in the open chapters went unfollowed (measured - case cb-book-close-chapter-undo).
	    Called by KFCCloseDocResponder for every close while a book's list is up - a run's own hand-backs too. */
	void ForgetBookChapter(const UIDRef& docRef);

	/** A DOCUMENT IS CLOSING, WHATEVER THE LIST SHOWS. The observer attached to it is taken off while the
	    document is still whole - what is attached is detached. Called by KFCCloseDocResponder for every close, ahead of its other exits. */
	void DocumentClosing(const UIDRef& docRef);

	/** Application shutdown: the kept writes hold rows (PMStrings) - release them. */
	void ShutdownCleanup();
}

#endif // __KFCUndoFollow_h__

// End, KFCUndoFollow.h.
