//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The result model: the last search's hits, grouped by chapter, that the result tree
//  (KFCResultListAdapter / KFCResultListWidgetMgr) displays. A tiny session-global store - the
//  KFC analog of KESCL's KESCLBatchCheck, minus its filters / reverse mode / per-value rows,
//  because the KFC tree is shallow: book -> document -> story -> hit for a book search, and
//  document -> story -> hit for a document search, which has no book row at all
//  (KFCResultNodeID.h draws every shape a node can take).
//
//  Each hit already carries its display text pre-split into three PMString segments (the text
//  before the match, the matched text, and the text after) so the colour cell just paints three
//  runs and no UTF-16 boundary maths happens at draw time (the split is done once, in
//  KFCSearchEngine, against the paragraph's wide string at the finder's exact match offsets).
//  The jump anchors (story UID + text range) are what the jump, the hit marker and the replace find
//  the match by.
//
//========================================================================================

#ifndef __KFCResultModel_h__
#define __KFCResultModel_h__

#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"
#include "KFCModelTypes.h"	// the types shared with the UI half (RowDisplay, ChangeOutcome, ...)

#include <map>
#include <set>			// GetChapterStories
#include <vector>

namespace KFCResultModel
{
	/** THE WHOLE-RUN CEILING - the same number the panel draws (kKFCDisplayHitLimit, KFCModelTypes.h - F9 of
	    docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md): a search stops collecting there and
	    says so ("Stopped at the 300 limit - narrow the search, or use ..."), so no query can pile up rows nobody
	    will replace one by one. Counted in ROWS. */
	const int32 kKFCCollectHitLimit = kKFCDisplayHitLimit;

	/** One match on one line of one chapter. The three text segments are the line split around
	    the match; the jump anchors point back at the exact occurrence. */
	struct Hit
	{
		PMString	locator;	// the page locator "P<page>(<n>)" / "overset" (drawn at full text
								// colour, ahead of the line, with one gap between the two - there is
								// no tab stop: the locator's width varies too much for a fixed
								// column, see KFCColorTextView)
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
								// gets " locked" and the row's Replace is greyed. InDesign can search
								// locked content but offers no way to change it ("Search Only"), so
								// the row is listed and jumpable but never replaced.
		bool		isHidden;	// match sits on a switched-off layer -> the locator gets " hidden".
								// Only reachable when the Find/Change dialog's "Include Hidden
								// Layers" is on, and then the text is composed and jumpable but
								// draws nothing, so the row has to say why the page looks empty.

		int32		fontGroup;	// which of its chapter's fontGroups (its STORY group) this hit belongs
		int32		fontGroupPos;	// to, and where it sits inside that group. -1 before AppendChapter
								// groups it (every hit has a group). Filled by AppendChapter; the
								// tree reads them to answer "who is my parent" and "which child am
								// I" without searching.

		UID			storyUID;	// the story the match lives in (within its chapter's database)
		TextIndex	textStart;	// the match's start position in that story
		TextIndex	textEnd;	// the match's end position (where the hit marker's rectangle ends)

		// The WHOLE match, as one number - what the same-occurrence test compares against.
		// matchText is capped for drawing (the line budget, kKFCMaxLineChars in
		// KFCSearchEngine.cpp), so it cannot answer "is this still the same text" for a long
		// GREP match; this can. 0 = never computed, or the text
		// could not be read, and it never compares equal (see MatchIsSameOccurrence). A zero-width
		// match (GREP ^ / $ / lookarounds) also stores 0, and is accepted on its length arm
		// instead - an empty range has no text for the hash to vouch for.
		uint64		matchHash;

		// --- replace support ---
		// (No walk order is kept - the order the walker handed a match back in, which would have to be
		// numbered again with a walk of the whole chapter. A row is found by its PLACE: the verify walk
		// asks for a match at the row's start (ChapterMovedUnderRows), the writing walk for one at its
		// thread, offset and length (RowOfMatchAnyOrder).)
		bool		replaced;	// already replaced in this result set - its Replace is greyed
		ChangeOutcome outcome;	// why this row was NOT replaced (kOutcomeNone = it was, or was never
								// reached at all). The locator shows it as a word.
		// (No per-hit change count that lets the replace take an unedited story on trust and skip the
		// same-occurrence test: that fast path skips the POSITION test too, and a query retyped between
		// the search and the replace then rewrites occurrences the user has never seen. The chapter's
		// storyVersions are a door BESIDE that test, not instead of it.)
		PMString	accentFlag;	// the one word on this row drawn in the theme accent colour, or empty.
								// Kept OUT of locator so the cell can paint it separately; built by
								// BuildHitLocator alongside it. Only "missing" and "refused" earn
								// it - the other flags stay in locator and read in the normal colour.
		// --- what the replace wrote ---
		// The WHOLE text a replace wrote at the row's place, taken as it was written (not capped for drawing like
		// matchText): a replaced row that an edit has moved is looked for again by it
		// (KFCSearchEngine::RelocateStaleRow). Empty until the row is replaced.
		PMString	replacedText;
		// The first characters of the match's STORY: what a story row of
		// the tree reads, taken when the hit is built - the search closes a chapter it opened as soon
		// as it has walked it, so the story cannot be read again when the tree draws.
		PMString	storyLead;
		int32		pageOrdinal;// this hit's place among the matches on its page, or 0 for "do not
								// show one". Kept as a number rather than only baked into the
								// locator string, so the locator can be rebuilt at any time.

		Hit() : pageIndex(-1), isOverset(false), isLocked(false), isHidden(false),
				fontGroup(-1), fontGroupPos(-1), storyUID(kInvalidUID),
				textStart(kInvalidTextIndex), textEnd(kInvalidTextIndex), matchHash(0),
				replaced(false), outcome(kOutcomeNone), pageOrdinal(0) {}
	};

	/** One STORY of a chapter's hits - one story row in the tree. The struct keeps the name it had when
	    this level held the fonts of Find Missing Glyphs (since removed).

	    hitIndices index the chapter's own hits vector, ASCENDING, which is what lets the display cap
	    be applied to a group with a lower_bound rather than a scan. */
	struct FontGroup
	{
		PMString			fontName;	// the story row's text ("P3  first words...")
		std::vector<int32>	hitIndices;	// this group's hits, in the chapter's own order
		// A STORY GROUP (the author's call). A Find/Change result groups its hits by story, the way KCM's
		// Story mode lists stories, and every group is one.
		UID					story;
		FontGroup() : story(kInvalidUID) {}
	};

	/** One chapter that holds at least one hit. */
	struct Chapter
	{
		PMString				name;	// the chapter's display name (its file name)
		UIDRef					docRef;	// current binding (RebindChapterDoc after a reopen)
		IDFile					file;	// the chapter's .indd (to reopen a closed chapter)
		std::vector<Hit>		hits;
		std::vector<FontGroup>	fontGroups;	// its story groups - every hit is in one
		// Each story's version (ITextModel::GetChangeCount) where KFC last knew the rows in it to stand
		// - see GetStoryVersion.
		std::map<UID, uint32>	storyVersions;

		// (No "not reached" mark per chapter for a cancelled replace: a cancel aborts the single sequence
		// the whole run is wrapped in, so either every chapter was replaced or none was.)
	};


	/** Append one chapter to the model - the ONE way results get in. The search clears the model and
	    then appends each chapter as it finishes. Only chapters with >=1 hit should be appended (empty
	    branches are never shown).

	    This is also where the chapter's STORY GROUPS are built, from the hits' own stories - so a
	    caller fills in nothing but the hits, and no result can reach the tree ungrouped. (One entry
	    point: a second way of filling the same model is a difference waiting to be tripped over.)

	    THE CHAPTER IS TAKEN, NOT COPIED. Pass it with std::move: the model takes the hits over and the
	    caller's Chapter is left empty. Every caller builds one, hands it over and drops it, and a
	    chapter of a large search holds thousands of Hits, each carrying its texts as PMStrings.

	    Every hit's fontGroup / fontGroupPos is written here, whatever it held before. */
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

	/** WHICH Search: THESE RESULTS WERE SEARCHED WITH. KFC follows Edit > Find/Change's Search:
	    (KFCSearchEngine::CurrentSearchScope). The list records which one it came from,
	    the way it records IsFromBook, so a Search: changed afterwards does not change how the rows already on
	    screen are shown: All Documents draws its document rows CLOSED and says which document has no window;
	    Story / To End of Story / Selection search one document and are drawn as Document is. kScopeBook =
	    Book Scope was on (Search: is then Document - any other is refused before the search).
	    Recorded beside SetFromBook, after the commit point; Clear() puts it back to kScopeDocument. */
	// (SearchScopeKind: KFCModelTypes.h.)
	void SetSearchScope(SearchScopeKind scope);
	SearchScopeKind GetSearchScope();

	/** A DOCUMENT OF AN ALL DOCUMENTS LIST WAS CLOSED (the author's call: only its rows go).
	    Its chapter is emptied - rows, groups, story versions - and unbound (no docRef, no file), but
	    it KEEPS ITS PLACE: KFCUndoFollow names rows by (chapter, row), and closing up the gap would renumber
	    every chapter after it and cut those off from their Undo. A chapter with no rows is not shown
	    (GetShownChapter) and holds nothing to count or write, so nothing else has to know it is there. */
	void CloseChapter(int32 chapterIdx);

	/** CloseChapter's work on a chapter that is not in the model - one of a kept whole result set
	    (KFCUndoFollow::ForgetDocument): its rows, groups and versions gone, no docRef, no file. */
	void EmptyChapter(Chapter& chapter);

	/** The nth chapter the tree SHOWS - the chapters with rows, under the display cap - as a chapter index;
	    -1 = none. With no emptied chapter the nth shown is chapter nth. */
	int32 GetShownChapter(int32 nth);

	/** The reverse: chapter 'chapterIdx''s place among the shown chapters; -1 = not shown. */
	int32 GetShownChapterPos(int32 chapterIdx);

	/** Has a command been RUN since the results were last discarded?

	    NOT the same question as "are there any hits": a search that found nothing has still been
	    run, and the panel is reporting its answer. And not the same as "is the status line empty"
	    either - the close responders put a message on that line while discarding everything.

	    What it is for is the panel's ILLUSTRATION, which changes once the user has asked for
	    something (KFCPanelIcon). Cleared by Clear(), so a document or book closing - which throws
	    the results away - puts the panel back to the picture it started with. Set AFTER Clear(),
	    like SetFromBook.

	    It is its own flag rather than a second meaning hung on SetFromBook - as NoteChangeAllWrote is: two
	    statements behind one flag cannot be changed independently afterwards. */
	void NoteRun();
	bool HasRun();

	/** DID THE LAST COMMAND WRITE WITH InDesign's CHANGE ALL - Change All in Book (No List), or a query run - AND CHANGE
	    SOMETHING (the author's call)? The panel's pencil cat says so (KFCPanelIcon). Set at the end of those runs - false
	    for one that wrote nothing, was cancelled or failed; a row's Replace does not touch it. Clear() puts it down, so
	    the next search, Clear Results or a closing document takes the picture back; a query run's Undo puts back the one
	    the list before it had (ModelSnapshot). */
	void NoteChangeAllWrote(bool wrote);
	bool HasChangeAllWritten();

	// (No "the search stopped short" flag for the replace to ask: a row's Replace writes that row alone, so results
	//  that stopped at the limit can be replaced - the author's call.)

	/** The Find/Change TAB these results were searched with (an IFindChangeOptions::SearchMode value;
	    -1 = nothing searched yet). Held as a plain int so this header needs no text includes.
	    Recorded beside SetFromBook, and cleared by Clear().

	    Why it has to be remembered: a row's Replace RE-WALKS the row's story, and a walk runs in the
	    mode that is current AT THAT MOMENT. Switching tabs between a search and the Replace
	    therefore re-walks with a different query and meets a different set of matches from the one
	    the rows list.

	    AND THAT WOULD BE WRITTEN. The replacing walk does not test each match against its row; the
	    same-occurrence test stands before the write (ChapterMovedUnderRows) and at every door
	    (KFCSearchEngine::RowReadsAsFound) - KFCReplaceEngine.h says where. So comparing this against the
	    current mode is not about explaining a write that failed harmlessly: with
	    KFCSearchEngine::BuildWalkSignature it is what refuses a replace under a changed query before
	    anything is written. */
	void SetSearchMode(int32 mode);
	int32 GetSearchMode();

	/** EVERYTHING a walk is driven by, as one opaque comparable string: the query itself plus every
	    Find/Change switch that decides which matches come back (see
	    KFCSearchEngine::BuildWalkSignature for the list).

	    A KEY, compared for equality and never shown.

	    Why the replace needs it. A row's Replace RE-WALKS the row's story and writes the match it meets
	    at the row's place. That only holds while the walk meets the matches the rows list, which
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
	    emptied - so the nth of them is GetShownChapter(nth), not chapter nth. */
	int32 GetDisplayChapterCount();

	/** The number of hits DISPLAYED under chapter 'chapterIdx' - capped in book order so the whole
	    tree shows at most kKFCDisplayHitLimit hit rows. The chapter still STORES every hit. */
	int32 GetDisplayHitCount(int32 chapterIdx);

	/** A chapter node's display: its name and its hit count. false = index out of range. */
	bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount);

	/** How many STORY rows this chapter shows - the groups that still have a displayed hit under the
	    panel's cap. ("Font" in the names below is the level's old name - it held the fonts of Find
	    Missing Glyphs, since removed.)

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

	/** HOW MANY OF THEM THIS LIST HAS REPLACED (Hit::replaced) - the R of a branch row's "(R/M)" (the author's call:
	    a document row and a story row say how many of their hits have been replaced, "1/3"). Uncapped like M: every hit
	    the row holds. An Undo of a replace puts the hit back as found, so the count follows it. 0 out of range. */
	int32 GetChapterReplacedCount(int32 chapterIdx);
	int32 GetFontReplacedCount(int32 chapterIdx, int32 fontIdx);
	/** ...and of every chapter - the book row's. */
	int32 GetTotalReplacedCount();

	/** The 'nth' hit of one font group, as an index into the CHAPTER's hits - the translation the
	    tree needs, since a node names its hit by the chapter-wide index throughout. -1 = out of
	    range. */
	int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth);

	/** Which story group a hit belongs to, and where it sits inside that group: the tree's "who is my
	    parent" and "which child am I". -1 for an index out of range (every hit has a group). */
	int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx);
	int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx);

	/** Is this one of the chapter's story groups? Every group is one, so this is the
	    index's range (the tree asks it of a node before using it). */
	bool IsStoryGroup(int32 chapterIdx, int32 groupIdx);

	// (RowDisplay - everything a hit row needs to draw itself, see GetHitRow: KFCModelTypes.h.)

	/** One row's worth of everything, in a single call.
	    Its own getter so that a row does not ask the model four separate times to draw itself - the
	    display strings, the flags, the outcome and the accent word - each one walking to the same
	    hit to hand back one part of it. Only the rows on screen are ever laid out, so that would
	    not be expensive; it is simply four questions where the row has one.
	    @return false for an index out of range, leaving out untouched. */
	bool GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out);

	// (No script property hands the result set out as text: the regression suite reads the panel
	//  through KIDMCP.)

	/** A hit's jump anchors: the chapter's document / file and the match's story + text range.
	    false = index out of range. */
	bool GetHitLocation(int32 chapterIdx, int32 hitIdx,
		UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd);

	/** Rebind a chapter's document reference: after a closed chapter is reopened - by a jump or a
	    replace - later calls must use the live database, not the dead one from search time. */
	void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef);

	/** A story's VERSION where KFC last knew the chapter's rows in it to stand:
	    ITextModel::GetChangeCount (KFCSearchEngine::ReadStoryVersion), taken by the search
	    for every story holding a hit, and taken again by each change KFC makes there - only while the
	    story was still at the version recorded, so a story that moved without KFC is never written off as
	    "known" (KFCReplaceEngine). False = nothing recorded (or the index is out of range), which the
	    replace reads as "cannot vouch for this story" and does not write to. */
	bool GetStoryVersion(int32 chapterIdx, UID story, uint32& outVersion);
	void SetStoryVersion(int32 chapterIdx, UID story, uint32 version);

	/** The stories holding a row of the chapter - one answer for the replace and KFCUndoFollow alike.
	    Empty for an index out of range. */
	void GetChapterStories(int32 chapterIdx, std::set<UID>& outStories);

	/** A hit's row flags: already replaced, and locked - both mean "this row's Replace is greyed", for different
	    reasons. false = index out of range. */
	bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outReplaced, bool& outLocked);

	/** The two flags that say the match IS in the document but is out of the user's reach there:
	    LOCKED (a locked layer or a locked story - InDesign can search locked content but offers no
	    way to change it) and HIDDEN (a switched-off layer - the text is composed and can be jumped
	    to, but draws nothing). false = index out of range.

	    Separate from GetHitFlags, which answers "can this row be replaced?". These two
	    answer "can the user work on this match where it is?" - the question the double-click asks
	    before it selects (KFCJump::SelectHitText). isLocked appears in both because it is a fact
	    that bears on both questions; the DECISIONS made from it stay one per place. */
	bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden);

	/** The hit row the right-click menu was popped over (its Replace acts on it). Cleared with the result set;
	    false when no hit row is stashed or it is out of range. */
	void SetContextMenuHit(int32 chapterIdx, int32 hitIdx);
	bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx);

	/** A row's outcome (kOutcomeNone for an out-of-range index). */
	ChangeOutcome GetHitOutcome(int32 chapterIdx, int32 hitIdx);

	/** The whole text the replace wrote at the row's place (Hit::replacedText). The replace reads it from the
	    document - the model reads no text - and hands it over here. */
	void SetHitWrittenText(int32 chapterIdx, int32 hitIdx, const PMString& writtenText);
	/** False when the row is not replaced, or the index is out of range. */
	bool GetHitWrittenText(int32 chapterIdx, int32 hitIdx, PMString& outWrittenText);

	/** A chapter's document binding and file. The replace pass works chapter at a time, so it
	    needs this without going through a hit. false = index out of range. */
	bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile);

	/** What the same-occurrence test asks of a row: the story it was found in, where the match
	    started and ENDED, and the whole of its text as one number.

	    !! The DRAWN text (matchText) is deliberately NOT handed out here. It is capped for the row
	    (the line budget, kKFCMaxLineChars), so comparing it would judge a long GREP match on its first
	    characters and let a rewrite past that point through as "the same occurrence". The hash covers
	    the match whole - see HashMatchText (KFCSearchEngine.cpp).

	    Its own getter because it runs once per row a door asks about, and the other getters carry freight it
	    does not want: GetHitLocation copies a UIDRef and an IDFile, GetHitRow copies every
	    string the row draws. false = index out of range. */
	bool GetHitMatchIdentity(int32 chapterIdx, int32 hitIdx, UID& outStoryUID, TextIndex& outStart,
		TextIndex& outEnd, uint64& outHash);

	// (No getter feeds a fast path past the same-occurrence test for a story nobody has edited: it
	// skips the POSITION half too, so a query retyped between the search and the replace rewrites
	// occurrences the user has never seen. See Hit's note, and the one on the SAME-OCCURRENCE TEST in
	// KFCReplaceEngine.cpp.)

	/** Is a row with this outcome still work - one its Replace can write? Only a row nothing was said about
	    (kOutcomeNone): a row the replace found missing, locked or refused says
	    why on its locator and is not offered again. */
	bool IsWorkOutcome(ChangeOutcome outcome);

	/** Record a completed replacement: the row keeps its page locator but takes the STORY AND RANGE
	    the replace command reported writing, and is marked replaced. A replaced hit is never offered
	    again - the text it matched is gone, so a second replace would have nothing to line it up with.

	    THE STORY IS TAKEN FROM THE COMMAND, NOT LEFT AS THE ROW HAD IT. The walk does not read a
	    match's text against its row before writing (KFCReplaceEngine.h), and when it lined the Nth match
	    up with the row numbered N, an edit that removed a whole frame between the search and the
	    replace made the Nth match a match in a LATER story (measured). A row holding one story with the
	    other story's range reads back a line at the end of the chapter, and a hash from it, from text
	    that has nothing to do with this row (SetHitSegments, SetHitRange). The run now refuses a row
	    that has moved before it starts (ChapterMovedUnderRows) and the walk finds a row by its place
	    (RowOfMatchAnyOrder); what the command reports writing is still the one answer that cannot be
	    wrong, so it is the one taken.

	    The three displayed segments are deliberately NOT set here: several matches can share one
	    paragraph, and a line read at the moment ITS match was written still shows the later matches
	    in that paragraph unreplaced. The replace pass fills them in once the chapter is finished -
	    see SetHitSegments and SetHitRange. Until then the row still shows what the search found. (A
	    cancel puts each row back whole either way - BeginRowBackup.)

	    THE RANGE GIVEN HERE IS WHERE THE TEXT WAS WRITTEN, NOT WHERE IT ENDS UP. A later
	    replacement in the same story can move it - the walk does not go in TextIndex order (see
	    SetHitRange) - so the replace pass keeps its own copy of the range, carries it forward, and
	    hands the final one over through SetHitRange before it reads the line. */
	void MarkHitReplaced(int32 chapterIdx, int32 hitIdx, UID newStoryUID,
		TextIndex newStart, TextIndex newEnd);

	/** Move a row to where its text stands NOW: the story and range the replace pass last saw it at,
	    carried forward past every later replacement in the same story. Nothing else on the row is
	    touched; SetHitSegments follows it with the line read from that range.

	    WHY A REPLACE HAS TO MOVE ROWS IT DID NOT WRITE TO. The list a replace leaves is still a work list -
	    the rows it changed AND the ones it left alone, locked or refused or not reached yet - and
	    every one of them is jumped to by its stored range. A replacement that changes the length of
	    the text earlier in the same story moves all of those, whether the row was written to or not;
	    a row left at the range the search found it at was then jumped to off by that much, failed the
	    same-occurrence test, and was stamped "missing" with "the text is no longer where the search
	    left it" - about text that was exactly where it had been (measured: a story threaded into a
	    locked frame, one replacement in the unlocked frame before it).

	    AND "EARLIER" IS NOT "VISITED EARLIER". The walk visits a table's cells where the table stands
	    in the text - while the cells' own characters live AFTER the whole body in TextIndex terms
	    (ITableTextContent.h:41-44) - and it walks backwards when the Find/Change dialog is set to
	    search backwards. Both measured; in both, a replacement made
	    LATER in the walk moved rows written EARLIER, which is what the range carried forward covers.

	    Backed up like every other change a replace makes, so a cancel puts the row back too. */
	void SetHitRange(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end);

	/** Give a row what it now stands for: the three text segments it DISPLAYS, and the hash the
	    same-occurrence test COMPARES. The other half of MarkHitReplaced: the replace pass calls it
	    after the chapter's last replacement, when the paragraphs have stopped moving - for every
	    replaced row, and for the rows the list keeps without writing to them, each straight after
	    SetHitRange has moved it. Every other field is left
	    alone.

	    THE HASH GOES IN THE SAME CALL, AND IT HAS TO. The two describe one fact - what this row
	    points at now - and a row carrying one of them from before the replacement and the other from
	    after it is a row that cannot be jumped to: MatchIsSameOccurrence reads the hash, finds the
	    text it was taken from is gone, and answers "the replacement is no longer here".

	    That is exactly what happened when the hash took over from matchText as the thing compared
	    (see HashMatchText, KFCSearchEngine.cpp) and the update did not follow it here: every replaced
	    row lost its jump. Splitting display from comparison is right - the drawn text is capped and
	    cannot vouch for a long match - but they are still written at the same moment, from the same
	    range.

	    @param newMatchHash the hash of the whole match (HashMatchText's) over the range the replace
	           command reported writing - the SAME range the three segments were read from. */
	void SetHitSegments(int32 chapterIdx, int32 hitIdx, const PMString& newPre,
		const PMString& newMatch, const PMString& newPost, uint64 newMatchHash);

	/** Build hit.locator from the hit's own fields. THE one definition - the search's page-ordering
	    pass and the outcome setter (SetHitOutcome) both call it, so they cannot drift apart.

	        P<page>(<n>) overset hidden locked     -> hit.locator
	        missing | refused                      -> hit.accentFlag, drawn after it in accent colour

	    The page ordinal comes from hit.pageOrdinal (0 = leave it out). The flags are separated by
	    spaces and spelled out IN FULL rather than clipped, because each one explains a row the user
	    cannot act on. "+" is deliberately NOT the separator: InDesign's own overset marker is a "+",
	    so "P5+locked" reads as "page 5, overset".

	    The flags STACK - "P4(1) locked missing" is a locked row that has since been jumped to and
	    found changed. Only the words that come from the row's outcome exclude each other, being values
	    of one field: missing and refused. */
	void BuildHitLocator(Hit& hit);

	/** Number one chapter's hits within their pages and rebuild each locator (BuildHitLocator). The
	    hits must already stand in page order: a run of equal pageIndex is one page, the ordinal is
	    the place in that run, and a page holding ONE row shows none, since there is nothing to tell
	    apart. Called by the search's page ordering (FinalizeHits, KFCSearchEngine.cpp), its one caller. */
	void NumberHitsWithinPages(std::vector<Hit>& hits);

	/** Turn the two break characters into the marks InDesign itself draws with Show Hidden
	    Characters on - a pilcrow for a paragraph end (CR), a return arrow for a forced line break
	    (LF) - in place. A string holding none of the characters below is left exactly as it came.

	    THE one definition, called where the document's text is shown: the panel's cell
	    (KFCColorTextView - where it draws, and where it measures), a story row's first words
	    (BuildFontGroups) and the "Preview Text:" line (KFCResultListWidgetMgr). A match is carried
	    WHOLE however many paragraphs it spans, and a raw break draws with no width - the paragraphs
	    either side of it run together and read as one piece of text - so it has to be marked.

	    DISPLAY ONLY. Never applied to a row's own texts. Those are what every door
	    compares against the document (KFCSearchEngine::RowReadsAsFound - the hash taken over the
	    stored range, and the line around it), and a marked-up copy would fail every comparison - a
	    click on a row would answer "the text is no longer here" about text that had not moved at all.
	    Callers mark a COPY, at the moment they draw it. (The one marked string the model keeps is a
	    story row's label, FontGroup::fontName, which nothing compares.)

	    It also DROPS the characters an object stands on - footnote / endnote references, anchors,
	    a table's per-row continuations, page number markers (the author's call: they drew as a box).
	    A TABLE'S ANCHOR BECOMES U+25A6 - KCM's table sign, the user's request: `a<table>b` reads
	    `a<sign>b` on a hit row, a story row and the "Preview Text:" alike. */
	void MarkUpBreaksForDisplay(PMString& s);

	/** Record why a hit was not replaced. Rebuilds the row's locator so the word shows up at once,
	    and the row is not offered again (IsWorkOutcome).
	    Called by the replace pass, and by the jump when it finds the text at a row's position is no
	    longer the text the row describes. Ignored for a hit that WAS replaced. */
	void SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome);

	/** Start remembering every row a replace changes, so a run the user stops can be put back.
	    Only the rows actually written to are copied - one copy each, taken just before the change -
	    so the cost follows the work done rather than the size of the result set.

	    A replace that fails rolls the TEXT back through its command sequence (a row's Replace ends its
	    plain one with the error state raised; a preview aborts its abortable one). That leaves the panel
	    describing replacements that no longer exist, so the two have to be put back together: this is
	    the panel's half.

	    Exactly one of RollBackRows (the write was taken back) or ForgetRowBackup (it committed) must
	    follow, or the copies stay alive until the next replace.
	    EVERY WRITE OF ROWS STARTS IT THROUGH KFCUndoFollow::StepRecorder, and the one that commits hands
	    the copies over with TakeRowBackup (the "before" of an Undo) instead of forgetting them. The story
	    versions a write records (SetStoryVersion) are copied the same way. */
	void BeginRowBackup();

	/** Put every remembered row back the way it was and stop remembering. A row is remembered once, as
	    the run found it, however many times the run changes it. */
	void RollBackRows();

	/** Stop remembering and release the copies: the replace committed, so the rows keep what they
	    were given. */
	void ForgetRowBackup();

	// THE PANEL FOLLOWS AN UNDO AND A REDO (the author: "the panel should come back with it, the way KCM's
	// does"). A write of KFC's own is one undo step, and what it did to the rows is kept beside it
	// (KFCUndoFollow): the rows as they were BEFORE it - the copies BeginRowBackup takes anyway - and as they
	// are AFTER it, and the story versions it recorded (SetStoryVersion) the same way. An Undo puts the
	// "before" copies back, a Redo the "after" ones. (Rows left as the write left them are wrong after an
	// Undo: a row replaced and undone keeps saying "Changed", and its Replace stays greyed.)

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
	    THE INDICES ARE ONLY GOOD IN THE LAYOUT THEY WERE TAKEN IN (GetLayoutGeneration): a query run's
	    Undo or Redo puts another list in place (RestoreModelSnapshot). */
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

	/** The whole result set - for the one write that replaces it, the query run (KFCUndoFollow::RunRecorder). */
	struct ModelSnapshot
	{
		std::vector<Chapter>	chapters;
		uint32					layout;
		// the list's header: a query run's Undo puts back a list of another kind
		bool					fromBook;
		SearchScopeKind			searchScope;
		PMString				bookName;
		int32					searchMode;
		PMString				walkSignature;
		bool					hasRun;
		// ...and the pencil cat's fact (NoteChangeAllWrote): an Undo of a query run puts the list before it back, and the
		// picture with it (measured - case xq-icon-undo: the pencil cat stayed over the list the Undo brought back).
		bool					changeAllWrote;
		ModelSnapshot() : layout(0), fromBook(false), searchScope(kScopeDocument),
			searchMode(-1), hasRun(false), changeAllWrote(false) {}
	};
	void TakeModelSnapshot(ModelSnapshot& out);
	/** Puts it back whole, its layout generation with it; the right-click targets are forgotten (they
	    index the chapters that went). */
	void RestoreModelSnapshot(const ModelSnapshot& snapshot);

	/** Which result set the rows are: a new number with every Clear (a search, a query run, Clear Results, a close) -
	    a write kept for an Undo belongs to one, and means nothing to the next. */
	uint32 GetResultSetId();

	/** A RUN THAT DID NOT HAPPEN LEAVES THE RESULT SET IT FOUND. A query run clears the list at its commit point (a new
	    number); cancelled or refused after it, the list is put back (KFCUndoFollow::RunRecorder::RestoreBefore) - and with
	    this the number too, so the writes kept for that list are followed again (measured - case xq-cancel-undo: a
	    Ctrl+Z of a row's Replace made before a cancelled run went unfollowed, the row still reading "Changed"). Not for
	    an Undo of a run that went through: its own step names the result set it left. */
	void ReturnToResultSet(uint32 resultSetId);

	/** Which layout of the result set the row indices name: a new number with every Clear;
	    RestoreModelSnapshot brings its own back. */
	uint32 GetLayoutGeneration();
}

#endif // __KFCResultModel_h__
