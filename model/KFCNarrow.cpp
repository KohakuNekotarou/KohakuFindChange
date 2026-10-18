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
#include "IGraphicFrameData.h"		// the frame the person selects - FrameOwner
#include "IHierarchy.h"
#include "IPageList.h"				// PageRangeStringToUIDList - Pages...' range, read in a document
#include "ITextModel.h"				// QueryStoryThread - the thread a match stands in
#include "ITextStoryThread.h"		// GetDictUID - the thread's owner

// General includes:
#include "FileUtils.h"				// SysFileToPMString - a document's identity (DocKey)
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
	// AN ENDNOTE'S TEXT: a story of the endnote story's class, or a thread of one - which of the two is the plan's M1, read off
	// the trace below (NARROWFACT).
	const ClassID storyClass = db->GetClass(storyRef.GetUID());
	io.inEndnote = (storyClass == kEndnoteStoryBoss || owner == kEndnoteStoryBoss);
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
	// Up from the match's own frame: a text frame's column (kFrameItemBoss) under its multi-column item under the spline the
	// person selects (SnpManipulateTextFrame.cpp calls that spline "the text frame's kSplineItemBoss"); a frame on a path
	// under the path's spline. Ten levels at most - a page item is never that deep below its frame.
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
