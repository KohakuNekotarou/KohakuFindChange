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

#include <boost/shared_ptr.hpp>	// HitCopy::paraOverrides - the shape ITextModelCmds::ApplyCmd takes
#include <map>
#include <memory>		// std::shared_ptr - the list's query copy (ListHeader::query)
#include <set>			// GetChapterStories
#include <vector>

struct KFCQueryCopy;	// KFCQueryCopy.h - Find/Change's query, copied in memory (the tier switch)
class AttributeBossList;	// AttributeBossList.h - a paragraph's own overrides, kept with a row's copy (HitCopy)

namespace KFCResultModel
{
	/** THE LIMIT, ONE NUMBER (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F9 - the
	    author's call): a list holds at most this many rows, and the panel draws every one of them. More than this is
	    not a list to walk and replace row by row - a Change All writes any number without one (KFC's for a book,
	    InDesign's own otherwise). Counted in ROWS.

	    Every road that adds rows stops at it - the search's walks, which say so ("Stopped at the 300 limit - narrow
	    the search, or use ..."), Search This Story / Document Again (the room the rest of the list leaves) and the
	    object search - so the counts the tree is built from (GetDisplayChapterCount and the rest) are the counts the
	    list holds, with no cap of their own. Not in KFCModelTypes.h: the UI half reads those counts (IKFCResults),
	    never this number. */
	const int32 kKFCCollectHitLimit = 300;

	/** THE OBJECT LISTS' LIMIT (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md N7 - the author's call: the object
	    search's limit raised; the number measured, M6 - 1000 objects: the search 4.6 s, the heaviest special operation 20 s, each
	    one Undo): an Object tab search's list and a Frames of These Matches list hold at most this many rows. Text lists keep
	    kKFCCollectHitLimit. */
	const int32 kKFCCollectObjectLimit = 1000;

	/** WHY A ROW'S REPLACE KEPT NO COPY (HitCopy::whyNot - docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md
	    C4): what the row's Restore from Copy says when it has none to put back. */
	enum CopyWhyNot
	{
		kCopyNone = 0,			// no Replace of the list has written the row (or one restored it since)
		kCopyKept,				// a copy is kept (HitCopy::store / story)
		kCopyOutsideMainText,	// its match is in a table cell, a footnote or a note - outside the story's primary thread
		kCopyTouchesHyperlink,	// what it copies holds a hyperlink source the paste would replace whole - the match
								// itself, one in the run-on rest, one at the match's edge (setting it again crashed
								// InDesign) - or touches a CROSS-REFERENCE source (its text is made by InDesign). Other
								// hyperlink sources are copied and put back as one link: HitCopy::links - C15
		kCopyRestHoldsObject,	// the rest of its paragraph, which the copy would run on over, holds an object or a marker
		kCopyHoldsAnchoredObject,	// its match holds an anchored or inline object - a copy would put it back under a new
								// UID (the author's call)
		kCopyFailed				// the copy could not be taken
	};

	/** THE COPY A ROW'S REPLACE TOOK OF THE TEXT IT WROTE OVER (KFCCopyStore - the copy-restore spec's C4 and C6),
	    taken just before the write (KFCReplaceEngine's ReplaceRowsNow) and given to the row once the write has gone
	    through (SetHitCopy): the match, or - a match holding a paragraph return - the match run on to just before the
	    return of the paragraph it ends in, so a paste puts that paragraph's own attributes back on the return it meets.
	    A Replace that sets paragraph formatting (C16) also keeps the formatting of the paragraph the match STARTS in -
	    its style and its own overrides - which a paste cannot carry (its first paragraph takes the attributes of the
	    one it is pasted into), put back on that paragraph after the paste. What Restore from Copy puts back
	    (KFCReplaceEngine::RestoreHits). Copied with the row - a tier, an Undo step, a backup - so the copy it names
	    comes back with it. */
	/** A HYPERLINK TEXT SOURCE THE COPY TOUCHED, and where it stood from the match's start when the copy was taken (the
	    copy-restore spec's C15): what Restore from Copy puts back where it stood - the paste makes a second link over a
	    source the Replace kept (measured live, H1 and H5), and cuts one the match crossed (H3). */
	struct HitLink
	{
		UID		source;		// the hyperlink text source
		int32	from;		// its start, from the match's start (negative: it began before the match)
		int32	to;			// its end, from the match's start (past the copy's length: it ran on after it)
		HitLink() : source(kInvalidUID), from(0), to(0) {}
	};

	struct HitCopy
	{
		uint32	store;		// the copy store's number (KFCCopyStore - 0 = none)
		UID		story;		// the copy's story in that store
		int32	rest;		// how far the copy runs on past the match, in text model positions (0 = the match alone)
		uint64	restHash;	// that run-on text as it read when the copy was taken (KFCSearchEngine::TextHash)
		int32	whyNot;		// CopyWhyNot - kCopyKept when the copy is kept
		UID		paraStyle;	// the starting paragraph's style when the Replace sets paragraph formatting (kInvalidUID = not kept)
		boost::shared_ptr<AttributeBossList> paraOverrides;	// ...and its own paragraph overrides then (nil = none)
		std::vector<HitLink> links;	// the hyperlink text sources the copy touched (C15)
		HitCopy() : store(0), story(kInvalidUID), rest(0), restHash(0), whyNot(kCopyNone), paraStyle(kInvalidUID) {}
	};

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
								// ("P<page>(<n>) overset"). An object row: no spread holds its item - an inline
								// in overset text (KFCObjectSearch::IsOnSpread).
		bool		isLocked;	// match sits on a locked layer or in a locked story -> the locator
								// gets " locked" and the row's Replace is greyed. InDesign can search
								// locked content but offers no way to change it ("Search Only"), so
								// the row is listed and jumpable but never replaced.
		bool		isHidden;	// match sits on a switched-off layer -> the locator gets " hidden".
								// Only reachable when the Find/Change dialog's "Include Hidden
								// Layers" is on, and then the text is composed and jumpable but
								// draws nothing, so the row has to say why the page looks empty.
		bool		onHiddenSpread;	// the frame the row names its page after (a text row's - the "+"
								// frame's for an overset match -, an object row's item) stands on a
								// HIDDEN SPREAD (the Pages panel's Hide Spread) -> the locator gets
								// " hidden spread" (the spec map's OBJ-23). Read as the search builds
								// the row: listed, but nothing of it can be shown or selected there.

		int32		fontGroup;	// which of its chapter's fontGroups (its STORY group) this hit belongs
		int32		fontGroupPos;	// to, and where it sits inside that group. -1 until the model groups
								// the chapter's rows (AppendChapter, and a chapter a search again lays out
								// anew - IndexRows in the .cpp), and on a row taken off the list (cleared).
								// An object row has no story, so no group (fontGroup -1): its fontGroupPos
								// is its place among the object rows its chapter shows (GetObjectRowHit).
								// The tree reads them to answer "who is my parent" and "which child am I"
								// without searching.

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
		bool		outlined;	// written as outlines (an object row's Special Operations > Create Outlines - KFCObjectOutline):
								// replaced too, and its item the outlines InDesign made (MarkItemOutlined); the row reads
								// "Outlined" where a replaced one reads "Changed"
		int32		special;	// THE SPECIAL OPERATION KFC last wrote the row's item with (an object row's Special Operations -
								// KFCObjectSpecial; a KFCSpecialOp, 0 = none): the row reads its word first (Fitted, Locked ...)
								// and keeps its eye on. The item is still the row's (its Replace stays offered) - but for Delete,
								// which takes it away and greys the row's Replace (replaced too). A Replace and Create Outlines
								// put their own word there (MarkItemReplaced, MarkItemOutlined clear it). MarkItemSpecial.
		bool		kept;		// THE ROW EYE (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md): on = the row
								// stays when its story or document is searched again, found or not (MergeKeptRows). A
								// replace turns it on (MarkHitReplaced, MarkItemReplaced); the eye's click turns it either
								// way (SetHitKept) - on a row KFC has WRITTEN only (replaced, outlined, or given a special
								// operation - the author's call: an eye on a row not written only kept a match
								// since deleted as Missing); its Restore from Copy turns it off. Copied whole with the
								// row - a tier, an Undo step, a backup.
		bool		cleared;	// TAKEN OFF THE LIST (Clear from Results - docs/superpowers/specs/2026-10-18-kfc-clear-from-results-design.md
								// K5): the row keeps its place in the chapter - the writes kept for an Undo name rows by their
								// place - but no story group and no object row's position holds it, so the tree does not draw it
								// and no count counts it. The list's alone (the document is not touched): an Undo's copy of the
								// row does not bring it back (ApplyRowStep keeps it), and a search again drops it for good.
		// WHERE THE MATCH'S TEXT STANDS (Narrow Down - docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md N5), read
		// as the search builds a text row (KFCSearchEngine's BuildHit): in a table cell, in a footnote, in an endnote's story.
		bool		inTable;
		bool		inFootnote;
		bool		inEndnote;
		bool		frameOnPasteboard;	// a TEXT row's frame - the match's, or for an overset match the "+" frame its page is
								// named after - stands on no page (Narrow Down's On Pasteboard; told by the class of the frame's
								// owner page, never by its "PB" spelling). An object row says so in onPasteboard.
		PMString	outlinedText;	// a text row written as outlines (RowDisplay::outlinedText): the characters it outlined
		UID			matchFrame;	// a TEXT row's match's own frame (the parcel's - IParcelList::GetParcelFrameUID), kInvalidUID when
								// it has none placed: what Frames of These Matches climbs to the frame the person selects
		UID			pageUID;	// the page the row names (a text row's frame's - the "+" frame's for an overset match -, an object
								// row's owner page), kInvalidUID for none: Narrow Down's Pages asks it of the range read in the
								// row's document (a parent page or a spread - the pasteboard - is never in a range read so)
		UID			frameLayer;	// a TEXT row's frame's document layer - the same frame pageUID is read off (the "+" frame for an
								// overset match) - kInvalidUID for none: Narrow Down's On Active Layer (KFCPageItemFacts::DocLayerOf,
								// read once a frame - FrameFacts). An object row's item is asked its own layer.
		ChangeOutcome outcome;	// why this row was NOT replaced (kOutcomeNone = it was, or was never
								// reached at all). The locator shows it as a word.
		// (No per-hit change count that lets the replace take an unedited story on trust and skip the
		// same-occurrence test: that fast path skips the POSITION test too, and a query retyped between
		// the search and the replace then rewrites occurrences the user has never seen. The chapter's
		// storyVersions are a door BESIDE that test, not instead of it.)
		PMString	accentFlag;	// the one word on this row drawn in the theme accent colour, or empty.
								// Kept OUT of locator so the cell can paint it separately; built by
								// BuildHitLocator alongside it. Only "refused" earns it - the other
								// flags stay in locator and read in the normal colour ("Missing" too,
								// first on the locator - the author's call).
		// --- what the replace wrote ---
		// The WHOLE text a replace wrote at the row's place, taken as it was written (not capped for drawing like
		// matchText): a replaced row that an edit has moved is looked for again by it
		// (KFCSearchEngine::LocateRow). Empty until the row is replaced.
		PMString	replacedText;
		// --- the copy its replace took (Restore from Copy - the copy-restore spec) ---
		HitCopy		copy;		// given once its Replace has gone through (SetHitCopy); taken off by its
								// Restore from Copy
		// The first characters of the match's STORY: what a story row of
		// the tree reads, taken when the hit is built - the search closes a chapter it opened as soon
		// as it has walked it, so the story cannot be read again when the tree draws.
		PMString	storyLead;
		int32		pageOrdinal;// this hit's place among the matches on its page, or 0 for "do not
								// show one". Kept as a number rather than only baked into the
								// locator string, so the locator can be rebuilt at any time.
		int32		storyOrdinal;	// this hit's place among its STORY's hits as the search found them - ordered by
								// textStart then (docs/superpowers/specs/2026-10-08-kfc-text-focus-jump-design.md
								// T14: a row among look-alikes is found again by its order). Set where the rows are
								// grouped (as fontGroup); a row a write moves keeps it.

		// --- an OBJECT row (the Object tab - docs/superpowers/specs/2026-10-09-kfc-object-search-design.md O8) ---
		// A row for a PAGE ITEM rather than a match in a story. storyUID stays kInvalidUID on it, so every text door - the
		// rows' text foci, the story groups, the replace's walks - passes it by; itemUID is the item, in the chapter's
		// database. The words on its locator: onPasteboard reads "pasteboard" in place of the page, then isMaster,
		// isGrouped, isInline and isAnchored (BuildHitLocator).
		UID			itemUID;
		bool		onPasteboard;
		bool		isMaster;
		bool		isGrouped;
		bool		isInline;		// an inline or above-line object in text
		bool		isAnchored;		// an anchored object with a custom position
		// Its FINGERPRINT (O12, KFCObjectSearch::Fingerprint): its own persistent data's hash and length, with what it holds
		// and a text frame's story's version - taken by the search, and again by each write of KFC's own that moves it: the
		// row's own Replace (MarkItemReplaced), or one of another row's (SetHitItemPrint). A row's Replace refuses an item that
		// no longer reads this way.
		uint64		itemPrint;
		uint32		itemPrintLength;
		// GONE WITH ITS DOCUMENT'S SESSION: the chapter's document was closed and opened again without the item - one
		// never saved. Its UID can then name only ANOTHER object: InDesign hands a never-saved item's UID to the next
		// item made after the reopen (measured - a book's chapter closed unsaved and opened again, a new rectangle
		// took the gone one's UID, and the old row's click selected it: case obj-book-uid-reused). Set where the
		// chapter is bound again (RebindChapterDoc), asked by every door that takes the row's item (RowItemIsThere,
		// IsHitItemGone).
		bool		itemGone;

		Hit() : pageIndex(-1), isOverset(false), isLocked(false), isHidden(false), onHiddenSpread(false),
				fontGroup(-1), fontGroupPos(-1), storyUID(kInvalidUID),
				textStart(kInvalidTextIndex), textEnd(kInvalidTextIndex), matchHash(0),
				replaced(false), outlined(false), special(0), kept(false), cleared(false), inTable(false),
				inFootnote(false), inEndnote(false), frameOnPasteboard(false), matchFrame(kInvalidUID), pageUID(kInvalidUID),
				frameLayer(kInvalidUID), outcome(kOutcomeNone),
				pageOrdinal(0), storyOrdinal(-1),
				itemUID(kInvalidUID), onPasteboard(false), isMaster(false), isGrouped(false), isInline(false),
				isAnchored(false), itemPrint(0), itemPrintLength(0), itemGone(false) {}
	};

	/** One STORY of a chapter's hits - one story row in the tree. The struct keeps the name it had when
	    this level held the fonts of Find Missing Glyphs (since removed).

	    hitIndices index the chapter's own hits vector, ASCENDING - the chapter's own (page) order, the order the
	    tree lists the story's rows in (GetFontGroupHit). */
	struct FontGroup
	{
		// (Its row's text - "ID260: first words..." - is made from its rows' storyLead as it is read: GetFontDisplay.)
		std::vector<int32>	hitIndices;	// this group's hits, in the chapter's own order
		// A STORY GROUP (the author's call). A Find/Change result groups its hits by story, the way KCM's
		// Story mode lists stories, and every group is one.
		UID					story;
		FontGroup() : story(kInvalidUID) {}
	};

	/** THE PART OF ONE STORY A SEARCH OVER PART OF A STORY WALKED (Search: To End of Story / Selection - the author's
	    idea: "keep where the search started with a text focus"), for Search This Story Again
	    (KFCSearchEngine::SearchStoryAgain): to the story's end from `start` (To End of Story), or [start, end)
	    (Selection) - the stretch InDesign's own scope walked, with the tables and footnotes inside it. With the text
	    just outside it: what finds its edges again after the story is edited. `start` and `end` are exact while the
	    story is at `version` (ITextModel::GetChangeCount - an Undo back to it included); the part's text focus
	    (KFCRowFoci) follows the edits made since. */
	struct SearchedRange
	{
		bool		walkable;	// false: InDesign's walk could not be taken again as one stretch (table cells
								// selected over more than one row) - the story is not searched again on its own
		TextIndex	start;
		TextIndex	end;		// Selection: the part's end; To End of Story: the story's end when it was taken
		uint32		version;
		PMString	before;		// up to kSearchedEdgeChars characters just before start; "" = start is the story's start
		PMString	after;		// Selection: as many just after end; "" = end is the story's end
		SearchedRange() : walkable(false), start(kInvalidTextIndex), end(kInvalidTextIndex), version(0) {}
	};
	enum { kSearchedEdgeChars = 8 };

	/** One chapter that holds at least one hit. */
	struct Chapter
	{
		PMString				name;	// the chapter's display name (its file name)
		UIDRef					docRef;	// current binding (RebindChapterDoc after a reopen)
		IDFile					file;	// the chapter's .indd (to reopen a closed chapter)
		IDFile					closedFile;	// the file its document was closed from (EmptyChapter): read only by the
										// heading's Search These Results Again, to find the document OPEN again - never
										// reopened, never rebound by it (LIFE-01; a document-scope chapter has no `file`)
		std::vector<Hit>		hits;
		std::vector<FontGroup>	fontGroups;	// its story groups - every text hit is in one (an object row in none)
		// Each story's version (ITextModel::GetChangeCount) where KFC last knew the rows in it to stand
		// - see GetStoryVersion.
		std::map<UID, uint32>	storyVersions;
		// A search over part of a story: the part of each story holding a row (SearchedRange). Empty otherwise. What
		// the story row's and the tier heading's searches again walk again (the author: "the same conditions as the
		// first search") - so kept through them, and recorded again where it is found.
		std::map<UID, SearchedRange>	searchedRanges;
		// A SEARCH: STORY'S STORY - the one story it searched (its first match's, KFCSearchEngine's CollectTargets):
		// what the tier heading's search again walks again ("a Story search, that story alone" - the author), as the
		// story row walks its own. kInvalidUID otherwise.
		UID						searchedStory;
		// SEARCH TEXT IN THESE OBJECTS' OBJECTS (docs/superpowers/specs/2026-10-21-kfc-text-in-object-results-design.md
		// T6): the page items whose text it searched in this document - what its searches again make the stretches from
		// again (KFCObjectText::Collect). An object its document came back without is let go - kInvalidUID in its place
		// (RebindChapterDoc): a search again counts it gone, and never takes the next item made for it. Empty for any
		// other search.
		std::vector<UID>		searchedObjects;
		// SEARCHED AGAIN AND NOTHING FOUND (Search This Document Again, and Search This Story Again over its last story
		// with rows - the author's calls): shown with no rows, (0/0), so it can be searched again after the next edit.
		// So is a document whose objects held no match for Search Text in These Objects (FileChapter): it keeps them
		// for its search again (searchedObjects). A chapter with no rows is otherwise not shown - a search lists only
		// documents that hold a match, and CloseChapter empties a closed one in place (ChapterShown).
		bool					shownEmpty;
		// ITS DOCUMENT CLOSED SINCE IT WAS LAST BOUND (ChapterDocumentClosing): the next document bound to it
		// (RebindChapterDoc) is a new session of its file - the one bind that can find an object row's item gone for
		// good (Hit::itemGone), and that lets go of the Search Text in These Objects' objects it came back without
		// (searchedObjects). A bind with no close before it (every object write's check binds again) finds an item
		// missing in this session, deleted - which an Undo can bring back.
		bool					closedSinceBound;

		// (No "not reached" mark per chapter: rows of several documents replaced together are written in one
		// sequence, which a chapter not written whole takes back entire - every chapter is written or none is.)

		Chapter() : searchedStory(kInvalidUID), shownEmpty(false), closedSinceBound(false) {}
	};


	/** Append one chapter to the model - the ONE way results get in. The search clears the model and then appends each
	    chapter as it finishes. A search appends only the chapters that hold a match - it lists only the documents that
	    hold one - but for Search Text in These Objects, which appends a document whose objects hold none too, shown
	    empty, so its search again knows them (Chapter::shownEmpty - as Search This Document Again leaves one).

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

	/** Did these results come from an ALL DOCUMENTS search? Recorded on the results as IsFromBook is, beside the scope
	    (SetSearchScope) - which says All Documents for a list of its own (kScopeAllDocuments) but not for a list Search Text
	    in These Objects made from one (kScopeInObjects): the documents a list was searched over, whatever its scope
	    says. The tree draws such a list's document rows CLOSED and says which document has no window; its searches again
	    say its documents' count. Cleared by Clear(), set beside SetFromBook. */
	void SetFromAllDocuments(bool fromAllDocuments);
	bool IsFromAllDocuments();

	/** WHICH Search: THESE RESULTS WERE SEARCHED WITH. KFC follows Edit > Find/Change's Search:
	    (KFCSearchEngine::CurrentSearchScope). The list records which one it came from,
	    the way it records IsFromBook, so a Search: changed afterwards does not change how the rows already on
	    screen are shown: All Documents is drawn as IsFromAllDocuments says;
	    Story / To End of Story / Selection search one document and are drawn as Document is. kScopeBook =
	    Book Scope was on (Search: is then Document - any other is refused before the search). kScopeInObjects = a list
	    Search Text in These Objects made: no Search: of its own - its objects' text (Chapter::searchedObjects), its
	    book and All Documents as IsFromBook / IsFromAllDocuments say.
	    Recorded beside SetFromBook, after the commit point; Clear() puts it back to kScopeDocument. */
	// (SearchScopeKind: KFCModelTypes.h.)
	void SetSearchScope(SearchScopeKind scope);
	SearchScopeKind GetSearchScope();

	/** A NARROWED LIST'S STEPS (Narrow Down - the narrow-down spec's N4, N5): its conditions in the order they were
	    taken - kept with the list as its Search: is, so a tier's heading names them and its search again searches with
	    them; recorded beside SetSearchScope, after the commit point. Empty for every list no Narrow Down made. */
	void SetNarrowSteps(const std::vector<KFCNarrowStep>& steps);
	std::vector<KFCNarrowStep> GetNarrowSteps();
	/** The steps in words, as a tier's heading draws them after its name and the status line names them: "In Tables /
	    Pages 1-5" - empty for none. */
	PMString NarrowWords();
	/** ARE THE LIST'S ROWS OBJECT ROWS - an Object tab search's, or a Frames of These Matches list's? The heading's menu greys
	    a condition that only text rows can meet, and Search Text in These Objects over text rows - which
	    KFCSearchEngine::SearchTextInObjects refuses too. */
	bool ListHoldsObjects();
	/** Is the list a FRAMES OF THESE MATCHES list - object rows found by a text search, not by the Object tab's (its
	    Replace writes with the Object tab's Change Object Format alone - KFCObjectReplace, the narrow-down
	    spec's N6)? */
	bool ListIsFrames();
	/** The list's limit of rows: kKFCCollectObjectLimit for a list of object rows (ListHoldsObjects), kKFCCollectHitLimit for
	    any other - what a search's walks and its searches again take the room from. */
	int32 ListLimit();
	/** ...the same rule for a list whose header is not written yet: a Narrow Down's over part of a document, filed
	    after its walks (KFCSearchEngine.cpp's SearchPartsAgain). objectRows = its rows are object rows (a Frames
	    step's). */
	int32 ListLimitFor(bool objectRows);

	/** A DOCUMENT OF THE LIST WAS CLOSED (the author's calls: only its rows go - an All Documents list's, and every
	    list's, its heading staying while it is a tier - KFCCloseDocResponder). Its chapter is emptied - rows, groups,
	    story versions - and unbound (no docRef, no file), but it KEEPS ITS PLACE: KFCUndoFollow names rows by (chapter,
	    row), and closing up the gap would renumber every chapter after it and cut those off from their Undo. A chapter
	    with no rows is not shown (GetShownChapter) and holds nothing to count or write, so nothing else has to know it
	    is there. `closedFile` = the closing document's file (IDataBase::GetSysFile - nil for one never saved), kept on
	    the emptied chapter as Chapter::closedFile. */
	void CloseChapter(int32 chapterIdx, const IDFile* closedFile);

	/** CloseChapter's work on a chapter that is not in the model - one of a kept whole result set
	    (KFCUndoFollow::ForgetDocument, KFCTierStore::ForgetDocument): its rows, groups, versions and searched parts
	    gone, no docRef, no file - and `closedFile` kept as Chapter::closedFile (nil keeps what an earlier close
	    left). */
	void EmptyChapter(Chapter& chapter, const IDFile* closedFile = nil);

	/** The nth chapter the tree SHOWS - the chapters with rows, and one searched again with none (Chapter::shownEmpty) -
	    as a chapter index; -1 = none. With no emptied chapter the nth shown is chapter nth. */
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

	    AND THAT WOULD BE WRITTEN. The replacing walk does not read each match's text against its row; the
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

	/** THE QUERY THE LIST WAS SEARCHED WITH, COPIED (KFCQueryCopy - docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md
	    3-3): set by the search after its commit point, beside the walk signature; null for a list no search made (a query
	    run's, Change All's) and after Clear(). Shared, not copied, by every snapshot of the list (an immutable copy) - what
	    the tier switch states back into Find/Change when the list is switched to (KFCTierStore::SwitchTo). */
	void SetQueryCopy(const std::shared_ptr<const KFCQueryCopy>& copy);
	std::shared_ptr<const KFCQueryCopy> GetQueryCopy();

	/** The name of the book these results came from - shown on the tree's BOOK row, which is how
	    the panel says which book was searched. Set beside SetFromBook; the file NAME only, because
	    a full path does not fit a palette. Empty for a document-scope search, and cleared by
	    Clear() so a stale name can never outlive the results it belongs to. */
	void SetBookName(const PMString& name);
	PMString GetBookName();

	/** Application-shutdown cleanup: release the vectors' and the strings' storage, no UI. */
	void ShutdownCleanup();

	/** The number of chapters in the model - a chapter CloseChapter emptied, and one searched again with no match
	    (Chapter::shownEmpty), included: indexes run 0 .. this - 1. (What the tree shows: GetDisplayChapterCount.) */
	int32 GetChapterCount();

	/** The number of hits under chapter 'chapterIdx' - every row it holds, one taken off the list (Hit::cleared) too: the range
	    of its row indices. */
	int32 GetHitCount(int32 chapterIdx);

	/** Was this row taken off the list (Hit::cleared)? false for an index out of range. */
	bool IsHitCleared(int32 chapterIdx, int32 hitIdx);

	/** Was this row's match in overset text when it was found or last read (Hit::isOverset)? false for an index out of range.
	    (A text row's Create Outlines is greyed over one - InDesign refuses overset text, the text-outlines spec's M7.) */
	bool IsHitOverset(int32 chapterIdx, int32 hitIdx);

	/** The total number of hits across ALL chapters the list shows - none taken off it - for the status summary, the book
	    row's count and the list's limit. */
	int32 GetTotalHitCount();

	/** The number of chapters the tree shows - those with a row, and one searched again with none (Chapter::shownEmpty):
	    the tree root's child count. One CloseChapter emptied is not shown - so the nth of them is GetShownChapter(nth),
	    not chapter nth. */
	int32 GetDisplayChapterCount();

	/** The rows of chapter 'chapterIdx' the list still shows - none taken off it (Clear from Results): what the tree
	    shows under the chapter, what its row counts, and the room a search again takes the list's limit with. */
	int32 GetDisplayHitCount(int32 chapterIdx);

	/** A chapter node's display: its name and its hit count. false = index out of range. */
	bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount);

	/** How many STORY rows this chapter shows - every story group (each holds a row). ("Font" in the names below is
	    the level's old name - it held the fonts of Find Missing Glyphs, since removed.) */
	int32 GetDisplayFontCount(int32 chapterIdx);

	/** How many rows the tree shows under one story group - every row it holds. 0 for an index out of range. */
	int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx);

	/** A story row's display: its text ("ID260: first words...") and its hit count - what the row holds.
	    false = index out of range. */
	bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount);

	/** HOW MANY OF THEM THIS LIST HAS REPLACED (Hit::replaced) - the R of a branch row's "(R/M)" (the author's call:
	    a document row and a story row say how many of their hits have been replaced, "1/3"). Counted like M: every hit
	    the row holds. An Undo of a replace puts the hit back as found, so the count follows it. 0 out of range. */
	int32 GetChapterReplacedCount(int32 chapterIdx);
	int32 GetFontReplacedCount(int32 chapterIdx, int32 fontIdx);
	/** ...and of every chapter - the book row's. */
	int32 GetTotalReplacedCount();

	/** The 'nth' hit of one font group, as an index into the CHAPTER's hits - the translation the
	    tree needs, since a node names its hit by the chapter-wide index throughout. -1 = out of
	    range. */
	int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth);

	/** Which story group a hit belongs to, and where it sits inside that group: the tree's "who is my parent" and
	    "which child am I". -1 for an index out of range and for a row taken off the list (Hit::cleared); the group is
	    -1 for an object row too, which has no story, and its position is its place among the object rows its chapter
	    shows (GetObjectRowHit). Every text row the list shows has a group. */
	int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx);
	int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx);

	/** Is this one of the chapter's story groups? Every group is one, so this is the
	    index's range (the tree asks it of a node before using it). */
	bool IsStoryGroup(int32 chapterIdx, int32 groupIdx);

	/** The story a story group stands for (FontGroup::story); kInvalidUID for an index out of range. */
	UID GetGroupStory(int32 chapterIdx, int32 groupIdx);

	// (RowDisplay - everything a hit row needs to draw itself, see GetHitRow: KFCModelTypes.h.)

	/** One row's worth of everything, in a single call.
	    Its own getter so that a row does not ask the model once for each thing it draws - the
	    display strings, the replaced flag and the accent word - each one walking to the same
	    hit to hand back one part of it. Only the rows on screen are ever laid out, so that would
	    not be expensive; it is simply several questions where the row has one.
	    @return false for an index out of range, leaving out untouched. */
	bool GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out);

	/** THE ROW EYE'S CLICK (KFCResultEyeEH, through IKFCResults::SetHitKept): the row's eye on or off. Not a change to
	    the document - no Undo step, no backup. false = no such row, or an eye turned on on a row KFC has not written
	    (replaced, outlined, or given a special operation - the author's call). */
	bool SetHitKept(int32 chapterIdx, int32 hitIdx, bool kept);

	/** A ROW THAT STAYS WHEN ITS LIST IS SEARCHED AGAIN: its eye on (Hit::kept), and not taken off the list
	    (Hit::cleared - Clear from Results). The one test the searches again carry rows by (ReplaceStoryRows,
	    ReplaceChapterRows, and KFCSearchEngine's Search These Results Again) and CountKeptRows counts them by. */
	bool StaysKept(const Hit& hit);

	/** How many rows of a chapter stay kept (StaysKept) - of one story, or of the whole chapter (story = kInvalidUID):
	    the room a search again leaves them (SearchStoryAgain, SearchDocumentAgain). */
	int32 CountKeptRows(int32 chapterIdx, UID story);

	/** CLEAR FROM RESULTS (docs/superpowers/specs/2026-10-18-kfc-clear-from-results-design.md K3, K5): the rows taken off the
	    list (Hit::cleared - their eyes off with them) and each of their chapters' story groups and object rows' positions
	    built again around them; no row moves, so whatever names a row by its place still names it. Not backed up and no
	    Undo step: the document is not touched. Answers how many rows were taken off (one already off counts nothing). */
	int32 ClearRows(const KFCRowsByChapter& rows);

	/** A chapter with no story groups - an object list's: the nth of its rows the tree shows, as an index into its hits;
	    -1 = none. (Its rows' positions there are their Hit::fontGroupPos.) */
	int32 GetObjectRowHit(int32 chapterIdx, int32 nth);

	// (No script property hands the result set out as text: the regression suite reads the panel
	//  through KIDMCP.)

	/** A hit's jump anchors: the chapter's document / file and the match's story + text range.
	    false = index out of range. */
	bool GetHitLocation(int32 chapterIdx, int32 hitIdx,
		UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd);

	/** Rebind a chapter's document reference: after a closed chapter is reopened - by a jump or a
	    replace - later calls must use the live database, not the dead one from search time. The first bind after its
	    document closed (Chapter::closedSinceBound) marks the object rows whose items the document came back without as
	    gone for good (Hit::itemGone), and lets go of the Search Text in These Objects' objects it came back without
	    (Chapter::searchedObjects - kInvalidUID in their place). */
	void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef);

	/** A DOCUMENT CLOSING (KFCCloseDocResponder, kBeforeCloseDoc - every close): the chapters bound to it take note
	    (Chapter::closedSinceBound). The UIDRef is compared, never dereferenced. */
	void ChapterDocumentClosing(const UIDRef& closingDocRef);

	/** A DOCUMENT SAVED AS ANOTHER FILE (KFCSaveAsDocResponder - the author's call, the spec map's LIFE-14):
	    the chapter of a document-scope list bound to it takes its new name - the row's, and a status line's that names it
	    (a close's). A book's list keeps its names: its rows stand for the book's chapters, still the files the book lists.
	    Asked of the list (IsFromBook), as KFCCloseDocResponder asks it - not of a chapter's file, which a book's entry can
	    lack too (KFCBookScope::ChapterDoc). The UIDRef is compared, never dereferenced (IsDocStillOpen's rule). Returns
	    how many it renamed. */
	int32 RenameDocumentChapters(const UIDRef& docRef, const PMString& newName);

	/** A story's VERSION where KFC last knew the chapter's rows in it to stand:
	    ITextModel::GetChangeCount (KFCSearchEngine::ReadStoryVersion), taken by the search
	    for every story holding a hit, and taken again by each change KFC makes there - only while the
	    story was still at the version recorded, so a story that moved without KFC is never written off as
	    "known" (KFCReplaceEngine). False = nothing recorded (or the index is out of range), which the
	    replace reads as "cannot vouch for this story" and does not write to. */
	bool GetStoryVersion(int32 chapterIdx, UID story, uint32& outVersion);
	void SetStoryVersion(int32 chapterIdx, UID story, uint32 version);

	/** The stories holding a row of the chapter - KFCUndoFollow reads each one's version before a write of the
	    chapter's rows (the stories the write can move). Empty for an index out of range. */
	void GetChapterStories(int32 chapterIdx, std::set<UID>& outStories);

	/** The part of the story a search over part of a story walked (SearchedRange) - false when none is recorded (a
	    search of whole stories, or an index out of range). */
	bool GetSearchedRange(int32 chapterIdx, UID story, SearchedRange& outRange);
	/** Recorded again by Search This Story Again, where it found the part and the version it walked it at. */
	void SetSearchedRange(int32 chapterIdx, UID story, const SearchedRange& range);
	/** The stories of the chapter with a searched part - KFCRowFoci gives each part its focus. */
	void GetSearchedRangeStories(int32 chapterIdx, std::vector<UID>& outStories);
	/** The story a Search: Story search searched (Chapter::searchedStory) - kInvalidUID for any other search, or an index
	    out of range. */
	UID GetSearchedStory(int32 chapterIdx);
	/** The objects a Search Text in These Objects searched in the chapter (Chapter::searchedObjects) - false for an index out
	    of range; out empty for any other search. */
	bool GetChapterSearchedObjects(int32 chapterIdx, std::vector<UID>& out);

	/** A hit's row flags: already replaced, and locked - both mean "this row's Replace is greyed", for different
	    reasons. false = index out of range. */
	bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outReplaced, bool& outLocked);

	/** A row's place among its story's rows as the search found them (Hit::storyOrdinal); -1 = no such row. */
	int32 GetHitStoryOrdinal(int32 chapterIdx, int32 hitIdx);

	/** The page item an OBJECT row stands for (Hit::itemUID); kInvalidUID for a text row or an index out of range. */
	UID GetHitItem(int32 chapterIdx, int32 hitIdx);

	/** An object row whose item its document came back without (Hit::itemGone) - false for any other row or an
	    index out of range. */
	bool IsHitItemGone(int32 chapterIdx, int32 hitIdx);

	/** AN OBJECT ROW'S ITEM, STILL IN ITS DOCUMENT `db`: a page item there (KFCPageItemFacts::ItemIsThere) and not
	    gone with its document's session (Hit::itemGone - by then its UID may name another object). The one question
	    the doors that take a row's item ask: the object Replace's, Create Outlines' and Special Operations' checks. */
	bool RowItemIsThere(int32 chapterIdx, int32 hitIdx, IDataBase* db);

	/** An object row's fingerprint (Hit::itemPrint / itemPrintLength). false for a text row or an index out of range. */
	bool GetHitItemPrint(int32 chapterIdx, int32 hitIdx, uint64& outPrint, uint32& outLength);

	/** An object row KFC has replaced: marked replaced - "Changed", its Replace greyed - with the
	    item's fingerprint as the write left it. Backed up like every change a write makes (BeginRowBackup), so an Undo
	    puts the row and its fingerprint back together. */
	void MarkItemReplaced(int32 chapterIdx, int32 hitIdx, uint64 newPrint, uint32 newLength);

	/** AN OBJECT ROW OUTLINED (KFCObjectOutline - Special Operations > Create Outlines): replaced, outlined, its eye on
	    (the row eye), and its item the outlines InDesign made with their fingerprint (kInvalidUID when the command named
	    none). Backed up like every change a write makes (BeginRowBackup), so an Undo puts the row - its frame - back. */
	void MarkItemOutlined(int32 chapterIdx, int32 hitIdx, UID newItem, uint64 newPrint, uint32 newLength);

	/** AN OBJECT ROW A SPECIAL OPERATION WROTE (KFCObjectSpecial - docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md
	    E7): its special mark (op, a KFCSpecialOp - the row reads its word first), its eye on, its outcome cleared (the
	    write has just found the item there). place = the item's place read after the write (KFCObjectSearch::ReadItemPlace -
	    a move or a turn can take it to another page, Lock and Hide change its words) - the row's page, flags and locator
	    from it, its place on the page dropped when the page changed (the list is not ordered again); and its fingerprint
	    as the write left it. place = nil: the write took the item away (Delete) - replaced too, its Replace greyed. Backed up
	    like every change a write makes (BeginRowBackup), so an Undo puts the row back. */
	void MarkItemSpecial(int32 chapterIdx, int32 hitIdx, int32 op, uint64 newPrint, uint32 newLength, const Hit* place);

	/** An object row a write of KFC's own did NOT replace, but whose item's fingerprint the write moved - the text frame of
	    the story an inline written hangs in, the group holding an item written (KFCObjectSearch::WriteReach): its fingerprint
	    as the write left it, the row otherwise as it was. Backed up as MarkItemReplaced is, so an Undo puts the print back
	    with the item. */
	void SetHitItemPrint(int32 chapterIdx, int32 hitIdx, uint64 newPrint, uint32 newLength);

	/** The two flags that say the match IS in the document but is out of the user's reach there:
	    LOCKED (a locked layer or a locked story - InDesign can search locked content but offers no
	    way to change it) and HIDDEN (a switched-off layer - the text is composed and can be jumped
	    to, but draws nothing). false = index out of range.

	    Separate from GetHitFlags, which answers "can this row be replaced?". These two
	    answer "can the user work on this match where it is?" - the question a click asks before it
	    selects (KFCJump's SelectMatchOnClick). isLocked appears in both because it is a fact
	    that bears on both questions; the DECISIONS made from it stay one per place. */
	bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden);

	/** A row's outcome (kOutcomeNone for an out-of-range index). */
	ChangeOutcome GetHitOutcome(int32 chapterIdx, int32 hitIdx);

	/** The whole text the replace wrote at the row's place (Hit::replacedText). The replace reads it from the
	    document - the model reads no text - and hands it over here. */
	void SetHitWrittenText(int32 chapterIdx, int32 hitIdx, const PMString& writtenText);

	/** A story's first words, read again after a write (KFCSearchEngine::RereadStoryLead): every row of the
	    chapter in that story takes them as its storyLead - each one backed up first (a rollback, an Undo and a Redo put
	    them back with the row) - and the story row's text follows (GetFontDisplay). True when a row changed. */
	bool SetStoryLead(int32 chapterIdx, UID storyUID, const PMString& lead);
	/** False when the row is not replaced, or the index is out of range. */
	bool GetHitWrittenText(int32 chapterIdx, int32 hitIdx, PMString& outWrittenText);

	/** THE COPY A ROW'S REPLACE TOOK (HitCopy - KFCReplaceEngine's ReplaceRowsNow, once the write has gone through): backed
	    up like every change a write makes, so the Undo of that Replace puts the row's earlier copy back with it. */
	void SetHitCopy(int32 chapterIdx, int32 hitIdx, const HitCopy& copy);
	/** The row's copy (HitCopy). false = index out of range. */
	bool GetHitCopy(int32 chapterIdx, int32 hitIdx, HitCopy& outCopy);

	/** A ROW ITS RESTORE FROM COPY PUT BACK (KFCReplaceEngine::RestoreHits - the copy-restore spec's C10): no longer
	    replaced (its Replace offered again), its eye off (a row not written keeps none - Hit::kept), what it wrote and
	    its copy forgotten, its place the text put back - [start, end) of storyUID - and nothing said about it
	    (kOutcomeNone). Backed up first, so an Undo puts the replaced row back, copy and all. The caller reads its line
	    again (RereadRowText). */
	void MarkHitRestored(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end);

	/** A TEXT ROW WRITTEN AS OUTLINES (KFCReplaceEngine::OutlineHits - docs/superpowers/specs/2026-10-18-kfc-text-outlines-design.md
	    X6): replaced and outlined (its Replace greyed, "Outlined" on its locator), its eye on, its place [start, end) of storyUID
	    - the inline marks that took the match's place - and `original`, the characters it outlined, kept for the row to draw
	    (Hit::outlinedText); `written`, the text [start, end) holds now (the marks, and the spaces between them), as a Replace's
	    row keeps what it wrote (Hit::replacedText): what a later write asks to carry the row (RowStillStands). Backed up first,
	    as every change a write makes to a row, so an Undo puts the match back. The caller reads its line again
	    (RereadRowText). */
	void MarkHitOutlined(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end, const PMString& original,
		const PMString& written);

	/** A chapter's document binding and file. The replace pass works chapter at a time, so it
	    needs this without going through a hit. false = index out of range. */
	bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile);

	/** The chapter BOUND to this document (Chapter::docRef - compared, never followed: IsDocStillOpen's rule), the
	    first if several are; -1 = none. How a closing document finds its chapter (KFCCloseDocResponder,
	    KFCUndoFollow::ForgetDocument). By its binding, not its file - KFCBookScope::FindOpenChapterDoc asks that. */
	int32 GetDocumentChapter(const UIDRef& docRef);

	/** The file a chapter's document was closed from (Chapter::closedFile - empty for a chapter never closed, or one
	    whose document was never saved). false = index out of range. */
	bool GetChapterClosedFile(int32 chapterIdx, IDFile& outFile);

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

	        [Missing ]P<page>(<n>) overset hidden spread hidden locked   -> hit.locator
	        refused                                                    -> hit.accentFlag, drawn after it, accent colour

	    The page ordinal comes from hit.pageOrdinal (0 = leave it out). The flags are separated by
	    spaces and spelled out IN FULL rather than clipped, because each one explains a row the user
	    cannot act on. "+" is deliberately NOT the separator: InDesign's own overset marker is a "+",
	    so "P5+locked" reads as "page 5, overset".

	    The flags STACK - "Missing P4(1) locked" is a locked row that has since been jumped to and
	    found changed (Missing leads the locator - the author's call). Only the words that come
	    from the row's outcome exclude each other, being values of one field: missing and refused. */
	void BuildHitLocator(Hit& hit);

	/** One chapter's hits put in PAGE order - overset matches (pageIndex -1) to the end, a stable sort, so the hits on
	    one page keep the order they stand in - and numbered within their pages (NumberHitsWithinPages). One ordering
	    for every list: a search's finishing pass, the text search's and the object search's alike, and a search again. */
	void OrderHitsByPage(std::vector<Hit>& hits);

	/** SEARCH THIS STORY AGAIN's change to the list (KFCSearchEngine::SearchStoryAgain): the chapter's rows in that story
	    are replaced by storyHits - the story walked again, in walk order, taken (swapped out) - which stand where the
	    story's first row stood; the chapter is put in page order again, its story groups and their numbers rebuilt
	    (BuildFontGroups, Hit::storyOrdinal), and the story's version recorded (storyVersion - the story as KFC knows it
	    now). The rows' places in the list change, so whatever indexes them is let go: a kept write's rows (the layout
	    generation moves - KFCUndoFollow drops it, UNDO-14), the row backup, the chapter's text foci (attached again,
	    KFCRowFoci). With no row left that the list shows - a row taken off it (Hit::cleared) does not count - it is
	    shown empty (Chapter::shownEmpty), as ReplaceChapterRows leaves one. Returns the story's row count now; -1 = no
	    such chapter. */
	int32 ReplaceStoryRows(int32 chapterIdx, UID story, std::vector<Hit>& storyHits, uint32 storyVersion,
		int32* outStayed = nil);

	/** SEARCH THIS DOCUMENT AGAIN's change to the list (KFCSearchEngine::SearchDocumentAgain - the author's call):
	    every row of the chapter replaced by docHits - the document walked again, taken (swapped out) - put in page
	    order, its story groups and their numbers rebuilt, its stories' versions replaced by storyVersions (taken). What
	    a search over part of the document searched stays - its searched parts, recorded again by the caller where they
	    stand, and its story (Chapter::searchedStory): the tier heading's search again walks them again (the author:
	    "the same conditions as the first search"). With no row left the chapter is shown empty (Chapter::shownEmpty).
	    The rows' places in the list change, so what indexes them is let go, as ReplaceStoryRows lets it go. Returns the
	    chapter's row count now; -1 = no such chapter. */
	int32 ReplaceChapterRows(int32 chapterIdx, std::vector<Hit>& docHits, std::map<UID, uint32>& storyVersions,
		int32* outStayed = nil);

	/** THE ROW EYE'S KEEPING (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-2): `found` = a search
	    again's rows (the walk's, in its order), `kept` = rows of the list it replaces whose eye is on, at the places their
	    foci followed them to. On return `found` holds both and `kept` is empty; the answer = how many kept rows stayed
	    beside the found ones. The caller orders them (OrderHitsByPage) and indexes them.
	    - a kept REPLACED row stays as it is, and the found rows over what it wrote go (7): in its story, overlapping it;
	      an object row - the same page item;
	    - a kept row not replaced that the walk found again - the same story, start and end, or the same page item - is
	      the found row, its eye on (6);
	    - every other kept row stays as it is (5): just before its story's first found row that starts after it, else
	      just after its story's last one, else at the end (OrderHitsByPage then puts it on its recorded page). */
	int32 MergeKeptRows(std::vector<Hit>& found, std::vector<Hit>& kept);

	/** THE ROW EYE, SEARCH THESE RESULTS AGAIN (KFCSearchEngine::RunSearch): `kept` - rows of the list as it was whose
	    eye is on, all of one document - carried into chapter chapterIdx of the new list (MergeKeptRows), in page order,
	    indexed; a story of theirs the walk recorded no version for takes the one in `versions` (the old chapter's - its
	    foci were followed just before). The answer = how many stayed beside the found rows. No focus is attached here
	    (the search attaches every open chapter's after). */
	int32 CarryKeptRows(int32 chapterIdx, std::vector<Hit>& kept, const std::map<UID, uint32>& versions);

	/** The chapters put in this order - `order` names every chapter once by its index now. Only for a list made a moment
	    ago that nothing names by chapter yet (RunSearch's carry: no focus, no Undo step, no tier). false = `order` is not
	    such a list - nothing moved. */
	bool OrderChapters(const std::vector<int32>& order);

	/** Turn the two break characters into the marks InDesign itself draws with Show Hidden
	    Characters on - a pilcrow for a paragraph end (CR), a return arrow for a forced line break
	    (LF) - in place. A string holding none of the characters below is left exactly as it came.

	    THE one definition, called where the document's text is shown: the panel's cell
	    (KFCColorTextView - where it draws, and where it measures), a story row's first words
	    (GetFontDisplay) and the "Preview Text:" line (KFCResultListWidgetMgr). A match is carried
	    WHOLE however many paragraphs it spans, and a raw break draws with no width - the paragraphs
	    either side of it run together and read as one piece of text - so it has to be marked.

	    DISPLAY ONLY. Never applied to a row's own texts. Those are what every door
	    compares against the document (KFCSearchEngine::RowReadsAsFound - the hash taken over the
	    stored range, and the line around it), and a marked-up copy would fail every comparison - a
	    click on a row would answer "the text is no longer here" about text that had not moved at all.
	    Callers mark a COPY, at the moment they draw it. (A story row's label is marked as it is read -
	    GetFontDisplay - and nothing compares it.)

	    It also DROPS the characters an object stands on - footnote / endnote references, anchors,
	    a table's per-row continuations, page number markers (the author's call: they drew as a box).
	    A TABLE'S ANCHOR BECOMES U+25A6 - KCM's table sign, the user's request: `a<table>b` reads
	    `a<sign>b` on a hit row, a story row and the "Preview Text:" alike. */
	void MarkUpBreaksForDisplay(PMString& s);

	/** A character a row does not show, a hit row and a story row alike (MarkUpBreaksForDisplay, StoryLeadText): a mark an
	    object or a piece of formatting stands on, with no glyph of its own - every control character but the breaks, the
	    tab, a table's anchor and the non-Roman special glyph; U+FEFF, U+FFFC and an index marker. */
	bool IsMarkerNotShown(UTF32TextChar c);

	/** Record why a hit was not replaced. Rebuilds the row's locator so the word shows up at once,
	    and the row is not offered again (IsWorkOutcome).
	    Set by the replace pass, and by the jump when it finds the text at a row's position is no
	    longer the text the row describes; taken off again (kOutcomeNone) when a row said Missing is
	    found after all (KFCSearchEngine::LocateRow). Ignored for a hit that WAS replaced. */
	void SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome);

	/** Start remembering every row a replace changes, so a write taken back can be put back on the panel too.
	    Only the rows actually written to are copied - one copy each, taken just before the change -
	    so the cost follows the work done rather than the size of the result set.

	    A replace that fails rolls the TEXT back through its command sequence (a row's Replace ends its
	    plain one with the error state raised; rows of several documents, and a preview, abort their abortable one). That leaves the panel
	    describing replacements that no longer exist, so the two have to be put back together: this is
	    the panel's half.

	    Exactly one of RollBackRows (the write was taken back) or TakeRowBackup (it committed - the copies become the
	    "before" of its Undo) must follow, or the copies stay alive until the next write. EVERY WRITE OF ROWS STARTS
	    IT THROUGH KFCUndoFollow::StepRecorder, which ends it one way or the other. The story versions a write records
	    (SetStoryVersion) are copied the same way. */
	void BeginRowBackup();

	/** Put every remembered row back the way it was and stop remembering. A row is remembered once, as
	    the run found it, however many times the run changes it.

	    The rows' text foci are NOT put back here (ApplyRowStep puts back its rows' foci; this does not): a
	    rolled-back write moved them with the text and the rollback leaves them where it moved them, so a
	    caller that wrote text puts the foci of the stories it wrote back on their rows itself
	    (KFCRowFoci::ReanchorStory - every row of those stories, the ones never copied aside as well). */
	void RollBackRows();

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

	/** Stop remembering and hand the copies over: every row and recorded version changed since
	    BeginRowBackup, as it was when it was first changed and as it stands now. */
	void TakeRowBackup(RowStep& out);

	/** Put a step's rows and recorded versions back - its "after" copies (after = true) or its "before"
	    ones. A row whose index is out of range is passed over. */
	void ApplyRowStep(const RowStep& step, bool after);

	/** THE LIST'S HEADER - what a result set says about itself beside its chapters, each field written by the setter
	    named beside it. ONE STRUCT, so each place that takes all of it takes it in one statement - Clear() puts back a
	    fresh one, a snapshot carries it whole, the shutdown empties it - and a field added here cannot be left out of
	    one of them. (Listed by hand in each of those places, it was: the snapshot once lacked the pencil cat's fact -
	    case xq-icon-undo, the cat stayed over the list an Undo brought back - and a static string once missed the
	    shutdown's list.) */
	struct ListHeader
	{
		bool			fromBook;		// SetFromBook
		bool			fromAllDocuments;	// SetFromAllDocuments
		SearchScopeKind	searchScope;	// SetSearchScope
		std::vector<KFCNarrowStep>	narrow;	// SetNarrowSteps - a Narrow Down's conditions, in order (empty for any other list)
		PMString		bookName;		// SetBookName
		int32			searchMode;		// SetSearchMode (-1 = nothing searched yet)
		PMString		walkSignature;	// SetWalkSignature
		bool			hasRun;			// NoteRun
		bool			changeAllWrote;	// NoteChangeAllWrote
		std::shared_ptr<const KFCQueryCopy>	query;	// SetQueryCopy - the search's query, copied (null = not a search's list)
		ListHeader() : fromBook(false), fromAllDocuments(false), searchScope(kScopeDocument), searchMode(-1), hasRun(false),
			changeAllWrote(false) {}
	};

	/** The whole result set - for the one write that replaces it, the query run (KFCUndoFollow::RunRecorder). Its
	    header with it: a query run's Undo puts back a list of another kind, and the picture with it. */
	struct ModelSnapshot
	{
		std::vector<Chapter>	chapters;
		uint32					layout;
		ListHeader				header;
		ModelSnapshot() : layout(0) {}
	};
	void TakeModelSnapshot(ModelSnapshot& out);
	/** Puts it back whole, its layout generation with it; the row backup is let go (it indexes the rows
	    that went). */
	void RestoreModelSnapshot(const ModelSnapshot& snapshot);

	/** ApplyRowStep's work on a list that is NOT in the model - a kept tier's (KFCTierStore - the tier switch): an Undo
	    or a Redo of a write made while that list was the live one, followed while it is kept. Its rows and recorded
	    versions put back on `list`; no text focus is touched (a kept list's foci are held - KFCRowFoci::HandOver - and
	    follow the Undo or the Redo themselves: KFCRowFocus.h). A row whose index is out of range is passed over, as
	    ApplyRowStep does. */
	void ApplyRowStepTo(ModelSnapshot& list, const RowStep& step, bool after);

	/** A LIST'S COUNTS, AS ITS TIER'S ROW READS THEM - how many of its hits it has replaced, of how many - for a list that
	    is NOT in the model (a kept tier's: KFCTierStore::ForgetDocument tells the panel its heading's counts once a
	    closed document's rows went). The live list's are GetTotalReplacedCount / GetTotalHitCount, counted the same way. */
	void GetListCounts(const ModelSnapshot& list, int32& outReplaced, int32& outHits);

	/** Which result set the rows are: a new number with every Clear (a search, a query run, Clear Results, a close) -
	    a write kept for an Undo belongs to one, and means nothing to the next unless a kept query run leads back to it
	    (its Undo puts that set back - KFCUndoFollow's DropOtherResultSets). */
	uint32 GetResultSetId();

	/** A LIST PUT BACK TAKES ITS OWN RESULT SET AGAIN. A query run clears the list at its commit point (a new number);
	    cancelled or refused after it, the list is put back (KFCUndoFollow::RunRecorder::RestoreBefore) - and with this
	    the number too, so the writes kept for that list are followed again (measured - case xq-cancel-undo: a Ctrl+Z of a
	    row's Replace made before a cancelled run went unfollowed, the row still reading "Changed"). The same for a run
	    that went through, as its Undo or Redo puts a list back (KFCUndoFollow.cpp's Follow - the run's step names both sets,
	    the one it found and the one it left; cases xq-done-undo, xq-two-runs-undo). */
	void ReturnToResultSet(uint32 resultSetId);

	/** Which layout of the result set the row indices name: a new number with every Clear;
	    RestoreModelSnapshot brings its own back. */
	uint32 GetLayoutGeneration();
}

#endif // __KFCResultModel_h__
