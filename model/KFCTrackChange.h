//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Track Changes parts for the replace (the author's design). Every replace KFC makes is written with
//  Track Changes on, and the records are LEFT in the document - they are what lets one replaced row be
//  taken back later (Reject Change) and what the jump finds a replaced row by. The story's own "Track
//  Changes" setting is handed back as it was found (TrackingScope).
//
//  SIGNED "KohakuFindChange" AT A TIME KFC HANDS OUT (the author's call; spec
//  docs/superpowers/specs/_done/2026-09-28-kbs-signed-designated-time-design.md). InDesign's user name is
//  not touched (a name the script DOM cannot put back to "unset" is a name that could be left behind -
//  switching it was tried and dropped): each replace is written under it and its records are rewritten
//  right after, before the next row is written (KFCSignRecordsCmd). The time is the run's start, cut to
//  the millisecond, plus the row's number in the result in 100 ns units (the four digits below the
//  millisecond - no panel and no script shows them). A replaced row keeps its time (Hit::recordTime) and
//  is tied to its records by that time exactly - not by its texts and the nearest position, nor under
//  the user's own name and the clock's time, both tried before. "Accept All Changes by KohakuFindChange
//  in This Document" accepts the signed records only (the author's call - not every change, as InDesign's
//  own Accept All).
//  ! Touching replaces written front to back leave one insertion per row but ONE deletion, carrying the
//    LAST row's time: InDesign joins a deletion to the one it touches whoever made either (measured with
//    two real user names). A touching group is taken back as one (KFCReplaceEngine RejectRowsNow).
//
//  WHERE THE RECORDS ARE (measured). A replacement leaves an insertion record over the new text and a
//  deletion record anchored right after it (redlineiterator.h:42-50); replacements that touch merge
//  their records. ! EXCEPT a GREP Change To holding $n (measured): the one-at-a-time replace KEEPS the
//  matched characters $n names and records only around them - c(at) -> k$1 on "cat": insertion "k",
//  deletion "c" in front of the kept "at"; cat -> <$0>: two insertions, "<" and ">", and no deletion;
//  c(at) -> $1: the deletion of "c" alone. (Change All records the whole match.) A row's change is found
//  with that in mind - Hit::recordLead, OriginalFromRecords. Text in table cells is recorded too (the
//  user checked it in the UI) - the DOM's Story.changes lists only the story's main text, which is why a
//  probe once said otherwise. Text in a FOOTNOTE is not: measured through IDML (IsInFootnote), and
//  case show-footnote finds no record of a footnote's replace. NewRedlineIterator walks the whole
//  story's TextIndex space - cells, footnotes and hidden conditional text included.
//
//========================================================================================

#ifndef __KFCTrackChange_h__
#define __KFCTrackChange_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WideString.h"		// RunOwnChars - a match's characters, one per position

#include <set>
#include <vector>

class IDataBase;

namespace KFCTrackChange
{
	/** The author of every record KFC writes. */
	extern const char* const kSignAuthor;		// "KohakuFindChange"
	// (No text of its own for a signing failure: every reason a run stops on is said the same way,
	//  KFCReplaceEngine's stoppedByFailure.)

	/** A replace run begins (Change Checked, or a row's / story's / document's Replace): its time T0 is
	    the clock cut to the millisecond - or the millisecond after the last time handed out, if the clock
	    is not already past it. */
	void BeginSignedRun();
	/** The row's time in the run: T0 + the row's number in the result - the rows of every chapter before
	    it, then its index - in 100 ns units. Remembered as the last handed out. */
	uint64 StampForRow(int32 chapterIdx, int32 hitIdx);
	/** Sign the records one replace just made - the insertion's pieces in [from, to) and the deletion
	    anchored in [from, to], those not yet "KohakuFindChange" - through kKFCSignRecordsCmdBoss.
	    False = they could not be signed (the caller stops the run). Nothing to sign (a footnote) is not
	    a failure. */
	bool SignReplace(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp);
	/** The command's work (KFCSignRecordsCmd::Do) - not to be called from anywhere else. */
	bool SignRecordsNow(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp);
	/** Where the first record carrying exactly this time stands in [from, to] (where a replace just wrote): the
	    first insertion piece, or the first deletion when there is no insertion. False = no such record (a
	    footnote, or a replace that changed no character). A row's Hit::recordLead is that place minus
	    `from`. */
	bool FirstRecordOfTimeIn(const UIDRef& story, TextIndex from, TextIndex to, uint64 time, TextIndex& outAt);
	/** "KohakuFindChange" IN AMBER (the author's call). A tracked change is drawn in
	    its AUTHOR's colour, from the document's list of users (IInCopyDocUserList on kDocBoss: a name -> an
	    index into the session's UI colours, IInCopyUIColors), and the name KFC signs with stands in no
	    document's list - its changes are drawn on a white background without this (measured, KT app.ktProbe
	    "usercolors"). This gives that name the UI colour Amber in the document db, through
	    kSetUserColorsCmdBoss with the document as the item list: it adds the name, or changes the colour
	    of a name already there, and leaves the current user and the application's user name and colour
	    alone (measured, the same probe - kCreateUserCmdBoss adds only, kSetUserColorCmdBoss takes no name).
	    Nothing is done when it is Amber already. Runs inside the caller's sequence, so the Undo takes it
	    back with the replace. false = it did not go in (the replace goes on without it). */
	bool ColourSignAuthor(IDataBase* db);

	/** Every story in `stories` recording changes for the life of the object; the ones this switched
	    on are switched back off. A story already recording is left alone both ways. */
	class TrackingScope
	{
	public:
		TrackingScope(IDataBase* db, const std::set<UID>& stories);
		~TrackingScope();
		/** False = a story could not be switched on; nothing written there would be recorded. */
		bool Ok() const { return fOk; }
	private:
		TrackingScope(const TrackingScope&);
		TrackingScope& operator=(const TrackingScope&);
		IDataBase*			fDB;
		std::vector<UID>	fSwitchedOn;
		bool				fOk;
	};

	/** One record, as the iterator reports it - whoever made it (see the head of this file). */
	struct Record
	{
		TextIndex	at;
		int32		len;		// an insertion's length; for a deletion, what the iterator says (1)
		bool		isDelete;
		uint64		time;		// VOSRedlineChange::GetTimeStamp - which run made it
		PMString	text;		// a deletion's deleted text - filled by CollectRecordsOfTimes only
	};

	/** Is `at` inside a footnote? Track Changes records nothing there (measured through IDML), so a
	    footnote's row, once replaced, cannot be taken back (it can be left unticked like any other).
	    A footnote's thread IS its reference boss (KCMTextRead's test). */
	bool IsInFootnote(const UIDRef& story, TextIndex at);




	/** Is `at` inside text a HIDDEN condition holds (case reject-hidden-condition)? While its
	    condition is hidden, conditional text - and the tracked changes in it - stand in a thread of their own
	    past the main text, and showing the condition puts them back (measured: a row's insertion read at 14
	    while hidden, at 0 once shown; its deletion stayed in the main text). That thread's owned item is
	    kHiddenTextBoss (customconditionaltext/CusCondTxtSuiteTextCSB.cpp:205 says so, and
	    conditionaltextui/ConditionalTextTips.cpp:232 asks it this way) - IsInFootnote's test, with that class. */
	bool IsInHiddenText(const UIDRef& story, TextIndex at);

	/** The story's text at [at, at+len), whole (not capped). Empty when it cannot be read. */
	PMString ReadText(const UIDRef& story, TextIndex at, int32 len);

	/** True when the story holds at least one record OUTSIDE HIDDEN CONDITIONAL TEXT - the strand's own
	    flag, which leaves those out (IRedlineDataStrand.h:107-112; not "at least one record").
	    A question for work on VISIBLE text only - the pending changes around a match being replaced
	    (KFCReplaceEngine). Nothing that looks for a row's records asks it: they are walked wherever they
	    stand (CollectRecordsOfTimes, CollectSignedRows, CountSignedRecords). */
	bool StoryHasChanges(const UIDRef& story);

	/** True when any story of the document holds a record signed "KohakuFindChange" - the ones Accept
	    All Changes by KohakuFindChange accepts. (Not any record.) */
	bool DocumentHasSignedRecords(IDataBase* db);


	/** ACCEPT THE PENDING CHANGES A MATCH ABOUT TO BE REPLACED SITS IN OR NEXT TO - ANYBODY'S (the
	    author's call: "only that part" - not a refusal of the run). Every insertion overlapping or touching
	    [from, to) is accepted, and every deletion anchored in [from, to] (an insertion's own deletion
	    stands at its end), one whole record at a time, whoever made it; nothing else in the story is
	    touched. Needed for the user's own insertion at least: replacing text its author inserted and has
	    not accepted leaves no record - InDesign rewrites the insertion it has (VOSRedline.h
	    CanApplyDeleteChange; measured with case rereplace-ours) - so the replace could never be taken
	    back. The main text does not move (an accepted insertion stays, an accepted deletion was never in
	    it), but a deleted-text thread goes, so a caller holding story indexes past it should hold them as
	    thread offsets. Only the records from one before `from` to `to` are walked. Runs
	    inside the caller's command sequence. Returns how many were accepted, or -1 when one would not be
	    (outWhy says so). Leaves the error state clear. */
	int32 AcceptPendingAround(const UIDRef& story, TextIndex from, TextIndex to, PMString& outWhy);

	/** A QUERY RUN KEEPS ITS OWN CHANGES PENDING (2026-10-04 - docs/superpowers/specs/2026-10-04-kfc-query-sequence-design.md
	    section 3). While a floor is set, AcceptPendingAround passes over every record signed "KohakuFindChange" whose time is at
	    or after it: a later query that writes over an earlier query's replacement leaves that one pending, so the records
	    read "the text before the run -> the text after it". Everybody else's pending changes are accepted as before.
	    OwnRunFloorNow = the clock cut to the millisecond, in the stamps' units - at or before every T0 BeginSignedRun hands
	    out after it. Clear it as the run ends, whichever way. */
	uint64 OwnRunFloorNow();
	void SetOwnRunFloor(uint64 floor);
	void ClearOwnRunFloor();

	/** A QUERY RUN'S CHAINED WRITE (2026-10-05 - measured on the regression case qs-chain): a later query that
	    replaces text an earlier query of the same run wrote leaves a DELETION holding that text - InDesign records
	    the deletion of another author's insertion, and the earlier one is signed "KohakuFindChange" by then
	    (deleting one's own pending insertion records nothing - measured the same day). So that deletion's text is
	    not what stood there before the run.
	    ! AND THE DELETION NEED NOT BE THE LATER ROW'S: GREP (c)at -> $1og, then (c)og -> $1ow, left ONE deletion
	    "ogat" carrying the FIRST row's time (measured 2026-10-05, case qs-chain-grep) - InDesign put the "og" it
	    deleted into the deletion standing next to it. So the run's characters are followed into whichever of the
	    run's deletions takes them: a MASK per deletion (by its time), one flag per character, set = the run wrote it.
	    RunOwnChars, asked before a row of a query run is written: false outside a query run (no floor); true =
	    outMatch holds [from, to)'s characters, one per position, and outOwn which of them the run itself wrote.
	    SnapshotRunDeletions, also before: the run's deletions anchored in [from - 1, to + 1], by time and text.
	    NoteRunDeletions, once the row is written and signed: the same window around what was written - each of the
	    run's deletions that grew (a new one counts as grown from nothing) has its new characters lined up with the
	    match in order (a GREP $n keeps some of the match: those are stepped over) and its mask extended. A deletion
	    that does not line up, or a time that carries two deletions in the window, loses its mask (read as it
	    stands). ApplyRunNotes takes the masked characters out of the records CollectRecordsOfTimes read;
	    RunMaskedText does it for one deletion's text (KFCShowChanges::ListOwnRun). SetOwnRunFloor starts the masks
	    afresh and ClearRunNotes ends them - after the list is built, not with the floor. */
	struct RunDeletion
	{
		uint64		time;
		PMString	text;
	};
	bool RunOwnChars(const UIDRef& story, TextIndex from, TextIndex to, WideString& outMatch, std::vector<bool>& outOwn);
	void SnapshotRunDeletions(const UIDRef& story, TextIndex from, TextIndex to, std::vector<RunDeletion>& out);
	void NoteRunDeletions(const UIDRef& story, TextIndex from, TextIndex to, const std::vector<RunDeletion>& before,
		const WideString& match, const std::vector<bool>& own);
	bool RunMaskedText(const UIDRef& story, uint64 time, const PMString& text, PMString& outText);
	void ApplyRunNotes(const UIDRef& story, std::vector<Record>& ioRecs);
	void ClearRunNotes();

	/** A QUERY RUN SIGNS AT ITS END (2026-10-05, the spec's D14 - the author's "A"). While a floor is set, nothing the
	    run writes is signed as it goes: a later query that deletes what an earlier one wrote then deletes the user's
	    own pending insertion, which records nothing (measured), and each place ends as one pair - the deletion of
	    what stood there before the run and the insertion of what stands there after it. QueryRunWriting = is a floor
	    set (a write path asks it to leave signing to the end). HasRunRecordIn = does any record at or after the floor
	    stand in [from, to] (a write's "was it recorded", with no stamp to look for). SignRunPlaces, once, inside the
	    run's sequence before it ends: every text model of `db`, its records at or after the floor not yet signed,
	    taken as PLACES (overlapping or touching ones together) and each place signed at a time of its own. Returns
	    the places signed, or -1 when a signing failed (the caller takes the whole run back). */
	bool QueryRunWriting();
	bool HasRunRecordIn(const UIDRef& story, TextIndex from, TextIndex to);
	int32 SignRunPlaces(IDataBase* db);


	/** Every record of the story carrying one of `times`, in position order - a deletion with its
	    deleted text. The records of a row, or of a touching group, found by their time alone
	    (KFC hands each row a time no other record can carry - the head of this file). */
	void CollectRecordsOfTimes(const UIDRef& story, const std::set<uint64>& times, std::vector<Record>& out);

	/** THE TEXT [at, at+len) HAD BEFORE THESE RECORDS. What taking
	    every record of `recs` back would leave there: each deletion's text put back where it is anchored (in
	    front of the character at its place - measured: c(at) -> k$1 on "x cat y" left the insertion "k"@2 and
	    the deletion "c"@3, in front of the kept "a"), each insertion piece left out, and every other character
	    kept. The door Reject Change and Accept Change ask of a run of rows: it must read as their original texts
	    joined. Not "the deletions hold exactly the originals" - true of a replace that writes the whole
	    match, not of a GREP Change To holding $n, whose kept characters no record holds (Hit::recordLead).
	    outAllInside = every record of `recs` stands inside the range (an insertion within [at, at+len), a
	    deletion anchored in [at, at+len]); false as well when the range cannot be read. */
	PMString OriginalFromRecords(const UIDRef& story, TextIndex at, int32 len, const std::vector<Record>& recs,
		bool& outAllInside);

	/** ONE ROW OF A LIST REBUILT FROM THE RECORDS (Show Changes by KohakuFindChange).
	    Every record signed "KohakuFindChange" carrying one time: the insertion's pieces (at = the first,
	    insLen = their sum - the text the replace wrote) and the deletion of that time, if any (its text is
	    what was replaced). A row replaced with nothing has no insertion: at = the deletion's place, insLen
	    = 0. Touching replaces written front to back leave ONE deletion, carrying the LAST row's time (the
	    head of this file) - the earlier rows of such a group come back with hasDelete false.
	    insertedText = the pieces' own text, joined - what the replace wrote. NOT the text of [at, at+insLen):
	    a piece split around somebody's typing reads their characters in that range, and FindRowChangeForHit
	    is to refuse such a row by exactly that difference. */
	struct SignedRow
	{
		uint64		time;
		TextIndex	at;
		int32		insLen;
		// From the first piece's start to the last piece's end - the row's place on the list. insLen when
		// the pieces stand side by side; more when text no record of this time holds stands between them: a
		// GREP <$0> leaves "<" and ">" around the match it kept (insLen alone would read "[<c]at>"). Such a
		// row is still refused (its pieces do not read as the text between them - FindRowChangeForHit): a
		// list rebuilt from the records cannot tell kept text from typing. KEPT SO - THE USER'S CALL ("as it
		// is"). Taking back only the pieces, as the Track Changes panel would, is not to be offered again.
		int32		spanLen;
		PMString	insertedText;
		bool		hasDelete;
		TextIndex	delAt;
		PMString	deletedText;
		SignedRow() : time(0), at(kInvalidTextIndex), insLen(0), spanLen(0), hasDelete(false), delAt(kInvalidTextIndex) {}
	};
	/** Every row the story's signed records make, one per time, in position order (Show Changes). */
	void CollectSignedRows(const UIDRef& story, std::vector<SignedRow>& out);


	/** One row's change: the text its replace wrote, [at, at+insLen), and whether a deletion of its time stands.
	    (Not its insertion pieces summed - the same thing for a replace that writes the whole match; for a
	    GREP Change To holding $n the written text also holds the matched characters it kept, which no
	    record covers. No texts and no time ride along: the texts are checked inside FindRowChangeForHit,
	    and the time is the row's own, Hit::recordTime.) */
	struct Change
	{
		TextIndex	at;
		int32		insLen;
		bool		hasDelete;
		Change() : at(0), insLen(0), hasDelete(false) {}
	};

	// (A row's records are found by its own time - CollectRecordsOfTimes, RejectRecord - never by
	//  position or by its texts and the nearest place; and a row keeps the time it was handed out,
	//  StampForRow, rather than reading one back off its records.)

	/** The touching group of a replaced row: the replaced rows outside a footnote, in text order. A group
	    is taken back as one (KFCReplaceEngine RejectRowsNow): touching replaces written front to back
	    leave ONE deletion, carrying the LAST row's time (the head of this file). */
	void ReplacedTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows);

	/** A REPLACED row put where its tracked change stands now - range, line and hash - so an edit
	    made since the replace does not put it off (the record moves with the text). False = the row
	    is not replaced, or no change of ours matches it (accepted or rejected in the Track Changes
	    panel, merged with a neighbour's): the row keeps what it had. The one lookup behind the jump,
	    Reject Change and Replace Again (section 5 of the spec named at the head of this file). */
	bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx);


	/** The document of a chapter, if it is open - found BY THE CHAPTER'S FILE, and the model rebound to
	    what it finds. The one question for everything that works on an open document only: a row's
	    records (FindRowChangeForHit), and Reject Change, Accept Change, Accept All and Replace Again
	    (KFCReplaceEngine asks this one, with no copy of its own). False = not open. */
	bool ChapterDocIfOpen(int32 chapterIdx, UIDRef& outDocRef);

	/** Like RefreshRowFromRecords, and hands the change back too. The row's own change, by its time
	    (Hit::recordTime): the records carrying it, the first of them Hit::recordLead into the text the row
	    wrote - so at = that record's place minus the lead, insLen = the replaced text's length - and three
	    questions: that text still reads as the row's replaced text, every record of its time stands inside
	    it, and what no insertion of its time covers (the characters a GREP $n kept) is a part of its
	    original text, in order. (Not the insertion pieces alone reading as the replaced text with one
	    deletion right after them: then a GREP $n's row could be neither taken back nor accepted.) A row
	    replaced with nothing whose deletion
	    InDesign joined to a touching neighbour's is found through that neighbour's deletion. False = no
	    record of the row is left. */
	bool FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange);

}

#endif // __KFCTrackChange_h__
