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
#include <vector>

// Project includes:
#include "KBSReplaceEngine.h"
#include "KBSResultModel.h"
#include "KBSSearchEngine.h"	// the shared walker scope and the line-splitting the rows use
#include "KBSBookScope.h"		// reopening a chapter the user closed since the search

namespace
{

// What Edit > Undo calls a replace. Passed with kUnknownEncoding wherever it is used, so it is
// taken literally rather than looked up as a key in the string tables - an untranslated UI string
// is otherwise liable to come back as somebody else's translation.
const char* const kKBSReplaceSequenceName = "Kohaku Replace";

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
	{
		// Clear here too, for the same reason as above. A command can report kSuccess and still
		// leave an error standing, and an error standing when EndCommandSequence runs rolls the
		// whole chapter back - including the replacements that did work.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return false;
	}

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

// Does the text at this range still read the way the stored hit says it did?
//
// The walk order alone cannot tell "the Nth match" apart from "a DIFFERENT Nth match". An edit
// made between the search and the replace that removes one match and adds another elsewhere keeps
// the COUNT intact, so every checked hit still comes up and nothing looks wrong - while the
// numbering now points at text the user never checked, and that text gets rewritten.
//
// Comparing the matched text catches it. Not a proof: the same string arriving at a shifted
// position still agrees. But every edit that changes what the matched text READS is caught, which
// is the case that rewrites something the user did not look at.
bool MatchTextStillAgrees(int32 chapterIdx, int32 hitIdx, const UIDRef& story, TextIndex start, TextIndex end)
{
	PMString locator, storedPre, storedMatch, storedPost;
	if (!KBSResultModel::GetHitDisplay(chapterIdx, hitIdx, locator, storedPre, storedMatch, storedPost))
		return false;		// no row to compare against - leave the text alone

	// The same splitter the search used, so the two strings are cut the same way (a match spanning
	// paragraphs is trimmed identically on both sides). The model holds the RAW text - the
	// ellipsizing is done by the cell at draw time - so this compares like with like.
	PMString livePre, liveMatch, livePost;
	KBSSearchEngine::SplitLineAroundMatch(story, start, end, livePre, liveMatch, livePost);
	return liveMatch == storedMatch;
}

// Replace this chapter's checked hits. Returns how many were replaced.
// outAborted   = the re-walk ended before every checked hit had come up, i.e. the document is no
//                longer the one the result set describes (edited since the search, or the query
//                changed since).
// outStepLimit = the walk was cut off by the safety ceiling instead. Same symptom - checked hits
//                left over - but a different cause and different advice, so the two are kept
//                apart rather than both reported as "edited since the search".
// outStale     = checked hits that DID come up but whose text no longer reads the way the panel
//                says. They are left untouched and counted (see MatchTextStillAgrees).
// outLocked    = checked hits sitting on a locked layer or in a locked story. InDesign can search
//                those but offers no way to change them, so KBS does not either - they are left
//                untouched and counted (see KBSSearchEngine::IsMatchEditable).
int32 ReplaceInChapter(int32 chapterIdx, const UIDRef& docRef, bool& outAborted, bool& outStepLimit,
	int32& outStale, int32& outLocked)
{
	outAborted = false;
	outStepLimit = false;
	outStale = 0;
	outLocked = 0;

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

	// A sequence around this chapter's replacements. It NESTS inside the one ReplaceChecked opens
	// around the whole run and is absorbed by it, so it is not what the user sees on the Undo menu
	// - the outer sequence carries the name. It is kept because it is what makes a chapter's
	// replacements commit or roll back together.
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence("KBS Replace Chapter");

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

	while (!targets.empty() && steps < kMaxSteps)
	{
		++steps;

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

		// An unselected hit is only counted past. Its stored text range is deliberately NOT
		// refreshed: ReplaceChecked ends by turning the panel into a list of what CHANGED
		// (KeepOnlyReplaced), so every row that was left alone is dropped moments later, and a
		// pass that replaced nothing has not moved anything to begin with.
		if (targets.find(walkIndex) != targets.end())
		{
			// May this text be rewritten at all? The Find/Change dialog can be told to SEARCH
			// locked layers and locked stories, but InDesign gives no way to CHANGE what it finds
			// there, so neither does KBS. The match had to be walked to keep the walk order lined
			// up with the search; it is simply not written to.
			if (!KBSSearchEngine::IsMatchEditable(story, start))
			{
				++outLocked;
				targets.erase(walkIndex);
				++walkIndex;
				continue;
			}

			// Last check before anything is written: does this text still read the way the row
			// says? A checked row whose text has changed underneath is left alone and counted -
			// rewriting it would be rewriting something the user never saw.
			if (hitIdx < 0 || !MatchTextStillAgrees(chapterIdx, hitIdx, story, start, end))
			{
				++outStale;
				targets.erase(walkIndex);
				++walkIndex;
				continue;
			}

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
		++walkIndex;
	}

	if (sequence != nil)
		CmdUtils::EndCommandSequence(sequence);

	if (walker->IsWalking())
		walker->Halt();

	// Checked hits the re-walk never reached. Which of the two reasons it was decides what the
	// summary can honestly tell the user to do about it.
	if (!targets.empty())
	{
		if (steps >= kMaxSteps)
			outStepLimit = true;
		else
			outAborted = true;
	}
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
	int32 chaptersStepLimited = 0;
	int32 totalStale = 0;
	int32 totalLocked = 0;
	PMString firstSkipped;
	firstSkipped.SetTranslatable(kFalse);
	// A separate flag rather than firstSkipped.IsEmpty(): a chapter whose name is empty would
	// otherwise never count as "the first one", and every later chapter would overwrite it.
	bool haveFirstSkipped = false;

	// Chapters that took a replacement, and so want a window afterwards. Collected rather than
	// opened on the spot - see the comment on the loop that consumes this, below.
	std::vector<UIDRef> touched;

	{
	// ONE sequence around EVERY chapter, so a book-wide replace is a SINGLE undo step.
	//
	// Measured on the running application, 2026-07-28: with a sequence per chapter, undoing in one
	// document also removed the step from the OTHER chapters' histories - but did NOT revert their
	// text. Those chapters were left replaced with nothing left to undo them with, which is a
	// silent, unrecoverable loss of the user's content. Wrapping the whole run in one sequence is
	// what makes a single Ctrl+Z put all of it back, whichever chapter happens to be in front.
	//
	// The per-chapter sequences inside ReplaceInChapter nest within this one and are absorbed by it
	// (of nested sequences, only the outermost appears on the Undo menu).
	//
	// What this costs: the run is now all-or-nothing. A failure that leaves the global error code
	// set when this sequence ends rolls back EVERY chapter, not only the one that failed. That is
	// why RunWalkerCmd clears the error state on both of its failure paths - and why any new exit
	// path added inside this block must do the same.
	CmdUtils::SequencePtr seq;
	seq->SetName(PMString(kKBSReplaceSequenceName, PMString::kUnknownEncoding));

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
				if (!haveFirstSkipped)
				{
					firstSkipped = chapterName;
					haveFirstSkipped = true;
				}
				continue;	// missing / locked: report it, replace nothing here
			}
			docRef = reopened;
			KBSResultModel::RebindChapterDoc(ci, reopened);
		}

		bool aborted = false;
		bool stepLimit = false;
		int32 stale = 0;
		int32 locked = 0;
		const int32 replaced = ReplaceInChapter(ci, docRef, aborted, stepLimit, stale, locked);
		totalReplaced += replaced;
		totalStale += stale;
		totalLocked += locked;
		if (replaced > 0)
			++chaptersTouched;
		if (aborted)
		{
			++chaptersSkipped;
			if (!haveFirstSkipped)
			{
				firstSkipped = chapterName;
				haveFirstSkipped = true;
			}
		}
		if (stepLimit)
			++chaptersStepLimited;

		// A chapter that received a replacement wants a real window, so the change is visible and
		// can be undone - or saved - by hand. Chapters nothing landed in stay as they were:
		// opening windows on untouched documents would only be clutter. Nothing is saved here.
		// The window is not opened yet - see the loop after the sequence.
		if (replaced > 0)
			touched.push_back(docRef);
	}

	}	// the sequence ends HERE: every chapter's replacements commit together, as one undo step

	// Windows are opened AFTER the sequence, never from inside it. Measured 2026-07-28: a
	// kOpenLayoutCmdBoss processed between two chapters' replacements discarded the undo history of
	// the chapters already done - their text stayed replaced with nothing left to undo it with.
	// Opening the windows once everything is committed keeps that command clear of the replacements.
	for (size_t i = 0; i < touched.size(); ++i)
		KBSBookScope::ShowChapterWindow(touched[i]);

	// The panel now becomes a list of WHAT CHANGED: the occurrences left alone are no longer
	// results worth keeping in front of the user, and a replaced row cannot be selected again
	// anyway. A replace that landed nowhere leaves the results as they were.
	if (totalReplaced > 0)
		KBSResultModel::KeepOnlyReplaced();

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

	// Checked rows whose text no longer reads the way the panel says. Not an error and not a
	// failure to line up - the row came up exactly where it was expected, the TEXT there had
	// changed - so it is reported on its own terms.
	if (totalStale > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(totalStale);
		outSummary.Append(" hit(s) left alone - the text there changed since the search.");
	}

	// Checked rows on a locked layer or in a locked story. Not a failure either: InDesign's own
	// Find/Change searches those when asked to and then refuses to change them ("Search Only"), and
	// this reports the same outcome rather than letting the count quietly come up short.
	if (totalLocked > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(totalLocked);
		outSummary.Append(" hit(s) left alone - locked layer or story (those can be searched, not changed).");
	}

	// Same symptom, different cause: the walk was cut off by its own safety ceiling. Saying
	// "edited since the search?" here would send the user looking for the wrong thing.
	if (chaptersStepLimited > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(chaptersStepLimited);
		outSummary.Append(" chapter(s) stopped at the safety limit - does the change text contain the find text?");
	}
	return totalReplaced;
}

// End, KBSReplaceEngine.cpp.
