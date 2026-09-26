//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Track Changes parts for the replace - see KBSTrackChange.h. The command shapes (the user name on
//  the workspace, the story's tracking switch) are KCM's (KCMImportAuthor / KCMStoryTrackingOn),
//  and the record walk is the one the 2026-09-26 spike measured (13fe01e).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"
#include "ICommand.h"
#include "IRedlineDataStrand.h"
#include "ITrackChangeUtils.h"		// PrimaryIndexToDeletedText - where a deletion's text lives
#include "ISession.h"
#include "IStringData.h"
#include "ITextModel.h"
#include "ITrackChangesSettings.h"	// ITrackChangeStorySettings - on kTextStoryBoss
#include "IUserInfo.h"
#include "IWorkspace.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "InCopySharedID.h"			// kRedlineStrandBoss, kSetUserNameCmdBoss, kSetRedlineTrackingCmdBoss
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

const char* const KBSTrackChange::kAuthor = "KohakuFindChange";

namespace
{
ErrorCode SetUserName(const PMString& name)
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

bool IsOurs(RedlineIterator* it)
{
	PMString who;
	it->DescribeUser(who);
	PMString ours(KBSTrackChange::kAuthor);
	ours.SetTranslatable(kFalse);
	return who == ours;
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

KBSTrackChange::AuthorScope::AuthorScope() : fSwitched(false)
{
	InterfacePtr<IWorkspace> ws(GetExecutionContextSession()->QueryWorkspace());
	InterfacePtr<IUserInfo> info(ws, UseDefaultIID());
	if (info == nil)
		return;
	fOld = info->GetUserName();
	fOld.SetTranslatable(kFalse);
	PMString name(kAuthor);
	name.SetTranslatable(kFalse);
	// ***** A NAME THAT IS ALREADY OURS IS A LEFTOVER (2026-09-26). ***** Only an interrupted run leaves
	// InDesign's user name as kAuthor - InDesign going down in the middle of a replace, or a script that
	// set it and stopped before setting it back (it happened, and the script DOM cannot set the name
	// back to "unset"). Handed back as unset, so the user's own tracked edits are never signed with
	// KBS's name - KBS would take them for its own. "Unset" is the value a never-set name reads as,
	// "Unknown User Name" (IUserInfo.h:53, shown translated in a Japanese UI - measured through the
	// script DOM); an empty name, which only such a repair leaves, is put back to it the same way
	// (IUserInfoUtils.h:52 treats the two alike).
	if (fOld == name || fOld.IsEmpty())
		fOld = PMString("Unknown User Name");
	fSwitched = (SetUserName(name) == kSuccess);
}

KBSTrackChange::AuthorScope::~AuthorScope()
{
	if (fSwitched)
		SetUserName(fOld);
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
		if (!IsOurs(it))
			continue;
		Record r;
		r.at = at;
		r.len = len;
		r.isDelete = isDelete;
		r.time = time;
		out.push_back(r);
	}
	delete it;
}

bool KBSTrackChange::StoryHasOurChanges(const UIDRef& story)
{
	std::vector<Record> records;
	CollectRecords(story, records);
	return !records.empty();
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
			if (IsOurs(it) && isDelete == wantDelete && (keepTimes == nil || keepTimes->count(time) == 0))
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
	// ***** WHOLE RECORDS, OURS ONLY (2026-09-26, measured). ***** A range reject was tried and: an
	// insertion's exact range took nothing back, and an insertion range whose start held the touching
	// neighbour's deletion brought InDesign down (ShuksanTerminate; rangelog-2026-09-26.txt). So every
	// record is rejected whole by RejectAt, picked by position, author and time - nobody else's change
	// can be taken, and no range is handed to InDesign at all. A deletion SHARED with a touching row
	// (replaces that wrote nothing) cannot be split this way: that shape is refused before the write.
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
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
			outWhy = "no deletion of KBS's own stands there";
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
			outWhy = "the inserted text is not KBS's own record, whole";
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
		if (!IsOurs(it))
			continue;
		// pieces of one replace are one run's: records of another run never join them
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
		if (changes[k].inserted != replacedText || changes[k].deleted != originalText)
			continue;
		if (rowTime != 0 && changes[k].time != rowTime)
			continue;
		const int32 mine = (changes[k].at > start) ? changes[k].at - start : start - changes[k].at;
		bool someoneNearer = false;
		for (size_t t = 0; t < twins.size() && !someoneNearer; ++t)
		{
			const int32 theirs = (changes[k].at > twins[t]) ? changes[k].at - twins[t] : twins[t] - changes[k].at;
			if (theirs < mine)
				someoneNearer = true;
		}
		if (someoneNearer)
			continue;
		if (!found || mine < best)
		{
			found = true;
			best = mine;
			outChange = changes[k];
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
	std::vector<Record> recs;
	CollectRecords(story, recs);
	uint64 newest = 0;
	for (size_t k = 0; k < recs.size(); ++k)
		if (recs[k].at >= from && recs[k].at <= to && recs[k].time > newest)
			newest = recs[k].time;
	return newest;
}
