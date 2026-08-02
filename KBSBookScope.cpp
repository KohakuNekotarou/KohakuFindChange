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
#include "IBookUtils.h"			// OpenOneDocument, OriginallyCloseDocInfo, IsSourceDocumentAlreadyOpen
#include "IControlView.h"		// a panel IS a control view - what GetNthPanelInfo's UID resolves to
#include "IDataBase.h"			// IsModified (the hide sweep skips dirty docs)
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open by file (windowless reopen)
#include "IDocumentList.h"
#include "IDocumentUIUtils.h"	// FindPresentationForDocument (has-a-window test)
#include "IDocumentPresentation.h"	// the predicate typedef / presentation handle
#include "IDocumentUtils.h"
#include "IOpenFileCmdData.h"	// kOpenDefault / kUseLockFile
#include "ICommand.h"			// SetItemList - kOpenLayoutCmdBoss takes the document as its item
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear?
#include "IMenuUtils.h"			// InsertAmpersandForDisplay - a chapter name may contain '&'
#include "IPanelMgr.h"			// GetPanelCount / GetNthPanelInfo - one book panel per open book
#include "ISession.h"
#include "IWindow.h"			// the window kOpenLayoutCmdBoss is supposed to have produced

// General includes:
#include "ErrorUtils.h"			// PMSetGlobalErrorCode - a failed Open must not poison later commands
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "CmdUtils.h"
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss - give a windowless chapter a real window
#include "PaletteRefUtils.h"	// IsPaletteVisible - the front tab is decided on the container
#include "SDKFileHelper.h"
#include "UIDList.h"
#include "Utils.h"
#include "PMString.h"
#include "WideString.h"

// Project includes:
#include "KBSBookScope.h"

namespace
{
	// The chapters WE opened (only the originally-closed ones - OpenOneDocument records exactly
	// those in here), held open so a repeat search skips the load. OpenOneDocument also uses
	// this record to stay under the open-database cap.
	OriginallyCloseDocInfo gHeldDocInfo;

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

	/** The open book whose file path is 'bookPath', or nil when none has it. Non-owning - the
	    book manager keeps the list, so nothing here is released.

	    Paths rather than IBook pointers because one caller is a close notification: the closing
	    book's IBook is already gone by then, so there is no pointer left to compare.

	    The IsOpen() test is what makes this usable from that notification. Measured 2026-07-27 on
	    the release build: when kCloseBookCmdBoss is broadcast, the closing book is STILL on
	    IBookManager's list, so membership alone answers "yes, still open" for the very book that is
	    closing - which made the guard reject the one case it exists for. IBook::IsOpen is the flag
	    the close clears. */
	IBook* FindOpenBookByPath(const PMString& bookPath)
	{
		if (bookPath.IsEmpty())
			return nil;

		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		if (bookMgr == nil)
			return nil;

		const int32 bookCount = bookMgr->GetBookCount();
		for (int32 i = 0; i < bookCount; ++i)
		{
			IBook* book = bookMgr->GetNthBook(i);	// non-owning pointer - no release
			if (book == nil)
				continue;
			if (!book->IsOpen())
				continue;
			SDKFileHelper bookFileHelper(book->GetBookFileSpec());
			if (bookFileHelper.GetPath() == bookPath)
				return book;
		}
		return nil;
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
	InterfacePtr<IDocumentList> docList(GetExecutionContextSession()->QueryDocumentList());
	if (docList == nil)
		return false;
	const int32 count = docList->GetDocCount();
	for (int32 i = 0; i < count; ++i)
	{
		IDocument* doc = docList->GetNthDoc(i);
		if (doc != nil && ::GetUIDRef(doc) == docRef)
			return true;
	}
	return false;
}

void KBSBookScope::ReleaseHeldDocs()
{
	// NOTE: the searched-book path is NOT cleared here. Closing the chapters says nothing about
	// which book the panel is showing - and since every run closes its chapters as it goes, doing
	// so would blank the path while a full result set is still up (which broke both readers named
	// on gSearchedBookPath).
	if (gHeldDocInfo.fCurrentOpenedDocumentList.size() == 0)
		return;

	// Take the list first, so a re-entrant call finds it already empty instead of scheduling
	// the closes twice.
	K2Vector<UIDRef> held;
	held = gHeldDocInfo.fCurrentOpenedDocumentList;
	gHeldDocInfo.Clear();

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
		InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(held[i]));
		if (docFileHandler == nil)
			continue;
		if (docFileHandler->CanClose(held[i]))
			docFileHandler->Close(held[i], kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kSchedule);
	}
}

void KBSBookScope::ReleaseHeldDoc(const UIDRef& docRef)
{
	if (docRef == UIDRef::gNull)
		return;

	// Ours to close? A chapter the user already had open is not on this list and must stay. That
	// test lives HERE rather than at every call site: a run walks its chapters without caring who
	// opened which, and hands every one of them back the same way.
	int32 heldIndex = -1;
	for (int32 i = 0; i < static_cast<int32>(gHeldDocInfo.fCurrentOpenedDocumentList.size()); ++i)
	{
		if (gHeldDocInfo.fCurrentOpenedDocumentList[i] == docRef)
		{
			heldIndex = i;
			break;
		}
	}
	if (heldIndex < 0)
		return;

	// Off the list FIRST, so a re-entrant call cannot schedule the same close twice.
	gHeldDocInfo.fCurrentOpenedDocumentList.erase(
		gHeldDocInfo.fCurrentOpenedDocumentList.begin() + heldIndex);

	// The same close ReleaseHeldDocs uses, one document at a time: kSchedule defers it until the
	// current notification / idle tick has unwound, and kSuppressUI plus the run's dirty guard
	// (IDataBase::SaveRestoreModifiedState, which wraps every walk) means no save prompt. Skip a
	// chapter the user closed already - a dead UIDRef must not reach the close machinery.
	//
	// Do NOT "improve" this to IBookUtils::CloseDocumentsInBook: it takes no UI flag and no command
	// mode, closes immediately, and crashed KESCL in 2026-07-17 when called from a notification.
	// See the longer note on ReleaseHeldDocs.
	if (!IsDocStillOpen(docRef))
		return;
	InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(docRef));
	if (docFileHandler == nil)
		return;
	if (docFileHandler->CanClose(docRef))
		docFileHandler->Close(docRef, kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kSchedule);
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
	gHeldDocInfo.Clear();
	gSearchedBookPath.Clear();
	gBookScopeOn = false;
}

// Accepts every presentation. A local stand-in for the stock accept-all predicate (KESCL keeps
// its own too, so we do not depend on where the stock predicate objects live).
static bool KBSAcceptAnyPresentation(IDocumentPresentation* /*p*/)
{
	return true;
}

bool KBSBookScope::ReopenChapterDoc(const IDFile& file, UIDRef& outDocRef)
{
	outDocRef = UIDRef::gNull;

	SDKFileHelper fileHelper(file);
	if (fileHelper.GetPath().empty())
		return false;	// a front-document entry carries no file - nothing to reopen

	// If the user reopened it themselves, rebind to THEIR document and do NOT hold it (closing it
	// would surprise them).
	{
		int32 fileIndex = -1;
		if (Utils<IBookUtils>()->IsSourceDocumentAlreadyOpen(file, fileIndex))
		{
			InterfacePtr<IDocumentList> docList(GetExecutionContextSession()->QueryDocumentList());
			if (docList != nil)
			{
				IDocument* doc = docList->GetNthDoc(fileIndex);
				if (doc != nil)
				{
					outDocRef = ::GetUIDRef(doc);
					return true;
				}
			}
			return false;
		}
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
	gHeldDocInfo.fCurrentOpenedDocumentList.push_back(docRef);
	outDocRef = docRef;
	return true;
}

bool KBSBookScope::ShowChapterWindow(const UIDRef& docRef)
{
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// Does it already have a window - front, or behind another tab? Then leave it alone. This has
	// to be the ALL-presentations search: GetFrontmostPresentationForDocument answers nil for a
	// document sitting behind another tab, and acting on that would open a SECOND window on the
	// same document.
	FindPresentation_PreferCriteria noPreference;
	if (Utils<IDocumentUIUtils>()->FindPresentationForDocument(db, KBSAcceptAnyPresentation, noPreference) != nil)
		return false;

	// Windowless (the search opened it that way): give it a real layout window so the user can
	// see the replacement, undo it by hand, and decide about saving. Nothing is saved here.
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
	if (cmd == nil)
		return false;
	cmd->SetItemList(UIDList(docRef));

	// The command's data interface, taken BEFORE processing so the result can be read back off it
	// afterwards. Nothing is set on it - the defaults are what a chapter window should get.
	InterfacePtr<IOpenLayoutPresentationCmdData> openData(cmd, IID_IOPENLAYOUTCMDDATA);

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// a failed open must not poison later commands
		return false;
	}

	// Did a window actually appear? SDKLayoutHelper::OpenLayoutWindow (the SDK's own recipe for this
	// command) does not stop at the return code: it reads GetResultingPresentation() and checks an
	// IWindow comes out of it, because "the command succeeded" and "there is a window" are two
	// different statements. Saying so here matters - the caller reports this chapter as shown.
	if (openData != nil)
	{
		InterfacePtr<IWindow> window(openData->GetResultingPresentation(), UseDefaultIID());
		if (window == nil)
			return false;
	}

	// It has a window now, so it is no longer part of the windowless reopen cache - dropping it
	// keeps a later ReleaseHeldDocs from closing a window the user is looking at.
	for (int32 i = 0; i < static_cast<int32>(gHeldDocInfo.fCurrentOpenedDocumentList.size()); ++i)
	{
		if (gHeldDocInfo.fCurrentOpenedDocumentList[i] == docRef)
		{
			gHeldDocInfo.fCurrentOpenedDocumentList.erase(
				gHeldDocInfo.fCurrentOpenedDocumentList.begin() + i);
			break;
		}
	}
	return true;
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

		IDataBase* db = ref.GetDataBase();
		if (db == nil || db->IsModified())
			continue;	// a dirty document would want a save - leave it to the user

		// Only documents that HAVE a window go: a windowless held chapter survives as the reopen
		// cache (speed over tidiness).
		FindPresentation_PreferCriteria noPreference;
		IDocumentPresentation* pres = Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, KBSAcceptAnyPresentation, noPreference);
		if (pres == nil)
			continue;	// windowless - keep it held

		toClose.push_back(ref);
	}

	for (int32 i = 0; i < static_cast<int32>(toClose.size()); ++i)
	{
		// Drop it from the held list first (a closed held chapter must come off before its close).
		for (int32 h = static_cast<int32>(gHeldDocInfo.fCurrentOpenedDocumentList.size()) - 1; h >= 0; --h)
		{
			if (gHeldDocInfo.fCurrentOpenedDocumentList[h] == toClose[i])
				gHeldDocInfo.fCurrentOpenedDocumentList.erase(gHeldDocInfo.fCurrentOpenedDocumentList.begin() + h);
		}
		InterfacePtr<IDocFileHandler> docFileHandler(Utils<IDocumentUtils>()->QueryDocFileHandler(toClose[i]));
		if (docFileHandler == nil)
			continue;
		if (docFileHandler->CanClose(toClose[i]))
			docFileHandler->Close(toClose[i], kSuppressUI, kFalse /*allowCancel*/, IDocFileHandler::kSchedule);
	}
}

bool KBSBookScope::HasActiveBook()
{
	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil)
		return false;
	return bookMgr->GetCurrentActiveBook() != nil;	// non-owning pointer - no release
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
		PMString name(skipped[i].name);
		name.SetTranslatable(kFalse);
		Utils<IMenuUtils>()->InsertAmpersandForDisplay(&name);	// this string gets DRAWN
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
		if (panelView == nil)
			continue;

		const bool isBookPanel = (::GetClass(panelView).Get() == kBookPanelBossRawClassID)
								 || PanelNameMatchesOpenBook(panelName);
		if (!isBookPanel)
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

		// Hand the panel itself in. With a real widget this resolves THAT panel's book instead of
		// falling through to the active book the way a nil widget does.
		IDFile panelBookFile;
		Utils<IBookUIUtils>()->GetBookFileFromBookPanel(panelBookFile, panelView);

		// An empty result means the panel could not be resolved - keep looking rather than handing
		// back a blank file that would later look like "no book".
		SDKFileHelper panelFileHelper(panelBookFile);
		if (panelFileHelper.GetPath().empty())
			continue;

		outFile = panelBookFile;
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
		if (panelView == nil)
			continue;

		const bool isBookPanel = (::GetClass(panelView).Get() == kBookPanelBossRawClassID)
								 || PanelNameMatchesOpenBook(panelName);
		if (!isBookPanel)
			continue;

		// Unlike GetPanelBookFile, visibility is NOT a filter here: the tab we are looking for is
		// precisely the one that is NOT in front yet.
		IDFile panelBookFile;
		Utils<IBookUIUtils>()->GetBookFileFromBookPanel(panelBookFile, panelView);
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
		book = bookMgr->FindOpenBookByName(panelBookFile);	// nil = that file is not open

	// GetCurrentActiveBook hands out a non-owning pointer - no release. Same for FindOpenBookByName.
	if (book == nil)
		book = bookMgr->GetCurrentActiveBook();
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
		return false;

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

	return !outDocs.empty();
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
	// If this answers nil for a document that IS open, nothing breaks: ReopenChapterDoc below
	// asks the same question a second way (IsSourceDocumentAlreadyOpen) and rebinds to the
	// user's copy. So the fallback covers the case where this API does not behave as read.
	if (bookDB != nil && ioChapter.contentUID != kInvalidUID)
	{
		IDFile openSysFile;
		IDocument* alreadyOpenDoc = Utils<IBookUtils>()->FindDocFromContentUID(
			bookDB, ioChapter.contentUID, openSysFile, isMissingPlugins, kFalse /*bShowAlert*/);
		if (alreadyOpenDoc != nil)
		{
			// The user's (or an earlier run's) own copy. NOT held: closing a document somebody
			// else opened would surprise them - the same rule ReopenChapterDoc follows, and the
			// reason ReleaseHeldDoc can be called on every chapter without checking who opened it.
			ioChapter.docRef = ::GetUIDRef(alreadyOpenDoc);
			return true;
		}
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

bool KBSBookScope::GetBookChapterDocs(std::vector<ChapterDoc>& outDocs, PMString& outBookName,
	std::vector<SkippedChapter>* outSkipped)
{
	// TRANSITIONAL: the engines are being moved to the one-chapter-at-a-time form
	// (ListBookChapters + OpenChapterDoc + ReleaseHeldDoc). This keeps the old all-at-once shape
	// working meanwhile, built out of the new pieces so there is only one open path to maintain.
	if (outSkipped != nil)
		outSkipped->clear();

	std::vector<ChapterDoc> listed;
	if (!ListBookChapters(listed, outBookName))
	{
		outDocs.clear();
		return false;
	}

	outDocs.clear();
	for (size_t i = 0; i < listed.size(); ++i)
	{
		if (OpenChapterDoc(listed[i], outSkipped))
			outDocs.push_back(listed[i]);
	}
	return !outDocs.empty();
}

// End, KBSBookScope.cpp.
