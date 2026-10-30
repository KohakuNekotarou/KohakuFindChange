//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The model half's PLAIN TYPES that the UI half reads as well (the model/UI split).
//  Structs, enums, constants and inline helpers on them only: nothing of the model plug-in's code is declared
//  here, so a UI file that includes this cannot reach into it by accident - every call goes through the session
//  interfaces (IKFCResults / IKFCRuns / IKFCChapters). The namespaces are the ones these types always
//  had, so KFCResultModel::RowDisplay is still spelled KFCResultModel::RowDisplay.
//  A type here that changes its layout moves kKFCBoundaryRevision up by one (KFCBoundaryID.h), as a door does.
//  Only what crosses stands here: a type the UI half does not read lives with the model code that uses it (the
//  result model's - Hit, Chapter and the rest - in KFCResultModel.h).
//
//========================================================================================

#ifndef __KFCModelTypes_h__
#define __KFCModelTypes_h__

#include "BaseType.h"
#include "IDFile.h"			// KFCSavedQuery
#include "PMPoint.h"		// PBPMPoint - KFCOversetLoc
#include "PMString.h"
#include "TextChar.h"		// kTextChar_StyleGroupPathSeparator - KFCStylePathWords
#include "UIDRef.h"
#include "WideString.h"		// KFCStylePathSwapped

#include <vector>

namespace KFCResultModel
{
	/** What became of a hit when a replace ran over it. Only ever set on rows the replace actually
	    reached; everything else stays kOutcomeNone. Drawn as a word on the locator - "Missing" first,
	    the others after it (KFCResultModel::BuildHitLocator). */
	enum ChangeOutcome
	{
		kOutcomeNone = 0,	// replaced, or never reached
		kOutcomeMissing,	// the text could not be found where the search left it (moved or deleted)
		kOutcomeLocked,		// it became locked between the search and the replace
		kOutcomeRefused		// InDesign's own replace command would not run there
	};

	/** Where a row is now - KFCSearchEngine::LocateRow's answer (IKFCRuns::LocateRow), for the jump and the double
	    click (docs/superpowers/specs/2026-10-08-kfc-text-focus-jump-design.md T2 / T3). */
	enum RowLocation
	{
		kRowNotFound = 0,	// nowhere that can be told - the jump says "Not found"
		kRowAtPlace,		// at its stored place
		kRowMoved,			// moved to the one free candidate (OneFreePlace's rule) - the row stands there now
		kRowElsewhere		// found through its text focus or by order - for this jump only (T3 above)
	};

	enum SearchScopeKind
	{
		kScopeDocument = 0,
		kScopeBook,
		kScopeAllDocuments,
		kScopeStory,
		kScopeToEndOfStory,
		kScopeSelection,
		kScopeInObjects		// Search Text in These Objects: the text of a tier's objects (Chapter::searchedObjects)
	};

	/** THE MOST TIERS KEPT (the author's call - 50 being too many searches to keep): past it the oldest goes. One
	    number for both halves - the model keeps the tiers to it (KFCTierStore), the UI half names it when one
	    goes (KFCResultHistory). */
	enum { kMaxKeptTiers = 10 };

	/** Everything a hit row needs to lay itself out and paint itself. @see GetHitRow. */
	struct RowDisplay
	{
		PMString		locator;	// "[Missing ]P1(2) overset hidden locked" - drawn at the full text colour
		PMString		accentFlag;	// "refused", or empty - drawn in the accent
									// colour (BuildHitLocator's tests are the list of both strings)
		PMString		preText;	// the line, split around the match
		PMString		matchText;
		PMString		postText;
		bool			replaced;
		bool			outlined;	// written as outlines (Hit::outlined): the row reads "Outlined" for "Changed"
		bool			kept;		// the row eye (Hit::kept - docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md)
		int32			special;	// the special operation KFC last wrote the row's item with (Hit::special - a KFCSpecialOp;
									// 0 = none): the row reads its word first (the UI half's KFCSpecialRowWord)
		PMString		outlinedText;	// A TEXT ROW WRITTEN AS OUTLINES (Hit::outlinedText -
									// docs/superpowers/specs/2026-10-18-kfc-text-outlines-design.md X6): the characters
									// it outlined, drawn where the match stands; matchText holds what the row reads
									// now (the inline's mark, not shown), which the doors compare. Empty on other rows.
		PMString		runChain;	// A ROW OF A QUERY RUN'S LIST (Hit::runChain -
									// docs/superpowers/specs/2026-10-30-kfc-run-result-list-design.md R6): what its place has
									// been, drawn where the match stands ("cat -> dog -> kog"); matchText holds what the
									// row reads now, which the doors compare. Empty on other rows.
		PMString		runNames;	// ...and the queries that wrote it, drawn after the line ("  (a, b)").

		RowDisplay() : replaced(false), outlined(false), kept(false), special(0) {}
	};
}

/** A TIER'S NARROWING (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md): one condition of a tier heading's
    Narrow Down, kept with the list in the order the steps were taken (ListHeader::narrow) and handed over one at a time
    (IKFCRuns::NarrowList). The kind crosses the boundary, so it is FIXED: a new one is appended before
    kKFCNarrowKindCount, none is renumbered. */
enum KFCNarrowKind
{
	kKFCNarrowNone = 0,
	kKFCNarrowInTables = 1,		// the match's text in a table cell
	kKFCNarrowInFootnotes,		// ...in a footnote
	kKFCNarrowInEndnotes,		// ...in an endnote (its own story)
	kKFCNarrowPages,			// the row's page in a range of pages (`pages` - the print dialog's way of writing one)
	kKFCNarrowOnPasteboard,		// the item, or the match's frame, out on the pasteboard
	kKFCNarrowFrames,			// the frames holding the matches, one object row each
	kKFCNarrowOnLayer,			// the item, or the match's frame, on a layer of a name (`layer` - On Active Layer)
	kKFCNarrowWithOverrides,	// the match's text overridden - by its own attributes or its paragraphs' - as InDesign says it
								// (KFCTextOverrides::HasOverrides - docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md B1)
	kKFCNarrowKindCount
};

struct KFCNarrowStep
{
	int32		kind;		// a KFCNarrowKind
	PMString	pages;		// kKFCNarrowPages: the range as typed ("1-5, 8"); empty for every other kind
	PMString	layer;		// kKFCNarrowOnLayer: the layer's name - the front document's active layer when it was
							// pressed (the author's call: in Narrow Down, not a command of its own, and for text
							// tiers too); in another document, its layer of that name. Empty for every other kind

	KFCNarrowStep() : kind(kKFCNarrowNone) {}
	explicit KFCNarrowStep(int32 k) : kind(k) {}
};

/** AN OBJECT ROW'S SPECIAL OPERATIONS (docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md):
    InDesign's own Object menu commands over the rows selected together, across spreads and documents
    (KFCObjectSpecial). The number crosses the boundary - IKFCRuns::SpecialRows, RowDisplay::special - so it is
    FIXED: a new operation is appended before kKFCSpecialOpCount, none is renumbered. (Create Outlines is not one of
    them: it has its own door and mark - KFCObjectOutline, Hit::outlined.) */
enum KFCSpecialOp
{
	kKFCSpecialNone = 0,
	// Object > Fitting
	kKFCSpecialFillFrameProp = 1,
	kKFCSpecialFitContentProp,
	kKFCSpecialContentAwareFit,
	kKFCSpecialFitFrameToContent,
	kKFCSpecialFitContentToFrame,
	kKFCSpecialCenterContent,
	// Object > Arrange
	kKFCSpecialBringToFront,
	kKFCSpecialBringForward,
	kKFCSpecialSendBackward,
	kKFCSpecialSendToBack,
	// Object > Transform, with no value to ask
	kKFCSpecialRotate90CW,
	kKFCSpecialRotate90CCW,
	kKFCSpecialRotate180,
	kKFCSpecialFlipHorizontal,
	kKFCSpecialFlipVertical,
	kKFCSpecialRedefineScaling,
	kKFCSpecialClearTransformations,
	// Object > Lock / Unlock, Hide / Show
	kKFCSpecialLock,
	kKFCSpecialUnlock,
	kKFCSpecialHide,
	kKFCSpecialShow,
	// the Layers panel's active layer, the Object Styles panel's Clear Overrides, Edit > Clear
	kKFCSpecialMoveToLayer,
	kKFCSpecialClearStyleOverrides,
	kKFCSpecialDelete,
	// Object > Transform with a value asked first (a dialog of the UI half's)
	kKFCSpecialMove,
	kKFCSpecialScale,
	kKFCSpecialRotate,
	kKFCSpecialShear,
	// (No "Run Script on the rows' objects": dropped - the author's call, E13 of the spec named above.)
	// A TEXT row's Special Operations > Clear Overrides (docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md
	// B4): its mark alone - the row reads "Cleared". Written by KFCReplaceEngine::ClearOverridesHits, never by SpecialRows.
	kKFCSpecialClearTextOverrides,
	// A table row's Replace (the Table tab - docs/superpowers/specs/2026-10-31-kfc-table-tab-design.md T9): its mark alone - the row reads
	// "Styled". Written by KFCReplaceEngine's table rows, never by SpecialRows.
	kKFCSpecialTableStyle,
	// A ruby row's Replace (the Ruby tab - its spec's R8): the same - the row reads "Changed", and can be written again.
	kKFCSpecialRubyChange,
	kKFCSpecialOpCount
};

/** WHAT A SPECIAL OPERATION IS GIVEN besides its rows (IKFCRuns::SpecialRows). x, y: Move - the horizontal and vertical
    distances in points (right and down positive); Scale - the horizontal and vertical percentages; Rotate, Shear - the
    angle in degrees in x (counter-clockwise positive, as InDesign's dialogs have it); Shear - its axis in y, 0 Horizontal
    and 1 Vertical (the Shear dialog's Axis, as InDesign's own). text: Move to Active Layer - the layer's name. */
struct KFCSpecialParams
{
	double		x;
	double		y;
	PMString	text;

	KFCSpecialParams() : x(0.0), y(0.0) {}
};

/** Where the overset "+" locator for a text position is. 'found' is false when nothing in the
    thread (or any enclosing thread) is placed, so there is no on-page location to point at. */
struct KFCOversetLoc
{
	bool		found;		// true if an outport location was resolved
	UID			frameUID;	// the frame carrying the "+" (for naming its page)
	PBPMPoint	outportPb;	// the "+" point in pasteboard coordinates (for scrolling)

	KFCOversetLoc() : found(false), frameUID(kInvalidUID), outportPb(0.0, 0.0) {}
};

/** THE QR CODE TAB'S "SEARCH MODE" (docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md - KFC's own tab in
    Edit > Find/Change): what KFCSearchEngine::CurrentSearchMode answers - and a QR code list's header holds - while that
    tab is Find/Change's tab (KFCQRSearch::IsTabCurrent). KFC'S ALONE: it is never handed to IFindChangeOptions, to
    kFindSearchModeCmdBoss or to a saved query - InDesign has no such mode, and showing the tab changes none of its own
    (measured in KT - GetSearchMode stays the tab shown before it). */
enum { kKFCQRCodeSearchMode = 100 };

/** A QUERY RUN'S LIST'S "SEARCH MODE" (docs/superpowers/specs/2026-10-30-kfc-run-result-list-design.md - KFCRunList): what the
    header of the list a saved query run leaves holds (KFCResultModel::ListIsRunList). No tab is on it, no search made it and
    it has no query to search or write with: KFC'S ALONE, as kKFCQRCodeSearchMode is - never handed to InDesign. */
enum { kKFCRunListSearchMode = 101 };

/** THE TABLE TAB'S "SEARCH MODE" (docs/superpowers/specs/2026-10-31-kfc-table-tab-design.md - KFC's own tab in Edit > Find/Change, as
    the QR Code tab is): what KFCSearchEngine::CurrentSearchMode answers - and a table list's header holds - while that tab is
    Find/Change's tab (KFCTableSearch::IsTabCurrent). KFC'S ALONE: never handed to InDesign. */
enum { kKFCTableSearchMode = 102 };

/** THE RUBY TAB'S "SEARCH MODE" (docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md): the Table tab's for its own tab (KFCRubySearch::IsTabCurrent). KFC'S ALONE. */
enum { kKFCRubySearchMode = 103 };

/** WHAT THE RUBY TAB ASKS (the face's - KFCRubyTab, read by IKFCUIServices::RubyTabConditions; a ruby list keeps its find side in its query copy). */
struct KFCRubyConditions
{
	bool		withoutRuby;	// Find: Text Without Ruby - the occurrences of base with none on them; else the text with ruby
	PMString	base;			// its base contains this; empty = any (Text Without Ruby: what it looks for - refused empty)
	PMString	reading;		// its reading contains this; empty = any (Text With Ruby alone)
	PMString	changeTo;		// the reading a row's Replace gives it; empty = none given
	bool		removeRuby;		// ...or its Replace takes the ruby off - over changeTo
	int32		scope;			// its Search: - IWalkerScopeFactoryUtils::kDocumentScope (at launch) or kAllDocumentScope

	KFCRubyConditions() : withoutRuby(false), removeRuby(false), scope(0) {}
};

/** A style path with one character swapped for another - KFCStylePathWords' way, and the test build's fault switch's the other way. */
inline PMString KFCStylePathSwapped(const PMString& path, textchar from, textchar to)
{
	const WideString raw(path);
	WideString swapped;
	for (WideString::const_iterator at = raw.begin(); at != raw.end(); ++at)
	{
		const uint32 c = static_cast<uint32>(*at);
		swapped.Append(UTF32TextChar((c == static_cast<uint32>(from)) ? static_cast<uint32>(to) : c));
	}
	PMString out(swapped);
	out.SetTranslatable(kFalse);
	return out;
}

/** A STYLE'S FULL PATH AS A PERSON READS IT: IStyleGroupHierarchy::GetFullPath joins a group and its style with
    kTextChar_StyleGroupPathSeparator, a private-use character drawn as nothing ("GTS1" - measured), not the ":" its header's example shows. */
inline PMString KFCStylePathWords(const PMString& path)
{
	return KFCStylePathSwapped(path, kTextChar_StyleGroupPathSeparator, kTextChar_Colon);
}

/** WHAT THE TABLE TAB ASKS (the face's - KFCTableTab, read by IKFCUIServices::TableTabConditions; a table list keeps its find side
    in its query copy, so its searches again ask what its search asked). A table style is named by its full path, untranslated
    (GetFullPath(kFalse) - its group's separator in it; KFCStylePathWords reads it out): its UID means nothing in a book's other chapter. */
struct KFCTableConditions
{
	PMString	findStyle;		// the style looked for; empty = any
	bool		overridesOnly;	// only tables with overrides of their own
	PMString	changeStyle;	// the style a row's Replace gives its table; empty = it keeps its own
	bool		clearOverrides;	// ...and its Replace clears its table's overrides
	int32		scope;			// its Search: - IWalkerScopeFactoryUtils::kDocumentScope (at launch) or kAllDocumentScope

	KFCTableConditions() : overridesOnly(false), clearOverrides(false), scope(0) {}
};

/** WHAT KEEPS A QR CODE FIND NEXT FOUND FROM BEING SELECTED (IKFCRuns::QRCodeNext's outReach - KFCQRSearch::Next; the UI half's
    landing says it on the tab's line - KFCJump::LandOnQRCode): its row's own words (KFCObjectSearch::ReadItemPlace), as bits. */
enum
{
	kKFCQRReachLocked		= 1,	// locked (Object > Lock, or a locked layer) - shown, not selected
	kKFCQRReachHidden		= 2,	// hidden (Object > Hide, or a hidden layer) - shown, not selected
	kKFCQRReachHiddenSpread	= 4,	// on a hidden spread - nothing moves
	kKFCQRReachOverset		= 8		// in overset text - the view on its "+", not selected
};

/** What the UI half's alert handler does with an alert raised inside a walk of ours (IKFCRuns::AnswerAlert - the
    model's KFCHiddenSpreadAlert.h): a button to press, 1 to 3 - or one of these. */
enum
{
	// Put it on screen as it would have been: alerts were on before the walk switched them off.
	kKFCAlertShowIt = 0,
	// Its default button, as with no handler: alerts were off already (a script's run).
	kKFCAlertLeaveToDefault = -1
};

/** What one notification from the model half to the UI half carries (the model/UI split; sent by
    KFCModelNotify.h, received by KFCModelObserver.cpp). The model never calls the panel: it
    says what happened on the session's subject, and the panel - when there is one listening - does the
    drawing. Handed over as ISubject::Change's changedBy and read during delivery only, so `text` may
    point at the sender's own string. */
struct KFCNotifyPayload
{
	enum Kind
	{
		kRebuild = 0,		// the result set changed shape: build the tree again (KFCResultTree::Rebuild)
		kRefreshRows,		// only what the rows draw changed (KFCResultTree::RefreshRows)
		kChapterRowGoes,	// chapterIdx's row is about to go (KFCResultTree::BeforeChapterRowGoes)
		kStatus,			// say *text on the message line (KFCResultTree::ShowStatus)
		kKeptChapterGone,	// kept tier `set`'s chapter chapterIdx was emptied - its document closed: its grey copy
							// lets that document's rows go, and its heading reads `replaced` of `hits`
							// (KFCResultHistory::KeptChapterGone)
		kKeptChapterRenamed	// kept tier `set`'s chapter chapterIdx's document saved as another file: its grey copy's
							// document row reads *text (KFCResultHistory::KeptChapterRenamed)
	};
	Kind				kind;
	int32				chapterIdx;
	const PMString*		text;		// kStatus' words, kKeptChapterRenamed's name - read while Change delivers it
	uint32				set;		// kKeptChapterGone, kKeptChapterRenamed: the kept tier's result set
	int32				replaced;	// kKeptChapterGone: the kept list's counts now - its hits replaced, of how many
	int32				hits;

	explicit KFCNotifyPayload(Kind k) : kind(k), chapterIdx(-1), text(nil), set(0), replaced(0), hits(0) {}
};

/** One saved Find/Change query as the query dialog shows it (KFCSavedQueries;
    docs/superpowers/specs/_done/2026-10-07-kfc-query-dialog-and-selected-documents-design.md section 2-2):
    the file, its name, its kind, InDesign's own or the user's, and whether the file is there now. */
struct KFCSavedQuery
{
	IDFile		file;
	PMString	name;		// the file's name without ".xml" - what the dialog shows
	int32		mode;		// IFindChangeOptions::SearchMode: kTextSearch, kGrepSearch, kGlyphSearch, kObjectSearch,
							// kTransliterateSearch; -1 = no kind's folder
	bool		bundled;	// InDesign's own (Presets\Find-Change Queries), not the user's
	bool		exists;		// the file is there now

	KFCSavedQuery() : mode(-1), bundled(false), exists(false) {}
};

// HIT ROWS SELECTED TOGETHER, by document (the author's call: rows of several documents are replaced together, in one
// undo step): each chapter of the result model with its hit rows, in the order the list draws them.
struct KFCChapterRows
{
	int32				chapter;	// the chapter's index into the result model
	std::vector<int32>	hits;		// its hit rows - indices into that chapter's hits - in the order the list draws them

	KFCChapterRows() : chapter(-1) {}
	explicit KFCChapterRows(int32 c) : chapter(c) {}					// no row yet
	KFCChapterRows(int32 c, int32 hit) : chapter(c), hits(1, hit) {}	// one row
};
typedef std::vector<KFCChapterRows> KFCRowsByChapter;

// How many rows they are - every chapter's together.
inline size_t KFCRowCount(const KFCRowsByChapter& rows)
{
	size_t total = 0;
	for (size_t k = 0; k < rows.size(); ++k)
		total += rows[k].hits.size();
	return total;
}

// The first of them: the one row when there is one, and the row that tells which kind they are (rows of one kind are
// selected together). Every chapter the list hands over holds a row (KFCResultTree::GetSelectedHitRows); a chapter with
// none is passed over all the same. false = no row.
inline bool KFCFirstRow(const KFCRowsByChapter& rows, int32& outChapter, int32& outHit)
{
	for (size_t k = 0; k < rows.size(); ++k)
		if (!rows[k].hits.empty())
		{
			outChapter = rows[k].chapter;
			outHit = rows[k].hits[0];
			return true;
		}
	outChapter = -1;
	outHit = -1;
	return false;
}

#endif // __KFCModelTypes_h__
