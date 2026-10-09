//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Object tab's writes - see KFCObjectReplace.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommandSequence.h"
#include "IDataBase.h"
#include "IFindChangeService.h"
#include "IHierarchy.h"
#include "IObjectWalker.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "UIDList.h"

#include <set>

// Project includes:
#include "KFCBookScope.h"
#include "KFCDiag.h"
#include "KFCID.h"				// kKFCReplaceStepKey
#include "KFCLoc.h"
#include "KFCObjectReplace.h"
#include "KFCObjectSearch.h"
#include "KFCPageItemFacts.h"
#include "KFCReplaceEngine.h"	// RefuseChangedQuery, CanReplaceHit
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"	// KFCForwardSearchScope
#include "KFCUndoFollow.h"

namespace
{
const int32 kMaxWalkSteps = 100000;		// KFCObjectSearch.cpp's guard

// A refusal begins with the command's name (the text Replace's rule).
void Refuse(PMString& out, const char* why)
{
	out = "Replace: ";
	out.Append(why);
	out.SetTranslatable(kFalse);
}

// O10 steps 1-5. outDoc / outItem: the row's document (reached - a closed chapter reopened windowless) and its item.
bool CheckRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus, UIDRef& outDoc, UID& outItem)
{
	outItem = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	if (outItem == kInvalidUID || !KFCReplaceEngine::CanReplaceHit(chapterIdx, hitIdx))
	{
		Refuse(outStatus, "this row cannot be replaced (already replaced, locked, or not a match of Find/Change).");
		return false;
	}
	// 1. The settings the search ran with (a changed query clears the results - the author's rule for text, kept).
	PMString refusal;
	if (KFCReplaceEngine::RefuseChangedQuery(refusal))
	{
		outStatus = "Replace: ";
		outStatus.Append(refusal);
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	// 2. Something to change to.
	if (!KFCObjectSearch::HasChangeObjectFormat())
	{
		Refuse(outStatus, "nothing to change to - set Change Object Format or an object style in Edit > Find/Change.");
		return false;
	}
	// 3. The item, still in its document - by the chapter's file first (KFCBookScope::ReachChapterDoc).
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, outDoc, file))
	{
		Refuse(outStatus, "the document of this row could not be found.");
		return false;
	}
	if (!KFCBookScope::ReachChapterDoc(file, outDoc))
	{
		Refuse(outStatus, "the document of this row could not be opened.");
		return false;
	}
	KFCResultModel::RebindChapterDoc(chapterIdx, outDoc);
	IDataBase* const db = outDoc.GetDataBase();
	bool there = false;
	if (db != nil && db->IsValidUID(outItem))
	{
		InterfacePtr<IHierarchy> hier(db, outItem, UseDefaultIID());
		there = (hier != nil);
	}
	if (!there)
	{
		KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeMissing);
		Refuse(outStatus, "the object is no longer in the document - search again.");
		return false;
	}
	// 4. Not locked (Object > Lock, an insert lock, a locked layer).
	if (KFCPageItemFacts::IsPageItemLockedForEdit(db, outItem) || KFCPageItemFacts::IsFrameOnLockedLayer(db, outItem))
	{
		Refuse(outStatus, "the object is locked now (a locked layer, or Object > Lock) - left as it is.");
		return false;
	}
	// 5. As the search found it - or as KFC last wrote it (O12): any change the item's snippet shows refuses.
	uint64 was = 0, now = 0;
	uint32 wasLength = 0, nowLength = 0;
	if (!KFCResultModel::GetHitItemPrint(chapterIdx, hitIdx, was, wasLength)
		|| !KFCObjectSearch::Fingerprint(UIDRef(db, outItem), now, nowLength) || was != now || wasLength != nowLength)
	{
		Refuse(outStatus, "the object has changed since the search (moved, restyled or edited) - search again.");
		return false;
	}
	return true;
}

// A chapter of ours reopened to be asked goes back when nothing is written (KFCReplaceEngine's ChapterAfter rule).
void HandBackIfHeld(const UIDRef& docRef)
{
	if (docRef.GetDataBase() != nil && KFCBookScope::IsHeldDoc(docRef))
		(void)KFCBookScope::HandBackHeldDocNow(docRef);
}
}	// anonymous namespace

bool KFCObjectReplace::CheckRowNow(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	UID item = kInvalidUID;
	if (CheckRow(chapterIdx, hitIdx, outStatus, docRef, item))
		return true;
	HandBackIfHeld(docRef);
	return false;
}

bool KFCObjectReplace::ReplaceRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	// Forward, as the search was - outside the sequence (KFCForwardSearchScope's contract).
	KFCForwardSearchScope forward;
	UIDRef docRef;
	UID item = kInvalidUID;
	if (!CheckRow(chapterIdx, hitIdx, outStatus, docRef, item))
	{
		HandBackIfHeld(docRef);
		return false;
	}
	IDataBase* const db = docRef.GetDataBase();
	const bool wasModified = db->IsModified() != kFalse;
	InterfacePtr<IFindChangeService> svc(KFCObjectSearch::CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(KFCObjectSearch::QuerySharedWalker());
	if (svc == nil || shared == nil)
	{
		Refuse(outStatus, "InDesign's object search could not be reached - nothing was changed.");
		return false;
	}
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (O11): the row backup starts here, the item's fingerprint is read.
	KFCUndoFollow::StepRecorder recorder(chapterIdx);
	recorder.RecordItem(chapterIdx, item);
	ICommandSequence* const sequence = CmdUtils::BeginCommandSequence("KFC Replace");
	if (sequence == nil)
	{
		Refuse(outStatus, "InDesign would not start a command sequence - nothing was changed.");
		return false;
	}
	sequence->SetName(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep));
	bool reached = false, ok = false;
	{
		// 7. WALK TO IT: InDesign's own matching, from the document's first match to the row's item.
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, nil));
		std::set<UID> seen;
		for (int32 step = 0; step < kMaxWalkSteps; ++step)
		{
			UIDRef found;
			if (svc->SearchObject(found, kFalse) != IFindChangeService::kSuccess)
				break;
			const UIDRef current(shared->GetCurrentItem());
			if (current.GetDataBase() != db || !seen.insert(current.GetUID()).second)
				break;
			if (current.GetUID() == item)
			{
				reached = true;
				break;
			}
		}
		// (A test build's fault switch: the walk taken as never reaching the item - Review Focus 3, case oca-walk-miss.)
		if (KFC_DIAG_FAULT("object-walk-miss"))
			reached = false;
		// 8. That item alone (D1 - the plan's Task 1 M8: ReplaceObject answers kSuccess when it wrote).
		if (reached)
			ok = (svc->ReplaceObject(kFalse) == IFindChangeService::kSuccess);
		if (ok)
			KFCUndoFollow::MarkWrite(db);		// in this step, so its Undo / Redo is heard
	}
	// The plain sequence's end, rolled back on a failure - KFCReplaceEngine's EndPlainSequence.
	if (!ok)
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
	CmdUtils::EndCommandSequence(sequence);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (!ok && !wasModified)
		db->SetModified(kFalse);
	KFCObjectSearch::AimSharedWalkerAtFront();		// 11 (O7)
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		Refuse(outStatus, reached ? "InDesign's replace would not run on this object - nothing was changed."
			: "the object no longer matches the Find/Change settings - search again.");
		return false;
	}
	// 10. The row Changed, with the item as the write left it; the message names the item (REP-22's form).
	uint64 print = 0;
	uint32 length = 0;
	(void)KFCObjectSearch::Fingerprint(UIDRef(db, item), print, length);
	KFCResultModel::MarkItemReplaced(chapterIdx, hitIdx, print, length);
	recorder.Keep(KFCUndoFollow::kStepReplace);
	outStatus = "Replaced ID:";
	outStatus.AppendNumber(static_cast<int32>(item.Get()));
	outStatus.Append(".");
	outStatus.SetTranslatable(kFalse);
	return true;
}

bool KFCObjectReplace::ReplaceAllInDoc(const UIDRef& docRef, int32& outReplaced, int32& outPartially)
{
	outReplaced = 0;
	outPartially = 0;
	InterfacePtr<IFindChangeService> svc(KFCObjectSearch::CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(KFCObjectSearch::QuerySharedWalker());
	if (svc == nil || shared == nil || docRef.GetDataBase() == nil)
		return false;
	shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, nil));
	int32 found = 0, fully = 0, partially = 0;
	const IFindChangeService::FindChangeResult result = svc->ReplaceAllObject(&found, &fully, &partially, kFalse);
	// The error state is the caller's to judge by (its sequence ends by it) - this one's answer is the result's.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (result == IFindChangeService::kFailure)
		return false;
	outReplaced = fully + partially;
	outPartially = partially;
	return true;
}

// End, KFCObjectReplace.cpp.
