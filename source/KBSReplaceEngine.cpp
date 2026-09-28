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
#include "ErrorUtils.h"				// PMSetGlobalErrorCode, GlobalErrorStatePreserver
// (ITextModel.h was here for GetTextChangeCount, which fed the trusted-story fast path. Removed
// 2026-08-03 with that path - see the note over MatchStillStandsHere. It is back since 2026-09-26
// for a different job: QueryStoryThread / FindStoryThread, which is how a row's position is kept
// as "this far into this thread" - see RowNow.)
#include "IItemStrand.h"			// a footnote's reference: where it stands, and what stands there now
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "textiterator.h"			// the object characters a Change All row may hold
#include "WideString.h"			// Reject Change: the original text's length in code points
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "ProgressBar.h"		// RangeProgressBar - the replace's progress + cancel, as the search does it
#include "StringUtils.h"			// ::ReplaceStringParameters - fills the ^1 in a translated string
#include "Utils.h"

#include <algorithm>
#include <chrono>		// SPIKE 2026-09-28 (not for main): the walk's time for the signing log
#include <map>
#include <set>
#include <vector>

// Project includes:
#include "KBSReplaceEngine.h"
#include "KBSID.h"				// the string keys the edited-chapter warning is worded from
#include "KBSLoc.h"				// runtime Japanese - the jaJP string table is gone (2026-08-05)
#include "KBSResultModel.h"
#include "KBSRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KBSSearchEngine.h"	// the shared walker scope and the line-splitting the rows use
#include "KBSBookScope.h"		// reopening a chapter the user closed since the search
#include "KBSTrackChange.h"	// every replace under Track Changes, its records left (2026-09-26)
// (KBSJump.h was included here for IsHidePreviousChapterOn until 2026-08-03. A run that saves now
// hands every chapter back as it goes, whatever that toggle says - it is about JUMPING, not about
// what a run does with the chapters it opened for itself. Same call the search stopped making on
// 2026-08-02, for the same reason.)

namespace
{

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

	// (A changeSinceSearch stood here from 2026-08-08 to 2026-08-10, carrying KBSEditStamp's
	//  verdict on this chapter from the resolve pass to a prompt further down. The resolve pass
	//  now WALKS each chapter instead and checks the ticked positions themselves, and a mismatch
	//  ends the run there and then - so there is nothing to carry.)

	// This chapter's checked, not-yet-replaced hits, counted ONCE where the run is sized and read
	// from here after that (how far this chapter's slice moves the bar). The number cannot change
	// in between: the panel's actions are greyed out for the whole run, the bar is modal, and a
	// chapter's own hits are only marked replaced as IT runs. Until 2026-08-07 every reading
	// counted the chapter's rows over again.
	int32	checkedCount;

	PendingChapter() : chapterIdx(-1), opened(false), wasModified(false), tookReplacement(false),
		checkedCount(0) {}
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

	// The run ended without committing anything. The whole run is one command sequence, so this
	// means the sequence was aborted and nothing at all was written - see BuildSummary. TWO things
	// raise it: the user stopping the run from the progress bar, and the error state being found
	// standing at the moment the sequence was about to be committed (stoppedByError, below).
	bool		cancelled;

	// ...and WHICH of the two it was. A failure the run never noticed is not the user changing
	// their mind, and calling it "cancelled" would send them looking for a button nobody pressed.
	// errorText is InDesign's own wording for it and may be empty - it is only ever shown when it
	// is not (2026-08-09).
	bool		stoppedByError;
	PMString	errorText;

	// ...or the third thing (2026-09-26): what Change All left did not line up with the rows, so the
	// run was aborted on purpose. errorText then says how it did not line up.
	bool		stoppedByMismatch;

	// ...or the fourth (2026-09-26, the user's call "Change All only"): the ticks form a shape Change
	// All cannot write as asked, so nothing was written at all. errorText says which shape.
	bool		refusedByShape;

	// Ticked rows in an endnote story that was left alone (2026-09-27): a match there ends an endnote.
	int32		endnoteLeft;
	int32		acceptedFirst;	// pending tracked changes accepted before the first write (2026-09-27)

	RunTotals()
		: replaced(0), chaptersTouched(0), chaptersSkipped(0),
		  chaptersNotWalked(0), chaptersNoWindow(0), chaptersWalkFailed(0),
		  missing(0), locked(0), refused(0), endnoteLeft(0), acceptedFirst(0),
		  haveFirstSkipped(false), haveFirstNotWalked(false), haveFirstWalkFailed(false),
		  cancelled(false), stoppedByError(false), stoppedByMismatch(false), refusedByShape(false)
	{
		firstSkipped.SetTranslatable(kFalse);
		firstNotWalked.SetTranslatable(kFalse);
		firstWalkFailed.SetTranslatable(kFalse);
		errorText.SetTranslatable(kFalse);
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
// ***** SINCE 2026-08-10 THE SAME QUESTION IS ASKED BEFORE THE RUN STARTS, AND IT REFUSES. *****
// The resolve pass walks every chapter it is about to write to - verifyOnly below - and checks that
// each ticked hit still BEGINS in the same story at the same index. It is the test removed here,
// moved to the one moment where it is both answerable and free: nothing has been written, so the
// positions are the ones the search recorded, with no replacements of ours to cancel out (that is
// what the posDelta map was for) - and a mismatch stops the run with not one character to take back.
//
// A per-chapter fingerprint (KBSEditStamp: every story's ITextModel::GetChangeCount, recorded as
// the search walked) did this job from 2026-08-08 to 2026-08-10, warning rather than refusing. It
// went because it answered a DIFFERENT question - "does this chapter look untouched?" - which has
// to enumerate the ways a document can move (text, stories, layers, locks, conditions...) and is
// never finished. Walking asks about the thing itself.
//
// So the walk below no longer needs a same-occurrence test of its own: by the time it runs, every
// ticked position has been confirmed and nothing has moved since (the verify pass writes nothing).
//
// KBSSearchEngine::MatchIsSameOccurrence and the hash behind it are NOT gone - the JUMP still asks
// them, which is how clicking a row can answer "the replacement is no longer here" instead of
// scrolling to whatever now sits at that position.

// Where one of the chapter's rows stands in the text NOW - where the search found it, carried past
// every replacement this pass has made since - or, for a row this pass replaced, where its new text
// stands. One per row, indexed like the model's rows. The replace walk asks it two things: which
// row a match it has just found IS (WalkOrderOfMatch), and, once the walk is over, where each row
// the report keeps has ended up.
//
// ***** THE RANGE IS CARRIED FORWARD, BECAUSE THE WALK IS NOT IN TextIndex ORDER. ***** It was
// believed to be ("the walk only ever moves forward, so every replacement after this row's
// happened LATER in the story and cannot shift its start"), and measured on 2026-09-25 to hold for
// body text only:
//   * a table's cells are visited where the table stands - but their characters live after the
//     whole body in TextIndex terms (ITableTextContent.h:41-44), so replacing the body text AFTER
//     the table moved every cell row written before it; and
//   * the walk follows the Find/Change dialog's own direction - "search backwards" walked
//     "cat1 cat2 cat3" as cat3, cat2, cat1 although the scope options say forwards - so there
//     EVERY later replacement moved every row written before it.
// Either way the row then read its line, and took its hash, from text beside its own: the panel
// showed "wo<CR>kit" for a cell that read "kitten cell", and a double click SELECTED those wrong
// characters, the hash having come from the same wrong range. The rows passed and left alone had
// the same fault from the other side: kept at the range the search found them at, they were
// jumped to off by the replacements before them and stamped "missing".
//
// ***** KEPT AS "THIS FAR INTO THIS THREAD", NOT AS A STORY INDEX. ***** A story's text is its body
// followed by one thread per table cell and per footnote (ITableTextContent.h:41-44), and a
// replacement can take a whole thread away: deleting a footnote's reference marker deletes the
// footnote's text with it. Measured 2026-09-26: GREP ~F|cat on "A<fn1> B<fn2>" (fn1 "x one", fn2
// "cat two"), rows "marker 1" and "cat" of fn2 ticked - the marker's replacement took fn1's six
// characters out as well, fn2's "cat" came up six places earlier than a story index carried by the
// replaced length alone, and it was stepped over as a match nobody had listed and reported missing.
// A thread knows where it starts NOW (ITextModel::FindStoryThread, by the thread's own identity),
// so an offset into it is carried only by replacements inside that thread, and every other move -
// a thread before it growing, shrinking or disappearing - comes with the thread's start for free.
struct RowNow
{
	bool		known;		// false: the row's identity could not be read (no story, no range)
	UID			story;
	UID			threadDict;	// the thread the row's text lives in (ITextStoryThread::GetDictUID/Key)
	uint32		threadKey;
	TextIndex	offset;		// start, from the start of that thread
	int32		length;

	RowNow() : known(false), story(kInvalidUID), threadDict(kInvalidUID), threadKey(0), offset(0),
		length(0) {}
};

// Which thread holds `at` in this story, and where it starts. False when the story or the thread
// cannot be read.
bool ThreadAt(IDataBase* db, UID story, TextIndex at, UID& outDict, uint32& outKey,
	TextIndex& outThreadStart)
{
	if (db == nil)
		return false;
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	if (model == nil)
		return false;
	TextIndex threadStart = kInvalidTextIndex;
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, nil));
	if (thread == nil)
		return false;
	outDict = thread->GetDictUID();
	outKey = thread->GetDictKey();
	outThreadStart = threadStart;
	return true;
}

// Put a row at [start, end) of `story` as the text stands at this moment.
void SetRowAt(IDataBase* db, RowNow& row, UID story, TextIndex start, TextIndex end)
{
	row.known = false;
	UID dict = kInvalidUID;
	uint32 key = 0;
	TextIndex threadStart = kInvalidTextIndex;
	if (story == kInvalidUID || !ThreadAt(db, story, start, dict, key, threadStart))
		return;
	row.known = true;
	row.story = story;
	row.threadDict = dict;
	row.threadKey = key;
	row.offset = start - threadStart;
	row.length = end - start;
}

// Where the row starts now. False when its thread is gone (the replacement that took a footnote
// away took the rows in it too) or cannot be read.
bool RowStartNow(IDataBase* db, const RowNow& row, TextIndex& outStart)
{
	if (!row.known || db == nil)
		return false;
	InterfacePtr<ITextModel> model(db, row.story, UseDefaultIID());
	if (model == nil)
		return false;
	TextIndex threadStart = kInvalidTextIndex;
	if (!model->FindStoryThread(row.threadDict, row.threadKey, &threadStart, nil))
		return false;
	outStart = threadStart + row.offset;
	return true;
}

// A replacement has just turned [oldStartOffset, oldEndOffset) of one thread into newLength
// characters. Every row lying AFTER it in the same thread moves by the difference; a row before
// it, or in another thread, does not - another thread's move is its start's, which FindStoryThread
// answers (see RowNow).
//
// "After" is offset >= oldEndOffset, NOT >= oldStartOffset: matches never overlap, so a row either
// lies wholly past the replaced text or wholly before it - and a zero-width row sitting exactly at
// the start (GREP's ^ before a match that begins at the same place) is BEFORE the text that was
// replaced and must stay where it is. For a zero-width replacement (an insertion) a row starting
// at that very position is pushed right by the insertion, which is what the text there did.
void CarryRowsPast(std::vector<RowNow>& rows, UID story, UID threadDict, uint32 threadKey,
	TextIndex oldStartOffset, TextIndex oldEndOffset, TextIndex newLength)
{
	const TextIndex delta = newLength - (oldEndOffset - oldStartOffset);
	if (delta == 0)
		return;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		RowNow& row = rows[i];
		if (!row.known || row.story != story || row.threadDict != threadDict
			|| row.threadKey != threadKey || row.offset < oldEndOffset)
			continue;
		row.offset += delta;
	}
}

// Which row is the match the walk has just found? The walk order it answers with is the one to
// deal with; -1 means NO row - a match the search never found, which the walk must step over
// without spending a walk order on it.
//
// ***** WHY THE WALK CANNOT SIMPLY COUNT. ***** The walk order joins a replace to its search by
// counting ("the Nth match found now is the Nth row"), and the count holds only while the replace
// walk meets the same matches the search met. A replacement can make a NEW one. Measured on
// 2026-09-25: GREP's ^ and $ read the real text past the point the walk resumes from (lookbehind,
// lookahead and \b do not - see kbs-bughunt-2026-08-09 R-1), so deleting the "a" of "ab<CR>ab"
// under GREP a|^b left the "b" at the start of its paragraph - a match the search never listed -
// and it took the second row's walk order: the document came out "<CR>ab" instead of "b<CR>b",
// and the summary said "2 replaced". Backwards, "ab<CR>ab" under b|a$ did the same from the other
// end. InDesign's own Change All gets "b<CR>b", because it collects every position before it
// writes a character (spellpanel SpellReplaceWalker.cpp:748, :823); its Change/Find finds the new
// "b" too, but shows it and leaves the choice to the user - a list made BEFORE the replacements has
// no such chance, so it has to recognise the match itself.
//
// So each match is recognised by WHERE it is, against rowNow - every row's position carried past
// every replacement so far, which is where that row's text has to be now ("at a row" = the row's
// start AND its length - see below):
//   * at the expected row: that row - the ordinary case, asked first;
//   * at a LATER row: that row, and the rows in between are gone (a replacement took away what they
//     matched). Answering with the later walk order leaves those in targets, and the end of the walk
//     counts them missing, as it does any row that never came up;
//   * at no row: a match the search never listed. -1.
// A row whose identity could not be read (known == false) cannot be recognised; at its own turn it
// is taken on the count, as every row was until this function existed.
//
// ***** THE START AND THE LENGTH ARE BOTH COMPARED. ***** Until 2026-09-26 it was the start alone
// ("the start is what the search recorded; the end belongs to the query"), and a new match can
// begin exactly where a row begins and still not be that row. Measured 2026-09-26 (H-8): GREP
// \r|^a|ab on "x<CR>ab" lists <CR>@1 and a@2 (a paragraph start, so ^a wins); deleting the return
// runs "ab" on from x, and the walk resuming at 1 meets ab@1 - the second row's start now, two
// characters where the row listed one. Taken as that row, it deleted "ab": KBS left "x" and said
// "2 replaced", where Change All leaves "xb".
// The length of a row's own match cannot change on the way: the walk never sees past the point it
// resumes from (lookbehind, lookahead and \b - kbs-bughunt-2026-08-09 R-1), so a listed match meets
// the text it met at search time and matches it the same way - a literal Text or Glyph query cannot
// differ at all. What does see further - ^ and $ - is exactly what makes a match the search never
// listed, so a length that differs names one of those. It is stepped over like any other; the row
// is then left in targets and reported missing - untouched and said so - rather than written with
// text nobody ticked.
//
// A row whose thread is gone cannot be where the match is, and is passed over like one that does
// not stand there.
bool RowIsThisMatch(IDataBase* db, const RowNow& row, UID story, TextIndex start, TextIndex end)
{
	TextIndex rowStart = kInvalidTextIndex;
	return row.known && row.story == story && RowStartNow(db, row, rowStart) && rowStart == start
		&& row.length == end - start;
}

int32 WalkOrderOfMatch(IDataBase* db, const std::vector<RowNow>& rowNow,
	const std::map<int32, int32>& rowByWalkOrder, int32 walkIndex, UID story, TextIndex start,
	TextIndex end)
{
	std::map<int32, int32>::const_iterator it = rowByWalkOrder.lower_bound(walkIndex);
	if (it != rowByWalkOrder.end() && it->first == walkIndex)
	{
		const RowNow& expected = rowNow[it->second];
		if (!expected.known)
			return walkIndex;
		if (RowIsThisMatch(db, expected, story, start, end))
			return walkIndex;
	}
	for (; it != rowByWalkOrder.end(); ++it)
	{
		if (RowIsThisMatch(db, rowNow[it->second], story, start, end))
			return it->first;
	}
	return -1;
}

// The walk has dealt with a row the report keeps (replaced, locked, refused): put it where its text
// stands now and name it for the read-back after the walk.
void KeepRowAt(IDataBase* db, std::vector<RowNow>& rowNow, std::vector<int32>& keptRows, int32 hitIdx,
	UID story, TextIndex start, TextIndex end)
{
	SetRowAt(db, rowNow[static_cast<size_t>(hitIdx)], story, start, end);
	keptRows.push_back(hitIdx);
}

// ======================================================================================================
// ***** THE CHANGE ALL REPLACE (2026-09-26 to 2026-09-27) IS GONE. ***** Each story with a ticked row got
// InDesign's Change All under Track Changes, and the rows NOT ticked were taken back record by record;
// the ticked rows' records were lined up with the rows by position (LineUpStory), a footnote's rows
// were placed from its text before and after (AlignFootnoteRows - nothing is recorded in a footnote),
// and the run was checked to have changed nothing but the ticked rows (CheckOnlyTickedChanged). The
// user's call on 2026-09-27 went back to writing only the ticked matches, one at a time (below), and
// that code went with the 2026-09-27 cleanup - it is in git history (c876bc7 and before). Two of its
// pieces stay, for Redo and for the walk.
// ======================================================================================================

// (RunChangeAllInScope - Change All over one row's range, for Redo - went with Redo on 2026-09-27: a
// row taken back is replaced again with Replace, like any other.)

// ***** A REPLACE THAT WOULD WRITE AT AN ENDNOTE'S END (2026-09-27). ***** InDesign's replace - Change All,
// changeText, one write over a range - that writes at the END of an endnote leaves the endnote's range
// (IDML EndnoteRange) ending short, and the character before the overhang can never be deleted again
// (measured, work/kbs-regress/probe-endnote-*-0927.jsx; no Track Changes needed). True = `matchEnd` is in
// the endnote story (kEndnoteStoryBoss - SnpManipulateTextEndnotes::IsEndnoteStory) and the code point
// there is the U+FEFF an endnote ends with. Asked by the replace (per row, as the walk meets it)
// and by Redo (the row's text may have been moved to an endnote's end since).
bool MatchEndsAnEndnote(const UIDRef& story, TextIndex matchEnd)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil || ::GetClass(model) != kEndnoteStoryBoss || matchEnd < 0 || matchEnd >= model->TotalLength())
		return false;
	TextIterator it(model, matchEnd);
	return !it.IsNull() && (*it).GetValue() == kTextChar_ZeroSpaceNoBreak;
}

// ======================================================================================================
// ***** ONE MATCH AT A TIME AGAIN, STORY BY STORY, UNDER TRACK CHANGES (2026-09-27, the user's call). *****
//
// Change All over whole stories (above, 2026-09-26 to 2026-09-27) wrote the rows NOT ticked as well and
// then took them back, which is what a footnote (nothing recorded there) and a deletion shared by
// touching matches could not survive. The user's call: back to writing only the ticked matches, one at a
// time (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on the match it made current - the old walk's two
// commands), a story at a time, still with Track Changes on so a replaced row keeps its Reject Change.
//
// ***** WHICH WAY THE WALK GOES IS DECIDED BEFORE ANYTHING IS WRITTEN (the user's call B). ***** A walk
// that writes as it goes reads text it has already rewritten, and each direction has one shape it
// cannot get right (measured 2026-09-26, spike/2026-09-26-backward-replace):
//   - forward, GREP ^ reads the character a replacement has just deleted in front of it (H-8:
//     \r|^a|ab on "x<CR>ab" left "xab" and one row missing, where Change All makes "xb");
//   - backward, a lookahead reads text a replacement has just rewritten behind it (x(?=y)|y on "xy"
//     left "x" and one row missing, where Change All makes "").
// Neither ever writes a match the search did not list - every match is recognised by its place and
// length (RowOfMatchAnyOrder), so a match the walk meets that no row accounts for is stepped over and
// the row is reported missing. So the choice only decides which shape comes out missing: a GREP query
// holding ^ walks backward, everything else forward (WriteBackward). Retrying the missing rows the other
// way round after the fact was considered (A) and dropped: by then the text has already moved, and the
// retry meets the same unlisted match (x<CR>ab -> xab: the "a" is part of "ab" either way).
// ======================================================================================================

// Does a GREP query hold ^ as the start of a paragraph? Not \^ (a literal caret), not a ^ inside a
// character class ([^a], [a^]), and not one quoted by \Q...\E. A POSIX class inside a class ([[:alpha:]])
// does not end the class at its own ']'.
bool GrepQueryHoldsLineStart(const WideString& query)
{
	bool inClass = false;
	bool quoted = false;
	bool classJustOpened = false;	// the ']' right after '[' or '[^' is a literal
	WideString::const_iterator it = query.begin();
	const WideString::const_iterator end = query.end();
	while (it != end)
	{
		const uint32 c = *it;
		++it;
		if (quoted)
		{
			if (c == '\\' && it != end && *it == 'E')
			{
				quoted = false;
				++it;
			}
			continue;
		}
		if (c == '\\')
		{
			if (it == end)
				break;
			const uint32 next = *it;
			++it;
			if (next == 'Q' && !inClass)
				quoted = true;
			classJustOpened = false;
			continue;
		}
		if (inClass)
		{
			if (c == '[' && it != end && *it == ':')
			{
				// [:name:] - skip to its ":]"
				++it;
				while (it != end)
				{
					const uint32 p = *it;
					++it;
					if (p == ':' && it != end && *it == ']')
					{
						++it;
						break;
					}
				}
				classJustOpened = false;
				continue;
			}
			if (c == '^' && classJustOpened)
				continue;		// [^ - still "just opened" for the ']' that may follow
			if (c == ']' && !classJustOpened)
				inClass = false;
			classJustOpened = false;
			continue;
		}
		if (c == '[')
		{
			inClass = true;
			classJustOpened = true;
			continue;
		}
		if (c == '^')
			return true;
	}
	return false;
}

// Should this run's writing walk go backward? Only on the GREP tab, and only when the query holds ^.
bool WriteBackward()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil || opts->GetSearchMode() != IFindChangeOptions::kGrepSearch)
		return false;
	return GrepQueryHoldsLineStart(opts->GetFindString(IFindChangeOptions::kGrepSearch));
}

// Which pending row is the match the walk has just found - by its place alone (the thread, how far into
// it, and the length), in whatever order the walk meets the rows. -1 = no row: a match the search never
// listed, which the walk steps over. (Taken from spike/2026-09-26-backward-replace. The length is
// compared for the reason RowIsThisMatch gives.)
int32 RowOfMatchAnyOrder(IDataBase* db, const std::vector<RowNow>& rowNow, const std::set<int32>& pending,
	UID story, TextIndex start, TextIndex end)
{
	UID dict = kInvalidUID;
	uint32 key = 0;
	TextIndex threadStart = kInvalidTextIndex;
	if (!ThreadAt(db, story, start, dict, key, threadStart))
		return -1;
	const TextIndex offset = start - threadStart;
	const int32 length = end - start;
	for (std::set<int32>::const_iterator it = pending.begin(); it != pending.end(); ++it)
	{
		const RowNow& row = rowNow[static_cast<size_t>(*it)];
		if (row.known && row.story == story && row.threadDict == dict && row.threadKey == key
			&& row.offset == offset && row.length == length)
			return *it;
	}
	return -1;
}

// One story's walk: every pending row the walk meets is written, one at a time, in the direction the
// session is set to (the caller's KBSBackwardSearchScope). What the walk writes moves every row after it
// in the same thread (CarryRowsPast); a written row is put where its new text stands. A row that would
// write at an endnote's end is left, and said so (MatchEndsAnEndnote). What is still in `pending` at the
// end never came up. False = the walk could not start at all; outWalkFailed = it started and broke off.
bool WalkStoryReplacing(int32 chapterIdx, const UIDRef& storyRef, const WalkerScopeOptions& scopeOptions,
	IFindChangeOptions* opts, std::vector<RowNow>& rowNow, std::set<int32>& pending, std::vector<int32>& keptRows,
	int32& ioReplaced, int32& ioRefused, int32& ioEndnoteLeft, bool& outWalkFailed,
	RangeProgressBar* progressBar, int32 progressBase, int32& ioProgressReported, int32& ioDone)
{
	IDataBase* const db = storyRef.GetDataBase();
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(registry != nil
		? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss) : nil);
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(storyRef, scopeOptions));
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (db == nil || walker == nil || scope == nil || client == nil)
		return false;
	if (walker->IsWalking())
		walker->Halt();
	walker->Initialize(client, scope, opts, nil);
	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	if (selUtils == nil)
	{
		if (walker->IsWalking())
			walker->Halt();
		return false;
	}
	{
		const TextWalkerSelections_CriticalSection criticalSection(selUtils);
		// The range the last replacement wrote: a match inside it ("cat" -> "cat cat") is none of ours.
		UID lastStory = kInvalidUID;
		TextIndex lastStart = kInvalidTextIndex, lastEnd = kInvalidTextIndex;
		while (!pending.empty())
		{
			UIDRef story;
			TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
			const IFindChangeService::FindChangeResult found = RunWalkerCmd(kFindTextCmdBoss, walker, story, start, end);
			if (found != IFindChangeService::kSuccess)
			{
				if (found == IFindChangeService::kFailure)
					outWalkFailed = true;
				break;
			}
			if (story.GetUID() == lastStory && start >= lastStart && start < lastEnd)
				continue;
			const int32 hitIdx = RowOfMatchAnyOrder(db, rowNow, pending, story.GetUID(), start, end);
			if (hitIdx < 0)
				continue;
			pending.erase(hitIdx);

			if (MatchEndsAnEndnote(story, end))
			{
				// InDesign's replace at an endnote's end breaks the endnote's range for good (see
				// MatchEndsAnEndnote) - one at a time, only this row is left (2026-09-27).
				++ioEndnoteLeft;
				KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeEndnoteLeft);
				KeepRowAt(db, rowNow, keptRows, hitIdx, story.GetUID(), start, end);
			}
			else
			{
				// The thread and the offset into it BEFORE the command - what every row's place is kept in.
				UID matchDict = kInvalidUID;
				uint32 matchKey = 0;
				TextIndex matchThreadStart = kInvalidTextIndex;
				const bool haveThread = ThreadAt(db, story.GetUID(), start, matchDict, matchKey, matchThreadStart);
				UIDRef written;
				TextIndex writtenStart = kInvalidTextIndex, writtenEnd = kInvalidTextIndex;
				if (RunWalkerCmd(kTWReplaceTextCmdBoss, walker, written, writtenStart, writtenEnd) == IFindChangeService::kSuccess)
				{
					++ioReplaced;
					// SPIKE 2026-09-28 (not for main): sign this replace's records before the next one is written
					KBSTrackChange::SignReplace(written, writtenStart, writtenEnd, KBSTrackChange::TakeSignedStamp());
					lastStory = written.GetUID();
					lastStart = writtenStart;
					lastEnd = writtenEnd;
					KBSResultModel::MarkHitReplaced(chapterIdx, hitIdx, written.GetUID(), writtenStart, writtenEnd);
					// every row after it first, then this row at what was written - so it is not moved by itself
					if (haveThread)
						CarryRowsPast(rowNow, story.GetUID(), matchDict, matchKey, start - matchThreadStart,
							end - matchThreadStart, writtenEnd - writtenStart);
					KeepRowAt(db, rowNow, keptRows, hitIdx, written.GetUID(), writtenStart, writtenEnd);
				}
				else
				{
					++ioRefused;
					KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeRefused);
					KeepRowAt(db, rowNow, keptRows, hitIdx, story.GetUID(), start, end);
				}
			}
			++ioDone;
			KBSAdvanceProgress(progressBar, ioProgressReported, progressBase + ioDone);
		}
	}
	if (walker->IsWalking())
		walker->Halt();
	return true;
}

// ***** IS THE ROW'S STORED PLACE STILL ITS TEXT? (2026-09-27 re-check.) ***** A row is carried and read
// back only when it is: a replaced row is first put where its tracked change stands (the record moves
// with the text - RefreshRowFromRecords), any other row must still hold the text the search hashed
// (MatchIsSameOccurrence - the jump's own test). An edit made since can have moved the rest - reading
// such a row back at its old place took the line AND the hash from the wrong characters, so a click
// then selected them (measured, case edit-then-reject: "ZZkitt"); left alone, it says "missing" when
// clicked, which is the truth.
bool RowStillStands(int32 chapterIdx, int32 hitIdx, IDataBase* db)
{
	bool checked = false, replaced = false, locked = false;
	if (!KBSResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked))
		return false;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	const bool haveIdentity = KBSResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash);
	if (replaced)
	{
		// ***** THE CHEAP TEST FIRST (2026-09-27 re-check). ***** Matching a replaced row against every
		// tracked change of its story costs a pass over the chapter's rows and the story's records, and
		// this runs for every row of the chapter on every Reject / Redo / right-click Replace - squared
		// in the rows. The ordinary case is a row whose stored place still holds the text its replace
		// wrote: that is asked by reading those characters. Only a row that fails it (an edit since) is
		// looked for through its records.
		PMString originalText, replacedText;
		if (haveIdentity && KBSResultModel::GetHitChangeTexts(chapterIdx, hitIdx, originalText, replacedText)
			&& !replacedText.IsEmpty()
			&& KBSTrackChange::ReadText(UIDRef(db, story), a, b - a) == replacedText)
			return true;
		return KBSTrackChange::RefreshRowFromRecords(chapterIdx, hitIdx);
	}
	return haveIdentity
		&& KBSSearchEngine::MatchIsSameOccurrence(UIDRef(db, story), a, b, story, a, b, hash);
}

// ***** THE CHAPTER'S REPLACE (2026-09-27). ***** Every ticked row is written by the walk of its story,
// with Track Changes on for the stories written (TrackingScope) and the direction the caller set.
// Refuses before anything is written - returns false, outWhyNot says why - only for a row already
// replaced. The pending tracked changes the ticked matches sit in or next to are accepted before the first
// write (outAcceptedFirst
// = how many). outCancelled / outFailed: the caller aborts the whole run.
bool ReplaceInChapterOneByOne(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	RangeProgressBar* progressBar, int32 progressBase, int32& ioProgressReported,
	int32& outReplaced, int32& outMissing, int32& outLocked, int32& outRefused, int32& outEndnoteLeft,
	int32& outAcceptedFirst, bool& outWalkFailed, bool& outCancelled, bool& outFailed, PMString& outWhyNot,
	const std::set<int32>* onlyHits = nil)
{
	outAcceptedFirst = 0;
	outReplaced = 0;
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;
	outEndnoteLeft = 0;
	outWalkFailed = false;
	outCancelled = false;
	outFailed = false;
	outWhyNot.Clear();
	outWhyNot.SetTranslatable(kFalse);

	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (db == nil || opts == nil)
	{
		outWhyNot = "the document or the Find/Change options could not be read";
		return false;
	}
	// ***** GLYPH AND TRANSLITERATE AGAIN (2026-09-27, the user's call). ***** Change All (2026-09-26 to
	// 2026-09-27) ran on the Text and GREP tabs only, and a Glyph or Transliterate run was refused after
	// its prompt (defect sweep D-1). One match at a time is the walk those two tabs were built and
	// measured on (2026-07-30, 2026-08-08): ReplaceChecked has already stated the change glyph or the
	// character type the command writes (KBSSearchEngine::CommitReplaceSide), and the menu refuses the
	// Object and Colour tabs before any of this.

	// Every row's place now (RowNow) - the ticked ones are what the walks look for, and every row is
	// carried past what they write, for the read-back at the end.
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	std::vector<RowNow> rowNow(static_cast<size_t>(hitCount > 0 ? hitCount : 0));
	std::vector<int32> keptRows;						// the rows the report keeps, read back at the end
	std::map<UID, std::set<int32> > pendingByStory;		// the ticked, editable rows, story by story
	std::map<std::pair<UID, UID>, bool> editableFrames;
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		if (!KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked))
		{
			outWhyNot = "a row could not be read";
			return false;
		}
		// ***** WHAT THIS RUN WRITES (2026-09-27). ***** A ticked row not yet replaced - or, for the
		// right-click Replace (onlyHits), those rows - ticked or not for a row, the ticked ones for a story. A row already replaced (by a
		// right-click Replace before this run; it refused the run with "search again" until then) is not
		// written, only carried; so is every row, for the one-row Replace, since its list stays a work list.
		const bool target = (onlyHits != nil)
			? (onlyHits->count(i) != 0 && !replaced && !locked
				&& KBSResultModel::IsWorkOutcome(KBSResultModel::GetHitOutcome(chapterIdx, i)))
			: (checked && !replaced && KBSResultModel::IsWorkOutcome(KBSResultModel::GetHitOutcome(chapterIdx, i)));
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		if (!KBSResultModel::GetHitMatchIdentity(chapterIdx, i, story, start, end, hash))
		{
			if (target)
			{
				++outMissing;
				KBSResultModel::SetHitOutcome(chapterIdx, i, KBSResultModel::kOutcomeMissing);
			}
			continue;
		}
		SetRowAt(db, rowNow[static_cast<size_t>(i)], story, start, end);
		if (!rowNow[static_cast<size_t>(i)].known)
		{
			if (target)
			{
				++outMissing;
				KBSResultModel::SetHitOutcome(chapterIdx, i, KBSResultModel::kOutcomeMissing);
			}
			continue;
		}
		if (!target)
		{
			// a locked row the report keeps (it never had a box), a replaced row - and every row, for
			// the one-row Replace: each has to stand where its text is afterwards. ONLY a row whose
			// stored place is still its text (RowStillStands - a replaced row is put where its tracked
			// change stands first): one an edit has moved is left as it is, never read back at a place
			// that is no longer its own (2026-09-27 re-check).
			// ...and a row with an outcome (taken back with Reject Change, above all): Change Checked's report
			// keeps it (KBSResultModel::KeepCheckedRows) - left out until 2026-09-28, a row taken back and not
			// ticked stayed at its old place in the report, and its Replace refused as "changed since the
			// search" (case rejected-row-after-change).
			if ((onlyHits != nil || locked || replaced
					|| KBSResultModel::GetHitOutcome(chapterIdx, i) != KBSResultModel::kOutcomeNone)
				&& RowStillStands(chapterIdx, i, db))
			{
				UID s2 = kInvalidUID;
				TextIndex a2 = kInvalidTextIndex, b2 = kInvalidTextIndex;
				uint64 h2 = 0;
				if (KBSResultModel::GetHitMatchIdentity(chapterIdx, i, s2, a2, b2, h2))
					SetRowAt(db, rowNow[static_cast<size_t>(i)], s2, a2, b2);
				keptRows.push_back(i);
			}
			else
				rowNow[static_cast<size_t>(i)].known = false;
			continue;
		}
		const UIDRef storyRef(db, story);
		const UID frameUID = KBSSearchEngine::EditableFrameForMatch(storyRef, start);
		const std::pair<UID, UID> frameKey(story, frameUID);
		std::map<std::pair<UID, UID>, bool>::const_iterator known = editableFrames.find(frameKey);
		bool editable = false;
		if (known != editableFrames.end())
			editable = known->second;
		else
			editableFrames[frameKey] = editable = KBSSearchEngine::IsFrameEditable(storyRef, frameUID);
		if (!editable)
		{
			// locked since the search: never written, counted as locked
			++outLocked;
			KBSResultModel::SetHitOutcome(chapterIdx, i, KBSResultModel::kOutcomeLocked);
			keptRows.push_back(i);
			continue;
		}
		// (A ticked match inside or next to the user's own pending insertion refused the run here from
		//  2026-09-27 morning (P-4): replacing it leaves no record, so it could never be taken back. The
		//  pending changes around the ticked matches are accepted before anything is written instead - below.)
		pendingByStory[story].insert(i);
	}
	if (pendingByStory.empty())
	{
		for (size_t k = 0; k < keptRows.size(); ++k)
		{
			TextIndex s = kInvalidTextIndex;
			if (RowStartNow(db, rowNow[static_cast<size_t>(keptRows[k])], s))
				KBSResultModel::SetHitRange(chapterIdx, keptRows[k], rowNow[static_cast<size_t>(keptRows[k])].story,
					s, s + rowNow[static_cast<size_t>(keptRows[k])].length);
		}
		return true;
	}

	// ===== from here on things are WRITTEN. A cancel or a failure below leaves the caller to abort. =====

	// ***** THE PENDING CHANGES A TICKED MATCH SITS IN OR NEXT TO ARE ACCEPTED FIRST - ANYBODY'S, AND
	// ***** NOTHING ELSE (2026-09-27, the user's call: "only that part"). ***** A replace written inside
	// or next to the user's own pending insertion leaves no record of its own (KBSTrackChange::
	// IsInsideOwnPendingInsertion), so it could never be taken back; that refused the run until the
	// user's call. The rest of the document keeps its records - an earlier replace's rows keep their
	// Reject Change. (The whole document was accepted for a few hours on 2026-09-27 evening.) Inside the
	// run's sequence, so the Undo and a cancel take it back with the replaces.
	// ! Each row's place is asked NOW, from its thread offset (RowStartNow): an accepted deletion's
	//   deleted-text thread goes, which moves the story indexes of the cells and footnotes behind it.
	for (std::map<UID, std::set<int32> >::const_iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
	{
		const UIDRef storyRef(db, s->first);
		if (!KBSTrackChange::StoryHasChanges(storyRef))
			continue;
		for (std::set<int32>::const_iterator p = s->second.begin(); p != s->second.end(); ++p)
		{
			const RowNow& row = rowNow[static_cast<size_t>(*p)];
			TextIndex at = kInvalidTextIndex;
			if (!RowStartNow(db, row, at))
				continue;
			PMString why;
			why.SetTranslatable(kFalse);
			const int32 accepted = KBSTrackChange::AcceptPendingAround(storyRef, at, at + row.length, why);
			if (accepted < 0)
			{
				outFailed = true;
				outWhyNot = "a pending tracked change next to a ticked match could not be accepted - ";
				outWhyNot.Append(why);
				return true;
			}
			outAcceptedFirst += accepted;
		}
	}

	std::set<UID> targetStories;
	for (std::map<UID, std::set<int32> >::const_iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
		targetStories.insert(s->first);
	KBSTrackChange::TrackingScope tracking(db, targetStories);
	if (!tracking.Ok())
	{
		outFailed = true;
		outWhyNot = "Track Changes could not be switched on";
		return true;
	}

	// SPIKE 2026-09-28 (not for main): the run's T0, and the clock for the log line
	KBSTrackChange::BeginSignedRun();
	const std::chrono::steady_clock::time_point spikeWalkStart = std::chrono::steady_clock::now();

	int32 done = 0;		// ticked rows accounted for, for the bar
	for (std::map<UID, std::set<int32> >::iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
	{
		// Between stories is where a cancel is heard (the user's call: story by story).
		if (progressBar != nil && progressBar->WasCancelled(kFalse))
		{
			outCancelled = true;
			return true;
		}
		std::set<int32>& pending = s->second;
		// A story an earlier story's replace deleted (an anchored object's): its rows never come up.
		if (db->IsValidUID(s->first))
		{
			if (!WalkStoryReplacing(chapterIdx, UIDRef(db, s->first), scopeOptions, opts, rowNow, pending, keptRows,
				outReplaced, outRefused, outEndnoteLeft, outWalkFailed, progressBar, progressBase, ioProgressReported, done))
			{
				outFailed = true;
				outWhyNot = "the text walker could not be started";
				return true;
			}
		}
		for (std::set<int32>::const_iterator p = pending.begin(); p != pending.end(); ++p)
		{
			TextIndex at = kInvalidTextIndex;
			if (!db->IsValidUID(s->first) || !RowStartNow(db, rowNow[static_cast<size_t>(*p)], at))
			{
				// Its thread went with the footnote, table or object another ticked row deleted (or its
				// story with an anchored frame): asked for, and done - with its object. Counted and shown
				// as the Change All path did (SetHitDeleted: "deleted").
				++outReplaced;
				KBSResultModel::SetHitDeleted(chapterIdx, *p);
			}
			else
			{
				++outMissing;
				KBSResultModel::SetHitOutcome(chapterIdx, *p, KBSResultModel::kOutcomeMissing);
			}
			++done;
		}
		KBSAdvanceProgress(progressBar, ioProgressReported, progressBase + done);
	}
	// SPIKE 2026-09-28 (not for main)
	KBSTrackChange::WriteSignedRunLog(chapterIdx, outReplaced,
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - spikeWalkStart).count());

	// ----- every row the report keeps, where its text stands now: its range, its line, and - for a
	// replaced row outside a footnote - the time of the records its replace made (what Reject Change and
	// Redo find it by). Read once the chapter has stopped changing: a line read as its own match was
	// written would still show the later matches of its paragraph as they were (2026-07-28).
	for (size_t k = 0; k < keptRows.size(); ++k)
	{
		const int32 hitIdx = keptRows[k];
		const RowNow& kept = rowNow[static_cast<size_t>(hitIdx)];
		TextIndex keptStart = kInvalidTextIndex;
		if (!RowStartNow(db, kept, keptStart))
			continue;		// its thread went with an object another replace deleted
		const TextIndex keptEnd = keptStart + kept.length;
		const UIDRef rowStoryRef(db, kept.story);
		KBSResultModel::SetHitRange(chapterIdx, hitIdx, kept.story, keptStart, keptEnd);
		PMString pre, match, post;
		KBSSearchEngine::SplitLineAroundMatch(rowStoryRef, keptStart, keptEnd, pre, match, post);
		KBSResultModel::SetHitSegments(chapterIdx, hitIdx, pre, match, post,
			KBSSearchEngine::HashMatchText(rowStoryRef, keptStart, keptEnd));
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) && replaced
			&& !KBSTrackChange::IsInFootnote(rowStoryRef, keptStart))
			KBSResultModel::SetHitRecordTime(chapterIdx, hitIdx, KBSTrackChange::RecordTimeIn(rowStoryRef, keptStart, keptEnd));
	}
	return true;
}

// ***** ONLY THE VERIFY PASS CALLS THIS SINCE 2026-09-26 (verifyOnly = true). ***** The replace itself
// is ReplaceInChapterOneByOne since 2026-09-27 (Change All over whole stories from 2026-09-26), which
// walks a story at a time with the same two commands. Everything below that is about WRITING - the walk's replace step,
// RowNow and CarryRowsPast, the kept rows' read-back, the refused / not-walked / walk-failed counters -
// is kept but not reached (2026-09-27 defect sweep, C-2). The description that follows is the one it
// was written with.
//
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
//
// ***** verifyOnly - THE SAME WALK, WRITING NOTHING. ***** Every ticked row carries the position
// the search found it at, and this walk visits the same matches in the same order, so the two can
// simply be compared: at each ticked walk order, does the match still BEGIN in the same story at
// the same index? If one does not - or if the walk ends with ticked rows it never reached - the
// document is not the one the results describe, and the caller stops the whole run before a
// character is written (2026-08-10, the user's design).
//
// It is a MODE of this function rather than a function of its own, deliberately: the verify pass
// has to visit exactly what the replace pass will visit, and the surest way to promise that is
// for both to be the same code. In this mode nothing is written, no row is marked, no counter is
// touched and the bar is not moved; the answer comes back in outChanged.
//
// The END of the match is not compared, and neither is its text - the start alone is the test
// (the user's decision). A match that still begins where it began is the occurrence that was
// found there; length and content belong to the query, which RefuseChangedQuery has already
// established has not changed. (That is the VERIFY pass. The writing pass compares the length as
// well, for a different question - which row a match it meets IS, once replacements have made
// matches the search never listed; see WalkOrderOfMatch.)
int32 ReplaceInChapter(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	int32& outMissing, int32& outLocked, int32& outRefused, bool& outNotWalked, bool& outWalkFailed,
	RangeProgressBar* progressBar, int32 progressBase, int32& ioProgressReported,
	bool verifyOnly = false, bool* outChanged = nil)
{
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;
	outNotWalked = false;
	outWalkFailed = false;
	if (outChanged != nil)
		*outChanged = false;

	// walkOrder -> row index, plus the set of walk orders to replace. The rows are stored in PAGE
	// order and the walk runs in DOCUMENT order, so walkOrder is what joins them - and, in the
	// writing pass, each match is first recognised by its position (WalkOrderOfMatch), because a
	// replacement can put a match in the walk's way that the search never counted.
	std::map<int32, int32> rowByWalkOrder;
	std::set<int32> targets;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);

	// Every row's position NOW (see RowNow), starting from where the search found it. That is the
	// text as it stands: the verify pass has just confirmed the chapter has not moved under the rows.
	// Every replacement carries it from there. Filled for the writing pass only - the verify pass
	// writes nothing, so nothing moves, and it lines rows up by its own test.
	std::vector<RowNow> rowNow;
	if (!verifyOnly && hitCount > 0)
		rowNow.resize(static_cast<size_t>(hitCount));

	// The rows the report will keep, whose lines are read back once the walk is over from rowNow and
	// not from the model's range: the model holds where a replacement was WRITTEN, or where the
	// search FOUND a match, and a later replacement can have moved either. (A list of replaced row
	// indices stood here until 2026-09-25 and read the model's range back, on the belief that "the
	// walk only moves forward" - see RowNow.)
	//
	// Two ways onto it: a checked row joins as the walk passes it (replaced, locked, refused), and a
	// LOCKED unchecked row is put on right here, below - so it comes out right even when the walk ends
	// at the last checked row before ever reaching it.
	std::vector<int32> keptRows;

	for (int32 i = 0; i < hitCount; ++i)
	{
		const int32 walkOrder = KBSResultModel::GetHitWalkOrder(chapterIdx, i);
		bool checked = false, replaced = false, locked = false;
		const bool haveFlags = KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked);
		// A report's own rows keep their ticks, and a report can run a Change Checked on its taken-back
		// rows (2026-09-27, B): work is what ReplaceInChapterOneByOne writes - not replaced, and no
		// outcome but "taken back".
		const bool isWork = haveFlags && checked && !replaced
			&& KBSResultModel::IsWorkOutcome(KBSResultModel::GetHitOutcome(chapterIdx, i));
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
			if (isWork)
			{
				// In verify, a ticked row that no walk can ever reach is reported as a change and
				// nothing is marked: the pass writes nothing at all, outcomes included, and the
				// run is about to be stopped anyway.
				if (verifyOnly)
				{
					if (outChanged != nil)
						*outChanged = true;
					return 0;
				}
				++outMissing;
				KBSResultModel::SetHitOutcome(chapterIdx, i, KBSResultModel::kOutcomeMissing);
			}
			continue;
		}
		rowByWalkOrder[walkOrder] = i;
		if (isWork)
			targets.insert(walkOrder);

		if (verifyOnly)
			continue;
		UID storyUID = kInvalidUID;
		TextIndex rowStart = kInvalidTextIndex, rowEnd = kInvalidTextIndex;
		uint64 rowHash = 0;
		if (KBSResultModel::GetHitMatchIdentity(chapterIdx, i, storyUID, rowStart, rowEnd, rowHash))
			SetRowAt(docRef.GetDataBase(), rowNow[static_cast<size_t>(i)], storyUID, rowStart, rowEnd);

		// A locked row the report will keep (it never had a box, so it is never checked).
		if (haveFlags && locked && !replaced && !checked && rowNow[static_cast<size_t>(i)].known)
			keptRows.push_back(i);
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
		// ***** ONE OF THE TWO EXITS THAT ARE PAST Initialize. ***** Every refusal above this line
		// is before the walker was given anything to walk, so there is nothing to stop; this one is
		// after, and so is the verify pass's refusal in the walk below - both halt before they
		// return, and this comment said "the one exit" until the verify pass was added (2026-08-10),
		// which is how the second one came to be written without a halt at all. A
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

	// (rowNow and keptRows are declared above the row scan at the top of this function, which fills
	// the one and seeds the other with the locked rows - see there.)

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
		//
		// The verify pass runs under the OPENING bar, which its caller moves per chapter - it is
		// not sized in hits and must not be driven from in here.
		if (!verifyOnly)
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

		// ***** WHICH ROW IS THIS MATCH? ASKED BY POSITION, NOT BY COUNT. ***** A replacement can
		// also make a new match OUTSIDE the text it wrote - GREP's ^ and $ read past the point the walk
		// resumes from - and counting it took the next row's walk order: that row was stamped
		// "replaced" while its text stayed, and a match nobody had listed was written instead
		// (measured 2026-09-25; see WalkOrderOfMatch). A match at no row's position is stepped over
		// like the one above; a match at a LATER row's position means the rows in between are gone,
		// and the walk moves on to that row, leaving those in targets to be counted missing.
		//
		// Writing pass only. The verify pass writes nothing, so it meets exactly the matches the
		// search met, and it has its own test (the start of each ticked row) a few lines below.
		if (!verifyOnly)
		{
			const int32 matchWalkOrder = WalkOrderOfMatch(docRef.GetDataBase(), rowNow, rowByWalkOrder, walkIndex,
				story.GetUID(), start, end);
			if (matchWalkOrder < 0)
				continue;
			walkIndex = matchWalkOrder;
		}

		// An unselected hit is counted past. ReplaceChecked ends by turning the panel into a report
		// of the run (KeepCheckedRows), which drops the rows the user had unchecked - so such a row
		// is on its way out of the list and nothing is read back for it.
		//
		// ***** EXCEPT A LOCKED ONE, WHICH THE REPORT KEEPS. ***** It never had a box to tick, it is
		// kept so the report can account for what the search found and the replace could not touch
		// (KeepCheckedRows), and it is jumped to by its range like any other row. It is put on
		// keptRows BEFORE the walk, not here: the walk stops at the last checked row, and a locked
		// row after it would never be passed at all. (Until 2026-09-25 this said "a row left alone
		// here is on its way out of the list anyway", and the locked ones were not; see RowNow.)
		if (targets.find(walkIndex) != targets.end())
		{
			const std::map<int32, int32>::const_iterator row = rowByWalkOrder.find(walkIndex);
			const int32 hitIdx = (row != rowByWalkOrder.end()) ? row->second : -1;

			// ***** VERIFY: DOES THIS MATCH STILL BEGIN WHERE THE SEARCH FOUND IT? *****
			// The whole of the verify pass is these few lines - everything above and below is the
			// walk itself, which is why the two passes share it. The story is compared as well as
			// the index: an index means nothing without the story it counts into.
			//
			// A ticked row whose identity cannot be read is treated as changed. It is the same
			// judgement the rest of this pass makes - we are about to rewrite the user's text on
			// the strength of these positions, so "cannot tell" is not good enough to proceed on.
			if (verifyOnly)
			{
				UID expectStory = kInvalidUID;
				TextIndex expectStart = kInvalidTextIndex, expectEnd = kInvalidTextIndex;
				uint64 expectHash = 0;
				const bool haveIdentity = (hitIdx >= 0)
					&& KBSResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, expectStory,
							expectStart, expectEnd, expectHash);
				if (!haveIdentity || story.GetUID() != expectStory || start != expectStart)
				{
					// ***** HALT BEFORE LEAVING. ***** This return is PAST Initialize, so the
					// walker is still walking - the same case, and the same two lines, as the
					// selUtils exit above and the end of the walk below. The walker comes from
					// the session's service registry, so it is not ours to leave in that state:
					// the next caller that guards its own Initialize with IsWalking CONTINUES
					// this walk instead of starting one, which is what InDesign's own
					// Find/Change does (SnpFindAndReplace.cpp:772). KBS itself would not have
					// noticed - the top of this function halts before it starts - but a refusal
					// here is the ordinary way a replace ends when the user has edited the
					// document, so it is the LIKELIEST exit of the three, not the rarest.
					if (walker->IsWalking())
						walker->Halt();
					if (outChanged != nil)
						*outChanged = true;
					return 0;
				}
				targets.erase(walkIndex);
				++walkIndex;
				continue;
			}

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
				{
					KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeLocked);
					// Kept by the report and jumped to by its range - so the range is the one the
					// walk is standing on, carried past what comes after (see RowNow).
					KeepRowAt(docRef.GetDataBase(), rowNow, keptRows, hitIdx, story.GetUID(), start, end);
				}
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
			//
			// Which thread the match is in, and how far into it, asked BEFORE the command: that is
			// the state every row's offset is kept in (see CarryRowsPast below).
			UID matchDict = kInvalidUID;
			uint32 matchKey = 0;
			TextIndex matchThreadStart = kInvalidTextIndex;
			const bool haveMatchThread = ThreadAt(docRef.GetDataBase(), story.GetUID(), start,
				matchDict, matchKey, matchThreadStart);

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

				// Every row that lies after the text just rewritten moves with it - the rows still to
				// come, which the walk recognises by where they are now (WalkOrderOfMatch), and the
				// rows already passed, which in the walk's order need not lie before it (see RowNow) -
				// and THEN this row is put at the range the command reports writing. In that order,
				// so the row is not moved by its own replacement.
				//
				// [start, end) is the match as it stood the moment before the command, taken as offsets
				// into its thread as it stood then - the state every row's offset is kept in, which is
				// what makes the comparison inside CarryRowsPast a comparison of like with like.
				// Rows in OTHER threads are not touched: their threads' starts move by themselves,
				// including when this replacement took a whole thread away (see RowNow).
				if (haveMatchThread)
					CarryRowsPast(rowNow, story.GetUID(), matchDict, matchKey, start - matchThreadStart,
						end - matchThreadStart, replacedEnd - replacedStart);
				KeepRowAt(docRef.GetDataBase(), rowNow, keptRows, hitIdx, replacedStory.GetUID(),
					replacedStart, replacedEnd);
			}
			else
			{
				// The command would not run here. RunWalkerCmd has already cleared the error state
				// (it has to - a standing error would roll the whole sequence back), so without
				// this counter the hit just vanishes: the row came up, nothing was written, and the
				// replaced total silently comes up short with nothing to explain it.
				++outRefused;
				KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeRefused);
				// The report keeps it (its outcome says why), so it is carried like a locked row.
				KeepRowAt(docRef.GetDataBase(), rowNow, keptRows, hitIdx, story.GetUID(), start, end);
			}
			// Whether or not the command took, this walk order is dealt with: leaving it in
			// targets would make the chapter look like it never lined up.
			targets.erase(walkIndex);
		}
		++walkIndex;
	}

	if (walker->IsWalking())
		walker->Halt();

	// ***** THE VERIFY PASS ENDS HERE, and what is left in targets is the answer. ***** A ticked
	// row the walk never reached is as much a change as one that moved: the match the results
	// promise is not there to be replaced. Nothing below this line runs in verify - no row is
	// re-read, no outcome is marked, nothing is counted.
	if (verifyOnly)
	{
		if (!targets.empty() && outChanged != nil)
			*outChanged = true;
		return 0;
	}

	// The chapter has stopped changing, so now each row the report keeps is moved to where its text
	// ended up and given the line it stands on.
	//
	// NOT while the walk was running: matches share paragraphs, and a line read at the moment its
	// own match was written still shows the LATER matches in that paragraph as they were before.
	// Changing every "cat" in "cat and dog and cat" to "kitten" left the first row reading
	// "kitten and dog and cat" for good, while the document read "kitten and dog and kitten"
	// (reported 2026-07-28). Every row now reads the paragraph in its final state.
	//
	// ***** FROM THE CARRIED RANGE, NOT THE ONE THE MODEL HOLDS. ***** This said "the range each row
	// stored is still the one to read: the walk only ever moves forward" until 2026-09-25, and the
	// walk does not - see RowNow for the two measured ways it goes back. keptRows names every row
	// the report will show that sits in this chapter's text (replaced, locked, refused), and rowNow
	// has each carried past every replacement made after it was taken, so SetHitRange hands the model
	// the range the text stands at now and the line and the hash are both read from there.
	//
	// Nothing to do when nothing was replaced: then nothing moved, and every range is already true.
	//
	// Reading, not writing, so it needs no command sequence of its own - and it opens none. It runs
	// INSIDE whichever sequence the caller has standing (there is no per-chapter sequence any more -
	// see the note above the walk), which costs nothing, because not one step of it is a command.
	// A run the user cancels does reach this point, and reads text that is about to be rolled back -
	// but the rows are rolled back with it (KBSResultModel::RollBackRows - SetHitRange and
	// SetHitSegments both back the row up first), so nothing of it survives.
	if (replacedCount > 0)
	{
		IDataBase* const db = docRef.GetDataBase();
		for (size_t r = 0; r < keptRows.size(); ++r)
		{
			const int32 hitIdx = keptRows[r];
			const RowNow& kept = rowNow[static_cast<size_t>(hitIdx)];
			// A row whose thread is gone - a replacement took the footnote it sat in away - has no
			// text left to stand at, and keeps what it had.
			TextIndex keptStart = kInvalidTextIndex;
			if (!RowStartNow(db, kept, keptStart))
				continue;
			const TextIndex keptEnd = keptStart + kept.length;
			KBSResultModel::SetHitRange(chapterIdx, hitIdx, kept.story, keptStart, keptEnd);
			// ***** BOTH of the row's descriptions of itself, read from the SAME range, written in
			// the SAME call. ***** The three segments are what the row DRAWS; the hash is what the
			// same-occurrence test COMPARES, and a jump into this row runs that test. Reading only
			// the segments left the hash describing the text that was here BEFORE the replacement,
			// so every replaced row answered a click with "the replacement is no longer here"
			// (2026-08-04 to 2026-08-05 - see SetHitSegments).
			const UIDRef rowStoryRef(db, kept.story);
			PMString pre, match, post;
			KBSSearchEngine::SplitLineAroundMatch(rowStoryRef, keptStart, keptEnd, pre, match, post);
			KBSResultModel::SetHitSegments(chapterIdx, hitIdx, pre, match, post,
				KBSSearchEngine::HashMatchText(rowStoryRef, keptStart, keptEnd));
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
		// ***** NOT "cancelled" WHEN NOBODY CANCELLED. ***** The sequence was about to be committed
		// with the error state standing, which would have rolled every replacement back on its own -
		// the difference is that the run now says so, instead of reporting a count of replacements
		// that no longer exist. InDesign's own wording for the failure is quoted when there is one:
		// it is the only description of what went wrong that anybody has.
		if (t.refusedByShape)
		{
			outSummary.Append("Replace not run - ");
			outSummary.Append(t.errorText);
			outSummary.Append(" - so nothing was changed.");
			return;
		}
		if (t.stoppedByMismatch)
		{
			// Said as a failure, not as a cancel (the user's call, 2026-09-26): nothing was retried.
			outSummary.Append("Replace stopped - the tracked changes did not line up with the results, so nothing was changed (");
			outSummary.Append(t.errorText);
			outSummary.Append(").");
			return;
		}
		if (t.stoppedByError)
		{
			outSummary.Append("Replace stopped - InDesign reported an error, so nothing was changed");
			if (!t.errorText.IsEmpty())
			{
				outSummary.Append(" (\"");
				outSummary.Append(t.errorText);
				outSummary.Append("\")");
			}
			outSummary.Append(".");
			return;
		}

		outSummary.Append("Replace cancelled - nothing was changed.");
		return;
	}

	// The count leads, so it survives the narrow status field's tail truncation.
	outSummary.AppendNumber(t.replaced);
	// "in N chapter(s)" only for a book: a document's replace said "in 1 chapter(s)" until 2026-09-28,
	// a word the search's own summary had never used there (KBSSearchEngine: "hit(s)." alone).
	if (KBSResultModel::IsFromBook())
	{
		outSummary.Append(" replaced in ");
		outSummary.AppendNumber(t.chaptersTouched);
		outSummary.Append(" chapter(s).");
	}
	else
	{
		outSummary.Append(" replaced.");
	}

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

	// The documents' pending tracked changes, accepted before the first write (2026-09-27, the user's
	// call) - said, because they no longer show in the Track Changes panel.
	if (t.acceptedFirst > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.acceptedFirst);
		outSummary.Append(" pending tracked change(s) accepted first.");
	}

	// Ticked rows in the endnotes, left alone on purpose (2026-09-27): a match ends an endnote, and
	// InDesign's replace leaves an endnote it writes at the end of with a character nobody can delete.
	if (t.endnoteLeft > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.endnoteLeft);
		outSummary.Append(" hit(s) in endnotes not replaced - a match ends an endnote, and InDesign's replace breaks an endnote there.");
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
			// IsDocStillOpen FIRST - the order ReleaseHeldDoc itself spells out ("asked first, and
			// the order is the whole point"): docRef is only (IDataBase*, UID), and for a chapter
			// closed under the run - the very case the three questions below allow for - that
			// pointer is dangling, and SetModified through it is undefined behaviour. This deref
			// stood in FRONT of any liveness test until 2026-08-09, while the release two steps
			// down asked the question properly.
			if (KBSBookScope::IsDocStillOpen(pending[pi].docRef))
			{
				IDataBase* const chapterDB = pending[pi].docRef.GetDataBase();
				if (chapterDB != nil)
					chapterDB->SetModified(kFalse);
			}
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
// Both ways out of a run that stops BEFORE the command sequence opens: the opening bar's Cancel,
// and a Cancel on the "this chapter has been edited" alert. Neither has written a character, so
// there is nothing to roll back - what is left is handing the chapters back and saying so.
//
// resultsAreStale says WHICH of the two it was, in terms of what it means rather than where it came
// from: the edited-chapter alert only appears because the document no longer holds the text these
// rows describe, and a list that no longer describes anything must not be left on screen offering
// to replace things (user's call, 2026-08-09). A Cancel on the bar is the other case entirely -
// nothing has changed, the rows are still true, and throwing them away would lose a search the user
// may have waited a long time for.
int32 StopBeforeAnythingIsWritten(const std::vector<PendingChapter>& pending, RunTotals& totals,
	PMString& outSummary, bool resultsAreStale)
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

	// ***** BACK TO BEFORE THE SEARCH. ***** The rows were found in text that is not there any more,
	// which is the whole reason the user was asked and the whole reason they said no. Leaving them
	// up invites a second run against a list that describes the old text - the same reasoning, and
	// the same three calls, as RefuseChangedQuery above.
	//
	// The three go together, always: ReleaseSearchedBook because "every KBSResultModel::Clear() is
	// paired with one" (KBSBookScope.cpp:81), and ForgetSearchedFindFormat because the format the
	// replace's door compares against belongs to the rows going away here.
	//
	// The PANEL is not touched from here: the caller redraws the tree and writes this summary to the
	// status line (KBSActionComponent.cpp:344-345), and the illustration follows the model by itself
	// - Clear() puts HasRun back down, so KBSPanelIcon::Choose returns the picture the panel had
	// before anything was run.
	if (resultsAreStale)
	{
		KBSResultModel::Clear();
		KBSBookScope::ReleaseSearchedBook();
		KBSSearchEngine::ForgetSearchedFindFormat();
		// ***** SHORT ENOUGH TO BE READ WHOLE. ***** This follows BuildSummary's own cancel sentence,
		// so what the panel draws is both of them - and at the panel's floor the two used to come to
		// 128 characters against a box that held about 88: the line stopped at "the document has" and
		// the reader never saw that they were being asked to search again (measured on the running
		// panel, 2026-08-10). The block is four lines now (KBSPanelMetrics) and this says the same
		// thing in half the room. WHY the document no longer matches is the alert's job
		// (TellResultsWentStale), which has a whole dialog to say it in.
		outSummary.Append(" Results cleared - the document changed. Search again.");
	}

	// The chapters that would not close, at the end, the way every other exit says it.
	KBSBookScope::AppendUnclosedNote(outSummary, unclosed);
	return 0;
}

// ***** THE MATCHES THESE ROWS DESCRIBE ARE NOT WHERE THEY WERE. *****
//
// The run walks each chapter again and gives the Nth match to the Nth ticked row, so anything that
// adds, removes or moves a match between the search and now would hand a replacement to an
// occurrence the user never ticked. The verify walk in the resolve pass has just found exactly
// that, and the run is over before it started - this only tells them.
//
// ***** IT IS A STATEMENT, NOT A QUESTION (user's decision, 2026-08-10). ***** There is no "carry
// on anyway": a work list that has come apart cannot be replaced safely whatever anyone answers.
// A per-chapter "the text has been edited - OK / Cancel?" prompt stood here from 2026-08-08 until
// then, resting on KBSEditStamp's per-chapter fingerprint.
//
// Shown with the progress bar DOWN - the resolve pass closes its own bar before this goes up, so a
// modal alert never stands over a modal bar - and before the command sequence opens, so not one
// character has been written when it appears.
//
// With userInteractionLevel at NEVER_INTERACT the alert is never drawn (CAlert::SetShowAlerts,
// CAlert.h:218-226: "If showAlert is kFalse, no alerts will be displayed") and the run stops all the
// same: the caller returns through StopBeforeAnythingIsWritten either way, and a scripted run reads
// what happened off the panel's status line (app.kfcStatus until 2026-09-27; KIDMCP's inspect_ui now).
// (That citation read CAlert.h:178-183 until 2026-08-10, which is the argument list of a call this
// file does not make - WarningAlertWithDontShowAgain. The fact was measured and is right; only the
// line was pointing at the wrong function.)
// @param chapterIdx the chapter the mismatch was found in, or -1.
void TellResultsWentStale(int32 chapterIdx)
{
	// A DOCUMENT-scope run has one "chapter" and it is the front document, which has no name to put
	// here - the book wording would name a chapter of a book that is not there.
	PMString msg;
	if (chapterIdx < 0 || !KBSResultModel::IsFromBook())
		msg = KBSLoc::Text(kKBSStaleResultsDocKey, KBSJa::kStaleResultsDoc);
	else
	{
		// The model's display name for the chapter row, marked untranslatable BEFORE it goes into a
		// translated string - the order every other line of this plug-in's prompts takes.
		PMString name;
		int32 chapterHits = 0;
		KBSResultModel::GetChapterDisplay(chapterIdx, name, chapterHits);
		name.SetTranslatable(kFalse);
		msg = KBSLoc::Text(kKBSStaleResultsOneKey, KBSJa::kStaleResultsOne);
		::ReplaceStringParameters(&msg, name);
	}

	// ***** ONE sentence since 2026-08-10 (user's call). ***** A second line saying "please search
	// again" stood under this one. The alert's job is to state that the replace stopped and that
	// nothing was written; what to do next is on the status line the panel is left showing.
	// Finished text: every part was translated as it was taken, so the alert must not look the
	// result up again (CAlert translates the message it is handed).
	msg.SetTranslatable(kFalse);

	// ***** WarningAlert - THE OFFICIAL CALL FOR EXACTLY THIS: a message and a warning icon. *****
	// CAlert.h:75-79 ("Modal alert, displaying text plus eWarningIcon"). It is what the product uses
	// to make this kind of statement: spellpanel says "Change All cannot run" this way
	// (SpellChangeAllObserver.cpp:292), and so do SpellSkipObserver.cpp:519,543 and
	// PrivateSpellingUtils.cpp:816.
	//
	// ***** A ONE-BUTTON ModalAlert STOOD HERE, and its reason was already dead when it was written
	// ***** (found in the API audit, 2026-08-10). ***** The reason given was that ModalAlert is the
	// shape IAlertHandler::HandleAlert takes - message plus three button labels plus a default - and
	// so the shape KT's alert recorder implements, which would let an automated run read the alert
	// back. Measured the same day, BOTH ways: it does not reach app.ktLastAlert either way, because
	// under NEVER_INTERACT CAlert answers without asking the handler at all. (KT's recorder was armed
	// and answering - app.ktAlertAnswer read back "1" - so the suppression is CAlert's own, not KT's.)
	// With nothing to gain, the all-powerful call was only carrying a default-button argument that
	// means nothing when there is one button, and a return value with nothing to decide.
	//
	// So an automated run cannot read this alert THROUGH KT. It can read it through Win32, which is
	// how it is tested now (measured 2026-08-10, correcting the line that stood here saying the eye
	// was the only way): left at INTERACT_WITH_ALL the alert is drawn for real, and a real CAlert is
	// a #32770 whose body sits in a hidden Edit child that answers WM_GETTEXT. The wording, the
	// warning icon and the untouched document are all checked by
	// work/kbs-selftest/run-stale-alert-shot.ps1.
	CAlert::WarningAlert(msg);
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
			outSummary.Append("The Find/Change dialog is on a different tab than when this search ran. Search again.");
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
		outSummary.Append("These results did not come from a text tab - use InDesign's own Find/Change to change them.");
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
		outSummary.Append("The Find/Change tab could not be set - nothing was changed. Try reopening Edit > Find/Change.");
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
			outSummary.Append("The Find/Change query has changed since this search - the results have been cleared. Search again.");
			return true;
		}
	}

	return false;
}

int32 KBSReplaceEngine::ReplaceChecked(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);
	// Forward, as the search was (2026-09-26): the verify pass and Change All both follow the session's
	// direction. Outside the run's sequence, and put back as the function ends.
	KBSForwardSearchScope forward;

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
	if (KBSResultModel::IsShowingReplaceOutcome() && !KBSResultModel::AnyRejectedRowOpen())
	{
		outSummary.Append("This is the last replace's report - search again to replace more.");
		return 0;
	}
	if (KBSResultModel::GetCheckedCount() <= 0)
	{
		outSummary.Append("Nothing checked.");
		return 0;
	}
	// (Results that stop short of the scope - KBSResultModel::SetStoppedShort - were refused here while
	//  the replace was Change All, which would have written the matches past where the search stopped.
	//  One match at a time writes the ticked rows and nothing else, so the refusal went in the
	//  2026-09-27 cleanup. A chapter whose search broke off is still caught by the verify pass: a
	//  ticked row its walk cannot reach stops the run.)

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
	// Set by the verify walk inside the pass - see it for what it asks and why it asks it there.
	bool changedSinceSearch = false;
	int32 changedChapterIdx = -1;
	{
	// ***** THIS PASS HANDS THE ERROR STATE BACK THE WAY IT FOUND IT. ***** It opens documents and,
	// since 2026-08-10, walks every chapter to verify the ticked positions - work that can raise the
	// global error state without any of it being a failure of this run. What makes that matter is
	// what comes after: the command sequence below decides between committing and rolling back by
	// READING that state (the abort at the end of this function), so an error left standing by this
	// pass would throw away a replace that went through perfectly.
	//
	// ***** PRESERVE, THEN CLEAR - and it is Adobe's own base class that spells the pair out. *****
	// CDialogObserver.cpp:392-394 ("GlobalErrorStatePreserver followed by setting global error to
	// success"); also CPathCreationTracker.cpp:666. The contract is ErrorUtils.h:115-117 - the
	// constructor saves the caller's error state and the destructor puts it back, here at the closing
	// brace of this pass. The clear is the other half: the pass should not START on an error either,
	// since a standing one fails whatever it does next (opening a chapter, above all).
	//
	// Clearing WITHOUT preserving is what stood here for one hour on 2026-08-10, as a single
	// PMSetGlobalErrorCode in front of the sequence. It worked, but it decided for this function's
	// CALLER that their error state did not matter, and it guarded only the one path that reaches
	// the sequence - the pass's early exits leaked. This guards every way out of the pass, including
	// the two that return through StopBeforeAnythingIsWritten (which keeps a clear of its own: it
	// closes documents after this scope has already ended).
	GlobalErrorStatePreserver passErrorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

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

		// ***** AND THE CHAPTER IS WALKED AGAIN, WRITING NOTHING, TO SEE WHETHER THE RESULTS STILL
		// DESCRIBE IT. ***** Every ticked row carries the position the search found it at; this
		// walk visits the same matches in the same order, so if one of them no longer BEGINS where
		// it began, the document has moved under the results and the Nth-match join would hand a
		// replacement to an occurrence the user never ticked. Whatever the cause - text edited, a
		// story added or deleted, a layer hidden or locked, a condition switched - the effect is
		// the same and it is the effect that is measured (2026-08-10, the user's design; it
		// replaced KBSEditStamp, which tried to enumerate the causes instead).
		//
		// ***** HERE, and here is the only place it can be. ***** The chapter has just become a
		// live document, and NOT ONE CHARACTER has been written by this run - the command sequence
		// does not open until this pass is over. So a chapter found to have changed is stopped
		// with nothing to roll back at all.
		//
		// The first chapter that has changed ends the pass: the run is all-or-nothing, so there is
		// nothing to learn from opening the rest.
		//
		// docRef, not the UIDRef the search recorded: this one was resolved BY FILE just above,
		// while the recorded one may name a database that has since been closed and its address
		// reused (the resolve above is entirely about that).
		{
			int32 vMissing = 0, vLocked = 0, vRefused = 0;
			bool vNotWalked = false, vWalkFailed = false, vChanged = false;
			int32 vReported = 0;
			ReplaceInChapter(ci, docRef, scopeOptions, vMissing, vLocked, vRefused,
				vNotWalked, vWalkFailed, nil /*no bar of its own*/, 0, vReported,
				true /*verifyOnly*/, &vChanged);
			// ***** WHAT "CHANGED" COVERS HERE, EXACTLY - the two walk failures do not divide the
			// ***** way this comment claimed until 2026-08-10 ("a chapter that could not be walked
			// ***** is not a chapter that has changed" - true of one of them, not of both).
			//
			// A walk that never STARTED (no database, no scope, no walker: vNotWalked) answers
			// false, and rightly: nothing was compared, nothing will be written either, and the
			// replace pass meets the chapter again and names it in the summary (chaptersNotWalked).
			//
			// A walk that started and then BROKE OFF (vWalkFailed) answers true - through the
			// ticked rows it never reached, which are left in its target set. That is deliberate
			// and it is the safe answer: this run will not write to positions it could not check.
			// ***** It is not a "the document moved" answer, though. ***** The alert says the
			// results changed whichever it was. Wording the two apart would need a second string
			// and a failure nothing has been able to construct, so it is recorded here instead.
			//
			// vMissing / vLocked / vRefused are not read: the verify pass counts nothing at all
			// (see verifyOnly), and the replace pass counts them for real immediately afterwards.
			if (vChanged)
			{
				changedSinceSearch = true;
				changedChapterIdx = ci;
				break;
			}
		}
	}

	// ASK ONCE MORE, now that the pass is over: the reading at the top only ever sees a cancel that
	// arrived while an EARLIER chapter was opening, and one pressed during the last chapter's open
	// would have no next pass to be noticed in. The same shape, and the same reason, as the reading
	// at the end of the replace loop below.
	if (!cancelledWhileOpening && openBar.WasCancelled(kFalse))
		cancelledWhileOpening = true;

	}	// end of the scope the opening bar lived in - THE BAR IS DOWN FROM HERE

	// kFalse: the bar's Cancel says "not now", not "these rows are wrong". Nothing has been edited
	// and nothing has been written, so the results stay exactly as they were.
	if (cancelledWhileOpening)
		return StopBeforeAnythingIsWritten(pending, totals, outSummary, false /*resultsAreStale*/);

	// ***** THE DOCUMENT MOVED UNDER THE RESULTS - SO THE RUN DOES NOT START. *****
	//
	// The verify walk in the pass above found a ticked hit that no longer begins where the search
	// found it. It is NOT put to the user as a question: a work list that has come apart cannot be
	// replaced safely whatever they answer, so the run is simply stopped and they are told (the
	// user's decision, 2026-08-10 - it replaced a per-chapter "carry on / cancel?" prompt).
	//
	// Said here rather than inside the pass because the opening bar is modal and so is the alert,
	// and the two must not stand at once. The pass has just taken its bar down.
	//
	// kTrue: the rows go with the run. They describe text that is not there any more, and the
	// status line says so and asks for another search (see StopBeforeAnythingIsWritten).
	if (changedSinceSearch)
	{
		TellResultsWentStale(changedChapterIdx);
		return StopBeforeAnythingIsWritten(pending, totals, outSummary, true /*resultsAreStale*/);
	}

	// ***** THE REPLACE'S OWN BAR. ***** A second object, not the one the pass above used - see the
	// note there for why the run cannot carry a single bar across both.
	//
	// ***** WHAT "STOPPED" MEANS HERE, EXACTLY. ***** WasCancelled is read between chapters, between
	// the STORIES of a chapter (ReplaceInChapterOneByOne - a story's walk holds the walker's critical
	// section, so it is not asked inside one), and once more when the loop ends. A Cancel pressed
	// during a story is heard when that story is done; the whole sequence is then aborted and every
	// character put back. (The stories were Change All's from 2026-09-26 to 2026-09-27, one command
	// each; before that, the one-at-a-time walk ran a chapter at a time and was asked between chapters.)
	// DisableChildProgressBars keeps anything the replacements raise from putting up bars of their
	// own (the chapter opens the other bar covers are the same case, and it says so there).
	//
	// SIZED IN HITS, not chapters, and moved by ReplaceInChapterOneByOne as it goes, a replace at a
	// time (progressBase below).
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
	// Nothing inside opens a sequence of its own: every Change All, take-back and Track Changes switch
	// of every chapter goes straight into this one (see the note in ReplaceInChapter on why a nested
	// per-chapter sequence was measured to be harmful). (This said "the per-chapter sequences inside
	// ReplaceInChapter nest within this one" - there have been none since 2026-07-31.)
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
	// ***** THE ERROR STATE THIS SEQUENCE READS AT ITS END IS ITS OWN - see the resolve pass, which
	// ***** hands its own back before this line is reached (GlobalErrorStatePreserver, up there).
	// How this sequence ends is decided by reading the global error code (the abort at the bottom of
	// this function): anything standing there is taken as a failure nothing reported, and the whole
	// run is rolled back and the user told why. That reading is only honest about failures from
	// INSIDE the sequence, and the pass above - which opens documents and walks every chapter - is
	// outside it. Nothing between the two runs a command: a progress bar is built and the rows are
	// backed up, and neither touches the error state.
	//
	// ***** EXCEPT ONE COMMAND, ON PURPOSE (2026-09-27): THE WRITING WALK'S DIRECTION. ***** A GREP query
	// holding ^ is written backward (WriteBackward, and the note above ReplaceInChapterOneByOne). The
	// session's direction is turned here - after the verify pass, which re-walks the search forward, and
	// before the sequence, so neither the Undo step nor an abort carries the switch - and put back when
	// this function returns, after the sequence has ended. A switch that fails clears its own error.
	const KBSBackwardSearchScope writeDirection(WriteBackward());
	IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KBS Replace");
	// ***** NAMED AGAIN (user's call, 2026-09-26): "Replace" / Japanese UI KBSJa::kReplaceStep. ***** It was left
	// unnamed on 2026-07-28 so InDesign would word the step itself - but an unnamed step is worded
	// by its LAST command, which is not the replace: while the run switched the user name (until
	// 2026-09-27) Edit > Undo read "Undo Set User Name" (measured, case undo-then-reject), and the
	// story's tracking switch put back (TrackingScope) is last now.
	// (The string passed above is TRACKING DATA, not that name - CmdUtils.h:134 - so it names this
	// caller in a lost-sequence report and nowhere else.)
	if (seq != nil)
		seq->SetName(KBSLoc::Text(kKBSReplaceStepKey, KBSJa::kReplaceStep));

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
		int32 replaced = 0;
		{
			// ***** EVERY REPLACE IS TRACKED (2026-09-26). ***** The story's own Track Changes setting is
			// handed back as it was found (TrackingScope). The records are signed with the user's own
			// name - InDesign's user name is not touched (2026-09-27; KBSTrackChange.h says why).
			// The whole text of every ticked row BEFORE anything is written - half of what a replaced
			// row's tracked change is found by later (Hit::originalText). The positions are the
			// search's own, which the verify pass has just vouched for.
			const int32 hitCount = KBSResultModel::GetHitCount(ci);
			std::vector<PMString> originals(static_cast<size_t>(hitCount > 0 ? hitCount : 0));
			std::vector<bool> haveOriginal(originals.size(), false);	// a zero-width match reads ""
			for (int32 hi = 0; hi < hitCount; ++hi)
			{
				bool checked = false, wasReplaced = false, isLocked = false;
				UID story = kInvalidUID;
				TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
				uint64 h = 0;
				if (KBSResultModel::GetHitFlags(ci, hi, checked, wasReplaced, isLocked) && checked && !wasReplaced
					&& KBSResultModel::GetHitMatchIdentity(ci, hi, story, a, b, h))
				{
					originals[static_cast<size_t>(hi)] = KBSTrackChange::ReadText(UIDRef(docRef.GetDataBase(), story), a, b - a);
					haveOriginal[static_cast<size_t>(hi)] = true;
				}
			}

			bool runCancelled = false;
			bool runFailed = false;
			PMString whyNot;
			int32 endnoteLeft = 0;
			int32 acceptedFirst = 0;
			// ***** ONE MATCH AT A TIME, STORY BY STORY (2026-09-27, the user's call). ***** Change All
			// over whole stories stood here from 2026-09-26 (removed in the 2026-09-27 cleanup).
			const bool wrote = ReplaceInChapterOneByOne(ci, docRef, scopeOptions,
				&progressBar, progressBase, progressReported, replaced, missing, locked, refused, endnoteLeft,
				acceptedFirst, walkFailed, runCancelled, runFailed, whyNot);
			totals.endnoteLeft += endnoteLeft;
			totals.acceptedFirst += acceptedFirst;
			if (runCancelled)
			{
				totals.cancelled = true;
				break;
			}
			if (runFailed)
			{
				totals.stoppedByMismatch = true;
				totals.errorText = whyNot;
				totals.errorText.SetTranslatable(kFalse);
				totals.cancelled = true;	// everything a cancel does, this needs too
				break;
			}
			if (!wrote)
			{
				// Refused before this chapter wrote anything (a row already replaced); the abort below
				// takes back what earlier chapters wrote.
				totals.refusedByShape = true;
				totals.errorText = whyNot;
				totals.errorText.SetTranslatable(kFalse);
				totals.cancelled = true;
				break;
			}

			// ...and the other half: what each replaced row now reads.
			for (int32 hi = 0; hi < hitCount; ++hi)
			{
				bool checked = false, isReplaced = false, isLocked = false;
				UID story = kInvalidUID;
				TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
				uint64 h = 0;
				if (KBSResultModel::GetHitFlags(ci, hi, checked, isReplaced, isLocked) && isReplaced
					&& haveOriginal[static_cast<size_t>(hi)]
					&& KBSResultModel::GetHitMatchIdentity(ci, hi, story, a, b, h))
					KBSResultModel::SetHitChangeTexts(ci, hi, originals[static_cast<size_t>(hi)],
						KBSTrackChange::ReadText(UIDRef(docRef.GetDataBase(), story), a, b - a));
			}
		}
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
		// ***** A COMMAND CAN REPORT SUCCESS AND STILL LEAVE THE ERROR STATE UP. ***** Ending a
		// sequence in that state rolls back everything it did - silently - while the summary would
		// go on saying "N replaced" (the 2026-07-31 measurement, seen from the other direction).
		// Every failure the run KNOWS about clears the state where it happens (RunWalkerCmd's two
		// doors, and the cancel's own clear below), so anything still standing at this line is a
		// failure that nothing reported.
		//
		// ***** WHICH IS WHY IT IS READ, NOT CLEARED. ***** A clear stood here for one day
		// (2026-08-09) and was replaced the same day on the user's call: clearing commits whatever
		// half-written state that unreported failure left behind, and says nothing about it -
		// trading a rollback the user can SEE for a corruption they cannot. A standing error means
		// this run must not be committed, which is exactly what a cancel already means and already
		// does below: abort, roll the rows back, restore the modified flags, hand the chapters back.
		// The only thing that has to be added is telling the user WHY.
		//
		// Asked BEFORE either ending, and before the abort raises anything of its own. Reading the
		// global error code after work that reports nothing back is the SDK's own idiom - the
		// closest match is textimportfilter/TxtImpFilter.cpp:435-441, where ITextModel::Insert
		// returns void and the code is the only answer there is (also xmldataupdater:470,
		// xmlcataloghandler:237, xdocbookworkflow:272).
		if (!totals.cancelled && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		{
			totals.stoppedByError = true;
			totals.errorText = ErrorUtils::PMGetGlobalErrorString();
			totals.errorText.SetTranslatable(kFalse);
			// Everything a cancel does, this needs too - so it IS one from here on.
			totals.cancelled = true;
		}

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
			// IsDocStillOpen FIRST - the same rule, spelled the same way, as this loop's twin in
			// HandBackChaptersWithNothingInThem: docRef is only (IDataBase*, UID), and for a
			// chapter closed under the run that pointer is dangling, so SetModified through it is
			// undefined behaviour. The twin got its guard on 2026-08-09; this was the one deref of
			// the pair that the same sweep missed (found in the pre-submission re-check).
			if (!KBSBookScope::IsDocStillOpen(pending[pi].docRef))
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

	// The saved report's heading, recorded on THIS exit alone: the panel is the replace's report
	// now (KeepCheckedRows above), so this sentence is what describes its rows. The two cancel
	// exits deliberately do not record - they leave the SEARCH's rows standing (rolled back, or
	// never written to), and the search's own summary keeps describing those.
	return totals.replaced;
}

bool KBSReplaceEngine::IsReplacing()
{
	return gReplacing;
}

// ======================================================================================================
// ***** REPLACE ONE ROW, FROM ITS RIGHT-CLICK MENU (2026-09-27, the user's call). ***** No prompt; the
// Track Changes note goes on the status line afterwards. The list stays a WORK LIST - unlike Change
// Checked, which turns it into a report - so the row reads "replaced" and every other row is carried
// to where its text now stands and numbered again by a fresh walk (RenumberWalkOrders), ready for the
// next Replace or a Change Checked. One undo step ("Replace"). The same doors as Change Checked, asked
// for this row: the query unchanged (RefuseChangedQuery), the row's text still the one the search found
// (MatchIsSameOccurrence - the jump's own test), and only the pending changes the row sits in or next
// to accepted first.
// ======================================================================================================

// The chapter walked again, and every row not yet replaced given the walk order of the match that
// stands at its place (story, start and length); -1 when none does. Replaced rows get -1: they are
// not work, and their old numbers would collide.
static void RenumberWalkOrders(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions)
{
	// FORWARD, whoever calls (2026-09-28): the numbers are what a Change Checked's verify pass counts by,
	// and that pass walks forwards. A Reject Change came here in whatever direction the Find/Change dialog
	// was set to - "search backwards" numbered the rows the other way round - and a row Replace with a
	// query holding ^ came here still inside its backward write (see ReplaceRowsNow).
	KBSForwardSearchScope forward;
	std::vector<KBSResultModel::Hit> hits;
	if (!KBSSearchEngine::CollectDocHits(docRef, scopeOptions, hits))
		return;
	std::map<std::pair<UID, std::pair<TextIndex, TextIndex> >, int32> byPlace;
	for (size_t h = 0; h < hits.size(); ++h)
		byPlace[std::make_pair(hits[h].storyUID, std::make_pair(hits[h].textStart, hits[h].textEnd))] = hits[h].walkOrder;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 hash = 0;
		int32 order = -1;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && !replaced
			&& KBSResultModel::GetHitMatchIdentity(chapterIdx, i, story, a, b, hash))
		{
			std::map<std::pair<UID, std::pair<TextIndex, TextIndex> >, int32>::const_iterator it =
				byPlace.find(std::make_pair(story, std::make_pair(a, b)));
			if (it != byPlace.end())
				order = it->second;
		}
		KBSResultModel::SetHitWalkOrder(chapterIdx, i, order);
	}
}

// ***** THE OTHER ROWS FOLLOW A REJECT OR A REDO (2026-09-27 re-check). ***** Taking one row's replace
// back, or writing it again, changes that row's length, and every row after it in the same thread moved
// with the text - which the model's stored ranges did not: the jump then compared the wrong characters
// ("missing"), and in a work list a Change Checked after it refused as stale. Each row is taken as
// "this far into this thread" BEFORE (SnapshotRows - RowNow, as the replace keeps them), carried past
// each change in text order (CarryPastChange, given where the change stands AFTER - the earlier changes
// of the same thread are already carried by then), and put back (WriteBackRows).
static void SnapshotRows(int32 chapterIdx, IDataBase* db, std::vector<RowNow>& out)
{
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	out.assign(static_cast<size_t>(hitCount > 0 ? hitCount : 0), RowNow());
	for (int32 i = 0; i < hitCount; ++i)
	{
		if (!RowStillStands(chapterIdx, i, db))
			continue;		// left as it is: known == false, never carried nor read back
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 hash = 0;
		if (KBSResultModel::GetHitMatchIdentity(chapterIdx, i, story, a, b, hash))
			SetRowAt(db, out[static_cast<size_t>(i)], story, a, b);
	}
}

static void CarryPastChange(IDataBase* db, std::vector<RowNow>& rows, UID story, TextIndex startAfter,
	int32 lengthBefore, int32 lengthAfter)
{
	UID dict = kInvalidUID;
	uint32 key = 0;
	TextIndex threadStart = kInvalidTextIndex;
	if (!ThreadAt(db, story, startAfter, dict, key, threadStart))
		return;
	const TextIndex offset = startAfter - threadStart;
	CarryRowsPast(rows, story, dict, key, offset, offset + lengthBefore, lengthAfter);
}

static void WriteBackRows(int32 chapterIdx, IDataBase* db, const std::vector<RowNow>& rows, const std::vector<int32>& skip)
{
	for (size_t i = 0; i < rows.size(); ++i)
	{
		if (std::find(skip.begin(), skip.end(), static_cast<int32>(i)) != skip.end())
			continue;
		const RowNow& row = rows[i];
		TextIndex at = kInvalidTextIndex;
		if (!RowStartNow(db, row, at))
			continue;
		const UIDRef storyRef(db, row.story);
		KBSResultModel::SetHitRange(chapterIdx, static_cast<int32>(i), row.story, at, at + row.length);
		PMString pre, match, post;
		KBSSearchEngine::SplitLineAroundMatch(storyRef, at, at + row.length, pre, match, post);
		KBSResultModel::SetHitSegments(chapterIdx, static_cast<int32>(i), pre, match, post,
			KBSSearchEngine::HashMatchText(storyRef, at, at + row.length));
	}
}

bool KBSReplaceEngine::CanReplaceHit(int32 chapterIdx, int32 hitIdx)
{
	bool checked = false, replaced = false, locked = false;
	return !gReplacing
		&& KBSResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked)
		&& !replaced && !locked
		&& KBSResultModel::IsWorkOutcome(KBSResultModel::GetHitOutcome(chapterIdx, hitIdx));
}

// The rows of one chapter replaced now, in ONE undo step - the right-click Replace of a row (one row) or
// of a story (its ticked rows). The callers have asked CanReplaceHit of every row.
static bool ReplaceRowsNow(int32 chapterIdx, const std::set<int32>& rowsToReplace, PMString& outStatus,
	const char* unit = "story")
{
	const int32 hitIdx = rowsToReplace.empty() ? -1 : *rowsToReplace.begin();	// the one row, for a row's Replace
	// Forward, as the search was - outside the sequence below (the walk's direction for a GREP query
	// holding ^ is turned below, also outside it).
	KBSForwardSearchScope forward;
	PMString refusal;
	if (KBSReplaceEngine::RefuseChangedQuery(refusal))
	{
		outStatus = "Replace: the Find/Change query changed since the search - search again. ";
		outStatus.Append(refusal);
		return false;
	}
	if (!KBSSearchEngine::CommitReplaceSide())
	{
		outStatus = "Replace: the Change To in Find/Change could not be stated - nothing was changed.";
		return false;
	}
	// The chapter's document, by its file first (a UIDRef can outlive its document - see the jump's
	// EnsureChapterReachable, whose shape this is).
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file))
	{
		outStatus = "Replace: the document of this row could not be found.";
		return false;
	}
	UIDRef reopened;
	if (KBSBookScope::ReopenChapterDoc(file, reopened))
	{
		docRef = reopened;
		KBSResultModel::RebindChapterDoc(chapterIdx, reopened);
	}
	else if (KBSBookScope::ChapterHasFile(file) || docRef.GetDataBase() == nil || !KBSBookScope::IsDocStillOpen(docRef))
	{
		outStatus = "Replace: the document of this row could not be opened.";
		return false;
	}
	// ***** A CHAPTER THIS OPENED WITHOUT A WINDOW GETS ONE (2026-09-27 re-check). ***** ReopenChapterDoc
	// opens a closed chapter windowless and holds it; a replace left in it would be seen by nobody and
	// could not be saved. Change Checked gives every chapter it wrote to a window (ShowChapterWindow);
	// the one-row Replace does the same, after its sequence, whether it went through or not.
	struct WindowAfter
	{
		UIDRef doc;
		bool want;
		WindowAfter(const UIDRef& d, bool w) : doc(d), want(w) {}
		~WindowAfter() { if (want) (void)KBSBookScope::ShowChapterWindow(doc); }
	} windowAfter(docRef, KBSBookScope::IsHeldDoc(docRef));
	IDataBase* const db = docRef.GetDataBase();
	std::map<int32, PMString> originals;		// each row's text before, for Hit::originalText
	for (std::set<int32>::const_iterator r = rowsToReplace.begin(); r != rowsToReplace.end(); ++r)
	{
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		if (!KBSResultModel::GetHitMatchIdentity(chapterIdx, *r, story, start, end, hash)
			|| !KBSSearchEngine::MatchIsSameOccurrence(UIDRef(db, story), start, end, story, start, end, hash))
		{
			outStatus = (rowsToReplace.size() == 1)
				? "Replace: the text of this row has changed since the search - search again."
				: "Replace: the text of a row of this story has changed since the search - search again.";
			return false;
		}
		originals[*r] = KBSTrackChange::ReadText(UIDRef(db, story), start, end - start);
	}
	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);

	int32 replaced = 0, missing = 0, locked = 0, refused = 0, endnoteLeft = 0, accepted = 0;
	bool walkFailed = false, cancelled = false, failed = false;
	PMString whyNot;
	bool ok = false;
	{
		// ***** BACKWARDS FOR THE WRITE ONLY (2026-09-28). ***** A query holding ^ is written backwards
		// (WriteBackward). This scope stood to the end of the function until 2026-09-28, so the
		// renumbering walk below (RenumberWalkOrders) ran backwards too, and gave the rows numbers the
		// other way round from the forward walk a Change Checked's verify pass counts by - which then
		// refused the Change Checked as "changed since the search" (case caret-row-then-change).
		const KBSBackwardSearchScope writeDirection(WriteBackward());

		ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
		if (sequence == nil)
		{
			outStatus = "Replace: InDesign would not start a command sequence - nothing was changed.";
			return false;
		}
		sequence->SetName(KBSLoc::Text(kKBSReplaceStepKey, KBSJa::kReplaceStep));
		KBSResultModel::BeginRowBackup();
		int32 progressReported = 0;
		const bool wrote = ReplaceInChapterOneByOne(chapterIdx, docRef, scopeOptions, nil, 0, progressReported,
			replaced, missing, locked, refused, endnoteLeft, accepted, walkFailed, cancelled, failed, whyNot, &rowsToReplace);
		ok = wrote && !failed && !cancelled && replaced == static_cast<int32>(rowsToReplace.size());
		if (!ok)
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
		CmdUtils::EndCommandSequence(sequence);
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	if (!ok)
	{
		KBSResultModel::RollBackRows();
		if (endnoteLeft > 0)
			outStatus = "Replace: the match ends an endnote, and InDesign's replace breaks an endnote there - left as it is.";
		else if (locked > 0)
			outStatus = "Replace: the match is locked now (a locked layer or story) - left as it is.";
		else if (missing > 0)
			outStatus = "Replace: the match was not found where the search found it - search again.";
		else if (refused > 0)
			outStatus = "Replace: InDesign's replace command would not run there - left as it is.";
		else
		{
			outStatus = "Replace: ";
			outStatus.Append(whyNot.IsEmpty() ? PMString("it did not go through") : whyNot);
			outStatus.Append(" - nothing was changed.");
		}
		return false;
	}
	KBSResultModel::ForgetRowBackup();
	windowAfter.want = true;		// written to: it has to be seen and saved (a no-op when it has a window)
	// each row's two texts - what its tracked change is found by (Hit::originalText / replacedText)
	for (std::map<int32, PMString>::const_iterator o = originals.begin(); o != originals.end(); ++o)
	{
		bool checked = false, isReplaced = false, isLocked = false;
		UID s2 = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 h = 0;
		if (KBSResultModel::GetHitFlags(chapterIdx, o->first, checked, isReplaced, isLocked) && isReplaced
			&& KBSResultModel::GetHitMatchIdentity(chapterIdx, o->first, s2, a, b, h))
			KBSResultModel::SetHitChangeTexts(chapterIdx, o->first, o->second, KBSTrackChange::ReadText(UIDRef(db, s2), a, b - a));
	}
	// the change side the rows were replaced with, for the report (Save Results)
	RenumberWalkOrders(chapterIdx, docRef, scopeOptions);
	if (rowsToReplace.size() == 1)
		outStatus = KBSResultModel::GetHitInFootnote(chapterIdx, hitIdx)
			? "Replaced (inside a footnote - Track Changes records nothing there, so it cannot be taken back with Reject Change)."
			: "Replaced with Track Changes on - Reject Change on the row's right-click menu takes it back.";
	else
	{
		outStatus = "Replaced ";
		outStatus.AppendNumber(replaced);
		outStatus.Append(" checked row(s) of this ");
		outStatus.Append(unit);
		outStatus.Append(" with Track Changes on - Reject Change on a row's (or its story's) right-click menu takes it back.");
	}
	if (accepted > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(accepted);
		outStatus.Append(" pending tracked change(s) next to it accepted first.");
	}
	return true;
}

bool KBSReplaceEngine::ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (!CanReplaceHit(chapterIdx, hitIdx))
	{
		outStatus = "Replace: this row cannot be replaced (already replaced, locked, or not a match of Find/Change).";
		return false;
	}
	std::set<int32> one;
	one.insert(hitIdx);
	return ReplaceRowsNow(chapterIdx, one, outStatus);
}

// ======================================================================================================
// Reject Change (2026-09-26) - see the header. The sequence is a PLAIN one, as KCM's own reject is
// (KCMFacades.cpp, RejectImportChange): an abortable sequence, ended, was measured to take the undo
// step below it away. The rollback of a plain sequence is the SDK's own: raise the error state, end it,
// clear it (CmdUtils.h, SequenceContext).
// ======================================================================================================
// One row's replace taken back inside the caller's sequence - the change its records were found as
// (KBSTrackChange::FindRowChangeForHit, asked by the caller before anything was taken back), its records
// whole, this row's run only (KBSTrackChange::RejectReplacement), then its original text read back where
// it stood. Nothing in the model is touched; outAt / outLen say where the original text now stands.
static bool RejectOneRow(const UIDRef& story, const KBSTrackChange::Change& change, const PMString& originalText,
	TextIndex& outAt, int32& outLen, PMString& outWhy)
{
	outLen = WideString(originalText).CharCount();
	outAt = change.at;
	if (change.hasDelete && change.deleteTime != 0 && change.deleteTime != change.time)
	{
		// The deletion was stamped a clock tick after the insertion (FindRowChangeForHit): each is
		// taken back under its own time - the deletion first, as a touching group's merged change is.
		if (!KBSTrackChange::RejectReplacement(story, change.at, 0, change.at + change.insLen, 0,
				outLen, nil, change.deleteTime, outWhy)
			|| (change.insLen > 0 && !KBSTrackChange::RejectReplacement(story, change.at, change.insLen,
				kInvalidTextIndex, 0, 0, nil, change.time, outWhy)))
			return false;
	}
	else if (!KBSTrackChange::RejectReplacement(story, change.at, change.insLen,
		change.at + change.insLen, 0, change.hasDelete ? outLen : 0, nil, change.time, outWhy))
		return false;
	if (KBSTrackChange::ReadText(story, change.at, outLen) != originalText)
	{
		outWhy = "the original text did not come all the way back";
		return false;
	}
	return true;
}

// ***** A STORY ROW'S ROWS (2026-09-27, the story level). ***** Which of a story's rows each of its menu
// items acts on: Replace = the TICKED rows that can be replaced (the user's call: "only the ticked
// ones"); Reject Change = the replaced rows outside a footnote whose tracked change is still there;
// (Redo, the rows taken back, went on 2026-09-27: they carry a box and are replaced again with Replace.)
// True = at least one. `firstOnly` = stop at the first (the menu's greying asks
// only "is there one", on every right-click: matching every replaced row of a large story against its
// tracked changes took seconds - 2026-09-27 re-check).
static bool StoryRowsToReplace(int32 chapterIdx, int32 groupIdx, std::set<int32>& out, bool firstOnly = false)
{
	out.clear();
	std::vector<int32> rows;
	KBSResultModel::GetGroupHits(chapterIdx, groupIdx, rows);
	for (size_t k = 0; k < rows.size(); ++k)
	{
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, rows[k], checked, replaced, locked) && checked
			&& KBSReplaceEngine::CanReplaceHit(chapterIdx, rows[k]))
		{
			out.insert(rows[k]);
			if (firstOnly)
				break;
		}
	}
	return !out.empty();
}

static bool StoryRowsToReject(int32 chapterIdx, int32 groupIdx, std::vector<int32>& out, bool firstOnly = false)
{
	out.clear();
	std::vector<int32> rows;
	KBSResultModel::GetGroupHits(chapterIdx, groupIdx, rows);
	for (size_t k = 0; k < rows.size(); ++k)
	{
		bool checked = false, replaced = false, locked = false;
		UIDRef storyRef;
		KBSTrackChange::Change change;
		if (KBSResultModel::GetHitFlags(chapterIdx, rows[k], checked, replaced, locked) && replaced
			&& !KBSResultModel::GetHitInFootnote(chapterIdx, rows[k])
			&& KBSTrackChange::FindRowChangeForHit(chapterIdx, rows[k], storyRef, change))
		{
			out.push_back(rows[k]);
			if (firstOnly)
				break;
		}
	}
	return !out.empty();
}

bool KBSReplaceEngine::CanReplaceStory(int32 chapterIdx, int32 groupIdx)
{
	std::set<int32> rows;
	return StoryRowsToReplace(chapterIdx, groupIdx, rows, true);
}

bool KBSReplaceEngine::CanRejectStory(int32 chapterIdx, int32 groupIdx)
{
	std::vector<int32> rows;
	return StoryRowsToReject(chapterIdx, groupIdx, rows, true);
}

// ***** TAKE A SET OF REPLACED ROWS BACK, IN ONE UNDO STEP (2026-09-27: the row's touching group, or a
// ***** story's replaced rows). ***** The rows are split into runs of touching rows. Inside a run they go
// front to back (the runs themselves from the last - below): taking back the LATER of two touching replaces drops the EARLIER one's deletion record
// (it sits on the later one's first character), while front to back leaves every record whole
// (2026-09-26, measured). A run InDesign merged into one insertion and one deletion (touching matches
// replaced one at a time - KBSTrackChange::FindGroupChange) goes back in one reject of those two records;
// any other row goes back on its own records (RejectOneRow). Any failure rolls all of it back. `rows` =
// replaced rows outside a footnote (a footnote's row has nothing recorded), in any order.
// ***** EVERY CHANGE FOUND FIRST, THEN THE RUNS TAKEN BACK FROM THE LAST (2026-09-28). ***** Each row's
// change is "the change nearest this row's stored start, unless another replaced row with the same texts is
// nearer" (FindRowChangeForHit). This took the rows back front to back and looked each one up just before
// its turn - by which time every row behind the ones already taken back stood where it had been before the
// text shrank, and the rows taken back still read "replaced" at their old places. With many alike replaces
// close together (1200 "cat"->"kitten" a space apart, case reject-many-alike) the third row's change was
// then handed to the second row, the third was "not found", and the whole reject was cancelled - every time.
// Now every change is looked up while nothing has moved. The RUNS are taken back from the last in text
// order: a run is separated from the next by text, and taking one back moves only what lies after it -
// all done by then - so each change found is still where it was found when its turn comes. INSIDE a run
// the rows still go front to back (above: the later of two touching replaces carries the earlier one's
// deletion record), each change moved first by what the rows in front of it in the run gave back - one
// run is one stretch of one thread, so that is the whole of its move. The rows follow the text as "this
// far into this thread" (RowNow, carried past each change as it is made - CarryPastChange), which also
// carries the rows in cells and footnotes behind a body change and past the threads a taken-back deletion
// takes with it; their stored places are written once, at the end.
static bool RejectRowsNow(int32 chapterIdx, std::vector<int32> rows, const UIDRef& docRef, PMString& outStatus)
{
	IDataBase* const db = docRef.GetDataBase();
	// each row where its change stands now, then text order
	for (size_t k = 0; k < rows.size(); ++k)
		KBSTrackChange::RefreshRowFromRecords(chapterIdx, rows[k]);
	std::vector<std::pair<std::pair<UID, TextIndex>, int32> > order;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 h = 0;
		KBSResultModel::GetHitMatchIdentity(chapterIdx, rows[k], story, a, b, h);
		order.push_back(std::make_pair(std::make_pair(story, a), rows[k]));
	}
	std::sort(order.begin(), order.end());
	std::vector<std::vector<int32> > runs;	// touching rows together
	UID lastStory = kInvalidUID;
	TextIndex lastEnd = kInvalidTextIndex;
	for (size_t k = 0; k < order.size(); ++k)
	{
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 h = 0;
		KBSResultModel::GetHitMatchIdentity(chapterIdx, order[k].second, story, a, b, h);
		if (runs.empty() || story != lastStory || a > lastEnd)
			runs.push_back(std::vector<int32>());
		runs.back().push_back(order[k].second);
		lastStory = story;
		lastEnd = b;
	}

	// What each run takes back: a touching group's merged change (FindGroupChange), or each row's own.
	// Found now, before anything moves; a row with none refuses the whole reject before a thing is written.
	struct Plan
	{
		std::vector<int32>		rows;
		bool					group;
		UIDRef					story;
		KBSTrackChange::Change	change;
		uint64					deleteTime;		// a group's merged deletion's own time (FindGroupChange)
		Plan() : group(false), deleteTime(0) {}
	};
	std::vector<std::vector<Plan> > runPlans(runs.size());	// one per run, in text order
	std::set<std::pair<UID, TextIndex> > claimed;			// no change taken back twice
	for (size_t r = 0; r < runs.size(); ++r)
	{
		const std::vector<int32>& run = runs[r];
		if (run.size() >= 2)
		{
			Plan p;
			p.rows = run;
			p.group = true;
			if (KBSTrackChange::FindGroupChange(chapterIdx, run, p.story, p.change, &p.deleteTime))
				runPlans[r].push_back(p);
		}
		for (size_t k = 0; runPlans[r].empty() && k < run.size(); ++k)
		{
			Plan p;
			p.rows.push_back(run[k]);
			if (!KBSTrackChange::FindRowChangeForHit(chapterIdx, run[k], p.story, p.change))
			{
				outStatus = "Reject Change: no tracked change of this replace is left for a row (accepted or rejected in the Track Changes panel, or in a footnote, where nothing is recorded) - nothing was changed.";
				return false;
			}
			runPlans[r].push_back(p);
		}
		if (runPlans[r].size() > 1 && runPlans[r][0].group)
			runPlans[r].resize(1);
	}
	for (size_t r = 0; r < runPlans.size(); ++r)
	{
		for (size_t k = 0; k < runPlans[r].size(); ++k)
		{
			if (!claimed.insert(std::make_pair(runPlans[r][k].story.GetUID(), runPlans[r][k].change.at)).second)
			{
				outStatus = "Reject Change: two rows came to the same tracked change - nothing was changed. Search again.";
				return false;
			}
		}
	}

	std::vector<RowNow> others;		// every row as "this far into its thread", carried past each change
	SnapshotRows(chapterIdx, db, others);
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
	if (sequence == nil)
	{
		outStatus = "Reject Change: InDesign would not start a command sequence - nothing was changed.";
		return false;
	}
	PMString name("Reject Change");
	name.SetTranslatable(kFalse);
	sequence->SetName(name);
	std::vector<int32> taken;		// every row taken back; others[row] follows where its original text stands
	PMString why;
	bool ok = true;
	for (size_t r = runPlans.size(); r-- > 0 && ok; )
	{
		int32 shift = 0;		// what the rows in front of this one in the run gave back
		for (size_t pi = 0; pi < runPlans[r].size() && ok; ++pi)
		{
			Plan p = runPlans[r][pi];
			p.change.at += shift;
			if (p.group)
			{
				PMString allOriginal;
				allOriginal.SetTranslatable(kFalse);
				std::vector<int32> lengths;
				for (size_t k = 0; k < p.rows.size() && ok; ++k)
				{
					PMString originalText, replacedText;
					ok = KBSResultModel::GetHitChangeTexts(chapterIdx, p.rows[k], originalText, replacedText);
					lengths.push_back(WideString(originalText).CharCount());
					allOriginal.Append(originalText);
				}
				const int32 allLen = WideString(allOriginal).CharCount();
				// the deletion first, then the insertion - each under its own time (FindGroupChange)
				if (ok && p.change.hasDelete)
					ok = KBSTrackChange::RejectReplacement(p.story, p.change.at, 0, p.change.at + p.change.insLen, 0,
						allLen, nil, p.deleteTime, why);
				if (ok && p.change.insLen > 0)
					ok = KBSTrackChange::RejectReplacement(p.story, p.change.at, p.change.insLen, kInvalidTextIndex, 0,
						0, nil, p.change.time, why);
				if (ok && KBSTrackChange::ReadText(p.story, p.change.at, allLen) != allOriginal)
				{
					why = "the original text did not come all the way back";
					ok = false;
				}
				if (ok)
				{
					CarryPastChange(db, others, p.story.GetUID(), p.change.at, p.change.insLen, allLen);
					TextIndex at = p.change.at;
					for (size_t k = 0; k < p.rows.size(); ++k)
					{
						SetRowAt(db, others[static_cast<size_t>(p.rows[k])], p.story.GetUID(), at, at + lengths[k]);
						taken.push_back(p.rows[k]);
						at += lengths[k];
					}
				}
			}
			else
			{
				const int32 row = p.rows[0];
				PMString originalText, replacedText;
				TextIndex at = kInvalidTextIndex;
				int32 len = 0;
				ok = KBSResultModel::GetHitChangeTexts(chapterIdx, row, originalText, replacedText)
					&& RejectOneRow(p.story, p.change, originalText, at, len, why);
				if (ok)
				{
					CarryPastChange(db, others, p.story.GetUID(), at, p.change.insLen, len);
					SetRowAt(db, others[static_cast<size_t>(row)], p.story.GetUID(), at, at + len);
					taken.push_back(row);
					shift += len - p.change.insLen;
				}
			}
		}
	}
	if (!ok)
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
	CmdUtils::EndCommandSequence(sequence);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (!ok)
	{
		outStatus = "Reject Change: ";
		outStatus.Append(why);
		outStatus.Append(" - the reject was cancelled and the document is as it was.");
		return false;
	}

	// every other row where the text has taken it, then the rows taken back where their original text stands
	WriteBackRows(chapterIdx, db, others, taken);
	for (size_t k = 0; k < taken.size(); ++k)
	{
		const RowNow& row = others[static_cast<size_t>(taken[k])];
		TextIndex at = kInvalidTextIndex;
		if (!RowStartNow(db, row, at))
			continue;
		const UIDRef storyRef(db, row.story);
		KBSResultModel::SetHitRejected(chapterIdx, taken[k], row.story, at, at + row.length);
		PMString pre, match, post;
		KBSSearchEngine::SplitLineAroundMatch(storyRef, at, at + row.length, pre, match, post);
		KBSResultModel::SetHitSegments(chapterIdx, taken[k], pre, match, post,
			KBSSearchEngine::HashMatchText(storyRef, at, at + row.length));
	}
	// ***** THE REST NUMBERED AGAIN (2026-09-27 re-check; in a report too since the same day, B). ***** The
	// text is back, so its match is in the walk again, and a Change Checked after this - a report can
	// run one on its taken-back rows now - would find every later row one walk order off and refuse.
	{
		WalkerScopeOptions scopeOptions;
		KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);
		RenumberWalkOrders(chapterIdx, docRef, scopeOptions);
	}
	return true;
}

// The document of a chapter, open - or false and a status line saying so (Reject Change / Redo work on
// the open document only).
static bool OpenChapterDoc(int32 chapterIdx, const char* who, UIDRef& outDocRef, PMString& outStatus)
{
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, outDocRef, file) || outDocRef.GetDataBase() == nil
		|| !KBSBookScope::IsDocStillOpen(outDocRef))
	{
		outStatus = who;
		outStatus.Append(": the document of this row is not open.");
		return false;
	}
	return true;
}

bool KBSReplaceEngine::RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!OpenChapterDoc(chapterIdx, "Reject Change", docRef, outStatus))
		return false;
	// The row with every replaced row touching it (see RejectRowsNow on why a touching group goes together).
	//
	// ***** ONLY THE NEIGHBOURS WHOSE CHANGE IS STILL THERE (2026-09-27, case touching-group-redo-reject). *****
	// A neighbour's change can be gone while the row still reads "replaced": the replace of THIS row accepts
	// the pending changes it touches first (AcceptPendingAround), and a neighbour replaced a moment earlier
	// is exactly such a change. Taking that neighbour along found nothing to reject for it, and the whole
	// reject was cancelled - so this row could never be taken back at all. A neighbour with no change of its
	// own left is not part of what can be taken back; the row itself always is (RejectOneRow says why when
	// its own change is gone).
	std::vector<int32> group;
	KBSTrackChange::RefreshRowFromRecords(chapterIdx, hitIdx);
	KBSResultModel::GetTouchingGroup(chapterIdx, hitIdx, group);
	std::vector<int32> rows;
	for (size_t k = 0; k < group.size(); ++k)
	{
		bool checked = false, replaced = false, locked = false;
		if (!KBSResultModel::GetHitFlags(chapterIdx, group[k], checked, replaced, locked) || !replaced
			|| KBSResultModel::GetHitInFootnote(chapterIdx, group[k]))
			continue;
		UIDRef storyRef;
		KBSTrackChange::Change change;
		if (group[k] == hitIdx || KBSTrackChange::FindRowChangeForHit(chapterIdx, group[k], storyRef, change))
			rows.push_back(group[k]);
	}
	if (rows.empty())
		rows.push_back(hitIdx);
	if (!RejectRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	outStatus = (rows.size() > 1)
		? "Rejected - the row and the matches touching it are back to their original text. Tick them, or right-click for Replace, to replace them again."
		: "Rejected - the row is back to its original text. Tick it, or right-click it for Replace, to replace it again.";
	return true;
}

// A document row's Reject Change (2026-09-27): the replaced rows of the whole document that can be taken back.
static bool ChapterRowsToReject(int32 chapterIdx, std::vector<int32>& out, bool firstOnly)
{
	out.clear();
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		UIDRef storyRef;
		KBSTrackChange::Change change;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && replaced
			&& !KBSResultModel::GetHitInFootnote(chapterIdx, i)
			&& KBSTrackChange::FindRowChangeForHit(chapterIdx, i, storyRef, change))
		{
			out.push_back(i);
			if (firstOnly)
				break;
		}
	}
	return !out.empty();
}

bool KBSReplaceEngine::CanRejectChapter(int32 chapterIdx)
{
	std::vector<int32> rows;
	return chapterIdx >= 0 && chapterIdx < KBSResultModel::GetChapterCount() && ChapterRowsToReject(chapterIdx, rows, true);
}

bool KBSReplaceEngine::RejectChapter(int32 chapterIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!OpenChapterDoc(chapterIdx, "Reject Change", docRef, outStatus))
		return false;
	std::vector<int32> rows;
	if (!ChapterRowsToReject(chapterIdx, rows, false))
	{
		outStatus = "Reject Change: no replaced row with a tracked change in this document.";
		return false;
	}
	if (!RejectRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	outStatus = "Rejected ";
	outStatus.AppendNumber(static_cast<int32>(rows.size()));
	outStatus.Append(" row(s) of this document - back to their original text. Tick them and Replace to replace them again.");
	return true;
}

bool KBSReplaceEngine::RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!OpenChapterDoc(chapterIdx, "Reject Change", docRef, outStatus))
		return false;
	std::vector<int32> rows;
	if (!StoryRowsToReject(chapterIdx, groupIdx, rows))
	{
		outStatus = "Reject Change: no replaced row with a tracked change in this story.";
		return false;
	}
	if (!RejectRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	outStatus = "Rejected ";
	outStatus.AppendNumber(static_cast<int32>(rows.size()));
	outStatus.Append(" row(s) of this story - back to their original text. Tick them and Replace to replace them again.");
	return true;
}

// ======================================================================================================
// Accept All Changes in This Document (2026-09-27) - see the header. The same plain sequence and rollback
// as Reject Change above.
// ======================================================================================================
bool KBSReplaceEngine::CanAcceptAllInChapter(int32 chapterIdx)
{
	UIDRef docRef;
	IDFile file;
	return KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) && docRef.GetDataBase() != nil
		&& KBSBookScope::IsDocStillOpen(docRef) && KBSTrackChange::DocumentHasChanges(docRef.GetDataBase());
}

bool KBSReplaceEngine::AcceptAllInChapter(int32 chapterIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) || docRef.GetDataBase() == nil
		|| !KBSBookScope::IsDocStillOpen(docRef))
	{
		outStatus = "Accept All Changes: the document is not open.";
		return false;
	}
	// ***** THE ROWS FOLLOW THE TEXT (2026-09-28). ***** Accepting a deletion takes its deleted-text thread
	// away, and every thread behind it in the story - a table cell's, a footnote's - moves up. The rows
	// were left at their story indexes, so a cell's row stood where its text no longer was: its jump said
	// "missing" and its Replace refused as "changed since the search" (case accept-all-then-cell). Taken as
	// "this far into this thread" before (SnapshotRows) and put back after (WriteBackRows), as a reject does;
	// accepting changes no text, so nothing is carried within a thread.
	IDataBase* const db = docRef.GetDataBase();
	std::vector<RowNow> rows;
	SnapshotRows(chapterIdx, db, rows);
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
	if (sequence == nil)
	{
		outStatus = "Accept All Changes: InDesign would not start a command sequence - nothing was changed.";
		return false;
	}
	PMString name("Accept All Changes");
	name.SetTranslatable(kFalse);
	sequence->SetName(name);
	PMString why;
	why.SetTranslatable(kFalse);
	const int32 accepted = KBSTrackChange::AcceptAllInDocument(db, why);
	if (accepted < 0)
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
	CmdUtils::EndCommandSequence(sequence);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (accepted < 0)
	{
		outStatus = "Accept All Changes: ";
		outStatus.Append(why);
		outStatus.Append(" - nothing was accepted and the document is as it was.");
		return false;
	}
	WriteBackRows(chapterIdx, db, rows, std::vector<int32>());
	outStatus = "Accepted ";
	outStatus.AppendNumber(accepted);
	// Every change in the document, whoever made it - as InDesign's own Accept All Changes in This
	// Document (2026-09-27, the user's call).
	outStatus.Append(" change(s) in the document - every tracked change in it, as InDesign's own Accept All does. They can no longer be rejected.");
	return true;
}

// Replace on a DOCUMENT row (2026-09-27, the user's call): that document's TICKED rows, as a story row's.
static bool ChapterRowsToReplace(int32 chapterIdx, std::set<int32>& out, bool firstOnly)
{
	out.clear();
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		if (KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && checked
			&& KBSReplaceEngine::CanReplaceHit(chapterIdx, i))
		{
			out.insert(i);
			if (firstOnly)
				break;
		}
	}
	return !out.empty();
}

bool KBSReplaceEngine::CanReplaceChapter(int32 chapterIdx)
{
	std::set<int32> rows;
	return chapterIdx >= 0 && chapterIdx < KBSResultModel::GetChapterCount() && ChapterRowsToReplace(chapterIdx, rows, true);
}

bool KBSReplaceEngine::ReplaceChapter(int32 chapterIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	std::set<int32> rows;
	if (chapterIdx < 0 || chapterIdx >= KBSResultModel::GetChapterCount() || !ChapterRowsToReplace(chapterIdx, rows, false))
	{
		outStatus = "Replace: no checked row in this document to replace - tick the rows first.";
		return false;
	}
	return ReplaceRowsNow(chapterIdx, rows, outStatus, "document");
}

// ***** Redo on a story row (2026-09-27, the user's call C). ***** The story's rows taken back with Reject
// Change, replaced again - ticked or not, so they can all be done without ticking them one by one (Check
// All would tick the rows never replaced as well). With what Find/Change holds NOW, like Replace: the
// row's own menu has only Replace since the same day, and Redo's old test ("the Change To the replace
// used") compared a description, not the replace itself. A row whose text is not its original any more
// (edited, or undone since) is skipped and counted.
static void RowsToRedo(int32 chapterIdx, const std::vector<int32>& rows, IDataBase* db, std::set<int32>& outFit,
	int32& outSkipped, bool firstOnly)
{
	outFit.clear();
	outSkipped = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (KBSResultModel::GetHitOutcome(chapterIdx, rows[k]) != KBSResultModel::kOutcomeRejected
			|| !KBSReplaceEngine::CanReplaceHit(chapterIdx, rows[k]))
			continue;
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 hash = 0;
		if (db == nil || (KBSResultModel::GetHitMatchIdentity(chapterIdx, rows[k], story, a, b, hash)
			&& KBSSearchEngine::MatchIsSameOccurrence(UIDRef(db, story), a, b, story, a, b, hash)))
		{
			outFit.insert(rows[k]);
			if (firstOnly)
				return;
		}
		else
			++outSkipped;
	}
}

// The rows of a story (groupIdx >= 0) or of the whole document (groupIdx < 0).
static void RedoScopeRows(int32 chapterIdx, int32 groupIdx, std::vector<int32>& out)
{
	out.clear();
	if (groupIdx >= 0)
	{
		KBSResultModel::GetGroupHits(chapterIdx, groupIdx, out);
		return;
	}
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
		out.push_back(i);
}

static bool CanRedoRows(int32 chapterIdx, int32 groupIdx)
{
	std::vector<int32> scope;
	RedoScopeRows(chapterIdx, groupIdx, scope);
	std::set<int32> rows;
	int32 skipped = 0;
	RowsToRedo(chapterIdx, scope, nil, rows, skipped, true);	// nil = no text test for the greying
	return !rows.empty();
}

// Redo over a story or a document: see RowsToRedo. `unit` = "story" / "document", for the status line.
static bool RedoRowsNowIn(int32 chapterIdx, int32 groupIdx, const char* unit, PMString& outStatus);

bool KBSReplaceEngine::CanRedoStory(int32 chapterIdx, int32 groupIdx)
{
	return CanRedoRows(chapterIdx, groupIdx);
}

bool KBSReplaceEngine::CanRedoChapter(int32 chapterIdx)
{
	return chapterIdx >= 0 && chapterIdx < KBSResultModel::GetChapterCount() && CanRedoRows(chapterIdx, -1);
}

bool KBSReplaceEngine::RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	return RedoRowsNowIn(chapterIdx, groupIdx, "story", outStatus);
}

bool KBSReplaceEngine::RedoChapter(int32 chapterIdx, PMString& outStatus)
{
	if (chapterIdx < 0 || chapterIdx >= KBSResultModel::GetChapterCount())
		return false;
	return RedoRowsNowIn(chapterIdx, -1, "document", outStatus);
}

static bool RedoRowsNowIn(int32 chapterIdx, int32 groupIdx, const char* unit, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	IDFile file;
	IDataBase* db = nil;
	if (KBSResultModel::GetChapterLocation(chapterIdx, docRef, file) && docRef.GetDataBase() != nil
		&& KBSBookScope::IsDocStillOpen(docRef))
		db = docRef.GetDataBase();
	if (db == nil)
	{
		outStatus = "Redo: the document of these rows is not open.";
		return false;
	}
	std::vector<int32> scope;
	RedoScopeRows(chapterIdx, groupIdx, scope);
	std::set<int32> rows;
	int32 skipped = 0;
	RowsToRedo(chapterIdx, scope, db, rows, skipped, false);
	if (rows.empty())
	{
		outStatus = "Redo: no row of this ";
		outStatus.Append(unit);
		outStatus.Append(" taken back with Reject Change can be replaced again (its text has changed since).");
		return false;
	}
	if (!ReplaceRowsNow(chapterIdx, rows, outStatus, unit))
		return false;
	outStatus = "Redone ";
	outStatus.AppendNumber(static_cast<int32>(rows.size()));
	outStatus.Append(" row(s) of this ");
	outStatus.Append(unit);
	outStatus.Append(" with Track Changes on.");
	if (skipped > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(skipped);
		outStatus.Append(" taken back could not be replaced again (text changed since) and were left.");
	}
	return true;
}

// Replace on a story row (2026-09-27): that story's TICKED rows (the user's call), one undo step.
bool KBSReplaceEngine::ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	std::set<int32> rows;
	if (!StoryRowsToReplace(chapterIdx, groupIdx, rows))
	{
		outStatus = "Replace: no checked row in this story to replace - tick the rows first.";
		return false;
	}
	return ReplaceRowsNow(chapterIdx, rows, outStatus);
}

// End, KBSReplaceEngine.cpp.
