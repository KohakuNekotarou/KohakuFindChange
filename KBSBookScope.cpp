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
#include "IBook.h"
#include "IBookContent.h"
#include "IBookContentMgr.h"
#include "IBookManager.h"
#include "IBookUtils.h"			// OpenOneDocument, OriginallyCloseDocInfo, IsSourceDocumentAlreadyOpen
#include "IDataBase.h"			// IsModified (the hide sweep skips dirty docs)
#include "IDocFileHandler.h"
#include "IDocument.h"
#include "IDocumentCommands.h"	// Open by file (windowless reopen)
#include "IDocumentList.h"
#include "IDocumentUIUtils.h"	// FindPresentationForDocument (has-a-window test)
#include "IDocumentPresentation.h"	// the predicate typedef / presentation handle
#include "IDocumentUtils.h"
#include "IOpenFileCmdData.h"	// kOpenDefault / kUseLockFile
#include "ISession.h"

// General includes:
#include "ErrorUtils.h"			// PMSetGlobalErrorCode - a failed Open must not poison later commands
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "SDKFileHelper.h"
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

	// Which book the held chapters belong to (its full file path). A search against a DIFFERENT
	// book first closes the previous book's held chapters, so switching books never piles up
	// hidden documents from every book visited.
	PMString gHeldBookPath;

	// The search-scope toggle. Session state only (every launch starts OFF), like KESCL's
	// gBookSearchOn: OFF searches the front document, ON the whole active book.
	bool gBookScopeOn = false;
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
	if (gHeldDocInfo.fCurrentOpenedDocumentList.size() == 0)
	{
		gHeldBookPath.Clear();
		return;
	}

	// Take the list first, so a re-entrant call finds it already empty instead of scheduling
	// the closes twice.
	K2Vector<UIDRef> held;
	held = gHeldDocInfo.fCurrentOpenedDocumentList;
	gHeldDocInfo.Clear();
	gHeldBookPath.Clear();

	// Close each chapter through the stock document close. kSchedule defers each close until the
	// current notification / idle tick has unwound; kSuppressUI + the search-time dirty guard =
	// no save prompt. Skip chapters the user closed already (a dead UIDRef must not reach the
	// close machinery).
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

void KBSBookScope::ShutdownCleanup()
{
	// State only, no closing, no UI: this runs while InDesign is tearing down. Clear() releases
	// the vector's storage too, so the static destructor at DLL unload finds nothing to do.
	gHeldDocInfo.Clear();
	gHeldBookPath.Clear();
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

bool KBSBookScope::GetBookChapterDocs(std::vector<ChapterDoc>& outDocs, PMString& outBookName)
{
	outDocs.clear();
	outBookName.Clear();

	InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
	if (bookMgr == nil)
		return false;

	// The one active book (the front-most open book panel). GetCurrentActiveBook hands out a
	// non-owning pointer - no release.
	IBook* book = bookMgr->GetCurrentActiveBook();
	if (book == nil)
		return false;

	outBookName = book->GetBookTitleName();
	outBookName.SetTranslatable(kFalse);

	IDataBase* bookDB = ::GetDataBase(book);
	if (bookDB == nil)
		return false;

	// A different book than the chapters we hold were opened for? Close those first, then
	// remember the new book as the held one.
	SDKFileHelper bookFileHelper(book->GetBookFileSpec());
	const PMString bookPath = bookFileHelper.GetPath();
	if (!(gHeldBookPath == bookPath))
	{
		ReleaseHeldDocs();
		gHeldBookPath = bookPath;
	}

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

		// Open the chapter's document without a layout window (or reuse it if it is already
		// open). Chapters that cannot be opened - missing file, document locked by another user -
		// are skipped; the searchable rest is still worth having. gHeldDocInfo accumulates
		// whatever this call had to open.
		UIDRef docRef;
		if (Utils<IBookUtils>()->OpenOneDocument(bookDB, contentUID, docRef, gHeldDocInfo) != kSuccess)
			continue;
		if (docRef == UIDRef::gNull)
			continue;

		ChapterDoc chapter;
		chapter.docRef = docRef;
		// The chapter's .indd, so navigation can reopen the chapter if the user closes it.
		content->GetIDFile(chapter.file);
		// The chapter's file name for the read-out. Via the UTF-16 buffer (AppendW), so a
		// Japanese chapter name survives - the PMString(char*) conversions do not.
		WideString shortName = content->GetShortName();
		chapter.shortName.SetTranslatable(kFalse);
		const UTF16TextChar* buf = shortName.GrabUTF16Buffer(nil);
		if (buf != nil)
			chapter.shortName.AppendW(buf);
		outDocs.push_back(chapter);
	}

	return !outDocs.empty();
}

// End, KBSBookScope.cpp.
