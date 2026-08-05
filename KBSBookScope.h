//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Book-wide search scope. Turns the book the book panel is showing into a list of searchable
//  chapter documents. Chapters that are not open are opened WITHOUT a layout window and with the
//  UI suppressed - ONE AT A TIME. A run opens a chapter when its turn comes (OpenChapterDoc) and
//  hands it straight back once it has been walked (ReleaseHeldDoc), so a book run of any size
//  holds at most one chapter of its own and locks at most one .indd.
//
//  Chapters the user already had open are never held and never closed.
//
//  A chapter that ends up with a WINDOW stops being held at that moment (ForgetHeldDoc), whoever
//  opened it and whatever is in it: the user can see it, so it is theirs. What stays held BETWEEN
//  runs is therefore only what was reopened windowless and never shown - a jump whose window could
//  not be raised, a replace whose chapter took none - which ReleaseHeldDocs hands back when the
//  results are let go (ReleaseSearchedBook), the book is closed, or the application quits. Those
//  closes are UI-suppressed, so a chapter with unsaved work in it is kept as well (see
//  ReleaseHeldDocs).
//
//  Ported from KESCL's KESCLBookScope (KESCL is left untouched). KBS always searches the book the
//  panel is showing, so KESCL's "Search book" toggle is dropped here.
//
//========================================================================================

#ifndef __KBSBookScope_h__
#define __KBSBookScope_h__

#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

namespace KBSBookScope
{
	/** One searchable document: a book chapter (shortName = its file name for the read-out;
	    file = the chapter's .indd so a closed chapter can be reopened later; contentUID = its
	    entry in the book, which OpenChapterDoc needs to ask the book API about it).

	    docRef is null until the chapter is opened. A document-scope target is built by hand with
	    docRef already set and contentUID left invalid - that is what tells the engines apart. */
	struct ChapterDoc
	{
		UIDRef		docRef;
		PMString	shortName;
		IDFile		file;
		UID			contentUID;

		ChapterDoc() : contentUID(kInvalidUID) {}
	};

	/** Is the search scope the whole book (ON) or just the front document (OFF)? Session state
	    only - every launch starts OFF, like KESCL's "Search book" toggle. */
	bool IsBookScopeOn();

	/** Flip the scope. JUST THE FLAG: nothing is closed and no result is cleared here (KESCL
	    learned this the hard way - closing the held chapters inside the toggle crashed). The held
	    windowless chapters are released by the next book search or at shutdown, and a jump into a
	    chapter the user closed since reopens it through ReopenChapterDoc. */
	void SetBookScopeOn(bool on);

	/** Is there an active book right now? A cheap look at IBookManager only - nothing is
	    opened, listed or held. */
	bool HasActiveBook();

	/** Is there anything for the CURRENT scope to run on - the active book while Book Scope is ON,
	    a front document while it is OFF? Asked by the menu's enablement (KBSActionComponent's
	    UpdateActionStates) so the three commands that start a run go grey when there is nothing to
	    run them against, rather than starting and reporting "No open document to search."

	    It asks exactly what the engines ask when they resolve their own scope - HasActiveBook() and
	    ILayoutUIUtils::GetFrontDocument() - so the grey state and the run cannot disagree. Cheap
	    enough for a menu hook: neither call opens, lists or holds anything.

	    NOT a substitute for the engines' own checks. This answers for the menu; a script reaching
	    an action directly still meets the engine's guard. */
	bool HasScopeTarget();

	/** A chapter that could NOT be turned into a searchable document, and why. Reported rather
	    than dropped: a chapter missing from the list is indistinguishable from a chapter that
	    simply held no matches, and there is no way for the user to see through that. */
	struct SkippedChapter
	{
		PMString	name;		// the chapter's short name (its file name)
		PMString	reason;		// what the book says about it - see IBookUtils::GetBookContentStatus
	};

	/** List the active book's chapters WITHOUT opening anything. Each entry comes back with its
	    file, its short name and its content UID; docRef stays null until OpenChapterDoc fills it
	    in. Also resolves WHICH book the run is against (see GetSearchedBookPath) and releases
	    chapters still held for a different one.

	    @return true when a book was resolved and it has at least one chapter. Note that this says
	            nothing about whether those chapters can be OPENED - only OpenChapterDoc knows. */
	bool ListBookChapters(std::vector<ChapterDoc>& outDocs, PMString& outBookName);

	/** Open ONE listed chapter: reuse the user's copy when they already have it open (and do not
	    hold it), otherwise open it windowless + UI-suppressed and hold it, so ReleaseHeldDoc can
	    close it again. Fills ioChapter.docRef.

	    @param outSkipped optional: on false, the chapter's name and the book's own reason for it
	                      are APPENDED to this list. Pass nil to drop the reason.
	    @return true when ioChapter.docRef is usable. */
	bool OpenChapterDoc(ChapterDoc& ioChapter, std::vector<SkippedChapter>* outSkipped);

	/** Name the chapters OpenChapterDoc could not hand over, and what the book says about them,
	    appending to a status line. Reported rather than dropped: a chapter missing from the list is
	    indistinguishable from a chapter that simply held no findings, and nothing on screen would
	    let the user tell them apart.

	    Appends nothing when every chapter opened. At most three are named and the rest become
	    "..." - a status line stays short even at three lines.

	    It lives beside SkippedChapter rather than in whichever engine happens to need it: the two
	    scans and the search all end their summary the same way, and one sentence written twice is
	    one sentence that gets corrected once. */
	void AppendUnopenableNote(PMString& outSummary, const std::vector<SkippedChapter>& skipped);

	/** Is this document still in the session's open-document list? Compares list entries
	    against the UIDRef without dereferencing its (possibly dead) database. */
	bool IsDocStillOpen(const UIDRef& docRef);

	/** The full file path of the book the results on the panel came from. false (and an empty
	    string) when the panel is not showing a book's results.

	    Set when a run resolves its book; cleared by ReleaseSearchedBook. ReleaseHeldDocs does NOT
	    touch it - closing chapters and showing results are separate facts, and since every run
	    closes its chapters as it goes, tying the two together blanked this while a full result
	    set was still on screen. */
	bool GetSearchedBookPath(PMString& outPath);

	/** Let go of the book the results came from: forget the path AND close whatever chapters are
	    still held for it. Pair this with every KBSResultModel::Clear().

	    The two halves are one operation on purpose. Nothing else remembers which book a held
	    chapter belongs to, so a path dropped on its own strands them with their .indd files locked
	    (see KBSBookWatch's header on the two cases where that used to happen). Held chapters at
	    this point are only ever ones a jump or a replace reopened - a run closes its own as it
	    goes - so this is "the results are gone, and so is the reason those chapters were open". */
	void ReleaseSearchedBook();

	/** Is a book with this full file path still in the session's open-book list? Compares paths
	    rather than IBook pointers because the caller is a close notification: the closed book's
	    IBook is already gone by then, so there is no pointer left to compare. */
	bool IsBookStillOpen(const PMString& bookPath);

	/** Make the book at 'bookPath' the current one in BOTH senses: IBookManager's active book and
	    the front tab of the book panel. Those are separate states that do not follow each other -
	    selecting a tab leaves the active book alone and vice versa - so a caller that means "this
	    book now" has to say both, which is what this does.

	    The tab is brought forward WITHOUT taking the key focus, so a keyboard walk over the result
	    tree is not interrupted by it.

	    @param bookPath the book's full file path (what GetSearchedBookPath hands back).
	    @return false when no OPEN book has that path - nothing is changed then. A true return means
	            the active book was set; the tab follows unless the panel could not be resolved. */
	bool ActivateBook(const PMString& bookPath);

	/** The file of the book whose tab is FRONTMOST in the book panel, which is NOT necessarily
	    IBookManager::GetCurrentActiveBook: selecting a book's tab switches the panel but does not
	    make that book active - only touching a chapter inside it does (measured 2026-07-27).

	    Found by walking IPanelMgr: InDesign creates one book panel per open book, and the front
	    tab is the one whose containing palette is visible (measured 2026-07-28, full write-up in
	    docs/ai-notes/book-panel-active-tab.md).

	    @return false when no book panel is frontmost - the panel is iconised, its palette is
	            closed, or no book is open - leaving outFile untouched. Callers fall back to the
	            active book in that case. */
	bool GetPanelBookFile(IDFile& outFile);

	/** Close the chapters this module opened (the originally-closed ones only). Chapters the
	    user already had open are never touched. The closes are SCHEDULED
	    (IDocFileHandler::kSchedule + kSuppressUI), so this is safe to call from inside a
	    search or a notification.

	    ***** A chapter holding UNSAVED CHANGES IS KEPT, not closed. ***** These closes are
	    UI-suppressed, and IDocFileHandler::Close only offers to save "if uiFlags allow"
	    (IDocFileHandler.h:97-101) - so closing a modified chapter throws that modification away
	    without a word. It is reachable in ordinary use: a jump opens a chapter and gives it a
	    window, the user replaces in it (a replace never saves) or simply types in it, and the next
	    run would hand it back with the work still in it. A kept chapter stays ON
	    the held list, so a later call closes it once it has been saved. This is the distinction
	    CloseDisplayedDocsIfClean has always made - it skips a dirty document for exactly this
	    reason - now made here as well. */
	void ReleaseHeldDocs();

	/** Close THIS chapter, if KBS is the one who opened it AND it has nothing unsaved in it. A
	    chapter the user already had open is not held and is left alone, so a run can hand back every
	    chapter it walked without keeping track of who opened which. UI-suppressed exactly like
	    ReleaseHeldDocs, and like it, it refuses to close a chapter with unsaved work in it.

	    ***** closeNow decides WHEN, and it is not a detail. ***** The default SCHEDULES the close
	    (IDocFileHandler::kSchedule), which does not happen until the current notification or idle
	    tick has unwound - and a RUN does not unwind until it returns. Measured 2026-08-04 by counting
	    the .indd lock files during a four-chapter saving replace: they went 1, 2, 3, 4 and only fell
	    to zero once the run was over, so "hands each chapter back as it goes" held every chapter to
	    the end after all. closeNow = true closes on the spot (kProcess), which is what a run needs
	    when the whole point of its shape is to hold one chapter at a time.

	    Only pass true from OUTSIDE a command sequence and with no walk standing - a run that has just
	    ended a chapter's sequence and saved it, which is the case this was added for.

	    @return true when the chapter was actually handed back. false when it was not ours, when it
	            is no longer open, or when it holds unsaved changes. A caller that REPORTS having
	            closed chapters has to read this rather than assume: "nothing of ours was open" and
	            "we closed what was" are different facts, and only this can tell them apart. */
	bool ReleaseHeldDoc(const UIDRef& docRef, bool closeNow = false);

	/** Stop holding this chapter WITHOUT closing it: it has a WINDOW now, so it is the user's and no
	    longer something a run may hand back.

	    A run opens its chapters windowless and closes them again - that is what the held list is
	    for. A chapter that gains a window leaves that arrangement: it got one because the user asked
	    to be taken there (a jump, a document row) or because a replace landed in it, and closing it
	    afterwards would take away a window they are working in, along with anything they have typed
	    or replaced into it since (user, 2026-08-03: "a document the user opened by jumping should
	    not be closed, even if nothing was replaced in it").

	    Called from wherever a held chapter is given a window: ShowChapterWindow after a replace, and
	    KBSJump when a jump brings one to the front. Does nothing when the chapter is not held, so it
	    is safe to call on any document. */
	void ForgetHeldDoc(const UIDRef& docRef);

	/** Does this chapter entry name a FILE at all?

	    A BOOK chapter always does. A DOCUMENT-scope row does not: it is the front document, and it
	    is carried as a docRef with an empty file beside it.

	    ***** That difference is what tells ReopenChapterDoc's two failures apart. ***** It answers
	    false both when there was nothing to open BY and when the file would not open, and a caller
	    has to know which: only the first may fall back on the docRef the search left behind. The
	    second must give up instead, because that docRef belongs to a document which was closed when
	    the search finished - and asking IsDocStillOpen about a closed one is the very fault removed
	    on 2026-08-04 (a UIDRef is only (IDataBase*, UID), so a reused address with a matching UID
	    answers YES about a DIFFERENT document). See the resolve pass in KBSReplaceEngine.

	    Asked through the same SDKFileHelper::GetPath() ReopenChapterDoc itself asks - through this
	    very function - so the two cannot come to differ. */
	bool ChapterHasFile(const IDFile& file);

	/** Reopen a chapter by its file (Task 3 jump): if the user reopened it themselves, rebind to
	    THEIR open copy (and do not hold it); otherwise open it windowless + UI-suppressed and hold
	    it. The (re)opened document is returned in outDocRef. false = cannot reopen (missing file,
	    locked) - or there was no file to open by at all, which ChapterHasFile is what tells apart.
	    Used when a jump target's held chapter was closed by the user since the search. */
	bool ReopenChapterDoc(const IDFile& file, UIDRef& outDocRef);

	/** Give a chapter that is open WITHOUT a window (the search opens them that way) a real layout
	    window, so the user can see what a replace did to it. Does NOT save, and does not bring an
	    already-visible document to the front - a document that already has a window anywhere,
	    including behind another tab, is left exactly as it is.

	    @return true when the chapter HAS a window afterwards - whether this call opened it or it
	            already had one. false means it has none and the user cannot see it, which for a
	            chapter a replace has just written to is worth reporting: the run leaves every
	            chapter unsaved for the user to deal with, and one with no window cannot be dealt
	            with. (It answered false for "it already had a window" until 2026-08-05, which made
	            the two indistinguishable and the answer not worth reading. The replace's caller was
	            discarding it.) */
	bool ShowChapterWindow(const UIDRef& docRef);

	/** The "Hide Previous Chapter" sweep (Task 3): close every OTHER document that HAS a window and
	    needs no save, on schedule - whoever opened it. The exception document (the one a jump just
	    landed in) and the windowless held chapters (the reopen cache) survive. Dirty documents stay
	    (closing them would want a save). */
	void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc);

	/** Application-shutdown cleanup (state only, no closing, no UI): forget the held-chapter
	    list without closing anything - the quitting application closes every document itself. */
	void ShutdownCleanup();
}

#endif // __KBSBookScope_h__
