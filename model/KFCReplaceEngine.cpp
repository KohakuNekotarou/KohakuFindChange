//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Replace engine implementation. See KFCReplaceEngine.h for the contract, and RunWalkerCmd below
//  for what the find/replace walker commands this is built on answer.
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
#include "ISession.h"				// GetExecutionContextSession - the walker's registry, and where IKFCUIServices sits
#include "IEndnoteFacade.h"			// MatchEndsAnEndnote - is it the endnote story, and an endnote range's marker

// General includes:
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss / kTWReplaceTextCmdBoss / kFindChangeClientBoss
#include "WalkerScopeOptions.h"
#include "IKFCUIServices.h"			// TellResultsWentStale - the chapter moved under its rows; the alert is the UI half's
#include "CmdUtils.h"				// commands and command sequences
#include "CreateObject.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode, GlobalErrorStatePreserver
#include "ITextModel.h"				// QueryStoryThread / FindStoryThread - a row kept as "this far into this thread" (RowNow)
#include "ITextStoryThread.h"
#include "textiterator.h"			// the character at an endnote's end (MatchEndsAnEndnote's fallback)
#include "WideString.h"			// Reject Change: the original text's length in code points
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "KFCProgressBar.h"	// the replace's progress + cancel, as the search does it - the bar is the UI half's
#include "StringUtils.h"			// ::ReplaceStringParameters - fills the ^1 in a translated string
#include "Utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

// Project includes:
#include "KFCReplaceEngine.h"
#include "KFCDiag.h"			// KFC_DIAG_LOG - why a write was refused, in a test build (compiled out of a shipping one)
#include "KFCDiagCommands.h"	// the test build's command count (KFC_DIAG_COMMANDS)
#include "KFCID.h"				// the string keys the stale-results alert and the Undo step are worded from
#include "KFCLoc.h"				// runtime Japanese - there is no jaJP string table
#include "KFCResultModel.h"
#include "KFCRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KFCSearchEngine.h"	// the shared walker scope and the line-splitting the rows use
#include "KFCBookScope.h"		// reopening a chapter the user closed since the search
#include "KFCTrackChange.h"	// every replace under Track Changes, its records left
#include "KFCUndoFollow.h"		// every write recorded, so that the panel follows its Undo and Redo
#ifdef KFC_DIAG
#include "ITextFocusManager.h"	// how many foci a story carries while it is written (WALKSTEP) - test builds only
#include "IFrameList.h"			// whether its frames stand damaged around a find (WALKSTEP) - test builds only
#endif

namespace
{

// A QUERY RUN'S LIST IS TAKEN BACK WHOLE (2026-10-05 - docs/superpowers/specs/2026-10-04-kfc-query-sequence-design.md,
// D5): the whole run is one undo step, not a row at a time, so every Reject and Accept below refuses on that list
// (KFCResultModel::IsFromQueryRun) - the Can... ones answer false, the rest say why.
const char* const kQueryRunRefusal = "This list is a query run's - undo the whole run with Ctrl+Z (Edit > Undo Run Queries).";

// True = refused, outStatus says why.
bool RefusedOnQueryRun(PMString& outStatus)
{
	if (!KFCResultModel::IsFromQueryRun())
		return false;
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	outStatus.Append(kQueryRunRefusal);
	return true;
}

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
//                    function: that answer belongs to kReplaceAllTextCmdBoss, and KFC does not run
//                    it - the official Change All takes no subset of the matches, which is the
//                    whole reason this engine walks match by match. Listed anyway because the enum
//                    has FIVE members (:43), and a table naming four of them reads as if it were
//                    the whole contract. The four above are the ones this function can produce.
//
// kFailure IS WHY THIS RETURNS A RESULT AND NOT A BOOL. With true or false, a chapter whose search
// failed halfway looks exactly like a chapter whose matches are gone, and its remaining rows are
// reported as 'missing' - "not found when the chapter was searched again", which is a statement
// about the DOCUMENT and is not true. The official loop
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
// returns from a dozen places. Modelled on KFCSearchEngine's SearchingFlagGuard.
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
	// AbortCommandSequence restores the text but not the flag (a cancelled run would leave every chapter
	// it had reached looking unsaved with nothing to save; a real Undo does clear it, the application
	// does that itself, which is what makes the difference visible), and on the way out of a run that
	// went through, for a chapter no replacement landed in (see HandBackChaptersWithNothingInThem).
	// Chapters that were already dirty stay dirty; that was not ours to change.
	bool	wasModified;

	// Did a replacement actually land here - or a pending change this run accepted first? A chapter that got
	// one is the user's to look at and save (it is given a window and left open); a chapter that got none has
	// nothing in it and is handed straight back, because it is holding its .indd locked for no reason at all.
	bool	tookReplacement;

	// This chapter's checked, not-yet-replaced hits, counted ONCE where the run is sized and read
	// from here after that (how far this chapter's slice moves the bar). The number cannot change
	// in between: the panel's actions are greyed out for the whole run, the bar is modal, and a
	// chapter's own hits are only marked replaced as IT runs.
	int32	checkedCount;

	// The chapter's row stories at the version KFC last recorded when the run began - the ones that take
	// their new version if the run goes through (NoteStoryVersions). A
	// ticked row's story is always among them, or the run would not have started (ChapterMovedUnderRows).
	std::set<UID>	storiesAsLeft;

	PendingChapter() : chapterIdx(-1), opened(false), wasModified(false), tookReplacement(false),
		checkedCount(0) {}
};

// Everything a run has to say about itself, in one place: the counters are what the summary is built
// from, and they are easier to follow gathered than scattered.
//
// Counters only - no decisions. BuildSummary says nothing about a counter that stayed zero.
struct RunTotals
{
	int32	replaced;			// hits actually rewritten
	int32	chaptersTouched;	// chapters at least one replacement landed in
	int32	chaptersSkipped;	// could not be opened at all
	// (No "opened, but the walker would not run" count: a story whose walk will not start stops the
	//  whole run, and stoppedByFailure below says why.)
	int32	chaptersNoWindow;	// a replacement landed, but no window could be opened on it
	int32	chaptersLeftHidden;	// a replacement landed in a document the user keeps without a window
	// The walk STARTED here and then broke off with an error - a different thing from a chapter that
	// could not be opened, and from a walk that simply ran out of matches. The
	// rows it never reached are counted as missing like any others (there is nothing else honest
	// to say about them), so this exists to explain WHY there are so many: the chapter was not
	// searched to the end. Without it a broken search reads as "the text has moved", which is a
	// statement about the user's document and is not true.
	int32	chaptersWalkFailed;
	int32	missing;			// checked hits whose text is no longer where the row says
	int32	locked;				// checked hits on a locked layer or in a locked story
	int32	refused;			// the replace command was asked and said no

	// The FIRST name in each of the two lists that name one. Kept with a flag of its own rather
	// than testing IsEmpty(): a chapter whose name is empty would otherwise never count as the
	// first, and every later one would overwrite it.
	PMString	firstSkipped;
	PMString	firstWalkFailed;
	bool		haveFirstSkipped;
	bool		haveFirstWalkFailed;

	// The run ended without committing anything. The whole run is one command sequence, so this
	// means the sequence was aborted and nothing at all was written - see BuildSummary. THREE things
	// raise it: the user stopping the run from the progress bar, the run finding it cannot go on
	// (stoppedByFailure, below), and the error state being found standing at the moment the sequence
	// was about to be committed (stoppedByError, below).
	bool		cancelled;

	// ...and WHICH it was. A failure is not the user changing their mind, and calling it "cancelled"
	// would send them looking for a button nobody pressed. For stoppedByError errorText is InDesign's
	// own wording and may be empty - it is only ever shown when it is not.
	bool		stoppedByError;
	PMString	errorText;

	// ...or the run itself found it could not go on (ReplaceInChapterOneByOne's outWhyNot, which
	// errorText then holds): Track Changes could not be switched on, a pending change would not be
	// accepted, the walker would not start, a replace could not be signed - each said in its own words,
	// not folded into one "did not line up".
	bool		stoppedByFailure;

	// Ticked rows in an endnote story that were left alone: a match there ends an endnote.
	int32		endnoteLeft;
	int32		acceptedFirst;	// pending tracked changes accepted before the first write
	// Rows written that Track Changes recorded nothing for: a footnote's, or a replace that changed no
	// character (Change Format with an empty Change To). Reject Change cannot take those back, and the
	// line that says it can is decided here (BuildSummary), not by the panel.
	int32		unrecorded;

	RunTotals()
		: replaced(0), chaptersTouched(0), chaptersSkipped(0),
		  chaptersNoWindow(0), chaptersLeftHidden(0), chaptersWalkFailed(0),
		  missing(0), locked(0), refused(0), endnoteLeft(0), acceptedFirst(0), unrecorded(0),
		  haveFirstSkipped(false), haveFirstWalkFailed(false),
		  cancelled(false), stoppedByError(false), stoppedByFailure(false)
	{
		firstSkipped.SetTranslatable(kFalse);
		firstWalkFailed.SetTranslatable(kFalse);
		errorText.SetTranslatable(kFalse);
	}
};

// Count a chapter into one of the summary's chapter lists (chaptersSkipped / firstSkipped, or the
// walk-failed pair), keeping the FIRST one's name - the model's display name for its row.
void NoteChapter(int32 chapterIdx, int32& ioCount, PMString& ioFirst, bool& ioHaveFirst)
{
	++ioCount;
	if (ioHaveFirst)
		return;
	int32 chapterHits = 0;
	KFCResultModel::GetChapterDisplay(chapterIdx, ioFirst, chapterHits);
	ioFirst.SetTranslatable(kFalse);
	ioHaveFirst = true;
}

// THE SAME-OCCURRENCE TEST IS NOT IN THE WRITING WALK - IT STANDS BEFORE THE RUN, AND AT EVERY DOOR.
//
// Inside the walk - is the match the walk has landed on the one the row describes: same story, same
// position (our own replacements cancelled out), same text - it cannot work across a BOOK, where a
// chapter may be closed between the search and the replace; keeping the document steady between
// searching and replacing is the USER's responsibility (the author's decision).
//
// What it guards against is real: the walk order alone cannot tell "the Nth match" from "a DIFFERENT
// Nth match". An edit made between the search and the replace that removes one match and adds another
// keeps the COUNT intact, so every checked hit still comes up and nothing looks wrong - while the
// numbering points at text the user never checked.
//
// SO THE SAME QUESTION IS ASKED BEFORE THE RUN STARTS, AND IT REFUSES (the author's design). The resolve
// pass walks every chapter it is about to write to - ChapterMovedUnderRows - and checks that each ticked
// hit's story is at the version on record, that the row still reads as found, and that a match still
// BEGINS at its place with its length. That is the one moment where the question is both answerable and
// free: nothing has been written, so the positions are the ones the rows carry, with no replacements of
// ours to cancel out - and a mismatch stops the run with not one character to take back.
//
// Not a per-chapter fingerprint (every story's ITextModel::GetChangeCount, warning rather than
// refusing): that answers a DIFFERENT question - "does this chapter look untouched?" - which has to
// enumerate the ways a document can move (text, stories, layers, locks, conditions...) and is never
// finished. Walking asks about the thing itself. (The story version asked first covers only the stories
// holding a ticked row, and it refuses - A STORY'S VERSION, below.)
//
// So the walk below needs no same-occurrence test of its own: by the time it runs, every ticked position
// has been confirmed and nothing has moved since (the verify pass writes nothing). It still finds each
// row by its place (RowOfMatchAnyOrder).
//
// The test and the hash behind it live on in KFCSearchEngine::RowReadsAsFound, which every door asks -
// the JUMP, which is how clicking a row can answer "the replacement is no longer here" instead of
// scrolling to whatever now sits at that position; the row doors (RowStillStands, ReplaceRowsNow,
// RowsToRedo); and the verify walk (ChapterMovedUnderRows).

// Where one of the chapter's rows stands in the text NOW - where the search found it, carried past
// every replacement this pass has made since - or, for a row this pass replaced, where its new text
// stands. One per row, indexed like the model's rows. The replace walk asks it two things: which
// row a match it has just found IS (RowOfMatchAnyOrder), and, once the walk is over, where each row
// the report keeps has ended up.
//
// THE RANGE IS CARRIED FORWARD, BECAUSE THE WALK IS NOT IN TextIndex ORDER. "The walk only ever
// moves forward, so every replacement after this row's happens LATER in the story and cannot shift
// its start" holds for body text only (measured):
//   * a table's cells are visited where the table stands - but their characters live after the
//     whole body in TextIndex terms (ITableTextContent.h:41-44), so replacing the body text AFTER
//     the table moved every cell row written before it; and
//   * the walk follows the Find/Change dialog's own direction - "search backwards" walked
//     "cat1 cat2 cat3" as cat3, cat2, cat1 although the scope options say forwards - so there
//     EVERY later replacement moved every row written before it.
// Either way a row not carried forward reads its line, and takes its hash, from text beside its own:
// the panel showed "wo<CR>kit" for a cell that read "kitten cell", and a double click SELECTED those
// wrong characters, the hash having come from the same wrong range. The rows passed and left alone
// have the same fault from the other side: kept at the range the search found them at, they are
// jumped to off by the replacements before them and stamped "missing".
//
// KEPT AS "THIS FAR INTO THIS THREAD", NOT AS A STORY INDEX. A story's text is its body
// followed by one thread per table cell and per footnote (ITableTextContent.h:41-44), and a
// replacement can take a whole thread away: deleting a footnote's reference marker deletes the
// footnote's text with it. Measured: GREP ~F|cat on "A<fn1> B<fn2>" (fn1 "x one", fn2
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

// The walk has dealt with a row the report keeps (replaced, locked, refused): put it where its text
// stands now and name it for the read-back after the walk.
void KeepRowAt(IDataBase* db, std::vector<RowNow>& rowNow, std::vector<int32>& keptRows, int32 hitIdx,
	UID story, TextIndex start, TextIndex end)
{
	SetRowAt(db, rowNow[static_cast<size_t>(hitIdx)], story, start, end);
	keptRows.push_back(hitIdx);
}

// ======================================================================================================
// NOT InDesign's CHANGE ALL. A Change All over each story with a ticked row, under Track Changes, with the
// rows NOT ticked taken back record by record afterwards (LineUpStory, AlignFootnoteRows,
// CheckOnlyTickedChanged) was tried and dropped (the author's call): it writes the rows nobody ticked,
// and a footnote (nothing recorded there) and a deletion shared by touching matches cannot be taken
// back. If it is ever wanted again, it is in git history (c876bc7 and before).
// ======================================================================================================

// A REPLACE THAT WOULD WRITE AT AN ENDNOTE'S END. InDesign's replace - Change All,
// changeText, one write over a range - that writes at the END of an endnote leaves the endnote's range
// (IDML EndnoteRange) ending short, and the character before the overhang can never be deleted again
// (measured, work/kbs-regress/probe-endnote-*-0927.jsx; no Track Changes needed). True = `matchEnd` is in
// the endnote story and stands on an endnote range's marker. Asked by the replace (per row, as the walk
// meets it).
// ASKED OF InDesign's ENDNOTE FACADE.
// Facade::IEndnoteFacade::IsEndnoteStory (as the product asks it, InCopyDocUtils.cpp:2389-2390) and
// IsEndnoteTextRangeMarker: true on the U+FEFF an endnote range starts and ends with, false on the U+FEFF
// of an index marker in the endnote's text (measured, KTRedlineProbe endnote) - which the class test
// (kEndnoteStoryBoss and any U+FEFF, SnpManipulateTextEndnotes::IsEndnoteStory) takes for an endnote's
// end, refusing a match that ends before an index marker. That test stays as the fallback, when the
// facade is not there.
bool MatchEndsAnEndnote(const UIDRef& story, TextIndex matchEnd)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	if (model == nil || matchEnd < 0 || matchEnd >= model->TotalLength())
		return false;
	Utils<Facade::IEndnoteFacade> endnotes;
	if (endnotes)
		return endnotes->IsEndnoteStory(story) && endnotes->IsEndnoteTextRangeMarker(matchEnd, model);
	if (::GetClass(model) != kEndnoteStoryBoss)
		return false;
	TextIterator it(model, matchEnd);
	return !it.IsNull() && (*it).GetValue() == kTextChar_ZeroSpaceNoBreak;
}

// ======================================================================================================
// ONE MATCH AT A TIME, STORY BY STORY, UNDER TRACK CHANGES (the author's call). Only the ticked matches
// are written, one at a time (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on the match it made current),
// a story at a time, with Track Changes on so a replaced row keeps its Reject Change. (Not Change All -
// see above.)
//
// WHICH WAY THE WALK GOES IS DECIDED BEFORE ANYTHING IS WRITTEN (the author's call). A walk that writes
// as it goes reads text it has already rewritten, and each direction has one shape it cannot get right
// (measured, spike/2026-09-26-backward-replace):
//   - forward, GREP ^ reads the character a replacement has just deleted in front of it
//     (\r|^a|ab on "x<CR>ab" left "xab" and one row missing, where Change All makes "xb");
//   - backward, a lookahead reads text a replacement has just rewritten behind it (x(?=y)|y on "xy"
//     left "x" and one row missing, where Change All makes "").
// Neither ever writes a match the search did not list - every match is recognised by its place and
// length (RowOfMatchAnyOrder), so a match the walk meets that no row accounts for is stepped over and
// the row is reported missing. So the choice only decides which shape comes out missing: a GREP query
// holding ^ walks backward, everything else forward (WriteBackward). Retrying the missing rows the other
// way round after the fact was considered and dropped: by then the text has already moved, and the
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
#ifdef KFC_DIAG
	// (Fault switch perf-backward, a test build's only - KFCDiag.h: every write walks backward, to measure what the
	// direction does to the find's time. Throwaway documents only.)
	if (KFC_DIAG_FAULT("perf-backward"))
		return true;
#endif
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil || opts->GetSearchMode() != IFindChangeOptions::kGrepSearch)
		return false;
	return GrepQueryHoldsLineStart(opts->GetFindString(IFindChangeOptions::kGrepSearch));
}

// Which pending row is the match the walk has just found - by its place alone (the thread, how far into
// it, and the length), in whatever order the walk meets the rows. -1 = no row: a match the search never
// listed, which the walk steps over. (Taken from spike/2026-09-26-backward-replace.)
//
// WHY NOT BY COUNT. "The Nth match found now is the Nth row" holds only while the writing walk meets the
// matches the search met, and a replacement can make a NEW one: GREP's ^ and $ read the real text past the
// point the walk resumes from (lookbehind, lookahead and \b do not -
// docs/ai-notes/kbs-bughunt-2026-08-09.md). Measured: deleting the "a" of "ab<CR>ab" under GREP a|^b left the
// "b" at the start of its paragraph - a match the search never listed - and counting gave it the second row's
// turn: "<CR>ab" instead of "b<CR>b", with "2 replaced" said. InDesign's own Change All collects every position
// before it writes (spellpanel SpellReplaceWalker.cpp:748, :823); a list made BEFORE the replacements has to
// recognise each match itself.
//
// THE LENGTH AS WELL AS THE PLACE. A new match can begin exactly where a row begins and still not be
// that row. Measured: GREP \r|^a|ab on "x<CR>ab" lists <CR>@1 and a@2; deleting
// the return runs "ab" on from x, and the walk meets ab@1 - the second row's start now, two characters
// where the row listed one. Taken as that row it deleted "ab" (KFC left "x" and said "2 replaced", where
// Change All leaves "xb"). A listed match's own length cannot change on the way - the walk never sees past
// the point it resumes from - so a length that differs names a match nobody listed: it is stepped over,
// and the row is left and reported missing rather than written with text nobody ticked.
//
// outDict / outKey / outThreadStart = the thread the match is in, and where it starts - what the caller
// carries the other rows past once the row is written (read once per match, here).
int32 RowOfMatchAnyOrder(IDataBase* db, const std::vector<RowNow>& rowNow, const std::set<int32>& pending,
	UID story, TextIndex start, TextIndex end, UID& outDict, uint32& outKey, TextIndex& outThreadStart)
{
	if (!ThreadAt(db, story, start, outDict, outKey, outThreadStart))
		return -1;
	const TextIndex offset = start - outThreadStart;
	const int32 length = end - start;
	for (std::set<int32>::const_iterator it = pending.begin(); it != pending.end(); ++it)
	{
		const RowNow& row = rowNow[static_cast<size_t>(*it)];
		if (row.known && row.story == story && row.threadDict == outDict && row.threadKey == outKey
			&& row.offset == offset && row.length == length)
			return *it;
	}
	return -1;
}

// One story's walk: every pending row the walk meets is written, one at a time, in the direction the
// session is set to (the caller's KFCBackwardSearchScope). What the walk writes moves every row after it
// in the same thread (CarryRowsPast); a written row is put where its new text stands. A row that would
// write at an endnote's end is left, and said so (MatchEndsAnEndnote). What is still in `pending` at the
// end never came up. False = the walk could not start at all; outWalkFailed = it started and broke off.
// ioUnrecorded = the rows written that Track Changes recorded nothing for (a footnote, or a replace that
// changed no character): the status lines say those cannot be taken back.
// (Cancel is not asked in here - between stories, by the caller: the author's call, story by story.)
bool WalkStoryReplacing(int32 chapterIdx, const UIDRef& storyRef, const WalkerScopeOptions& scopeOptions,
	IFindChangeOptions* opts, std::vector<RowNow>& rowNow, std::set<int32>& pending, std::vector<int32>& keptRows,
	int32& ioReplaced, int32& ioRefused, int32& ioEndnoteLeft, int32& ioUnrecorded, bool& outWalkFailed,
	KFCProgressBar* progressBar, int32 progressBase, int32& ioProgressReported, int32& ioDone,
	bool& outSignFailed)
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
#ifdef KFC_DIAG
		// TEST BUILDS ONLY: where a story's walk spends its time (2026-10-05 - a query run that wrote 6000 rows in two
		// 3000-row stories took 23 minutes; each story was ONE paragraph, which every find recomposed whole - in short
		// paragraphs a row costs about 7 ms, flat). Summed per phase over one story's walk and written once (WALKTIME); every
		// 100 finds the last 100's time, with the foci the story carries (WALKSTEP - does something pile up?).
		double tFind = 0, tMatch = 0, tPre = 0, tReplace = 0, tSign = 0, tRecord = 0, tTexts = 0, tCarry = 0;
		double tFindStep = 0;
		int32 timedRows = 0, finds = 0;
		KFCDiagPerf stepPerf;		// InDesign's counters over the last 100 finds (KFCDiag.h)
		int stepCommands = KFCDiagCommands::Count();	// ...and the commands processed (0 unless perf-commands is on)
		// The fault switch perf-fresh-walker (KFCDiag.h): the walk started again after every `restartEvery` writes -
		// forward only (a backward walk's restart point would be the write's START, not measured).
		const int restartMode = KFCDiagFaultValue("perf-fresh-walker", 0, 0);
		const int restartEvery = KFCDiagFaultValue("perf-fresh-walker", 1, 50);
		const bool walkingBackward = opts->GetSearchBackwards(opts->GetSearchMode()) != kFalse;
		int32 writesSinceRestart = 0, restarts = 0;
		bool justRestarted = false;
		TextIndex restartAt = kInvalidTextIndex;
#endif
		// The range the last replacement wrote: a match inside it ("cat" -> "cat cat") is none of ours.
		UID lastStory = kInvalidUID;
		TextIndex lastStart = kInvalidTextIndex, lastEnd = kInvalidTextIndex;
		while (!pending.empty())
		{
			UIDRef story;
			TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
#ifdef KFC_DIAG
			// the 100th find's story: its first damaged frame before the find (-1 = none) - does the find compose?
			int32 damagedBefore = -2;
			if ((finds + 1) % 100 == 0)
			{
				InterfacePtr<ITextModel> damageModel(storyRef, UseDefaultIID());
				InterfacePtr<IFrameList> damageFrames(damageModel != nil ? damageModel->QueryFrameList() : nil);
				damagedBefore = (damageFrames != nil) ? damageFrames->GetFirstDamagedFrameIndex() : -3;
			}
#endif
			KFC_CLOCK(cFind);
			const IFindChangeService::FindChangeResult found = RunWalkerCmd(kFindTextCmdBoss, walker, story, start, end);
			KFC_SPENT(tFind, cFind);
#ifdef KFC_DIAG
			KFC_SPENT(tFindStep, cFind);
			if (justRestarted)
			{
				KFC_DIAG_LOG("RESTART story=%u n=%d at=%d first=%d result=%d", storyRef.GetUID().Get(), (int)restarts,
					(int)restartAt, (int)start, (int)found);
				justRestarted = false;
			}
			if (++finds % 100 == 0)
			{
				InterfacePtr<ITextModel> stepModel(storyRef, UseDefaultIID());
				InterfacePtr<ITextFocusManager> stepFoci(stepModel, UseDefaultIID());
				InterfacePtr<IFrameList> stepFrames(stepModel != nil ? stepModel->QueryFrameList() : nil);
				char stepCounters[300] = { 0 };
				stepPerf.Since(stepCounters, sizeof(stepCounters));
				const int commandsNow = KFCDiagCommands::Count();
				KFC_DIAG_LOG("WALKSTEP story=%u finds=%d rows=%d last100=%.1f ms/find foci=%d length=%d at=%d walker=%p damaged=%d->%d cmds=%d %s",
					storyRef.GetUID().Get(), (int)finds, (int)timedRows, tFindStep / 100.0,
					(stepFoci != nil) ? (int)stepFoci->GetFocusCount() : -1,
					(stepModel != nil) ? (int)stepModel->TotalLength() : -1, (int)start, (void*)walker.get(),
					(int)damagedBefore, (stepFrames != nil) ? (int)stepFrames->GetFirstDamagedFrameIndex() : -3,
					commandsNow - stepCommands, stepCounters);
				tFindStep = 0;
				stepPerf.Take();
				stepCommands = commandsNow;
			}
#endif
			if (found != IFindChangeService::kSuccess)
			{
				if (found == IFindChangeService::kFailure)
					outWalkFailed = true;
				break;
			}
			if (story.GetUID() == lastStory && start >= lastStart && start < lastEnd)
				continue;
			// The thread and the offset into it BEFORE the command - what every row's place is kept in.
			UID matchDict = kInvalidUID;
			uint32 matchKey = 0;
			TextIndex matchThreadStart = kInvalidTextIndex;
			KFC_CLOCK(cMatch);
			const int32 hitIdx = RowOfMatchAnyOrder(db, rowNow, pending, story.GetUID(), start, end,
				matchDict, matchKey, matchThreadStart);
			KFC_SPENT(tMatch, cMatch);
			if (hitIdx < 0)
				continue;
			pending.erase(hitIdx);

			if (MatchEndsAnEndnote(story, end))
			{
				// InDesign's replace at an endnote's end breaks the endnote's range for good (see
				// MatchEndsAnEndnote) - one at a time, only this row is left.
				++ioEndnoteLeft;
				KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeEndnoteLeft);
				KeepRowAt(db, rowNow, keptRows, hitIdx, story.GetUID(), start, end);
			}
			else
			{
				// The row's text as it stands the moment before it is written (Hit::originalText).
				KFC_CLOCK(cPre);
				const PMString original = KFCTrackChange::ReadText(story, start, end - start);
				// A QUERY RUN'S CHAINED WRITE (2026-10-05): which characters of the match an earlier query of the run
				// wrote, and the run's deletions around it, read now - once the row is written, the deletions that grew
				// are masked where the run's own characters went into them (KFCTrackChange.h, RunOwnChars /
				// SnapshotRunDeletions / NoteRunDeletions). Nothing outside a query run: no floor, no mask.
				WideString runMatch;
				std::vector<bool> runOwn;
				std::vector<KFCTrackChange::RunDeletion> runBefore;
				const bool inQueryRun = KFCTrackChange::RunOwnChars(story, start, end, runMatch, runOwn);
				if (inQueryRun)
					KFCTrackChange::SnapshotRunDeletions(story, start, end, runBefore);
				KFC_SPENT(tPre, cPre);
				UIDRef written;
				TextIndex writtenStart = kInvalidTextIndex, writtenEnd = kInvalidTextIndex;
				// (Fault switch replace-refuse, a test build's only - KFCDiag.h: InDesign's replace refuses every row,
				// the one way a test reaches a chapter where nothing lands after its pending changes were accepted.)
				bool refuseForTest = false;
#ifdef KFC_DIAG
				refuseForTest = KFC_DIAG_FAULT("replace-refuse");
#endif
				KFC_CLOCK(cReplace);
				const bool replacedHere = !refuseForTest
					&& RunWalkerCmd(kTWReplaceTextCmdBoss, walker, written, writtenStart, writtenEnd) == IFindChangeService::kSuccess;
				KFC_SPENT(tReplace, cReplace);
				if (replacedHere)
				{
					++ioReplaced;
#ifdef KFC_DIAG
					++timedRows;
#endif
					// SIGNED BEFORE THE NEXT ONE IS WRITTEN. "KohakuFindChange" at the
					// row's time (KFCTrackChange.h) - a replace written next to this one has to meet records
					// already signed, or InDesign joins its insertion to this one's.
					KFC_CLOCK(cSign);
					const uint64 stamp = KFCTrackChange::StampForRow(chapterIdx, hitIdx);
					bool skipSignForTest = false;
#ifdef KFC_DIAG
					// (Fault switch perf-no-sign, a test build's only - KFCDiag.h: a measurement of what signing costs the next
					// find; the rows are left unsigned. Never on a document anybody keeps.)
					skipSignForTest = KFC_DIAG_FAULT("perf-no-sign");
#endif
					// A QUERY RUN SIGNS AT ITS END (the spec's D14 - KFCTrackChange::SignRunPlaces): nothing is signed here
					// while one writes, so a later query deleting what this row writes deletes the user's own insertion.
					const bool deferSign = KFCTrackChange::QueryRunWriting();
					const bool signedHere = skipSignForTest || deferSign
						|| KFCTrackChange::SignReplace(written, writtenStart, writtenEnd, stamp);
					KFC_SPENT(tSign, cSign);
					if (!signedHere)
					{
						outSignFailed = true;
						break;
					}
					// The row's time only when a record carries it; 0 = none (a footnote records nothing, nor does a
					// replace that changed no character - Change Format with an empty Change To) - set either way, so
					// a row replaced once, taken back and replaced again where nothing is recorded does not keep the
					// first replace's time. AND WHERE ITS FIRST RECORD STANDS IN WHAT IT WROTE: 0 for a
					// whole-match replace; a GREP $n keeps the matched characters it names, and its records
					// start past them (Hit::recordLead).
					KFC_CLOCK(cRecord);
					TextIndex firstRecord = kInvalidTextIndex;
					const bool recorded = deferSign
						? KFCTrackChange::HasRunRecordIn(written, writtenStart, writtenEnd)		// unsigned: no time to look for
						: KFCTrackChange::FirstRecordOfTimeIn(written, writtenStart, writtenEnd, stamp, firstRecord);
					if (deferSign && recorded)
						firstRecord = writtenStart;		// (the query run's list is rebuilt from the records - this row's lead is not read)
					KFCResultModel::SetHitRecord(chapterIdx, hitIdx, recorded ? stamp : 0,
						recorded ? static_cast<int32>(firstRecord - writtenStart) : 0);
					if (!recorded)
						++ioUnrecorded;
					if (inQueryRun)
						KFCTrackChange::NoteRunDeletions(written, writtenStart, writtenEnd, runBefore, runMatch, runOwn);
					KFC_SPENT(tRecord, cRecord);
					lastStory = written.GetUID();
					lastStart = writtenStart;
					lastEnd = writtenEnd;
					// ITS TWO TEXTS, TAKEN HERE - once, not read again for every row before and after the
					// chapter. The text written now is the text the row holds at the end: no later replace of
					// the run writes inside it, since every other row is carried outside it and a match there
					// is none of theirs (stepped over, above).
					// Set before MarkHitReplaced: the row's locator says "no track" at once when the replace
					// changed no character (KFCResultModel::GetHitTextUnchanged), which asks them.
					KFC_CLOCK(cTexts);
					KFCResultModel::SetHitChangeTexts(chapterIdx, hitIdx, original,
						KFCTrackChange::ReadText(written, writtenStart, writtenEnd - writtenStart));
					KFCResultModel::MarkHitReplaced(chapterIdx, hitIdx, written.GetUID(), writtenStart, writtenEnd);
					KFC_SPENT(tTexts, cTexts);
					KFC_CLOCK(cCarry);
					// every row after it first, then this row at what was written - so it is not moved by itself
					CarryRowsPast(rowNow, story.GetUID(), matchDict, matchKey, start - matchThreadStart,
						end - matchThreadStart, writtenEnd - writtenStart);
					KeepRowAt(db, rowNow, keptRows, hitIdx, written.GetUID(), writtenStart, writtenEnd);
					KFC_SPENT(tCarry, cCarry);
#ifdef KFC_DIAG
					// THE WALK STARTED AGAIN (fault switch perf-fresh-walker - a measurement, not the product's walk): a
					// new scope and a new find/change client on the walker, mode 2 from where this write ended (a
					// story scope given a range starts there and loops back to the top - IWalkerScopeFactoryUtils.h;
					// the rows the loop meets again are none of the pending ones, RowOfMatchAnyOrder), mode 3 from the
					// top. GREP's lookbehind does not see past where a walk resumes either way.
					if (restartMode >= 2 && restartEvery > 0 && !walkingBackward && !pending.empty()
						&& ++writesSinceRestart >= restartEvery)
					{
						writesSinceRestart = 0;
						Text::StoryRangeList from;
						from.push_back(Text::StoryRange(writtenEnd, writtenEnd));
						InterfacePtr<ITextWalkerScope> freshScope((restartMode == 2)
							? Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(storyRef, from, scopeOptions)
							: Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(storyRef, scopeOptions));
						InterfacePtr<ITextWalkerClient> freshClient(static_cast<ITextWalkerClient*>(
							::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
						if (freshScope != nil && freshClient != nil)
						{
							if (walker->IsWalking())
								walker->Halt();
							walker->Initialize(freshClient, freshScope, opts, nil);
							scope.reset(freshScope.forget());
							client.reset(freshClient.forget());
							++restarts;
							justRestarted = true;
							restartAt = writtenEnd;
							// the written range is behind the walk now; a loop back past it meets no pending row
						}
						else
							KFC_DIAG_LOG("RESTART story=%u could not build the scope or the client", storyRef.GetUID().Get());
					}
#endif
				}
				else
				{
					++ioRefused;
					KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeRefused);
					KeepRowAt(db, rowNow, keptRows, hitIdx, story.GetUID(), start, end);
				}
			}
			++ioDone;
			KFCAdvanceProgress(progressBar, ioProgressReported, progressBase + ioDone);
		}
#ifdef KFC_DIAG
		KFC_DIAG_LOG("WALKTIME story=%u rows=%d finds=%d restarts=%d find=%.0f match=%.0f pre=%.0f replace=%.0f sign=%.0f record=%.0f texts=%.0f carry=%.0f ms",
			storyRef.GetUID().Get(), (int)timedRows, (int)finds, (int)restarts, tFind, tMatch, tPre, tReplace, tSign, tRecord,
			tTexts, tCarry);
#endif
	}
	if (walker->IsWalking())
		walker->Halt();
	return true;
}

// A QUERY RUN'S STORY, WRITTEN WITH INDESIGN'S OWN CHANGE ALL (2026-10-05, the spec's D13). Written one match at a
// time, a row costs a find and a replace - about 7 ms in a document of ordinary paragraphs (1000 tracked rows in
// 8 s, measured that night: docs/ai-notes/kfc-speedup-ideas-2026-10-05.md section 9), and far more when the matches
// stand in one long paragraph, where each find recomposes all of it (36 ms a find at 1000 rows - the measurement this
// was first decided on); InDesign's Change All writes 3000 tracked matches in 4.4 s. So a query run - which writes every match
// it finds, no row picked out - hands the story to kReplaceAllTextCmdBoss: the command of the Find/Change dialog's
// Change All, run the way SnpFindAndReplace.cpp runs it (ProcessFindChangeCommand: a walker initialised on a scope,
// the find/change client and the session's options, inside the selections' critical section). GREP and its $n are
// the dialog's own, as with the one-at-a-time commands beside it.
// THE SCOPE IS THE SEARCH'S: `changeAllScope` = kDocumentScope - the story's own (QueryStoryWalkerScope, as
// WalkStoryReplacing walks it); a Story / To End of Story / Selection Search: - that scope again
// (QueryWalkerScope_UsingSelections, as the search took it), so nothing outside it is written.
// outCount = what the command reports (IFindChangeCmdData::GetReplacementCount), -1 when it says nothing. False = the
// walk could not start or the command failed; the error state is left clear.
bool ChangeAllInStory(const UIDRef& storyRef, int32 changeAllScope, const WalkerScopeOptions& scopeOptions,
	IFindChangeOptions* opts, int32& outCount)
{
	outCount = -1;
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(registry != nil
		? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss) : nil);
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	InterfacePtr<ITextWalkerScope> scope(
		(changeAllScope == static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope))
			? Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(storyRef, scopeOptions)
			: Utils<IWalkerScopeFactoryUtils>()->QueryWalkerScope_UsingSelections(
				static_cast<IWalkerScopeFactoryUtils::WalkScopeType>(changeAllScope), scopeOptions));
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (walker == nil || scope == nil || client == nil || opts == nil)
		return false;
	if (walker->IsWalking())
		walker->Halt();
	walker->Initialize(client, scope, opts, nil);
	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kReplaceAllTextCmdBoss));
	InterfacePtr<IFindChangeCmdData> cmdData(cmd, UseDefaultIID());
	bool ok = false;
	if (selUtils != nil && cmd != nil && cmdData != nil)
	{
		const TextWalkerSelections_CriticalSection criticalSection(selUtils);
		cmdData->SetTextWalker(walker);
		if (CmdUtils::ProcessCommand(cmd) == kSuccess)
		{
			const IFindChangeService::FindChangeResult result = cmdData->GetFindChangeResult();
			ok = (result != IFindChangeService::kFailure);
			outCount = cmdData->GetReplacementCount();
			KFC_DIAG_LOG("CHANGEALL story=%u result=%d count=%d", storyRef.GetUID().Get(), (int)result, (int)outCount);
		}
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (walker->IsWalking())
		walker->Halt();
	return ok;
}

// IS THE ROW'S STORED PLACE STILL ITS TEXT? A row is carried and read back only when it is: a replaced
// row is first put where its tracked change stands (the record moves with the text -
// RefreshRowFromRecords), any other row must still read as it was found (KFCSearchEngine::RowReadsAsFound
// - the jump's own test). An edit made since can have moved the rest - reading such a row back at its old
// place takes the line AND the hash from the wrong characters, so a click then selects them (measured,
// case edit-then-reject: "ZZkitt"); left alone, it says "missing" when clicked, which is the truth.
bool RowStillStands(int32 chapterIdx, int32 hitIdx, IDataBase* db)
{
	bool checked = false, replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked))
		return false;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	const bool haveIdentity = KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash);
	if (replaced)
	{
		// THE CHEAP TEST FIRST. Matching a replaced row against every
		// tracked change of its story costs a pass over the chapter's rows and the story's records, and
		// this runs for every row of the chapter on every Reject / Redo / right-click Replace - squared
		// in the rows. The ordinary case is a row whose stored place still holds the text its replace
		// wrote: that is asked by reading those characters. Only a row that fails it (an edit since) is
		// looked for through its records.
		PMString originalText, replacedText;
		if (haveIdentity && KFCResultModel::GetHitChangeTexts(chapterIdx, hitIdx, originalText, replacedText)
			&& !replacedText.IsEmpty()
			&& KFCTrackChange::ReadText(UIDRef(db, story), a, b - a) == replacedText)
			return true;
		return KFCTrackChange::RefreshRowFromRecords(chapterIdx, hitIdx);
	}
	// The match AND the line around it: a row an Undo left on another occurrence of its own text passes the
	// match's hash alone, and would then be read back THERE - from then on describing that other
	// occurrence, with a line to match.
	return KFCSearchEngine::RowReadsAsFound(chapterIdx, hitIdx, db);
}

// ======================================================================================================
// A STORY'S VERSION (the author's call: "safety first"). A row's place is carried past every change KFC makes
// and past nothing else: typing, Ctrl+Z / Ctrl+Shift+Z, the Track Changes panel or a script can move the text
// under it, and the place can then stand on ANOTHER occurrence of the same text, which the match's hash cannot
// tell apart ("catcatcatcat", row 1 replaced from its menu, Ctrl+Z: rows 2 and 3 were left on the third and
// fourth "cat", and a Change Checked wrote there - before this door stood). InDesign keeps a version of every
// story (ITextModel::GetChangeCount - moved by any change to its text, attributes, tables or inlines, and moved
// BACK by Undo to exactly the value it had): the search records it for every story holding a hit, each change
// KFC makes records the new one, and nothing is written to a story whose version is not the one recorded. The
// price, accepted: an edit ANYWHERE in such a story between the search and the replace means searching again.
// EXCEPT AN UNDO OR A REDO OF A WRITE OF KFC'S OWN (KFCUndoFollow). The panel follows those: the rows AND
// the versions recorded here are put back as they were on that side of the write, so the story is at the
// version on record again and the rows stand where their text does. The example above ("catcatcatcat",
// Ctrl+Z) is such an Undo and is followed (case undo-shift-then-change);
// typing, an Undo of anything else, the Track Changes panel and a script still stop the write.
// ======================================================================================================

// Is the story at the version KFC last recorded for it? Nothing recorded, or unreadable, answers no.
bool StoryAsKFCLeftIt(int32 chapterIdx, IDataBase* db, UID story)
{
	uint32 recorded = 0, now = 0;
	const bool had = KFCResultModel::GetStoryVersion(chapterIdx, story, recorded);
	const bool read = had && KFCSearchEngine::ReadStoryVersion(db, story, now);
	KFC_DIAG_LOG("DOOR version chapter=%d story=%u recorded=%s%u now=%s%u -> %s", chapterIdx, story.Get(),
		had ? "" : "none:", recorded, read ? "" : "unread:", now, (read && recorded == now) ? "as left" : "MOVED");
	return read && recorded == now;
}

// Of `stories`, the ones at the version KFC last recorded - asked BEFORE a change of KFC's own, so that
// only those take their new version after it (NoteStoryVersions). A story that had already moved without
// KFC keeps the version it had: taking its new one would vouch for rows nobody has looked at since.
void StoriesAsKFCLeftThem(int32 chapterIdx, IDataBase* db, const std::set<UID>& stories, std::set<UID>& out)
{
	out.clear();
	for (std::set<UID>::const_iterator s = stories.begin(); s != stories.end(); ++s)
		if (StoryAsKFCLeftIt(chapterIdx, db, *s))
			out.insert(*s);
}

// After a change KFC made and kept: the stories it found as it had left them take their version now.
void NoteStoryVersions(int32 chapterIdx, IDataBase* db, const std::set<UID>& stories)
{
	for (std::set<UID>::const_iterator s = stories.begin(); s != stories.end(); ++s)
	{
		uint32 now = 0;
		if (KFCSearchEngine::ReadStoryVersion(db, *s, now))
			KFCResultModel::SetStoryVersion(chapterIdx, *s, now);
	}
}

// THE CHAPTER'S REPLACE. Every ticked row is written by the walk of its story,
// with Track Changes on for the stories written (TrackingScope) and the direction the caller set.
// Refuses before anything is written - returns false, outWhyNot says why - only when the document, the
// Find/Change options or a row cannot be read. The pending tracked changes the ticked matches sit in or
// next to are accepted before the first write (outAcceptedFirst = how many) - except around a match at
// an endnote's end, which the walk leaves. outCancelled / outFailed: the caller aborts the
// whole run. outUnrecorded = the rows written that Track Changes recorded nothing for (WalkStoryReplacing).
bool ReplaceInChapterOneByOne(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	KFCProgressBar* progressBar, int32 progressBase, int32& ioProgressReported,
	int32& outReplaced, int32& outMissing, int32& outLocked, int32& outRefused, int32& outEndnoteLeft,
	int32& outUnrecorded, int32& outAcceptedFirst, bool& outWalkFailed, bool& outCancelled, bool& outFailed,
	PMString& outWhyNot, const std::set<int32>* onlyHits = nil, int32 changeAllScope = -1)
{
	outAcceptedFirst = 0;
	outReplaced = 0;
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;
	outEndnoteLeft = 0;
	outUnrecorded = 0;
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
	// GLYPH AND TRANSLITERATE ARE WRITTEN TOO (the author's call). One match at a time is the walk those
	// two tabs were built and measured on: ReplaceChecked has already stated the change glyph or the
	// character type the command writes (KFCSearchEngine::CommitReplaceSide), and the menu refuses the
	// Object and Colour tabs before any of this.

	// Every row's place now (RowNow) - the ticked ones are what the walks look for, and every row is
	// carried past what they write, for the read-back at the end.
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	std::vector<RowNow> rowNow(static_cast<size_t>(hitCount > 0 ? hitCount : 0));
	std::vector<int32> keptRows;						// the rows the report keeps, read back at the end
	std::map<UID, std::set<int32> > pendingByStory;		// the ticked, editable rows, story by story
	std::map<std::pair<UID, UID>, bool> editableFrames;
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		if (!KFCResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked))
		{
			outWhyNot = "a row could not be read";
			return false;
		}
		// WHAT THIS RUN WRITES. A ticked row not yet replaced (IsHitCheckedWork) - or, for the right-click
		// Replace (onlyHits), those rows: ticked or not for a row, the ticked ones for a story. A row
		// already replaced (by a right-click Replace before this run) is not written, only carried - it
		// does not refuse the run with "search again"; so is every row, for the one-row Replace, since its
		// list stays a work list.
		const bool target = (onlyHits != nil)
			? (onlyHits->count(i) != 0 && !replaced && !locked
				&& KFCResultModel::IsWorkOutcome(KFCResultModel::GetHitOutcome(chapterIdx, i)))
			: KFCResultModel::IsHitCheckedWork(chapterIdx, i);
		if (!target)
		{
			// a locked row the report keeps (it never had a box), a replaced row - and every row, for
			// the one-row Replace: each has to stand where its text is afterwards. ONLY a row whose
			// stored place is still its text (RowStillStands - a replaced row is put where its tracked
			// change stands first): one an edit has moved is left as it is, never read back at a place
			// that is no longer its own.
			// ...and a row with an outcome (taken back with Reject Change, above all): Change Checked's report
			// keeps it (KFCResultModel::KeepCheckedRows) - left out, a row taken back and not ticked would stay
			// at its old place in the report, and its Replace refuse as "changed since the search" (case
			// rejected-row-after-change).
			// Its place is taken only here, after RowStillStands, which can move it: every other row not
			// written stays unknown - neither carried nor read back.
			if ((onlyHits != nil || locked || replaced
					|| KFCResultModel::GetHitOutcome(chapterIdx, i) != KFCResultModel::kOutcomeNone)
				&& RowStillStands(chapterIdx, i, db))
			{
				UID s2 = kInvalidUID;
				TextIndex a2 = kInvalidTextIndex, b2 = kInvalidTextIndex;
				uint64 h2 = 0;
				if (KFCResultModel::GetHitMatchIdentity(chapterIdx, i, s2, a2, b2, h2))
					SetRowAt(db, rowNow[static_cast<size_t>(i)], s2, a2, b2);
				keptRows.push_back(i);
			}
			continue;
		}
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, i, story, start, end, hash))
			SetRowAt(db, rowNow[static_cast<size_t>(i)], story, start, end);
		if (!rowNow[static_cast<size_t>(i)].known)
		{
			++outMissing;
			KFCResultModel::SetHitOutcome(chapterIdx, i, KFCResultModel::kOutcomeMissing);
			continue;
		}
		const UIDRef storyRef(db, story);
		const UID frameUID = KFCSearchEngine::EditableFrameForMatch(storyRef, start);
		const std::pair<UID, UID> frameKey(story, frameUID);
		std::map<std::pair<UID, UID>, bool>::const_iterator known = editableFrames.find(frameKey);
		bool editable = false;
		if (known != editableFrames.end())
			editable = known->second;
		else
			editableFrames[frameKey] = editable = KFCSearchEngine::IsFrameEditable(storyRef, frameUID);
		if (!editable)
		{
			// locked since the search: never written, counted as locked
			++outLocked;
			KFCResultModel::SetHitOutcome(chapterIdx, i, KFCResultModel::kOutcomeLocked);
			keptRows.push_back(i);
			continue;
		}
		// (A ticked match inside or next to the user's own pending insertion does not refuse the run -
		//  replacing it would leave no record, so it could never be taken back: the pending changes around
		//  the ticked matches are accepted before anything is written instead - below.)
		pendingByStory[story].insert(i);
	}
	// Nothing to write: every row kept was placed a moment ago from where it stands, and nothing has moved
	// since - nothing to write back either.
	if (pendingByStory.empty())
		return true;

	// ===== from here on things are WRITTEN. A cancel or a failure below leaves the caller to abort. =====
	const int32 replacedBefore = outReplaced;

	// THE PENDING CHANGES A TICKED MATCH SITS IN OR NEXT TO ARE ACCEPTED FIRST - ANYBODY'S, AND NOTHING
	// ELSE (the author's call: "only that part"). A replace written inside or next to the user's own
	// pending insertion leaves no record of its own (InDesign rewrites the insertion it has - VOSRedline.h
	// CanApplyDeleteChange; case rereplace-ours), so it could never be taken back - and refusing the run
	// for it is not the answer either. Not the whole document: the rest of it keeps its records - an
	// earlier replace's rows keep their Reject Change. Inside the run's sequence, so the Undo and a cancel
	// take it back with the replaces.
	// ! Each row's place is asked NOW, from its thread offset (RowStartNow): an accepted deletion's
	//   deleted-text thread goes, which moves the story indexes of the cells and footnotes behind it.
	KFC_CLOCK(cAccept);
	for (std::map<UID, std::set<int32> >::const_iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
	{
		const UIDRef storyRef(db, s->first);
		if (!KFCTrackChange::StoryHasChanges(storyRef))
			continue;
		for (std::set<int32>::const_iterator p = s->second.begin(); p != s->second.end(); ++p)
		{
			const RowNow& row = rowNow[static_cast<size_t>(*p)];
			TextIndex at = kInvalidTextIndex;
			if (!RowStartNow(db, row, at))
				continue;
			// NOT AROUND A ROW THAT IS NOT WRITTEN. A match at an endnote's end is left by the walk below
			// (MatchEndsAnEndnote, the same question), so nothing next to it is accepted either - an accept for a
			// replace that never comes, and with nothing written in the chapter,
			// HandBackChaptersWithNothingInThem would put its modified flag back over the accept (case
			// endnote-end-pending-kept: the user's Z next to the endnote's last match).
			if (MatchEndsAnEndnote(storyRef, at + row.length))
				continue;
			PMString why;
			why.SetTranslatable(kFalse);
			const int32 accepted = KFCTrackChange::AcceptPendingAround(storyRef, at, at + row.length, why);
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
#ifdef KFC_DIAG
	{
		double tAccept = 0;
		KFC_SPENT(tAccept, cAccept);
		KFC_DIAG_LOG("ACCEPTTIME rows=%u accepted=%d %.0f ms", (unsigned)rowNow.size(), (int)outAcceptedFirst, tAccept);
	}
#endif

	std::set<UID> targetStories;
	for (std::map<UID, std::set<int32> >::const_iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
		targetStories.insert(s->first);
#ifdef KFC_DIAG
	// (Fault switch perf-no-track, a test build's only - KFCDiag.h: a measurement of what Track Changes costs the
	// walk; the stories are written untracked. Never on a document anybody keeps.)
	if (KFC_DIAG_FAULT("perf-no-track"))
		targetStories.clear();
#endif
	KFCTrackChange::TrackingScope tracking(db, targetStories);
	if (!tracking.Ok())
	{
		outFailed = true;
		outWhyNot = "Track Changes could not be switched on";
		return true;
	}

	int32 done = 0;		// ticked rows accounted for, for the bar
	for (std::map<UID, std::set<int32> >::iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
	{
		// Between stories is where a cancel is heard (the author's call: story by story - not inside a story's
		// walk as well, which was tried; the search takes the same step). The whole run is then aborted and put
		// back.
		if (progressBar != nil && progressBar->WasCancelled(kFalse))
		{
			outCancelled = true;
			return true;
		}
		std::set<int32>& pending = s->second;
		// A QUERY RUN WRITES A STORY WITH CHANGE ALL (2026-10-05, the spec's D13 - ChangeAllInStory): every match the
		// search listed there is written, none picked out - a match at an endnote's end too, as InDesign's own Change
		// All writes it (which breaks that endnote's range - InDesign's own fault, the author's call: "ignore it";
		// the one-at-a-time walk below still leaves such a match for Change Checked).
		if (changeAllScope >= 0 && db->IsValidUID(s->first))
		{
			const UIDRef storyRef(db, s->first);
			int32 inFootnotes = 0;
			for (std::set<int32>::const_iterator p = pending.begin(); p != pending.end(); ++p)
			{
				const RowNow& row = rowNow[static_cast<size_t>(*p)];
				TextIndex at = kInvalidTextIndex;
				if (RowStartNow(db, row, at) && KFCTrackChange::IsInFootnote(storyRef, at))
					++inFootnotes;
			}
			{
				// "Recorded?" asked of the whole story before and after: a footnote records nothing, nor does a
				// format-only change - a story that had no record of the run's and has none now recorded nothing.
				const TextIndex wholeStory = 0x7FFFFFFF;
				const bool hadRunRecords = KFCTrackChange::HasRunRecordIn(storyRef, 0, wholeStory);
				int32 count = -1;
				if (!ChangeAllInStory(storyRef, changeAllScope, scopeOptions, opts, count))
				{
					outFailed = true;
					outWhyNot = "InDesign's Change All could not run";
					return true;
				}
				const int32 rows = static_cast<int32>(pending.size());
				const int32 written = (count > 0) ? count : rows;
				outReplaced += written;
				outUnrecorded += (!hadRunRecords && !KFCTrackChange::HasRunRecordIn(storyRef, 0, wholeStory))
					? written : inFootnotes;
				done += rows;
				pending.clear();
				KFCAdvanceProgress(progressBar, ioProgressReported, progressBase + done);
				continue;
			}
		}
		// A story an earlier story's replace deleted (an anchored object's): its rows never come up.
		if (db->IsValidUID(s->first))
		{
			bool signFailed = false;
			if (!WalkStoryReplacing(chapterIdx, UIDRef(db, s->first), scopeOptions, opts, rowNow, pending, keptRows,
				outReplaced, outRefused, outEndnoteLeft, outUnrecorded, outWalkFailed, progressBar, progressBase,
				ioProgressReported, done, signFailed))
			{
				outFailed = true;
				outWhyNot = "the text walker could not be started";
				return true;
			}
			// a replace whose records could not be signed stops the whole run (the caller rolls it back)
			if (signFailed)
			{
				outFailed = true;
				outWhyNot = "the tracked changes could not be signed";
				return true;
			}
		}
		for (std::set<int32>::const_iterator p = pending.begin(); p != pending.end(); ++p)
		{
			TextIndex at = kInvalidTextIndex;
			if (!db->IsValidUID(s->first) || !RowStartNow(db, rowNow[static_cast<size_t>(*p)], at))
			{
				// Its thread went with the footnote, table or object another ticked row deleted (or its
				// story with an anchored frame): asked for, and done - with its object. Counted as replaced
				// and shown as "deleted" (SetHitDeleted).
				++outReplaced;
				KFCResultModel::SetHitDeleted(chapterIdx, *p);
			}
			else
			{
				++outMissing;
				KFCResultModel::SetHitOutcome(chapterIdx, *p, KFCResultModel::kOutcomeMissing);
			}
			++done;
		}
		KFCAdvanceProgress(progressBar, ioProgressReported, progressBase + done);
	}
	// THE RECORDS JUST WRITTEN, IN AMBER (the author's call). Their author, KFC's
	// name, given the UI colour Amber in this document (KFCTrackChange::ColourSignAuthor) - inside the run's
	// sequence, so the Undo takes it back with the replace. Nothing stops the replace if it does not go in.
	if (outReplaced > replacedBefore)
		(void)KFCTrackChange::ColourSignAuthor(db);

	// ----- every row the report keeps, where its text stands now: its range and its line. Read once the
	// chapter has stopped changing: a line read as its own match was written would still show the later
	// matches of its paragraph as they were. (A replaced row's time - what Reject Change and the jump find
	// its records by - is set when it is written, WalkStoryReplacing, not read back off the records here.)
	// (Not for a query run's Change All: nothing carried the rows past what it wrote, and a query run's list is
	// rebuilt from the records anyway - KFCShowChanges::ListOwnRun.)
	if (changeAllScope >= 0)
		return true;
	for (size_t k = 0; k < keptRows.size(); ++k)
	{
		const int32 hitIdx = keptRows[k];
		const RowNow& kept = rowNow[static_cast<size_t>(hitIdx)];
		TextIndex keptStart = kInvalidTextIndex;
		if (!RowStartNow(db, kept, keptStart))
			continue;		// its thread went with an object another replace deleted
		const TextIndex keptEnd = keptStart + kept.length;
		KFCResultModel::SetHitRange(chapterIdx, hitIdx, kept.story, keptStart, keptEnd);
		KFCSearchEngine::RereadRowText(chapterIdx, hitIdx, UIDRef(db, kept.story), keptStart, keptEnd);
	}
	return true;
}

// DOES THE CHAPTER STILL HOLD WHAT ITS TICKED ROWS DESCRIBE? (the author's design)
// The resolve pass asks it of every chapter before a character is written. Every ticked row carries the
// place its match stands at - where the search found it, carried past every change KFC has made since -
// and this walks every story that holds one the way the writing walk will (WalkStoryReplacing: the same
// story scope, the same options, from the top of the story), asking THREE QUESTIONS of every ticked row
// (below): is its story at the version KFC left it at, does the row still read as it was found
// (KFCSearchEngine::RowReadsAsFound - the jump's own test), and does a match of the walk still stand at
// its start and length? A row that fails any of them, or one the walk never reaches, means the document is
// not the one the results describe, and the caller stops the whole run before a character is written. It
// writes nothing, marks no row and moves no bar.
//
// BY PLACE AND TEXT, NOT BY COUNT (the author's call: safe, even if slower). Lining the Nth match of the
// walk up with the row numbered N, reading no text, is wrong on both sides of what the writing walk does -
// it writes only a match standing at a ticked row's thread, offset and length (RowOfMatchAnyOrder):
//   - a ticked row whose text was changed to other text the query also matches (GREP \d+ over "123"
//     edited to "456"), with as many matches before it as there were, passes the count and is WRITTEN -
//     text nobody ticked. The text is asked, and the run stops;
//   - a match added or removed somewhere before a ticked row, the row itself untouched, fails the count
//     and stops the run for nothing.
//   ! PLACE AND TEXT ALONE ARE NOT ENOUGH EITHER: "where it was" is an index, and an edit KFC did not see
//     (above all Ctrl+Z) can leave that index on ANOTHER occurrence of the same text, which place and text
//     pass. Hence the story's version and the row's line, asked first (below) - so any edit in the story
//     stops the run. (An Undo or a Redo of a write of KFC's own is SEEN - KFCUndoFollow puts the rows and
//     the versions back with it; the doors below are for every other edit.)
// Numbers would also have to be kept up: every row menu's Replace and Reject would walk the whole chapter
// again to number the rows afresh - with no tab stated, so a Reject made with the dialog on another tab
// would number the rows by that tab's matches.
//
// The END of a match is compared too, not left to the writing walk (which asks the length, and would
// leave a row whose match ran longer or shorter as missing after writing the others): such a row stops
// the run before anything is written.
//
// STORY BY STORY, AS THE WRITING WALK GOES - not the whole chapter (QueryDocumentWalkerScope, every story,
// the ones with nothing ticked in them too): the walk that writes goes one ticked story at a time
// (QueryStoryWalkerScope). A row is found by its place, not by how many matches came before it, so the
// stories with no ticked row have nothing to say; and asking with the writing walk's own scope means what
// is checked is what will be met.
//
// A walk that cannot START (no database, no options, no walker, no scope) answers false - nothing was
// compared, and the writing walk meets the same failure and stops the whole run ("the text walker could
// not be started"). A walk that starts and then breaks off answers true through the ticked rows it never
// reached: this run will not write to positions it could not check. The alert then says the results
// changed, which is the safe answer if not the precise one.
//
// (The chapter walk this grew out of - ReplaceInChapter, which wrote match by match, with a verify-only
// mode - is in git history: 8bf650d and before.)
//
// THREE QUESTIONS, EACH ONE ENOUGH TO STOP THE RUN (the author's call: "safety first"). "Place and text"
// alone passes a row an Undo or the user's typing has left on ANOTHER occurrence of its own text -
// "catcatcatcat", row 1 replaced from its menu, Ctrl+Z, rows 2 and 3 ticked: both stood on the next "cat",
// and were written there (measured, before these doors). (That Ctrl+Z - of a write of KFC's own - is
// followed, KFCUndoFollow: the rows go back to where the text is, and rows 2 and 3 are written where they
// stand. The three questions stand for every other edit.) So, for every row:
//   1. its STORY is at the version KFC last recorded for it (StoryAsKFCLeftIt - ITextModel::GetChangeCount,
//      which Undo moves back): any change KFC did not make, anywhere in the story, stops the run;
//   2. the row still READS as it was found - the whole match AND the line around it
//      (KFCSearchEngine::RowReadsAsFound), for a version that has come back to the same number;
//   3. the walk meets a match with the row's start AND its length.
// `onlyRows` = the rows a row menu's Replace is about to write (ReplaceRowsNow asks the same three, not the
// match's hash alone); nil = Change Checked's work, every ticked row.
// `outWhy` (the author's call) = which of the three stopped it, for a row menu's refusal to say: question 1
// is about the whole story - an edit anywhere in it - and is not "the text of this row has changed".
// Change Checked's alert says one sentence for all three and passes nil.
enum MovedWhy
{
	kMovedRow,		// questions 2 and 3, or a row whose identity cannot be read: the row itself
	kMovedStory		// question 1: the row's story was changed by something other than KFC
};

bool ChapterMovedUnderRows(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	const std::set<int32>* onlyRows = nil, MovedWhy* outWhy = nil)
{
	if (outWhy != nil)
		*outWhy = kMovedRow;
	// The DATABASE first, the way the search asks it (KFCSearchEngine's CollectHitsInDoc). It is NOT a
	// liveness test - a UIDRef carries the IDataBase* itself, and "is this document still open?" has one
	// honest answer in KFC (KFCBookScope::IsDocStillOpen); what this catches is a UIDRef that never had one.
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// Where the rows stand, story by story: story -> ((start, end) -> how many of them stand there - a
	// zero-width GREP match and a wider one can share a start). A row whose story is gone or has moved
	// without KFC, whose text or line is not the one that was ticked, or whose identity cannot be read is a
	// change like any other - "cannot tell" is not good enough to rewrite the user's text on.
	std::map<UID, std::map<std::pair<TextIndex, TextIndex>, int32> > waiting;
	std::map<UID, int32> waitingInStory;
	std::map<UID, bool> storyAsLeft;		// the version question, asked once per story
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		// A report's own rows keep their ticks, and a report can run a Change Checked on its taken-back
		// rows: work is what ReplaceInChapterOneByOne writes, by the same rule.
		const bool asked = (onlyRows != nil) ? (onlyRows->count(i) != 0) : KFCResultModel::IsHitCheckedWork(chapterIdx, i);
		if (!asked)
			continue;
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, i, story, start, end, hash)
			|| story == kInvalidUID || !db->IsValidUID(story))
		{
			KFC_DIAG_LOG("DOOR moved chapter=%d row=%d - its identity cannot be read (story=%u)", chapterIdx, i, story.Get());
			return true;
		}
		std::map<UID, bool>::const_iterator known = storyAsLeft.find(story);
		const bool asLeft = (known != storyAsLeft.end())
			? known->second : (storyAsLeft[story] = StoryAsKFCLeftIt(chapterIdx, db, story));
		if (!asLeft)
		{
			KFC_DIAG_LOG("DOOR moved chapter=%d row=%d - its story %u is not at the version on record", chapterIdx, i, story.Get());
			if (outWhy != nil)
				*outWhy = kMovedStory;
			return true;
		}
		if (!KFCSearchEngine::RowReadsAsFound(chapterIdx, i, db))
		{
			KFC_DIAG_LOG("DOOR moved chapter=%d row=%d - its text at [%d,%d) is not what was found", chapterIdx, i,
				(int)start, (int)end);
			return true;
		}
		++waiting[story][std::make_pair(start, end)];
		++waitingInStory[story];
	}
	if (waiting.empty())
		return false;

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(registry != nil
		? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss) : nil);
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	if (opts == nil || walker == nil)
		return false;

	for (std::map<UID, std::map<std::pair<TextIndex, TextIndex>, int32> >::iterator s = waiting.begin(); s != waiting.end(); ++s)
	{
		// A fresh walk from the top of the story - the scope and the starting point the writing walk has.
		if (walker->IsWalking())
			walker->Halt();
		InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(UIDRef(db, s->first), scopeOptions));
		InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
		if (scope == nil || client == nil)
			return false;
		walker->Initialize(client, scope, opts, nil);

		// EVERY EXIT PAST Initialize HALTS. A walker left walking is not merely untidy: it comes
		// from the session's service registry, and the next caller that guards its own Initialize with
		// IsWalking CONTINUES it - which is what InDesign's own Find/Change does (SnpFindAndReplace.cpp:772).
		// The shape is Adobe's (SpellPreviousObserver.cpp:200-201: ask IsWalking, then Halt). The refusal in
		// the walk below is the likeliest exit of all: editing the document between the search and the
		// replace is the ordinary way a run ends here.
		InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
		if (selUtils == nil)
		{
			if (walker->IsWalking())
				walker->Halt();
			return false;
		}

		int32& left = waitingInStory[s->first];
		{
			// Required critical section around text-walker selection changes, held for the whole story - the
			// same deliberate departure from Adobe's examples that KFCSearchEngine explains: its contents are
			// the keyboard-focus hand-off (spellpanel names it in SpellCheckWalker.cpp:85), so entering it per
			// match would run that dance once per match.
			const TextWalkerSelections_CriticalSection criticalSection(selUtils);

			// How the walk moves forward is the walker's business alone: each find advances it to the next
			// match, as the official loop runs it (SnpFindAndReplace), and the walk ends when every ticked row
			// of the story has been met, when the find says there is nothing more - or when it breaks off
			// (kFailure), which leaves the rows it never reached waiting.
			while (left > 0)
			{
				UIDRef story;
				TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
				if (RunWalkerCmd(kFindTextCmdBoss, walker, story, start, end) != IFindChangeService::kSuccess)
					break;
				// the row's start AND its length (see above)
				const std::map<std::pair<TextIndex, TextIndex>, int32>::iterator here = s->second.find(std::make_pair(start, end));
				if (story.GetUID() == s->first && here != s->second.end() && here->second > 0)
				{
					--here->second;
					--left;
				}
			}
		}
		if (walker->IsWalking())
			walker->Halt();
		// A ticked row the walk never reached is as much a change as one that moved: the match the results
		// promise is not there to be replaced.
		if (left > 0)
			return true;
	}
	return false;
}

// The status line, from the counters alone. It reads RunTotals and nothing else - no document, no model,
// no session state - and it is worth keeping that way: the wording of a run is then decided in one place,
// from one set of facts.
//
// Every checked hit that was not replaced is named here rather than being allowed to make the total
// quietly come up short. That rule is what most of these branches exist for.
void BuildSummary(const RunTotals& t, PMString& outSummary)
{
	// A cancel is absolute. The whole run is one command sequence and a cancel aborts it, so the text
	// goes back and the panel goes back with it - there is nothing left to account for. (Nothing reaches
	// the disk, so no chapter is beyond taking back.)
	if (t.cancelled)
	{
		// NOT "cancelled" WHEN NOBODY CANCELLED. The sequence was about to be committed with the error state
		// standing, which would have rolled every replacement back on its own - the run says so, instead of
		// reporting a count of replacements that no longer exist. InDesign's own wording for the failure is
		// quoted when there is one: it is the only description of what went wrong that anybody has.
		if (t.stoppedByFailure)
		{
			// Said as a failure, not as a cancel (the author's call): nothing was retried.
			outSummary.Append("Replace stopped - ");
			outSummary.Append(t.errorText.IsEmpty() ? PMString("it could not go on") : t.errorText);
			outSummary.Append(", so nothing was changed.");
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
	// "in N chapter(s)" only for a book: not "in 1 chapter(s)" for a document, a word the search's own
	// summary never uses there (KFCSearchEngine: "hit(s)." alone).
	if (KFCResultModel::IsFromBook())
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
	// ...and a pending change accepted first IS written, replace or no replace: its chapter is kept open and unsaved
	// with it (ReplaceChecked, tookReplacement - the regression case ca-refused-after-accept).
	if (t.replaced > 0 || t.acceptedFirst > 0)
		outSummary.Append(" Not saved - check them and save yourself.");

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
	// THE WORD IS "missing", AND IT LEADS WITH A "!". The ROWS are marked "missing"
	// (KFCResultModel::BuildHitLocator), so the sentence uses the same word - one outcome under two
	// names leaves the reader matching a sentence against rows that do not use its word. The "!" is
	// there because this is the one line in the summary the user has to act on: some of what they
	// ticked was not written (the user's request).
	//
	// Not "no longer where the search left it": no test compares each match against its row while
	// writing (the SAME-OCCURRENCE TEST note, above). What is said is the plainer fact: the chapter
	// was walked again and those matches did not come up.
	if (t.missing > 0)
	{
		outSummary.Append(" ! ");
		outSummary.AppendNumber(t.missing);
		outSummary.Append(" hit(s) missing - not found when the chapter was searched again.");
	}

	// ...and WHEN THAT WAS NOT THE DOCUMENT'S FAULT, immediately after it. A chapter
	// whose re-walk broke off with an error leaves all its unreached rows in the count above, and
	// that sentence then says their text was not found - a statement about the user's document that
	// is simply untrue here. So the chapter is named, right where the reader is still looking at the
	// number it is explaining.
	//
	// It takes a distinction to see this at all: the walker commands answer with a whole enum
	// (IFindChangeService.h:43 - five members, of which the four in RunWalkerCmd's table can arrive
	// here), and reading every one that is not kSuccess as "nothing here" hides it. Adobe's own loop
	// keeps them apart - SnpFindAndReplace.cpp:796 returns the result untouched and :644-648 turns
	// kFailure, and only kFailure, into a failure.
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

	// The documents' pending tracked changes, accepted before the first write (the author's call) -
	// said, because they no longer show in the Track Changes panel.
	if (t.acceptedFirst > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.acceptedFirst);
		outSummary.Append(" pending tracked change(s) accepted first.");
	}

	// Ticked rows in the endnotes, left alone on purpose: a match ends an endnote, and
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

	// A chapter that WAS written to and has no window. Everything this run does is left for the user
	// to look at and save, so a replacement they cannot see is the one outcome that leaves them with no
	// move to make - so the window's answer is read and said (see KFCBookScope::ShowChapterWindow), not
	// thrown away. Rare: it means kOpenLayoutCmdBoss itself would not run.
	if (t.chaptersNoWindow > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersNoWindow);
		outSummary.Append(" chapter(s) were changed but could not be shown - open them from the book panel to save them.");
	}
	// ...and the documents left hidden ON PURPOSE (Search: = All Documents): the user kept them
	// without a window - perhaps because they are heavy - so the replace did not open one; the line says so,
	// since what was written there is not on screen. A click on one of their rows opens a window.
	if (t.chaptersLeftHidden > 0)
	{
		outSummary.Append(" ");
		outSummary.AppendNumber(t.chaptersLeftHidden);
		outSummary.Append(" document(s) without a window were changed - still hidden.");
	}

	// WHAT REJECT CHANGE CAN TAKE BACK, LAST. Not "Replaced with Track Changes on - Reject Change ... takes it
	// back." for every run that wrote: some rows have nothing recorded - a replace that changed no character
	// (Change Format with an empty Change To - measured, case xs5-b2-format-only-cc) or one in a footnote.
	// Those rows say "no track"; this says how many, and does not promise what cannot be done.
	if (t.replaced > 0)
	{
		if (t.unrecorded < t.replaced)
		{
			outSummary.Append(" Replaced with Track Changes on - Reject Change on a row's right-click menu takes it back.");
			if (t.unrecorded > 0)
			{
				outSummary.Append(" Not the ");
				outSummary.AppendNumber(t.unrecorded);
				outSummary.Append(" marked \"no track\" - inside a footnote, or no character changed (formatting only): Track Changes records nothing there.");
			}
		}
		else
			outSummary.Append(" Track Changes recorded nothing for them (inside a footnote, or no character changed - formatting only) - Reject Change cannot take them back; Edit > Undo can.");
	}

	// (No "this chapter was rolled back" sentence: the whole run is a single sequence, so no chapter goes
	// back on its own - an error standing at the end takes every chapter with it, and the cancel sentence
	// above covers that.)
}

// HAND BACK EVERY CHAPTER THIS RUN OPENED AND THEN LEFT NOTHING IN.
//
// The run keeps the chapters a replacement landed in - they hold the user's unsaved work and each
// one is given a window to be seen and saved through. A chapter that got NONE is a different thing
// entirely: it holds nothing of this run, nobody can see it (it was opened windowless), and while it
// stands it keeps its .indd locked. There is no way for the user to close it either - a document
// with no window is not in the Window menu.
//
// It happens whenever every checked hit in a chapter came back locked, missing, refused or left at an
// endnote's end, or when the walk ended before any of its rows came up.
//
// THE MODIFIED FLAG GOES BACK FIRST, AND THAT ORDER IS THE POINT. A walk can leave a
// database marked modified without changing a character - that is exactly why the SEARCH wraps its
// own walk in IDataBase::SaveRestoreModifiedState, which the replace deliberately does not (it is
// meant to leave documents changed). So a chapter nothing was written to can still come out of the
// walk looking unsaved, and ReleaseHeldDoc REFUSES to close a chapter with unsaved work in it - it
// would be thrown away silently. Left as it is, such a chapter can never be handed back again by
// anything, and stays open and locked for the rest of the session.
//
// Only for chapters that were CLEAN when this run found them. One the user had already edited is
// still edited, and saying otherwise would put their work at risk of a silent close.
//
// The same two steps the CANCEL path takes, for the same reasons; it takes them over every chapter,
// because an abort leaves nothing in any of them; this is the same half for the run that goes THROUGH.
// outUnclosed: the chapters this pass could NOT hand back, by name, for the summary
// (KFCBookScope::AppendUnclosedNote) - the release's answer is not discarded. A chapter left behind
// is windowless - nothing on screen shows it, nothing the user can do closes it, and it holds its
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
			// pointer is dangling, and SetModified through it is undefined behaviour - so no deref
			// before the liveness test, as the release two steps down asks it.
			if (KFCBookScope::IsDocStillOpen(pending[pi].docRef))
			{
				IDataBase* const chapterDB = pending[pi].docRef.GetDataBase();
				if (chapterDB != nil)
					chapterDB->SetModified(kFalse);
			}
		}

		// On the spot: a scheduled close does not run until the current tick has unwound, and this run
		// IS that tick - the chapters would stay open, and stay locked, until it was over. The same
		// call and the same reasoning as the search's per-chapter release (KFCSearchEngine::
		// SearchBook). Safe here: the command sequence is closed, the walk has halted, and a chapter
		// the user opened themselves is not on the held list and passes through untouched.
		if (!KFCBookScope::HandBackHeldDocNow(pending[pi].docRef))
		{
			// Named the way the summary's other chapter mentions are (firstSkipped and
			// firstWalkFailed): the model's display name for the row. Still valid here - this runs before
			// KeepCheckedRows reshapes the chapter list.
			PMString name;
			int32 hitCount = 0;
			KFCResultModel::GetChapterDisplay(pending[pi].chapterIdx, name, hitCount);
			name.SetTranslatable(kFalse);
			outUnclosed.push_back(name);
		}
	}
}

// THE WAY OUT WHEN THE RUN IS STOPPED BEFORE IT HAS WRITTEN ANYTHING.
//
// Both exits from the resolve pass come through here: the progress bar's Cancel while the chapters
// are being opened, and the verify walk finding a chapter moved under its rows (TellResultsWentStale
// has said so by then). Neither has written a character, so there is nothing to roll back: no command
// sequence to abort - it opens after that pass - and no row backup to drop, which is taken after it
// as well. What IS owed is the chapters the pass opened: each holds its .indd locked and has no
// window it could be closed through.
//
// The wording is BuildSummary's, not this function's: a cancel means one thing to the user wherever
// it was pressed, and a second spelling of it here is how the two come to differ.
//
// resultsAreStale says WHICH of the two it was, in terms of what it means rather than where it came
// from: the document no longer holds the text these rows describe, and a list that no longer
// describes anything must not be left on screen offering to replace things (the author's call). A
// Cancel on the bar is the other case entirely - nothing has changed, the rows are
// still true, and throwing them away would lose a search the user may have waited a long time for.
// @return 0, always - the number of replacements the run made.
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

	// BACK TO BEFORE THE SEARCH. The rows were found in text that is not there any more,
	// which is the whole reason the run stopped. Leaving them up invites a second run against a list
	// that describes the old text - the same reasoning, and the same call, as RefuseChangedQuery above:
	// DropResults, the rows with the book and the format that describe them.
	//
	// The PANEL is not touched from here: the caller redraws the tree and writes this summary to the
	// status line (KFCActionComponent::DoAction), and the illustration follows the model by itself
	// - Clear() puts HasRun back down, so KFCPanelIcon::Choose returns the picture the panel had
	// before anything was run.
	if (resultsAreStale)
	{
		KFCSearchEngine::DropResults();
		// SHORT ENOUGH TO BE READ WHOLE. This follows BuildSummary's own cancel sentence, so what the
		// panel draws is both of them - and at the panel's floor a longer pair came to 128 characters
		// against a box that held about 88: the line stopped at "the document has" and the reader never
		// saw that they were being asked to search again (measured on the running panel). The block is
		// four lines (KFCPanelMetrics) and this says the same thing in half the room. WHY the document no
		// longer matches is the alert's job (TellResultsWentStale), which has a whole dialog to say it in.
		outSummary.Append(" Results cleared - the document changed. Search again.");
	}

	// The chapters that would not close, at the end, the way every other exit says it.
	KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
	return 0;
}

// THE MATCHES THESE ROWS DESCRIBE ARE NOT WHERE THEY WERE.
//
// The run writes the match standing at each ticked row's place, so a ticked match that has moved,
// gone, or changed its text since would hand a replacement to an occurrence the user never ticked.
// The verify walk in the resolve pass has just found exactly that, and the run is over before it
// started - this only tells them.
//
// IT IS A STATEMENT, NOT A QUESTION (the author's decision). There is no "carry on anyway" - no "the
// text has been edited - OK / Cancel?": a work list that has come apart cannot be replaced safely
// whatever anyone answers.
//
// Shown with the progress bar DOWN - the resolve pass closes its own bar before this goes up, so a
// modal alert never stands over a modal bar - and before the command sequence opens, so not one
// character has been written when it appears.
//
// With userInteractionLevel at NEVER_INTERACT the alert is never drawn (CAlert::SetShowAlerts,
// CAlert.h:218-226: "If showAlert is kFalse, no alerts will be displayed") and the run stops all the
// same: the caller returns through StopBeforeAnythingIsWritten either way, and a scripted run reads
// what happened off the panel's status line (KIDMCP's inspect_ui).
// @param chapterIdx the chapter the mismatch was found in, or -1.
void TellResultsWentStale(int32 chapterIdx)
{
	// A DOCUMENT-scope run's "chapters" are open documents (one, or each open one with Search: = All
	// Documents), not a book's - the book wording would name a chapter of a book that is not there.
	PMString msg;
	if (chapterIdx < 0 || !KFCResultModel::IsFromBook())
		msg = KFCLoc::Text(kKFCStaleResultsDocKey, KFCJa::kStaleResultsDoc);
	else
	{
		// The model's display name for the chapter row, marked untranslatable BEFORE it goes into a
		// translated string - the order every other line of this plug-in's prompts takes.
		PMString name;
		int32 chapterHits = 0;
		KFCResultModel::GetChapterDisplay(chapterIdx, name, chapterHits);
		name.SetTranslatable(kFalse);
		msg = KFCLoc::Text(kKFCStaleResultsOneKey, KFCJa::kStaleResultsOne);
		::ReplaceStringParameters(&msg, name);
	}

	// ONE sentence (the author's call) - no second line saying "please search again". The alert's job
	// is to state that the replace stopped and that nothing was written; what to do next is on the
	// status line the panel is left showing.
	// Finished text: every part was translated as it was taken, so the alert must not look the
	// result up again (CAlert translates the message it is handed).
	msg.SetTranslatable(kFalse);

	// WarningAlert - THE OFFICIAL CALL FOR EXACTLY THIS: a message and a warning icon.
	// CAlert.h:75-79 ("Modal alert, displaying text plus eWarningIcon"). It is what the product uses
	// to make this kind of statement: spellpanel says "Change All cannot run" this way
	// (SpellChangeAllObserver.cpp:292), and so do SpellSkipObserver.cpp:519,543 and
	// PrivateSpellingUtils.cpp:816.
	//
	// NOT A ONE-BUTTON ModalAlert. Its one argument for it - ModalAlert is the shape
	// IAlertHandler::HandleAlert takes, so KT's alert recorder could read the alert back - does not hold
	// (measured BOTH ways): it does not reach app.ktLastAlert either way, because under NEVER_INTERACT
	// CAlert answers without asking the handler at all. (KT's recorder was armed and answering -
	// app.ktAlertAnswer read back "1" - so the suppression is CAlert's own, not KT's.) With nothing to
	// gain, the all-powerful call would only carry a default-button argument that means nothing with one
	// button, and a return value with nothing to decide.
	//
	// So an automated run cannot read this alert THROUGH KT. It can read it through Win32, which is how it
	// is tested (measured - the eye is not the only way): left at INTERACT_WITH_ALL the alert is drawn for
	// real, and a real CAlert is a #32770 whose body sits in a hidden Edit child that answers WM_GETTEXT.
	// The wording, the warning icon and the untouched document are all checked by
	// work/kbs-selftest/run-stale-alert-shot.ps1.
	//
	// SHOWN BY THE UI HALF (the model/UI split). Not a link matter - CAlert is PUBLIC_DECL, and Adobe's
	// own model plug-in incopyfileactions calls it - but KFC counts an alert among the dialogs the guide
	// lists as user-interface components (vol1-06, "UI component content"): the author's call
	// (IKFCUIServices.h says more). The wording and the moment stay here, and
	// IKFCUIServices::WarningAlert is this same CAlert::WarningAlert.
	// No UI (a background thread, InDesign Server) = no alert, and the run stops all the same - the
	// NEVER_INTERACT case described above.
	InterfacePtr<IKFCUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	if (ui != nil)
		ui->WarningAlert(msg);
}

// What CompareQueryWithSearch found - RefuseChangedQuery and QueryUnchangedSinceSearch share it.
enum QueryCompared
{
	kQueryUnchanged,	// the settings still describe the search (and its tab has been stated)
	kQueryOtherTab,		// the dialog is on another tab
	kQueryTabNotStated,	// the tab could not be stated (CommitSearchMode)
	kQueryChanged		// the query, a switch or the Find Format has changed
};

// Do the current Find/Change settings still describe the search the results came from? The QUESTION
// alone: what a refusal does with the answer - clearing the results above all - is
// RefuseChangedQuery's. The tab IS stated on the way (see below), which every walk after it needs.
QueryCompared CompareQueryWithSearch()
{
	// ----- (1) the TAB the results were searched with -----
	// Every chapter is RE-WALKED, and a walk in another mode returns another set of matches - so the
	// rows would be lined up with occurrences the user never saw. Asked first because it is the most
	// specific thing that can be said, and because it does NOT cost the results: a tab is one click to
	// put back. Read through the call the results were stamped with (KFCSearchEngine::CurrentSearchMode).
	// (Not "is the searched tab one this panel walks at all": the tab on the results is one SearchBook
	// could state, and only the four text tabs can be, so it would never answer no.)
	const int32 searchedMode = KFCResultModel::GetSearchMode();
	const int32 currentMode = KFCSearchEngine::CurrentSearchMode();
	if (searchedMode >= 0 && currentMode >= 0 && currentMode != searchedMode)
		return kQueryOtherTab;

	// State the tab, exactly as the search does. A walk runs in the mode last COMMITTED, not the one
	// IFindChangeOptions reports, so without this a replace runs as plain Text whatever tab is on
	// screen - which is how a Glyph-tab search came to be overwritten with the TEXT tab's change
	// string. See KFCSearchEngine::CommitSearchMode.
	//
	// It writes back the value it just read, so calling it here AND from a caller that asks this
	// question twice changes nothing: it states the mode, it does not choose one.
	//
	// IT HAS TO HAPPEN BEFORE THE COMPARISON BELOW, and that is not a detail. The search
	// records its signature after committing the mode too (KFCSearchEngine::SearchBook). Committing a
	// mode is a declaration and, as CommitSearchMode's own comment says, "there is no promise anywhere
	// that it leaves that mode's other settings untouched" - so a signature taken on one side of that
	// command and compared against one taken on the other side could differ with nothing having
	// changed, and would then refuse every replace there is. Both are taken on the same side.
	//
	// Deliberately OUTSIDE any command sequence: this processes a command of its own, and a
	// session-setting command inside the replace's sequence would become part of its undo step. Every
	// caller of this function asks before opening one.
	//
	// AND ITS ANSWER DECIDES WHETHER THERE IS A REPLACE AT ALL. A tab that could not be stated leaves the
	// engine in whatever mode was committed last, and a replace walked in the wrong mode writes the wrong tab's
	// change side over what this tab found - which is the very fault CommitSearchMode was written for (the
	// user's report). So the failure is not swallowed. The results are NOT cleared: they still describe the
	// search that found them, so RefuseChangedQuery refuses the way every other door in this plug-in does and
	// leaves the panel be.
	if (!KFCSearchEngine::CommitSearchMode())
		return kQueryTabNotStated;

	// ----- (2) the QUERY itself, which the tab does not cover -----
	//
	// The tab test catches "the user clicked another tab". It does not catch the far more ordinary
	// thing: the find string retyped, Case Sensitive ticked, a paragraph style put in Find Format,
	// Include Footnotes turned off - each of which leaves the tab alone and changes WHICH matches a
	// walk returns.
	//
	// That matters because the replace writes the matches its walk meets at the rows' places, and the
	// walker is handed the LIVE IFindChangeOptions (ITextWalker.h:58-61) - so what it walks by is
	// whatever the dialog holds RIGHT NOW, not what it held when these rows were found. A different
	// query can make a match of its own stand where a row does.
	//
	// THIS IS THE DOOR THAT KEEPS THE RUN FROM HAPPENING AT ALL - not one that explains a run that came
	// back all-missing: no per-hit same-occurrence test stands in the writing walk (the note on it, above;
	// a trusted-story fast path that skipped it let a retyped query write the change string over
	// occurrences the user had never seen, while the panel reported the ORIGINAL rows as replaced). The
	// verify walk behind it walks under the LIVE query too (ChapterMovedUnderRows), so a ticked row the
	// changed query no longer matches does stop the run - but a changed query that still matches every
	// ticked row passes it, and the run would then write, in silence, under a query the panel was not
	// searched with. Widen this test rather than lean on anything downstream.
	//
	// IT IS TWO QUESTIONS, NOT ONE. The signature covers the tab, the query and every switch, and it
	// COUNTS the Find Format conditions without saying what they are set to. The values are compared
	// separately, by the attribute list itself (KFCSearchEngine::FindFormatHasChanged) - not
	// fingerprinted by hand into the signature, where an attribute that answers none of the nine
	// interfaces such a probe uses goes in as its class alone: "Find Format: size 14 pt" edited to
	// "size 20 pt" then leaves the signature IDENTICAL and walks straight through this door.
	//
	// An EMPTY signature on either side means it could not be described, not that it differs -
	// results from before this field existed answer empty too - so only two known-different
	// signatures refuse. The format test follows the same rule and answers false when it cannot
	// tell.
	const PMString walkedSignature = KFCResultModel::GetWalkSignature();
	PMString currentSignature;
	KFCSearchEngine::BuildWalkSignature(currentSignature);
	const bool signatureDiffers = !walkedSignature.IsEmpty() && !currentSignature.IsEmpty()
		&& walkedSignature != currentSignature;
	return (signatureDiffers || KFCSearchEngine::FindFormatHasChanged()) ? kQueryChanged : kQueryUnchanged;
}

// THE WRITING LOOP OF A RUN, INSIDE A COMMAND SEQUENCE ITS CALLER HOLDS OPEN (2026-10-04, the query run -
// docs/superpowers/specs/2026-10-04-kfc-query-sequence-design.md). Every chapter of `pending` the caller has
// open, one match at a time (ReplaceInChapterOneByOne), the bar moved and Cancel asked between chapters;
// fills `totals` and each chapter's tookReplacement. It opens no document and closes none: an open processed
// between two chapters' replacements throws away the undo history of the chapters already written (measured -
// the resolve pass in ReplaceChecked). Change Checked and the query run (KFCQuerySequence) both write through it.
// `changeAllScope` (the query run only - WriteCheckedInHeldSequence): -1 = one match at a time (Change Checked);
// otherwise the run's Search: scope, and each story is written with Change All (ReplaceInChapterOneByOne).
void WriteCheckedChapters(std::vector<PendingChapter>& pending, const WalkerScopeOptions& scopeOptions,
	KFCProgressBar& progressBar, RunTotals& totals, int32 changeAllScope = -1)
{
	// How many hits the bar has behind it. The bar is sized in hits, so each chapter starts where
	// the last one ended and moves the bar itself as it goes. progressReported is how far it has
	// actually been advanced - what lets KFCAdvanceProgress swallow an advance too small to repaint
	// for - so it has to be carried along rather than recomputed.
	int32 progressBase = 0;
	int32 progressReported = 0;

	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		// The count the run was sized with, carried on the chapter - see PendingChapter::
		// checkedCount for why it is still right when this chapter's turn comes. (A recount taken
		// after the chapter ran would find zero, its hits being marked replaced by then.)
		const int32 chapterChecked = pending[pi].checkedCount;

		PMString chapterName;
		int32 chapterHits = 0;
		KFCResultModel::GetChapterDisplay(pending[pi].chapterIdx, chapterName, chapterHits);
		chapterName.SetTranslatable(kFalse);
		KFCSetChapterTask(progressBar, "Chapter", pi, pending.size(), chapterName);
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Cancel is asked here, after the bar has been moved - inside the chapter and just above
		// (KFCAdvanceProgress; which call on the bar takes the click is not measured, see there).
		//
		// kFALSE: do NOT raise the global error state. The error state is not the mechanism - it does not
		// carry a rollback across a book's several documents (measured, see the sequence above) - and the
		// sequence is aborted outright. Worse than not needed: it would still be standing while
		// AbortCommandSequence runs, and would then fail whatever the application does next.
		//
		// Cancelling means ONE thing (the author's call): the whole run is undone. Keeping the
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

		// EVERY REPLACE IS TRACKED. The story's own Track Changes setting is handed back as it was found
		// (TrackingScope), and every replace's records are signed "KohakuFindChange" at the row's time
		// (KFCTrackChange.h). A replaced row's two texts - before and after, Hit::originalText /
		// replacedText - are taken by the walk that writes it (WalkStoryReplacing).
		// ONE MATCH AT A TIME, STORY BY STORY (the author's call) - not Change All over whole stories.
		int32 replaced = 0, missing = 0, locked = 0, refused = 0, endnoteLeft = 0, unrecorded = 0, acceptedFirst = 0;
		bool walkFailed = false, runCancelled = false, runFailed = false;
		PMString whyNot;
		const bool wrote = ReplaceInChapterOneByOne(ci, docRef, scopeOptions,
			&progressBar, progressBase, progressReported, replaced, missing, locked, refused, endnoteLeft,
			unrecorded, acceptedFirst, walkFailed, runCancelled, runFailed, whyNot, nil, changeAllScope);
		totals.endnoteLeft += endnoteLeft;
		totals.unrecorded += unrecorded;
		totals.acceptedFirst += acceptedFirst;
		if (runCancelled)
		{
			totals.cancelled = true;
			break;
		}
		// Could not go on (runFailed), or could not start (!wrote: the document, the options or a row
		// could not be read): the abort below takes back what earlier chapters wrote.
		if (runFailed || !wrote)
		{
			totals.stoppedByFailure = true;
			totals.errorText = whyNot;
			totals.errorText.SetTranslatable(kFalse);
			totals.cancelled = true;	// everything a cancel does, this needs too
			break;
		}
		progressBase += chapterChecked;
		// Land exactly on the chapter boundary: a chapter that finished early (nothing left to
		// line up) must still hand the bar on at the right place.
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
		totals.replaced += replaced;
		totals.missing += missing;
		totals.locked += locked;
		totals.refused += refused;
		if (replaced > 0)
			++totals.chaptersTouched;

		// Did anything land here? What decides whether this chapter is kept open for the user -
		// and given a window, in the loop past the sequence - or handed straight back at the end
		// of the run (HandBackChaptersWithNothingInThem).
		// A PENDING CHANGE ACCEPTED FIRST COUNTS: it is a change this run made and keeps, whether or not a
		// replace came after it (every row of the chapter refused, or its walk broke off before the first).
		// Handed back as "nothing in it", such a chapter would have its unsaved mark put back over the
		// accept - a document of the user's would then say nothing had changed, a held chapter would be
		// closed with the accept thrown away - while the status line said it was accepted (measured with
		// the test build's fault switch replace-refuse - the regression case ca-refused-after-accept).
		pending[pi].tookReplacement = (replaced > 0 || acceptedFirst > 0);
		if (walkFailed)
		{
			// The walk STARTED here and broke off. Its unreached rows are already in `missing` above
			// - there is nothing truer to put on them one at a time - so this names the chapter to
			// say that the shortfall is a search error, not the document having moved on.
			NoteChapter(ci, totals.chaptersWalkFailed, totals.firstWalkFailed, totals.haveFirstWalkFailed);
		}
	}

	// ASK ONCE MORE, now that the loop is over.
	//
	// The test inside the loop sits at the TOP of each pass, so it only ever sees a cancel that
	// arrived while an EARLIER chapter was running. A cancel pressed during the LAST chapter has no
	// next pass to be noticed in, and the run would finish as though nothing had been asked - which is
	// exactly what "cancelling works in the first document but not across documents" was (the user's
	// report; a one-chapter book could never be cancelled at all).
	//
	// The work is already done by this point, so this changes nothing about what was written - but it
	// is what decides between committing that work and throwing it away, which is the whole promise
	// of the button.
	if (!totals.cancelled && progressBar.WasCancelled(kFalse))
		totals.cancelled = true;
}

} // anonymous namespace

bool KFCReplaceEngine::RefuseChangedQuery(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);
	switch (CompareQueryWithSearch())
	{
		case kQueryOtherTab:
			outSummary.Append("The Find/Change dialog is on a different tab than when this search ran. Search again.");
			return true;
		case kQueryTabNotStated:
			outSummary.Append("The Find/Change tab could not be set - nothing was changed. Try reopening Edit > Find/Change.");
			return true;
		case kQueryChanged:
			// AND THE RESULTS GO (the author's call). Every other refusal in this plug-in leaves the panel
			// exactly as it found it - the rule the search's own refusals stand above their Clear() to obey -
			// and this one is deliberately the exception. The
			// difference is what the rows would go on saying: a run turned away for any other reason leaves a
			// list that is still TRUE, while these rows describe a query the dialog no longer holds, so
			// leaving them up invites the user to try again against a list that cannot be acted on. Clearing
			// says plainly that the search has to be re-run, which is the only way forward anyway.
			//
			// The caller redraws the tree, so nothing here touches the panel. The remembered format goes
			// with the rows (DropResults), and here it matters more than anywhere: what was just compared
			// against it is gone, so leaving it standing would have the NEXT question about a changed query
			// answered from a search whose rows no longer exist.
			KFCSearchEngine::DropResults();
			outSummary.Append("The Find/Change query has changed since this search - the results have been cleared. Search again.");
			return true;
		default:
			return false;
	}
}

bool KFCReplaceEngine::QueryUnchangedSinceSearch()
{
	// NO SEARCH, NOTHING TO COMPARE WITH. A list rebuilt from the records has no tab and no signature on
	// it, which CompareQueryWithSearch reads as "cannot tell" and so as UNCHANGED - and the jump would then
	// walk the story under whatever Find/Change holds now to look for a row (RelocateStaleRow). Those rows
	// were never a query's matches.
	if (KFCResultModel::IsFromRecords())
		return false;
	return CompareQueryWithSearch() == kQueryUnchanged;
}

int32 KFCReplaceEngine::ReplaceChecked(PMString& outSummary)
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
	// ...and the same door for anything ELSE of ours - a search. Asked separately so each keeps the
	// message that is actually true. It matters more here than anywhere: this run holds an open
	// command sequence, and a search cancelled underneath it hands back the very chapters being
	// written to (see KFCRunGuard).
	if (KFCRunGuard::IsAnyRunning())
	{
		outSummary.Append(KFCRunGuard::BusyMessage());
		return 0;
	}
	const ReplacingFlagGuard replacingGuard;
	KFC_DIAG_PHASE(phaseReplace, "replace-checked");	// a test build's timer (KFCDiag.h)
	KFC_DIAG_COMMANDS(commandsReplace, "replace-checked");	// ...and its command count (KFCDiagCommands.h)

	const int32 chapterCount = KFCResultModel::GetChapterCount();
	if (chapterCount <= 0)
	{
		outSummary.Append("No results to replace - run a search first.");
		return 0;
	}
	// A LIST REBUILT FROM THE RECORDS OFFERS NO REPLACE (the author's call). No row
	// there carries a box and the menu greys this command; this is the same door for a caller that never
	// went through the menu. Asked ahead of the report's door below, which would call it a report.
	if (KFCResultModel::IsFromRecords())
	{
		outSummary.Append("Change Checked: these rows were rebuilt from Track Changes - search again to replace.");
		return 0;
	}
	// The panel is a report of what the LAST replace did, not a work list. Asked first, because a
	// report can still hold checked rows: the ones the run never reached keep their check so the
	// report can account for them, and they are exactly what a second run would go after - matches
	// the user has no box to select or clear anywhere on screen.
	//
	// The menu greys the command out for the same reason (KFCActionComponent::UpdateActionStates).
	// This is the same door on the far side of it, for a caller that never went through the menu -
	// a script invoking the action reaches this function whatever state the menu is in.
	if (KFCResultModel::NoRowHasCheckBox())		// a report - with no row taken back in it
	{
		outSummary.Append("This is the last replace's report - search again to replace more.");
		return 0;
	}
	if (KFCResultModel::GetCheckedCount() <= 0)
	{
		outSummary.Append("Nothing checked.");
		return 0;
	}
	// (Results that stop short of the scope are not refused: one match at a time writes the ticked rows
	//  and nothing else. A chapter whose search broke off is caught by the verify pass: a ticked row its
	//  walk cannot reach stops the run.)

	// Forward, as the search was: the verify pass follows the session's direction. Outside the run's
	// sequence, and put back as the function ends. Only past the refusals above, which ask nothing of
	// the session - a run turned away there touches no setting (the search's own scope stands the same
	// way).
	KFCForwardSearchScope forward;

	// Do the Find/Change settings still describe the search these rows came from - the tab, and the
	// query with every option that decides the match set? On the searched tab this also STATES it
	// (CommitSearchMode), which the walk below needs.
	//
	// Asked here only, on a Change Checked - not by the action as well: a refusal that clears the
	// results needs the tree rebuilt, which the caller does after this returns.
	if (KFCReplaceEngine::RefuseChangedQuery(outSummary))
		return 0;

	// ...and what the replace will WRITE, for the tabs whose change side is not a string: the Glyph
	// tab's Change To glyph, the Transliterate tab's change character type. Stated only here, never
	// on the search path, so a search can never leave a change-side value set behind the user's
	// back. An EMPTY Change To box is stated too, not refused - it means "delete every match", the
	// same as an empty change string on the Text tab. false means only that the Find/Change settings
	// could not be read at all. Also outside the sequence, and before it opens, so nothing has been
	// written yet.
	if (!KFCSearchEngine::CommitReplaceSide())
	{
		outSummary.Append("Find/Change settings are unavailable - nothing was changed.");
		return 0;
	}

	// The five scope switches, read ONCE for the whole run and handed to every chapter's walk -
	// the same single reading the search takes above its own chapter loop, and for the same two
	// reasons: the answer cannot differ between chapters (nothing can touch the dialog while the
	// run's modal bar is up), and this walk HAS to run with exactly the switches the search ran
	// with or it meets other matches than the rows list.
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);

	// The whole account of this run - every counter the summary reads, in one structure.
	//
	// totals.cancelled is set when the user stops the run from the progress bar. That means the
	// documents are given back as they were: the command sequence rolls the text back, and the
	// result model is rolled back with it, so the panel returns to being the search's results.
	// Nothing is half done.
	RunTotals totals;

	// ONE WAY THROUGH, and it opens every chapter it has work in. A run that does not save
	// has no choice but to hold every chapter it touches until the end (the header says why); what the
	// user gets in exchange is that Cancel is absolute: one sequence around everything, so stopping the
	// run puts the whole book back.

	// How much work the run has to get through - what the bar is sized with. Counted BEFORE anything
	// is opened, because the bar has to be up while the chapters are being opened: that is the slow
	// part when the user has closed the windows the search was holding.
	//
	// HITS, not chapters. A chapter is a coarse unit: one chapter of 5000 hits and one of 3 would each
	// be a single step, and the bar would stand still through the long one. Hits are the work.
	//
	// The run's chapter list is born HERE as well: a chapter with nothing checked is not in it -
	// not opened, not walked, not a step of the bar - and every chapter that is in it carries its
	// count with it (PendingChapter::checkedCount, which is also where the reasons the number
	// stays right are), counted once, by the model (GetChapterCheckedCount - the question the
	// tree's chapter row asks).
	std::vector<PendingChapter> pending;
	int32 totalCheckedHits = 0;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		const int32 checkedHere = KFCResultModel::GetChapterCheckedCount(ci);
		if (checkedHere <= 0)
			continue;		// nothing selected here - do not even open this chapter
		PendingChapter chapter;
		chapter.chapterIdx = ci;
		chapter.checkedCount = checkedHere;
		pending.push_back(chapter);
		totalCheckedHits += checkedHere;
	}

	// THE TITLE BOTH OF THE RUN'S BARS CARRY. They are two objects - see the resolve pass below - but
	// one run, and one title is what says so on screen.
	//
	// A bar is shown for BOTH scopes (the user's request), matching the search - not for a book only: a
	// one-document replace is a single chapter, but the bar is sized in HITS, and a single document
	// with thousands of them takes just as long and would have no way to be stopped.
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
	// exactly what was measured to throw away the undo history of the chapters
	// already done - that is why ShowChapterWindow was moved out to the far end of this function.
	// The reopen belongs on the same side of the fence for the same reason. Doing it here also
	// means a chapter that cannot be opened at all is counted before anything has been written,
	// instead of interrupting a run that is already half committed.
	//
	// A chapter that cannot be opened STAYS in the list, unopened: it is a step the bar was sized
	// with, and dropping it would leave the bar short of its own total, stopping at 4 of 5 with
	// nothing left to do.
	//
	// THIS PASS HAS A BAR OF ITS OWN, IN A SCOPE OF ITS OWN. One bar over both passes cannot work: a
	// chapter found to be EDITED puts a modal alert up between them, the bar is modal too, and the two
	// must not stand at once (the author's decision). Ending the scope takes the bar down, the
	// questions are asked, and the replace's own bar goes up after them.
	//
	// Sized in CHAPTERS here - this pass opens documents, it does not replace anything - where the
	// replace's bar below is sized in hits. So the opening, the slow part of a run, has something on
	// screen to show for it.
	bool cancelledWhileOpening = false;
	// Set by the verify walk inside the pass - see it for what it asks and why it asks it there.
	bool changedSinceSearch = false;
	int32 changedChapterIdx = -1;
	{
	// THIS PASS HANDS THE ERROR STATE BACK THE WAY IT FOUND IT. It opens documents and walks every
	// chapter to verify the ticked positions - work that can raise the
	// global error state without any of it being a failure of this run. What makes that matter is
	// what comes after: the command sequence below decides between committing and rolling back by
	// READING that state (the abort at the end of this function), so an error left standing by this
	// pass would throw away a replace that went through perfectly.
	//
	// PRESERVE, THEN CLEAR - and it is Adobe's own base class that spells the pair out.
	// CDialogObserver.cpp:392-394 ("GlobalErrorStatePreserver followed by setting global error to
	// success"); also CPathCreationTracker.cpp:666. The contract is ErrorUtils.h:115-117 - the
	// constructor saves the caller's error state and the destructor puts it back, here at the closing
	// brace of this pass. The clear is the other half: the pass should not START on an error either,
	// since a standing one fails whatever it does next (opening a chapter, above all).
	//
	// Clearing WITHOUT preserving would decide for this function's CALLER that their error state did
	// not matter, and guard only the one path that reaches the sequence. This guards every way out of
	// the pass, including the two that return through
	// StopBeforeAnythingIsWritten (which keeps a clear of its own: it closes documents after this scope
	// has already ended).
	GlobalErrorStatePreserver passErrorState;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);

	KFCProgressBar openBar(progressTitle, 0, static_cast<int32>(pending.size()), kTrue, kTrue);
	openBar.DisableChildProgressBars(kTrue);

	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		// The bar carries the chapters ALREADY opened, so it is set at the top of the pass rather
		// than the bottom: three of the paths below leave by continue, and a step written after them
		// would be the step they skip.
		openBar.SetPosition(static_cast<int32>(pi));

		// Cancel, asked between chapters exactly as the replace loop below asks it, right after the bar
		// has been moved (which call on the bar takes the click is not measured - KFCAdvanceProgress).
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
		if (!KFCResultModel::GetChapterLocation(ci, docRef, file))
		{
			// Unreachable while GetChapterLocation only fails on an out-of-range index and ci comes
			// straight from GetChapterCount - but if the two ever come apart, this chapter's checked
			// rows must not drop out of the run in silence: nothing on their locators would say a
			// word, and the replaced total would come up short with nothing to explain it, which is
			// the one thing the summary's rule exists to prevent. Counted and named exactly like a
			// chapter that would not open - to the user, that is what it is.
			NoteChapter(ci, totals.chaptersSkipped, totals.firstSkipped, totals.haveFirstSkipped);
			continue;		// unopened - the loop below only counts it past
		}

		// Resolve the chapter to a LIVE document, BY FILE - never by asking whether the docRef the
		// search left is still open, which can answer YES about a DIFFERENT document (measured: every
		// row of a chapter walked over its neighbour and came back 'missing'). See
		// KFCBookScope::ReachChapterDoc, which the jump and a row menu's Replace ask too.
		if (!KFCBookScope::ReachChapterDoc(file, docRef))
		{
			// Moved, deleted, or in use: counted and named in the summary; it stays in the list
			// unopened so the bar still takes its step for it.
			NoteChapter(ci, totals.chaptersSkipped, totals.firstSkipped, totals.haveFirstSkipped);
			continue;
		}
		KFCResultModel::RebindChapterDoc(ci, docRef);

		chapter.docRef = docRef;
		chapter.opened = true;
		// Read BEFORE anything is written to it - that is the whole point of the record.
		{
			IDataBase* const chapterDB = docRef.GetDataBase();
			chapter.wasModified = (chapterDB != nil) && (chapterDB->IsModified() != kFalse);
		}

		// AND THE CHAPTER IS WALKED AGAIN, WRITING NOTHING, TO SEE WHETHER THE RESULTS STILL DESCRIBE
		// IT. Every ticked row carries the place its match stands at; if one of them
		// no longer holds the ticked text, or no match of this walk begins there, the document has
		// moved under the results and the replace would hand a replacement to an occurrence the user
		// never ticked. Whatever the cause - text edited, a story added or deleted, a layer hidden or
		// locked, a condition switched - the effect is the same and it is the effect that is measured
		// (the author's design - not an enumeration of the causes).
		//
		// HERE, and here is the only place it can be. The chapter has just become a
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
		// (What "moved" covers - a walk that cannot start, one that breaks off - is written at
		// ChapterMovedUnderRows.)
		if (ChapterMovedUnderRows(ci, docRef, scopeOptions))
		{
			changedSinceSearch = true;
			changedChapterIdx = ci;
			break;
		}
		// ...and which of the chapter's row stories are as KFC left them: those, and only those, take
		// their new version if the run goes through.
		{
			std::set<UID> rowStories;
			KFCResultModel::GetChapterStories(ci, rowStories);
			StoriesAsKFCLeftThem(ci, docRef.GetDataBase(), rowStories, chapter.storiesAsLeft);
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

	// THE DOCUMENT MOVED UNDER THE RESULTS - SO THE RUN DOES NOT START.
	//
	// The verify walk in the pass above found a ticked hit that no longer begins where the search
	// found it. It is NOT put to the user as a question: a work list that has come apart cannot be
	// replaced safely whatever they answer, so the run is simply stopped and they are told (the
	// author's decision - no per-chapter "carry on / cancel?" prompt).
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

	// THE REPLACE'S OWN BAR. A second object, not the one the pass above used - see the note there for
	// why the run cannot carry a single bar across both.
	//
	// WHAT "STOPPED" MEANS HERE, EXACTLY. WasCancelled is read between chapters, between the STORIES of a
	// chapter (ReplaceInChapterOneByOne - the author's call, story by story), and once more when the loop
	// ends. A Cancel pressed during a story is heard when that story is done; the whole sequence is then
	// aborted and every character put back (BeginAbortableCmdSeq / AbortCommandSequence - nothing is left
	// on the Undo stack either). (Asking inside a story's walk as well - InDesign's own Change All asks
	// every step - was tried; the author chose story by story, for the search too.)
	// DisableChildProgressBars keeps anything the replacements raise from putting up bars of their
	// own (the chapter opens the other bar covers are the same case, and it says so there).
	//
	// SIZED IN HITS, not chapters, and moved by ReplaceInChapterOneByOne as it goes, a replace at a
	// time (progressBase below).
	// The walker will not report progress for us: ITextWalkerProgressMonitor is only a place to PARK
	// a bar - the client's own OnNextPosition is what calls SetPosition on it, and the stock
	// kFindChangeClientBoss does not (measured: registered fine, 5270 replacements, zero
	// calls). spellpanel gets its moving bar because it walks with a client it wrote itself. So KFC
	// counts its own work, which it can do better than the walker anyway: the number of checked hits
	// is known before the run starts.
	KFCProgressBar progressBar(progressTitle, 0, totalCheckedHits, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	// Remember every row the run is about to change. A cancel rolls the TEXT back through the
	// sequence below; this is what lets the PANEL be rolled back with it, so the two cannot end up
	// telling different stories. Exactly one of RollBackRows / ForgetRowBackup follows.
	// AND THE RUN IS RECORDED FOR THE PANEL'S FOLLOWING OF UNDO. The recorder starts that backup (no
	// BeginRowBackup of its own here), reads the version of every row story of the
	// chapters the resolve pass opened, and copies the WHOLE result set: the run turns the list into its
	// report (KeepCheckedRows), so an Undo of it has to put the work list back whole. Kept at the end.
	std::vector<int32> openedChapters;
	for (size_t pi = 0; pi < pending.size(); ++pi)
		if (pending[pi].opened)
			openedChapters.push_back(pending[pi].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(openedChapters, true);

	{
	// ONE sequence around EVERY chapter, so a book-wide replace is a SINGLE undo step.
	//
	// Measured on the running application: with a sequence per chapter, undoing in one
	// document also removed the step from the OTHER chapters' histories - but did NOT revert their
	// text. Those chapters were left replaced with nothing left to undo them with, which is a
	// silent, unrecoverable loss of the user's content. Wrapping the whole run in one sequence is
	// what makes a single Ctrl+Z put all of it back, whichever chapter happens to be in front.
	//
	// Nothing inside opens a sequence of its own: every replace, signature, acceptance and Track Changes
	// switch of every chapter goes straight into this one. A per-chapter sequence nested inside it is
	// measured to be harmful: closing the inner sequence settles its chapter, and the outer abort then
	// has nothing left to undo for it - a cancelled book replace left every finished chapter replaced
	// while the panel said nothing had changed (measured twice, once through the error state and once
	// through AbortCommandSequence).
	//
	// ABORTABLE, and that is the whole point of choosing this kind over a plain SequencePtr.
	//
	// A regular sequence decides between commit and rollback ONLY by looking at the global error
	// code as it ends (ICommandSequence.h:145-147). Raising the error state at a cancel
	// (WasCancelled(kTrue)) and expecting the sequence to put the book back does not work - measured with
	// the error code printed at both points: it was raised at the cancel (2) and STILL raised when the
	// sequence closed (2) - and 1622 replacements stayed in the document anyway, while the panel said
	// "nothing was changed". The error-code route does not carry a rollback across the several documents
	// a book replace touches; the header only ever promises "the database", singular.
	//
	// So the cancel is stated outright instead of being implied: AbortCommandSequence below.
	// That is what Adobe's own Change All does (spellpanel/SpellReplaceWalker.cpp:896-902), and the
	// header points at this class for exactly this case. It costs performance - the header says to
	// use it only where necessary - which is why it is here and not around every chapter.
	// THE ERROR STATE THIS SEQUENCE READS AT ITS END IS ITS OWN - see the resolve pass, which hands its
	// own back before this line is reached (GlobalErrorStatePreserver, up there).
	// How this sequence ends is decided by reading the global error code (the abort at the bottom of
	// this function): anything standing there is taken as a failure nothing reported, and the whole
	// run is rolled back and the user told why. That reading is only honest about failures from
	// INSIDE the sequence, and the pass above - which opens documents and walks every chapter - is
	// outside it. Nothing between the two runs a command: a progress bar is built and the rows are
	// backed up, and neither touches the error state.
	//
	// EXCEPT ONE COMMAND, ON PURPOSE: THE WRITING WALK'S DIRECTION. A GREP query
	// holding ^ is written backward (WriteBackward, and the note above ReplaceInChapterOneByOne). The
	// session's direction is turned here - after the verify pass, which re-walks the search forward, and
	// before the sequence, so neither the Undo step nor an abort carries the switch - and put back when
	// this function returns, after the sequence has ended. A switch that fails clears its own error.
	const KFCBackwardSearchScope writeDirection(WriteBackward());
	IAbortableCmdSeq* seq = nil;
	// (Fault switch perf-plain-seq, a test build's only - KFCDiag.h: a REGULAR sequence instead, to measure what the
	// abortable one costs the walk - ICommandSequence.h: abortable sequences "incur a heavy performance overhead".
	// Its cancel rolls back through the error state, which the note above measured NOT to carry across a book's
	// documents: one throwaway document only.)
	ICommandSequence* plainSeqForTest = nil;
#ifdef KFC_DIAG
	if (KFC_DIAG_FAULT("perf-plain-seq"))
		plainSeqForTest = CmdUtils::BeginCommandSequence("KFC Replace (test: plain sequence)");
	else
#endif
		seq = CmdUtils::BeginAbortableCmdSeq("KFC Replace");

	// NO SEQUENCE, NO RUN. BeginAbortableCmdSeq answers nil on error (CmdUtils.h:135),
	// and everything this function promises rests on the sequence it hands back: one Ctrl+Z for the
	// whole book, and a Cancel that puts every chapter back. Without it the replacements would go in
	// as loose commands - undoable one at a time at best - and, worse, a CANCEL would find no
	// sequence to abort while still reporting "nothing was changed" over a book that had been
	// rewritten. Refusing before a character is written is the only honest answer.
	if (seq == nil && plainSeqForTest == nil)
	{
		// Nothing was written, so there is nothing to roll back and nothing to keep: drop the row
		// backup, and hand back every chapter the resolve pass opened - none of them took a
		// replacement, which is exactly what this hands back.
		KFCResultModel::ForgetRowBackup();
		std::vector<PMString> unclosed;
		HandBackChaptersWithNothingInThem(pending, unclosed);
		outSummary.Append("Could not start an undoable step - nothing was changed.");
		KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
		return 0;
	}
	// NAMED (the author's call): "Replace" / Japanese UI KFCJa::kReplaceStep. Not left unnamed for
	// InDesign to word: an unnamed step is worded by its LAST command, which is not the replace - the
	// story's tracking switch put back (TrackingScope) is last (and a switch of the user name once made
	// Edit > Undo read "Undo Set User Name" - measured, case undo-then-reject).
	// (The string passed to BeginAbortableCmdSeq is TRACKING DATA, not that name - CmdUtils.h:134 - so
	// it names this caller in a lost-sequence report and nowhere else.)
	if (seq != nil)
		seq->SetName(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep));
	else
		plainSeqForTest->SetName(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep));
	KFCTrackChange::BeginSignedRun();	// the run's time - every row of every chapter is stamped from it

	{
		KFC_DIAG_PHASE(phaseWrite, "replace-write");	// a test build's timer: the writing alone, the checks before it apart
		WriteCheckedChapters(pending, scopeOptions, progressBar, totals);
	}

	// The sequence ends HERE, and HOW it ends is the cancel. Aborting is a statement - "undo
	// everything this sequence did" - where ending it only offers the changes up and lets the error
	// state decide. Either way the sequence must not be touched again afterwards
	// (ICommandSequence.h:153).
	// A COMMAND CAN REPORT SUCCESS AND STILL LEAVE THE ERROR STATE UP. Ending a sequence in that
	// state rolls back everything it did - silently - while the summary would go on saying
	// "N replaced" (the measurement above, seen from the other direction).
	// Every failure the run KNOWS about clears the state where it happens (RunWalkerCmd's two
	// doors, and the cancel's own clear below), so anything still standing at this line is a
	// failure that nothing reported.
	//
	// WHICH IS WHY IT IS READ, NOT CLEARED (the author's call): clearing commits whatever
	// half-written state that unreported failure left behind, and says nothing about it - trading
	// a rollback the user can SEE for a corruption they cannot. A standing error means
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

	// THE MARK: in every chapter this run changed (tookReplacement), inside this sequence, so the run's Undo
	// and Redo are heard on its documents (KFCUndoFollow::MarkWrite). After the error state was read above,
	// and a mark that fails puts it back as it found it: the run is never decided by it.
	if (!totals.cancelled)
		for (size_t pi = 0; pi < pending.size(); ++pi)
			if (pending[pi].tookReplacement)
				KFCUndoFollow::MarkWrite(pending[pi].docRef.GetDataBase());

	if (plainSeqForTest != nil)
	{
		// (the test build's plain sequence - see where it began: a cancel rolls back through the error state)
		if (totals.cancelled)
			ErrorUtils::PMSetGlobalErrorCode(kCancel);
		CmdUtils::EndCommandSequence(plainSeqForTest);
		plainSeqForTest = nil;
	}
	else if (totals.cancelled)
		CmdUtils::AbortCommandSequence(seq);
	else
		CmdUtils::EndCommandSequence(seq);
	seq = nil;

	}	// end of the block the sequence lived in

	if (totals.cancelled)
	{
		// The abort has just rolled the text back to where the run found it. The panel recorded
		// those replacements as they happened, so it has to shed them too - otherwise it would show
		// replaced rows sitting over text that is once again the original.
		//
		// What is left is exactly what the search produced: a work list with its checks intact,
		// ready to be run again. No window is opened either - nothing was changed to look at.
		KFCResultModel::RollBackRows();

		// Belt and braces: the cancel no longer raises the error state, but a command that failed
		// inside the run might have left one standing, and it must not outlive this function.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		// Put the "unsaved" flags back. AbortCommandSequence restores the TEXT but leaves every
		// database it touched marked modified, so a cancelled run would leave the chapters asking to
		// be saved with nothing in them to save. (Undo does clear the flag - the application handles
		// that itself - which is how the difference shows up.)
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
			// undefined behaviour.
			if (!KFCBookScope::IsDocStillOpen(pending[pi].docRef))
				continue;
			IDataBase* const chapterDB = pending[pi].docRef.GetDataBase();
			if (chapterDB != nil)
				chapterDB->SetModified(kFalse);
		}

		// Hand the chapters back. Nothing of this run is left in them - the abort took
		// every character back and the flags above went with it - so a chapter this plug-in opened
		// has no reason to stay, and each one holds its .indd locked while it does.
		//
		// The search does this on ITS cancel, through ReleaseSearchedBook (which closes the chapters
		// AND forgets the book). A replace asks for
		// the closing half alone: its results stay on the panel, so the book they came from must
		// still be remembered - that is what the book watcher reads to know when to drop them.
		//
		// AFTER the flags above, never before: ReleaseHeldDocs refuses to close a chapter with
		// unsaved work in it, and until they are back every chapter this run touched still says it
		// has some. A chapter the USER had already edited keeps its flag, so it stays open and stays
		// held - which is what should happen to somebody else's unsaved work.
		KFCBookScope::ReleaseHeldDocs();

		// The panel is back to being the search's results, so there is no report to turn it into -
		// KeepCheckedRows is deliberately NOT called on this exit. The wording is left to
		// BuildSummary, which is the only place that knows what a cancel means.
		BuildSummary(totals, outSummary);
		return 0;
	}
	KFCResultModel::ForgetRowBackup();

	// THE STORIES WRITTEN TO TAKE THEIR NEW VERSION. In each
	// chapter this run changed (tookReplacement), the row stories that were as KFC had left them when the run began -
	// so the next Replace, Reject or Change Checked finds them at the version it knows. A chapter nothing
	// landed in keeps what it had: it is handed back below, closed with its file as it was. BEFORE
	// KeepCheckedRows, which renumbers the chapters these indices name.
	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		if (pending[pi].tookReplacement)
			NoteStoryVersions(pending[pi].chapterIdx, pending[pi].docRef.GetDataBase(), pending[pi].storiesAsLeft);
	}

	// NOTHING IS SAVED. Every chapter a replacement LANDED in stays open and unsaved, and the summary
	// tells the user to deal with it - saving is the only thing that would make such a chapter safe to
	// close, and there is no "save after replace" (the header says why).

	// Windows are opened AFTER the sequence, never from inside it - the other half of the pair the
	// resolve pass above makes: no document and no window is opened while the sequence is standing.
	// Measured: a kOpenLayoutCmdBoss processed between two chapters' replacements
	// discarded the undo history of the chapters already done - their text stayed replaced with
	// nothing left to undo it with. Opening the windows once everything is committed keeps that
	// command clear of the replacements.
	//
	// Every chapter this run changed gets one - PendingChapter::tookReplacement is the one
	// record of which those are: the change has to be visible, because it is the user who has to save
	// it.
	//
	// AND THE ANSWER IS READ. A chapter that was written to and could not be SHOWN is the one outcome
	// that leaves the user nothing to do - the run saves nothing, so what it wrote can only be dealt with
	// through a window. (ShowChapterWindow answers true for "it already had a window" too - the ordinary
	// case, not a failure.)
	//
	// EXCEPT A DOCUMENT THE USER KEEPS WITHOUT A WINDOW (the author's call). Search: =
	// All Documents lists documents opened without one, and the user may have hidden a heavy one on purpose:
	// it stays hidden and the summary counts it. The windows this gives are for the chapters KFC itself
	// opened and holds (a book's) - the ones nobody could reach otherwise.
	for (size_t pi = 0; pi < pending.size(); ++pi)
	{
		if (!pending[pi].tookReplacement)
			continue;
		if (!KFCBookScope::IsHeldDoc(pending[pi].docRef) && !KFCBookScope::HasWindow(pending[pi].docRef))
		{
			if (KFCBookScope::IsDocStillOpen(pending[pi].docRef))
				++totals.chaptersLeftHidden;
			continue;
		}
		if (!KFCBookScope::ShowChapterWindow(pending[pi].docRef))
			++totals.chaptersNoWindow;
	}

	// AND THE CHAPTERS THIS RUN LEFT NOTHING IN GO BACK. Everything above is about the
	// chapters that were CHANGED; a chapter this run opened and then wrote nothing to has no reason
	// to stay - it holds nothing of the run, has no window to be seen through, and locks its .indd
	// while it stands. See HandBackChaptersWithNothingInThem for the whole of why, including why
	// the modified flag has to go back first.
	//
	// AFTER the windows above, not before: the opens are the commands with a measured history of
	// disturbing undo, so they stay as close to the end of the sequence as they are.
	//
	// BEFORE KeepCheckedRows, which reshapes the chapter list the hand-back's names come from.
	std::vector<PMString> unclosed;
	HandBackChaptersWithNothingInThem(pending, unclosed);

	// The panel now becomes a REPORT of what the replace did: the rows it changed, and the rows it
	// was asked about and left alone, each saying why on its locator. The rows the user had
	// unchecked are dropped - they were never part of the request. A replace that was asked for
	// nothing at all leaves the results exactly as they were.
	KFCResultModel::KeepCheckedRows();
	// The report is what an Undo's work list is put back over, and a Redo puts back.
	recorder.Keep(KFCUndoFollow::kStepChangeChecked);

	BuildSummary(totals, outSummary);
	// The chapters the hand-back could not close, at the end of the line - it appends nothing in the
	// ordinary case.
	KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
	return totals.replaced;
}

bool KFCReplaceEngine::WriteCheckedInHeldSequence(const PMString& barTitle, WriteOutcome& out, int32 changeAllScope)
{
	out = WriteOutcome();
	if (!KFCSearchEngine::CommitReplaceSide())
	{
		out.failed = true;
		out.why = "the Find/Change settings could not be read";
		return false;
	}
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);

	// The run's chapter list, as ReplaceChecked builds it - every chapter with a checked row - but every one of
	// them must already be open: the query run holds its documents for the whole run.
	std::vector<PendingChapter> pending;
	int32 totalChecked = 0;
	const int32 chapterCount = KFCResultModel::GetChapterCount();
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		const int32 checkedHere = KFCResultModel::GetChapterCheckedCount(ci);
		if (checkedHere <= 0)
			continue;
		PendingChapter chapter;
		chapter.chapterIdx = ci;
		chapter.checkedCount = checkedHere;
		IDFile file;
		if (!KFCResultModel::GetChapterLocation(ci, chapter.docRef, file) || !KFCBookScope::IsDocStillOpen(chapter.docRef))
		{
			out.failed = true;
			out.why = "a document of the run was not open";
			return false;
		}
		chapter.opened = true;
		pending.push_back(chapter);
		totalChecked += checkedHere;
	}
	if (pending.empty())
		return true;

	const KFCBackwardSearchScope writeDirection(WriteBackward());
	KFCTrackChange::BeginSignedRun();
	RunTotals totals;
	{
		KFCProgressBar progressBar(barTitle, 0, totalChecked, kTrue, kTrue);
		progressBar.DisableChildProgressBars(kTrue);
		WriteCheckedChapters(pending, scopeOptions, progressBar, totals, changeAllScope);
	}
	out.replaced = totals.replaced;
	out.missing = totals.missing;
	out.locked = totals.locked;
	out.refused = totals.refused;
	out.endnoteLeft = totals.endnoteLeft;
	out.unrecorded = totals.unrecorded;
	out.acceptedFirst = totals.acceptedFirst;
	out.failed = totals.stoppedByFailure;
	out.cancelled = totals.cancelled && !totals.stoppedByFailure;
	if (out.failed)
		out.why = totals.errorText;
	for (size_t pi = 0; pi < pending.size(); ++pi)
		if (pending[pi].tookReplacement)
			out.touchedDocs.push_back(pending[pi].docRef);
	return !out.cancelled && !out.failed;
}

bool KFCReplaceEngine::IsReplacing()
{
	return gReplacing;
}

// ======================================================================================================
// REPLACE ONE ROW, FROM ITS RIGHT-CLICK MENU (the author's call). No prompt; the
// Track Changes note goes on the status line afterwards. The list stays a WORK LIST - unlike Change
// Checked, which turns it into a report - so the row shows its new text and every other row is carried
// to where its text now stands, ready for the next Replace or a Change Checked. One undo step
// ("Replace"). The same doors as Change Checked, asked for these rows: the query unchanged
// (RefuseChangedQuery), the verify walk's three questions (ChapterMovedUnderRows), and only the pending
// changes the rows sit in or next to accepted first. Several rows (a story's, a document's) go in
// together or not at all (the author's call), and a refusal says how many stopped them.
// ======================================================================================================

// THE OTHER ROWS FOLLOW A REJECT OR A REDO. Taking one row's replace back, or writing it again, changes
// that row's length, and every row after it in the same thread moves with the text - which the model's
// stored ranges do not: the jump would then compare the wrong characters ("missing"), and in a work list
// a Change Checked after it would refuse as stale. Each row is taken as
// "this far into this thread" BEFORE (SnapshotRows - RowNow, as the replace keeps them), carried past
// each change in text order (CarryPastChange, given where the change stands AFTER - the earlier changes
// of the same thread are already carried by then), and put back (WriteBackRows).
static void SnapshotRows(int32 chapterIdx, IDataBase* db, std::vector<RowNow>& out)
{
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	out.assign(static_cast<size_t>(hitCount > 0 ? hitCount : 0), RowNow());
	for (int32 i = 0; i < hitCount; ++i)
	{
		if (!RowStillStands(chapterIdx, i, db))
			continue;		// left as it is: known == false, never carried nor read back
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 hash = 0;
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, i, story, a, b, hash))
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

// What a row put back takes from where it stands now: its range only, or its range and its line and hash
// read again (RereadRowText). A reject changes text a row may show; an accept changes none (so Accept
// All does not read every row's line again, for text it has not touched).
enum RowWriteBack { kRangeOnly, kRangeAndText };

// `skip` = rows left as they are, indexed like `rows` (empty = none) - not a list searched once per row.
static void WriteBackRows(int32 chapterIdx, IDataBase* db, const std::vector<RowNow>& rows, const std::vector<bool>& skip,
	RowWriteBack what)
{
	for (size_t i = 0; i < rows.size(); ++i)
	{
		if (i < skip.size() && skip[i])
			continue;
		const RowNow& row = rows[i];
		TextIndex at = kInvalidTextIndex;
		if (!RowStartNow(db, row, at))
			continue;
		KFCResultModel::SetHitRange(chapterIdx, static_cast<int32>(i), row.story, at, at + row.length);
		if (what == kRangeAndText)
			KFCSearchEngine::RereadRowText(chapterIdx, static_cast<int32>(i), UIDRef(db, row.story), at, at + row.length);
	}
}

bool KFCReplaceEngine::CanReplaceHit(int32 chapterIdx, int32 hitIdx)
{
	bool checked = false, replaced = false, locked = false;
	// (not on a list rebuilt from the records - the author's call: to replace again, search again)
	return !gReplacing && !KFCResultModel::IsFromRecords()
		&& KFCResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked)
		&& !replaced && !locked
		&& KFCResultModel::IsWorkOutcome(KFCResultModel::GetHitOutcome(chapterIdx, hitIdx));
}

// A status line of ReplaceRowsNow begun with the name of the menu item that asked (the author's call):
// Replace Again (Current Find/Change Settings) writes through ReplaceRowsNow too, and its refusals must
// not read "Replace:".
static void StartStatus(PMString& out, bool again)
{
	out = again ? "Replace Again: " : "Replace: ";
}

// THE ROW MENUS' SEQUENCE - ONE UNDO STEP, ROLLED BACK WHOLE ON A FAILURE. A PLAIN
// sequence, as KCM's own reject is (KCMFacades.cpp, RejectImportChange): an abortable sequence, ended, was
// measured to take the undo step below it away. Its rollback is the SDK's own: raise the error state, end
// it, clear it (CmdUtils.h, SequenceContext) - EndPlainSequence. Begun with the name the Undo menu shows;
// nil = InDesign would not start one, and outStatus then says so after `what` ("Reject Change: " ...).
static ICommandSequence* BeginPlainSequence(const PMString& stepName, const char* what, PMString& outStatus)
{
	ICommandSequence* sequence = CmdUtils::BeginCommandSequence();
	if (sequence == nil)
	{
		outStatus = what;
		outStatus.Append("InDesign would not start a command sequence - nothing was changed.");
		return nil;
	}
	sequence->SetName(stepName);
	return sequence;
}

// A STEP ROLLED BACK LEAVES THE DOCUMENT AS IT FOUND IT - ITS "UNSAVED" FLAG TOO (the author's call). The
// rollback puts the TEXT back and leaves the database marked modified (measured: a saved document, a story's
// Replace rolled back all or none - case story-replace-endnote-end - came out modified=true with not a
// character changed, and asked to be saved on close). Change Checked's cancel puts the flag back
// (ReplaceChecked); this is the row menus' steps' line for it. `wasModified` = the database's flag read
// before the step began: one the user had already changed is left changed.
static void EndPlainSequence(ICommandSequence* sequence, bool ok, IDataBase* db, bool wasModified)
{
	if (!ok)
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
	CmdUtils::EndCommandSequence(sequence);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (!ok && !wasModified && db != nil)
		db->SetModified(kFalse);
}

// The flag EndPlainSequence puts back - read before the step begins.
static bool DocIsModified(IDataBase* db)
{
	return db != nil && db->IsModified() != kFalse;
}

// The rows of one chapter replaced now, in ONE undo step - the right-click Replace of a row (one row) or
// of a story (its ticked rows), and Replace Again (`again`, RedoRowsNowIn - the rows taken back). The
// callers have asked CanReplaceHit of every row.
static bool ReplaceRowsNow(int32 chapterIdx, const std::set<int32>& rowsToReplace, PMString& outStatus,
	const char* unit = "story", bool again = false)
{
	const int32 hitIdx = rowsToReplace.empty() ? -1 : *rowsToReplace.begin();	// the one row, for a row's Replace
	// (A list rebuilt from the records - Show Changes - never reaches here: all three callers take their rows
	// through CanReplaceHit, which says no there. No door of its own, which could not be reached.)
	// Forward, as the search was - outside the sequence below (the walk's direction for a GREP query
	// holding ^ is turned below, also outside it).
	KFCForwardSearchScope forward;
	// A changed query CLEARS the results (RefuseChangedQuery) - the action redraws the tree for it
	// (KFCActionComponent RedrawAfterRowMenu). The refusal says which of its three answers it was - not
	// "the query changed" in front of all three, the other tab and a tab that could not be stated included.
	PMString refusal;
	if (KFCReplaceEngine::RefuseChangedQuery(refusal))
	{
		StartStatus(outStatus, again);
		outStatus.Append(refusal);
		return false;
	}
	if (!KFCSearchEngine::CommitReplaceSide())
	{
		StartStatus(outStatus, again);
		outStatus.Append("the Change To in Find/Change could not be stated - nothing was changed.");
		return false;
	}
	// The chapter's document, by its file first (a UIDRef can outlive its document - see
	// KFCBookScope::ReachChapterDoc).
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file))
	{
		StartStatus(outStatus, again);
		outStatus.Append("the document of this row could not be found.");
		return false;
	}
	if (!KFCBookScope::ReachChapterDoc(file, docRef))
	{
		StartStatus(outStatus, again);
		outStatus.Append("the document of this row could not be opened.");
		return false;
	}
	KFCResultModel::RebindChapterDoc(chapterIdx, docRef);
	IDataBase* const db = docRef.GetDataBase();
	// Read before anything here touches it: a step rolled back puts it back (EndPlainSequence), and so does a
	// refusal below (ChapterAfter).
	const bool wasModified = DocIsModified(db);
	// WHAT THE CHAPTER IS LEFT AS - CHANGE CHECKED'S RULE (the author's calls).
	// ReopenChapterDoc opens a closed chapter windowless and holds it. WRITTEN: a chapter of ours gets a window,
	// so the replace can be seen and saved (ShowChapterWindow); a document that is not ours is left as it is -
	// one with a window has it, and one the user keeps WITHOUT one (Search: = All Documents) stays hidden, as
	// Change Checked leaves it (the author's call) and the status line says (the action's NoteNoWindow).
	// NOT WRITTEN (refused, rolled back): nothing of this is in the document, so a flag the check's walk raised is
	// put back on a document that was clean (any document), and a chapter of ours is then handed back, as
	// HandBackChaptersWithNothingInThem does - flag first, since a held chapter that says "unsaved" is not closed.
	// (Not a window for every write: ShowChapterWindow asks no IsHeldDoc, so a hidden document of the user's
	// would be shown - measured - and a chapter of ours would get one whether the write went through or not.)
	struct ChapterAfter
	{
		UIDRef doc;
		bool wasModified;
		bool wrote;
		ChapterAfter(const UIDRef& d, bool m) : doc(d), wasModified(m), wrote(false) {}
		~ChapterAfter()
		{
			if (wrote)
			{
				if (KFCBookScope::IsHeldDoc(doc))
					(void)KFCBookScope::ShowChapterWindow(doc);
				return;
			}
			if (!wasModified && KFCBookScope::IsDocStillOpen(doc))
			{
				IDataBase* const chapterDB = doc.GetDataBase();
				if (chapterDB != nil)
					chapterDB->SetModified(kFalse);
			}
			if (KFCBookScope::IsHeldDoc(doc))
				(void)KFCBookScope::HandBackHeldDocNow(doc);
		}
	} chapterAfter(docRef, wasModified);
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
	// CHANGE CHECKED'S OWN CHECK, OVER THESE ROWS. Each row's story at the version KFC last recorded, the
	// row still reading as it was found - match and line - and a match of the walk at its start and length
	// (ChapterMovedUnderRows). Not the match's hash alone: a row an Undo has left on the next occurrence of
	// its own text passes that, and would be written there.
	// Forward, like the search - the scope at the head of this function.
	MovedWhy movedWhy = kMovedRow;
	if (ChapterMovedUnderRows(chapterIdx, docRef, scopeOptions, &rowsToReplace, &movedWhy))
	{
		StartStatus(outStatus, again);
		const bool oneRow = (rowsToReplace.size() == 1);
		if (movedWhy == kMovedStory)
		{
			// THE STORY, NOT THE ROW (the author's call). An edit anywhere in the story - typing, Undo, the
			// Track Changes panel, a script - stops it (StoryAsKFCLeftIt), the row's own text untouched or not;
			// so the line names the story, not "the text of this row".
			if (oneRow)
				outStatus.Append("the story of this row");
			else if (PMString(unit) == PMString("document"))
				outStatus.Append("a story of this document");
			else
				outStatus.Append("this story");
			outStatus.Append(" has changed since the search (edited or undone somewhere in it, not by KohakuFindChange) - search again.");
			return false;
		}
		outStatus.Append("the text of ");
		outStatus.Append(oneRow ? "this row" : "a row of this ");
		if (!oneRow)
			outStatus.Append(unit);
		outStatus.Append(" has changed since the search (edited, or undone) - search again.");
		return false;
	}
	// The stories this writes to - every one at the version on record, as the check has just said - take
	// their new version once it has gone through (NoteStoryVersions, below).
	std::set<UID> writtenStories;
	for (std::set<int32>::const_iterator r = rowsToReplace.begin(); r != rowsToReplace.end(); ++r)
	{
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, *r, story, start, end, hash))
			writtenStories.insert(story);
	}

	int32 replaced = 0, missing = 0, locked = 0, refused = 0, endnoteLeft = 0, unrecorded = 0, accepted = 0;
	bool walkFailed = false, cancelled = false, failed = false;
	PMString whyNot;
	bool ok = false;
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO. The story versions are read and the row backup started
	// here, outside the sequence; kept below once the rows show the replace - a failure puts them back
	// (RollBackRows).
	KFCUndoFollow::StepRecorder recorder(std::vector<int32>(1, chapterIdx), false);
	{
		// BACKWARDS FOR THE WRITE ONLY. A query holding ^ is written backwards (WriteBackward), inside this
		// block and nowhere else - not to the end of the function, where a walk after the write would run
		// backwards with it (case caret-row-then-change).
		const KFCBackwardSearchScope writeDirection(WriteBackward());

		ICommandSequence* sequence = BeginPlainSequence(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep),
			again ? "Replace Again: " : "Replace: ", outStatus);
		if (sequence == nil)
			return false;
		// (the row backup was started by the recorder, above)
		KFCTrackChange::BeginSignedRun();	// the run's time - every row it writes is stamped from it
		int32 progressReported = 0;
		const bool wrote = ReplaceInChapterOneByOne(chapterIdx, docRef, scopeOptions, nil, 0, progressReported,
			replaced, missing, locked, refused, endnoteLeft, unrecorded, accepted, walkFailed, cancelled, failed, whyNot,
			&rowsToReplace);
		ok = wrote && !failed && !cancelled && replaced == static_cast<int32>(rowsToReplace.size());
		if (ok)
			KFCUndoFollow::MarkWrite(db);	// in this step, so its Undo / Redo is heard
		EndPlainSequence(sequence, ok, db, wasModified);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		StartStatus(outStatus, again);
		// The reason a row was not written, in that row's words, and how many rows it stopped.
		const char* why = nil;
		int32 stopping = 0;
		bool searchAgain = false;
		if (endnoteLeft > 0)
		{
			stopping = endnoteLeft;
			why = "the match ends an endnote, and InDesign's replace breaks an endnote there";
		}
		else if (locked > 0)
		{
			stopping = locked;
			why = "the match is locked now (a locked layer or story)";
		}
		else if (missing > 0)
		{
			stopping = missing;
			why = "the match was not found where the search found it";
			searchAgain = true;
		}
		else if (refused > 0)
		{
			stopping = refused;
			why = "InDesign's replace command would not run there";
		}
		if (why == nil)
		{
			outStatus.Append(whyNot.IsEmpty() ? PMString("it did not go through") : whyNot);
			outStatus.Append(" - nothing was changed.");
		}
		else if (rowsToReplace.size() == 1)
		{
			outStatus.Append(why);
			outStatus.Append(searchAgain ? " - search again." : " - left as it is.");
		}
		else
		{
			// SEVERAL ROWS GO IN TOGETHER OR NOT AT ALL, AND THE LINE SAYS SO (the author's call: keep it, say
			// it). One row that cannot be written takes the others back with it (`ok` above, the sequence rolled
			// back whole). That row's reason alone would read as if that row alone had been left (case
			// story-replace-endnote-end).
			outStatus.Append("nothing was replaced - in ");
			outStatus.AppendNumber(stopping);
			outStatus.Append(" of these ");
			outStatus.AppendNumber(static_cast<int32>(rowsToReplace.size()));
			outStatus.Append(again ? " row(s) " : " checked row(s) ");
			outStatus.Append(why);
			if (searchAgain)
				outStatus.Append(" - search again.");
			else if (again)
				outStatus.Append(".");
			else
				outStatus.Append(stopping == 1 ? ". Untick it and Replace again." : ". Untick them and Replace again.");
		}
		return false;
	}
	NoteStoryVersions(chapterIdx, db, writtenStories);	// the next Replace / Reject finds them as KFC left them
	// Kept AFTER the versions are noted - they are the "after" an Undo's "before" is put back over.
	recorder.Keep(again ? KFCUndoFollow::kStepReplaceAgain : KFCUndoFollow::kStepReplace);
	chapterAfter.wrote = true;		// written to: a chapter of ours has to be seen and saved (ChapterAfter)
	// (Each row's two texts - Hit::originalText / replacedText - are taken by the walk that writes it,
	//  WalkStoryReplacing.)
	// WHAT REJECT CHANGE CAN TAKE BACK (BuildSummary's rule). The rows Track Changes recorded nothing for
	// (`unrecorded`: a footnote's, or a replace that changed no character - Change Format with an empty
	// Change To) are said, not promised to Reject Change - no "with Track Changes on - Reject Change ...
	// takes it back" of them (measured, case xs5-b-format-only).
	const char* const untracked = "inside a footnote, or no character changed - formatting only";
	if (again)
	{
		// Replace Again's own wording - not "Redone N row(s)" written over this line by RedoRowsNowIn, which
		// would also drop the "accepted first" note below.
		outStatus = "Replaced ";
		outStatus.AppendNumber(replaced);
		outStatus.Append(" row(s) of this ");
		outStatus.Append(unit);
		outStatus.Append(" again with the current Find/Change settings");
		if (unrecorded == 0)
			outStatus.Append(" (Track Changes on).");
		else if (unrecorded < replaced)
		{
			outStatus.Append(" (Track Changes on - not for the ");
			outStatus.AppendNumber(unrecorded);
			outStatus.Append(" marked \"no track\": ");
			outStatus.Append(untracked);
			outStatus.Append(").");
		}
		else
		{
			outStatus.Append(" - Track Changes recorded nothing for them (");
			outStatus.Append(untracked);
			outStatus.Append("), so Reject Change cannot take them back.");
		}
	}
	else if (rowsToReplace.size() == 1)
		outStatus = KFCResultModel::GetHitInFootnote(chapterIdx, hitIdx)
			? "Replaced (inside a footnote - Track Changes records nothing there, so it cannot be taken back with Reject Change)."
			: KFCResultModel::GetHitTextUnchanged(chapterIdx, hitIdx)
			? "Replaced (no character changed - formatting only - so Track Changes records nothing for it and Reject Change cannot take it back)."
			: "Replaced with Track Changes on - Reject Change on the row's right-click menu takes it back.";
	else
	{
		outStatus = "Replaced ";
		outStatus.AppendNumber(replaced);
		outStatus.Append(" checked row(s) of this ");
		outStatus.Append(unit);
		if (unrecorded < replaced)
		{
			outStatus.Append(" with Track Changes on - Reject Change on a row's (or its story's) right-click menu takes it back.");
			if (unrecorded > 0)
			{
				outStatus.Append(" Not the ");
				outStatus.AppendNumber(unrecorded);
				outStatus.Append(" marked \"no track\" - ");
				outStatus.Append(untracked);
				outStatus.Append(": Track Changes records nothing there.");
			}
		}
		else
		{
			outStatus.Append(" - Track Changes recorded nothing for them (");
			outStatus.Append(untracked);
			outStatus.Append("), so Reject Change cannot take them back.");
		}
	}
	if (accepted > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(accepted);
		outStatus.Append(" pending tracked change(s) next to it accepted first.");
	}
	return true;
}

bool KFCReplaceEngine::ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
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
// Reject Change - see the header. One plain sequence (BeginPlainSequence says why plain).
// ======================================================================================================
// A STORY ROW'S OR A DOCUMENT ROW'S ROWS. The rows of a story (groupIdx >= 0) or of the whole document
// (groupIdx < 0) - one scope for every menu item of the two rows, not two functions each identical but
// for this.
static void ScopeRows(int32 chapterIdx, int32 groupIdx, std::vector<int32>& out)
{
	out.clear();
	if (groupIdx >= 0)
	{
		KFCResultModel::GetGroupHits(chapterIdx, groupIdx, out);
		return;
	}
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
		out.push_back(i);
}

// Which of the scope's rows each menu item acts on: Replace = the TICKED rows that can be replaced (the
// author's call: "only the ticked ones"); Reject Change = the replaced rows whose tracked change is still
// there (FindRowChangeForHit - which refuses a footnote's row, where nothing is recorded). True = at
// least one. `firstOnly` = stop at the first (the menu's greying asks only "is there one", on every
// right-click: matching every replaced row of a large story against its tracked changes takes seconds -
// measured).
static bool RowsToReplace(int32 chapterIdx, int32 groupIdx, std::set<int32>& out, bool firstOnly = false)
{
	out.clear();
	std::vector<int32> rows;
	ScopeRows(chapterIdx, groupIdx, rows);
	for (size_t k = 0; k < rows.size(); ++k)
	{
		// Change Checked's own rule for "ticked" (IsHitCheckedWork - ticked AND a box on screen): "checked"
		// alone would take a report's unseen ticks of a chapter that could not be opened.
		if (KFCResultModel::IsHitCheckedWork(chapterIdx, rows[k]) && KFCReplaceEngine::CanReplaceHit(chapterIdx, rows[k]))
		{
			out.insert(rows[k]);
			if (firstOnly)
				break;
		}
	}
	return !out.empty();
}

// Of these rows, the ones Reject Change - and Accept Change - act on: replaced, with their
// tracked change still there. Over a story, a document (RowsToReject) or a run (its rows, KFCResultModel::
// GetRunHits).
static bool RowsWithChangeOf(int32 chapterIdx, const std::vector<int32>& rows, std::vector<int32>& out,
	bool firstOnly)
{
	out.clear();
	for (size_t k = 0; k < rows.size(); ++k)
	{
		bool checked = false, replaced = false, locked = false;
		UIDRef storyRef;
		KFCTrackChange::Change change;
		if (KFCResultModel::GetHitFlags(chapterIdx, rows[k], checked, replaced, locked) && replaced
			&& KFCTrackChange::FindRowChangeForHit(chapterIdx, rows[k], storyRef, change))
		{
			out.push_back(rows[k]);
			if (firstOnly)
				break;
		}
	}
	return !out.empty();
}

// WHY A ROW'S CHANGE CANNOT BE FOUND, WHEN THE REASON IS A HIDDEN CONDITION (the author's call). Text put under a
// condition that is then hidden moves - with its tracked insertion - out of the main text
// (KFCTrackChange::RowChangeIsHidden), so Reject Change and Accept Change refuse it until the condition is shown -
// and say so, not "no tracked change is left" (case reject-hidden-condition): the records are there all along.
// These rows' count under a hidden condition - and the sentence every such refusal ends on, `verb` = "reject" /
// "accept".
static int32 RowsUnderHiddenCondition(int32 chapterIdx, const std::vector<int32>& rows)
{
	int32 n = 0;
	for (size_t k = 0; k < rows.size(); ++k)
		if (KFCTrackChange::RowChangeIsHidden(chapterIdx, rows[k]))
			++n;
	return n;
}

static void AppendShowConditionToRetry(PMString& outStatus, const char* verb)
{
	outStatus.Append(" - show the condition and ");
	outStatus.Append(verb);
	outStatus.Append(" again.");
}

bool KFCReplaceEngine::StoryChangesHidden(int32 chapterIdx, int32 groupIdx)
{
	std::vector<int32> scope;
	ScopeRows(chapterIdx, groupIdx, scope);
	return RowsUnderHiddenCondition(chapterIdx, scope) > 0;
}

static bool RowsToReject(int32 chapterIdx, int32 groupIdx, std::vector<int32>& out, bool firstOnly = false)
{
	std::vector<int32> rows;
	ScopeRows(chapterIdx, groupIdx, rows);
	return RowsWithChangeOf(chapterIdx, rows, out, firstOnly);
}

bool KFCReplaceEngine::CanReplaceStory(int32 chapterIdx, int32 groupIdx)
{
	std::set<int32> rows;
	return RowsToReplace(chapterIdx, groupIdx, rows, true);
}

bool KFCReplaceEngine::CanRejectStory(int32 chapterIdx, int32 groupIdx)
{
	if (KFCResultModel::IsFromQueryRun())
		return false;
	std::vector<int32> rows;
	return RowsToReject(chapterIdx, groupIdx, rows, true);
}

// THE DOOR ACCEPT CHANGE ASKS OF ONE STORY'S ROWS - RejectRowsNow's, run by run. `rows` = where each row's
// written text starts, how long it is (KFCTrackChange::Change)
// and its original text. The rows are split into runs of touching rows (RejectRowsNow's rule), every record goes
// to the run whose text holds it, and each run's text with its records taken back
// (KFCTrackChange::OriginalFromRecords) must read as its rows' originals joined. False = one does not, or a
// record stands outside every run. (Not the records' deletions joined, compared with the originals: true of a
// whole-match replace, never of a GREP Change To holding $n, whose kept characters no deletion holds.)
struct RowSpan
{
	TextIndex	at;
	int32		len;
	PMString	original;
};
static bool RecordsGiveBackOriginals(const UIDRef& story, std::vector<RowSpan> rows,
	const std::vector<KFCTrackChange::Record>& recs)
{
	std::stable_sort(rows.begin(), rows.end(), [](const RowSpan& a, const RowSpan& b) { return a.at < b.at; });
	struct Run
	{
		TextIndex	at;
		TextIndex	end;
		PMString	original;
		std::vector<KFCTrackChange::Record>	recs;
	};
	std::vector<Run> runs;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (runs.empty() || rows[k].at > runs.back().end)
		{
			Run fresh;
			fresh.at = rows[k].at;
			fresh.end = rows[k].at;
			fresh.original.SetTranslatable(kFalse);
			runs.push_back(fresh);
		}
		Run& run = runs.back();
		if (rows[k].at + rows[k].len > run.end)
			run.end = rows[k].at + rows[k].len;
		run.original.Append(rows[k].original);
	}
	for (size_t r = 0; r < recs.size(); ++r)
	{
		const KFCTrackChange::Record& rec = recs[r];
		size_t home = runs.size();
		for (size_t k = 0; k < runs.size() && home == runs.size(); ++k)
		{
			const bool in = rec.isDelete ? (rec.at >= runs[k].at && rec.at <= runs[k].end)
				: (rec.at >= runs[k].at && rec.at + rec.len <= runs[k].end);
			if (in)
				home = k;
		}
		if (home == runs.size())
			return false;
		runs[home].recs.push_back(rec);
	}
	for (size_t k = 0; k < runs.size(); ++k)
	{
		bool inside = false;
		if (KFCTrackChange::OriginalFromRecords(story, runs[k].at, runs[k].end - runs[k].at, runs[k].recs, inside)
				!= runs[k].original || !inside)
			return false;
	}
	return true;
}

// TAKE A SET OF REPLACED ROWS BACK, IN ONE UNDO STEP (the row's touching group, or a story's replaced rows).
// The rows are split into runs of touching rows. `rows` = replaced rows
// outside a footnote (a footnote's row has nothing recorded), in any order. Any failure rolls all of it back.
// EACH RUN BY ITS ROWS' TIMES. Every record KFC writes carries the time of the row
// that wrote it (KFCTrackChange.h), so a run's records are exactly the ones carrying one of its rows' times -
// whatever InDesign did to them: touching replaces written front to back leave one insertion per row but ONE
// deletion, carrying the LAST row's time; written back to front (a GREP query holding ^) they leave one
// deletion per row and the later row's insertion split around the earlier row's deletion (both measured,
// cases touching-reject-first and touching-caret-back). Every row must still have its change
// (FindRowChangeForHit - its insertion must read as what it wrote); a row without one refuses the whole
// reject before a thing is written, so a group is never half taken back (case
// touching-accept-one-then-reject). Inside a run the DELETIONS go first, then the insertions, each time the
// one furthest on (measured: taking back a later replace's insertion drops an earlier one's deletion anchored
// on its first character), and the run's original text must then read back where the run starts. (Not by
// texts and the nearest place, nor each row alone.)
// EVERY CHANGE FOUND FIRST, THEN THE RUNS TAKEN BACK FROM THE LAST. Every change is
// looked up while nothing has moved. The RUNS are taken back from the last in text order: a run is
// separated from the next by text, and taking one back moves only what lies after it - all done by then -
// so each change found is still where it was found when its turn comes. (Front to back, looking each one up
// just before its turn, cancels a reject of 1200 alike rows every time: case reject-many-alike.) The rows
// follow the text as "this far into this thread" (RowNow, carried past each change as it is made -
// CarryPastChange), which also carries the rows in cells and footnotes behind a body change and past the
// threads a taken-back deletion takes with it; their stored places are written once, at the end.
static bool RejectRowsNow(int32 chapterIdx, std::vector<int32> rows, const UIDRef& docRef, PMString& outStatus)
{
	IDataBase* const db = docRef.GetDataBase();
	// each row where its change stands now, then text order - (story, start, row), each carrying its end
	// (read once, for the sort and the split alike)
	for (size_t k = 0; k < rows.size(); ++k)
		KFCTrackChange::RefreshRowFromRecords(chapterIdx, rows[k]);
	std::vector<std::pair<std::pair<std::pair<UID, TextIndex>, int32>, TextIndex> > order;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 h = 0;
		KFCResultModel::GetHitMatchIdentity(chapterIdx, rows[k], story, a, b, h);
		order.push_back(std::make_pair(std::make_pair(std::make_pair(story, a), rows[k]), b));
	}
	std::sort(order.begin(), order.end());
	std::vector<std::vector<int32> > runs;	// touching rows together
	UID lastStory = kInvalidUID;
	TextIndex lastEnd = kInvalidTextIndex;
	for (size_t k = 0; k < order.size(); ++k)
	{
		const UID story = order[k].first.first.first;
		const TextIndex a = order[k].first.first.second;
		if (runs.empty() || story != lastStory || a > lastEnd)
			runs.push_back(std::vector<int32>());
		runs.back().push_back(order[k].first.second);
		lastStory = story;
		lastEnd = order[k].second;
	}

	// What each run takes back: the records carrying its rows' times. Found now, before anything moves; a
	// row with no change of its own refuses the whole reject before a thing is written.
	struct Plan
	{
		std::vector<int32>	rows;		// text order
		UIDRef				story;
		std::set<uint64>	times;		// the run's rows' times - every record of the run carries one
		TextIndex			at;			// where the run's new text starts now
		int32				insLen;		// how long the run's new text is now
		PMString			allOriginal;
		std::vector<int32>	lengths;	// each row's original length, in text order
		std::vector<KFCTrackChange::Record>	recs;	// the records as the door read them - the reject starts from these
		Plan() : at(kInvalidTextIndex), insLen(0) {}
	};
	std::vector<Plan> plans(runs.size());
	std::set<std::pair<UID, TextIndex> > claimed;			// no change taken back twice
	for (size_t r = 0; r < runs.size(); ++r)
	{
		Plan& p = plans[r];
		p.rows = runs[r];
		p.allOriginal.SetTranslatable(kFalse);
		for (size_t k = 0; k < p.rows.size(); ++k)
		{
			KFCTrackChange::Change change;
			PMString originalText, replacedText;
			if (!KFCTrackChange::FindRowChangeForHit(chapterIdx, p.rows[k], p.story, change)
				|| !KFCResultModel::GetHitChangeTexts(chapterIdx, p.rows[k], originalText, replacedText))
			{
				if (KFCTrackChange::RowChangeIsHidden(chapterIdx, p.rows[k]))
				{
					outStatus = "Reject Change: nothing was changed - a row's replaced text is under a hidden condition";
					AppendShowConditionToRetry(outStatus, "reject");
				}
				else
					outStatus = "Reject Change: no tracked change of this replace is left for a row (undone, accepted or rejected in the Track Changes panel, deleted, or in a footnote or a replace that changed no character, where nothing is recorded) - nothing was changed.";
				return false;
			}
			const uint64 t = KFCResultModel::GetHitRecordTime(chapterIdx, p.rows[k]);
			if (t != 0)
				p.times.insert(t);
			if (p.at == kInvalidTextIndex || change.at < p.at)
				p.at = change.at;
			p.insLen += change.insLen;
			p.allOriginal.Append(originalText);
			p.lengths.push_back(WideString(originalText).CharCount());
		}
		// THE RUN'S DELETIONS MUST HOLD EXACTLY ITS ROWS' ORIGINAL TEXT (case touching-accept-one-then-reject).
		// InDesign joins a deletion to the one it touches, whoever made
		// either, so a deletion of this run can also hold a neighbour's original text - a neighbour whose own
		// change is gone (accepted in the Track Changes panel) and so is not in the run. Taking it back would
		// put that neighbour's text back beside the words it was accepted as, and the read-back below would not
		// see it (it reads the run's own length only): refused before a thing is written.
		// ASKED OF THE RUN'S TEXT WITH ITS RECORDS TAKEN BACK - not of the deletions alone, which a GREP Change To
		// holding $n never passes: it keeps the matched characters $n names, and no deletion holds them
		// (c(at) -> k$1 deletes "c" only). Taking every
		// record of the run back in its text (KFCTrackChange::OriginalFromRecords) is the same question for a
		// whole-match replace - nothing of its text is kept - and the right one for a $n.
		{
			KFCTrackChange::CollectRecordsOfTimes(p.story, p.times, p.recs);
			bool inside = false;
			if (KFCTrackChange::OriginalFromRecords(p.story, p.at, p.insLen, p.recs, inside) != p.allOriginal || !inside)
			{
				outStatus = "Reject Change: the tracked deletion of this replace also holds text that is not these rows' (a touching neighbour's change was accepted, or somebody else's deletion joined it) - nothing was changed.";
				return false;
			}
		}
		if (!claimed.insert(std::make_pair(p.story.GetUID(), p.at)).second)
		{
			outStatus = "Reject Change: two rows came to the same tracked change - nothing was changed. Search again.";
			return false;
		}
	}

	std::vector<RowNow> others;		// every row as "this far into its thread", carried past each change
	SnapshotRows(chapterIdx, db, others);
	// The stories taken back in that were as KFC left them take their new version afterwards. A reject finds
	// its records by their time, whatever moved the text - but one that had moved without KFC keeps its old
	// version, so the rows nobody has looked at since are not vouched for by this.
	std::set<UID> asLeft;
	{
		std::set<UID> rejectedIn;
		for (size_t r = 0; r < plans.size(); ++r)
			rejectedIn.insert(plans[r].story.GetUID());
		StoriesAsKFCLeftThem(chapterIdx, db, rejectedIn, asLeft);
	}
	// Recorded for the panel's following of Undo: the row a reject takes back is a replaced row
	// again when the reject is undone, and offers Reject Change again. Kept below, once the rows show it.
	KFCUndoFollow::StepRecorder recorder(std::vector<int32>(1, chapterIdx), false);
	const bool wasModified = DocIsModified(db);		// put back if the step is rolled back (EndPlainSequence)
	// (the step's name in the UI's language - KFCLoc)
	ICommandSequence* sequence = BeginPlainSequence(KFCLoc::Text(kKFCRejectStepKey, KFCJa::kRejectStep),
		"Reject Change: ", outStatus);
	if (sequence == nil)
		return false;
	std::vector<int32> taken;		// every row taken back; others[row] follows where its original text stands
	PMString why;
	bool ok = true;
	// the runs from the last: taking one back moves only what lies after it - all done by then
	for (size_t r = plans.size(); r-- > 0 && ok; )
	{
		const Plan& p = plans[r];
		// THE DELETIONS FIRST, THEN THE INSERTIONS - EACH TIME THE ONE FURTHEST ON (measured). Taking back a
		// later replace's insertion drops an earlier one's deletion anchored on its first character - the
		// header's own rule, found by measuring before it was read: "rejecting a nested insert will also reject
		// the delete change" (redlineiterator.h:46-50). The records are read again after each one: taking one
		// back moves the rest.
		// THE FIRST READING IS THE DOOR'S. Nothing this plan's records stand on has moved since: the runs after
		// it in the story took back text after it only. A record not found there all the same gets one fresh
		// reading before the reject is called off.
		std::vector<KFCTrackChange::Record> recs(p.recs);
		bool fresh = false;
		const size_t guard = recs.size() + 2;		// + the one fresh reading
		for (size_t g = 0; g < guard && ok && !recs.empty(); ++g)
		{
			size_t pick = recs.size();
			for (size_t k = 0; k < recs.size(); ++k)
				if (recs[k].isDelete && (pick == recs.size() || recs[k].at >= recs[pick].at))
					pick = k;
			if (pick == recs.size())
				for (size_t k = 0; k < recs.size(); ++k)
					if (pick == recs.size() || recs[k].at >= recs[pick].at)
						pick = k;
			if (!KFCTrackChange::RejectRecord(p.story, recs[pick].at, recs[pick].time, recs[pick].isDelete))
			{
				if (!fresh)
				{
					KFCTrackChange::CollectRecordsOfTimes(p.story, p.times, recs);
					fresh = true;
					continue;
				}
				why = "InDesign would not take back a tracked change of this replace";
				ok = false;
				break;
			}
			KFCTrackChange::CollectRecordsOfTimes(p.story, p.times, recs);
			fresh = true;
		}
		if (ok && !recs.empty())
		{
			why = "a tracked change of this replace was still there after taking it back";
			ok = false;
		}
		const int32 allLen = WideString(p.allOriginal).CharCount();
		if (ok && KFCTrackChange::ReadText(p.story, p.at, allLen) != p.allOriginal)
		{
			why = "the original text did not come all the way back";
			ok = false;
		}
		if (ok)
		{
			CarryPastChange(db, others, p.story.GetUID(), p.at, p.insLen, allLen);
			TextIndex at = p.at;
			for (size_t k = 0; k < p.rows.size(); ++k)
			{
				SetRowAt(db, others[static_cast<size_t>(p.rows[k])], p.story.GetUID(), at, at + p.lengths[k]);
				taken.push_back(p.rows[k]);
				at += p.lengths[k];
			}
		}
	}
	if (ok)
		KFCUndoFollow::MarkWrite(db);	// in this step, so its Undo / Redo is heard
	EndPlainSequence(sequence, ok, db, wasModified);
	if (!ok)
	{
		outStatus = "Reject Change: ";
		outStatus.Append(why);
		outStatus.Append(" - the reject was cancelled and the document is as it was.");
		return false;
	}
	NoteStoryVersions(chapterIdx, db, asLeft);

	// every other row where the text has taken it, then the rows taken back where their original text stands
	std::vector<bool> isTaken(others.size(), false);
	for (size_t k = 0; k < taken.size(); ++k)
		isTaken[static_cast<size_t>(taken[k])] = true;
	WriteBackRows(chapterIdx, db, others, isTaken, kRangeAndText);
	for (size_t k = 0; k < taken.size(); ++k)
	{
		const RowNow& row = others[static_cast<size_t>(taken[k])];
		TextIndex at = kInvalidTextIndex;
		if (!RowStartNow(db, row, at))
			continue;
		KFCResultModel::SetHitRejected(chapterIdx, taken[k], row.story, at, at + row.length);
		KFCSearchEngine::RereadRowText(chapterIdx, taken[k], UIDRef(db, row.story), at, at + row.length);
	}
	// (No walk numbers the rest again: a Change Checked after this finds each row by its place and text,
	//  which the lines above have just written.)
	recorder.Keep(KFCUndoFollow::kStepReject);
	return true;
}

// The row with every replaced row touching it (see RejectRowsNow on why a touching group goes together) -
// what a row's Reject Change and Accept Change act on.
//
// ONLY THE NEIGHBOURS WHOSE CHANGE IS STILL THERE (case touching-group-redo-reject).
// A neighbour's change can be gone while the row still reads as replaced: the replace of THIS row accepts
// the pending changes it touches first (AcceptPendingAround), and a neighbour replaced a moment earlier
// is exactly such a change. Taking that neighbour along finds nothing to reject for it, and the whole
// reject is cancelled - so this row could never be taken back at all. A neighbour with no change of its
// own left is not part of what can be taken back; the row itself always is (RejectRowsNow says why when
// its own change is gone, and refuses when its deletion also holds such a neighbour's text).
static void RowWithTouchingChanges(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows)
{
	std::vector<int32> group;
	KFCTrackChange::RefreshRowFromRecords(chapterIdx, hitIdx);
	KFCTrackChange::ReplacedTouchingGroup(chapterIdx, hitIdx, group);	// the replaced rows outside a footnote
	outRows.clear();
	for (size_t k = 0; k < group.size(); ++k)
	{
		UIDRef storyRef;
		KFCTrackChange::Change change;
		if (group[k] == hitIdx || KFCTrackChange::FindRowChangeForHit(chapterIdx, group[k], storyRef, change))
			outRows.push_back(group[k]);
	}
	if (outRows.empty())
		outRows.push_back(hitIdx);
}

bool KFCReplaceEngine::RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef))
	{
		outStatus.Append("Reject Change: the document of this row is not open.");
		return false;
	}
	std::vector<int32> rows;
	RowWithTouchingChanges(chapterIdx, hitIdx, rows);
	if (!RejectRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	// On a list rebuilt from the records there is no box and no Replace: what to do next is to
	// search again (the pre-flight finding of the Show Changes plan).
	if (KFCResultModel::IsFromRecords())
		outStatus = (rows.size() > 1)
			? "Rejected - the row and the matches touching it are back to their original text. To replace again, search again."
			: "Rejected - the row is back to its original text. To replace again, search again.";
	else
		outStatus = (rows.size() > 1)
			? "Rejected - the row and the matches touching it are back to their original text. Tick them, or right-click for Replace, to replace them again."
			: "Rejected - the row is back to its original text. Tick it, or right-click it for Replace, to replace it again.";
	return true;
}

// Reject Change over `scope` - a story row's rows, a document row's, or a run row's: every row
// of it RowsWithChangeOf names, in one undo step. `unit` = "story" / "document" / "run", for the status line.
static bool RejectRowsOf(int32 chapterIdx, const std::vector<int32>& scope, const char* unit, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef))
	{
		outStatus.Append("Reject Change: the document of this row is not open.");
		return false;
	}
	std::vector<int32> rows;
	// Rows whose replaced text is under a hidden condition are not among `rows` (their change is not found
	// where they are) - said, whichever way this ends, rather than left out in silence.
	const int32 hidden = RowsUnderHiddenCondition(chapterIdx, scope);
	if (!RowsWithChangeOf(chapterIdx, scope, rows, false))
	{
		if (hidden > 0)
		{
			outStatus.Append("Reject Change: nothing was changed - the replaced text in this ");
			outStatus.Append(unit);
			outStatus.Append(" is under a hidden condition");
			AppendShowConditionToRetry(outStatus, "reject");
			return false;
		}
		outStatus.Append("Reject Change: no replaced row with a tracked change in this ");
		outStatus.Append(unit);
		outStatus.Append(".");
		return false;
	}
	if (!RejectRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	outStatus = "Rejected ";
	outStatus.AppendNumber(static_cast<int32>(rows.size()));
	outStatus.Append(" row(s) of this ");
	outStatus.Append(unit);
	outStatus.Append(KFCResultModel::IsFromRecords()
		? " - back to their original text. To replace again, search again."
		: " - back to their original text. Tick them and Replace to replace them again.");
	if (hidden > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(hidden);
		outStatus.Append(" left - under a hidden condition");
		AppendShowConditionToRetry(outStatus, "reject");
	}
	return true;
}

// Reject Change on a story row (groupIdx >= 0) or a document row (groupIdx < 0).
static bool RejectRowsIn(int32 chapterIdx, int32 groupIdx, const char* unit, PMString& outStatus)
{
	std::vector<int32> scope;
	ScopeRows(chapterIdx, groupIdx, scope);
	return RejectRowsOf(chapterIdx, scope, unit, outStatus);
}

// (No range check of chapterIdx in front of the scope: the model's own range check answers an empty
//  scope for that already.)
bool KFCReplaceEngine::CanRejectChapter(int32 chapterIdx)
{
	if (KFCResultModel::IsFromQueryRun())
		return false;
	std::vector<int32> rows;
	return RowsToReject(chapterIdx, -1, rows, true);
}

bool KFCReplaceEngine::RejectChapter(int32 chapterIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	return RejectRowsIn(chapterIdx, -1, "document", outStatus);
}

bool KFCReplaceEngine::RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	return RejectRowsIn(chapterIdx, groupIdx, "story", outStatus);
}

// ======================================================================================================
// Accept All Changes by KohakuFindChange in This Document (the signed records only) - see the header.
// The same plain sequence and rollback as Reject Change above.
// ======================================================================================================
bool KFCReplaceEngine::CanAcceptAllInChapter(int32 chapterIdx)
{
	if (KFCResultModel::IsFromQueryRun())
		return false;
	UIDRef docRef;
	return KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef) && KFCTrackChange::DocumentHasSignedRecords(docRef.GetDataBase());
}

bool KFCReplaceEngine::AcceptAllInChapter(int32 chapterIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef))
	{
		outStatus.Append("Accept All Changes by KohakuFindChange: the document is not open.");
		return false;
	}
	// THE ROWS FOLLOW THE TEXT. Accepting a deletion takes its deleted-text thread away, and every thread
	// behind it in the story - a table cell's, a footnote's - moves up. Left at their story indexes, a cell's
	// row would stand where its text no longer is: its jump says "missing" and its Replace refuses as
	// "changed since the search" (case accept-all-then-cell). Taken as
	// "this far into this thread" before (SnapshotRows) and put back after (WriteBackRows), as a reject does;
	// accepting changes no text, so nothing is carried within a thread - and no row's line or hash is read
	// again, only its range moved (kRangeOnly).
	IDataBase* const db = docRef.GetDataBase();
	std::vector<RowNow> rows;
	SnapshotRows(chapterIdx, db, rows);
	// The row stories that were as KFC left them take their new version afterwards: accepting moves a
	// story's version, and one that had moved without KFC keeps its old one.
	std::set<UID> asLeft;
	{
		std::set<UID> rowStories;
		KFCResultModel::GetChapterStories(chapterIdx, rowStories);
		StoriesAsKFCLeftThem(chapterIdx, db, rowStories, asLeft);
	}
	// Recorded for the panel's following of Undo - Reject Change's reason.
	KFCUndoFollow::StepRecorder recorder(std::vector<int32>(1, chapterIdx), false);
	const bool wasModified = DocIsModified(db);		// put back if the step is rolled back (EndPlainSequence)
	ICommandSequence* sequence = BeginPlainSequence(KFCLoc::Text(kKFCAcceptAllStepKey, KFCJa::kAcceptAllStep),
		"Accept All Changes by KohakuFindChange: ", outStatus);
	if (sequence == nil)
		return false;
	PMString why;
	why.SetTranslatable(kFalse);
	int32 left = 0;
	std::set<uint64> acceptedTimes;		// the rows carrying one were accepted
	const int32 accepted = KFCTrackChange::AcceptSignedInDocument(db, left, why, &acceptedTimes);
	if (accepted >= 0)
		KFCUndoFollow::MarkWrite(db);	// in this step, so its Undo / Redo is heard
	EndPlainSequence(sequence, accepted >= 0, db, wasModified);
	if (accepted < 0)
	{
		outStatus = "Accept All Changes by KohakuFindChange: ";
		outStatus.Append(why);
		outStatus.Append(" - nothing was accepted and the document is as it was.");
		return false;
	}
	WriteBackRows(chapterIdx, db, rows, std::vector<bool>(), kRangeOnly);
	NoteStoryVersions(chapterIdx, db, asLeft);
	// THE ROWS IT ACCEPTED SAY SO. A replaced row whose time the accept
	// took away: "accepted", as Accept Change on a row, a story or a run leaves it. Rows left (hidden
	// conditional text), a footnote's (nothing recorded) and rows whose change had gone already (the Track
	// Changes panel) are left as they were. The times come from the accept's own counting walks - no
	// walks of every story of its own.
	{
		const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
		for (int32 i = 0; i < hitCount; ++i)
		{
			bool checked = false, replaced = false, locked = false;
			const uint64 t = KFCResultModel::GetHitRecordTime(chapterIdx, i);
			if (t != 0 && acceptedTimes.count(t) > 0
				&& KFCResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) && replaced)
				KFCResultModel::SetHitAccepted(chapterIdx, i);
		}
	}
	recorder.Keep(KFCUndoFollow::kStepAcceptAll);
	// OURS ONLY, AND THE HIDDEN ONES SAID (the author's call). The records signed "KohakuFindChange" -
	// everybody else's changes stay (not every change in the document, as InDesign's own Accept All). The
	// numbers come first: a status line cut short cuts its end. They count REPLACES, one per row as Show
	// Changes counts them - not records.
	outStatus = "Accepted ";
	outStatus.AppendNumber(accepted);
	if (left > 0)
	{
		// The one cause measured: text under a hidden condition, which the accept leaves.
		outStatus.Append(" change(s) by KohakuFindChange; ");
		outStatus.AppendNumber(left);
		outStatus.Append(" left unaccepted - text under a hidden condition is left: show the condition and accept again.");
	}
	else
		outStatus.Append(" change(s) by KohakuFindChange in the document - other changes are left. They can no longer be rejected.");
	return true;
}

// ======================================================================================================
// ACCEPT CHANGE BY KohakuFindChange - A ROW, A STORY, A RUN (Show Changes). Reject
// Change's twin, built the same way (RejectRowsNow): every row's change found first, while nothing has
// moved - a row without one refuses the whole accept; the records carrying the rows' times accepted one at
// a time (KFCTrackChange::AcceptRecord), the deletions first and each time the one furthest on, the records
// read again after each; the rows put back where the text has taken them (accepting a deletion takes its
// deleted-text thread away, and the threads behind it move up - AcceptAllInChapter's reason); one plain
// sequence, rolled back whole on any failure. Accepting changes no text, so no row's line is read again.
// ======================================================================================================
static bool AcceptRowsNow(int32 chapterIdx, const std::vector<int32>& rows, const UIDRef& docRef, PMString& outStatus)
{
	IDataBase* const db = docRef.GetDataBase();
	// Each row where its change stands now, then its change: the times to accept, story by story, and each
	// story's rows' original texts in text order.
	std::map<UID, std::set<uint64> > timesOfStory;
	std::map<UID, std::vector<RowSpan> > spansOfStory;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		KFCTrackChange::RefreshRowFromRecords(chapterIdx, rows[k]);
		UIDRef storyRef;
		KFCTrackChange::Change change;
		PMString originalText, replacedText;
		if (!KFCTrackChange::FindRowChangeForHit(chapterIdx, rows[k], storyRef, change)
			|| !KFCResultModel::GetHitChangeTexts(chapterIdx, rows[k], originalText, replacedText))
		{
			if (KFCTrackChange::RowChangeIsHidden(chapterIdx, rows[k]))
			{
				outStatus = "Accept Change: nothing was changed - a row's replaced text is under a hidden condition";
				AppendShowConditionToRetry(outStatus, "accept");
			}
			else
				outStatus = "Accept Change: no tracked change of this replace is left for a row (undone, accepted or rejected in the Track Changes panel, deleted, or in a footnote or a replace that changed no character, where nothing is recorded) - nothing was changed.";
			return false;
		}
		const uint64 t = KFCResultModel::GetHitRecordTime(chapterIdx, rows[k]);
		if (t != 0)
			timesOfStory[storyRef.GetUID()].insert(t);
		RowSpan span;
		span.at = change.at;
		span.len = change.insLen;
		span.original = originalText;
		spansOfStory[storyRef.GetUID()].push_back(span);
	}
	// THE RECORDS MUST GIVE BACK EXACTLY THESE ROWS' ORIGINAL TEXT - RejectRowsNow's door. InDesign joins a
	// deletion to the one it touches, whoever made either: accepting such a deletion would make somebody
	// else's deletion final too, or a neighbour's this accept was not asked about.
	// (Asked of the rows' text with the records taken back - RecordsGiveBackOriginals; not of the deletions
	// alone, which a GREP $n's row never passes.)
	// The records read for it are the ones the accept below starts from: nothing moves in between.
	std::map<UID, std::vector<KFCTrackChange::Record> > recsOfStory;
	for (std::map<UID, std::set<uint64> >::const_iterator s = timesOfStory.begin(); s != timesOfStory.end(); ++s)
	{
		std::vector<KFCTrackChange::Record>& recs = recsOfStory[s->first];
		KFCTrackChange::CollectRecordsOfTimes(UIDRef(db, s->first), s->second, recs);
		if (!RecordsGiveBackOriginals(UIDRef(db, s->first), spansOfStory[s->first], recs))
		{
			outStatus = "Accept Change: the tracked deletion of this replace also holds text that is not these rows' (a touching neighbour's change was accepted, or somebody else's deletion joined it) - nothing was changed.";
			return false;
		}
	}

	std::vector<RowNow> others;		// every row as "this far into its thread", put back afterwards
	SnapshotRows(chapterIdx, db, others);
	// The stories that were as KFC left them take their new version afterwards.
	std::set<UID> asLeft;
	{
		std::set<UID> acceptedIn;
		for (std::map<UID, std::set<uint64> >::const_iterator s = timesOfStory.begin(); s != timesOfStory.end(); ++s)
			acceptedIn.insert(s->first);
		StoriesAsKFCLeftThem(chapterIdx, db, acceptedIn, asLeft);
	}
	// Recorded for the panel's following of Undo: an accept undone gives the row its time back, and with
	// it Reject Change and Accept Change.
	KFCUndoFollow::StepRecorder recorder(std::vector<int32>(1, chapterIdx), false);
	const bool wasModified = DocIsModified(db);		// put back if the step is rolled back (EndPlainSequence)
	ICommandSequence* sequence = BeginPlainSequence(KFCLoc::Text(kKFCAcceptStepKey, KFCJa::kAcceptStep),
		"Accept Change: ", outStatus);
	if (sequence == nil)
		return false;
	PMString why;
	bool ok = true;
	// ONE READING, THEN EVERY RECORD IN ORDER - not read again after every single accept, which walks a story n
	// times for a run of n records. Unlike a reject, an accept drops no other record (accepting a nested
	// insertion leaves the deletion - redlineiterator.h:49-50), so the order is kept from one reading: the
	// deletions first, then the insertions, each time the one furthest on (the reject's order). A record not
	// found where it was read (an accept before it moved it - a thread behind a deleted text's) ends the pass;
	// the story is read again and the rest go the same way. Every pass accepts at least one record or the
	// accept fails, and the last reading must find none left.
	for (std::map<UID, std::set<uint64> >::const_iterator s = timesOfStory.begin(); s != timesOfStory.end() && ok; ++s)
	{
		const UIDRef storyRef(db, s->first);
		std::vector<KFCTrackChange::Record>& recs = recsOfStory[s->first];		// the door's reading
		// a bound: an accept said to have gone through while its record stays must not spin
		size_t passesLeft = recs.size() + 1;
		while (ok && !recs.empty())
		{
			if (passesLeft-- == 0)
			{
				why = "a tracked change of this replace was still there after accepting it";
				ok = false;
				break;
			}
			std::stable_sort(recs.begin(), recs.end(),
				[](const KFCTrackChange::Record& a, const KFCTrackChange::Record& b)
				{ return (a.isDelete != b.isDelete) ? a.isDelete : a.at > b.at; });
			size_t done = 0;
			while (done < recs.size()
				&& KFCTrackChange::AcceptRecord(storyRef, recs[done].at, recs[done].time, recs[done].isDelete))
				++done;
			if (done == 0)
			{
				why = "InDesign would not accept a tracked change of this replace";
				ok = false;
				break;
			}
			KFCTrackChange::CollectRecordsOfTimes(storyRef, s->second, recs);	// what is left - normally none
		}
	}
	if (ok)
		KFCUndoFollow::MarkWrite(db);	// in this step, so its Undo / Redo is heard
	EndPlainSequence(sequence, ok, db, wasModified);
	if (!ok)
	{
		outStatus = "Accept Change: ";
		outStatus.Append(why);
		outStatus.Append(" - the accept was cancelled and the document is as it was.");
		return false;
	}
	NoteStoryVersions(chapterIdx, db, asLeft);
	WriteBackRows(chapterIdx, db, others, std::vector<bool>(), kRangeOnly);
	for (size_t k = 0; k < rows.size(); ++k)
		KFCResultModel::SetHitAccepted(chapterIdx, rows[k]);
	recorder.Keep(KFCUndoFollow::kStepAccept);
	return true;
}

bool KFCReplaceEngine::CanAcceptOrRejectHit(int32 chapterIdx, int32 hitIdx)
{
	if (KFCResultModel::IsFromQueryRun())
		return false;
	UIDRef storyRef;
	KFCTrackChange::Change change;
	return KFCTrackChange::FindRowChangeForHit(chapterIdx, hitIdx, storyRef, change);
}

bool KFCReplaceEngine::AcceptHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef))
	{
		outStatus.Append("Accept Change: the document of this row is not open.");
		return false;
	}
	// RejectHit's group, for its reason: a touching group written front to back shares one deletion.
	std::vector<int32> rows;
	RowWithTouchingChanges(chapterIdx, hitIdx, rows);
	if (!AcceptRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	outStatus = (rows.size() > 1)
		? "Accepted - the changes by KohakuFindChange of the row and the matches touching it are final: they can no longer be rejected."
		: "Accepted - the change by KohakuFindChange is final: it can no longer be rejected.";
	return true;
}

// Accept Change over `scope` - a story row's rows or a run row's: every row of it with a change left, in one
// undo step. `unit` = "story" / "run", for the status line.
static bool AcceptRowsOf(int32 chapterIdx, const std::vector<int32>& scope, const char* unit, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	if (!KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef))
	{
		outStatus.Append("Accept Change: the document of this row is not open.");
		return false;
	}
	std::vector<int32> rows;
	// RejectRowsOf's rule: rows under a hidden condition are said, not left out in silence.
	const int32 hidden = RowsUnderHiddenCondition(chapterIdx, scope);
	if (!RowsWithChangeOf(chapterIdx, scope, rows, false))
	{
		if (hidden > 0)
		{
			outStatus.Append("Accept Change: nothing was changed - the replaced text in this ");
			outStatus.Append(unit);
			outStatus.Append(" is under a hidden condition");
			AppendShowConditionToRetry(outStatus, "accept");
			return false;
		}
		outStatus.Append("Accept Change: no replaced row with a tracked change in this ");
		outStatus.Append(unit);
		outStatus.Append(".");
		return false;
	}
	if (!AcceptRowsNow(chapterIdx, rows, docRef, outStatus))
		return false;
	outStatus = "Accepted ";
	outStatus.AppendNumber(static_cast<int32>(rows.size()));
	outStatus.Append(" row(s) of this ");
	outStatus.Append(unit);
	outStatus.Append(" - their changes by KohakuFindChange are final: they can no longer be rejected.");
	if (hidden > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(hidden);
		outStatus.Append(" left - under a hidden condition");
		AppendShowConditionToRetry(outStatus, "accept");
	}
	return true;
}

bool KFCReplaceEngine::AcceptStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	std::vector<int32> scope;
	ScopeRows(chapterIdx, groupIdx, scope);
	return AcceptRowsOf(chapterIdx, scope, "story", outStatus);
}

bool KFCReplaceEngine::CanRejectOrAcceptRun(int32 chapterIdx, int32 runIdx)
{
	if (KFCResultModel::IsFromQueryRun())
		return false;
	std::vector<int32> scope, rows;
	KFCResultModel::GetRunHits(chapterIdx, runIdx, scope);
	return RowsWithChangeOf(chapterIdx, scope, rows, true);
}

bool KFCReplaceEngine::RejectRun(int32 chapterIdx, int32 runIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	std::vector<int32> scope;
	KFCResultModel::GetRunHits(chapterIdx, runIdx, scope);
	return RejectRowsOf(chapterIdx, scope, "run", outStatus);
}

bool KFCReplaceEngine::AcceptRun(int32 chapterIdx, int32 runIdx, PMString& outStatus)
{
	if (RefusedOnQueryRun(outStatus))
		return false;
	std::vector<int32> scope;
	KFCResultModel::GetRunHits(chapterIdx, runIdx, scope);
	return AcceptRowsOf(chapterIdx, scope, "run", outStatus);
}

// Replace on a story row (groupIdx >= 0) or a DOCUMENT row (groupIdx < 0) (the author's call):
// the scope's TICKED rows, no prompt, one undo step. `unit` = "story" / "document", for the status line.
static bool ReplaceRowsIn(int32 chapterIdx, int32 groupIdx, const char* unit, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	std::set<int32> rows;
	if (!RowsToReplace(chapterIdx, groupIdx, rows))
	{
		outStatus.Append("Replace: no checked row in this ");
		outStatus.Append(unit);
		outStatus.Append(" to replace - tick the rows first.");
		return false;
	}
	return ReplaceRowsNow(chapterIdx, rows, outStatus, unit);
}

bool KFCReplaceEngine::CanReplaceChapter(int32 chapterIdx)
{
	std::set<int32> rows;
	return RowsToReplace(chapterIdx, -1, rows, true);
}

bool KFCReplaceEngine::ReplaceChapter(int32 chapterIdx, PMString& outStatus)
{
	return ReplaceRowsIn(chapterIdx, -1, "document", outStatus);
}

// Replace on a story row: that story's TICKED rows (the author's call), one undo step.
bool KFCReplaceEngine::ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	return ReplaceRowsIn(chapterIdx, groupIdx, "story", outStatus);
}

// Redo (Replace Again) on a story row (the author's call). The story's rows taken back with Reject Change,
// replaced again - ticked or not, so they can all be done without ticking them one by one (Check All would
// tick the rows never replaced as well). With what Find/Change holds NOW, like Replace (the row's own menu
// has only Replace) - not "the Change To the replace used", which can only be compared as a description,
// not as the replace itself. A row whose text is not its original any more (edited, or undone since) is
// skipped and counted.
static void RowsToRedo(int32 chapterIdx, const std::vector<int32>& rows, IDataBase* db, std::set<int32>& outFit,
	int32& outSkipped, bool firstOnly)
{
	outFit.clear();
	outSkipped = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (KFCResultModel::GetHitOutcome(chapterIdx, rows[k]) != KFCResultModel::kOutcomeRejected
			|| !KFCReplaceEngine::CanReplaceHit(chapterIdx, rows[k]))
			continue;
		// the match and its line (RowReadsAsFound - not the match's hash alone); the
		// story's version is asked by the Replace this hands the rows to (ReplaceRowsNow)
		if (db == nil || KFCSearchEngine::RowReadsAsFound(chapterIdx, rows[k], db))
		{
			outFit.insert(rows[k]);
			if (firstOnly)
				return;
		}
		else
			++outSkipped;
	}
}

static bool CanRedoRows(int32 chapterIdx, int32 groupIdx)
{
	std::vector<int32> scope;
	ScopeRows(chapterIdx, groupIdx, scope);
	std::set<int32> rows;
	int32 skipped = 0;
	RowsToRedo(chapterIdx, scope, nil, rows, skipped, true);	// nil = no text test for the greying
	return !rows.empty();
}

// Redo over a story or a document: see RowsToRedo. `unit` = "story" / "document", for the status line.
static bool RedoRowsNowIn(int32 chapterIdx, int32 groupIdx, const char* unit, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	// (Every line here says "Replace Again", the menu item's name - the author's call; the functions keep
	//  Redo. ReplaceRowsNow writes the rest, `again` = true.)
	UIDRef docRef;
	if (!KFCTrackChange::ChapterDocIfOpen(chapterIdx, docRef))
	{
		StartStatus(outStatus, true);
		outStatus.Append("the document of these rows is not open.");
		return false;
	}
	IDataBase* const db = docRef.GetDataBase();
	std::vector<int32> scope;
	ScopeRows(chapterIdx, groupIdx, scope);
	std::set<int32> rows;
	int32 skipped = 0;
	RowsToRedo(chapterIdx, scope, db, rows, skipped, false);
	if (rows.empty())
	{
		StartStatus(outStatus, true);
		outStatus.Append("no row of this ");
		outStatus.Append(unit);
		outStatus.Append(" taken back with Reject Change can be replaced again (its text has changed since).");
		return false;
	}
	if (!ReplaceRowsNow(chapterIdx, rows, outStatus, unit, true))
		return false;
	if (skipped > 0)
	{
		outStatus.Append(" ");
		outStatus.AppendNumber(skipped);
		outStatus.Append(" taken back could not be replaced again (text changed since) and were left.");
	}
	return true;
}

bool KFCReplaceEngine::CanRedoStory(int32 chapterIdx, int32 groupIdx)
{
	return CanRedoRows(chapterIdx, groupIdx);
}

bool KFCReplaceEngine::CanRedoChapter(int32 chapterIdx)
{
	return CanRedoRows(chapterIdx, -1);
}

bool KFCReplaceEngine::RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	return RedoRowsNowIn(chapterIdx, groupIdx, "story", outStatus);
}

// (An out-of-range index meets RedoRowsNowIn's "not open", the way RejectChapter and ReplaceChapter do -
//  not a false with nothing in outStatus.)
bool KFCReplaceEngine::RedoChapter(int32 chapterIdx, PMString& outStatus)
{
	return RedoRowsNowIn(chapterIdx, -1, "document", outStatus);
}

// End, KFCReplaceEngine.cpp.
