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
#include "IIntData.h"				// SPIKE 2026-09-28: kReplaceDeleteChangeDataCmdBoss's position
#include "IRedlineChangeData.h"		// SPIKE 2026-09-28: kReplaceDeleteChangeDataCmdBoss's record
#include "IRedlineDataStrand.h"
#include "IStringData.h"			// SPIKE step 2: kSetUserNameCmdBoss's name
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
#include "GlobalTime.h"				// SPIKE 2026-09-28: the run's T0
#include "InCopySharedID.h"			// kRedlineStrandBoss, kSetRedlineTrackingCmdBoss
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
#include "KBSResultModel.h"
#include "KBSSearchEngine.h"		// the line a row shows, and its hash
#include "KBSTrackChange.h"

#include <chrono>		// SPIKE 2026-09-28: what the signing costs
#include <cstdio>
#include <cstdlib>
#include <string>

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

uint64 KBSTrackChange::RecordTimeIn(const UIDRef& story, TextIndex from, TextIndex to)
{
	// ***** THE ROW'S OWN RECORD, NOT THE NEWEST IN THE RANGE (2026-09-27). ***** The replaces of one Change
	// All do not all carry one time, and a touching neighbour's DELETION stands at this row's first
	// position ("DEL at=1 ...283696" beside "INS at=1 ...193368" - rangelog-2026-09-26.txt). "The newest
	// in [from, to]" took the neighbour's time whenever the clock ticked between the two, and the row's
	// change was then never found again (Reject Change grey, the jump by fingerprint only).
	// ! Nor does each replace carry a time of its OWN: the same log has three replaces sharing
	//   ...076391 and four sharing ...193368 - the time is the run's and the clock tick's, never a
	//   row's. Rows are told apart by POSITION; the time only keeps other runs' records out.
	//   (This said "every replace carries its own time" until the 2026-09-27 defect sweep, C-1.)
	std::vector<Record> recs;
	CollectRecords(story, recs);
	if (to > from)
	{
		// the replace wrote text: its insertion starts at the row
		for (size_t k = 0; k < recs.size(); ++k)
			if (!recs[k].isDelete && recs[k].at == from && recs[k].len > 0)
				return recs[k].time;
		return 0;
	}
	// it wrote nothing: its deletion stands at the row - unless another deletion stands there too, when
	// which is whose cannot be told (0 = the change is found without the time)
	uint64 time = 0;
	int32 found = 0;
	for (size_t k = 0; k < recs.size(); ++k)
	{
		if (recs[k].isDelete && recs[k].at == from)
		{
			time = recs[k].time;
			++found;
		}
	}
	return (found == 1) ? time : 0;
}

// ======================================================================================================
// SPIKE (2026-09-28, spike/2026-09-28-kbs-signed-time - NOT FOR MAIN). See KBSTrackChange.h.
// ! The insertion is rewritten on the strand directly (ApplyRedlineChange / RemoveRedlineChange), between
//   the run's commands inside its sequence - not inside a command of its own. Whether Undo and Redo carry
//   that is one of the things measured; a feature would wrap it in a command.
// ======================================================================================================
namespace
{
const char* const kSignAuthor = "KohakuFindChange";
const uint64 kStampStep = 10;		// 1 microsecond, in the stamps' 100 ns units

uint64 sLastStamp = 0;			// the last stamp handed out, over every run of the session
uint64 sNextStamp = 0;
uint64 sRunStartReal = 0;		// the clock at the run's start: records older than this are not the run's

struct SignStats
{
	int32	rows;			// SignReplace calls
	int32	insSigned;		// insertion pieces rewritten
	int32	delSigned;		// deletions rewritten
	int32	nothing;		// calls that found no record to sign (a footnote, or none made)
	int32	cmdFailed;		// kReplaceDeleteChangeDataCmdBoss said no
	int32	leftUnsigned;	// records of the run still not signed after it
	int32	stampMissing;	// calls after which no record carries the row's stamp
	double	ms;				// time spent in SignReplace
	std::string firstTrouble;
	SignStats() : rows(0), insSigned(0), delSigned(0), nothing(0), cmdFailed(0), leftUnsigned(0),
		stampMissing(0), ms(0.0) {}
};
SignStats sStats;

struct FoundRecord
{
	TextIndex			at;
	int32				len;
	bool				isDelete;
	VOSRedlineChange*	record;		// as the iterator handed it over - deleted by us
};

bool IsSignAuthor(const PMString& who)
{
	PMString a(kSignAuthor);
	a.SetTranslatable(kFalse);
	return who == a;
}

// The run's records not signed yet, standing in [from, to]: insertion pieces in [from, to), deletions in
// [from, to]. Taken from an iterator started at `from` and closed before anything is written.
void CollectUnsigned(IRedlineDataStrand* redline, TextIndex from, TextIndex to, std::vector<FoundRecord>& out,
	int32* outStampCount, uint64 stamp)
{
	out.clear();
	if (outStampCount != nil)
		*outStampCount = 0;
	RedlineIterator* it = redline->NewRedlineIterator(from);
	if (it == nil)
		return;
	const uint64 notBefore = (sRunStartReal > GlobalTime::kOneSecond) ? sRunStartReal - GlobalTime::kOneSecond : 0;
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
		if (signedAlready && outStampCount != nil && record->GetTimeStamp() == stamp)
			++*outStampCount;
		const bool inRange = isDelete ? (from <= at && at <= to) : (isInsert && len > 0 && from <= at && at < to);
		if (inRange && !signedAlready && record->GetTimeStamp() >= notBefore)
		{
			FoundRecord f;
			f.at = at;
			f.len = len;
			f.isDelete = isDelete;
			f.record = const_cast<VOSRedlineChange*>(record);
			out.push_back(f);
		}
		else
			delete record;		// the caller owns it (redlineiterator.h:137-138)
	}
	delete it;
}

void Trouble(const std::string& what)
{
	if (sStats.firstTrouble.empty())
		sStats.firstTrouble = what;
}
}	// anonymous namespace

void KBSTrackChange::BeginSignedRun()
{
	GlobalTime now;
	now.CurrentTime();
	sRunStartReal = now.GetTime();
	sNextStamp = (sRunStartReal > sLastStamp) ? sRunStartReal : sLastStamp + kStampStep;
	sStats = SignStats();		// a run that broke off before its log line does not leak into the next
}

uint64 KBSTrackChange::TakeSignedStamp()
{
	if (sNextStamp == 0)
		BeginSignedRun();
	const uint64 stamp = sNextStamp;
	sLastStamp = stamp;
	sNextStamp += kStampStep;
	return stamp;
}

int32 KBSTrackChange::SignReplace(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp)
{
	const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
	++sStats.rows;
	int32 done = 0;
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline != nil)
	{
		std::vector<FoundRecord> found;
		CollectUnsigned(redline, from, to, found, nil, stamp);
		if (found.empty())
			++sStats.nothing;
		PMString author(kSignAuthor);
		author.SetTranslatable(kFalse);
		// the insertion's pieces: the new record over the piece, then the old one off it
		for (size_t k = 0; k < found.size(); ++k)
		{
			if (found[k].isDelete)
				continue;
			redline->ApplyRedlineChange(VOSRedlineChange::kInsert, found[k].at, stamp,
				found[k].record->GetIsMovedText(), found[k].len, author);
			redline->RemoveRedlineChange(found[k].at, *found[k].record);
			++sStats.insSigned;
			++done;
		}
		// the deletion: the command the KCM spike measured
		for (size_t k = 0; k < found.size(); ++k)
		{
			if (!found[k].isDelete)
				continue;
			found[k].record->SetUserName(author);
			found[k].record->SetTimeStamp(stamp);
			InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kReplaceDeleteChangeDataCmdBoss));
			InterfacePtr<IRedlineChangeData> changeData(cmd, IID_IREDLINECHANGEDATA);
			InterfacePtr<IIntData> position(cmd, IID_IINTDATA);
			ErrorCode err = kFailure;
			if (cmd != nil && changeData != nil && position != nil)
			{
				changeData->Set(*found[k].record);
				position->Set(found[k].at);
				cmd->SetItemList(UIDList(story));
				err = CmdUtils::ProcessCommand(cmd);
			}
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			if (err == kSuccess)
			{
				++sStats.delSigned;
				++done;
			}
			else
			{
				++sStats.cmdFailed;
				Trouble("deletion at " + std::to_string(found[k].at) + ": the command answered " + std::to_string(err));
			}
		}
		for (size_t k = 0; k < found.size(); ++k)
			delete found[k].record;

		// read it back: nothing of the run left unsigned in [from, to], and the row's stamp there
		std::vector<FoundRecord> left;
		int32 withStamp = 0;
		CollectUnsigned(redline, from, to, left, &withStamp, stamp);
		if (!left.empty())
		{
			sStats.leftUnsigned += static_cast<int32>(left.size());
			Trouble(std::string(left[0].isDelete ? "deletion" : "insertion") + " at " + std::to_string(left[0].at)
				+ " (len " + std::to_string(left[0].len) + ") still unsigned after [" + std::to_string(from) + ", "
				+ std::to_string(to) + "]");
		}
		for (size_t k = 0; k < left.size(); ++k)
			delete left[k].record;
		if (!found.empty() && withStamp == 0)
		{
			++sStats.stampMissing;
			Trouble("no record carries the stamp after [" + std::to_string(from) + ", " + std::to_string(to) + "]");
		}
	}
	else
		++sStats.nothing;
	sStats.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	return done;
}

void KBSTrackChange::WriteSignedRunLog(int32 chapterIdx, int32 replaced, double walkMs)
{
	char* temp = nil;
	size_t tempLen = 0;
	if (_dupenv_s(&temp, &tempLen, "TEMP") == 0 && temp != nil)
	{
		const std::string path = std::string(temp) + "\\kbs-spike-signed.log";
		FILE* f = nil;
		if (fopen_s(&f, path.c_str(), "a") == 0 && f != nil)
		{
			GlobalTime now;
			now.CurrentTime();
			std::fprintf(f, "%llu chapter=%d replaced=%d rows=%d ins=%d del=%d nothing=%d cmdFailed=%d leftUnsigned=%d "
				"stampMissing=%d signMs=%.1f walkMs=%.1f lastStamp=%llu trouble=%s\n",
				static_cast<unsigned long long>(now.GetTime()), chapterIdx, replaced, sStats.rows, sStats.insSigned,
				sStats.delSigned, sStats.nothing, sStats.cmdFailed, sStats.leftUnsigned, sStats.stampMissing, sStats.ms,
				walkMs, static_cast<unsigned long long>(sLastStamp),
				sStats.firstTrouble.empty() ? "-" : sStats.firstTrouble.c_str());
			std::fclose(f);
		}
		std::free(temp);
	}
	sStats = SignStats();
}

// SPIKE step 2 (2026-09-28): the AuthorScope of 4522ccf^, unchanged but for this comment.
namespace
{
ErrorCode SpikeSetUserName(const PMString& name)
{
	InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetUserNameCmdBoss));
	InterfacePtr<IStringData> data(cmd, IID_ISTRINGDATA);
	if (ws == nil || cmd == nil || data == nil)
		return kFailure;
	data->Set(name);
	cmd->SetItemList(UIDList(::GetUIDRef(ws)));
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return err;
}
}	// anonymous namespace

KBSTrackChange::AuthorScope::AuthorScope() : fSwitched(false)
{
	InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
	InterfacePtr<IUserInfo> info(ws, UseDefaultIID());
	if (info == nil)
		return;
	fOld = info->GetUserName();
	fOld.SetTranslatable(kFalse);
	PMString name(kSignAuthor);
	name.SetTranslatable(kFalse);
	// A name that is already ours is a leftover of an interrupted run: handed back as unset.
	if (fOld == name || fOld.IsEmpty())
		fOld = PMString("Unknown User Name");
	fSwitched = (SpikeSetUserName(name) == kSuccess);
}

KBSTrackChange::AuthorScope::~AuthorScope()
{
	if (fSwitched)
		SpikeSetUserName(fOld);
}
