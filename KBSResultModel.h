//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The result model: the last book search's hits, grouped by chapter, that the result tree
//  (KBSResultListAdapter / KBSResultListWidgetMgr) displays. A tiny session-global store - the
//  KBS analog of KESCL's KESCLBatchCheck, minus its filters / reverse mode / per-value rows,
//  because the KBS tree is shallow: book -> document -> hit for a book search, and document -> hit
//  for a document search, which has no book row at all.
//
//  Each hit already carries its display text pre-split into three PMString segments (the text
//  before the match, the matched text, and the text after) so the colour cell just paints three
//  runs and no UTF-16 boundary maths happens at draw time (the split is done once, in
//  KBSSearchEngine, against the paragraph's wide string at the finder's exact match offsets).
//  The jump anchors (story UID + text range) are collected now but only consumed in Task 3.
//
//========================================================================================

#ifndef __KBSResultModel_h__
#define __KBSResultModel_h__

#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

namespace KBSResultModel
{
	/** The panel shows at most this many hit rows (book order). The model still HOLDS every hit -
	    a same-book re-search reuses them, and a future export / replace consumes them ALL - only
	    the tree display is capped, to keep a huge result set from flooding the panel. */
	const int32 kKBSDisplayHitLimit = 5000;

	/** The whole-RUN safety ceiling: a run stops collecting after this many hit rows across every
	    chapter, so no query or document can pile up an unbounded result set. Unlike the display cap
	    above this bounds the RESULT SET itself, so reaching it caps a later export too; the run says
	    so in its summary rather than coming back quietly short.

	    Shared by all three commands since 2026-08-03. It lived in KBSSearchEngine.cpp until then and
	    the two scans had no ceiling at all, on the reasoning that a document's faults should all be
	    named - which leaves the scans able to collect without bound on a document whose faults run
	    into the tens of thousands (a book of overset table cells reaches it). Counted in ROWS, the
	    same unit the display cap uses. NOT in glyphs: the glyph scan merges consecutive boxes into
	    one row ("55 missing glyphs in 6 places"), so counting glyphs would cut a run short while it
	    still had almost no rows to show. */
	const int32 kKBSCollectHitLimit = 10000;

	/** What became of a hit when a replace ran over it. Only ever set on rows the replace actually
	    reached; everything else stays kOutcomeNone. Drawn as a word on the end of the locator. */
	enum ChangeOutcome
	{
		kOutcomeNone = 0,	// replaced, or never reached
		kOutcomeMissing,	// the text could not be found where the search left it (moved or deleted)
		kOutcomeLocked,		// it became locked between the search and the replace
		kOutcomeRefused		// InDesign's own replace command would not run there
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

		PMString	fontName;	// the font that had no glyph for this text. Empty for a Find/Change
								// hit: only a glyph scan fills it, because there the font IS the
								// answer - a box almost always means "this font does not have this
								// character", and the fix is to apply one that does.
								//
								// It is the tree's FONT LEVEL that says it now, not the row: the
								// name was drawn at the end of every row until 2026-08-02, when the
								// hits were grouped under it instead (see FontGroup).

		int32		fontGroup;	// which of its chapter's fontGroups this hit belongs to, and where it
		int32		fontGroupPos;	// sits inside that group. Both -1 when the chapter has no groups
								// at all - a Find/Change result names no font, and its tree stays
								// the three levels it has always had. Filled by AppendChapter; the
								// tree reads them to answer "who is my parent" and "which child am
								// I" without searching.

		UID			storyUID;	// the story the match lives in (within its chapter's database)
		TextIndex	textStart;	// the match's start position in that story
		TextIndex	textEnd;	// the match's end position (Task 3 marker rectangle)

		// The WHOLE match, as one number - what the same-occurrence test compares against.
		// matchText below is capped at 500 characters for drawing, so it cannot answer "is this
		// still the same text" for a long GREP match; this can. 0 = never computed, or the text
		// could not be read, and it never compares equal (see MatchIsSameOccurrence).
		uint64		matchHash;

		// --- replace support ---
		// The order the text walker handed this match back in, WITHIN ITS CHAPTER (0-based). It is
		// stamped before the page-order sort, and it is the ONLY key that survives a replace pass:
		// textStart shifts the moment an earlier match is replaced, and the array order is page
		// order, not walk order. The replace pass re-walks the chapter and counts matches to line
		// them up with these numbers.
		int32		walkOrder;
		bool		checked;	// selected for replacement. A fresh search leaves every row UNticked
								// (see the constructor); ticking is the user's own act, through the
								// row, or Check All from the right-click menu.
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
		int32		pageOrdinal;// this hit's place among the matches on its page, or 0 for "do not
								// show one". Kept as a number rather than only baked into the
								// locator string, so the locator can be rebuilt at any time.

		// checked starts FALSE (changed 2026-08-02, user's request, for every mode - Text, GREP and
		// Glyph alike). A fresh search used to tick every row it was allowed to replace, which made
		// Change Checked a whole-document rewrite one keystroke away. Ticking is now something the
		// user does on purpose - and cheap to do, since the right-click menu gained Check All per
		// document and per book on 2026-08-01, which is what made the old default unnecessary.
		Hit() : pageIndex(-1), isOverset(false), isLocked(false), isHidden(false),
				fontGroup(-1), fontGroupPos(-1), storyUID(kInvalidUID),
				textStart(kInvalidTextIndex), textEnd(kInvalidTextIndex), matchHash(0),
				walkOrder(-1), checked(false), replaced(false), outcome(kOutcomeNone), pageOrdinal(0) {}
	};

	/** One font that had no glyph for some of a chapter's text - one FONT row in the tree.

	    The tree has a font level because a box means "this font has no glyph for this character",
	    so the font IS the unit a fix applies to - the official preflight rule offers "Apply a font
	    that has the glyph" for exactly the same reason.

	    hitIndices index the chapter's own hits vector, ASCENDING, which is what lets the display cap
	    be applied to a group with a lower_bound rather than a scan. */
	struct FontGroup
	{
		PMString			fontName;	// as the user sees it in the font menu (family + style)
		std::vector<int32>	hitIndices;	// this group's hits, in the chapter's own order
	};

	/** One chapter that holds at least one hit. */
	struct Chapter
	{
		PMString				name;	// the chapter's display name (its file name)
		UIDRef					docRef;	// current binding (Task 3 jump / reopen)
		IDFile					file;	// the chapter's .indd (Task 3 reopen of a closed chapter)
		std::vector<Hit>		hits;
		std::vector<FontGroup>	fontGroups;	// empty = this chapter has NO font level (Find/Change)

		// A `notReached` flag lived here from 2026-08-03 to 2026-08-05, marking a chapter a cancelled
		// replace never got to so its row could say "cancelled". Only the chapter-at-a-time path could
		// produce one, and it went with "save after replace": a cancel now aborts the single sequence
		// the whole run is wrapped in, so either every chapter was replaced or none was.
	};

	/** Append one chapter to the model - the ONE way results get in. The search clears the model and
	    then appends each chapter as it finishes. Only chapters with >=1 hit should be appended (empty
	    branches are never shown).

	    This is also where the chapter's FONT GROUPS are built, from the hits' own fontName - so a
	    caller fills in nothing but the hits, and no result can reach the tree ungrouped.
	    (A SetResults that swapped the whole vector in at once sat beside this until 2026-07-30, by
	    which time nothing called it: two entry points for filling the same model, one of them also
	    resetting the report flag, was a difference waiting to be tripped over.) */
	void AppendChapter(const Chapter& chapter);

	/** Forget the results (an empty search, or a teardown that still wants the tree emptied). */
	void Clear();

	/** Did these results come from a BOOK search (rather than the front document)? Recorded on the
	    results themselves rather than read from the Book Scope toggle, so flipping the toggle after
	    a search does not change how the existing results are displayed.

	    The tree uses it to decide the initial state of the chapter rows: a book's chapters come up
	    COLLAPSED (a book-wide search can fill the panel with one chapter's hits, hiding that other
	    chapters matched at all), a single document's one chapter comes up expanded. Cleared by
	    Clear(), so it must be set AFTER the scope is resolved. */
	void SetFromBook(bool fromBook);
	bool IsFromBook();

	/** Has a command been RUN since the results were last discarded?

	    NOT the same question as "are there any hits": a search that found nothing has still been
	    run, and the panel is reporting its answer. And not the same as "is the status line empty"
	    either - the close responders put a message on that line while discarding everything.

	    What it is for is the panel's ILLUSTRATION, which changes once the user has asked for
	    something (KBSPanelIcon). Cleared by Clear(), so a document or book closing - which throws
	    the results away - puts the panel back to the picture it started with. Set AFTER Clear(),
	    like SetFromBook.

	    It is its own flag rather than a second meaning hung on SetFromBook, for the reason stated
	    over SetResultKind below: two statements behind one flag cannot be changed independently
	    afterwards. */
	void NoteRun();
	bool HasRun();

	/** What KIND of results the model is holding.

	    A missing-glyph scan is a REPORT, not a work list: there is nothing about it to replace, so
	    no row offers a check box and Change Checked has nothing to act on.

	    Deliberately NOT folded into IsShowingReplaceOutcome, which states something else entirely -
	    "these rows are the aftermath of a replace". Two different statements behind one flag cannot
	    be changed independently afterwards.

	    Cleared by Clear() back to kResultFindChange, so - like SetFromBook - it has to be set AFTER
	    the model is cleared, not before. */
	enum ResultKind
	{
		kResultFindChange = 0,	// hits from the user's own Find/Change query (the original feature)
		kResultMissingGlyph,	// findings from a notdef scan
		kResultOverset			// findings from an overset scan
	};

	void SetResultKind(ResultKind kind);
	ResultKind GetResultKind();

	/** Is the model holding a REPORT rather than a work list? A scan finds something to look at;
	    it does not offer anything to change. So no row of one carries a check box, and the column
	    those boxes would have stood in is narrowed to half (KBSResultListWidgetMgr).

	    ONE question in ONE place. The model side (RowHasCheckBox) and the drawing side (ApplyHitRow)
	    each used to name kResultMissingGlyph themselves, which meant a second scan kind had to be
	    remembered in two files - and a row whose box the model refuses but the panel still draws is
	    a row that promises an action nothing carries out. */
	bool IsReportOnlyKind();

	/** Is a hit's matchText the text that is REALLY at its position in the story?

	    True for everything the search and the glyph scan produce - both take the row's three
	    segments straight out of the story - and false for an overset finding, where
	    KBSOversetScanEngine writes what it found ("Frame (370)") into matchText instead, because the
	    colour cell keeps the match at full strength and ellipsizes the context around it.

	    Asked by anything that compares a stored row against the document. KBSJump does it on every
	    click, to notice that an edit has moved the text out from under a row; over an overset row
	    that comparison could only ever fail, and it was announcing "Not found" and marking the row
	    'missing' on a jump that had just worked perfectly. */
	bool MatchTextIsLiveText();

	/** The Find/Change TAB these results were searched with (an IFindChangeOptions::SearchMode value;
	    -1 = nothing searched yet). Held as a plain int so this header needs no text includes.
	    Recorded beside SetFromBook, and cleared by Clear().

	    Why it has to be remembered: the replace pass RE-WALKS each chapter, and a walk runs in the
	    mode that is current AT THAT MOMENT. Switching tabs between a search and Change Checked
	    therefore re-walks with a different query, returns a different set of matches, and leaves
	    every stored walk order pointing at the wrong occurrence. Nothing wrong is written - the
	    same-occurrence test refuses each one - but the whole run comes back "missing" with nothing to
	    explain it. Comparing this against the current mode is what lets the replace say why. */
	void SetSearchMode(int32 mode);
	int32 GetSearchMode();

	/** The query these results were found with, as ONE READY-MADE LINE - the string plus the tab it
	    was searched on: "cat  (Text)", or "Glyph 1234 (Kozuka Mincho Pr6N  Regular) U+845B  (Glyph)".

	    Held as a finished line rather than as its parts because the parts live in two different
	    places: the string is on IFindChangeOptions, and the tab's NAME is the Find/Change dialog's own
	    label, spelled by the search engine. The model would have to learn both to join them, and the
	    only thing that reads this - the report's heading - wants them joined.

	    Recorded by the search beside SetSearchMode, and cleared by Clear(), so it can never outlive
	    the results it describes. It must NOT be read back off the Find/Change dialog at save time:
	    the user can retype the query between the search and the save, and the file would then name a
	    query these rows never came from.

	    Empty for a scan - neither scan has a query - and empty until the first search of a session. */
	void SetQueryText(const PMString& query);
	PMString GetQueryText();

	/** What the replace was told to WRITE, as one ready-made line - the change string plus its Change
	    Format, or the Glyph tab's replacement glyph. See KBSSearchEngine::DescribeCurrentChange.

	    The change side's counterpart to SetQueryText, and recorded by the same rule: the REPLACE
	    records it on its way in, because the user is free to retype Change To the moment it returns
	    and a report that read the dialog at save time would name a replacement these rows never took.

	    ***** WRITTEN INTO THE REPORT ONLY WHILE IsShowingReplaceOutcome IS ON. ***** A search that
	    has not been replaced yet leaves it empty, and that is the point (user's decision,
	    2026-08-04): a "Change:" line in the report of a plain search would name something that has
	    not happened, which reads as something that has.

	    Cleared by Clear(), so it can never outlive the results it describes. */
	void SetChangeText(const PMString& change);
	PMString GetChangeText();

	/** EVERYTHING a walk is driven by, as one opaque comparable string: the query itself plus every
	    Find/Change switch that decides which matches come back (see
	    KBSSearchEngine::BuildWalkSignature for the list).

	    Not the same thing as GetQueryText, and deliberately a second field rather than a richer
	    version of it: that one is a CAPTION - it is written into the saved report's heading and has
	    to stay readable - while this one is a KEY, compared for equality and never shown.

	    Why the replace needs it. Change Checked RE-WALKS each chapter and lines the Nth match of that
	    walk up with the hit whose walkOrder is N. That only holds while the walk returns the same
	    matches in the same order, which needs the query AND its options to be what they were when the
	    search ran - and the walker is handed the LIVE IFindChangeOptions (ITextWalker.h:58-61), so
	    whatever the dialog holds at replace time is what it walks by.

	    Comparing the TAB alone (SetSearchMode) is not enough: retyping the find string, or turning
	    Include Footnotes off, changes the match set without changing the tab.

	    Empty until the first search of a session, and empty for a scan - neither has a query.
	    Cleared by Clear(), so it can never outlive the results it describes. */
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

	/** The total number of hits across ALL chapters (uncapped) - for the status summary and a
	    future export. */
	int32 GetTotalHitCount();

	/** The number of chapters that have at least one DISPLAYED hit (the tree root's child count
	    under the display cap). Chapters past the cap are not shown. */
	int32 GetDisplayChapterCount();

	/** The number of hits DISPLAYED under chapter 'chapterIdx' - capped in book order so the whole
	    tree shows at most kKBSDisplayHitLimit hit rows. The chapter still STORES every hit. */
	int32 GetDisplayHitCount(int32 chapterIdx);

	/** A chapter node's display: its name and its hit count. false = index out of range. */
	bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount);

	/** How many FONT rows this chapter shows - the groups that still have a displayed hit under the
	    panel's cap. ZERO means this chapter has no font level at all, which is how the tree knows to
	    hang the hits off the chapter itself: a Find/Change result names no font.

	    The groups that lose everything to the cap are the LAST ones (they are in first-appearance
	    order, and the cap keeps a prefix of the chapter's hits), so the displayed groups are the
	    first N - which is why GetNthChild can ask for one by position. */
	int32 GetDisplayFontCount(int32 chapterIdx);

	/** How many hits are DISPLAYED under one font group - its share of the chapter's own display
	    cap. 0 for an index out of range, and for a group the cap cut off entirely. */
	int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx);

	/** A font node's display: the font's name and its FULL hit count (uncapped, so the row can say
	    "shown / total" the way a chapter row does). A group whose font could not be named answers
	    "(unknown font)" rather than an empty label. false = index out of range. */
	bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount);

	/** The 'nth' hit of one font group, as an index into the CHAPTER's hits - the translation the
	    tree needs, since a node names its hit by the chapter-wide index throughout. -1 = out of
	    range. */
	int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth);

	/** Which font group a hit belongs to, and where it sits inside that group: the tree's "who is my
	    parent" and "which child am I". -1 for an index out of range, and for a chapter with no
	    groups - which is the same answer, and means the same thing to the tree: hang off the
	    chapter. */
	int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx);
	int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx);

	/** Everything a hit row needs to lay itself out and paint itself. @see GetHitRow. */
	struct RowDisplay
	{
		PMString		locator;	// "P1(2) overset hidden locked" - drawn at the full text colour
		PMString		accentFlag;	// "missing" / "refused", or empty - drawn in the accent colour
		PMString		preText;	// the line, split around the match
		PMString		matchText;
		PMString		postText;
		PMString		fontName;	// drawn at the row's right edge; empty on a Find/Change row
		bool			checked;
		bool			replaced;
		bool			locked;
		ChangeOutcome	outcome;

		RowDisplay() : checked(false), replaced(false), locked(false), outcome(kOutcomeNone) {}
	};

	/** One row's worth of everything, in a single call.
	    Its own getter because a row used to ask the model four separate times to draw itself - the
	    display strings, the flags, the outcome and the accent word - each one walking to the same
	    hit to hand back one part of it. Only the rows on screen are ever laid out, so this was
	    never expensive; it is simply four questions where the row has one.
	    @return false for an index out of range, leaving out untouched. */
	bool GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out);

	/** The WHOLE result set as one tab-separated block, so a script can read what the panel is
	    showing. Serves app.kfcResults (KBSScriptProvider.cpp), its only caller.

	    Its reason to exist is the same as app.kfcStatus': verification. The status line gives one
	    summary sentence, which proves the counts and nothing else - whether the right ROW carries
	    "missing", whether a locked row lost its check box, whether a page reads "P4(1)ov" - none of
	    that is in it, and reading it off the screen cannot be automated. This is those same rows in
	    text.

	    Line 1 is a header, then one line per hit - uncapped, so it includes the hits past the
	    panel's display limit:

	        #  <book name>  <from book>  <showing outcome>  <chapters>  <total hits>
	        <chapter idx>  <chapter name>  <hit idx>  <locator>  <accent>  <pre>  <match>  <post>
	            <font name>  <checked>  <replaced>  <locked>  <outcome word>  <font group>

	    Tab, return and backslash inside the text are escaped (\t, \n, \\), so one hit is always
	    exactly one line with a fixed column count - a match CAN run across a paragraph break. */
	void DescribeAllRows(PMString& out);

	/** The whole result set as the text file "Save Results..." writes: a heading block, a blank line,
	    a column-heading row, then ONE LINE PER HIT - tab separated, uncapped (every stored hit,
	    including those past the panel's display limit).

	        Kohaku Find/Change (after Change Checked)
	        Query: cat  + Find Format (size: 14 pt + Paragraph style: Body)  (Text)
	        Change: dog  + Change Format (size: 20 pt)
	        Book: savetest.indb
	        Summary: 9 replaced in 3 of 3 chapter(s)
	        Rows: 9

	        <Document>  <Page>  <Text>  <Font>  <Flags>
	        ch1.indd    1       ...the cat sat on the...
	        ch1.indd    2       ...three cats...                    lock

	    The format detail in those two lines is written IN FULL, however long it runs (user's
	    decision, 2026-08-04) - the replace prompt is the one that shortens it. "Change:" appears on
	    the aftermath of a replace only; see SetChangeText.

	    NOT DescribeAllRows in another dress, though they read the same rows. That one is the machine
	    port behind app.kfcResults: it escapes tabs and newlines to "\t" / "\n" so a script can split
	    the block back into fields. This one is pasted into a spreadsheet by a person, where a literal
	    "\n" is noise - so here the same characters are FLATTENED TO A SPACE instead (a match can run
	    across a paragraph break, and a real newline in a cell splits the row).

	    @param summaryLine the panel's status line, written into the heading as "Summary:" - passed in
	           rather than fetched, because the model does not know the panel. Empty = leave the line
	           out. Handing it in is also what keeps the heading's wording identical to the panel's for
	           every kind of run, without the model counting anything a second time. */
	void BuildReportText(const PMString& summaryLine, PMString& out);

	/** A hit node's display: the page locator and the three line segments to paint. false = index
	    out of range. @see GetHitRow when the flags are wanted as well. */
	bool GetHitDisplay(int32 chapterIdx, int32 hitIdx,
		PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost);

	/** A hit's jump anchors (Task 3): the chapter's document / file and the match's story +
	    text range. false = index out of range. */
	bool GetHitLocation(int32 chapterIdx, int32 hitIdx,
		UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd);

	/** Rebind a chapter's document reference (Task 3): after a closed chapter is reopened at jump
	    time, later jumps must use the live database, not the dead one from search time. */
	void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef);

	/** Select / deselect one hit for replacement. Ignored for anything the panel draws no check box
	    on - a hit already replaced (the text it matched is gone), a locked one (InDesign offers no
	    way to change locked content), one that already says why it was left alone, and every row of
	    a scan (a report has nothing to replace). It asks that question the same way the panel does,
	    so the model can never hold a checked hit that no row offered; it is a backstop rather than
	    the first line of defence, since those rows carry no box to click in the first place. */
	void SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked);

	/** A hit's row-cell flags: selected, already replaced, and locked. The last two both mean "this
	    row gets no check box", for different reasons. false = index out of range.
	    (This also answers "is it checked?", which an IsHitChecked of its own used to do until
	    2026-07-30 with no callers left - every asker wants the other two flags in the same breath.) */
	bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked);

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

	/** How many CHAPTERS hold at least one checked, unreplaced hit - i.e. how many documents the
	    replace will write to. The confirmation prompt needs it because undo is per document: one
	    chapter means a single Ctrl+Z puts everything back, more than one means one undo each. */
	int32 GetCheckedChapterCount();

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
	    immediately before HandlePopupMenu and the Check All / Uncheck All actions read it back - an
	    action component is handed no widget context of its own. (The pattern is KESCL's
	    KESCLBatchCheck::SetContextMenuNode, which its "Copy as Text" row menu uses the same way.)

	    Clear() puts it back to kNoContextMenuChapter, so an index taken from one result set can never
	    name a chapter of the next one; the readers range-check it as well, because a caller that never
	    went through the menu - a script firing the action - reaches them with whatever is stored. */
	enum
	{
		kContextMenuBookRow		= -1,	// the BOOK row: the commands reach every chapter
		kNoContextMenuChapter	= -2	// nothing has been right-clicked: they do nothing at all
	};
	void SetContextMenuChapter(int32 chapterIdx);
	int32 GetContextMenuChapter();

	/** A hit's chapter-local walker order, or -1 for an out-of-range index. The replace pass
	    re-walks a chapter and lines the Nth match of that walk up with the hit whose walkOrder is
	    N - the only key that survives replacing (see Hit::walkOrder). */
	int32 GetHitWalkOrder(int32 chapterIdx, int32 hitIdx);

	/** A chapter's document binding and file. The replace pass works chapter at a time, so it
	    needs this without going through a hit. false = index out of range. */
	bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile);

	/** What the same-occurrence test asks of a row: the story it was found in, where the match
	    started and ENDED, and the whole of its text as one number.

	    ★ The DRAWN text (matchText) is deliberately NOT handed out here any more. It is capped at
	    500 characters for the row, so comparing it judged a long GREP match on its first 500 and
	    let a rewrite past that point through as "the same occurrence" (found 2026-08-04). The hash
	    covers the match whole - see KBSSearchEngine::HashMatchText.

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
	    replace was allowed to touch. Drops the rows the user had unchecked, and then the chapters
	    left with nothing.

	    Does NOTHING when no row was asked about, so a replace that was asked for nothing never
	    wipes the result set. Sets the aftermath flag (see IsShowingReplaceOutcome), which takes
	    every check box off the panel.
	    @return the number of rows left in the model. */
	int32 KeepCheckedRows();

	/** Record a completed replacement: the row keeps its page locator but takes the range the
	    replace command reported writing, is marked replaced, and leaves the selection. A replaced
	    hit can never be checked again - the text it matched is gone, so a second replace pass would
	    have nothing to line it up with.

	    The three displayed segments are deliberately NOT set here: several matches can share one
	    paragraph, and a line read at the moment ITS match was written still shows the later matches
	    in that paragraph unreplaced. The replace pass fills them in once the chapter is finished -
	    see SetHitSegments and GetHitReplacedRange. Until then the row still shows what the search
	    found, which is why a run that is cancelled between the two can be rolled back as one. */
	void MarkHitReplaced(int32 chapterIdx, int32 hitIdx, TextIndex newStart, TextIndex newEnd);

	/** Where a REPLACED row's new text sits: its story and the range MarkHitReplaced recorded.
	    The replace pass walks its chapter's rows with this at the end of the run to read each
	    replaced line back in its final state.
	    @return false for an index out of range, and for any row that was not replaced - so the
	            caller's loop needs no flag test of its own. */
	bool GetHitReplacedRange(int32 chapterIdx, int32 hitIdx, UID& outStoryUID, TextIndex& outStart,
		TextIndex& outEnd);

	/** Give a row what it now stands for: the three text segments it DISPLAYS, and the hash the
	    same-occurrence test COMPARES. The other half of MarkHitReplaced: the replace pass calls it
	    once per replaced row after the chapter's last replacement, when the paragraphs have stopped
	    moving. Every other field is left alone.

	    ***** THE HASH GOES IN THE SAME CALL, AND IT HAS TO. ***** The two describe one fact - what
	    this row points at now - and a row carrying one of them from before the replacement and the
	    other from after it is a row that cannot be jumped to: MatchIsSameOccurrence reads the hash,
	    finds the text it was taken from is gone, and answers "the replacement is no longer here".

	    That is exactly what happened between 2026-08-04 and 2026-08-05. The test used to compare
	    matchText, which this function has always updated, so the pair could not come apart; when
	    the hash took over as the thing compared (see KBSSearchEngine::HashMatchText), the update
	    did not follow it here, and every replaced row lost its jump. Splitting display from
	    comparison was
	    right - the drawn text is capped at 500 characters and cannot vouch for a long match - but
	    they are still written at the same moment, from the same range.

	    @param newMatchHash KBSSearchEngine::HashMatchText over the range the replace command
	           reported writing - the SAME range the three segments were read from. */
	void SetHitSegments(int32 chapterIdx, int32 hitIdx, const PMString& newPre,
		const PMString& newMatch, const PMString& newPost, uint64 newMatchHash);

	/** Build hit.locator from the hit's own fields. THE one definition - the search's page-ordering
	    pass and the post-replace thinning both call it, so the two can no longer drift apart.

	        P<page>(<n>) overset hidden locked  -> hit.locator
	        missing | refused                   -> hit.accentFlag, drawn after it in accent colour

	    The page ordinal comes from hit.pageOrdinal (0 = leave it out). The flags are separated by
	    spaces and spelled out IN FULL rather than clipped, because each one explains a row the user
	    cannot act on. "+" is deliberately NOT the separator: InDesign's own overset marker is a "+",
	    so "P5+locked" reads as "page 5, overset".

	    The flags STACK - "P4(1) locked missing" is a locked row that has since been jumped to and
	    found changed. Only missing and refused are exclusive, being two values of one field. */
	void BuildHitLocator(Hit& hit);

	/** Turn the two break characters into the marks InDesign itself draws with Show Hidden
	    Characters on - a pilcrow for a paragraph end (CR), a return arrow for a forced line break
	    (LF) - in place. A string holding neither is left exactly as it came.

	    THE one definition, called by BOTH places a match is shown: the panel's cell
	    (KBSColorTextView::Draw) and the saved report (BuildReportText). Since 2026-08-04 a match is
	    carried WHOLE however many paragraphs it spans, and neither of those draws a raw break with
	    any width - the paragraphs either side of it run together and read as one piece of text -
	    so both have to mark them, and marking them differently in the two places would make one row
	    read as two different rows.

	    ***** DISPLAY ONLY. ***** Never applied to what the model holds: the stored match text is
	    compared character for character against the document before a replace
	    (KBSSearchEngine::MatchIsSameOccurrence), and a marked-up copy would fail every comparison.
	    Callers mark a COPY, at the moment they draw or write it.

	    (The report flattens what is left afterwards, which is what keeps its tabs from splitting a
	    cell. The marks take the breaks out of that pass's way, so nothing there changes.) */
	void MarkUpBreaksForDisplay(PMString& s);

	/** Record why a hit was not replaced. Rebuilds the row's locator so the word shows up at once,
	    and clears the selection - a row that says why it cannot be changed must not stay checked.
	    Called by the replace pass, and by the jump when it finds the text at a row's position is no
	    longer the text the row describes. Ignored for a hit that WAS replaced. */
	void SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome);

	/** Is the panel showing the AFTERMATH of a replace rather than a search's results? Set by
	    KeepCheckedRows, cleared by Clear. While it is on, no row offers a check box:
	    the list is a report, not a work list. It is asked as well as the per-row flags because the
	    aftermath can hold rows with no flag at all - a chapter the safety limit cut short, or one
	    that could not be opened. Those were never looked at, so nothing can be said about them. */
	bool IsShowingReplaceOutcome();

	/** Start remembering every row a replace changes, so a run the user stops can be put back.
	    Only the rows actually written to are copied - one copy each, taken just before the change -
	    so the cost follows the work done rather than the size of the result set.

	    A replace that is cancelled rolls the TEXT back through its command sequence (a regular
	    ICommandSequence rolls the database back to where it started when the global error code is
	    not kSuccess, which is what ProgressBar's WasCancelled(kTrue) sets). That leaves the panel
	    describing replacements that no longer exist, so the two have to be put back together: this
	    is the panel's half.

	    Exactly one of RollBackRows (the run was cancelled) or ForgetRowBackup (it committed) must
	    follow, or the copies stay alive until the next replace. */
	void BeginRowBackup();

	/** Put every remembered row back the way it was and stop remembering. Newest change first, so a
	    row that was changed twice ends up holding the oldest copy - the one the search left. */
	void RollBackRows();

	/** Stop remembering and release the copies: the replace committed, so the rows keep what they
	    were given. */
	void ForgetRowBackup();

	/** Remove one chapter from the results, leaving the others in place. For retiring a single
	    chapter whose results have gone stale - the book-scope half of the result-invalidation work,
	    where closing one chapter should drop that chapter rather than the whole result set.
	    @note Indices after chapterIdx shift down by one - iterate backwards when dropping several.
	    @note NOTHING CALLS THIS YET. It is here for the chapter-level invalidation that is still to
	          be written; until that lands it is untested, so treat it as a sketch, not as API. */
	void DropChapter(int32 chapterIdx);
}

#endif // __KBSResultModel_h__
