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
#include "ICommandSequence.h"		// IAbortableCmdSeq - a cancel has to be stated, not implied
#include "IDataBase.h"				// IsModified / SetModified - putting the flag back after a cancel
#include "IDocumentCommands.h"		// Save - the facade the product's own code saves through
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
#include "ProgressBar.h"		// RangeProgressBar - the replace's progress + cancel, as the search does it
#include "Utils.h"

#include <map>
#include <set>
#include <vector>

// Project includes:
#include "KBSReplaceEngine.h"
#include "KBSResultModel.h"
#include "KBSRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KBSSearchEngine.h"	// the shared walker scope and the line-splitting the rows use
#include "KBSBookScope.h"		// reopening a chapter the user closed since the search
// (KBSJump.h was included here for IsHidePreviousChapterOn until 2026-08-03. A run that saves now
// hands every chapter back as it goes, whatever that toggle says - it is about JUMPING, not about
// what a run does with the chapters it opened for itself. Same call the search stopped making on
// 2026-08-02, for the same reason.)

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

// A replace is running. Its progress bar pumps events, so without this a menu command could be
// dispatched INTO the running replace. The panel's actions read it through IsReplacing().
bool gReplacing = false;

// Raise gReplacing for the length of a replace, whichever way ReplaceChecked returns - and it
// returns from a dozen places. Modelled on KBSSearchEngine's SearchingFlagGuard.
struct ReplacingFlagGuard
{
	ReplacingFlagGuard()	{ gReplacing = true; }
	~ReplacingFlagGuard()	{ gReplacing = false; }
};

// A chapter the run has to visit. Built before the command sequence opens - see the comment on the
// resolve pass in ReplaceChecked.
struct PendingChapter
{
	int32	chapterIdx;
	UIDRef	docRef;
	// Did the resolve pass get a live document for it? A chapter that could not be opened stays in
	// this list with opened = false, so the progress bar counts it like any other: the bar's total
	// is fixed before the opening starts, and dropping the failures out of the list would leave it
	// short of its own total and never reaching the end.
	bool	opened;
	// Was this chapter already unsaved BEFORE the run touched it? Only asked so that a CANCEL can
	// put the flag back: AbortCommandSequence restores the text but not the modified flag, so a
	// cancelled run left every chapter it had reached looking unsaved with nothing to save. (A real
	// Undo does clear it - the application does that itself - which is what made the difference
	// visible.) Chapters that were already dirty stay dirty; that was not ours to change.
	bool	wasModified;

	PendingChapter() : chapterIdx(-1), opened(false), wasModified(false) {}
};

// How many checked, not-yet-replaced hits does this chapter hold? Zero means the chapter is not
// visited at all, so a replace never brings up documents it is not going to touch. The COUNT (not
// just "any") is what sizes the progress bar: the run's real unit of work is a hit, not a chapter.
int32 CountCheckedInChapter(int32 chapterIdx)
{
	int32 checkedCount = 0;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && checked && !replaced)
			++checkedCount;
	}
	return checkedCount;
}

// (ChapterIndexForDoc lived here until 2026-08-03. It turned a saved document back into its chapter
// index so the "could not be saved" message could name it - needed only while the saving was done
// in a second pass over a list of UIDRefs. The chapter-at-a-time path saves inside the loop that
// already knows the index, so there is nothing left to look up.)

// Everything a run has to say about itself, in one place. It used to be seventeen locals in
// ReplaceChecked, which was workable while there was one way through that function; there are two
// now (all-at-once and chapter-at-a-time), and they have to hand the same account back to the same
// summary builder.
//
// Counters only - no decisions. Which of them get filled in is the running path's business, and
// BuildSummary says nothing about a counter that stayed zero.
struct RunTotals
{
	int32	replaced;			// hits actually rewritten
	int32	chaptersTouched;	// chapters at least one replacement landed in
	int32	chaptersSkipped;	// could not be opened at all
	int32	chaptersStepLimited;// the re-walk hit the safety ceiling
	int32	chaptersNotWalked;	// opened, but the text walker would not run on them
	int32	missing;			// checked hits whose text is no longer where the row says
	int32	locked;				// checked hits on a locked layer or in a locked story
	int32	refused;			// the replace command was asked and said no
	int32	chaptersSaved;
	int32	chaptersNotSaved;

	// The FIRST name in each of the three lists that name one. Kept with a flag of its own rather
	// than testing IsEmpty(): a chapter whose name is empty would otherwise never count as the
	// first, and every later one would overwrite it.
	PMString	firstSkipped;
	PMString	firstNotWalked;
	PMString	firstNotSaved;
	bool		haveFirstSkipped;
	bool		haveFirstNotWalked;
	bool		haveFirstNotSaved;

	// The user stopped the run from the progress bar. What that MEANS depends on which path was
	// running - see BuildSummary.
	bool		cancelled;

	// Were the chapters this plug-in opened handed back? The summary has to say so - and, more
	// importantly, has to say when they were NOT, since a user who ticked the box expects them gone.
	bool		chaptersClosed;

	// Chapters the run wants to leave on screen. The all-at-once path fills this with every chapter
	// it replaced in (unless it is about to close them); the chapter-at-a-time path fills it with
	// the ones whose SAVE failed, since those are the only ones it does not close. Opened after all
	// the replacing is over, never between chapters - see the note where they are consumed.
	std::vector<UIDRef>	windowsToOpen;

	RunTotals()
		: replaced(0), chaptersTouched(0), chaptersSkipped(0), chaptersStepLimited(0),
		  chaptersNotWalked(0), missing(0), locked(0), refused(0),
		  chaptersSaved(0), chaptersNotSaved(0),
		  haveFirstSkipped(false), haveFirstNotWalked(false), haveFirstNotSaved(false),
		  cancelled(false), chaptersClosed(false)
	{
		firstSkipped.SetTranslatable(kFalse);
		firstNotWalked.SetTranslatable(kFalse);
		firstNotSaved.SetTranslatable(kFalse);
	}
};

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
// outNotWalked = the chapter could not be WALKED AT ALL: no Find/Change options, no text walker, no
//                walker scope for this document, no walker client. Nothing was written, and nothing
//                can honestly be said about any individual row, so the summary names the chapter
//                instead - the same distinction the SEARCH makes with ChapterWalkResult. Without it
//                such a chapter dropped out of a replace in complete silence, which is exactly what
//                every other counter here exists to prevent.
//   progressBar  - the run's bar, sized in HITS. This chapter moves it from progressBase to
//                  progressBase + (its own checked hits) as it consumes them. nil is allowed.
int32 ReplaceInChapter(int32 chapterIdx, const UIDRef& docRef, bool& outStepLimit,
	int32& outMissing, int32& outLocked, int32& outRefused, bool& outNotWalked,
	RangeProgressBar* progressBar, int32 progressBase, int32& ioProgressReported)
{
	outStepLimit = false;
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;
	outNotWalked = false;

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

	// What the bar counts down from. Every target leaves this set exactly once - replaced, refused,
	// locked or missing - so "how many have gone" is the honest measure of this chapter's progress,
	// and it does not care WHY a hit was finished with.
	const int32 targetsAtStart = static_cast<int32>(targets.size());

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

	// Every failure from here to the critical section means the walk never started - see outNotWalked
	// on why each one has to be reported rather than returning a bare zero.
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
	{
		outNotWalked = true;
		return 0;
	}
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		outNotWalked = true;
		return 0;
	}
	InterfacePtr<IK2ServiceProvider> provider(registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss));
	if (provider == nil)
	{
		outNotWalked = true;
		return 0;
	}
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	if (walker == nil)
	{
		outNotWalked = true;
		return 0;
	}

	// Always start a fresh walk from the top of the chapter - the same starting point the search
	// had, which is what makes the walk order comparable.
	if (walker->IsWalking())
		walker->Halt();

	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);
	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions));
	if (scope == nil)
	{
		outNotWalked = true;
		return 0;
	}
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (client == nil)
	{
		outNotWalked = true;
		return 0;
	}
	walker->Initialize(client, scope, opts, nil);

	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	if (selUtils == nil)
	{
		outNotWalked = true;
		return 0;
	}

	// Required critical section around text-walker selection changes, HELD FOR THE WHOLE CHAPTER -
	// the same deliberate departure from Adobe's examples that KBSSearchEngine explains at length:
	// the section's contents are the keyboard-focus hand-off (spellpanel names it outright in
	// SpellCheckWalker.cpp:85), so entering it per match would run that dance once per replacement.
	// The cost is the same too: cancel is only asked between chapters, never inside one.
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);

	// NO SEQUENCE OF ITS OWN HERE - deliberately. ReplaceChecked opens ONE abortable sequence around
	// the whole run, and the replacements go straight into it.
	//
	// There used to be a per-chapter sequence nested inside that one, on the reasoning that nested
	// sequences are "absorbed by the outer one". That is true of how the Undo MENU reads - only the
	// outermost is named there - but it is not true of what can still be taken back: closing this
	// inner sequence settled the chapter, and the outer abort then had nothing left to undo for it.
	// Cancelling a book replace left every finished chapter replaced while the panel said nothing
	// had changed (measured 2026-07-31, twice - once through the error-state route, once through
	// AbortCommandSequence).
	//
	// What this gives up: a chapter no longer commits or rolls back as a unit of its own. Nothing
	// wanted that - the run is all-or-nothing by design, and the outer sequence is what carries it.

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

	// The rows THIS pass replaced. Their lines are read back once the walk is over (see the pass
	// below it), and this is the list it works from - not "every row in the chapter marked
	// replaced", which would also pick up rows an EARLIER pass replaced, whose stored ranges this
	// pass has very likely moved.
	std::vector<int32> replacedRows;

	while (!targets.empty() && steps < kMaxSteps)
	{
		++steps;

		// Move the run's bar to where this chapter has got to - through DoTask, which also pumps the
		// event queue and is therefore what makes the Cancel button work. A bar driven by SetPosition
		// alone moved perfectly and could not be cancelled at all (measured 2026-07-31, in both
		// engines). Advances smaller than a few hits are swallowed, so this does not run the message
		// loop once per replacement. (spellpanel updates its bar from inside the walk too -
		// SpellReplaceWalker.cpp:496 - so this is where Adobe puts it as well.)
		KBSAdvanceProgress(progressBar, ioProgressReported,
			progressBase + (targetsAtStart - static_cast<int32>(targets.size())));

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
		// refreshed: ReplaceChecked ends by turning the panel into a report of the run
		// (KeepCheckedRows), which keeps the rows it was ASKED about and drops the ones the user
		// had unchecked - so a row left alone here is on its way out of the list anyway, and a pass
		// that replaced nothing has not moved anything to begin with.
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
					// The range ONLY. The command reports the text it wrote, so this is exact - no
					// guessing at the change string's length, which GREP back-references would make
					// impossible anyway - but the line around it is not read until the chapter is
					// finished. See the pass below the walk for why it cannot be read here.
					KBSResultModel::MarkHitReplaced(chapterIdx, hitIdx, replacedStart, replacedEnd);
					replacedRows.push_back(hitIdx);
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

	if (walker->IsWalking())
		walker->Halt();

	// The chapter has stopped changing, so now each replaced row is given the line it ended up on.
	//
	// NOT while the walk was running: matches share paragraphs, and a line read at the moment its
	// own match was written still shows the LATER matches in that paragraph as they were before.
	// Changing every "cat" in "cat and dog and cat" to "kitten" left the first row reading
	// "kitten and dog and cat" for good, while the document read "kitten and dog and kitten"
	// (reported 2026-07-28). Every row now reads the paragraph in its final state.
	//
	// The range each row stored is still the one to read: the walk only ever moves forward, so
	// every replacement after this row's happened LATER in the story and cannot shift its start or
	// end - true whatever the change string's length. Reading it later is also no more work than
	// reading it early, being the same one read per replaced row.
	//
	// Reading, not writing, so it belongs outside the command sequence that has just ended. A run
	// the user cancels does reach this point, and reads text that is about to be rolled back - but
	// the rows are rolled back with it (KBSResultModel::RollBackRows), so nothing of it survives.
	if (!replacedRows.empty())
	{
		IDataBase* const db = docRef.GetDataBase();
		for (size_t r = 0; r < replacedRows.size(); ++r)
		{
			const int32 hi = replacedRows[r];
			UID rowStory = kInvalidUID;
			TextIndex rowStart = kInvalidTextIndex, rowEnd = kInvalidTextIndex;
			if (!KBSResultModel::GetHitReplacedRange(chapterIdx, hi, rowStory, rowStart, rowEnd)
				|| rowStory == kInvalidUID)
				continue;
			PMString pre, match, post;
			KBSSearchEngine::SplitLineAroundMatch(UIDRef(db, rowStory), rowStart, rowEnd,
				pre, match, post);
			KBSResultModel::SetHitSegments(chapterIdx, hi, pre, match, post);
		}
	}

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
			//
			// COUNTED as well, not merely marked. These are checked hits that were not replaced, and
			// the rule this file is built on is that every one of those is named in the summary
			// rather than letting the total quietly come up short (see this function's header, which
			// has always said outMissing covers a row "because the row's turn never came at all").
			// Marking the row and not counting it left a request for ten reading "7 replaced." with
			// nothing to explain the other three, and the explanation sitting on rows the user had to
			// go hunting for.
			for (std::set<int32>::const_iterator t = targets.begin(); t != targets.end(); ++t)
			{
				++outMissing;
				const std::map<int32, int32>::const_iterator row = rowByWalkOrder.find(*t);
				if (row != rowByWalkOrder.end())
					KBSResultModel::SetHitOutcome(chapterIdx, row->second, KBSResultModel::kOutcomeMissing);
			}
		}
	}
	return replacedCount;
}

// The other shape: ONE CHAPTER AT A TIME, for runs that save. Open a chapter, replace it, close its
// sequence, save it, hand it back - then the next one. A run of any size holds at most one chapter
// of its own, which is the whole point: replacing across a twenty-chapter book used to load all
// twenty before the first character was written, which is more than a modest machine has to give
// (user, 2026-08-03: "opening them all is hard on a weak PC").
//
// ***** Cancelling means something different here, and it has to. ***** The all-at-once path can
// put the whole book back because nothing reaches the disk until every chapter is done. This one
// has already overwritten the chapters it finished, and no command sequence can take a saved file
// back - so a cancel stops the run where it stands and leaves what is done, done. The user is
// warned before the run starts (the alert in KBSActionComponent), and the summary says how far it
// got.
//
// Only ever reached with saveAfterReplace true: without a save there would be nothing to close a
// chapter on, and closing an unsaved one would throw its replacements away.
void ReplaceChapterByChapter(RunTotals& io)
{
	const int32 chapterCount = KBSResultModel::GetChapterCount();

	// The bar is sized in HITS, and this path can count them exactly: which chapter a hit belongs to
	// is known from the result model without opening anything. (The SEARCH had to fall back to an
	// equal slice per chapter when it went sequential, because a story count needs an open document.
	// Nothing like that is needed here.)
	int32 totalCheckedHits = 0;
	int32 chaptersWithWork = 0;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		const int32 n = CountCheckedInChapter(ci);
		totalCheckedHits += n;
		if (n > 0)
			++chaptersWithWork;
	}

	// showImmediate = kTrue for the reason the search learned the hard way: with the default the bar
	// waits out an internal delay, and a fast run beats that delay - so the cancel button, the one
	// thing the bar is really there for, never reaches the screen.
	PMString progressTitle("Replacing...");
	progressTitle.SetTranslatable(kFalse);
	RangeProgressBar progressBar(progressTitle, 0, totalCheckedHits, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	int32 progressBase = 0;
	int32 progressReported = 0;
	int32 chapterOrdinal = 0;

	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		const int32 chapterChecked = CountCheckedInChapter(ci);
		if (chapterChecked <= 0)
			continue;		// nothing selected here - do not even open this chapter
		++chapterOrdinal;

		PMString chapterName;
		int32 chapterHits = 0;
		KBSResultModel::GetChapterDisplay(ci, chapterName, chapterHits);
		chapterName.SetTranslatable(kFalse);

		PMString taskLine;
		taskLine.SetTranslatable(kFalse);
		taskLine.Append("Chapter ");
		taskLine.AppendNumber(chapterOrdinal);
		taskLine.Append(" / ");
		taskLine.AppendNumber(chaptersWithWork);
		taskLine.Append(" - ");
		taskLine.Append(chapterName);
		progressBar.SetTaskText(taskLine);
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Cancel is asked at the TOP of each chapter, and answered by the DoTask calls inside the one
		// that is running. A chapter that has started therefore finishes and is saved - which is what
		// keeps the state describable: every chapter this run touched is either untouched or
		// replaced-and-saved, never half done.
		//
		// kFALSE: do NOT raise the global error state. It used to be worth doing when one sequence
		// covered the run; here there is a NEXT sequence to poison, and an error standing when that
		// one ends would roll back a chapter that had nothing wrong with it.
		if (progressBar.WasCancelled(kFalse))
		{
			io.cancelled = true;

			// This chapter and every one after it was never visited. Say so on the chapter ROW:
			// their hits keep their checks (they were asked for and their turn never came), and a
			// ticked row on its own is indistinguishable from a chapter that simply had nothing
			// done to it - which is exactly what was confusing on screen (user's report,
			// 2026-08-03). Only chapters that HAD work are marked; one with nothing checked was
			// never going to be visited anyway.
			//
			// A chapter that is already done has no checked hits left (they were cleared as they
			// were replaced), so this cannot reach backwards over the finished ones.
			for (int32 rest = ci; rest < chapterCount; ++rest)
			{
				if (CountCheckedInChapter(rest) > 0)
					KBSResultModel::SetChapterNotReached(rest);
			}
			break;
		}

		// ----- open this one chapter -----
		UIDRef docRef;
		IDFile file;
		if (!KBSResultModel::GetChapterLocation(ci, docRef, file))
		{
			progressBase += chapterChecked;
			KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
			continue;
		}

		// It may well be closed: the SEARCH hands every chapter back as soon as it has walked it
		// (2026-08-02), so by the time a replace runs, the chapters it wants are normally shut. Bring
		// it back the way a jump does, and rebind the model to the live database.
		if (!KBSBookScope::IsDocStillOpen(docRef))
		{
			UIDRef reopened;
			if (!KBSBookScope::ReopenChapterDoc(file, reopened))
			{
				++io.chaptersSkipped;
				if (!io.haveFirstSkipped)
				{
					int32 skippedHits = 0;
					KBSResultModel::GetChapterDisplay(ci, io.firstSkipped, skippedHits);
					io.firstSkipped.SetTranslatable(kFalse);
					io.haveFirstSkipped = true;
				}
				progressBase += chapterChecked;
				KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
				continue;
			}
			docRef = reopened;
			KBSResultModel::RebindChapterDoc(ci, reopened);
		}

		// Read BEFORE anything is written to it - that is the whole point of the record. Only used if
		// this chapter's own sequence has to be rolled back below.
		IDataBase* const chapterDB = docRef.GetDataBase();
		const bool wasModified = (chapterDB != nil) && (chapterDB->IsModified() != kFalse);

		// ----- replace it -----
		// Remember the rows this CHAPTER is about to change, so the panel can be put back with the
		// text if its sequence is rolled back. Scoped to the chapter, unlike the all-at-once path
		// where one backup covers the whole run: the chapters already finished here are on disk, and
		// their rows are a true record of what is in those files.
		KBSResultModel::BeginRowBackup();

		bool aborted = false;
		{
			// ABORTABLE for the same reason the other path uses one: the error-code route does not
			// carry a rollback (measured 2026-07-31), so it has to be stated outright. Per chapter
			// here, because a chapter is settled the moment it is saved.
			IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KBS Replace");

			bool stepLimit = false;
			bool notWalked = false;
			int32 missing = 0;
			int32 locked = 0;
			int32 refused = 0;
			const int32 replaced = ReplaceInChapter(ci, docRef, stepLimit, missing, locked, refused,
				notWalked, &progressBar, progressBase, progressReported);

			io.missing += missing;
			io.locked += locked;
			io.refused += refused;
			if (stepLimit)
				++io.chaptersStepLimited;
			if (notWalked)
			{
				// The walk never started here, so no row of this chapter carries a reason - the
				// chapter itself has to be named.
				++io.chaptersNotWalked;
				if (!io.haveFirstNotWalked)
				{
					int32 notWalkedHits = 0;
					KBSResultModel::GetChapterDisplay(ci, io.firstNotWalked, notWalkedHits);
					io.firstNotWalked.SetTranslatable(kFalse);
					io.haveFirstNotWalked = true;
				}
			}

			// An error standing when a sequence ends rolls that sequence back. On this path there is
			// a LATER sequence to poison as well, so the state is settled here either way.
			if (seq != nil)
			{
				if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
				{
					CmdUtils::AbortCommandSequence(seq);
					aborted = true;
				}
				else
					CmdUtils::EndCommandSequence(seq);
				seq = nil;
			}
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);

			if (!aborted)
			{
				io.replaced += replaced;
				if (replaced > 0)
					++io.chaptersTouched;
			}
		}

		progressBase += chapterChecked;
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		if (aborted)
		{
			// Nothing survived in this chapter after all, so the panel must not claim otherwise.
			// AbortCommandSequence restores the TEXT but leaves the database marked modified, so the
			// flag goes back too - but only when the chapter was clean when this run found it.
			KBSResultModel::RollBackRows();
			if (chapterDB != nil && !wasModified)
				chapterDB->SetModified(kFalse);
			continue;		// not saved and not closed: there is nothing of ours in it
		}
		KBSResultModel::ForgetRowBackup();

		// ----- save it, now that its sequence is closed -----
		// Deliberately outside the sequence: a save cannot be undone, so it has no business inside an
		// abortable one. kSuppressUI because this chapter usually has no window, and a save prompt on
		// one of those would be a modal dialog with nothing behind it. A document that has never been
		// saved has no file to write to and fails here - reported, not asked about.
		const ErrorCode saveErr = Utils<IDocumentCommands>()->Save(docRef, kSuppressUI);
		// ***** Cleared every time. ***** An error left standing here does not merely outlive this
		// call - it would roll back the NEXT chapter's sequence.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		if (saveErr == kSuccess)
		{
			++io.chaptersSaved;
			// ----- hand it back -----
			// Only if this plug-in opened it: ReleaseHeldDoc checks the held list itself, so a
			// chapter the user already had open passes through untouched - keeping its window, and
			// its undo. "Hide Previous Chapter" is NOT consulted; that toggle is about jumping, and
			// a run that saves has no reason to hold anything (user, 2026-08-03, matching the same
			// decision made for the search on 2026-08-02).
			KBSBookScope::ReleaseHeldDoc(docRef);
			io.chaptersClosed = true;
			continue;
		}

		++io.chaptersNotSaved;
		if (!io.haveFirstNotSaved)
		{
			// Named from the model rather than from the file: the model's name is the one the user
			// just read in the panel.
			int32 notSavedHits = 0;
			KBSResultModel::GetChapterDisplay(ci, io.firstNotSaved, notSavedHits);
			io.firstNotSaved.SetTranslatable(kFalse);
			io.haveFirstNotSaved = true;
		}
		// It keeps its replacements and cannot be closed on them, so the user is given a window to
		// deal with it in. NOT opened here - see where windowsToOpen is consumed.
		io.windowsToOpen.push_back(docRef);
	}
}

// The status line, from the counters alone.
//
// Split out from ReplaceChecked when a second way through the run arrived (the chapter-at-a-time
// path): both paths end here, so the wording is written once and cannot drift apart. It reads
// RunTotals and nothing else - no document, no model, no session state - which is what makes that
// possible.
//
// Every checked hit that was not replaced is named here rather than being allowed to make the total
// quietly come up short. That rule is what most of these branches exist for.
void BuildSummary(const RunTotals& t, bool saveAfterReplace, PMString& outSummary)
{
	// ***** What a cancel MEANS depends on which path ran. ***** Without a save, the whole run was
	// rolled back - the text through the aborted sequence, and the panel with it - so there is
	// nothing to account for and the sentence can be absolute. With one, the chapters that finished
	// are on disk and cannot be taken back by anything, so the count has to be stated.
	if (t.cancelled && !saveAfterReplace)
	{
		outSummary.Append("Replace cancelled - nothing was changed.");
		return;
	}

	if (t.cancelled)
	{
		// The count leads here too - it is the one thing the user needs to know. What follows is
		// appended by the branches below, exactly as on a run that finished.
		outSummary.Append("Replace cancelled - ");
		outSummary.AppendNumber(t.replaced);
		outSummary.Append(" replaced and saved in ");
		outSummary.AppendNumber(t.chaptersTouched);
		outSummary.Append(" chapter(s) before it stopped.");
	}
	else
	{
		// The count leads, so it survives the narrow status field's tail truncation.
		outSummary.AppendNumber(t.replaced);
		outSummary.Append(" replaced in ");
		outSummary.AppendNumber(t.chaptersTouched);
		outSummary.Append(" chapter(s).");
	}

	// Urge a save only when something was actually written AND the run did not do it itself. A run
	// where every checked row came back missing, locked or refused leaves every file exactly as it
	// found it, and "check them and save yourself" there reads as though something HAD been changed
	// - at the very moment the user is already wondering what became of their hits.
	if (t.replaced > 0 && !saveAfterReplace)
		outSummary.Append(" Not saved - check them and save yourself.");

	// NOT after a cancel: the opening sentence there has already said how many chapters were saved
	// ("2400 replaced and saved in 2 chapter(s)"), and a second "2 chapter(s) saved." right behind
	// it reads as a separate fact rather than the same one twice.
	if (t.chaptersSaved > 0 && !t.cancelled)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersSaved);
		outSummary.Append(" chapter(s) saved.");
	}

	// A file that could not be written is the one outcome here the user has to act on, so it is
	// named and the reason is guessed out loud. Two ways this happens: the file is read-only, or the
	// document has never been saved at all - no file to write to, and the prompt that would ask for
	// one is suppressed on purpose.
	if (t.chaptersNotSaved > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersNotSaved);
		outSummary.Append(" chapter(s) could not be saved (\"");
		outSummary.Append(t.firstNotSaved);
		outSummary.Append("\" first) - read-only or never saved?");
	}

	// Say that the desk was cleared - and, more importantly, say when it was NOT. A user who ticked
	// the box expecting the chapters to go away has to be told why any are still there.
	//
	// The two are no longer alternatives: the chapter-at-a-time path closes each chapter as it saves
	// it, so a run can perfectly well have closed three and left the fourth standing because its save
	// failed. Both sentences then apply, and both are wanted.
	if (t.chaptersClosed)
		outSummary.Append(" Chapters this plug-in opened were closed.");
	if (saveAfterReplace && t.chaptersNotSaved > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersNotSaved);
		outSummary.Append(" chapter(s) left open - they could not be saved.");
	}

	if (t.chaptersSkipped > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersSkipped);
		outSummary.Append(" chapter(s) could not be opened (\"");
		outSummary.Append(t.firstSkipped);
		outSummary.Append("\" first) - moved, deleted, or in use?");
	}

	// Checked rows whose text no longer reads the way the panel says. Not an error and not a
	// failure to line up - the row came up exactly where it was expected, the TEXT there had
	// changed - so it is reported on its own terms.
	if (t.missing > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.missing);
		outSummary.Append(" hit(s) not found - the text is no longer where the search left it.");
	}

	// Checked rows on a locked layer or in a locked story. Not a failure either: InDesign's own
	// Find/Change searches those when asked to and then refuses to change them ("Search Only"), and
	// this reports the same outcome rather than letting the count quietly come up short.
	if (t.locked > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.locked);
		outSummary.Append(" hit(s) left alone - locked layer or story (those can be searched, not changed).");
	}

	// Checked rows the replace command itself would not run on. The one entry in this list that is
	// a real failure rather than a deliberate decline, so it is worded as one - and reported at all,
	// which is the point: the alternative is a replaced total that comes up short in silence.
	if (t.refused > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.refused);
		outSummary.Append(" hit(s) could not be changed - InDesign refused the change there.");
	}

	// Same symptom, different cause: the walk was cut off by its own safety ceiling. Saying
	// "edited since the search?" here would send the user looking for the wrong thing.
	if (t.chaptersStepLimited > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersStepLimited);
		outSummary.Append(" chapter(s) stopped at the safety limit - does the change text contain the find text?");
	}

	// Chapters the text walker would not run on at all. Unlike every case above, nothing on their
	// rows explains it - the walk never got far enough to say anything about a single one - so the
	// chapter is named here instead. The SEARCH reports the same failure the same way; without this
	// the replace passed over such a chapter without a word.
	if (t.chaptersNotWalked > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersNotWalked);
		outSummary.Append(" chapter(s) could not be searched (\"");
		outSummary.Append(t.firstNotWalked);
		outSummary.Append("\" first) - nothing was written there.");
	}
}

} // anonymous namespace

int32 KBSReplaceEngine::ReplaceChecked(PMString& outSummary, bool saveAfterReplace)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// Re-entry stop, ahead of every other question. The panel greys its actions out while a replace
	// runs, but the progress bar below pumps events, so a command can still be dispatched into this
	// function - and unlike the search, this one holds an open command sequence while it works: a
	// second run underneath the first would nest a sequence inside it and Halt() the outer run's
	// walker in the middle of its walk.
	if (gReplacing)
	{
		outSummary.Append("A replace is already running.");
		return 0;
	}
	// ...and the same door for anything ELSE of ours - a search, or either scan. Asked separately so
	// each keeps the message that is actually true. It matters more here than anywhere: this run
	// holds an open command sequence, and a scan cancelled underneath it hands back the very
	// chapters being written to (see KBSRunGuard).
	if (KBSRunGuard::IsAnyRunning())
	{
		outSummary.Append(KBSRunGuard::BusyMessage());
		return 0;
	}
	const ReplacingFlagGuard replacingGuard;

	const int32 chapterCount = KBSResultModel::GetChapterCount();
	if (chapterCount <= 0)
	{
		outSummary.Append("No results to replace - run a search first.");
		return 0;
	}
	// The panel is a report of what the LAST replace did, not a work list. Asked first, because a
	// report can still hold checked rows: the ones the run never reached keep their check so the
	// report can account for them, and they are exactly what a second run would go after - matches
	// the user has no box to select or clear anywhere on screen.
	//
	// The menu greys the command out for the same reason (KBSActionComponent::UpdateActionStates).
	// This is the same door on the far side of it, for a caller that never went through the menu -
	// a script invoking the action reaches this function whatever state the menu is in.
	if (KBSResultModel::IsShowingReplaceOutcome())
	{
		outSummary.Append("This is the last replace's report - search again to replace more.");
		return 0;
	}
	if (KBSResultModel::GetCheckedCount() <= 0)
	{
		outSummary.Append("Nothing checked.");
		return 0;
	}

	// The Find/Change tab has to be the one the SEARCH ran in. Every chapter below is RE-WALKED, and a
	// walk in another mode returns another set of matches - so the stored walk orders would line up
	// with occurrences the user never saw. Nothing wrong would be written (MatchIsSameOccurrence
	// refuses each one), but the entire run would come back "missing" with nothing on screen to
	// explain why. Better to say it before anything is opened.
	{
		const int32 searchedMode = KBSResultModel::GetSearchMode();
		InterfacePtr<IFindChangeOptions> modeOpts(QuerySessionPreferences<IFindChangeOptions>());
		const int32 currentMode = (modeOpts != nil) ? static_cast<int32>(modeOpts->GetSearchMode()) : -1;
		if (searchedMode >= 0 && currentMode >= 0 && currentMode != searchedMode)
		{
			outSummary.Append("The Find/Change dialog is on a different tab than when this search ran - search again before replacing.");
			return 0;
		}

		// The three tabs this panel walks. Anything else searches by attribute through a walker of its
		// own and never produced these rows in the first place, so there is nothing here to rewrite.
		if (searchedMode >= 0
			&& searchedMode != IFindChangeOptions::kTextSearch
			&& searchedMode != IFindChangeOptions::kGrepSearch
			&& searchedMode != IFindChangeOptions::kGlyphSearch)
		{
			outSummary.Append("These results did not come from the Text, GREP or Glyph tab, so they cannot be replaced here - InDesign's own Find/Change can change them.");
			return 0;
		}
	}

	// State the tab before anything is walked, exactly as the search does. A walk runs in the mode
	// last COMMITTED, not the one IFindChangeOptions reports, so without this a replace ran as plain
	// Text whatever tab was on screen - which is how a Glyph-tab search came to be overwritten with
	// the TEXT tab's change string. See KBSSearchEngine::CommitSearchMode.
	//
	// Deliberately here, OUTSIDE the command sequence opened further down: this processes a command of
	// its own, and a session-setting command inside that sequence would become part of the undo step.
	KBSSearchEngine::CommitSearchMode();

	// ...and on the Glyph tab, the glyph that will be WRITTEN. Stated only here, never on the search
	// path, so a search can never leave a change glyph set behind the user's back. An EMPTY Change To
	// box is stated too, not refused - it means "delete every match", the same as an empty change
	// string on the Text tab. false now means only that the Find/Change settings could not be read at
	// all. Also outside the sequence, and before it opens, so nothing has been written yet.
	if (!KBSSearchEngine::CommitReplaceGlyph())
	{
		outSummary.Append("Find/Change settings are unavailable - nothing was changed.");
		return 0;
	}

	// The whole account of this run - every counter the summary reads, in one structure. It used to
	// be seventeen locals here. They were gathered up when a SECOND way through this function
	// arrived (the chapter-at-a-time path, 2026-08-03): both have to hand the same account to the
	// same summary builder, and a counter that lives in only one of them is a sentence the other
	// silently cannot say.
	//
	// totals.cancelled is set when the user stops the run from the progress bar. On THIS path that
	// means the document is given back as it was: the command sequence rolls the text back, and the
	// result model is rolled back with it, so the panel returns to being the search's results.
	// Nothing is half done.
	RunTotals totals;

	// ***** Two ways through from here, and saving is what decides between them. *****
	//
	// A chapter that has been written to disk is settled: it can be handed straight back, and the
	// run never has to hold more than one at a time. A chapter that has NOT been saved has to stay
	// open - closing it would throw its replacements away - so a run that does not save has no
	// choice but to open every chapter it touches and keep them all.
	//
	// The difference the user sees is what Cancel does. Not saving: one sequence around everything,
	// so a cancel puts the whole book back. Saving: a sequence per chapter, so a cancel stops the
	// run and leaves the finished chapters finished. The confirmation warns about that before the
	// run starts.
	if (saveAfterReplace)
	{
		ReplaceChapterByChapter(totals);

		// ***** Windows are opened here, after ALL the replacing is over - never between chapters.
		// ***** Measured 2026-07-28: a kOpenLayoutCmdBoss processed between two chapters'
		// replacements discarded the undo history of the chapters already done, leaving their text
		// replaced with nothing to undo it with. The chapters this path saved are past caring - they
		// are closed - but the ones whose save FAILED stay open, and two of those would be exactly
		// the measured case.
		for (size_t wi = 0; wi < totals.windowsToOpen.size(); ++wi)
			KBSBookScope::ShowChapterWindow(totals.windowsToOpen[wi]);

		// ***** Reached even on a cancel, and that is the requirement. ***** (User, 2026-08-03: "if
		// it was saved, the saved part should still be shown in the results even when I cancel.")
		// The chapters this run finished are on disk, so their replaced rows are a true record of
		// what is in those files and the panel has to keep showing them. The rows the run never
		// reached keep their check, which KeepCheckedRows already allows for.
		//
		// The other path returns before this, because there a cancel really did undo everything.
		KBSResultModel::KeepCheckedRows();
		BuildSummary(totals, saveAfterReplace, outSummary);
		return totals.replaced;
	}

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
	// How much work the run has to get through - what the bar is sized with. Counted BEFORE anything
	// is opened, because the bar has to be up while the chapters are being opened: that is the slow
	// part when the user has closed the windows the search was holding.
	//
	// HITS, not chapters. A chapter is a coarse unit: one chapter of 5000 hits and one of 3 both
	// counted as a single step, so the bar stood still through the long one. Hits are the work.
	// (A chapter count was taken alongside this until 2026-08-02 and never read - the bar is sized
	// in hits, and the chapters that hold them are counted again as they are resolved, below.)
	int32 totalCheckedHits = 0;
	for (int32 ci = 0; ci < chapterCount; ++ci)
		totalCheckedHits += CountCheckedInChapter(ci);

	// The progress bar. Shown for BOTH scopes since 2026-07-31 (user's request), matching the
	// search. It used to be book scope only, on the reasoning that a one-document replace is a
	// single step with nothing to cancel between - but the bar is sized in HITS, not chapters, so a
	// single document with thousands of them takes just as long and had no way to be stopped.
	// DisableChildProgressBars keeps the chapter opens in the resolve pass below from raising bars
	// of their own.
	//
	// SIZED IN HITS, not chapters, and moved by ReplaceInChapter as it goes (progressBase below).
	// The walker will not report progress for us: ITextWalkerProgressMonitor is only a place to PARK
	// a bar - the client's own OnNextPosition is what calls SetPosition on it, and the stock
	// kFindChangeClientBoss does not (measured 2026-07-31: registered fine, 5270 replacements, zero
	// calls). spellpanel gets its moving bar because it walks with a client it wrote itself. So KBS
	// counts its own work, which it can do better than the walker anyway: the number of checked hits
	// is known before the run starts.
	//
	// showImmediate = kTrue for the reason the search learned the hard way: with the default
	// the bar waits out an internal delay, and a fast run beats that delay - so the cancel
	// button, the one thing the bar is really there for, never reaches the screen.
	PMString progressTitle("Replacing...");
	progressTitle.SetTranslatable(kFalse);
	RangeProgressBar progressBar(progressTitle, 0, totalCheckedHits, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	std::vector<PendingChapter> pending;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		if (CountCheckedInChapter(ci) <= 0)
			continue;		// nothing selected here - do not even open this chapter

		// Past this line the chapter is one of the chaptersWithWork the bar was sized with, so it
		// joins the list whatever happens below. A chapter that cannot be opened is still a step
		// the bar has to take: dropping it here is what used to leave the bar short of its own
		// total, stopping at 4 of 5 with nothing left to do.
		PendingChapter chapter;
		chapter.chapterIdx = ci;

		UIDRef docRef;
		IDFile file;
		if (!KBSResultModel::GetChapterLocation(ci, docRef, file))
		{
			pending.push_back(chapter);		// unopened - the loop below only counts it past
			continue;
		}

		// The chapter may have been closed since the search (a held window the user shut). Bring
		// it back the way a jump does, and rebind the model to the live database.
		if (!KBSBookScope::IsDocStillOpen(docRef))
		{
			UIDRef reopened;
			if (!KBSBookScope::ReopenChapterDoc(file, reopened))
			{
				++totals.chaptersSkipped;
				if (!totals.haveFirstSkipped)
				{
					int32 chapterHits = 0;
					KBSResultModel::GetChapterDisplay(ci, totals.firstSkipped, chapterHits);
					totals.firstSkipped.SetTranslatable(kFalse);
					totals.haveFirstSkipped = true;
				}
				// Moved, deleted, or in use: counted and named just above, and kept in the list
				// unopened so the bar still takes its step for it.
				pending.push_back(chapter);
				continue;
			}
			docRef = reopened;
			KBSResultModel::RebindChapterDoc(ci, reopened);
		}

		chapter.docRef = docRef;
		chapter.opened = true;
		// Read BEFORE anything is written to it - that is the whole point of the record.
		{
			IDataBase* const chapterDB = docRef.GetDataBase();
			chapter.wasModified = (chapterDB != nil) && (chapterDB->IsModified() != kFalse);
		}
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
	// ABORTABLE, and that is the whole point of choosing this kind over a plain SequencePtr.
	//
	// A regular sequence decides between commit and rollback ONLY by looking at the global error
	// code as it ends (ICommandSequence.h:145-147). KBS relied on that: cancel raised the error
	// state through WasCancelled(kTrue) and the sequence was expected to put the book back. It did
	// not. Measured 2026-07-31 with the error code printed at both points: it was raised at the
	// cancel (2) and STILL raised when the sequence closed (2) - and 1622 replacements stayed in the
	// document anyway, while the panel said "nothing was changed". The error-code route does not
	// carry a rollback across the several documents a book replace touches; the header only ever
	// promises "the database", singular.
	//
	// So the cancel is now stated outright instead of being implied: AbortCommandSequence below.
	// That is what Adobe's own Change All does (spellpanel/SpellReplaceWalker.cpp:896-902), and the
	// header points at this class for exactly this case. It costs performance - the header says to
	// use it only where necessary - which is why it is here and not around every chapter.
	IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KBS Replace");
	// DELIBERATELY UNNAMED (user's call, 2026-07-28). SetName is what Edit > Undo would say after
	// the word "Undo"; leaving it unset lets InDesign word the step the way it words its own.
	// To put the name back: seq->SetName(PMString(kKBSReplaceSequenceName, PMString::kUnknownEncoding));

	// How many hits the bar has behind it. The bar is sized in hits, so each chapter starts where
	// the last one ended and moves the bar itself as it goes. progressReported is how far it has
	// actually been advanced - DoTask takes a difference, so that has to be carried along.
	int32 progressBase = 0;
	int32 progressReported = 0;

	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		// Counted BEFORE the chapter runs: afterwards these hits are marked replaced and would count
		// as zero, leaving the bar short of its own total.
		const int32 chapterChecked = CountCheckedInChapter(pending[pi].chapterIdx);

		PMString taskLine;
		taskLine.SetTranslatable(kFalse);
		taskLine.Append("Chapter ");
		taskLine.AppendNumber(static_cast<int32>(pi) + 1);
		taskLine.Append(" / ");
		taskLine.AppendNumber(static_cast<int32>(pending.size()));

		PMString chapterName;
		int32 chapterHits = 0;
		KBSResultModel::GetChapterDisplay(pending[pi].chapterIdx, chapterName, chapterHits);
		chapterName.SetTranslatable(kFalse);
		taskLine.Append(" - ");
		taskLine.Append(chapterName);
		// The chapter's name goes on the status line WITH its number. The POSITION is moved separately
		// through KBSAdvanceProgress - SetTaskText only writes text.
		progressBar.SetTaskText(taskLine);
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Cancel is asked here, and answered by the DoTask calls inside the chapter. WasCancelled only
		// reads a flag; what SETS it is the event queue being pumped, which DoTask does.
		//
		// kFALSE: do NOT raise the global error state. It used to be kTrue, because the error state
		// was the mechanism - a regular sequence rolls back when it ends with an error standing. It
		// did not work across a book's several documents (measured 2026-07-31, see the sequence
		// above), and now that the sequence is aborted outright the error state is not needed. Worse
		// than not needed: it would still be standing while AbortCommandSequence runs, and would
		// then fail whatever the application does next.
		//
		// Cancelling means ONE thing (user's call, 2026-07-28): the whole run is undone. Keeping the
		// finished chapters would leave the book half changed with nothing on screen saying where
		// the line fell. The cost is that the work done so far is thrown away - breaking off a
		// 900-of-1000 run starts over.
		if (progressBar.WasCancelled(kFalse))
		{
			totals.cancelled = true;
			break;
		}

		// A chapter the resolve pass could not open. It is in this list for the bar's sake and for
		// nothing else - it was counted and named in the summary where the opening failed - so its
		// hits are counted past here and it is skipped.
		if (!pending[pi].opened)
		{
			progressBase += chapterChecked;
			continue;
		}

		const int32 ci = pending[pi].chapterIdx;
		const UIDRef& docRef = pending[pi].docRef;

		bool stepLimit = false;
		bool notWalked = false;
		int32 missing = 0;
		int32 locked = 0;
		int32 refused = 0;
		const int32 replaced = ReplaceInChapter(ci, docRef, stepLimit, missing, locked, refused,
			notWalked, &progressBar, progressBase, progressReported);
		progressBase += chapterChecked;
		// Land exactly on the chapter boundary: a chapter that finished early (nothing left to line
		// up, or the safety ceiling) must still hand the bar on at the right place.
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
		totals.replaced += replaced;
		totals.missing += missing;
		totals.locked += locked;
		totals.refused += refused;
		if (replaced > 0)
			++totals.chaptersTouched;
		if (stepLimit)
			++totals.chaptersStepLimited;
		if (notWalked)
		{
			// The walk never started here, so no row of this chapter carries a reason - the chapter
			// itself has to be named, the way the resolve pass names one it could not open.
			++totals.chaptersNotWalked;
			if (!totals.haveFirstNotWalked)
			{
				int32 notWalkedHits = 0;
				KBSResultModel::GetChapterDisplay(ci, totals.firstNotWalked, notWalkedHits);
				totals.firstNotWalked.SetTranslatable(kFalse);
				totals.haveFirstNotWalked = true;
			}
		}

		// A chapter that received a replacement wants a real window, so the change is visible and
		// can be undone - or saved - by hand. Chapters nothing landed in stay as they were:
		// opening windows on untouched documents would only be clutter. Nothing is saved here.
		// The window is not opened yet - see the loop after the sequence.
		if (replaced > 0)
			touched.push_back(docRef);
	}

	// ASK ONCE MORE, now that the loop is over.
	//
	// The test inside the loop sits at the TOP of each pass, so it only ever sees a cancel that
	// arrived while an EARLIER chapter was running. A cancel pressed during the LAST chapter had no
	// next pass to be noticed in, and the run finished as though nothing had been asked - which is
	// exactly what "cancelling works in the first document but not across documents" was (user's
	// observation, 2026-07-31; a one-chapter book could never be cancelled at all).
	//
	// The work is already done by this point, so this changes nothing about what was written - but it
	// is what decides between committing that work and throwing it away, which is the whole promise
	// of the button.
	if (!totals.cancelled && progressBar.WasCancelled(kFalse))
		totals.cancelled = true;

	// The sequence ends HERE, and HOW it ends is the cancel. Aborting is a statement - "undo
	// everything this sequence did" - where ending it only offers the changes up and lets the error
	// state decide. Either way the sequence must not be touched again afterwards
	// (ICommandSequence.h:153).
	if (seq != nil)
	{
		if (totals.cancelled)
			CmdUtils::AbortCommandSequence(seq);
		else
			CmdUtils::EndCommandSequence(seq);
		seq = nil;
	}

	}	// end of the block the sequence lived in

	if (totals.cancelled)
	{
		// The abort has just rolled the text back to where the run found it. The panel recorded
		// those replacements as they happened, so it has to shed them too - otherwise it would show
		// replaced rows sitting over text that is once again the original.
		//
		// What is left is exactly what the search produced: a work list with its checks intact,
		// ready to be run again. No window is opened either - nothing was changed to look at.
		KBSResultModel::RollBackRows();

		// Belt and braces: the cancel no longer raises the error state, but a command that failed
		// inside the run might have left one standing, and it must not outlive this function.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		// Put the "unsaved" flags back. AbortCommandSequence restores the TEXT but leaves every
		// database it touched marked modified, so a cancelled run left the chapters asking to be
		// saved with nothing in them to save. (Undo does clear the flag - the application handles
		// that itself - which is how the difference showed up.)
		//
		// Only for chapters that were clean when the run found them: one the user had already edited
		// is still edited, and claiming otherwise would risk their work.
		for (size_t pi = 0; pi < pending.size(); ++pi)
		{
			if (!pending[pi].opened || pending[pi].wasModified)
				continue;
			IDataBase* const chapterDB = pending[pi].docRef.GetDataBase();
			if (chapterDB != nil)
				chapterDB->SetModified(kFalse);
		}

		// The panel is back to being the search's results, so there is no report to turn it into -
		// KeepCheckedRows is deliberately NOT called on this exit. The wording is left to
		// BuildSummary, which is the only place that knows what a cancel means on each path.
		BuildSummary(totals, saveAfterReplace, outSummary);
		return 0;
	}
	KBSResultModel::ForgetRowBackup();

	// NOTHING IS SAVED ON THIS PATH, and nothing is closed. That is not a decision taken here - it
	// is what the path IS: a run that saves went the other way at the fork above, because saving is
	// the only thing that makes a chapter safe to close. Everything this run touched therefore stays
	// open and unsaved, and the summary tells the user to deal with it.
	//
	// (Until 2026-08-03 the saving and the closing lived here, guarded by `if (saveAfterReplace)`.
	// They moved into ReplaceChapterByChapter, where each chapter is saved and handed back the
	// moment its own sequence closes.)

	// Windows are opened AFTER the sequence, never from inside it - the other half of the pair the
	// resolve pass above makes: no document and no window is opened while the sequence is standing.
	// Measured 2026-07-28: a kOpenLayoutCmdBoss processed between two chapters' replacements
	// discarded the undo history of the chapters already done - their text stayed replaced with
	// nothing left to undo it with. Opening the windows once everything is committed keeps that
	// command clear of the replacements.
	//
	// Every chapter a replacement landed in gets one: the change has to be visible, because it is
	// the user who has to save it.
	for (size_t i = 0; i < touched.size(); ++i)
		KBSBookScope::ShowChapterWindow(touched[i]);

	// The panel now becomes a REPORT of what the replace did: the rows it changed, and the rows it
	// was asked about and left alone, each saying why on its locator. The rows the user had
	// unchecked are dropped - they were never part of the request. A replace that was asked for
	// nothing at all leaves the results exactly as they were.
	KBSResultModel::KeepCheckedRows();

	BuildSummary(totals, saveAfterReplace, outSummary);
	return totals.replaced;
}

bool KBSReplaceEngine::IsReplacing()
{
	return gReplacing;
}

// End, KBSReplaceEngine.cpp.
