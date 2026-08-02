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
//  What stays held BETWEEN runs is only what a jump or a replace reopened, which is why
//  ReleaseHeldDocs (all of them) still exists: a jump means the user is looking at that chapter,
//  so it is kept until the results are let go (ReleaseSearchedBook), the book is closed, or the
//  application quits.
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
	    search or a notification. */
	void ReleaseHeldDocs();

	/** Close THIS chapter, if KBS is the one who opened it. A chapter the user already had open is
	    not held and is left alone, so a run can hand back every chapter it walked without keeping
	    track of who opened which. Scheduled + UI-suppressed exactly like ReleaseHeldDocs, so it is
	    safe to call from inside a run. */
	void ReleaseHeldDoc(const UIDRef& docRef);

	/** Reopen a chapter by its file (Task 3 jump): if the user reopened it themselves, rebind to
	    THEIR open copy (and do not hold it); otherwise open it windowless + UI-suppressed and hold
	    it. The (re)opened document is returned in outDocRef. false = cannot reopen (missing file,
	    locked). Used when a jump target's held chapter was closed by the user since the search. */
	bool ReopenChapterDoc(const IDFile& file, UIDRef& outDocRef);

	/** Give a chapter that is open WITHOUT a window (the search opens them that way) a real layout
	    window, so the user can see what a replace did to it. Does NOT save, and does not bring an
	    already-visible document to the front - a document that already has a window anywhere,
	    including behind another tab, is left exactly as it is.

	    @return true when a window was actually opened. */
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
