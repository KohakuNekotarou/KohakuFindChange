//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The result model: the last search's hits, grouped by chapter, that the result tree
//  (KBSResultListAdapter / KBSResultListWidgetMgr) displays. A tiny session-global store - the
//  KBS analog of KESCL's KESCLBatchCheck, minus its filters / reverse mode / per-value rows,
//  because the KBS tree is shallow: book -> document -> story -> hit for a book search, and
//  document -> story -> hit for a document search, which has no book row at all. (A list rebuilt
//  from the Track Changes records has a run level between a document and its stories -
//  KBSResultNodeID.h draws every shape a node can take.)
//
//  Each hit already carries its display text pre-split into three PMString segments (the text
//  before the match, the matched text, and the text after) so the colour cell just paints three
//  runs and no UTF-16 boundary maths happens at draw time (the split is done once, in
//  KBSSearchEngine, against the paragraph's wide string at the finder's exact match offsets).
//  The jump anchors (story UID + text range) are what the jump, the hit marker and the replace find
//  the match by.
//
//========================================================================================

#ifndef __KBSResultModel_h__
#define __KBSResultModel_h__

#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"
#include "KBSModelTypes.h"	// the types shared with the UI half (RowDisplay, ChangeOutcome, ...)

#include <map>
#include <set>			// GetChapterStories
#include <vector>

namespace KBSResultModel
{
	/** The whole-RUN safety ceiling: a search stops collecting after this many hit rows across every
	    chapter, so no query or document can pile up an unbounded result set. Unlike the display cap
	    (kKBSDisplayHitLimit, KBSModelTypes.h) this bounds the RESULT SET itself; the search says so in
	    its summary rather than coming back quietly short. Counted in ROWS, the same unit the display cap uses.
	    (It lived in KBSSearchEngine.cpp until 2026-08-03, when the two scans were given it too; they
	    were removed on 2026-09-27.) */
	const int32 kKBSCollectHitLimit = 10000;

	/** One match on one line of one chapter. The three text segments are the line split around
	    the match; the jump anchors point back at the exact occurrence. */
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
		TextIndex	textEnd;	// the match's end position (where the hit marker's rectangle ends)

		// The WHOLE match, as one number - what the same-occurrence test compares against.
		// matchText is capped for drawing (the line budget, kKBSMaxLineChars in
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
								// BuildHitLocator alongside it. Only "missing", "refused" and "not replaced" earn
								// it - the other flags stay in locator and read in the normal colour.
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
		// by story, the way KCM's Story mode lists stories, and every group is one (an isStory flag said
		// so until 2026-10-01). A story row carries Replace / Reject Change / Accept Change / Replace Again
		// (Redo in the code) / Check All / Uncheck All for its rows (KBSReplaceEngine::ReplaceStory and the rest).
		UID					story;
		// The run the group sits under (Hit::run of its hits, 2026-09-29); -1 = no run level. A story
		// that two runs changed stands once under each: the groups are keyed by (run, story).
		int32				run;
		FontGroup() : story(kInvalidUID), run(-1) {}
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
		UIDRef					docRef;	// current binding (RebindChapterDoc after a reopen)
		IDFile					file;	// the chapter's .indd (to reopen a closed chapter)
		std::vector<Hit>		hits;
		std::vector<FontGroup>	fontGroups;	// its story groups - every hit is in one since 2026-09-27
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


	/** Append one chapter to the model - the ONE way results get in. The search clears the model and
	    then appends each chapter as it finishes. Only chapters with >=1 hit should be appended (empty
	    branches are never shown).

	    This is also where the chapter's STORY GROUPS are built, from the hits' own stories - so a
	    caller fills in nothing but the hits, and no result can reach the tree ungrouped.
	    (A SetResults that swapped the whole vector in at once sat beside this until 2026-07-30, by
	    which time nothing called it: two entry points for filling the same model, one of them also
	    resetting the report flag, was a difference waiting to be tripped over.)

	    ***** THE CHAPTER IS TAKEN, NOT COPIED. ***** Pass it with std::move: the model takes the
	    hits over and the caller's Chapter is left empty. Every caller builds one, hands it over and
	    drops it, and a chapter of a large search holds thousands of Hits, each carrying its texts as
	    PMStrings - which this copied until 2026-08-08.

	    Every hit's fontGroup / fontGroupPos is written here, whatever it held before. (An "!! newly
	    built hits only" stood here until 2026-09-29: an ungrouped chapter left the pair as it came,
	    and every chapter is grouped since 2026-09-27.) */
	void AppendChapter(Chapter&& chapter);

	/** Forget the results (an empty search, or a teardown that still wants the tree emptied). */
	void Clear();

	/** Did these results come from a BOOK search (rather than what Find/Change's Search: names)? Recorded on the
	    results themselves rather than read from the Book Scope toggle, so flipping the toggle after
	    a search does not change how the existing results are displayed.

	    The tree uses it to decide the initial state of the chapter rows: a book's chapters come up
	    COLLAPSED (a book-wide search can fill the panel with one chapter's hits, hiding that other
	    chapters matched at all), a single document's one chapter comes up expanded. Cleared by
	    Clear(), so it must be set AFTER the scope is resolved. */
	void SetFromBook(bool fromBook);
	bool IsFromBook();

	/** ***** WHICH Search: THESE RESULTS WERE SEARCHED WITH (2026-09-29). ***** KBS follows Edit > Find/Change's
	    Search: since that day (KBSSearchEngine::CurrentSearchScope). The list records which one it came from,
	    the way it records IsFromBook, so a Search: changed afterwards does not change how the rows already on
	    screen are shown: All Documents draws its document rows CLOSED and says which document has no window;
	    Story / To End of Story / Selection search one document and are drawn as Document is. kScopeBook =
	    Book Scope was on (Search: is then Document - any other is refused before the search).
	    Recorded beside SetFromBook, after the commit point; Clear() puts it back to kScopeDocument. */
	// (SearchScopeKind: KBSModelTypes.h.)
	void SetSearchScope(SearchScopeKind scope);
	SearchScopeKind GetSearchScope();

	/** ***** A DOCUMENT OF AN ALL DOCUMENTS LIST WAS CLOSED (2026-09-29, the user's call: only its rows go). *****
	    Its chapter is emptied - rows, groups, runs, story versions - and unbound (no docRef, no file), but
	    it KEEPS ITS PLACE: KBSUndoFollow names rows by (chapter, row), and closing up the gap would renumber
	    every chapter after it and cut those off from their Undo. A chapter with no rows is not shown
	    (GetShownChapter) and holds nothing to count or write, so nothing else has to know it is there;
	    the next KeepCheckedRows drops it with the other empty ones, under a layout of its own. */
	void CloseChapter(int32 chapterIdx);

	/** CloseChapter's work on a chapter that is not in the model - one of a kept whole result set
	    (KBSUndoFollow::ForgetDocument, 2026-09-29): its rows, groups, runs and versions gone, no docRef, no file. */
	void EmptyChapter(Chapter& chapter);

	/** The nth chapter the tree SHOWS - the chapters with rows, under the display cap - as a chapter index;
	    -1 = none. With no emptied chapter the nth shown is chapter nth, as it always was (2026-09-29). */
	int32 GetShownChapter(int32 nth);

	/** The reverse: chapter 'chapterIdx''s place among the shown chapters; -1 = not shown. */
	int32 GetShownChapterPos(int32 chapterIdx);

	/** ***** WERE THESE ROWS REBUILT FROM THE TRACK CHANGES RECORDS (2026-09-29, Show Changes by
	    KohakuFindChange)? ***** Such a list was searched by nothing: no query, no walk signature, no search
	    mode - there is nothing to line a replace up with, so it offers NO replace of any kind (the user's
	    call B: to replace again, search again). While it is on, no row carries a check box
	    (RowHasCheckBox, NoRowHasCheckBox) and the replace's doors refuse (KBSReplaceEngine). Reject Change
	    and Accept Change work on it, and a row they took back says so ("rejected" - there is no box to say
	    it). Set beside SetFromBook, after the commit point; cleared by Clear(). */
	void SetFromRecords(bool fromRecords);
	bool IsFromRecords();

	/** Has a command been RUN since the results were last discarded?

	    NOT the same question as "are there any hits": a search that found nothing has still been
	    run, and the panel is reporting its answer. And not the same as "is the status line empty"
	    either - the close responders put a message on that line while discarding everything.

	    What it is for is the panel's ILLUSTRATION, which changes once the user has asked for
	    something (KBSPanelIcon). Cleared by Clear(), so a document or book closing - which throws
	    the results away - puts the panel back to the picture it started with. Set AFTER Clear(),
	    like SetFromBook.

	    It is its own flag rather than a second meaning hung on SetFromBook, for the reason stated
	    over IsShowingReplaceOutcome: two statements behind one flag cannot be changed independently
	    afterwards. */
	void NoteRun();
	bool HasRun();

	// (SetStoppedShort / IsStoppedShort - did the search stop short of its scope, at the whole-run ceiling
	//  or with a chapter whose walk broke off - stood here from 2026-09-27 to 2026-09-28. The replace
	//  asked it while it was InDesign's Change All over whole stories, which would have written the
	//  matches past where the search stopped (defect sweep D-5, cfdf50a). The replace became one match at
	//  a time the same day (486e2ef) and writes the ticked rows only - the user's call: results that
	//  stopped at the limit can be replaced - so the question lost its one reader and went.)

	// (ResultKind - Find/Change hits, or a scan's report - and IsReportOnlyKind stood here until the two
	//  scans were removed on 2026-09-27: every result set is a Find/Change one.)

	/** Does NO row of this result set carry a check box?

	    A property of the WHOLE list: a replace's REPORT is what is left after every row lost its box at
	    once (IsShowingReplaceOutcome) - unless a row of it has been taken back with Reject Change. (A
	    scan's report was the second way until the two scans were removed, 2026-09-27.)

	    ***** ASKED BY THE BRANCH ROWS TOO, AND THAT IS WHY IT IS HERE. ***** The book row and the
	    document rows read out "(N/M checked)", which is a sentence only a work list can mean. On a
	    list where nothing has a box the checked count is 0 by definition, so those rows read
	    "(0/55 checked)" over a scan that has nothing to check - which is what they did from
	    2026-08-05 until this question was given one home (KBSResultListWidgetMgr::ApplyBookRow /
	    ApplyChapterRow). ApplyHitRow, which narrows the check-box column on the same condition,
	    asks it here as well: three rows of one tree cannot be allowed to disagree about whether the
	    list they are in offers work. */
	bool NoRowHasCheckBox();

	/** The Find/Change TAB these results were searched with (an IFindChangeOptions::SearchMode value;
	    -1 = nothing searched yet). Held as a plain int so this header needs no text includes.
	    Recorded beside SetFromBook, and cleared by Clear().

	    Why it has to be remembered: the replace pass RE-WALKS each chapter, and a walk runs in the
	    mode that is current AT THAT MOMENT. Switching tabs between a search and Change Checked
	    therefore re-walks with a different query and meets a different set of matches from the one
	    the rows list.

	    ***** AND FROM 2026-08-05 THAT WOULD HAVE BEEN WRITTEN. ***** This note used to end "Nothing
	    wrong is written - the same-occurrence test refuses each one - but the whole run comes back
	    'missing'". That test left the replacing walk on 2026-08-05; it came back before the run
	    (ChapterMovedUnderRows, 2026-08-10) and at every door (KBSSearchEngine::RowReadsAsFound,
	    2026-09-29) - KBSReplaceEngine.h says where. So comparing this against the current mode is not
	    about explaining a run that failed harmlessly: with KBSSearchEngine::BuildWalkSignature it is
	    what refuses a replace under a changed query before anything is written. */
	void SetSearchMode(int32 mode);
	int32 GetSearchMode();

	// (SetQueryText / NoteRunSummary / GetRunSummary / SetChangeText / GetChangeText - the lines the
	//  saved report's heading read - went with Save Results... on 2026-09-27.)

	/** EVERYTHING a walk is driven by, as one opaque comparable string: the query itself plus every
	    Find/Change switch that decides which matches come back (see
	    KBSSearchEngine::BuildWalkSignature for the list).

	    A KEY, compared for equality and never shown. (A readable caption of the query stood beside it
	    for the saved report until Save Results... went, 2026-09-27.)

	    Why the replace needs it. Change Checked RE-WALKS each chapter and writes the matches it meets
	    at the rows' places. That only holds while the walk meets the matches the rows list, which
	    needs the query AND its options to be what they were when the search ran - and the walker is
	    handed the LIVE IFindChangeOptions (ITextWalker.h:58-61), so whatever the dialog holds at
	    replace time is what it walks by.

	    Comparing the TAB alone (SetSearchMode) is not enough: retyping the find string, or turning
	    Include Footnotes off, changes the match set without changing the tab.

	    Empty until the first search of a session. Cleared by Clear(), so it can never outlive the
	    results it describes. */
	void SetWalkSignature(const PMString& signature);
	PMString GetWalkSignature();

	/** The name of the book these results came from - shown on the tree's BOOK row, which is how
	    the panel says which book was searched. Set beside SetFromBook; the file NAME only, because
	    a full path does not fit a palette. Empty for a document-scope search, and cleared by
	    Clear() so a stale name can never outlive the results it belongs to. */
	void SetBookName(const PMString& name);
	PMString GetBookName();

	/** Application-shutdown cleanup: release the vectors' storage, no UI. */
	void ShutdownCleanup();

	/** The number of chapters that hold a hit (uncapped; every chapter with >=1 hit). */
	int32 GetChapterCount();

	/** The number of hits under chapter 'chapterIdx' (uncapped, the full stored count). */
	int32 GetHitCount(int32 chapterIdx);

	/** The total number of hits across ALL chapters (uncapped) - for the status summary. */
	int32 GetTotalHitCount();

	/** The number of chapters that have at least one DISPLAYED hit (the tree root's child count
	    under the display cap). Chapters past the cap are not shown, and neither is one CloseChapter
	    emptied (2026-09-29) - so the nth of them is GetShownChapter(nth), not chapter nth. */
	int32 GetDisplayChapterCount();

	/** The number of hits DISPLAYED under chapter 'chapterIdx' - capped in book order so the whole
	    tree shows at most kKBSDisplayHitLimit hit rows. The chapter still STORES every hit. */
	int32 GetDisplayHitCount(int32 chapterIdx);

	/** A chapter node's display: its name and its hit count. false = index out of range. */
	bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount);

	/** How many STORY rows this chapter shows - the groups that still have a displayed hit under the
	    panel's cap. ("Font" in the names below is the level's old name - it held the fonts of Find
	    Missing Glyphs until 2026-09-27.)

	    The groups that lose everything to the cap are the LAST ones (they are in first-appearance
	    order, and the cap keeps a prefix of the chapter's hits), so the displayed groups are the
	    first N - which is why GetNthChild can ask for one by position. */
	int32 GetDisplayFontCount(int32 chapterIdx);

	/** How many hits are DISPLAYED under one font group - its share of the chapter's own display
	    cap. 0 for an index out of range, and for a group the cap cut off entirely. */
	int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx);

	/** A story row's display: its text ("P3  first words...") and its FULL hit count - uncapped, like
	    every other number the tree reads out: what a row holds, not what the panel drew of it.
	    false = index out of range. */
	bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount);

	/** The 'nth' hit of one font group, as an index into the CHAPTER's hits - the translation the
	    tree needs, since a node names its hit by the chapter-wide index throughout. -1 = out of
	    range. */
	int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth);

	/** Which story group a hit belongs to, and where it sits inside that group: the tree's "who is my
	    parent" and "which child am I". -1 for an index out of range (every hit has a group since
	    2026-09-27). */
	int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx);
	int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx);

	/** ***** THE RUN LEVEL (2026-09-29, Show Changes by KohakuFindChange). ***** Only a list rebuilt from the
	    records has one (Chapter::runs); every question below answers 0 / -1 / false / nothing for any other.

	    GetDisplayRunCount: the runs that still show a hit under the panel's cap - the first N, since the
	    hits stand in run order and the cap keeps a prefix of them (GetDisplayFontCount's reasoning).
	    GetRunDisplay: the run row's label and its FULL hit count. GetDisplayRunGroupCount / GetRunGroup:
	    the story rows under a run row, the nth as a chapter-wide group index. GetGroupRun /
	    GetGroupPosInRun: a story row's parent and its place under it (-1 = no run). GetRunHits: every hit
	    of the run, chapter-wide indexes in the chapter's order. */
	int32 GetDisplayRunCount(int32 chapterIdx);
	bool GetRunDisplay(int32 chapterIdx, int32 runIdx, PMString& outLabel, int32& outHitCount);
	int32 GetDisplayRunGroupCount(int32 chapterIdx, int32 runIdx);
	int32 GetRunGroup(int32 chapterIdx, int32 runIdx, int32 nth);
	int32 GetGroupRun(int32 chapterIdx, int32 groupIdx);
	int32 GetGroupPosInRun(int32 chapterIdx, int32 groupIdx);
	void GetRunHits(int32 chapterIdx, int32 runIdx, std::vector<int32>& outHits);
	/** The run row a right-click menu was popped over (SetContextMenuGroup's twin); cleared by Clear().
	    False = none, or no longer in range. */
	void SetContextMenuRun(int32 chapterIdx, int32 runIdx);
	bool GetContextMenuRun(int32& outChapterIdx, int32& outRunIdx);

	/** Is this one of the chapter's story groups? Every group is one since 2026-09-27, so this is the
	    index's range (the tree asks it of a node before using it). */
	bool IsStoryGroup(int32 chapterIdx, int32 groupIdx);
	/** Every hit of the group, chapter-wide indexes in the chapter's order (empty when out of range). */
	void GetGroupHits(int32 chapterIdx, int32 groupIdx, std::vector<int32>& outHits);
	/** The group's rows that are checked and still waiting to be replaced (GetChapterCheckedCount's rule). */
	int32 GetGroupCheckedCount(int32 chapterIdx, int32 groupIdx);
	/** Check All / Uncheck All on a story row: every row of the group that carries a box. */
	void SetGroupChecked(int32 chapterIdx, int32 groupIdx, bool checked);
	/** How many of those rows there are - the story row's Check All / Uncheck All are offered while it is above
	    zero, as GetChapterCheckableCount offers them on a document row (2026-10-04, D-1: the whole result set's
	    NoRowHasCheckBox until then, so a story whose rows had all been replaced still offered them, changed
	    nothing and said "all checked"). */
	int32 GetGroupCheckableCount(int32 chapterIdx, int32 groupIdx);
	/** The story row a right-click menu was popped over (2026-09-27); cleared by the other rows'
	    right-clicks and by Clear(). False = none. */
	void SetContextMenuGroup(int32 chapterIdx, int32 groupIdx);
	bool GetContextMenuGroup(int32& outChapterIdx, int32& outGroupIdx);

	// (RowDisplay - everything a hit row needs to draw itself, see GetHitRow: KBSModelTypes.h.)

	/** One row's worth of everything, in a single call.
	    Its own getter because a row used to ask the model four separate times to draw itself - the
	    display strings, the flags, the outcome and the accent word - each one walking to the same
	    hit to hand back one part of it. Only the rows on screen are ever laid out, so this was
	    never expensive; it is simply four questions where the row has one.

	    ***** hasCheckBox IS THE MODEL'S ANSWER, NOT A HINT. ***** It is RowHasCheckBox - the same
	    function SetHitChecked, SetAllChecked and SetChapterChecked take their orders from - so the
	    box the panel draws and the box the model will accept a tick from are one decision. The
	    drawing side spelled the rule out itself (replaced || locked || outcome || the whole list has
	    none) until 2026-08-11: it agreed to the letter, but a sixth reason to withhold a box would
	    have had to be remembered in two files, and the one that forgot would have drawn a box whose
	    click SetHitChecked then refuses in silence.
	    @return false for an index out of range, leaving out untouched. */
	bool GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out);

	// (DescribeAllRows - the whole result set as one block, for app.kfcResults - went with that
	//  property on 2026-09-27. The regression suite reads the panel through KIDMCP.)

	// (BuildReportText - the text file Save Results... wrote - was removed on 2026-09-27.)

	/** A hit node's display: the page locator and the three line segments to paint. false = index
	    out of range. @see GetHitRow when the flags are wanted as well. */
	bool GetHitDisplay(int32 chapterIdx, int32 hitIdx,
		PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost);

	/** A hit's jump anchors: the chapter's document / file and the match's story + text range.
	    false = index out of range. */
	bool GetHitLocation(int32 chapterIdx, int32 hitIdx,
		UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd);

	/** Rebind a chapter's document reference: after a closed chapter is reopened - by a jump, a
	    replace or Reject Change - later calls must use the live database, not the dead one from
	    search time. */
	void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef);

	/** A story's VERSION where KBS last knew the chapter's rows in it to stand (2026-09-29, the defect
	    re-check F-2): ITextModel::GetChangeCount (KBSSearchEngine::ReadStoryVersion), taken by the search
	    for every story holding a hit, and taken again by each change KBS makes there - only while the
	    story was still at the version recorded, so a story that moved without KBS is never written off as
	    "known" (KBSReplaceEngine). False = nothing recorded (or the index is out of range), which the
	    replace reads as "cannot vouch for this story" and does not write to. */
	bool GetStoryVersion(int32 chapterIdx, UID story, uint32& outVersion);
	void SetStoryVersion(int32 chapterIdx, UID story, uint32 version);

	/** The stories holding a row of the chapter (2026-10-01: the replace and KBSUndoFollow each walked
	    the rows for this themselves). Empty for an index out of range. */
	void GetChapterStories(int32 chapterIdx, std::set<UID>& outStories);

	/** Select / deselect one hit for replacement. Ignored for anything the panel draws no check box
	    on - a hit already replaced (the text it matched is gone), a locked one (InDesign offers no
	    way to change locked content), one that already says why it was left alone, and every row of
	    a replace's report but the ones taken back. It asks that question the same way the panel does,
	    so the model can never hold a checked hit that no row offered; it is a backstop rather than
	    the first line of defence, since those rows carry no box to click in the first place.
	    (Every row touching it went on and off with it from 2026-09-26 to 2026-09-27, and this returned
	    how many rows that was - nothing read the number after the boxes became the row's own again.) */
	void SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked);

	/** The rows touching `hitIdx` in its chapter - same story, ranges meeting or overlapping, followed
	    both ways - in TEXT order, `hitIdx` included. Reads the ranges as they stand (the search's
	    before a replace, the replaced text's after one). */
	void GetTouchingGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outHits);
	/** Is the row inside a footnote (Hit::inFootnote)? False for an out-of-range index. */
	bool GetHitInFootnote(int32 chapterIdx, int32 hitIdx);
	/** The time stamp of the row's tracked changes (Hit::recordTime); 0 for none or out of range. */
	uint64 GetHitRecordTime(int32 chapterIdx, int32 hitIdx);
	void SetHitRecordTime(int32 chapterIdx, int32 hitIdx, uint64 time);
	// (GetHitPinned - why a row can never be taken back, an enum with one reason, a footnote - stood here
	//  from 2026-09-26 to 2026-09-29, beside GetHitInFootnote asking the same thing.)

	/** A hit's row-cell flags: selected, already replaced, and locked. The last two both mean "this
	    row gets no check box", for different reasons. false = index out of range.
	    (This also answers "is it checked?", which an IsHitChecked of its own used to do until
	    2026-07-30 with no callers left - every asker wants the other two flags in the same breath.) */
	bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked);

	/** The two flags that say the match IS in the document but is out of the user's reach there:
	    LOCKED (a locked layer or a locked story - InDesign can search locked content but offers no
	    way to change it) and HIDDEN (a switched-off layer - the text is composed and can be jumped
	    to, but draws nothing). false = index out of range.

	    Separate from GetHitFlags, which answers "what does this row's check box do?". These two
	    answer "can the user work on this match where it is?" - the question the double-click asks
	    before it selects (KBSJump::SelectHitText). isLocked appears in both because it is a fact
	    that bears on both questions; the DECISIONS made from it stay one per place. */
	bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden);

	/** Select / deselect EVERY hit in every chapter - Check All / Uncheck All over the tree's BOOK
	    row. Applies to all stored hits, including those past the panel's display cap - the display cap
	    must not silently shrink what a replace touches. Replaced and locked hits are skipped. */
	void SetAllChecked(bool checked);

	/** Select / deselect every hit in ONE chapter - the same two commands over a DOCUMENT row
	    (2026-08-01, when they moved off the panel flyout onto the rows' right-click menu). Identical
	    rules to SetAllChecked, applied to one chapter: every stored hit including those past the
	    display cap, and the rows that carry no check box are left alone. An index out of range, and a
	    panel showing a replace's report, are both no-ops. */
	void SetChapterChecked(int32 chapterIdx, bool checked);

	/** How many hits are selected across all chapters (uncapped) - for the status read-out and for
	    the replace command's enablement. */
	int32 GetCheckedCount();

	/** The same count for ONE chapter - what its row in the tree reads out as "(N/M checked)".
	    Uncapped like the whole-model count: a chapter's hits past the panel's display cap are
	    still its hits, and Check All still ticks them. Out of range = 0. */
	int32 GetChapterCheckedCount(int32 chapterIdx);

	// (GetCheckedChapterCount - how many CHAPTERS hold at least one checked, unreplaced hit - stood
	//  here. Its one caller was the confirmation prompt's closing line, which named the number;
	//  that line has stated the case instead of counting it since 2026-08-07
	//  (KBSReplaceConfirmDialog::BuildUnsavedLine - itself gone since 2026-09-26), and nothing has
	//  read the count since. It was kept for three days as "a real question about the model, cheap to
	//  answer" and removed on 2026-08-10 for the reason DropChapter was (see where it stood, at the end
	//  of this namespace): a function nobody runs is a guess about what a future caller will want, and
	//  this file had already decided that once. git has it.)

	/** How many hits COULD be checked at all: every hit that is neither replaced nor locked, i.e.
	    every row that actually has a check box (uncapped). Zero means no row has one - the panel is
	    showing a replace's aftermath, or every match landed in locked content - and Check All /
	    Uncheck All have nothing to act on, so they are greyed out. */
	int32 GetCheckableCount();

	/** The same count for ONE chapter, which is the range Check All / Uncheck All cover when the
	    right-click menu was popped over a document row. Zero greys them out there for the same reason
	    the whole-model count does over the book row: every row in that document has lost its box. */
	int32 GetChapterCheckableCount(int32 chapterIdx);

	/** Which row the result tree's right-click menu was popped over. KBSResultNodeEH stashes it
	    immediately before HandlePopupMenu and the book and document rows' commands (Check All /
	    Uncheck All, and since 2026-09-27 Replace and the rest) read it back - an
	    action component is handed no widget context of its own. (The pattern is KESCL's
	    KESCLBatchCheck::SetContextMenuNode, which its "Copy as Text" row menu uses the same way.)

	    Clear() puts it back to kNoContextMenuChapter, so an index taken from one result set can never
	    name a chapter of the next one; the readers range-check it as well, because a caller that never
	    went through the menu - a script firing the action - reaches them with whatever is stored.

	    !Clear() is not the only pass that makes a stored index mean something else: KeepCheckedRows
	     drops the chapters a replace left empty, which renumbers the ones after them, and it does NOT
	     reset this. What keeps that safe is that the index is written again the moment a row is
	     right-clicked (KBSResultNodeEH, just before the menu pops), so no menu acts on an index from
	     before the renumbering; the range check stands behind it for a caller arriving by ActionID.
	     (Until the 2026-09-27 defect sweep this said the row menu does not open at all after a
	     replace. Since 2026-09-26 it does - Reject Change and the commands that came after it.)
	     Anything that ever drops chapters WITHOUT a right-click in between has to reset this the way
	     Clear() does. */
	// (kContextMenuBookRow / kNoContextMenuChapter: KBSModelTypes.h.)
	void SetContextMenuChapter(int32 chapterIdx);
	int32 GetContextMenuChapter();

	/** The hit row the right-click menu was popped over (2026-09-26: Replace, Reject Change and Accept
	    Change act on it). Cleared with the result set; false when no hit row is stashed or it is out of range. */
	void SetContextMenuHit(int32 chapterIdx, int32 hitIdx);
	bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx);

	/** A row's outcome (kOutcomeNone for an out-of-range index). */
	ChangeOutcome GetHitOutcome(int32 chapterIdx, int32 hitIdx);

	/** The texts a replaced row's tracked change is found by (see Hit::originalText). The replace
	    reads them from the document - the model reads no text - and hands them over here. */
	void SetHitChangeTexts(int32 chapterIdx, int32 hitIdx, const PMString& originalText,
		const PMString& replacedText);
	/** False when the row was neither replaced nor taken back (a row taken back keeps the texts of
	    the replace it undid), or the index is out of range. */
	bool GetHitChangeTexts(int32 chapterIdx, int32 hitIdx, PMString& outOriginalText,
		PMString& outReplacedText);

	/** What REPLACED rows written side by side read before the replace (2026-09-29 - the panel's
	    "Source Text:", KBSResultTree::ShowRowsBefore): the first row's leading words, the text they took
	    joined in text order, the last row's trailing words (a list rebuilt from the records keeps a
	    group's one deletion on its last row, so a row alone could say nothing). WHICH rows is the caller's
	    question - KBSTrackChange::CurrentReplacedGroup, which asks the records rather than the stored
	    ranges. One row is a group of one.
	    The text is as stored: raw breaks (the caller marks them up), the context cut the way the row's is.
	    An accepted row and a footnote's row ("no track") are replaced rows too.
	    @param rows the rows, in text order.
	    @return false - and three empty strings - for no rows, a row that holds no replace (not replaced,
	        taken back) or an index out of range. */
	bool GetRowsBefore(int32 chapterIdx, const std::vector<int32>& rows, PMString& outPre,
		PMString& outOriginal, PMString& outPost);

	/** Every row of hitIdx's STORY, in the list's order, hitIdx included (2026-09-29). The list's order
	    is the search's walk order sorted by page, so rows of one story stand in the order of their text
	    within a thread - which, unlike the stored ranges, no edit can change. */
	void GetStoryRowsInOrder(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows);

	/** Reject Change took this row back: it shows its original text at [start, end) again, is no
	    longer replaced, and says "rejected". */
	void SetHitRejected(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end);
	/** Accept Change by KohakuFindChange accepted this row's tracked change (2026-09-29): the replace is final.
	    It stays replaced, says "accepted", and keeps no record time - there is nothing left to find, so
	    neither Reject Change nor Accept Change is offered on it again. */
	void SetHitAccepted(int32 chapterIdx, int32 hitIdx);
	// (SetHitRedone went with Redo on 2026-09-27: a row taken back is replaced again like any other -
	//  MarkHitReplaced clears its "taken back".)
	/** The row's text went with an object another ticked row deleted: replaced, no range, "deleted". */
	void SetHitDeleted(int32 chapterIdx, int32 hitIdx);

	// (GetHitWalkOrder / SetHitWalkOrder stood here until 2026-09-29 - see where Hit::walkOrder stood.)

	/** A chapter's document binding and file. The replace pass works chapter at a time, so it
	    needs this without going through a hit. false = index out of range. */
	bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile);

	/** What the same-occurrence test asks of a row: the story it was found in, where the match
	    started and ENDED, and the whole of its text as one number.

	    !! The DRAWN text (matchText) is deliberately NOT handed out here any more. It is capped
	    for the row (500 characters at the time), so comparing it judged a long GREP match on its
	    first 500 and let a rewrite past that point through as "the same occurrence"
	    (found 2026-08-04). The hash covers the match whole - see HashMatchText (KBSSearchEngine.cpp).

	    Its own getter because it runs once per checked hit, and the two getters it replaces carry
	    freight it does not want: GetHitLocation copies a UIDRef and an IDFile, GetHitDisplay copies
	    four PMStrings to hand back one. false = index out of range. */
	bool GetHitMatchIdentity(int32 chapterIdx, int32 hitIdx, UID& outStoryUID, TextIndex& outStart,
		TextIndex& outEnd, uint64& outHash);

	// (GetHitAnchor and GetHitStoryStamp stood here. Both served the replace's trusted-story fast
	// path, which skipped the same-occurrence test for a story nobody had edited - and skipped its
	// POSITION half with it, so a query retyped between the search and the replace rewrote
	// occurrences the user had never seen. The fast path was removed on 2026-08-03 rather than
	// repaired; the test now runs for every row, so neither getter has a caller. See the note over
	// MatchStillStandsHere in KBSReplaceEngine.cpp.)

	/** Turn the result set into a REPORT of what the replace did. Keeps every row the replace was
	    asked about - the ones it changed, and the ones it left alone with the reason on the
	    locator - plus the locked rows, which account for a search that turned up more than the
	    replace was allowed to touch, and every row that already says something about itself, ticked
	    or not: an outcome set before this run (a row Reject Change took back, one a jump found
	    missing) stays on screen, and a row taken back is work again (KBSReplaceEngine.cpp carries
	    these rows' places through the run). Drops the other rows the user had left unticked, and
	    then the chapters left with nothing.

	    Does NOTHING when no row was asked about, so a replace that was asked for nothing never
	    wipes the result set. Sets the aftermath flag (see IsShowingReplaceOutcome), which takes
	    every check box off the panel.
	    @return the number of rows left in the model. */
	int32 KeepCheckedRows();

	/** ***** A ROW TAKEN BACK IS WORK AGAIN (2026-09-27, the user's call A and B). ***** A row Reject Change
	    put back to its original text carries a box again - in a work list and in a replace's report
	    alike - so it can be ticked and replaced again (Change Checked, or its menu's Replace). (Redo went
	    on 2026-09-27, the user's call: Replace is the one way.) IsWorkOutcome = nothing said about the row
	    but "taken back". AnyRejectedRowOpen = does a report hold one (then the report offers work). */
	bool IsWorkOutcome(ChangeOutcome outcome);
	bool AnyRejectedRowOpen();

	/** ***** IS THIS ROW CHANGE CHECKED'S WORK? (2026-09-28: one rule, asked everywhere) ***** Ticked, not
	    replaced, and nothing said about it but "taken back" (IsWorkOutcome). What the checked counts count -
	    the run is sized with them - and what the replace writes and its verify walk checks: the rule was
	    spelled out in five places until 2026-09-28, and the bar's size rests on their agreeing. Out of
	    range = false.
	    ***** AND ITS BOX ON SCREEN (2026-09-29, the defect re-check F-4): ticked AND RowHasCheckBox. *****
	    A replace's report keeps the rows of a chapter that could not be opened still ticked, with no box
	    to see or clear; once a row of that report was taken back with Reject Change the report offered
	    work again, and Change Checked (or a story's Replace) wrote those unseen rows as well. */
	bool IsHitCheckedWork(int32 chapterIdx, int32 hitIdx);

	/** Record a completed replacement: the row keeps its page locator but takes the STORY AND RANGE
	    the replace command reported writing, is marked replaced, and leaves the selection. A replaced
	    hit can never be checked again - the text it matched is gone, so a second replace pass would
	    have nothing to line it up with.

	    ***** THE STORY IS TAKEN FROM THE COMMAND, NOT LEFT AS THE ROW HAD IT. ***** It used to be
	    left alone, on the reasoning that the walk could only ever land a checked hit on the story
	    that hit was found in - which the same-occurrence test inside the walk guaranteed, by refusing
	    to write anything that had moved. That test left the walk on 2026-08-05 (KBSReplaceEngine.h),
	    and with it the guarantee: an edit that removed a whole frame between the search and the
	    replace made the Nth match a match in a LATER story. The row then held one story with the other
	    story's range, so the line read back at the end of the chapter, and the hash taken from it, both
	    came from text that has nothing to do with this row (SetHitSegments, SetHitRange). (Since then
	    the run refuses a row that has moved before it starts - ChapterMovedUnderRows - and the walk
	    finds a row by its place - RowOfMatchAnyOrder; what the command reports writing is still the
	    one answer that cannot be wrong.)

	    The three displayed segments are deliberately NOT set here: several matches can share one
	    paragraph, and a line read at the moment ITS match was written still shows the later matches
	    in that paragraph unreplaced. The replace pass fills them in once the chapter is finished -
	    see SetHitSegments and SetHitRange. Until then the row still shows what the search found. (A
	    cancel puts each row back whole either way - BeginRowBackup.)

	    ***** THE RANGE GIVEN HERE IS WHERE THE TEXT WAS WRITTEN, NOT WHERE IT ENDS UP. ***** A later
	    replacement in the same story can move it - the walk does not go in TextIndex order (see
	    SetHitRange) - so the replace pass keeps its own copy of the range, carries it forward, and
	    hands the final one over through SetHitRange before it reads the line. */
	void MarkHitReplaced(int32 chapterIdx, int32 hitIdx, UID newStoryUID,
		TextIndex newStart, TextIndex newEnd);

	/** Move a row to where its text stands NOW: the story and range the replace pass last saw it at,
	    carried forward past every later replacement in the same story. Nothing else on the row is
	    touched; SetHitSegments follows it with the line read from that range.

	    ***** WHY A REPLACE HAS TO MOVE ROWS IT DID NOT WRITE TO. ***** The report a replace leaves
	    (KeepCheckedRows) keeps the rows it changed AND the ones it left alone - locked, refused - and
	    every one of them is jumped to by its stored range. A replacement that changes the length of
	    the text earlier in the same story moves all of those, whether the row was written to or not;
	    a row left at the range the search found it at was then jumped to off by that much, failed the
	    same-occurrence test, and was stamped "missing" with "the text is no longer where the search
	    left it" - about text that was exactly where it had been (measured 2026-09-25: a story
	    threaded into a locked frame, one replacement in the unlocked frame before it).

	    ***** AND "EARLIER" IS NOT "VISITED EARLIER". ***** The walk visits a table's cells where the
	    table stands in the text - while the cells' own characters live AFTER the whole body in
	    TextIndex terms (ITableTextContent.h:41-44) - and it walks backwards when the Find/Change
	    dialog is set to search backwards. Both measured 2026-09-25; in both, a replacement made
	    LATER in the walk moved rows written EARLIER, which is what the range carried forward covers.

	    Backed up like every other change a replace makes, so a cancel puts the row back too. */
	void SetHitRange(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end);

	/** Give a row what it now stands for: the three text segments it DISPLAYS, and the hash the
	    same-occurrence test COMPARES. The other half of MarkHitReplaced: the replace pass calls it
	    after the chapter's last replacement, when the paragraphs have stopped moving - for every
	    replaced row, and since 2026-09-25 for the rows the report keeps without writing to them
	    (locked, refused), each straight after SetHitRange has moved it. Every other field is left
	    alone.

	    ***** THE HASH GOES IN THE SAME CALL, AND IT HAS TO. ***** The two describe one fact - what
	    this row points at now - and a row carrying one of them from before the replacement and the
	    other from after it is a row that cannot be jumped to: MatchIsSameOccurrence reads the hash,
	    finds the text it was taken from is gone, and answers "the replacement is no longer here".

	    That is exactly what happened between 2026-08-04 and 2026-08-05. The test used to compare
	    matchText, which this function has always updated, so the pair could not come apart; when
	    the hash took over as the thing compared (see HashMatchText, KBSSearchEngine.cpp), the update
	    did not follow it here, and every replaced row lost its jump. Splitting display from
	    comparison was right - the drawn text is capped and cannot vouch for a long match - but
	    they are still written at the same moment, from the same range.

	    @param newMatchHash the hash of the whole match (HashMatchText's) over the range the replace
	           command reported writing - the SAME range the three segments were read from. */
	void SetHitSegments(int32 chapterIdx, int32 hitIdx, const PMString& newPre,
		const PMString& newMatch, const PMString& newPost, uint64 newMatchHash);

	/** Build hit.locator from the hit's own fields. THE one definition - the search's page-ordering
	    pass and the post-replace thinning both call it, so the two can no longer drift apart.

	        P<page>(<n>) overset hidden locked     -> hit.locator
	        missing | refused | not replaced       -> hit.accentFlag, drawn after it in accent colour

	    The page ordinal comes from hit.pageOrdinal (0 = leave it out). The flags are separated by
	    spaces and spelled out IN FULL rather than clipped, because each one explains a row the user
	    cannot act on. "+" is deliberately NOT the separator: InDesign's own overset marker is a "+",
	    so "P5+locked" reads as "page 5, overset".

	    The flags STACK - "P4(1) locked missing" is a locked row that has since been jumped to and
	    found changed. Only the words that come from the row's outcome exclude each other, being values
	    of one field: missing, refused, not replaced, and on the locator rejected, deleted, accepted.

	    The locator also says " deleted" (gone with the object another ticked row deleted), and since
	    2026-09-29 " rejected" (a row taken back, on a list rebuilt from the records only - it has no box
	    to say it), " accepted" (its change accepted) and " no track" (replaced inside a footnote, where
	    Track Changes records nothing - the user's request). */
	void BuildHitLocator(Hit& hit);

	/** Number one chapter's hits within their pages and rebuild each locator (BuildHitLocator). The
	    hits must already stand in page order: a run of equal pageIndex is one page, the ordinal is
	    the place in that run, and a page holding ONE row shows none, since there is nothing to tell
	    apart. THE one definition - the search's page ordering (KBSSearchEngine::FinalizeHits) and a
	    replace's report (KeepCheckedRows) both call it. */
	void NumberHitsWithinPages(std::vector<Hit>& hits);

	/** Turn the two break characters into the marks InDesign itself draws with Show Hidden
	    Characters on - a pilcrow for a paragraph end (CR), a return arrow for a forced line break
	    (LF) - in place. A string holding none of the characters below is left exactly as it came.

	    THE one definition, called where the document's text is shown: the panel's cell
	    (KBSColorTextView - where it draws, and where it measures), a story row's first words
	    (BuildFontGroups) and the "Source Text:" line (KBSResultListWidgetMgr). Since 2026-08-04 a
	    match is carried WHOLE however many paragraphs it spans, and a raw break draws with no width -
	    the paragraphs either side of it run together and read as one piece of text - so it has to be
	    marked. (The saved report, BuildReportText, called it too until Save Results... went on
	    2026-09-27.)

	    ***** DISPLAY ONLY. ***** Never applied to a row's own texts. Those are what every door
	    compares against the document (KBSSearchEngine::RowReadsAsFound - the hash taken over the
	    stored range, and the line around it), and a marked-up copy would fail every comparison - a
	    click on a row would answer "the text is no longer here" about text that had not moved at all.
	    Callers mark a COPY, at the moment they draw it. (The one marked string the model keeps is a
	    story row's label, FontGroup::fontName, which nothing compares.)

	    It also DROPS the characters an object stands on - footnote / endnote references, anchors,
	    a table's per-row continuations, page number markers (2026-09-26, the user's call: they drew as
	    a box). ***** A TABLE'S ANCHOR BECOMES U+25A6 since 2026-09-29 ***** - KCM's table sign, the
	    user's request: `a<table>b` reads `a<sign>b` on a hit row, a story row and the "Source Text:"
	    alike. */
	void MarkUpBreaksForDisplay(PMString& s);

	/** Record why a hit was not replaced. Rebuilds the row's locator so the word shows up at once,
	    and clears the selection - a row that says why it cannot be changed must not stay checked.
	    Called by the replace pass, and by the jump when it finds the text at a row's position is no
	    longer the text the row describes. Ignored for a hit that WAS replaced. */
	void SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome);

	/** Is the panel showing the AFTERMATH of a replace rather than a search's results? Set by
	    KeepCheckedRows, cleared by Clear. While it is on, no row offers a check box:
	    the list is a report, not a work list. It is asked as well as the per-row flags because the
	    aftermath can hold rows with no flag at all - a chapter that could not be opened. Those
	    were never looked at, so nothing can be said about them. */
	bool IsShowingReplaceOutcome();

	/** Start remembering every row a replace changes, so a run the user stops can be put back.
	    Only the rows actually written to are copied - one copy each, taken just before the change -
	    so the cost follows the work done rather than the size of the result set.

	    A replace that is cancelled or fails rolls the TEXT back through its command sequence
	    (Change Checked aborts its abortable sequence; a row menu's Replace ends its plain one with the
	    error state raised). That leaves the panel describing replacements that no longer exist, so
	    the two have to be put back together: this is the panel's half.

	    Exactly one of RollBackRows (the run was cancelled) or ForgetRowBackup (it committed) must
	    follow, or the copies stay alive until the next replace.
	    ***** SINCE 2026-09-29 EVERY WRITE OF KBS'S STARTS IT THROUGH KBSUndoFollow::StepRecorder ***** -
	    Reject Change and Accept Change too - and the one that commits hands the copies over with
	    TakeRowBackup (the "before" of an Undo) instead of forgetting them. The story versions a write
	    records (SetStoryVersion) are copied the same way. */
	void BeginRowBackup();

	/** Put every remembered row back the way it was and stop remembering. A row is remembered once, as
	    the run found it, however many times the run changes it (since 2026-09-28). */
	void RollBackRows();

	/** Stop remembering and release the copies: the replace committed, so the rows keep what they
	    were given. */
	void ForgetRowBackup();

	// ***** THE PANEL FOLLOWS AN UNDO AND A REDO (2026-09-29, the user: "after an Undo the row cannot be
	// ***** rejected again - the panel should come back with it, the way KCM's does"). ***** A write of KBS's
	// own is one undo step, and what it did to the rows is kept beside it (KBSUndoFollow): the rows as they
	// were BEFORE it - the copies BeginRowBackup takes anyway - and as they are AFTER it, and the story
	// versions it recorded (SetStoryVersion) the same way. An Undo puts the "before" copies back, a Redo the
	// "after" ones. (Until then the rows stayed as the write left them: the 2026-09-26 decision "A", which
	// this reverses - a row taken back and undone kept saying "taken back", so its Reject Change was greyed.)

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

	/** Stop remembering, as ForgetRowBackup does, and hand the copies over: every row and recorded
	    version changed since BeginRowBackup, as it was when it was first changed and as it stands now. */
	void TakeRowBackup(RowStep& out);

	/** Put a step's rows and recorded versions back - its "after" copies (after = true) or its "before"
	    ones. A row whose index is out of range is passed over. */
	void ApplyRowStep(const RowStep& step, bool after);

	/** The whole result set - for the one write that reshapes it, Change Checked (KeepCheckedRows). */
	struct ModelSnapshot
	{
		std::vector<Chapter>	chapters;
		bool					showingOutcome;
		uint32					layout;
		ModelSnapshot() : showingOutcome(false), layout(0) {}
	};
	void TakeModelSnapshot(ModelSnapshot& out);
	/** Puts it back whole, its layout generation with it; the right-click targets are forgotten (they
	    index the chapters that went). */
	void RestoreModelSnapshot(const ModelSnapshot& snapshot);

	/** Which result set the rows are: a new number with every Clear (a search, Show Changes, a close) -
	    a write kept for an Undo belongs to one, and means nothing to the next. */
	uint32 GetResultSetId();

	/** Which layout of the result set the row indices name: a new number with every Clear and every
	    KeepCheckedRows that reshaped the list; RestoreModelSnapshot brings its own back. */
	uint32 GetLayoutGeneration();

	// (DropChapter(int32) stood here - erase one chapter from the results and leave the others - as
	// groundwork for chapter-level invalidation: closing one chapter of a book would drop that
	// chapter rather than the whole result set. It was never called: the feature it was for had not
	// been written, and a function nobody has run is a guess about what that feature will need,
	// not a head start on it. Removed 2026-08-07. The feature came on 2026-09-29 as CloseChapter,
	// and the guess would have been wrong: it does not erase the chapter but empties it in place,
	// because erasing would renumber the chapters after it and cut their rows off from their Undo.)
}

#endif // __KBSResultModel_h__
