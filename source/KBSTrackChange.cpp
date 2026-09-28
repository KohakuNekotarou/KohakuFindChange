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

// ======================================================================================================
// THE READ SIDE (2026-09-28): a row's records are the ones carrying its time - KBS hands every row a time
// no other record can carry (StampForRow, the head of KBSTrackChange.h). Until 2026-09-28 a row's change
// was found by its texts and the nearest place, pairing records by time (CollectChanges, FindRowChange,
// FindGroupChange) and taken back by position and "not an earlier run's time" (RejectAt,
// RejectReplacement).
// ======================================================================================================
void KBSTrackChange::CollectRecordsOfTimes(const UIDRef& story, const std::set<uint64>& times, std::vector<Record>& out)
{
	out.clear();
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil || times.empty() || !redline->StoryHasChanges())
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
		const uint64 time = record->GetTimeStamp();
		const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		delete record;		// the caller owns it (redlineiterator.h:137-138)
		if (times.count(time) == 0)
			continue;
		Record r;
		r.at = at;
		r.len = len;
		r.isDelete = isDelete;
		r.time = time;
		if (isDelete)
		{
			it->DescribeChangeContent(r.text, 0x7fffffff);
			r.text.SetTranslatable(kFalse);
		}
		out.push_back(r);
	}
	delete it;
}

bool KBSTrackChange::RejectRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return false;
	RedlineIterator* it = redline->NewRedlineIterator(at);
	if (it == nil)
		return false;
	bool found = false;
	for (bool16 more = kTrue; more && it->GetCurrentPosition() <= at; more = it->Increment(kFalse))
	{
		if (it->GetCurrentPosition() != at)
			continue;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord();
		if (record == nil)
			continue;
		const bool del = (record->GetChangeType() == VOSRedlineChange::kDelete);
		const uint64 t = record->GetTimeStamp();
		delete record;		// the caller owns it (redlineiterator.h:137-138)
		if (del == isDelete && t == time)
		{
			found = true;
			break;
		}
	}
	const bool ok = found && it->ProcessReject(nil, kFalse, kFalse);
	delete it;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return ok;
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
	const uint64 rowTime = KBSResultModel::GetHitRecordTime(chapterIdx, hitIdx);
	if (rowTime == 0)
		return false;		// nothing was recorded for it (a footnote), or it was never replaced
	outStory = UIDRef(docRef.GetDataBase(), story);

	std::set<uint64> own;
	own.insert(rowTime);
	std::vector<Record> recs;
	CollectRecordsOfTimes(outStory, own, recs);
	Change c;
	c.time = rowTime;
	bool haveIns = false;
	TextIndex delAt = kInvalidTextIndex;
	for (size_t k = 0; k < recs.size(); ++k)
	{
		if (recs[k].isDelete)
		{
			if (!c.hasDelete)
			{
				c.hasDelete = true;
				c.deleted = recs[k].text;
				delAt = recs[k].at;
			}
			continue;
		}
		if (!haveIns)
		{
			c.at = recs[k].at;
			haveIns = true;
		}
		c.insLen += recs[k].len;
	}
	if (haveIns)
	{
		// ***** THE PIECES MUST READ, WHERE THEY STAND, AS WHAT THE ROW WROTE. ***** Somebody else's text
		// typed in between (an insertion of theirs splitting the row's) means the row's change is not its
		// own any more (case signed-user-typed-then-reject) - and a piece accepted in the Track Changes
		// panel leaves the rest short.
		c.inserted = ReadText(outStory, c.at, c.insLen);
		if (c.inserted != replacedText)
			return false;
		// ***** AND ITS DELETION STANDS RIGHT AFTER THEM (2026-09-28, case reject-next-to-user-edit). ***** A
		// replace's deletion is anchored at the end of its insertion. Somebody else's text typed right after
		// the row - a record of its own now that the row's records are signed - stands between the two, and
		// taking the row back would put the original text after that typing, not where it was.
		if (c.hasDelete && delAt != c.at + c.insLen)
			return false;
		outChange = c;
		return true;
	}
	if (!replacedText.IsEmpty())
		return false;		// it wrote text, and no record of it is left (accepted, or rejected in the panel)
	if (c.hasDelete)
	{
		c.at = delAt;
		outChange = c;
		return true;
	}
	// ***** REPLACED WITH NOTHING, AND JOINED TO A NEIGHBOUR (2026-09-28). ***** A deletion written next to
	// a touching neighbour's is joined to it and carries the neighbour's time: this row is found through a
	// deletion of a replaced touching neighbour that stands where this row stands and holds its text
	// (case touching-empty-both).
	std::vector<int32> group;
	ReplacedTouchingGroup(chapterIdx, hitIdx, group);
	std::set<uint64> theirs;
	for (size_t g = 0; g < group.size(); ++g)
	{
		const uint64 t = KBSResultModel::GetHitRecordTime(chapterIdx, group[g]);
		if (group[g] != hitIdx && t != 0)
			theirs.insert(t);
	}
	CollectRecordsOfTimes(outStory, theirs, recs);
	for (size_t k = 0; k < recs.size(); ++k)
	{
		if (recs[k].isDelete && recs[k].at == start && WideString(recs[k].text).IndexOf(WideString(originalText)) >= 0)
		{
			c.at = start;
			outChange = c;
			return true;
		}
	}
	return false;
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
