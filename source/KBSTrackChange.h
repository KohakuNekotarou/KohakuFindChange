//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Track Changes parts for the replace (2026-09-26, the user's design). Every replace KBS makes is
//  written with Track Changes on and signed "KohakuFindChange", and the records are LEFT in the
//  document - they are what lets one replaced row be taken back later (Reject Change) and what the
//  jump finds a replaced row by. The story's own "Track Changes" setting and InDesign's user name
//  are handed back as they were found (AuthorScope, TrackingScope).
//
//  Only records signed kAuthor are ever read or rejected here: a user's own records in the same
//  story are not KBS's to touch.
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
	/** The author name KBS's records carry. */
	extern const char* const kAuthor;	// "KohakuFindChange"

	/** InDesign's user name set to kAuthor for the life of the object, and back. */
	class AuthorScope
	{
	public:
		AuthorScope();
		~AuthorScope();
		/** False = the name could not be set; nothing may be written under it. */
		bool Ok() const { return fSwitched; }
	private:
		AuthorScope(const AuthorScope&);
		AuthorScope& operator=(const AuthorScope&);
		bool		fSwitched;
		PMString	fOld;
	};

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

	/** One record of ours, as the iterator reports it. */
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

	/** Every record of ours in the story, in position order. */
	void CollectRecords(const UIDRef& story, std::vector<Record>& out);

	/** True when the story holds at least one record of ours. */
	bool StoryHasOurChanges(const UIDRef& story);

	/** True when any story of the document holds a record of ours (Accept All Changes in This Document). */
	bool DocumentHasOurChanges(IDataBase* db);

	/** ***** ACCEPT ALL CHANGES IN THIS DOCUMENT - OURS ONLY (2026-09-27, the user's call A). ***** Every
	    record signed kAuthor in every story of the document is accepted, one whole record at a time
	    (RedlineIterator::ProcessAccept - the mirror of RejectAt's ProcessReject); nobody else's record is
	    touched. Runs inside the caller's command sequence. Returns how many were accepted, or -1 when one
	    would not be (outWhy says so - the caller rolls the sequence back). Leaves the error state clear. */
	int32 AcceptOursInDocument(IDataBase* db, PMString& outWhy);

	/** Reject ONE record of ours standing AT `position` - the deletion when `wantDelete`, else the
	    insertion - except one whose time stamp is in `keepTimes` (an earlier run's, or another row's).
	    ***** ONE, AND OF THE KIND ASKED (2026-09-26, case touching-mixed). ***** Touching replaces put the
	    first one's deletion and the next one's insertion at the SAME position ("catcat" -> "kitten":
	    deleted "cat"@6 and inserted "k"@6); rejecting "everything of ours there" took the ticked row's
	    deletion back with the unticked row's insertion. Returns 1 when one was rejected, 0 when none.
	    Leaves the global error state clear. */
	int32 RejectAt(const UIDRef& story, TextIndex position, const std::set<uint64>* keepTimes, bool wantDelete);

	/** ***** TAKE ONE REPLACE BACK - WHOLE RECORDS, OURS ONLY (2026-09-26). ***** Its deletion (at
	    delAnchor; delOffset must be 0 - a deletion shared with a touching row cannot be split) and its
	    insertion's pieces in [insAt, insAt + insLen), each rejected whole by RejectAt, and only records
	    signed kAuthor whose time stamp is not in oldTimes and, when onlyTime is not 0, is onlyTime. No
	    range is handed to InDesign (measured: an insertion range with a deletion at its start brought it
	    down). False = not ours, or not whole; outWhy says which. Callers read the text back. */
	bool RejectReplacement(const UIDRef& story, TextIndex insAt, int32 insLen,
		TextIndex delAnchor, int32 delOffset, int32 delLen, const std::set<uint64>* oldTimes, uint64 onlyTime,
		PMString& outWhy);

	/** One replacement of ours, paired: the insertion [at, at+insLen) and the deletion anchored at
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

	/** Every change of ours in the story, insertions and deletions paired, in position order. */
	void CollectChanges(const UIDRef& story, std::vector<Change>& out);

	/** The change that is a row's: inserted == newText and deleted == oldText; of several, the one
	    nearest `nearAt`. False = none. */
	bool FindRowChange(const UIDRef& story, const PMString& newText, const PMString& oldText,
		TextIndex nearAt, Change& out);

	/** The time stamp of the records of ours standing in [from, to] - the newest, 0 when none. What a
	    replaced row keeps (Hit::recordTime) so its change is found among that run's alone. */
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
