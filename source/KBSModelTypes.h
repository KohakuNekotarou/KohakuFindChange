//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The model half's PLAIN TYPES - the ones the UI half reads as well (2026-10-01, the model/UI split).
//  Structs, enums and constants only: no function is declared here, so a UI file that includes this
//  cannot reach into the model plug-in's code by accident - every call goes through the session
//  interfaces (IKBSResults / IKBSRuns / IKBSChapters). The namespaces are the ones these types always
//  had, so KBSResultModel::RowDisplay is still spelled KBSResultModel::RowDisplay.
//  Moved here verbatim from KBSResultModel.h, KBSSearchEngine.h and KBSOversetLocator.h.
//
//========================================================================================

#ifndef __KBSModelTypes_h__
#define __KBSModelTypes_h__

#include "BaseType.h"
#include "IDFile.h"
#include "PMPoint.h"		// PBPMPoint - KBSOversetLoc
#include "PMString.h"
#include "UIDRef.h"

#include <map>
#include <vector>

namespace KBSResultModel
{
	/** The panel shows at most this many hit rows (book order). The model still HOLDS every hit -
	    a same-book re-search reuses them, and a future export / replace consumes them ALL - only
	    the tree display is capped, to keep a huge result set from flooding the panel. */
	const int32 kKBSDisplayHitLimit = 5000;

	/** The whole-RUN safety ceiling: a search stops collecting after this many hit rows across every
	    chapter, so no query or document can pile up an unbounded result set. Unlike the display cap
	    above this bounds the RESULT SET itself; the search says so in its summary rather than coming
	    back quietly short. Counted in ROWS, the same unit the display cap uses.
	    (It lived in KBSSearchEngine.cpp until 2026-08-03, when the two scans were given it too; they
	    were removed on 2026-09-27.) */
	const int32 kKBSCollectHitLimit = 10000;

	/** What became of a hit when a replace ran over it. Only ever set on rows the replace actually
	    reached; everything else stays kOutcomeNone. Drawn as a word on the end of the locator. */
	enum ChangeOutcome
	{
		kOutcomeNone = 0,	// replaced, or never reached
		kOutcomeMissing,	// the text could not be found where the search left it (moved or deleted)
		kOutcomeLocked,		// it became locked between the search and the replace
		kOutcomeRefused,	// InDesign's own replace command would not run there
		kOutcomeRejected,	// replaced, then taken back with Reject Change (2026-09-26): the row
							// shows the original text again and can be replaced once more (Redo)
		kOutcomeDeleted,	// ticked, and gone WITH the footnote / table / anchored object another
							// ticked row deleted (2026-09-26) - Change All's own result; no place to jump to
		kOutcomeEndnoteLeft,// ticked, in the endnote story, left alone: a match there ends an endnote,
							// and InDesign's replace breaks an endnote at its end (2026-09-27, the
							// user's call - the whole endnote story is left, Change All works by story)
		kOutcomeAccepted	// replaced, then its tracked change ACCEPTED with Accept Change by
							// KohakuFindChange (2026-09-29): the replace is final, nothing is left to
							// take back or accept - the locator says "accepted"
	};

	/** One match on one line of one chapter. The three text segments are the line split around
	    the match; the jump anchors (Task 3) point back at the exact occurrence. */
	struct Hit
	{
		PMString	locator;	// the page locator "P<page>(<n>)" / "overset" (drawn at full text
								// colour, ahead of the line, with one gap between the two - there is
								// no tab stop: the locator's width varies too much for a fixed
								// column, see KBSColorTextView)
		PMString	preText;	// the line's text before the match
		PMString	matchText;	// the matched text (drawn at full text colour)
		PMString	postText;	// the line's text after the match

		PMString	pageString;	// the page named in the locator, Pages-panel style. For a visible
								// match: its own page. For an overset match: the page of the "+"
								// indicator (or "" when nothing is placed anywhere).
		int32		pageIndex;	// that page's document order (-1 = no page); sorts hits into page order
		bool		isOverset;	// match is overset -> the locator gets a trailing " overset"
								// ("P<page>(<n>) overset")
		bool		isLocked;	// match sits on a locked layer or in a locked story -> the locator
								// gets " locked" and the row gets NO check box. InDesign can search
								// locked content but offers no way to change it ("Search Only"), so
								// the row is listed and jumpable but never selectable.
		bool		isHidden;	// match sits on a switched-off layer -> the locator gets " hidden".
								// Only reachable when the Find/Change dialog's "Include Hidden
								// Layers" is on, and then the text is composed and jumpable but
								// draws nothing, so the row has to say why the page looks empty.

		int32		fontGroup;	// which of its chapter's fontGroups (its STORY group) this hit belongs
		int32		fontGroupPos;	// to, and where it sits inside that group. -1 before AppendChapter
								// groups it (every hit has a group since 2026-09-27). Filled by AppendChapter; the
								// tree reads them to answer "who is my parent" and "which child am
								// I" without searching.

		UID			storyUID;	// the story the match lives in (within its chapter's database)
		TextIndex	textStart;	// the match's start position in that story
		TextIndex	textEnd;	// the match's end position (Task 3 marker rectangle)

		// The WHOLE match, as one number - what the same-occurrence test compares against.
		// matchText below is capped for drawing (the line budget, kKBSMaxLineChars in
		// KBSSearchEngine.cpp), so it cannot answer "is this still the same text" for a long
		// GREP match; this can. 0 = never computed, or the text
		// could not be read, and it never compares equal (see MatchIsSameOccurrence). A zero-width
		// match (GREP ^ / $ / lookarounds) also stores 0, and is accepted on its length arm
		// instead - an empty range has no text for the hash to vouch for.
		uint64		matchHash;

		// --- replace support ---
		// (A walkOrder stood here until 2026-09-29 - the order the walker handed this match back in,
		// which Change Checked's verify walk lined its matches up by, and which every row menu's Replace
		// or Reject numbered again with a walk of the whole chapter. A row is found by its PLACE now:
		// the verify walk asks for a match at the row's start (ChapterMovedUnderRows), the writing walk
		// for one at its thread, offset and length (RowOfMatchAnyOrder).)
		bool		checked;	// selected for replacement. Every row starts UNTICKED (2026-09-27, the
								// user's call - AppendChapter); the user ticks what is to be replaced.
		bool		replaced;	// already replaced in this result set - not selectable any more
		ChangeOutcome outcome;	// why this row was NOT replaced (kOutcomeNone = it was, or was never
								// reached at all). The locator shows it as a word.
		// (uint32 storyChangeCount stood here - ITextModel::GetTextChangeCount for this hit's story as
		// the search left it, so the replace could take an unedited story on trust and skip the
		// same-occurrence test. Removed 2026-08-03 with the fast path it fed: it was skipping the
		// POSITION test too, and a query retyped between the search and the replace then rewrote
		// occurrences the user had never seen.)
		PMString	accentFlag;	// the one word on this row drawn in the theme accent colour, or empty.
								// Kept OUT of locator so the cell can paint it separately; built by
								// BuildHitLocator alongside it. Only "missing" earns it - the other
								// flags stay in locator and read in the normal colour.
		// --- Track Changes (2026-09-26) ---
		// The WHOLE text of the match before the replace and the whole text the replace wrote, taken
		// as it was written (not capped for drawing like matchText). A replaced row's change is found by
		// its time (recordTime); these are what that change must still read as - its insertion as
		// replacedText (KBSTrackChange::FindRowChangeForHit), its run's deletions as the originalTexts
		// (KBSReplaceEngine RejectRowsNow). Empty until the row is replaced.
		PMString	originalText;
		PMString	replacedText;
		// The first characters of the match's STORY (2026-09-27, the story level): what a story row of
		// the tree reads, taken when the hit is built - the search closes a chapter it opened as soon
		// as it has walked it, so the story cannot be read again when the tree draws.
		PMString	storyLead;
		// The match sits inside a footnote (2026-09-26). Track Changes records nothing there, so such
		// a row cannot be taken back (GetHitInFootnote).
		bool		inFootnote;
		// The time stamp of the tracked changes the replace made for this row (2026-09-26): its change
		// is looked for among that run's records only. 0 = not replaced (or nothing recorded).
		uint64		recordTime;
		int32		pageOrdinal;// this hit's place among the matches on its page, or 0 for "do not
								// show one". Kept as a number rather than only baked into the
								// locator string, so the locator can be rebuilt at any time.
		// The RUN this row belongs to - an index into its chapter's runs - on a list rebuilt from the
		// Track Changes records (2026-09-29, Show Changes by KohakuFindChange); -1 on every other list,
		// which has no run level. Set by whoever builds the hits; the groups follow it (AppendChapter).
		int32		run;

		// checked starts FALSE here and stays so for a search's rows (unticked since 2026-09-27; ticked
		// from 2026-09-26, unticked from 2026-08-02 - each the user's call).
		Hit() : pageIndex(-1), isOverset(false), isLocked(false), isHidden(false),
				fontGroup(-1), fontGroupPos(-1), storyUID(kInvalidUID),
				textStart(kInvalidTextIndex), textEnd(kInvalidTextIndex), matchHash(0),
				checked(false), replaced(false), outcome(kOutcomeNone), inFootnote(false),
				recordTime(0), pageOrdinal(0), run(-1) {}
	};

	/** One STORY of a chapter's hits - one story row in the tree (2026-09-27). The struct keeps the name
	    it had when this level held the fonts of Find Missing Glyphs (removed 2026-09-27).

	    hitIndices index the chapter's own hits vector, ASCENDING, which is what lets the display cap
	    be applied to a group with a lower_bound rather than a scan. */
	struct FontGroup
	{
		PMString			fontName;	// the story row's text ("P3  first words...")
		std::vector<int32>	hitIndices;	// this group's hits, in the chapter's own order
		// ***** A STORY GROUP (2026-09-27, the user's call). ***** A Find/Change result groups its hits
		// by story, the way KCM's Story mode lists stories. A story row carries Replace / Reject Change / Redo / Check All / Uncheck All for its
		// rows (KBSReplaceEngine::ReplaceStory and the rest).
		bool				isStory;
		UID					story;
		// The run the group sits under (Hit::run of its hits, 2026-09-29); -1 = no run level. A story
		// that two runs changed stands once under each: the groups are keyed by (run, story).
		int32				run;
		FontGroup() : isStory(false), story(kInvalidUID), run(-1) {}
	};

	/** ***** ONE RUN OF A LIST REBUILT FROM THE RECORDS (2026-09-29, Show Changes by KohakuFindChange). *****
	    Every record KBS signs carries its run's start in its time (KBSTrackChange.h, the head): the rows
	    whose times share t0 (the time with its low four decimal digits dropped) were written by one
	    replace. A run row of the tree stands between the document row and the story rows.

	    groups index the chapter's fontGroups, ASCENDING - rebuilt with the groups (AppendChapter,
	    KeepCheckedRows). The builder hands the hits over sorted by run, so the runs' groups and hits
	    stand in run order and the display cap cuts the LAST runs first. */
	struct RunGroup
	{
		uint64				t0;		// the run's start - the time of its rows, low four digits dropped
		PMString			label;	// the run row's text: the run's start as a local date and time
		std::vector<int32>	groups;	// the run's story groups
		RunGroup() : t0(0) {}
	};

	/** One chapter that holds at least one hit. */
	struct Chapter
	{
		PMString				name;	// the chapter's display name (its file name)
		UIDRef					docRef;	// current binding (Task 3 jump / reopen)
		IDFile					file;	// the chapter's .indd (Task 3 reopen of a closed chapter)
		std::vector<Hit>		hits;
		std::vector<FontGroup>	fontGroups;	// empty = this chapter has NO font level (Find/Change)
		// The runs (2026-09-29, Show Changes by KohakuFindChange): empty on every list but one rebuilt from
		// the records, where each hit's run indexes this, newest run first.
		std::vector<RunGroup>	runs;
		// Each story's version (ITextModel::GetChangeCount) where KBS last knew the rows in it to stand
		// (2026-09-29, the defect re-check F-2) - see GetStoryVersion.
		std::map<UID, uint32>	storyVersions;

		// A `notReached` flag lived here from 2026-08-03 to 2026-08-05, marking a chapter a cancelled
		// replace never got to so its row could say "cancelled". Only the chapter-at-a-time path could
		// produce one, and it went with "save after replace": a cancel now aborts the single sequence
		// the whole run is wrapped in, so either every chapter was replaced or none was.
	};

	enum SearchScopeKind
	{
		kScopeDocument = 0,
		kScopeBook,
		kScopeAllDocuments,
		kScopeStory,
		kScopeToEndOfStory,
		kScopeSelection
	};

	/** Everything a hit row needs to lay itself out and paint itself. @see GetHitRow. */
	struct RowDisplay
	{
		PMString		locator;	// "P1(2) overset hidden locked" - drawn at the full text colour
		PMString		accentFlag;	// "missing" / "refused", or empty - drawn in the accent colour
		PMString		preText;	// the line, split around the match
		PMString		matchText;
		PMString		postText;
		bool			checked;
		bool			replaced;
		bool			locked;
		ChangeOutcome	outcome;
		bool			hasCheckBox;	// does THIS row carry a check box? RowHasCheckBox's own answer,
										// so the panel does not have to re-derive it from the four
										// fields above - see GetHitRow.
		// (An inFootnote stood here from 2026-09-26 - a footnote's box drawn ticked and greyed, while
		// the replace was Change All. Nothing read it after the one-at-a-time replace of 2026-09-27;
		// removed 2026-09-28. A footnote's row is told apart by GetHitInFootnote now.)

		RowDisplay() : checked(false), replaced(false), locked(false), outcome(kOutcomeNone),
					   hasCheckBox(false) {}
	};

	enum
	{
		kContextMenuBookRow		= -1,	// the BOOK row: the commands reach every chapter
		kNoContextMenuChapter	= -2	// nothing has been right-clicked: they do nothing at all
	};

	/** One row, copied. */
	struct RowCopy
	{
		int32	chapter;
		int32	hit;
		Hit		row;
		RowCopy() : chapter(-1), hit(-1) {}
	};

	/** One story's recorded version (GetStoryVersion), copied - `had` false = none was recorded. */
	struct VersionCopy
	{
		int32	chapter;
		UID		story;
		bool	had;
		uint32	version;
		VersionCopy() : chapter(-1), story(kInvalidUID), had(false), version(0) {}
	};

	/** What one write did to the rows: every row and recorded version it changed, before and after.
	    ***** THE INDICES ARE ONLY GOOD IN THE LAYOUT THEY WERE TAKEN IN ***** (GetLayoutGeneration): a
	    Change Checked turns the list into its report and numbers the rows again. */
	struct RowStep
	{
		std::vector<RowCopy>		before;
		std::vector<RowCopy>		after;
		std::vector<VersionCopy>	versionsBefore;
		std::vector<VersionCopy>	versionsAfter;
	};

	/** The whole result set - for the one write that reshapes it, Change Checked (KeepCheckedRows). */
	struct ModelSnapshot
	{
		std::vector<Chapter>	chapters;
		bool					showingOutcome;
		uint32					layout;
		ModelSnapshot() : showingOutcome(false), layout(0) {}
	};
}

namespace KBSSearchEngine
{
	/** How much of each match CollectStoryHits fills in. A walk costs the same whatever is asked for;
	    what differs is how much is then read about every match it lands on. (A kHitPlace - the story
	    and range alone, for numbering the rows again - went with that numbering on 2026-09-29.) */
	enum HitDetail
	{
		kHitPlaceAndText,	// story, range, the three drawn segments and the hash - what finding a
							// row again compares
		kHitEverything		// ...and the page, the hidden / locked / footnote flags and the story's first
							// words - a search's row
	};
}

/** Where the overset "+" locator for a text position is. 'found' is false when nothing in the
    thread (or any enclosing thread) is placed, so there is no on-page location to point at. */
struct KBSOversetLoc
{
	bool		found;		// true if an outport location was resolved
	UID			frameUID;	// the frame carrying the "+" (for naming its page)
	PBPMPoint	outportPb;	// the "+" point in pasteboard coordinates (for scrolling)

	KBSOversetLoc() : found(false), frameUID(kInvalidUID), outportPb(0.0, 0.0) {}
};

/** What one notification from the model half to the UI half carries (2026-10-01, the model/UI split;
    sent by KBSModelNotify.h, received by KBSModelObserver.cpp). The model never calls the panel: it
    says what happened on the session's subject, and the panel - when there is one listening - does the
    drawing. Handed over as ISubject::Change's changedBy and read during delivery only, so `text` may
    point at the sender's own string. */
struct KBSNotifyPayload
{
	enum Kind
	{
		kRebuild = 0,		// the result set changed shape: build the tree again (KBSResultTree::Rebuild)
		kRefreshRows,		// only what the rows draw changed (KBSResultTree::RefreshRows)
		kChapterRowGoes,	// chapterIdx's row is about to go (KBSResultTree::BeforeChapterRowGoes)
		kStatus				// say *text on the message line (KBSResultTree::ShowStatus)
	};
	Kind				kind;
	int32				chapterIdx;
	const PMString*		text;

	explicit KBSNotifyPayload(Kind k) : kind(k), chapterIdx(-1), text(nil) {}
};

#endif // __KBSModelTypes_h__
