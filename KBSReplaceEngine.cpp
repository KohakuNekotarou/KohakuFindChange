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
#include "ITextModel.h"				// GetTextChangeCount - "has this story moved since the search?"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "ProgressBar.h"		// TaskProgressBar / SuppressProgressBarDisplay - as the search does it
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

// What Edit > Undo would call a replace, if the sequence were named. It is NOT named at the
// moment - see the SequencePtr in ReplaceChecked. Kept here so putting the name back is one line.
//
// Pass it with kUnknownEncoding wherever it is used, so it is taken literally rather than looked
// up as a key in the string tables - an untranslated UI string is otherwise liable to come back as
// somebody else's translation.
//const char* const kKBSReplaceSequenceName = "Kohaku Replace";

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

// A chapter that has work to do, already resolved to a LIVE document. Built before the command
// sequence opens - see the comment on the resolve pass in ReplaceChecked.
struct PendingChapter
{
	int32	chapterIdx;
	UIDRef	docRef;

	PendingChapter() : chapterIdx(-1) {}
};

// Does this chapter hold at least one checked, not-yet-replaced hit? Asked before the chapter is
// opened, so a replace never brings up documents it is not going to touch.
bool ChapterHasChecked(int32 chapterIdx)
{
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && checked && !replaced)
			return true;
	}
	return false;
}

// Is the match the walk just landed on the SAME occurrence this row describes?
//
// The walk order alone cannot tell "the Nth match" apart from "a DIFFERENT Nth match". An edit
// made between the search and the replace that removes one match and adds another elsewhere keeps
// the COUNT intact, so every checked hit still comes up and nothing looks wrong - while the
// numbering now points at text the user never checked, and that text gets rewritten.
//
// Comparing the matched TEXT alone does not catch it: in a plain-text search every match reads the
// same, so that question is always answered yes. Asking for the story, the position and the text
// together does - see KBSSearchEngine::MatchIsSameOccurrence, which both this and the jump use.
//
// posDelta is what this pass has already added to (or taken from) this story ahead of here, so the
// only difference left to find is the user's editing.
bool MatchStillStandsHere(int32 chapterIdx, int32 hitIdx, const UIDRef& story,
	TextIndex start, TextIndex end, int32 posDelta)
{
	UID expectStory = kInvalidUID;
	TextIndex expectStart = kInvalidTextIndex;
	PMString storedMatch;
	if (!KBSResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, expectStory, expectStart, storedMatch))
		return false;		// no row to compare against - leave the text alone

	return KBSSearchEngine::MatchIsSameOccurrence(story, start, end,
		expectStory, expectStart, storedMatch, posDelta);
}

// Replace this chapter's checked hits. Returns how many were replaced.
// outStepLimit = the walk was cut off by the safety ceiling. Checked hits are left over, as they
//                are when the walk simply runs out of matches - but these rows were never looked
//                at, so they get no word on their locator and the summary names the chapter.
// outMissing   = checked hits the walk could not find where the row said they were, either
//                because what came up there is a different occurrence, or because the row's turn
//                never came at all. Left untouched, counted and marked (MatchStillStandsHere).
// outLocked    = checked hits sitting on a locked layer or in a locked story. InDesign can search
//                those but offers no way to change them, so KBS does not either - they are left
//                untouched and counted (see KBSSearchEngine::IsMatchEditable).
// outRefused   = checked hits the replace command itself would not run on. Not a decision of ours
//                like the two above, and not a walk that lost its place like the two flags - the
//                command was asked and said no.
int32 ReplaceInChapter(int32 chapterIdx, const UIDRef& docRef, bool& outStepLimit,
	int32& outMissing, int32& outLocked, int32& outRefused)
{
	outStepLimit = false;
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;

	// walkOrder -> row index, plus the set of walk orders to replace. The rows are stored in PAGE
	// order and the walk runs in DOCUMENT order, so walkOrder is the only thing joining them.
	std::map<int32, int32> rowByWalkOrder;
	std::set<int32> targets;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	// story -> the text-change counter the search recorded for it. Collected for the CHECKED hits
	// only: a story nothing is going to be written to needs no trust decision.
	std::map<UID, uint32> searchStamps;

	for (int32 i = 0; i < hitCount; ++i)
	{
		const int32 walkOrder = KBSResultModel::GetHitWalkOrder(chapterIdx, i);
		if (walkOrder < 0)
			continue;
		rowByWalkOrder[walkOrder] = i;
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && checked && !replaced)
		{
			targets.insert(walkOrder);

			// The counter that story carried when the search read it, kept per story.
			UID stampStory = kInvalidUID;
			uint32 stampCount = 0;
			if (KBSResultModel::GetHitStoryStamp(chapterIdx, i, stampStory, stampCount)
				&& stampStory != kInvalidUID)
			{
				searchStamps.insert(std::make_pair(stampStory, stampCount));
			}
		}
	}
	if (targets.empty())
		return 0;

	// Which of this chapter's stories still hold EXACTLY the text the search walked.
	//
	// ITextModel keeps a counter it bumps on every character inserted, removed or replaced
	// (ITextModel.h:158-163). If it reads the same now as it did during the search, not one
	// character has moved, so the re-walk below returns the very same matches in the very same
	// order - which is all the walk order needs to be trustworthy. Every row in such a story can
	// then be replaced without reading the text under it first, which is what the same-occurrence
	// test spends its time doing.
	//
	// Read HERE, before a single character is written: our own replacements bump the counter too,
	// so the baseline has to be taken while the chapter is still untouched.
	//
	// A story that cannot be reached, or was left out of the stamps, simply is not trusted - the
	// per-hit test runs for it as before. Every way this can be wrong points the same way: towards
	// checking more, never towards writing something unchecked.
	std::map<UID, bool> trustedStories;
	for (std::map<UID, uint32>::const_iterator s = searchStamps.begin(); s != searchStamps.end(); ++s)
	{
		InterfacePtr<ITextModel> storyModel(docRef.GetDataBase(), s->first, UseDefaultIID());
		trustedStories[s->first] =
			(storyModel != nil) && (storyModel->GetTextChangeCount() == s->second);
	}

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

	// How far THIS pass has moved the text in each story. A replacement whose change string is not
	// the same length as the find string shifts every later match in that story - our own doing, so
	// it is cancelled out before the position test. Without this, "cat" -> "kitten" would refuse
	// every match after the first one.
	std::map<UID, int32> posDelta;

	// "May this frame's text be written to?" answered once per FRAME rather than once per hit. The
	// check climbs the page-item hierarchy and asks four separate locks, while a chapter's hits
	// usually sit in a handful of frames, so this is the per-hit cost most worth remembering.
	//
	// Safe to remember for the length of the pass: nothing in here locks anything, and the walk
	// holds the walker's critical section throughout, so no lock can change underneath it. Keyed by
	// story as well as frame because the story carries a lock of its own.
	std::map<std::pair<UID, UID>, bool> editableFrames;

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

		// How far this pass has already moved the text ahead of here, in THIS story. Subtracted
		// out before the position test below, so what is left to find is the user's editing.
		const UID storyUID = story.GetUID();
		int32 delta = 0;
		{
			const std::map<UID, int32>::const_iterator d = posDelta.find(storyUID);
			if (d != posDelta.end())
				delta = d->second;
		}

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
			// The frame is resolved per hit, but the lock question is asked once per frame and
			// remembered - see editableFrames, where the reasoning is.
			const UID frameUID = KBSSearchEngine::EditableFrameForMatch(story, start);
			const std::pair<UID, UID> frameKey(story.GetUID(), frameUID);
			bool editable = false;
			const std::map<std::pair<UID, UID>, bool>::const_iterator known = editableFrames.find(frameKey);
			if (known != editableFrames.end())
			{
				editable = known->second;
			}
			else
			{
				editable = KBSSearchEngine::IsFrameEditable(story, frameUID);
				editableFrames[frameKey] = editable;
			}

			if (!editable)
			{
				++outLocked;
				if (hitIdx >= 0)
					KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeLocked);
				targets.erase(walkIndex);
				++walkIndex;
				continue;
			}

			// Last check before anything is written: is this the same occurrence the row
			// describes - same story, same place, same text? A checked row whose text has moved or
			// changed underneath is left alone and counted, never rewritten.
			//
			// Skipped outright for a story whose change counter has not moved since the search: it
			// holds the same characters in the same order, so there is nothing for the test to
			// find. That is the ordinary case - search, then replace - and it takes the test's cost
			// off every row at once. See trustedStories above.
			bool trusted = false;
			{
				const std::map<UID, bool>::const_iterator t = trustedStories.find(storyUID);
				trusted = (t != trustedStories.end()) && t->second;
			}
			if (hitIdx < 0 || (!trusted && !MatchStillStandsHere(chapterIdx, hitIdx, story, start, end, delta)))
			{
				++outMissing;
				if (hitIdx >= 0)
					KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeMissing);
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

				// The command reports the range it WROTE, so the shift this replacement causes is
				// exact - no guessing at the change string's length, which GREP back-references
				// would make impossible anyway.
				posDelta[storyUID] += static_cast<int32>((replacedEnd - replacedStart) - (end - start));

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
			else
			{
				// The command would not run here. RunWalkerCmd has already cleared the error state
				// (it has to - a standing error would roll the whole sequence back), so without
				// this counter the hit just vanishes: the row came up, nothing was written, and the
				// replaced total silently comes up short with nothing to explain it.
				++outRefused;
				if (hitIdx >= 0)
					KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeRefused);
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

	// Checked hits the re-walk never reached. WHY it did not reach them decides what may be
	// said about them.
	if (!targets.empty())
	{
		if (steps >= kMaxSteps)
		{
			// The safety ceiling cut the walk short. These rows were never looked at, so nothing
			// can honestly be said about them: no word goes on their locator, and the summary
			// explains the chapter instead.
			outStepLimit = true;
		}
		else
		{
			// The walk ran to the end of the chapter without them coming up, so those matches are
			// gone. Said on the rows THEMSELVES rather than on the chapter, which named a file and
			// left the user to guess which of its rows it meant.
			for (std::set<int32>::const_iterator t = targets.begin(); t != targets.end(); ++t)
			{
				const std::map<int32, int32>::const_iterator row = rowByWalkOrder.find(*t);
				if (row != rowByWalkOrder.end())
					KBSResultModel::SetHitOutcome(chapterIdx, row->second, KBSResultModel::kOutcomeMissing);
			}
		}
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
	int32 totalMissing = 0;
	int32 totalLocked = 0;
	int32 totalRefused = 0;
	PMString firstSkipped;
	firstSkipped.SetTranslatable(kFalse);
	// A separate flag rather than firstSkipped.IsEmpty(): a chapter whose name is empty would
	// otherwise never count as "the first one", and every later chapter would overwrite it.
	bool haveFirstSkipped = false;

	// Set when the user stops the run from the progress bar. Cancelling gives back the document as
	// it was: the command sequence rolls the text back, and the result model is rolled back with
	// it, so the panel returns to being the search's results. Nothing is half done.
	bool cancelled = false;

	// Chapters that took a replacement, and so want a window afterwards. Collected rather than
	// opened on the spot - see the comment on the loop that consumes this, below.
	std::vector<UIDRef> touched;

	// FIRST PASS, deliberately OUTSIDE the command sequence: turn every chapter that has work into
	// a live document, reopening the ones the user has closed since the search.
	//
	// Reopening is a document OPEN, and an open processed BETWEEN two chapters' replacements is
	// exactly what was measured on 2026-07-28 to throw away the undo history of the chapters
	// already done - that is why ShowChapterWindow was moved out to the far end of this function.
	// The reopen belongs on the same side of the fence for the same reason. Doing it here also
	// means a chapter that cannot be opened at all is counted before anything has been written,
	// instead of interrupting a run that is already half committed.
	// How many chapters the run has to work through. Counted BEFORE anything is opened, because
	// the bar has to be up while the chapters are being opened - that is the slow part when the
	// user has closed the windows the search was holding.
	int32 chaptersWithWork = 0;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		if (ChapterHasChecked(ci))
			++chaptersWithWork;
	}

	// The progress bar. Book scope only, exactly as the search does it: a one-document replace
	// is a single step with nothing to cancel between. DisableChildProgressBars keeps the
	// chapter opens in the resolve pass below from raising bars of their own.
	//
	// showImmediate = kTrue for the reason the search learned the hard way: with the default
	// the bar waits out an internal delay, and a fast run beats that delay - so the cancel
	// button, the one thing the bar is really there for, never reaches the screen.
	PMString progressTitle("Replacing...");
	progressTitle.SetTranslatable(kFalse);
	const SuppressProgressBarDisplay suppressBar(KBSResultModel::IsFromBook() ? kFalse : kTrue);
	TaskProgressBar progressBar(progressTitle, chaptersWithWork, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	std::vector<PendingChapter> pending;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		if (!ChapterHasChecked(ci))
			continue;		// nothing selected here - do not even open this chapter

		UIDRef docRef;
		IDFile file;
		if (!KBSResultModel::GetChapterLocation(ci, docRef, file))
			continue;

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
					int32 chapterHits = 0;
					KBSResultModel::GetChapterDisplay(ci, firstSkipped, chapterHits);
					firstSkipped.SetTranslatable(kFalse);
					haveFirstSkipped = true;
				}
				continue;	// missing / locked: report it, replace nothing here
			}
			docRef = reopened;
			KBSResultModel::RebindChapterDoc(ci, reopened);
		}

		PendingChapter chapter;
		chapter.chapterIdx = ci;
		chapter.docRef = docRef;
		pending.push_back(chapter);
	}

	// Remember every row the run is about to change. A cancel rolls the TEXT back through the
	// sequence below; this is what lets the PANEL be rolled back with it, so the two cannot end up
	// telling different stories. Exactly one of RollBackRows / ForgetRowBackup follows.
	KBSResultModel::BeginRowBackup();

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
	// path added inside this block must do the same. Nothing in here opens a document or a window
	// either; both are kept outside, above and below (see the two passes around this block).
	CmdUtils::SequencePtr seq;
	// DELIBERATELY UNNAMED (user's call, 2026-07-28). SetName is what Edit > Undo would say after
	// the word "Undo"; leaving it unset lets InDesign word the step the way it words its own.
	// To put the name back: seq->SetName(PMString(kKBSReplaceSequenceName, PMString::kUnknownEncoding));

	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		PMString taskLine;
		taskLine.SetTranslatable(kFalse);
		taskLine.Append("Chapter ");
		taskLine.AppendNumber(static_cast<int32>(pi) + 1);
		taskLine.Append(" / ");
		taskLine.AppendNumber(static_cast<int32>(pending.size()));
		progressBar.SetTaskStatus(taskLine);

		PMString chapterName;
		int32 chapterHits = 0;
		KBSResultModel::GetChapterDisplay(pending[pi].chapterIdx, chapterName, chapterHits);
		chapterName.SetTranslatable(kFalse);
		progressBar.DoTask(chapterName);

		// Cancel is looked at HERE ONLY - between chapters. Inside a chapter the walk sits in
		// a TextWalkerSelections critical section and WasCancelled pumps events, so asking in
		// there would run UI work in the middle of a text walk.
		//
		// kTrue - the default - raises the global error state, and that IS the mechanism: a
		// regular command sequence commits when the global error code is kSuccess as it ends,
		// and otherwise rolls the database back to where it started (ICommandSequence.h). So
		// cancelling UNDOES the chapters already replaced instead of keeping them.
		//
		// That is the user's call (2026-07-28), and it makes cancel mean ONE thing. Keeping the
		// finished chapters left the book half changed with nothing on screen to say where the
		// line fell; now stopping is stopping, and the panel goes back to being the search's
		// results. The cost is that the work done so far is thrown away - breaking off a
		// 900-of-1000 run starts over.
		//
		// The panel is put back to match just below, where the error state is cleared as well,
		// before anything else is asked to run.
		if (progressBar.WasCancelled(kTrue))
		{
			cancelled = true;
			break;
		}

		const int32 ci = pending[pi].chapterIdx;
		const UIDRef& docRef = pending[pi].docRef;

		bool stepLimit = false;
		int32 missing = 0;
		int32 locked = 0;
		int32 refused = 0;
		const int32 replaced = ReplaceInChapter(ci, docRef, stepLimit, missing, locked, refused);
		totalReplaced += replaced;
		totalMissing += missing;
		totalLocked += locked;
		totalRefused += refused;
		if (replaced > 0)
			++chaptersTouched;
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

	if (cancelled)
	{
		// The sequence has just rolled the text back to where the run found it. The panel recorded
		// those replacements as they happened, so it has to shed them too - otherwise it would show
		// replaced rows sitting over text that is once again the original.
		//
		// What is left is exactly what the search produced: a work list with its checks intact,
		// ready to be run again. No window is opened either - nothing was changed to look at.
		KBSResultModel::RollBackRows();

		// The error the cancel raised has done its work. Leaving it standing would roll back
		// whatever command runs next, anywhere in the application.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		outSummary.Append("Replace cancelled - nothing was changed.");
		return 0;
	}
	KBSResultModel::ForgetRowBackup();

	// Windows are opened AFTER the sequence, never from inside it - the other half of the pair the
	// resolve pass above makes: no document and no window is opened while the sequence is standing.
	// Measured 2026-07-28: a kOpenLayoutCmdBoss processed between two chapters' replacements
	// discarded the undo history of the chapters already done - their text stayed replaced with
	// nothing left to undo it with. Opening the windows once everything is committed keeps that
	// command clear of the replacements.
	for (size_t i = 0; i < touched.size(); ++i)
		KBSBookScope::ShowChapterWindow(touched[i]);

	// The panel now becomes a REPORT of what the replace did: the rows it changed, and the rows it
	// was asked about and left alone, each saying why on its locator. The rows the user had
	// unchecked are dropped - they were never part of the request. A replace that was asked for
	// nothing at all leaves the results exactly as they were.
	KBSResultModel::KeepCheckedRows();

	// The count leads, so it survives the narrow status field's tail truncation.
	outSummary.AppendNumber(totalReplaced);
	outSummary.Append(" replaced in ");
	outSummary.AppendNumber(chaptersTouched);
	outSummary.Append(" chapter(s). Not saved - check them and save yourself.");

	if (chaptersSkipped > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(chaptersSkipped);
		outSummary.Append(" chapter(s) could not be opened (\"");
		outSummary.Append(firstSkipped);
		outSummary.Append("\" first) - moved, deleted, or in use?");
	}

	// Checked rows whose text no longer reads the way the panel says. Not an error and not a
	// failure to line up - the row came up exactly where it was expected, the TEXT there had
	// changed - so it is reported on its own terms.
	if (totalMissing > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(totalMissing);
		outSummary.Append(" hit(s) not found - the text is no longer where the search left it.");
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

	// Checked rows the replace command itself would not run on. The one entry in this list that is
	// a real failure rather than a deliberate decline, so it is worded as one - and reported at all,
	// which is the point: the alternative is a replaced total that comes up short in silence.
	if (totalRefused > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(totalRefused);
		outSummary.Append(" hit(s) could not be changed - InDesign refused the change there.");
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
