//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
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
//  results are let go (ReleaseSearchedBook - the next search, of any scope, or query run at its
//  commit point), when the book is closed (KFCBookWatch), and when a replace is cancelled. Those
//  closes are UI-suppressed, so a chapter with unsaved work in it is kept as well (see
//  ReleaseHeldDocs). At application quit nothing is handed back: ShutdownCleanup only forgets the
//  list, and the quitting application closes every document itself.
//
//  Ported from KESCL's KESCLBookScope (KESCL is left untouched). The book a run targets is the one the
//  book panel is showing (ResolveTargetBook in the .cpp).
//
//========================================================================================

#ifndef __KFCBookScope_h__
#define __KFCBookScope_h__

#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

class IDocumentList;

namespace KFCBookScope
{
	/** One searchable document: a book chapter (shortName = its file name for the read-out;
	    file = the chapter's .indd so a closed chapter can be reopened later; contentUID = its
	    entry in the book, which OpenChapterDoc needs to ask the book API about it).

	    docRef is null until the chapter is opened. A document-scope target (DocAsChapter) comes with
	    docRef already set and contentUID left invalid - that is what tells the engines apart.

	    file is EMPTY when there is no file to name: a document-scope target, and a book entry for which
	    IBookContent::GetIDFile answered kFalse - "returns kTrue if a file can be obtained ... kFalse
	    otherwise" (IBookContent.h:121-125), a state the book API allows and this plug-in may not assume
	    away. ListBookChapters empties it on that answer, because the header does not say what the
	    function leaves in its argument then, and an empty file is what every door here reads as "no
	    file" (ChapterHasFile) - no flag of its own beside it. */
	struct ChapterDoc
	{
		UIDRef		docRef;
		PMString	shortName;
		IDFile		file;
		UID			contentUID;

		ChapterDoc() : contentUID(kInvalidUID) {}
	};

	/** Is the search scope the whole book (ON), or what Edit > Find/Change's Search: names (OFF)?
	    Session state only - every launch starts OFF,
	    like KESCL's "Search book" toggle. */
	bool IsBookScopeOn();

	/** Flip the scope. JUST THE FLAG: nothing is closed and no result is cleared here (KESCL
	    learned this the hard way - closing the held chapters inside the toggle crashed). The held
	    windowless chapters are released by the next search (of either scope) or query run at its
	    commit point, or when their book is closed, and a jump into a chapter the user closed since
	    reopens it through ReopenChapterDoc. */
	void SetBookScopeOn(bool on);

	/** FIND/CHANGE SELECTED DOCUMENTS (BOOK) (docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
	    G9, G10): while it is ON and Book Scope is on, a run takes only the documents selected in the Book panel - none
	    or all selected is the whole book. It does nothing with Book Scope off (the menu greys it then). A session flag
	    like Book Scope's, but kept by Save Panel Settings (the author's call), so OFF at launch unless that restored it. */
	bool IsSelectedDocumentsOn();

	/** JUST THE FLAG, like SetBookScopeOn: nothing is closed and no result is cleared. */
	void SetSelectedDocumentsOn(bool on);

	/** The book a book-scope run would TARGET - the book panel's book (what ListBookChapters will
	    actually search), or failing that the active book (its fallback) - and whether it has a chapter
	    to run on. A cheap look: nothing is opened, listed or held (the chapters are COUNTED, through
	    IBookContentMgr::GetContentCount - the list ListBookChapters walks).

	    The one answer to "would a book run have something to run on", asked by the menu gate
	    (HasScopeTarget) and by the front doors of the search and the book's runs - through the run's own
	    resolver (ResolveTargetBook in the .cpp), not a copy of it. Doors that asked only whether a
	    book is ACTIVE, while the run resolves the PANEL's book, would answer the same question two
	    ways - a book on show in the panel with no active book behind it turned away at every door the
	    run has.

	    AN EMPTY BOOK IS AN ANSWER OF ITS OWN. Asked only whether there is a book, a book with no
	    chapters passes every door, and the run says "That book has no chapters." only past its commit
	    point, with the previous results already thrown away. (The case: a book just made with File >
	    New > Book, whose tab comes to the front of the Book panel and so becomes the target - measured
	    by the regression case book-empty-keeps-results.) Each door refuses it before it touches
	    anything, and the menu greys over it (the author's call: a command that cannot run is grey) -
	    from this one answer, so the two cannot come to differ. */
	enum TargetBook
	{
		kNoTargetBook,		// no book a run could target: none open, or the one there is still closing
		kTargetBookEmpty,	// a book with no chapters in it
		kTargetBookReady	// a book with at least one chapter
	};
	TargetBook GetTargetBook();

	/** Is there anything for the CURRENT scope to run on - a target book with chapters while Book
	    Scope is ON (GetTargetBook), an active document while it is OFF? Asked by the menu's
	    enablement (KFCActionComponent's UpdateActionStates) so the two commands that start a run on it -
	    Find and Change All in Book - go grey when there is nothing to run them against, rather
	    than starting and reporting "No open document to search."

	    It asks exactly what the engines ask when they resolve their own scope - GetTargetBook() and
	    ActiveDocument() - so the grey state and the run cannot disagree. Cheap enough for a menu
	    hook: nothing here opens, lists or holds anything (the panel-book half reads a palette's file
	    field, one IBookManager lookup and one chapter count).

	    NOT a substitute for the engines' own checks. This answers for the menu; a script reaching
	    an action directly still meets the engine's guard. */
	bool HasScopeTarget();

	/** The document a document-scope run searches: the ACTIVE document - the one the user is working
	    in, and the one Edit > Find/Change searches - through IActiveContext::GetContextDocument.
	    Non-owning; nil when there is none. The one place the menu's grey state (HasScopeTarget) and the
	    search (KFCSearchEngine::SearchBook) ask.

	    NOT ILayoutUIUtils::GetFrontDocument. That one answers "the document of the frontmost LAYOUT
	    presentation" (ILayoutUIUtils.h:95-98) - a UI question, nil off the main thread - while the
	    active context is architecture and safe in a model plug-in (memory
	    document-activation-is-presentation; KCM's KCMActiveDoc is the same call). Measured: with
	    another document's Story Editor brought to the front, InDesign brings that document's layout
	    window up with it, so the two answer alike there - the choice is the official question, not a
	    fix of a case seen going wrong.

	    A chapter a run opened WINDOWLESS is never the answer: a document with no window cannot be
	    made active. */
	class IDocument* ActiveDocument();

	/** An open document as a run's target - the one builder the search's non-book scopes and the
	    query run's share: docRef and its name set, no file and no book entry - which is what
	    tells the engines it is not a book's chapter (see ChapterDoc). */
	ChapterDoc DocAsChapter(class IDocument* doc);

	/** A chapter that could NOT be turned into a searchable document, and why. Reported rather
	    than dropped: a chapter missing from the list is indistinguishable from a chapter that
	    simply held no matches, and there is no way for the user to see through that. */
	struct SkippedChapter
	{
		PMString	name;		// the chapter's short name (its file name)
		PMString	reason;		// what the book says about it - see IBookUtils::GetBookContentStatus
	};

	/** How much of the target book a run takes (docs/superpowers/specs/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
	    section 4-4): total = the book's documents; selected = how many of them a run takes when it is narrowed to the
	    Book panel's selection (Find/Change Selected Documents (Book)), 0 = the whole book. */
	struct BookSelection
	{
		int32	selected;
		int32	total;
		BookSelection() : selected(0), total(0) {}
	};

	/** List the TARGET book's chapters WITHOUT opening anything - the book the BOOK PANEL is showing,
	    or the active book when no panel can be reached (ResolveTargetBook). Each entry comes back with
	    its file, its short name and its content UID; docRef stays null until OpenChapterDoc fills it
	    in. Also records WHICH book the run is against (see GetSearchedBookPath) - so call
	    ReleaseSearchedBook first, as the search does at its commit point: that hands back whatever
	    the last run left held, and this does not.
	    NARROWED: with Find/Change Selected Documents (Book) on and a PART of the book selected in the
	    Book panel that shows it, only those chapters are listed - read now, as the run starts (section 4-2).
	    outSelection, when given, says how many were taken and of how many (selected 0 = the whole book).

	    @return true when a book was resolved and it has at least one chapter. Note that this says
	            nothing about whether those chapters can be OPENED - only OpenChapterDoc knows. */
	bool ListBookChapters(std::vector<ChapterDoc>& outDocs, PMString& outBookName, BookSelection* outSelection = nil);

	/** THE TARGET BOOK, DESCRIBED WITHOUT A TRACE (the same section): its title, how many documents it
	    holds and how many a run would take (the same narrowing as ListBookChapters, from the same place) - the book
	    ListBookChapters would list (ResolveTargetBook). For the words that name a run before it starts (the menu's
	    names, the query dialog's Runs on: line), so it may be asked at any time: unlike ListBookChapters it records
	    nothing (GetSearchedBookPath), opens nothing and holds nothing.
	    @return false when there is no target book (outBookName empty, outSelection zero). */
	bool DescribeTargetBook(PMString& outBookName, BookSelection& outSelection);

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

	    It lives beside SkippedChapter rather than in whichever engine happens to need it: one
	    sentence written twice is one sentence that gets corrected once. */
	void AppendUnopenableNote(PMString& outSummary, const std::vector<SkippedChapter>& skipped);

	/** The shape every chapter note in KFC has, and the one place it is spelled:
	    "  N chapter(s) <what> (<name>, <name>, <name>, ...)<tail>". At most three names, then "..." -
	    a status line stays short even at three lines. Appends nothing when 'names' is empty, so the
	    ordinary summary is unchanged.

	    Names are appended RAW, ampersands and all: the message area is drawn by hand and takes '&' as
	    it is (the note at the definition says what doubling them looked like). Every chapter note is
	    built on it - AppendUnopenableNote, AppendUnclosedNote and the search's two. */
	void AppendChapterNote(PMString& outSummary, const char* what, const std::vector<PMString>& names,
		const char* tail);

	/** The list of open documents, AddRef'd - or nil (the application can be gone during shutdown).
	    IApplication's, NOT ISession's (the author's call). ISession::QueryDocumentList
	    sits in that header's "For internal use only" group (ISession.h:162-196); IApplication's
	    (IApplication.h:113) is public, and it is the one the SDK's snippets and InDesign's own open source
	    ask (SnpExportDocAsPDF.cpp, GoToAnchorPanelObserver.cpp, PrivateSpellingUtils.cpp - none asks the
	    session). Every list of open documents in the model half comes from here. */
	IDocumentList* QueryOpenDocumentList();

	/** Is this document still in the application's open-document list? Compares list entries
	    against the UIDRef without dereferencing its (possibly dead) database. */
	bool IsDocStillOpen(const UIDRef& docRef);

	/** Is this document open AND showing a window anywhere (front, or behind another tab)? IsDocStillOpen
	    first - the window question reads the database - then the same all-presentations search the
	    held-chapter releases ask. (Search: = All Documents: a document the user keeps without a window
	    is searched and written, but only a jump opens one - its row says "(no window)".) */
	bool HasWindow(const UIDRef& docRef);

	/** The full file path of the book the results on the panel came from. false (and an empty
	    string) when the panel is not showing a book's results.

	    Set when a run resolves its book; cleared by ReleaseSearchedBook. ReleaseHeldDocs does NOT
	    touch it - closing chapters and showing results are separate facts, and since every run
	    closes its chapters as it goes, tying the two together would blank this while a full result
	    set is still on screen. */
	bool GetSearchedBookPath(PMString& outPath);

	/** Let go of the book the results came from: forget the path AND close whatever chapters are
	    still held for it. It goes with every KFCResultModel::Clear() - KFCSearchEngine::DropResults
	    does the two together.

	    The two halves are one operation on purpose. Nothing else remembers which book a held
	    chapter belongs to, so a path dropped on its own strands them with their .indd files locked
	    (see KFCBookWatch's header for two such cases). Held chapters at
	    this point are only ever ones a jump or a replace reopened - a run closes its own as it
	    goes - so this is "the results are gone, and so is the reason those chapters were open". */
	void ReleaseSearchedBook();

	/** Is a book with this full file path still in the session's open-book list? Asked by FILE
	    (IBookManager::FindOpenBookByName, the book API's own lookup) rather than by IBook pointer
	    because the caller is a close notification: the closed book's IBook is already gone by then, so
	    there is no pointer left to compare. */
	bool IsBookStillOpen(const PMString& bookPath);

	/** Make the book at 'bookPath' IBookManager's active book - the one every book API answers about.

	    HALF OF "THIS BOOK NOW" (the model/UI split). The active book and
	    the front tab of the book panel are separate states that do not follow each other - selecting a tab
	    leaves the active book alone and vice versa - so a caller that means "this book now" has to say
	    both. This is the model's half; the tab is the UI half's (KFCBookPanelLookup::BringBookTabForward),
	    which the caller asks next.

	    @param bookPath the book's full file path (what GetSearchedBookPath hands back).
	    @return false when no OPEN book has that path - nothing is changed then. True = the active book
	            was set. */
	bool MakeBookActive(const PMString& bookPath);

	// (The walk of InDesign's book panels is the UI half's - KFCBookPanelLookup.h. The front tab's book
	//  reaches this module through IKFCUIServices::GetPanelBookFile.)

	/** Close the chapters this module opened (the originally-closed ones only). Chapters the
	    user already had open are never touched. The closes are SCHEDULED
	    (IDocFileHandler::kSchedule + kSuppressUI), so this is safe to call from inside a
	    search or a notification.

	    A chapter holding UNSAVED CHANGES IS KEPT, not closed. These closes are UI-suppressed, and
	    IDocFileHandler::Close only offers to save "if uiFlags allow" (IDocFileHandler.h:97-101) - so
	    closing a modified chapter throws that modification away without a word. A kept chapter stays
	    ON the held list, so a later call closes it once it has been saved. CloseDisplayedDocsIfClean
	    makes the same distinction - it skips a dirty document for exactly this reason.

	    WHOSE unsaved work this door protects. Not the user's typing in a chapter a jump
	    opened: a chapter that gains a window stops being held at that moment (ForgetHeldDoc - both
	    ShowChapterWindow and KFCJump call it), and even where one slips through, the window test
	    drops it first. Nobody can type into a document with no window. A SCRIPT can open one
	    windowless and modify it, but such a document was not opened by us and is never held -
	    ReopenChapterDoc rebinds to an already-open document without holding it, whoever opened it.

	    What does reach here is windowless AND dirty AND held, and only this plug-in can produce
	    that combination: a chapter a REPLACE wrote to whose window would not open (the run reports
	    it as chaptersNoWindow). So the work being protected is the user's REPLACEMENTS, not their
	    typing.

	    A chapter whose close cannot go through at all (no file handler, or CanClose refuses) is
	    kept the same way: dropped, it would sit windowless with its .indd locked and nothing able to
	    hand it back for the rest of the session.

	    And a chapter found with a WINDOW is dropped from the list, not closed. A window
	    makes it the user's whoever raised it - the book panel can window a held chapter behind this
	    module's back - and a visible document is one the user can deal with themselves.

	    Every one of these verdicts is ReleaseHeldDoc's: this hands each held chapter to it, on a
	    schedule - the tests are not written out a second time here.

	    closeNow = true closes on the spot instead (ReleaseHeldDoc's closeNow, and its rule: outside any
	    command sequence, with no walk standing). The search asks for it at its commit point: a SCHEDULED
	    close leaves the chapter open, and no longer held, until the run is over - so an All Documents walk
	    would meet it, list it as a document of the user's, and lose its rows when the close went through
	    after the search. The query run asks for it at its own commit point as well (one rule for both). */
	void ReleaseHeldDocs(bool closeNow = false);

	/** Close THIS chapter, if KFC is the one who opened it AND it has nothing unsaved in it. A
	    chapter the user already had open is not held and is left alone, so a run can hand back every
	    chapter it walked without keeping track of who opened which. UI-suppressed exactly like
	    ReleaseHeldDocs, and like it, it refuses to close a chapter with unsaved work in it.

	    closeNow decides WHEN, and it is not a detail. The default SCHEDULES the close
	    (IDocFileHandler::kSchedule), which does not happen until the current notification or idle
	    tick has unwound - and a RUN does not unwind until it returns. Measured by counting the .indd
	    lock files during a four-chapter saving replace with scheduled closes: they went 1, 2, 3, 4 and
	    only fell to zero once the run was over, so "hands each chapter back as it goes" held every
	    chapter to the end after all. closeNow = true closes on the spot (kProcess), which is what a run needs
	    when the whole point of its shape is to hold one chapter at a time.

	    Only pass true from OUTSIDE a command sequence and with no walk standing - a run that has just
	    ended a chapter's sequence and saved it, which is the case this was added for.

	    A chapter found with a WINDOW is handed to the user, not closed. The panel-raised
	    window this covers is one this module never saw being opened (the book panel windows a held
	    chapter without a word to us); the chapter comes off the held list, nothing is closed, and the
	    answer is TRUE - it is no longer ours to close, which is what "handed back" means.

	    @return true when the chapter was actually handed back - closed, or found with a window and
	            left to the user. false when it was not ours, when it is no longer open, when it
	            holds unsaved changes (and no window), or when the close was refused. The last two
	            leave the chapter ON the held list, so a later release gets another try. A caller
	            that REPORTS having closed chapters has to read this rather than assume: "nothing of ours was open" and
	            "we closed what was" are different facts, and only this can tell them apart - and
	            telling the ordinary "no longer open" from the failures takes IsHeldDoc before plus
	            IsDocStillOpen after - HandBackHeldDocNow, below. */
	bool ReleaseHeldDoc(const UIDRef& docRef, bool closeNow = false);

	/** A RUN HANDS A CHAPTER BACK ON THE SPOT - AND SAYS WHETHER ONE OF OURS IS LEFT STANDING - one
	    answer for the search, Change All in Book and the query run. ReleaseHeldDoc(docRef, closeNow = true),
	    its false read with the two questions it cannot answer alone: was the chapter ours (IsHeldDoc,
	    BEFORE - one the user had open is never ours to close), and is it still open (IsDocStillOpen,
	    AFTER - one the user closed under the run is not left behind). False ONLY for a chapter of
	    ours still standing - windowless, its .indd locked - which the run then names
	    (AppendUnclosedNote). closeNow's rule applies: outside any command sequence, with no walk
	    standing. */
	bool HandBackHeldDocNow(const UIDRef& docRef);

	/** Is this chapter one WE opened - is it on the held list right now?

	    It exists because ReleaseHeldDoc's false cannot be read on its own. That answer
	    covers four different things: the chapter was never ours, it is no longer open, it holds
	    unsaved work, or the close was refused. The first two are the ordinary course of a run and
	    mean nothing is wrong; the last two mean a chapter this plug-in opened WINDOWLESS is still
	    standing, with its .indd locked and no window for the user to find it by. A caller that wants
	    to report the second pair has to be able to tell them apart, and this is the question that
	    does it: ask before the release, and a false afterwards is then a real failure.

	    Cheap - a walk of a list that holds at most a handful of entries. */
	bool IsHeldDoc(const UIDRef& docRef);

	/** Name the chapters a run opened and could NOT hand back, appending to a status line. The
	    companion of AppendUnopenableNote, at the other end of the same run: one says which chapters
	    never opened, this one says which ones never closed.

	    Worth saying because the user cannot see them: a chapter opened windowless has no window, so
	    it can neither be looked at nor closed by hand, and it keeps its .indd locked for the rest of
	    the session. It happens when the chapter came out MODIFIED (a run guards against that with
	    IDataBase::SaveRestoreModifiedState, so it means something else touched it) or when the close
	    was refused.

	    Appends nothing when every chapter was handed back, which is the ordinary case. Built on
	    AppendChapterNote, like AppendUnopenableNote. */
	void AppendUnclosedNote(PMString& outSummary, const std::vector<PMString>& names);

	/** Stop holding this chapter WITHOUT closing it: it has a WINDOW now, so it is the user's and no
	    longer something a run may hand back.

	    A run opens its chapters windowless and closes them again - that is what the held list is
	    for. A chapter that gains a window leaves that arrangement: it got one because the user asked
	    to be taken there (a jump, a document row) or because a replace landed in it, and closing it
	    afterwards would take away a window they are working in, along with anything they have typed
	    or replaced into it since (the user: "a document the user opened by jumping should not be
	    closed, even if nothing was replaced in it").

	    WHO CALLS IT, by name - not only "wherever a window is given".
	      * ShowChapterWindow, both exits (it opened one, or it found one already there);
	      * KFCJump (the UI half, through IKFCChapters), twice, when a jump brings a chapter to the front;
	      * CloseDisplayedDocsIfClean, on every document it is about to close - a window makes it the
	        user's whoever raised it, so the claim goes whether or not the close goes through;
	      * KFCCloseDocResponder, on EVERY document close in the session, so a chapter
	        somebody else closed cannot leave a dangling (IDataBase*, UID) on the held list.

	    Does nothing when the chapter is not held, so it is safe to call on any document. */
	void ForgetHeldDoc(const UIDRef& docRef);

	/** Does this chapter entry name a FILE at all?

	    A DOCUMENT-scope row does not: it is an open document (DocAsChapter), and it is carried as a
	    docRef with an empty file beside it. A BOOK chapter is expected to, and MEASURED, it does even
	    when the .indd has been deleted behind the book's back - the entry keeps the link and the
	    book calls it MISSING_DOCUMENT, so IBookContent::GetIDFile still answers kTrue with the path
	    intact (work/kbs-selftest/run-getidfile-probe.ps1).

	    "Expected to", not "always". That header promises nothing of the kind - it
	    "returns kTrue if a file can be obtained for the book content, kFalse otherwise"
	    (IBookContent.h:121-125). What rests on it is not small: the callers below read a false as
	    "this is a document-scope row", and that is the answer allowed to fall back on a docRef the
	    search left behind. So the book's answer is not assumed here: ListBookChapters gives an entry the
	    book says names no file an EMPTY file (see ChapterDoc), this function answers false for it - and
	    such an entry never gets a docRef to fall back on, because both doors that could give it one
	    refuse it: OpenChapterDoc's already-open lookup (KFCDocumentLivesInFile matches nothing against an
	    empty file) and ReopenChapterDoc (nothing to open by). It is reported as a chapter that could not
	    be opened.

	    That difference is what tells ReopenChapterDoc's two failures apart. It answers
	    false both when there was nothing to open BY and when the file would not open, and a caller
	    has to know which: only the first may fall back on the docRef the search left behind. The
	    second must give up instead, because that docRef belongs to a document which was closed when
	    the search finished - and asking IsDocStillOpen about a closed one is the address-reuse fault
	    (a UIDRef is only (IDataBase*, UID), so a reused address with a matching UID answers YES about
	    a DIFFERENT document). ReachChapterDoc, below, tells the two apart.

	    Asked through the same SDKFileHelper::GetPath() ReopenChapterDoc itself asks - through this
	    very function - so the two cannot come to differ. */
	bool ChapterHasFile(const IDFile& file);

	/** Reopen a chapter by its file: if it is open already (the user's copy, or a conversion of an
	    older InDesign's chapter - see KFCDocumentLivesInFile), rebind to that copy and do not hold
	    it; otherwise open it windowless + UI-suppressed and hold it. The (re)opened document is
	    returned in outDocRef. false = cannot reopen (missing file, locked) - or there was no file to
	    open by at all, which ChapterHasFile is what tells apart.
	    Used by every run that walks a book's chapters (through OpenChapterDoc), and through
	    ReachChapterDoc by the jump, the replace's resolve pass and a row menu's Replace - a chapter is
	    closed as soon as each walk is done with it, so this is how any of them reaches one again. */
	bool ReopenChapterDoc(const IDFile& file, UIDRef& outDocRef);

	/** A result chapter's document, LIVE - the one question the jump (KFCJump
	    EnsureChapterReachable), the replace's resolve pass and a row menu's Replace all ask, in one
	    spelling.

	    BY FILE FIRST, never gated on IsDocStillOpen. ioDocRef is what the results
	    hold, and its document may have been closed since the search: a UIDRef is only
	    (IDataBase*, UID), so once the address is reused by a document opened afterwards and the
	    UID lands the same (chapters built the same way share internal UIDs), IsDocStillOpen
	    answers YES about a DIFFERENT document - measured: 2 to 4 book replaces in 10 walked a
	    neighbour and reported every row missing, never in the first chapter. ReopenChapterDoc asks
	    by FILE, which cannot be confused.

	    ONLY A CHAPTER WITH NO FILE FALLS BACK ON ioDocRef - a document-scope row
	    (one of the documents Search: named), which nothing closed behind anybody - and only while it is still
	    open. A file that would not open (moved, deleted, in use) gives up: that ioDocRef is the
	    one the search left.

	    @return true = ioDocRef is live now (the reopened document, or the one it held). The
	            caller rebinds the model to it (KFCResultModel::RebindChapterDoc - a no-op when
	            it is the same). */
	bool ReachChapterDoc(const IDFile& file, UIDRef& ioDocRef);

	/** ReachChapterDoc WITHOUT THE OPEN: the chapter's
	    document if it is open now - by its file, the same lookup ReopenChapterDoc makes before it
	    opens anything; a chapter with no file (a document-scope row) by IsDocStillOpen(ioDocRef).
	    For everything that acts on an open document only - KFCUndoFollow and a hit row's right-click.
	    Not IsDocStillOpen of the docRef the results hold: a chapter the user closed and opened again
	    reads "not open" by it (a new address) - and one whose address a document opened later has
	    taken reads as THAT document (the address-reuse fault above).
	    @return true = ioDocRef is the open document now. The caller rebinds the model to it. */
	bool FindOpenChapterDoc(const IDFile& file, UIDRef& ioDocRef);

	/** Give a chapter that is open WITHOUT a window (the search opens them that way) a real layout
	    window, so the user can see what a replace did to it. Does NOT save, and does not bring an
	    already-visible document to the front - a document that already has a window anywhere,
	    including behind another tab, is left exactly as it is.

	    @return true when the chapter HAS a window afterwards - whether this call opened it or it
	            already had one. false means it has none and the user cannot see it, which for a
	            chapter a replace has just written to is worth reporting: the run leaves every
	            chapter unsaved for the user to deal with, and one with no window cannot be dealt
	            with. */
	bool ShowChapterWindow(const UIDRef& docRef);

	/** The "Hide Previous Chapter" sweep: close every OTHER document that HAS a window and needs no
	    save, on schedule - whoever opened it. The exception document (the one a jump just landed in)
	    and any windowless held chapter survive. (Runs close each chapter as they finish with it, so a
	    windowless held chapter is left only by a failure - a jump or a replace whose window did not
	    appear.)

	    "Needs no save" is IDocFileHandler::CanSave, "modified OR UNSAVED" - so what stays is not
	    only a dirty document but also one that has never been saved at all: the untitled document
	    the user just made with Ctrl+N, which reads as unmodified and would otherwise be closed
	    without a prompt. See HasUnsavedChanges in the .cpp for the measurement.
	    The one exception to CanSave is a CONVERSION of an older InDesign's chapter, which it calls
	    unsaved from the start: that is asked what was written to it (HasUnsavedWork in the .cpp),
	    so a converted chapter a jump opened goes like any other clean one. */
	void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc);

	/** Application-shutdown cleanup (state only, no closing, no UI): forget the held-chapter
	    list without closing anything - the quitting application closes every document itself. */
	void ShutdownCleanup();
}

#endif // __KFCBookScope_h__
