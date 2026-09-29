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
#include "IStoryList.h"			// Accept All Changes by KohakuFindChange - every text model
#include "IStringData.h"			// kAcceptAllRedlineCmdBoss's author
#include "ITextModel.h"
#include "ITrackChangeUtils.h"		// GetDeletedText - a deletion's text
#include "ITrackChangesSettings.h"	// ITrackChangeStorySettings - on kTextStoryBoss

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "GlobalTime.h"				// the run's time
#include "InCopySharedID.h"			// kRedlineStrandBoss, kSetRedlineTrackingCmdBoss, kReplaceDeleteChangeDataCmdBoss,
									// kAcceptAllRedlineCmdBoss
#include "PersistUtils.h"			// ::GetClass - IsInFootnote
#include "ITextStoryThread.h"
#include "TextID.h"					// kFootnoteReferenceBoss
#include "UIDList.h"
#include "Utils.h"
#include "VOSRedline.h"
#include "redlineiterator.h"
#include "textiterator.h"
#include "WideString.h"

#include <algorithm>				// CollectSignedRows - position order
#include <map>						// CollectSignedRows - one row per time

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

bool IsSignAuthor(const PMString& who)
{
	PMString a(KBSTrackChange::kSignAuthor);
	a.SetTranslatable(kFalse);
	return who == a;
}

// The story's records signed "KohakuFindChange" - wherever they stand, hidden conditional text included (a
// hidden condition's text, and its records with it, stand in a thread past the main text: measured,
// KTRedlineProbe - a record at 9 read at 16 once its condition was hidden). The walk is not gated by
// StoryHasChanges: what it answers for a story whose records all stand in hidden text is not measured.
// firstOnly = stop at 1. outTimes (optional, 2026-09-29, the waste re-check P-2) = every time those records
// carry, gathered on the same walk - what Accept All compares before and after.
int32 CountSignedRecords(const UIDRef& story, bool firstOnly, std::set<uint64>* outTimes = nil)
{
	if (outTimes != nil)
		outTimes->clear();
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return 0;
	int32 n = 0;
	for (bool16 more = kTrue; more && !(firstOnly && n > 0); more = it->Increment(kFalse))
	{
		const VOSRedlineChange* record = it->GetCurrentChangeRecord();
		if (record == nil)
			continue;
		if (IsSignAuthor(record->GetUserName()))
		{
			++n;
			if (outTimes != nil)
				outTimes->insert(record->GetTimeStamp());
		}
		delete record;		// the caller owns it (redlineiterator.h:137-138)
	}
	delete it;
	return n;
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
	// The official one-call read (textiterator.h AppendToStringAndIncrement), as KCM (KCMTextWords.h
	// WordsAt) and KESCL (KESCLFindInDoc.cpp MatchStillValid) read a range - after the same check they
	// make that it lies inside the story. A range running past the end reads up to the end, as the
	// character-by-character loop here did until 2026-09-28.
	if (model != nil && len > 0 && at >= 0 && at < model->TotalLength())
	{
		const int32 n = (len < model->TotalLength() - at) ? len : static_cast<int32>(model->TotalLength() - at);
		TextIterator it(model, at);
		it.AppendToStringAndIncrement(&w, n);
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

// (CollectRecords - every record of a story, for the bound on the two accept loops below - stood here until
//  2026-09-29. Accept All is InDesign's own command now, and the pending changes around a match are walked
//  in the match's own window, which bounds itself.)

bool KBSTrackChange::StoryHasChanges(const UIDRef& story)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	return redline != nil && redline->StoryHasChanges();
}

bool KBSTrackChange::DocumentHasSignedRecords(IDataBase* db)
{
	InterfacePtr<IStoryList> storyList(db, db != nil ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (storyList == nil)
		return false;
	// Every text model, not only the user-accessible ones: a record counts wherever it stands.
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
		if (CountSignedRecords(storyList->GetNthTextModelUID(i), true) > 0)
			return true;
	return false;
}

// (CollectSignedTimes - every signed time of the document, walked once before and once after Accept All to
//  tell the rows it accepted - stood here for one build on 2026-09-29. AcceptSignedInDocument hands those
//  times back now, from the walks it makes anyway: the waste re-check P-2.)

int32 KBSTrackChange::AcceptPendingAround(const UIDRef& story, TextIndex from, TextIndex to, PMString& outWhy)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	// ***** THE MATCH'S OWN WINDOW, NOT THE WHOLE STORY (2026-09-29, the official-terms audit A-3). ***** An
	// iterator made at a position stands first on the object CONTAINING it - an insertion that runs into the
	// match is met - but one made just past an insertion's end starts after that insertion, which is exactly
	// the one touching the match from the left (measured, KTRedlineProbe iterfrom: made at 6 or 7 it stood
	// on the insertion at 5; made at 8, right after it, it did not). So the walk starts one before the match
	// and stops past its end - the way RejectRecord, CollectUnsigned and HasRecordsOfTimeIn below walk. It
	// walked every record of the story from 0, for every ticked match, until then (and once more for the
	// loop's bound). The records the window holds bound the loop: an accept that leaves its record in place
	// must not spin.
	const TextIndex windowStart = (from > 0) ? from - 1 : 0;
	int32 inWindow = 0;
	{
		RedlineIterator* it = redline->NewRedlineIterator(windowStart);
		if (it == nil)
			return 0;
		for (bool16 more = kTrue; more && it->GetCurrentPosition() <= to; more = it->Increment(kFalse))
		{
			const VOSRedlineChange* record = it->GetCurrentChangeRecord();
			if (record == nil)
				continue;
			delete record;		// the caller owns it (redlineiterator.h:137-138)
			++inWindow;
		}
		delete it;
	}
	int32 done = 0;
	// The walk starts over after each accept (the iterator it came from is spent).
	for (int32 guard = 0; guard <= inWindow; ++guard)
	{
		if (!redline->StoryHasChanges())
			break;
		RedlineIterator* it = redline->NewRedlineIterator(windowStart);
		if (it == nil)
			break;
		bool found = false;
		for (bool16 more = kTrue; more && !found && it->GetCurrentPosition() <= to;
			more = found ? kFalse : it->Increment(kFalse))
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

int32 KBSTrackChange::AcceptSignedInDocument(IDataBase* db, int32& outLeft, PMString& outWhy,
	std::set<uint64>* outAcceptedTimes)
{
	outLeft = 0;
	if (outAcceptedTimes != nil)
		outAcceptedTimes->clear();
	InterfacePtr<IStoryList> storyList(db, db != nil ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (storyList == nil)
	{
		outWhy = "the document's stories could not be read";
		return -1;
	}
	PMString author(kSignAuthor);
	author.SetTranslatable(kFalse);
	int32 total = 0;
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
	{
		const UIDRef story = storyList->GetNthTextModelUID(i);
		std::set<uint64> timesBefore;
		const int32 before = CountSignedRecords(story, false, (outAcceptedTimes != nil) ? &timesBefore : nil);
		if (before == 0)
			continue;
		// ***** InDesign's OWN ACCEPT ALL, TOLD WHOSE (2026-09-29, the official-terms audit A-4 and the user's
		// ***** call: "only the ones named KohakuFindChange"). ***** kAcceptAllRedlineCmdBoss over the story, as
		// the product does it (InCopyDocUtils.cpp:2399-2405), with its IStringData set to the author: the
		// command then accepts that author's changes and leaves everybody else's (measured, KTRedlineProbe
		// acceptall - SDK use: none. InDesign's own by-author accept is ITrackChangeSuite::AcceptAllByUser,
		// whose way down to the command the SDK does not show).
		// Its IID_IACCEPTREDLINEINHIDDENTEXTDATA is left at its default, false: a change in hidden conditional
		// text is not accepted (measured) - counted in outLeft and said. InDesign's own Accept All seems to
		// leave them too (kAcceptAllDocSomeHiddenChangesMsgID, InCopySharedID.h:477 - not measured: its menu
		// action cannot be run from a script). The loop that stood here from 2026-09-27 accepted every
		// change, whoever made it.
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kAcceptAllRedlineCmdBoss));
		InterfacePtr<IStringData> whose(cmd, IID_ISTRINGDATA);
		if (cmd == nil || whose == nil)
		{
			outWhy = "InDesign's accept command could not be made";
			return -1;
		}
		whose->Set(author);
		cmd->SetItemList(UIDList(story));
		if (CmdUtils::ProcessCommand(cmd) != kSuccess)
		{
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// the caller rolls the step back and says why
			outWhy = "InDesign would not accept the changes";
			return -1;
		}
		std::set<uint64> timesAfter;
		const int32 after = CountSignedRecords(story, false, (outAcceptedTimes != nil) ? &timesAfter : nil);
		total += before - after;
		outLeft += after;
		// the times this accept took away - a row carrying one was accepted (re-check R-4)
		if (outAcceptedTimes != nil)
			for (std::set<uint64>::const_iterator t = timesBefore.begin(); t != timesBefore.end(); ++t)
				if (timesAfter.count(*t) == 0)
					outAcceptedTimes->insert(*t);
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
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	Utils<ITrackChangeUtils> utils;
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
			// ***** THE DELETED TEXT FROM THE UTILITY THE GUIDE NAMES (2026-09-29, the official-terms audit
			// ***** A-6). ***** ITrackChangeUtils::GetDeletedText reads the deleted-text thread anchored at the
			// deletion - the same text DescribeChangeContent gave for a tab, a return and a footnote
			// reference (measured, KTRedlineProbe deltext: 2 of 2 alike), which stays as the fallback - also
			// when the utility reads nothing there.
			if (model != nil && utils)
				utils->GetDeletedText(model, at, r.text);
			if (r.text.IsEmpty())
				it->DescribeChangeContent(r.text, 0x7fffffff);
			r.text.SetTranslatable(kFalse);
		}
		out.push_back(r);
	}
	delete it;
}

void KBSTrackChange::CollectSignedRows(const UIDRef& story, std::vector<SignedRow>& out)
{
	out.clear();
	// ***** NO StoryHasChanges() IN FRONT (2026-09-29, the official-terms audit A-1). ***** It answers no for a
	// story whose only changes stand in hidden conditional text (IRedlineDataStrand.h:107-112), so its rows
	// would drop out of the list for a reason that has nothing to do with KBS - the ledger's warning
	// (api-official-examples.md, "walk the tracked changes one by one"). Walking is cheap.
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	Utils<ITrackChangeUtils> utils;
	RedlineIterator* it = redline->NewRedlineIterator(0);
	if (it == nil)
		return;
	// One row per time, in the order the times are first met (position order - the walk runs from 0).
	std::map<uint64, size_t> rowOfTime;
	for (bool16 more = kTrue; more; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		const bool isInsert = (record->GetChangeType() == VOSRedlineChange::kInsert);
		const bool ours = IsSignAuthor(record->GetUserName());
		const uint64 time = record->GetTimeStamp();
		delete record;		// the caller owns it (redlineiterator.h:137-138)
		if (!ours || !(isDelete || (isInsert && len > 0)))
			continue;
		std::map<uint64, size_t>::const_iterator known = rowOfTime.find(time);
		if (known == rowOfTime.end())
		{
			SignedRow fresh;
			fresh.time = time;
			out.push_back(fresh);
			known = rowOfTime.insert(std::make_pair(time, out.size() - 1)).first;
		}
		SignedRow& row = out[known->second];
		if (isDelete)
		{
			if (row.hasDelete)
				continue;		// one deletion per time (a second would be a split KBS never writes)
			row.hasDelete = true;
			row.delAt = at;
			// The deleted text as CollectRecordsOfTimes reads it: the guide's utility, the description as the
			// fallback.
			if (model != nil && utils)
				utils->GetDeletedText(model, at, row.deletedText);
			if (row.deletedText.IsEmpty())
				it->DescribeChangeContent(row.deletedText, 0x7fffffff);
			row.deletedText.SetTranslatable(kFalse);
		}
		else
		{
			if (row.insLen == 0)
				row.at = at;
			row.insLen += len;
			row.insertedText.Append(ReadText(story, at, len));	// the piece's own text (re-check R-2)
		}
	}
	delete it;
	// A row replaced with nothing stands where its deletion does; then everything in position order.
	for (size_t k = 0; k < out.size(); ++k)
	{
		out[k].insertedText.SetTranslatable(kFalse);
		if (out[k].insLen == 0)
			out[k].at = out[k].delAt;
	}
	std::stable_sort(out.begin(), out.end(), [](const SignedRow& a, const SignedRow& b) { return a.at < b.at; });
}

namespace
{
// Take back (accept = false) or accept the ONE record standing at `at` of that kind and of exactly that
// time - RejectRecord's walk, which AcceptRecord shares since 2026-09-29.
bool ProcessRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete, bool accept)
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
	const bool ok = found && (accept ? it->ProcessAccept(nil, kFalse, kFalse) : it->ProcessReject(nil, kFalse, kFalse));
	delete it;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return ok;
}
}	// anonymous namespace

bool KBSTrackChange::RejectRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete)
{
	return ProcessRecord(story, at, time, isDelete, false);
}

bool KBSTrackChange::AcceptRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete)
{
	return ProcessRecord(story, at, time, isDelete, true);
}

bool KBSTrackChange::FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange)
{
	bool checked = false, replaced = false, locked = false;
	if (!KBSResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) || !replaced)
		return false;
	// A footnote's row is never taken back - see IsInFootnote.
	if (KBSResultModel::GetHitInFootnote(chapterIdx, hitIdx))
		return false;
	UIDRef docRef;
	IDFile file;
	// ***** OPEN, OR NOT AT ALL. ***** A chapter closed since the search leaves a dangling database pointer
	// behind - asked before anything is read through it. By the chapter's FILE since 2026-09-29 (the defect
	// re-check F-3, KBSBookScope::FindOpenChapterDoc): the held docRef read a chapter closed and opened
	// again as "not open", and could be answered for by a document that took a closed chapter's address.
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KBSBookScope::FindOpenChapterDoc(file, docRef))
		return false;
	KBSResultModel::RebindChapterDoc(chapterIdx, docRef);
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
	bool haveIns = false;
	TextIndex delAt = kInvalidTextIndex;
	for (size_t k = 0; k < recs.size(); ++k)
	{
		if (recs[k].isDelete)
		{
			if (!c.hasDelete)
			{
				c.hasDelete = true;
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
		if (ReadText(outStory, c.at, c.insLen) != replacedText)
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
	KBSSearchEngine::RereadRowText(chapterIdx, hitIdx, storyRef, c.at, end);
	return true;
}

// (RecordTimeIn - a replaced row's time read back off its records, the clock's - stood here until
//  2026-09-28. A row keeps the time it was handed out now: StampForRow, below.)

// ======================================================================================================
// THE SIGNATURE (2026-09-28) - see the head of KBSTrackChange.h.
// ======================================================================================================
const char* const KBSTrackChange::kSignAuthor = "KohakuFindChange";

namespace
{
const uint64 kTicksPerMs = 10000;	// the stamps' 100 ns units in a millisecond
uint64 gRunT0 = 0;					// the current run's time
uint64 gRunStartReal = 0;			// the clock at the run's start: a record older than a second before it is not the run's
uint64 gLastStamp = 0;				// the last time handed out, over the session

// (IsSignAuthor stands at the head of this file since 2026-09-29: Accept All Changes by KohakuFindChange counts
//  the signed records too.)

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

// The row's number in the result: the rows of every chapter before it, then its index.
int32 RowNumber(int32 chapterIdx, int32 hitIdx)
{
	int32 n = 0;
	for (int32 c = 0; c < chapterIdx; ++c)
		n += KBSResultModel::GetHitCount(c);
	return n + hitIdx;
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
