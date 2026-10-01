//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Track Changes parts for the replace (2026-09-26, the user's design). Every replace KBS makes is
//  written with Track Changes on, and the records are LEFT in the document - they are what lets one
//  replaced row be taken back later (Reject Change) and what the jump finds a replaced row by. The
//  story's own "Track Changes" setting is handed back as it was found (TrackingScope).
//
//  ***** SIGNED "KohakuFindChange" AT A TIME KBS HANDS OUT (2026-09-28, the user's call; spec
//  docs/superpowers/specs/_done/2026-09-28-kbs-signed-designated-time-design.md). ***** InDesign's user name is
//  not touched (a name the script DOM cannot put back to "unset" is a name that could be left behind -
//  why the 2026-09-26 switch of it went on 2026-09-27): each replace is written under it and its records
//  are rewritten right after, before the next row is written (KBSSignRecordsCmd). The time is the run's
//  start, cut to the millisecond, plus the row's number in the result in 100 ns units (the four digits
//  below the millisecond - no panel and no script shows them). A replaced row keeps its time
//  (Hit::recordTime) and is tied to its records by that time exactly. (From 2026-09-27 to 2026-09-28 the
//  records carried the user's own name and the clock's time, and a row was found by its texts and the
//  nearest position.) "Accept All Changes by KohakuFindChange in This Document" accepts the signed records only
//  (2026-09-29, the user's call - from 2026-09-27 it accepted every change, as InDesign's own Accept All).
//  ! Touching replaces written front to back leave one insertion per row but ONE deletion, carrying the
//    LAST row's time: InDesign joins a deletion to the one it touches whoever made either (measured with
//    two real user names). A touching group is taken back as one (KBSReplaceEngine RejectRowsNow).
//
//  ***** WHERE THE RECORDS ARE (measured 2026-09-26). ***** A replacement leaves an insertion record
//  over the new text and a deletion record anchored right after it (redlineiterator.h:42-50);
//  replacements that touch merge their records. Text in table cells and footnotes is recorded too
//  (the user checked it in the UI) - the DOM's Story.changes lists only the story's main text, which
//  is why a probe once said otherwise. NewRedlineIterator walks the whole story's TextIndex space,
//  cells and footnotes included.
//
//========================================================================================

#ifndef __KBSTrackChange_h__
#define __KBSTrackChange_h__

#include "PMString.h"
#include "UIDRef.h"

#include <set>
#include <vector>

class IDataBase;

namespace KBSTrackChange
{
	// (kAuthor and AuthorScope - the "KohakuFindChange" signature and the switch of InDesign's user
	//  name that wrote it - stood here until 2026-09-27. See the head of this file.)

	/** The author of every record KBS writes (2026-09-28). */
	extern const char* const kSignAuthor;		// "KohakuFindChange"
	// (kSignFailedWhy - the summary told a signing failure apart by its text - stood here until 2026-09-28:
	//  every reason a run stops on is said the same way now, KBSReplaceEngine's stoppedByFailure.)

	/** A replace run begins (Change Checked, or a row's / story's / document's Replace): its time T0 is
	    the clock cut to the millisecond - or the millisecond after the last time handed out, if the clock
	    is not already past it. */
	void BeginSignedRun();
	/** The row's time in the run: T0 + the row's number in the result - the rows of every chapter before
	    it, then its index - in 100 ns units. Remembered as the last handed out. */
	uint64 StampForRow(int32 chapterIdx, int32 hitIdx);
	/** Sign the records one replace just made - the insertion's pieces in [from, to) and the deletion
	    anchored in [from, to], those not yet "KohakuFindChange" - through kKBSSignRecordsCmdBoss.
	    False = they could not be signed (the caller stops the run). Nothing to sign (a footnote) is not
	    a failure. */
	bool SignReplace(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp);
	/** The command's work (KBSSignRecordsCmd::Do) - not to be called from anywhere else. */
	bool SignRecordsNow(const UIDRef& story, TextIndex from, TextIndex to, uint64 stamp);
	/** Is there a record carrying exactly this time standing in [from, to] (where a replace just wrote)? */
	bool HasRecordsOfTimeIn(const UIDRef& story, TextIndex from, TextIndex to, uint64 time);

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

	/** Is `at` inside a footnote? Track Changes records nothing there (measured 2026-09-26 through
	    IDML), so a footnote's rows are always replaced and can be neither left out nor taken back.
	    A footnote's thread IS its reference boss (KCMTextRead's test). */
	bool IsInFootnote(const UIDRef& story, TextIndex at);

	/** The story's text at [at, at+len), whole (not capped). Empty when it cannot be read. */
	PMString ReadText(const UIDRef& story, TextIndex at, int32 len);

	// (CollectRecords - every record in the story, the bound on the accept loops - stood here until
	//  2026-09-29: see AcceptSignedInDocument and AcceptPendingAround.)

	/** True when the story holds at least one record. */
	bool StoryHasChanges(const UIDRef& story);

	/** True when any story of the document holds a record signed "KohakuFindChange" - the ones Accept
	    All Changes by KohakuFindChange accepts. (Until 2026-09-29: any record, DocumentHasChanges.) */
	bool DocumentHasSignedRecords(IDataBase* db);

	/** ***** ACCEPT ALL CHANGES BY KohakuFindChange - ONLY THE RECORDS SIGNED SO (2026-09-29, the user's
	    ***** call: "only the ones named KohakuFindChange"). ***** InDesign's own Accept All command
	    (kAcceptAllRedlineCmdBoss) over each story holding such a record, told the author: everybody
	    else's changes stay. A change in hidden conditional text is not accepted (the command's default,
	    measured) - outLeft counts the signed records still there after it, for the caller to say (hidden
	    conditional text is the one cause measured). (From 2026-09-27 to 2026-09-29 it accepted
	    every change, whoever made it, one whole record at a time - AcceptAllInDocument.) Runs inside the
	    caller's command sequence. Returns how many were accepted, or -1 when InDesign would not (outWhy
	    says so - the caller rolls the sequence back). Leaves the error state clear.
	    outAcceptedTimes (optional, 2026-09-29) = the times every record of which this took away - a row
	    carrying one was accepted (re-check R-4). Gathered on the two walks each story is counted by anyway. */
	int32 AcceptSignedInDocument(IDataBase* db, int32& outLeft, PMString& outWhy,
		std::set<uint64>* outAcceptedTimes = nil);

	// (IsInsideOwnPendingInsertion - does a match touch the current user's pending insertion, which the
	//  run then refused - stood here until 2026-09-28. Nothing had called it since the pending changes
	//  around a ticked match came to be accepted first, AcceptPendingAround, on 2026-09-27.)

	/** ***** ACCEPT THE PENDING CHANGES A MATCH ABOUT TO BE REPLACED SITS IN OR NEXT TO - ANYBODY'S
	    ***** (2026-09-27, the user's call: "only that part"). ***** Every insertion overlapping or touching
	    [from, to) is accepted, and every deletion anchored in [from, to] (an insertion's own deletion
	    stands at its end), one whole record at a time, whoever made it; nothing else in the story is
	    touched. Needed for the user's own insertion at least: replacing text its author inserted and has
	    not accepted leaves no record - InDesign rewrites the insertion it has (VOSRedline.h
	    CanApplyDeleteChange; measured with case rereplace-ours) - so the replace could never be taken
	    back. The main text does not move (an accepted insertion stays, an accepted deletion was never in
	    it), but a deleted-text thread goes, so a caller holding story indexes past it should hold them as
	    thread offsets. Only the records from one before `from` to `to` are walked (2026-09-29). Runs
	    inside the caller's command sequence. Returns how many were accepted, or -1 when one would not be
	    (outWhy says so). Leaves the error state clear. */
	int32 AcceptPendingAround(const UIDRef& story, TextIndex from, TextIndex to, PMString& outWhy);


	/** Every record of the story carrying one of `times`, in position order - a deletion with its
	    deleted text. The records of a row, or of a touching group, found by their time alone
	    (2026-09-28: KBS hands each row a time no other record can carry - the head of this file). */
	void CollectRecordsOfTimes(const UIDRef& story, const std::set<uint64>& times, std::vector<Record>& out);

	/** ***** ONE ROW OF A LIST REBUILT FROM THE RECORDS (2026-09-29, Show Changes by KohakuFindChange). *****
	    Every record signed "KohakuFindChange" carrying one time: the insertion's pieces (at = the first,
	    insLen = their sum - the text the replace wrote) and the deletion of that time, if any (its text is
	    what was replaced). A row replaced with nothing has no insertion: at = the deletion's place, insLen
	    = 0. Touching replaces written front to back leave ONE deletion, carrying the LAST row's time (the
	    head of this file) - the earlier rows of such a group come back with hasDelete false.
	    insertedText = the pieces' own text, joined - what the replace wrote. NOT the text of [at, at+insLen):
	    a piece split around somebody's typing reads their characters in that range, and FindRowChangeForHit
	    is to refuse such a row by exactly that difference (re-check R-2, 2026-09-29). */
	struct SignedRow
	{
		uint64		time;
		TextIndex	at;
		int32		insLen;
		PMString	insertedText;
		bool		hasDelete;
		TextIndex	delAt;
		PMString	deletedText;
		SignedRow() : time(0), at(kInvalidTextIndex), insLen(0), hasDelete(false), delAt(kInvalidTextIndex) {}
	};
	/** Every row the story's signed records make, one per time, in position order (Show Changes). */
	void CollectSignedRows(const UIDRef& story, std::vector<SignedRow>& out);

	/** Take back the ONE record standing at `at` of that kind and of exactly that time - whole: no range
	    is handed to InDesign (an insertion range with a deletion at its start brought it down,
	    2026-09-26). True = it was. Leaves the global error state clear.
	    ***** ONE, AND OF THE KIND ASKED (2026-09-26, case touching-mixed). ***** Touching replaces put one
	    row's deletion and the next row's insertion at the SAME position ("catcat" -> "kitten": deleted
	    "cat"@6 and inserted "k"@6). */
	bool RejectRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete);
	/** RejectRecord's twin (2026-09-29, Accept Change by KohakuFindChange): ACCEPT the one record standing at
	    `at` of that kind and of exactly that time, whole. True = it was. Leaves the global error state clear. */
	bool AcceptRecord(const UIDRef& story, TextIndex at, uint64 time, bool isDelete);

	/** One row's change: its insertion [at, at+insLen) and whether a deletion of its time stands.
	    (The two texts and the time rode along until 2026-09-29, and no caller read them: the texts are
	    checked inside FindRowChangeForHit, and the time is the row's own, Hit::recordTime.) */
	struct Change
	{
		TextIndex	at;
		int32		insLen;
		bool		hasDelete;
		Change() : at(0), insLen(0), hasDelete(false) {}
	};

	// (RejectAt / RejectReplacement - a row's records taken back by position and "not an earlier run's
	//  time" - and CollectChanges / FindRowChange / FindGroupChange - a row's change found by its texts and
	//  the nearest place, pairing records by time - stood here until 2026-09-28. A row's records are
	//  found by its own time now: CollectRecordsOfTimes, RejectRecord.)
	// (RecordTimeIn - the time read back off a replaced row's records - stood here until 2026-09-28: a row
	//  keeps the time it was handed out now, StampForRow.)

	/** A REPLACED row put where its tracked change stands now - range, line and hash - so an edit
	    made since the replace does not put it off (the record moves with the text). False = the row
	    is not replaced, or no change of ours matches it (accepted or rejected in the Track Changes
	    panel, merged with a neighbour's): the row keeps what it had. The one lookup behind the jump,
	    Reject Change and Redo (spec section 5). */
	bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx);

	/** ***** THE REPLACED ROWS WRITTEN SIDE BY SIDE WITH THIS ONE, AS THE DOCUMENT HAS THEM NOW (2026-09-29 -
	    ***** the message area's "Source Text:", KBSJump::ActivateNode). ***** Taken outward from the row
	    in the list's order (KBSResultModel::GetStoryRowsInOrder), each neighbour put where its tracked
	    change stands first (RefreshRowFromRecords) and taken in while its text meets the group's - so an
	    edit made since the replace, which leaves the stored ranges of every row but the clicked one behind,
	    does not split the group (the stored-range rule, GetTouchingGroup, did - measured). Reads the group
	    and one row either side, not the whole story.
	    The row itself is expected to have been put where it stands already (the jump does it).
	    @param outRows the rows in text order, hitIdx included; EMPTY when the row holds no replace.
	    @param outRefreshed true when a neighbour was read again - its line may read differently now, and
	        the caller repaints the list. */
	void CurrentReplacedGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows, bool& outRefreshed);

	/** Like RefreshRowFromRecords, and hands the change back too. The row's own change, by its time
	    (Hit::recordTime): the insertion pieces carrying it (at = the first, insLen = their sum, which must
	    read as the row's replaced text) and the deletion carrying it, if any. A row replaced with nothing
	    whose deletion InDesign joined to a touching neighbour's is found through that neighbour's
	    deletion. False = no record of the row is left. */
	bool FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange);

	/** The touching group of a replaced row: the replaced rows outside a footnote, in text order. A group
	    is taken back as one (KBSReplaceEngine RejectRowsNow): touching replaces written front to back
	    leave ONE deletion, carrying the LAST row's time (the head of this file). */
	void ReplacedTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows);
}

#endif // __KBSTrackChange_h__
