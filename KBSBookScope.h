//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Book-wide search scope. Turns the active book (IBookManager::GetCurrentActiveBook) into a
//  list of searchable chapter documents. Chapters that are not open are opened WITHOUT a
//  layout window - the same windowless open InDesign's own TOC/index-across-book machinery
//  uses (Utils<IBookUtils>::OpenOneDocument) - and stay held open until ReleaseHeldDocs.
//
//  Ported from KESCL's KESCLBookScope (KESCL is left untouched). This Step-1 subset keeps only
//  what a count needs: enumerate the book's chapters, hold the ones we opened, and release
//  them afterwards. The jump-time reopen/early-close machinery arrives with the result panel.
//  KBS always searches the active book, so KESCL's "Search book" toggle is dropped here.
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
	    file = the chapter's .indd so a closed chapter can be reopened later). */
	struct ChapterDoc
	{
		UIDRef		docRef;
		PMString	shortName;
		IDFile		file;
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

	/** List the active book's chapters as open documents, in book order. Chapters that are not
	    open are opened windowless here (and remembered, so ReleaseHeldDocs can close them
	    again); chapters already open are used as they are.

	    @param outDocs      the chapters as (document, file name) pairs; empty on failure.
	    @param outBookName  the active book's title, for status messages.
	    @param outSkipped   optional: chapters that could not be opened, each with the book's own
	                        reason. Pass nil to ignore them (they are still left out of outDocs).
	    @return true when there is an active book with at least one openable chapter. */
	bool GetBookChapterDocs(std::vector<ChapterDoc>& outDocs, PMString& outBookName,
		std::vector<SkippedChapter>* outSkipped = nil);

	/** Name the chapters GetBookChapterDocs could not hand over, and what the book says about them,
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

	/** The full file path of the book the held chapters were opened for - i.e. the book the last
	    book search ran against. false (and an empty string) when nothing is held.
	    NOTE: ReleaseHeldDocs clears it, so read this BEFORE releasing. */
	bool GetHeldBookPath(PMString& outPath);

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

	    @param bookPath the book's full file path (what GetHeldBookPath hands back).
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
