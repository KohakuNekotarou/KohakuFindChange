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
#include "IGraphicFrameData.h"		// text / graphic / unassigned - the Object tab's own Type words
#include "IHierarchy.h"
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ILayoutUtils.h"			// GetOwnerPageUID / IsAMaster
#include "ILinkResource.h"			// a graphic frame's file name
#include "ILinkUtils.h"				// FindLinkResource
#include "IObjectWalker.h"
#include "IPageList.h"				// GetPageString / GetPageIndex
#include "IPMStream.h"
#include "ISession.h"
#include "ISnippetExport.h"			// the fingerprint (O12)
#include "ITextModel.h"
#include "ITextUtils.h"				// QueryTextModelFromSpline
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
#include "TextID.h"					// kInlineBoss
#include "TextWalkerServiceProviderID.h"	// kObjectWalkerService, kFindChangeServiceBoss, kTextWalkerService
#include "Utils.h"

#include <cstring>					// memcmp - the fingerprint's end at the XMP packet
#include <set>
#include <utility>

// Project includes:
#include "IKFCUIServices.h"			// GetSelectedPageItems - the selection is the UI half's
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
WalkEnd WalkDoc(IFindChangeService* svc, IObjectWalker* shared, const UIDRef& docRef, const UIDList* items, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || svc == nil || shared == nil)
		return kWalkBroke;
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, items));
	std::set<UID> seen;
	for (int32 step = 0; step < kMaxWalkSteps; ++step)
	{
		UIDRef found;	// stays empty even on kSuccess (measured) - the match is the shared walker's current item
		const IFindChangeService::FindChangeResult result = svc->SearchObject(found, kFalse);
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
		// Selection with no page item selected: nothing to search, as InDesign's own Find has it (ObjectSearchScope).
		UIDList items;
		SelectedPageItems(items);
		if (items.Length() == 0)
		{
			outRefusal.Append("Search: is Selection, but no page item is selected. Select page items, or set Search: to Document in Edit > Find/Change.");
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

	// THE KIND AND WHAT IT HOLDS (O8): the Object tab's own words for its Type menu. A text frame shows its story's first
	// words, a graphic frame the file placed in it; an unassigned frame holds nothing to show.
	PMString kind("Frame"), content;
	kind.SetTranslatable(kFalse);
	content.SetTranslatable(kFalse);
	InterfacePtr<IGraphicFrameData> frame(itemRef, UseDefaultIID());
	if (frame != nil && frame->GetTextContentUID() != kInvalidUID)
	{
		kind = "Text Frame";
		InterfacePtr<ITextModel> model(Utils<ITextUtils>()->QueryTextModelFromSpline(frame));
		if (model != nil)
			content = KFCPageItemFacts::StoryLeadText(::GetUIDRef(model));
	}
	else if (frame != nil && frame->IsGraphicFrame())
	{
		kind = "Graphic Frame";
		const UID resourceUID = Utils<ILinkUtils>()->FindLinkResource(itemRef);
		InterfacePtr<ILinkResource> resource(db, resourceUID, UseDefaultIID());
		if (resource != nil)
			content = PMString(resource->GetShortName(true));
	}
	out.matchText = kind;
	out.matchText.Append("  ID ");
	out.matchText.AppendNumber(static_cast<int32>(item.Get()));
	out.matchText.SetTranslatable(kFalse);
	if (!content.IsEmpty())
	{
		out.postText = ": ";
		out.postText.Append(content);
		out.postText.SetTranslatable(kFalse);
	}

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
	UIDList selected;
	if (!fromBook && !allDocuments && selectionScope == static_cast<int32>(IWalkerScopeFactoryUtils::kSelectionScope))
		SelectedPageItems(selected);
	const UIDList* const items = (selected.Length() > 0) ? &selected : nil;
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
			const WalkEnd end = WalkDoc(svc, shared, docRef, items, limit - static_cast<size_t>(out.total), hits, capped);
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
	AimSharedWalkerAtFront();		// O7
}

// End, KFCObjectSearch.cpp.
