//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Object tab's search - see KFCObjectSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAnchoredObjectData.h"	// inline or anchored - where an item inside text stands (O8)
#include "IDataBase.h"
#include "IDocument.h"
#include "IFindChangeOptions.h"
#include "IFindChangeService.h"
#include "IHierarchy.h"
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ILayoutUtils.h"			// GetOwnerPageUID / IsAMaster
#include "IObjectWalker.h"
#include "IPageItemNameFacade.h"	// a row's item by the name the Layers panel shows (2026-10-10)
#include "IPageList.h"				// GetPageString / GetPageIndex
#include "IPMStream.h"
#include "ISession.h"
#include "ISnippetExport.h"			// the fingerprint (O12)
#include "ITextWalkerSelectionUtils.h"	// the selection snapshot (O6)
#include "IWalkerScopeFactoryUtils.h"

// General includes:
#include "AttributeBossList.h"		// Find / Change Object Format: CountBosses
#include "CreateObject.h"
#include "ErrorUtils.h"
#include "GroupID.h"				// kGroupItemBoss
#include "PersistUtils.h"			// ::GetUIDRef
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "SpreadID.h"				// kPageBoss
#include "StreamUtil.h"				// CreateMemoryStreamWrite
#include "TextChar.h"				// kTextChar_Ellipse - a long item name, cut
#include "TextID.h"					// kInlineBoss
#include "TextWalkerServiceProviderID.h"	// kObjectWalkerService, kFindChangeServiceBoss, kTextWalkerService
#include "Utils.h"

#include <cstring>					// memcmp - the fingerprint's end at the XMP packet
#include <set>
#include <utility>

// Project includes:
#include "IKFCUIServices.h"			// GetSelectedPageItems - the selection is the UI half's
#include "KFCDiag.h"				// KFC_DIAG_LOG - the OBJWALK / OBJSEL trace, test builds only
#include "KFCMemXferBytes.h"
#include "KFCObjectSearch.h"
#include "KFCPageItemFacts.h"
#include "KFCProgressBar.h"

namespace
{
// THE WALK'S GUARD: no document's walk asks the service more often than this. InDesign ends a walk itself (kNotFound -
// measured) and the walk below stops on an item given twice; this is for a walk that does neither.
const int32 kMaxWalkSteps = 100000;

// "Chapter 3 / 12 - ch03.indd" - the text search's form (KFCSetChapterTask, KFCSearchEngine.cpp).
void SetTask(KFCProgressBar& bar, const char* noun, size_t index, size_t count, const PMString& name)
{
	PMString task(noun);
	task.SetTranslatable(kFalse);
	task.Append(" ");
	task.AppendNumber(static_cast<int32>(index) + 1);
	task.Append(" / ");
	task.AppendNumber(static_cast<int32>(count));
	task.Append(" - ");
	task.Append(name);
	bar.SetTaskText(task);
}

// THE USER'S SELECTION, GIVEN BACK (O6). The text walker service's snapshot - the spelling panel's, which InDesign's own
// search takes the same way (ITextWalkerSelectionUtils.h, SaveSelectionsSnapshot / RestoreSelectionsSnapshot); measured
// with an object search over a windowless document and over the front one (KT, 2026-10-09 and the plan's Task 1 M4 -
// five selection states, the selection and the active layer the same before and after). Never around a write: see the
// header.
class SelectionKeeper
{
public:
	SelectionKeeper() : fUtils(nil)
	{
		InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
		InterfacePtr<IK2ServiceProvider> provider(registry != nil
			? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss) : nil);
		InterfacePtr<ITextWalkerSelectionUtils> utils(provider, UseDefaultIID());
		fUtils = utils.forget();
		if (fUtils != nil)
			fUtils->SaveSelectionsSnapshot();
	}
	~SelectionKeeper()
	{
		if (fUtils == nil)
			return;
		fUtils->RestoreSelectionsSnapshot();
		fUtils->Release();
	}
private:
	ITextWalkerSelectionUtils* fUtils;
	SelectionKeeper(const SelectionKeeper&);
	SelectionKeeper& operator=(const SelectionKeeper&);
};

// How one document's walk ended - which is not how many rows it found (the text search's ChapterWalkResult, cut down).
enum WalkEnd
{
	kWalkDone = 0,		// InDesign said there is no more (kNotFound), or came round to an item already given
	kWalkBroke			// the service failed, or handed back an item of another database - the rest was not looked at
};

// ONE DOCUMENT'S WALK (O2 / O5): the shared walker initialised for it, InDesign's SearchObject asked "as is" until it
// stops answering kSuccess, each match read off the shared walker. Read-only: the document's "unsaved" flag is put back
// (SaveRestoreModifiedState - the text walk's guard).
// fromSearchScope (2026-10-10 - the author's call: Search: = Selection with the cursor in text searches the frame the
// cursor stands in, as Find/Change's own Find does): the walker is NOT initialised here - the first SearchObject asks
// InDesign to initialise it from Search: itself (kTrue), which takes the caret's frame (measured: the plan's Task 1 M6),
// and the steps after it go on "as is". Only for the front document, whose selection that is.
WalkEnd WalkDoc(IFindChangeService* svc, IObjectWalker* shared, const UIDRef& docRef, const UIDList* items, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool fromSearchScope)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || svc == nil || shared == nil)
		return kWalkBroke;
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	if (!fromSearchScope)
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, items));
	std::set<UID> seen;
	for (int32 step = 0; step < kMaxWalkSteps; ++step)
	{
		UIDRef found;	// stays empty even on kSuccess (measured) - the match is the shared walker's current item
		const IFindChangeService::FindChangeResult result =
			svc->SearchObject(found, (fromSearchScope && step == 0) ? kTrue : kFalse);
#ifdef KFC_DIAG
		{
			// (Test builds only) Each step's answer and what it left selected - the trace of the author's report E.
			InterfacePtr<IKFCUIServices> diagUi(GetExecutionContextSession(), UseDefaultIID());
			KFC_DIAG_LOG("OBJWALK step=%d result=%d current=%u selected=%d", static_cast<int>(step), static_cast<int>(result),
				static_cast<unsigned>(shared->GetCurrentItem().GetUID().Get()),
				(diagUi != nil && diagUi->HasAnySelection()) ? 1 : 0);
		}
#endif
		if (result == IFindChangeService::kNotFound || result == IFindChangeService::kFoundCompleted)
			return kWalkDone;
		if (result != IFindChangeService::kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			return kWalkBroke;
		}
		const UIDRef current(shared->GetCurrentItem());
		if (current.GetDataBase() != db || current.GetUID() == kInvalidUID)
			return kWalkBroke;
		if (!seen.insert(current.GetUID()).second)
			return kWalkDone;
		if (outHits.size() >= limit)
		{
			outCapped = true;
			return kWalkDone;
		}
		KFCResultModel::Hit hit;
		KFCObjectSearch::BuildObjectHit(docRef, current.GetUID(), hit);
		outHits.push_back(std::move(hit));
	}
	return kWalkBroke;
}
}	// anonymous namespace

IObjectWalker* KFCObjectSearch::QuerySharedWalker()
{
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(registry != nil
		? registry->QueryServiceProviderByClassID(kObjectWalkerService, kObjectWalkerServiceProviderBoss) : nil);
	InterfacePtr<IObjectWalker> walker(provider, UseDefaultIID());
	return walker.forget();
}

IFindChangeService* KFCObjectSearch::CreateFindChangeService()
{
	// As SnpFindAndReplace.cpp makes it - IFindChangeService has no kDefaultIID.
	return static_cast<IFindChangeService*>(::CreateObject(kFindChangeServiceBoss, IID_IFINDCHANGSERVICE));
}

ObjectWalkerScopeOptions KFCObjectSearch::WalkerOptionsFor(const UIDRef& docRef, const UIDList* items)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	const bool selection = (items != nil && items->Length() > 0);
	if (opts == nil)
	{
		ObjectWalkerScopeOptions stock;		// the stock defaults - every switch on, all frames
		stock.SetSearchScopeType(selection ? ObjectWalkerScopeOptions::kSelection : ObjectWalkerScopeOptions::kDocument);
		stock.SetDocumentToBeSearched(docRef);
		if (selection)
			stock.SetPageItemsToBeSearched(*items);
		return stock;
	}
	const IFindChangeOptions::SearchMode mode = IFindChangeOptions::kObjectSearch;
	ObjectWalkerScopeOptions::WalkType type = ObjectWalkerScopeOptions::kAllFrames;
	switch (opts->GetObjectSearchType())
	{
		case IFindChangeOptions::kTextFrames:		type = ObjectWalkerScopeOptions::kTextFrames; break;
		case IFindChangeOptions::kGraphicFrames:	type = ObjectWalkerScopeOptions::kGraphicFrames; break;
		case IFindChangeOptions::kUnassignedFrames:	type = ObjectWalkerScopeOptions::kUnassignedFrames; break;
		default:									break;
	}
	ObjectWalkerScopeOptions options(opts->GetIncludeMasterPages(mode), opts->GetIncludeLockedLayersForFind(mode),
		opts->GetIncludeHiddenLayers(mode), opts->GetIncludeLockedStoriesForFind(mode), opts->GetIncludeFootnotes(mode),
		type, selection ? ObjectWalkerScopeOptions::kSelection : ObjectWalkerScopeOptions::kDocument);
	options.SetDocumentToBeSearched(docRef);
	if (selection)
		options.SetPageItemsToBeSearched(*items);
	return options;
}

bool KFCObjectSearch::HasFindObjectFormat()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	IDataBase* const db = (opts != nil) ? opts->GetUIDAttrDB() : nil;
	if (db == nil)
		return false;
	// NOT IsThereSomethingToFind: on the Object tab it answered no with a format set (measured, KT 2026-10-09 and the
	// plan's Task 1 M7 - fill Black set, the answer still 0).
	const AttributeBossList* const attrs = opts->GetFindAttributeBossList(db, IFindChangeOptions::kObjectSearch);
	if (attrs != nil && attrs->CountBosses() > 0)
		return true;
	return opts->GetObjectFindStyle(db) != kInvalidUID;	// (Task 1 M7: an unset style reads kInvalidUID)
}

bool KFCObjectSearch::HasChangeObjectFormat()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	IDataBase* const db = (opts != nil) ? opts->GetUIDAttrDB() : nil;
	if (db == nil)
		return false;
	// kFalse: ask, never make one (allowCreation would put an empty list in place to answer the question).
	const AttributeBossList* const attrs = opts->GetChangeAttributeBossList(db, IFindChangeOptions::kObjectSearch, kFalse);
	if (attrs != nil && attrs->CountBosses() > 0)
		return true;
	return opts->GetObjectChangeStyle(db, kFalse) != kInvalidUID;
}

void KFCObjectSearch::SelectedPageItems(UIDList& out)
{
	out.Clear();
	InterfacePtr<IKFCUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	if (ui != nil)
		(void)ui->GetSelectedPageItems(out);
}

bool KFCObjectSearch::CursorInTextOnly()
{
	UIDList items;
	SelectedPageItems(items);
	if (items.Length() > 0)
		return false;
	InterfacePtr<IKFCUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	return ui != nil && ui->HasTextSelection();
}

int32 KFCObjectSearch::ObjectSearchScope(PMString& outFellBackNote)
{
	// THE STORED VALUE (D4 - the plan's Task 1 M6): InDesign's own object walk follows Search: as stored, not what an open
	// dialog shows (it showed Selection / Text Frames while Document / All Frames were stored, and walked the latter).
	outFellBackNote.Clear();
	outFellBackNote.SetTranslatable(kFalse);
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return -1;
	const IWalkerScopeFactoryUtils::WalkScopeType stored = opts->GetFindChangeScope(IFindChangeOptions::kObjectSearch);
	switch (stored)
	{
		case IWalkerScopeFactoryUtils::kEmptyScope:			// unset - what the dialog shows as Document
		case IWalkerScopeFactoryUtils::kDocumentScope:
			return IWalkerScopeFactoryUtils::kDocumentScope;
		case IWalkerScopeFactoryUtils::kAllDocumentScope:
			return IWalkerScopeFactoryUtils::kAllDocumentScope;
		case IWalkerScopeFactoryUtils::kSelectionScope:
			// Selection: the page items selected when the run begins. With none selected the run is REFUSED
			// (ResolveObjectRunScope), not widened to the document: InDesign's own Find walks nothing there (measured - the
			// plan's Task 1 M6), and its object search fails a walk aimed at the whole document while this is stored
			// (SearchObject answered kFailure at once; with Document stored, the same walk found the first item - measured
			// 2026-10-10).
			return IWalkerScopeFactoryUtils::kSelectionScope;
		default:
		{
			// Story / To End of Story / a list of stories: text-only values the Object tab cannot walk.
			const char* const name = KFCSearchEngine::SearchScopeName(stored);
			outFellBackNote.Append(" Search: is ");
			outFellBackNote.Append(*name != '\0' ? name : "a text-only value");
			outFellBackNote.Append(" - the Object tab searches page items, so the whole document was searched.");
			return IWalkerScopeFactoryUtils::kDocumentScope;
		}
	}
}

bool KFCObjectSearch::ResolveObjectRunScope(KFCSearchEngine::RunScope& out, PMString& outRefusal)
{
	// KFCSearchEngine::ResolveRunScope's doors and sentences - only Search: is read the Object tab's way.
	out = KFCSearchEngine::RunScope();
	outRefusal.Clear();
	outRefusal.SetTranslatable(kFalse);
	const bool fromBook = KFCBookScope::IsBookScopeOn();
	PMString fellBackNote;
	const int32 scope = ObjectSearchScope(fellBackNote);
	if (scope < 0)
	{
		outRefusal.Append("Search: in Edit > Find/Change is set to something this panel cannot follow - choose another one there.");
		return false;
	}
	if (fromBook && scope != IWalkerScopeFactoryUtils::kDocumentScope)
	{
		outRefusal.Append("Book Scope is on, and Search: is ");
		outRefusal.Append(KFCSearchEngine::SearchScopeName(scope));
		outRefusal.Append(". Set Search: to Document in Edit > Find/Change, or turn Book Scope off.");
		return false;
	}
	if (!fromBook && scope == IWalkerScopeFactoryUtils::kSelectionScope)
	{
		// Selection with nothing selected: nothing to search, as InDesign's own Find has it (ObjectSearchScope). The
		// cursor in text counts as its frame selected - InDesign's Find searches that frame (CursorInTextOnly).
		UIDList items;
		SelectedPageItems(items);
		if (items.Length() == 0 && !CursorInTextOnly())
		{
			outRefusal.Append("Search: is Selection, but nothing is selected. Select page items or click in a text frame, or set Search: to Document in Edit > Find/Change.");
			return false;
		}
	}
	const KFCBookScope::TargetBook targetBook = fromBook ? KFCBookScope::GetTargetBook() : KFCBookScope::kNoTargetBook;
	if (fromBook && targetBook == KFCBookScope::kNoTargetBook)
	{
		outRefusal.Append("Book Scope is on, but no book is open.");
		return false;
	}
	if (fromBook && targetBook == KFCBookScope::kTargetBookEmpty)
	{
		outRefusal.Append("That book has no chapters.");
		return false;
	}
	if (!fromBook && KFCBookScope::ActiveDocument() == nil)
	{
		outRefusal.Append("No open document to search.");
		return false;
	}
	out.fromBook = fromBook;
	out.allDocuments = !fromBook && scope == IWalkerScopeFactoryUtils::kAllDocumentScope;
	out.selectionScope = (!fromBook && scope == IWalkerScopeFactoryUtils::kSelectionScope)
		? static_cast<int32>(IWalkerScopeFactoryUtils::kSelectionScope) : static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope);
	if (!fromBook)
		out.fellBackNote = fellBackNote;
	return true;
}

bool KFCObjectSearch::Fingerprint(const UIDRef& item, uint64& outHash, uint32& outLength)
{
	outHash = 0;
	outLength = 0;
	IDataBase* const db = item.GetDataBase();
	if (db == nil || item.GetUID() == kInvalidUID || !db->IsValidUID(item.GetUID()))
		return false;
	// The exporter is a service: its arrow on a missing one would be the crash (memory utils-boss-facade-access).
	Utils<ISnippetExport> exporter;
	if (!exporter.Exists())
		return false;
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);	// an export reads; the flag is put back all the same
	KFCMemXferBytes bytes;
	ErrorCode status = kFailure;
	{
		// takeOwnership and recycleBoss both kFalse: the bytes live on this frame (KIDMCPVerify.cpp's rule).
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
		if (stream == nil)
			return false;
		status = exporter->ExportPageitems(stream, UIDList(item));
		stream->Flush();
	}
	if (status != kSuccess || bytes.GetData() == nil || bytes.GetSize() == 0)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return false;
	}
	// ONLY UP TO THE SNIPPET'S XMP PACKET. The packet that ends a snippet carries fresh xmpMM:InstanceID / DocumentID
	// GUIDs and the export's dates: two exports of an untouched item differed there and nowhere else (measured - the
	// plan's Task 1 M2: 20 of 20 differed with the packet in; without it 40 of 40 agreed, and a move, a tint, a typed
	// character still changed it while a recompose did not).
	static const char kPacket[] = "<?xpacket begin";
	const uint32 packetLength = static_cast<uint32>(sizeof(kPacket) - 1);
	const char* const data = bytes.GetData();
	const uint32 size = bytes.GetSize();
	uint32 end = size;
	for (uint32 i = 0; i + packetLength <= size; ++i)
	{
		if (std::memcmp(data + i, kPacket, packetLength) == 0)
		{
			end = i;
			break;
		}
	}
	// FNV-1a, 64-bit - the text rows' hash family (KFCSearchEngine.cpp).
	uint64 hash = 14695981039346656037ULL;
	for (uint32 i = 0; i < end; ++i)
	{
		hash ^= static_cast<uint64>(static_cast<unsigned char>(data[i]));
		hash *= 1099511628211ULL;
	}
	outHash = (hash != 0) ? hash : 1;
	outLength = end;
	return true;
}

void KFCObjectSearch::AimSharedWalkerAtFront()
{
	IDocument* const front = KFCBookScope::ActiveDocument();
	InterfacePtr<IObjectWalker> shared(QuerySharedWalker());
	if (front == nil || shared == nil)
		return;
	shared->Initialize(WalkerOptionsFor(::GetUIDRef(front), nil));
}

void KFCObjectSearch::BuildObjectHit(const UIDRef& docRef, UID item, KFCResultModel::Hit& out)
{
	IDataBase* const db = docRef.GetDataBase();
	const UIDRef itemRef(db, item);
	out.itemUID = item;

	// THE ROW'S TEXT (the author's calls of 2026-10-10): "ID<uid>:<the item's name>" - no kind word, no space after "ID".
	// The name as the Layers panel shows it, by the panel's own recipe (LayerPanelUtils::GetDefaultPageItemElementName,
	// source/open): the user's name when one was given; else InDesign's default for the item, translated, in < >; and
	// in [ ] for an item that takes no name of the user's. Cut at kMaxItemNameChars, as a row's text is kept short
	// (ROW-16) - never through a surrogate pair.
	PMString name;
	Utils<Facade::IPageItemNameFacade> nameFacade;
	if (nameFacade.Exists())
	{
		bool usedDefaultName = false;
		name.SetString(nameFacade->GetUserAssignedPageItemName(itemRef));
		if (name.IsEmpty())
		{
			name = nameFacade->GetDefaultPageItemName(itemRef);
			usedDefaultName = true;
		}
		if (!nameFacade->IsPageItemNameUserAssignable(itemRef))
		{
			name.Translate();
			name.Insert("[");
			name.Append("]");
		}
		else if (usedDefaultName)
		{
			name.Translate();
			name.Insert("<");
			name.Append(">");
		}
	}
	name.SetTranslatable(kFalse);
	const int32 kMaxItemNameChars = 40;
	if (name.CharCount() > kMaxItemNameChars)
	{
		// Not through the middle of a surrogate pair (the doubt KFCStatusTextView's KFCSafeCut carries).
		int32 keep = kMaxItemNameChars;
		const uint32 at = name.GetWChar(keep).GetValue();
		if (at >= 0xDC00 && at <= 0xDFFF)
			--keep;
		name.Truncate(name.CharCount() - keep);
		name.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	out.matchText = "ID";
	out.matchText.AppendNumber(static_cast<int32>(item.Get()));
	out.matchText.Append(":");
	out.matchText.Append(name);
	out.matchText.SetTranslatable(kFalse);

	// WHERE IT STANDS: in a group, or in text (inline / above line, or anchored with a position of its own).
	InterfacePtr<IHierarchy> hier(itemRef, UseDefaultIID());
	const UID parent = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
	const ClassID parentClass = (parent != kInvalidUID) ? db->GetClass(parent) : kInvalidClass;
	out.isGrouped = (parentClass == kGroupItemBoss);
	if (parentClass == kInlineBoss)
	{
		InterfacePtr<IAnchoredObjectData> anchored(db, parent, UseDefaultIID());
		out.isAnchored = (anchored != nil && anchored->GetPosition() == IAnchoredObjectData::kAnchoredObject);
		out.isInline = !out.isAnchored;
	}

	// THE PAGE (D7 - the plan's Task 1 M10: grouped, inline and anchored items read their owner page; an item on the
	// pasteboard reads its SPREAD there). A spread (or nothing) means the pasteboard. Ordered in the document's page order;
	// a parent page's items and the pasteboard's after them, in the order InDesign walked them - a parent page's by a key
	// of its own, so each is numbered on its own (NumberHitsWithinPages).
	const UID page = (hier != nil) ? Utils<ILayoutUtils>()->GetOwnerPageUID(hier) : kInvalidUID;
	if (page == kInvalidUID || db->GetClass(page) != kPageBoss)
	{
		out.onPasteboard = true;
		out.pageIndex = -1;
	}
	else
	{
		InterfacePtr<IPageList> pageList(docRef, UseDefaultIID());
		if (pageList != nil)
			pageList->GetPageString(page, &out.pageString, kTrue /*bIncludeSectionName*/, kFalse /*bUseIntegerStyle*/);
		out.pageString.SetTranslatable(kFalse);
		out.isMaster = Utils<ILayoutUtils>()->IsAMaster(page, db) != kFalse;
		out.pageIndex = out.isMaster ? (-2 - static_cast<int32>(page.Get()))
			: ((pageList != nil) ? pageList->GetPageIndex(page) : -1);
	}

	// Out of the user's reach where it is (the text rows' two words, by the same code).
	out.isHidden = KFCPageItemFacts::IsFrameHidden(db, item);
	out.isLocked = KFCPageItemFacts::IsPageItemLockedForEdit(db, item) || KFCPageItemFacts::IsFrameOnLockedLayer(db, item);

	(void)Fingerprint(itemRef, out.itemPrint, out.itemPrintLength);
}

void KFCObjectSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
	int32 selectionScope, std::vector<KFCBookScope::SkippedChapter>& unopenable, Tally& out)
{
	out = Tally();
	InterfacePtr<IFindChangeService> svc(CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(QuerySharedWalker());
	if (svc == nil || shared == nil)
	{
		for (size_t t = 0; t < targets.size(); ++t)
		{
			PMString name(targets[t].shortName);
			name.SetTranslatable(kFalse);
			name.Append(": no object search");
			out.unsearchable.push_back(name);
		}
		return;
	}
	// Search: = Selection: the page items selected when the search began (O3) - read before anything is selected by it.
	// None selected but the cursor in text: InDesign aims the walk itself, at the frame the cursor stands in (WalkDoc's
	// fromSearchScope). targets holds the front document alone then (KFCSearchEngine's targets for Search: = Selection).
	UIDList selected;
	bool fromSearchScope = false;
	if (!fromBook && !allDocuments && selectionScope == static_cast<int32>(IWalkerScopeFactoryUtils::kSelectionScope))
	{
		SelectedPageItems(selected);
		fromSearchScope = (selected.Length() == 0) && CursorInTextOnly();
	}
	const UIDList* const items = (selected.Length() > 0) ? &selected : nil;
	// NOTHING SELECTED IS GIVEN BACK TOO (2026-10-10 - the author's report: "an object-style search starts with an object
	// selected; leave the selection as it was before the search"). With nothing selected the snapshot below has nothing to
	// give back, and what InDesign's walk selected stayed selected - measured in the test build's trace (OBJWALK / OBJSEL):
	// both searches began with nothing selected; the style search's last find, A3 on page 1, was still selected after the
	// snapshot, while a fill-colour search whose last find was on page 2 ended with nothing selected (obj-list-basic passed
	// by that, not by the snapshot). So the UI half is asked first, and clears the selection after the snapshot when
	// nothing was selected.
	InterfacePtr<IKFCUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	const bool nothingSelected = (ui != nil) && !ui->HasAnySelection();
	{
		const SelectionKeeper keep;		// given back as this block ends, after the last chapter is handed back (O6)
		PMString title(fromBook ? "Searching book..." : "Searching...");
		title.SetTranslatable(kFalse);
		KFCProgressBar bar(title, 0, static_cast<int32>(targets.size()), kTrue, kTrue);
		bar.DisableChildProgressBars(kTrue);
		const size_t limit = static_cast<size_t>(KFCResultModel::kKFCCollectHitLimit);
		for (size_t i = 0; i < targets.size(); ++i)
		{
			SetTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
			bar.SetPosition(static_cast<int32>(i));
			if (bar.WasCancelled(kFalse))
			{
				out.cancelled = true;
				break;
			}
			if (static_cast<size_t>(out.total) >= limit)
			{
				out.truncated = true;
				break;
			}
			if (fromBook && targets[i].docRef == UIDRef::gNull && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
				continue;
			const UIDRef docRef = targets[i].docRef;
			std::vector<KFCResultModel::Hit> hits;
			bool capped = false;
			const WalkEnd end = WalkDoc(svc, shared, docRef, items, limit - static_cast<size_t>(out.total), hits, capped,
				fromSearchScope);
			// A book's chapter goes back now, closed on the spot (the text search's reason: one chapter of ours at a time).
			if (fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
				out.unclosed.push_back(targets[i].shortName);
			if (end == kWalkBroke)
				out.brokeOff.push_back(targets[i].shortName);
			if (capped)
				out.truncated = true;
			if (!hits.empty())
			{
				KFCResultModel::OrderHitsByPage(hits);
				KFCResultModel::Chapter chapter;
				chapter.name = targets[i].shortName;
				chapter.name.SetTranslatable(kFalse);
				chapter.docRef = docRef;
				chapter.file = targets[i].file;
				chapter.hits.swap(hits);
				out.total += static_cast<int32>(chapter.hits.size());
				++out.chaptersWithHits;
				KFCResultModel::AppendChapter(std::move(chapter));
			}
			if (capped)
				break;
		}
		if (!out.cancelled && bar.WasCancelled(kFalse))
			out.cancelled = true;
		bar.SetPosition(static_cast<int32>(targets.size()));
	}
#ifdef KFC_DIAG
	KFC_DIAG_LOG("OBJSEL nothing-before=%d selected-after-restore=%d", nothingSelected ? 1 : 0,
		(ui != nil && ui->HasAnySelection()) ? 1 : 0);
#endif
	if (nothingSelected)
		ui->ClearSelection();
	AimSharedWalkerAtFront();		// O7
}

// End, KFCObjectSearch.cpp.
