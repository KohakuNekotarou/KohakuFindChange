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
//  replacements that touch merge their records. ! EXCEPT a GREP Change To holding $n (measured 2026-10-04,
//  scenario cross-check 5): the one-at-a-time replace KEEPS the matched characters $n names and records only
//  around them - c(at) -> k$1 on "cat": insertion "k", deletion "c" in front of the kept "at"; cat -> <$0>: two
//  insertions, "<" and ">", and no deletion; c(at) -> $1: the deletion of "c" alone. (Change All records the
//  whole match.) A row's change is found with that in mind - Hit::recordLead, OriginalFromRecords. Text in table cells is recorded too (the user checked
//  it in the UI) - the DOM's Story.changes lists only the story's main text, which is why a probe once
//  said otherwise. Text in a FOOTNOTE is not: measured the same day through IDML (IsInFootnote), and
//  case show-footnote finds no record of a footnote's replace. NewRedlineIterator walks the whole
//  story's TextIndex space - cells, footnotes and hidden conditional text included.
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
	/** Where the first record carrying exactly this time stands in [from, to] (where a replace just wrote): the
	    first insertion piece, or the first deletion when there is no insertion. False = no such record (a
	    footnote, or a replace that changed no character). A row's Hit::recordLead is that place minus `from`
	    (2026-10-04, scenario cross-check 5 - HasRecordsOfTimeIn, the yes/no alone, until then). */
	bool FirstRecordOfTimeIn(const UIDRef& story, TextIndex from, TextIndex to, uint64 time, TextIndex& outAt);
	/** ***** "KohakuFindChange" IN AMBER (2026-10-02, the user's call). ***** A tracked change is drawn in
	    its AUTHOR's colour, from the document's list of users (IInCopyDocUserList on kDocBoss: a name -> an
	    index into the session's UI colours, IInCopyUIColors), and the name KBS signs with stood in no
	    document's list - its changes were drawn on a white background (measured, KT app.ktProbe
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

	/** Is `at` inside a footnote? Track Changes records nothing there (measured 2026-09-26 through
	    IDML), so a footnote's rows are always replaced and can be neither left out nor taken back.
	    A footnote's thread IS its reference boss (KCMTextRead's test). */
	bool IsInFootnote(const UIDRef& story, TextIndex at);

	/** Is `at` inside text a HIDDEN condition holds (2026-10-02, case reject-hidden-condition)? While its
	    condition is hidden, conditional text - and the tracked changes in it - stand in a thread of their own
	    past the main text, and showing the condition puts them back (measured: a row's insertion read at 14
	    while hidden, at 0 once shown; its deletion stayed in the main text). That thread's owned item is
	    kHiddenTextBoss (customconditionaltext/CusCondTxtSuiteTextCSB.cpp:205 says so, and
	    conditionaltextui/ConditionalTextTips.cpp:232 asks it this way) - IsInFootnote's test, with that class. */
	bool IsInHiddenText(const UIDRef& story, TextIndex at);

	/** ***** WHERE HIDDEN TEXT COMES BACK TO (2026-10-04, the [9b] re-read D9b-1). ***** kInvalidTextIndex
	    unless `at` is inside text a hidden condition holds; then the place that text goes back to when the
	    condition is shown - the anchor of its kHiddenTextBoss, an owned item (IOwnedItem::GetTextIndex;
	    kHiddenTextOwnedItemImpl in the 20.5 boss dump), climbed again while that place is hidden too. Such
	    text sits in a thread no frame holds, so its page and its jump are asked at this place instead: a row
	    of it - only a list rebuilt from the records has one, the search does not walk hidden conditional
	    text - read "P1(2) overset" and its jump went to the frame's "+" until then (measured). */
	TextIndex HiddenTextAnchor(const UIDRef& story, TextIndex at);

	/** Does this replaced row's insertion stand under a hidden condition right now? Then its change cannot be
	    found where the row is - FindRowChangeForHit refuses it, the deletion having stayed in the main text -
	    and Reject Change / Accept Change on it are refused until the condition is shown (the user's call A,
	    2026-10-02: refused and SAID, not taken back while hidden). The one question every refusal asks to give
	    the right reason. False for a row with no insertion (replaced with nothing), a footnote's, or a closed
	    document's. */
	bool RowChangeIsHidden(int32 chapterIdx, int32 hitIdx);

	/** The story's text at [at, at+len), whole (not capped). Empty when it cannot be read. */
	PMString ReadText(const UIDRef& story, TextIndex at, int32 len);

	// (CollectRecords - every record in the story, the bound on the accept loops - stood here until
	//  2026-09-29: see AcceptSignedInDocument and AcceptPendingAround.)

	/** True when the story holds at least one record OUTSIDE HIDDEN CONDITIONAL TEXT - the strand's own
	    flag, which leaves those out (IRedlineDataStrand.h:107-112; "at least one record" until 2026-10-02).
	    A question for work on VISIBLE text only - the pending changes around a match being replaced
	    (KBSReplaceEngine). Nothing that looks for a row's records asks it: they are walked wherever they
	    stand (CollectRecordsOfTimes, CollectSignedRows, CountSignedRecords). */
	bool StoryHasChanges(const UIDRef& story);

	/** True when any story of the document holds a record signed "KohakuFindChange" - the ones Accept
	    All Changes by KohakuFindChange accepts. (Until 2026-09-29: any record, DocumentHasChanges.) */
	bool DocumentHasSignedRecords(IDataBase* db);

	/** ***** ACCEPT ALL CHANGES BY KohakuFindChange - ONLY THE RECORDS SIGNED SO (2026-09-29, the user's
	    ***** call: "only the ones named KohakuFindChange"). ***** InDesign's own Accept All command
	    (kAcceptAllRedlineCmdBoss) over each story holding such a record, told the author: everybody
	    else's changes stay. A change in hidden conditional text is not accepted (the command's default,
	    measured). (From 2026-09-27 to 2026-09-29 it accepted every change, whoever made it, one whole
	    record at a time - AcceptAllInDocument.) Runs inside the caller's command sequence.
	    ***** COUNTED IN REPLACES, AS SHOW CHANGES COUNTS THEM (2026-10-04, the [9b] re-read D9b-2). *****
	    Returns how many replaces it accepted - the times every record of which it took away, one per row -
	    or -1 when InDesign would not (outWhy says so - the caller rolls the sequence back). outLeft = the
	    replaces with a record still standing after it, for the caller to say (hidden conditional text is
	    the one cause measured). Until then both counted RECORDS, an insertion and a deletion apiece: a
	    document Show Changes had just called "Found 2 change(s)" came out "Accepted 4 change(s)". Leaves
	    the error state clear.
	    outAcceptedTimes (optional, 2026-09-29) = those accepted times - a row carrying one was accepted
	    (re-check R-4). Gathered on the two walks each story is counted by anyway. */
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

	/** ***** THE TEXT [at, at+len) HAD BEFORE THESE RECORDS (2026-10-04, scenario cross-check 5). ***** What taking
	    every record of `recs` back would leave there: each deletion's text put back where it is anchored (in
	    front of the character at its place - measured: c(at) -> k$1 on "x cat y" left the insertion "k"@2 and
	    the deletion "c"@3, in front of the kept "a"), each insertion piece left out, and every other character
	    kept. The door Reject Change and Accept Change ask of a run of rows: it must read as their original texts
	    joined. It replaced "the deletions hold exactly the originals" - true of a replace that writes the whole
	    match, not of a GREP Change To holding $n, whose kept characters no record holds (Hit::recordLead).
	    outAllInside = every record of `recs` stands inside the range (an insertion within [at, at+len), a
	    deletion anchored in [at, at+len]); false as well when the range cannot be read. */
	PMString OriginalFromRecords(const UIDRef& story, TextIndex at, int32 len, const std::vector<Record>& recs,
		bool& outAllInside);

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
		// From the first piece's start to the last piece's end (2026-10-04, scenario cross-check 5) - the row's
		// place on the list. insLen when the pieces stand side by side; more when text no record of this time
		// holds stands between them: a GREP <$0> leaves "<" and ">" around the match it kept, and its row read
		// "[<c]at>" until then. Such a row is still refused (its pieces do not read as the text between them -
		// FindRowChangeForHit): a list rebuilt from the records cannot tell kept text from typing. ***** KEPT SO
		// - THE USER'S CALL (2026-10-04, scenario cross-check 5 Q-1: "as it is"). ***** Taking back only the
		// pieces, as the Track Changes panel would, is not to be offered again.
		int32		spanLen;
		PMString	insertedText;
		bool		hasDelete;
		TextIndex	delAt;
		PMString	deletedText;
		SignedRow() : time(0), at(kInvalidTextIndex), insLen(0), spanLen(0), hasDelete(false), delAt(kInvalidTextIndex) {}
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

	/** One row's change: the text its replace wrote, [at, at+insLen), and whether a deletion of its time stands.
	    (Its insertion pieces summed until 2026-10-04 - the same thing for a replace that writes the whole
	    match; for a GREP Change To holding $n the written text also holds the matched characters it kept,
	    which no record covers. The two texts and the time rode along until 2026-09-29, and no caller read
	    them: the texts are checked inside FindRowChangeForHit, and the time is the row's own, Hit::recordTime.) */
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

	/** The document of a chapter, if it is open - found BY THE CHAPTER'S FILE, and the model rebound to
	    what it finds (2026-09-29, the defect re-check F-3). The one question for everything that works
	    on an open document only: a row's records (FindRowChangeForHit), and Reject Change, Accept Change,
	    Accept All and Replace Again (KBSReplaceEngine - its own copy until 2026-10-01). False = not open. */
	bool ChapterDocIfOpen(int32 chapterIdx, UIDRef& outDocRef);

	/** Like RefreshRowFromRecords, and hands the change back too. The row's own change, by its time
	    (Hit::recordTime): the records carrying it, the first of them Hit::recordLead into the text the row
	    wrote - so at = that record's place minus the lead, insLen = the replaced text's length - and three
	    questions (2026-10-04, scenario cross-check 5): that text still reads as the row's replaced text, every
	    record of its time stands inside it, and what no insertion of its time covers (the characters a GREP $n
	    kept) is a part of its original text, in order. (Until then: the insertion pieces alone, which had to
	    read as the replaced text - at = the first, insLen = their sum - and one deletion right after them, so a
	    GREP $n's row could be neither taken back nor accepted.) A row replaced with nothing whose deletion
	    InDesign joined to a touching neighbour's is found through that neighbour's deletion. False = no
	    record of the row is left. */
	bool FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange);

	/** The touching group of a replaced row: the replaced rows outside a footnote, in text order. A group
	    is taken back as one (KBSReplaceEngine RejectRowsNow): touching replaces written front to back
	    leave ONE deletion, carrying the LAST row's time (the head of this file). */
	void ReplacedTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows);
}

#endif // __KBSTrackChange_h__
