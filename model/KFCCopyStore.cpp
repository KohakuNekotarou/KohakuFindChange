//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  KFC's own copy stores - see KFCCopyStore.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IDataBase.h"
#include "IDataExchangeController.h"
#include "ITextModel.h"
#include "ITextModelCmds.h"		// DeleteCmd - a copy with no text put back
#include "ITextUtils.h"			// CreateNewStory, QueryCopyStoryToEndCommand, QueryCopyStoryCommand

// General includes:
#include "CDataExchangeController.h"
#include "CmdUtils.h"
#include "CreateObject.h"
#include "DBUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"			// IsEqual - a reopened chapter's store found by its file
#include "IDFile.h"
#include "RangeData.h"			// StoryRange
#include "Utils.h"

#include <map>

// Project includes:
#include "KFCCopyStore.h"
#include "KFCDiag.h"			// KFC_DIAG_LOG / KFC_DIAG_FAULT - test builds only
#include "KFCID.h"

namespace
{
// THE NUMBER THE NEXT STORE IS MADE WITH - read by the controller as it is constructed, for its scrap database's file
// name: CDataExchangeController.h asks that name to be unique, and InDesign keeps a scrap database as a file in its
// Caches folder (its own clipboards are "InDesign ClipboardScrap1", "...2" - measured, 2026-10-17). KFC holds one store
// per document, so each takes the number of its own.
uint32 gNamingNumber = 0;
}

/** THE STORE'S CONTROLLER - KIDMCP's task-start store's (KIDMCPTaskStoreController), itself KT's spike's
	(KTScrapStoreController, branch exp/2026-10-06-private-scrap): nothing registers it with the clipboard or a drag, so
	the person's clipboard is never touched and the scrap is nobody else's. */
class KFCCopyStoreController : public CDataExchangeController
{
public:
	KFCCopyStoreController(IPMUnknown* boss) : CDataExchangeController(boss), fNumber(gNamingNumber) {}
	virtual ~KFCCopyStoreController() {}

	virtual IDataExchangeHandler* QueryHandlerForInternalize(DataExchangeResponse& /*response*/,
		const PMFlavor& /*internalizeToThisFlavor*/) const
	{
		return nil;		// nothing is ever brought in from outside
	}
	virtual bool16 NeedsToInternalize() const
	{
		return kFalse;
	}
	virtual ErrorCode FulfillPromise(IPMDataObject* /*whichItem*/, const ExternalPMFlavor& /*whichFlavor*/) const
	{
		return kFailure;	// nothing is ever promised to another application
	}

	/** NEVER PURGED (KIDMCP's addition): a scrap may be purged when memory runs low (CDataExchangeController.h, "Memory
		purge") - the clipboard's can be, since the person can copy again. These are the only copies of what the rows'
		Replace wrote over, so a store is let go by KFCCopyStore and by nothing else. */
	virtual void Purge(int32 /*level*/) {}

protected:
	virtual PMString GetScrapDBFileName() const
	{
		PMString name("KohakuFindChangeCopyStore");
		name.AppendNumber(static_cast<int32>(fNumber));
		name.SetTranslatable(kFalse);
		return name;
	}
	virtual IDataBase* CreateDataBase() const
	{
		// DBUtils.h: set the magic bytes of the kind of database to make, make it, set them back.
		static const DBUtils::MagicBytes kStoreBytes = { 'K', 'F', 'C', 'C', 'O', 'P', 'Y', '1' };
		DBUtils::SetMagicBytes(kStoreBytes);
		IDataBase* db = DBUtils::CreateDataBase(IDataBase::kNoUndoSupport);
		ResetMagicBytes();
		return db;
	}
	virtual void ResetMagicBytes() const
	{
		static const DBUtils::MagicBytes kDocumentBytes = { 'D', 'O', 'C', 'U', 'M', 'E', 'N', 'T' };
		DBUtils::SetMagicBytes(kDocumentBytes);
	}

private:
	const uint32	fNumber;
};

CREATE_PMINTERFACE(KFCCopyStoreController, kKFCCopyStoreImpl)

namespace
{
// One store: its controller, and the document it holds copies of.
struct Store
{
	IDataExchangeController*	controller;	// held - let go by LetGo
	IDataBase*					openDB;		// the document while it is open; nil once it closed. Compared, never followed.
	IDFile						file;		// the file it closed from (hasFile) - how the document is found again
	bool						hasFile;
	Store() : controller(nil), openDB(nil), hasFile(false) {}
};

// THE STORES, BY NUMBER. A RAW POINTER IN EACH, NOT AN InterfacePtr: they are let go while the session is whole
// (ShutdownCleanup); a static InterfacePtr would release whatever is left at DLL unload, after the object model has gone.
std::map<uint32, Store>	gStores;
uint32					gLastNumber = 0;

void LetGo(Store& store)
{
	IDataExchangeController* const going = store.controller;
	store.controller = nil;
	store.openDB = nil;
	if (going != nil)
	{
		// Cleanup deletes the scrap database ("Release referenced objects and destroy database",
		// CDataExchangeController.h); a scrap is independent of the document it was copied from, so this is safe before
		// or after that document closes (KT measured copies outliving their document, 2026-10-06).
		going->Cleanup();
		going->Release();
	}
}

// The store's database, or nil (no such store, or one let go).
IDataBase* StoreDB(uint32 number)
{
	const std::map<uint32, Store>::const_iterator it = gStores.find(number);
	return (it != gStores.end() && it->second.controller != nil) ? it->second.controller->GetScrapDataBase() : nil;
}

// THE STORE OF A DOCUMENT: the one bound to it; else one whose document closed from the same file (a chapter closed
// and opened again - its rows still name copies there); else a new one. 0 = none could be made.
uint32 StoreFor(IDataBase* db)
{
	for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); ++it)
		if (it->second.controller != nil && it->second.openDB == db)
			return it->first;
	const IDFile* file = db->GetSysFile();
	if (file != nil)
	{
		for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); ++it)
		{
			Store& s = it->second;
			if (s.controller != nil && s.openDB == nil && s.hasFile && FileUtils::IsEqual(s.file, *file))
			{
				s.openDB = db;
				KFC_DIAG_LOG("COPY store %u bound again doc=%p", (unsigned)it->first, (void*)db);
				return it->first;
			}
		}
	}
	const uint32 number = gLastNumber + 1;
	gNamingNumber = number;		// read by the controller's constructor (its scrap database's file name)
	IDataExchangeController* const controller = ::CreateObject2<IDataExchangeController>(kKFCCopyStoreBoss);
	if (controller == nil)
	{
		KFC_DIAG_LOG("COPY store could not be made - the boss (is KFCFactoryList.h current?)");
		return 0;
	}
	const ErrorCode init = controller->Initialize();
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (init != kSuccess || !controller->IsValid() || controller->GetScrapDataBase() == nil)
	{
		KFC_DIAG_LOG("COPY store could not be made err=%d valid=%d", (int)init, controller->IsValid() ? 1 : 0);
		controller->Cleanup();
		controller->Release();
		return 0;
	}
	Store store;
	store.controller = controller;
	store.openDB = db;
	gStores[number] = store;
	gLastNumber = number;
	KFC_DIAG_LOG("COPY store %u made doc=%p", (unsigned)number, (void*)db);
	return number;
}

// The primary thread's text length of a story - its span without the closing return; -1 when it cannot be read.
int32 PrimaryLength(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	return (model != nil) ? model->GetPrimaryStoryThreadSpan() - 1 : -1;
}
}	// anonymous namespace

bool KFCCopyStore::Take(const UIDRef& story, TextIndex start, TextIndex end, CopyRef& outCopy)
{
	outCopy = CopyRef();
	IDataBase* const db = story.GetDataBase();
	// NOTHING PROCESSED OVER AN ERROR ALREADY STANDING: a command processed while the global error code is set is
	// InDesign's protective shutdown (CmdUtils.h, ProcessCommand) - and the error is not this function's to clear.
	if (db == nil || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		return false;
#ifdef KFC_DIAG
	// (Fault switch copy-fail, a test build's only - KFCDiag.h: no copy is kept, the Replace goes on without it.)
	if (KFC_DIAG_FAULT("copy-fail"))
	{
		KFC_DIAG_LOG("COPY take story=%u at=%d len=%d ok=0 (fault switch)", story.GetUID().Get(), (int)start, (int)(end - start));
		return false;
	}
#endif
	// THE RANGE MUST LIE IN THE STORY'S PRIMARY THREAD: the copy command checks nothing - [1, 2) of an empty story, and a
	// range run into a table cell's thread, crashed InDesign in KT (2026-10-06).
	const int32 span = PrimaryLength(story);
	if (span < 0 || start < 0 || end < start || end > span)
		return false;
	const uint32 number = StoreFor(db);
	IDataBase* const sdb = StoreDB(number);
	if (sdb == nil)
		return false;
	const UID copy = Utils<ITextUtils>()->CreateNewStory(sdb, kFalse);
	bool ok = (copy != kInvalidUID) && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
	if (ok && end > start)
	{
		InterfacePtr<ICommand> cmd(Utils<ITextUtils>()->QueryCopyStoryToEndCommand(story, UIDRef(sdb, copy),
			StoryRange(start, end)));
		ok = (cmd != nil && CmdUtils::ProcessCommand(cmd) == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess);
	}
	// What the copy raised is the store's, not the caller's - who found no error standing.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	CopyRef taken;
	taken.store = number;
	taken.story = copy;
	// THE COPY MUST READ AS LONG AS THE RANGE: a copy that came out otherwise is not offered back.
	const int32 len = ok ? Length(taken) : -1;
	ok = ok && (len == end - start);
	KFC_DIAG_LOG("COPY take story=%u at=%d len=%d -> store=%u copy=%u copied=%d ok=%d", story.GetUID().Get(), (int)start,
		(int)(end - start), (unsigned)number, copy.Get(), (int)len, ok ? 1 : 0);
	if (!ok)
		return false;
	outCopy = taken;
	return true;
}

int32 KFCCopyStore::Length(const CopyRef& copy)
{
	IDataBase* const sdb = StoreDB(copy.store);
	if (sdb == nil || copy.story == kInvalidUID || !sdb->IsValidUID(copy.story))
		return -1;
	return PrimaryLength(UIDRef(sdb, copy.story));
}

bool KFCCopyStore::PasteOver(const CopyRef& copy, const UIDRef& story, TextIndex start, TextIndex end)
{
	const int32 len = Length(copy);
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	// The same check as the copy's: the commands check nothing.
	const int32 span = (model != nil) ? model->GetPrimaryStoryThreadSpan() - 1 : -1;
	if (len < 0 || span < 0 || start < 0 || end < start || end > span || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		KFC_DIAG_LOG("COPY paste refused store=%u copy=%u len=%d story=%u at=%d over=%d span=%d", (unsigned)copy.store,
			copy.story.Get(), (int)len, story.GetUID().Get(), (int)start, (int)(end - start), (int)span);
		return false;
	}
	ErrorCode result = kSuccess;
	if (len == 0)
	{
		// A COPY WITH NO TEXT (a zero-width match's): the range written is taken away, nothing put in its place.
		if (end > start)
		{
			InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
			InterfacePtr<ICommand> del(cmds != nil ? cmds->DeleteCmd(start, end - start) : nil);
			result = (del != nil) ? CmdUtils::ProcessCommand(del) : kFailure;
		}
	}
	else
	{
		// ONE COMMAND THAT REPLACES THE RANGE WITH THE COPY (ITextUtils.h: "This replaces the range in the destination
		// story with a copy of the specified text from the source story").
		InterfacePtr<ICommand> cmd(Utils<ITextUtils>()->QueryCopyStoryCommand(UIDRef(StoreDB(copy.store), copy.story),
			story, StoryRange(0, len), StoryRange(start, end)));
		result = (cmd != nil) ? CmdUtils::ProcessCommand(cmd) : kFailure;
	}
	const bool ok = (result == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess);
	KFC_DIAG_LOG("COPY paste store=%u copy=%u len=%d story=%u at=%d over=%d ok=%d", (unsigned)copy.store, copy.story.Get(),
		(int)len, story.GetUID().Get(), (int)start, (int)(end - start), ok ? 1 : 0);
	return ok;
}

void KFCCopyStore::DocumentClosing(IDataBase* db)
{
	if (db == nil)
		return;
	const IDFile* file = db->GetSysFile();
	for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); )
	{
		Store& s = it->second;
		if (s.openDB != db)
		{
			++it;
			continue;
		}
		if (file == nil)
		{
			// A DOCUMENT NEVER SAVED: no row can reach it again - it is gone with its rows (LIFE-01) - so its copies go too.
			KFC_DIAG_LOG("COPY store %u let go - its document closes, never saved", (unsigned)it->first);
			LetGo(s);
			it = gStores.erase(it);
			continue;
		}
		s.openDB = nil;
		s.file = *file;
		s.hasFile = true;
		KFC_DIAG_LOG("COPY store %u kept - its document closes, found again by its file", (unsigned)it->first);
		++it;
	}
}

void KFCCopyStore::ShutdownCleanup()
{
	for (std::map<uint32, Store>::iterator it = gStores.begin(); it != gStores.end(); ++it)
		LetGo(it->second);
	std::map<uint32, Store>().swap(gStores);	// fresh storage, not Clear() (memory plugin-teardown-robustness)
	gLastNumber = 0;
}

// End, KFCCopyStore.cpp.
