//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Book-wide search scope implementation. See KBSBookScope.h for the overall contract.
//  Ported from KESCLBookScope (KESCL left untouched); the toggle and the jump-time
//  reopen/early-close machinery are omitted in this Step-1 subset.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager - the panel walk starts here
#include "IBook.h"
#include "IBookContent.h"
#include "IBookContentMgr.h"
#include "IBookManager.h"
#include "IBookUIUtils.h"		// GetBookFileFromBookPanel (panel vs active book)
#include "IBookUtils.h"			// FindDocFromContentUID, GetBookContentStatus
#include "IControlView.h"		// a panel IS a control view - what GetNthPanelInfo's UID resolves to
#include "IDataBase.h"			// IsModified - HasUnsavedChanges asks it before every close here
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open by file (windowless reopen)
#include "IDocumentList.h"
#include "IDocumentUIUtils.h"	// FindPresentationForDocument (has-a-window test)
#include "IDocumentPresentation.h"	// the predicate typedef / presentation handle
#include "IDocumentUtils.h"
#include "ILayoutUIUtils.h"		// GetFrontDocument - the document-scope half of HasScopeTarget
#include "IOpenFileCmdData.h"	// kOpenDefault / kUseLockFile
#include "ICommand.h"			// SetItemList - kOpenLayoutCmdBoss takes the document as its item
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear?
// (IMenuUtils.h was here for InsertAmpersandForDisplay until 2026-08-03. The status line doubles
// its own ampersands for the whole message, so doubling a chapter name here as well ran it twice -
// see AppendUnopenableNote.)
#include "IPanelMgr.h"			// GetPanelCount / GetNthPanelInfo - one book panel per open book
#include "ISession.h"
#include "IWindow.h"			// the window kOpenLayoutCmdBoss is supposed to have produced

// General includes:
#include "ErrorUtils.h"			// PMSetGlobalErrorCode - a failed Open must not poison later commands
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "CmdUtils.h"
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss - give a windowless chapter a real window
#include "PaletteRefUtils.h"	// IsPaletteVisible - the front tab is decided on the container
#include "K2Vector.h"			// the held-chapter list (it came in through IBookUtils.h until 2026-08-08)
#include "SDKFileHelper.h"
#include "UIDList.h"
#include "Utils.h"
#include "PMString.h"
#include "WideString.h"

// Project includes:
#include "KBSBookScope.h"

namespace
{
	// The chapters WE opened (only the originally-closed ones), held so that ReleaseHeldDoc can hand
	// each one back once its turn is over. A chapter the user already had open never goes on here.
	//
	// ***** A PLAIN VECTOR, because nothing is handed to the book API any more. ***** This used to be
	// IBookUtils' own OriginallyCloseDocInfo - the container OpenOneDocument fills in and
	// CloseDocumentsInBook drains - and this plug-in stopped calling BOTH of those in 2026-07 (no UI
	// suppression on that open, no UI flag or command mode on that close; the reasons are at
	// OpenChapterDoc and ReleaseHeldDocs). Keeping their container afterwards only suggested a
	// partnership that is not there: the note here still read "OpenOneDocument records exactly those
	// in here" and "uses this record to stay under the open-database cap", in the present tense,
	// about calls that had been gone for six weeks (found in the block 11 API audit, 2026-08-08).
	K2Vector<UIDRef> gHeldDocs;

	// Which book the RESULTS on the panel came from (its full file path). NOT "which book we hold
	// chapters for": since 2026-08-02 every run closes each chapter as soon as it is done with it,
	// so the held list is empty most of the time while a full result set is still on screen.
	//
	// Two things read this and both are about the RESULTS, not about open documents:
	//   * KBSBookWatch - "the book these results name has been closed, retire them"
	//   * KBSJump::ShowBook - "the book row was clicked, bring that book forward"
	// Let go through ReleaseSearchedBook, which every KBSResultModel::Clear() is paired with.
	PMString gSearchedBookPath;

	// The search-scope toggle. Session state only (every launch starts OFF), like KESCL's
	// gBookSearchOn: OFF searches the front document, ON the whole active book.
	bool gBookScopeOn = false;

	// kBookPanelBoss lives in BOOK PANEL.APLN and is declared in no public header, so the number
	// has to be spelled out. Taken from a live object-model dump and cross-checked against the
	// running panel list, where every open book's panel came back as kBookPanelBoss (measured
	// 2026-07-28, docs/ai-notes/book-panel-active-tab.md). Compared as a raw number because there
	// is no constant to compare against. A future build could renumber it - that is why the name
	// check below exists, and why a total miss just falls back to the active book.
	const uint32 kBookPanelBossRawClassID = 0x10101;

	/** Does this panel name belong to a book that is open right now?
	    Backstop for the hard-coded ClassID above. A book panel is titled with the book's title
	    name, and when two open books share a name InDesign appends " 2" to the later one - so a
	    leading match counts too. Cheap enough: there are rarely more than a handful of books. */
	bool PanelNameMatchesOpenBook(const PMString& panelName)
	{
		if (panelName.IsEmpty())
			return false;

		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		if (bookMgr == nil)
			return false;

		const int32 bookCount = bookMgr->GetBookCount();
		for (int32 i = 0; i < bookCount; ++i)
		{
			IBook* book = bookMgr->GetNthBook(i);	// non-owning pointer - no release
			if (book == nil)
				continue;

			const PMString bookTitle = book->GetBookTitleName();
			if (bookTitle.IsEmpty())
				continue;
			if (panelName.Compare(kFalse, bookTitle) == 0)
				return true;
			if (panelName.IndexOfString(bookTitle) == 0)		// "Book 1" -> tab "Book 1 2"
				return true;
		}
		return false;
	}

	/** Is this registered panel one of InDesign's book panels?

	    ***** ONE PLACE, because the hard-coded ClassID above can go stale. ***** Two walks below need
	    this answer and each spelled the test out for itself, so a renumbered kBookPanelBoss would
	    have had to be found twice (block 11 API audit, 2026-08-08). */
	bool IsBookPanelView(IControlView* panelView, const PMString& panelName)
	{
		if (panelView == nil)
			return false;
		return (::GetClass(panelView).Get() == kBookPanelBossRawClassID)
			   || PanelNameMatchesOpenBook(panelName);
	}

	/** The book file THIS panel is showing; false when the panel could not be resolved.

	    Handing the panel itself in is the whole point: with a real widget IBookUIUtils resolves that
	    panel's book, where a nil widget falls through to QueryActiveBookPanel - the active book, which
	    is precisely the value both callers exist to avoid.

	    An empty result is refused here rather than passed on, because further up it would read as
	    "no book at all" instead of "this panel could not be asked". */
	bool GetBookFileFromPanelView(IControlView* panelView, IDFile& outFile)
	{
		IDFile panelBookFile;
		Utils<IBookUIUtils>()->GetBookFileFromBookPanel(panelBookFile, panelView);

		SDKFileHelper panelFileHelper(panelBookFile);
		if (panelFileHelper.GetPath().empty())
			return false;

		outFile = panelBookFile;
		return true;
	}

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

	    A document with no database reads as "nothing to lose": there is no modification flag to
	    consult, and refusing to close on that basis would strand the chapter for the session. */
	bool HasUnsavedChanges(const UIDRef& docRef)
	{
		IDataBase* db = docRef.GetDataBase();
		return (db != nil) && (db->IsModified() != kFalse);
	}

	/** Accepts every presentation.

	    ***** A LOCAL PREDICATE IS WHAT ADOBE ASKS FOR HERE. ***** The stock one exists and is named
	    FindPresCriteria::accept_all (DocumentPresFindCriteria.h:82), but that file's own preamble
	    (:40-46) says its implementations "are found in the WidgetBin shared library, so you cannot
	    use them from a model only plugin. Should the need arise you can create local
	    implementations" - and prints a two-line example of exactly this shape. So this is the
	    documented route, not a stand-in for one. (Until 2026-08-08 the note here said we kept our own
	    because we did not know where the stock objects live, which was no longer true and read like
	    an avoidable dependency.) KESCL carries the same predicate for the same reason. */
	bool KBSAcceptAnyPresentation(IDocumentPresentation* /*p*/)
	{
		return true;
	}

	/** Does this document have a WINDOW anywhere - front, or behind another tab? The
	    all-presentations search, because GetFrontmostPresentationForDocument answers nil for a
	    document sitting behind another tab (ShowChapterWindow has always asked it this way).

	    Asked by the held-chapter releases since 2026-08-05: a window makes a chapter the USER'S,
	    whoever raised the window. ShowChapterWindow and the jump take a chapter off the held list
	    when they raise one themselves, but a window can be raised behind this module's back - the
	    book panel lists every chapter, and double-clicking one there windows the very document being
	    held. A release that closed it then would take a window the user is looking at; and once they
	    had saved their work, not even the unsaved-work door would stand in the way. */
	bool DocHasAnyWindow(const UIDRef& docRef)
	{
		IDataBase* db = docRef.GetDataBase();
		if (db == nil)
			return false;
		FindPresentation_PreferCriteria noPreference;
		return Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, KBSAcceptAnyPresentation, noPreference) != nil;
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
}

bool KBSBookScope::IsBookScopeOn()
{
	return gBookScopeOn;
}

void KBSBookScope::SetBookScopeOn(bool on)
{
	// Just the flag. Nothing is closed and nothing is cleared here: KESCL's first shape closed the
	// held chapters right inside the toggle and crashed (see KESCLBookScope::SetBookSearchOn). The
	// held chapters are released by the next book search or at shutdown, and a jump into a chapter
	// the user closed meanwhile goes through ReopenChapterDoc anyway.
	gBookScopeOn = on;
}

bool KBSBookScope::IsDocStillOpen(const UIDRef& docRef)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	InterfacePtr<IDocumentList> docList(GetExecutionContextSession()->QueryDocumentList());
	if (docList == nil)
		return false;

	// The session's own lookup by database (IDocumentList.h:71-76), which is how the rest of this
	// plug-in asks this (KBSDrawEventHandler.cpp) and how KESCM asks it everywhere. It replaced a
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

void KBSBookScope::ReleaseHeldDocs()
{
	// NOTE: the searched-book path is NOT cleared here. Closing the chapters says nothing about
	// which book the panel is showing - and since every run closes its chapters as it goes, doing
	// so would blank the path while a full result set is still up (which broke both readers named
	// on gSearchedBookPath).
	if (gHeldDocs.size() == 0)
		return;

	// Take the list first, so a re-entrant call finds it already empty instead of scheduling
	// the closes twice.
	K2Vector<UIDRef> held;
	held = gHeldDocs;
	gHeldDocs.clear();

	// Close each chapter through the stock document close. kSchedule defers each close until the
	// current notification / idle tick has unwound; kSuppressUI + the search-time dirty guard =
	// no save prompt. Skip chapters the user closed already (a dead UIDRef must not reach the
	// close machinery).
	//
	// WHY NOT the book API's own IBookUtils::CloseDocumentsInBook(OriginallyCloseDocInfo&), which
	// is the documented partner of the OpenOneDocument this list came from: it takes no UI flag and
	// no command mode, so it closes IMMEDIATELY and with whatever UI it likes. KESCL called it here
	// and CRASHED (2026-07-17) - the toggle it ran from was a widget notification, and closing a
	// document in the middle of one is the wrong context. That helper is written for InDesign's own
	// TOC / index commands, which run from a command, not from a notification. Do not "improve"
	// this back to it. (docs/ai-notes/book-api.md)
	for (int32 i = 0; i < static_cast<int32>(held.size()); ++i)
	{
		if (!IsDocStillOpen(held[i]))
			continue;

		// ***** A window makes it the user's, whoever raised it. ***** Not closed, and DROPPED
		// rather than put back on the list (the list was taken and cleared above, so skipping is
		// dropping): a windowed chapter is not something a later sweep may close either, and the
		// user can see it and close it themselves. See DocHasAnyWindow for how a held chapter
		// comes to have a window at all.
		if (DocHasAnyWindow(held[i]))
			continue;

		// ***** Unsaved work in it? Then it is not ours to close. ***** Put it BACK on the held
		// list: it is still a chapter this plug-in opened, so once it has been saved a later call
		// hands it back like any other. Leaving it off the list instead would mean nothing ever
		// closes it again. See HasUnsavedChanges for what closing it would cost.
		if (HasUnsavedChanges(held[i]))
		{
			gHeldDocs.push_back(held[i]);
			continue;
		}

		// ***** A close that cannot go through leaves the chapter HELD, like unsaved work above. *****
		// The list was taken and cleared at the top, so a plain `continue` here is a silent drop:
		// until 2026-08-08 these two answers stranded the chapter - windowless, its .indd locked,
		// and off the one list that could ever hand it back. Put it back instead, so a later call
		// gets another try (the same treatment the unsaved door above gives its chapter).
		InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(held[i]));
		if (docFileHandler == nil || !docFileHandler->CanClose(held[i]))
		{
			gHeldDocs.push_back(held[i]);
			continue;
		}
		docFileHandler->Close(held[i], kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kSchedule);

		// Close returns nothing (IDocFileHandler.h:101), so a failure inside it can only speak
		// through the global error state - and an error state left standing fails every command
		// after it (the rule every Open in this file already follows, and Close did not until
		// 2026-08-09). Cleared per document, so one refusal cannot shadow the next.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
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

	// ***** IS IT STILL OPEN? ASKED FIRST, AND THE ORDER IS THE WHOLE POINT. *****
	// Everything below this line reads the document, and the first of them - HasUnsavedChanges -
	// DEREFERENCES the database the UIDRef carries (db->IsModified()). A UIDRef is only
	// (IDataBase*, UID), so for a chapter that has been closed since it was held that pointer is
	// dangling, and asking it anything at all is undefined behaviour. IsDocStillOpen dereferences
	// nothing: it compares the pair against the session's open-document list.
	//
	// ReleaseHeldDocs has always asked in this order (see the loop there). This one asked in the
	// opposite one until 2026-08-05 - the same two questions, in one module, answered two different
	// ways, which is the shape this plug-in keeps being bitten by. The callers all happen to hand
	// over a chapter they have just opened, so nothing reached it; the header, however, states
	// outright that a chapter which "is no longer open" may be passed in, and that promise has to
	// hold.
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
	// known to mean ONE thing: a replace landed in it and its window would not open. "The user
	// typing in a chapter a jump opened for them", named here until then, cannot get this far: the
	// window test just above drops it, and a jump takes its chapter off the held list anyway
	// (ForgetHeldDoc). The whole of it is in ReleaseHeldDocs' header. See HasUnsavedChanges.
	if (HasUnsavedChanges(docRef))
		return false;

	// The same close ReleaseHeldDocs uses, one document at a time: kSchedule defers it until the
	// current notification / idle tick has unwound, and kSuppressUI plus the run's dirty guard
	// (IDataBase::SaveRestoreModifiedState, which wraps every walk) means no save prompt.
	//
	// Do NOT "improve" this to IBookUtils::CloseDocumentsInBook: it takes no UI flag and no command
	// mode, closes immediately, and crashed KESCL in 2026-07-17 when called from a notification.
	// See the longer note on ReleaseHeldDocs.
	//
	// ***** A REFUSED CLOSE STAYS HELD, exactly as unsaved work does. ***** These two exits sat
	// BELOW the erase until 2026-08-08, so a chapter whose close was refused fell off the held list
	// in the same breath as it failed - windowless, its .indd locked, and no later ReleaseHeldDocs
	// able to find it again. The unsaved door above has always kept its chapter listed so a later
	// call can hand it back; a refusal is the same situation with a different cause, so it keeps
	// the claim the same way.
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
	docFileHandler->Close(docRef, kSuppressUI, kFalse /*allowCancel*/,
		closeNow ? IDocFileHandler::kProcess : IDocFileHandler::kSchedule);
	// Same as ReleaseHeldDocs' loop: Close reports nothing back, so whatever it raised is cleared
	// before the caller's next command walks into it - this one matters most, because a run calls
	// this with kProcess BETWEEN chapters, and an error left here would fail the next chapter's
	// whole walk (2026-08-09).
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
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

// (KBSAcceptAnyPresentation lived here until 2026-08-05; it moved up into the anonymous namespace
// when DocHasAnyWindow - which the held-chapter releases ask - joined it there.)

// Does this open document live in that file? Asked through IDataBase::GetSysFile - "the file
// associated with the database" (IDataBase.h:270-274), which is how the SDK's own samples read a
// document's file (persistentlistui/PstLstUITVHierarchyAdapter.cpp:97).
//
// A document that has never been saved has no file and can never be the chapter being looked for.
static bool KBSDocumentLivesInFile(IDocument* doc, const PMString& wantedPath)
{
	if (doc == nil)
		return false;
	IDataBase* db = ::GetDataBase(doc);
	if (db == nil)
		return false;
	const IDFile* docFile = db->GetSysFile();
	if (docFile == nil)
		return false;
	SDKFileHelper helper(*docFile);
	return helper.GetPath() == wantedPath;
}

bool KBSBookScope::ChapterHasFile(const IDFile& file)
{
	SDKFileHelper fileHelper(file);
	return !fileHelper.GetPath().empty();
}

bool KBSBookScope::ReopenChapterDoc(const IDFile& file, UIDRef& outDocRef)
{
	outDocRef = UIDRef::gNull;

	// Through ChapterHasFile rather than testing the path here, so "does this entry name a file"
	// is asked in ONE place: callers have to ask it too, to tell this failure from the one below
	// (see that function's header), and two spellings of one question is how they come to disagree.
	if (!ChapterHasFile(file))
		return false;	// a front-document entry carries no file - nothing to reopen

	SDKFileHelper fileHelper(file);
	const PMString wantedPath = fileHelper.GetPath();

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
	// So the check stays whatever the lookup is: it costs one string compare, and it is the only
	// thing standing between a wrong answer and a replace in the wrong document.
	{
		InterfacePtr<IDocumentList> docList(GetExecutionContextSession()->QueryDocumentList());
		if (docList != nil)
		{
			IDocument* openDoc = docList->FindDoc(file);
			if (KBSDocumentLivesInFile(openDoc, wantedPath))
			{
				outDocRef = ::GetUIDRef(openDoc);
				return true;
			}
		}
		// Not open - or an answer that is not this file, which is treated the same way, since the
		// open below resolves by file and cannot be confused. ***** NEVER return false from here.
		// ***** "It is not already open" is the ordinary case, and the old code turned two of its own
		// nil checks into a false, which the caller reports as a chapter that could not be opened.
	}

	// The windowless, UI-suppressed open - by FILE (the book may be closed).
	UIDRef docRef;
	const ErrorCode err = Utils<IDocumentCommands>()->Open(&docRef, file, kSuppressUI,
		IOpenFileCmdData::kOpenDefault, IOpenFileCmdData::kUseLockFile, kFalse /*showInWindow*/);
	if (err != kSuccess || docRef == UIDRef::gNull)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// a failed open must not poison later commands
		return false;
	}

	// Held, so ReleaseHeldDocs closes it later.
	gHeldDocs.push_back(docRef);
	outDocRef = docRef;
	return true;
}

bool KBSBookScope::ShowChapterWindow(const UIDRef& docRef)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// Does it already have a window - front, or behind another tab? Then leave it alone. Asked
	// through DocHasAnyWindow, which is where this module keeps that question (it wrote the search
	// out by hand here until the block 11 API audit, 2026-08-08, making three copies of it in one
	// file). That function has to be the ALL-presentations search: GetFrontmostPresentationForDocument
	// answers nil for a document sitting behind another tab, and acting on that would open a SECOND
	// window on the same document.
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
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
	if (cmd == nil)
		return false;
	cmd->SetItemList(UIDList(docRef));

	// The command's data interface, taken BEFORE processing so the result can be read back off it
	// afterwards. Nothing is set on it - the defaults are what a chapter window should get.
	//
	// ***** NO DATA INTERFACE = FAILURE, and the command is not run at all. ***** The recipe this
	// follows breaks off here too (SDKLayoutHelper.cpp:268-272). It used to run the command anyway
	// and skip the window test when this was nil, which returned TRUE without having established the
	// one thing this function's true means - that the chapter now has a window (block 11 API audit,
	// 2026-08-08).
	InterfacePtr<IOpenLayoutPresentationCmdData> openData(cmd, IID_IOPENLAYOUTCMDDATA);
	if (openData == nil)
		return false;

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// a failed open must not poison later commands
		return false;
	}

	// Did a window actually appear? SDKLayoutHelper::OpenLayoutWindow (the SDK's own recipe for this
	// command) does not stop at the return code: it reads GetResultingPresentation() and checks an
	// IWindow comes out of it, because "the command succeeded" and "there is a window" are two
	// different statements. Saying so here matters - the caller reports this chapter as shown.
	InterfacePtr<IWindow> window(openData->GetResultingPresentation(), UseDefaultIID());
	if (window == nil)
		return false;

	// It has a window now, so it is no longer part of the windowless reopen cache - dropping it
	// keeps a later ReleaseHeldDocs from closing a window the user is looking at.
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
	InterfacePtr<IDocumentList> docList(GetExecutionContextSession()->QueryDocumentList());
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

		// A dirty document would want a save - leave it to the user. Asked through the same
		// HasUnsavedChanges the held-chapter releases ask: this tested db == nil || IsModified()
		// by hand and so answered "do not close" for a document with no database, while
		// HasUnsavedChanges answers "nothing to lose" for that same document - one module, two
		// opposite verdicts on one state (block 11 API audit, 2026-08-08). Nothing changes on
		// screen: a document with no database has no window either, so the test below drops it.
		if (HasUnsavedChanges(ref))
			continue;

		// Only documents that HAVE a window go: a windowless held chapter survives as the reopen
		// cache (speed over tidiness).
		if (!DocHasAnyWindow(ref))
			continue;	// windowless - keep it held

		toClose.push_back(ref);
	}

	for (int32 i = 0; i < static_cast<int32>(toClose.size()); ++i)
	{
		// Drop it from the held list first (a closed held chapter must come off before its close),
		// through ForgetHeldDoc - which is the function for exactly that and whose own comment said
		// it was built to "the same shape CloseDisplayedDocsIfClean uses", i.e. it knew the loop was
		// written twice (block 11 API audit, 2026-08-08).
		KBSBookScope::ForgetHeldDoc(toClose[i]);

		InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(toClose[i]));
		if (docFileHandler == nil)
			continue;
		if (docFileHandler->CanClose(toClose[i]))
		{
			docFileHandler->Close(toClose[i], kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kSchedule);
			// Same as ReleaseHeldDocs' loop: Close reports nothing back, and this sweep runs in
			// the middle of a jump - an error left standing would fail the jump's next step.
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		}
	}
}

bool KBSBookScope::HasActiveBook()
{
	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil)
		return false;
	return bookMgr->GetCurrentActiveBook() != nil;	// non-owning pointer - no release
}

bool KBSBookScope::HasTargetBook()
{
	// The SAME two-step answer ListBookChapters gives, asked without building the chapter list: the
	// book PANEL's book first, the active book as the fallback. Until 2026-08-09 the menu gate and
	// the three engines' front doors all asked HasActiveBook alone while the run itself resolved
	// the panel's book - the same question answered two ways, which is the shape this plug-in keeps
	// being bitten by. A book on show in the panel with no active book behind it read as "no book
	// is open" at every door the run has, while the run itself could have searched it.
	IDFile panelBookFile;
	if (GetPanelBookFile(panelBookFile))
	{
		SDKFileHelper panelHelper(panelBookFile);
		if (FindOpenBookByPath(panelHelper.GetPath()) != nil)
			return true;
	}
	return HasActiveBook();
}

bool KBSBookScope::HasScopeTarget()
{
	// The same two questions the engines ask when they resolve their scope (KBSSearchEngine.cpp,
	// and the two scans beside it), asked here so the menu can go grey BEFORE a run that would only
	// report that there was nothing to run on.
	if (IsBookScopeOn())
		return HasTargetBook();

	// GetFrontDocument, not "is any document open": a book search opens its chapters WINDOWLESS and
	// this is the document-scope branch, where the front layout window is exactly what gets searched.
	// A chapter one of our own runs is holding open therefore does not count as a target, which is
	// what we want.
	return Utils<ILayoutUIUtils>()->GetFrontDocument() != nil;
}

void KBSBookScope::AppendUnopenableNote(PMString& outSummary,
	const std::vector<KBSBookScope::SkippedChapter>& skipped)
{
	if (skipped.empty())
		return;

	outSummary.Append("  ");
	outSummary.AppendNumber(static_cast<int32>(skipped.size()));
	outSummary.Append(" chapter(s) could not be opened (");
	for (size_t i = 0; i < skipped.size(); ++i)
	{
		if (i > 0)
			outSummary.Append(", ");
		if (i >= 3)								// a status line stays short, even at three lines
		{
			outSummary.Append("...");
			break;
		}
		// RAW, with its ampersands as the user typed them. What this builds is a STATUS LINE, and the
		// one place that draws one doubles the ampersands of the WHOLE line on its way to the widget
		// (KBSResultListWidgetMgr's WriteStatusWidget, since 2026-07-31). Doubling the name here as
		// well ran that twice: "A&B.indd" went to "A&&B.indd" and then to "A&&&&B.indd", which a
		// StaticText draws as "A&&B.indd" - and the extra one also reached app.kfcStatus and the
		// saved report's Summary line, which are supposed to hold the message verbatim
		// (found 2026-08-03 in the defect audit).
		//
		// The sibling sentence in the search engine (AppendUnsearchableNote) never doubled, and was
		// right not to. Anything that starts drawing this string WITHOUT going through the status
		// line has to do its own doubling, exactly as the tree's rows do (SetColumnText).
		PMString name(skipped[i].name);
		name.SetTranslatable(kFalse);
		outSummary.Append(name);
		if (!skipped[i].reason.IsEmpty())
		{
			outSummary.Append(": ");
			PMString reason(skipped[i].reason);
			reason.SetTranslatable(kFalse);
			outSummary.Append(reason);
		}
	}
	outSummary.Append(").");
}

void KBSBookScope::AppendUnclosedNote(PMString& outSummary, const std::vector<PMString>& names)
{
	if (names.empty())
		return;

	// The other end of the run from AppendUnopenableNote, and deliberately built to the same shape:
	// same count-then-name form, same three-and-then-"..." limit, same RAW names (the status line
	// doubles the ampersands of the whole message on its way to the widget - see the long note in
	// AppendUnopenableNote).
	outSummary.Append("  ");
	outSummary.AppendNumber(static_cast<int32>(names.size()));
	outSummary.Append(" chapter(s) left open with no window (");
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			outSummary.Append(", ");
		if (i >= 3)								// a status line stays short, even at three lines
		{
			outSummary.Append("...");
			break;
		}
		PMString name(names[i]);
		name.SetTranslatable(kFalse);
		outSummary.Append(name);
	}
	outSummary.Append(").");
}

bool KBSBookScope::GetSearchedBookPath(PMString& outPath)
{
	outPath = gSearchedBookPath;
	return !gSearchedBookPath.IsEmpty();
}

bool KBSBookScope::GetPanelBookFile(IDFile& outFile)
{
	if (!Utils<IBookUIUtils>().Exists())
		return false;

	// Walk every registered panel instead of asking for "the" book panel. Two earlier attempts
	// failed and are not worth repeating (measured 2026-07-27/28):
	//   - GetBookPanelWidget() returns nil for us. It is fed by the book panel's OWN actions
	//     (SetBookPanelWidget), so a command from another panel's flyout finds nothing stored.
	//   - GetBookFileFromBookPanel(file, nil) falls through to QueryActiveBookPanel(), i.e. the
	//     ACTIVE book - which is exactly the value we are trying not to use.
	// The walk works because InDesign creates one book panel per open book (tab count == panel
	// count) and registers each with IPanelMgr. Details: docs/ai-notes/book-panel-active-tab.md.
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return false;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return false;

	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return false;

	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		PMString panelName;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, nil, &panelName))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!IsBookPanelView(panelView, panelName))
			continue;

		// The front tab is decided on the CONTAINER, never on the panel. A book panel sitting
		// behind another tab still reports itself visible - all three panels came back
		// "Visible state 1" in the measurement - while only the front tab's kTabPanelContainerType
		// is visible. Asking panelView->IsVisible() here would match every book panel and pick
		// whichever came first.
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid())
			continue;
		if (!PaletteRefUtils::IsPaletteVisible(container))
			continue;

		// Keep looking when this panel could not be asked, rather than handing back a blank file.
		if (!GetBookFileFromPanelView(panelView, outFile))
			continue;

		return true;
	}

	// No visible book panel: it is iconised, its palette is closed, or no book is open at all.
	// The caller falls back to the active book, which is what the user expects in that state.
	return false;
}

bool KBSBookScope::IsBookStillOpen(const PMString& bookPath)
{
	// The path walk, the IsOpen() test and the reasons for both live in FindOpenBookByPath, which
	// ActivateBook needs as well - it wants the book itself, not just whether there is one.
	return FindOpenBookByPath(bookPath) != nil;
}

bool KBSBookScope::ActivateBook(const PMString& bookPath)
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
	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr != nil)
		bookMgr->SetCurrentActiveBook(book);

	// 2. The tab the user can SEE. One panel per open book, each registered with IPanelMgr, so the
	//    panel is found by walking the list and asking each candidate which book it belongs to -
	//    the WidgetID is numbered per book at runtime, which is why no name can be used here.
	if (!Utils<IBookUIUtils>().Exists())
		return true;	// the active book was still set; the tab is the part we could not do

	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return true;
	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return true;
	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return true;

	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		WidgetID panelWidgetID;
		PMString panelName;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, &panelWidgetID, &panelName))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!IsBookPanelView(panelView, panelName))
			continue;

		// Unlike GetPanelBookFile, visibility is NOT a filter here: the tab we are looking for is
		// precisely the one that is NOT in front yet.
		IDFile panelBookFile;
		if (!GetBookFileFromPanelView(panelView, panelBookFile))
			continue;

		SDKFileHelper panelFileHelper(panelBookFile);
		if (!(panelFileHelper.GetPath() == bookPath))
			continue;

		// kFalse = do not take the key focus. The keyboard walk calls this while the user is
		// holding an arrow key on the result tree; handing the focus to the book panel would end
		// the walk at the first book row.
		panelMgr->ShowPanelByWidgetID(panelWidgetID, kFalse);
		break;
	}
	return true;
}

bool KBSBookScope::ListBookChapters(std::vector<ChapterDoc>& outDocs, PMString& outBookName)
{
	outDocs.clear();
	outBookName.Clear();

	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil)
		return false;

	// Which book to search: the one the BOOK PANEL is showing, not the "active" one.
	//
	// Selecting a book's tab switches the panel but does NOT make that book active - only touching
	// a chapter inside it does (measured 2026-07-27). So a user who picks a tab and runs a search
	// gets whatever book was active before, silently. For a search that is confusing; for Change
	// Checked it would rewrite the wrong book, which is unacceptable. Adobe splits the two ideas in
	// IBookUIUtils itself (GetBookFileFromBookPanel vs "the active book"), so the panel's own book
	// is the right thing to ask for.
	//
	// Falls back to the active book when the panel cannot be reached, which keeps the old
	// behaviour rather than failing outright.
	IBook* book = nil;

	IDFile panelBookFile;
	if (GetPanelBookFile(panelBookFile))
	{
		// FindOpenBookByPath, not a bare FindOpenBookByName: the path lookup adds the IsOpen()
		// test that keeps a book still broadcasting its close off the list - the reason that
		// function itself documents. This was the one lookup in the module still asking bare
		// (found 2026-08-09, the one-question-one-place sweep).
		SDKFileHelper panelHelper(panelBookFile);
		book = FindOpenBookByPath(panelHelper.GetPath());	// nil = not open, or already closing
	}

	// GetCurrentActiveBook hands out a non-owning pointer - no release. Same for the lookup above.
	if (book == nil)
	{
		book = bookMgr->GetCurrentActiveBook();
		if (book != nil && !book->IsOpen())
			book = nil;		// the same closing-book door the panel-side lookup has
	}
	if (book == nil)
		return false;

	outBookName = book->GetBookTitleName();
	outBookName.SetTranslatable(kFalse);

	IDataBase* bookDB = ::GetDataBase(book);
	if (bookDB == nil)
		return false;

	// A different book than the one the last run held chapters for? Close those first. Chapters
	// are normally closed as each run finishes with them, so this only ever finds chapters a JUMP
	// reopened - which belong to the old book and have no reason to stay.
	SDKFileHelper bookFileHelper(book->GetBookFileSpec());
	const PMString bookPath = bookFileHelper.GetPath();
	if (!(gSearchedBookPath == bookPath))
		ReleaseHeldDocs();
	gSearchedBookPath = bookPath;

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
		content->GetIDFile(chapter.file);

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
	// here puts nothing on the panel at all (every run answers "The active book has no chapters."
	// and returns). Leaving the path standing left that statement true about a book with no results
	// behind it, which is the one thing the two readers of this value - KBSBookWatch and
	// KBSJump::ShowBook - are not allowed to be told.
	//
	// Cleared rather than restored to what it was: the callers all clear it through
	// ReleaseSearchedBook immediately before calling this, so there is no earlier value to go back
	// to, and "no results, no book" is the honest state either way.
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
	// pumps events through its progress bar. Looking it up costs one walk of a handful of books.
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
		// LIVES IN THIS CHAPTER'S FILE. Checking costs one string compare, and the material is
		// already here - this very call hands the file back in openSysFile.
		//
		// Asked through KBSDocumentLivesInFile, which is what ReopenChapterDoc asks, so the two
		// cannot come to differ. A chapter entry with no file of its own cannot be checked and is
		// taken as before: every BOOK chapter names a file (IBookContent::GetIDFile), so that case
		// does not arise here, and refusing it would only lose a document that is already open.
		SDKFileHelper chapterFileHelper(ioChapter.file);
		const PMString wantedPath = chapterFileHelper.GetPath();
		if (alreadyOpenDoc != nil
			&& (wantedPath.empty() || KBSDocumentLivesInFile(alreadyOpenDoc, wantedPath)))
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
