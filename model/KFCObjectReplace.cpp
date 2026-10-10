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
	kRefusedOverset,		// no spread holds it - in overset text (KFCObjectSearch::IsOnSpread)
	kRefusedLocked,			// locked now
	kRefusedEdited,			// changed since the search (its fingerprint)
	kRefusedNotReached,		// InDesign's walk never came to it - no longer a match
	kRefusedWalkFailed,		// InDesign's walk broke off with an error before it came to it - nobody looked
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
		case kRefusedOverset:		return "the object is in overset text - left as it is.";
		case kRefusedLocked:		return "the object is locked now (a locked layer, or Object > Lock) - left as it is.";
		case kRefusedEdited:		return "the object has changed since the search (moved, restyled or edited) - search again.";
		case kRefusedNotReached:	return "the object no longer matches the Find/Change settings - search again.";
		case kRefusedWalkFailed:	return "InDesign's search stopped with an error before it reached the object - search again.";
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
		case kRefusedOverset:		return "in overset text";
		case kRefusedLocked:		return "locked";
		case kRefusedEdited:		return "changed since the search";
		case kRefusedNotReached:	return "no longer matching";
		case kRefusedWalkFailed:	return "not reached - InDesign's search stopped with an error";
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
	// 3b. On a spread (2026-10-10 night): an item in overset text is not written. A row's Replace brings the view to its
	// item and leaves it selected (KFCJump::ReplaceObjectRow), and nothing of an overset one is laid out to show or
	// select (KFCObjectSearch::IsOnSpread).
	if (!KFCObjectSearch::IsOnSpread(UIDRef(db, outItem)))
		return kRefusedOverset;
	// 4. Not locked (Object > Lock, an insert lock, a locked layer).
	if (KFCPageItemFacts::IsPageItemLockedForEdit(db, outItem) || KFCPageItemFacts::IsFrameOnLockedLayer(db, outItem))
		return kRefusedLocked;
	// 5. As the search found it - or as KFC last wrote it (O12): any change its fingerprint shows refuses.
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

// Did a walk's SearchObject that answered other than kSuccess break off - rather than come to the end of the matches
// (kNotFound / kFoundCompleted)? An error left standing is a break too. KFCObjectSearch.cpp's WalkDoc reads a walk's end
// the same way, and the text Replace tells its walk's error from "not found" (its walkFailed).
bool WalkBroke(IFindChangeService::FindChangeResult result)
{
	return (result != IFindChangeService::kNotFound && result != IFindChangeService::kFoundCompleted)
		|| ErrorUtils::PMGetGlobalErrorCode() != kSuccess;
}

// A chapter of ours that a check reopened goes back when the check refuses (KFCBookScope::HandBackIfHeld - the UI half's
// landings hand back the same way). The writes are reached only through the UI half's landing, which has brought the
// chapter's window forward first - the chapter is then the user's, no longer held - and which hands back on its own way
// out whatever an exit of the write's leaves (KFCJump's HandBackChapterOnExit).
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
	bool reached = false, walkFailed = false, ok = false;
	{
		// 7. WALK TO IT: InDesign's own matching, from the document's first match to the row's item. The walk ends by
		// itself - InDesign has no more, or an item comes round again (the search's guard, spec O5).
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, nil));
		std::set<UID> seen;
		for (;;)
		{
			UIDRef found;
			const IFindChangeService::FindChangeResult result = svc->SearchObject(found, kFalse);
			if (result != IFindChangeService::kSuccess)
			{
				walkFailed = WalkBroke(result);
				break;
			}
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
		Refuse(outStatus, RowSentence(reached ? kRefusedNotWritten : (walkFailed ? kRefusedWalkFailed : kRefusedNotReached)));
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

namespace
{

// ONE DOCUMENT'S OBJECT ROWS in a Replace of rows selected together (CheckRowsNow / ReplaceRows): its rows that pass the
// checks, with their items, and what the write did to them.
struct ObjectChapterWrite
{
	int32								chapterIdx;
	UIDRef								docRef;			// reached by CheckItem (a closed chapter reopened windowless, held)
	bool								wasModified;	// its "unsaved" flag before the step
	std::vector<std::pair<int32, UID> >	todo;			// (the row, its item)
	std::set<UID>						written;
	ObjectChapterWrite() : chapterIdx(-1), wasModified(false) {}
};

// WHAT EACH DOCUMENT IS LEFT AS (the text rows' ChaptersAfter, KFCReplaceEngine.cpp): written - a chapter of ours is given
// a window, to be seen and saved; not written - a flag the checks raised is put back on a document that was clean, and a
// chapter of ours is handed back.
void LeaveObjectChapters(const std::vector<ObjectChapterWrite>& chapters)
{
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		const ObjectChapterWrite& c = chapters[k];
		if (c.docRef.GetDataBase() == nil)
			continue;
		if (!c.written.empty())
		{
			if (KFCBookScope::IsHeldDoc(c.docRef))
				(void)KFCBookScope::ShowChapterWindow(c.docRef);
			continue;
		}
		if (!c.todo.empty() && !c.wasModified && KFCBookScope::IsDocStillOpen(c.docRef))
			c.docRef.GetDataBase()->SetModified(kFalse);
		HandBackIfHeld(c.docRef);
	}
}

size_t RowCount(const KFCRowsByChapter& rows)
{
	size_t total = 0;
	for (size_t k = 0; k < rows.size(); ++k)
		total += rows[k].second.size();
	return total;
}

// The one row of rows selected together that holds one (a Ctrl+click left a chapter's list empty).
bool TheOneRow(const KFCRowsByChapter& rows, int32& outChapter, int32& outHit)
{
	for (size_t k = 0; k < rows.size(); ++k)
		if (!rows[k].second.empty())
		{
			outChapter = rows[k].first;
			outHit = rows[k].second[0];
			return true;
		}
	return false;
}

}	// anonymous namespace

bool KFCObjectReplace::CheckRowsNow(const KFCRowsByChapter& rows, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	const size_t total = RowCount(rows);
	int32 oneChapter = -1, oneHit = -1;
	if (total == 1 && TheOneRow(rows, oneChapter, oneHit))
		return CheckRowNow(oneChapter, oneHit, outStatus);
	if (!CheckSettings(outStatus))
		return false;
	// Yes at the first row that would be written; the rest are asked again by the write itself (ReplaceRows), which
	// hands back what it reached and did not write.
	int32 counts[kRefusalCount] = {};
	std::vector<UIDRef> reached;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		const int32 chapterIdx = rows[c].first;
		const std::vector<int32>& hitIdxs = rows[c].second;
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
		reached.push_back(docRef);
	}
	for (size_t k = 0; k < reached.size(); ++k)
		HandBackIfHeld(reached[k]);
	outStatus = "Replace: none of the ";
	outStatus.AppendNumber(static_cast<int32>(total));
	outStatus.Append(" selected rows can be replaced - ");
	AppendRefusals(outStatus, counts);
	outStatus.Append(".");
	outStatus.SetTranslatable(kFalse);
	return false;
}

bool KFCObjectReplace::ReplaceRows(const KFCRowsByChapter& rows, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	const size_t total = RowCount(rows);
	// ONE ROW is the row's own Replace, word for word.
	int32 oneChapter = -1, oneHit = -1;
	if (total == 1 && TheOneRow(rows, oneChapter, oneHit))
		return ReplaceRow(oneChapter, oneHit, outStatus);
	if (total == 0)
		return false;
	// Forward, as the search was - outside the sequence (KFCForwardSearchScope's contract).
	KFCForwardSearchScope forward;
	if (!CheckSettings(outStatus))
		return false;

	// O10 steps 3-5, row by row: the rows that pass are written, the rest are counted by their reason.
	int32 counts[kRefusalCount] = {};
	std::vector<ObjectChapterWrite> chapters(rows.size());
	size_t toWrite = 0, documents = 0;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		ObjectChapterWrite& chapter = chapters[c];
		chapter.chapterIdx = rows[c].first;
		const std::vector<int32>& hitIdxs = rows[c].second;
		for (size_t k = 0; k < hitIdxs.size(); ++k)
		{
			if (!RowIsReplaceable(chapter.chapterIdx, hitIdxs[k]))
			{
				++counts[GreyedRefusal(chapter.chapterIdx, hitIdxs[k])];
				continue;
			}
			UID item = kInvalidUID;
			const Refusal why = CheckItem(chapter.chapterIdx, hitIdxs[k], chapter.docRef, item);
			if (why != kRefusedNone)
			{
				++counts[why];
				continue;
			}
			chapter.todo.push_back(std::make_pair(hitIdxs[k], item));
		}
		if (!chapter.todo.empty())
		{
			toWrite += chapter.todo.size();
			++documents;
			chapter.wasModified = chapter.docRef.GetDataBase()->IsModified() != kFalse;
		}
	}
	if (toWrite == 0)
	{
		LeaveObjectChapters(chapters);
		outStatus = "Replace: none of the ";
		outStatus.AppendNumber(static_cast<int32>(total));
		outStatus.Append(" selected rows can be replaced - ");
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	InterfacePtr<IFindChangeService> svc(KFCObjectSearch::CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(KFCObjectSearch::QuerySharedWalker());
	if (svc == nil || shared == nil)
	{
		LeaveObjectChapters(chapters);
		Refuse(outStatus, "InDesign's object search could not be reached - nothing was changed.");
		return false;
	}
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (O11): one step, every item's fingerprint read before the write - of
	// every document it writes.
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
		if (!chapters[c].todo.empty())
			chapterIdxs.push_back(chapters[c].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	for (size_t c = 0; c < chapters.size(); ++c)
		for (size_t k = 0; k < chapters[c].todo.size(); ++k)
			recorder.RecordItem(chapters[c].chapterIdx, chapters[c].todo[k].second);
	// ONE STEP. One document keeps the plain sequence its rows have always had; SEVERAL documents are one abortable
	// sequence around every document's walk and none inside it - Change Checked's measured rule for a step across
	// documents (KFCReplaceEngine.cpp, ReplaceRowsNow's note).
	const bool acrossDocuments = documents > 1;
	const PMString stepName(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep));
	ICommandSequence* sequence = nil;
	IAbortableCmdSeq* acrossSequence = nil;
	if (acrossDocuments)
	{
		acrossSequence = CmdUtils::BeginAbortableCmdSeq("KFC Replace");
		if (acrossSequence != nil)
			acrossSequence->SetName(stepName);
	}
	else
	{
		sequence = CmdUtils::BeginCommandSequence("KFC Replace");
		if (sequence != nil)
			sequence->SetName(stepName);
	}
	if (sequence == nil && acrossSequence == nil)
	{
		LeaveObjectChapters(chapters);
		Refuse(outStatus, "InDesign would not start a command sequence - nothing was changed.");
		return false;
	}
	bool errorStands = false;		// InDesign left the error state raised: the step is rolled back whole
	bool searchError = false;		// ...raised by the walk's search rather than by a replace
	size_t writtenCount = 0;
	const bool walkMisses = KFC_DIAG_FAULT("object-walk-miss");	// (a test build's: no item reached - oca-walk-miss)
	for (size_t c = 0; c < chapters.size() && !errorStands; ++c)
	{
		ObjectChapterWrite& chapter = chapters[c];
		if (chapter.todo.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		std::set<UID> wanted, refusedByInDesign;
		for (size_t k = 0; k < chapter.todo.size(); ++k)
			wanted.insert(chapter.todo[k].second);
		bool walkFailed = false;		// the walk broke off before every item was met (WalkBroke)
		// 7-8 FOR EVERY ROW IN ONE WALK of its document: InDesign's own matching from the document's first match, and its
		// replace on each row's item as the walk stands on it - its own Change/Find, item after item. The walk ends when
		// every item has been met, or InDesign has no more, or an item comes round again (the search's guard, spec O5).
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(chapter.docRef, nil));
		std::set<UID> seen;
		while (!walkMisses)
		{
			UIDRef found;
			const IFindChangeService::FindChangeResult result = svc->SearchObject(found, kFalse);
			if (result != IFindChangeService::kSuccess)
			{
				walkFailed = WalkBroke(result);
				break;
			}
			const UIDRef current(shared->GetCurrentItem());
			if (current.GetDataBase() != db || !seen.insert(current.GetUID()).second)
				break;
			if (wanted.count(current.GetUID()) == 0)
				continue;
			const bool replacedIt = (svc->ReplaceObject(kFalse) == IFindChangeService::kSuccess);
			// The error state too, as one row's write asks it (ReplaceRow's step 8): the step ends by it, so with one
			// standing the whole step goes back - and nothing more is written into it.
			if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
			{
				errorStands = true;
				break;
			}
			if (replacedIt)
				chapter.written.insert(current.GetUID());
			else
				refusedByInDesign.insert(current.GetUID());
			if (chapter.written.size() + refusedByInDesign.size() == wanted.size())
				break;
		}
		// ...AND AFTER THE WALK (the header re-read 102's find, 2026-10-10): a search step that broke off with the error state
		// raised, after rows were written, ends the step by that error just the same - the whole step goes back, so the
		// rows are not marked Changed, and no mark is put over the error (MarkWrite).
		if (!errorStands && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		{
			errorStands = true;
			searchError = true;
		}
		writtenCount += chapter.written.size();
		counts[kRefusedNotWritten] += static_cast<int32>(refusedByInDesign.size());
		counts[walkFailed ? kRefusedWalkFailed : kRefusedNotReached]
			+= static_cast<int32>(wanted.size() - chapter.written.size() - refusedByInDesign.size());
	}
	const bool ok = writtenCount > 0 && !errorStands;
	if (ok)
		for (size_t c = 0; c < chapters.size(); ++c)
			if (!chapters[c].written.empty())
				KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());		// in this step, so its Undo / Redo is heard
	if (acrossDocuments)
	{
		// Ended, or taken back whole across the documents (AbortCommandSequence - the error state does not carry a
		// rollback across them); no error left standing for the application to report again.
		if (ok)
			CmdUtils::EndCommandSequence(acrossSequence);
		else
			CmdUtils::AbortCommandSequence(acrossSequence);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	else
	{
		// The plain sequence's end, rolled back when nothing was written or an error stands - KFCReplaceEngine's
		// EndPlainSequence.
		if (!ok)
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
		CmdUtils::EndCommandSequence(sequence);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	KFCObjectSearch::AimSharedWalkerAtFront();		// 11 (O7)
	if (!ok)
	{
		for (size_t c = 0; c < chapters.size(); ++c)
			chapters[c].written.clear();		// rolled back: nothing of it is in the documents
		KFCResultModel::RollBackRows();
		LeaveObjectChapters(chapters);
		if (errorStands)
		{
			Refuse(outStatus, searchError ? "InDesign's search stopped with an error - nothing was changed."
				: "InDesign's replace stopped with an error - nothing was changed.");
			return false;
		}
		outStatus = "Replace: nothing was changed - ";
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	// 10. Each written row Changed, with its item as the write left it; the message names the items (REP-22's form).
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const ObjectChapterWrite& chapter = chapters[c];
		IDataBase* const db = chapter.docRef.GetDataBase();
		for (size_t k = 0; k < chapter.todo.size(); ++k)
		{
			if (chapter.written.count(chapter.todo[k].second) == 0)
				continue;
			uint64 print = 0;
			uint32 length = 0;
			(void)KFCObjectSearch::Fingerprint(UIDRef(db, chapter.todo[k].second), print, length);
			KFCResultModel::MarkItemReplaced(chapter.chapterIdx, chapter.todo[k].first, print, length);
		}
	}
	recorder.Keep(KFCUndoFollow::kStepReplace);
	LeaveObjectChapters(chapters);
	// "Replaced 3 objects: ID:259, ID:260, ID:261." - and, when some were left, "Replaced 3 of 5 objects: ... Left as they
	// were: 1 locked, 1 changed since the search." The IDs are listed up to kNamedIDs. Objects of several documents name
	// each document before its IDs (2026-10-10 night): "Replaced 3 objects in 2 documents: a.indd ID:259, ID:260;
	// b.indd ID:12."
	const size_t kNamedIDs = 8;
	outStatus = "Replaced ";
	outStatus.AppendNumber(static_cast<int32>(writtenCount));
	if (writtenCount < total)
	{
		outStatus.Append(" of ");
		outStatus.AppendNumber(static_cast<int32>(total));
	}
	outStatus.Append(" objects");
	size_t writtenDocuments = 0;
	for (size_t c = 0; c < chapters.size(); ++c)
		if (!chapters[c].written.empty())
			++writtenDocuments;
	if (writtenDocuments > 1)
	{
		outStatus.Append(" in ");
		outStatus.AppendNumber(static_cast<int32>(writtenDocuments));
		outStatus.Append(" documents");
	}
	outStatus.Append(": ");
	size_t named = 0;
	bool firstDocument = true;
	for (size_t c = 0; c < chapters.size() && named < kNamedIDs; ++c)
	{
		const ObjectChapterWrite& chapter = chapters[c];
		if (chapter.written.empty())
			continue;
		bool first = true;
		if (writtenDocuments > 1)
		{
			if (!firstDocument)
				outStatus.Append("; ");
			PMString docName;
			int32 rowsNow = 0;
			if (KFCResultModel::GetChapterDisplay(chapter.chapterIdx, docName, rowsNow))
			{
				outStatus.Append(docName);
				outStatus.Append(" ");
			}
		}
		firstDocument = false;
		for (size_t k = 0; k < chapter.todo.size() && named < kNamedIDs; ++k)
		{
			if (chapter.written.count(chapter.todo[k].second) == 0)
				continue;
			if (!first || (writtenDocuments <= 1 && named > 0))
				outStatus.Append(", ");
			outStatus.Append("ID:");
			outStatus.AppendNumber(static_cast<int32>(chapter.todo[k].second.Get()));
			first = false;
			++named;
		}
	}
	if (writtenCount > kNamedIDs)
		outStatus.Append(", ...");
	outStatus.Append(".");
	if (writtenCount < total)
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
