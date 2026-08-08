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
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"
#include "ISession.h"				// GetExecutionContextSession

// General includes:
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss / kTWReplaceTextCmdBoss / kFindChangeClientBoss
#include "WalkerScopeOptions.h"
#include "CAlert.h"					// the edited-chapter warning, asked as each chapter is opened
#include "CmdUtils.h"				// commands and command sequences
#include "CoreResTypes.h"			// kLineSeparatorString - that alert composes its own line breaks
#include "CreateObject.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
// (ITextModel.h was here for GetTextChangeCount, which fed the trusted-story fast path. Removed
// 2026-08-03 with that path - see the note over MatchStillStandsHere.)
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "ProgressBar.h"		// RangeProgressBar - the replace's progress + cancel, as the search does it
#include "StringUtils.h"			// ::ReplaceStringParameters - fills the ^1 in a translated string
#include "Utils.h"

#include <map>
#include <set>
#include <vector>

// Project includes:
#include "KBSReplaceEngine.h"
#include "KBSEditStamp.h"		// has this chapter been edited since the search walked it?
#include "KBSID.h"				// the string keys the edited-chapter warning is worded from
#include "KBSLoc.h"				// runtime Japanese - the jaJP string table is gone (2026-08-05)
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

// Run one find/change walker command and hand back WHAT IT ANSWERED, not merely whether it landed
// on something. Only kSuccess fills the story and range; every other answer leaves them invalid,
// which is the header's own contract (IFindChangeService.h:46-49) rather than a convention here:
//
//   kSuccess         a match, with its position
//   kNotFound        no match - the walk is over
//   kFoundCompleted  the walk is over AND at least one match came up along the way. Start and end
//                    are kInvalidTextIndex, so there is nothing here to act on either.
//   kFailure         an ERROR. The walk did not finish, it broke off.
//   kReplaceAllCompleted
//                    a Change All finished (IFindChangeService.h:81-84). NEVER reachable from this
//                    function: that answer belongs to kReplaceAllTextCmdBoss, and KBS does not run
//                    it - the official Change All takes no subset of the matches, which is the
//                    whole reason this engine walks match by match. Listed anyway because the enum
//                    has FIVE members (:43), and a table naming four of them reads as if it were
//                    the whole contract. The four above are the ones this function can produce.
//
// ***** THE LAST ONE IS WHY THIS RETURNS A RESULT AND NOT A BOOL. ***** It used to answer true or
// false, so a chapter whose search failed halfway looked exactly like a chapter whose matches were
// gone, and its remaining rows were reported as 'missing' - "not found when the chapter was
// searched again", which is a statement about the DOCUMENT and was not true. The official loop
// keeps the distinction for the same reason: SnpFindAndReplace.cpp:796 returns the result untouched
// and its caller (:642-670) turns kFailure, and only kFailure, into a failure.
//
// A command that fails leaves an error on the thread's global error state. It is cleared here on
// purpose: left standing it would make the surrounding command sequence roll back (taking the
// successful replacements with it) and would block later commands in the session.
IFindChangeService::FindChangeResult RunWalkerCmd(const ClassID& cmdBoss, ITextWalker* walker,
	UIDRef& outStory, TextIndex& outStart, TextIndex& outEnd)
{
	outStory = UIDRef();
	outStart = kInvalidTextIndex;
	outEnd = kInvalidTextIndex;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
	if (cmd == nil)
		return IFindChangeService::kFailure;
	InterfacePtr<IFindChangeCmdData> cmdData(cmd, UseDefaultIID());
	if (cmdData == nil)
		return IFindChangeService::kFailure;
	cmdData->SetTextWalker(walker);

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return IFindChangeService::kFailure;
	}
	// GetReplacementCount is never updated by these commands, so the result code is the only
	// signal that something actually happened.
	const IFindChangeService::FindChangeResult result = cmdData->GetFindChangeResult();
	if (result != IFindChangeService::kSuccess)
	{
		// Clear here too, for the same reason as above. A command can report kSuccess and still
		// leave an error standing, and an error standing when EndCommandSequence runs rolls the
		// whole chapter back - including the replacements that did work.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return result;
	}

	outStory = cmdData->GetRange(outStart, outEnd);
	return result;
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
	// Was this chapter already unsaved BEFORE the run touched it? Asked so that the flag can be put
	// back for a chapter this run leaves with nothing in it - on a CANCEL, where
	// AbortCommandSequence restores the text but not the flag (a cancelled run left every chapter it
	// had reached looking unsaved with nothing to save; a real Undo does clear it, the application
	// does that itself, which is what made the difference visible), and on the way out of a run that
	// went through, for a chapter no replacement landed in (see HandBackChaptersWithNothingInThem).
	// Chapters that were already dirty stay dirty; that was not ours to change.
	bool	wasModified;

	// Did a replacement actually land here? A chapter that got one is the user's to look at and save
	// (it is given a window and left open); a chapter that got none has nothing in it and is handed
	// straight back, because it is holding its .indd locked for no reason at all.
	bool	tookReplacement;

	// Has this chapter's text been edited since the search walked it (KBSEditStamp)?
	//
	// ***** READ IN THE RESOLVE PASS, AND THAT IS THE ONLY PLACE IT CAN BE READ FOR EVERY CHAPTER.
	// ***** IsChapterCurrent needs the chapter OPEN, and a book search closes each one as it finishes
	// with it - so before the resolve pass, a chapter the user had closed cannot be asked at all. The
	// confirmation prompt could only ever ask about the chapters that happened to be open, which is
	// why the question moved here on 2026-08-08 (user's decision) and left the prompt entirely.
	//
	// Read after the chapter is a live document again and before a character is written to it.
	bool	editedSinceSearch;

	// This chapter's checked, not-yet-replaced hits, counted ONCE where the run is sized and read
	// from here after that (how far this chapter's slice moves the bar). The number cannot change
	// in between: the panel's actions are greyed out for the whole run, the bar is modal, and a
	// chapter's own hits are only marked replaced as IT runs. Until 2026-08-07 every reading
	// counted the chapter's rows over again.
	int32	checkedCount;

	PendingChapter() : chapterIdx(-1), opened(false), wasModified(false), tookReplacement(false),
		editedSinceSearch(false), checkedCount(0) {}
};

// ***** A CountCheckedInChapter STOOD HERE UNTIL 2026-08-08, AND THE MODEL ALREADY ANSWERED IT.
// ***** It asked "how many of this chapter's hits are checked and not yet replaced" with its own
// loop over GetHitCount / GetHitFlags - which is KBSResultModel::GetChapterCheckedCount, by the
// same rule (checked && !replaced), with the same out-of-range answer, and that is the question
// the tree's chapter row reads out as "(N/M checked)". Two spellings of one question is how they
// come to disagree; the run asks the model now.
//
// The 2026-08-07 pass that cut this from three calls per chapter down to one did not notice that
// the surviving call was still a private copy of the model's own.

// Everything a run has to say about itself, in one place. It used to be seventeen locals in
// ReplaceChecked; they were gathered up when a second way through that function arrived on
// 2026-08-03 (the chapter-at-a-time path, which saved). That path was removed with "save after
// replace" on 2026-08-05 and there is one way through again - but the structure earns its keep on
// its own: the counters are what the summary is built from, and they are easier to follow gathered
// than scattered.
//
// Counters only - no decisions. BuildSummary says nothing about a counter that stayed zero.
struct RunTotals
{
	int32	replaced;			// hits actually rewritten
	int32	chaptersTouched;	// chapters at least one replacement landed in
	int32	chaptersSkipped;	// could not be opened at all
	int32	chaptersNotWalked;	// opened, but the text walker would not run on them
	int32	chaptersNoWindow;	// a replacement landed, but no window could be opened on it
	// The walk STARTED here and then broke off with an error - a different thing from the two
	// above, where it never started at all, and from a walk that simply ran out of matches. The
	// rows it never reached are counted as missing like any others (there is nothing else honest
	// to say about them), so this exists to explain WHY there are so many: the chapter was not
	// searched to the end. Without it a broken search reads as "the text has moved", which is a
	// statement about the user's document and is not true.
	int32	chaptersWalkFailed;
	int32	missing;			// checked hits whose text is no longer where the row says
	int32	locked;				// checked hits on a locked layer or in a locked story
	int32	refused;			// the replace command was asked and said no

	// The FIRST name in each of the three lists that name one. Kept with a flag of its own rather
	// than testing IsEmpty(): a chapter whose name is empty would otherwise never count as the
	// first, and every later one would overwrite it.
	PMString	firstSkipped;
	PMString	firstNotWalked;
	PMString	firstWalkFailed;
	bool		haveFirstSkipped;
	bool		haveFirstNotWalked;
	bool		haveFirstWalkFailed;

	// The user stopped the run from the progress bar. The whole run is one command sequence, so
	// this means the sequence was aborted and nothing at all was written - see BuildSummary.
	bool		cancelled;

	RunTotals()
		: replaced(0), chaptersTouched(0), chaptersSkipped(0),
		  chaptersNotWalked(0), chaptersNoWindow(0), chaptersWalkFailed(0),
		  missing(0), locked(0), refused(0),
		  haveFirstSkipped(false), haveFirstNotWalked(false), haveFirstWalkFailed(false),
		  cancelled(false)
	{
		firstSkipped.SetTranslatable(kFalse);
		firstNotWalked.SetTranslatable(kFalse);
		firstWalkFailed.SetTranslatable(kFalse);
	}
};

// ***** A SAME-OCCURRENCE TEST STOOD HERE UNTIL 2026-08-05, AND IT NO LONGER DOES. *****
//
// MatchStillStandsHere asked, of every checked hit and with no fast path past it, whether the match
// the walk had landed on was the one the row described - same story, same position (our own
// replacements cancelled out through a posDelta map), same text. A row that did not line up was
// left alone and reported as 'missing' rather than written.
//
// What it was guarding against is real and has not gone away: the walk order alone cannot tell "the
// Nth match" from "a DIFFERENT Nth match". An edit made between the search and the replace that
// removes one match and adds another keeps the COUNT intact, so every checked hit still comes up and
// nothing looks wrong - while the numbering now points at text the user never checked.
//
// It was removed on the user's decision (2026-08-05): keeping the document steady between searching
// and replacing is the USER's responsibility. What it could not do was work across a BOOK, where a
// chapter may be closed between the search and the replace.
//
// ***** Since 2026-08-08 the edit is DETECTED, though still not refused. ***** KBSEditStamp records
// every story's ITextModel::GetChangeCount() as the search walks a chapter, and those counters live
// in the FILE - so a chapter that was closed and reopened, even across a restart, still answers.
// That is what defeated the test removed here. The confirmation names the chapters that changed and
// the user decides. NOTHING stands in the way of the write itself - an all-or-nothing rollback was
// considered the same day and deliberately not taken. What the walk finds when a checked hit's turn
// comes is what gets rewritten, and a run that reaches fewer hits than it was given still commits
// the ones it managed; the summary reports the shortfall, as it always has.
//
// KBSSearchEngine::MatchIsSameOccurrence and the hash behind it are NOT gone - the JUMP still asks
// them, which is how clicking a row can answer "the replacement is no longer here" instead of
// scrolling to whatever now sits at that position.

// Replace this chapter's checked hits. Returns how many were replaced.
// outMissing   = checked hits whose turn never came: the walk ran to the end of the chapter
//                without them coming up, so the matches the search found are no longer there.
//                Left untouched, counted and marked. (Until 2026-08-05 this also covered a hit
//                whose turn DID come but whose text no longer lined up, which is what the
//                same-occurrence test caught - see the note above the walk for where it went.)
// outLocked    = checked hits sitting on a locked layer or in a locked story. InDesign can search
//                those but offers no way to change them, so KBS does not either - they are left
//                untouched and counted (see KBSSearchEngine::EditableFrameForMatch).
// outRefused   = checked hits the replace command itself would not run on. Not a decision of ours
//                like the two above, and not a walk that lost its place like the two flags - the
//                command was asked and said no.
// outNotWalked = the chapter could not be WALKED AT ALL: no database, no Find/Change options, no
//                text walker, no walker scope for this document, no walker client. Nothing was
//                written, and nothing can honestly be said about any individual row, so the summary
//                names the chapter instead - the same distinction the SEARCH makes with
//                ChapterWalkResult. Without it such a chapter dropped out of a replace in complete
//                silence, which is exactly what every other counter here exists to prevent.
// outWalkFailed = the walk DID start and then broke off with an error (RunWalkerCmd came back
//                kFailure). Whatever had not come up by then is counted as missing, because there
//                is nothing else true to say about those rows - but the CHAPTER is named as well,
//                so the user is not told that their text has moved when what actually happened is
//                that the search failed. Distinct from outNotWalked (never started) and from an
//                ordinary end of walk (kNotFound / kFoundCompleted).
//   progressBar  - the run's bar, sized in HITS. This chapter moves it from progressBase to
//                  progressBase + (its own checked hits) as it consumes them. nil is allowed.
//   scopeOptions - the search's five scope switches, read ONCE for the whole run by the caller
//                  and handed to every chapter alike - the same shape CollectHitsInDoc takes
//                  them in, and the same necessity: this walk has to visit exactly what the
//                  search's walk visited or the walk orders stop lining up.
int32 ReplaceInChapter(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	int32& outMissing, int32& outLocked, int32& outRefused, bool& outNotWalked, bool& outWalkFailed,
	RangeProgressBar* progressBar, int32 progressBase, int32& ioProgressReported)
{
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;
	outNotWalked = false;
	outWalkFailed = false;

	// walkOrder -> row index, plus the set of walk orders to replace. The rows are stored in PAGE
	// order and the walk runs in DOCUMENT order, so walkOrder is the only thing joining them.
	std::map<int32, int32> rowByWalkOrder;
	std::set<int32> targets;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);

	for (int32 i = 0; i < hitCount; ++i)
	{
		const int32 walkOrder = KBSResultModel::GetHitWalkOrder(chapterIdx, i);
		bool checked = false, replaced = false, locked = false;
		const bool haveFlags = KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked);
		if (walkOrder < 0)
		{
			// A checked row with NO walk order cannot be lined up with any match of the re-walk, so
			// the walk below can never reach it - and it must not vanish from the count either: the
			// rule this file is built on is that every checked hit that is not replaced is named in
			// the summary. Counted as missing, which to the user is what it is: asked for, not done.
			//
			// ***** UNREACHABLE AS THE MODEL IS FILLED TODAY, AND KEPT ANYWAY. ***** The search
			// stamps every hit with its walk order (KBSSearchEngine, before the page-order sort),
			// and a scan's rows carry no check box at all - so no checked row without a walk order
			// exists yet. This is a door against the two coming to be built differently, the same
			// door the walk below keeps for a walk order with no row behind it (hitIdx < 0). Without
			// it such a row was on the bar - the run is sized with GetChapterCheckedCount, which
			// asks nothing about walk orders - and in no counter at all.
			if (haveFlags && checked && !replaced)
			{
				++outMissing;
				KBSResultModel::SetHitOutcome(chapterIdx, i, KBSResultModel::kOutcomeMissing);
			}
			continue;
		}
		rowByWalkOrder[walkOrder] = i;
		if (haveFlags && checked && !replaced)
			targets.insert(walkOrder);
	}
	if (targets.empty())
		return 0;

	// What the bar counts down from. Every target leaves this set exactly once - replaced, refused,
	// locked or missing - so "how many have gone" is the honest measure of this chapter's progress,
	// and it does not care WHY a hit was finished with.
	const int32 targetsAtStart = static_cast<int32>(targets.size());

	// NO IDataBase::SaveRestoreModifiedState here. The search wraps its walk in one because it
	// must leave a windowless chapter unmodified; a replace is meant to leave the document
	// changed, so guarding it would throw away the entire point. (Do not copy it over from
	// KBSSearchEngine::CollectHitsInDoc.)

	// Every failure from here to the critical section means the walk never started - see outNotWalked
	// on why each one has to be reported rather than returning a bare zero.
	//
	// The DATABASE is asked for first, the same way the search asks it (KBSSearchEngine's
	// CollectHitsInDoc, which has a kChapterNoDatabase of its own for the answer). Every step below
	// takes it - QueryDocumentWalkerScope above all - and without this door the failure would arrive
	// further down wearing a different name. It is NOT a liveness test: a UIDRef carries the
	// IDataBase* itself, so a document closed underneath us leaves a dangling pointer here rather
	// than a nil one, and "is this document still open?" has exactly one honest answer in KBS
	// (KBSBookScope::IsDocStillOpen). What this catches is a UIDRef that never had a database.
	if (docRef.GetDataBase() == nil)
	{
		outNotWalked = true;
		return 0;
	}

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
		// ***** THE ONE EXIT THAT IS PAST Initialize. ***** Every refusal above this line is before
		// the walker was given anything to walk, so there is nothing to stop; this one is after. A
		// walker left walking is not merely untidy: the next caller that guards its Initialize with
		// IsWalking CONTINUES it, and that is what InDesign's own Find/Change does
		// (SnpFindAndReplace.cpp:772). The shape is Adobe's (SpellPreviousObserver.cpp:200-201: ask
		// IsWalking, then Halt), and it is the shape the bottom of this function already uses.
		//
		// Added 2026-08-08, on the fourth audit of the search block: KBSSearchEngine::CollectHitsInDoc
		// halts at its matching exit and says in a comment that this engine "is already symmetric this
		// way". It was not - that claim was written without opening this file.
		if (walker->IsWalking())
			walker->Halt();
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

	// The range the last replacement wrote, so a match INSIDE it can be recognised.
	UID lastReplStory = kInvalidUID;
	TextIndex lastReplStart = kInvalidTextIndex;
	TextIndex lastReplEnd = kInvalidTextIndex;

	// (A posDelta map stood here: how far THIS pass had moved the text in each story, so that our own
	// replacements could be cancelled out before the same-occurrence test compared positions -
	// without it, "cat" -> "kitten" would have refused every match after the first. It had no other
	// reader, so it went with that test on 2026-08-05.)

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

	// How the walk moves forward is the walker's business alone: each find command advances it to
	// the next match, exactly as the official loop runs it (SnpFindAndReplace), and the walk ends
	// when the find says there is nothing more.
	while (!targets.empty())
	{
		// Move the run's bar to where this chapter has got to. Moving it from inside the walk is what
		// makes the Cancel button answer at all; advances smaller than a few hits are swallowed, so
		// this does not run the message loop once per replacement. (spellpanel updates its bar from
		// inside the walk too - SpellReplaceWalker.cpp:496 - so this is where Adobe puts it as well.)
		// The call itself is SetPosition, not the DoTask this comment used to name - see
		// KBSAdvanceProgress, which is the one place any KBS bar is moved.
		KBSAdvanceProgress(progressBar, ioProgressReported,
			progressBase + (targetsAtStart - static_cast<int32>(targets.size())));

		// ALWAYS find first: the replace command does not search on its own, it only acts on the
		// match a find has just made current.
		UIDRef story;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		const IFindChangeService::FindChangeResult findResult =
			RunWalkerCmd(kFindTextCmdBoss, walker, story, start, end);
		if (findResult != IFindChangeService::kSuccess)
		{
			// Two ways to get here and they are NOT the same thing to report.
			//
			// kNotFound / kFoundCompleted - the walk is finished. Whatever is left in targets never
			// came up, which is exactly what 'missing' means, and the loop below says so on the rows.
			//
			// kFailure - the walk BROKE OFF. The same rows are counted the same way, because nothing
			// truer can be said about them one by one, but the CHAPTER is named as well: without that
			// the summary tells the user their text was not found when it was searched again, which
			// is a statement about their document and is not what happened.
			if (findResult == IFindChangeService::kFailure)
				outWalkFailed = true;
			break;
		}

		// A match sitting inside the text the previous replacement just wrote - a change string
		// that contains the find string ("cat" -> "cat cat"). It was never in the search results,
		// so it must NOT consume a walk order, or every later hit would line up one off and the
		// wrong occurrences would be replaced.
		if (story.GetUID() == lastReplStory && start >= lastReplStart && start < lastReplEnd)
			continue;

		// An unselected hit is only counted past. Its stored text range is deliberately NOT
		// refreshed: ReplaceChecked ends by turning the panel into a report of the run
		// (KeepCheckedRows), which keeps the rows it was ASKED about and drops the ones the user
		// had unchecked - so a row left alone here is on its way out of the list anyway, and a pass
		// that replaced nothing has not moved anything to begin with. Its row is not even looked
		// up - only a checked hit needs one.
		if (targets.find(walkIndex) != targets.end())
		{
			const std::map<int32, int32>::const_iterator row = rowByWalkOrder.find(walkIndex);
			const int32 hitIdx = (row != rowByWalkOrder.end()) ? row->second : -1;
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

			// ***** The same-occurrence test stood HERE, and this is the line it guarded. *****
			// See the long note above the walk for what it did and why it went (2026-08-05): what
			// the walk lands on at a checked hit's turn is now simply what gets rewritten.
			//
			// This much remains: a walk position with no row behind it cannot be reported on, and a
			// row is what MarkHitReplaced needs to record the outcome against. Counted as missing
			// for the same reason a row the walk never reached is - it was asked for and not done -
			// so the total cannot quietly come up short.
			//
			// ***** UNREACHABLE AS THIS FUNCTION STANDS, AND KEPT ANYWAY. ***** targets and
			// rowByWalkOrder are filled from the same pass over the same rows, and only a walkOrder
			// of 0 or more goes into either, so every walk order in targets has a row. It is a door
			// against the two coming to be built differently - the one thing that must never happen
			// here is a checked hit that is neither replaced nor accounted for.
			if (hitIdx < 0)
			{
				++outMissing;
				targets.erase(walkIndex);
				++walkIndex;
				continue;
			}

			// TWO answers and no more on this side, unlike the find above. The service's own
			// ReplaceText states that contract outright (IFindChangeService.h:57-65: kSuccess with
			// the range it wrote, or kFailure), and the command was measured to behave the same way
			// (see the file header). So there is nothing here to tell apart - anything that is not
			// kSuccess is the command declining.
			UIDRef replacedStory;
			TextIndex replacedStart = kInvalidTextIndex, replacedEnd = kInvalidTextIndex;
			if (RunWalkerCmd(kTWReplaceTextCmdBoss, walker, replacedStory, replacedStart, replacedEnd)
				== IFindChangeService::kSuccess)
			{
				++replacedCount;
				lastReplStory = replacedStory.GetUID();
				lastReplStart = replacedStart;
				lastReplEnd = replacedEnd;

				// (The shift this replacement causes was accumulated here, into posDelta, for the
				// same-occurrence test to cancel out. Nothing reads it since that test went.)

				// The STORY AND RANGE the command reports, and nothing else. Both come from the
				// command, so both are exact - no guessing at the change string's length, which
				// GREP back-references would make impossible anyway - but the line around them is
				// not read until the chapter is finished. See the pass below the walk for why it
				// cannot be read here.
				//
				// The story is handed over as well since 2026-08-05. It is normally the one the
				// row already named, and had to be while the same-occurrence test stood in front
				// of this line; with that test gone, a walk landing this hit in a DIFFERENT story
				// is possible, and a row holding one story with the other's range would have its
				// line and its hash read out of unrelated text (see MarkHitReplaced).
				KBSResultModel::MarkHitReplaced(chapterIdx, hitIdx, replacedStory.GetUID(),
					replacedStart, replacedEnd);
				replacedRows.push_back(hitIdx);
			}
			else
			{
				// The command would not run here. RunWalkerCmd has already cleared the error state
				// (it has to - a standing error would roll the whole sequence back), so without
				// this counter the hit just vanishes: the row came up, nothing was written, and the
				// replaced total silently comes up short with nothing to explain it.
				++outRefused;
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
	// Reading, not writing, so it needs no command sequence of its own - and it opens none. It runs
	// INSIDE whichever sequence the caller has standing (there is no per-chapter sequence any more -
	// see the note above the walk), which costs nothing, because not one step of it is a command.
	// A run the user cancels does reach this point, and reads text that is about to be rolled back -
	// but the rows are rolled back with it (KBSResultModel::RollBackRows), so nothing of it survives.
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
			// ***** BOTH of the row's descriptions of itself, read from the SAME range, written in
			// the SAME call. ***** The three segments are what the row DRAWS; the hash is what the
			// same-occurrence test COMPARES, and a jump into this row runs that test. Reading only
			// the segments left the hash describing the text that was here BEFORE the replacement,
			// so every replaced row answered a click with "the replacement is no longer here"
			// (2026-08-04 to 2026-08-05 - see SetHitSegments).
			const UIDRef rowStoryRef(db, rowStory);
			PMString pre, match, post;
			KBSSearchEngine::SplitLineAroundMatch(rowStoryRef, rowStart, rowEnd, pre, match, post);
			KBSResultModel::SetHitSegments(chapterIdx, hi, pre, match, post,
				KBSSearchEngine::HashMatchText(rowStoryRef, rowStart, rowEnd));
		}
	}

	// Checked hits the re-walk never reached: the walk ran out of matches before their turn came,
	// so as far as this chapter is concerned those matches are gone. Said on the rows THEMSELVES
	// rather than on the chapter, which named a file and left the user to guess which of its rows
	// it meant.
	//
	// COUNTED as well, not merely marked. These are checked hits that were not replaced, and the
	// rule this file is built on is that every one of those is named in the summary rather than
	// letting the total quietly come up short. Marking the row and not counting it left a request
	// for ten reading "7 replaced." with nothing to explain the other three, and the explanation
	// sitting on rows the user had to go hunting for.
	for (std::set<int32>::const_iterator t = targets.begin(); t != targets.end(); ++t)
	{
		++outMissing;
		const std::map<int32, int32>::const_iterator row = rowByWalkOrder.find(*t);
		if (row != rowByWalkOrder.end())
			KBSResultModel::SetHitOutcome(chapterIdx, row->second, KBSResultModel::kOutcomeMissing);
	}
	return replacedCount;
}

// A SECOND shape lived here from 2026-08-03 to 2026-08-05: ReplaceChapterByChapter ran a SAVING
// run one chapter at a time - open, replace, close its own sequence, save, hand it back - so that
// a book of twenty chapters was never all open at once. It went with "save after replace" itself,
// and had to: a chapter that has not been written to disk cannot be closed, because closing it
// would throw its replacements away. There is nothing that shape can do without the save. A
// machine that cannot hold a whole book open is served by ticking fewer rows (user, 2026-08-05).

// The status line, from the counters alone.
//
// Split out from ReplaceChecked when a second way through the run arrived (the chapter-at-a-time
// path, 2026-08-03 to 2026-08-05). It reads RunTotals and nothing else - no document, no model, no
// session state - and it is worth keeping that way: the wording of a run is then decided in one
// place, from one set of facts.
//
// Every checked hit that was not replaced is named here rather than being allowed to make the total
// quietly come up short. That rule is what most of these branches exist for.
void BuildSummary(const RunTotals& t, PMString& outSummary)
{
	// ***** A cancel is absolute. ***** The whole run is one command sequence and a cancel aborts
	// it, so the text goes back and the panel goes back with it - there is nothing left to account
	// for. (While the saving path existed this had a second ending: chapters already written to disk
	// could not be taken back, so the count had to be stated. Nothing reaches the disk now.)
	if (t.cancelled)
	{
		outSummary.Append("Replace cancelled - nothing was changed.");
		return;
	}

	// The count leads, so it survives the narrow status field's tail truncation.
	outSummary.AppendNumber(t.replaced);
	outSummary.Append(" replaced in ");
	outSummary.AppendNumber(t.chaptersTouched);
	outSummary.Append(" chapter(s).");

	// Urge a save whenever something was actually written - nothing here writes to disk. A run where
	// every checked row came back missing, locked or refused leaves every file exactly as it found
	// it, and "check them and save yourself" there reads as though something HAD been changed - at
	// the very moment the user is already wondering what became of their hits.
	if (t.replaced > 0)
		outSummary.Append(" Not saved - check them and save yourself.");

	// Four sentences stood here until 2026-08-05, all belonging to "save after replace": how many
	// chapters were saved, how many could not be, that the chapters this plug-in opened were closed,
	// and how many were left open because their save failed. None of them can happen now.

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
	//
	// ***** THE WORD IS "missing", AND IT LEADS WITH A "!". ***** It used to read "not found",
	// while the ROWS have always been marked "missing" (KBSResultModel::BuildHitLocator) - one
	// outcome under two names, which left the reader matching a sentence against rows that did not
	// use its word. The "!" is there because this is the one line in the summary the user has to
	// act on: some of what they ticked was not written (user's request, 2026-08-04).
	//
	// The WORDING changed on 2026-08-05 with the same-occurrence test. It used to say the text was
	// "no longer where the search left it", which was that test speaking - it compared each match
	// against the row before writing. What is left is the plainer fact: the chapter was walked
	// again and those matches did not come up.
	if (t.missing > 0)
	{
		outSummary.Append(" ! ");
		outSummary.AppendNumber(t.missing);
		outSummary.Append(" hit(s) missing - not found when the chapter was searched again.");
	}

	// ***** ...and WHEN THAT WAS NOT THE DOCUMENT'S FAULT, immediately after it. ***** A chapter
	// whose re-walk broke off with an error leaves all its unreached rows in the count above, and
	// that sentence then says their text was not found - a statement about the user's document that
	// is simply untrue here. So the chapter is named, right where the reader is still looking at the
	// number it is explaining.
	//
	// It took a distinction to see this at all: the walker commands answer with a whole enum
	// (IFindChangeService.h:43 - five members, of which the four in RunWalkerCmd's table can arrive
	// here) and KBS used to read every one of them that was not kSuccess as "nothing here". Adobe's own loop keeps them apart - SnpFindAndReplace.cpp:796
	// returns the result untouched and :644-648 turns kFailure, and only kFailure, into a failure.
	if (t.chaptersWalkFailed > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersWalkFailed);
		outSummary.Append(" of those chapter(s) stopped with a search error (\"");
		outSummary.Append(t.firstWalkFailed);
		// "each", not "that chapter": this counter can be more than one, and every other sentence
		// in this summary is written to read correctly at any count.
		outSummary.Append("\" first) - the rest of each was never reached.");
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

	// ***** A chapter that WAS written to and has no window. ***** Everything this run does is left
	// for the user to look at and save, so a replacement they cannot see is the one outcome that
	// leaves them with no move to make - and it used to be reported nowhere: the window was asked
	// for and the answer thrown away (fixed 2026-08-05, along with the answer being worth reading -
	// see KBSBookScope::ShowChapterWindow). Rare: it means kOpenLayoutCmdBoss itself would not run.
	if (t.chaptersNoWindow > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersNoWindow);
		outSummary.Append(" chapter(s) were changed but could not be shown - open them from the book panel to save them.");
	}

	// One more sentence stood here until 2026-08-05: chapters whose OWN sequence was rolled back,
	// which only the chapter-at-a-time path could produce. This path wraps the whole run in a single
	// sequence, so there is no such thing here as one chapter going back on its own - an error
	// standing at the end takes every chapter with it, and the cancel sentence above covers that.
}

// ***** HAND BACK EVERY CHAPTER THIS RUN OPENED AND THEN LEFT NOTHING IN. *****
//
// The run keeps the chapters a replacement landed in - they hold the user's unsaved work and each
// one is given a window to be seen and saved through. A chapter that got NONE is a different thing
// entirely: it holds nothing of this run, nobody can see it (it was opened windowless), and while it
// stands it keeps its .indd locked. There is no way for the user to close it either - a document
// with no window is not in the Window menu.
//
// It happens whenever every checked hit in a chapter came back locked, missing or refused, when the
// walker would not run there at all, or when the walk ended before any of its rows came up.
//
// ***** THE MODIFIED FLAG GOES BACK FIRST, AND THAT ORDER IS THE POINT. ***** A walk can leave a
// database marked modified without changing a character - that is exactly why the SEARCH wraps its
// own walk in IDataBase::SaveRestoreModifiedState, which the replace deliberately does not (it is
// meant to leave documents changed). So a chapter nothing was written to can still come out of the
// walk looking unsaved, and ReleaseHeldDoc REFUSES to close a chapter with unsaved work in it - it
// would be thrown away silently. Left as it was, such a chapter could never be handed back again by
// anything, and stayed open and locked for the rest of the session.
//
// Only for chapters that were CLEAN when this run found them. One the user had already edited is
// still edited, and saying otherwise would put their work at risk of a silent close.
//
// The same two steps the CANCEL path takes, for the same reasons; it takes them over every chapter,
// because an abort leaves nothing in any of them. (This is the half that was missing from the run
// that goes THROUGH - added 2026-08-05.)
// outUnclosed: the chapters this pass could NOT hand back, by name, for the summary
// (KBSBookScope::AppendUnclosedNote). The other three runs have counted these since before today;
// the replace discarded the release's answer here until 2026-08-08. A chapter left behind is
// windowless - nothing on screen shows it, nothing the user can do closes it, and it holds its
// .indd locked - so it is worth a line.
void HandBackChaptersWithNothingInThem(const std::vector<PendingChapter>& pending,
	std::vector<PMString>& outUnclosed)
{
	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		if (!pending[pi].opened || pending[pi].tookReplacement)
			continue;

		if (!pending[pi].wasModified)
		{
			IDataBase* const chapterDB = pending[pi].docRef.GetDataBase();
			if (chapterDB != nil)
				chapterDB->SetModified(kFalse);
		}

		// closeNow: a scheduled close does not run until the current tick has unwound, and this run
		// IS that tick - the chapters would stay open, and stay locked, until it was over. The same
		// call and the same reasoning as the search's per-chapter release (KBSSearchEngine::
		// SearchBook). Safe here: the command sequence is closed, the walk has halted, and a chapter
		// the user opened themselves is not on the held list and passes through untouched.
		//
		// THREE questions, the shape all four runs share since 2026-08-08: IsHeldDoc before (a
		// chapter that was never ours answers false for no fault of anyone's), the release itself,
		// and IsDocStillOpen after ("the user closed it under the run" is their own doing, not a
		// chapter left standing).
		const bool wasOurs = KBSBookScope::IsHeldDoc(pending[pi].docRef);
		if (!KBSBookScope::ReleaseHeldDoc(pending[pi].docRef, true /*close now*/)
			&& wasOurs && KBSBookScope::IsDocStillOpen(pending[pi].docRef))
		{
			// Named the way the summary's other chapter mentions are (firstNotWalked and its
			// siblings): the model's display name for the row. Still valid here - this runs before
			// KeepCheckedRows reshapes the chapter list.
			PMString name;
			int32 hitCount = 0;
			KBSResultModel::GetChapterDisplay(pending[pi].chapterIdx, name, hitCount);
			name.SetTranslatable(kFalse);
			outUnclosed.push_back(name);
		}
	}
}

// ***** THE WAY OUT WHEN THE RUN IS STOPPED BEFORE IT HAS WRITTEN ANYTHING. *****
//
// Both exits from the resolve pass come through here: the progress bar's Cancel while the chapters
// are being opened, and a Cancel on an edited chapter's warning. Neither has a command sequence to
// abort - it opens after that pass - and neither has a row backup to drop, which is taken after it
// as well. What IS owed is the chapters the pass opened: each holds its .indd locked and has no
// window it could be closed through.
//
// The wording is BuildSummary's, not this function's: a cancel means one thing to the user wherever
// it was pressed, and a second spelling of it here is how the two come to differ.
// @return 0, always - the number of replacements the run made.
int32 StopBeforeAnythingIsWritten(const std::vector<PendingChapter>& pending, RunTotals& totals,
	PMString& outSummary)
{
	std::vector<PMString> unclosed;
	HandBackChaptersWithNothingInThem(pending, unclosed);

	// Belt and braces, the same reading the cancel inside the run takes: nothing here raises the
	// error state deliberately (WasCancelled is asked with kFalse, and the alert raises nothing at
	// all), but the pass this leaves behind OPENS DOCUMENTS, and a chapter that would not open can
	// leave one standing. It must not outlive this function - a raised error state fails whatever
	// the application does next.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	totals.cancelled = true;
	BuildSummary(totals, outSummary);
	// The chapters that would not close, at the end, the way every other exit says it.
	KBSBookScope::AppendUnclosedNote(outSummary, unclosed);
	return 0;
}

// ***** THE TEXT THIS CHAPTER'S ROWS WERE FOUND IN IS NOT THE TEXT THAT IS THERE NOW. *****
//
// The run walks the chapter again and gives the Nth match to the Nth checked row, so an edit that
// added or removed a match between the search and now moves every row after it onto a match the
// user never ticked. Nothing in the walk can see that; only the counters can (KBSEditStamp), and
// this is where what they say is put to the user.
//
// Asked ONE CHAPTER AT A TIME (user's decision, 2026-08-08), and with the progress bar DOWN: the
// resolve pass closes its own bar before the first of these goes up, so a modal alert never stands
// over a modal bar.
//
// ***** CANCEL STOPS THE WHOLE RUN, AND HERE THAT COSTS NOTHING TO UNDO. ***** This is asked before
// the command sequence opens, so a cancel returns with not one character written: there is no
// rollback to do and no chapter left half replaced. (Once the run is under way a cancel is
// AbortCommandSequence instead - see the sequence in ReplaceChecked.)
//
// Cancel is the DEFAULT button, the decision the glyph confirmation records for itself (KBS.fr,
// 2026-07-31): Enter must not start a rewrite.
//
// ***** AND THAT DECIDES WHAT HAPPENS WHEN NOBODY IS THERE. ***** With userInteractionLevel at
// NEVER_INTERACT the alert is never drawn and ModalAlert hands back the DEFAULT button, so a
// scripted run STOPS at the first edited chapter and says why through app.kfcStatus ("Replace
// cancelled - nothing was changed."). Measured 2026-08-08.
//
// ⚠ Not symmetrical with the confirmation prompt, which is simply skipped while interaction is
// suppressed and lets the replace run - and the asymmetry is deliberate (user's decision,
// 2026-08-08). An unattended run that has been told the document moved under it should stop, and
// the status line is there to be read afterwards. ***** Do not "fix" this by making OK the default:
// that would trade an unattended stop for an unattended rewrite, and it would also hand the Enter
// key the one thing this dialog exists to keep it away from.
// @param chapterIdx the chapter's index in KBSResultModel.
// @return false when the user stops the run.
bool AskEditedChapter(int32 chapterIdx)
{
	PMString msg;

	// A DOCUMENT-scope run has one "chapter" and it is the front document, which has no name to put
	// here - the book wording would name a chapter of a book that is not there.
	if (!KBSResultModel::IsFromBook())
		msg = KBSLoc::Text(kKBSConfirmEditedDocKey, KBSJa::kConfirmEditedDoc);
	else
	{
		// The model's display name for the chapter row, marked untranslatable BEFORE it goes into a
		// translated string - the order every other line of this prompt takes (BuildCountLine).
		PMString name;
		int32 chapterHits = 0;
		KBSResultModel::GetChapterDisplay(chapterIdx, name, chapterHits);
		name.SetTranslatable(kFalse);
		msg = KBSLoc::Text(kKBSConfirmEditedOneKey, KBSJa::kConfirmEditedOne);
		::ReplaceStringParameters(&msg, name);
	}

	// What it can cost, and then what Cancel does. The line breaks are ours: the platform breaks an
	// alert where it sees fit, and these are three separate sentences (CAlert.h:116-122).
	msg.Append(kLineSeparatorString);
	msg.Append(KBSLoc::Text(kKBSConfirmEditedTailKey, KBSJa::kConfirmEditedTail));
	msg.Append(kLineSeparatorString);
	msg.Append(KBSLoc::Text(kKBSConfirmEditedCancelAllKey, KBSJa::kConfirmEditedCancelAll));
	// Finished text: every part was translated as it was taken, so the alert must not look the
	// result up again (CAlert translates the message it is handed).
	msg.SetTranslatable(kFalse);

	const int16 answer = CAlert::ModalAlert(msg,
		kOKString,				// 1 - go on with the replace
		kCancelString,			// 2 - stop the whole run
		kNullString,			// no third button
		2,						// ...and 2 is the DEFAULT
		CAlert::eWarningIcon);
	return (answer == 1);
}

} // anonymous namespace

bool KBSReplaceEngine::RefuseChangedQuery(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// ----- (1) the TAB the results were searched with -----
	// Every chapter is RE-WALKED below, and a walk in another mode returns another set of matches -
	// so the stored walk orders would line up with occurrences the user never saw. Asked first
	// because it is the most specific thing that can be said, and because it does NOT cost the
	// results: a tab is one click to put back.
	const int32 searchedMode = KBSResultModel::GetSearchMode();
	{
		InterfacePtr<IFindChangeOptions> modeOpts(QuerySessionPreferences<IFindChangeOptions>());
		const int32 currentMode = (modeOpts != nil) ? static_cast<int32>(modeOpts->GetSearchMode()) : -1;
		if (searchedMode >= 0 && currentMode >= 0 && currentMode != searchedMode)
		{
			outSummary.Append("The Find/Change dialog is on a different tab than when this search ran - search again before replacing.");
			return true;
		}
	}

	// ----- (2) whether that tab is one this panel can walk at all -----
	// Object and Colour search by attribute through a walker of their own and never produced these
	// rows in the first place, so there is nothing here to rewrite.
	if (searchedMode >= 0
		&& searchedMode != IFindChangeOptions::kTextSearch
		&& searchedMode != IFindChangeOptions::kGrepSearch
		&& searchedMode != IFindChangeOptions::kGlyphSearch
		&& searchedMode != IFindChangeOptions::kTransliterateSearch)
	{
		outSummary.Append("These results did not come from a text tab, so they cannot be replaced here - InDesign's own Find/Change can change them.");
		return true;
	}

	// State the tab, exactly as the search does. A walk runs in the mode last COMMITTED, not the one
	// IFindChangeOptions reports, so without this a replace ran as plain Text whatever tab was on
	// screen - which is how a Glyph-tab search came to be overwritten with the TEXT tab's change
	// string. See KBSSearchEngine::CommitSearchMode.
	//
	// It writes back the value it just read, so calling it here AND from a caller that asks this
	// question twice changes nothing: it states the mode, it does not choose one.
	//
	// ***** IT HAS TO HAPPEN BEFORE THE COMPARISON BELOW, and that is not a detail. ***** The search
	// records its signature after committing the mode too (KBSSearchEngine::SearchBook). Committing a
	// mode is a declaration and, as CommitSearchMode's own comment says, "there is no promise anywhere
	// that it leaves that mode's other settings untouched" - so a signature taken on one side of that
	// command and compared against one taken on the other side could differ with nothing having
	// changed, and would then refuse every replace there is. Both are taken on the same side.
	//
	// Deliberately OUTSIDE any command sequence: this processes a command of its own, and a
	// session-setting command inside the replace's sequence would become part of its undo step. Every
	// caller of this function asks before opening one.
	//
	// ***** AND ITS ANSWER DECIDES WHETHER THERE IS A REPLACE AT ALL. ***** A tab that could not be
	// stated leaves the engine in whatever mode was committed last, and a replace walked in the
	// wrong mode writes the wrong tab's change side over what this tab found - which is the very
	// fault CommitSearchMode was written for (user's report, 2026-07-30). The failure was swallowed
	// here until 2026-08-08. The results are NOT cleared: they still describe the search that found
	// them, so this refuses the way every other door in this plug-in does and leaves the panel be.
	if (!KBSSearchEngine::CommitSearchMode())
	{
		outSummary.Append("The Find/Change tab could not be set, so nothing was changed. Try again, or reopen Edit > Find/Change.");
		return true;
	}

	// ----- (3) the QUERY itself, which the tab does not cover -----
	//
	// The tab test catches "the user clicked another tab". It does not catch the far more ordinary
	// thing: the find string retyped, Case Sensitive ticked, a paragraph style put in Find Format,
	// Include Footnotes turned off - each of which leaves the tab alone and changes WHICH matches a
	// walk returns.
	//
	// That matters because the Nth match of the re-walk is lined up with the hit whose walkOrder is
	// N. A different match set makes the Nth match a different occurrence, and the walker is handed
	// the LIVE IFindChangeOptions (ITextWalker.h:58-61) - so what it walks by is whatever the dialog
	// holds RIGHT NOW, not what it held when these rows were found.
	//
	// ***** THERE IS NOTHING BEHIND THIS TEST ANY MORE. ***** The per-hit same-occurrence test used
	// to be the backstop for it ("Nothing wrong is written - the same-occurrence test refuses each
	// one", KBSResultModel.h). It first stopped being one when the trusted-story fast path arrived -
	// a story nobody had edited was taken on trust and the test skipped outright, so a retyped query
	// wrote the change string over occurrences the user had never seen while the panel reported the
	// ORIGINAL rows as replaced (found 2026-08-03 in the defect audit) - and then it was removed
	// outright on 2026-08-05 (user's decision, see ReplaceChecked).
	//
	// So this is no longer the door that says WHY a run came back all-missing; it is the door that
	// keeps the run from happening at all. What it does not catch is not caught by anything (the
	// DOCUMENT being edited - stated on the confirmation instead), so anything it does not know
	// about a query is a wrong replacement made in silence. Widen the test rather than lean on
	// anything downstream.
	//
	// ***** IT IS TWO QUESTIONS, NOT ONE. ***** The signature covers the tab, the query and every
	// switch, and it COUNTS the Find Format conditions without saying what they are set to. The
	// values are compared separately, by the attribute list itself
	// (KBSSearchEngine::FindFormatHasChanged) - because until 2026-08-07 they were fingerprinted by
	// hand into the signature, and an attribute that answered none of the nine interfaces that probe
	// used went in as its class alone. "Find Format: size 14 pt" edited to "size 20 pt" then left the
	// signature IDENTICAL and walked straight through this door.
	//
	// An EMPTY signature on either side means it could not be described, not that it differs -
	// results from before this field existed answer empty too - so only two known-different
	// signatures refuse. The format test follows the same rule and answers false when it cannot
	// tell.
	//
	// ***** AND THE RESULTS GO. ***** (User's call, 2026-08-03.) Every other refusal in this plug-in
	// leaves the panel exactly as it found it - that is the rule the search's own refusals were moved
	// above their Clear() to obey on the same day - and this one is deliberately the exception. The
	// difference is what the rows would go on saying: a run turned away for any other reason leaves a
	// list that is still TRUE, while these rows describe a query the dialog no longer holds, so
	// leaving them up invites the user to try again against a list that cannot be acted on. Clearing
	// says plainly that the search has to be re-run, which is the only way forward anyway.
	{
		const PMString walkedSignature = KBSResultModel::GetWalkSignature();
		PMString currentSignature;
		KBSSearchEngine::BuildWalkSignature(currentSignature);
		const bool signatureDiffers = !walkedSignature.IsEmpty() && !currentSignature.IsEmpty()
			&& walkedSignature != currentSignature;

		if (signatureDiffers || KBSSearchEngine::FindFormatHasChanged())
		{
			// Paired, always - see KBSBookScope::ReleaseSearchedBook. The caller redraws the tree, so
			// nothing here touches the panel.
			//
			// The remembered format goes with them, and here it matters more than anywhere: what was
			// just compared against it is gone, so leaving it standing would have the NEXT question
			// about a changed query answered from a search whose rows no longer exist.
			KBSResultModel::Clear();
			KBSBookScope::ReleaseSearchedBook();
			KBSSearchEngine::ForgetSearchedFindFormat();
			outSummary.Append("The Find/Change query has changed since this search ran, so these results no longer describe it - they have been cleared. Search again.");
			return true;
		}
	}

	return false;
}

int32 KBSReplaceEngine::ReplaceChecked(PMString& outSummary)
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

	// Do the Find/Change settings still describe the search these rows came from - the tab, and the
	// query with every option that decides the match set? This also STATES the tab
	// (CommitSearchMode), which the walk below needs whatever the answer is.
	//
	// The menu asks the same question before it puts the confirmation prompt up
	// (KBSActionComponent::DoAction), so this is the same door on the far side of it, for a caller
	// that never went through the menu. Asking twice costs one command that writes back the value it
	// just read; not asking here would leave a script route with no door at all.
	if (KBSReplaceEngine::RefuseChangedQuery(outSummary))
		return 0;

	// ...and what the replace will WRITE, for the tabs whose change side is not a string: the Glyph
	// tab's Change To glyph, the Transliterate tab's change character type. Stated only here, never
	// on the search path, so a search can never leave a change-side value set behind the user's
	// back. An EMPTY Change To box is stated too, not refused - it means "delete every match", the
	// same as an empty change string on the Text tab. false means only that the Find/Change settings
	// could not be read at all. Also outside the sequence, and before it opens, so nothing has been
	// written yet.
	if (!KBSSearchEngine::CommitReplaceSide())
	{
		outSummary.Append("Find/Change settings are unavailable - nothing was changed.");
		return 0;
	}

	// ***** WHAT THIS RUN WAS TOLD TO WRITE, RECORDED BEFORE IT WRITES ANY OF IT. ***** The heading of
	// the file "Save Results..." produces names it, and the user can retype Change To the moment this
	// returns - so reading the dialog at save time would put a replacement in the report that these
	// rows never took. Same rule, same reason, as the query line the search records (SetQueryText).
	//
	// Here rather than earlier because CommitReplaceSide has just stated the Glyph tab's change glyph
	// on the options: before that call, the change side of a glyph replace is not there to be read.
	// Still outside every command sequence, and past all the doors above, so nothing that gets this
	// far is recorded without running.
	KBSResultModel::SetChangeText(KBSSearchEngine::DescribeCurrentChange());

	// The five scope switches, read ONCE for the whole run and handed to every chapter's walk -
	// the same single reading the search takes above its own chapter loop, and for the same two
	// reasons: the answer cannot differ between chapters (nothing can touch the dialog while the
	// run's modal bar is up), and this walk HAS to run with exactly the switches the search ran
	// with or the walk orders stop lining up. Read once per chapter here until 2026-08-07.
	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);

	// The whole account of this run - every counter the summary reads, in one structure. It used to
	// be seventeen locals here.
	//
	// totals.cancelled is set when the user stops the run from the progress bar. That means the
	// documents are given back as they were: the command sequence rolls the text back, and the
	// result model is rolled back with it, so the panel returns to being the search's results.
	// Nothing is half done.
	RunTotals totals;

	// ***** ONE WAY THROUGH, and it opens every chapter it has work in. ***** A second shape stood
	// here from 2026-08-03 to 2026-08-05 - chapter at a time, for runs that SAVED - and it went with
	// the save: a chapter that has not been written to disk cannot be closed, so a run that does not
	// save has no choice but to hold every chapter it touches until the end.
	//
	// What the user gets in exchange is that Cancel is absolute: one sequence around everything, so
	// stopping the run puts the whole book back.

	// How much work the run has to get through - what the bar is sized with. Counted BEFORE anything
	// is opened, because the bar has to be up while the chapters are being opened: that is the slow
	// part when the user has closed the windows the search was holding.
	//
	// HITS, not chapters. A chapter is a coarse unit: one chapter of 5000 hits and one of 3 both
	// counted as a single step, so the bar stood still through the long one. Hits are the work.
	//
	// The run's chapter list is born HERE as well: a chapter with nothing checked is not in it -
	// not opened, not walked, not a step of the bar - and every chapter that is in it carries its
	// count with it (PendingChapter::checkedCount, which is also where the reasons the number
	// stays right are). Until 2026-08-07 that count was taken up to three times per chapter -
	// here, at the door of the resolve pass below, and again as each chapter's turn came - three
	// passes over the same rows for an answer that cannot change while the run stands.
	//
	// ...and it is the MODEL that counts, since 2026-08-08. The one reading left was still a
	// private loop of this file's own; GetChapterCheckedCount is the same question asked where the
	// tree's chapter row asks it. See the note where that loop used to live.
	std::vector<PendingChapter> pending;
	int32 totalCheckedHits = 0;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		const int32 checkedHere = KBSResultModel::GetChapterCheckedCount(ci);
		if (checkedHere <= 0)
			continue;		// nothing selected here - do not even open this chapter
		PendingChapter chapter;
		chapter.chapterIdx = ci;
		chapter.checkedCount = checkedHere;
		pending.push_back(chapter);
		totalCheckedHits += checkedHere;
	}

	// ***** THE TITLE BOTH OF THE RUN'S BARS CARRY. ***** They are two objects - see the resolve
	// pass below - but one run, and one title is what says so on screen.
	//
	// A bar is shown for BOTH scopes since 2026-07-31 (user's request), matching the search. It used
	// to be book scope only, on the reasoning that a one-document replace is a single step with
	// nothing to cancel between - but the bar is sized in HITS, not chapters, so a single document
	// with thousands of them takes just as long and had no way to be stopped.
	//
	// showImmediate = kTrue on both, for the reason the search learned the hard way: with the
	// default the bar waits out an internal delay, and a fast run beats that delay - so the cancel
	// button, the one thing the bar is really there for, never reaches the screen.
	PMString progressTitle("Replacing...");
	progressTitle.SetTranslatable(kFalse);

	// FIRST PASS, deliberately OUTSIDE the command sequence: turn every chapter in the list into a
	// live document, reopening the ones the user has closed since the search.
	//
	// Reopening is a document OPEN, and an open processed BETWEEN two chapters' replacements is
	// exactly what was measured on 2026-07-28 to throw away the undo history of the chapters
	// already done - that is why ShowChapterWindow was moved out to the far end of this function.
	// The reopen belongs on the same side of the fence for the same reason. Doing it here also
	// means a chapter that cannot be opened at all is counted before anything has been written,
	// instead of interrupting a run that is already half committed.
	//
	// A chapter that cannot be opened STAYS in the list, unopened: it is a step the bar was sized
	// with, and dropping it is what used to leave the bar short of its own total, stopping at
	// 4 of 5 with nothing left to do.
	//
	// ***** THIS PASS HAS A BAR OF ITS OWN, IN A SCOPE OF ITS OWN, SINCE 2026-08-08. ***** One bar
	// covered both passes until then, which cannot work now that a chapter found to be EDITED puts a
	// modal alert up between them: the bar is modal too, and the two must not stand at once (user's
	// decision). Ending the scope takes the bar down, the questions are asked, and the replace's own
	// bar goes up after them.
	//
	// Sized in CHAPTERS here - this pass opens documents, it does not replace anything - where the
	// replace's bar below is sized in hits. Until the split, the opening moved no bar at all: it was
	// the slow part of a run with nothing on screen to show for it.
	bool cancelledWhileOpening = false;
	{
	RangeProgressBar openBar(progressTitle, 0, static_cast<int32>(pending.size()), kTrue, kTrue);
	openBar.DisableChildProgressBars(kTrue);

	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		// The bar carries the chapters ALREADY opened, so it is set at the top of the pass rather
		// than the bottom: three of the paths below leave by continue, and a step written after them
		// would be the step they skip.
		openBar.SetPosition(static_cast<int32>(pi));

		// Cancel, asked between chapters exactly as the replace loop below asks it, and answered by
		// the bar having just been moved (WasCancelled only reads a flag - something has to have
		// given the button a chance to set it).
		//
		// kFalse: do NOT raise the global error state, the same reading the replace loop takes.
		// Nothing has been written at this point - the sequence does not open until this pass is
		// over - so a cancel here is answered by handing the chapters back and returning, with no
		// rollback to do at all.
		if (openBar.WasCancelled(kFalse))
		{
			cancelledWhileOpening = true;
			break;
		}

		PendingChapter& chapter = pending[pi];
		const int32 ci = chapter.chapterIdx;

		UIDRef docRef;
		IDFile file;
		if (!KBSResultModel::GetChapterLocation(ci, docRef, file))
		{
			// Unreachable while GetChapterLocation only fails on an out-of-range index and ci comes
			// straight from GetChapterCount - but if the two ever come apart, this chapter's checked
			// rows must not drop out of the run in silence: nothing on their locators would say a
			// word, and the replaced total would come up short with nothing to explain it, which is
			// the one thing the summary's rule exists to prevent. Counted and named exactly like a
			// chapter that would not open - to the user, that is what it is.
			++totals.chaptersSkipped;
			if (!totals.haveFirstSkipped)
			{
				int32 chapterHits = 0;
				KBSResultModel::GetChapterDisplay(ci, totals.firstSkipped, chapterHits);
				totals.firstSkipped.SetTranslatable(kFalse);
				totals.haveFirstSkipped = true;
			}
			continue;		// unopened - the loop below only counts it past
		}

		// Resolve the chapter to a LIVE document, BY FILE.
		//
		// ***** NOT gated on IsDocStillOpen, and that is the whole point. ***** It used to be:
		// "still open? then keep the docRef the search left". But that question is asked of a
		// docRef whose document was closed when the search finished, and a UIDRef is only
		// (IDataBase*, UID) - so once the address is reused by a document opened afterwards, and
		// the UID lands the same (chapters built the same way have the same internal UIDs), it
		// answers YES about a DIFFERENT DOCUMENT. The chapter was then never reopened, the walk ran
		// over its neighbour, and every row came back 'missing' - measured 2026-08-04, 2 to 4 book
		// replaces in 10, always in a chapter after the first, never in the first one (nothing is
		// open yet when the first is resolved, so there is nothing to be confused with).
		//
		// ReopenChapterDoc answers the same question by FILE: it hands back the open document that
		// lives in this chapter's .indd, or opens it. A file cannot be confused with another file.
		{
			UIDRef reopened;
			if (KBSBookScope::ReopenChapterDoc(file, reopened))
			{
				docRef = reopened;
				KBSResultModel::RebindChapterDoc(ci, reopened);
			}
			// ***** TWO different failures, and only ONE of them may fall back. *****
			//
			//   - There was no file to open BY. Normal: a DOCUMENT-scope row is the front document
			//     and carries none. The old question is safe here - that docRef IS the live front
			//     document, and nothing was closed behind it.
			//
			//   - The file would not open (moved, deleted, in use by another application). Here the
			//     docRef is the one the SEARCH left, and its document was closed when the search
			//     finished - so asking IsDocStillOpen about it is the exact fault removed above. It
			//     can answer YES about a DIFFERENT document, and the walk then runs over the wrong
			//     chapter and reports every row missing. Skip instead: it is the one answer that
			//     cannot be wrong, and the summary names the chapter either way.
			else if (KBSBookScope::ChapterHasFile(file) || !KBSBookScope::IsDocStillOpen(docRef))
			{
				++totals.chaptersSkipped;
				if (!totals.haveFirstSkipped)
				{
					int32 chapterHits = 0;
					KBSResultModel::GetChapterDisplay(ci, totals.firstSkipped, chapterHits);
					totals.firstSkipped.SetTranslatable(kFalse);
					totals.haveFirstSkipped = true;
				}
				// Moved, deleted, or in use: counted and named just above; it stays in the list
				// unopened so the bar still takes its step for it.
				continue;
			}
		}

		chapter.docRef = docRef;
		chapter.opened = true;
		// Read BEFORE anything is written to it - that is the whole point of the record.
		{
			IDataBase* const chapterDB = docRef.GetDataBase();
			chapter.wasModified = (chapterDB != nil) && (chapterDB->IsModified() != kFalse);
		}

		// ...and whether the TEXT has moved on since the search walked it. Read before this run
		// writes anything, for the same reason as the flag above, and asked HERE rather than at the
		// confirmation prompt because here is the only point where EVERY chapter can be asked:
		// IsChapterCurrent needs the document open, and this one has just been opened.
		//
		// docRef, not the UIDRef the search recorded: this one was resolved BY FILE just above,
		// while the recorded one may name a database that has since been closed and its address
		// reused (the resolve above is entirely about that). The stamp holds story UIDs and
		// counters, which read the same in the reopened document (measured 2026-08-08).
		chapter.editedSinceSearch = !KBSEditStamp::IsChapterCurrent(ci, docRef);
	}

	// ASK ONCE MORE, now that the pass is over: the reading at the top only ever sees a cancel that
	// arrived while an EARLIER chapter was opening, and one pressed during the last chapter's open
	// would have no next pass to be noticed in. The same shape, and the same reason, as the reading
	// at the end of the replace loop below.
	if (!cancelledWhileOpening && openBar.WasCancelled(kFalse))
		cancelledWhileOpening = true;

	}	// end of the scope the opening bar lived in - THE BAR IS DOWN FROM HERE

	if (cancelledWhileOpening)
		return StopBeforeAnythingIsWritten(pending, totals, outSummary);

	// ***** AND THE CHAPTERS THAT HAVE BEEN EDITED SINCE THE SEARCH ARE PUT TO THE USER. *****
	//
	// One at a time (user's decision, 2026-08-08), each naming its own chapter, and any one of them
	// can stop the whole run - which is why they are asked HERE, after the pass that opens the
	// chapters and before the sequence that writes to them. Every chapter has been opened by now, so
	// this is the first moment at which the question can be asked about ALL of them rather than
	// about whichever ones happened to be open.
	//
	// Not inside the pass above: the bar is modal and so is the alert, and the two must not stand at
	// once. The pass has just taken its bar down.
	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		// A chapter that could not be opened is not asked about. Nothing will be written to it -
		// it is skipped below and named in the summary - so a warning about its text would be a
		// question with no consequence attached to either answer.
		if (!pending[pi].opened || !pending[pi].editedSinceSearch)
			continue;

		if (!AskEditedChapter(pending[pi].chapterIdx))
			return StopBeforeAnythingIsWritten(pending, totals, outSummary);
	}

	// ***** THE REPLACE'S OWN BAR. ***** A second object, not the one the pass above used - see the
	// note there for why the run cannot carry a single bar across both.
	//
	// ***** WHAT "STOPPED" MEANS HERE, EXACTLY. ***** WasCancelled is read between chapters and once
	// more when the loop ends, never inside a chapter - the walk holds the walker's critical section
	// and must not pump UI work (see the section in ReplaceInChapter). So a Cancel pressed during a
	// ONE-CHAPTER run does not break off the work: the chapter is replaced to the end, and then the
	// whole sequence is aborted and every character put back. The button is honoured - nothing is
	// left changed - but it is honoured at the end rather than at the moment it is pressed.
	// DisableChildProgressBars keeps anything the replacements raise from putting up bars of their
	// own (the chapter opens the other bar covers are the same case, and it says so there).
	//
	// SIZED IN HITS, not chapters, and moved by ReplaceInChapter as it goes (progressBase below).
	// The walker will not report progress for us: ITextWalkerProgressMonitor is only a place to PARK
	// a bar - the client's own OnNextPosition is what calls SetPosition on it, and the stock
	// kFindChangeClientBoss does not (measured 2026-07-31: registered fine, 5270 replacements, zero
	// calls). spellpanel gets its moving bar because it walks with a client it wrote itself. So KBS
	// counts its own work, which it can do better than the walker anyway: the number of checked hits
	// is known before the run starts.
	RangeProgressBar progressBar(progressTitle, 0, totalCheckedHits, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

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
	//
	// (The string above is TRACKING DATA, not that name - CmdUtils.h:134 - so it names this caller in
	// a lost-sequence report and nowhere else.)

	// ***** NO SEQUENCE, NO RUN. ***** BeginAbortableCmdSeq answers nil on error (CmdUtils.h:135),
	// and everything this function promises rests on the sequence it hands back: one Ctrl+Z for the
	// whole book, and a Cancel that puts every chapter back. Without it the replacements would go in
	// as loose commands - undoable one at a time at best - and, worse, a CANCEL would find no
	// sequence to abort while still reporting "nothing was changed" over a book that had been
	// rewritten. Refusing before a character is written is the only honest answer (2026-08-05).
	if (seq == nil)
	{
		// Nothing was written, so there is nothing to roll back and nothing to keep: drop the row
		// backup, and hand back every chapter the resolve pass opened - none of them took a
		// replacement, which is exactly what this hands back.
		KBSResultModel::ForgetRowBackup();
		std::vector<PMString> unclosed;
		HandBackChaptersWithNothingInThem(pending, unclosed);
		outSummary.Append("Could not start an undoable step - nothing was changed.");
		KBSBookScope::AppendUnclosedNote(outSummary, unclosed);
		return 0;
	}

	// How many hits the bar has behind it. The bar is sized in hits, so each chapter starts where
	// the last one ended and moves the bar itself as it goes. progressReported is how far it has
	// actually been advanced - what lets KBSAdvanceProgress swallow an advance too small to repaint
	// for - so it has to be carried along rather than recomputed.
	int32 progressBase = 0;
	int32 progressReported = 0;

	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		// The count the run was sized with, carried on the chapter - see PendingChapter::
		// checkedCount for why it is still right when this chapter's turn comes. (A recount taken
		// after the chapter ran would find zero, its hits being marked replaced by then.)
		const int32 chapterChecked = pending[pi].checkedCount;

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

		// Cancel is asked here, and answered by the bar being moved inside the chapter
		// (KBSAdvanceProgress). WasCancelled only reads a flag; something has to have given the
		// button a chance to set it.
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

		bool notWalked = false;
		bool walkFailed = false;
		int32 missing = 0;
		int32 locked = 0;
		int32 refused = 0;
		const int32 replaced = ReplaceInChapter(ci, docRef, scopeOptions, missing, locked,
			refused, notWalked, walkFailed, &progressBar, progressBase, progressReported);
		progressBase += chapterChecked;
		// Land exactly on the chapter boundary: a chapter that finished early (nothing left to
		// line up) must still hand the bar on at the right place.
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
		totals.replaced += replaced;
		totals.missing += missing;
		totals.locked += locked;
		totals.refused += refused;
		if (replaced > 0)
			++totals.chaptersTouched;

		// Did anything land here? What decides whether this chapter is kept open for the user -
		// and given a window, in the loop past the sequence - or handed straight back at the end
		// of the run (HandBackChaptersWithNothingInThem).
		pending[pi].tookReplacement = (replaced > 0);
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
		if (walkFailed)
		{
			// The walk STARTED here and broke off. Its unreached rows are already in `missing` above
			// - there is nothing truer to put on them one at a time - so this names the chapter to
			// say that the shortfall is a search error, not the document having moved on. Counted
			// separately from notWalked because the two are different failures and reading them as
			// one would hide whichever is rarer.
			++totals.chaptersWalkFailed;
			if (!totals.haveFirstWalkFailed)
			{
				int32 failedHits = 0;
				KBSResultModel::GetChapterDisplay(ci, totals.firstWalkFailed, failedHits);
				totals.firstWalkFailed.SetTranslatable(kFalse);
				totals.haveFirstWalkFailed = true;
			}
		}
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

		// ***** Hand the chapters back. ***** Nothing of this run is left in them - the abort took
		// every character back and the flags above went with it - so a chapter this plug-in opened
		// has no reason to stay, and each one holds its .indd locked while it does.
		//
		// The search and the two scans have always done this on THEIR cancel, through
		// ReleaseSearchedBook (which closes the chapters AND forgets the book). A replace asks for
		// the closing half alone: its results stay on the panel, so the book they came from must
		// still be remembered - that is what the book watcher reads to know when to drop them.
		//
		// AFTER the flags above, never before: ReleaseHeldDocs refuses to close a chapter with
		// unsaved work in it, and until they are back every chapter this run touched still says it
		// has some. A chapter the USER had already edited keeps its flag, so it stays open and stays
		// held - which is what should happen to somebody else's unsaved work.
		//
		// (Nothing was closed here between 2026-08-02 and 2026-08-05: the run that SAVED handed its
		// chapters back as it went, and that was the only path that closed anything. It went with
		// "save after replace", and took this with it until now.)
		KBSBookScope::ReleaseHeldDocs();

		// The panel is back to being the search's results, so there is no report to turn it into -
		// KeepCheckedRows is deliberately NOT called on this exit. The wording is left to
		// BuildSummary, which is the only place that knows what a cancel means.
		BuildSummary(totals, outSummary);
		return 0;
	}
	KBSResultModel::ForgetRowBackup();

	// NOTHING IS SAVED ON THIS PATH. That is not a decision taken here - it is what the path IS: a
	// run that saves went the other way at the fork above, because saving is the only thing that
	// makes a chapter with replacements in it safe to close. Every chapter a replacement LANDED in
	// therefore stays open and unsaved, and the summary tells the user to deal with it.
	//
	// (Saving lived here, guarded by `if (saveAfterReplace)`, from 2026-08-02 to 2026-08-03; it then
	// moved into the chapter-at-a-time path, and went with it on 2026-08-05.)

	// Windows are opened AFTER the sequence, never from inside it - the other half of the pair the
	// resolve pass above makes: no document and no window is opened while the sequence is standing.
	// Measured 2026-07-28: a kOpenLayoutCmdBoss processed between two chapters' replacements
	// discarded the undo history of the chapters already done - their text stayed replaced with
	// nothing left to undo it with. Opening the windows once everything is committed keeps that
	// command clear of the replacements.
	//
	// Every chapter a replacement landed in gets one - PendingChapter::tookReplacement is the
	// record of which those are (a second list of the same chapters stood beside it until
	// 2026-08-07): the change has to be visible, because it is the user who has to save it.
	//
	// ***** AND THE ANSWER IS READ. ***** A chapter that was written to and could not be SHOWN is
	// the one outcome that leaves the user nothing to do - the run saves nothing, so what it wrote
	// can only be dealt with through a window. It was discarded here until 2026-08-05, when
	// ShowChapterWindow's answer was also made worth reading ("it already had a window" used to come
	// back false as well, which is the ordinary case, not a failure).
	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		if (!pending[pi].tookReplacement)
			continue;
		if (!KBSBookScope::ShowChapterWindow(pending[pi].docRef))
			++totals.chaptersNoWindow;
	}

	// ***** AND THE CHAPTERS THIS RUN LEFT NOTHING IN GO BACK. ***** Everything above is about the
	// chapters that were CHANGED; a chapter this run opened and then wrote nothing to has no reason
	// to stay - it holds nothing of the run, has no window to be seen through, and locks its .indd
	// while it stands. See HandBackChaptersWithNothingInThem for the whole of why, including why
	// the modified flag has to go back first.
	//
	// AFTER the windows above, not before: the opens are the commands with a measured history of
	// disturbing undo (2026-07-28), so they stay as close to the end of the sequence as they were.
	//
	// BEFORE KeepCheckedRows, which reshapes the chapter list the hand-back's names come from.
	std::vector<PMString> unclosed;
	HandBackChaptersWithNothingInThem(pending, unclosed);

	// The panel now becomes a REPORT of what the replace did: the rows it changed, and the rows it
	// was asked about and left alone, each saying why on its locator. The rows the user had
	// unchecked are dropped - they were never part of the request. A replace that was asked for
	// nothing at all leaves the results exactly as they were.
	KBSResultModel::KeepCheckedRows();

	BuildSummary(totals, outSummary);
	// The chapters the hand-back could not close, at the end the way the scans say it - it appends
	// nothing in the ordinary case.
	KBSBookScope::AppendUnclosedNote(outSummary, unclosed);
	return totals.replaced;
}

bool KBSReplaceEngine::IsReplacing()
{
	return gReplacing;
}

// End, KBSReplaceEngine.cpp.
