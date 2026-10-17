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
#include "ITextModel.h"				// QueryStoryThread - the thread a match stands in
#include "ITextStoryThread.h"		// GetDictUID - the thread's owner

// General includes:
#include "TablesID.h"				// kTableModelBoss - a table's cells are its threads
#include "TextID.h"					// kFootnoteReferenceBoss, kEndnoteStoryBoss
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

bool KFCNarrow::Filter::KeepText(const KFCResultModel::Hit& hit) const
{
	for (size_t k = 0; k < fFramesAt; ++k)
		if (!Meets(fSteps[k], hit, false))
			return false;
	return true;
}

bool KFCNarrow::Filter::KeepObject(const KFCResultModel::Hit& hit) const
{
	// A frame a Frames step made: the conditions after that step. An Object tab list's item: every condition.
	for (size_t k = MakesFrames() ? fFramesAt + 1 : 0; k < fSteps.size(); ++k)
		if (!Meets(fSteps[k], hit, true))
			return false;
	return true;
}

bool KFCNarrow::Filter::Meets(const KFCNarrowStep& step, const KFCResultModel::Hit& hit, bool objectRow) const
{
	switch (step.kind)
	{
		case kKFCNarrowInTables:		return !objectRow && hit.inTable;
		case kKFCNarrowInFootnotes:		return !objectRow && hit.inFootnote;
		case kKFCNarrowInEndnotes:		return !objectRow && hit.inEndnote;
		case kKFCNarrowOnPasteboard:	return objectRow ? hit.onPasteboard : hit.frameOnPasteboard;
		default:						return true;	// (Frames is a turn, not a test; Pages - the plan's Task 5)
	}
}

// End, KFCNarrow.cpp.
