//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The search engine: walks the user's CURRENT Find/Change query across the scope the Book Scope
//  toggle selects - every chapter of the TARGET book when it is ON (the book the Book panel is
//  showing, or the active book when no panel can be reached: KBSBookScope::ResolveTargetBook), just
//  the active document (KBSBookScope::ActiveDocument) when it
//  is OFF, never a silent fallback between them - and collects the matches into
//  KBSResultModel, grouped by chapter. Unlike KESCL - which supplied its own literal text and
//  pinned the mode to plain text - KBS touches nothing on the Find/Change panel: it walks with
//  whatever the user set there, MODE INCLUDED (Text or GREP). The walk is read-only (a
//  SaveRestoreModifiedState dirty guard per document).
//
//  Each collected hit carries the line's text pre-split into the three segments the colour cell
//  paints (before / matched / after) and the jump anchors (story UID + text range) for Task 3.
//
//========================================================================================

#ifndef __KBSSearchEngine_h__
#define __KBSSearchEngine_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WalkerScopeOptions.h"
#include "KBSResultModel.h"		// Hit - CollectDocHits fills them

#include <vector>

class RangeProgressBar;

/** Move the run's progress bar to an absolute position. ioReported is the position already sent, so
    that advances too small to be worth a repaint can be swallowed (see the .cpp); pass force = true
    where the bar must land exactly, such as a chapter boundary.

    NOTE: this does NOT make the run cancellable, and neither does any other way of moving the bar - that
    was measured both ways on 2026-07-31. WasCancelled has to be ASKED, and asking it only inside the
    chapter loop misses a cancel pressed during the last chapter. See the ask-once-more test that
    follows the loop in SearchBook and ReplaceChecked. */
void KBSAdvanceProgress(RangeProgressBar* bar, int32& ioReported, int32 target, bool force = false);

/** ***** KBS SEARCHES AND REPLACES FORWARD ONLY (2026-09-26, the user's call). ***** The results are a
    list and the replace is Change All, so a direction means nothing to KBS - and a backward search
    lists matches Change All does not make (H-5: GREP lookarounds read other text backwards). For the
    life of the object the session's search direction is forward; the user's own setting is put back
    by the destructor, whatever way the run ends (kSearchBackwardsSilentCmdBoss, the command the
    2026-09-26 spike measured). Create it OUTSIDE any command sequence: an aborted sequence would take
    the switch back and leave the restore to set it a second time. */
class KBSForwardSearchScope
{
public:
	KBSForwardSearchScope();
	~KBSForwardSearchScope();
private:
	KBSForwardSearchScope(const KBSForwardSearchScope&);
	KBSForwardSearchScope& operator=(const KBSForwardSearchScope&);
	bool	fRestore;
	int32	fMode;
};

/** ***** THE REPLACE'S WRITING WALK GOES BACKWARD WHEN THE GREP QUERY HOLDS ^ (2026-09-27, the user's
    call B). ***** The search, and the verify pass that re-walks it, stay forward (KBSForwardSearchScope
    above); only the walk that writes turns round, for the life of this object, and the direction it
    found is put back by the destructor. Chosen BEFORE anything is written - which way it has to go
    depends on the text as the search saw it (see WriteBackward in KBSReplaceEngine.cpp). Create it
    inside a KBSForwardSearchScope and OUTSIDE any command sequence, for the same reason as that one. */
class KBSBackwardSearchScope
{
public:
	explicit KBSBackwardSearchScope(bool wanted);
	~KBSBackwardSearchScope();
private:
	KBSBackwardSearchScope(const KBSBackwardSearchScope&);
	KBSBackwardSearchScope& operator=(const KBSBackwardSearchScope&);
	bool	fRestore;
	int32	fMode;
};

namespace KBSSearchEngine
{
	/** Resolve the scope from the Book Scope toggle (the TARGET book's chapters when it is ON - see
	    the head of this file for which book that is - the
	    active document when it is OFF - never a silent fallback between them), walk the user's
	    current Find/Change query across it, fill KBSResultModel with the hits (grouped by
	    chapter, only chapters with >=1 hit), and build a one-line status summary. Each chapter
	    the search opened windowless is handed back as soon as it has been walked: the hits' display
	    text is already extracted, and a jump or a replace reopens a chapter by its file.

	    @param outSummary  a ready-to-show status line for the panel.
	    @return the total number of matches across the scope. */
	int32 SearchBook(PMString& outSummary);

	/** Is a search running right now? The progress bar pumps events while it is up, so a menu
	    command could otherwise be dispatched INTO a running search. The panel's actions ask this
	    and grey themselves out; SearchBook itself turns a re-entrant call away as a last resort. */
	bool IsSearching();

	/** State which Find/Change TAB to work in, by re-committing the mode the user already has
	    selected. Call this immediately before a find or a replace walk.

	    This is the ONE thing KBS sets on the Find/Change side, and it sets it to the value that is
	    already there. It is needed because the engine does not pick the mode up from
	    IFindChangeOptions when a command runs - the mode has to have been COMMITTED through
	    kFindSearchModeCmdBoss, which is what the dialog itself does when a tab is clicked. Without
	    it, a walk driven from outside the dialog runs as plain TEXT whatever tab is on screen:
	      * a Glyph-tab search was matching its find string as literal text (it looked like it worked,
	        because the glyph's character was in that string), and
	      * a replace then wrote the TEXT tab's change string over what the Glyph tab had found.
	    Both reported by the user on 2026-07-30. Every one of the four entry points in the SDK's own
	    SnpFindAndReplace (find/replace text, find/replace glyph) does this right before it runs -
	    including the glyph pair, which use the same kTWReplaceTextCmdBoss this does.

	    Writes the value it just read, so the user's own settings never change: this STATES the mode,
	    it does not choose one.

	    On the Glyph tab it states the FIND GLYPH too, for the same reason and by the same rule: the
	    dialog commits the glyph through kFindChangeGlyphIDCmdBoss when one is picked, so a walk driven
	    from outside the dialog has to commit it again. Stating the mode alone left the engine in glyph
	    mode with no glyph and the panel found nothing at all (user, 2026-07-30). The CHANGE glyph is
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
	            (SnpFindAndReplace.cpp:511-516, :598-603). This returned void until 2026-08-08, and
	            both callers therefore believed it had always worked.

	    @note Call it OUTSIDE any command sequence. It processes a command, and a session-setting
	          command inside the replace sequence would become part of that undo step. */
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

	    (Until 2026-08-08 only the first of those two answered false: the stating was done through
	    calls that returned void, so a command that failed left this promising it was safe to write.)

	    @return true when it is safe to replace.
	    @note Same as CommitSearchMode - call it OUTSIDE any command sequence. */
	bool CommitReplaceSide();

	/** The Find/Change dialog's English name for a tab (an IFindChangeOptions::SearchMode value):
	    "Text", "GREP", "Glyph", "Transliterate" ... - empty for a value outside the enum. The status
	    line and the panel's tab (KBSPanelTitle) both name the tab through this one table. */
	const char* TabName(int32 mode);

	/** The tab the Find/Change dialog is on NOW (the session's IFindChangeOptions), as an
	    IFindChangeOptions::SearchMode value; -1 when the settings cannot be read. Held as a plain int
	    so this header needs no text includes. */
	int32 CurrentSearchMode();

	/** Can Find in ... run on this tab? No on Object and Colour, which find page items rather than
	    text. Asked by the menu (greys the command) and by the search itself (refuses), so the two
	    cannot disagree. */
	bool CanSearchTab(int32 mode);

	// (CharacterTypeName, HasFindFormatSet / HasChangeFormatSet, DescribeFormatSetting and
	// DescribeCurrentChange - the captions the replace prompt and the saved report printed - were
	// removed with them on 2026-09-27.)

	/** EVERYTHING the current Find/Change settings would drive a walk BY, as one opaque string:
	    the tab, the query itself, and every switch that decides WHICH matches come back -
	    case / whole word / kana / width, and the five scope switches GetKBSWalkerScopeOptions reads.
	    Recorded on the results at search time (KBSResultModel::SetWalkSignature) and compared before
	    Change Checked re-walks.

	    Why it has to exist: the replace lines the Nth match of its re-walk up with the hit whose
	    walkOrder is N, and the walker is handed the LIVE IFindChangeOptions (ITextWalker.h:58-61) -
	    so a query edited between the search and the replace makes the walk return a DIFFERENT set of
	    matches, in which the Nth one is a different occurrence entirely. Comparing the tab alone does
	    not see that: retyping the find string, or turning Include Footnotes off, changes the match set
	    without changing the tab.

	    ***** FIND FORMAT IS ONLY COUNTED HERE, NOT DESCRIBED. ***** The signature carries how MANY
	    attributes the format pane holds, and the two styles it keeps outside that list - but not the
	    attributes' values, because the list itself knows how to compare itself and does it better:
	    see RememberFindFormat / FindFormatHasChanged, which is the pair that answers "same conditions,
	    different value".

	    ***** THE DIRECTION IS NOT IN IT (again, since 2026-09-26). ***** It was added on 2026-09-25,
	    when the walk was measured to follow the dialog's "search backwards" (the walker is handed the
	    live options), and taken out the next day when KBS began searching and replacing FORWARD ONLY
	    (KBSForwardSearchScope, round every search, replace and Redo): the dialog's direction no longer
	    changes anything KBS does. (This paragraph still said "the direction is in it" until the
	    2026-09-27 defect sweep.)

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

	    ***** THE LIST COMPARES ITSELF. ***** AttributeBossList::IsEqual is a deep compare over every
	    attribute in both lists (AttributeBossList.h:179-182), which is exactly the question being
	    asked and is not one this plug-in can answer from outside: there is no generic "read this
	    attribute's value" call, so KBS used to probe each attribute through nine value interfaces and
	    fingerprint whatever answered. An attribute answering none of the nine went into that
	    fingerprint as its CLASS alone - so "same condition, different value" was invisible, and with
	    the per-hit same-occurrence test gone (2026-08-05) that is a wrong replacement made in
	    silence. The copy is shallow (Duplicate, AttributeBossList.h:157: the attributes' reference
	    counts go up), held in a boost::shared_ptr the way chmlfilter does it
	    (CHMLFiltTextHelper.cpp:134).

	    @note The operators are no help - AttributeBossList keeps operator== and operator!= private
	          (:245-252) - which is a normal C++ way of steering callers to the named method, not a
	          sign that the comparison is unavailable. A comment in this file said the opposite for
	          months. */
	void RememberFindFormat();

	/** Has the Find Format changed since RememberFindFormat was called?

	    @return true ONLY when the two lists can both be read and are positively different. Nothing
	            remembered, settings unreadable, or a different attribute database - all read as
	            false, because "cannot tell" must never turn into a refusal (the same rule an empty
	            walk signature follows). */
	bool FindFormatHasChanged();

	/** Drop what RememberFindFormat kept. PAIR THIS WITH EVERY KBSResultModel::Clear(), exactly as
	    KBSBookScope::ReleaseSearchedBook is paired with one - the remembered format describes the
	    rows that are being thrown away, so it has no business outliving them.

	    The rule is worth having no exceptions to even where a particular call has nothing to do
	    (a scan's results are not a Find/Change query, and the search's own commit point overwrites
	    the memory a moment later anyway): that is the same reasoning KBSCloseDocResponder gives for
	    calling ReleaseSearchedBook on a document-scope result set. Until 2026-08-08 this was kept in
	    step by hand at two of the places that clear the model, and the comment in the .cpp said
	    there were only those two. (The pairing was put right that day.)

	    ***** THE PLACES ARE NAMED IN THE .cpp, NOT COUNTED HERE. ***** This sentence said EIGHT,
	    then NINE, and was wrong both times - the second time because 2026-08-10's verify pass gave
	    the replace a SECOND clearing exit and no number in any comment moved. A count is the one
	    kind of claim that goes stale without anybody editing the line it is on, so the list lives
	    in one place and is spelled by name (the same decision block 4 took for KBSPanelMetrics). */
	void ForgetSearchedFindFormat();

	/** The walker scope options EVERY KBS walk uses: the five switches read straight off the
	    Find/Change dialog, exactly as the query itself is. The replace pass must re-walk a chapter
	    with exactly the options the search that produced the hits used, or the walk order those
	    hits were numbered by no longer lines up - hence one definition, shared by both.

	    @note Two of the five ("include locked layers" / "include locked stories") are FIND-only in
	          InDesign - the header states there is no option to change in locked content. They stay
	          in the shared scope so both walks visit the same matches in the same order, and the
	          replace refuses the locked ones one at a time instead (see EditableFrameForMatch). */
	void GetKBSWalkerScopeOptions(WalkerScopeOptions& outOptions);

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
	            an item on no layer - reads as editable, because that is what it was before this
	            test existed and a "cannot tell" must not start refusing ordinary replacements.

	    (A single-call IsMatchEditable(storyRef, pos) stood in front of this pair for callers with
	    one hit to ask about. It turned out to have none - the replace pass and the jump both want
	    the frame in hand for their own reasons, so both called the pair - and it was removed on
	    2026-08-08.) */
	UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos);
	bool IsFrameEditable(const UIDRef& storyRef, UID frameUID);

	/** Is the text at this position OVERSET - composed, but placed in no frame?

	    The official answer, and the reason this lives here rather than beside the jump that asks it:
	    ITextParcelList.h:87-101 states that "if the TextIndex is in overset an invalid ParcelKey
	    will be returned" by GetParcelContaining. That walk - position to parcel to frame - is
	    already this file's FrameUIDForPosition, which every hit is built through, so asking it here
	    is what keeps "the row was overset when we found it" and "the jump treats it as overset"
	    the same statement.

	    (This said "its ONE caller" from the day it arrived - 2026-08-08, block 12's audit, when
	    that was true - until the fifth audit of this block, 2026-08-10. There are TWO:
	    KBSJump::JumpToHit and KBSJump::SelectHitText, the double click that selects the match,
	    which arrived on 2026-08-09 and did not come back to the sentence that had counted its
	    callers. Sharing one answer between them is the whole point, so the second caller
	    strengthens the reason rather than weakening it - but a count in a comment is a claim like
	    any other. Named rather than cited by line: both citations here read :624 and :866 until
	    2026-08-11, by which time the calls were at :709 and :957. The file they point at says so
	    itself - "a function name cannot go stale" - and that decision had not travelled back
	    along the citation.)

	    @note NOT the same question as ITextParcelList::GetIsOverset, which is about a whole
	          THREAD (and is the only test that answers for a table cell overflowing on its own).
	          This one is about a single position.

	    @return true when the position has no frame of its own. Anything that cannot be resolved -
	            no text model, no parcel list - reads the same way, exactly as FrameUIDForPosition
	            folds its query failures into kInvalidUID for every other caller. The jump does
	            nothing useful in either case: the overset locator and the wax geometry both fail
	            on the same nil, so the marker is cleared and the view stays put. */
	bool IsPositionOverset(const UIDRef& storyRef, TextIndex pos);

	/** The whole of a match, boiled down to one 64-bit number.

	    WHAT IT REPLACED: a CopyMatchText that handed the matched characters back as a PMString,
	    capped at 500 characters because a row only ever draws one line. The
	    same-occurrence test compared that capped copy - so a GREP match of 2000 characters was
	    judged on its first 500, and a rewrite past that point went through as "the same
	    occurrence" (found 2026-08-04). This reads the match WHOLE and keeps nothing but the hash,
	    so the length of the match costs one number either way.

	    64-bit, not 32: a collision here means "the text changed and we replaced it anyway", which
	    is the one direction this plug-in must not fail in.

	    @return the hash, or 0 when the text could not be read at all. 0 is treated as "cannot
	            vouch for this" by MatchIsSameOccurrence - it never compares equal. A ZERO-WIDTH
	            range also answers 0, but never reaches that test: MatchIsSameOccurrence accepts an
	            empty range on its length arm alone, since there is no text left to disagree about
	            (GREP's ^ / $ / lookarounds hand through start == end; found the hard way
	            2026-08-09, when every ^ hit read as missing on an untouched document). */
	uint64 HashMatchText(const UIDRef& storyRef, TextIndex start, TextIndex end);

	/** Give a row what [start, end) reads NOW - the three segments it paints and the hash of the
	    whole match - in one KBSResultModel::SetHitSegments. For the callers that have just moved a
	    row: the replace pass rebuilding a replaced row from the range the command reports, and the
	    rows carried past a Reject or a Redo. Call it after the row's range has been set.

	    The segments are the line around the match: the text before it, the match, and the text
	    after it - never reaching outside the paragraphs the match starts and ends in (a different
	    paragraph once the match spans a break).

	    ***** ONE LINE BUDGET, MATCH FIRST - kKBSMaxLineChars = 50, the user's numbers
	    (2026-08-10). ***** The three segments carry at most fifty characters BETWEEN THEM: the
	    match takes what it needs up to the whole budget, and what is left is split evenly between
	    the two contexts, a side with less to say than its half handing the remainder to the other.
	    So a paragraph with no breaks in it cannot make every hit carry the whole paragraph, which
	    is what the segments did until the 2026-08-10 re-check (F-8).

	    ***** EVERY CUT END IS MARKED with an ellipsis. ***** The pre at its head (it keeps its
	    TAIL, the end nearest the match, which is the end the cell keeps when it ellipsizes); the
	    post at its tail (it keeps its HEAD); and a match longer than the budget at its own tail,
	    drawn in the match colour - with no post at all then, because what follows that cut is more
	    MATCH, and a normal-coloured segment there would show it as text lying outside it.

	    Display only; the same-occurrence test reads none of the three segments (it compares the
	    whole match through HashMatchText). Any of the three may come back empty; all three are
	    empty when the position cannot be read.

	    The same one reading of the story the search's own hits get: one scanner for both halves,
	    the matched characters copied once and hashed from that copy whenever they are the whole
	    match. (Its callers split the line and then hashed the same range again, opening the story
	    twice, until 2026-09-28.) */
	void RereadRowText(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef, TextIndex start, TextIndex end);

	/** How much of each match CollectDocHits fills in. A walk costs the same whatever is asked for;
	    what differs is how much is then read about every match it lands on. */
	enum HitDetail
	{
		kHitPlace,			// story, range and walk order - all that numbering the rows again reads
		kHitPlaceAndText,	// ...and the three drawn segments and the hash - what finding a row again compares
		kHitEverything		// ...and the page, the hidden / locked / footnote flags and the story's first
							// words - a search's row
	};

	/** Every match of the current query in one open document, as the search walks them (walk order
	    stamped, not yet in page order), with the given scope switches, each filled in as far as
	    'detail' says. For the rows of a work list after it has changed under them: the rest of the
	    list is numbered again (KBSReplaceEngine.cpp, RenumberWalkOrders - kHitPlace) and a row a
	    jump found out of place is looked for again (KBSJump.cpp, RelocateStaleRow -
	    kHitPlaceAndText). The walk is the search's own, so the walk orders are the numbers the search
	    would give. Read-only (the walk's own dirty guard). False = the document could not be walked. */
	bool CollectDocHits(const UIDRef& docRef, const WalkerScopeOptions& scopeOptions, HitDetail detail,
		std::vector<KBSResultModel::Hit>& outHits);

	/** Is the match at [start, end) the SAME occurrence a stored hit describes? FOUR questions,
	    asked in this order, none of which may answer no:

	      - same story          (a match in another story is never the one the row means)
	      - same position       (start == expectStart)
	      - same LENGTH         (end - start == expectEnd - expectStart)
	      - same text, WHOLE    (the stored hash covers every character of the match)

	    ***** THE FOURTH IS NOT ASKED OF A ZERO-WIDTH MATCH. ***** A match with start == end - what
	    GREP's ^ / $ / lookarounds hand back - has no text for the fourth question to be about, and
	    its stored hash is 0, which that question reads as "could not vouch for this" and refuses.
	    So an empty range that has satisfied the first three is accepted there and then (2026-08-09,
	    after every ^ hit read as missing on a document nobody had touched). This list said "all of
	    which must answer yes" until the fifth audit of this block, 2026-08-10 - the fix went into
	    HashMatchText's @return and into the .cpp, and stopped one door short of here.

	    ***** WHO ASKS. ***** The jump - KBSJump::JumpToHit, and KBSJump::SelectHitText, the double
	    click that selects the match (the same pair that share IsPositionOverset above) - and, since
	    the row menus of 2026-09-27, the replace's row doors in KBSReplaceEngine.cpp:
	    RowStillStands, ReplaceRowsNow and RowsToRedo. Each asks about the very range its row
	    recorded, so the first three questions are satisfied by construction and the hash is what
	    does the work - the answer being how the panel can say "the replacement is no longer here"
	    instead of scrolling to whatever took its place, how the double click refuses to hand the
	    user a selection over text they never searched for, and how a row whose text was edited
	    is not written. (This said "asked from KBSJump and nowhere else" until 2026-09-28, a day
	    after the replace began asking it again.)

	    The REPLACE asked it until 2026-08-05 too, of every row before writing it, and that is what
	    the position arm was for. It carried a posDelta alongside - how far the replace pass had
	    already moved the text in this story, its own replacements cancelled out, so that whatever
	    difference was left was the USER's editing. Both went together (KBSReplaceEngine::
	    ReplaceChecked), and the posDelta has not come back with the row doors: a row now carries
	    its own range past every change KBS makes, so what it asks about is already where it stands.

	    ***** The last two questions arrived on 2026-08-04. ***** Until then the text was compared
	    through the row's DRAWN match, capped at 500 characters - so a GREP match of 2000 characters
	    was judged on its first 500, and neither a rewrite past the cap nor a change of length was
	    seen.

	    expectHash 0 means the search could not read that match, so nothing can be vouched for and
	    the answer is false - the safe answer: when in doubt, do not write. */
	bool MatchIsSameOccurrence(const UIDRef& storyRef, TextIndex start, TextIndex end,
		UID expectStoryUID, TextIndex expectStart, TextIndex expectEnd, uint64 expectHash);

	/** Let go of the module's static storage during InDesign's controlled shutdown, so every static
	    destructor at DLL unload finds nothing left to do.

	    What there is to let go of: the Find Format this file remembers for the replace's "has the
	    query changed" door (RememberFindFormat) - an AttributeBossList holding references to the
	    dialog's attributes, and the raw IDataBase* they belong to. Dropping the list releases those
	    attributes, which is database work, and doing it from a static destructor means doing it
	    after the application has torn down.

	    Called from KBSStartupShutdown::Shutdown, beside the same call on KBSHitMarker,
	    KBSBookScope and KBSResultModel. */
	void ShutdownCleanup();
}

#endif // __KBSSearchEngine_h__
