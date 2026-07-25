//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Replace engine implementation. See KBSReplaceEngine.h for the contract and for the measured
//  behaviour of the find/replace walker commands that this loop is built on.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IFindChangeCmdData.h"
#include "IFindChangeOptions.h"
#include "IFindChangeService.h"		// FindChangeResult enum
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"
#include "ISession.h"				// GetExecutionContextSession

// General includes:
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss / kTWReplaceTextCmdBoss / kFindChangeClientBoss
#include "WalkerScopeOptions.h"
#include "CmdUtils.h"				// commands and command sequences
#include "CreateObject.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "Utils.h"

#include <map>
#include <set>

// Project includes:
#include "KBSReplaceEngine.h"
#include "KBSResultModel.h"
#include "KBSSearchEngine.h"	// the shared walker scope and the line-splitting the rows use
#include "KBSBookScope.h"		// reopening a chapter the user closed since the search

namespace
{

// Run one find/change walker command. true = it landed on a match, with the story and range in
// the out parameters. Anything else - no more matches, a failure, a command that would not
// process - reads as "nothing here".
//
// A command that fails leaves an error on the thread's global error state. It is cleared here on
// purpose: left standing it would make the surrounding command sequence roll back (taking the
// successful replacements with it) and would block later commands in the session.
bool RunWalkerCmd(const ClassID& cmdBoss, ITextWalker* walker,
	UIDRef& outStory, TextIndex& outStart, TextIndex& outEnd)
{
	outStory = UIDRef();
	outStart = kInvalidTextIndex;
	outEnd = kInvalidTextIndex;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IFindChangeCmdData> cmdData(cmd, UseDefaultIID());
	if (cmdData == nil)
		return false;
	cmdData->SetTextWalker(walker);

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return false;
	}
	// GetReplacementCount is never updated by these commands, so the result code is the only
	// signal that something actually happened.
	if (cmdData->GetFindChangeResult() != IFindChangeService::kSuccess)
		return false;

	outStory = cmdData->GetRange(outStart, outEnd);
	return true;
}

// Does this chapter hold at least one checked, not-yet-replaced hit? Asked before the chapter is
// opened, so a replace never brings up documents it is not going to touch.
bool ChapterHasChecked(int32 chapterIdx)
{
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced) && checked && !replaced)
			return true;
	}
	return false;
}

// Replace this chapter's checked hits. Returns how many were replaced.
// outAborted = the re-walk ended before every checked hit had come up, i.e. the document is no
// longer the one the result set describes (edited since the search, or the query changed).
int32 ReplaceInChapter(int32 chapterIdx, const UIDRef& docRef, bool& outAborted)
{
	outAborted = false;

	// walkOrder -> row index, plus the set of walk orders to replace. The rows are stored in PAGE
	// order and the walk runs in DOCUMENT order, so walkOrder is the only thing joining them.
	std::map<int32, int32> rowByWalkOrder;
	std::set<int32> targets;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		const int32 walkOrder = KBSResultModel::GetHitWalkOrder(chapterIdx, i);
		if (walkOrder < 0)
			continue;
		rowByWalkOrder[walkOrder] = i;
		bool checked = false, replaced = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced) && checked && !replaced)
			targets.insert(walkOrder);
	}
	if (targets.empty())
		return 0;

	// NO IDataBase::SaveRestoreModifiedState here. The search wraps its walk in one because it
	// must leave a windowless chapter unmodified; a replace is meant to leave the document
	// changed, so guarding it would throw away the entire point. (Do not copy it over from
	// KBSSearchEngine::CollectHitsInDoc.)

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return 0;
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
		return 0;
	InterfacePtr<IK2ServiceProvider> provider(registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss));
	if (provider == nil)
		return 0;
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	if (walker == nil)
		return 0;

	// Always start a fresh walk from the top of the chapter - the same starting point the search
	// had, which is what makes the walk order comparable.
	if (walker->IsWalking())
		walker->Halt();

	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);
	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions));
	if (scope == nil)
		return 0;
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (client == nil)
		return 0;
	walker->Initialize(client, scope, opts, nil);

	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	if (selUtils == nil)
		return 0;

	// Required critical section around text-walker selection changes.
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);

	// One sequence for the whole chapter: every replacement in it undoes with a single Ctrl+Z.
	// Undo is per document, so the chapter is the largest grain InDesign allows.
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence("KBS Replace Checked");

	int32 walkIndex = 0;
	int32 replacedCount = 0;
	int32 steps = 0;
	// Room for the stored hits plus slack for matches that only exist because of a replacement
	// (see the re-hit skip below). A hard stop, so no query can spin in here.
	const int32 kMaxSteps = hitCount * 4 + 64;

	// The range the last replacement wrote, so a match INSIDE it can be recognised.
	UID lastReplStory = kInvalidUID;
	TextIndex lastReplStart = kInvalidTextIndex;
	TextIndex lastReplEnd = kInvalidTextIndex;

	while (!targets.empty() && steps++ < kMaxSteps)
	{
		// ALWAYS find first: the replace command does not search on its own, it only acts on the
		// match a find has just made current.
		UIDRef story;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		if (!RunWalkerCmd(kFindTextCmdBoss, walker, story, start, end))
			break;		// the walk is finished; whatever is left in targets never came up

		// A match sitting inside the text the previous replacement just wrote - a change string
		// that contains the find string ("cat" -> "cat cat"). It was never in the search results,
		// so it must NOT consume a walk order, or every later hit would line up one off and the
		// wrong occurrences would be replaced.
		if (story.GetUID() == lastReplStory && start >= lastReplStart && start < lastReplEnd)
			continue;

		const std::map<int32, int32>::const_iterator row = rowByWalkOrder.find(walkIndex);
		const int32 hitIdx = (row != rowByWalkOrder.end()) ? row->second : -1;

		if (targets.find(walkIndex) != targets.end())
		{
			UIDRef replacedStory;
			TextIndex replacedStart = kInvalidTextIndex, replacedEnd = kInvalidTextIndex;
			if (RunWalkerCmd(kTWReplaceTextCmdBoss, walker, replacedStory, replacedStart, replacedEnd))
			{
				++replacedCount;
				lastReplStory = replacedStory.GetUID();
				lastReplStart = replacedStart;
				lastReplEnd = replacedEnd;

				if (hitIdx >= 0)
				{
					// The reported range describes the REPLACED text, so the row's new line is
					// read straight from it - no guessing at the change string's length, which
					// GREP back-references would make impossible anyway.
					PMString pre, match, post;
					KBSSearchEngine::SplitLineAroundMatch(replacedStory, replacedStart, replacedEnd, pre, match, post);
					KBSResultModel::MarkHitReplaced(chapterIdx, hitIdx, pre, match, post, replacedStart, replacedEnd);
				}
			}
			// Whether or not the command took, this walk order is dealt with: leaving it in
			// targets would make the chapter look like it never lined up.
			targets.erase(walkIndex);
		}
		else if (hitIdx >= 0)
		{
			// Not selected: refresh its anchors anyway. Earlier replacements in the same story
			// have moved it, and its row still has to jump to the right place afterwards.
			KBSResultModel::SetHitRange(chapterIdx, hitIdx, start, end);
		}
		++walkIndex;
	}

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	if (walker->IsWalking())
		walker->Halt();

	// Checked hits the re-walk never reached: this document is not the one the results describe.
	if (!targets.empty())
		outAborted = true;
	return replacedCount;
}

} // anonymous namespace

int32 KBSReplaceEngine::ReplaceChecked(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	const int32 chapterCount = KBSResultModel::GetChapterCount();
	if (chapterCount <= 0)
	{
		outSummary.Append("No results to replace - run a search first.");
		return 0;
	}
	if (KBSResultModel::GetCheckedCount() <= 0)
	{
		outSummary.Append("Nothing checked.");
		return 0;
	}

	int32 totalReplaced = 0;
	int32 chaptersTouched = 0;
	int32 chaptersSkipped = 0;
	PMString firstSkipped;
	firstSkipped.SetTranslatable(kFalse);

	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		if (!ChapterHasChecked(ci))
			continue;		// nothing selected here - do not even open this chapter

		UIDRef docRef;
		IDFile file;
		if (!KBSResultModel::GetChapterLocation(ci, docRef, file))
			continue;

		PMString chapterName;
		int32 chapterHits = 0;
		KBSResultModel::GetChapterDisplay(ci, chapterName, chapterHits);

		// The chapter may have been closed since the search (a held window the user shut). Bring
		// it back the way a jump does, and rebind the model to the live database.
		if (!KBSBookScope::IsDocStillOpen(docRef))
		{
			UIDRef reopened;
			if (!KBSBookScope::ReopenChapterDoc(file, reopened))
			{
				++chaptersSkipped;
				if (firstSkipped.IsEmpty())
					firstSkipped = chapterName;
				continue;	// missing / locked: report it, replace nothing here
			}
			docRef = reopened;
			KBSResultModel::RebindChapterDoc(ci, reopened);
		}

		bool aborted = false;
		const int32 replaced = ReplaceInChapter(ci, docRef, aborted);
		totalReplaced += replaced;
		if (replaced > 0)
			++chaptersTouched;
		if (aborted)
		{
			++chaptersSkipped;
			if (firstSkipped.IsEmpty())
				firstSkipped = chapterName;
		}

		// Task 7 hooks in here: a chapter that received a replacement gets a real window, so the
		// change is visible and can be undone by hand.
	}

	// The count leads, so it survives the narrow status field's tail truncation.
	outSummary.AppendNumber(totalReplaced);
	outSummary.Append(" replaced in ");
	outSummary.AppendNumber(chaptersTouched);
	outSummary.Append(" chapter(s). Not saved - check them and save yourself.");

	if (chaptersSkipped > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(chaptersSkipped);
		outSummary.Append(" chapter(s) did not line up (\"");
		outSummary.Append(firstSkipped);
		outSummary.Append("\" first) - edited since the search? Search again.");
	}
	return totalReplaced;
}

// End, KBSReplaceEngine.cpp.
