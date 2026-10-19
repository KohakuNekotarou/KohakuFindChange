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
#include "IPageItemAdapter.h"		// the wrapper of an item placed in text or in a table - its story (WriteReach)
#include "IPageItemNameFacade.h"	// a row's item by the name the Layers panel shows
#include "IPageList.h"				// GetPageString / GetPageIndex
#include "IPasteboardUtils.h"		// QuerySpread - the spread an item stands on
#include "IPMPersist.h"				// SaveAll - an item's own persistent data, the fingerprint (O12)
#include "IPMStream.h"
#include "ISession.h"
#include "ISpread.h"
#include "ISpreadList.h"			// where the walk stands, for the bar inside a document
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
#include <string>
#include <utility>

// Project includes:
#include "IKFCUIServices.h"			// GetSelectedPageItems - the selection is the UI half's
#include "KFCDiag.h"				// KFC_DIAG_LOG - the OBJWALK / OBJSEL trace, test builds only
#include "KFCMemXferBytes.h"
#include "KFCObjectSearch.h"
#include "KFCPageItemFacts.h"
#include "KFCProgressBar.h"
#include "KFCTextCut.h"				// KFCSafeCut - where a long item name is cut

namespace
{
// THE USER'S SELECTION, GIVEN BACK (O6). The text walker service's snapshot (ITextWalkerSelectionUtils.h,
// SaveSelectionsSnapshot / RestoreSelectionsSnapshot). No worked example in the SDK: the header names
// SpellPanelObserver::Update as their caller, and the open spellpanel source has both calls commented out
// (SpellPanelObserver.cpp:338, :350). So the shape is the header's and the proof is measured - with an object search over
// a windowless document and over the front one (KT - the object search plan's Task 1 M4,
// docs/superpowers/plans/2026-10-09-kfc-object-search.md: five selection states, the selection and the active layer the
// same before and after). Never around a write: see the header.
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
//  - NOTHING SELECTED, given back too (the author's report: "an object-style search starts with an object
//    selected; leave the selection as it was before the search"). With nothing selected the snapshot has nothing to give
//    back, and what InDesign's walk selected stayed selected - measured in the test build's trace (OBJWALK / OBJSEL): both
//    searches began with nothing selected; the style search's last find, A3 on page 1, was still selected after the
//    snapshot, while a fill-colour search whose last find was on page 2 ended with nothing selected (obj-list-basic passed
//    by that, not by the snapshot). So the UI half is asked first, and clears the selection after the snapshot when
//    nothing was selected;
//  - THE WINDOWS BEHIND THE FRONT VIEW, given back too (IKFCUIServices::SnapshotBackgroundSelections - measured: the
//    walks select what they find in every document with a window, and an All Documents search with nothing selected
//    anywhere left the last item it found selected in the document behind - in a minimised window and a document's
//    second window as well, so every layout window is read; the snapshot is the front view's alone);
//  - the shared walker, aimed at the front document again (O7).
// Made before the walks' bar (whose events can come in while it stands) and after the page items Search: = Selection
// names are read.
class WalkAftercare
{
public:
	WalkAftercare() : fUi(KFCQueryUIServices()), fNothingSelected(fUi != nil && !fUi->HasAnySelection())
	{
		if (fUi != nil)
			fUi->SnapshotBackgroundSelections();
	}
	~WalkAftercare()
	{
		fKeep.GiveBack();
#ifdef KFC_DIAG
		KFC_DIAG_LOG("OBJSEL nothing-before=%d selected-after-restore=%d", fNothingSelected ? 1 : 0,
			(fUi != nil && fUi->HasAnySelection()) ? 1 : 0);
#endif
		if (fNothingSelected)
			fUi->ClearSelection();
		if (fUi != nil)
			fUi->RestoreBackgroundSelections();
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
	kWalkCancelled		// the user's Cancel, heard inside the document
};

// One document's slice of the bar, in steps. The bar runs 0 .. targets x this.
const int32 kDocSpan = 1000;

// THE BAR INSIDE ONE DOCUMENT (the author's call: a Cancel is heard inside a document, not only between documents,
// where the many items of one - up to the object lists' limit - could not be stopped). The walk does not know how many
// items a document holds until it has found them all, but it knows where it stands - the spread of the item it is on -
// as the text search knows the story (KFCSearchEngine.cpp, its slice cut by stories). So: one step per item listed, and
// on to a later spread's share of the slice when the walk gets there; a parent page's spread (not in the spread list)
// only crawls. Never backwards, never past the slice.
// Moved at every item, and asked at every item (WalkDoc): which call on the bar lets a click on Cancel in is not
// measured (KFCSearchEngine.cpp's KFCAdvanceProgress), so the walk makes both, as KIDMCP's working cancel does. Each item
// already costs a fingerprint, so the two calls were judged to add little - against the snippet print of the time (20-75
// ms, the plan's Task 1 M2); the SaveAll print that replaced it is faster, and the two calls are not timed against it (a
// test build's FPRINT line times the print).
class DocProgress
{
public:
	// (span: the document's slice - kDocSpan, or its share of the run's bar by its size: the progress-share spec)
	DocProgress(KFCProgressBar& bar, const UIDRef& docRef, int32 base, int32 span)
		: fBar(bar), fBase(base), fSpan(span > 0 ? span : 1), fReported(base), fSpreads(docRef, UseDefaultIID()) {}

	// The walk has listed an item of the spread `spread` (SpreadOf - kInvalidUID for an item on none): move the bar.
	void Listed(UID spread)
	{
		int32 target = fReported + 1;
		const int32 count = (fSpreads != nil) ? fSpreads->GetSpreadCount() : 0;
		const int32 index = (fSpreads != nil && spread != kInvalidUID) ? fSpreads->GetSpreadIndex(spread) : -1;
		if (count > 0 && index >= 0)
		{
			const int32 spreadStart = fBase + static_cast<int32>((static_cast<int64>(fSpan) * index) / count);
			if (spreadStart > target)
				target = spreadStart;
		}
		const int32 last = fBase + fSpan - 1;
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
	const int32					fSpan;
	int32						fReported;
	InterfacePtr<ISpreadList>	fSpreads;
	DocProgress(const DocProgress&);
	DocProgress& operator=(const DocProgress&);
};

// THE SPREAD AN ITEM STANDS ON, ASKED OF THE ITEM (IPasteboardUtils::QuerySpread - IsOnSpread asks it too): what the
// bar inside a document moves by (DocProgress). kInvalidUID for an item on no spread - an inline in overset text.
// NOT THE SHARED WALKER'S IObjectWalker::GetCurrentSpread - IT BROUGHT InDesign DOWN (the author's
// Find Objects on Active Layer; reproduced): a walker initialized for Search: = Selection walks the items it was given
// and puts no document in its document list, while GetCurrentSpread reads that list's first entry whatever the scope
// (TEXT WALKER.RPLN - Initialize +0xb9cd0, GetCurrentSpread +0xb9110, read in the disassembly). The list keeps the
// storage a document walk gave it, so a selection walk read a stale entry - until the session's FIRST object walk was a
// selection's, with no storage yet: a read of address 0 (+0xb9151), at the first item it listed. The header promises
// "the spread being walked" and says nothing of the scope.
UID SpreadOf(const UIDRef& item)
{
	InterfacePtr<IHierarchy> hier(item, UseDefaultIID());
	if (hier == nil)
		return kInvalidUID;
	InterfacePtr<ISpread> spread(Utils<IPasteboardUtils>()->QuerySpread(hier));
	return (spread != nil) ? ::GetUID(spread) : kInvalidUID;
}

// ONE DOCUMENT'S WALK (O2 / O5): the shared walker initialised for it, InDesign's SearchObject asked "as is" until it
// stops answering kSuccess, each match read off the shared walker. Read-only: the document's "unsaved" flag is put back
// (SaveRestoreModifiedState - the text walk's guard).
// fromSearchScope (the author's call: Search: = Selection with the cursor in text searches the frame the
// cursor stands in, as Find/Change's own Find does): the walker is NOT initialised here - the first SearchObject asks
// InDesign to initialise it from Search: itself (kTrue), which takes the caret's frame (measured: the plan's Task 1 M6),
// and the steps after it go on "as is". Only for the front document, whose selection that is.
// bar / barBase: the run's bar and this document's slice of it - moved and asked at every item listed (DocProgress).
// narrow (Narrow Down - KFCNarrow.h): a narrowed list's conditions - an item that does not meet them is passed over
// before the list's limit counts it; nil = every match. The matching stays InDesign's own: the walk is the same, and
// only what it lists is narrowed.
WalkEnd WalkDoc(IFindChangeService* svc, IObjectWalker* shared, const UIDRef& docRef, const UIDList* items, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool fromSearchScope, KFCProgressBar& bar, int32 barBase,
	const KFCNarrow::Filter* narrow, int32 barSpan = kDocSpan)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || svc == nil || shared == nil)
		return kWalkBroke;
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	if (!fromSearchScope)
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, items));
	DocProgress progress(bar, docRef, barBase, barSpan);
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
			// (Test builds only) Each step's answer and what it left selected - the trace behind WalkAftercare's NOTHING SELECTED.
			InterfacePtr<IKFCUIServices> diagUi(KFCQueryUIServices());
			const UID diagItem = shared->GetCurrentItem().GetUID();
			const unsigned diagClass = (diagItem != kInvalidUID && db->IsValidUID(diagItem))
				? static_cast<unsigned>(db->GetClass(diagItem).Get()) : 0u;
			KFC_DIAG_LOG("OBJWALK step=%d result=%d current=%u class=0x%x selected=%d", static_cast<int>(step),
				static_cast<int>(result), static_cast<unsigned>(diagItem.Get()), diagClass,
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
		// NOT A PAGE ITEM - NOT A ROW: passed over. For a table that runs into overset text InDesign's walk hands back
		// the table's own frame (kTableFrameBoss, which has no IHierarchy - measured: OBJWALK class=0xb651 for every
		// overset table, a graphic cell or not), which nobody can see, select or change; InDesign's own Find Next
		// selects nothing there. Listed, it was a row "overset [ID...:[]]" that a click took for gone (Missing).
		// NOT MEETING A NARROWED LIST'S CONDITIONS (narrow): built to be asked, then passed over. The limit counts only
		// the rows that pass.
		const bool pageItem = KFCPageItemFacts::ItemIsThere(db, current.GetUID());
		KFCResultModel::Hit hit;
		const bool built = (pageItem && narrow != nil);
		if (built)
			KFCObjectSearch::BuildRow(docRef, current.GetUID(), hit);
		if (!pageItem)
		{
			KFC_DIAG_LOG("OBJSKIP item=%u - not a page item, passed over", current.GetUID().Get());
		}
		else if (narrow != nil && !narrow->KeepObject(hit, db))
		{
			KFC_DIAG_LOG("NARROWOBJ item=%u pb=%d page=%s - passed over", current.GetUID().Get(), hit.onPasteboard ? 1 : 0,
				hit.pageString.GetUTF8String().c_str());
		}
		else
		{
			if (outHits.size() >= limit)
			{
				outCapped = true;
				return kWalkDone;
			}
			if (!built)
				KFCObjectSearch::BuildRow(docRef, current.GetUID(), hit);
			outHits.push_back(std::move(hit));
		}
		// CANCEL IS ASKED HERE, AT EVERY ITEM - one passed over too, so a document whose matches mostly fail a narrowed
		// list's conditions can still be cancelled while it is walked. The bar first, then the question (DocProgress
		// says why both). kFalse = no global error state.
		progress.Listed(SpreadOf(current));
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
		// takeOwnership and recycleBoss both kFalse: the bytes live on this frame - the stream must not delete them, and a
		// recycled stream boss may keep hold of them until it is reused (StreamUtil.h, CreateMemoryStreamWrite).
		InterfacePtr<IPMStream> stream(StreamUtil::CreateMemoryStreamWrite(&bytes, kFalse, kFalse));
		if (stream == nil)
			return false;
		persist->SaveAll(stream, kFalse);
		stream->Flush();
	}
	// A buffer that could not grow (KFCMemXferBytes::Write) holds part of the item: no fingerprint, rather than a print of
	// that part - a door then refuses the row as it refuses any item it cannot read (KFCObjectReplace's CheckItem).
	if (bytes.GetStreamState() == kStreamStateFailure)
		return false;
	if (bytes.GetData() != nil)
		print.Add(bytes.GetData(), bytes.GetSize());
	return true;
}

// An item and what it holds - a group's members, a graphic frame's image, a text frame's columns object - each by
// PrintObject. NOT a text frame's column frames (ITextFrameColumn - kFrameItemBoss, kTOPFrameItemBoss): they hold the
// composition - which text fell in them, the inlines composed there - and typing moved their bytes while the frame
// itself stayed as it was (measured - the SaveAll probe's T1). The story's version stands for the text
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
// anchor moved them while the item stayed as it was (measured - the SaveAll probe's I1).
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

// THE COLUMNS OBJECT A STORY IS SHOWN BY: a text frame's (IGraphicFrameData), and text on a path's (IMainItemTOPData) -
// an item can have both. nil when it shows none. The fingerprint prints the story of each (PrintStory); a write's reach
// is told by them (AddStoriesShown).
IMultiColumnTextFrame* QueryFrameColumns(IDataBase* db, UID uid)
{
	InterfacePtr<IGraphicFrameData> frameData(db, uid, UseDefaultIID());
	return (frameData != nil) ? frameData->QueryMCTextFrame() : nil;
}

IMultiColumnTextFrame* QueryPathColumns(IDataBase* db, UID uid)
{
	InterfacePtr<IMainItemTOPData> path(db, uid, UseDefaultIID());
	return (path != nil) ? path->QueryTOPMCTextFrame() : nil;
}

// The stories an item shows (QueryFrameColumns, QueryPathColumns), added to out.
void AddStoriesShown(IDataBase* db, UID uid, std::set<UID>& out)
{
	InterfacePtr<IMultiColumnTextFrame> columns(QueryFrameColumns(db, uid));
	if (columns != nil)
		out.insert(columns->GetTextModelUID());
	InterfacePtr<IMultiColumnTextFrame> pathColumns(QueryPathColumns(db, uid));
	if (pathColumns != nil)
		out.insert(pathColumns->GetTextModelUID());
}

// The item holding this one (IHierarchy) - kInvalidUID at the top.
UID HolderOf(IDataBase* db, UID uid)
{
	InterfacePtr<IHierarchy> hier(db, uid, UseDefaultIID());
	return (hier != nil) ? hier->GetParentUID() : kInvalidUID;
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
	// NOT IsThereSomethingToFind: on the Object tab it answered no with a format set (measured in KT - the plan's Task 1
	// M7: fill Black set, the answer still 0).
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

namespace
{
// The page items selected now - what Search: = Selection searches. Empty when none, or no UI half.
void SelectedPageItems(UIDList& out)
{
	out.Clear();
	InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
	if (ui != nil)
		(void)ui->GetSelectedPageItems(out);
}

// Is the cursor in text now - a caret or a range? With no page item selected (the callers have just read the page
// items - SelectedPageItems), Search: = Selection then searches the frame the cursor stands in, as InDesign's own Find
// does (the author's call; the plan's Task 1 M6 measured it: a caret -> that frame only). false with no UI half.
bool CursorInText()
{
	InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
	return ui != nil && ui->HasTextSelection();
}
}	// anonymous namespace

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
			// (SearchObject answered kFailure at once; with Document stored, the same walk found the first item - measured).
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
	if (!KFCSearchEngine::ResolveRunTarget(fromBook, outRefusal))
		return false;
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
	InterfacePtr<IMultiColumnTextFrame> columns(QueryFrameColumns(db, item.GetUID()));
	PrintStory(columns, print);
	InterfacePtr<IMultiColumnTextFrame> pathColumns(QueryPathColumns(db, item.GetUID()));
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

void KFCObjectSearch::ReachOfWrite(IDataBase* db, const std::set<UID>& toWrite, WriteReach& out)
{
	out = WriteReach();
	out.written = toWrite;
	if (db == nil)
		return;
	for (std::set<UID>::const_iterator w = toWrite.begin(); w != toWrite.end(); ++w)
	{
#ifdef KFC_DIAG
		std::string chain;		// the classes the walk met, for the trace
#endif
		// Up from the item, the item first. A walk that comes to an item already met stops: the rest is in.
		for (UID uid = *w; uid != kInvalidUID && db->IsValidUID(uid) && out.items.insert(uid).second; uid = HolderOf(db, uid))
		{
			AddStoriesShown(db, uid, out.stories);		// a text frame or path: its own story
			InterfacePtr<ITextFrameColumn> column(db, uid, UseDefaultIID());
			if (column != nil)
				out.stories.insert(column->GetTextModelUID());		// a frame's column: the story composed in it
			// THE WRAPPER OF AN ITEM PLACED IN TEXT OR IN A TABLE - an inline's or anchored item's anchor (kInlineBoss), a
			// graphic cell's adapter (kCellPageItemAdapterBoss), where the walk up from such an item ends (measured - cases
			// obj-f2-inline, obj-f2-cell): the story it is placed in, a table's for a cell (IPageItemAdapter.h - "always
			// available on such objects"). Asked of the wrapper alone: ITableUtils::QueryCellContentfromPlacedItem asked of an
			// inline brought InDesign down - a read of address 0 inside TABLE MODEL.RPLN (case obj-f2-inline).
			InterfacePtr<IPageItemAdapter> adapter(db, uid, UseDefaultIID());
			InterfacePtr<ITextModel> placedIn((adapter != nil) ? adapter->QueryTextModel() : nil);
			if (placedIn != nil)
				out.stories.insert(::GetUID(placedIn));
#ifdef KFC_DIAG
			char step[24];
			_snprintf_s(step, sizeof(step), _TRUNCATE, "%s0x%x", chain.empty() ? "" : ">",
				static_cast<unsigned>(db->GetClass(uid).Get()));
			chain += step;
#endif
		}
#ifdef KFC_DIAG
		KFC_DIAG_LOG("OBJREACH item=%u up=%s stories=%u", static_cast<unsigned>(w->Get()), chain.c_str(),
			static_cast<unsigned>(out.stories.size()));
#endif
	}
	out.stories.erase(kInvalidUID);
}

bool KFCObjectSearch::WriteReaches(IDataBase* db, const WriteReach& reach, UID item)
{
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return false;
	// One to be written, or one holding it: its print holds the item (PrintTree).
	if (reach.items.count(item) > 0)
		return true;
	// A text frame or path showing a story the write reaches: its print holds the story's version (PrintStory).
	if (!reach.stories.empty())
	{
		std::set<UID> shown;
		AddStoriesShown(db, item, shown);
		for (std::set<UID>::const_iterator s = shown.begin(); s != shown.end(); ++s)
			if (reach.stories.count(*s) > 0)
				return true;
	}
	// Held by one to be written - a group's member, a frame's image: a write to the holder can reach into it. (A holder met
	// twice ends the walk, as in ReachOfWrite.)
	std::set<UID> met;
	for (UID uid = HolderOf(db, item); uid != kInvalidUID && db->IsValidUID(uid) && met.insert(uid).second;
		uid = HolderOf(db, uid))
		if (reach.written.count(uid) > 0)
			return true;
	return false;
}

bool KFCObjectSearch::IsOnSpread(const UIDRef& item)
{
	return SpreadOf(item) != kInvalidUID;
}

void KFCObjectSearch::AimSharedWalkerAtFront()
{
	IDocument* const front = KFCBookScope::ActiveDocument();
	InterfacePtr<IObjectWalker> shared(QuerySharedWalker());
	if (front == nil || shared == nil)
		return;
	shared->Initialize(WalkerOptionsFor(::GetUIDRef(front), nil));
}

// ONE OBJECT ROW FOR ONE PAGE ITEM (O8): its text ("ID<uid>:<name>"), its page and where it stands - grouped, inline,
// anchored, in overset text - hidden / locked, its fingerprint. The locator is built later, with the chapter's page
// order (KFCResultModel::OrderHitsByPage).
void KFCObjectSearch::BuildRow(const UIDRef& docRef, UID item, KFCResultModel::Hit& out)
{
	IDataBase* const db = docRef.GetDataBase();
	const UIDRef itemRef(db, item);
	out.itemUID = item;

	// THE ROW'S TEXT (the author's calls): "ID<uid>:<the item's name>" - no kind word, no space after "ID".
	// The name as the Layers panel shows it, by the panel's own recipe (LayerPanelUtils::GetDefaultPageItemElementName,
	// source/open): the user's name when one was given; else InDesign's default for the item, translated, in < >; and
	// in [ ] for an item that takes no name of the user's. Cut at kMaxItemNameChars, as a row's text is kept short
	// (ROW-16) - never through a surrogate pair: a PMString counts and cuts in code points (UnicodeSavvyString.h -
	// CharCount, and GetUTF32TextChar's "position (in code points)"); and never in front of a mark that belongs to
	// the character before it (KFCSafeCut, KFCTextCut.h) - a text frame's default name is its first words.
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
		name.Remove(KFCSafeCut(name, kMaxItemNameChars), kMaxInt32);	// kMaxInt32: everything from the cut on
		name.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	out.matchText = "ID";
	out.matchText.AppendNumber(static_cast<int32>(item.Get()));
	out.matchText.Append(":");
	out.matchText.Append(name);
	out.matchText.SetTranslatable(kFalse);

	// WHERE IT STANDS - its page, its words (ReadItemPlace: the one reading of it, which a special operation of KFC's own
	// reads again after its write - KFCObjectSpecial).
	(void)KFCObjectSearch::ReadItemPlace(docRef, item, out);

	(void)KFCObjectSearch::Fingerprint(itemRef, out.itemPrint, out.itemPrintLength);
}

bool KFCObjectSearch::ReadItemPlace(const UIDRef& docRef, UID item, KFCResultModel::Hit& io)
{
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return false;
	const UIDRef itemRef(db, item);

	// WHERE IT STANDS: in a group, or in text (inline / above line, or anchored with a position of its own).
	InterfacePtr<IHierarchy> hier(itemRef, UseDefaultIID());
	const UID parent = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
	const ClassID parentClass = (parent != kInvalidUID) ? db->GetClass(parent) : kInvalidClass;
	io.isGrouped = (parentClass == kGroupItemBoss);
	io.isAnchored = false;
	io.isInline = false;
	if (parentClass == kInlineBoss)
	{
		InterfacePtr<IAnchoredObjectData> anchored(db, parent, UseDefaultIID());
		io.isAnchored = (anchored != nil && anchored->GetPosition() == IAnchoredObjectData::kAnchoredObject);
		io.isInline = !io.isAnchored;
	}

	// THE PAGE (D7 - the plan's Task 1 M10: grouped, inline and anchored items read their owner page; an item on the
	// pasteboard reads its SPREAD there). A spread (or nothing) means the pasteboard. Ordered in the document's page order;
	// a parent page's items and the pasteboard's after them, in the order InDesign walked them - a parent page's by a key
	// of its own, so each is numbered on its own (NumberHitsWithinPages).
	const UID page = (hier != nil) ? Utils<ILayoutUtils>()->GetOwnerPageUID(hier) : kInvalidUID;
	const bool onPage = (page != kInvalidUID && db->GetClass(page) == kPageBoss);
	io.pageString.Clear();
	io.pageString.SetTranslatable(kFalse);
	io.isMaster = false;
	io.pageUID = onPage ? page : kInvalidUID;	// (Narrow Down's Pages)
	if (!onPage)
	{
		io.onPasteboard = true;
		io.pageIndex = -1;
	}
	else
	{
		io.onPasteboard = false;
		InterfacePtr<IPageList> pageList(docRef, UseDefaultIID());
		if (pageList != nil)
			pageList->GetPageString(page, &io.pageString, kTrue /*bIncludeSectionName*/, kFalse /*bUseIntegerStyle*/);
		io.pageString.SetTranslatable(kFalse);
		io.isMaster = Utils<ILayoutUtils>()->IsAMaster(page, db) != kFalse;
		io.pageIndex = io.isMaster ? (-2 - static_cast<int32>(page.Get()))
			: ((pageList != nil) ? pageList->GetPageIndex(page) : -1);
	}

	// NO SPREAD HOLDS IT - an inline in overset text (QuerySpread answers nil): its row reads "overset" (BuildHitLocator),
	// nothing of it is shown or selected, and its row's Replace is refused (IsOnSpread).
	io.isOverset = !IsOnSpread(itemRef);

	// Out of the user's reach where it is (the text rows' two words, by the same code).
	io.isHidden = KFCPageItemFacts::IsFrameHidden(db, item);
	io.isLocked = KFCPageItemFacts::IsPageItemLockedForEdit(db, item) || KFCPageItemFacts::IsFrameOnLockedLayer(db, item);
	return true;
}

void KFCObjectSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
	int32 selectionScope, std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out,
	int32 reserved, const KFCNarrow::Filter* narrow)
{
	out = KFCSearchEngine::CollectTally();
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
		// A document's slice is kDocSpan steps on average, so the bar can move inside one (DocProgress) - dealt out among the
		// documents by their sizes (the progress-share spec - KFCBookScope::ProgressBases: a book's chapter by the size its book
		// records, an open document by its pages).
		const int32 barEnd = static_cast<int32>(targets.size()) * kDocSpan;
		const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
		KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
		bar.DisableChildProgressBars(kTrue);
		int32 objectLimit = KFCResultModel::kKFCCollectObjectLimit;	// the object lists' own (KFCResultModel.h)
#ifdef KFC_DIAG
		// (Test builds only) Fault value object-limit (%TEMP%\kbs-diag-fault-object-limit holding a number): the list's
		// limit for this walk set to it - how the object lists' limit was measured before it was chosen (300 / 500 /
		// 1000 objects: the search and the special operations timed - KFCResultModel.h's kKFCCollectObjectLimit says
		// what came of it).
		const int diagLimit = KFCDiagFaultValue("object-limit", 0, 0);
		if (diagLimit > 0)
			objectLimit = diagLimit;
#endif
		const int32 room = objectLimit - reserved;	// the row eye's kept rows keep theirs
		const size_t limit = static_cast<size_t>(room > 0 ? room : 0);
		for (size_t i = 0; i < targets.size(); ++i)
		{
			KFCSetCountedTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
			const int32 barBase = shares[i];
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
				fromSearchScope, bar, barBase, narrow, shares[i + 1] - barBase);
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
				KFCSearchEngine::FileChapter(targets[i], hits, out);		// the text tabs' filing: page order, the tally
			if (capped)
				break;
		}
		if (!out.cancelled && bar.WasCancelled(kFalse))
			out.cancelled = true;
		bar.SetPosition(barEnd);
	}
}

bool KFCObjectSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled, const KFCNarrow::Filter* narrow)
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
		end = WalkDoc(svc, shared, docRef, nil, limit, outHits, outCapped, false, bar, 0, narrow);
		if (end == kWalkDone && bar.WasCancelled(kFalse))
			end = kWalkCancelled;
		bar.SetPosition(kDocSpan);
	}
	outCancelled = (end == kWalkCancelled);
	return end == kWalkDone;
}

// End, KFCObjectSearch.cpp.
