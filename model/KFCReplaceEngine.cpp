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
#include "ISession.h"				// GetExecutionContextSession - the walker's registry

// General includes:
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss / kTWReplaceTextCmdBoss / kFindChangeClientBoss
#include "WalkerScopeOptions.h"
#include "CmdUtils.h"				// commands and command sequences
#include "CreateObject.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode, GlobalErrorStatePreserver
#include "ITextModel.h"				// QueryStoryThread / FindStoryThread - a row kept as "this far into this thread" (RowNow)
#include "ITextStoryThread.h"
#include "WideString.h"			// GrepQueryHoldsLineStart: the query read as code points
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
#include "KFCUndoFollow.h"		// every write recorded, so that the panel follows its Undo and Redo
#ifdef KFC_DIAG
#include "ITextFocusManager.h"	// how many foci a story carries while it is written (WALKSTEP) - test builds only
#include "IFrameList.h"			// whether its frames stand damaged around a find (WALKSTEP) - test builds only
#endif

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
// NOT InDesign's CHANGE ALL. A row's Replace writes that row and nothing else; InDesign's Change All writes every
// match of its scope and is KFCChangeAll's (Change All in Book (No List), and each query of a query run). A
// Change All over each story with the rows NOT asked for taken back record by record afterwards was tried and
// dropped (the author's call) - it is in git history (c876bc7 and before).
// ======================================================================================================

// ======================================================================================================
// ONE MATCH AT A TIME, STORY BY STORY (the author's call). Only the rows asked for are written, one at a time
// (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on the match it made current), a story at a time, with Track
// Changes as each story has it - KFC records nothing of its own (2026-10-06, spec F1). (Not Change All - see
// above.)
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
// in the same thread (CarryRowsPast); a written row is put where its new text stands - an endnote's
// last match too, written as InDesign writes it (2026-10-06, spec F12). What is still in `pending` at the
// end never came up. False = the walk could not start at all; outWalkFailed = it started and broke off.
// (Cancel is not asked in here - between stories, by the caller: the author's call, story by story.)
bool WalkStoryReplacing(int32 chapterIdx, const UIDRef& storyRef, const WalkerScopeOptions& scopeOptions,
	IFindChangeOptions* opts, std::vector<RowNow>& rowNow, std::set<int32>& pending, std::vector<int32>& keptRows,
	int32& ioReplaced, int32& ioRefused, bool& outWalkFailed,
	KFCProgressBar* progressBar, int32 progressBase, int32& ioProgressReported, int32& ioDone)
{
	IDataBase* const db = storyRef.GetDataBase();
	// The walker, and the shared walker's selection utilities the critical section is taken on
	// (KFCSearchEngine::AcquireWalker - KFC's own).
	InterfacePtr<ITextWalker> walker;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils;
	bool ownWalker = false;
	const bool haveWalker = KFCSearchEngine::AcquireWalker(walker, selUtils, ownWalker);
	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(storyRef, scopeOptions));
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (db == nil || !haveWalker || walker == nil || scope == nil || client == nil)
		return false;
	if (walker->IsWalking())
		walker->Halt();
	walker->Initialize(client, scope, opts, nil);
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
		double tFind = 0, tMatch = 0, tReplace = 0, tTexts = 0, tCarry = 0;
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

			UIDRef written;
			TextIndex writtenStart = kInvalidTextIndex, writtenEnd = kInvalidTextIndex;
			// (Fault switch replace-refuse, a test build's only - KFCDiag.h: InDesign's replace refuses every row,
			// the one way a test reaches a chapter where nothing lands.)
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
				lastStory = written.GetUID();
				lastStart = writtenStart;
				lastEnd = writtenEnd;
				// WHAT IT WROTE, TAKEN HERE - once (Hit::replacedText): the jump looks for a moved row by it
				// (KFCSearchEngine::RelocateStaleRow). The text written now is the text the row holds at the end: no
				// later replace of the run writes inside it, since every other row is carried outside it and a match
				// there is none of theirs (stepped over, above).
				KFC_CLOCK(cTexts);
				KFCResultModel::SetHitWrittenText(chapterIdx, hitIdx,
					KFCSearchEngine::ReadText(written, writtenStart, writtenEnd - writtenStart));
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
			++ioDone;
			KFCAdvanceProgress(progressBar, ioProgressReported, progressBase + ioDone);
		}
#ifdef KFC_DIAG
		KFC_DIAG_LOG("WALKTIME story=%u rows=%d finds=%d restarts=%d find=%.0f match=%.0f replace=%.0f texts=%.0f carry=%.0f ms",
			storyRef.GetUID().Get(), (int)timedRows, (int)finds, (int)restarts, tFind, tMatch, tReplace, tTexts, tCarry);
#endif
	}
	if (walker->IsWalking())
		walker->Halt();
	return true;
}

// IS THE ROW'S STORED PLACE STILL ITS TEXT? A row is carried and read back only when it is: a replaced row
// must still hold what its replace wrote there (Hit::replacedText), any other row must still read as it was
// found (KFCSearchEngine::RowReadsAsFound - the jump's own test). An edit made since can have moved the rest -
// reading such a row back at its old place takes the line AND the hash from the wrong characters, so a click
// then selects them (measured: "ZZkitt"); left alone, its jump looks for it again
// (KFCSearchEngine::RelocateStaleRow) and says it is no longer here when no one place is found.
bool StoryAsKFCLeftIt(int32 chapterIdx, IDataBase* db, UID story);	// below, with the story versions
bool RowStillStands(int32 chapterIdx, int32 hitIdx, IDataBase* db)
{
	bool replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked))
		return false;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	const bool haveIdentity = KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash);
	if (replaced)
	{
		// A REPLACED ROW STANDS WHERE ITS STORED PLACE STILL HOLDS WHAT ITS REPLACE WROTE (Hit::replacedText). One
		// that does not - an edit since, which KFC does not follow - is left as it is; its jump looks for it again
		// (KFCSearchEngine::RelocateStaleRow).
		PMString written;
		if (!haveIdentity || !KFCResultModel::GetHitWrittenText(chapterIdx, hitIdx, written))
			return false;
		// A REPLACE THAT WROTE NOTHING (an empty Change To) leaves nothing to read back. Its row stands where it was
		// carried to while its story is at the version KFC left it at: every write of KFC's carries every row past it
		// (CarryRowsPast), so the place is exact until something else edits the story. Asked by reading, it was never
		// carried - its line kept the text of the moment it was written, and a write before it left its place behind
		// (cases adjacent-delete, jump-replaced-empty; until 2026-10-06 its tracked change found it).
		if (written.IsEmpty())
			return a == b && StoryAsKFCLeftIt(chapterIdx, db, story);
		return KFCSearchEngine::ReadText(UIDRef(db, story), a, b - a) == written;
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

// THE CHAPTER'S REPLACE. Every row asked for is written by the walk of its story, in the direction the caller
// set - Track Changes as each story has it (KFC records nothing of its own, 2026-10-06 -
// docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F1). Refuses before anything is written -
// returns false, outWhyNot says why - only when the document, the Find/Change options or a row cannot be read.
// outCancelled / outFailed: the caller aborts the whole run.
bool ReplaceInChapterOneByOne(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	KFCProgressBar* progressBar, int32 progressBase, int32& ioProgressReported,
	int32& outReplaced, int32& outMissing, int32& outLocked, int32& outRefused,
	bool& outWalkFailed, bool& outCancelled, bool& outFailed,
	PMString& outWhyNot, const std::set<int32>* onlyHits = nil)
{
	outReplaced = 0;
	outMissing = 0;
	outLocked = 0;
	outRefused = 0;
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
	// two tabs were built and measured on: the caller (ReplaceRowsNow) has already stated the change glyph or the
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
		bool replaced = false, locked = false;
		if (!KFCResultModel::GetHitFlags(chapterIdx, i, replaced, locked))
		{
			outWhyNot = "a row could not be read";
			return false;
		}
		// WHAT THIS RUN WRITES: the rows it was handed (onlyHits - a hit row's Replace) that are not yet replaced
		// and not locked. Every other row is carried, not written: the list stays a work list. (The ticked rows of
		// Change Checked were the other answer until 2026-10-06 - spec F16.)
		const bool target = onlyHits != nil && onlyHits->count(i) != 0 && !replaced && !locked
			&& KFCResultModel::IsWorkOutcome(KFCResultModel::GetHitOutcome(chapterIdx, i));
		if (!target)
		{
			// a locked row the report keeps (it never had a box), a replaced row - and every row, for
			// the one-row Replace: each has to stand where its text is afterwards. ONLY a row whose
			// stored place is still its text (RowStillStands - a replaced row must still hold what it
			// wrote, or, having written nothing, sit in a story as KFC left it): one an edit has moved is left as it
			// is, never read back at a place
			// that is no longer its own.
			// ...and a row with an outcome (missing, refused): the list keeps it, so it is carried
			// like the rest.
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
		// (A match inside or next to the user's own pending insertion is written as InDesign writes it - into the
		//  insertion, as its own Change does (case nt-own-insertion): KFC accepts nothing, 2026-10-06 - spec F1.)
		pendingByStory[story].insert(i);
	}
	// Nothing to write: every row kept was placed a moment ago from where it stands, and nothing has moved
	// since - nothing to write back either.
	if (pendingByStory.empty())
		return true;

	// ===== from here on things are WRITTEN. A cancel or a failure below leaves the caller to abort. =====

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
		// A story an earlier story's replace deleted (an anchored object's): its rows never come up.
		if (db->IsValidUID(s->first))
		{
			if (!WalkStoryReplacing(chapterIdx, UIDRef(db, s->first), scopeOptions, opts, rowNow, pending, keptRows,
				outReplaced, outRefused, outWalkFailed, progressBar, progressBase,
				ioProgressReported, done))
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
	// ----- every row the report keeps, where its text stands now: its range and its line. Read once the
	// chapter has stopped changing: a line read as its own match was written would still show the later
	// matches of its paragraph as they were. (What a replaced row wrote - what its jump looks for it by
	// once it has moved - is taken when it is written, WalkStoryReplacing.)
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
		// The rows the write was handed - the rows ReplaceInChapterOneByOne writes.
		const bool asked = onlyRows != nil && onlyRows->count(i) != 0;
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
	// The walker, and the shared walker's selection utilities the critical section is taken on
	// (KFCSearchEngine::AcquireWalker - KFC's own).
	InterfacePtr<ITextWalker> walker;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils;
	bool ownWalker = false;
	if (opts == nil || !KFCSearchEngine::AcquireWalker(walker, selUtils, ownWalker) || walker == nil)
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
	return CompareQueryWithSearch() == kQueryUnchanged;
}

// ======================================================================================================
// REPLACE ONE ROW, FROM ITS RIGHT-CLICK MENU (the author's call) - since 2026-10-06 the one write from the list
// (spec F16: Change Checked and the story / document rows' Replace are gone). No prompt. The list stays a WORK
// LIST, so the row shows its new text and every other row is carried to where its text now stands, ready for
// the next Replace. One undo step ("Replace"). Its doors: the query unchanged (RefuseChangedQuery) and the verify
// walk's three questions (ChapterMovedUnderRows). Nothing is accepted, and nothing recorded of KFC's own
// (2026-10-06, spec F1).
// ======================================================================================================

bool KFCReplaceEngine::CanReplaceHit(int32 chapterIdx, int32 hitIdx)
{
	bool replaced = false, locked = false;
	return KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked)
		&& !replaced && !locked
		&& KFCResultModel::IsWorkOutcome(KFCResultModel::GetHitOutcome(chapterIdx, hitIdx));
}

// A refusal of ReplaceRowsNow begun with the name of the menu item that asked (the author's call).
static void StartStatus(PMString& out)
{
	out = "Replace: ";
}

// THE ROW MENUS' SEQUENCE - ONE UNDO STEP, ROLLED BACK WHOLE ON A FAILURE. A PLAIN
// sequence, as KCM's own reject is (KCMFacades.cpp, RejectImportChange): an abortable sequence, ended, was
// measured to take the undo step below it away. Its rollback is the SDK's own: raise the error state, end
// it, clear it (CmdUtils.h, SequenceContext) - EndPlainSequence. Begun with the name the Undo menu shows;
// nil = InDesign would not start one, and outStatus then says so after `what` ("Replace: ").
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
// character changed, and asked to be saved on close). Change All's cancel puts the flag back
// (KFCChangeAll); this is the row menu's step's line for it. `wasModified` = the database's flag read
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

// The rows of one chapter replaced now, in ONE undo step - a hit row's Replace (its right-click menu; since
// 2026-10-06 the one write from the list - spec F16). The caller has asked CanReplaceHit of the row.
static bool ReplaceRowsNow(int32 chapterIdx, const std::set<int32>& rowsToReplace, PMString& outStatus,
	const char* unit = "story")
{
	// Forward, as the search was - outside the sequence below (the walk's direction for a GREP query
	// holding ^ is turned below, also outside it).
	KFCForwardSearchScope forward;
	// A changed query CLEARS the results (RefuseChangedQuery) - the action redraws the tree for it
	// (KFCActionComponent RedrawAfterRowMenu). The refusal says which of its three answers it was - not
	// "the query changed" in front of all three, the other tab and a tab that could not be stated included.
	PMString refusal;
	if (KFCReplaceEngine::RefuseChangedQuery(refusal))
	{
		StartStatus(outStatus);
		outStatus.Append(refusal);
		return false;
	}
	if (!KFCSearchEngine::CommitReplaceSide())
	{
		StartStatus(outStatus);
		outStatus.Append("the Change To in Find/Change could not be stated - nothing was changed.");
		return false;
	}
	// The chapter's document, by its file first (a UIDRef can outlive its document - see
	// KFCBookScope::ReachChapterDoc).
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file))
	{
		StartStatus(outStatus);
		outStatus.Append("the document of this row could not be found.");
		return false;
	}
	if (!KFCBookScope::ReachChapterDoc(file, docRef))
	{
		StartStatus(outStatus);
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
		StartStatus(outStatus);
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

	int32 replaced = 0, missing = 0, locked = 0, refused = 0;
	bool walkFailed = false, cancelled = false, failed = false;
	PMString whyNot;
	bool ok = false;
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO. The story versions are read and the row backup started
	// here, outside the sequence; kept below once the rows show the replace - a failure puts them back
	// (RollBackRows).
	KFCUndoFollow::StepRecorder recorder(std::vector<int32>(1, chapterIdx));
	{
		// BACKWARDS FOR THE WRITE ONLY. A query holding ^ is written backwards (WriteBackward), inside this
		// block and nowhere else - not to the end of the function, where a walk after the write would run
		// backwards with it (case caret-row-then-change).
		const KFCBackwardSearchScope writeDirection(WriteBackward());

		ICommandSequence* sequence = BeginPlainSequence(KFCLoc::Text(kKFCReplaceStepKey, KFCJa::kReplaceStep),
			"Replace: ", outStatus);
		if (sequence == nil)
			return false;
		// (the row backup was started by the recorder, above)
		int32 progressReported = 0;
		const bool wrote = ReplaceInChapterOneByOne(chapterIdx, docRef, scopeOptions, nil, 0, progressReported,
			replaced, missing, locked, refused, walkFailed, cancelled, failed, whyNot,
			&rowsToReplace);
		ok = wrote && !failed && !cancelled && replaced == static_cast<int32>(rowsToReplace.size());
		if (ok)
			KFCUndoFollow::MarkWrite(db);	// in this step, so its Undo / Redo is heard
		EndPlainSequence(sequence, ok, db, wasModified);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		StartStatus(outStatus);
		// The reason a row was not written, in that row's words, and how many rows it stopped.
		const char* why = nil;
		int32 stopping = 0;
		bool searchAgain = false;
		if (locked > 0)
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
			outStatus.Append(" checked row(s) ");
			outStatus.Append(why);
			if (searchAgain)
				outStatus.Append(" - search again.");
			else
				outStatus.Append(stopping == 1 ? ". Untick it and Replace again." : ". Untick them and Replace again.");
		}
		return false;
	}
	NoteStoryVersions(chapterIdx, db, writtenStories);	// the next Replace / Reject finds them as KFC left them
	// Kept AFTER the versions are noted - they are the "after" an Undo's "before" is put back over.
	recorder.Keep(KFCUndoFollow::kStepReplace);
	chapterAfter.wrote = true;		// written to: a chapter of ours has to be seen and saved (ChapterAfter)
	// (What each row wrote - Hit::replacedText - is taken by the walk that writes it, WalkStoryReplacing.)
	// WHAT IT DID (2026-10-06, spec F16 / F19 / F20): the row's Replace is the one write from the list, one undo step.
	// The message says nothing of Ctrl+Z (the author's call); the Track Changes wording went with Reject Change.
	// ★WHICH ROW, IN THE MESSAGE (2026-10-07, the author: replacing row after row with Return, every message read the
	// same "Replaced." and which one had just happened could not be told). The author's choice: the story's UID and the
	// hit's place among that story's results - "Replaced ID:262 #3." (the author's spelling) - the story row and its third hit row
	// in the list (the tree's middle level is one group per story, BuildFontGroups; GetHitFontGroupPos is the hit's
	// place under it, from 0), and the UID a script reaches the story by (stories.itemByID). A story UID is the
	// document's own number: in a book the chapter row above says which document.
	outStatus = "Replaced.";
	if (rowsToReplace.size() == 1)
	{
		const int32 hitIdx = *rowsToReplace.begin();
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		const int32 place = KFCResultModel::GetHitFontGroupPos(chapterIdx, hitIdx);
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash) && story != kInvalidUID && place >= 0)
		{
			outStatus = "Replaced ID:";
			outStatus.AppendNumber(static_cast<int32>(story.Get()));
			outStatus.Append(" #");
			outStatus.AppendNumber(place + 1);
			outStatus.Append(".");
		}
	}
	outStatus.SetTranslatable(kFalse);
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

// End, KFCReplaceEngine.cpp.
