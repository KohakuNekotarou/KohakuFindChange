//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Track Changes parts for the replace - see KBSTrackChange.h. The command shape of the story's
//  tracking switch is KCM's (KCMStoryTrackingOn), and the record walk is the one the 2026-09-26 spike
//  measured (13fe01e).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"
#include "ICommand.h"
#include "IInt64Data.h"			// kKBSSignRecordsCmdBoss's time stamp
#include "IIntData.h"				// kReplaceDeleteChangeDataCmdBoss's position
#include "IRangeData.h"			// kKBSSignRecordsCmdBoss's range
#include "IRedlineChangeData.h"		// kReplaceDeleteChangeDataCmdBoss's record
#include "IRedlineDataStrand.h"
#include "ITrackChangeUtils.h"		// PrimaryIndexToDeletedText - where a deletion's text lives
#include "ISession.h"
#include "IStoryList.h"			// Accept All Changes in This Document - every text model of it
#include "ITextModel.h"
#include "ITrackChangesSettings.h"	// ITrackChangeStorySettings - on kTextStoryBoss
#include "IUserInfo.h"				// the current user - whose own pending insertion a replace rewrites
#include "IWorkspace.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "GlobalTime.h"				// the run's time
#include "InCopySharedID.h"			// kRedlineStrandBoss, kSetRedlineTrackingCmdBoss, kReplaceDeleteChangeDataCmdBoss
#include "PersistUtils.h"			// ::GetUIDRef
#include "ITextStoryThread.h"
#include "TextID.h"					// kFootnoteReferenceBoss
#include "UIDList.h"
#include "VOSRedline.h"
#include "redlineiterator.h"
#include "textiterator.h"
#include "WideString.h"

// Project includes:
#include "KBSBookScope.h"			// IsDocStillOpen - a closed chapter is not read
#include "KBSID.h"					// kKBSSignRecordsCmdBoss
#include "KBSResultModel.h"
#include "KBSSearchEngine.h"		// the line a row shows, and its hash
#include "KBSTrackChange.h"

namespace
{
ErrorCode SetTracking(const UIDRef& story, bool16 on)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetRedlineTrackingCmdBoss));
	InterfacePtr<IBoolData> data(cmd, IID_IBOOLDATA);
	if (cmd == nil || data == nil)
		return kFailure;
	data->Set(on);
	cmd->SetItemList(UIDList(story));
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}

IRedlineDataStrand* QueryRedline(const UIDRef& story)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return nil;
	return static_cast<IRedlineDataStrand*>(model->QueryStrand(kRedlineStrandBoss, IRedlineDataStrand::kDefaultIID));
}

}	// anonymous namespace

bool KBSTrackChange::IsInFootnote(const UIDRef& story, TextIndex at)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextStoryThread> thread(model != nil ? model->QueryStoryThread(at, nil, nil) : nil);
	return thread != nil && ::GetClass(thread) == kFootnoteReferenceBoss;
}

PMString KBSTrackChange::ReadText(const UIDRef& story, TextIndex at, int32 len)
{
	WideString w;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model != nil && len > 0)
	{
		TextIterator it(model, at);
		for (int32 k = 0; k < len && !it.IsNull(); ++k, ++it)
			w.Append((*it).GetValue());
	}
	PMString s(w);
	s.SetTranslatable(kFalse);
	return s;
}

KBSTrackChange::TrackingScope::TrackingScope(IDataBase* db, const std::set<UID>& stories) : fDB(db), fOk(true)
{
	for (std::set<UID>::const_iterator st = stories.begin(); st != stories.end(); ++st)
	{
		const UIDRef ref(db, *st);
		InterfacePtr<ITrackChangeStorySettings> settings(ref, UseDefaultIID());
		if (settings == nil)
		{
			fOk = false;
			continue;
		}
		if (settings->GetIsTracking())
			continue;		// the user's own setting: on before, on after
		if (SetTracking(ref, kTrue) == kSuccess)
			fSwitchedOn.push_back(*st);
		else
			fOk = false;
	}
}

KBSTrackChange::TrackingScope::~TrackingScope()
{
	for (size_t i = 0; i < fSwitchedOn.size(); ++i)
		SetTracking(UIDRef(fDB, fSwitchedOn[i]), kFalse);
}

void KBSTrackChange::CollectRecords(const UIDRef& story, std::vector<Record>& out)
{
	out.clear();
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil || !redline->StoryHasChanges())
		return;
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return;
	for (bool16 more = kTrue; more; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		const uint64 time = record->GetTimeStamp();
		delete record;		// the caller owns it (redlineiterator.h:137-138)
		Record r;
		r.at = at;
		r.len = len;
		r.isDelete = isDelete;
		r.time = time;
		out.push_back(r);
	}
	delete it;
}

bool KBSTrackChange::StoryHasChanges(const UIDRef& story)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	return redline != nil && redline->StoryHasChanges();
}

bool KBSTrackChange::DocumentHasChanges(IDataBase* db)
{
	InterfacePtr<IStoryList> storyList(db, db != nil ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (storyList == nil)
		return false;
	// Every text model, not only the user-accessible ones: a record counts wherever it stands.
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
		if (StoryHasChanges(storyList->GetNthTextModelUID(i)))
			return true;
	return false;
}

// One story: accept its records one whole record at a time, the walk started over after each (an
// accept moves what comes after it, and the iterator it was made from is spent). -1 = one would not go.
static int32 AcceptAllInStory(const UIDRef& story, PMString& outWhy)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	std::vector<KBSTrackChange::Record> records;
	KBSTrackChange::CollectRecords(story, records);
	int32 done = 0;
	// Bounded by the records there were: an accept that leaves its record in place must not spin.
	for (size_t guard = 0; guard <= records.size(); ++guard)
	{
		if (!redline->StoryHasChanges())
			break;
		RedlineIterator* it = redline->NewRedlineIterator(0);
		if (it == nil)
			break;
		bool found = false;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			const VOSRedlineChange* record = it->GetCurrentChangeRecord();
			if (record == nil)
				continue;
			delete record;
			found = true;
			break;
		}
		const bool ok = found && it->ProcessAccept(nil, kFalse, kFalse);
		delete it;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		if (!found)
			break;
		if (!ok)
		{
			outWhy = "InDesign would not accept one of the changes";
			return -1;
		}
		++done;
	}
	if (KBSTrackChange::StoryHasChanges(story))
	{
		outWhy = "a change was still there after accepting";
		return -1;
	}
	return done;
}

bool KBSTrackChange::IsInsideOwnPendingInsertion(const UIDRef& story, TextIndex from, TextIndex to,
	const std::set<uint64>* onlyTimes)
{
	InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
	InterfacePtr<IUserInfo> info(ws, UseDefaultIID());
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (info == nil || redline == nil || !redline->StoryHasChanges())
		return false;
	PMString me(info->GetUserName());
	me.SetTranslatable(kFalse);
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return false;
	bool inside = false;
	for (bool16 more = kTrue; more && !inside; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool isInsert = (record->GetChangeType() != VOSRedlineChange::kDelete);
		const uint64 time = record->GetTimeStamp();
		PMString who(record->GetUserName());
		who.SetTranslatable(kFalse);
		delete record;
		if (!isInsert || len <= 0 || who != me || (onlyTimes != nil && onlyTimes->count(time) == 0))
			continue;
		// overlapping [from, to) or touching it at either end: typing extends an insertion of the same
		// author, so new text written right next to one may join it (not measured - kept out alike)
		if (from <= at + len && at <= to)
			inside = true;
	}
	delete it;
	return inside;
}

int32 KBSTrackChange::AcceptPendingAround(const UIDRef& story, TextIndex from, TextIndex to, PMString& outWhy)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	std::vector<Record> records;
	CollectRecords(story, records);
	int32 done = 0;
	// The walk starts over after each accept (the iterator it came from is spent), bounded by the
	// records there were, as AcceptAllInStory.
	for (size_t guard = 0; guard <= records.size(); ++guard)
	{
		if (!redline->StoryHasChanges())
			break;
		RedlineIterator* it = redline->NewRedlineIterator(0);
		if (it == nil)
			break;
		bool found = false;
		for (bool16 more = kTrue; more && !found; more = found ? kFalse : it->Increment(kFalse))
		{
			TextIndex at = 0;
			int32 len = 0;
			const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
			if (record == nil)
				continue;
			const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
			delete record;
			if (isDelete ? (from <= at && at <= to) : (len > 0 && from <= at + len && at <= to))
				found = true;
		}
		const bool ok = found && it->ProcessAccept(nil, kFalse, kFalse);
		delete it;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		if (!found)
			break;
		if (!ok)
		{
			outWhy = "InDesign would not accept a pending change next to a match being replaced";
			return -1;
		}
		++done;
	}
	return done;
}

int32 KBSTrackChange::AcceptAllInDocument(IDataBase* db, PMString& outWhy)
{
	InterfacePtr<IStoryList> storyList(db, db != nil ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (storyList == nil)
	{
		outWhy = "the document's stories could not be read";
		return -1;
	}
	int32 total = 0;
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
	{
		const int32 n = AcceptAllInStory(storyList->GetNthTextModelUID(i), outWhy);
		if (n < 0)
			return -1;
		total += n;
	}
	return total;
}

int32 KBSTrackChange::RejectAt(const UIDRef& story, TextIndex position, const std::set<uint64>* keepTimes, bool wantDelete)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	int32 done = 0;
	for (int32 guard = 0; guard < 1; ++guard)	// ONE record - see the header
	{
		RedlineIterator* it = redline->NewRedlineIterator(position);
		if (it == nil)
			break;
		bool found = false;
		for (bool16 more = kTrue; more && it->GetCurrentPosition() <= position; more = it->Increment(kFalse))
		{
			if (it->GetCurrentPosition() != position)
				continue;
			const VOSRedlineChange* record = it->GetCurrentChangeRecord();
			if (record == nil)
				continue;
			const uint64 time = record->GetTimeStamp();
			const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
			delete record;
			if (isDelete == wantDelete && (keepTimes == nil || keepTimes->count(time) == 0))
			{
				found = true;
				break;
			}
		}
		const bool ok = found && it->ProcessReject(nil, kFalse, kFalse);
		delete it;
		if (!ok)
			break;
		++done;
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return done;
}

bool KBSTrackChange::RejectReplacement(const UIDRef& story, TextIndex insAt, int32 insLen,
	TextIndex delAnchor, int32 delOffset, int32 delLen, const std::set<uint64>* oldTimes, uint64 onlyTime,
	PMString& outWhy)
{
	// ***** WHOLE RECORDS, THE RUN'S ONLY (2026-09-26, measured). ***** A range reject was tried and: an
	// insertion's exact range took nothing back, and an insertion range whose start held the touching
	// neighbour's deletion brought InDesign down (ShuksanTerminate; rangelog-2026-09-26.txt). So every
	// record is rejected whole by RejectAt, picked by position and time - no change of another run, and
	// none made before the run (the user's), can be taken, and no range is handed to InDesign at all.
	// (Picked by author too until 2026-09-27, when records stopped carrying a name of KBS's own.) A
	// deletion SHARED with a touching row (replaces that wrote nothing) cannot be split this way: that
	// shape is refused before the write.
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	if (oldTimes == nil && onlyTime == 0)
	{
		outWhy = "whose change it is was not told";		// any record there would qualify
		return false;
	}
	std::vector<Record> recs;
	CollectRecords(story, recs);
	std::set<uint64> keep;			// the times NOT to take back: an earlier run's, or not this row's
	if (oldTimes != nil)
		keep = *oldTimes;
	if (onlyTime != 0)
		for (size_t k = 0; k < recs.size(); ++k)
			if (recs[k].time != onlyTime)
				keep.insert(recs[k].time);
	if (delLen > 0)
	{
		if (delOffset != 0)
		{
			outWhy = "the deletion is shared with a touching row";
			return false;
		}
		if (RejectAt(story, delAnchor, &keep, true) == 0)
		{
			outWhy = "no deletion of that replace stands there";
			return false;
		}
	}
	if (insLen > 0)
	{
		// the insertion's pieces, from the last to the first - each whole
		std::vector<TextIndex> starts;
		int32 covered = 0;
		for (size_t k = 0; k < recs.size(); ++k)
		{
			if (recs[k].isDelete || keep.count(recs[k].time) != 0)
				continue;
			if (recs[k].at >= insAt && recs[k].at + recs[k].len <= insAt + insLen)
			{
				starts.push_back(recs[k].at);
				covered += recs[k].len;
			}
		}
		if (covered != insLen)
		{
			outWhy = "the inserted text is not that replace's record, whole";
			return false;
		}
		for (size_t k = starts.size(); k-- > 0; )
			RejectAt(story, starts[k], &keep, false);
	}
	return true;
}

void KBSTrackChange::CollectChanges(const UIDRef& story, std::vector<Change>& out)
{
	out.clear();
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil || !redline->StoryHasChanges())
		return;
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return;
	for (bool16 more = kTrue; more; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		const uint64 time = record->GetTimeStamp();
		delete record;
		// pieces of one replace are one run's: records of another run never join them.
		// ! This pairing leans on the ORDER the iterator hands records over in at one position: where a
		//   replace's deletion and the next (touching) replace's insertion share a position, the DELETION
		//   comes first (every record of rangelog-2026-09-26.txt: "at=1 DEL" then "at=1 INS"), so it
		//   closes the earlier insertion before the later one can be taken for a continuation of it.
		//   Nothing in redlineiterator.h promises that order; it is what was measured.
		const bool follows = !out.empty() && !out.back().hasDelete
			&& out.back().at + out.back().insLen == at && out.back().time == time;
		if (isDelete)
		{
			PMString deleted;
			it->DescribeChangeContent(deleted, 0x7fffffff);
			deleted.SetTranslatable(kFalse);
			// the deletion anchored right after an insertion is that insertion's pair
			if (follows)
			{
				out.back().hasDelete = true;
				out.back().deleted = deleted;
			}
			else
			{
				Change c;
				c.at = at;
				c.hasDelete = true;
				c.deleted = deleted;
				c.time = time;
				out.push_back(c);
			}
		}
		else if (follows)
		{
			out.back().insLen += len;	// an insertion split into pieces
		}
		else
		{
			Change c;
			c.at = at;
			c.insLen = len;
			c.time = time;
			out.push_back(c);
		}
	}
	delete it;
	for (size_t i = 0; i < out.size(); ++i)
		out[i].inserted = ReadText(story, out[i].at, out[i].insLen);
}

bool KBSTrackChange::FindRowChange(const UIDRef& story, const PMString& newText, const PMString& oldText,
	TextIndex nearAt, Change& out)
{
	std::vector<Change> changes;
	CollectChanges(story, changes);
	bool found = false;
	int32 best = 0;
	for (size_t i = 0; i < changes.size(); ++i)
	{
		if (changes[i].inserted != newText || changes[i].deleted != oldText)
			continue;
		const int32 distance = (changes[i].at > nearAt) ? changes[i].at - nearAt : nearAt - changes[i].at;
		if (!found || distance < best)
		{
			found = true;
			best = distance;
			out = changes[i];
		}
	}
	return found;
}

bool KBSTrackChange::FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange)
{
	bool checked = false, replaced = false, locked = false;
	if (!KBSResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) || !replaced)
		return false;
	// A footnote's row is never taken back - see IsInFootnote.
	if (KBSResultModel::GetHitPinned(chapterIdx, hitIdx) != KBSResultModel::kPinnedNone)
		return false;
	UIDRef docRef;
	IDFile file;
	// ***** OPEN, OR NOT AT ALL. ***** A chapter closed since the search leaves a dangling database pointer
	// behind (KBSBookScope::IsDocStillOpen says why) - asked before anything is read through it.
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) || docRef.GetDataBase() == nil
		|| !KBSBookScope::IsDocStillOpen(docRef))
		return false;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (!KBSResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash))
		return false;
	PMString originalText, replacedText;
	if (!KBSResultModel::GetHitChangeTexts(chapterIdx, hitIdx, originalText, replacedText))
		return false;
	outStory = UIDRef(docRef.GetDataBase(), story);

	// ***** A CHANGE BELONGS TO THE ROW NEAREST IT (2026-09-26, case accepted-then-reject). ***** Rows with
	// the same texts leave changes that look alike; when one row's change is gone (accepted in the Track
	// Changes panel), "the nearest change with the same texts" is ANOTHER row's, and rejecting it took
	// the wrong row back. So every change is handed to the nearest replaced row of the same story and
	// texts, and this row gets only a change handed to it.
	std::vector<TextIndex> twins;	// the stored starts of every replaced row that looks like this one
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool c2 = false, r2 = false, l2 = false;
		UID s2 = kInvalidUID;
		TextIndex a2 = kInvalidTextIndex, b2 = kInvalidTextIndex;
		uint64 h2 = 0;
		PMString o2, n2;
		if (i != hitIdx && KBSResultModel::GetHitFlags(chapterIdx, i, c2, r2, l2) && r2
			&& KBSResultModel::GetHitMatchIdentity(chapterIdx, i, s2, a2, b2, h2) && s2 == story
			&& KBSResultModel::GetHitChangeTexts(chapterIdx, i, o2, n2) && o2 == originalText && n2 == replacedText)
			twins.push_back(a2);
	}
	// ***** ONLY THE ROW'S OWN RUN (2026-09-26, the user's design): the time stamp its records were made
	// with, kept when it was replaced. Another run's records - an earlier replace of the same words -
	// are not candidates at all.
	const uint64 rowTime = KBSResultModel::GetHitRecordTime(chapterIdx, hitIdx);
	std::vector<Change> changes;
	CollectChanges(outStory, changes);
	bool found = false;
	int32 best = 0;
	for (size_t k = 0; k < changes.size(); ++k)
	{
		// ***** ONE REPLACE, TWO CLOCK TICKS (2026-09-28). ***** A replace's insertion and its deletion are
		// stamped separately, and now and then the clock ticks between the two: 1 replace in 600 of
		// InDesign's own changeText, 8 ms apart (work/kbs-regress/probe-tick-0928.jsx). CollectChanges
		// pairs by time, so that replace comes out as a lone insertion followed by a lone deletion - and
		// this row's change was not found: Reject Change grey, the status "no tracked change of this
		// replace is left" (case worklist-reject-then-jump, once on 2026-09-27; the user saw the same
		// line that day). FindGroupChange already took "the deletion standing alone right after the
		// insertion under its own time" for a touching group; a single row now does the same. The
		// deletion must hold exactly this row's original text, so another run's record cannot join.
		Change candidate = changes[k];
		if (!candidate.hasDelete && !originalText.IsEmpty() && k + 1 < changes.size()
			&& changes[k + 1].insLen == 0 && changes[k + 1].hasDelete
			&& changes[k + 1].at == candidate.at + candidate.insLen && changes[k + 1].deleted == originalText)
		{
			candidate.hasDelete = true;
			candidate.deleted = originalText;
			candidate.deleteTime = changes[k + 1].time;
		}
		if (candidate.inserted != replacedText || candidate.deleted != originalText)
			continue;
		if (rowTime != 0 && candidate.time != rowTime)
			continue;
		const int32 mine = (candidate.at > start) ? candidate.at - start : start - candidate.at;
		bool someoneNearer = false;
		for (size_t t = 0; t < twins.size() && !someoneNearer; ++t)
		{
			const int32 theirs = (candidate.at > twins[t]) ? candidate.at - twins[t] : twins[t] - candidate.at;
			if (theirs < mine)
				someoneNearer = true;
		}
		if (someoneNearer)
			continue;
		if (!found || mine < best)
		{
			found = true;
			best = mine;
			outChange = candidate;
		}
	}
	if (found)
		return true;

	// ***** A TOUCHING GROUP'S MERGED CHANGE (2026-09-27): this row's share of it. *****
	std::vector<int32> group;
	ReplacedTouchingGroup(chapterIdx, hitIdx, group);
	Change merged;
	UIDRef groupStory;
	if (group.size() < 2 || !FindGroupChange(chapterIdx, group, groupStory, merged))
		return false;
	int32 before = 0;		// the new text of the group's rows in front of this one
	for (size_t k = 0; k < group.size() && group[k] != hitIdx; ++k)
	{
		PMString o2, n2;
		if (!KBSResultModel::GetHitChangeTexts(chapterIdx, group[k], o2, n2))
			return false;
		before += WideString(n2).CharCount();
	}
	outStory = groupStory;
	outChange = merged;
	outChange.at = merged.at + before;
	outChange.insLen = WideString(replacedText).CharCount();
	outChange.inserted = replacedText;
	outChange.deleted = originalText;
	return true;
}

void KBSTrackChange::ReplacedTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows)
{
	outRows.clear();
	std::vector<int32> group;
	KBSResultModel::GetTouchingGroup(chapterIdx, hitIdx, group);	// in text order
	for (size_t k = 0; k < group.size(); ++k)
	{
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, group[k], checked, replaced, locked) && replaced
			&& !KBSResultModel::GetHitInFootnote(chapterIdx, group[k]))
			outRows.push_back(group[k]);
	}
}

bool KBSTrackChange::FindGroupChange(int32 chapterIdx, const std::vector<int32>& rows, UIDRef& outStory, Change& outChange,
	uint64* outDeleteTime)
{
	if (outDeleteTime != nil)
		*outDeleteTime = 0;
	if (rows.size() < 2)
		return false;
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) || docRef.GetDataBase() == nil
		|| !KBSBookScope::IsDocStillOpen(docRef))
		return false;
	PMString allOriginal, allReplaced;
	allOriginal.SetTranslatable(kFalse);
	allReplaced.SetTranslatable(kFalse);
	UID story = kInvalidUID;
	TextIndex firstStart = kInvalidTextIndex;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		PMString o, n;
		UID s = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 h = 0;
		if (!KBSResultModel::GetHitChangeTexts(chapterIdx, rows[k], o, n)
			|| !KBSResultModel::GetHitMatchIdentity(chapterIdx, rows[k], s, a, b, h))
			return false;
		if (k == 0)
		{
			story = s;
			firstStart = a;
		}
		else if (s != story)
			return false;
		allOriginal.Append(o);
		allReplaced.Append(n);
	}
	outStory = UIDRef(docRef.GetDataBase(), story);
	std::vector<Change> changes;
	CollectChanges(outStory, changes);
	bool found = false;
	int32 best = 0;
	for (size_t k = 0; k < changes.size(); ++k)
	{
		Change candidate = changes[k];
		uint64 deleteTime = candidate.hasDelete ? candidate.time : 0;
		if (candidate.inserted != allReplaced)
			continue;
		// the merged deletion, standing alone right after the merged insertion under its own time
		if (!allOriginal.IsEmpty() && !candidate.hasDelete && k + 1 < changes.size()
			&& changes[k + 1].insLen == 0 && changes[k + 1].hasDelete
			&& changes[k + 1].at == candidate.at + candidate.insLen && changes[k + 1].deleted == allOriginal)
		{
			candidate.hasDelete = true;
			candidate.deleted = allOriginal;
			deleteTime = changes[k + 1].time;
		}
		if (candidate.deleted != allOriginal)
			continue;
		const int32 distance = (candidate.at > firstStart) ? candidate.at - firstStart : firstStart - candidate.at;
		if (!found || distance < best)
		{
			found = true;
			best = distance;
			outChange = candidate;
			if (outDeleteTime != nil)
				*outDeleteTime = deleteTime;
		}
	}
	return found;
}

bool KBSTrackChange::RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx)
{
	UIDRef storyRef;
	Change c;
	if (!FindRowChangeForHit(chapterIdx, hitIdx, storyRef, c))
		return false;
	const TextIndex end = c.at + c.insLen;
	KBSResultModel::SetHitRange(chapterIdx, hitIdx, storyRef.GetUID(), c.at, end);
	PMString pre, match, post;
	KBSSearchEngine::SplitLineAroundMatch(storyRef, c.at, end, pre, match, post);
	KBSResultModel::SetHitSegments(chapterIdx, hitIdx, pre, match, post,
		KBSSearchEngine::HashMatchText(storyRef, c.at, end));
	return true;
}

// (RecordTimeIn - a replaced row's time read back off its records, the clock's - stood here until
//  2026-09-28. A row keeps the time it was handed out now: StampForRow, below.)

// ======================================================================================================
// THE SIGNATURE (2026-09-28) - see the head of KBSTrackChange.h.
// ======================================================================================================
const char* const KBSTrackChange::kSignAuthor = "KohakuFindChange";
const char* const KBSTrackChange::kSignFailedWhy = "the tracked changes could not be signed";

namespace
{
const uint64 kTicksPerMs = 10000;	// the stamps' 100 ns units in a millisecond
uint64 gRunT0 = 0;					// the current run's time
uint64 gRunStartReal = 0;			// the clock at the run's start: a record older than a second before it is not the run's
uint64 gLastStamp = 0;				// the last time handed out, over the session

bool IsSignAuthor(const PMString& who)
{
	PMString a(KBSTrackChange::kSignAuthor);
	a.SetTranslatable(kFalse);
	return who == a;
}

struct Unsigned
{
	TextIndex			at;
	int32				len;
	bool				isDelete;
	VOSRedlineChange*	record;		// as the iterator handed it over - the collector's caller deletes it
};

// The run's records in [from, to] not yet signed: insertion pieces in [from, to), deletions in [from, to].
// withStamp (when not nil) counts the records already carrying `stamp` there.
void CollectUnsigned(IRedlineDataStrand* redline, TextIndex from, TextIndex to, uint64 stamp,
	std::vector<Unsigned>& out, int32* withStamp)
{
	out.clear();
	if (withStamp != nil)
		*withStamp = 0;
	RedlineIterator* it = redline->NewRedlineIterator(from);
	if (it == nil)
		return;
	const uint64 notBefore = (gRunStartReal > GlobalTime::kOneSecond) ? gRunStartReal - GlobalTime::kOneSecond : 0;
	for (bool16 more = kTrue; more && it->GetCurrentPosition() <= to; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		const bool isInsert = (record->GetChangeType() == VOSRedlineChange::kInsert);
		const bool signedAlready = IsSignAuthor(record->GetUserName());
		if (withStamp != nil && signedAlready && record->GetTimeStamp() == stamp)
			++*withStamp;
		const bool inRange = isDelete ? (from <= at && at <= to) : (isInsert && len > 0 && from <= at && at < to);
		if (inRange && !signedAlready && record->GetTimeStamp() >= notBefore)
		{
			Unsigned u;
			u.at = at;
			u.len = len;
			u.isDelete = isDelete;
			u.record = const_cast<VOSRedlineChange*>(record);
			out.push_back(u);
		}
		else
			delete record;		// the caller owns it (redlineiterator.h:137-138)
	}
	delete it;
}
}	// anonymous namespace

void KBSTrackChange::BeginSignedRun()
{
	GlobalTime now;
	now.CurrentTime();
	gRunStartReal = now.GetTime();
	const uint64 floorNow = (gRunStartReal / kTicksPerMs) * kTicksPerMs;
	// never at or before the last time handed out: two runs in one millisecond take the next one
	gRunT0 = (gLastStamp == 0 || floorNow > gLastStamp) ? floorNow : (gLastStamp / kTicksPerMs + 1) * kTicksPerMs;
}

int32 KBSTrackChange::RowNumber(int32 chapterIdx, int32 hitIdx)
{
	int32 n = 0;
	for (int32 c = 0; c < chapterIdx; ++c)
		n += KBSResultModel::GetHitCount(c);
	return n + hitIdx;
}

uint64 KBSTrackChange::StampForRow(int32 chapterIdx, int32 hitIdx)
{
	if (gRunT0 == 0)
		BeginSignedRun();
	const uint64 stamp = gRunT0 + static_cast<uint64>(RowNumber(chapterIdx, hitIdx));
	if (stamp > gLastStamp)
		gLastStamp = stamp;
	return stamp;
}

bool KBSTrackChange::SignReplace(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kKBSSignRecordsCmdBoss));
	InterfacePtr<IRangeData> range(cmd, UseDefaultIID());
	InterfacePtr<IInt64Data> stampData(cmd, UseDefaultIID());
	if (cmd == nil || range == nil || stampData == nil)
		return false;
	range->Set(from, to);
	stampData->Set(static_cast<IInt64Data::ValueType>(stamp));
	cmd->SetItemList(UIDList(story));
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// the caller stops the run and says why
	return err == kSuccess;
}

bool KBSTrackChange::SignRecordsNow(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return true;		// nothing recorded in this story
	std::vector<Unsigned> found;
	CollectUnsigned(redline, from, to, stamp, found, nil);
	PMString author(kSignAuthor);
	author.SetTranslatable(kFalse);
	bool ok = true;
	// the insertion's pieces: the signed record over each piece, then the old one off it
	for (size_t k = 0; k < found.size(); ++k)
	{
		if (found[k].isDelete)
			continue;
		redline->ApplyRedlineChange(VOSRedlineChange::kInsert, found[k].at, stamp,
			found[k].record->GetIsMovedText(), found[k].len, author);
		redline->RemoveRedlineChange(found[k].at, *found[k].record);
	}
	// the deletion: InDesign's own command for a deletion's data (measured on the 2026-09-28 spike)
	for (size_t k = 0; k < found.size() && ok; ++k)
	{
		if (!found[k].isDelete)
			continue;
		found[k].record->SetUserName(author);
		found[k].record->SetTimeStamp(stamp);
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kReplaceDeleteChangeDataCmdBoss));
		InterfacePtr<IRedlineChangeData> changeData(cmd, IID_IREDLINECHANGEDATA);
		InterfacePtr<IIntData> position(cmd, IID_IINTDATA);
		ok = (cmd != nil && changeData != nil && position != nil);
		if (ok)
		{
			changeData->Set(*found[k].record);
			position->Set(found[k].at);
			cmd->SetItemList(UIDList(story));
			ok = (CmdUtils::ProcessCommand(cmd) == kSuccess);
		}
	}
	for (size_t k = 0; k < found.size(); ++k)
		delete found[k].record;
	if (!ok)
		return false;
	// read back: nothing of the run left unsigned in [from, to], and the row's time there
	std::vector<Unsigned> left;
	int32 withStamp = 0;
	CollectUnsigned(redline, from, to, stamp, left, &withStamp);
	for (size_t k = 0; k < left.size(); ++k)
		delete left[k].record;
	return left.empty() && (found.empty() || withStamp > 0);
}

bool KBSTrackChange::HasRecordsOfTimeIn(const UIDRef& story, TextIndex from, TextIndex to, uint64 time)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return false;
	RedlineIterator* it = redline->NewRedlineIterator(from);
	if (it == nil)
		return false;
	bool found = false;
	for (bool16 more = kTrue; more && !found && it->GetCurrentPosition() <= to; more = it->Increment(kFalse))
	{
		const VOSRedlineChange* record = it->GetCurrentChangeRecord();
		if (record == nil)
			continue;
		found = (record->GetTimeStamp() == time);
		delete record;		// the caller owns it (redlineiterator.h:137-138)
	}
	delete it;
	return found;
}
