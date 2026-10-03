//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Book-wide search scope implementation. See KBSBookScope.h for the overall contract.
//  Ported from KESCLBookScope (KESCL left untouched).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryDocumentList - the open documents (ISession's own is "for internal use only")
#include "IBook.h"
#include "IBookContent.h"
#include "IBookContentMgr.h"
#include "IBookManager.h"
#include "IBookUtils.h"			// FindDocFromContentUID, GetBookContentStatus
#include "IDataBase.h"			// the IDataBase* a UIDRef carries, and GetSysFile
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open by file (windowless reopen)
#include "IDocumentList.h"
#include "IDocumentUtils.h"
#include "IActiveContext.h"		// GetContextDocument - ActiveDocument, the document-scope half of HasScopeTarget
#include "IOpenFileCmdData.h"	// kOpenDefault / kUseLockFile
// (IMenuUtils.h was here for InsertAmpersandForDisplay until 2026-08-03. The status line doubled
// its own ampersands for the whole message, so doubling a chapter name here as well ran it twice -
// see AppendChapterNote. Since 2026-09-29 nothing doubles them: the message area draws '&' as it is.)
#include "IOpenedFileInfo.h"	// the file an older-version chapter's conversion was opened from
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"			// GlobalErrorStatePreserver / PMSetGlobalErrorCode - an open or a close
								// that is allowed to fail must not poison the caller's next command
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "CmdUtils.h"
#include "K2Vector.h"			// the held-chapter list (it came in through IBookUtils.h until 2026-08-08)
#include "FileUtils.h"		// IsEqual - is this open document that chapter's file
#include "SDKFileHelper.h"
#include "UIDList.h"
#include "Utils.h"
#include "PMString.h"
#include "WideString.h"

// Project includes:
#include "KBSBookScope.h"
#include "KBSDiag.h"			// KBS_DIAG_FAULT / KBS_DIAG_LOG - a test build's fault switch (compiled out of a shipping one)
#include "IKBSUIServices.h"		// the windows and the Book panel are the UI half's (2026-10-01)

namespace
{
	// The chapters WE opened (only the originally-closed ones), held so that ReleaseHeldDoc can hand
	// each one back once its turn is over. A chapter the user already had open never goes on here.
	//
	// ***** A PLAIN VECTOR, because nothing is handed to the book API any more. ***** This used to be
	// IBookUtils' own OriginallyCloseDocInfo - the container OpenOneDocument fills in and
	// CloseDocumentsInBook drains - and this plug-in stopped calling BOTH of those in 2026-07 (no UI
	// suppression on that open, no UI flag or command mode on that close; the reasons are at
	// OpenChapterDoc and ReleaseHeldDoc). Keeping their container afterwards only suggested a
	// partnership that is not there.
	K2Vector<UIDRef> gHeldDocs;

	// Which book the RESULTS on the panel came from (its full file path). NOT "which book we hold
	// chapters for": since 2026-08-02 every run closes each chapter as soon as it is done with it,
	// so the held list is empty most of the time while a full result set is still on screen.
	//
	// Read from OUTSIDE through GetSearchedBookPath, by two callers, and both ask about the RESULTS:
	//   * KBSBookWatch - "the book these results name has been closed, retire them"
	//   * KBSJump's ShowBook (the UI half, through IKBSChapters) - "the book row was clicked, bring
	//     that book forward"
	// And read from INSIDE by OpenChapterDoc, which is not about the results at all: it is how a
	// chapter finds the book it belongs to, having deliberately not parked an IBook* between calls.
	//
	// Let go through ReleaseSearchedBook, which goes with every KBSResultModel::Clear()
	// (KBSSearchEngine::DropResults).
	PMString gSearchedBookPath;

	// The search-scope toggle. Session state only (every launch starts OFF), like KESCL's
	// gBookSearchOn: ON searches the whole target book (ResolveTargetBook); OFF, what Edit >
	// Find/Change's Search: names (2026-09-29 - the front document until then).
	bool gBookScopeOn = false;

	// (The book panel's learned class, IsBookPanelView and GetBookFileFromPanelView stood here until
	//  2026-10-01 - the UI half's KBSBookPanelLookup.cpp now, with their notes.)

	/** The book's own word for a chapter's state, for the "could not be opened" report. Empty for
	    a chapter the book considers fine - then the failure is something the book does not track
	    (a lock file, permissions) and there is nothing honest to add. Not translatable: these are
	    short internal words, in English like the rest of this panel's status line. */
	const char* BookContentStatusText(BookContentStatus::State state)
	{
		switch (state)
		{
			case BookContentStatus::kDocMising:		return "file is missing";	// (sic - the SDK spells it this way)
			case BookContentStatus::kDocOutofDate:	return "out of date";
			case BookContentStatus::kDocInUse:		return "in use elsewhere";
			case BookContentStatus::kDocOpen:		return "already open";
			case BookContentStatus::kDocNormal:		return "";
			default:								return "";
		}
	}

	/** Is there work in this document that closing it would throw away?

	    Asked before every one of this module's UI-SUPPRESSED closes. IDocFileHandler::Close only
	    offers to save "if uiFlags allow" (IDocFileHandler.h:97-101), so a modified document closed
	    with kSuppressUI loses what is in it silently - no prompt, no undo, no file on disk. The
	    hide-previous-chapter sweep has always asked this (CloseDisplayedDocsIfClean); the held-chapter
	    releases did not, which is what let a replace that the user chose NOT to save disappear when
	    the next run reclaimed the chapter it was in.

	    ***** ASKED THE WAY THE SDK ASKS IT - CanSave, NOT IDataBase::IsModified. ***** Those are two
	    different questions, and the difference is a whole class of document:

	      IsModified()  = "has been modified since LAST SAVE"   (IDataBase.h:253-256)
	      CanSave()     = "is modified OR UNSAVED"              (IDocFileHandler.h:62-63)

	    A document that has never been saved AT ALL - the untitled one the user just made with
	    Ctrl+N - answers NO to the first and YES to the second. This function asked the first until
	    2026-08-10, so the hide-previous-chapter sweep took such a document for clean and closed it
	    with kSuppressUI: no prompt, and nothing on disk to reopen. MEASURED on the running
	    application that day (work/kbs-selftest/run-untitled-sweep-test.ps1), which also drew the
	    line either side of it: a fresh document is modified=false, and it turns dirty the moment
	    ANYTHING is added to it - so what was being closed was always empty, and no work was ever
	    lost. Corrected anyway: a window that shuts by itself is its own kind of wrong, and the
	    official question was right there. (The official close sequence asks them in this order as
	    well - CanSave, then CanClose, then Close: SDKLayoutHelper.cpp:200-217.)

	    ***** A document this cannot be asked about is NOT closed. ***** No file handler means no
	    answer, and the safe answer to "would closing this lose something" is yes - which every close
	    reaches anyway: each one refuses a document whose handler will not come. */
	bool HasUnsavedChanges(const UIDRef& docRef)
	{
		InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
		if (docFileHandler == nil)
			return true;
		return docFileHandler->CanSave(docRef) != kFalse;
	}

	/** Is this document an in-memory CONVERSION of a file an older InDesign saved - one with no file
	    of its own?

	    That is what opening an old chapter gives: "Templates and converted documents open as new
	    files which lose information such as file path" (IOpenedFileInfo.h:31-33). Measured
	    2026-09-25 on a CC 2017 chapter: IsConverted() true, never saved, no file - so every test in
	    this module that goes BY FILE answers "a different document" about it, and CanSave answers
	    "unsaved" whether or not anything was written to it. Both of those used to strand it: each
	    search opened another copy, and none of them could be closed (see HasUnsavedWork and
	    KBSDocumentLivesInFile).

	    A converted document the user has since SAVED has a file again and is an ordinary document -
	    hence the second test. */
	bool IsFilelessConversion(const UIDRef& docRef)
	{
		IDataBase* const db = docRef.GetDataBase();
		if (db == nil || db->GetSysFile() != nil)
			return false;
		InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
		return doc != nil && doc->IsConverted() != kFalse;
	}

	/** HasUnsavedChanges, with the one kind of document it answers wrongly for put right: the
	    question every close in this module asks - the held-chapter release (ReleaseHeldDoc, which
	    ReleaseHeldDocs goes through) and the hide-previous-chapter sweep.

	    ***** THE ONE DIFFERENCE IS A CONVERTED CHAPTER. ***** CanSave says "modified OR unsaved", and a
	    conversion is unsaved from the moment it exists, so it read as work to protect after every
	    read-only walk and was never let go (2026-09-25: two searches of a book with one CC 2017
	    chapter left two windowless copies of it open, and nothing could close them). What closing
	    one would really throw away is only what has been WRITTEN to it - the chapter's own file on
	    disk is untouched by the conversion - and that is what IsModified answers, because
	    ReopenChapterDoc marks a conversion it opens clean as it opens it. A replacement that landed
	    in one sets the flag again, and it is kept like any chapter with work in it.

	    ***** AND THE SWEEP ASKS IT TOO (2026-09-25 re-check). ***** It was first given to the held
	    releases only, under the name HeldDocHasUnsavedWork, and the sweep went on asking CanSave - so
	    a converted chapter a jump had put in a window was the one window "Hide Previous Chapter" never
	    closed. The sweep also meets conversions the USER opened; for those the same answer holds -
	    closing one loses only what was written to it, and the old file on disk is untouched. */
	bool HasUnsavedWork(const UIDRef& docRef)
	{
		if (IsFilelessConversion(docRef))
		{
			IDataBase* const db = docRef.GetDataBase();
			return db == nil || db->IsModified() != kFalse;
		}
		return HasUnsavedChanges(docRef);
	}

	/** Does this document have a WINDOW anywhere - front, or behind another tab?

	    Asked by the held-chapter releases since 2026-08-05: a window makes a chapter the USER'S,
	    whoever raised the window. ShowChapterWindow and the jump take a chapter off the held list
	    when they raise one themselves, but a window can be raised behind this module's back - the
	    book panel lists every chapter, and double-clicking one there windows the very document being
	    held.

	    ***** ASKED OF THE UI HALF SINCE 2026-10-01 (the model/UI split). ***** Presentations are user
	    interface (IDocumentUIUtils); the all-presentations search and its notes - and the local
	    predicate Adobe asks for - are IKBSUIServices::DocHasAnyWindow now (KBSUIServices.cpp). No UI
	    (a background thread, InDesign Server) has no window to show a document in: false. */
	bool DocHasAnyWindow(const UIDRef& docRef)
	{
		InterfacePtr<IKBSUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
		return ui != nil && ui->DocHasAnyWindow(docRef);
	}

	/** The OPEN book whose file path is 'bookPath', or nil when none has it. Non-owning - the book
	    manager keeps the list, so nothing here is released.

	    A path rather than an IBook* because one caller is a close notification: the closing book's
	    IBook is already gone by the time we can act on it, so there is no pointer left to compare. */
	IBook* FindOpenBookByPath(const PMString& bookPath)
	{
		if (bookPath.IsEmpty())
			return nil;

		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		if (bookMgr == nil)
			return nil;

		// The book API's own lookup by file - "Search to see if whatBook is already open or not.
		// Returns nil means whatBook is not open" (IBookManager.h:144-149) - which is the same
		// question ListBookChapters asks it further down this file. It used to be a walk of
		// GetBookCount/GetNthBook comparing paths here, so the one module had two ways of asking
		// (block 11 API audit, 2026-08-08).
		SDKFileHelper bookFileHelper(bookPath);
		IBook* book = bookMgr->FindOpenBookByName(bookFileHelper.GetIDFile());	// non-owning
		if (book == nil)
			return nil;

		// ***** AND THEN IsOpen(), WHICH IS NOT REDUNDANT. ***** Measured 2026-07-27 on the release
		// build: when kCloseBookCmdBoss is broadcast, the closing book is STILL on IBookManager's
		// list, so anything that answers "is it listed" says "yes, still open" about the very book
		// that is closing - which made the book watcher's guard reject the one case it exists for.
		// IBook::IsOpen is the flag the close clears. Do not drop this test when tidying: the lookup
		// above answers a different question from the one this function is asked.
		return book->IsOpen() ? book : nil;
	}

	/** The book a book-scope run would be against: the book the BOOK PANEL is showing, and failing
	    that the active book. Nil when neither can be had.

	    ***** ONE DEFINITION, because two callers have to give the SAME answer. ***** The run resolves
	    its book here (ListBookChapters) and the menu's grey state asks whether there would be one
	    (GetTargetBook); if those two ever disagree, either a command greys out over a book the run
	    could have searched or it starts a run that reports "no book" - which is the shape this
	    plug-in keeps being bitten by.

	    They were two spellings of the same two steps from 2026-08-09 to 2026-08-10, and the second
	    had already come out differently (the run dropped an active book that was already closing). */
	IBook* ResolveTargetBook()
	{
		// The panel's book first. Selecting a book's tab switches the panel but does NOT make that
		// book active (measured 2026-07-27), so a user who picks a tab and runs a search would
		// otherwise get whatever book was active before - see the long note in ListBookChapters.
		// ...as the UI half observes it (IKBSUIServices::GetPanelBookFile, 2026-10-01: the Book panel is user
		// interface). No UI - a background thread, InDesign Server - is "no panel", and the fallback below
		// answers, the same as an iconised panel always has.
		IDFile panelBookFile;
		InterfacePtr<IKBSUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
		if (ui != nil && ui->GetPanelBookFile(panelBookFile))
		{
			SDKFileHelper panelHelper(panelBookFile);
			IBook* panelBook = FindOpenBookByPath(panelHelper.GetPath());	// nil = not open, or closing
			if (panelBook != nil)
				return panelBook;
		}

		// The fallback: the active book, which is what the user expects when no book panel can be
		// reached (it is iconised, or its palette is closed).
		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		if (bookMgr == nil)
			return nil;

		// GetCurrentActiveBook hands out a non-owning pointer - no release. The IsOpen() test is the
		// same closing-book door FindOpenBookByPath documents and applies to the panel-side answer:
		// a book still broadcasting its close is on every list there is, and is not a book anything
		// may be run against.
		IBook* activeBook = bookMgr->GetCurrentActiveBook();
		return (activeBook != nil && activeBook->IsOpen()) ? activeBook : nil;
	}
}

bool KBSBookScope::IsBookScopeOn()
{
	return gBookScopeOn;
}

void KBSBookScope::SetBookScopeOn(bool on)
{
	// Just the flag. Nothing is closed and nothing is cleared here: KESCL's first shape closed the
	// held chapters right inside the toggle and crashed (see KESCLBookScope::SetBookSearchOn). The
	// held chapters are released by the next search (either scope) or Show Changes at its commit
	// point, or when their book closes - not at shutdown, which only forgets them (ShutdownCleanup) -
	// and a jump into a chapter the user closed meanwhile goes through ReopenChapterDoc anyway.
	gBookScopeOn = on;
}

IDocumentList* KBSBookScope::QueryOpenDocumentList()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	return (app != nil) ? app->QueryDocumentList() : nil;
}

bool KBSBookScope::IsDocStillOpen(const UIDRef& docRef)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	InterfacePtr<IDocumentList> docList(KBSBookScope::QueryOpenDocumentList());
	if (docList == nil)
		return false;

	// The document list's own lookup by database (IDocumentList.h:71-76), which is how the rest of this
	// plug-in asks this (the UI half's KBSHitMarkerView.cpp) and how KCM asks it everywhere. It replaced a
	// walk of GetDocCount/GetNthDoc comparing UIDRefs (block 11 API audit, 2026-08-08).
	//
	// ***** THE DATABASE POINTER IS COMPARED, NEVER DEREFERENCED. ***** For a chapter closed since
	// its UIDRef was taken that pointer is dangling, and asking it anything is undefined behaviour -
	// which is the whole reason callers have this function instead of reading the document. Passing
	// it to FindDocByDataBase only matches it against the list. The UID is then checked as well, so
	// the answer stays exactly as strict as the walk it replaced.
	IDocument* doc = docList->FindDocByDataBase(db);
	return doc != nil && ::GetUIDRef(doc) == docRef;
}

bool KBSBookScope::HasWindow(const UIDRef& docRef)
{
	return IsDocStillOpen(docRef) && DocHasAnyWindow(docRef);
}

void KBSBookScope::ReleaseHeldDocs(bool closeNow)
{
	// NOTE: the searched-book path is NOT cleared here. Closing the chapters says nothing about
	// which book the panel is showing - and since every run closes its chapters as it goes, doing
	// so would blank the path while a full result set is still up (which broke both readers named
	// on gSearchedBookPath).
	//
	// ***** EACH ONE THROUGH ReleaseHeldDoc, ON A SCHEDULE (2026-10-01). ***** This wrote the same four
	// questions and the same close out a second time until then - "the same function written twice, and
	// only one of them was covered", as a note in that one said of the error-state guard. Every verdict
	// is that function's: no longer open = dropped, a window = dropped and left to the user, unsaved work
	// or a refused close = kept on the list for a later call, otherwise closed (kSchedule - this runs
	// from notifications - unless the caller asks for closeNow, see the header). A copy is walked, because
	// ReleaseHeldDoc edits the list; a chapter a re-entrant call has already handed back is no longer on it
	// and answers false here.
	const K2Vector<UIDRef> held = gHeldDocs;
	for (int32 i = 0; i < static_cast<int32>(held.size()); ++i)
		(void)ReleaseHeldDoc(held[i], closeNow);
}

bool KBSBookScope::IsHeldDoc(const UIDRef& docRef)
{
	if (docRef == UIDRef::gNull)
		return false;

	// The same walk ReleaseHeldDoc opens with. Kept as its own function rather than folded into that
	// one's answer because the two questions are asked at different MOMENTS - this one before the
	// release, that one after - and only the pair of them says whether a failure to close was real.
	// See the header.
	for (int32 i = 0; i < static_cast<int32>(gHeldDocs.size()); ++i)
	{
		if (gHeldDocs[i] == docRef)
			return true;
	}
	return false;
}

bool KBSBookScope::HandBackHeldDocNow(const UIDRef& docRef)
{
	const bool wasOurs = IsHeldDoc(docRef);
#ifdef KBS_DIAG
	// Test builds only: the fault switch keep-held (KBSDiag.h) leaves a chapter of ours held, clean and
	// windowless after its run - the state a window that would not open leaves behind (2026-10-02, B8-2's
	// case held-chapter-alldocs). Answered as handed back, so the run's summary says nothing about it.
	if (wasOurs && KBS_DIAG_FAULT("keep-held"))
	{
		KBS_DIAG_LOG("FAULT keep-held: doc=%p uid=%u stays held", (void*)docRef.GetDataBase(), docRef.GetUID().Get());
		return true;
	}
#endif
	return ReleaseHeldDoc(docRef, true /*close now*/) || !wasOurs || !IsDocStillOpen(docRef);
}

bool KBSBookScope::ReleaseHeldDoc(const UIDRef& docRef, bool closeNow)
{
	if (docRef == UIDRef::gNull)
		return false;

	// Ours to close? A chapter the user already had open is not on this list and must stay. That
	// test lives HERE rather than at every call site: a run walks its chapters without caring who
	// opened which, and hands every one of them back the same way.
	int32 heldIndex = -1;
	for (int32 i = 0; i < static_cast<int32>(gHeldDocs.size()); ++i)
	{
		if (gHeldDocs[i] == docRef)
		{
			heldIndex = i;
			break;
		}
	}
	if (heldIndex < 0)
		return false;

	// ***** AND FROM HERE ON THE ERROR STATE IS THIS FUNCTION'S OWN - PRESERVE, THEN CLEAR. ***** Close
	// reports nothing at all (IDocFileHandler.h:101), so a failure inside it can only speak through the
	// global error state - and one left standing fails every command after it, starting with the next
	// chapter's close (or, with kProcess BETWEEN a run's chapters, the next chapter's whole walk). The
	// pair is the one Adobe's own base class spells out (CDialogObserver.cpp:392-394; the contract is
	// ErrorUtils.h:115-117): the preserver puts the CALLER's state back when this returns - clearing it
	// is not this function's to decide - and the clear in front of the close keeps one chapter's failure
	// from shadowing the next.
	//
	// ***** IT COVERS THE THREE TESTS BELOW AS WELL, not just the close. ***** They are not silent
	// readings: DocHasAnyWindow goes through IDocumentUIUtils (in the UI half since 2026-10-01) and
	// HasUnsavedChanges through QueryDocFileHandler + CanSave, and NONE of the three reports a failure
	// back, which is the very property the preserver exists for.
	GlobalErrorStatePreserver closeErrorState;

	// ***** IS IT STILL OPEN? ASKED FIRST, AND THE ORDER IS THE WHOLE POINT. *****
	// Everything below this line reads the document. DocHasAnyWindow takes the database out of the
	// UIDRef and hands it to IDocumentUIUtils (through the UI half); HasUnsavedChanges hands the UIDRef itself to
	// QueryDocFileHandler and then to CanSave. A UIDRef is only (IDataBase*, UID), so for a chapter
	// that has been closed since it was held that pointer is dangling, and handing it to anything at
	// all is undefined behaviour. IsDocStillOpen is the one question that does not: it compares the
	// pair against the application's open-document list without following it. ReleaseHeldDocs hands over
	// chapters the user may have closed since, and the header promises that a chapter which "is no
	// longer open" may be passed in.
	//
	// Off the list when it goes, because a chapter nobody has open any more is not something a later
	// call can hand back either.
	if (!IsDocStillOpen(docRef))
	{
		gHeldDocs.erase(
			gHeldDocs.begin() + heldIndex);
		return false;
	}

	// ***** A window makes it the user's, whoever raised it. ***** ShowChapterWindow and the jump
	// take a chapter off this list when THEY raise one; this catches a window raised behind this
	// module's back (the book panel windows a held chapter without a word to us - see
	// DocHasAnyWindow). Claim dropped, nothing closed - and TRUE, not false: the chapter is no
	// longer this module's to close, which is what "handed back" means to every caller. Closing it
	// instead would take a window the user is looking at; once they had saved their work, the
	// unsaved-work door below would not even stand in the way any more.
	if (DocHasAnyWindow(docRef))
	{
		gHeldDocs.erase(
			gHeldDocs.begin() + heldIndex);
		return true;
	}

	// ***** Unsaved work in it? Then it is not ours to close. ***** Asked BEFORE it comes off the
	// list, so it stays held and a later call can hand it back once it has been saved. Reached when
	// a chapter this plug-in opened has been written to and not saved - which since 2026-08-08 is
	// known to mean ONE thing: a replace landed in it and its window would not open (the window test
	// just above drops anything a user could type into). The whole of it is in ReleaseHeldDocs'
	// header. See HasUnsavedChanges - and HasUnsavedWork, which asks it of a converted chapter by what
	// was WRITTEN rather than by "has never been saved", the thing every conversion is (2026-09-25).
	if (HasUnsavedWork(docRef))
		return false;

	// The stock document close: kSchedule defers it until the current notification / idle tick has
	// unwound, and kSuppressUI plus the run's dirty guard (IDataBase::SaveRestoreModifiedState, which
	// wraps every walk) means no save prompt.
	//
	// WHY NOT the book API's own IBookUtils::CloseDocumentsInBook(OriginallyCloseDocInfo&), the
	// documented partner of the OpenOneDocument this plug-in stopped calling: it takes no UI flag and
	// no command mode, so it closes IMMEDIATELY and with whatever UI it likes. KESCL called it and
	// CRASHED (2026-07-17) - the toggle it ran from was a widget notification, and closing a document in
	// the middle of one is the wrong context. That helper is written for InDesign's own TOC / index
	// commands, which run from a command. Do not "improve" this back to it. (docs/ai-notes/book-api.md)
	//
	// ***** A REFUSED CLOSE STAYS HELD, exactly as unsaved work does. ***** These two exits sat
	// BELOW the erase until 2026-08-08, so a chapter whose close was refused fell off the held list
	// in the same breath as it failed - windowless, its .indd locked, and no later ReleaseHeldDocs
	// able to find it again.
	//
	// Cleared immediately in front of the close - a standing error would fail this one. WHOSE state is
	// being taken away here and whose is not: see the preserver at the top of this function.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (docFileHandler == nil)
		return false;
	if (!docFileHandler->CanClose(docRef))
		return false;

	// Off the list BEFORE the close goes through, so a re-entrant call cannot schedule the same
	// close twice - and not a line earlier, for the reason above.
	gHeldDocs.erase(
		gHeldDocs.begin() + heldIndex);

	// ***** kProcess closes NOW; kSchedule closes when the caller has finished. ***** A run asks for
	// the first (see the header): a scheduled close does not happen until the current tick unwinds,
	// and a run is that tick - so every chapter it "handed back" was still open, and still locking
	// its .indd, until the whole run was over. Measured 2026-08-04 on a four-chapter saving replace.
	//
	// ***** AND kProcess IS ONLY LEGAL BECAUSE THE WINDOW TEST ABOVE HAS ALREADY RUN. ***** Closing
	// a document that HAS a window with kProcess is called out as an error by the only Close
	// implementation this SDK ships - InCopy's - which asserts on it outright: "Close() illegal with
	// open document windows and cmdMode == kProcess" (InCopyDocUtils::DoClose, at
	// incopyfileactions/utils/InCopyDocUtils.cpp:1376-1383). That is InCopy's document file handler,
	// NOT the layout handler that will take our chapters, but it is the only statement anyone has made
	// about that combination, and it is not a rule worth testing: the DocHasAnyWindow door above
	// returns before this line for every windowed chapter, so what reaches here is windowless by
	// construction. Keep that order if either test is ever moved.
	docFileHandler->Close(docRef, kSuppressUI, kFalse /*allowCancel*/,
		closeNow ? IDocFileHandler::kProcess : IDocFileHandler::kSchedule);
	return true;
}

void KBSBookScope::ReleaseSearchedBook()
{
	// Both halves, always together. Letting the path go without the chapters would strand them:
	// nothing else remembers which book they belong to, so the book watcher - whose only question
	// is "is OUR book still open" - would have no book left to ask about, and they would keep their
	// .indd files locked for the rest of the session.
	//
	// This is the second of the two holes KBSBookWatch's header records (a book run followed by a
	// DOCUMENT-scope run). Closing the chapters here fixes it at the moment the results are dropped,
	// rather than leaving it for a book close that may never come.
	ReleaseHeldDocs();
	gSearchedBookPath.Clear();
}

void KBSBookScope::ShutdownCleanup()
{
	// State only, no closing, no UI: this runs while InDesign is tearing down. Clear() releases
	// the vector's storage too, so the static destructor at DLL unload finds nothing to do.
	gHeldDocs.clear();
	gSearchedBookPath.Clear();
	gBookScopeOn = false;
}

// (KBSAcceptAnyPresentation lived here until 2026-08-05. It went with DocHasAnyWindow's window search
// to the UI half on 2026-10-01, and is the SDK's FindPresCriteria::accept_all there - KBSUIServices.cpp.)

// Does this open document live in that file? Asked through IDataBase::GetSysFile - "the file
// associated with the database" (IDataBase.h:270-274), which is how the SDK's own samples read a
// document's file (persistentlistui/PstLstUITVHierarchyAdapter.cpp:97).
//
// A document that has never been saved has no file and can never be the chapter being looked for -
// with ONE exception, below.
//
// ***** A CONVERSION OF THE CHAPTER'S OWN FILE IS THE CHAPTER. ***** Opening a chapter an older
// InDesign saved gives an in-memory conversion with no file of its own ("converted documents open
// as new files which lose information such as file path. At this point, only file path is being
// preserved" - IOpenedFileInfo.h:31-33): what the document still knows is the file it was OPENED
// FROM, and that is the chapter. Without this every lookup answered "not open" about it, and each
// search, jump and replace opened one more copy (measured 2026-09-25: two searches and two jumps
// left four copies of one CC 2017 chapter, two of them in windows).
//
// Asked only of a document with NO file: one the user has saved under another name has a file of
// its own and is that file's document, not the chapter's, whatever it was opened from.
//
// ***** THE FILES ARE COMPARED, NOT THEIR PATH STRINGS (2026-09-27, the open/close re-check R-1). *****
// FileUtils::IsEqual, which is what the product's own lookup-then-check does
// (buttonui/.../GoToAnchorPanelObserver.cpp, FindDocInDocList) and what KCM asks (KCMOpenDocOnFile).
// Measured with the path strings: a chapter the user had open as Q:\hb\hb1.indd (a subst of the
// book's folder) read as "not this chapter", the open that followed hit the chapter's lock, and the
// search reported "1 chapter(s) could not be opened (hb1.indd: in use elsewhere)" - that chapter's
// hits were silently left out (work/kbs-selftest/r1-path-spelling.ps1).
static bool KBSDocumentLivesInFile(IDocument* doc, const IDFile& wanted)
{
	if (doc == nil)
		return false;
	IDataBase* db = ::GetDataBase(doc);
	if (db == nil)
		return false;
	const IDFile* docFile = db->GetSysFile();
	if (docFile != nil)
		return FileUtils::IsEqual(*docFile, wanted) != kFalse;
	if (!doc->IsConverted())
		return false;
	InterfacePtr<IOpenedFileInfo> openedFrom(doc, UseDefaultIID());
	if (openedFrom == nil)
		return false;
	const IDFile openedFile = openedFrom->GetOpenedFilePath();
	if (!KBSBookScope::ChapterHasFile(openedFile))
		return false;		// nothing recorded to compare with
	return FileUtils::IsEqual(openedFile, wanted) != kFalse;
}

bool KBSBookScope::ChapterHasFile(const IDFile& file)
{
	SDKFileHelper fileHelper(file);
	return !fileHelper.GetPath().empty();
}

// The open document living in a chapter's file, or gNull - OPENS NOTHING. ReopenChapterDoc's first half,
// and FindOpenChapterDoc's whole (2026-09-29: the defect re-check F-3 - the two asked it in one place
// from then). See ReopenChapterDoc for why the answer is checked against the file and why conversions
// are walked for.
static UIDRef KBSOpenDocOfChapterFile(const IDFile& file)
{
	InterfacePtr<IDocumentList> docList(KBSBookScope::QueryOpenDocumentList());
	if (docList == nil)
		return UIDRef::gNull;
	IDocument* openDoc = docList->FindDoc(file);
	if (KBSDocumentLivesInFile(openDoc, file))
		return ::GetUIDRef(openDoc);
	const int32 openCount = docList->GetDocCount();
	for (int32 i = 0; i < openCount; ++i)
	{
		IDocument* candidate = docList->GetNthDoc(i);
		if (candidate == nil || candidate == openDoc || !candidate->IsConverted())
			continue;
		if (KBSDocumentLivesInFile(candidate, file))
			return ::GetUIDRef(candidate);
	}
	return UIDRef::gNull;
}

bool KBSBookScope::FindOpenChapterDoc(const IDFile& file, UIDRef& ioDocRef)
{
	// See the header: by file for a chapter with one; the docRef the results hold only for one without.
	if (!ChapterHasFile(file))
		return IsDocStillOpen(ioDocRef);
	const UIDRef open = KBSOpenDocOfChapterFile(file);
	if (open == UIDRef::gNull)
		return false;
	ioDocRef = open;
	return true;
}

bool KBSBookScope::ReopenChapterDoc(const IDFile& file, UIDRef& outDocRef)
{
	outDocRef = UIDRef::gNull;

	// Through ChapterHasFile rather than testing the path here, so "does this entry name a file"
	// is asked in ONE place: callers have to ask it too, to tell this failure from the one below
	// (see that function's header), and two spellings of one question is how they come to disagree.
	if (!ChapterHasFile(file))
		return false;	// a document-scope entry carries no file - nothing to reopen


	// Is it open already - because the user reopened it, or because an earlier chapter of this very
	// run is still standing? Rebind to THAT document and do NOT hold it: closing something somebody
	// else opened would surprise them.
	//
	// Asked through the session's own lookup by file - "Search to see if one (whatFile) is already
	// open. If so, return it" (IDocumentList.h:64-69) - which is how the product asks it
	// (incopyfileactions/utils/InCopyDocUtils.cpp:3177, buttonui/.../GoToAnchorPanelObserver.cpp:401).
	// A walk of the whole document list stood here until the block 11 API audit (2026-08-08).
	//
	// ***** THE ANSWER IS CHECKED AGAINST THE FILE WE ASKED FOR, NOT TAKEN ON TRUST. ***** The same
	// rule OpenChapterDoc applies to FindDocFromContentUID, and for the same reason. This used to
	// ask IBookUtils::IsSourceDocumentAlreadyOpen, which hands back an INDEX into the document list
	// (IBookUtils.h:314-319), and trusted it - which put a DIFFERENT chapter's document in this
	// chapter's place. Measured 2026-08-04: 4 book replaces in 10 came back with a chapter's rows all
	// marked 'missing' and that chapter never opened at all. The walk had run over the wrong
	// document and every row failed its same-occurrence test - silently, since the call had reported
	// success. The give-away was WHERE it failed: the FIRST chapter never did, because a replace
	// resolves its chapters with the earlier ones still open (1, then 2, then 3 documents standing)
	// while a SEARCH closes each before opening the next and asks with none.
	//
	// So the check stays whatever the lookup is: it costs one file compare, and it is the only
	// thing standing between a wrong answer and a replace in the wrong document.
	//
	// ***** AND A CONVERSION OF IT, WHICH THE LOOKUP BY FILE CANNOT FIND. ***** An older InDesign's
	// chapter opens as a document with no file (see KBSDocumentLivesInFile), so FindDoc answers nil
	// about it even while one stands open - and opening the file again below made a second conversion
	// every time (2026-09-25). A walk of the open documents is the only way to ask; it is short, and
	// KBSDocumentLivesInFile does the matching, so "is this the chapter" is still decided in one place.
	//
	// Both are KBSOpenDocOfChapterFile's (above) since 2026-09-29, which FindOpenChapterDoc asks too.
	{
		const UIDRef open = KBSOpenDocOfChapterFile(file);
		if (open != UIDRef::gNull)
		{
			outDocRef = open;
			return true;
		}
		// Not open - or an answer that is not this file, which is treated the same way, since the
		// open below resolves by file and cannot be confused. ***** NEVER return false from here.
		// ***** "It is not already open" is the ordinary case, and the old code turned two of its own
		// nil checks into a false, which the caller reports as a chapter that could not be opened.
	}

	// The windowless, UI-suppressed open - by FILE (the book may be closed).
	//
	// ***** THIS OPEN IS ALLOWED TO FAIL, so what it raises must not leave this scope. ***** A
	// chapter that will not open is reported to the user as a skipped chapter (the caller builds
	// that list), not as a failure of whatever command runs next - and an error left standing fails
	// all of them. Preserve, then clear: only what the open raised is taken away, and the caller's
	// own state comes back at the closing brace (ErrorUtils.h:115-117).
	//
	// ***** AND THAT IS THE SHAPE THE SDK USES FOR THIS EXACT OPERATION. ***** The lines this
	// function names above as its model - a windowless, UI-suppressed open whose result is then
	// looked up with IDocumentList::FindDoc - wrap that open in a preserver:
	// buttonui/actiondatapanels/gotoanchor/GoToAnchorPanelObserver.cpp:395-401. This file copied the
	// FindDoc half and not the preserver half, and cleared the state bare on the failure path
	// instead (found in the block 11 re-audit, 2026-08-10): one exit guarded, and the caller's error
	// thrown away along with the open's.
	UIDRef docRef;
	ErrorCode err = kFailure;
	{
		GlobalErrorStatePreserver openErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		err = Utils<IDocumentCommands>()->Open(&docRef, file, kSuppressUI,
			IOpenFileCmdData::kOpenDefault, IOpenFileCmdData::kUseLockFile, kFalse /*showInWindow*/);
	}
	if (err != kSuccess || docRef == UIDRef::gNull)
		return false;

	// ***** A CONVERSION STARTS OUT CLEAN. ***** A chapter an older InDesign saved has just been
	// converted in memory, and the conversion is not the user's work: the chapter's own file is
	// untouched and closing this copy loses nothing. Marking it unmodified here is what lets the
	// releases tell a copy that only a walk has read from one a replacement has written to
	// (HasUnsavedWork) - the walks guard the flag, a replacement sets it. (2026-09-25; until
	// then every such copy counted as unsaved work and was never closed.)
	if (IsFilelessConversion(docRef))
	{
		IDataBase* const openedDB = docRef.GetDataBase();
		if (openedDB != nil)
			openedDB->SetModified(kFalse);
	}

	// Held, so ReleaseHeldDocs closes it later.
	gHeldDocs.push_back(docRef);
	outDocRef = docRef;
	return true;
}

bool KBSBookScope::ReachChapterDoc(const IDFile& file, UIDRef& ioDocRef)
{
	// See the header: by file first; the docRef held only for a chapter with no file, while it is open.
	UIDRef reopened;
	if (ReopenChapterDoc(file, reopened))
	{
		ioDocRef = reopened;
		return true;
	}
	return !ChapterHasFile(file) && IsDocStillOpen(ioDocRef);
}

bool KBSBookScope::ShowChapterWindow(const UIDRef& docRef)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// Still open? Asked the way IsDocStillOpen asks everything - the pair against the session's
	// list, no dereference - because the callers hand over chapters their run recorded EARLIER in
	// the same tick, and every question below this line reads the document; this is the door that
	// keeps a dangling (IDataBase*, UID) away from all of them (2026-08-09, the pre-submission
	// re-check). False is also the true answer: a chapter nobody has open cannot be given a window.
	//
	// ! A DOOR, NOT A KNOWN CASE. What was MEASURED (2026-08-04): a close this module scheduled did
	//   not run until the run was over (the .idlk files stayed until then). So no path is known that
	//   closes a chapter under a run; the test stays because it costs nothing and the thing it guards
	//   against would be a crash.
	if (!IsDocStillOpen(docRef))
		return false;

	// Does it already have a window - front, or behind another tab? Then leave it alone. Asked
	// through DocHasAnyWindow, which is where this module keeps that question. It has to be the
	// ALL-presentations search: GetFrontmostPresentationForDocument answers nil for a document sitting
	// behind another tab, and acting on that would open a SECOND window on the same document.
	//
	// ***** TRUE, NOT FALSE: the question this answers is "can the user see this chapter?" *****
	// It returned false here until 2026-08-05, which put "it already had a window" and "the window
	// could not be opened" behind one answer - so a caller could not tell the ordinary case from the
	// failure, and the replace's caller simply discarded the result. See the header.
	//
	// ForgetHeldDoc for the same reason the successful path below calls it: a chapter with a window
	// is the user's, and must not be sitting on the list of chapters a run may close. Normally it is
	// not on that list at all, and this does nothing.
	if (DocHasAnyWindow(docRef))
	{
		KBSBookScope::ForgetHeldDoc(docRef);
		return true;
	}

	// Windowless (the search opened it that way): give it a real layout window so the user can
	// see the replacement, undo it by hand, and decide about saving. Nothing is saved here.
	//
	// ***** THE WINDOW IS THE UI HALF'S TO OPEN (2026-10-01, the model/UI split). ***** kOpenLayoutCmdBoss
	// is a boss of the LayoutUI plug-in (LayoutUIID.h), and a model plug-in that instantiates a UI plug-in's
	// boss is one of the two cases the guide names for "crashes or corrupt documents" (vol1-06, "Problems
	// when mixing model and UI"). IKBSUIServices::OpenLayoutWindow runs the command and asks for the
	// window it produced, exactly as this function did; its notes went with it. No UI = no window.
	InterfacePtr<IKBSUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	if (ui == nil || !ui->OpenLayoutWindow(docRef))
		return false;

	// It has a window now, so it is the user's - dropping it from the held list keeps a later
	// ReleaseHeldDocs from closing a window the user is looking at.
	KBSBookScope::ForgetHeldDoc(docRef);
	return true;
}

void KBSBookScope::ForgetHeldDoc(const UIDRef& docRef)
{
	if (docRef == UIDRef::gNull)
		return;

	// Backwards, and every match rather than the first: the same shape CloseDisplayedDocsIfClean
	// uses. Nothing should ever put one document on this list twice, and a function whose whole job
	// is "this is not ours any more" should not be the place that discovers otherwise.
	for (int32 i = static_cast<int32>(gHeldDocs.size()) - 1; i >= 0; --i)
	{
		if (gHeldDocs[i] == docRef)
			gHeldDocs.erase(
				gHeldDocs.begin() + i);
	}
}

void KBSBookScope::CloseDisplayedDocsIfClean(const UIDRef& exceptDoc)
{
	InterfacePtr<IDocumentList> docList(KBSBookScope::QueryOpenDocumentList());
	if (docList == nil)
		return;

	// Collect first, then close (closing mutates the document list).
	K2Vector<UIDRef> toClose;
	const int32 count = docList->GetDocCount();
	for (int32 i = 0; i < count; ++i)
	{
		IDocument* doc = docList->GetNthDoc(i);
		if (doc == nil)
			continue;
		const UIDRef ref = ::GetUIDRef(doc);
		if (ref == exceptDoc)
			continue;	// the document the jump just landed in stays

		// A document with something to save would want saving - leave it to the user. Asked
		// through the same question the held-chapter releases ask (it tested db == nil ||
		// IsModified() by hand until 2026-08-08).
		//
		// ***** THIS LOOP IS THE ONE THE QUESTION ITSELF WAS WRONG FOR. ***** Unlike the two
		// held-chapter releases - whose documents were all opened from a chapter file - this walks
		// EVERY open document, so it meets the one kind that has never been saved at all: the
		// untitled document the user just made. IsModified() reads that as clean and this closed
		// it, without a prompt (measured 2026-08-10). Since that day the shared question is
		// IDocFileHandler::CanSave, "modified OR unsaved" - see HasUnsavedChanges.
		//
		// ***** ASKED THROUGH HasUnsavedWork SINCE 2026-09-25, like the two releases. ***** CanSave
		// calls every conversion of an older InDesign's chapter "unsaved", so a converted chapter a
		// jump had opened in a window was the one window this sweep never closed. HasUnsavedWork asks a
		// conversion what was WRITTEN to it and leaves every other document to CanSave, so the
		// untitled document above is kept exactly as before.
		if (HasUnsavedWork(ref))
			continue;

		// Only documents that HAVE a window go: a windowless held chapter is not this sweep's to close
		// (the releases take it). Runs close chapters as they go, so one is left only when a jump or a
		// replace could not give it a window.
		if (!DocHasAnyWindow(ref))
			continue;	// windowless - keep it held

		toClose.push_back(ref);
	}

	// The same preserve-then-clear pair the held-chapter releases use (the long note is in
	// ReleaseHeldDoc): Close reports nothing back, and this sweep runs in the MIDDLE of a jump -
	// an error left standing would fail the jump's next step, while clearing the caller's own
	// would be deciding for the jump that its error state did not matter.
	GlobalErrorStatePreserver closeErrorState;

	for (int32 i = 0; i < static_cast<int32>(toClose.size()); ++i)
	{
		// ***** IT COMES OFF THE HELD LIST BECAUSE IT HAS A WINDOW, not because it is about to be
		// closed. ***** Everything in toClose passed DocHasAnyWindow above, and a window makes a
		// chapter the user's whoever raised it - the same verdict ReleaseHeldDocs reaches, where a
		// windowed chapter is DROPPED from the list rather than put back on it. So the claim goes
		// whether or not the close below goes through.
		//
		// Which is why this is NOT the "erased before a refused close" fault the held-chapter
		// releases had to correct on 2026-08-08. There, a chapter whose close was refused fell off
		// the one list that could ever hand it back and sat windowless with its .indd locked; here
		// a refusal leaves a document the user can see and close themselves. (KESCL's sweep takes the
		// OPPOSITE order - handler and CanClose first, KESCLBookScope.cpp:272-278 - because it can take
		// windowless documents as well; this one cannot.)
		KBSBookScope::ForgetHeldDoc(toClose[i]);

		InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(toClose[i]));
		if (docFileHandler == nil)
			continue;
		if (docFileHandler->CanClose(toClose[i]))
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// a standing error would fail this close
			docFileHandler->Close(toClose[i], kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kSchedule);
		}
	}
}

KBSBookScope::TargetBook KBSBookScope::GetTargetBook()
{
	// ***** THE BOOK THE RUN WOULD RESOLVE, asked through the very function the run resolves it
	// with. ***** ListBookChapters calls ResolveTargetBook too, so the menu's grey state and the
	// run itself cannot come to different answers - which is the whole reason this exists. Nothing
	// here opens, lists or holds anything: it reads a palette's file field and asks IBookManager.
	// (KBSBookScope::HasActiveBook stood beside this until 2026-08-10: every door wants the book a run
	// would TARGET, which is not "is a book active".)
	IBook* const book = ResolveTargetBook();		// non-owning - no release
	if (book == nil)
		return kNoTargetBook;

	// ...and whether it has a chapter (2026-10-03, B11-2 - see the header): COUNTED, the list
	// ListBookChapters walks, without building a single entry. A book whose content manager will not
	// come is empty to the run as well - ListBookChapters returns false for it - so it is answered the same.
	InterfacePtr<IBookContentMgr> contentMgr(book, UseDefaultIID());
	return (contentMgr != nil && contentMgr->GetContentCount() > 0) ? kTargetBookReady : kTargetBookEmpty;
}

bool KBSBookScope::HasScopeTarget()
{
	// The same two questions the search asks when it resolves its scope (KBSSearchEngine.cpp, SearchBook),
	// asked here so the menu can go grey BEFORE a run that would only report that there was nothing to
	// run on - an empty book included (B11-2).
	if (IsBookScopeOn())
		return GetTargetBook() == kTargetBookReady;

	// The active document, not "is any document open": a book search opens its chapters WINDOWLESS and
	// this is the document-scope branch, where the active document is exactly what gets searched.
	// A chapter one of our own runs is holding open therefore does not count as a target, which is
	// what we want.
	return ActiveDocument() != nil;
}

IDocument* KBSBookScope::ActiveDocument()
{
	ISession* const session = GetExecutionContextSession();
	IActiveContext* const context = (session != nil) ? session->GetActiveContext() : nil;
	return (context != nil) ? context->GetContextDocument() : nil;
}

KBSBookScope::ChapterDoc KBSBookScope::DocAsChapter(IDocument* doc)
{
	ChapterDoc one;
	if (doc == nil)
		return one;
	one.docRef = ::GetUIDRef(doc);
	doc->GetName(one.shortName);
	one.shortName.SetTranslatable(kFalse);
	return one;
}

void KBSBookScope::AppendChapterNote(PMString& outSummary, const char* what,
	const std::vector<PMString>& names, const char* tail)
{
	if (names.empty())
		return;

	outSummary.Append("  ");
	outSummary.AppendNumber(static_cast<int32>(names.size()));
	outSummary.Append(" chapter(s) ");
	outSummary.Append(what);
	outSummary.Append(" (");
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			outSummary.Append(", ");
		if (i >= 3)								// a status line stays short, even at three lines
		{
			outSummary.Append("...");
			break;
		}
		// RAW, with its ampersands as the user typed them. What this builds is a STATUS LINE. Until
		// 2026-09-29 the one place that draws one doubled the ampersands of the WHOLE line on its way to
		// a stock StaticText, so doubling the name here as well ran that twice: "A&B.indd" went to
		// "A&&&&B.indd" and drew as "A&&B.indd" (found 2026-08-03 in the defect audit). The message
		// area is drawn by hand now and takes '&' as it is (KBSStatusTextView.cpp) - raw is still
		// right. Anything that starts drawing this string in a STOCK widget has to do its own doubling,
		// exactly as the tree's rows do (SetColumnText).
		PMString name(names[i]);
		name.SetTranslatable(kFalse);
		outSummary.Append(name);
	}
	outSummary.Append(")");
	outSummary.Append(tail);
}

void KBSBookScope::AppendUnopenableNote(PMString& outSummary,
	const std::vector<KBSBookScope::SkippedChapter>& skipped)
{
	// Each chapter as "name: what the book says about it" - or its name alone when the book said
	// nothing, rather than a name with a dangling colon.
	std::vector<PMString> entries;
	entries.reserve(skipped.size());
	for (size_t i = 0; i < skipped.size(); ++i)
	{
		PMString entry(skipped[i].name);
		if (!skipped[i].reason.IsEmpty())
		{
			entry.Append(": ");
			entry.Append(skipped[i].reason);
		}
		entries.push_back(entry);
	}
	AppendChapterNote(outSummary, "could not be opened", entries, ".");
}

void KBSBookScope::AppendUnclosedNote(PMString& outSummary, const std::vector<PMString>& names)
{
	// The other end of the run from AppendUnopenableNote.
	AppendChapterNote(outSummary, "left open with no window", names, ".");
}

bool KBSBookScope::GetSearchedBookPath(PMString& outPath)
{
	outPath = gSearchedBookPath;
	return !gSearchedBookPath.IsEmpty();
}

// (GetPanelBookFile and IsBookPanel - the walk of InDesign's book panels - stood here until 2026-10-01.
//  The Book panel is user interface: they are the UI half's now, KBSBookPanelLookup, and this module asks
//  for the front tab's book through IKBSUIServices::GetPanelBookFile in ResolveTargetBook.)

bool KBSBookScope::IsBookStillOpen(const PMString& bookPath)
{
	// The lookup by file, the IsOpen() test and the reasons for both live in FindOpenBookByPath, which
	// MakeBookActive needs as well - it wants the book itself, not just whether there is one.
	return FindOpenBookByPath(bookPath) != nil;
}

bool KBSBookScope::MakeBookActive(const PMString& bookPath)
{
	IBook* book = FindOpenBookByPath(bookPath);		// non-owning pointer - no release
	if (book == nil)
		return false;

	// TWO separate states, and they really are separate: selecting a tab in the book panel does
	// NOT change IBookManager's current active book, and setting the active book does not move the
	// tab (measured 2026-07-27; docs/ai-notes/book-panel-active-tab.md). The user asked for both,
	// so both are set here.

	// 1. The active book - what every book API answers about. Set directly, the way Adobe's own
	//    AcquireCurrentBook does (source/open/includes/layout/AcquireCurrentBook.h:72,87), which is
	//    the only worked example in the SDK. kSetCurrentActiveBookCmdBoss exists but has no call
	//    site anywhere in the SDK, and this is session UI state rather than document data.
	//    A nil manager is answered with false rather than carried past, because the header states
	//    outright that a true return means the active book WAS set - and this used to walk on to the
	//    tab and return true having set nothing. It cannot happen: FindOpenBookByPath above took the
	//    same interface off the same session to find this book at all, so reaching here with a book
	//    in hand means the manager came. Made to match the promise anyway - a door that cannot open
	//    is the cheapest kind to shut (block 11 defect re-check, 2026-08-11).
	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil)
		return false;
	bookMgr->SetCurrentActiveBook(book);

	// 2. The tab the user can SEE is the UI half's (KBSBookPanelLookup::BringBookTabForward, 2026-10-01 -
	//    the Book panel is user interface, which a model plug-in may not reach). The caller asks it next.
	return true;
}

bool KBSBookScope::ListBookChapters(std::vector<ChapterDoc>& outDocs, PMString& outBookName)
{
	outDocs.clear();
	outBookName.Clear();

	// Which book to search: the one the BOOK PANEL is showing, not the "active" one.
	//
	// Selecting a book's tab switches the panel but does NOT make that book active - only touching
	// a chapter inside it does (measured 2026-07-27). So a user who picks a tab and runs a search
	// gets whatever book was active before, silently. For a search that is confusing; for Change
	// Checked it would rewrite the wrong book, which is unacceptable. Adobe splits the two ideas in
	// IBookUIUtils itself (GetBookFileFromBookPanel vs "the active book"), so the panel's own book
	// is the right thing to ask for. It falls back to the active book when the panel cannot be
	// reached, which keeps the old behaviour rather than failing outright.
	//
	// Both steps - and the closing-book door on each of them - live in ResolveTargetBook, which the
	// menu's grey state asks as well (GetTargetBook). Non-owning pointer, whichever step answered:
	// nothing here is released.
	IBook* book = ResolveTargetBook();
	if (book == nil)
		return false;

	outBookName = book->GetBookTitleName();
	outBookName.SetTranslatable(kFalse);

	IDataBase* bookDB = ::GetDataBase(book);
	if (bookDB == nil)
		return false;

	// Record this run's book. What the last run left held has already been handed back: the caller
	// calls ReleaseSearchedBook at its commit point, just before this (the header's contract). (A
	// "different book? hand the held chapters back" test stood here until 2026-09-28; with the path
	// always cleared by then it was always true, and the list it emptied was always empty.)
	SDKFileHelper bookFileHelper(book->GetBookFileSpec());
	gSearchedBookPath = bookFileHelper.GetPath();

	InterfacePtr<IBookContentMgr> contentMgr(book, UseDefaultIID());
	if (contentMgr == nil)
	{
		gSearchedBookPath.Clear();		// see the note on the empty-list exit below
		return false;
	}

	const int32 contentCount = contentMgr->GetContentCount();
	for (int32 i = 0; i < contentCount; ++i)
	{
		const UID contentUID = contentMgr->GetNthContent(i);
		if (contentUID == kInvalidUID)
			continue;

		InterfacePtr<IBookContent> content(bookDB, contentUID, UseDefaultIID());
		if (content == nil)
			continue;

		ChapterDoc chapter;
		// The chapter's entry in the book. OpenChapterDoc needs it to ask the book API about this
		// chapter by the purpose-built question rather than by file name.
		chapter.contentUID = contentUID;

		// The chapter's .indd. It is what OpenChapterDoc opens by, and what navigation uses to
		// reopen the chapter after it has been closed.
		//
		// ***** THE ANSWER IS KEPT, NOT DISCARDED. ***** GetIDFile "returns kTrue if a file can be
		// obtained for the book content, kFalse otherwise" (IBookContent.h:121-125), so a chapter
		// whose entry names no file is a state the book API allows. This line dropped that answer
		// until 2026-08-11, while two places in this module declared the case impossible - and one
		// of them named THIS call as its authority. What the loss costs is at ChapterHasFile: an
		// entry with no file reads exactly like a DOCUMENT-scope row, which is the one thing that
		// may fall back on a docRef the search left behind.
		chapter.hasFile = (content->GetIDFile(chapter.file) != kFalse);

		// The chapter's file name for the read-out, built HERE rather than at open time: a chapter
		// that cannot be opened still has to be named in the report. Via the UTF-16 buffer
		// (AppendW), so a Japanese chapter name survives - the PMString(char*) conversions do not.
		chapter.shortName.SetTranslatable(kFalse);
		{
			WideString shortName = content->GetShortName();
			const UTF16TextChar* buf = shortName.GrabUTF16Buffer(nil);
			if (buf != nil)
				chapter.shortName.AppendW(buf);
		}

		// docRef is left null: nothing is opened here. The run opens each chapter when its turn
		// comes (OpenChapterDoc) and hands it back as soon as it has walked it (ReleaseHeldDoc).
		outDocs.push_back(chapter);
	}

	// ***** NO CHAPTERS = NO SEARCHED BOOK. *****
	// gSearchedBookPath has to be written before the loop - the entries are read out of the book it
	// names - but it means "the panel is showing THIS book's results", and a caller that gets false
	// here puts nothing on the panel at all (every run answers "That book has no chapters."
	// and returns). Leaving the path standing left that statement true about a book with no results
	// behind it, which is the one thing the two readers of this value - KBSBookWatch and
	// KBSJump's ShowBook - are not allowed to be told.
	//
	// Cleared rather than restored to what it was: the callers all clear it through
	// ReleaseSearchedBook immediately before calling this, so there is no earlier value to go back
	// to, and "no results, no book" is the honest state either way.
	//
	// ***** A SAFETY NET SINCE 2026-10-03 (B11-2). ***** The callers ask GetTargetBook at their front
	// door, before their commit point, and an empty book is refused THERE with the previous results
	// still standing. What can still reach this is a book that counted chapters none of which could be
	// read (an entry that is not there, or not a chapter) - and that run's results are already gone.
	if (outDocs.empty())
	{
		gSearchedBookPath.Clear();
		return false;
	}
	return true;
}

bool KBSBookScope::OpenChapterDoc(ChapterDoc& ioChapter, std::vector<SkippedChapter>* outSkipped)
{
	ioChapter.docRef = UIDRef::gNull;

	// The book this chapter belongs to, found again by path rather than carried along in a static:
	// a book pointer parked between calls goes stale the moment the user closes the book, and a run
	// pumps events through its progress bar. Looking it up costs one IBookManager::FindOpenBookByName.
	//
	// A nil answer here is not fatal - it only costs the two things that need the book itself (the
	// already-open lookup by content UID, and the reason text for a chapter that will not open).
	// The open below goes by FILE and works either way.
	IBook* book = FindOpenBookByPath(gSearchedBookPath);
	IDataBase* bookDB = (book != nil) ? ::GetDataBase(book) : nil;
	bool16 isMissingPlugins = kFalse;

	// Is it already open? Ask the book API first, by CONTENT UID - the purpose-built question
	// (IBookUtils.h:119-127), and the one that can answer "it is open but its plug-ins are
	// missing". bShowAlert = kFalse keeps it silent, which is the whole reason this plug-in
	// stopped using OpenOneDocument.
	//
	// The returned IDocument* is treated as NON-OWNING and is never released: nothing in the
	// IBookUtils / IBookManager family that hands out interface pointers is a Query (the SDK's
	// marker for "you own this"), and Adobe's own AcquireCurrentBook.h holds
	// IBookManager::GetCurrentActiveBook() the same way without ever releasing it.
	//
	// If this answers nil for a document that IS open, nothing breaks: ReopenChapterDoc below asks
	// the same question a second way - by walking the open-document list and comparing FILES - and
	// rebinds to the user's copy. So the fallback covers the case where this API does not behave as
	// read. (It used to name IsSourceDocumentAlreadyOpen here; that API was taken out on 2026-08-04,
	// see the note in ReopenChapterDoc.)
	if (bookDB != nil && ioChapter.contentUID != kInvalidUID)
	{
		IDFile openSysFile;
		IDocument* alreadyOpenDoc = Utils<IBookUtils>()->FindDocFromContentUID(
			bookDB, ioChapter.contentUID, openSysFile, isMissingPlugins, kFalse /*bShowAlert*/);

		// ***** THE ANSWER IS CHECKED AGAINST THIS CHAPTER'S FILE, NOT TAKEN ON TRUST. *****
		//
		// FindDocFromContentUID has no caller anywhere in this SDK - not in a sample, not in
		// source/open - so there is nothing to check its behaviour against, which is the exact
		// condition that produced the worst fault this plug-in has had. IsSourceDocumentAlreadyOpen
		// was trusted the same way and handed back a DIFFERENT chapter's document; the walk then ran
		// over the wrong document and every row of that chapter came back 'missing', silently,
		// because the call had reported success (measured 2026-08-04, 4 book replaces in 10).
		//
		// That fix reached ReopenChapterDoc and stopped there. This is the same question asked one
		// step earlier, so it gets the same answer: a document is this chapter's document when it
		// LIVES IN THIS CHAPTER'S FILE. Checking costs one file compare (FileUtils::IsEqual, since
		// 2026-09-27 - a string compare until then), and the material is already here - this very call
		// hands the file back in openSysFile.
		//
		// Asked through KBSDocumentLivesInFile, which is what ReopenChapterDoc asks, so the two
		// cannot come to differ.
		//
		// ***** AN ENTRY THAT NAMES NO FILE CANNOT BE CHECKED, SO IT IS NOT ACCEPTED. ***** It was
		// waved through until 2026-08-11 - "every BOOK chapter names a file, so that case does not
		// arise here" - which made this door the one place where an unverified document could still
		// be taken for a chapter's own. The claim rested on IBookContent::GetIDFile always
		// answering kTrue, and the header does not say that: it "returns kTrue if a file can be
		// obtained ... kFalse otherwise" (IBookContent.h:121-125).
		//
		// MEASURED 2026-08-11 (work/kbs-selftest/run-getidfile-probe.ps1): a chapter whose .indd
		// has been DELETED behind the book's back still answers kTrue with its path intact - the
		// entry keeps the link, and the book reports it as MISSING_DOCUMENT - so this branch does
		// not change what happens to any book we can build today. It is closed because the answer
		// is the book's to give, not ours to assume, and because falling through costs nothing: an
		// entry with no file cannot be opened either, so the chapter is reported as unopenable,
		// which is what a chapter nobody can resolve should look like.
		if (alreadyOpenDoc != nil && KBSDocumentLivesInFile(alreadyOpenDoc, ioChapter.file))
		{
			// The user's (or an earlier run's) own copy. NOT held: closing a document somebody
			// else opened would surprise them - the same rule ReopenChapterDoc follows, and the
			// reason ReleaseHeldDoc can be called on every chapter without checking who opened it.
			ioChapter.docRef = ::GetUIDRef(alreadyOpenDoc);
			return true;
		}
		// Answered with something that is NOT this chapter: fall through to the open below, which
		// resolves by file and cannot be confused. Nothing is reported here - being handed the wrong
		// document is this function's problem to absorb, not a chapter the book could not open.
	}

	// Open it without a layout window AND with the UI suppressed.
	//
	// Why not IBookUtils::OpenOneDocument, which this used to call: that API takes no
	// UI-suppression flag (IBookUtils.h:341). A chapter that raises ANY alert while opening -
	// a missing font, a missing link, a document last saved by another version - therefore put
	// an alert on screen, failed to open, and was skipped without a word. The panel then showed
	// the book minus that chapter, which is indistinguishable from a chapter that simply held no
	// matches. ReopenChapterDoc is the windowless + kSuppressUI open the jump path already used,
	// so both paths open a chapter the same way.
	//
	// What is given up: OpenOneDocument also watched the open-database ceiling and would close an
	// already-opened chapter to stay under it. That is a poor fit here - it can invalidate a docRef
	// somebody is still holding - and it matters far less now that a run holds one chapter at a time.
	UIDRef docRef;
	if (ReopenChapterDoc(ioChapter.file, docRef) && docRef != UIDRef::gNull)
	{
		ioChapter.docRef = docRef;
		return true;
	}

	// Report it instead of dropping it. A chapter that is simply absent from the list reads
	// exactly like a chapter with no matches, and the book API knows the real reason:
	// GetBookContentStatus answers missing / out of date / in use / open (IBookUtils.h:113-117).
	if (outSkipped != nil)
	{
		SkippedChapter skipped;
		skipped.name = ioChapter.shortName;
		skipped.name.SetTranslatable(kFalse);

		if (bookDB != nil && ioChapter.contentUID != kInvalidUID)
		{
			InterfacePtr<IBookContent> content(bookDB, ioChapter.contentUID, UseDefaultIID());
			if (content != nil)
			{
				skipped.reason = PMString(BookContentStatusText(
					Utils<IBookUtils>()->GetBookContentStatus(content)));
				skipped.reason.SetTranslatable(kFalse);
			}
		}

		// The status can be kDocNormal for a chapter that still would not open (a lock file,
		// a permissions problem). Missing plug-in data is the other thing the lookup above
		// can tell us, and it is worth more than "normal".
		if (skipped.reason.IsEmpty() && isMissingPlugins)
		{
			skipped.reason = PMString("missing plug-in data");
			skipped.reason.SetTranslatable(kFalse);
		}

		outSkipped->push_back(skipped);
	}
	return false;
}

// End, KBSBookScope.cpp.
