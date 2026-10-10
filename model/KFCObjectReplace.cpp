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
#include <utility>
#include <vector>

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

// WHY A ROW IS NOT WRITTEN - said in full for one row (RowSentence), counted for several (RefusalWords - O18).
enum Refusal
{
	kRefusedNone = 0,
	kRefusedRow,			// already replaced, locked when found, or not a match of Find/Change (CanReplaceHit) - one row's
							// sentence; rows selected together are counted by the row's own reason (GreyedRefusal)
	kRefusedReplaced,		// already replaced (rows selected together - 2026-10-10)
	kRefusedNoDoc,			// its document could not be found
	kRefusedNotOpened,		// ...or not opened
	kRefusedMissing,		// the item is gone - the row now reads Missing
	kRefusedLocked,			// locked now
	kRefusedEdited,			// changed since the search (its fingerprint)
	kRefusedNotReached,		// InDesign's walk never came to it - no longer a match
	kRefusedNotWritten,		// InDesign's replace would not run on it
	kRefusalCount
};

// One row's refusal, after "Replace: ".
const char* RowSentence(Refusal why)
{
	switch (why)
	{
		case kRefusedRow:			return "this row cannot be replaced (already replaced, locked, or not a match of Find/Change).";
		case kRefusedReplaced:		return "this row has already been replaced.";
		case kRefusedNoDoc:			return "the document of this row could not be found.";
		case kRefusedNotOpened:		return "the document of this row could not be opened.";
		case kRefusedMissing:		return "the object is no longer in the document - search again.";
		case kRefusedLocked:		return "the object is locked now (a locked layer, or Object > Lock) - left as it is.";
		case kRefusedEdited:		return "the object has changed since the search (moved, restyled or edited) - search again.";
		case kRefusedNotReached:	return "the object no longer matches the Find/Change settings - search again.";
		case kRefusedNotWritten:	return "InDesign's replace would not run on this object - nothing was changed.";
		default:					return "";
	}
}

// The same reasons, a few words each, for a count of rows (O18): "2 locked".
const char* RefusalWords(Refusal why)
{
	switch (why)
	{
		case kRefusedRow:			return "not an object row";		// (never - rows of one kind are selected together)
		case kRefusedReplaced:		return "already replaced";
		case kRefusedNoDoc:			return "document not found";
		case kRefusedNotOpened:		return "document not opened";
		case kRefusedMissing:		return "no longer in the document";
		case kRefusedLocked:		return "locked";
		case kRefusedEdited:		return "changed since the search";
		case kRefusedNotReached:	return "no longer matching";
		case kRefusedNotWritten:	return "not replaced by InDesign";
		default:					return "";
	}
}

// "1 locked, 2 changed since the search" - the reasons counted, in the order of the enum.
void AppendRefusals(PMString& out, const int32 (&counts)[kRefusalCount])
{
	bool first = true;
	for (int32 why = kRefusedRow; why < kRefusalCount; ++why)
	{
		if (counts[why] == 0)
			continue;
		if (!first)
			out.Append(", ");
		out.AppendNumber(counts[why]);
		out.Append(" ");
		out.Append(RefusalWords(static_cast<Refusal>(why)));
		first = false;
	}
}

// O10 steps 1-2, asked once for a Replace: the settings the search ran with (a changed query CLEARS the results - the
// author's rule for text, kept), and something to change to. false = refused, outStatus says why.
bool CheckSettings(PMString& outStatus)
{
	PMString refusal;
	if (KFCReplaceEngine::RefuseChangedQuery(refusal))
	{
		outStatus = "Replace: ";
		outStatus.Append(refusal);
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	if (!KFCObjectSearch::HasChangeObjectFormat())
	{
		Refuse(outStatus, "nothing to change to - set Change Object Format or an object style in Edit > Find/Change.");
		return false;
	}
	return true;
}

// O10 steps 3-5 for one row whose Replace is not greyed. outDoc / outItem: the row's document (reached - a closed chapter
// reopened windowless) and its item.
Refusal CheckItem(int32 chapterIdx, int32 hitIdx, UIDRef& outDoc, UID& outItem)
{
	outItem = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	// 3. The item, still in its document - by the chapter's file first (KFCBookScope::ReachChapterDoc).
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, outDoc, file))
		return kRefusedNoDoc;
	if (!KFCBookScope::ReachChapterDoc(file, outDoc))
		return kRefusedNotOpened;
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
		return kRefusedMissing;
	}
	// 4. Not locked (Object > Lock, an insert lock, a locked layer).
	if (KFCPageItemFacts::IsPageItemLockedForEdit(db, outItem) || KFCPageItemFacts::IsFrameOnLockedLayer(db, outItem))
		return kRefusedLocked;
	// 5. As the search found it - or as KFC last wrote it (O12): any change the item's snippet shows refuses.
	uint64 was = 0, now = 0;
	uint32 wasLength = 0, nowLength = 0;
	if (!KFCResultModel::GetHitItemPrint(chapterIdx, hitIdx, was, wasLength)
		|| !KFCObjectSearch::Fingerprint(UIDRef(db, outItem), now, nowLength) || was != now || wasLength != nowLength)
		return kRefusedEdited;
	return kRefusedNone;
}

// Is the row's Replace greyed (O10's door before step 1 - the text rows' CanReplaceHit)?
bool RowIsReplaceable(int32 chapterIdx, int32 hitIdx)
{
	return KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID
		&& KFCReplaceEngine::CanReplaceHit(chapterIdx, hitIdx);
}

// A greyed row of several selected together, counted by the reason its row shows (2026-10-10 - the author's call, as the
// text rows are: KFCReplaceEngine::WhyGreyed). An item that went missing reads as one "no longer in the document", one
// locked as "locked" - the words a row found so by the write itself is counted with.
Refusal GreyedRefusal(int32 chapterIdx, int32 hitIdx)
{
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) == kInvalidUID)
		return kRefusedRow;
	switch (KFCReplaceEngine::WhyGreyed(chapterIdx, hitIdx))
	{
		case KFCReplaceEngine::kGreyedReplaced:	return kRefusedReplaced;
		case KFCReplaceEngine::kGreyedMissing:	return kRefusedMissing;
		case KFCReplaceEngine::kGreyedLocked:	return kRefusedLocked;
		case KFCReplaceEngine::kGreyedRefused:	return kRefusedNotWritten;
		default:								return kRefusedRow;
	}
}

// O10 steps 1-5 for one row, in the order a row's Replace has always asked them.
bool CheckRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus, UIDRef& outDoc, UID& outItem)
{
	outItem = kInvalidUID;
	if (!RowIsReplaceable(chapterIdx, hitIdx))
	{
		Refuse(outStatus, RowSentence(kRefusedRow));
		return false;
	}
	if (!CheckSettings(outStatus))
		return false;
	const Refusal why = CheckItem(chapterIdx, hitIdx, outDoc, outItem);
	if (why != kRefusedNone)
	{
		Refuse(outStatus, RowSentence(why));
		return false;
	}
	return true;
}

// A chapter of ours reopened to be asked goes back when nothing is written (KFCReplaceEngine's ChapterAfter rule) -
// KFCBookScope::HandBackIfHeld, the one the UI half's landings use too (2026-10-10).
using KFCBookScope::HandBackIfHeld;
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
		// 8. That item alone (D1 - the plan's Task 1 M8: ReplaceObject answers kSuccess when it wrote). Its answer AND the
		// error state: the sequence below ends by the error state (rolled back when one stands), so a write left with an
		// error standing would be taken back by InDesign under a row marked Changed - the text Replace asks both
		// (KFCReplaceEngine's walks: the command's ErrorCode and its result).
		if (reached)
			ok = (svc->ReplaceObject(kFalse) == IFindChangeService::kSuccess) && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
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
		Refuse(outStatus, RowSentence(reached ? kRefusedNotWritten : kRefusedNotReached));
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

bool KFCObjectReplace::CheckRowsNow(int32 chapterIdx, const std::vector<int32>& hitIdxs, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (hitIdxs.size() == 1)
		return CheckRowNow(chapterIdx, hitIdxs[0], outStatus);
	if (!CheckSettings(outStatus))
		return false;
	// Yes at the first row that would be written; the rest are asked again by the write itself (ReplaceRows).
	int32 counts[kRefusalCount] = {};
	UIDRef docRef;
	for (size_t k = 0; k < hitIdxs.size(); ++k)
	{
		if (!RowIsReplaceable(chapterIdx, hitIdxs[k]))
		{
			++counts[GreyedRefusal(chapterIdx, hitIdxs[k])];
			continue;
		}
		UID item = kInvalidUID;
		const Refusal why = CheckItem(chapterIdx, hitIdxs[k], docRef, item);
		if (why == kRefusedNone)
			return true;
		++counts[why];
	}
	HandBackIfHeld(docRef);
	outStatus = "Replace: none of the ";
	outStatus.AppendNumber(static_cast<int32>(hitIdxs.size()));
	outStatus.Append(" selected rows can be replaced - ");
	AppendRefusals(outStatus, counts);
	outStatus.Append(".");
	outStatus.SetTranslatable(kFalse);
	return false;
}

bool KFCObjectReplace::ReplaceRows(int32 chapterIdx, const std::vector<int32>& hitIdxs, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	// ONE ROW is the row's own Replace, word for word.
	if (hitIdxs.size() == 1)
		return ReplaceRow(chapterIdx, hitIdxs[0], outStatus);
	if (hitIdxs.empty())
		return false;
	// Forward, as the search was - outside the sequence (KFCForwardSearchScope's contract).
	KFCForwardSearchScope forward;
	if (!CheckSettings(outStatus))
		return false;

	// O10 steps 3-5, row by row: the rows that pass are written, the rest are counted by their reason.
	int32 counts[kRefusalCount] = {};
	UIDRef docRef;
	std::vector<std::pair<int32, UID> > todo;		// (the row, its item)
	for (size_t k = 0; k < hitIdxs.size(); ++k)
	{
		if (!RowIsReplaceable(chapterIdx, hitIdxs[k]))
		{
			++counts[GreyedRefusal(chapterIdx, hitIdxs[k])];
			continue;
		}
		UID item = kInvalidUID;
		const Refusal why = CheckItem(chapterIdx, hitIdxs[k], docRef, item);
		if (why != kRefusedNone)
		{
			++counts[why];
			continue;
		}
		todo.push_back(std::make_pair(hitIdxs[k], item));
	}
	if (todo.empty())
	{
		HandBackIfHeld(docRef);
		outStatus = "Replace: none of the ";
		outStatus.AppendNumber(static_cast<int32>(hitIdxs.size()));
		outStatus.Append(" selected rows can be replaced - ");
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
		outStatus.SetTranslatable(kFalse);
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
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (O11): one step, every item's fingerprint read before the write.
	KFCUndoFollow::StepRecorder recorder(chapterIdx);
	std::set<UID> wanted;
	for (size_t k = 0; k < todo.size(); ++k)
	{
		recorder.RecordItem(chapterIdx, todo[k].second);
		wanted.insert(todo[k].second);
	}
	ICommandSequence* const sequence = CmdUtils::BeginCommandSequence("KFC Replace");
	if (sequence == nil)
	{
		Refuse(outStatus, "InDesign would not start a command sequence - nothing was changed.");
		return false;
	}
	sequence->SetName(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep));
	std::set<UID> written, refusedByInDesign;
	bool errorStands = false;		// InDesign's replace left the error state raised: the step is rolled back whole
	{
		// 7-8 FOR EVERY ROW IN ONE WALK: InDesign's own matching from the document's first match, and its replace on each
		// row's item as the walk stands on it - its own Change/Find, item after item. The walk ends when every item has
		// been met, or InDesign has no more.
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, nil));
		std::set<UID> seen;
		const bool walkMisses = KFC_DIAG_FAULT("object-walk-miss");	// (a test build's: no item reached - oca-walk-miss)
		for (int32 step = 0; step < kMaxWalkSteps && !walkMisses; ++step)
		{
			UIDRef found;
			if (svc->SearchObject(found, kFalse) != IFindChangeService::kSuccess)
				break;
			const UIDRef current(shared->GetCurrentItem());
			if (current.GetDataBase() != db || !seen.insert(current.GetUID()).second)
				break;
			if (wanted.count(current.GetUID()) == 0)
				continue;
			const bool replacedIt = (svc->ReplaceObject(kFalse) == IFindChangeService::kSuccess);
			// The error state too, as one row's write asks it (ReplaceRow's step 8): the sequence ends by it, so with one
			// standing the whole step goes back - and nothing more is written into it.
			if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
			{
				errorStands = true;
				break;
			}
			if (replacedIt)
				written.insert(current.GetUID());
			else
				refusedByInDesign.insert(current.GetUID());
			if (written.size() + refusedByInDesign.size() == wanted.size())
				break;
		}
		if (!written.empty() && !errorStands)
			KFCUndoFollow::MarkWrite(db);		// in this step, so its Undo / Redo is heard
	}
	// The plain sequence's end, rolled back when nothing was written or an error stands - KFCReplaceEngine's
	// EndPlainSequence.
	const bool ok = !written.empty() && !errorStands;
	if (!ok)
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
	CmdUtils::EndCommandSequence(sequence);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (!ok && !wasModified)
		db->SetModified(kFalse);
	KFCObjectSearch::AimSharedWalkerAtFront();		// 11 (O7)
	counts[kRefusedNotWritten] += static_cast<int32>(refusedByInDesign.size());
	counts[kRefusedNotReached] += static_cast<int32>(wanted.size() - written.size() - refusedByInDesign.size());
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		if (errorStands)
		{
			Refuse(outStatus, "InDesign's replace stopped with an error - nothing was changed.");
			return false;
		}
		outStatus = "Replace: nothing was changed - ";
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	// 10. Each written row Changed, with its item as the write left it; the message names the items (REP-22's form).
	std::vector<UID> named;
	for (size_t k = 0; k < todo.size(); ++k)
	{
		if (written.count(todo[k].second) == 0)
			continue;
		uint64 print = 0;
		uint32 length = 0;
		(void)KFCObjectSearch::Fingerprint(UIDRef(db, todo[k].second), print, length);
		KFCResultModel::MarkItemReplaced(chapterIdx, todo[k].first, print, length);
		named.push_back(todo[k].second);
	}
	recorder.Keep(KFCUndoFollow::kStepReplace);
	// "Replaced 3 objects: ID:259, ID:260, ID:261." - and, when some were left, "Replaced 3 of 5 objects: ... Left as they
	// were: 1 locked, 1 changed since the search." The IDs are listed up to kNamedIDs.
	const size_t kNamedIDs = 8;
	outStatus = "Replaced ";
	outStatus.AppendNumber(static_cast<int32>(named.size()));
	if (named.size() < hitIdxs.size())
	{
		outStatus.Append(" of ");
		outStatus.AppendNumber(static_cast<int32>(hitIdxs.size()));
	}
	outStatus.Append(" objects: ");
	for (size_t k = 0; k < named.size() && k < kNamedIDs; ++k)
	{
		if (k > 0)
			outStatus.Append(", ");
		outStatus.Append("ID:");
		outStatus.AppendNumber(static_cast<int32>(named[k].Get()));
	}
	if (named.size() > kNamedIDs)
		outStatus.Append(", ...");
	outStatus.Append(".");
	if (named.size() < hitIdxs.size())
	{
		outStatus.Append(" Left as they were: ");
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
	}
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
	// FAILED = its answer is kFailure, OR it left the error state raised - the text side's Change All asks both of its
	// command (WriteDocument). The state is cleared here either way, and a failure is the caller's to act on: it rolls
	// its whole step back (KFCChangeAll::Run, KFCQuerySequence::Run).
	const bool errorStood = (ErrorUtils::PMGetGlobalErrorCode() != kSuccess);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (result == IFindChangeService::kFailure || errorStood)
		return false;
	outReplaced = fully + partially;
	outPartially = partially;
	return true;
}

// End, KFCObjectReplace.cpp.
