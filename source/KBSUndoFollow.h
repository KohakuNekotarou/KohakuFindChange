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
//  ***** WHO TELLS US: A LAZY OBSERVER ON EACH STORY WRITTEN TO ***** (IID_IKBSSTORYUNDOOBSERVER, AddIn'd on
//  kTextStoryBoss, attached at run time), listening under the SDK's own IID_ITEXTMODEL. Lazy because lazy is
//  the only notification an Undo and a Redo broadcast - the same message ids as the Do
//  (LazyNotificationData.h:50-58); a responder and a command interceptor are not called at all. The same
//  shape as KCM's Story Edits list (KCMStoryFollowObserver - the user's "the way KCM does").
//  ***** A UI PLUG-IN'S IMPLEMENTATION ON A MODEL BOSS ***** is allowed through an AddIn in the UI plug-in's
//  resource (guide vol1-07, "Object-Model Rules" 2), and is not there in a background task - where there
//  is neither an Undo nor a panel to follow it.
//
//  ***** WHAT IS NOT FOLLOWED: anything that is not a write of KBS's own ***** - typing, the Track Changes
//  panel, a script, a replace made before Show Changes rebuilt the list. Those leave the rows as they are,
//  as before; the doors that were there for them (the story's version, the row's text, the records' times)
//  still stand.
//
//========================================================================================

#ifndef __KBSUndoFollow_h__
#define __KBSUndoFollow_h__

#include "UIDRef.h"		// UID

#include <vector>

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

	/** ***** THE WORK OF THE OBSERVER. ***** Every kept write an Undo or a Redo has moved - the newest one
	    first for an Undo, the oldest first for a Redo, never past a write in the same document that has not
	    moved - has its rows put back, and the panel is drawn again and says so on its message line.
	    `story` = the story that sent the notification, or kInvalidUID; nothing is done unless a kept write
	    names it (typing in a story no write touched costs one comparison).
	    @return true when anything was put back. */
	bool Follow(UID story);

	/** ***** A DOCUMENT OF AN ALL DOCUMENTS LIST IS CLOSING (2026-09-29). ***** Only its rows leave the panel
	    (KBSResultModel::CloseChapter), and the kept writes let it go the same way: its stories come off every
	    write - a write left with none is dropped, as one of a closed document always was - and its chapter is
	    emptied in every kept whole result set, so an Undo of a Change Checked that also wrote it (it spans
	    documents; every write of rows is one document's) puts back the other documents' rows and not this
	    one's. Without it the whole write was dropped at the next follow (a document of it no longer open),
	    and an Undo in the documents still open went unfollowed.
	    Called by KBSCloseDocResponder, before the document goes (its UIDRef is still good). */
	void ForgetDocument(const UIDRef& docRef);

	/** ***** A DOCUMENT IS CLOSING, WHATEVER THE LIST SHOWS (2026-09-29). ***** The observers attached to its
	    stories are taken off while the document is still whole - what is attached is detached (KBSUndoFollow.cpp,
	    Watch). Called by KBSCloseDocResponder for every close, ahead of its other exits. */
	void DocumentClosing(const UIDRef& docRef);

	/** Application shutdown: the kept writes hold rows (PMStrings) - release them. */
	void ShutdownCleanup();
}

#endif // __KBSUndoFollow_h__

// End, KBSUndoFollow.h.
