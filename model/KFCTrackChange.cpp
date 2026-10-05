//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Track Changes parts for the replace - see KFCTrackChange.h. The command shape of the story's
//  tracking switch is KCM's (KCMStoryTrackingOn), and the record walk is the one the Track Changes
//  spike measured (13fe01e).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"
#include "ICommand.h"
#include "IInCopyDocUserList.h"		// ColourSignAuthor - the document's users and their colours
#include "IInCopyUIColors.h"		// ColourSignAuthor - the session's UI colours (Amber's index)
#include "IInt64Data.h"			// kKFCSignRecordsCmdBoss's time stamp
#include "IIntData.h"				// kReplaceDeleteChangeDataCmdBoss's position
#include "IOwnedItem.h"				// HiddenTextAnchor - where a hidden condition's text is anchored
#include "IRangeData.h"			// kKFCSignRecordsCmdBoss's range
#include "IRedlineChangeData.h"		// kReplaceDeleteChangeDataCmdBoss's record
#include "IRedlineDataStrand.h"
#include "ISession.h"				// ColourSignAuthor - the UI colours are the session's
#include "ISetUserColorsCmdData.h"	// ColourSignAuthor - kSetUserColorsCmdBoss's name and colour
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
#include "ScriptingDefs.h"			// en_UIAmber - the colour asked for by its scripting name
#include "ITextStoryThread.h"
#include "TextID.h"					// kFootnoteReferenceBoss
#include "ConditionalTextID.h"		// kHiddenTextBoss - IsInHiddenText
#include "UIDList.h"
#include "Utils.h"
#include "VOSRedline.h"
#include "redlineiterator.h"
#include "textiterator.h"
#include "WideString.h"

#include <algorithm>				// CollectSignedRows - position order
#include <map>						// CollectSignedRows - one row per time; OriginalFromRecords - the deletions by place

// Project includes:
#include "KFCBookScope.h"			// IsDocStillOpen - a closed chapter is not read
#include "KFCID.h"					// kKFCSignRecordsCmdBoss
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"		// the line a row shows, and its hash
#include "KFCTrackChange.h"

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

// "KohakuFindChange" as the records carry it - what the signing writes, Accept All tells InDesign, and the
// readers compare.
PMString SignAuthorName()
{
	PMString a(KFCTrackChange::kSignAuthor);
	a.SetTranslatable(kFalse);
	return a;
}

bool IsSignAuthor(const PMString& who)
{
	return who == SignAuthorName();
}

// A query run's floor (KFCTrackChange::SetOwnRunFloor): 0 = none. AcceptPendingAround leaves a signed record at or
// after it pending.
uint64 gOwnRunFloor = 0;

// A query run's masks (KFCTrackChange::NoteRunDeletions): by a deletion's place - its document, its story and its
// time - one flag per character of its text, set where the run itself had written that character. Only the run's
// deletions that took characters from a write of the run are here.
struct RunNoteKey
{
	IDataBase*	db;
	UID			story;
	uint64		stamp;
	bool operator<(const RunNoteKey& o) const
	{
		if (db != o.db)
			return db < o.db;
		if (story != o.story)
			return story < o.story;
		return stamp < o.stamp;
	}
};
std::map<RunNoteKey, std::vector<bool> > gRunNotes;

// A DELETION'S TEXT FROM THE UTILITY THE GUIDE NAMES. ITrackChangeUtils::GetDeletedText reads the
// deleted-text thread anchored at the deletion - the same text DescribeChangeContent gave for a tab, a
// return and a footnote reference (measured, KTRedlineProbe deltext: 2 of 2 alike), which stays as the
// fallback - also when the utility reads nothing there. `it` stands on the deletion. The one reader for
// CollectRecordsOfTimes and CollectSignedRows alike.
PMString ReadDeletedText(ITextModel* model, Utils<ITrackChangeUtils>& utils, RedlineIterator* it, TextIndex at)
{
	PMString text;
	if (model != nil && utils)
		utils->GetDeletedText(model, at, text);
	if (text.IsEmpty())
		it->DescribeChangeContent(text, 0x7fffffff);
	text.SetTranslatable(kFalse);
	return text;
}

// The story's records signed "KohakuFindChange" - wherever they stand, hidden conditional text included (a
// hidden condition's text, and its records with it, stand in a thread past the main text: measured,
// KTRedlineProbe - a record at 9 read at 16 once its condition was hidden). The walk is not gated by
// StoryHasChanges: it answers no for a story whose records all stand in hidden text - its header says so
// (IRedlineDataStrand.h:107-112), and case accept-all-hidden-condition met it.
// firstOnly = stop at 1. outTimes (optional) = every time those records
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

bool KFCTrackChange::IsInFootnote(const UIDRef& story, TextIndex at)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextStoryThread> thread(model != nil ? model->QueryStoryThread(at, nil, nil) : nil);
	return thread != nil && ::GetClass(thread) == kFootnoteReferenceBoss;
}

bool KFCTrackChange::IsInHiddenText(const UIDRef& story, TextIndex at)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextStoryThread> thread(model != nil ? model->QueryStoryThread(at, nil, nil) : nil);
	return thread != nil && ::GetClass(thread) == kHiddenTextBoss;
}

TextIndex KFCTrackChange::HiddenTextAnchor(const UIDRef& story, TextIndex at)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil)
		return kInvalidTextIndex;
	TextIndex anchor = kInvalidTextIndex;
	TextIndex pos = at;
	// A condition can stand inside text another hidden condition holds: climbed until the place shows. A
	// thread cannot hold its own anchor, so the bound is only a guard.
	for (int32 depth = 0; depth < 8; ++depth)
	{
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(pos, nil, nil));
		if (thread == nil || ::GetClass(thread) != kHiddenTextBoss)
			break;
		InterfacePtr<IOwnedItem> owned(thread, UseDefaultIID());
		const TextIndex next = (owned != nil) ? owned->GetTextIndex() : kInvalidTextIndex;
		if (next == kInvalidTextIndex || next == pos)
			break;
		anchor = pos = next;
	}
	return anchor;
}

PMString KFCTrackChange::ReadText(const UIDRef& story, TextIndex at, int32 len)
{
	WideString w;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	// The official one-call read (textiterator.h AppendToStringAndIncrement), as KCM (KCMTextWords.h
	// WordsAt) and KESCL (KESCLFindInDoc.cpp MatchStillValid) read a range - after the same check they
	// make that it lies inside the story. A range running past the end reads up to the end.
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

KFCTrackChange::TrackingScope::TrackingScope(IDataBase* db, const std::set<UID>& stories) : fDB(db), fOk(true)
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

KFCTrackChange::TrackingScope::~TrackingScope()
{
	for (size_t i = 0; i < fSwitchedOn.size(); ++i)
		SetTracking(UIDRef(fDB, fSwitchedOn[i]), kFalse);
}

bool KFCTrackChange::StoryHasChanges(const UIDRef& story)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	return redline != nil && redline->StoryHasChanges();
}

bool KFCTrackChange::DocumentHasSignedRecords(IDataBase* db)
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

int32 KFCTrackChange::AcceptPendingAround(const UIDRef& story, TextIndex from, TextIndex to, PMString& outWhy)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	// THE MATCH'S OWN WINDOW, NOT THE WHOLE STORY (not every record of the story from 0, for every ticked
	// match). An iterator made at a position stands first on the object CONTAINING it - an insertion that
	// runs into the match is met - but one made just past an insertion's end starts after that insertion,
	// which is exactly the one touching the match from the left (measured, KTRedlineProbe iterfrom: made at
	// 6 or 7 it stood on the insertion at 5; made at 8, right after it, it did not). So the walk starts one
	// before the match and stops past its end - the way RejectRecord, CollectUnsigned and
	// FirstRecordOfTimeIn below walk. The records the window holds bound the loop: an accept that leaves its
	// record in place must not spin.
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
			// A QUERY RUN'S OWN RECORD STAYS PENDING (SetOwnRunFloor): not accepted, and not a reason to stop looking.
			// (any author: a query run signs its records only at its end - the spec's D14 - so until then they are
			// the user's; and while it runs, nobody else writes - every record at or after the floor is the run's)
			const bool ownRun = (gOwnRunFloor != 0) && record->GetTimeStamp() >= gOwnRunFloor;
			delete record;
			if (ownRun)
				continue;
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

int32 KFCTrackChange::AcceptSignedInDocument(IDataBase* db, int32& outLeft, PMString& outWhy,
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
	const PMString author = SignAuthorName();
	int32 total = 0;
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
	{
		const UIDRef story = storyList->GetNthTextModelUID(i);
		// The times, not the records, are what is counted (the header): one per replace.
		std::set<uint64> timesBefore;
		if (CountSignedRecords(story, false, &timesBefore) == 0)
			continue;
		// InDesign's OWN ACCEPT ALL, TOLD WHOSE (the author's call: "only the ones named KohakuFindChange").
		// kAcceptAllRedlineCmdBoss over the story, as
		// the product does it (InCopyDocUtils.cpp:2399-2405), with its IStringData set to the author: the
		// command then accepts that author's changes and leaves everybody else's (measured, KTRedlineProbe
		// acceptall - SDK use: none. InDesign's own by-author accept is ITrackChangeSuite::AcceptAllByUser,
		// whose way down to the command the SDK does not show).
		// Its IID_IACCEPTREDLINEINHIDDENTEXTDATA is left at its default, false: a change in hidden conditional
		// text is not accepted (measured) - counted in outLeft and said. InDesign's own Accept All seems to
		// leave them too (kAcceptAllDocSomeHiddenChangesMsgID, InCopySharedID.h:477 - not measured: its menu
		// action cannot be run from a script). Not a loop of our own over the records, which accepts every
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
		(void)CountSignedRecords(story, false, &timesAfter);
		outLeft += static_cast<int32>(timesAfter.size());
		// the times this accept took away - a row carrying one was accepted
		for (std::set<uint64>::const_iterator t = timesBefore.begin(); t != timesBefore.end(); ++t)
		{
			if (timesAfter.count(*t) != 0)
				continue;
			++total;
			if (outAcceptedTimes != nil)
				outAcceptedTimes->insert(*t);
		}
	}
	return total;
}

// ======================================================================================================
// THE READ SIDE: a row's records are the ones carrying its time - KFC hands every row a time no other
// record can carry (StampForRow, the head of KFCTrackChange.h). Not its texts and the nearest place, and
// not a position and "not an earlier run's time": both were tried before the time was KFC's own.
// ======================================================================================================
void KFCTrackChange::CollectRecordsOfTimes(const UIDRef& story, const std::set<uint64>& times, std::vector<Record>& out)
{
	out.clear();
	// NO StoryHasChanges() IN FRONT (CollectSignedRows' rule). It answers no for a story whose only changes
	// stand in hidden conditional text (IRedlineDataStrand.h:107-112), and a row's records are looked for
	// wherever they stand: with the gate, a row whose replaced text the user hid would be told "no tracked
	// change of this replace is left" while its records were there - and only when nothing else in its
	// story was tracked, since one visible change anywhere in the story opens the gate and the walk below
	// finds the hidden ones too (read from the code; case reject-hidden-condition is the measurement).
	// Walking is cheap.
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil || times.empty())
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
			r.text = ReadDeletedText(model, utils, it, at);
		out.push_back(r);
	}
	delete it;
}

void KFCTrackChange::CollectSignedRows(const UIDRef& story, std::vector<SignedRow>& out)
{
	out.clear();
	// NO StoryHasChanges() IN FRONT. It answers no for a story whose only changes stand in hidden
	// conditional text (IRedlineDataStrand.h:107-112), so its rows would drop out of the list for a reason
	// that has nothing to do with KFC - the ledger's warning (docs/ai-notes/api-official-examples.md, at
	// StoryHasChanges). Walking is cheap.
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
				continue;		// one deletion per time (a second would be a split KFC never writes)
			row.hasDelete = true;
			row.delAt = at;
			row.deletedText = ReadDeletedText(model, utils, it, at);
		}
		else
		{
			if (row.insLen == 0)
				row.at = at;
			row.insLen += len;
			row.spanLen = at + len - row.at;	// to the end of this piece - the walk runs in position order
			row.insertedText.Append(ReadText(story, at, len));	// the piece's own text (see SignedRow)
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
// Are `part`'s characters those of `whole` with some left out, in their order? What a GREP $n's
// kept characters are of the match they came from (FindRowChangeForHit).
bool IsInOrderPartOf(const PMString& part, const PMString& whole)
{
	const WideString p(part), w(whole);
	WideString::const_iterator pi = p.begin();
	for (WideString::const_iterator wi = w.begin(); pi != p.end() && wi != w.end(); ++wi)
		if (*pi == *wi)
			++pi;
	return pi == p.end();
}

// Take back (accept = false) or accept the ONE record standing at `at` of that kind and of exactly that
// time - RejectRecord's walk, which AcceptRecord shares.
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

bool KFCTrackChange::RejectRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete)
{
	return ProcessRecord(story, at, time, isDelete, false);
}

bool KFCTrackChange::AcceptRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete)
{
	return ProcessRecord(story, at, time, isDelete, true);
}

// BY THE CHAPTER'S FILE, AND THE MODEL REBOUND TO WHAT IT FINDS. Not IsDocStillOpen of the docRef the
// results hold: a chapter closed and opened again sits at a new address (it would read "not open" - its
// replaced rows could not be taken back until a click on one rebound it), and a closed chapter's address
// taken by a document opened later would answer for THAT one. One question for every door, rather than
// each asking it with a test for no database in front (IsDocStillOpen answers false for that itself).
bool KFCTrackChange::ChapterDocIfOpen(int32 chapterIdx, UIDRef& outDocRef)
{
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, outDocRef, file)
		|| !KFCBookScope::FindOpenChapterDoc(file, outDocRef))
		return false;
	KFCResultModel::RebindChapterDoc(chapterIdx, outDocRef);
	return true;
}

bool KFCTrackChange::FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange)
{
	bool checked = false, replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) || !replaced)
		return false;
	// A footnote's row is never taken back - see IsInFootnote.
	if (KFCResultModel::GetHitInFootnote(chapterIdx, hitIdx))
		return false;
	// OPEN, OR NOT AT ALL. A chapter closed since the search leaves a dangling database pointer
	// behind - asked before anything is read through it (ChapterDocIfOpen, above).
	UIDRef docRef;
	if (!ChapterDocIfOpen(chapterIdx, docRef))
		return false;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash))
		return false;
	PMString originalText, replacedText;
	if (!KFCResultModel::GetHitChangeTexts(chapterIdx, hitIdx, originalText, replacedText))
		return false;
	const uint64 rowTime = KFCResultModel::GetHitRecordTime(chapterIdx, hitIdx);
	if (rowTime == 0)
		return false;		// nothing was recorded for it (a footnote), or it was never replaced
	outStory = UIDRef(docRef.GetDataBase(), story);

	std::set<uint64> own;
	own.insert(rowTime);
	std::vector<Record> recs;
	CollectRecordsOfTimes(outStory, own, recs);
	Change c;
	// THE ROW'S RECORDS, AND THE TEXT IT WROTE AROUND THEM. The first record of its time - its first
	// insertion piece, or its first deletion when it inserted nothing - stands Hit::recordLead into the text
	// the row wrote: 0 for a replace that writes the whole match, more for a GREP Change To holding $n, which
	// keeps the matched characters $n names and records only around them (the head of KFCTrackChange.h). The
	// row's change is that whole written text - not the insertion pieces alone, read as the replaced text,
	// with one deletion right after them, which a $n's row never is ("k" of "kat"): it could be neither taken
	// back nor accepted, and would be said to have "no tracked change left".
	TextIndex firstIns = kInvalidTextIndex, firstDel = kInvalidTextIndex;
	std::vector<Record> insertions;
	for (size_t k = 0; k < recs.size(); ++k)
	{
		if (recs[k].isDelete)
		{
			if (firstDel == kInvalidTextIndex)
				firstDel = recs[k].at;
			c.hasDelete = true;
		}
		else if (recs[k].len > 0)
		{
			if (firstIns == kInvalidTextIndex)
				firstIns = recs[k].at;
			insertions.push_back(recs[k]);
		}
	}
	const TextIndex first = (firstIns != kInvalidTextIndex) ? firstIns : firstDel;
	if (first != kInvalidTextIndex)
	{
		// NOT WHILE A HIDDEN CONDITION HOLDS IT (the author's call). A whole-match replace under a hidden
		// condition is refused by its deletion alone too, which stays in the main text while its insertion goes
		// with the hidden text (RowChangeIsHidden). A GREP $n that only inserted (cat -> $0s) has no deletion to
		// do that, and its text would read whole in the hidden thread - so it is asked of the first record
		// itself.
		if (IsInHiddenText(outStory, first))
			return false;
		c.at = first - KFCResultModel::GetHitRecordLead(chapterIdx, hitIdx);
		c.insLen = WideString(replacedText).CharCount();
		// THE TEXT MUST READ, WHERE ITS RECORDS PUT IT, AS WHAT THE ROW WROTE. Somebody else's text
		// typed in between (an insertion of theirs splitting the row's) means the row's change is not its
		// own any more (case signed-user-typed-then-reject) - and a piece accepted in the Track Changes
		// panel leaves the rest short.
		if (c.insLen > 0 && ReadText(outStory, c.at, c.insLen) != replacedText)
			return false;
		// EVERY RECORD OF ITS TIME INSIDE IT (case reject-next-to-user-edit; not only "its deletion right after
		// the insertion"). A whole-match replace anchors its deletion at the end of
		// what it wrote. Somebody else's text typed right after the row - a record of its own now that the row's
		// records are signed - stands between the two, and taking the row back would put the original text after
		// that typing, not where it was.
		bool inside = false;
		(void)OriginalFromRecords(outStory, c.at, c.insLen, recs, inside);
		if (!inside)
			return false;
		// AND WHAT NO INSERTION OF ITS TIME COVERS IS PART OF ITS ORIGINAL TEXT, IN ORDER. For a GREP $n
		// that is the characters it kept ("at" of "cat" in "kat"); for a whole-match replace it is nothing. Not the
		// whole original: a touching group written front to back leaves ONE deletion, carrying the LAST row's time,
		// so a row of it cannot give back its own original alone - the run's door does (KFCReplaceEngine
		// RejectRowsNow / AcceptRowsNow, through OriginalFromRecords).
		bool unused = false;
		if (!IsInOrderPartOf(OriginalFromRecords(outStory, c.at, c.insLen, insertions, unused), originalText))
			return false;
		outChange = c;
		return true;
	}
	if (!replacedText.IsEmpty())
		return false;		// it wrote text, and no record of it is left (accepted, or rejected in the panel)
	// REPLACED WITH NOTHING, AND JOINED TO A NEIGHBOUR. A deletion written next to
	// a touching neighbour's is joined to it and carries the neighbour's time: this row is found through a
	// deletion of a replaced touching neighbour that stands where this row stands and holds its text
	// (case touching-empty-both).
	std::vector<int32> group;
	ReplacedTouchingGroup(chapterIdx, hitIdx, group);
	std::set<uint64> theirs;
	for (size_t g = 0; g < group.size(); ++g)
	{
		const uint64 t = KFCResultModel::GetHitRecordTime(chapterIdx, group[g]);
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

bool KFCTrackChange::RowChangeIsHidden(int32 chapterIdx, int32 hitIdx)
{
	bool checked = false, replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) || !replaced
		|| KFCResultModel::GetHitInFootnote(chapterIdx, hitIdx))
		return false;
	UIDRef docRef;
	if (!ChapterDocIfOpen(chapterIdx, docRef))
		return false;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	const uint64 rowTime = KFCResultModel::GetHitRecordTime(chapterIdx, hitIdx);
	if (rowTime == 0 || !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash))
		return false;
	// The row's records, by its time (FindRowChangeForHit's walk); its first insertion says where its text is.
	const UIDRef storyRef(docRef.GetDataBase(), story);
	std::set<uint64> own;
	own.insert(rowTime);
	std::vector<Record> recs;
	CollectRecordsOfTimes(storyRef, own, recs);
	for (size_t k = 0; k < recs.size(); ++k)
	{
		if (!recs[k].isDelete && recs[k].len > 0)
			return IsInHiddenText(storyRef, recs[k].at);
	}
	return false;
}

void KFCTrackChange::ReplacedTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows)
{
	outRows.clear();
	std::vector<int32> group;
	KFCResultModel::GetTouchingGroup(chapterIdx, hitIdx, group);	// in text order
	for (size_t k = 0; k < group.size(); ++k)
	{
		bool checked = false, replaced = false, locked = false;
		if (KFCResultModel::GetHitFlags(chapterIdx, group[k], checked, replaced, locked) && replaced
			&& !KFCResultModel::GetHitInFootnote(chapterIdx, group[k]))
			outRows.push_back(group[k]);
	}
}

void KFCTrackChange::CurrentReplacedGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows,
	bool& outRefreshed)
{
	outRows.clear();
	outRefreshed = false;
	bool checked = false, replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) || !replaced)
		return;
	outRows.push_back(hitIdx);

	// Where a row stands NOW: put where its tracked change is first, when it has one of its own (a footnote's
	// row and an accepted one have none - they keep the range they were given, as the jump does).
	auto rangeNow = [&](int32 row, TextIndex& outStart, TextIndex& outEnd) -> bool
	{
		if (row != hitIdx && RefreshRowFromRecords(chapterIdx, row))
			outRefreshed = true;
		UIDRef docRef;
		IDFile file;
		UID story = kInvalidUID;
		return KFCResultModel::GetHitLocation(chapterIdx, row, docRef, file, story, outStart, outEnd)
			&& outStart != kInvalidTextIndex && outEnd != kInvalidTextIndex;
	};
	auto isReplaced = [&](int32 row) -> bool
	{
		bool c = false, r = false, l = false;
		return KFCResultModel::GetHitFlags(chapterIdx, row, c, r, l) && r;
	};

	TextIndex groupStart = kInvalidTextIndex, groupEnd = kInvalidTextIndex;
	if (!rangeNow(hitIdx, groupStart, groupEnd))
		return;

	// OUTWARD FROM THE ROW, IN THE LIST'S ORDER - NOT BY THE STORED RANGES. A stored range is where the
	// row stood when it was last read, and an edit moves the text under every row of the story at once while
	// only the row clicked is read again (the jump). Measured with a grouping by the stored ranges
	// (KFCResultModel::GetTouchingGroup): "catcat dog" replaced, "ZZ" typed in front, row 2 clicked - row 2
	// stood at its new place and row 1 at its old one, they no longer met, and the box read
	// "ZZkitten[catcat] dog" where the text had been "ZZcatcat dog" (case before-group-after-edit-mixed).
	// The list's ORDER is what no edit changes (KFCResultModel::GetStoryRowsInOrder), so the neighbours are
	// taken from it, each one read again from its records before it is asked whether it meets the group -
	// the group and one row either side are read, not the story's every row.
	std::vector<int32> story;
	KFCResultModel::GetStoryRowsInOrder(chapterIdx, hitIdx, story);
	size_t me = 0;
	while (me < story.size() && story[me] != hitIdx)
		++me;
	if (me == story.size())
		return;

	// Backward: a row whose text ends where the group starts (or past it - the rule GetTouchingGroup keeps).
	for (size_t k = me; k-- > 0; )
	{
		const int32 row = story[k];
		TextIndex s = kInvalidTextIndex, e = kInvalidTextIndex;
		if (!isReplaced(row) || !rangeNow(row, s, e) || e < groupStart || s > groupStart)
			break;		// not written, or not meeting it: nothing before it can meet it either
		outRows.insert(outRows.begin(), row);
		groupStart = s;
	}
	// Forward, the mirror.
	for (size_t k = me + 1; k < story.size(); ++k)
	{
		const int32 row = story[k];
		TextIndex s = kInvalidTextIndex, e = kInvalidTextIndex;
		if (!isReplaced(row) || !rangeNow(row, s, e) || s > groupEnd || e < groupEnd)
			break;
		outRows.push_back(row);
		groupEnd = e;
	}
}

bool KFCTrackChange::RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx)
{
	UIDRef storyRef;
	Change c;
	if (!FindRowChangeForHit(chapterIdx, hitIdx, storyRef, c))
		return false;
	const TextIndex end = c.at + c.insLen;
	KFCResultModel::SetHitRange(chapterIdx, hitIdx, storyRef.GetUID(), c.at, end);
	KFCSearchEngine::RereadRowText(chapterIdx, hitIdx, storyRef, c.at, end);
	return true;
}

// ======================================================================================================
// THE SIGNATURE - see the head of KFCTrackChange.h. A row keeps the time it was handed out (StampForRow,
// below); nothing reads one back off its records.
// ======================================================================================================
const char* const KFCTrackChange::kSignAuthor = "KohakuFindChange";

namespace
{
const uint64 kTicksPerMs = 10000;	// the stamps' 100 ns units in a millisecond
uint64 gRunT0 = 0;					// the current run's time
uint64 gRunStartReal = 0;			// the clock at the run's start: a record older than a second before it is not the run's
uint64 gLastStamp = 0;				// the last time handed out, over the session

// (IsSignAuthor stands at the head of this file: Accept All Changes by KohakuFindChange counts the signed
//  records too.)

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
	// A query run signs at its end (SignRunPlaces, the spec's D14): what it wrote reaches back to its floor, not to a
	// second before the last BeginSignedRun.
	const uint64 notBefore = (gOwnRunFloor != 0) ? gOwnRunFloor
		: (gRunStartReal > GlobalTime::kOneSecond) ? gRunStartReal - GlobalTime::kOneSecond : 0;
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
		n += KFCResultModel::GetHitCount(c);
	return n + hitIdx;
}
}	// anonymous namespace

void KFCTrackChange::BeginSignedRun()
{
	GlobalTime now;
	now.CurrentTime();
	gRunStartReal = now.GetTime();
	const uint64 floorNow = (gRunStartReal / kTicksPerMs) * kTicksPerMs;
	// never at or before the last time handed out: two runs in one millisecond take the next one
	gRunT0 = (gLastStamp == 0 || floorNow > gLastStamp) ? floorNow : (gLastStamp / kTicksPerMs + 1) * kTicksPerMs;
}

uint64 KFCTrackChange::OwnRunFloorNow()
{
	GlobalTime now;
	now.CurrentTime();
	return (now.GetTime() / kTicksPerMs) * kTicksPerMs;
}

void KFCTrackChange::SetOwnRunFloor(uint64 floor)
{
	gOwnRunFloor = floor;
	gRunNotes.clear();		// a new run's notes start empty
}

void KFCTrackChange::ClearOwnRunFloor()
{
	gOwnRunFloor = 0;
}

bool KFCTrackChange::RunOwnChars(const UIDRef& story, TextIndex from, TextIndex to, WideString& outMatch,
	std::vector<bool>& outOwn)
{
	outMatch.clear();
	outOwn.clear();
	if (gOwnRunFloor == 0 || to <= from)
		return false;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil || to > model->TotalLength())
		return false;
	std::vector<bool> own(static_cast<size_t>(to - from), false);
	// The run's own insertions in [from, to), position by position - the window AcceptPendingAround walks (one
	// before: an iterator made at a position stands on the record containing it).
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline != nil && redline->StoryHasChanges())
	{
		RedlineIterator* it = redline->NewRedlineIterator((from > 0) ? from - 1 : 0);
		if (it != nil)
		{
			for (bool16 more = kTrue; more && it->GetCurrentPosition() <= to; more = it->Increment(kFalse))
			{
				TextIndex at = 0;
				int32 len = 0;
				const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
				if (record == nil)
					continue;
				const bool ownInsert = (record->GetChangeType() == VOSRedlineChange::kInsert) && len > 0
					&& record->GetTimeStamp() >= gOwnRunFloor;		// any author (D14 - see AcceptPendingAround)
				delete record;		// the caller owns it (redlineiterator.h:137-138)
				if (ownInsert)
					for (TextIndex i = (at > from ? at : from); i < at + len && i < to; ++i)
						own[static_cast<size_t>(i - from)] = true;
			}
			delete it;
		}
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	// one character per position, the way OriginalFromRecords reads a range
	TextIterator ti(model, from);
	for (TextIndex i = from; i < to; ++i, ++ti)
		outMatch.Append(*ti);
	outOwn.swap(own);
	return true;
}

void KFCTrackChange::SnapshotRunDeletions(const UIDRef& story, TextIndex from, TextIndex to, std::vector<RunDeletion>& out)
{
	out.clear();
	if (gOwnRunFloor == 0)
		return;
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (redline == nil || model == nil)
		return;
	Utils<ITrackChangeUtils> utils;
	const TextIndex lo = (from > 0) ? from - 1 : 0;
	RedlineIterator* it = redline->NewRedlineIterator(lo > 0 ? lo - 1 : 0);
	if (it == nil)
		return;
	for (bool16 more = kTrue; more && it->GetCurrentPosition() <= to + 1; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool runDeletion = (record->GetChangeType() == VOSRedlineChange::kDelete)
			&& record->GetTimeStamp() >= gOwnRunFloor;
		const uint64 time = record->GetTimeStamp();
		delete record;
		if (!runDeletion || at < lo || at > to + 1)
			continue;
		RunDeletion d;
		d.time = time;
		d.text = ReadDeletedText(model, utils, it, at);
		out.push_back(d);
	}
	delete it;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
}

void KFCTrackChange::NoteRunDeletions(const UIDRef& story, TextIndex from, TextIndex to,
	const std::vector<RunDeletion>& before, const WideString& match, const std::vector<bool>& own)
{
	std::vector<RunDeletion> after;
	SnapshotRunDeletions(story, from, to, after);
	// A time that two deletions in either look carry cannot be followed by its time: dropped.
	std::map<uint64, int32> seen;
	for (size_t k = 0; k < before.size(); ++k)
		++seen[before[k].time];
	std::map<uint64, int32> seenAfter;
	for (size_t k = 0; k < after.size(); ++k)
		++seenAfter[after[k].time];
	const int32 matchCount = match.CharCount();
	int32 m = 0;		// the match characters are taken in order, across the deletions that grew
	for (size_t k = 0; k < after.size(); ++k)
	{
		const uint64 time = after[k].time;
		RunNoteKey key = { story.GetDataBase(), story.GetUID(), time };
		if (seenAfter[time] > 1 || seen[time] > 1)
		{
			gRunNotes.erase(key);
			continue;
		}
		PMString prior;
		for (size_t b = 0; b < before.size(); ++b)
			if (before[b].time == time)
				prior = before[b].text;
		const WideString now(after[k].text);
		const WideString old(prior);
		if (now == old)
			continue;		// not touched by this write
		// THE OLD TEXT INSIDE THE NEW ONE: the write's characters went in front of it, behind it, or both.
		int32 at = -1;
		for (int32 p = 0; p + old.CharCount() <= now.CharCount() && at < 0; ++p)
		{
			bool same = true;
			for (int32 q = 0; q < old.CharCount() && same; ++q)
				same = (now[p + q] == old[q]);
			if (same)
				at = p;
		}
		if (at < 0)
		{
			gRunNotes.erase(key);
			continue;
		}
		std::vector<bool> oldMask;
		std::map<RunNoteKey, std::vector<bool> >::const_iterator found = gRunNotes.find(key);
		if (found != gRunNotes.end() && static_cast<int32>(found->second.size()) == old.CharCount())
			oldMask = found->second;
		else
			oldMask.assign(static_cast<size_t>(old.CharCount()), false);		// written by a row with nothing of the run in it
		// the new characters, in order, lined up through the match
		std::vector<bool> mask;
		bool fits = true;
		for (int32 c = 0; c < now.CharCount() && fits; ++c)
		{
			if (c >= at && c < at + old.CharCount())
			{
				mask.push_back(oldMask[static_cast<size_t>(c - at)]);
				continue;
			}
			while (m < matchCount && match[m] != now[c])
				++m;
			if (m >= matchCount || static_cast<size_t>(m) >= own.size())
				fits = false;
			else
				mask.push_back(own[static_cast<size_t>(m++)]);
		}
		bool anyOwn = false;
		for (size_t c = 0; c < mask.size(); ++c)
			anyOwn = anyOwn || mask[c];
		if (!fits || !anyOwn)
			gRunNotes.erase(key);		// no line-up: read as it stands; nothing of the run in it: nothing to take out
		else
			gRunNotes[key] = mask;
	}
}

bool KFCTrackChange::RunMaskedText(const UIDRef& story, uint64 time, const PMString& text, PMString& outText)
{
	RunNoteKey key = { story.GetDataBase(), story.GetUID(), time };
	std::map<RunNoteKey, std::vector<bool> >::const_iterator found = gRunNotes.find(key);
	const WideString w(text);
	if (found == gRunNotes.end() || static_cast<int32>(found->second.size()) != w.CharCount())
		return false;
	WideString kept;
	for (int32 c = 0; c < w.CharCount(); ++c)
		if (!found->second[static_cast<size_t>(c)])
			kept.Append(w[c]);
	outText = PMString(kept);
	outText.SetTranslatable(kFalse);
	return true;
}

void KFCTrackChange::ApplyRunNotes(const UIDRef& story, std::vector<Record>& ioRecs)
{
	if (gRunNotes.empty())
		return;
	std::map<uint64, int32> deletionsOf;
	for (size_t k = 0; k < ioRecs.size(); ++k)
		if (ioRecs[k].isDelete)
			++deletionsOf[ioRecs[k].time];
	for (size_t k = 0; k < ioRecs.size(); ++k)
	{
		if (!ioRecs[k].isDelete || deletionsOf[ioRecs[k].time] != 1)
			continue;		// one deletion per time is what a mask follows
		PMString masked;
		if (RunMaskedText(story, ioRecs[k].time, ioRecs[k].text, masked))
			ioRecs[k].text = masked;
	}
}

bool KFCTrackChange::QueryRunWriting()
{
	return gOwnRunFloor != 0;
}

bool KFCTrackChange::HasRunRecordIn(const UIDRef& story, TextIndex from, TextIndex to)
{
	if (gOwnRunFloor == 0)
		return false;
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return false;
	RedlineIterator* it = redline->NewRedlineIterator((from > 0) ? from - 1 : 0);
	if (it == nil)
		return false;
	bool found = false;
	for (bool16 more = kTrue; more && !found && it->GetCurrentPosition() <= to; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
		const bool runs = record->GetTimeStamp() >= gOwnRunFloor;
		delete record;
		if (runs && (isDelete ? (from <= at && at <= to) : (len > 0 && at < to && at + len > from)))
			found = true;
	}
	delete it;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return found;
}

int32 KFCTrackChange::SignRunPlaces(IDataBase* db)
{
	if (gOwnRunFloor == 0 || db == nil)
		return 0;
	InterfacePtr<IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
		return 0;
	// One time per place, from a time of its own (never before the run's floor: BeginSignedRun takes the clock,
	// and never at or before a time already handed out). A place's row number is its place in this count - the
	// four digits below the millisecond (StampForRow's convention), so past 9999 places a new time is taken.
	BeginSignedRun();
	int32 k = 0;
	int32 signedPlaces = 0;
	// Every text model, not only the user-accessible ones: a record counts wherever it stands (CollectSignedRows).
	const int32 count = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < count; ++i)
	{
		const UIDRef story = storyList->GetNthTextModelUID(i);
		InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
		if (redline == nil)
			continue;
		// THE RUN'S RECORDS, AS PLACES: unsigned and at or after the floor, in position order, the ones that overlap
		// or touch taken together - an insertion and the deletion beside it, two touching matches (InDesign joined
		// their insertions: they were written unsigned, by one author).
		std::vector<std::pair<TextIndex, TextIndex> > raw;
		RedlineIterator* it = redline->NewRedlineIterator(0);
		if (it == nil)
			continue;
		for (bool16 more = kTrue; more; more = it->Increment(kFalse))
		{
			TextIndex at = 0;
			int32 len = 0;
			const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
			if (record == nil)
				continue;
			const bool isDelete = (record->GetChangeType() == VOSRedlineChange::kDelete);
			const bool runs = !IsSignAuthor(record->GetUserName()) && record->GetTimeStamp() >= gOwnRunFloor;
			delete record;
			if (runs)
				raw.push_back(std::make_pair(at, isDelete ? at : at + len));
		}
		delete it;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		if (raw.empty())
			continue;
		std::sort(raw.begin(), raw.end());
		std::vector<std::pair<TextIndex, TextIndex> > places;
		for (size_t r = 0; r < raw.size(); ++r)
		{
			if (!places.empty() && raw[r].first <= places.back().second)
			{
				if (raw[r].second > places.back().second)
					places.back().second = raw[r].second;
			}
			else
				places.push_back(raw[r]);
		}
		for (size_t p = 0; p < places.size(); ++p)
		{
			if (k > 9999)
			{
				BeginSignedRun();
				k = 0;
			}
			const uint64 stamp = gRunT0 + static_cast<uint64>(k++);
			if (stamp > gLastStamp)
				gLastStamp = stamp;
			if (!SignReplace(story, places[p].first, places[p].second, stamp))
				return -1;
			++signedPlaces;
		}
	}
	return signedPlaces;
}

void KFCTrackChange::ClearRunNotes()
{
	gRunNotes.clear();
}

uint64 KFCTrackChange::StampForRow(int32 chapterIdx, int32 hitIdx)
{
	if (gRunT0 == 0)
		BeginSignedRun();
	const uint64 stamp = gRunT0 + static_cast<uint64>(RowNumber(chapterIdx, hitIdx));
	if (stamp > gLastStamp)
		gLastStamp = stamp;
	return stamp;
}

bool KFCTrackChange::SignReplace(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kKFCSignRecordsCmdBoss));
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

bool KFCTrackChange::SignRecordsNow(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return true;		// nothing recorded in this story
	std::vector<Unsigned> found;
	CollectUnsigned(redline, from, to, stamp, found, nil);
	const PMString author = SignAuthorName();
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
	// the deletion: InDesign's own command for a deletion's data (measured on the signing spike)
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

bool KFCTrackChange::ColourSignAuthor(IDataBase* db)
{
	if (db == nil)
		return false;
	InterfacePtr<IInCopyDocUserList> users(db, db->GetRootUID(), UseDefaultIID());
	InterfacePtr<IInCopyUIColors> colours(GetExecutionContextSession(), UseDefaultIID());
	if (users == nil || colours == nil)
		return false;
	// Asked by its scripting enumeration - not by its place in the list (Amber was 56 of 65 on 21.0,
	// measured) nor by its name, which a translation could change.
	const int32 amber = colours->GetEnumerationIndex(ScriptID(en_UIAmber));
	if (amber < 0)
		return false;
	const PMString author = SignAuthorName();
	if (users->FindUserByName(author) >= 0 && users->GetUserColorIndex(author) == amber)
		return true;
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSetUserColorsCmdBoss));
	InterfacePtr<ISetUserColorsCmdData> data(cmd, UseDefaultIID());
	if (cmd == nil || data == nil)
		return false;
	data->Set(author, amber);
	cmd->SetItemList(UIDList(UIDRef(db, db->GetRootUID())));
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	if (err != kSuccess)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);		// not a reason to roll the replace back
	return err == kSuccess;
}

bool KFCTrackChange::FirstRecordOfTimeIn(const UIDRef& story, TextIndex from, TextIndex to, uint64 time,
	TextIndex& outAt)
{
	outAt = kInvalidTextIndex;
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return false;
	RedlineIterator* it = redline->NewRedlineIterator(from);
	if (it == nil)
		return false;
	// The first insertion piece wins - a row rebuilt from the records starts there too (CollectSignedRows) -
	// and the first deletion stands in for it only when there is none (a GREP $n that only deleted).
	TextIndex firstDelete = kInvalidTextIndex;
	for (bool16 more = kTrue; more && outAt == kInvalidTextIndex && it->GetCurrentPosition() <= to; more = it->Increment(kFalse))
	{
		TextIndex at = 0;
		int32 len = 0;
		const VOSRedlineChange* record = it->GetCurrentChangeRecord(&at, &len);
		if (record == nil)
			continue;
		if (record->GetTimeStamp() == time)
		{
			if (record->GetChangeType() == VOSRedlineChange::kDelete)
			{
				if (firstDelete == kInvalidTextIndex)
					firstDelete = at;
			}
			else if (record->GetChangeType() == VOSRedlineChange::kInsert && len > 0)
				outAt = at;
		}
		delete record;		// the caller owns it (redlineiterator.h:137-138)
	}
	delete it;
	if (outAt == kInvalidTextIndex)
		outAt = firstDelete;
	return outAt != kInvalidTextIndex;
}

PMString KFCTrackChange::OriginalFromRecords(const UIDRef& story, TextIndex at, int32 len,
	const std::vector<Record>& recs, bool& outAllInside)
{
	outAllInside = true;
	PMString original;
	original.SetTranslatable(kFalse);
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	const TextIndex end = at + len;
	if (model == nil || at < 0 || len < 0 || end > model->TotalLength())
	{
		outAllInside = false;
		return original;
	}
	// Which characters of the range an insertion covers, and which deletions stand at each place - laid out
	// once, so a long touching run is not walked once per record.
	std::vector<bool> inserted(static_cast<size_t>(len), false);
	std::multimap<TextIndex, size_t> deletionsAt;
	for (size_t k = 0; k < recs.size(); ++k)
	{
		const Record& r = recs[k];
		if (r.isDelete)
		{
			if (r.at < at || r.at > end)
				outAllInside = false;
			else
				deletionsAt.insert(std::make_pair(r.at, k));
			continue;
		}
		if (r.at < at || r.at + r.len > end)
			outAllInside = false;
		for (TextIndex i = (r.at > at ? r.at : at); i < r.at + r.len && i < end; ++i)
			inserted[static_cast<size_t>(i - at)] = true;
	}
	WideString w;
	TextIterator it(model, at);
	for (TextIndex i = at; ; ++i)
	{
		// a deletion's text stands in front of the character at its place (in the order the walk met them)
		typedef std::multimap<TextIndex, size_t>::const_iterator DelIt;
		const std::pair<DelIt, DelIt> here = deletionsAt.equal_range(i);
		for (DelIt d = here.first; d != here.second; ++d)
			w.Append(WideString(recs[d->second].text));
		if (i >= end)
			break;
		if (!inserted[static_cast<size_t>(i - at)])
			w.Append(*it);
		++it;
	}
	original = PMString(w);
	original.SetTranslatable(kFalse);
	return original;
}
