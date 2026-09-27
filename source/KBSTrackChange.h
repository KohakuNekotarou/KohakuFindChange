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
//  ***** SIGNED WITH THE USER'S OWN NAME, AND TOLD APART BY TIME (2026-09-27, the user's call). *****
//  Until then every replace switched InDesign's user name to "KohakuFindChange" for the run and back
//  after it, and told its records apart by that author. A name the script DOM cannot put back to
//  "unset" was a name that could be left behind (the user: "a name left behind is what I fear"), so
//  the name is not touched any more. A record is found by WHERE it stands and by its TIME STAMP
//  (VOSRedlineChange::GetTimeStamp - the date and time the change was created, 100 ns units since
//  1601): the records a run made are the ones whose time was not in the story before the run, and a
//  replaced row keeps the time its records carry (Hit::recordTime). No list of KBS's times is kept
//  anywhere - "Accept All Changes in This Document" accepts every change, as InDesign's own does.
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
	};

	/** Is `at` inside a footnote? Track Changes records nothing there (measured 2026-09-26 through
	    IDML), so a footnote's rows are always replaced and can be neither left out nor taken back.
	    A footnote's thread IS its reference boss (KCMTextRead's test). */
	bool IsInFootnote(const UIDRef& story, TextIndex at);

	/** The story's text at [at, at+len), whole (not capped). Empty when it cannot be read. */
	PMString ReadText(const UIDRef& story, TextIndex at, int32 len);

	/** Every record in the story, in position order - whoever made it. Which ones are a run's is told
	    by time (the caller's set of the times that stood before it wrote). */
	void CollectRecords(const UIDRef& story, std::vector<Record>& out);

	/** True when the story holds at least one record. */
	bool StoryHasChanges(const UIDRef& story);

	/** True when any story of the document holds a record (Accept All Changes in This Document). */
	bool DocumentHasChanges(IDataBase* db);

	/** ***** ACCEPT ALL CHANGES IN THIS DOCUMENT - EVERY CHANGE, AS InDesign's OWN (2026-09-27, the user's
	    call). ***** Every record in every story of the document is accepted, one whole record at a time
	    (RedlineIterator::ProcessAccept - the mirror of RejectAt's ProcessReject), whoever made it. (It
	    accepted "KohakuFindChange"-signed records only from 2026-09-27 morning until the signature went
	    the same day.) Runs inside the caller's command sequence. Returns how many were accepted, or -1
	    when one would not be (outWhy says so - the caller rolls the sequence back). Leaves the error
	    state clear. */
	int32 AcceptAllInDocument(IDataBase* db, PMString& outWhy);

	/** ***** DOES [from, to) OVERLAP OR TOUCH A PENDING INSERTION OF THE CURRENT USER? (2026-09-27, P-4.) *****
	    An author replacing text it inserted itself, not yet accepted, gets NO new record - InDesign
	    rewrites the insertion it has (VOSRedline.h CanApplyDeleteChange; measured with case
	    rereplace-ours). A replace there cannot be lined up or taken back, so the run asks this BEFORE it
	    writes and refuses. Since KBS signs with the user's own name, that covers an earlier KBS replace
	    not yet accepted AND the user's own tracked typing. Touching counts too (not measured: typing
	    extends an insertion of the same author, so new text written next to one may join it).
	    `onlyTimes` (when not nil) = look only at records whose time is in it - Redo passes the times that
	    stood before it wrote, so the row it has just written for a touching neighbour does not count
	    (case touching-group-redo; touching insertions of different times stay apart, as Redo measured). */
	bool IsInsideOwnPendingInsertion(const UIDRef& story, TextIndex from, TextIndex to,
		const std::set<uint64>* onlyTimes = nil);

	/** Reject ONE record standing AT `position` - the deletion when `wantDelete`, else the insertion -
	    except one whose time stamp is in `keepTimes`. The caller puts in `keepTimes` every time that is
	    not the row's run: the times that stood before the run (the replace), or every time but the
	    row's own (Reject Change) - so nobody else's record, and no other run's, can be taken.
	    ! A time does not name a ROW: several replaces of one run share one (rangelog-2026-09-26.txt), so
	    it is the position that says which row's record this is.
	    ***** ONE, AND OF THE KIND ASKED (2026-09-26, case touching-mixed). ***** Touching replaces put the
	    first one's deletion and the next one's insertion at the SAME position ("catcat" -> "kitten":
	    deleted "cat"@6 and inserted "k"@6); rejecting "everything of ours there" took the ticked row's
	    deletion back with the unticked row's insertion. Returns 1 when one was rejected, 0 when none.
	    Leaves the global error state clear. */
	int32 RejectAt(const UIDRef& story, TextIndex position, const std::set<uint64>* keepTimes, bool wantDelete);

	/** ***** TAKE ONE REPLACE BACK - WHOLE RECORDS, THE ROW'S RUN ONLY (2026-09-26). ***** Its deletion (at
	    delAnchor; delOffset must be 0 - a deletion shared with a touching row cannot be split) and its
	    insertion's pieces in [insAt, insAt + insLen), each rejected whole by RejectAt, and only records
	    whose time stamp is not in oldTimes and, when onlyTime is not 0, is onlyTime. At least one of the
	    two has to be given: with neither, any record there would qualify (refused). No range is handed to
	    InDesign (measured: an insertion range with a deletion at its start brought it down). False = not
	    the run's, or not whole; outWhy says which. Callers read the text back. */
	bool RejectReplacement(const UIDRef& story, TextIndex insAt, int32 insLen,
		TextIndex delAnchor, int32 delOffset, int32 delLen, const std::set<uint64>* oldTimes, uint64 onlyTime,
		PMString& outWhy);

	/** One replacement, paired: the insertion [at, at+insLen) and the deletion anchored at
	    at+insLen. `inserted` / `deleted` are the texts (deleted read from the deleted-text record). */
	struct Change
	{
		TextIndex	at;
		int32		insLen;
		bool		hasDelete;
		PMString	inserted;
		PMString	deleted;
		uint64		time;		// the records' time stamp - one run's (VOSRedlineChange::GetTimeStamp)
		Change() : at(0), insLen(0), hasDelete(false), time(0) {}
	};

	/** Every change in the story, insertions and deletions paired (same time, the deletion right after
	    the insertion), in position order. Whose it is, is the caller's question - by time. */
	void CollectChanges(const UIDRef& story, std::vector<Change>& out);

	/** The change that is a row's: inserted == newText and deleted == oldText; of several, the one
	    nearest `nearAt`. False = none. */
	bool FindRowChange(const UIDRef& story, const PMString& newText, const PMString& oldText,
		TextIndex nearAt, Change& out);

	/** The time stamp of the row's own record of ours: the insertion starting at `from` when the replace
	    wrote text (to > from), else the one deletion standing at `from`; 0 when none, or when two
	    deletions stand there. What a replaced row keeps (Hit::recordTime) so its change is found among
	    that replace's alone. NOT the newest in [from, to]: a touching neighbour's deletion stands at
	    `from` with its own time (2026-09-27). */
	uint64 RecordTimeIn(const UIDRef& story, TextIndex from, TextIndex to);

	/** A REPLACED row put where its tracked change stands now - range, line and hash - so an edit
	    made since the replace does not put it off (the record moves with the text). False = the row
	    is not replaced, or no change of ours matches it (accepted or rejected in the Track Changes
	    panel, merged with a neighbour's): the row keeps what it had. The one lookup behind the jump,
	    Reject Change and Redo (spec section 5). */
	bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx);

	/** Like RefreshRowFromRecords, and hands the change back too. */
	bool FindRowChangeForHit(int32 chapterIdx, int32 hitIdx, UIDRef& outStory, Change& outChange);
}

#endif // __KBSTrackChange_h__
