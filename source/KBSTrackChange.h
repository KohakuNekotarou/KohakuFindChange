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

	/** Does a match ending at `end` run up to the END of an endnote (the endnote story's closing
	    U+FEFF marker follows it)? InDesign rejects such a tracked insertion one character short
	    (measured 2026-09-26: "kitten" rejected leaves "e", by script as well) - so such a row is
	    handled like a footnote's: always replaced, never taken back. */
	bool IsAtEndnoteEnd(const UIDRef& story, TextIndex end);

	/** Can this row be neither left out nor taken back? A footnote's (IsInFootnote) or one at an
	    endnote's end (IsAtEndnoteEnd). */
	bool IsPinned(const UIDRef& story, TextIndex start, TextIndex end);

	/** The story's text at [at, at+len), whole (not capped). Empty when it cannot be read. */
	PMString ReadText(const UIDRef& story, TextIndex at, int32 len);

	/** Every record of ours in the story, in position order. */
	void CollectRecords(const UIDRef& story, std::vector<Record>& out);

	/** True when the story holds at least one record of ours. */
	bool StoryHasOurChanges(const UIDRef& story);

	/** Reject every record of ours standing AT `position` (an insertion and a deletion can share one),
	    except records whose time stamp is in `keepTimes` (an earlier run's - never taken back here).
	    Returns how many were rejected. Leaves the global error state clear. */
	int32 RejectAt(const UIDRef& story, TextIndex position, const std::set<uint64>* keepTimes = nil);

	/** How many records in [from, to) are NOT this row's own - its insertion [insAt, delAt) and its
	    deletion at delAt, signed kAuthor. RejectRange rejects every author's changes in its range, so
	    anything counted here would be taken back with the row (Reject Change refuses then). */
	int32 CountOthersIn(const UIDRef& story, TextIndex from, TextIndex to, TextIndex insAt, TextIndex delAt);

	/** kRejectRangeRedlineCmdBoss over [from, to): every change in the range, any author, in ONE
	    command - the Story Editor's own Reject Change over a selection. ***** The guide (vol2, the command's
	    entry): a deletion at the range's START or END index is not rejected - so a replace's range is
	    taken one past its insertion, to hold the deletion inside. False = the command did not run. */
	bool RejectRange(const UIDRef& story, TextIndex from, TextIndex to);

	/** One replacement of ours, paired: the insertion [at, at+insLen) and the deletion anchored at
	    at+insLen. `inserted` / `deleted` are the texts (deleted read from the deleted-text record). */
	struct Change
	{
		TextIndex	at;
		int32		insLen;
		bool		hasDelete;
		PMString	inserted;
		PMString	deleted;
		Change() : at(0), insLen(0), hasDelete(false) {}
	};

	/** Every change of ours in the story, insertions and deletions paired, in position order. */
	void CollectChanges(const UIDRef& story, std::vector<Change>& out);

	/** The change that is a row's: inserted == newText and deleted == oldText; of several, the one
	    nearest `nearAt`. False = none. */
	bool FindRowChange(const UIDRef& story, const PMString& newText, const PMString& oldText,
		TextIndex nearAt, Change& out);

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
