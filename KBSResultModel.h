//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The result model: the last book search's hits, grouped by chapter, that the result tree
//  (KBSResultListAdapter / KBSResultListWidgetMgr) displays. A tiny session-global store - the
//  KBS analog of KESCL's KESCLBatchCheck, minus its filters / reverse mode / per-value rows,
//  because the KBS tree is a flat two levels: chapter -> hit.
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
	const int32 kKBSDisplayHitLimit = 500;

	/** One match on one line of one chapter. The three text segments are the line split around
	    the match; the jump anchors (Task 3) point back at the exact occurrence. */
	struct Hit
	{
		PMString	locator;	// the page locator "P<page>(<n>)" / "ov" (drawn at full text colour,
								// followed by a tab stop, ahead of the line)
		PMString	preText;	// the line's text before the match
		PMString	matchText;	// the matched text (drawn at full text colour)
		PMString	postText;	// the line's text after the match

		PMString	pageString;	// the page named in the locator, Pages-panel style. For a visible
								// match: its own page. For an overset match: the page of the "+"
								// indicator (or "" when nothing is placed anywhere).
		int32		pageIndex;	// that page's document order (-1 = no page); sorts hits into page order
		bool		isOverset;	// match is overset -> the locator gets an "ov" prefix ("ovP<page>")

		UID			storyUID;	// the story the match lives in (within its chapter's database)
		TextIndex	textStart;	// the match's start position in that story
		TextIndex	textEnd;	// the match's end position (Task 3 marker rectangle)

		// --- replace support ---
		// The order the text walker handed this match back in, WITHIN ITS CHAPTER (0-based). It is
		// stamped before the page-order sort, and it is the ONLY key that survives a replace pass:
		// textStart shifts the moment an earlier match is replaced, and the array order is page
		// order, not walk order. The replace pass re-walks the chapter and counts matches to line
		// them up with these numbers.
		int32		walkOrder;
		bool		checked;	// selected for replacement (a fresh search checks every hit)
		bool		replaced;	// already replaced in this result set - not selectable any more

		Hit() : pageIndex(-1), isOverset(false), storyUID(kInvalidUID),
				textStart(kInvalidTextIndex), textEnd(kInvalidTextIndex),
				walkOrder(-1), checked(true), replaced(false) {}
	};

	/** One chapter that holds at least one hit. */
	struct Chapter
	{
		PMString			name;	// the chapter's display name (its file name)
		UIDRef				docRef;	// current binding (Task 3 jump / reopen)
		IDFile				file;	// the chapter's .indd (Task 3 reopen of a closed chapter)
		std::vector<Hit>	hits;
	};

	/** Replace the whole model with a fresh search's chapters (only chapters with >=1 hit). */
	void SetResults(const std::vector<Chapter>& chapters);

	/** Append one chapter to the model. The progressive search adds chapters one at a time as each
	    finishes (after Clear), instead of one SetResults at the end. Only chapters with >=1 hit
	    should be appended (empty branches are never shown). */
	void AppendChapter(const Chapter& chapter);

	/** Forget the results (an empty search, or a teardown that still wants the tree emptied). */
	void Clear();

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

	/** A hit node's display: the page locator and the three line segments to paint. false = index
	    out of range. */
	bool GetHitDisplay(int32 chapterIdx, int32 hitIdx,
		PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost);

	/** A hit's jump anchors (Task 3): the chapter's document / file and the match's story +
	    text range. false = index out of range. */
	bool GetHitLocation(int32 chapterIdx, int32 hitIdx,
		UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd);

	/** Rebind a chapter's document reference (Task 3): after a closed chapter is reopened at jump
	    time, later jumps must use the live database, not the dead one from search time. */
	void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef);

	/** Select / deselect one hit for replacement. A hit that was already replaced is ignored (it
	    cannot be replaced again without a fresh search - the text it matched is gone). */
	void SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked);

	/** Is this hit selected for replacement? false for an out-of-range index. */
	bool IsHitChecked(int32 chapterIdx, int32 hitIdx);

	/** A hit's row-cell flags: selected, and already replaced. false = index out of range. */
	bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced);

	/** Select / deselect EVERY hit in every chapter (the flyout's Check All / Uncheck All). Applies
	    to all stored hits, including those past the panel's display cap - the display cap must not
	    silently shrink what a replace touches. Replaced hits are skipped. */
	void SetAllChecked(bool checked);

	/** How many hits are selected across all chapters (uncapped) - for the status read-out and for
	    the replace command's enablement. */
	int32 GetCheckedCount();

	/** A hit's chapter-local walker order, or -1 for an out-of-range index. The replace pass
	    re-walks a chapter and lines the Nth match of that walk up with the hit whose walkOrder is
	    N - the only key that survives replacing (see Hit::walkOrder). */
	int32 GetHitWalkOrder(int32 chapterIdx, int32 hitIdx);

	/** A chapter's document binding and file. The replace pass works chapter at a time, so it
	    needs this without going through a hit. false = index out of range. */
	bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile);

	/** Drop every hit that was NOT replaced, and every chapter left without one. Called right after
	    a replace, which turns the panel into a list of WHAT WAS CHANGED: the occurrences that were
	    left alone are no longer results worth showing, and the replaced ones cannot be selected
	    again in any case. Does NOTHING when no hit was replaced, so a replace that landed nowhere
	    never wipes the result set.
	    @return the number of hits left in the model. */
	int32 KeepOnlyReplaced();

	/** Record a completed replacement: the row keeps its page locator but takes the replaced
	    line's three segments and its new range, is marked replaced, and leaves the selection. A
	    replaced hit can never be checked again - the text it matched is gone, so a second replace
	    pass would have nothing to line it up with. */
	void MarkHitReplaced(int32 chapterIdx, int32 hitIdx,
		const PMString& newPre, const PMString& newMatch, const PMString& newPost,
		TextIndex newStart, TextIndex newEnd);
}

#endif // __KBSResultModel_h__
