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
#include "IPageList.h"				// PageRangeStringToUIDList - Pages...' range, read in a document
#include "ITextModel.h"				// QueryStoryThread - the thread a match stands in
#include "ITextStoryThread.h"		// GetDictUID - the thread's owner

// General includes:
#include "TablesID.h"				// kTableModelBoss - a table's cells are its threads
#include "TextID.h"					// kFootnoteReferenceBoss, kEndnoteStoryBoss
#include "UIDList.h"
#include "Utils.h"

// Project includes:
#include "KFCDiag.h"			// NARROWFACT - a test build's trace of the facts read
#include "KFCNarrow.h"

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
		default:						words.Append("?"); break;
	}
	return words;
}

KFCNarrow::Filter::Filter(const std::vector<KFCNarrowStep>& steps)
	: fSteps(steps), fFramesAt(steps.size())
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
	const std::pair<size_t, IDataBase*> key(k, db);
	std::map<std::pair<size_t, IDataBase*>, std::set<UID> >::const_iterator known = fPages.find(key);
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
		default:						return true;	// (Frames is a turn, not a test)
	}
}

// End, KFCNarrow.cpp.
