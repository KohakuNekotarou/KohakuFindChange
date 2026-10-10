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
#include "IGraphicFrameData.h"		// a text frame's columns - its story (the fingerprint, O12)
#include "IHierarchy.h"
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ILayoutUtils.h"			// GetOwnerPageUID / IsAMaster
#include "IMainItemTOPData.h"		// text on a path - its story (the fingerprint)
#include "IMultiColumnTextFrame.h"	// QueryTextModel - the story a text frame holds
#include "IObjectWalker.h"
#include "IPageItemNameFacade.h"	// a row's item by the name the Layers panel shows (2026-10-10)
#include "IPageList.h"				// GetPageString / GetPageIndex
#include "IPasteboardUtils.h"		// QuerySpread - the spread an item stands on (O17)
#include "IPMPersist.h"				// SaveAll - an item's own persistent data, the fingerprint (O12)
#include "IPMStream.h"
#include "ISession.h"
#include "ISpread.h"
#include "ISpreadList.h"			// where the walk stands, for the bar inside a document (O16)
#include "ITextFrameColumn.h"		// a text frame's column - the composition, left out of the fingerprint
#include "ITextModel.h"				// GetChangeCount - a story's version (the fingerprint)
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
	~SelectionKeeper() { GiveBack(); }
	// The snapshot given back now (once - the destructor then does nothing).
	void GiveBack()
	{
		if (fUtils == nil)
			return;
		fUtils->RestoreSelectionsSnapshot();
		fUtils->Release();
		fUtils = nil;
	}
private:
	ITextWalkerSelectionUtils* fUtils;
	SelectionKeeper(const SelectionKeeper&);
	SelectionKeeper& operator=(const SelectionKeeper&);
};

// WHAT A LISTING'S WALKS COST THE USER, PUT BACK as they end - every listing's (CollectTargets, WalkOneDocument):
//  - the selection, given back (SelectionKeeper - O6);
//  - NOTHING SELECTED, given back too (2026-10-10 - the author's report: "an object-style search starts with an object
//    selected; leave the selection as it was before the search"). With nothing selected the snapshot has nothing to give
//    back, and what InDesign's walk selected stayed selected - measured in the test build's trace (OBJWALK / OBJSEL): both
//    searches began with nothing selected; the style search's last find, A3 on page 1, was still selected after the
//    snapshot, while a fill-colour search whose last find was on page 2 ended with nothing selected (obj-list-basic passed
//    by that, not by the snapshot). So the UI half is asked first, and clears the selection after the snapshot when
//    nothing was selected;
//  - the shared walker, aimed at the front document again (O7).
// Made before the walks' bar (whose events can come in while it stands) and after the page items Search: = Selection
// names are read.
class WalkAftercare
{
public:
	WalkAftercare() : fUi(GetExecutionContextSession(), UseDefaultIID()), fNothingSelected(fUi != nil && !fUi->HasAnySelection()) {}
	~WalkAftercare()
	{
		fKeep.GiveBack();
#ifdef KFC_DIAG
		KFC_DIAG_LOG("OBJSEL nothing-before=%d selected-after-restore=%d", fNothingSelected ? 1 : 0,
			(fUi != nil && fUi->HasAnySelection()) ? 1 : 0);
#endif
		if (fNothingSelected)
			fUi->ClearSelection();
		KFCObjectSearch::AimSharedWalkerAtFront();
	}
private:
	InterfacePtr<IKFCUIServices>	fUi;
	const bool						fNothingSelected;
	SelectionKeeper					fKeep;		// the snapshot - taken after the question above
	WalkAftercare(const WalkAftercare&);
	WalkAftercare& operator=(const WalkAftercare&);
};

// How one document's walk ended - which is not how many rows it found (the text search's ChapterWalkResult, cut down).
enum WalkEnd
{
	kWalkDone = 0,		// InDesign said there is no more (kNotFound), or came round to an item already given
	kWalkBroke,			// the service failed, or handed back an item of another database - the rest was not looked at
	kWalkCancelled		// the user's Cancel, heard inside the document (O16)
};

// One document's slice of the bar, in steps (O16). The bar runs 0 .. targets x this.
const int32 kDocSpan = 1000;

// THE BAR INSIDE ONE DOCUMENT (O16 - the author's call of 2026-10-10: a Cancel heard inside a document, where 300 items
// could not be stopped before). The walk does not know how many items a document holds until it has found them all, but
// it knows where it stands - the spread of the item it is on - as the text search knows the story (KFCSearchEngine.cpp,
// its slice cut by stories). So: one step per item listed, and on to a later spread's share of the slice when the walk
// gets there; a parent page's spread (not in the spread list) only crawls. Never backwards, never past the slice.
// Moved at every item, and asked at every item (WalkDoc): which call on the bar lets a click on Cancel in is not
// measured (KFCSearchEngine.cpp's KFCAdvanceProgress), so the walk makes both, as KIDMCP's working cancel does. Each item
// already costs a fingerprint (20-75 ms - the plan's Task 1 M2), so the two calls add little.
class DocProgress
{
public:
	DocProgress(KFCProgressBar& bar, const UIDRef& docRef, int32 base)
		: fBar(bar), fBase(base), fReported(base), fSpreads(docRef, UseDefaultIID()) {}

	// The walk has listed an item of the spread `spread`: move the bar.
	void Listed(const UIDRef& spread)
	{
		int32 target = fReported + 1;
		const int32 count = (fSpreads != nil) ? fSpreads->GetSpreadCount() : 0;
		const int32 index = (fSpreads != nil && spread.GetUID() != kInvalidUID) ? fSpreads->GetSpreadIndex(spread.GetUID()) : -1;
		if (count > 0 && index >= 0)
		{
			const int32 spreadStart = fBase + static_cast<int32>((static_cast<int64>(kDocSpan) * index) / count);
			if (spreadStart > target)
				target = spreadStart;
		}
		const int32 last = fBase + kDocSpan - 1;
		if (target > last)
			target = last;
		if (target != fReported)
		{
			fBar.SetPosition(target);
			fReported = target;
		}
	}

private:
	KFCProgressBar&				fBar;
	const int32					fBase;
	int32						fReported;
	InterfacePtr<ISpreadList>	fSpreads;
	DocProgress(const DocProgress&);
	DocProgress& operator=(const DocProgress&);
};

// ONE DOCUMENT'S WALK (O2 / O5): the shared walker initialised for it, InDesign's SearchObject asked "as is" until it
// stops answering kSuccess, each match read off the shared walker. Read-only: the document's "unsaved" flag is put back
// (SaveRestoreModifiedState - the text walk's guard).
// fromSearchScope (2026-10-10 - the author's call: Search: = Selection with the cursor in text searches the frame the
// cursor stands in, as Find/Change's own Find does): the walker is NOT initialised here - the first SearchObject asks
// InDesign to initialise it from Search: itself (kTrue), which takes the caret's frame (measured: the plan's Task 1 M6),
// and the steps after it go on "as is". Only for the front document, whose selection that is.
// bar / barBase: the run's bar and this document's slice of it - moved and asked at every item listed (O16, DocProgress).
WalkEnd WalkDoc(IFindChangeService* svc, IObjectWalker* shared, const UIDRef& docRef, const UIDList* items, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool fromSearchScope, KFCProgressBar& bar, int32 barBase)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || svc == nil || shared == nil)
		return kWalkBroke;
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	if (!fromSearchScope)
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, items));
	DocProgress progress(bar, docRef, barBase);
#ifdef KFC_DIAG
	// (Test builds only) Fault switch objsearch-cancel - KFCDiag.h: Cancel taken as pressed once this document's walk has
	// listed its <n>th item, heard where the walk asks the bar.
	const int diagCancelAt = KFCDiagFaultValue("objsearch-cancel", 0, 0);
#endif
	// THE WALK ENDS BY ITSELF - no count of steps of our own (spec O5; the text walks' rule as well): InDesign says there is
	// no more (kNotFound - measured), an item comes round again, or the rows reach the run's limit.
	std::set<UID> seen;
	for (int32 step = 0; ; ++step)
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
		const KFCObjectSearch::WalkStep at = KFCObjectSearch::ReadWalkStep(result);
		if (at == KFCObjectSearch::kWalkStepEnd)
			return kWalkDone;
		if (at == KFCObjectSearch::kWalkStepBroke)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// the search's own failure, cleared where it happened
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
		// CANCEL IS ASKED HERE, AT EVERY ITEM LISTED (O16). The bar first, then the question (DocProgress says why both).
		// kFalse = no global error state.
		progress.Listed(shared->GetCurrentSpread());
		bool cancel = bar.WasCancelled(kFalse) != kFalse;
#ifdef KFC_DIAG
		if (diagCancelAt > 0 && static_cast<int>(outHits.size()) == diagCancelAt)
		{
			KFC_DIAG_LOG("FAULT objsearch-cancel: Cancel pressed at item %d", diagCancelAt);
			cancel = true;
		}
#endif
		if (cancel)
			return kWalkCancelled;
	}
}

// AN ITEM'S PRINT (O12, the fingerprint): bytes folded into one 64-bit FNV-1a hash - the text rows' hash family
// (KFCSearchEngine.cpp) - and counted.
class Print
{
public:
	Print() : fHash(14695981039346656037ULL), fLength(0) {}
	void Add(const void* data, uint32 size)
	{
		const unsigned char* const bytes = static_cast<const unsigned char*>(data);
		for (uint32 i = 0; i < size; ++i)
		{
			fHash ^= static_cast<uint64>(bytes[i]);
			fHash *= 1099511628211ULL;
		}
		fLength += size;
	}
	void AddNumber(int64 n) { Add(&n, sizeof(n)); }
	void AddReal(const PMReal& r) { const double d = ::ToDouble(r); Add(&d, sizeof(d)); }
	uint64 Hash() const { return (fHash != 0) ? fHash : 1; }
	uint32 Length() const { return fLength; }

private:
	uint64	fHash;
	uint32	fLength;
};

// One object's own persistent data - the bytes the database would write for it (IPMPersist::SaveAll, clearDirty false:
// "in all other cases" than the database's own write, IPMPersist.h). A plain memory stream writes a child it owns as the
// child's UID alone (IPMStream::XferObject), so PrintTree prints the children itself. false = nothing could be written.
bool PrintObject(IDataBase* db, UID uid, Print& print)
{
	InterfacePtr<IPMPersist> persist(db, uid, UseDefaultIID());
	if (persist == nil)
		return false;
	KFCMemXferBytes bytes;
	{
		// takeOwnership and recycleBoss both kFalse: the bytes live on this frame (KIDMCPVerify.cpp's rule).
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
		if (stream == nil)
			return false;
		persist->SaveAll(stream, kFalse);
		stream->Flush();
	}
	if (bytes.GetData() != nil)
		print.Add(bytes.GetData(), bytes.GetSize());
	return true;
}

// An item and what it holds - a group's members, a graphic frame's image, a text frame's columns object - each by
// PrintObject. NOT a text frame's column frames (ITextFrameColumn - kFrameItemBoss, kTOPFrameItemBoss): they hold the
// composition - which text fell in them, the inlines composed there - and typing moved their bytes while the frame
// itself stayed as it was (measured 2026-10-10 night - the SaveAll probe's T1). The story's version stands for the text
// (PrintStory), and a reflow is no change.
bool PrintTree(IDataBase* db, UID uid, Print& print)
{
	if (!PrintObject(db, uid, print))
		return false;
	InterfacePtr<IHierarchy> hier(db, uid, UseDefaultIID());
	if (hier == nil)
		return true;
	const int32 children = hier->GetChildCount();
	for (int32 i = 0; i < children; ++i)
	{
		const UID child = hier->GetChildUID(i);
		InterfacePtr<ITextFrameColumn> column(db, child, UseDefaultIID());
		if (column == nil && !PrintTree(db, child, print))
			return false;
	}
	return true;
}

// AN INLINE OR ANCHORED ITEM'S SETTINGS (Object > Anchored Object > Options): IAnchoredObjectData on the kInlineBoss it
// hangs from, value by value. Not that boss's bytes: they carry where the anchor was composed, and typing before the
// anchor moved them while the item stayed as it was (measured 2026-10-10 night - the SaveAll probe's I1).
void PrintAnchor(IDataBase* db, UID item, Print& print)
{
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	const UID parent = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
	if (parent == kInvalidUID || db->GetClass(parent) != kInlineBoss)
		return;
	InterfacePtr<IAnchoredObjectData> anchored(db, parent, UseDefaultIID());
	if (anchored == nil)
		return;
	print.AddNumber(anchored->GetPosition());
	print.AddNumber(anchored->GetSpineRelative());
	print.AddNumber(anchored->GetAnchorTypeHorizontal());
	print.AddNumber(anchored->GetObjectHorizontal());
	print.AddNumber(anchored->GetAnchorPtHorizontal());
	print.AddNumber(anchored->GetAnchorTypeVertical());
	print.AddNumber(anchored->GetObjectVertical());
	print.AddNumber(anchored->GetAnchorPtVertical());
	const PMPoint offset = anchored->GetOffset();
	print.AddReal(offset.X());
	print.AddReal(offset.Y());
	print.AddReal(anchored->GetYOffsetAbove());
	print.AddNumber(anchored->GetPinPosition());
	print.AddNumber(anchored->GetLockPosition());
}

// THE TEXT A FRAME HOLDS - a text frame's, or a path's: its story's version (ITextModel::GetChangeCount - every change
// to the story's text, attributes, and what hangs in it, inlines and tables included - the text rows' version,
// KFCSearchEngine::ReadStoryVersion, which an Undo puts back to exactly the value it had).
void PrintStory(const IMultiColumnTextFrame* columns, Print& print)
{
	InterfacePtr<ITextModel> model((columns != nil) ? columns->QueryTextModel() : nil);
	if (model == nil)
		return;
	print.AddNumber(columns->GetTextModelUID().Get());
	print.AddNumber(model->GetChangeCount());
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

KFCObjectSearch::WalkStep KFCObjectSearch::ReadWalkStep(IFindChangeService::FindChangeResult result)
{
	if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		return kWalkStepBroke;
	if (result == IFindChangeService::kSuccess)
		return kWalkStepOn;
	if (result == IFindChangeService::kNotFound || result == IFindChangeService::kFoundCompleted)
		return kWalkStepEnd;
	return kWalkStepBroke;
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

bool KFCObjectSearch::CursorInText()
{
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
		// cursor in text counts as its frame selected - InDesign's Find searches that frame (CursorInText).
		UIDList items;
		SelectedPageItems(items);
		if (items.Length() == 0 && !CursorInText())
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
#ifdef KFC_DIAG
	const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
#endif
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);	// SaveAll reads; the flag is put back all the same
	Print print;
	if (!PrintTree(db, item.GetUID(), print))
		return false;
	PrintAnchor(db, item.GetUID(), print);
	InterfacePtr<IGraphicFrameData> frameData(item, UseDefaultIID());
	InterfacePtr<IMultiColumnTextFrame> columns((frameData != nil) ? frameData->QueryMCTextFrame() : nil);
	PrintStory(columns, print);
	InterfacePtr<IMainItemTOPData> path(item, UseDefaultIID());
	InterfacePtr<IMultiColumnTextFrame> pathColumns((path != nil) ? path->QueryTOPMCTextFrame() : nil);
	PrintStory(pathColumns, print);
	outHash = print.Hash();
	outLength = print.Length();
#ifdef KFC_DIAG
	KFC_DIAG_LOG("FPRINT item=%u print=%016llx/%u us=%.0f", static_cast<unsigned>(item.GetUID().Get()),
		static_cast<unsigned long long>(outHash), static_cast<unsigned>(outLength),
		std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
#endif
	return true;
}

bool KFCObjectSearch::IsOnSpread(const UIDRef& item)
{
	InterfacePtr<IHierarchy> hier(item, UseDefaultIID());
	if (hier == nil)
		return false;
	InterfacePtr<ISpread> spread(Utils<IPasteboardUtils>()->QuerySpread(hier));
	return spread != nil;
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
	// (ROW-16) - never through a surrogate pair: a PMString counts and cuts in code points (UnicodeSavvyString.h -
	// CharCount, and GetUTF32TextChar's "position (in code points)").
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
		name.Truncate(name.CharCount() - kMaxItemNameChars);		// Truncate(n) takes n characters off the end
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

	// NO SPREAD HOLDS IT - an inline in overset text (QuerySpread answers nil): its row reads "overset" (BuildHitLocator),
	// nothing of it is shown or selected, and its row's Replace is refused (IsOnSpread).
	out.isOverset = !IsOnSpread(itemRef);

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
		fromSearchScope = (selected.Length() == 0) && CursorInText();
	}
	const UIDList* const items = (selected.Length() > 0) ? &selected : nil;
	{
		const WalkAftercare aftercare;		// O6 / O7 as this block ends, after the last chapter is handed back
		PMString title(fromBook ? "Searching book..." : "Searching...");
		title.SetTranslatable(kFalse);
		// A document's slice is kDocSpan steps, so the bar can move inside one (O16 - DocProgress).
		const int32 barEnd = static_cast<int32>(targets.size()) * kDocSpan;
		KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
		bar.DisableChildProgressBars(kTrue);
		const size_t limit = static_cast<size_t>(KFCResultModel::kKFCCollectHitLimit);
		for (size_t i = 0; i < targets.size(); ++i)
		{
			KFCSetCountedTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
			const int32 barBase = static_cast<int32>(i) * kDocSpan;
			bar.SetPosition(barBase);
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
				fromSearchScope, bar, barBase);
			// A book's chapter goes back now, closed on the spot (the text search's reason: one chapter of ours at a time).
			if (fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
				out.unclosed.push_back(targets[i].shortName);
			// Cancelled inside this document: the run ends as one cancelled between documents does - its rows are thrown
			// away by the caller (KFCSearchEngine's "Search cancelled."), this document's with them.
			if (end == kWalkCancelled)
			{
				out.cancelled = true;
				break;
			}
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
		bar.SetPosition(barEnd);
	}
}

bool KFCObjectSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	InterfacePtr<IFindChangeService> svc(CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(QuerySharedWalker());
	if (svc == nil || shared == nil)
		return false;
	WalkEnd end = kWalkDone;
	{
		const WalkAftercare aftercare;		// O6 / O7 as this block ends - CollectTargets' rule
		PMString title("Searching...");
		title.SetTranslatable(kFalse);
		KFCProgressBar bar(title, 0, kDocSpan, kTrue, kTrue);
		bar.DisableChildProgressBars(kTrue);
		KFCSetCountedTask(bar, "Document", 0, 1, name);
		bar.SetPosition(0);
		// The whole document, from the top - Search: as the search had it is a whole document's (the caller refuses one
		// over part of a document).
		end = WalkDoc(svc, shared, docRef, nil, limit, outHits, outCapped, false, bar, 0);
		if (end == kWalkDone && bar.WasCancelled(kFalse))
			end = kWalkCancelled;
		bar.SetPosition(kDocSpan);
	}
	outCancelled = (end == kWalkCancelled);
	return end == kWalkDone;
}

// End, KFCObjectSearch.cpp.
