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
#include "ICommandSequence.h"		// the write's undo step (BeginWriteStep) and the preview's, thrown away
#include "IDataBase.h"				// IsModified / SetModified - putting the flag back after a rollback
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"
#include "IAttrReport.h"			// IsParagraphAttribute - a Change side that sets paragraph formatting (TakeRowCopy)
#include "IAttributeStrand.h"		// a paragraph's style and own overrides, kept and put back (TakeRowCopy, PutParagraphBack)
#include "IStyleInfo.h"				// a kept paragraph style still one of the document's (ParagraphStyleStands)
#include "IComposeScanner.h"		// FindSurroundingParagraph - the paragraph of a copy (ParagraphOf)
#include "ICreateOutlinesUtils.h"		// CreateCOTextCmd - a text row's Create Outlines (OutlineHits)
#include "ITextModelCmds.h"			// ApplyStyleCmd / ApplyCmd - a paragraph's formatting put back (PutParagraphBack)
#include "IHyperlinkUtils.h"		// hyperlink sources a copy would cut or carry (TakeRowCopy) - source/open/interfaces/layout
#include "HyperlinkID.h"			// IID_IHYPERLINKTABLE - the document's hyperlinks (LinksOn, PutLinksBack)
#include "IHyperlink.h"				// a link's destination (PutLinksBack)
#include "IHyperlinkFacade.h"		// a source's range set, a link and its source deleted, a destination let go (PutLinksBack)
#include "IHyperlinkTable.h"
#include "IHyperlinkTextSourceData.h"	// a text source's story and its range reference (LinksOn)
#include "itextRangeNode.h"			// where a text source's range stands (LinksOn)

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
#include "Utils.h"
#include "AttributeBossList.h"		// the Change side's attributes (TakeRowCopy)
#include "TextChar.h"				// kTextChar_CR, the table's characters (TakeRowCopy)
#include "textiterator.h"			// a range read character by character (TakeRowCopy)

#include <algorithm>		// std::sort - Restore from Copy's rows in text order
#include <map>
#include <set>
#include <vector>

// Project includes:
#include "KFCReplaceEngine.h"
#include "KFCDiag.h"			// KFC_DIAG_LOG - why a write was refused, in a test build (compiled out of a shipping one)
#include "KFCDiagWrittenDocs.h"	// the test build's written-document check (nothing in a shipping one)
#include "KFCID.h"				// kKFCReplaceStepKey - the string key the Undo step is named from
#include "KFCResultModel.h"
#include "KFCRowFoci.h"			// ReanchorStory - the rows' text foci put back after a write, its rollback and a preview
#include "KFCRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KFCSearchEngine.h"	// the shared walker scope and the line-splitting the rows use
#include "KFCBookScope.h"		// reopening a chapter the user closed since the search
#include "KFCUndoFollow.h"		// every write recorded, so that the panel follows its Undo and Redo
#include "KFCObjectReplace.h"	// an object row's Replace
#include "KFCQRSearch.h"		// NotChangedWords - a QR code list's rows are never replaced
#include "KFCCopyStore.h"		// the copies a row's Replace takes and its Restore from Copy puts back
#include "KFCTableSearch.h"		// a table row's Replace - the Table tab's Change side
#include "KFCRubySearch.h"		// a ruby row's Replace - the Ruby tab's
#include "KFCKentenSearch.h"	// a kenten row's Replace - the Kenten kind's
#include "IKentenStyle.h"		// Kenten_None - Remove Kenten
#include "KFCQueryCopy.h"		// rubyKind - a list of ruby marks
#include "ITableModel.h"
#include "KFCTextOverrides.h"	// ClearOverrides - a text row's Special Operations > Clear Overrides
#ifdef KFC_DIAG
#include "ITextFocusManager.h"	// how many foci a story carries while it is written (WALKSTEP) - test builds only
#include "IFrameList.h"			// whether its frames stand damaged around a find (WALKSTEP) - test builds only
#endif

namespace
{

// HOW MANY WRITES FROM THE LIST ARE UNDER WAY - KFCReplaceEngine::WriteUnderWay's depth (IsReplacing - KFCRunGuard
// counts it; the header says who holds one).
int32 gReplacingDepth = 0;

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
//                    function: that answer belongs to kReplaceAllTextCmdBoss, which this engine does not
//                    run (KFCChangeAll does, for Change All in Book and the query run) - the official
//                    Change All takes no subset of the matches, which is the whole reason a row's
//                    Replace walks match by match. Listed anyway because the enum
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

	// NOTHING PROCESSED OVER AN ERROR ALREADY STANDING (MarkWrite's rule): a command processed while the global error code is
	// set is InDesign's protective shutdown (CmdUtils.h, ProcessCommand) - and a find that answered kSuccess is not cleared
	// below. The error is not this function's to clear: it is answered as a walk that broke off, and the step it is in
	// ends by it and goes back.
	if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		return IFindChangeService::kFailure;

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

// THE SAME-OCCURRENCE TEST IS NOT IN THE WRITING WALK - IT STANDS BEFORE THE WRITE, AND AT EVERY DOOR.
//
// Inside the walk - is the match the walk has landed on the one the row describes: same story, same
// position (our own replacements cancelled out), same text - it cannot work across a BOOK, where a
// chapter may be closed between the search and the replace; keeping the document steady between
// searching and replacing is the USER's responsibility (the author's decision).
//
// What it guards against is real: the walk order alone cannot tell "the Nth match" from "a DIFFERENT
// Nth match". An edit made between the search and the replace that removes one match and adds another
// keeps the COUNT intact, so every row asked for still comes up and nothing looks wrong - while the
// numbering points at text the user never chose.
//
// SO THE SAME QUESTION IS ASKED BEFORE THE WRITE STARTS, AND IT REFUSES (the author's design). The verify
// walk - ChapterMovedUnderRows - walks what it is about to write to and checks that the row's story is at
// the version on record, that the row still reads as found, and that a match still BEGINS at its place
// with its length. That is the one moment where the question is both answerable and free: nothing has
// been written, so the positions are the ones the rows carry, with no replacements of ours to cancel out
// - and a mismatch stops the write with not one character to take back.
//
// Not a per-chapter fingerprint (every story's ITextModel::GetChangeCount, warning rather than
// refusing): that answers a DIFFERENT question - "does this chapter look untouched?" - which has to
// enumerate the ways a document can move (text, stories, layers, locks, conditions...) and is never
// finished. Walking asks about the thing itself. (The story version asked first covers only the stories
// holding a row asked for, and it refuses - A STORY'S VERSION, below.)
//
// So the walk below needs no same-occurrence test of its own: by the time it runs, every position asked for
// has been confirmed and nothing has moved since (the verify walk writes nothing). It still finds each
// row by its place (RowOfMatchAnyOrder).
//
// The test and the hash behind it live on in KFCSearchEngine::RowReadsAsFound, which every door asks -
// the JUMP, which is how clicking a row can answer "the replacement is no longer here" instead of
// scrolling to whatever now sits at that position; RowStillStands, for every row carried past a write; and the
// verify walk (ChapterMovedUnderRows) before the row's Replace.

// Where one of the chapter's rows stands in the text NOW - where the search found it, carried past
// every replacement this pass has made since - or, for a row this pass replaced, where its new text
// stands. One per row, indexed like the model's rows. The replace walk asks it two things: which
// row a match it has just found IS (RowOfMatchAnyOrder), and, once the walk is over, where each row
// the list keeps has ended up.
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
// "cat two"), rows "marker 1" and "cat" of fn2 written in one run - the marker's replacement took fn1's six
// characters out as well, fn2's "cat" came up six places earlier than a story index carried by the
// replaced length alone, and it was stepped over as a match nobody had listed and reported missing.
// A thread knows where it starts NOW (ITextModel::FindStoryThread, by the thread's own identity),
// so an offset into it is carried only by replacements inside that thread, and every other move -
// a thread before it growing, shrinking or disappearing - comes with the thread's start for free.
//
// THE THREAD IS ONE VALUE: its story and the thread's own identity (ITextStoryThread::GetDictUID/Key), compared
// whole - "the same thread" is asked in one place, and a dictionary is never paired with another thread's key.
struct RowThread
{
	UID		story;
	UID		dict;
	uint32	key;

	RowThread() : story(kInvalidUID), dict(kInvalidUID), key(0) {}
	bool operator==(const RowThread& other) const { return story == other.story && dict == other.dict && key == other.key; }
	bool operator!=(const RowThread& other) const { return !(*this == other); }
};

struct RowNow
{
	bool		known;		// false: the row's identity could not be read (no story, no range)
	RowThread	thread;		// the thread the row's text lives in
	TextIndex	offset;		// start, from the start of that thread
	int32		length;

	RowNow() : known(false), offset(0), length(0) {}
};

// Which thread holds `at` in this story, and where it starts. False when the story or the thread
// cannot be read.
bool ThreadAt(IDataBase* db, UID story, TextIndex at, RowThread& outThread, TextIndex& outThreadStart)
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
	outThread.story = story;
	outThread.dict = thread->GetDictUID();
	outThread.key = thread->GetDictKey();
	outThreadStart = threadStart;
	return true;
}

// Put a row at [start, end) of `story` as the text stands at this moment.
void SetRowAt(IDataBase* db, RowNow& row, UID story, TextIndex start, TextIndex end)
{
	row.known = false;
	RowThread thread;
	TextIndex threadStart = kInvalidTextIndex;
	if (story == kInvalidUID || !ThreadAt(db, story, start, thread, threadStart))
		return;
	row.known = true;
	row.thread = thread;
	row.offset = start - threadStart;
	row.length = end - start;
}

// Where the row starts now. False when its thread is gone (the replacement that took a footnote
// away took the rows in it too) or cannot be read.
bool RowStartNow(IDataBase* db, const RowNow& row, TextIndex& outStart)
{
	if (!row.known || db == nil)
		return false;
	InterfacePtr<ITextModel> model(db, row.thread.story, UseDefaultIID());
	if (model == nil)
		return false;
	TextIndex threadStart = kInvalidTextIndex;
	if (!model->FindStoryThread(row.thread.dict, row.thread.key, &threadStart, nil))
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
void CarryRowsPast(std::vector<RowNow>& rows, const RowThread& thread, TextIndex oldStartOffset, TextIndex oldEndOffset,
	TextIndex newLength)
{
	const TextIndex delta = newLength - (oldEndOffset - oldStartOffset);
	if (delta == 0)
		return;
	for (size_t i = 0; i < rows.size(); ++i)
	{
		RowNow& row = rows[i];
		if (!row.known || row.thread != thread || row.offset < oldEndOffset)
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
// (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on the match it made current), a story at a time, with Track Changes as
// each story has it - KFC records nothing of its own (the no-track spec's F1). (Not Change All - see above.)
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
// and the row is left and reported missing rather than written with text nobody chose.
//
// outThread / outThreadStart = the thread the match is in, and where it starts - what the caller
// carries the other rows past once the row is written (read once per match, here).
int32 RowOfMatchAnyOrder(IDataBase* db, const std::vector<RowNow>& rowNow, const std::set<int32>& pending,
	UID story, TextIndex start, TextIndex end, RowThread& outThread, TextIndex& outThreadStart)
{
	if (!ThreadAt(db, story, start, outThread, outThreadStart))
		return -1;
	const TextIndex offset = start - outThreadStart;
	const int32 length = end - start;
	for (std::set<int32>::const_iterator it = pending.begin(); it != pending.end(); ++it)
	{
		const RowNow& row = rowNow[static_cast<size_t>(*it)];
		if (row.known && row.thread == outThread && row.offset == offset && row.length == length)
			return *it;
	}
	return -1;
}

// WHAT ONE CHAPTER'S WRITE DID - ReplaceInChapterOneByOne's answer: one value, handed down to the story walks that add
// to it and added up whole across the chapters of a step (ReplaceRowsNow) - a field added here goes wherever the answer
// goes.
struct ChapterOutcome
{
	int32		replaced;		// rows written
	int32		missing;		// rows asked for that no walk came to, or whose place could not be read
	int32		locked;			// rows asked for whose frame is locked since the search
	int32		refused;		// rows InDesign's replace would not write
	bool		walkFailed;		// a walk started and broke off before it came to every row asked for
	bool		failed;			// a walk could not be started - the caller rolls the step back
	PMString	whyNot;			// a refusal before anything was written, or the walk that could not start

	ChapterOutcome() : replaced(0), missing(0), locked(0), refused(0), walkFailed(false), failed(false) {}

	// Another chapter's, added on: the counts summed, a failure kept, the first reason kept.
	void Add(const ChapterOutcome& other)
	{
		replaced += other.replaced;
		missing += other.missing;
		locked += other.locked;
		refused += other.refused;
		walkFailed = walkFailed || other.walkFailed;
		failed = failed || other.failed;
		if (whyNot.IsEmpty())
			whyNot = other.whyNot;
	}
};

// One story's walk: every pending row the walk meets is written, one at a time, in the direction the session is set to
// (the caller's KFCBackwardSearchScope). What the walk writes moves every row after it in the same thread
// (CarryRowsPast); a written row is put where its new text stands - an endnote's last match too, written as InDesign
// writes it (the no-track spec's F12). What is still in `pending` at the end never came up. False = the walk could not
// start at all; io.walkFailed = it started and broke off.
bool WalkStoryReplacing(int32 chapterIdx, const UIDRef& storyRef, const WalkerScopeOptions& scopeOptions,
	IFindChangeOptions* opts, std::vector<RowNow>& rowNow, std::set<int32>& pending, std::vector<int32>& keptRows,
	ChapterOutcome& io)
{
	IDataBase* const db = storyRef.GetDataBase();
	// The walker, and the shared walker's selection utilities the critical section is taken on
	// (KFCSearchEngine::AcquireWalker - the session's shared one, as the SDK's snippet walks).
	InterfacePtr<ITextWalker> walker;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils;
	const bool haveWalker = KFCSearchEngine::AcquireWalker(walker, selUtils);
	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(storyRef, scopeOptions));
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (db == nil || !haveWalker || walker == nil || scope == nil || client == nil)
		return false;
	if (walker->IsWalking())
		walker->Halt();
	walker->Initialize(client, scope, opts, nil);
	{
		const KFCSearchEngine::WalkerStops stops(walker);	// halted on every way out of this block
		if (selUtils == nil)
			return false;
		const TextWalkerSelections_CriticalSection criticalSection(selUtils);
#ifdef KFC_DIAG
		// TEST BUILDS ONLY: where a story's walk spends its time (measured when the query run still wrote through this
		// walk: 6000 rows in two 3000-row stories took 23 minutes; each story was ONE paragraph, which every find
		// recomposed whole - in short paragraphs a row costs about 7 ms, flat). Summed per phase over one story's walk and
		// written once (WALKTIME); every 100 finds the last 100's time, with the foci the story carries (WALKSTEP - does
		// something pile up?).
		double tFind = 0, tMatch = 0, tReplace = 0, tTexts = 0, tCarry = 0;
		double tFindStep = 0;
		int32 timedRows = 0, finds = 0;
		KFCDiagPerf stepPerf;		// InDesign's counters over the last 100 finds (KFCDiag.h)
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
#ifdef KFC_DIAG
			// (Fault switch replace-walk-fail, a test build's only - KFCDiag.h: InDesign's find breaks off with an error at
			// once, the one way a test reaches a walk that stops before the row.)
			if (KFC_DIAG_FAULT("replace-walk-fail"))
			{
				io.walkFailed = true;
				break;
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
				KFC_DIAG_LOG("WALKSTEP story=%u finds=%d rows=%d last100=%.1f ms/find foci=%d length=%d at=%d walker=%p damaged=%d->%d %s",
					storyRef.GetUID().Get(), (int)finds, (int)timedRows, tFindStep / 100.0,
					(stepFoci != nil) ? (int)stepFoci->GetFocusCount() : -1,
					(stepModel != nil) ? (int)stepModel->TotalLength() : -1, (int)start, (void*)walker.get(),
					(int)damagedBefore, (stepFrames != nil) ? (int)stepFrames->GetFirstDamagedFrameIndex() : -3,
					stepCounters);
				tFindStep = 0;
				stepPerf.Take();
			}
#endif
			if (found != IFindChangeService::kSuccess)
			{
				if (found == IFindChangeService::kFailure)
					io.walkFailed = true;
				break;
			}
			if (story.GetUID() == lastStory && start >= lastStart && start < lastEnd)
				continue;
			// The thread and the offset into it BEFORE the command - what every row's place is kept in.
			RowThread matchThread;
			TextIndex matchThreadStart = kInvalidTextIndex;
			KFC_CLOCK(cMatch);
			const int32 hitIdx = RowOfMatchAnyOrder(db, rowNow, pending, story.GetUID(), start, end,
				matchThread, matchThreadStart);
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
				++io.replaced;
#ifdef KFC_DIAG
				++timedRows;
#endif
				lastStory = written.GetUID();
				lastStart = writtenStart;
				lastEnd = writtenEnd;
				// WHAT IT WROTE, TAKEN HERE - once (Hit::replacedText): the jump looks for a moved row by it
				// (KFCSearchEngine::LocateRow). The text written now is the text the row holds at the end: no
				// later replace of the run writes inside it, since every other row is carried outside it and a match
				// there is none of theirs (stepped over, above).
				KFC_CLOCK(cTexts);
				KFCResultModel::SetHitWrittenText(chapterIdx, hitIdx,
					KFCSearchEngine::ReadText(written, writtenStart, writtenEnd - writtenStart));
				KFCResultModel::MarkHitReplaced(chapterIdx, hitIdx, written.GetUID(), writtenStart, writtenEnd);
				KFC_SPENT(tTexts, cTexts);
				KFC_CLOCK(cCarry);
				// every row after it first, then this row at what was written - so it is not moved by itself
				CarryRowsPast(rowNow, matchThread, start - matchThreadStart, end - matchThreadStart,
					writtenEnd - writtenStart);
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
				++io.refused;
				KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeRefused);
				KeepRowAt(db, rowNow, keptRows, hitIdx, story.GetUID(), start, end);
			}
		}
#ifdef KFC_DIAG
		KFC_DIAG_LOG("WALKTIME story=%u rows=%d finds=%d restarts=%d find=%.0f match=%.0f replace=%.0f texts=%.0f carry=%.0f ms",
			storyRef.GetUID().Get(), (int)timedRows, (int)finds, (int)restarts, tFind, tMatch, tReplace, tTexts, tCarry);
#endif
	}
	// The story's first words, read again for its row (the author's call): what this walk wrote can be among them. Each
	// row is backed up before it takes them (KFCResultModel::SetStoryLead), so a preview's rollback, an Undo and a Redo
	// put the words back with the rows.
	KFCSearchEngine::RereadStoryLead(chapterIdx, storyRef);
	return true;
}

// IS THE ROW'S STORED PLACE STILL ITS TEXT? A row is carried and read back only when it is: a replaced row
// must still hold what its replace wrote there (Hit::replacedText), any other row must still read as it was
// found (KFCSearchEngine::RowReadsAsFound - the jump's own test). An edit made since can have moved the rest -
// reading such a row back at its old place takes the line AND the hash from the wrong characters, so a click
// then selects them (measured: "ZZkitt"); left alone, its jump looks for it again
// (KFCSearchEngine::LocateRow) and says it is no longer here when no one place is found.
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
		// (KFCSearchEngine::LocateRow).
		PMString written;
		if (!haveIdentity || !KFCResultModel::GetHitWrittenText(chapterIdx, hitIdx, written))
			return false;
		// A REPLACE THAT WROTE NOTHING (an empty Change To) leaves nothing to read back. Its row stands where it was
		// carried to while its story is at the version KFC left it at: every write of KFC's carries every row past it
		// (CarryRowsPast), so the place is exact until something else edits the story. Asked by reading, it was never
		// carried - its line kept the text of the moment it was written, and a write before it left its place behind
		// (cases adjacent-delete, jump-replaced-empty).
		if (written.IsEmpty())
			return a == b && StoryAsKFCLeftIt(chapterIdx, db, story);
		return KFCSearchEngine::ReadText(UIDRef(db, story), a, b - a) == written;
	}
	// The match AND the line around it: a row an Undo left on another occurrence of its own text passes the
	// match's hash alone, and would then be read back THERE - from then on describing that other
	// occurrence, with a line to match.
	return KFCSearchEngine::RowReadsAsFound(chapterIdx, hitIdx, db);
}

// A ROW A WRITE DOES NOT TOUCH - every other row of a Replace's chapter, of a Restore from Copy's, of a
// Create Outlines': carried and read back only when its stored place is still its text (RowStillStands). One an edit
// has moved is left as it is, never read back at a place that is no longer its own: its place is taken only here, after
// RowStillStands, so a row that does not stand stays unknown - neither carried nor read back. (RowStillStands only
// asks; it moves no row.)
void KeepRowIfItStands(int32 chapterIdx, int32 hitIdx, IDataBase* db, std::vector<RowNow>& rowNow,
	std::vector<int32>& keptRows)
{
	if (!RowStillStands(chapterIdx, hitIdx, db))
		return;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash))
		SetRowAt(db, rowNow[static_cast<size_t>(hitIdx)], story, start, end);
	keptRows.push_back(hitIdx);
}

// EVERY ROW THE LIST KEEPS, WHERE ITS TEXT STANDS NOW - its range and its line, read once the chapter has stopped
// changing: a line read as its own match was written would still show the later matches of its paragraph as they were.
// A row whose thread went with an object a write took away is left as it is.
void ReadBackRows(int32 chapterIdx, IDataBase* db, const std::vector<RowNow>& rowNow, const std::vector<int32>& rows)
{
	for (size_t k = 0; k < rows.size(); ++k)
	{
		const RowNow& kept = rowNow[static_cast<size_t>(rows[k])];
		TextIndex keptStart = kInvalidTextIndex;
		if (!RowStartNow(db, kept, keptStart))
			continue;
		const TextIndex keptEnd = keptStart + kept.length;
		KFCResultModel::SetHitRange(chapterIdx, rows[k], kept.thread.story, keptStart, keptEnd);
		KFCSearchEngine::RereadRowText(chapterIdx, rows[k], UIDRef(db, kept.thread.story), keptStart, keptEnd);
	}
}

// ======================================================================================================
// A STORY'S VERSION (the author's call: "safety first"). A row's place is carried past every change KFC makes
// and past nothing else: typing, Ctrl+Z / Ctrl+Shift+Z, the Track Changes panel or a script can move the text
// under it, and the place can then stand on ANOTHER occurrence of the same text, which the match's hash cannot
// tell apart ("catcatcatcat", row 1 replaced from its menu, Ctrl+Z: rows 2 and 3 were left on the third and
// fourth "cat", and a replace wrote there - before this door stood). InDesign keeps a version of every
// story (ITextModel::GetChangeCount - moved by any change to its text, attributes, tables or inlines, and moved
// BACK by Undo to exactly the value it had): the search records it for every story holding a hit, each change
// KFC makes records the new one, and nothing is written to a story whose version is not the one recorded. The
// price, accepted: an edit ANYWHERE in such a story between the search and the replace means searching again.
// EXCEPT AN UNDO OR A REDO OF A WRITE OF KFC'S OWN (KFCUndoFollow). The panel follows those: the rows AND
// the versions recorded here are put back as they were on that side of the write, so the story is at the
// version on record again and the rows stand where their text does. The example above ("catcatcatcat",
// Ctrl+Z) is such an Undo and is followed;
// typing, an Undo of anything else, the Track Changes panel and a script still stop the write.
// OVERTURNED FOR ROWS THAT FOLLOW THEIR FOCI (the author's call -
// docs/superpowers/plans/2026-10-14-kfc-row-focus-undo.md): a row's text focus is of KFC's own class now, and the
// user's Undo and Redo put it back (KFCRowFocus.h). Before every write the rows of each story take their foci's places
// and the story its version as it stands (KFCSearchEngine::FollowStoryFoci, in ReplaceRowsNow), so typing, an Undo of
// anything, the Track Changes panel, a script and InDesign's own Change All no longer stop it; a row whose own text was
// edited is Missing. This version rule still decides for a story whose rows have no foci - and a story with a focus
// that could not follow (an Undo of an edit older than the search) is refused whatever its version says.
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
// set - Track Changes as each story has it (KFC records nothing of its own -
// docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F1). Refuses before anything is written -
// returns false, out.whyNot says why - only when the document, the Find/Change options or a row cannot be read.
// out.failed: the walk could not be started - the caller rolls the step back. `onlyHits` = the rows to write.
bool ReplaceInChapterOneByOne(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	const std::set<int32>& onlyHits, ChapterOutcome& out)
{
	out = ChapterOutcome();
	out.whyNot.SetTranslatable(kFalse);

	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (db == nil || opts == nil)
	{
		out.whyNot = "the document or the Find/Change options could not be read";
		return false;
	}
	// GLYPH AND TRANSLITERATE ARE WRITTEN TOO (the author's call). One match at a time is the walk those
	// two tabs were built and measured on: the caller (ReplaceRowsNow) has already stated the change glyph or the
	// character type the command writes (KFCSearchEngine::CommitReplaceSide - PreviewHit writes on the GREP tab
	// only, where there is nothing to state), and the Colour tab is refused before any of this. (An Object row never comes
	// here - ReplaceHit and ReplaceHits send it to KFCObjectReplace.)

	// Every row's place now (RowNow) - the ones asked for are what the walks look for, and every row is
	// carried past what they write, for the read-back at the end.
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	std::vector<RowNow> rowNow(static_cast<size_t>(hitCount > 0 ? hitCount : 0));
	std::vector<int32> keptRows;						// the rows the list keeps, read back at the end
	std::map<UID, std::set<int32> > pendingByStory;		// the rows asked for that can be written, story by story
	std::map<std::pair<UID, UID>, bool> editableFrames;
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool replaced = false, locked = false;
		if (!KFCResultModel::GetHitFlags(chapterIdx, i, replaced, locked))
		{
			out.whyNot = "a row could not be read";
			return false;
		}
		// WHAT THIS RUN WRITES: the rows it was handed (onlyHits - a hit row's Replace) that are not yet replaced
		// and not locked. Every other row is carried, not written: the list stays a work list.
		const bool target = onlyHits.count(i) != 0 && !replaced && !locked
			&& KFCResultModel::IsWorkOutcome(KFCResultModel::GetHitOutcome(chapterIdx, i));
		if (!target)
		{
			// Every other row - replaced, locked, with an outcome or not - has to stand where its text is afterwards:
			// only one whose stored place is still its text (KeepRowIfItStands).
			KeepRowIfItStands(chapterIdx, i, db, rowNow, keptRows);
			continue;
		}
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		uint64 hash = 0;
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, i, story, start, end, hash))
			SetRowAt(db, rowNow[static_cast<size_t>(i)], story, start, end);
		if (!rowNow[static_cast<size_t>(i)].known)
		{
			++out.missing;
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
			++out.locked;
			KFCResultModel::SetHitOutcome(chapterIdx, i, KFCResultModel::kOutcomeLocked);
			keptRows.push_back(i);
			continue;
		}
		// (A match inside or next to the user's own pending insertion is written as InDesign writes it - into the
		//  insertion, as its own Change does (case nt-own-insertion): KFC accepts nothing - the no-track spec's F1.)
		pendingByStory[story].insert(i);
	}
	// Nothing to write: every row kept was placed a moment ago from where it stands, and nothing has moved
	// since - nothing to write back either.
	if (pendingByStory.empty())
		return true;

	// ===== from here on things are WRITTEN. A failure below leaves the caller to roll the step back. =====

	for (std::map<UID, std::set<int32> >::iterator s = pendingByStory.begin(); s != pendingByStory.end(); ++s)
	{
		std::set<int32>& pending = s->second;
		// A story an earlier story's replace deleted (an anchored object's): its rows never come up.
		if (db->IsValidUID(s->first))
		{
			if (!WalkStoryReplacing(chapterIdx, UIDRef(db, s->first), scopeOptions, opts, rowNow, pending, keptRows, out))
			{
				out.failed = true;
				out.whyNot = "the text walker could not be started";
				return true;
			}
		}
		// A row asked for that the walk never came to is missing. (Several rows of a document can be written in one go -
		// ReplaceRowsNow, the rows selected together: one whose thread an earlier row's write took away, with a
		// footnote or an object, is missing here, and that caller rolls the whole step back.)
		for (std::set<int32>::const_iterator p = pending.begin(); p != pending.end(); ++p)
		{
			++out.missing;
			KFCResultModel::SetHitOutcome(chapterIdx, *p, KFCResultModel::kOutcomeMissing);
		}
	}
	// ----- every row the report keeps, where its text stands now (ReadBackRows). (What a replaced row wrote - what its
	// jump looks for it by once it has moved - is taken when it is written, WalkStoryReplacing.)
	ReadBackRows(chapterIdx, db, rowNow, keptRows);
	return true;
}

// DOES THE CHAPTER STILL HOLD WHAT THE ROWS ASKED FOR DESCRIBE? (the author's design)
// The Replace asks it before a character is written. Every row carries the place its match stands at -
// where the search found it, carried past every change KFC has made since - and this walks every story
// that holds a row asked for the way the writing walk will (WalkStoryReplacing: the same story scope, the
// same options, from the top of the story), asking THREE QUESTIONS of every such row (below): is its story
// at the version KFC left it at, does the row still read as it was found (KFCSearchEngine::RowReadsAsFound
// - the jump's own test), and does a match of the walk still stand at its start and length? A row that
// fails any of them, or one the walk never reaches, means the document is not the one the results
// describe, and the caller stops before a character is written. It writes nothing, marks no row and moves
// no bar.
//
// BY PLACE AND TEXT, NOT BY COUNT (the author's call: safe, even if slower). Lining the Nth match of the
// walk up with the row numbered N, reading no text, is wrong on both sides of what the writing walk does -
// it writes only a match standing at a row's thread, offset and length (RowOfMatchAnyOrder):
//   - a row whose text was changed to other text the query also matches (GREP \d+ over "123" edited to
//     "456"), with as many matches before it as there were, passes the count and is WRITTEN - text
//     nobody chose. The text is asked, and the write stops;
//   - a match added or removed somewhere before the row, the row itself untouched, fails the count and
//     stops the write for nothing.
//   ! PLACE AND TEXT ALONE ARE NOT ENOUGH EITHER: "where it was" is an index, and an edit KFC did not see
//     (above all Ctrl+Z) can leave that index on ANOTHER occurrence of the same text, which place and text
//     pass. Hence the story's version and the row's line, asked first (below) - so any edit in the story
//     stops the write. (An Undo or a Redo of a write of KFC's own is SEEN - KFCUndoFollow puts the rows and
//     the versions back with it; the doors below are for every other edit.)
// Numbers would also have to be kept up: every row's Replace would walk the whole chapter again to number
// the rows afresh - with no tab stated, so a Replace made with the dialog on another tab would number the
// rows by that tab's matches.
//
// The END of a match is compared too, not left to the writing walk (which asks the length, and would
// leave a row whose match ran longer or shorter as missing): such a row stops the write before anything
// is written.
//
// STORY BY STORY, AS THE WRITING WALK GOES - not the whole chapter (QueryDocumentWalkerScope, every story):
// the walk that writes goes one story at a time (QueryStoryWalkerScope). A row is found by its place, not
// by how many matches came before it, so the stories holding no row asked for have nothing to say; and
// asking with the writing walk's own scope means what is checked is what will be met.
//
// A walk that cannot START (no database, no options, no walker, no scope) answers false - nothing was
// compared, and the writing walk meets the same failure and stops ("the text walker could not be
// started"). A walk that starts and then breaks off answers true through the rows it never reached: no
// position is written that could not be checked. The refusal then says the row changed, which is the
// safe answer if not the precise one.
//
// (The chapter walk this grew out of - ReplaceInChapter, which wrote match by match, with a verify-only
// mode - is in git history: 8bf650d and before.)
//
// THREE QUESTIONS, EACH ONE ENOUGH TO STOP THE WRITE (the author's call: "safety first"). "Place and text"
// alone passes a row an Undo or the user's typing has left on ANOTHER occurrence of its own text -
// "catcatcatcat", row 1 replaced from its menu, Ctrl+Z, rows 2 and 3 written next: both stood on the next
// "cat", and were written there (measured, before these doors). (That Ctrl+Z - of a write of KFC's own - is
// followed, KFCUndoFollow: the rows go back to where the text is, and rows 2 and 3 are written where they
// stand. The three questions stand for every other edit.) So, for every row:
//   1. its STORY is at the version KFC last recorded for it (StoryAsKFCLeftIt - ITextModel::GetChangeCount,
//      which Undo moves back): any change KFC did not make, anywhere in the story, stops the write - unless the
//      rows followed their foci first (ReplaceRowsNow - KFCSearchEngine::FollowStoryFoci records the version as it
//      stands for a story it followed);
//   2. the row still READS as it was found - the whole match AND the line around it
//      (KFCSearchEngine::RowReadsAsFound), for a version that has come back to the same number;
//   3. the walk meets a match with the row's start AND its length.
// `onlyRows` = the rows the Replace is about to write (asked the same three, not the match's hash alone).
// `outWhy` (the author's call) = which of the three stopped it, for the refusal to say: question 1 is about the
// whole story - an edit anywhere in it - and is not "the text of this row has changed".
enum MovedWhy
{
	kMovedRow,		// questions 2 and 3, or a row whose identity cannot be read: the row itself
	kMovedStory		// question 1: the row's story was changed by something other than KFC
};

bool ChapterMovedUnderRows(int32 chapterIdx, const UIDRef& docRef, const WalkerScopeOptions& scopeOptions,
	const std::set<int32>& onlyRows, MovedWhy& outWhy)
{
	outWhy = kMovedRow;
	// The DATABASE first, the way the search asks it (KFCSearchEngine's CollectHitsInDoc). It is NOT a
	// liveness test - a UIDRef carries the IDataBase* itself, and "is this document still open?" has one
	// honest answer in KFC (KFCBookScope::IsDocStillOpen); what this catches is a UIDRef that never had one.
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// Where the rows stand, story by story: story -> ((start, end) -> how many of them stand there - a
	// zero-width GREP match and a wider one can share a start). A row whose story is gone or has moved
	// without KFC, whose text or line is not the one that was found, or whose identity cannot be read is a
	// change like any other - "cannot tell" is not good enough to rewrite the user's text on.
	std::map<UID, std::map<std::pair<TextIndex, TextIndex>, int32> > waiting;
	std::map<UID, int32> waitingInStory;
	std::map<UID, bool> storyAsLeft;		// the version question, asked once per story
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		// The rows the write was handed - the rows ReplaceInChapterOneByOne writes.
		if (onlyRows.count(i) == 0)
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
			outWhy = kMovedStory;
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
	// (KFCSearchEngine::AcquireWalker - the session's shared one, as the SDK's snippet walks).
	InterfacePtr<ITextWalker> walker;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils;
	if (opts == nil || !KFCSearchEngine::AcquireWalker(walker, selUtils) || walker == nil)
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

		// EVERY EXIT PAST Initialize HALTS (KFCSearchEngine::WalkerStops says why). The refusal in the walk below is
		// the likeliest exit of all: editing the document between the search and the replace is the ordinary way a
		// run ends here.
		const KFCSearchEngine::WalkerStops stops(walker);
		if (selUtils == nil)
			return false;

		int32& left = waitingInStory[s->first];
		{
			// Required critical section around text-walker selection changes, held for the whole story - the
			// same deliberate departure from Adobe's examples that KFCSearchEngine explains: by spellpanel's own
			// hedged note (SpellCheckWalker.cpp:85, "hmm same code as") its contents are the keyboard-focus
			// hand-off, so entering it per match would run that dance once per match.
			const TextWalkerSelections_CriticalSection criticalSection(selUtils);

			// How the walk moves forward is the walker's business alone: each find advances it to the next
			// match, as the official loop runs it (SnpFindAndReplace), and the walk ends when every row asked for
			// in the story has been met, when the find says there is nothing more - or when it breaks off
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
		// A row asked for that the walk never reached is as much a change as one that moved: the match the results
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
	// ----- (0) A QR CODE LIST (KFCQRSearch) has no query to change: it lists every QR code, and no setting of Find/Change
	// steers its walk - so nothing is asked of Find/Change and nothing is stated. Its searches again list its QR codes again
	// whatever tab the dialog is on, and its rows are never replaced (CanReplaceHit). A query run's list has none either: no
	// search made it, and its rows are not written again (RefuseRunListRow). Nor a table list: what it asks is the Table tab's, and
	// its Replace reads that tab as it stands (WriteOwnTabRows).
	if (KFCResultModel::ListIsQRCodes() || KFCResultModel::ListIsRunList() || KFCResultModel::ListHasOwnChangeTab())
		return kQueryUnchanged;

	// ----- (1) the TAB the results were searched with -----
	// The row's story is RE-WALKED, and a walk in another mode returns another set of matches - so the rows would be
	// lined up with occurrences the user never saw. Asked first because it is the most specific thing that can be said,
	// and because it does NOT cost the results: a tab is one click to put back. (Not "is the searched tab one this panel
	// walks at all": the tab on the results is one a search walked (KFCSearchEngine::RunSearch) - the four text tabs or
	// the Object tab - so it would never answer no.)
	// READ AS INDESIGN'S OWN MODE (KFCSearchEngine::FindChangeMode) - what a text or Object list was stamped with, what the
	// walk runs in, and what the list's query is stated into (KFCQueryScope) - not as the QR Code tab the dialog may be
	// showing, which changes no mode of InDesign's: read so, a row's operation under its list's query would be refused
	// every time the QR Code tab is shown.
	const int32 searchedMode = KFCResultModel::GetSearchMode();
	const int32 currentMode = KFCSearchEngine::FindChangeMode();
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
	// IT HAS TO HAPPEN BEFORE THE COMPARISON BELOW, and that is not a detail. The search records its signature after
	// committing the mode too (KFCSearchEngine::RunSearch commits it, then WriteListHeader records the signature).
	// Committing a mode is a declaration and, as CommitSearchMode's own comment says, "there is no promise anywhere
	// that it leaves that mode's other settings untouched" - so a signature taken on one side of that command and
	// compared against one taken on the other side could differ with nothing having changed, and would then refuse
	// every replace there is. Both are taken on the same side.
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
	//
	// NOT FOR THE OBJECT TAB: its walk is the service's own object search, which no text walker mode steers, and
	// CommitSearchMode turns that tab away by design; the search commits nothing for it either
	// (KFCSearchEngine::RunSearch's note on it), so the signatures below are still taken on the same side.
	if (currentMode != IFindChangeOptions::kObjectSearch && !KFCSearchEngine::CommitSearchMode())
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
	// THIS IS THE DOOR THAT KEEPS THE WRITE FROM HAPPENING AT ALL - not one that explains a write that came
	// back missing: no per-hit same-occurrence test stands in the writing walk (the note on it, above;
	// a trusted-story fast path that skipped it let a retyped query write the change string over
	// occurrences the user had never seen, while the panel reported the ORIGINAL rows as replaced). The
	// verify walk behind it walks under the LIVE query too (ChapterMovedUnderRows), so a row the changed
	// query no longer matches does stop the write - but a changed query that still matches the row passes
	// it, and the replace would then write, in silence, under a query the panel was not searched with.
	// Widen this test rather than lean on anything downstream.
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
// REPLACE A ROW - OR THE ROWS SELECTED TOGETHER - FROM ITS RIGHT-CLICK MENU OR RETURN (the author's calls - the
// no-track spec's F16). No prompt. The list stays a WORK LIST, so a row written shows its new text and every other row
// is carried to where its text now stands, ready for the next Replace. One undo step ("Replace"). Its doors
// (ReplaceRowsNow): the query unchanged (RefuseChangedQuery), every document of several still open once the last is
// reached, and the verify walk's three questions (ChapterMovedUnderRows). Nothing is accepted, and nothing recorded of
// KFC's own (the no-track spec's F1).
// ======================================================================================================

// A QUERY RUN'S LIST'S ROWS ARE NOT WRITTEN AGAIN (the run list spec's R12): they record what the run wrote - each a place of a chain
// of queries, with no copy and no query of its own to write with. The menu greys what it offers (KFCActionComponent); these
// doors are for a press that never went through it - Return on a row, a shortcut. True = refused, what is said in outStatus.
static bool RefuseRunListRow(const char* what, PMString& outStatus)
{
	if (!KFCResultModel::ListIsRunList())
		return false;
	outStatus = what;
	outStatus.Append(": this list records a query run - its rows are not written again.");
	outStatus.SetTranslatable(kFalse);
	return true;
}

// A LIST OF A TAB OF KFC'S WITH A CHANGE SIDE IS WRITTEN BY ITS REPLACE ALONE (the table tab spec's T11, the ruby tab spec's R9): no other write of a text row is what its tab asks. The
// menu greys them; these doors are for a press that never went through it.
static bool RefuseOwnTabRow(const char* what, PMString& outStatus)
{
	if (!KFCResultModel::ListHasOwnChangeTab())
		return false;
	outStatus = what;
	outStatus.Append(": only this row's Replace writes it - its tab's Change side.");
	outStatus.SetTranslatable(kFalse);
	return true;
}

// The own tabs' Replace (below - ClearOverridesHits' neighbour).
static bool ReplaceOwnTabRows(const KFCRowsByChapter& rowsByChapter, PMString& outStatus);

bool KFCReplaceEngine::CanReplaceHit(int32 chapterIdx, int32 hitIdx)
{
	// Nothing greys it (WhyGreyed - the one place the reasons are asked: the menu's grey state and the rows a Replace of
	// rows selected together leaves are the same question).
	// A QR CODE LIST'S ROWS ARE NEVER REPLACED (QR-18 - the author: "no replace"): greyed whatever the row says
	// - the doors that turn a press away name it (ReplaceHit, ReplaceHits, KFCObjectReplace). Nor a query run's (RefuseRunListRow).
	if (KFCResultModel::ListIsQRCodes() || KFCResultModel::ListIsRunList())
		return false;
	return WhyGreyed(chapterIdx, hitIdx) == kGreyedNot;
}

KFCReplaceEngine::GreyedReason KFCReplaceEngine::WhyGreyed(int32 chapterIdx, int32 hitIdx)
{
	// A Find/Change match not replaced, not locked, with no outcome is not greyed. Each other answer with the word its row
	// shows (KFCResultModel::BuildHitLocator).
	bool replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked))
		return kGreyedMissing;		// no such row (a selected row always is one - asked anyway)
	if (replaced)
		return kGreyedReplaced;
	const KFCResultModel::ChangeOutcome outcome = KFCResultModel::GetHitOutcome(chapterIdx, hitIdx);
	if (outcome == KFCResultModel::kOutcomeMissing)
		return kGreyedMissing;
	if (locked || outcome == KFCResultModel::kOutcomeLocked)
		return kGreyedLocked;
	if (!KFCResultModel::IsWorkOutcome(outcome))
		return kGreyedRefused;		// refused - and any outcome that is not work (IsWorkOutcome)
	return kGreyedNot;
}

namespace
{
// THE ROWS LEFT, COUNTED BY WHY - "1 already replaced, 2 locked": each reason that counts a row, in its enum's order
// from 1 (0 is "written"), in its few words (`words`). One shape for the reasons of the Replace (GreyedReason), of
// Restore from Copy and of Create Outlines.
template <class Why, size_t N>
void AppendCountsByWhy(PMString& out, const int32 (&counts)[N], const char* (*words)(Why))
{
	bool first = true;
	for (size_t why = 1; why < N; ++why)
	{
		if (counts[why] == 0)
			continue;
		if (!first)
			out.Append(", ");
		out.AppendNumber(counts[why]);
		out.Append(" ");
		out.Append(words(static_cast<Why>(why)));
		first = false;
	}
}
}	// anonymous namespace

// A greyed row's reason in a few words, for a count of rows (AppendCountsByWhy): "2 locked".
static const char* GreyedWords(KFCReplaceEngine::GreyedReason why)
{
	switch (why)
	{
		case KFCReplaceEngine::kGreyedReplaced:	return "already replaced";
		case KFCReplaceEngine::kGreyedMissing:	return "missing";
		case KFCReplaceEngine::kGreyedLocked:	return "locked";
		case KFCReplaceEngine::kGreyedRefused:	return "refused";
		default:								return "";
	}
}

// A refusal of the row's Replace begins with the command's name (the author's call).
static void StartStatus(PMString& out)
{
	out = "Replace: ";
}

// THE STORY, NOT THE ROW (the author's call): a row whose story changed since the search - an edit anywhere in it by
// typing, an Undo, the Track Changes panel or a script (StoryAsKFCLeftIt), or one its foci could not follow
// (FollowStoryFoci) - is told of its story, the row's own text untouched or not; not "the text of this row".
// FOUR LINES AT THE FLOOR (KFCPanelMetrics.cpp, kKFCMessageLines): what to do comes last and has to be read, so the
// line is kept under about 117 characters, "Replace: " included (with "(edited or undone somewhere in it, not by
// KohakuFindChange) - search again, or" it ran to 200, and the panel cut it at "or ri..." - measured, case
// msg-story-changed-shot).
static const char* StoryChangedWords(bool several)
{
	return several
		? "a selected row's story has changed since the search - right-click the story row: Search This Story Again."
		: "the story of this row has changed since the search - right-click the story row: Search This Story Again.";
}

// ONE UNDO STEP FOR A WRITE FROM THE LIST - see the header (WriteStep): the rule for one document and for several.
bool KFCReplaceEngine::BeginWriteStep(bool acrossDocuments, WriteStep& outStep, const char* stepKey,
	const char* internalName)
{
	outStep = WriteStep();
	// The caller's name, as KFC's other sequences pass one (CmdUtils.h: a lost sequence is tracked by it); the name the
	// Undo menu shows is SetName's.
	const PMString stepName(stepKey);
	if (acrossDocuments)
	{
		outStep.across = CmdUtils::BeginAbortableCmdSeq(internalName);
		if (outStep.across != nil)
			outStep.across->SetName(stepName);
	}
	else
	{
		outStep.plain = CmdUtils::BeginCommandSequence(internalName);
		if (outStep.plain != nil)
			outStep.plain->SetName(stepName);
	}
	return outStep.plain != nil || outStep.across != nil;
}

void KFCReplaceEngine::EndWriteStep(WriteStep& step, bool ok)
{
	if (step.across != nil)
	{
		// Kept, or taken back whole across the documents - the error state carries no rollback across them.
		if (ok)
			CmdUtils::EndCommandSequence(step.across);
		else
			CmdUtils::AbortCommandSequence(step.across);
	}
	else if (step.plain != nil)
	{
		// The SDK's own rollback (CmdUtils.h, SequenceContext): raised, ended, cleared below.
		if (!ok)
			ErrorUtils::PMSetGlobalErrorCode(kFailure);
		CmdUtils::EndCommandSequence(step.plain);
	}
	// Taken back or kept, no error is left standing for the application to report again.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	step = WriteStep();
}

// WHAT EACH DOCUMENT OF A WRITE IS LEFT AS - the header says the rule (DocsAfterWrite).
void KFCReplaceEngine::DocsAfterWrite::Reached(const UIDRef& doc, bool readFlag)
{
	Doc d;
	d.doc = doc;
	d.flagRead = readFlag;
	d.wasModified = readFlag && doc.GetDataBase() != nil && doc.GetDataBase()->IsModified() != kFalse;
	d.wrote = false;
	fDocs.push_back(d);
}

void KFCReplaceEngine::DocsAfterWrite::Wrote(const UIDRef& doc)
{
	// By the database: two chapters can be one document (the header says why that matters).
	for (size_t k = 0; k < fDocs.size(); ++k)
		if (doc.GetDataBase() != nil && fDocs[k].doc.GetDataBase() == doc.GetDataBase())
			fDocs[k].wrote = true;
}

KFCReplaceEngine::DocsAfterWrite::~DocsAfterWrite()
{
	for (size_t k = 0; k < fDocs.size(); ++k)
	{
		const Doc& d = fDocs[k];
		IDataBase* const db = d.doc.GetDataBase();
		if (db == nil)
			continue;
		if (d.wrote)
		{
			if (KFCBookScope::IsHeldDoc(d.doc))
				(void)KFCBookScope::ShowChapterWindow(d.doc);
			continue;
		}
		if (d.flagRead && !d.wasModified && KFCBookScope::IsDocStillOpen(d.doc))
			db->SetModified(kFalse);
		KFCBookScope::HandBackIfHeld(d.doc);
	}
}

// ======================================================================================================
// THE COPIES A ROW'S REPLACE TAKES, FOR ITS RESTORE FROM COPY
// (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md C3 / C4): taken after every door of the write and
// before its step opens (ReplaceRowsNow), into the document's copy store (KFCCopyStore) - nothing of it is in the
// document's undo step - and given to the rows once the write has gone through (KFCResultModel::SetHitCopy). Put back
// by RestoreHits, below.
// ======================================================================================================
namespace
{

// WHICH ROW, IN A WRITE'S MESSAGE (the user's report: replacing row after row with Return, every message read the same
// "Replaced." and which one had just happened could not be told). The author's choice: the story's UID and the hit's
// place among that story's results - "Replaced ID:262 #3." (the author's spelling) - the story row and its third hit
// row in the list (the tree's middle level is one group per story, BuildFontGroups; GetHitFontGroupPos is the hit's
// place under it, from 0), and the UID a script reaches the story by (stories.itemByID). A story UID is the document's
// own number: in a book the chapter row above says which document. One place for the messages of every write of text
// rows (SayRowsWritten - the Replace, Restore from Copy, Create Outlines). "ID:262 #3" for one row - empty when its
// story or its place under the story row cannot be read.
PMString RowName(int32 chapterIdx, int32 hitIdx)
{
	PMString name;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	const int32 place = KFCResultModel::GetHitFontGroupPos(chapterIdx, hitIdx);
	if (KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash) && story != kInvalidUID && place >= 0)
	{
		name.Append("ID:");
		name.AppendNumber(static_cast<int32>(story.Get()));
		name.Append(" #");
		name.AppendNumber(place + 1);
	}
	return name;
}

// WHAT A WRITE OF THE LIST DID - the one message of the Replace, Restore from Copy and Create Outlines (the no-track
// spec's F16 / F19 / F20, the copy-restore spec's C12, the outlines spec's X8), which says nothing of Ctrl+Z (the
// author's call). One row by its name (RowName): "Replaced ID:262 #3." ("Replaced." when its name cannot be read).
// Several counted - "Replaced 3 rows: ID:262 #1, ID:262 #3, ID:270 #1." - up to kNamedRows of them named, each
// chapter's in `written`'s order; with rows left, "Replaced 2 of 3 rows: ... Left as they were: <left>."; rows of
// several documents name each document before its rows: "Replaced 3 rows in 2 documents: a.indd ID:262 #1, ID:262 #3;
// b.indd ID:270 #1." `written` = each chapter's rows written (a chapter with none left out); `selected` = the rows the
// command was given; `left` = the others, counted by why.
typedef std::vector<std::pair<int32, std::vector<int32> > > RowsWritten;		// (the chapter, its rows written)
void SayRowsWritten(PMString& out, const char* verb, const char* noun, const RowsWritten& written, size_t selected,
	const PMString& left)
{
	size_t count = 0;
	for (size_t c = 0; c < written.size(); ++c)
		count += written[c].second.size();
	out = verb;
	if (selected == 1)
	{
		const PMString name(count > 0 ? RowName(written[0].first, written[0].second[0]) : PMString());
		if (!name.IsEmpty())
		{
			out.Append(" ");
			out.Append(name);
		}
		out.Append(".");
		out.SetTranslatable(kFalse);
		return;
	}
	const size_t kNamedRows = 8;
	const bool severalDocuments = written.size() > 1;
	out.Append(" ");
	out.AppendNumber(static_cast<int32>(count));
	if (count < selected)
	{
		out.Append(" of ");
		out.AppendNumber(static_cast<int32>(selected));
	}
	out.Append(" ");
	out.Append(noun);
	if (severalDocuments)
	{
		out.Append(" in ");
		out.AppendNumber(static_cast<int32>(written.size()));
		out.Append(" documents");
	}
	out.Append(": ");
	size_t named = 0;
	for (size_t c = 0; c < written.size() && named < kNamedRows; ++c)
	{
		if (severalDocuments)
		{
			if (c > 0)
				out.Append("; ");
			PMString docName;
			int32 rowsNow = 0;
			if (KFCResultModel::GetChapterDisplay(written[c].first, docName, rowsNow))
			{
				out.Append(docName);
				out.Append(" ");
			}
		}
		bool first = true;
		const std::vector<int32>& hits = written[c].second;
		for (size_t r = 0; r < hits.size() && named < kNamedRows; ++r)
		{
			const PMString name(RowName(written[c].first, hits[r]));
			if (name.IsEmpty())
				continue;
			if (!first || (!severalDocuments && named > 0))
				out.Append(", ");
			out.Append(name);
			first = false;
			++named;
		}
	}
	if (count > kNamedRows)
		out.Append(", ...");
	out.Append(".");
	if (count < selected)
	{
		out.Append(" Left as they were: ");
		out.Append(left);
		out.Append(".");
	}
	out.SetTranslatable(kFalse);
}

// The rows a write put through, chapter by chapter, for SayRowsWritten: each chapter's rows done, in its order (a
// chapter with none left out) - Restore from Copy's and Create Outlines'.
template <class Chapter>
RowsWritten RowsDone(const std::vector<Chapter>& chapters)
{
	RowsWritten written;
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		std::vector<int32> done;
		for (size_t k = 0; k < chapters[c].rows.size(); ++k)
			if (chapters[c].rows[k].done)
				done.push_back(chapters[c].rows[k].hit);
		if (!done.empty())
			written.push_back(std::make_pair(chapters[c].chapterIdx, done));
	}
	return written;
}

// DOES THE CHANGE SIDE SET PARAGRAPH FORMATTING - a paragraph style, or a paragraph attribute in Change Format, on the
// tab the write is stated in? Its Replace then changes the paragraphs the rows stand in, which a copy of the match does
// not put back (TakeRowCopy - measured live, the copy-restore spec's R13 / R13b).
bool ChangeSideTouchesParagraphs(IDataBase* db)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (db == nil || opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	if (opts->GetChangeParaStyle(db, mode, kFalse) != kInvalidUID)
		return true;
	const AttributeBossList* attrs = opts->GetChangeAttributeBossList(db, mode, kFalse);
	for (int32 i = 0; attrs != nil && i < attrs->CountBosses(); ++i)
	{
		InterfacePtr<const IAttrReport> report(static_cast<const IAttrReport*>(attrs->QueryBossN(i, IID_IATTRREPORT)));
		if (report != nil && report->IsParagraphAttribute())
			return true;
	}
	return false;
}

// A HYPERLINK TEXT SOURCE ON A STRETCH OF A STORY (LinksOn): the source, the link that names it, where its range stands,
// and whether the link is a cross-reference.
struct LinkAt
{
	UID			source;
	UID			hyperlink;		// kInvalidUID when no link names the source
	TextIndex	start;
	int32		length;
	bool		xref;
	LinkAt() : source(kInvalidUID), hyperlink(kInvalidUID), start(kInvalidTextIndex), length(0), xref(false) {}
};

// THE HYPERLINK TEXT SOURCES ON [from, to) OF A STORY (the copy-restore spec's C15): those the range tree names there -
// inside the range or across its edge (IHyperlinkUtils::GetRangeContainedHyperlinks) and one around it
// (IsRangeContainedByAHyperlink) - each read off its range's node: the source is the text object the range is of
// (ITextRangeNode::GetBaseUIDRef - the source the document's hyperlink table names for that range, measured in KT),
// where its range stands is the node's, and the table (reached as SnpCreateCrossReference.cpp reaches it) gives its
// link and whether that is a cross-reference. A source both questions name (the range is its whole text) is listed
// once. False when the range tree or the table cannot be asked, or a range named cannot be read.
bool LinksOn(ITextModel* model, const UIDRef& storyRef, TextIndex from, TextIndex to, std::vector<LinkAt>& out)
{
	Utils<IHyperlinkUtils> utils;
	IDataBase* const db = storyRef.GetDataBase();
	if (!utils || model == nil || db == nil)
		return false;
	InterfacePtr<IHyperlinkTable> table(db, db->GetRootUID(), IID_IHYPERLINKTABLE);
	if (table == nil)
		return false;
	RangeRefList refs = utils->GetRangeContainedHyperlinks(model, from, to);
	const TextRangeReference around = utils->IsRangeContainedByAHyperlink(model, from, to);
	if (around.GetLogicalID() != kInvalidSBOSID)
		refs.push_back(around);
	std::set<UID> listed;
	for (size_t r = 0; r < refs.size(); ++r)
	{
		InterfacePtr<ITextRangeNode> node(refs[r].Instantiate());
		if (node == nil)
			return false;
		const UIDRef sourceRef = node->GetBaseUIDRef();
		const UID source = sourceRef.GetUID();
		InterfacePtr<IHyperlinkTextSourceData> data(sourceRef, UseDefaultIID());
		if (data == nil || data->GetOwningStoryUID() != storyRef.GetUID() || !listed.insert(source).second)
			continue;
		LinkAt at;
		at.source = source;
		at.start = node->GetStartIndex();
		at.length = node->GetRangeLength();
		at.hyperlink = table->GetHyperlinkOfSource(sourceRef);
		at.xref = (at.hyperlink != kInvalidUID) && utils->IsXRef(UIDRef(db, at.hyperlink));
		KFC_DIAG_LOG("LINK on story=%u [%d,%d): source=%u at=%d len=%d link=%u xref=%d", (unsigned)storyRef.GetUID().Get(),
			(int)from, (int)to, (unsigned)source.Get(), (int)at.start, (int)at.length, (unsigned)at.hyperlink.Get(), at.xref ? 1 : 0);
		out.push_back(at);
	}
	return true;
}

// THE PARAGRAPH THAT HOLDS `at`, WITH ITS RETURN - the last paragraph's with the story's own (IComposeScanner::
// FindSurroundingParagraph, the end of the story counted - kFalse, SnpCreateCrossReference.cpp's call; it answered as
// the walk for the returns it replaced at every position of a story in KT - an empty paragraph, a forced line break, a
// table's anchor, the last paragraph). The copy of a match holding a return runs on to it (TakeRowCopy), and the
// formatting of the paragraph a match starts in is put back over it (PutParagraphBack). False when it cannot be read.
bool ParagraphOf(ITextModel* model, TextIndex at, TextIndex& outStart, int32& outLength)
{
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	outLength = 0;
	outStart = (scanner != nil) ? scanner->FindSurroundingParagraph(at, &outLength, kFalse) : kInvalidTextIndex;
	return outStart >= 0 && outLength > 0;
}

// THE COPY ONE ROW'S REPLACE TAKES - of [start, end) of its story, as the doors before the write have just found it. Run on
// to just before the return of the paragraph the match ends in when the match holds a return: a paste ending there puts
// that paragraph's own attributes back on the return it meets and leaves the next paragraph alone - "copy up to the
// return, paste up to the return" (measured in KT, docs/ai-notes/private-scrap-store-2026-10-06.md; a return copied or
// pasted on its own broke the paragraph after it). The paragraph the match starts in keeps the attributes it has - the
// merged paragraph's, its own first paragraph's when the Change side sets no paragraph formatting.
// A ROW WHOSE CHANGE SIDE SETS PARAGRAPH FORMATTING (`paragraphChange`) ALSO KEEPS THE FORMATTING OF THE PARAGRAPH IT
// STARTS IN - its style and its own overrides (the paragraph strand, SnpManipulateTextStyle.cpp's road) - which
// RestoreInChapter puts back after the paste (the copy-restore spec's C16, the author's call). Measured live, the copy
// alone cannot: inside one paragraph (R13) the copy put the text back and left the paragraph style the Replace set;
// over a return (R13b) the paragraph the match starts in kept it - a paste's first paragraph takes the attributes of
// the paragraph it is pasted into. (A copy of the whole paragraph, head to return, would carry them, and would collide
// with every other row replaced in it - each copy holding the others' text from before them.)
// A HYPERLINK SOURCE THE COPY TOUCHES - strictly inside the match, around it, across its edge - is kept with where it
// stood (HitCopy::links), for RestoreInChapter to put the ONE link back (PutLinksBack - the copy-restore spec's C15,
// the author's call: "delete one of the duplicated links, perhaps"). Measured live with the doors open (switch
// copy-links - the spec's H1-H5): one the match crossed is cut back to its part outside the match (set back where it
// stood); one around the match comes back as it was, and so does one strictly inside it - in the part of the match the
// Replace deletes, the link goes with it and the paste brings it back under its own name (H4); on what the Replace
// writes over, the Replace keeps it on what it wrote, and the Restore moves it out of the paste's way and sets it back
// (MoveLinksOutOfPaste - measured, case restore-link-inside-named). One the paste would replace WHOLE - the match
// itself (InDesign's replace keeps it on what it writes), one in the run-on rest, one at the match's edge - keeps no
// copy: the paste brings a second link over it and leaves it with no story, and setting it again crashed InDesign.
// NONE is taken - the row says why - for a match outside the story's primary thread (a table cell's, a footnote's or a
// note's: a copy run across threads crashed InDesign), one that touches a CROSS-REFERENCE source (its text is made by
// InDesign - unmeasured), and a run-on rest holding an object or a marker (pasted again over itself, such a thing is made
// anew - another name, or two of it - measured).
KFCResultModel::HitCopy TakeRowCopy(const UIDRef& storyRef, TextIndex start, TextIndex end, bool paragraphChange)
{
	KFCResultModel::HitCopy copy;
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	// THE THREAD THE MATCH STANDS IN (KFCCopyStore::CopyableThread - the story's own text, a table cell's, a footnote's: the
	// next-overrides-restore spec's C1): [threadFirst, threadLast) - its closing character stands at `threadLast`. A note's
	// keeps no copy, nor a range run across threads.
	TextIndex threadFirst = kInvalidTextIndex, threadLast = kInvalidTextIndex;
	if (model == nil || start < 0 || end < start || !KFCCopyStore::CopyableThread(model, start, threadFirst, threadLast)
		|| end > threadLast)
	{
		copy.whyNot = KFCResultModel::kCopyOutsideMainText;
		return copy;
	}
	// THE MATCH, READ ONCE: does it hold a return (the copy then runs on, below)? AN ANCHORED OR INLINE OBJECT IN IT
	// KEEPS NO COPY (the author's call: its UID changing is the problem) - put back, it would be a copy of the object
	// under a new UID (measured live - the copy-restore spec's R19c: the same rectangle in all but its identity).
	bool holdsReturn = false;
	{
		TextIterator it(model, start);
		for (TextIndex i = start; i < end && !it.IsNull(); ++i, ++it)
		{
			const UTF32TextChar c = *it;
			if (c.GetValue() == kTextChar_ObjectReplacementCharacter)
			{
				copy.whyNot = KFCResultModel::kCopyHoldsAnchoredObject;
				return copy;
			}
			if (c.GetValue() == kTextChar_CR)
				holdsReturn = true;
		}
	}
	if (paragraphChange)
	{
		// The paragraph the match starts in: its style and its own overrides, as they read before the write.
		InterfacePtr<IAttributeStrand> paraStrand(static_cast<IAttributeStrand*>(
			model->QueryStrand(kParaAttrStrandBoss, IID_IATTRIBUTESTRAND)));
		if (paraStrand == nil)
		{
			copy.whyNot = KFCResultModel::kCopyFailed;
			return copy;
		}
		int32 count = 0;
		copy.paraStyle = paraStrand->GetStyleUID(start, &count);
		const DataWrapper<AttributeBossList> overrides = paraStrand->GetLocalOverrides(start, &count);
		const AttributeBossList* const own = overrides.get();		// (DataWrapper has no ->: its get())
		if (own != nil && own->CountBosses() > 0)
			copy.paraOverrides.reset(own->Duplicate());		// shared - a later paragraph change puts in new ones
		if (copy.paraStyle == kInvalidUID)
		{
			copy.whyNot = KFCResultModel::kCopyFailed;
			return copy;
		}
	}
	TextIndex copyEnd = end;
	if (holdsReturn)
	{
		// The run-on rest: up to the return of the paragraph the match ends in (ParagraphOf - the last paragraph's is
		// the story's own), with no character an object or a marker stands on (KFCResultModel::IsMarkerNotShown - the
		// ones a row does not show - and a table's two).
		TextIndex paraStart = kInvalidTextIndex;
		int32 paraLength = 0;
		if (!ParagraphOf(model, end, paraStart, paraLength))
		{
			copy.whyNot = KFCResultModel::kCopyFailed;
			return copy;
		}
		copyEnd = paraStart + paraLength - 1;		// its return
		// ...in the match's thread (a cell's last paragraph ends on the thread's closing character - the copy stops short
		// of it: the spec's C2).
		if (copyEnd > threadLast)
		{
			copy.whyNot = KFCResultModel::kCopyOutsideMainText;
			return copy;
		}
		TextIterator it(model, end);
		for (TextIndex i = end; i < copyEnd && !it.IsNull(); ++i, ++it)
		{
			const UTF32TextChar c = *it;
			if (KFCResultModel::IsMarkerNotShown(c) || c.GetValue() == kTextChar_Table
				|| c.GetValue() == kTextChar_TableContinued)
			{
				copy.whyNot = KFCResultModel::kCopyRestHoldsObject;
				return copy;
			}
		}
	}
	// HYPERLINK SOURCES - text ranges, not characters: every one on what the copy takes, [start, copyEnd), kept with where it
	// stood from the match's start (LinksOn - in the match, around it, across its edge, in the run-on rest); a
	// cross-reference's keeps no copy. (A zero-width match's copy is empty: nothing of a link goes with it.)
	if (copyEnd > start)
	{
		std::vector<LinkAt> touching;
		if (!LinksOn(model, storyRef, start, copyEnd, touching))
		{
			copy.whyNot = KFCResultModel::kCopyFailed;
			return copy;
		}
		for (size_t i = 0; i < touching.size(); ++i)
		{
			// (Fault switch copy-links, a test build's only - KFCDiag.h: the cross-reference door stands open, to measure.)
			if (touching[i].xref && !KFC_DIAG_FAULT("copy-links"))
			{
				copy.whyNot = KFCResultModel::kCopyTouchesHyperlink;
				return copy;
			}
			// A SOURCE THE PASTE WOULD REPLACE WHOLE keeps no copy (the copy-restore spec's H1 - H5): one inside what
			// the copy covers that is not strictly inside the match - the match itself (the Replace keeps it on what it
			// writes, H1), one in the run-on rest (H5), one at the match's edge - is left with no story once the copy
			// is pasted over its text, and setting it again took InDesign down (HYPERLINKS.RPLN, under
			// IHyperlinkFacade::ChangeTextSource). Strictly inside the match the Replace deletes it and the paste
			// brings it back (H4), or keeps it on what it wrote and the Restore moves it out of the paste's way first
			// (MoveLinksOutOfPaste); around the match or across its edge, its part outside stays and it is set back
			// where it stood (H2, H3).
			if (touching[i].start >= start && touching[i].start + touching[i].length <= copyEnd
				&& !(touching[i].start > start && touching[i].start + touching[i].length < end))
			{
				copy.whyNot = KFCResultModel::kCopyTouchesHyperlink;
				return copy;
			}
			KFCResultModel::HitLink link;
			link.source = touching[i].source;
			link.from = touching[i].start - start;
			link.to = touching[i].start + touching[i].length - start;
			copy.links.push_back(link);
		}
	}
	KFCCopyStore::CopyRef taken;
	if (!KFCCopyStore::Take(storyRef, start, copyEnd, taken))
	{
		copy.whyNot = KFCResultModel::kCopyFailed;
		return copy;
	}
	copy.store = taken.store;
	copy.story = taken.story;
	copy.rest = copyEnd - end;
	copy.restHash = (copy.rest > 0) ? KFCSearchEngine::TextHash(model, end, copyEnd) : 0;
	copy.whyNot = KFCResultModel::kCopyKept;
	return copy;
}

}	// anonymous namespace

struct ChapterWrite
{
	int32			chapterIdx;
	std::set<int32>	rows;				// the rows to write
	UIDRef			docRef;
	std::set<UID>	writtenStories;		// the stories the write moves - their versions taken once it went through
	ChapterWrite() : chapterIdx(-1) {}
};

// The rows replaced now, in ONE undo step - a hit row's Replace (its right-click menu or Return - the no-track spec's
// F16), or the text rows selected together (the author's call: replaced together, as object rows are) - of one document
// or of several (one step for every document of them - the header's WriteStep).
// The caller has asked CanReplaceHit of every row. All or nothing, as one row's has always been: a row the check finds
// moved refuses them all before a character is written, and a write that does not land on every one is rolled back
// whole. greyed = the rows selected with them whose Replace was greyed, counted by reason (WhyGreyed - all 0 for one
// row's Replace): one row's sentences are its own ("this row", "the match"); several rows' say "a selected row", and
// the rows left are counted by their reason.
static bool ReplaceRowsNow(std::vector<ChapterWrite>& chapters,
	const int32 (&greyed)[KFCReplaceEngine::kGreyedReasonCount], PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("replace-rows");	// (test builds: after DocsAfterWrite, below - KFCDiagWrittenDocs.h)
	int32 toWrite = 0;
	for (size_t k = 0; k < chapters.size(); ++k)
		toWrite += static_cast<int32>(chapters[k].rows.size());
	if (toWrite == 0)
		return false;
	int32 left = 0;
	for (int32 why = 0; why < KFCReplaceEngine::kGreyedReasonCount; ++why)
		left += greyed[why];
	const int32 selectedCount = toWrite + left;
	const bool several = selectedCount > 1;
	const bool acrossDocuments = chapters.size() > 1;
	// Forward, as the search was - outside the sequence below (the walk's direction for a GREP query
	// holding ^ is turned below, also outside it).
	KFCForwardSearchScope forward;
	// A changed query CLEARS the results (RefuseChangedQuery) - the UI half redraws the tree for it
	// (KFCResultTree::ReplaceRows). The refusal says which of its three answers it was - not
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
	// Each chapter's document, by its file first (a UIDRef can outlive its document - see
	// KFCBookScope::ReachChapterDoc); what each is left as is DocsAfterWrite's, from the moment it is reached.
	KFCReplaceEngine::DocsAfterWrite docsAfter;
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		ChapterWrite& c = chapters[k];
		IDFile file;
		if (!KFCResultModel::GetChapterLocation(c.chapterIdx, c.docRef, file))
		{
			StartStatus(outStatus);
			outStatus.Append(several ? "the document of the selected rows could not be found." : "the document of this row could not be found.");
			return false;
		}
		if (!KFCBookScope::ReachChapterDoc(file, c.docRef))
		{
			StartStatus(outStatus);
			outStatus.Append(several ? "the document of the selected rows could not be opened." : "the document of this row could not be opened.");
			return false;
		}
		KFCResultModel::RebindChapterDoc(c.chapterIdx, c.docRef);
		// Its flag read before anything here touches it: a step rolled back, or a refusal below, has it put back.
		docsAfter.Reached(c.docRef, true);
	}
	// EVERY DOCUMENT STILL OPEN ONCE THE LAST IS REACHED: reaching a closed chapter opens it, and opening runs the message
	// loop (KFCRunGuard keeps KFC's own doors shut there, not InDesign's) - a close dispatched there would leave a document
	// reached before it behind its docRef (not measured). Asked by comparing alone (KFCBookScope::IsDocStillOpen), as the
	// query run asks before each write.
	for (size_t k = 0; acrossDocuments && k < chapters.size(); ++k)
	{
		if (!KFCBookScope::IsDocStillOpen(chapters[k].docRef))
		{
			StartStatus(outStatus);
			outStatus.Append("a document of the selected rows has been closed - nothing was changed.");
			return false;
		}
	}
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
	// THE VERIFY WALK'S THREE QUESTIONS, OVER THESE ROWS. Each one's story at the version KFC last recorded, the row
	// still reading as it was found - match and line - and a match of the walk at its start and length
	// (ChapterMovedUnderRows). Not the match's hash alone: a row an Undo has left on the next occurrence of its own
	// text passes that, and would be written there. Forward, like the search - the scope at the head of this function.
	// THE ROWS FOLLOW THEIR FOCI FIRST (KFCSearchEngine::FollowStoryFoci - the author's call). For each story of the
	// rows to write: a story whose foci all followed every edit and every Undo takes their places and its version as it
	// stands, so the doors below go by where the text is now; a story with a focus that could not follow (an Undo of an
	// edit older than the search) is refused here - its version can have come back to the number on record with other
	// text in it; a story with no foci is left to the version rule, as before. They are the stories the write moves
	// (writtenStories): at the versions on record, as the doors below say, they take their new versions once it has
	// gone through (NoteStoryVersions, below).
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		ChapterWrite& c = chapters[k];
		for (std::set<int32>::const_iterator row = c.rows.begin(); row != c.rows.end(); ++row)
		{
			UID story = kInvalidUID;
			TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
			uint64 hash = 0;
			if (KFCResultModel::GetHitMatchIdentity(c.chapterIdx, *row, story, start, end, hash) && story != kInvalidUID)
				c.writtenStories.insert(story);
		}
		for (std::set<UID>::const_iterator s = c.writtenStories.begin(); s != c.writtenStories.end(); ++s)
		{
			if (KFCSearchEngine::FollowStoryFoci(c.chapterIdx, c.docRef.GetDataBase(), *s) == KFCSearchEngine::kFollowBroken)
			{
				StartStatus(outStatus);
				outStatus.Append(StoryChangedWords(several));
				return false;
			}
		}
	}
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		const ChapterWrite& c = chapters[k];
		MovedWhy movedWhy = kMovedRow;
		if (ChapterMovedUnderRows(c.chapterIdx, c.docRef, scopeOptions, c.rows, movedWhy))
		{
			StartStatus(outStatus);
			if (movedWhy == kMovedStory)
			{
				outStatus.Append(StoryChangedWords(several));		// the story, not the row
				return false;
			}
			outStatus.Append(several ? "the text of a selected row has changed since the search (edited, or undone) - search again."
				: "the text of this row has changed since the search (edited, or undone) - search again.");
			return false;
		}
	}
	// THE COPIES RESTORE FROM COPY PUTS BACK (the copy-restore spec's C3 / C4 - TakeRowCopy): every row to write,
	// copied now - after every door, at the places they have just confirmed, and before the step opens, so nothing of
	// it is in the document's undo step. A row with none is written all the same (its Restore from Copy says why); each
	// row takes its copy once the write has gone through (SetHitCopy, below).
	std::vector<std::vector<std::pair<int32, KFCResultModel::HitCopy> > > copies(chapters.size());
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		const ChapterWrite& c = chapters[k];
		IDataBase* const db = c.docRef.GetDataBase();
		const bool paragraphChange = ChangeSideTouchesParagraphs(db);
		for (std::set<int32>::const_iterator r = c.rows.begin(); r != c.rows.end(); ++r)
		{
			UID story = kInvalidUID;
			TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
			uint64 hash = 0;
			KFCResultModel::HitCopy copy;
			if (db != nil && KFCResultModel::GetHitMatchIdentity(c.chapterIdx, *r, story, start, end, hash) && story != kInvalidUID)
				copy = TakeRowCopy(UIDRef(db, story), start, end, paragraphChange);
			else
				copy.whyNot = KFCResultModel::kCopyFailed;
			copies[k].push_back(std::make_pair(*r, copy));
		}
	}

	ChapterOutcome total;		// every chapter's, added up (ChapterOutcome::Add)
	bool ok = false;
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO. The story versions are read and the row backup started
	// here, outside the sequence; kept below once the rows show the replace - a failure puts them back
	// (RollBackRows). One recording for every document of the step.
	std::vector<int32> chapterIdxs;
	for (size_t k = 0; k < chapters.size(); ++k)
		chapterIdxs.push_back(chapters[k].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	{
		// BACKWARDS FOR THE WRITE ONLY. A query holding ^ is written backwards (WriteBackward), inside this
		// block and nowhere else - not to the end of the function, where a walk after the write would run
		// backwards with it.
		const KFCBackwardSearchScope writeDirection(WriteBackward());

		KFCReplaceEngine::WriteStep step;
		if (!KFCReplaceEngine::BeginWriteStep(acrossDocuments, step))
		{
			StartStatus(outStatus);
			outStatus.Append(acrossDocuments ? "InDesign would not start an undoable step - nothing was changed."
				: "InDesign would not start a command sequence - nothing was changed.");
			return false;
		}
		// (the row backup was started by the recorder, above)
		ok = true;
		for (size_t k = 0; k < chapters.size() && ok; ++k)
		{
			ChapterWrite& c = chapters[k];
			ChapterOutcome chapter;
			const bool wrote = ReplaceInChapterOneByOne(c.chapterIdx, c.docRef, scopeOptions, c.rows, chapter);
			total.Add(chapter);
			// A chapter not written whole ends the step there: nothing more is processed over what it left (a command
			// processed while the error state stands is a protective shutdown - guide vol1-03), and the step goes back.
			ok = wrote && !chapter.failed && chapter.replaced == static_cast<int32>(c.rows.size());
		}
		// THE ERROR STATE AT THE END: a command can report success and still leave it up, so anything standing here is a
		// failure nothing reported, and the step goes back (EndWriteStep - the abortable step by its abort, the plain one
		// by the error state) with the rows put back and the line saying so. Asked of BOTH: a plain step ends by the error
		// state whatever `ok` says (CmdUtils.h - SequenceContext), so one left to end by it alone would be taken back by
		// InDesign under rows marked Changed (the object rows ask the same - KFCObjectReplace::ReplaceRow and ReplaceRows).
		if (ok && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		{
			ok = false;
			total.failed = true;
		}
		if (ok)
			for (size_t k = 0; k < chapters.size(); ++k)
				KFCUndoFollow::MarkWrite(chapters[k].docRef.GetDataBase());	// in this step, so its Undo / Redo is heard
		// THE WRITTEN STORIES' FOCI ON THE ROWS, IN THIS STEP (the row-focus plan's D7): InDesign moved them with the write,
		// and a replaced row's own focus goes on what it wrote. Marked first (inStep), so this step's Undo and Redo hold them
		// too - a move made after the step would be read back over by the next Undo (KFCRowFoci::MoveTo).
		if (ok)
			for (size_t k = 0; k < chapters.size(); ++k)
				for (std::set<UID>::const_iterator ws = chapters[k].writtenStories.begin(); ws != chapters[k].writtenStories.end(); ++ws)
					KFCRowFoci::ReanchorStory(chapters[k].chapterIdx, *ws, true);
		// Kept, or taken back whole with nothing left standing - the line below says why; the "unsaved" flags are
		// DocsAfterWrite's.
		KFCReplaceEngine::EndWriteStep(step, ok);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		// A write that went in and was rolled back moved the foci of the story without moving them back (T13 of
		// docs/superpowers/specs/2026-10-08-kfc-text-focus-jump-design.md): the story is as KFC knew it before the write
		// (the check above refuses any other), so the rows' places are exact.
		for (size_t k = 0; k < chapters.size(); ++k)
			for (std::set<UID>::const_iterator ws = chapters[k].writtenStories.begin(); ws != chapters[k].writtenStories.end(); ++ws)
				KFCRowFoci::ReanchorStory(chapters[k].chapterIdx, *ws);
		StartStatus(outStatus);
		// The reason the row was not written, in its words - for several, the first reason met (and nothing of them was
		// written: the step went back whole).
		const char* why = nil;
		bool searchAgain = false;
		if (total.locked > 0)
			why = several ? "a selected row's match is locked now (a locked layer or story)" : "the match is locked now (a locked layer or story)";
		else if (total.walkFailed)
		{
			// The walk broke off before it reached the row: the row is "not found" only because nobody looked (the
			// search's own "stopped with a search error" - measured, case xq-walkfail).
			why = several ? "InDesign's search stopped with an error before it reached a selected row's match"
				: "InDesign's search stopped with an error before it reached the match";
			searchAgain = true;
		}
		else if (total.missing > 0)
		{
			why = several ? "a selected row's match was not found where the search found it" : "the match was not found where the search found it";
			searchAgain = true;
		}
		else if (total.refused > 0)
			why = "InDesign's replace command would not run there";
		if (why == nil)
		{
			outStatus.Append(total.whyNot.IsEmpty() ? PMString("it did not go through") : total.whyNot);
			outStatus.Append(" - nothing was changed.");
		}
		else
		{
			outStatus.Append(why);
			outStatus.Append(searchAgain ? " - search again." : " - left as it is.");
		}
		return false;
	}
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		const ChapterWrite& c = chapters[k];
		NoteStoryVersions(c.chapterIdx, c.docRef.GetDataBase(), c.writtenStories);	// the next Replace finds it as KFC left it
		// (The written stories' foci were put on the rows' places inside the step, above.)
	}
	// Each row written takes the copy taken for it (the copy-restore spec's C6) - inside the row backup, so the Undo of
	// this Replace puts the row's earlier copy back with it.
	for (size_t k = 0; k < chapters.size(); ++k)
		for (size_t r = 0; r < copies[k].size(); ++r)
			KFCResultModel::SetHitCopy(chapters[k].chapterIdx, copies[k][r].first, copies[k][r].second);
	// Kept AFTER the versions are noted - they are the "after" an Undo's "before" is put back over.
	recorder.Keep();
	for (size_t k = 0; k < chapters.size(); ++k)
	{
		docsAfter.Wrote(chapters[k].docRef);		// written to: a chapter of ours has to be seen and saved
		KFC_DIAG_NOTE_WRITE(chapters[k].docRef);
		// (Fault switch clean-after-write, a test build's only - KFCDiag.h: the written document's "unsaved" flag taken
		// down, a close would then throw the write away unasked - the case that shows the written-document check catching it.)
		if (KFC_DIAG_FAULT("clean-after-write") && chapters[k].docRef.GetDataBase() != nil)
			chapters[k].docRef.GetDataBase()->SetModified(kFalse);
	}
	// (What each row wrote - Hit::replacedText - is taken by the walk that writes it, WalkStoryReplacing.)
	// WHAT IT DID (SayRowsWritten): the rows in the chapter's own order (the search's page order); the rows left
	// counted by reason (WhyGreyed - the object rows' form, KFCObjectReplace::ReplaceRows).
	RowsWritten written;
	for (size_t k = 0; k < chapters.size(); ++k)
		written.push_back(std::make_pair(chapters[k].chapterIdx,
			std::vector<int32>(chapters[k].rows.begin(), chapters[k].rows.end())));
	PMString leftWords;
	AppendCountsByWhy(leftWords, greyed, GreyedWords);
	SayRowsWritten(outStatus, "Replaced", "rows", written, static_cast<size_t>(selectedCount), leftWords);
	return true;
}

KFCReplaceEngine::WriteUnderWay::WriteUnderWay()
{
	++gReplacingDepth;
}

KFCReplaceEngine::WriteUnderWay::~WriteUnderWay()
{
	--gReplacingDepth;
}

bool KFCReplaceEngine::IsReplacing()
{
	return gReplacingDepth > 0;
}

bool KFCReplaceEngine::ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	const KFCReplaceEngine::WriteUnderWay replacing;	// counted from the door to the hand-back (IsReplacing)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (KFCResultModel::ListIsQRCodes())
	{
		outStatus = "Replace: ";
		outStatus.Append(KFCQRSearch::NotChangedWords());
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	if (RefuseRunListRow("Replace", outStatus))
		return false;
	if (KFCResultModel::ListHasOwnChangeTab())
	{
		KFCRowsByChapter one(1);
		one[0].chapter = chapterIdx;
		one[0].hits.push_back(hitIdx);
		return ReplaceOwnTabRows(one, outStatus);
	}
	if (!CanReplaceHit(chapterIdx, hitIdx))
	{
		outStatus = "Replace: this row cannot be replaced (already replaced, locked, or not a match of Find/Change).";
		return false;
	}
	// AN OBJECT ROW is written by InDesign's object replace, not by a walk of a story (KFCObjectReplace).
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID)
		return KFCObjectReplace::ReplaceRow(chapterIdx, hitIdx, outStatus);
	std::vector<ChapterWrite> chapters(1);
	chapters[0].chapterIdx = chapterIdx;
	chapters[0].rows.insert(hitIdx);
	const int32 none[kGreyedReasonCount] = {};
	return ReplaceRowsNow(chapters, none, outStatus);
}

bool KFCReplaceEngine::ReplaceHits(const KFCRowsByChapter& rowsByChapter, PMString& outStatus)
{
	const KFCReplaceEngine::WriteUnderWay replacing;	// (IsReplacing - ReplaceHit's)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	int32 firstChapter = -1, firstHit = -1;
	if (!KFCFirstRow(rowsByChapter, firstChapter, firstHit))
		return false;
	const size_t total = KFCRowCount(rowsByChapter);
	if (total == 1)
		return ReplaceHit(firstChapter, firstHit, outStatus);
	// A QR CODE LIST'S ROWS (CanReplaceHit's note): none of them.
	if (KFCResultModel::ListIsQRCodes())
	{
		outStatus = "Replace: ";
		outStatus.Append(KFCQRSearch::NotChangedWords());
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	if (RefuseRunListRow("Replace", outStatus))
		return false;
	if (KFCResultModel::ListHasOwnChangeTab())
		return ReplaceOwnTabRows(rowsByChapter, outStatus);
	// OBJECT ROWS: InDesign's object replace, row by row (KFCObjectReplace::ReplaceRows - it asks each row).
	if (KFCResultModel::GetHitItem(firstChapter, firstHit) != kInvalidUID)
		return KFCObjectReplace::ReplaceRows(rowsByChapter, outStatus);
	// TEXT ROWS: a row whose Replace is greyed (already replaced, missing, locked, refused) is left and counted by its
	// reason; the others are written together, all or nothing (ReplaceRowsNow) - of one document or several.
	std::vector<ChapterWrite> chapters;
	int32 greyed[kGreyedReasonCount] = {};
	for (size_t k = 0; k < rowsByChapter.size(); ++k)
	{
		ChapterWrite c;
		c.chapterIdx = rowsByChapter[k].chapter;
		const std::vector<int32>& hits = rowsByChapter[k].hits;
		for (size_t h = 0; h < hits.size(); ++h)
		{
			const GreyedReason why = WhyGreyed(c.chapterIdx, hits[h]);
			if (why == kGreyedNot)
				c.rows.insert(hits[h]);
			else
				++greyed[why];
		}
		if (!c.rows.empty())
			chapters.push_back(c);
	}
	if (chapters.empty())
	{
		// (the object rows' form - KFCObjectReplace::CheckRowsNow)
		outStatus = "Replace: none of the ";
		outStatus.AppendNumber(static_cast<int32>(total));
		outStatus.Append(" selected rows can be replaced - ");
		AppendCountsByWhy(outStatus, greyed, GreyedWords);
		outStatus.Append(".");
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	return ReplaceRowsNow(chapters, greyed, outStatus);
}

// ======================================================================================================
// RESTORE FROM COPY (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md - the author: "copy back, from
// the row's right-click menu"). A text row a Replace of the list wrote gets back the text that Replace wrote over: the
// copy taken just before it (TakeRowCopy) pasted over what it wrote - and, for a copy run on to its paragraph's return,
// over the rest of that paragraph too, which the copy holds as it read then (C4). ONE undo step ("Restore from Copy" -
// WriteStep), KFC's mark in it: the panel follows its Ctrl+Z and Redo as it follows a Replace's.
// THE ROW'S OWN PLACE IS WHAT IS TRUSTED, NOT THE STORY'S VERSION (C8): the rows of its story follow their foci first
// (KFCSearchEngine::FollowStoryFoci), and a row is put back only where its own focus stands exactly on its place and that
// place still reads what its Replace wrote - so an edit anywhere else in the story, made since, does not stop it (what
// Restore from Copy is for: Ctrl+Z cannot reach one replace past the edits after it), and an edit of what it wrote
// does. A story with no foci falls back on the version rule (StoryAsKFCLeftIt), as a Replace does.
// The rows that pass are pasted in text order, each checked again just before its paste (C9 - its run-on rest is
// compared there); one whose rest has changed waits for another round, since putting back a row after it in the same
// paragraph can make the rest read as copied again. Every other row is carried as a Replace carries them (RowNow): the
// paste turned what was written (W characters) into the match (M), and the rest was pasted over with the same text.
// ======================================================================================================

namespace
{

// WHY A ROW IS NOT PUT BACK - said in full for one row (RestoreSentence), counted for several (RestoreWords).
enum RestoreRefusal
{
	kRestoreOk = 0,
	kRestoreNotText,		// an object row (never from the panel - its menu has no Restore from Copy)
	kRestoreNotReplaced,	// nothing to put back
	kRestoreNoCopy,			// no copy kept - HitCopy::whyNot says why
	kRestoreNoDoc,			// its document could not be found
	kRestoreNotOpened,		// ...or not opened
	kRestoreNotFollowed,	// its story changed in a way its foci could not follow
	kRestoreEdited,			// what its Replace wrote has been edited since
	kRestoreRestChanged,	// the rest of its paragraph has changed since the Replace
	kRestoreLocked,			// locked now
	kRestoreLinkInTheWay,	// a hyperlink the paste would break (MoveLinksOutOfPaste)
	kRestoreStyleGone,		// the paragraph style it puts back has been deleted since (ParagraphStyleStands)
	kRestoreRefusalCount
};

// A few words each, for a count of rows: "2 edited since".
const char* RestoreWords(RestoreRefusal why)
{
	switch (why)
	{
		case kRestoreNotText:		return "not a text row";
		case kRestoreNotReplaced:	return "not replaced";
		case kRestoreNoCopy:		return "no copy kept";
		case kRestoreNoDoc:			return "document not found";
		case kRestoreNotOpened:		return "document not opened";
		case kRestoreNotFollowed:	return "not followed";
		case kRestoreEdited:		return "edited since";
		case kRestoreRestChanged:	return "rest of paragraph changed";
		case kRestoreLocked:		return "locked";
		case kRestoreLinkInTheWay:	return "hyperlink in the way";
		case kRestoreStyleGone:		return "paragraph style deleted";
		default:					return "";
	}
}

// One row's refusal, after "Restore from Copy: ". A row with no copy says why it has none (HitCopy::whyNot - the
// copy-restore spec's C12).
const char* RestoreSentence(RestoreRefusal why, int32 whyNoCopy)
{
	switch (why)
	{
		case kRestoreNotText:		return "this row is not a text row.";
		case kRestoreNotReplaced:	return "this row has not been replaced.";
		case kRestoreNoCopy:
			switch (whyNoCopy)
			{
				case KFCResultModel::kCopyOutsideMainText:
					return "no copy was kept when this row was replaced - its match is in a note, or runs out of the text it stands in.";
				case KFCResultModel::kCopyTouchesHyperlink:
					return "no copy was kept when this row was replaced - its match is a hyperlink's whole text or touches a cross-reference, or the rest of its paragraph holds a hyperlink: a copy put back would break the link.";
				case KFCResultModel::kCopyRestHoldsObject:
					return "no copy was kept when this row was replaced - the rest of its paragraph holds an object or a marker.";
				case KFCResultModel::kCopyHoldsAnchoredObject:
					return "no copy was kept when this row was replaced - its match holds an anchored object, which a copy would put back as a new object (a new ID).";
				case KFCResultModel::kCopyFailed:
					return "no copy could be kept when this row was replaced.";
				default:
					return "no copy is kept for this row.";
			}
		case kRestoreNoDoc:			return "the document of this row could not be found.";
		case kRestoreNotOpened:		return "the document of this row could not be opened.";
		case kRestoreNotFollowed:	return "the story of this row has changed in a way KohakuFindChange cannot follow - left as it is.";
		case kRestoreEdited:		return "the text its Replace wrote has been edited since - left as it is.";
		case kRestoreRestChanged:	return "the rest of its paragraph has changed since the replace - left as it is.";
		case kRestoreLocked:		return "the text is locked now (a locked layer or story) - left as it is.";
		case kRestoreLinkInTheWay:
			return "a hyperlink stands on the text its Replace wrote, and a copy put back over it would break "
				"the link - left as it is.";
		case kRestoreStyleGone:		return "the paragraph style it puts back has been deleted since - left as it is.";
		default:					return "";
	}
}

// A RESTORE FROM COPY THAT PUT NOTHING BACK: one row's reason in full (RestoreSentence), several rows' counted by
// reason - "1 edited since, 2 no copy kept".
void SayNoneRestored(PMString& out, size_t total, RestoreRefusal oneWhy, int32 oneWhyNoCopy,
	const int32 (&counts)[kRestoreRefusalCount])
{
	out = "Restore from Copy: ";
	if (total == 1)
		out.Append(RestoreSentence(oneWhy, oneWhyNoCopy));
	else
	{
		out.Append("none of the ");
		out.AppendNumber(static_cast<int32>(total));
		out.Append(" selected rows can be restored - ");
		AppendCountsByWhy(out, counts, RestoreWords);
		out.Append(".");
	}
	out.SetTranslatable(kFalse);
}

// IS THE ROW ONE TO PUT BACK, BY THE ROW ALONE (the copy-restore spec's C7) - asked in one place: the menu greys
// Restore from Copy by it (CanRestoreHit), and RestoreHits' first door counts a row by it. A text row a Replace of the
// list wrote, with its copy kept (outCopy - read when the row gets that far).
RestoreRefusal RestoreRowRefusal(int32 chapterIdx, int32 hitIdx, KFCResultModel::HitCopy& outCopy)
{
	bool replaced = false, locked = false;
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID)
		return kRestoreNotText;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked) || !replaced)
		return kRestoreNotReplaced;
	if (!KFCResultModel::GetHitCopy(chapterIdx, hitIdx, outCopy) || outCopy.whyNot != KFCResultModel::kCopyKept)
		return kRestoreNoCopy;
	return kRestoreOk;
}

// WHERE A ROW STANDS NOW, ITS STORY FOLLOWED - Restore from Copy's rule (the copy-restore spec's C8) and
// Create Outlines' (the outlines spec's X4). The rows of each story follow their foci once, before any row is asked
// (FollowStoriesOf - KFCSearchEngine::FollowStoryFoci, a Replace's way); then a row's place (out*, read after the
// follow) stands when its own focus is exactly on it - it followed every edit since, and FollowStoryFoci put the row
// there because its focus reads the row's text - or, in a story with no foci (its document was not open when the rows
// took theirs), when the story is at the version on record (the version rule, StoryAsKFCLeftIt). kPlaceNotFollowed: the
// story changed in a way its foci could not follow, or, with no foci, its version moved. kPlaceEdited: the row cannot
// be read, or its focus is elsewhere - its own text was edited.
enum PlaceFollowed { kPlaceStands, kPlaceNotFollowed, kPlaceEdited };

template <class Row>
void FollowStoriesOf(int32 chapterIdx, IDataBase* db, const std::vector<Row>& rows,
	std::map<UID, KFCSearchEngine::FollowResult>& followed)
{
	for (size_t k = 0; k < rows.size(); ++k)
	{
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 hash = 0;
		if (db != nil && KFCResultModel::GetHitMatchIdentity(chapterIdx, rows[k].hit, story, a, b, hash)
			&& story != kInvalidUID && followed.find(story) == followed.end())
			followed[story] = KFCSearchEngine::FollowStoryFoci(chapterIdx, db, story);
	}
}

PlaceFollowed RowPlaceFollowed(int32 chapterIdx, int32 hitIdx, IDataBase* db,
	const std::map<UID, KFCSearchEngine::FollowResult>& followed, UID& outStory, TextIndex& outStart, TextIndex& outEnd)
{
	uint64 hash = 0;
	if (db == nil || !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, outStory, outStart, outEnd, hash)
		|| outStory == kInvalidUID || !db->IsValidUID(outStory))
		return kPlaceEdited;
	const std::map<UID, KFCSearchEngine::FollowResult>::const_iterator f = followed.find(outStory);
	if (f == followed.end())
		return kPlaceEdited;
	if (f->second == KFCSearchEngine::kFollowBroken)
		return kPlaceNotFollowed;
	if (f->second == KFCSearchEngine::kFollowNone)
		return StoryAsKFCLeftIt(chapterIdx, db, outStory) ? kPlaceStands : kPlaceNotFollowed;
	TextIndex fs = kInvalidTextIndex, fe = kInvalidTextIndex;
	return (KFCRowFoci::Where(chapterIdx, hitIdx, db, fs, fe) == KFCRowFoci::kFocused && fs == outStart && fe == outEnd)
		? kPlaceStands : kPlaceEdited;
}

// One row to put back, as the doors found it.
struct RestoreRow
{
	int32						hit;
	UID							story;
	TextIndex					start;		// what its Replace wrote: [start, end)
	TextIndex					end;
	KFCResultModel::HitCopy		copy;
	PMString					written;	// Hit::replacedText
	bool						done;
	RestoreRefusal				why;		// why it has not been put back (yet - a round can still do it)
	RestoreRow() : hit(-1), story(kInvalidUID), start(kInvalidTextIndex), end(kInvalidTextIndex), done(false), why(kRestoreOk) {}
};

// One document's rows in the write.
struct RestoreChapter
{
	int32					chapterIdx;
	UIDRef					docRef;
	std::vector<RestoreRow>	rows;		// the rows that passed the doors, in the request's order
	std::set<UID>			stories;	// their stories - the ones the write moves
	int32					restored;
	RestoreChapter() : chapterIdx(-1), restored(0) {}
};

KFCCopyStore::CopyRef RefOf(const KFCResultModel::HitCopy& copy)
{
	KFCCopyStore::CopyRef ref;
	ref.store = copy.store;
	ref.story = copy.story;
	return ref;
}

// THE DESTINATIONS OF A DOCUMENT, by UID - what a paste may add to (PutLinksBack lets go the ones it made that nothing names).
void DestinationsOf(IDataBase* db, std::set<UID>& out)
{
	InterfacePtr<IHyperlinkTable> table(db, db->GetRootUID(), IID_IHYPERLINKTABLE);
	if (table == nil)
		return;
	for (UniqueKeyToUIDMap::const_iterator d = table->GetDestinationKeyToUIDMapBeginIter();
		 d != table->GetDestinationKeyToUIDMapEndIter(); ++d)
		out.insert(d->second);
}

// THE LINKS THE PASTE WOULD REPLACE WHOLE - MOVED OUT OF ITS WAY FIRST, OR THE ROW IS LEFT. A source whose whole text
// lies on [at, pasteEnd) is left by the paste with no text beside the paste's own copy of it (measured: "L1 1" on the
// text put back, "L1" on none - cases restore-link-inside*), and giving such a source its story back took InDesign down
// (PutLinksBack). One of the row's kept links can be one: InDesign's replace writes the Change To over the match where
// it can, so a link strictly inside the match but on what it wrote over stays on what it wrote ("at sa" of "cat sat"
// stood on "og" of "dog"; the copy-restore spec's H4 was a link in the part it deletes, which goes with it). Such a
// kept source is moved onto a character beside the paste that no source stands on - right after it, else right before
// it; not the story's last return, not a character an object or a marker stands on, not a table's - so the paste leaves
// it whole, and PutLinksBack sets it back where it stood: the one link, its name and destination as they were (REP-38),
// the paste's copy of it deleted. Any other source the paste would replace whole - one made on what the Replace wrote
// since (case restore-link-made-after) - leaves the row as it is (outInTheWay), as does a kept one with no character
// beside the paste to stand on. `before` = the sources on [at, pasteEnd) now (LinksOn). False = a range could not be
// read, or InDesign would not move a source: the caller takes the step back.
bool MoveLinksOutOfPaste(ITextModel* model, const UIDRef& storyRef, TextIndex at, TextIndex pasteEnd,
	const std::vector<KFCResultModel::HitLink>& kept, const std::vector<LinkAt>& before, bool& outInTheWay)
{
	outInTheWay = false;
	// The characters beside the paste, in the order they are offered - each to one source at most: in the paste's own
	// thread (KFCCopyStore::CopyableThread - a table cell's or a footnote's too, the next-overrides-restore spec's C3), never
	// its closing character nor the character before its start.
	TextIndex threadFirst = 0, threadLast = model->GetPrimaryStoryThreadSpan() - 1;
	(void)KFCCopyStore::CopyableThread(model, at, threadFirst, threadLast);		// (left as the story's text when not one)
	std::vector<TextIndex> beside;
	if (pasteEnd < threadLast)
		beside.push_back(pasteEnd);
	if (at > threadFirst)
		beside.push_back(at - 1);
	size_t offered = 0;
	// Each kept source to move, and the character it stands on while the paste goes in.
	std::vector<std::pair<UID, TextIndex> > moves;
	for (size_t b = 0; b < before.size(); ++b)
	{
		// Part of it outside the paste stays, and it is set back from there (the copy-restore spec's H2, H3); one of no
		// text loses nothing.
		if (before[b].length <= 0 || before[b].start < at || before[b].start + before[b].length > pasteEnd)
			continue;
		bool ours = false;
		for (size_t k = 0; k < kept.size() && !ours; ++k)
			ours = (kept[k].source == before[b].source);
		TextIndex spot = kInvalidTextIndex;
		while (ours && spot == kInvalidTextIndex && offered < beside.size())
		{
			const TextIndex c = beside[offered++];
			TextIterator it(model, c);
			if (it.IsNull())
				continue;
			const UTF32TextChar ch = *it;
			if (KFCResultModel::IsMarkerNotShown(ch) || ch.GetValue() == kTextChar_Table
				|| ch.GetValue() == kTextChar_NonRomanSpecialGlyph)
				continue;
			std::vector<LinkAt> onIt;
			if (!LinksOn(model, storyRef, c, c + 1, onIt))
				return false;
			if (onIt.empty())
				spot = c;
		}
		KFC_DIAG_LOG("LINK in the paste's way: source=%u [%d,%d) ours=%d -> %d", (unsigned)before[b].source.Get(),
			(int)before[b].start, (int)(before[b].start + before[b].length), ours ? 1 : 0, (int)spot);
		if (spot == kInvalidTextIndex)
		{
			outInTheWay = true;
			return true;
		}
		moves.push_back(std::make_pair(before[b].source, spot));
	}
	if (moves.empty())
		return true;
	Utils<Facade::IHyperlinkFacade> facade;
	if (!facade)
		return false;
	IDataBase* const db = storyRef.GetDataBase();
	for (size_t m = 0; m < moves.size(); ++m)
	{
		const UIDRef source(db, moves[m].first);
		if (facade->ChangeTextSource(source, kFalse, storyRef, kTrue, moves[m].second, 1) != kSuccess)
			return false;
	}
	return true;
}

// ONE LINK PUT BACK WHERE IT STOOD, once the copy is pasted over [at, at + copied) (the copy-restore spec's C15 -
// measured live, its H1-H5). (1) A source the paste made there - one not on the range before the paste (`before`) and
// not a kept one - over which a kept source still in the document is to stand is deleted with its link: the Replace had
// kept the original on what it wrote, and the copy brought a second (one the paste would have replaced whole was moved
// out of its way first - MoveLinksOutOfPaste). A source the paste made where the Replace had deleted the original
// stays - it IS the link put back (H4). (2) Every kept source still in the document is set where it stood: its part in
// the copy at the same place from `at`, a part outside the copy where it stands now (H3: one the match crossed had lost
// what the paste replaced; H2: one around the match stands as it was). (3) The destinations the paste made that no link
// names now are let go (the facade lets a hidden one go with its link; a shared one stays).
bool PutLinksBack(ITextModel* model, const UIDRef& storyRef, TextIndex at, int32 copied,
				  const std::vector<KFCResultModel::HitLink>& kept, const std::vector<LinkAt>& before,
				  const std::set<UID>& destinationsBefore)
{
	IDataBase* const db = storyRef.GetDataBase();
	Utils<Facade::IHyperlinkFacade> facade;
	InterfacePtr<IHyperlinkTable> table(db, db->GetRootUID(), IID_IHYPERLINKTABLE);
	if (!facade || table == nil)
		return false;
	// Where each kept source still in this story is to stand. ONE WITH NO STORY IS NEVER TOUCHED: a source whose text
	// the paste replaced whole reads no owning story (measured live, the copy-restore spec's H1 and H5 - the DOM's
	// sourceText an error), and IHyperlinkFacade::ChangeTextSource giving it the story back took InDesign down
	// (HYPERLINKS.RPLN) - TakeRowCopy keeps no copy where that can happen, and MoveLinksOutOfPaste moves a kept one out
	// of the paste's way; this is the backstop.
	struct Wanted
	{
		UID			source;
		TextIndex	start, end;
	};
	std::vector<Wanted> wanted;
	for (size_t k = 0; k < kept.size(); ++k)
	{
		if (!db->IsValidUID(kept[k].source))
			continue;
		InterfacePtr<IHyperlinkTextSourceData> data(db, kept[k].source, UseDefaultIID());
		if (data == nil)
			continue;
		const UID owner = data->GetOwningStoryUID();
		KFC_DIAG_LOG("LINK kept source=%u owner=%u (story %u)", (unsigned)kept[k].source.Get(), (unsigned)owner.Get(),
			(unsigned)storyRef.GetUID().Get());
		if (owner != storyRef.GetUID())
			continue;
		TextIndex nowStart = kInvalidTextIndex;
		int32 nowLength = 0;
		{
			InterfacePtr<ITextRangeNode> node(data->GetHyperlinkRangeReference().Instantiate());
			if (node != nil)
			{
				nowStart = node->GetStartIndex();
				nowLength = node->GetRangeLength();
			}
		}
		Wanted w;
		w.source = kept[k].source;
		w.start = (kept[k].from < 0 && nowStart != kInvalidTextIndex) ? nowStart : at + kept[k].from;
		w.end = (kept[k].to > copied && nowStart != kInvalidTextIndex) ? nowStart + nowLength : at + kept[k].to;
		if (w.start < 0 || w.end < w.start)
			continue;
		wanted.push_back(w);
	}
	// (1) The paste's own sources over a kept one.
	std::vector<LinkAt> now;
	if (copied > 0 && !LinksOn(model, storyRef, at, at + copied, now))
		return false;
	for (size_t n = 0; n < now.size(); ++n)
	{
		bool known = false;
		for (size_t b = 0; b < before.size() && !known; ++b)
			known = (before[b].source == now[n].source);
		for (size_t k = 0; k < kept.size() && !known; ++k)
			known = (kept[k].source == now[n].source);
		if (known)
			continue;
		bool over = false;
		for (size_t w = 0; w < wanted.size() && !over; ++w)
			over = (now[n].start < wanted[w].end && wanted[w].start < now[n].start + now[n].length);
		if (!over)
			continue;
		KFC_DIAG_LOG("LINK the paste's own over a kept one: source=%u link=%u - deleted", (unsigned)now[n].source.Get(),
			(unsigned)now[n].hyperlink.Get());
		if (now[n].hyperlink != kInvalidUID && facade->DeleteHyperlinks(UIDList(db, now[n].hyperlink)) != kSuccess)
			return false;
		if (db->IsValidUID(now[n].source) && facade->DeleteHyperlinkSource(UIDList(db, now[n].source)) != kSuccess)
			return false;
	}
	// (2) The kept sources where they stood.
	for (size_t w = 0; w < wanted.size(); ++w)
	{
		KFC_DIAG_LOG("LINK kept source=%u set to [%d,%d)", (unsigned)wanted[w].source.Get(), (int)wanted[w].start,
			(int)wanted[w].end);
		if (facade->ChangeTextSource(UIDRef(db, wanted[w].source), kFalse, storyRef, kTrue, wanted[w].start,
									 wanted[w].end - wanted[w].start) != kSuccess)
			return false;
	}
	// (3) The destinations the paste made that no link names now.
	std::set<UID> destinationsNow, named;
	DestinationsOf(db, destinationsNow);
	for (int32 i = 0; i < table->GetHyperlinkCount(); ++i)
	{
		InterfacePtr<IHyperlink> link(db, table->GetNthHyperlink(i), UseDefaultIID());
		// The destination as a UIDRef (IHyperlink.h: GetDestinationUID is deprecated - "USE SAFER
		// GetDestinationUIDRef"); a link to another document (IsExternalDestination) names none of this one's.
		if (link != nil && !link->IsExternalDestination())
			named.insert(link->GetDestinationUIDRef().GetUID());
	}
	UIDList unnamed(db);
	for (std::set<UID>::const_iterator d = destinationsNow.begin(); d != destinationsNow.end(); ++d)
		if (destinationsBefore.count(*d) == 0 && named.count(*d) == 0)
			unnamed.Append(*d);
	if (unnamed.Length() > 0)
	{
		KFC_DIAG_LOG("LINK destinations the paste made, named by none: %d - let go", (int)unnamed.Length());
		if (facade->DeleteHyperlinkDestination(unnamed) != kSuccess)
			return false;
	}
	return true;
}

// THE FORMATTING OF THE PARAGRAPH A MATCH STARTS IN, PUT BACK once its copy is pasted (the copy-restore spec's C16):
// the style it had, with what the Replace left on it cleared, then its own overrides as they read before the write
// (TakeRowCopy). The paste cannot - its first paragraph takes the attributes of the paragraph it is pasted into
// (measured live, R13b). The paragraph is the one that holds `at`, with its return (ParagraphOf - the paragraph the
// copy's run-on rest ends at).
bool PutParagraphBack(ITextModel* model, TextIndex at, const KFCResultModel::HitCopy& copy)
{
	InterfacePtr<ITextModelCmds> cmds(model, UseDefaultIID());
	TextIndex paraStart = kInvalidTextIndex;
	int32 length = 0;		// the paragraph with its return
	if (cmds == nil || at < 0 || !ParagraphOf(model, at, paraStart, length))
		return false;
	InterfacePtr<ICommand> style(cmds->ApplyStyleCmd(paraStart, length, copy.paraStyle, kParaAttrStrandBoss,
		kTrue /*replaceOverrides*/, kFalse /*autoNextStyle*/, kFalse /*keepOldStyleAsOverrides*/));
	if (style == nil || CmdUtils::ProcessCommand(style) != kSuccess)
		return false;
	if (copy.paraOverrides != nil && copy.paraOverrides->CountBosses() > 0)
	{
		InterfacePtr<ICommand> overrides(cmds->ApplyCmd(paraStart, length, copy.paraOverrides, kParaAttrStrandBoss));
		if (overrides == nil || CmdUtils::ProcessCommand(overrides) != kSuccess)
			return false;
	}
	return true;
}

// IS THE PARAGRAPH STYLE A COPY PUTS BACK (the copy-restore spec's C16 - PutParagraphBack) STILL ONE OF THE DOCUMENT'S?
// A style deleted since the Replace would be given to InDesign by a UID nothing holds - what ApplyStyleCmd does with
// one is not known, and is not tried: the row is left as it is (RestoreHits' doors).
bool ParagraphStyleStands(IDataBase* db, UID style)
{
	if (db == nil || style == kInvalidUID || !db->IsValidUID(style))
		return false;
	InterfacePtr<IStyleInfo> info(db, style, UseDefaultIID());
	return info != nil && info->GetStyleType() == IStyleInfo::kParagraphStyle;
}

// THE PASTES OF ONE DOCUMENT, inside the write's step: every row of the chapter that stands taken as "this far into
// this thread" (RowNow - a Replace's way), the rows to put back pasted in text order, each checked again just before
// its paste - what its Replace wrote, its run-on rest (by its hash, ending at a paragraph's return or the story's) and
// the hyperlinks the paste would replace whole (MoveLinksOutOfPaste) - in rounds until a round puts none back. Then
// every row where its text stands: the ones put back as before their Replace (MarkHitRestored), the rest of the list
// carried. The rows not put back are counted by why. False = an error stands, or InDesign would not paste a copy: the
// caller takes the step back.
bool RestoreInChapter(RestoreChapter& chapter, int32 (&counts)[kRestoreRefusalCount])
{
	IDataBase* const db = chapter.docRef.GetDataBase();
	const int32 chapterIdx = chapter.chapterIdx;
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	if (db == nil || hitCount <= 0)
		return false;
	std::vector<RowNow> rowNow(static_cast<size_t>(hitCount));
	std::map<int32, size_t> restoring;		// hit -> its place in chapter.rows
	for (size_t k = 0; k < chapter.rows.size(); ++k)
		restoring[chapter.rows[k].hit] = k;
	std::vector<int32> keptRows;			// every other row that stands - read back at the end
	for (int32 i = 0; i < hitCount; ++i)
	{
		const std::map<int32, size_t>::const_iterator r = restoring.find(i);
		if (r != restoring.end())
		{
			const RestoreRow& row = chapter.rows[r->second];
			SetRowAt(db, rowNow[static_cast<size_t>(i)], row.story, row.start, row.end);	// the doors have just read it there
			continue;
		}
		// ONLY A ROW WHOSE STORED PLACE IS STILL ITS TEXT is carried and read back (a Replace's rule).
		KeepRowIfItStands(chapterIdx, i, db, rowNow, keptRows);
	}

	std::set<UID> pastedStories;
	bool progress = true;
	while (progress)
	{
		progress = false;
		// The rows still to put back, in text order NOW (their story, then where they start).
		std::vector<std::pair<std::pair<UID, TextIndex>, size_t> > order;
		for (size_t k = 0; k < chapter.rows.size(); ++k)
		{
			RestoreRow& row = chapter.rows[k];
			if (row.done)
				continue;
			TextIndex now = kInvalidTextIndex;
			if (!RowStartNow(db, rowNow[static_cast<size_t>(row.hit)], now))
			{
				row.why = kRestoreEdited;		// its thread went with an object another paste took away
				continue;
			}
			order.push_back(std::make_pair(std::make_pair(row.story, now), k));
		}
		std::sort(order.begin(), order.end());
		for (size_t o = 0; o < order.size(); ++o)
		{
			RestoreRow& row = chapter.rows[order[o].second];
			TextIndex at = kInvalidTextIndex;
			if (!RowStartNow(db, rowNow[static_cast<size_t>(row.hit)], at))
			{
				row.why = kRestoreEdited;
				continue;
			}
			const RowThread thread = rowNow[static_cast<size_t>(row.hit)].thread;
			const TextIndex offset = rowNow[static_cast<size_t>(row.hit)].offset;
			const int32 written = rowNow[static_cast<size_t>(row.hit)].length;
			const UIDRef storyRef(db, row.story);
			InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
			const TextIndex pasteEnd = at + written + row.copy.rest;
			// The paste inside the thread it stands in (the next-overrides-restore spec's C3).
			TextIndex threadFirst = kInvalidTextIndex, threadLast = kInvalidTextIndex;
			const bool inThread = model != nil && KFCCopyStore::CopyableThread(model, at, threadFirst, threadLast)
				&& pasteEnd <= threadLast;
			// What its Replace wrote, at its place now.
			if (!inThread || KFCSearchEngine::ReadText(storyRef, at, written) != row.written)
			{
				row.why = kRestoreEdited;
				continue;
			}
			// Its run-on rest as it was copied, the paragraph's return (or its thread's closing character) right after it.
			if (row.copy.rest > 0)
			{
				bool asCopied = (KFCSearchEngine::TextHash(model, at + written, pasteEnd) == row.copy.restHash);
				if (asCopied && pasteEnd < threadLast)
				{
					TextIterator it(model, pasteEnd);
					asCopied = !it.IsNull() && (*it).GetValue() == kTextChar_CR;
				}
				if (!asCopied)
				{
					row.why = kRestoreRestChanged;
					continue;
				}
			}
			const KFCCopyStore::CopyRef ref(RefOf(row.copy));
			const int32 copied = KFCCopyStore::Length(ref);
			// The links on the range before the paste (the copy-restore spec's C15) and, when the copy touched some,
			// the document's destinations - so that what the paste makes is told from what was there. A link the paste
			// would replace whole is moved out of its way first, or the row is left (MoveLinksOutOfPaste). (Fault
			// switch restore-link-in-way, a test build's only - KFCDiag.h: neither, the old way - the paste leaves such
			// a source with no text.)
			std::vector<LinkAt> linksBefore;
			std::set<UID> destinationsBefore;
			if ((pasteEnd > at || !row.copy.links.empty()) && !LinksOn(model, storyRef, at, pasteEnd, linksBefore))
				return false;
			bool inTheWay = false;
			if (!KFC_DIAG_FAULT("restore-link-in-way")
				&& !MoveLinksOutOfPaste(model, storyRef, at, pasteEnd, row.copy.links, linksBefore, inTheWay))
				return false;
			if (inTheWay)
			{
				row.why = kRestoreLinkInTheWay;
				continue;
			}
			if (!row.copy.links.empty())
				DestinationsOf(db, destinationsBefore);
			if (copied < row.copy.rest || !KFCCopyStore::PasteOver(ref, storyRef, at, pasteEnd))
				return false;
			// A Replace that set paragraph formatting: the starting paragraph's own put back (the copy-restore spec's
			// C16) - no length changes, so the rows' places below hold.
			if (row.copy.paraStyle != kInvalidUID && !PutParagraphBack(model, at, row.copy))
				return false;
			// The one link back where it stood (the copy-restore spec's C15) - no length changes either.
			if (!row.copy.links.empty()
				&& !PutLinksBack(model, storyRef, at, copied, row.copy.links, linksBefore, destinationsBefore))
				return false;
			// The paste turned the W characters written into the match (M); the rest was pasted over with itself. Every row
			// after the written text in this thread moves by M - W (its rest's rows among them); then this row at the match.
			const int32 match = copied - row.copy.rest;
			CarryRowsPast(rowNow, thread, offset, offset + written, match);
			SetRowAt(db, rowNow[static_cast<size_t>(row.hit)], row.story, at, at + match);
			row.done = true;
			row.why = kRestoreOk;
			++chapter.restored;
			pastedStories.insert(row.story);
			progress = true;
		}
	}
	for (size_t k = 0; k < chapter.rows.size(); ++k)
		if (!chapter.rows[k].done)
			++counts[chapter.rows[k].why != kRestoreOk ? chapter.rows[k].why : kRestoreEdited];
	if (chapter.restored == 0)
		return true;		// nothing pasted here: nothing moved, nothing to read back

	// EVERY ROW WHERE ITS TEXT STANDS NOW - read once the chapter has stopped changing (a Replace's read-back,
	// ReadBackRows): the rows carried, the rows left (replaced still, moved by the pastes before them), and the ones
	// put back, as before their Replace.
	ReadBackRows(chapterIdx, db, rowNow, keptRows);
	for (size_t k = 0; k < chapter.rows.size(); ++k)
	{
		const RestoreRow& row = chapter.rows[k];
		const RowNow& place = rowNow[static_cast<size_t>(row.hit)];
		TextIndex s = kInvalidTextIndex;
		if (!RowStartNow(db, place, s))
			continue;
		if (row.done)
			KFCResultModel::MarkHitRestored(chapterIdx, row.hit, row.story, s, s + place.length);
		else
			KFCResultModel::SetHitRange(chapterIdx, row.hit, row.story, s, s + place.length);
		KFCSearchEngine::RereadRowText(chapterIdx, row.hit, UIDRef(db, row.story), s, s + place.length);
	}
	// The story rows' first words (a paste can be among them) - each row backed up before it takes them.
	for (std::set<UID>::const_iterator ps = pastedStories.begin(); ps != pastedStories.end(); ++ps)
		KFCSearchEngine::RereadStoryLead(chapterIdx, UIDRef(db, *ps));
	return true;
}

}	// anonymous namespace

// A replaced text row WITH ITS COPY, by the row alone (RestoreRowRefusal - the copy-restore spec's C7; the author's
// call: a menu item that cannot run is greyed). What only the document can tell - edited since, the rest changed,
// locked, a hyperlink in the way, the paragraph style deleted - the doors say when it is pressed.
bool KFCReplaceEngine::CanRestoreHit(int32 chapterIdx, int32 hitIdx)
{
	if (KFCResultModel::ListHasOwnChangeTab())
		return false;		// (RefuseOwnTabRow's note)
	KFCResultModel::HitCopy copy;
	return RestoreRowRefusal(chapterIdx, hitIdx, copy) == kRestoreOk;
}

bool KFCReplaceEngine::RestoreHits(const KFCRowsByChapter& rowsByChapter, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("restore-rows");	// (test builds: after DocsAfterWrite, below - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (RefuseRunListRow("Restore from Copy", outStatus) || RefuseOwnTabRow("Restore from Copy", outStatus))
		return false;
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const KFCReplaceEngine::WriteUnderWay replacing;	// counted from the door to the hand-back (IsReplacing)
	const size_t total = KFCRowCount(rowsByChapter);
	if (total == 0)
		return false;

	// THE DOORS, ROW BY ROW (the copy-restore spec's C8) - nothing written yet. What each document is left as is
	// DocsAfterWrite's, on every way out.
	int32 counts[kRestoreRefusalCount] = {};
	RestoreRefusal lastWhy = kRestoreOk;
	int32 lastWhyNoCopy = KFCResultModel::kCopyNone;
	std::vector<RestoreChapter> chapters;
	KFCReplaceEngine::DocsAfterWrite docsAfter;
	size_t toWrite = 0;
	for (size_t c = 0; c < rowsByChapter.size(); ++c)
	{
		RestoreChapter chapter;
		chapter.chapterIdx = rowsByChapter[c].chapter;
		// The row's own flags and copy first: a chapter with nothing to put back is not opened.
		std::vector<RestoreRow> candidates;
		const std::vector<int32>& hits = rowsByChapter[c].hits;
		for (size_t k = 0; k < hits.size(); ++k)
		{
			RestoreRow row;
			row.hit = hits[k];
			// The menu's question (RestoreRowRefusal), and the copy still held by its store.
			RestoreRefusal why = RestoreRowRefusal(chapter.chapterIdx, row.hit, row.copy);
			if (why == kRestoreOk && KFCCopyStore::Length(RefOf(row.copy)) < 0)
				why = kRestoreNoCopy;
			if (why != kRestoreOk)
			{
				++counts[why];
				lastWhy = why;
				lastWhyNoCopy = row.copy.whyNot;
				continue;
			}
			candidates.push_back(row);
		}
		if (candidates.empty())
			continue;
		// Its document, by its file first (a UIDRef can outlive its document - KFCBookScope::ReachChapterDoc): a closed
		// chapter is reopened windowless and held, as a Replace reaches it.
		IDFile file;
		RestoreRefusal docWhy = kRestoreOk;
		if (!KFCResultModel::GetChapterLocation(chapter.chapterIdx, chapter.docRef, file))
			docWhy = kRestoreNoDoc;
		else if (!KFCBookScope::ReachChapterDoc(file, chapter.docRef))
			docWhy = kRestoreNotOpened;
		if (docWhy != kRestoreOk)
		{
			counts[docWhy] += static_cast<int32>(candidates.size());
			lastWhy = docWhy;
			continue;
		}
		KFCResultModel::RebindChapterDoc(chapter.chapterIdx, chapter.docRef);
		IDataBase* const db = chapter.docRef.GetDataBase();
		// THE ROWS OF EACH STORY FOLLOW THEIR FOCI FIRST (as a Replace's do), once per story - then each row's place
		// (RowPlaceFollowed).
		std::map<UID, KFCSearchEngine::FollowResult> followed;
		FollowStoriesOf(chapter.chapterIdx, db, candidates, followed);
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			RestoreRow& row = candidates[k];
			const PlaceFollowed place = RowPlaceFollowed(chapter.chapterIdx, row.hit, db, followed, row.story,
				row.start, row.end);
			RestoreRefusal why = kRestoreOk;
			if (place == kPlaceNotFollowed)
				why = kRestoreNotFollowed;
			else if (place == kPlaceEdited)
				why = kRestoreEdited;
			// What its Replace wrote, still at its place.
			if (why == kRestoreOk && (!KFCResultModel::GetHitWrittenText(chapter.chapterIdx, row.hit, row.written)
				|| KFCSearchEngine::ReadText(UIDRef(db, row.story), row.start, row.end - row.start) != row.written))
				why = kRestoreEdited;
			// The range the paste covers, inside the thread it stands in - the story's text, a table cell's, a footnote's
			// (KFCCopyStore::CopyableThread, which asks it again: the commands check nothing - the spec's C3).
			if (why == kRestoreOk)
			{
				InterfacePtr<ITextModel> model(db, row.story, UseDefaultIID());
				TextIndex threadFirst = kInvalidTextIndex, threadLast = kInvalidTextIndex;
				if (model == nil || !KFCCopyStore::CopyableThread(model, row.start, threadFirst, threadLast)
					|| row.end + row.copy.rest > threadLast)
					why = kRestoreRestChanged;
			}
			// Not locked since (a Replace's own question - IsFrameEditable).
			if (why == kRestoreOk)
			{
				const UIDRef storyRef(db, row.story);
				if (!KFCSearchEngine::IsFrameEditable(storyRef, KFCSearchEngine::EditableFrameForMatch(storyRef, row.start)))
					why = kRestoreLocked;
			}
			// The paragraph style it puts back, when it puts one back, still in the document (ParagraphStyleStands).
			if (why == kRestoreOk && row.copy.paraStyle != kInvalidUID && !ParagraphStyleStands(db, row.copy.paraStyle))
				why = kRestoreStyleGone;
			if (why != kRestoreOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			chapter.rows.push_back(row);
			chapter.stories.insert(row.story);
		}
		// Its "unsaved" flag read now when it has rows to write (a document whose flag was not read is never made clean).
		docsAfter.Reached(chapter.docRef, !chapter.rows.empty());
		toWrite += chapter.rows.size();
		if (!chapter.rows.empty())
			chapters.push_back(chapter);
	}
	if (toWrite == 0)
	{
		SayNoneRestored(outStatus, total, lastWhy, lastWhyNoCopy, counts);
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last is reached (a Replace's rule: reaching a closed chapter runs the
	// message loop).
	const bool acrossDocuments = chapters.size() > 1;
	for (size_t c = 0; acrossDocuments && c < chapters.size(); ++c)
	{
		if (!KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			outStatus = "Restore from Copy: a document of the selected rows has been closed - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
	}

	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (the copy-restore spec's C11): one step for every document of it, by
	// its name.
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
		chapterIdxs.push_back(chapters[c].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	recorder.SetName("Restore from Copy");
	bool ok = false;
	bool pasteFailed = false;
	int32 restored = 0;
	{
		KFCReplaceEngine::WriteStep step;
		if (!KFCReplaceEngine::BeginWriteStep(acrossDocuments, step, kKFCRestoreOriginalStepKey, "KFC Restore from Copy"))
		{
			outStatus = acrossDocuments ? "Restore from Copy: InDesign would not start an undoable step - nothing was changed."
				: "Restore from Copy: InDesign would not start a command sequence - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
		for (size_t c = 0; c < chapters.size() && !pasteFailed; ++c)
		{
			if (!RestoreInChapter(chapters[c], counts))
				pasteFailed = true;
			restored += chapters[c].restored;
		}
		// THE ERROR STATE AT THE END (a Replace's rule): anything standing here is a failure nothing reported.
		ok = !pasteFailed && restored > 0 && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				if (chapters[c].restored > 0)
					KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());	// in this step, so its Undo / Redo is heard
		// THE STORIES' FOCI ON THE ROWS, IN THIS STEP (the row-focus plan's D7): the pastes moved them, and a row put
		// back has its own focus put on the text it reads now - so this step's Undo and Redo hold them.
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
					KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s, true);
		KFCReplaceEngine::EndWriteStep(step, ok);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		// The pastes that went in and were taken back moved the stories' foci without moving them back: the stories are
		// as the doors found them, so the rows' places are exact.
		for (size_t c = 0; c < chapters.size(); ++c)
			for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
				KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s);
		if (pasteFailed || restored > 0)
		{
			outStatus = "Restore from Copy: InDesign would not put a copy back - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
		// One row passed the doors and its check in the step did not: its own reason from there (several are counted).
		RestoreRefusal why = kRestoreEdited;
		for (size_t c = 0; c < chapters.size(); ++c)
			for (size_t k = 0; k < chapters[c].rows.size(); ++k)
				if (chapters[c].rows[k].why != kRestoreOk)
					why = chapters[c].rows[k].why;
		SayNoneRestored(outStatus, total, why, KFCResultModel::kCopyKept, counts);
		return false;
	}
	for (size_t c = 0; c < chapters.size(); ++c)
		NoteStoryVersions(chapters[c].chapterIdx, chapters[c].docRef.GetDataBase(), chapters[c].stories);	// as KFC left them
	// Kept AFTER the versions are noted - they are the "after" an Undo's "before" is put back over.
	recorder.Keep();
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (chapters[c].restored == 0)
			continue;
		docsAfter.Wrote(chapters[c].docRef);		// written to: a chapter of ours has to be seen and saved
		KFC_DIAG_NOTE_WRITE(chapters[c].docRef);
	}

	// WHAT IT DID (the copy-restore spec's C12) - the Replace's words (SayRowsWritten): "Restored ID:262 #3." /
	// "Restored 2 of 3 rows: ID:262 #1, ID:262 #3. Left as they were: 1 edited since." /
	// "Restored 3 rows in 2 documents: a.indd ID:262 #1, ID:262 #3; b.indd ID:270 #1."
	PMString leftWords;
	AppendCountsByWhy(leftWords, counts, RestoreWords);
	SayRowsWritten(outStatus, "Restored", "rows", RowsDone(chapters), total, leftWords);
	return true;
}

// ======================================================================================================
// A TEXT ROW'S SPECIAL OPERATIONS > CREATE OUTLINES (docs/superpowers/specs/2026-10-18-kfc-text-outlines-design.md -
// the author: "only the matched characters, Create Outlines - a special operation for text"). The rows' matches
// outlined as InDesign's Type > Create Outlines does them (ICreateOutlinesUtils::CreateCOTextCmd -
// SnpInspectPathInfo.cpp's call, with the original deleted: the outlines go in as inline objects, one a line of the
// match - M7). Restore from Copy's shape: the doors row by row (its place followed by its focus, its text as the row
// holds it, not locked, not overset), the rows written story by story in text order, each checked again just before its
// command, every other row carried past what the command turned the match into - by the story's length before and after
// it, since that is not a fixed number (M7: "aaaa bbbb" over two lines becomes outline, space, outline). ONE undo step
// ("Create Outlines" - the object rows' name), KFC's mark in it.
// ======================================================================================================

namespace
{

// WHY A ROW IS NOT OUTLINED - said in full for one row (OutlineSentence), counted for several (OutlineWords).
enum OutlineRefusal
{
	kOutlineOk = 0,
	kOutlineNotText,		// an object row (its own Create Outlines is KFCObjectOutline's)
	kOutlineDone,			// outlined already
	kOutlineEmpty,			// a match of no characters (GREP's ^, $)
	kOutlineNoDoc,			// its document could not be found
	kOutlineNotOpened,		// ...or not opened
	kOutlineNotFollowed,	// its story changed in a way its foci could not follow
	kOutlineEdited,			// its text has been edited since
	kOutlineOverset,		// in overset text - InDesign refuses it (the outlines spec's M7: error 80650)
	kOutlineLocked,			// locked
	kOutlineRefusalCount
};

const char* OutlineWords(OutlineRefusal why)
{
	switch (why)
	{
		case kOutlineNotText:		return "not a text row";
		case kOutlineDone:			return "outlined already";
		case kOutlineEmpty:			return "no characters";
		case kOutlineNoDoc:			return "document not found";
		case kOutlineNotOpened:		return "document not opened";
		case kOutlineNotFollowed:	return "not followed";
		case kOutlineEdited:		return "edited since";
		case kOutlineOverset:		return "in overset text";
		case kOutlineLocked:		return "locked";
		default:					return "";
	}
}

// One row's refusal, after "Create Outlines: ".
const char* OutlineSentence(OutlineRefusal why)
{
	switch (why)
	{
		case kOutlineNotText:		return "this row is not a text row.";
		case kOutlineDone:			return "this row is outlined already.";
		case kOutlineEmpty:			return "this row's match has no characters to outline.";
		case kOutlineNoDoc:			return "the document of this row could not be found.";
		case kOutlineNotOpened:		return "the document of this row could not be opened.";
		case kOutlineNotFollowed:	return "the story of this row has changed in a way KohakuFindChange cannot follow - left as it is.";
		case kOutlineEdited:		return "the text of this row has been edited since - left as it is.";
		case kOutlineOverset:		return "the match is in overset text, which InDesign cannot outline - left as it is.";
		case kOutlineLocked:		return "the text is locked (a locked layer or story) - left as it is.";
		default:					return "";
	}
}

// A CREATE OUTLINES THAT OUTLINED NOTHING: one row's reason in full (OutlineSentence), several rows' counted by
// reason - "2 in overset text, 1 locked".
void SayNoneOutlined(PMString& out, size_t total, OutlineRefusal oneWhy, const int32 (&counts)[kOutlineRefusalCount])
{
	out = "Create Outlines: ";
	if (total == 1)
		out.Append(OutlineSentence(oneWhy));
	else
	{
		out.Append("none of the ");
		out.AppendNumber(static_cast<int32>(total));
		out.Append(" selected rows can be outlined - ");
		AppendCountsByWhy(out, counts, OutlineWords);
		out.Append(".");
	}
	out.SetTranslatable(kFalse);
}

// CAN THE ROW BE OUTLINED, BY THE ROW ALONE (the outlines spec's X9) - asked in one place: the menu greys
// Create Outlines by it (CanOutlineHit), and OutlineHits' first door counts a row by it. A text row not outlined yet,
// not locked, not in overset text, not of zero width - one reason a row, in that order.
OutlineRefusal OutlineRowRefusal(int32 chapterIdx, int32 hitIdx)
{
	KFCResultModel::RowDisplay look;
	bool replaced = false, locked = false;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID)
		return kOutlineNotText;
	if (!KFCResultModel::GetHitRow(chapterIdx, hitIdx, look) || look.outlined)
		return kOutlineDone;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked) || locked)
		return kOutlineLocked;
	if (KFCResultModel::IsHitOverset(chapterIdx, hitIdx))
		return kOutlineOverset;
	if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash) || end <= start)
		return kOutlineEmpty;
	return kOutlineOk;
}

// One row to outline, as the doors found it.
struct OutlineRow
{
	int32			hit;
	UID				story;
	TextIndex		start;		// its match: [start, end)
	TextIndex		end;
	PMString		original;	// the characters there, read by the doors - checked again before the write, kept for the row
	bool			done;
	OutlineRefusal	why;
	OutlineRow() : hit(-1), story(kInvalidUID), start(kInvalidTextIndex), end(kInvalidTextIndex), done(false), why(kOutlineOk) {}
};

// One document's rows in the write.
struct OutlineChapter
{
	int32					chapterIdx;
	UIDRef					docRef;
	std::vector<OutlineRow>	rows;		// the rows that passed the doors
	std::set<UID>			stories;	// their stories - the ones the write moves
	int32					outlined;
	OutlineChapter() : chapterIdx(-1), outlined(0) {}
};

// THE OUTLINES OF ONE DOCUMENT, inside the write's step (RestoreInChapter's shape): every row of the chapter that stands
// taken as "this far into this thread" (RowNow), the rows to outline in text order NOW, each checked again just before its
// command - its characters, overset - then outlined, and every row after it in its thread carried by what the match became
// (the story's length before and after the command). Then every row where its text stands: the outlined ones Outlined
// (MarkHitOutlined), the rest carried. False = InDesign would not outline a match: the caller takes the step back.
bool OutlineInChapter(OutlineChapter& chapter, int32 (&counts)[kOutlineRefusalCount])
{
	IDataBase* const db = chapter.docRef.GetDataBase();
	const int32 chapterIdx = chapter.chapterIdx;
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	Utils<ICreateOutlinesUtils> outlines;
	if (db == nil || hitCount <= 0 || !outlines)
		return false;
	std::vector<RowNow> rowNow(static_cast<size_t>(hitCount));
	std::map<int32, size_t> outlining;		// hit -> its place in chapter.rows
	for (size_t k = 0; k < chapter.rows.size(); ++k)
		outlining[chapter.rows[k].hit] = k;
	std::vector<int32> keptRows;			// every other row that stands - read back at the end
	for (int32 i = 0; i < hitCount; ++i)
	{
		const std::map<int32, size_t>::const_iterator r = outlining.find(i);
		if (r != outlining.end())
		{
			const OutlineRow& row = chapter.rows[r->second];
			SetRowAt(db, rowNow[static_cast<size_t>(i)], row.story, row.start, row.end);	// the doors have just read it there
			continue;
		}
		KeepRowIfItStands(chapterIdx, i, db, rowNow, keptRows);
	}

	// The rows to outline in text order (their story, then where they start now).
	std::vector<std::pair<std::pair<UID, TextIndex>, size_t> > order;
	for (size_t k = 0; k < chapter.rows.size(); ++k)
	{
		TextIndex now = kInvalidTextIndex;
		if (!RowStartNow(db, rowNow[static_cast<size_t>(chapter.rows[k].hit)], now))
		{
			chapter.rows[k].why = kOutlineEdited;
			continue;
		}
		order.push_back(std::make_pair(std::make_pair(chapter.rows[k].story, now), k));
	}
	std::sort(order.begin(), order.end());
	std::set<UID> outlinedStories;
	for (size_t o = 0; o < order.size(); ++o)
	{
		OutlineRow& row = chapter.rows[order[o].second];
		TextIndex at = kInvalidTextIndex;
		if (!RowStartNow(db, rowNow[static_cast<size_t>(row.hit)], at))
		{
			row.why = kOutlineEdited;
			continue;
		}
		const RowThread thread = rowNow[static_cast<size_t>(row.hit)].thread;
		const TextIndex offset = rowNow[static_cast<size_t>(row.hit)].offset;
		const int32 length = rowNow[static_cast<size_t>(row.hit)].length;
		const UIDRef storyRef(db, row.story);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || length <= 0 || KFCSearchEngine::ReadText(storyRef, at, length) != row.original)
		{
			row.why = kOutlineEdited;
			continue;
		}
		if (KFCSearchEngine::IsPositionOverset(storyRef, at))
		{
			row.why = kOutlineOverset;
			continue;
		}
		const int32 before = model->TotalLength();
		InterfacePtr<ICommand> cmd(outlines->CreateCOTextCmd(storyRef, at, at + length, kTrue /*deleteOriginal*/));
		if (cmd == nil || CmdUtils::ProcessCommand(cmd) != kSuccess)
			return false;
		// WHAT THE MATCH BECAME (the outlines spec's M7): its characters minus what the story lost - an inline a line,
		// the spaces and returns between kept as text.
		const int32 became = length + (model->TotalLength() - before);
		KFC_DIAG_LOG("TEXTOUTLINE chapter=%d row=%d story=%u at=%d length=%d became=%d items=%d", chapterIdx, row.hit,
			row.story.Get(), (int)at, (int)length, (int)became, (int)cmd->GetItemList()->Length());
		if (became <= 0)
			return false;		// (never measured - a match the command took away whole is not a row to keep)
		CarryRowsPast(rowNow, thread, offset, offset + length, became);
		SetRowAt(db, rowNow[static_cast<size_t>(row.hit)], row.story, at, at + became);
		row.done = true;
		row.why = kOutlineOk;
		++chapter.outlined;
		outlinedStories.insert(row.story);
	}
	for (size_t k = 0; k < chapter.rows.size(); ++k)
		if (!chapter.rows[k].done)
			++counts[chapter.rows[k].why != kOutlineOk ? chapter.rows[k].why : kOutlineEdited];
	if (chapter.outlined == 0)
		return true;		// nothing outlined here: nothing moved, nothing to read back

	// EVERY ROW WHERE ITS TEXT STANDS NOW (a Replace's read-back, ReadBackRows): the rows carried, the rows left, and
	// the outlined ones.
	ReadBackRows(chapterIdx, db, rowNow, keptRows);
	for (size_t k = 0; k < chapter.rows.size(); ++k)
	{
		const OutlineRow& row = chapter.rows[k];
		const RowNow& place = rowNow[static_cast<size_t>(row.hit)];
		TextIndex s = kInvalidTextIndex;
		if (!RowStartNow(db, place, s))
			continue;
		if (row.done)
			KFCResultModel::MarkHitOutlined(chapterIdx, row.hit, row.story, s, s + place.length, row.original,
				KFCSearchEngine::ReadText(UIDRef(db, row.story), s, place.length));
		else
			KFCResultModel::SetHitRange(chapterIdx, row.hit, row.story, s, s + place.length);
		KFCSearchEngine::RereadRowText(chapterIdx, row.hit, UIDRef(db, row.story), s, s + place.length);
	}
	for (std::set<UID>::const_iterator os = outlinedStories.begin(); os != outlinedStories.end(); ++os)
		KFCSearchEngine::RereadStoryLead(chapterIdx, UIDRef(db, *os));
	return true;
}

}	// anonymous namespace

bool KFCReplaceEngine::CanOutlineHit(int32 chapterIdx, int32 hitIdx)
{
	if (KFCResultModel::ListIsRunList() || KFCResultModel::ListHasOwnChangeTab())
		return false;		// (RefuseRunListRow's and RefuseOwnTabRow's notes)
	return OutlineRowRefusal(chapterIdx, hitIdx) == kOutlineOk;
}

bool KFCReplaceEngine::OutlineHits(const KFCRowsByChapter& rowsByChapter, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("outline-text-rows");	// (test builds: after DocsAfterWrite, below - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (RefuseRunListRow("Create Outlines", outStatus) || RefuseOwnTabRow("Create Outlines", outStatus))
		return false;
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const KFCReplaceEngine::WriteUnderWay replacing;	// counted from the door to the hand-back (IsReplacing)
	const size_t total = KFCRowCount(rowsByChapter);
	if (total == 0)
		return false;

	// THE DOORS, ROW BY ROW (the outlines spec's X4) - nothing written yet. What each document is left as is
	// DocsAfterWrite's, on every way out.
	int32 counts[kOutlineRefusalCount] = {};
	OutlineRefusal lastWhy = kOutlineOk;
	std::vector<OutlineChapter> chapters;
	KFCReplaceEngine::DocsAfterWrite docsAfter;
	size_t toWrite = 0;
	for (size_t c = 0; c < rowsByChapter.size(); ++c)
	{
		OutlineChapter chapter;
		chapter.chapterIdx = rowsByChapter[c].chapter;
		// The row's own flags first: a chapter with nothing to outline is not opened.
		std::vector<OutlineRow> candidates;
		const std::vector<int32>& hits = rowsByChapter[c].hits;
		for (size_t k = 0; k < hits.size(); ++k)
		{
			OutlineRow row;
			row.hit = hits[k];
			const OutlineRefusal why = OutlineRowRefusal(chapter.chapterIdx, row.hit);		// the menu's question
			if (why != kOutlineOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			candidates.push_back(row);
		}
		if (candidates.empty())
			continue;
		// Its document, by its file first: a closed chapter is reopened windowless and held, as a Replace reaches it.
		IDFile file;
		OutlineRefusal docWhy = kOutlineOk;
		if (!KFCResultModel::GetChapterLocation(chapter.chapterIdx, chapter.docRef, file))
			docWhy = kOutlineNoDoc;
		else if (!KFCBookScope::ReachChapterDoc(file, chapter.docRef))
			docWhy = kOutlineNotOpened;
		if (docWhy != kOutlineOk)
		{
			counts[docWhy] += static_cast<int32>(candidates.size());
			lastWhy = docWhy;
			continue;
		}
		KFCResultModel::RebindChapterDoc(chapter.chapterIdx, chapter.docRef);
		IDataBase* const db = chapter.docRef.GetDataBase();
		// THE ROWS OF EACH STORY FOLLOW THEIR FOCI FIRST (a Replace's way), once per story - then each row's place
		// (RowPlaceFollowed - Restore from Copy's rule).
		std::map<UID, KFCSearchEngine::FollowResult> followed;
		FollowStoriesOf(chapter.chapterIdx, db, candidates, followed);
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			OutlineRow& row = candidates[k];
			const PlaceFollowed place = RowPlaceFollowed(chapter.chapterIdx, row.hit, db, followed, row.story,
				row.start, row.end);
			OutlineRefusal why = kOutlineOk;
			if (place == kPlaceNotFollowed)
				why = kOutlineNotFollowed;
			else if (place == kPlaceEdited)
				why = kOutlineEdited;
			// ITS TEXT AS THE ROW HOLDS IT: what its Replace wrote for a replaced row (the author's call - a replaced row can be
			// outlined), the match as it was found for any other (RowReadsAsFound - its line and its hash).
			if (why == kOutlineOk)
			{
				bool replaced = false, locked = false;
				PMString written;
				const UIDRef storyRef(db, row.story);
				if (KFCResultModel::GetHitFlags(chapter.chapterIdx, row.hit, replaced, locked) && replaced)
				{
					if (!KFCResultModel::GetHitWrittenText(chapter.chapterIdx, row.hit, written)
						|| KFCSearchEngine::ReadText(storyRef, row.start, row.end - row.start) != written)
						why = kOutlineEdited;
				}
				else if (!KFCSearchEngine::RowReadsAsFound(chapter.chapterIdx, row.hit, db))
					why = kOutlineEdited;
				if (why == kOutlineOk)
				{
					row.original = KFCSearchEngine::ReadText(storyRef, row.start, row.end - row.start);
					row.original.SetTranslatable(kFalse);
					if (KFCSearchEngine::IsPositionOverset(storyRef, row.start))
						why = kOutlineOverset;
					else if (!KFCSearchEngine::IsFrameEditable(storyRef, KFCSearchEngine::EditableFrameForMatch(storyRef, row.start)))
						why = kOutlineLocked;
				}
			}
			if (why != kOutlineOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			chapter.rows.push_back(row);
			chapter.stories.insert(row.story);
		}
		docsAfter.Reached(chapter.docRef, !chapter.rows.empty());
		toWrite += chapter.rows.size();
		if (!chapter.rows.empty())
			chapters.push_back(chapter);
	}
	if (toWrite == 0)
	{
		SayNoneOutlined(outStatus, total, lastWhy, counts);
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last is reached (a Replace's rule).
	const bool acrossDocuments = chapters.size() > 1;
	for (size_t c = 0; acrossDocuments && c < chapters.size(); ++c)
	{
		if (!KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			outStatus = "Create Outlines: a document of the selected rows has been closed - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
	}

	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (the outlines spec's X5): one step for every document of it, by the
	// object rows' name.
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
		chapterIdxs.push_back(chapters[c].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	recorder.SetName("Create Outlines");
	bool ok = false;
	bool failed = false;
	int32 outlinedRows = 0;
	{
		KFCReplaceEngine::WriteStep step;
		if (!KFCReplaceEngine::BeginWriteStep(acrossDocuments, step, kKFCCreateOutlinesStepKey, "KFC Create Outlines"))
		{
			outStatus = acrossDocuments ? "Create Outlines: InDesign would not start an undoable step - nothing was changed."
				: "Create Outlines: InDesign would not start a command sequence - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
		for (size_t c = 0; c < chapters.size() && !failed; ++c)
		{
			if (!OutlineInChapter(chapters[c], counts))
				failed = true;
			outlinedRows += chapters[c].outlined;
		}
		// THE ERROR STATE AT THE END (a Replace's rule): anything standing here is a failure nothing reported.
		ok = !failed && outlinedRows > 0 && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				if (chapters[c].outlined > 0)
					KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());	// in this step, so its Undo / Redo is heard
		// THE STORIES' FOCI ON THE ROWS, IN THIS STEP (the row-focus plan's D7).
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
					KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s, true);
		KFCReplaceEngine::EndWriteStep(step, ok);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		for (size_t c = 0; c < chapters.size(); ++c)
			for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
				KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s);
		if (failed || outlinedRows > 0)
		{
			outStatus = "Create Outlines: InDesign would not outline a match - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
		// One row passed the doors and its check in the step did not: its own reason from there (several are counted).
		OutlineRefusal why = kOutlineEdited;
		for (size_t c = 0; c < chapters.size(); ++c)
			for (size_t k = 0; k < chapters[c].rows.size(); ++k)
				if (chapters[c].rows[k].why != kOutlineOk)
					why = chapters[c].rows[k].why;
		SayNoneOutlined(outStatus, total, why, counts);
		return false;
	}
	for (size_t c = 0; c < chapters.size(); ++c)
		NoteStoryVersions(chapters[c].chapterIdx, chapters[c].docRef.GetDataBase(), chapters[c].stories);	// as KFC left them
	recorder.Keep();
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (chapters[c].outlined == 0)
			continue;
		docsAfter.Wrote(chapters[c].docRef);
		KFC_DIAG_NOTE_WRITE(chapters[c].docRef);
	}

	// WHAT IT DID (the outlines spec's X8 - SayRowsWritten): "Outlined ID:262 #3." /
	// "Outlined 2 of 3 matches: ID:262 #1, ID:262 #3. Left as they were: 1 in overset text." /
	// "Outlined 3 matches in 2 documents: a.indd ID:262 #1, ID:262 #3; b.indd ID:270 #1."
	PMString leftWords;
	AppendCountsByWhy(leftWords, counts, OutlineWords);
	SayRowsWritten(outStatus, "Outlined", "matches", RowsDone(chapters), total, leftWords);
	return true;
}

// ======================================================================================================
// A TEXT ROW'S SPECIAL OPERATIONS > CLEAR OVERRIDES (docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md
// B2 - B7): Create Outlines' doors and step (OutlineHits, above) - the overrides of the matches taken off where they stand
// (KFCTextOverrides::ClearOverrides). Only attributes change: no row moves, so none is carried.
// ======================================================================================================

namespace
{

// WHY A ROW IS NOT CLEARED - said in full for one row (ClearSentence), counted for several (ClearWords).
enum ClearRefusal
{
	kClearOk = 0,
	kClearNotText,		// an object row (its own is Clear Object Style Overrides - KFCObjectSpecial)
	kClearOutlined,		// written as outlines: its match is inline outlines now, no text to clear
	kClearEmpty,		// a match of no characters (GREP's ^, $)
	kClearNoDoc,		// its document could not be found
	kClearNotOpened,	// ...or not opened
	kClearNotFollowed,	// its story changed in a way its foci could not follow
	kClearEdited,		// its text has been edited since
	kClearLocked,		// locked
	kClearNone,			// InDesign calls its text not overridden: counted, not written (the spec's B3)
	kClearRefusalCount
};

const char* ClearWords(ClearRefusal why)
{
	switch (why)
	{
		case kClearNotText:		return "not a text row";
		case kClearOutlined:	return "outlined";
		case kClearEmpty:		return "no characters";
		case kClearNoDoc:		return "document not found";
		case kClearNotOpened:	return "document not opened";
		case kClearNotFollowed:	return "not followed";
		case kClearEdited:		return "edited since";
		case kClearLocked:		return "locked";
		case kClearNone:		return "had no overrides";
		default:				return "";
	}
}

// One row's refusal, after "Clear Overrides: ".
const char* ClearSentence(ClearRefusal why)
{
	switch (why)
	{
		case kClearNotText:		return "this row is not a text row.";
		case kClearOutlined:	return "this row's match is outlines now - there is no text to clear.";
		case kClearEmpty:		return "this row's match has no characters to clear.";
		case kClearNoDoc:		return "the document of this row could not be found.";
		case kClearNotOpened:	return "the document of this row could not be opened.";
		case kClearNotFollowed:	return "the story of this row has changed in a way KohakuFindChange cannot follow - left as it is.";
		case kClearEdited:		return "the text of this row has been edited since - left as it is.";
		case kClearLocked:		return "the text is locked (a locked layer or story) - left as it is.";
		case kClearNone:		return "the match has no overrides to clear - left as it is.";
		default:				return "";
	}
}

// A CLEAR OVERRIDES THAT CLEARED NOTHING: one row's reason in full (ClearSentence), several rows' counted by reason.
void SayNoneCleared(PMString& out, size_t total, ClearRefusal oneWhy, const int32 (&counts)[kClearRefusalCount])
{
	out = "Clear Overrides: ";
	if (total == 1)
		out.Append(ClearSentence(oneWhy));
	else
	{
		out.Append("none of the ");
		out.AppendNumber(static_cast<int32>(total));
		out.Append(" selected rows can be cleared - ");
		AppendCountsByWhy(out, counts, ClearWords);
		out.Append(".");
	}
	out.SetTranslatable(kFalse);
}

// CAN THE ROW BE CLEARED, BY THE ROW ALONE - asked in one place: the menu greys Clear Overrides by it
// (CanClearOverridesHit), and ClearOverridesHits' first door counts a row by it. A text row not outlined, not locked, not
// of zero width - one reason a row, in that order. (Overset text is cleared: nothing has to be composed - the spec's B3.)
ClearRefusal ClearRowRefusal(int32 chapterIdx, int32 hitIdx)
{
	KFCResultModel::RowDisplay look;
	bool replaced = false, locked = false;
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID)
		return kClearNotText;
	if (!KFCResultModel::GetHitRow(chapterIdx, hitIdx, look) || look.outlined)
		return kClearOutlined;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked) || locked)
		return kClearLocked;
	if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash) || end <= start)
		return kClearEmpty;
	return kClearOk;
}

// One row to clear, as the doors found it.
struct ClearRow
{
	int32			hit;
	UID				story;
	TextIndex		start;		// its match: [start, end)
	TextIndex		end;
	PMString		original;	// the characters there, read by the doors - checked again before the write
	bool			done;
	ClearRefusal	why;
	ClearRow() : hit(-1), story(kInvalidUID), start(kInvalidTextIndex), end(kInvalidTextIndex), done(false), why(kClearOk) {}
};

// One document's rows in the write.
struct ClearChapter
{
	int32					chapterIdx;
	UIDRef					docRef;
	std::vector<ClearRow>	rows;		// the rows that passed the doors
	std::set<UID>			stories;	// their stories - the ones the write changes
	int32					cleared;
	ClearChapter() : chapterIdx(-1), cleared(0) {}
};

// THE OVERRIDES OF ONE DOCUMENT, inside the write's step: each row checked again just before its command - its characters
// where the doors read them (nothing this write does moves text) - then cleared (KFCTextOverrides::ClearOverrides, which
// writes nothing where InDesign calls the text not overridden: kClearNone, counted). The cleared rows marked
// (MarkHitSpecial - "Cleared"). False = InDesign would not clear a match: the caller takes the step back.
bool ClearInChapter(ClearChapter& chapter, int32 (&counts)[kClearRefusalCount])
{
	IDataBase* const db = chapter.docRef.GetDataBase();
	if (db == nil)
		return false;
	for (size_t k = 0; k < chapter.rows.size(); ++k)
	{
		ClearRow& row = chapter.rows[k];
		const UIDRef storyRef(db, row.story);
		if (KFCSearchEngine::ReadText(storyRef, row.start, row.end - row.start) != row.original)
		{
			row.why = kClearEdited;
			continue;
		}
		bool hadAny = false;
		if (KFCTextOverrides::ClearOverrides(storyRef, row.start, row.end, hadAny) != kSuccess)
			return false;
		KFC_DIAG_LOG("CLEAROVERRIDES chapter=%d row=%d story=%u at=%d..%d hadAny=%d", chapter.chapterIdx, row.hit,
			row.story.Get(), (int)row.start, (int)row.end, hadAny ? 1 : 0);
		if (!hadAny)
		{
			row.why = kClearNone;
			continue;
		}
		row.done = true;
		row.why = kClearOk;
		++chapter.cleared;
	}
	for (size_t k = 0; k < chapter.rows.size(); ++k)
		if (!chapter.rows[k].done)
			++counts[chapter.rows[k].why != kClearOk ? chapter.rows[k].why : kClearEdited];
	for (size_t k = 0; k < chapter.rows.size(); ++k)
		if (chapter.rows[k].done)
			KFCResultModel::MarkHitSpecial(chapter.chapterIdx, chapter.rows[k].hit, kKFCSpecialClearTextOverrides);
	return true;
}

}	// anonymous namespace

bool KFCReplaceEngine::CanClearOverridesHit(int32 chapterIdx, int32 hitIdx)
{
	if (KFCResultModel::ListIsRunList() || KFCResultModel::ListHasOwnChangeTab())
		return false;		// (RefuseRunListRow's and RefuseOwnTabRow's notes)
	return ClearRowRefusal(chapterIdx, hitIdx) == kClearOk;
}

bool KFCReplaceEngine::ClearOverridesHits(const KFCRowsByChapter& rowsByChapter, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("clear-overrides-rows");	// (test builds: after DocsAfterWrite, below - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (RefuseRunListRow("Clear Overrides", outStatus) || RefuseOwnTabRow("Clear Overrides", outStatus))
		return false;
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const KFCReplaceEngine::WriteUnderWay replacing;	// counted from the door to the hand-back (IsReplacing)
	const size_t total = KFCRowCount(rowsByChapter);
	if (total == 0)
		return false;

	// THE DOORS, ROW BY ROW - Create Outlines' (OutlineHits): nothing written yet. What each document is left as is
	// DocsAfterWrite's, on every way out.
	int32 counts[kClearRefusalCount] = {};
	ClearRefusal lastWhy = kClearOk;
	std::vector<ClearChapter> chapters;
	KFCReplaceEngine::DocsAfterWrite docsAfter;
	size_t toWrite = 0;
	for (size_t c = 0; c < rowsByChapter.size(); ++c)
	{
		ClearChapter chapter;
		chapter.chapterIdx = rowsByChapter[c].chapter;
		// The row's own flags first: a chapter with nothing to clear is not opened.
		std::vector<ClearRow> candidates;
		const std::vector<int32>& hits = rowsByChapter[c].hits;
		for (size_t k = 0; k < hits.size(); ++k)
		{
			ClearRow row;
			row.hit = hits[k];
			const ClearRefusal why = ClearRowRefusal(chapter.chapterIdx, row.hit);		// the menu's question
			if (why != kClearOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			candidates.push_back(row);
		}
		if (candidates.empty())
			continue;
		// Its document, by its file first: a closed chapter is reopened windowless and held, as a Replace reaches it.
		IDFile file;
		ClearRefusal docWhy = kClearOk;
		if (!KFCResultModel::GetChapterLocation(chapter.chapterIdx, chapter.docRef, file))
			docWhy = kClearNoDoc;
		else if (!KFCBookScope::ReachChapterDoc(file, chapter.docRef))
			docWhy = kClearNotOpened;
		if (docWhy != kClearOk)
		{
			counts[docWhy] += static_cast<int32>(candidates.size());
			lastWhy = docWhy;
			continue;
		}
		KFCResultModel::RebindChapterDoc(chapter.chapterIdx, chapter.docRef);
		IDataBase* const db = chapter.docRef.GetDataBase();
		// THE ROWS OF EACH STORY FOLLOW THEIR FOCI FIRST (a Replace's way), once per story - then each row's place.
		std::map<UID, KFCSearchEngine::FollowResult> followed;
		FollowStoriesOf(chapter.chapterIdx, db, candidates, followed);
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			ClearRow& row = candidates[k];
			const PlaceFollowed place = RowPlaceFollowed(chapter.chapterIdx, row.hit, db, followed, row.story,
				row.start, row.end);
			ClearRefusal why = kClearOk;
			if (place == kPlaceNotFollowed)
				why = kClearNotFollowed;
			else if (place == kPlaceEdited)
				why = kClearEdited;
			// ITS TEXT AS THE ROW HOLDS IT: what its Replace wrote for a replaced row (a replaced row can be cleared - the
			// spec's B4), the match as it was found for any other (RowReadsAsFound).
			if (why == kClearOk)
			{
				bool replaced = false, locked = false;
				PMString written;
				const UIDRef storyRef(db, row.story);
				if (KFCResultModel::GetHitFlags(chapter.chapterIdx, row.hit, replaced, locked) && replaced)
				{
					if (!KFCResultModel::GetHitWrittenText(chapter.chapterIdx, row.hit, written)
						|| KFCSearchEngine::ReadText(storyRef, row.start, row.end - row.start) != written)
						why = kClearEdited;
				}
				else if (!KFCSearchEngine::RowReadsAsFound(chapter.chapterIdx, row.hit, db))
					why = kClearEdited;
				if (why == kClearOk)
				{
					row.original = KFCSearchEngine::ReadText(storyRef, row.start, row.end - row.start);
					row.original.SetTranslatable(kFalse);
					if (!KFCSearchEngine::IsFrameEditable(storyRef, KFCSearchEngine::EditableFrameForMatch(storyRef, row.start)))
						why = kClearLocked;
				}
			}
			if (why != kClearOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			chapter.rows.push_back(row);
			chapter.stories.insert(row.story);
		}
		docsAfter.Reached(chapter.docRef, !chapter.rows.empty());
		toWrite += chapter.rows.size();
		if (!chapter.rows.empty())
			chapters.push_back(chapter);
	}
	if (toWrite == 0)
	{
		SayNoneCleared(outStatus, total, lastWhy, counts);
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last is reached (a Replace's rule).
	const bool acrossDocuments = chapters.size() > 1;
	for (size_t c = 0; acrossDocuments && c < chapters.size(); ++c)
	{
		if (!KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			outStatus = "Clear Overrides: a document of the selected rows has been closed - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
	}

	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (the spec's B5): one step for every document of it.
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
		chapterIdxs.push_back(chapters[c].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	recorder.SetName("Clear Overrides");
	bool ok = false;
	bool failed = false;
	int32 clearedRows = 0;
	{
		KFCReplaceEngine::WriteStep step;
		if (!KFCReplaceEngine::BeginWriteStep(acrossDocuments, step, kKFCClearOverridesStepKey, "KFC Clear Overrides"))
		{
			outStatus = acrossDocuments ? "Clear Overrides: InDesign would not start an undoable step - nothing was changed."
				: "Clear Overrides: InDesign would not start a command sequence - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
		for (size_t c = 0; c < chapters.size() && !failed; ++c)
		{
			if (!ClearInChapter(chapters[c], counts))
				failed = true;
			clearedRows += chapters[c].cleared;
		}
		// THE ERROR STATE AT THE END (a Replace's rule): anything standing here is a failure nothing reported.
		ok = !failed && clearedRows > 0 && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				if (chapters[c].cleared > 0)
					KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());	// in this step, so its Undo / Redo is heard
		// THE STORIES' FOCI ON THE ROWS, IN THIS STEP (the row-focus plan's D7 - as every write from the list).
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
					KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s, true);
		KFCReplaceEngine::EndWriteStep(step, ok);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		for (size_t c = 0; c < chapters.size(); ++c)
			for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
				KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s);
		if (failed)
		{
			outStatus = "Clear Overrides: InDesign would not clear a match's overrides - nothing was changed.";
			outStatus.SetTranslatable(kFalse);
			return false;
		}
		// No row was cleared - each one's reason from the step (several are counted): "had no overrides", edited since.
		ClearRefusal why = kClearNone;
		for (size_t c = 0; c < chapters.size(); ++c)
			for (size_t k = 0; k < chapters[c].rows.size(); ++k)
				if (chapters[c].rows[k].why != kClearOk)
					why = chapters[c].rows[k].why;
		SayNoneCleared(outStatus, total, why, counts);
		return false;
	}
	for (size_t c = 0; c < chapters.size(); ++c)
		NoteStoryVersions(chapters[c].chapterIdx, chapters[c].docRef.GetDataBase(), chapters[c].stories);	// as KFC left them
	recorder.Keep();
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (chapters[c].cleared == 0)
			continue;
		docsAfter.Wrote(chapters[c].docRef);
		KFC_DIAG_NOTE_WRITE(chapters[c].docRef);
	}

	// WHAT IT DID (the spec's B7 - SayRowsWritten): "Cleared overrides on ID:262 #3." /
	// "Cleared overrides on 2 of 3 matches: ID:262 #1, ID:262 #3. Left as they were: 1 had no overrides."
	PMString leftWords;
	AppendCountsByWhy(leftWords, counts, ClearWords);
	SayRowsWritten(outStatus, "Cleared overrides on", "matches", RowsDone(chapters), total, leftWords);
	return true;
}

// ======================================================================================================
// A ROW OF A TAB OF KFC'S WITH A CHANGE SIDE, REPLACED - the Table tab's (docs/superpowers/specs/2026-10-31-kfc-table-tab-design.md T8 - T10) and the Ruby tab's
// (2026-10-31-kfc-ruby-tab-design.md R7, R8). What is written is what the tab's Change side says NOW - a text row's Change To's rule: a Replace is always the settings as they stand.
// Nothing written moves text, so no row is carried. ONE undo step, KFC's mark in it (the panel follows its Ctrl+Z and Redo). Each written row marked
// (KFCResultModel::MarkHitOwnChange - its word, what it draws read again). The doors, the step and the words are one (ClearOverridesHits' shape); what differs is OwnTabWrite.
// ======================================================================================================
namespace
{
// WHY A ROW IS NOT WRITTEN - said in full for one row (OwnTabRefusalSentence), counted for several (OwnTabRefusalWords).
enum OwnTabRefusal
{
	kOwnTabOk = 0,
	kOwnTabMissing,		// no such row
	kOwnTabLocked,		// locked
	kOwnTabNoDoc,		// its document could not be found
	kOwnTabNotOpened,	// ...or not opened
	kOwnTabNotFollowed,	// its story changed in a way its foci could not follow
	kOwnTabEdited,		// its text has been edited since (a table row: or holds no table now)
	kOwnTabNoStyle,		// its document has no table style of the Change side's name
	kOwnTabAsAsked,		// it already reads as the Change side asks: counted, not written
	kOwnTabRefusalCount
};

const char* OwnTabRefusalWords(OwnTabRefusal why)
{
	switch (why)
	{
		case kOwnTabMissing:		return "not found";
		case kOwnTabLocked:			return "locked";
		case kOwnTabNoDoc:			return "document not found";
		case kOwnTabNotOpened:		return "document not opened";
		case kOwnTabNotFollowed:	return "not followed";
		case kOwnTabEdited:			return "edited since";
		case kOwnTabNoStyle:		return "no such table style";
		case kOwnTabAsAsked:		return "already as asked";
		default:					return "";
	}
}

// One row's refusal, after "Replace: ".
const char* OwnTabRefusalSentence(OwnTabRefusal why)
{
	switch (why)
	{
		case kOwnTabMissing:		return "this row is no longer in the list.";
		case kOwnTabLocked:			return "this row is locked.";
		case kOwnTabNoDoc:			return "the document of this row could not be found.";
		case kOwnTabNotOpened:		return "the document of this row could not be opened.";
		case kOwnTabNotFollowed:	return "this row's story has changed in a way its row could not follow.";
		case kOwnTabEdited:			return "this row has been edited since the search.";
		case kOwnTabNoStyle:		return "this document has no table style of that name.";
		case kOwnTabAsAsked:		return "this row already reads as its tab asks.";
		default:					return "this row cannot be replaced.";
	}
}

// One row to write, as the doors found it.
struct OwnTabRow
{
	int32			hit;
	UID				story;
	TextIndex		start;		// its text: [start, end)
	TextIndex		end;
	UID				item;		// what the write's Undo is told by, when no story's version says it (a table - StepRecorder::RecordItem); kInvalidUID = its story's
	bool			done;
	OwnTabRefusal	why;
	OwnTabRow() : hit(-1), story(kInvalidUID), start(kInvalidTextIndex), end(kInvalidTextIndex), item(kInvalidUID), done(false), why(kOwnTabOk) {}
};

// One document's rows in the write.
struct OwnTabChapter
{
	int32					chapterIdx;
	UIDRef					docRef;
	std::vector<OwnTabRow>	rows;		// the rows that passed the doors
	std::set<UID>			stories;	// their stories
	int32					written;
	OwnTabChapter() : chapterIdx(-1), written(0) {}
};

// WHAT DIFFERS BETWEEN THE TABS. The doors ask it for a row after every row's own door (place, text, lock) has passed.
class OwnTabWrite
{
public:
	virtual ~OwnTabWrite() {}
	/** Said when the Change side asks nothing - nullptr when it asks something. */
	virtual const char* NothingToChange() const = 0;
	/** A document the rows reach, before its rows' doors. */
	virtual void EnterDocument(IDataBase* db) = 0;
	/** kOwnTabOk = write it; sets row.item if its Undo is told by one. */
	virtual OwnTabRefusal RowDoor(const UIDRef& storyRef, OwnTabRow& row) = 0;
	/** Inside the step: writes the row, outWords what it draws after it (empty: its own text). */
	virtual ErrorCode WriteRow(const UIDRef& storyRef, const OwnTabRow& row, PMString& outWords) = 0;
	/** Inside the step: writes a document's rows - each by WriteRow, marked. A way whose write moves text writes them its own way (MarksWrite). False = InDesign would not write a row. */
	virtual bool WriteChapter(OwnTabChapter& chapter);
	/** What is said when it would not (a way that knows why says so). */
	virtual const char* WontWriteWords() const { return "Replace: InDesign would not write a row - nothing was changed."; }
	/** The special operation that marks a written row (its word - SpecialRowWord). */
	virtual int32 RowMark() const = 0;
	virtual const char* StepKey() const = 0;
	virtual const char* StepName() const = 0;		// a literal: the Undo's words on the message line (StepRecorder::SetName), the sequence's name
	virtual const char* Verb() const = 0;			// SayRowsWritten's
	virtual const char* Noun() const = 0;
};

// THE TABLE TAB'S: the style by its NAME in the row's own document (a book's chapters each have their own styles; one without it turns the row away, counted).
class TableWrite : public OwnTabWrite
{
public:
	TableWrite() : fStyle(kInvalidUID), fDB(nil) { (void)KFCTableSearch::IsTabCurrent(&fAsked); }
	virtual const char* NothingToChange() const
	{
		return (fAsked.changeStyle.IsEmpty() && !fAsked.clearOverrides)
			? "Replace: nothing to change on the Kohaku+ tab - choose Change to Table Style or Clear Overrides in Edit > Find/Change." : nil;
	}
	virtual void EnterDocument(IDataBase* db)
	{
		fDB = db;
		fStyle = fAsked.changeStyle.IsEmpty() ? kInvalidUID : KFCTableSearch::StyleNamed(db, fAsked.changeStyle);
	}
	virtual OwnTabRefusal RowDoor(const UIDRef& storyRef, OwnTabRow& row)
	{
		InterfacePtr<ITableModel> table(KFCTableSearch::QueryTableAt(storyRef, row.start));
		if (table == nil)
			return kOwnTabEdited;
		if (!fAsked.changeStyle.IsEmpty() && fStyle == kInvalidUID)
			return kOwnTabNoStyle;
		if (!KFCTableSearch::WouldChange(table, fStyle, fAsked.clearOverrides))
			return kOwnTabAsAsked;
		row.item = ::GetUID(table);		// (a table's write need not move its story's version)
		return kOwnTabOk;
	}
	virtual ErrorCode WriteRow(const UIDRef& /*storyRef*/, const OwnTabRow& row, PMString& outWords)
	{
		InterfacePtr<ITableModel> table(fDB, row.item, UseDefaultIID());
		if (table == nil)
			return kFailure;
		const ErrorCode wrote = KFCTableSearch::Write(table, fStyle, fAsked.clearOverrides);
		if (wrote == kSuccess)
			outWords = KFCTableSearch::TableWords(table);
		return wrote;
	}
	virtual int32 RowMark() const { return kKFCSpecialTableStyle; }
	virtual const char* StepKey() const { return kKFCTableStyleStepKey; }
	virtual const char* StepName() const { return "Apply Table Style"; }
	virtual const char* Verb() const { return "Styled"; }
	virtual const char* Noun() const { return "tables"; }
private:
	KFCTableConditions	fAsked;
	UID					fStyle;		// the Change side's style in the document entered; kInvalidUID = it keeps its own
	IDataBase*			fDB;
};

// THE RUBY TAB'S: the reading as the Change side has it, or the ruby off (Remove Ruby over Change Ruby To - R7).
class RubyWrite : public OwnTabWrite
{
public:
	RubyWrite() { (void)KFCRubySearch::IsTabCurrent(&fAsked); }
	virtual const char* NothingToChange() const
	{
		return (fAsked.changeTo.IsEmpty() && !fAsked.removeRuby)
			? "Replace: nothing to change on the Kohaku+ tab - type Change Ruby To or choose Remove Ruby in Edit > Find/Change." : nil;
	}
	virtual void EnterDocument(IDataBase* /*db*/) {}
	virtual OwnTabRefusal RowDoor(const UIDRef& storyRef, OwnTabRow& row)
	{
		const WideString now(KFCRubySearch::ReadingOn(storyRef, row.start, row.end));
		if (fAsked.removeRuby)
			return now.empty() ? kOwnTabAsAsked : kOwnTabOk;
		return (now == WideString(fAsked.changeTo)) ? kOwnTabAsAsked : kOwnTabOk;
	}
	virtual ErrorCode WriteRow(const UIDRef& storyRef, const OwnTabRow& row, PMString& outWords)
	{
		const WideString reading(fAsked.removeRuby ? WideString() : WideString(fAsked.changeTo));
		const ErrorCode wrote = fAsked.removeRuby ? KFCRubySearch::Remove(storyRef, row.start, row.end)
			: KFCRubySearch::Apply(storyRef, row.start, row.end, reading);
		if (wrote == kSuccess)
			outWords = KFCRubySearch::RowWords(storyRef, row.start, row.end, reading);
		return wrote;
	}
	virtual int32 RowMark() const { return kKFCSpecialRubyChange; }
	virtual const char* StepKey() const { return kKFCRubyChangeStepKey; }
	virtual const char* StepName() const { return "Change Ruby"; }
	virtual const char* Verb() const { return "Changed ruby on"; }
	virtual const char* Noun() const { return "rows"; }
private:
	KFCRubyConditions	fAsked;
};

// THE KENTEN KIND'S: the kind of mark on a row's text, the look of the marks left as it stands - or, Remove Kenten, the marks taken off whole (KFCKentenSearch::Remove).
class KentenWrite : public OwnTabWrite
{
public:
	KentenWrite() { (void)KFCKentenSearch::IsTabCurrent(&fAsked); }
	virtual const char* NothingToChange() const
	{
		return (fAsked.changeTo == 0 && !fAsked.removeKenten)
			? "Replace: nothing to change on the Kohaku+ tab - choose Change Kenten To or Remove Kenten in Edit > Find/Change." : nil;
	}
	virtual void EnterDocument(IDataBase* /*db*/) {}
	virtual OwnTabRefusal RowDoor(const UIDRef& storyRef, OwnTabRow& row)
	{
		return (KFCKentenSearch::MarkOn(storyRef, row.start, row.end) == Wanted()) ? kOwnTabAsAsked : kOwnTabOk;
	}
	virtual ErrorCode WriteRow(const UIDRef& storyRef, const OwnTabRow& row, PMString& outWords)
	{
		const ErrorCode wrote = fAsked.removeKenten ? KFCKentenSearch::Remove(storyRef, row.start, row.end) : KFCKentenSearch::Write(storyRef, row.start, row.end, Wanted());
		if (wrote == kSuccess)
			outWords = KFCKentenSearch::RowWords(storyRef, row.start, row.end, Wanted());
		return wrote;
	}
	virtual int32 RowMark() const { return kKFCSpecialKentenChange; }
	virtual const char* StepKey() const { return kKFCKentenChangeStepKey; }
	virtual const char* StepName() const { return "Change Kenten"; }
	virtual const char* Verb() const { return "Changed kenten on"; }
	virtual const char* Noun() const { return "rows"; }
private:
	int32 Wanted() const { return fAsked.removeKenten ? static_cast<int32>(IKentenStyle::Kenten_None) : fAsked.changeTo; }
	KFCKentenConditions	fAsked;
};

// THE ROWS OF ONE DOCUMENT, inside the write's step. Each row's words read again after its command. False = InDesign would not write a row: the caller takes the step back.
bool OwnTabWrite::WriteChapter(OwnTabChapter& chapter)
{
	IDataBase* const db = chapter.docRef.GetDataBase();
	if (db == nil)
		return false;
	EnterDocument(db);
	for (size_t k = 0; k < chapter.rows.size(); ++k)
	{
		OwnTabRow& row = chapter.rows[k];
		const UIDRef storyRef(db, row.story);
		PMString words;
		words.SetTranslatable(kFalse);
		if (WriteRow(storyRef, row, words) != kSuccess)
			return false;
		KFC_DIAG_LOG("OWNTABWRITE %s chapter=%d row=%d story=%u at=%d", StepName(), chapter.chapterIdx, row.hit, row.story.Get(), (int)row.start);
		row.done = true;
		++chapter.written;
		KFCResultModel::MarkHitOwnChange(chapter.chapterIdx, row.hit, RowMark(), words);
	}
	return true;
}

// THE RUBY TAB'S MARKS (the spec's R12): the reading in the mark itself, whatever the Change side says; the marks taken out of the text, so the document's other rows are carried and read
// back (OutlineInChapter's way) - a written row stands on its base then, and reads as a row with ruby does.
class MarksWrite : public OwnTabWrite
{
public:
	MarksWrite() : fStillThere(false) {}
	virtual const char* NothingToChange() const { return nil; }
	virtual void EnterDocument(IDataBase* /*db*/) {}
	virtual OwnTabRefusal RowDoor(const UIDRef& storyRef, OwnTabRow& row)
	{
		int32 baseOffset = 0, baseLength = 0;
		WideString reading;
		return KFCRubySearch::ReadMark(storyRef, row.start, row.end, baseOffset, baseLength, reading) ? kOwnTabOk : kOwnTabAsAsked;
	}
	virtual ErrorCode WriteRow(const UIDRef& /*storyRef*/, const OwnTabRow& /*row*/, PMString& /*outWords*/) { return kFailure; }	// (WriteChapter's own)
	virtual bool WriteChapter(OwnTabChapter& chapter)
	{
		IDataBase* const db = chapter.docRef.GetDataBase();
		const int32 chapterIdx = chapter.chapterIdx;
		const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
		if (db == nil || hitCount <= 0)
			return false;
		std::vector<RowNow> rowNow(static_cast<size_t>(hitCount));
		std::map<int32, size_t> writing;		// hit -> its place in chapter.rows
		for (size_t k = 0; k < chapter.rows.size(); ++k)
			writing[chapter.rows[k].hit] = k;
		std::vector<int32> keptRows;			// every other row that stands - read back at the end
		for (int32 i = 0; i < hitCount; ++i)
		{
			const std::map<int32, size_t>::const_iterator r = writing.find(i);
			if (r != writing.end())
			{
				const OwnTabRow& row = chapter.rows[r->second];
				SetRowAt(db, rowNow[static_cast<size_t>(i)], row.story, row.start, row.end);	// the doors have just read it there
				continue;
			}
			KeepRowIfItStands(chapterIdx, i, db, rowNow, keptRows);
		}
		// IN TEXT ORDER (their story, then where they start now).
		std::vector<std::pair<std::pair<UID, TextIndex>, size_t> > order;
		for (size_t k = 0; k < chapter.rows.size(); ++k)
		{
			TextIndex now = kInvalidTextIndex;
			if (RowStartNow(db, rowNow[static_cast<size_t>(chapter.rows[k].hit)], now))
				order.push_back(std::make_pair(std::make_pair(chapter.rows[k].story, now), k));
		}
		std::sort(order.begin(), order.end());
		std::vector<int32> baseLengths(chapter.rows.size(), 0);
		for (size_t o = 0; o < order.size(); ++o)
		{
			OwnTabRow& row = chapter.rows[order[o].second];
			TextIndex at = kInvalidTextIndex;
			if (!RowStartNow(db, rowNow[static_cast<size_t>(row.hit)], at))
				continue;
			const RowThread thread = rowNow[static_cast<size_t>(row.hit)].thread;
			const TextIndex offset = rowNow[static_cast<size_t>(row.hit)].offset;
			const int32 length = rowNow[static_cast<size_t>(row.hit)].length;
			int32 baseLength = 0;
			if (KFCRubySearch::WriteMark(UIDRef(db, row.story), at, length, baseLength, fStillThere) != kSuccess)
				return false;
			CarryRowsPast(rowNow, thread, offset, offset + length, baseLength);
			SetRowAt(db, rowNow[static_cast<size_t>(row.hit)], row.story, at, at + baseLength);
			KFC_DIAG_LOG("OWNTABWRITE mark chapter=%d row=%d story=%u at=%d length=%d base=%d", chapterIdx, row.hit, row.story.Get(), (int)at, (int)length, (int)baseLength);
			row.done = true;
			++chapter.written;
		}
		// EVERY ROW WHERE ITS TEXT STANDS NOW (a Replace's read-back): the rows carried, the rows left, and the written ones - on their base now.
		ReadBackRows(chapterIdx, db, rowNow, keptRows);
		for (size_t k = 0; k < chapter.rows.size(); ++k)
		{
			const OwnTabRow& row = chapter.rows[k];
			const RowNow& place = rowNow[static_cast<size_t>(row.hit)];
			TextIndex s = kInvalidTextIndex;
			if (!RowStartNow(db, place, s))
				continue;
			const UIDRef storyRef(db, row.story);
			KFCResultModel::SetHitRange(chapterIdx, row.hit, row.story, s, s + place.length);
			KFCSearchEngine::RereadRowText(chapterIdx, row.hit, storyRef, s, s + place.length);
			if (row.done)
				KFCResultModel::MarkHitOwnChange(chapterIdx, row.hit, RowMark(),
					KFCRubySearch::RowWords(storyRef, s, s + place.length, KFCRubySearch::ReadingOn(storyRef, s, s + place.length)));
		}
		for (std::set<UID>::const_iterator st = chapter.stories.begin(); st != chapter.stories.end(); ++st)
			KFCSearchEngine::RereadStoryLead(chapterIdx, UIDRef(db, *st));
		return true;
	}
	virtual const char* WontWriteWords() const
	{
		return fStillThere ? "Replace: the marks stay in the text under Track Changes - turn it off for this story to take them out. Nothing was changed."
			: "Replace: InDesign would not write a row - nothing was changed.";
	}
	virtual int32 RowMark() const { return kKFCSpecialRubyChange; }
	virtual const char* StepKey() const { return kKFCRubyChangeStepKey; }
	virtual const char* StepName() const { return "Change Ruby"; }
	virtual const char* Verb() const { return "Changed ruby on"; }
	virtual const char* Noun() const { return "rows"; }
private:
	bool	fStillThere;
};

bool WriteOwnTabRows(OwnTabWrite& how, const KFCRowsByChapter& rowsByChapter, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("own-tab-rows");	// (test builds: after DocsAfterWrite, below - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	// (The run guard is the callers' - ReplaceRows in the UI half: ReplaceHit and ReplaceHits count this write as a run themselves.)
	const size_t total = KFCRowCount(rowsByChapter);
	if (total == 0)
		return false;
	// THE CHANGE SIDE AS IT STANDS: nothing on it is said when pressed - the face changes while the rows stay.
	const char* const nothing = how.NothingToChange();
	if (nothing != nil)
	{
		outStatus.Append(nothing);
		return false;
	}

	// THE DOORS, ROW BY ROW (ClearOverridesHits' shape): nothing written yet.
	int32 counts[kOwnTabRefusalCount] = {};
	OwnTabRefusal lastWhy = kOwnTabOk;
	std::vector<OwnTabChapter> chapters;
	KFCReplaceEngine::DocsAfterWrite docsAfter;
	size_t toWrite = 0;
	for (size_t c = 0; c < rowsByChapter.size(); ++c)
	{
		OwnTabChapter chapter;
		chapter.chapterIdx = rowsByChapter[c].chapter;
		// The row's own flags first: a chapter with nothing to write is not opened.
		std::vector<OwnTabRow> candidates;
		const std::vector<int32>& hits = rowsByChapter[c].hits;
		for (size_t k = 0; k < hits.size(); ++k)
		{
			OwnTabRow row;
			row.hit = hits[k];
			const KFCReplaceEngine::GreyedReason greyed = KFCReplaceEngine::WhyGreyed(chapter.chapterIdx, row.hit);
			const OwnTabRefusal why = (greyed == KFCReplaceEngine::kGreyedNot) ? kOwnTabOk
				: (greyed == KFCReplaceEngine::kGreyedLocked) ? kOwnTabLocked : kOwnTabMissing;
			if (why != kOwnTabOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			candidates.push_back(row);
		}
		if (candidates.empty())
			continue;
		// Its document, by its file first: a closed chapter is reopened windowless and held, as a Replace reaches it.
		IDFile file;
		OwnTabRefusal docWhy = kOwnTabOk;
		if (!KFCResultModel::GetChapterLocation(chapter.chapterIdx, chapter.docRef, file))
			docWhy = kOwnTabNoDoc;
		else if (!KFCBookScope::ReachChapterDoc(file, chapter.docRef))
			docWhy = kOwnTabNotOpened;
		if (docWhy != kOwnTabOk)
		{
			counts[docWhy] += static_cast<int32>(candidates.size());
			lastWhy = docWhy;
			continue;
		}
		KFCResultModel::RebindChapterDoc(chapter.chapterIdx, chapter.docRef);
		IDataBase* const db = chapter.docRef.GetDataBase();
		how.EnterDocument(db);
		// THE ROWS OF EACH STORY FOLLOW THEIR FOCI FIRST (a Replace's way), once per story - then each row's place.
		std::map<UID, KFCSearchEngine::FollowResult> followed;
		FollowStoriesOf(chapter.chapterIdx, db, candidates, followed);
		for (size_t k = 0; k < candidates.size(); ++k)
		{
			OwnTabRow& row = candidates[k];
			const PlaceFollowed place = RowPlaceFollowed(chapter.chapterIdx, row.hit, db, followed, row.story, row.start, row.end);
			OwnTabRefusal why = (place == kPlaceNotFollowed) ? kOwnTabNotFollowed : (place == kPlaceEdited) ? kOwnTabEdited : kOwnTabOk;
			if (why == kOwnTabOk && !KFCSearchEngine::RowReadsAsFound(chapter.chapterIdx, row.hit, db))
				why = kOwnTabEdited;
			if (why == kOwnTabOk)
			{
				const UIDRef storyRef(db, row.story);
				if (!KFCSearchEngine::IsFrameEditable(storyRef, KFCSearchEngine::EditableFrameForMatch(storyRef, row.start)))
					why = kOwnTabLocked;
				else
					why = how.RowDoor(storyRef, row);
			}
			if (why != kOwnTabOk)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			chapter.rows.push_back(row);
			chapter.stories.insert(row.story);
		}
		docsAfter.Reached(chapter.docRef, !chapter.rows.empty());
		toWrite += chapter.rows.size();
		if (!chapter.rows.empty())
			chapters.push_back(chapter);
	}
	if (toWrite == 0)
	{
		outStatus.Append("Replace: ");
		if (total == 1)
			outStatus.Append(OwnTabRefusalSentence(lastWhy));
		else
		{
			outStatus.Append("none of the ");
			outStatus.AppendNumber(static_cast<int32>(total));
			outStatus.Append(" selected rows can be written - ");
			AppendCountsByWhy(outStatus, counts, OwnTabRefusalWords);
			outStatus.Append(".");
		}
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last is reached (a Replace's rule).
	const bool acrossDocuments = chapters.size() > 1;
	for (size_t c = 0; acrossDocuments && c < chapters.size(); ++c)
	{
		if (!KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			outStatus.Append("Replace: a document of the selected rows has been closed - nothing was changed.");
			return false;
		}
	}

	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO: one step for every document of it - the stories' versions, and each row's item if it has one.
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
		chapterIdxs.push_back(chapters[c].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	recorder.SetName(how.StepName());
	for (size_t c = 0; c < chapters.size(); ++c)
		for (size_t k = 0; k < chapters[c].rows.size(); ++k)
			if (chapters[c].rows[k].item != kInvalidUID)
				recorder.RecordItem(chapters[c].chapterIdx, chapters[c].rows[k].item);
	bool ok = false;
	bool failed = false;
	int32 writtenRows = 0;
	{
		KFCReplaceEngine::WriteStep step;
		if (!KFCReplaceEngine::BeginWriteStep(acrossDocuments, step, how.StepKey(), how.StepName()))
		{
			outStatus.Append(acrossDocuments ? "Replace: InDesign would not start an undoable step - nothing was changed."
				: "Replace: InDesign would not start a command sequence - nothing was changed.");
			return false;
		}
		for (size_t c = 0; c < chapters.size() && !failed; ++c)
		{
			if (!how.WriteChapter(chapters[c]))
				failed = true;
			writtenRows += chapters[c].written;
		}
		// THE ERROR STATE AT THE END (a Replace's rule): anything standing here is a failure nothing reported.
		ok = !failed && writtenRows > 0 && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				if (chapters[c].written > 0)
					KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());	// in this step, so its Undo / Redo is heard
		// THE STORIES' FOCI ON THE ROWS, IN THIS STEP (the row-focus plan's D7 - as every write from the list).
		if (ok)
			for (size_t c = 0; c < chapters.size(); ++c)
				for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
					KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s, true);
		KFCReplaceEngine::EndWriteStep(step, ok);
	}
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		for (size_t c = 0; c < chapters.size(); ++c)
			for (std::set<UID>::const_iterator s = chapters[c].stories.begin(); s != chapters[c].stories.end(); ++s)
				KFCRowFoci::ReanchorStory(chapters[c].chapterIdx, *s);
		outStatus.Append(failed ? how.WontWriteWords() : "Replace: no row was written.");
		return false;
	}
	for (size_t c = 0; c < chapters.size(); ++c)
		NoteStoryVersions(chapters[c].chapterIdx, chapters[c].docRef.GetDataBase(), chapters[c].stories);	// as KFC left them
	recorder.Keep();
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (chapters[c].written == 0)
			continue;
		docsAfter.Wrote(chapters[c].docRef);
		KFC_DIAG_NOTE_WRITE(chapters[c].docRef);
	}

	// WHAT IT DID (SayRowsWritten): "Styled ID:262 #3." / "Styled 2 of 3 tables: ID:262 #1, ID:262 #3. Left as they were: 1 locked."
	PMString leftWords;
	AppendCountsByWhy(leftWords, counts, OwnTabRefusalWords);
	SayRowsWritten(outStatus, how.Verb(), how.Noun(), RowsDone(chapters), total, leftWords);
	return true;
}
}	// anonymous namespace

static bool ReplaceOwnTabRows(const KFCRowsByChapter& rowsByChapter, PMString& outStatus)
{
	if (KFCResultModel::ListIsKenten())
	{
		KentenWrite kenten;
		return WriteOwnTabRows(kenten, rowsByChapter, outStatus);
	}
	if (KFCResultModel::ListIsRuby())
	{
		const std::shared_ptr<const KFCQueryCopy> asked(KFCResultModel::GetQueryCopy());
		if (asked != nil && asked->rubyKind == kKFCRubyFindMarks)
		{
			MarksWrite marks;
			return WriteOwnTabRows(marks, rowsByChapter, outStatus);
		}
		RubyWrite ruby;
		return WriteOwnTabRows(ruby, rowsByChapter, outStatus);
	}
	TableWrite table;
	return WriteOwnTabRows(table, rowsByChapter, outStatus);
}

// ======================================================================================================
// A GREP ROW'S AFTER-TEXT - WRITTEN, READ, AND THROWN AWAY (the author's call: an ordinary search's row shows what
// it would become, as an ordinary search's after-text - GREP only, the row selected, that row alone). The row's own
// Replace (ReplaceInChapterOneByOne, as ReplaceRowsNow writes it), inside an ABORTABLE sequence that is then aborted -
// measured in KT first (docs/ai-notes/kfc-preview-spike-2026-10-07.md):
//  - no undo step, the text back, the caret where it was, no Track Changes record left;
//  - the story's version (ITextModel::GetChangeCount) back where the search recorded it - so the Return that follows
//    is not refused as "the story has changed";
//  - the modified flag is NOT put back by the abort - IDataBase::SaveRestoreModifiedState does that;
//  - the rows the walk touched (written text, place, outcome) are put back by the StepRecorder, never kept.
// Shown only where Return would write it: a GREP search, the query unchanged since it (ASKED, not acted on -
// RefuseChangedQuery would clear the results), a work row not replaced, its document open (a preview never opens one),
// its story as KFC left it, nothing of KFC's running.
// ======================================================================================================

bool KFCReplaceEngine::PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter)
{
	outAfter.Clear();
	outAfter.SetTranslatable(kFalse);
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID)
		return false;		// an object row has no after-text (a GREP row's alone)
	if (KFCRunGuard::IsAnyRunning() || !CanReplaceHit(chapterIdx, hitIdx))
		return false;
	// (InDesign's own mode - the list's query stated - not a QR Code tab the dialog may be showing: FindChangeMode.)
	if (KFCSearchEngine::FindChangeMode() != IFindChangeOptions::kGrepSearch || !QueryUnchangedSinceSearch())
		return false;
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KFCBookScope::IsDocStillOpen(docRef))
		return false;
	IDataBase* const db = docRef.GetDataBase();
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (db == nil || !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash))
		return false;
	// The rows follow their foci first, as the Replace's do (KFCSearchEngine::FollowStoryFoci): a story they cannot follow
	// has no preview, and a row the follow found edited (Missing) has none either.
	if (KFCSearchEngine::FollowStoryFoci(chapterIdx, db, story) == KFCSearchEngine::kFollowBroken
		|| !CanReplaceHit(chapterIdx, hitIdx)
		|| !StoryAsKFCLeftIt(chapterIdx, db, story))
		return false;

	// TEST BUILDS ONLY: what one preview costs - it runs on every row landed on, the arrow keys' walk included
	// ("PHASE preview end <ms> <InDesign's counters>", KFCDiag.h).
	KFC_DIAG_PHASE(previewPhase, "preview");

	// The Replace's own directions: forward, as the search was, and a GREP query holding ^ written backwards.
	KFCForwardSearchScope forward;
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
	std::set<int32> one;
	one.insert(hitIdx);
	bool shown = false;
	{
		IDataBase::SaveRestoreModifiedState keepClean(db);		// goes after the abort below: clean again if it was
		KFCUndoFollow::StepRecorder recorder(chapterIdx);	// never kept: the rows roll back
		{
			const KFCBackwardSearchScope writeDirection(WriteBackward());
			IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KFC Preview");
			if (seq == nil)
				return false;
			ChapterOutcome outcome;
			const bool wrote = ReplaceInChapterOneByOne(chapterIdx, docRef, scopeOptions, one, outcome);
			// What the walk wrote at the row's place - read before the abort takes it away and the recorder puts the
			// row back.
			if (wrote && !outcome.failed && outcome.replaced == 1)
				shown = KFCResultModel::GetHitWrittenText(chapterIdx, hitIdx, outAfter);
			CmdUtils::AbortCommandSequence(seq);
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		}
	}
	// The preview wrote and took it back: the foci of the story moved with the write and not back with the abort (the
	// text-focus-jump spec's T13). The story was as KFC knew it (StoryAsKFCLeftIt above), so the rows' stored places
	// are exact.
	KFCRowFoci::ReanchorStory(chapterIdx, story);
	if (!shown)
		outAfter.Clear();
	outAfter.SetTranslatable(kFalse);
	KFC_DIAG_LOG("PREVIEW chapter=%d hit=%d shown=%d", (int)chapterIdx, (int)hitIdx, shown ? 1 : 0);
	return shown;
}

// End, KFCReplaceEngine.cpp.
