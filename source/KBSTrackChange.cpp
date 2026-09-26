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
#include "UIDList.h"
#include "VOSRedline.h"
#include "redlineiterator.h"
#include "textiterator.h"

// Project includes:
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
		delete record;		// the caller owns it (redlineiterator.h:137-138)
		if (!IsOurs(it))
			continue;
		Record r;
		r.at = at;
		r.len = len;
		r.isDelete = isDelete;
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

int32 KBSTrackChange::RejectAt(const UIDRef& story, TextIndex position)
{
	InterfacePtr<IRedlineDataStrand> redline(QueryRedline(story));
	if (redline == nil)
		return 0;
	int32 done = 0;
	for (int32 guard = 0; guard < 100; ++guard)
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
			delete record;
			if (IsOurs(it))
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
		delete record;
		if (!IsOurs(it))
			continue;
		if (isDelete)
		{
			PMString deleted;
			it->DescribeChangeContent(deleted, 0x7fffffff);
			deleted.SetTranslatable(kFalse);
			// the deletion anchored right after an insertion is that insertion's pair
			if (!out.empty() && !out.back().hasDelete && out.back().at + out.back().insLen == at)
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
				out.push_back(c);
			}
		}
		else if (!out.empty() && !out.back().hasDelete && out.back().at + out.back().insLen == at)
		{
			out.back().insLen += len;	// an insertion split into pieces
		}
		else
		{
			Change c;
			c.at = at;
			c.insLen = len;
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
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) || docRef.GetDataBase() == nil)
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
	return FindRowChange(outStory, replacedText, originalText, start, outChange);
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
