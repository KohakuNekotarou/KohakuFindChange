//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A tier's Narrow Down - the conditions. See KFCNarrow.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocument.h"				// GetName - a document with no layer of the name, said by name (LayerOf)
#include "IEndnoteFacade.h"			// IsEndnoteStory - an endnote's text (ReadTextPlace)
#include "IGraphicFrameData.h"		// the test build's old climb (FrameOwner)
#include "IHierarchy.h"
#include "IPageList.h"				// PageRangeStringToUIDList - Pages...' range, read in a document
#include "ITextFrameColumn.h"		// the match's frame - a text column (FrameOwner)
#include "ITextModel.h"				// QueryStoryThread - the thread a match stands in
#include "ITextStoryThread.h"		// GetDictUID - the thread's owner
#include "ITextUtils.h"				// QuerySplineFromTextFrame - the frame the person selects (FrameOwner)
#include "ITOPFrameData.h"			// GetMainSplineItemUID - text on a path's path (FrameOwner)

// General includes:
#include "FileUtils.h"				// SysFileToPMString - a document's identity (DocKey)
#include "PersistUtils.h"			// ::GetUID
#include "TablesID.h"				// kTableModelBoss - a table's cells are its threads
#include "TextID.h"					// kFootnoteReferenceBoss, kEndnoteStoryBoss
#include "UIDList.h"
#include "Utils.h"

#include <cstdio>

// Project includes:
#include "KFCDiag.h"			// NARROWFACT - a test build's trace of the facts read
#include "KFCNarrow.h"
#include "KFCPageItemFacts.h"	// DocLayerOf / DocLayerNamed - On Active Layer's layer, Move to Active Layer's reading

void KFCNarrow::ReadTextPlace(const UIDRef& storyRef, TextIndex at, KFCResultModel::Hit& io)
{
	io.inTable = false;
	io.inFootnote = false;
	io.inEndnote = false;
	IDataBase* const db = storyRef.GetDataBase();
	if (db == nil || storyRef.GetUID() == kInvalidUID || at < 0)
		return;
	// THE OWNER OF THE THREAD THE MATCH STANDS IN: a table's cells are threads of its table model, a footnote's text a thread
	// of its reference (the iid-boss dictionary's owners of IID_ITEXTSTORYTHREADDICT). None read: plain text, as far as the
	// conditions go.
	UID dict = kInvalidUID;
	ClassID owner = kInvalidClass;
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model != nil && at < model->TotalLength())
	{
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at));
		dict = (thread != nil) ? thread->GetDictUID() : kInvalidUID;
		if (dict != kInvalidUID && db->IsValidUID(dict))
			owner = db->GetClass(dict);
	}
	io.inTable = (owner == kTableModelBoss);
	io.inFootnote = (owner == kFootnoteReferenceBoss);
	// AN ENDNOTE'S TEXT: a story of its own (the narrow-down spec's M1 - of kEndnoteStoryBoss, its threads' owner the
	// story itself), asked as the product asks it - Facade::IEndnoteFacade::IsEndnoteStory (InCopyDocUtils.cpp,
	// InCopyImportProvider.cpp; measured to agree with the class, KT's KTRedlineProbe endnote); by the class itself
	// when the facade is not there (SnpManipulateTextEndnotes' IsEndnoteStory).
	const ClassID storyClass = db->GetClass(storyRef.GetUID());
	Utils<Facade::IEndnoteFacade> endnotes;
	if (endnotes)
		io.inEndnote = (endnotes->IsEndnoteStory(storyRef) != kFalse);
	else
		io.inEndnote = (storyClass == kEndnoteStoryBoss);
	// (Test builds only) The facts the conditions were read from - the plan's M1 and M2.
	KFC_DIAG_LOG("NARROWFACT story=%u class=0x%x at=%d dict=%u dictClass=0x%x table=%d foot=%d endnote=%d",
		storyRef.GetUID().Get(), (unsigned)storyClass.Get(), (int)at, dict.Get(), (unsigned)owner.Get(),
		io.inTable ? 1 : 0, io.inFootnote ? 1 : 0, io.inEndnote ? 1 : 0);
}

ITextDataValidation::RangeError KFCNarrow::ReadPageRange(IDataBase* db, const PMString& pages, std::set<UID>& outPages)
{
	outPages.clear();
	if (db == nil || pages.IsEmpty())
		return ITextDataValidation::kInvalidValue;
	InterfacePtr<IPageList> pageList(db, db->GetRootUID(), UseDefaultIID());
	if (pageList == nil)
		return ITextDataValidation::kInvalidValue;
	PMString range(pages);		// (the call takes it by reference, not const)
	range.SetTranslatable(kFalse);
	UIDList uids(db);
	const ITextDataValidation::RangeError error = pageList->PageRangeStringToUIDList(range, &uids);
	if (error == ITextDataValidation::kNoError)
		for (int32 i = 0; i < uids.Length(); ++i)
			outPages.insert(uids[i]);
	// (Test builds only) What each document answered - the plan's M4.
	KFC_DIAG_LOG("PAGERANGE db=%p range=%s error=%d pages=%d", (void*)db, pages.GetUTF8String().c_str(), (int)error,
		(int)outPages.size());
	return error;
}

UID KFCNarrow::FrameOwner(IDataBase* db, UID matchFrame)
{
	if (db == nil || matchFrame == kInvalidUID || !db->IsValidUID(matchFrame))
		return kInvalidUID;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch frame-owner-climb - KFCDiag.h: up the hierarchy to the first item with
	// IGraphicFrameData, the old way. Text on a path then reaches its kTOPSplineItemBoss, not the path.
	if (KFC_DIAG_FAULT("frame-owner-climb"))
	{
		UID at = matchFrame;
		for (int32 depth = 0; depth < 10 && at != kInvalidUID; ++depth)
		{
			InterfacePtr<IGraphicFrameData> frame(db, at, UseDefaultIID());
			if (frame != nil)
				return at;
			InterfacePtr<IHierarchy> hier(db, at, UseDefaultIID());
			at = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
		}
		return kInvalidUID;
	}
#endif
	// THE OFFICIAL TWO STEPS FROM A TEXT COLUMN TO THE ITEM SHOWING IT (sdksamples/common/SDKLayoutHelper.cpp's
	// GetGraphicFrameRef; basicpersistinterface/BPIHelper.cpp the same). Text on a path: the column's ITOPFrameData
	// names the path (GetMainSplineItemUID). Its three bosses (kTOPSplineItemBoss -> kMultiColumnItemBoss ->
	// kTOPFrameItemBoss) hang from the path through IMainItemTOPData / ITOPSplineData, not through its hierarchy - up
	// the hierarchy stands kTOPSplineItemBoss, which carries IGraphicFrameData (the live object-model dump) and is not
	// what the person selects. Any other column: the spline of its multi-column item
	// (ITextUtils::QuerySplineFromTextFrame) - a text frame's, an inline's frame, the frame a table cell or a footnote
	// stands in.
	InterfacePtr<ITOPFrameData> onPath(db, matchFrame, UseDefaultIID());
	if (onPath != nil)
		return onPath->GetMainSplineItemUID();
	InterfacePtr<ITextFrameColumn> column(db, matchFrame, UseDefaultIID());
	if (column == nil)
		return kInvalidUID;
	InterfacePtr<IHierarchy> spline(Utils<ITextUtils>()->QuerySplineFromTextFrame(column));
	return (spline != nil) ? ::GetUID(spline) : kInvalidUID;
}

bool KFCNarrow::IsTextOnly(int32 kind)
{
	return kind == kKFCNarrowInTables || kind == kKFCNarrowInFootnotes || kind == kKFCNarrowInEndnotes
		|| kind == kKFCNarrowFrames;
}

PMString KFCNarrow::StepWords(const KFCNarrowStep& step)
{
	PMString words;
	words.SetTranslatable(kFalse);
	switch (step.kind)
	{
		case kKFCNarrowInTables:		words.Append("In Tables"); break;
		case kKFCNarrowInFootnotes:		words.Append("In Footnotes"); break;
		case kKFCNarrowInEndnotes:		words.Append("In Endnotes"); break;
		case kKFCNarrowPages:			words.Append("Pages "); words.Append(step.pages); break;
		case kKFCNarrowOnPasteboard:	words.Append("On Pasteboard"); break;
		case kKFCNarrowFrames:			words.Append("Frames of These Matches"); break;
		case kKFCNarrowOnLayer:			words.Append("On Active Layer (\""); words.Append(step.layer); words.Append("\")"); break;
		default:						words.Append("?"); break;
	}
	return words;
}

KFCNarrow::Filter::Filter(const std::vector<KFCNarrowStep>& steps)
	: fSteps(steps), fFramesAt(steps.size()), fNoFrame(0)
{
	for (size_t k = 0; k < fSteps.size(); ++k)
		if (fSteps[k].kind == kKFCNarrowFrames)
		{
			fFramesAt = k;
			break;
		}
}

bool KFCNarrow::Filter::NeedsTextPlace() const
{
	for (size_t k = 0; k < fFramesAt; ++k)
		if (fSteps[k].kind == kKFCNarrowInTables || fSteps[k].kind == kKFCNarrowInFootnotes
			|| fSteps[k].kind == kKFCNarrowInEndnotes)
			return true;
	return false;
}

bool KFCNarrow::Filter::MakesFrames() const
{
	return fFramesAt < fSteps.size();
}

bool KFCNarrow::Filter::FirstFrame(IDataBase* db, UID frame) const
{
	return fFrames.insert(std::make_pair(DocKey(db), frame)).second;
}

std::string KFCNarrow::Filter::DocKey(IDataBase* db)
{
	if (db == nil)
		return std::string();
	const IDFile* file = db->GetSysFile();
	if (file != nil)
		return "f:" + FileUtils::SysFileToPMString(*file).GetUTF8String();
	char address[32] = {};
	std::snprintf(address, sizeof(address), "p:%p", static_cast<void*>(db));
	return address;
}

PMString KFCNarrow::Filter::LayerName() const
{
	for (size_t k = 0; k < fSteps.size(); ++k)
		if (fSteps[k].kind == kKFCNarrowOnLayer)
		{
			PMString name(fSteps[k].layer);
			name.SetTranslatable(kFalse);
			return name;
		}
	PMString none;
	none.SetTranslatable(kFalse);
	return none;
}

UID KFCNarrow::Filter::LayerOf(size_t k, IDataBase* db) const
{
	const std::pair<size_t, std::string> key(k, DocKey(db));
	std::map<std::pair<size_t, std::string>, UID>::const_iterator known = fLayers.find(key);
	if (known != fLayers.end())
		return known->second;
	const UID layer = (db != nil) ? KFCPageItemFacts::DocLayerNamed(UIDRef(db, db->GetRootUID()), fSteps[k].layer) : kInvalidUID;
	fLayers[key] = layer;
	// A DOCUMENT WITH NO LAYER OF THE NAME lists nothing - said by name on the status line, as Find Objects on Active Layer said
	// it (the spec's OBJ-21: "kfc-layer-b.indd: no layer named "Images"").
	if (layer == kInvalidUID && db != nil)
	{
		InterfacePtr<IDocument> doc(db, db->GetRootUID(), UseDefaultIID());
		PMString name;
		if (doc != nil)
			doc->GetName(name);
		name.SetTranslatable(kFalse);
		if (!name.IsEmpty())
			fNoLayerDocs.push_back(name);
	}
	return layer;
}

bool KFCNarrow::Filter::KeepText(const KFCResultModel::Hit& hit, IDataBase* db) const
{
	for (size_t k = 0; k < fFramesAt; ++k)
		if (!Meets(k, hit, false, db))
			return false;
	return true;
}

bool KFCNarrow::Filter::KeepObject(const KFCResultModel::Hit& hit, IDataBase* db) const
{
	// A frame a Frames step made: the conditions after that step. An Object tab list's item: every condition.
	for (size_t k = MakesFrames() ? fFramesAt + 1 : 0; k < fSteps.size(); ++k)
		if (!Meets(k, hit, true, db))
			return false;
	return true;
}

const std::set<UID>& KFCNarrow::Filter::PagesOf(size_t k, IDataBase* db) const
{
	const std::pair<size_t, std::string> key(k, DocKey(db));
	std::map<std::pair<size_t, std::string>, std::set<UID> >::const_iterator known = fPages.find(key);
	if (known != fPages.end())
		return known->second;
	std::set<UID>& pages = fPages[key];
	(void)ReadPageRange(db, fSteps[k].pages, pages);
	return pages;
}

bool KFCNarrow::Filter::Meets(size_t k, const KFCResultModel::Hit& hit, bool objectRow, IDataBase* db) const
{
	const KFCNarrowStep& step = fSteps[k];
	switch (step.kind)
	{
		case kKFCNarrowInTables:		return !objectRow && hit.inTable;
		case kKFCNarrowInFootnotes:		return !objectRow && hit.inFootnote;
		case kKFCNarrowInEndnotes:		return !objectRow && hit.inEndnote;
		case kKFCNarrowOnPasteboard:	return objectRow ? hit.onPasteboard : hit.frameOnPasteboard;
		// THE PAGE THE ROW NAMES, in the range as the row's document reads it (N5): an overset text row by its "+" page, an
		// object row by its own; a row on a parent page or the pasteboard is in no such range.
		case kKFCNarrowPages:			return hit.pageUID != kInvalidUID && PagesOf(k, db).count(hit.pageUID) > 0;
		// THE LAYER OF THE NAME in the row's document (On Active Layer - Find Objects on Active Layer moved here, 2026-10-18): an
		// object row by its item's own layer (a group's member by its group's, an item in text by its anchor frame's - as the
		// command read it); a text row by its frame's - the frame its page is named after, the "+" frame for an overset match
		// (Hit::frameLayer), as On Pasteboard and Pages read the place a text row stands.
		case kKFCNarrowOnLayer:
		{
			const UID want = LayerOf(k, db);
			if (want == kInvalidUID)
				return false;
			return objectRow ? (KFCPageItemFacts::DocLayerOf(db, hit.itemUID) == want) : (hit.frameLayer == want);
		}
		default:						return true;	// (Frames is a turn, not a test)
	}
}

// End, KFCNarrow.cpp.
