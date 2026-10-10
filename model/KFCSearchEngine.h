//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The search engine: walks the user's CURRENT Find/Change query across the scope the Book Scope
//  toggle selects - every chapter of the TARGET book when it is ON (the book the Book panel is
//  showing, or the active book when no panel can be reached: KFCBookScope::ResolveTargetBook), what
//  Edit > Find/Change's Search: names when it is OFF, never a silent fallback between
//  them - and collects the matches into KFCResultModel, grouped by chapter. Unlike KESCL - which
//  supplied its own literal text and pinned the mode to plain text - KFC touches nothing on the
//  Find/Change panel: it walks with whatever the user set there, MODE INCLUDED (Text or GREP). The
//  walk is read-only (a SaveRestoreModifiedState dirty guard per document).
//
//  Each collected hit carries the line's text pre-split into the three segments the colour cell
//  paints (before / matched / after) and the jump anchors (story UID + text range).
//
//========================================================================================

#ifndef __KFCSearchEngine_h__
#define __KFCSearchEngine_h__

#include "InterfacePtr.h"
#include "PMString.h"
#include "UIDRef.h"
#include "WalkerScopeOptions.h"
#include "KFCModelTypes.h"	// KFCResultModel::RowLocation - LocateRow's answer

class ITextWalker;			// AcquireWalker
class ITextWalkerSelectionUtils;

/** KFC SEARCHES AND REPLACES FORWARD ONLY (the author's call). The results are a list, so a direction
    means nothing to KFC - and a backward search lists matches a forward replace does not make (GREP
    lookarounds read other text backwards). The one exception is the replace's writing walk for a GREP
    query holding ^ (KFCBackwardSearchScope, below). For the life of the object the session's search
    direction is forward; the user's own setting is put back by the destructor, whatever way the run
    ends (kSearchBackwardsSilentCmdBoss, measured). Create it OUTSIDE any command sequence: an aborted
    sequence would take the switch back and leave the restore to set it a second time. */
class KFCForwardSearchScope
{
public:
	KFCForwardSearchScope();
	~KFCForwardSearchScope();
private:
	KFCForwardSearchScope(const KFCForwardSearchScope&);
	KFCForwardSearchScope& operator=(const KFCForwardSearchScope&);
	bool	fRestore;
	int32	fMode;
};

/** THE REPLACE'S WRITING WALK GOES BACKWARD WHEN THE GREP QUERY HOLDS ^ (the author's call). The search,
    and the verify pass that re-walks it, stay forward (KFCForwardSearchScope above); only the walk
    that writes turns round, for the life of this object, and the direction it found is put back by
    the destructor. Chosen BEFORE anything is written - which way it has to go
    depends on the text as the search saw it (see WriteBackward in KFCReplaceEngine.cpp). Create it
    inside a KFCForwardSearchScope and OUTSIDE any command sequence, for the same reason as that one. */
class KFCBackwardSearchScope
{
public:
	explicit KFCBackwardSearchScope(bool wanted);
	~KFCBackwardSearchScope();
private:
	KFCBackwardSearchScope(const KFCBackwardSearchScope&);
	KFCBackwardSearchScope& operator=(const KFCBackwardSearchScope&);
	bool	fRestore;
	int32	fMode;
};

namespace KFCSearchEngine
{
	/** Resolve the scope from the Book Scope toggle (the TARGET book's chapters when it is ON - see
	    the head of this file for which book that is - what Search: names when it is OFF, never a
	    silent fallback between them), walk the user's current Find/Change query across it, fill
	    KFCResultModel with the hits (grouped by chapter, only chapters with >=1 hit), and build a
	    one-line status summary. Each chapter the search opened windowless is handed back as soon as
	    it has been walked: the hits' display text is already extracted, and a jump or a replace
	    reopens a chapter by its file.

	    @param outSummary  a ready-to-show status line for the panel.
	    @return the total number of matches across the scope. */
	int32 SearchBook(PMString& outSummary);

	/** What a run searches - asked of Book Scope and Edit > Find/Change's Search: by ResolveRunScope. */
	struct RunScope
	{
		bool		fromBook;			// Book Scope on
		bool		allDocuments;		// Search: = All Documents (Book Scope off)
		int32		selectionScope;		// an IWalkerScopeFactoryUtils::WalkScopeType: Document, Story, To End of Story, Selection
		PMString	fellBackNote;		// a Search: the selection did not offer, said (empty otherwise)
		RunScope() : fromBook(false), allDocuments(false), selectionScope(0) { fellBackNote.SetTranslatable(kFalse); }
	};

	/** THE SCOPE DOORS SEARCHBOOK HAS ALWAYS ASKED, IN ONE PLACE (so the query run asks the same):
	    Book Scope with a Search: other than Document, a Search: this panel cannot follow, no target book, an
	    empty one, no active document. false = refused, outRefusal holds SearchBook's own sentence. Touches nothing. */
	bool ResolveRunScope(RunScope& out, PMString& outRefusal);

	/** WHAT A RUN WOULD RUN ON, IN WORDS (docs/superpowers/specs/_done/2026-10-07-kfc-query-dialog-and-selected-documents-design.md
	    section 4-4): the query dialog's Runs on: line, made from ResolveRunScope's answer so the line and the run
	    cannot differ. true = "Runs on: the book "<title>" (<n> document(s))." / "Runs on: <n> selected document(s) of the
	    book "<title>"." (Find/Change Selected Documents (Book)) / "Runs on: All Documents (Search: in
	    Edit > Find/Change)." / "Runs on: Document "<name>" (...)." / "Runs on: Story in "<name>" (...)." (To End of
	    Story and Selection alike); false = "Cannot run: " and ResolveRunScope's refusal. Touches nothing - the book
	    is described by KFCBookScope::DescribeTargetBook, not listed - so it may be asked at any time. */
	bool DescribeRunScope(PMString& outWords);

	/** Is there anything to find on the tab Edit > Find/Change is on - a string, a glyph, a character type or a
	    format? (HasFindQuery, which SearchBook asks before its commit point.) */
	bool HasFindQueryNow();

	/** Is a search running right now? The progress bar pumps events while it is up, so a menu
	    command could otherwise be dispatched INTO a running search. The panel's actions ask this
	    and grey themselves out; SearchBook itself turns a re-entrant call away as a last resort. */
	bool IsSearching();

	/** State which Find/Change TAB to work in, by re-committing the mode the user already has
	    selected. Call this immediately before a find or a replace walk.

	    This is the ONE thing KFC sets on the Find/Change side, and it sets it to the value that is
	    already there. It is needed because the engine does not pick the mode up from
	    IFindChangeOptions when a command runs - the mode has to have been COMMITTED through
	    kFindSearchModeCmdBoss, which is what the dialog itself does when a tab is clicked. Without
	    it, a walk driven from outside the dialog runs as plain TEXT whatever tab is on screen:
	      * a Glyph-tab search was matching its find string as literal text (it looked like it worked,
	        because the glyph's character was in that string), and
	      * a replace then wrote the TEXT tab's change string over what the Glyph tab had found.
	    Both reported by the user. Every one of the four entry points in the SDK's own
	    SnpFindAndReplace (find/replace text, find/replace glyph) does this right before it runs -
	    including the glyph pair, which use the same kTWReplaceTextCmdBoss this does.

	    Writes the value it just read, so the user's own settings never change: this STATES the mode,
	    it does not choose one.

	    On the Glyph tab it states the FIND GLYPH too, for the same reason and by the same rule: the
	    dialog commits the glyph through kFindChangeGlyphIDCmdBoss when one is picked, so a walk driven
	    from outside the dialog has to commit it again. Stating the mode alone left the engine in glyph
	    mode with no glyph and the panel found nothing at all (reported by the user). The CHANGE glyph is
	    deliberately NOT stated here - see CommitReplaceSide.

	    The CHANGE MODE - IFindChangeOptions' second axis, kChange versus kTransliterate (the CJK
	    character-type conversion) - is stated as well, whatever the tab, through
	    kFindChangeModeCmdBoss: SnpFindAndReplace's own header says a walk that wants character types
	    "must first change the mode with this command", and the same axis committed by somebody else
	    would otherwise be what a Text-tab walk quietly runs with. On the Transliterate tab the FIND
	    character type is stated too (kFindCharacterTypeCmdBoss), the exact glyph rule again; the
	    CHANGE character type belongs to CommitReplaceSide.

	    @return true when every value above was actually stated. FALSE MUST STOP THE CALLER: what
	            was not stated is not merely missing, it is whatever was committed last - by an
	            earlier run, or by the dialog on a tab the user has since left - so a walk that went
	            ahead would search by a query nobody typed and the results would then be filed under
	            the tab that IS on screen. The SDK's own snippet stops on this command too
	            (SnpFindAndReplace.cpp:511-516, :598-603).

	    @note Call it OUTSIDE any command sequence. It processes a command, and a session-setting
	          command inside the replace sequence would become part of that undo step.
	          ! ONE CALLER DOES NOT, BY DESIGN: the query run (KFCQuerySequence) states each query's tab
	          inside its one sequence, between queries, and loads the query there too. Measured
	          (the spec's M3, case qs-undo): a Ctrl+Z of the run brought no Find/Change setting back - the
	          strings stayed empty. */
	bool CommitSearchMode();

	/** State what a replace will WRITE, for the tabs whose change side is not a string: the Glyph
	    tab's Change To glyph, and the Transliterate tab's change character type. Both are
	    re-committed at the value the dialog already holds. Call it immediately after
	    CommitSearchMode on the replace path only - a search must never leave a change-side value
	    standing, since nothing on screen would say it had been set.

	    Does nothing on the other tabs, whose change side is a string the replace command carries
	    itself. On the Glyph tab an EMPTY Change To is stated as -1 (it deletes every match).

	    False means the value is NOT standing on the options - either the settings could not be read
	    at all, or the command that states them did not go through. The caller must then refuse the
	    replace rather than walk, because the command would otherwise write whatever was committed
	    last - a value the user never chose on this run, and the exact failure this whole mechanism
	    exists to prevent.

	    @return true when it is safe to replace.
	    @note Same as CommitSearchMode - call it OUTSIDE any command sequence (and the same one exception:
	          the query run, KFCQuerySequence::Run, states it for each query inside the run's sequence). */
	bool CommitReplaceSide();

	/** The Find/Change dialog's English name for a tab (an IFindChangeOptions::SearchMode value):
	    "Text", "GREP", "Glyph", "Transliterate" ... - empty for a value outside the enum. The status
	    line and the panel's tab (KFCPanelTitle) both name the tab through this one table. */
	const char* TabName(int32 mode);

	/** The tab the Find/Change dialog is on NOW (the session's IFindChangeOptions), as an
	    IFindChangeOptions::SearchMode value; -1 when the settings cannot be read. Held as a plain int
	    so this header needs no text includes. */
	int32 CurrentSearchMode();

	/** THE Search: OF THAT TAB (KFC follows it - the author's call). Edit >
	    Find/Change's Search: - Document / All Documents / Story / To End of Story / Selection - as an
	    IWalkerScopeFactoryUtils::WalkScopeType value (IFindChangeOptions::GetFindChangeScope of the current
	    tab); -1 when the settings cannot be read. Each tab keeps a Search: of its own.
	    kEmptyScope is answered as kDocumentScope: it is what a session reads before anything has set
	    Search: (measured, straight after a launch), and a user who never opened the dialog has asked
	    for nothing but the document. */
	int32 CurrentSearchScope();

	/** Put Search: of tab 'mode' back to 'scope' (a WalkScopeType value, as CurrentSearchScope gives it) - 1.4.0, the
	    author's call of 2026-10-10: a row's click selects its match, and Edit > Find/Change, open, answers a selection
	    change by re-picking Search: (measured: to Document, or to Selection), which its own Find Next does not do. The
	    UI half asks this the moment it hears the change (KFCJump::KeepSearchScopeAfterClick). SCHEDULED, not processed:
	    it is asked from inside the notification of the dialog's own command. Measured: written back, the open dialog
	    shows the value again and keeps it. */
	void RestoreSearchScope(int32 mode, int32 scope);

	/** Read a story's first words again into its rows (KFCResultModel::SetStoryLead) - the story row's text - after a
	    write of KFC's in it (1.4.0, the author's call of 2026-10-10: the story row says what the story says now). */
	void RereadStoryLead(int32 chapterIdx, const UIDRef& storyRef);

	/** WHAT THE SELECTION MAKES OF A Search:. The dialog offers Search: values by the
	    selection - InDesign's own answer, IWalkerScopeFactoryUtils::GetActiveSelectionScope, the widest one it
	    allows (SnpFindAndReplace builds its scope menu from it): nothing selected = All Documents and Document;
	    a text frame = + Story; a text cursor = + To End of Story; text = + Selection. A value it does not
	    offer comes back as kDocumentScope - what the dialog then shows and searches (measured:
	    Story with nothing selected, and Selection with only a caret, searched the whole document); any other
	    value comes back as it went in. */
	int32 SearchScopeForSelection(int32 scope);

	/** Search:'s English name for a WalkScopeType value, the way the menu, the panel's tab and the status
	    line say it: "Document", "All Documents", "Story", "To End of Story", "Selection"; empty otherwise. */
	const char* SearchScopeName(int32 scope);

	/** The Find command's name for the scope it would search NOW: "Find in Book" while Book
	    Scope is on - "Find in Selected Documents" when the run would take only the Book panel's selected documents
	    (Find/Change Selected Documents (Book) - KFCBookScope::DescribeTargetBook); with it off, Search:'s -
	    "Find in Document", "Find in All Documents", "Find in Story",
	    "Find to End of Story", "Find in Selection" ("Find in Document" for a Search: this panel refuses). */
	const char* FindCommandName(bool bookScopeOn);

	/** Can Find in ... run on this tab? No on Colour alone (since 1.4.0 the Object tab lists page items -
	    KFCObjectSearch). Asked by the menu (greys the command) and by the search itself (refuses), so the two
	    cannot disagree. */
	bool CanSearchTab(int32 mode);

	/** Put `text` into Edit > Find/Change on the tab `mode` names - IFindChangeOptions::kTextSearch or
	    kGrepSearch, nothing else: that tab made current, and its Find what set to `text`. For "Link the
	    Application Bar's Search Field to This Panel" (the author's design): Return in that field on the
	    Text or GREP tab - the tab the dialog is on - with text that differs from the tab's.
	    Through the dialog's own commands, kFindSearchModeCmdBoss and kFindStringCmdBoss, in the shape
	    SnpFindAndReplace gives them (ProcessFindChangeCommandInt32 / ProcessFindChangeCommandString) - so the
	    dialog shows what will be searched, and the search that follows reads it like any other. The other
	    settings (switches, Find Format, Search:) are the user's and are left as they are. False = another
	    tab asked for, or a command failed; nothing should be searched then. */
	bool SetQuery(const PMString& text, int32 mode);

	/** EVERYTHING the current Find/Change settings would drive a walk BY, as one opaque string:
	    the tab, the query itself, and every switch that decides WHICH matches come back -
	    case / whole word / kana / width, and the five scope switches GetKFCWalkerScopeOptions reads.
	    Recorded on the results at search time (KFCResultModel::SetWalkSignature) and compared before
	    a row's Replace re-walks.

	    Why it has to exist: the replace re-walks and writes the matches it meets at the rows' places,
	    and the walker is handed the LIVE IFindChangeOptions (ITextWalker.h:58-61) - so a query edited
	    between the search and the replace makes the walk return a DIFFERENT set of matches, one of
	    which can stand where a row does. Comparing the tab alone does not see that: retyping the find
	    string, or turning Include Footnotes off, changes the match set without changing the tab.

	    FIND FORMAT IS ONLY COUNTED HERE, NOT DESCRIBED. The signature carries how MANY
	    attributes the format pane holds, and the two styles it keeps outside that list - but not the
	    attributes' values, because the list itself knows how to compare itself and does it better:
	    see RememberFindFormat / FindFormatHasChanged, which is the pair that answers "same conditions,
	    different value".

	    THE DIRECTION IS NOT IN IT. The walk would follow the dialog's "search backwards" (measured: the
	    walker is handed the live options), but KFC searches and replaces FORWARD ONLY
	    (KFCForwardSearchScope, round every search and replace): the dialog's direction changes
	    nothing KFC does.

	    Everything on the CHANGE side stays out: it decides what gets written rather than what gets
	    found.

	    Comes back EMPTY when the settings cannot be read at all, which every caller has to treat as
	    "cannot tell" rather than as "different": refusing a replace because a query could not be
	    described would be a new way to fail. */
	void BuildWalkSignature(PMString& outSignature);

	/** Keep a copy of the FIND FORMAT this search ran with - the attribute list behind the dialog's
	    format pane, and on the Glyph tab the query's own font - so the replace can ask whether it is
	    still the same one. Called once per search, beside BuildWalkSignature; a search that cannot
	    read the settings simply remembers nothing, and FindFormatHasChanged then says "cannot tell".

	    THE LIST COMPARES ITSELF (AttributeBossList::IsEqual, a deep compare - why a copy
	    rather than a description is the note over this function in the .cpp). The copy is shallow
	    (Duplicate, AttributeBossList.h:157: the attributes' reference counts go up), held in a
	    boost::shared_ptr the way chmlfilter does it (CHMLFiltTextHelper.cpp:134). */
	void RememberFindFormat();

	/** Has the Find Format changed since RememberFindFormat was called?

	    @return true ONLY when the two lists can both be read and are positively different. Nothing
	            remembered, settings unreadable, or a different attribute database - all read as
	            false, because "cannot tell" must never turn into a refusal (the same rule an empty
	            walk signature follows). */
	bool FindFormatHasChanged();

	/** Drop what RememberFindFormat kept. It goes with EVERY KFCResultModel::Clear(), exactly as
	    KFCBookScope::ReleaseSearchedBook does - the remembered format describes the rows that are
	    being thrown away, so it has no business outliving them. DropResults does all three. */
	void ForgetSearchedFindFormat();

	/** THROW THE RESULTS AWAY - ALL OF WHAT DESCRIBES THEM. The rows
	    (KFCResultModel::Clear), the book they were searched in and the chapters it holds open
	    (KFCBookScope::ReleaseSearchedBook) and the Find Format they were found with
	    (ForgetSearchedFindFormat): one fact, so every place that throws the results away calls this
	    rather than keeping the three in step by hand. Worth having no exceptions to even where a
	    part has nothing to do - the search's own commit point remembers the format again a moment
	    later. (KFCBookWatch hands its book back first, unconditionally, and clears the rest only when
	    the panel shows that book - so it pairs the other two itself.) */
	void DropResults();

	/** The walker scope options EVERY KFC walk uses: the five switches read straight off the
	    Find/Change dialog, exactly as the query itself is. A row's Replace must re-walk a story
	    with exactly the options the search that produced the hits used, or it meets other matches
	    than the rows list - hence one definition, shared by both.

	    @note Two of the five ("include locked layers" / "include locked stories") are FIND-only in
	          InDesign - the header states there is no option to change in locked content. They stay
	          in the shared scope so both walks visit the same matches in the same order, and the
	          replace refuses the locked ones one at a time instead (see EditableFrameForMatch). */
	void GetKFCWalkerScopeOptions(WalkerScopeOptions& outOptions);

	/** May the text a hit describes be REWRITTEN? ONE question, asked in TWO steps - the frame the
	    match sits in, then the locks on it - because the second half is the expensive one and a
	    chapter's hits usually share a handful of frames. A pass asking about many hits resolves the
	    frame per hit and the locks ONCE PER FRAME; no lock can change while a replace pass is
	    running.

	    Why it has to be asked at all: the Find/Change dialog can be told to search locked layers
	    and locked stories, but InDesign offers no way to CHANGE what it finds there ("Search
	    Only"), so the replace has to make the same distinction itself. Those matches are listed and
	    can be jumped to, and are then left untouched.

	    EditableFrameForMatch - the frame the match composes into, or, for an overset match
	    (composed but placed nowhere), the frame carrying the "+" indicator. kInvalidUID when
	    neither can be resolved, which IsFrameEditable then reads as editable.

	    IsFrameEditable - are the story and that frame both unlocked? The locks asked about are the
	    ones the dialog names:
	      - the STORY's insert lock (IItemLockData on the text story, which also answers for an
	        inline by way of its parent),
	      - the page ITEM's own lock flags, asked of the frame and of its outermost parent, and
	      - the LAYER the frame sits on.

	    @return false ONLY when one of those locks is positively found. Anything that cannot be
	            resolved - a story without the lock interface, an overset match placed in no frame,
	            an item on no layer - reads as editable, because a "cannot tell" must not start
	            refusing ordinary replacements.

	    (No single-call version: the replace pass and the jump both want the frame in hand for
	    their own reasons.) */
	UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos);
	bool IsFrameEditable(const UIDRef& storyRef, UID frameUID);

	/** Is the text at this position OVERSET - composed, but placed in no frame?

	    The official answer, and the reason this lives here rather than beside the jump that asks it:
	    ITextParcelList.h:87-101 states that "if the TextIndex is in overset an invalid ParcelKey
	    will be returned" by GetParcelContaining. That walk - position to parcel to frame - is
	    already this file's FrameUIDForPosition, which every hit is built through, so asking it here
	    is what keeps "the row was overset when we found it" and "the jump treats it as overset"
	    the same statement. Asked by the jump - KFCJump's JumpToHit - through IKFCRuns.

	    @note NOT the same question as ITextParcelList::GetIsOverset, which is about a whole
	          THREAD (and is the only test that answers for a table cell overflowing on its own).
	          This one is about a single position.

	    @return true when the position has no frame of its own. Anything that cannot be resolved -
	            no text model, no parcel list - reads the same way, exactly as FrameUIDForPosition
	            folds its query failures into kInvalidUID for every other caller. The jump does
	            nothing useful in either case: the overset locator and the wax geometry both fail
	            on the same nil, so the marker is cleared and the view stays put. */
	bool IsPositionOverset(const UIDRef& storyRef, TextIndex pos);

	/** The story's text at [at, at+len), whole (not capped for drawing). Empty when it cannot be read; a range running
	    past the story's end reads up to the end. The official one-call read (textiterator.h AppendToStringAndIncrement),
	    as KCM (KCMTextWords.h WordsAt) and KESCL (KESCLFindInDoc.cpp MatchStillValid) read a range. */
	PMString ReadText(const UIDRef& story, TextIndex at, int32 len);

	/** Give a row what [start, end) reads NOW - the three segments it paints and the hash of the
	    whole match - in one KFCResultModel::SetHitSegments. For the callers that have just moved a
	    row: the replace pass rebuilding a replaced row from the range the command reports, and the
	    rows carried past a write. Call it after the row's range has been set.

	    The segments are the line around the match: the text before it, the match, and the text
	    after it - never reaching outside the paragraphs the match starts and ends in (a different
	    paragraph once the match spans a break).

	    ONE LINE BUDGET, MATCH FIRST - kKFCMaxLineChars = 50, the author's numbers. The three segments
	    carry at most fifty characters BETWEEN THEM: the match takes what it needs up to the whole
	    budget, and what is left is split evenly between the two contexts, a side with less to say
	    than its half handing the remainder to the other. So a paragraph with no breaks in it cannot
	    make every hit carry the whole paragraph.

	    EVERY CUT END IS MARKED with an ellipsis. The pre at its head (it keeps its
	    TAIL, the end nearest the match, which is the end the cell keeps when it ellipsizes); the
	    post at its tail (it keeps its HEAD); and a match longer than the budget at its own tail,
	    drawn in the match colour - with no post at all then, because what follows that cut is more
	    MATCH, and a normal-coloured segment there would show it as text lying outside it.

	    Drawn, and compared: the match's own test (MatchIsSameOccurrence) reads none of the three - it
	    compares the whole match through its hash (HashMatchText in the .cpp) - but RowReadsAsFound,
	    below, compares them as the line around the match. Any of the three may come back empty; all
	    three are empty when the position cannot be read.

	    The same one reading of the story the search's own hits get: one scanner for both halves,
	    the matched characters copied once and hashed from that copy whenever they are the whole
	    match. */
	void RereadRowText(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef, TextIndex start, TextIndex end);

	// (MatchIsSameOccurrence and HashMatchText are inside the .cpp, with their notes: every door asks
	//  through RowReadsAsFound, below.)

	/** DOES THE ROW STILL READ AS IT WAS FOUND? The
	    test every door asks before it acts on a row's stored place: the match's own test over that
	    place - the same story, position and length, and the whole match by its hash
	    (MatchIsSameOccurrence, in the .cpp) - AND the line around it: the three drawn segments read
	    again the way the search read them (ReadHitText, RereadRowText's own reading) and compared with
	    what the row holds. The line is what RelocateStaleRow already asks of a candidate ("the same text
	    with the same line around it"). The answer is how the panel can say "the replacement is no
	    longer here" instead of scrolling to whatever took its place, how a click refuses to hand the
	    user a selection over text they never searched for, and how a row whose text was edited is not
	    written.

	    Why the line as well: a row's place is carried past every change KFC makes - and past an Undo
	    or a Redo of one (KFCUndoFollow puts the rows back with it) - but never
	    past the user's typing or an Undo of anything else, and after those its stored place can stand on
	    ANOTHER occurrence of the same text. The match's own hash cannot tell them apart - a
	    one-character query (the particle U+306E) lands on another match a few per cent of the time in
	    running Japanese, and a zero-width row has no text for the hash at all. Worked through on paper
	    (not measured): "catcatcatcat", row 1 replaced with "kitten" from its menu, Ctrl+Z - rows 2 and 3
	    were left three characters on, standing on the third and fourth "cat", and a Replace of
	    rows 2 and 3 wrote there. (The story's version - ReadStoryVersion - stops that write first; this
	    is the second guard, for a version that has come back to the same number. A Ctrl+Z of a write
	    of KFC's own IS followed now, and the rows stand where their text is; the example stands for an
	    edit that is not followed.)

	    False when the row, its story or its place cannot be read. Asked by the verify walk, the row
	    menus' Replace / Redo and RowStillStands (KFCReplaceEngine); the jump reads a row the same way
	    at each place LocateRow tries (RowReadsAsFoundAt). */
	bool RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db);

	/** A ROW WHOSE PLACE HAS MOVED UNDER IT, LOOKED FOR AGAIN (the author's call). Before the jump gives up
	    on a row that does not read as found, the row's story is walked again under the same query, and the
	    row moves to the ONE match with the same text and the same line around it that no other row stands
	    on. Rows not replaced only. In the model half because it is a walk and the rows put right - the jump
	    (KFCJump.cpp) asks it through IKFCRuns; the notes are at the definition. True = the row was moved,
	    and ioStart / ioEnd are its new place. */
	bool RelocateStaleRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID,
		TextIndex& ioStart, TextIndex& ioEnd);

	/** Where the row is now (spec T2 / T3 / T14) - its stored place, its text focus, the story looked through again.
	    kRowMoved = the row was moved (RelocateStaleRow's rule); kRowElsewhere = found for this jump only - the row's
	    stored place is unchanged. ioStart / ioEnd = the place to go to (unchanged for kRowAtPlace / kRowNotFound). */
	KFCResultModel::RowLocation LocateRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, TextIndex& ioStart,
		TextIndex& ioEnd);

	/** Search This Story Again (a story row's right-click menu): that story alone walked again with the search's query,
	    its rows put back as the walk finds them and its version recorded (KFCResultModel::ReplaceStoryRows). False =
	    refused or failed, the list unchanged; outStatus says what happened either way. */
	bool SearchStoryAgain(int32 chapterIdx, int32 groupIdx, PMString& outStatus);

	/** Search This Document Again (a document row's right-click menu - the author's call of 2026-10-10): that document
	    walked again whole with the search's query - the walk the search makes of each document, the Object tab's
	    included - under a bar with Cancel, its rows put back as the walk finds them and its stories' versions recorded
	    (KFCResultModel::ReplaceChapterRows). A search over part of a document is refused. False = refused, failed or
	    cancelled, the list unchanged; outStatus says what happened either way. */
	bool SearchDocumentAgain(int32 chapterIdx, PMString& outStatus);

	/** A story's VERSION: ITextModel::GetChangeCount - the counter InDesign moves for every change to the
	    story's text, attributes, tables and inlines (ITextModel.h, GetChangeCount), and moves BACK on Undo
	    to exactly the value it had (measured - docs/ai-notes/text-change-counters-2026-08-08.md). The
	    results keep it for every story holding a row (KFCResultModel::SetStoryVersion), taken where KFC
	    last knew the rows to stand - the search, and each change of its own - and the replace compares it
	    before anything is written: a story that moved without KFC knowing (typing, Ctrl+Z, the Track
	    Changes panel, a script) is not written to (the author's call: "safety first", a search again
	    rather than a guess). (KFC knows an Undo or a Redo of a write of its OWN - KFCUndoFollow puts the
	    recorded version back with the rows - so only a Ctrl+Z of anything else leaves it unknown.) The
	    same number is what tells KFCUndoFollow a write was undone or redone.
	    False when the story cannot be read (no database, a UID that is not valid, no text model). */
	bool ReadStoryVersion(IDataBase* db, UID story, uint32& outVersion);

	/** THE WALKER A KFC WALK RUNS ON - the search's, the verify walk's and the write's (the speed-up's S1:
	    docs/superpowers/specs/2026-10-05-kfc-one-at-a-time-and-limits-design.md section 3).
	    outWalker = the walker to Initialize and drive: a kBasicTextWalkerBoss of KFC's own, which carries IID_ITEXTWALKER
	    alone - not the session's shared one (kTextWalkerServiceProviderBoss), which Edit > Find/Change and the spelling
	    panel walk and watch. outSelUtils = what the walk's critical section is taken on: always the SHARED walker's -
	    InDesign's own spelling Change All takes the same shape (spellpanel SpellChangeAllObserver.cpp:257 walks a
	    kBasicTextWalkerBoss, :318-319 takes the section on the shared walker). False = no walker at all; outSelUtils
	    can still be nil - each caller keeps its own gate on it. */
	bool AcquireWalker(InterfacePtr<ITextWalker>& outWalker, InterfacePtr<ITextWalkerSelectionUtils>& outSelUtils);

	/** Let go of the module's static storage during InDesign's controlled shutdown, so every static
	    destructor at DLL unload finds nothing left to do.

	    What there is to let go of: the Find Format this file remembers for the replace's "has the
	    query changed" door (RememberFindFormat) - an AttributeBossList holding references to the
	    dialog's attributes, and the raw IDataBase* they belong to. Dropping the list releases those
	    attributes, which is database work, and doing it from a static destructor means doing it
	    after the application has torn down.

	    Called from KFCStartupShutdown::Shutdown, beside the same call on KFCHitMarker,
	    KFCBookScope and KFCResultModel. */
	void ShutdownCleanup();
}

#endif // __KFCSearchEngine_h__
