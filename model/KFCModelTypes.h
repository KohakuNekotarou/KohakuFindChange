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
#include "UIDRef.h"

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
		kRowElsewhere		// found through its text focus or by order - for this jump only (spec T3)
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

	/** THE MOST TIERS KEPT (the author's call, 10-14 - 50 being too many searches to keep): past it the oldest goes. One
	    number for both halves - the model keeps the tiers to it (KFCTierStore::kMaxTiers), the UI half names it when one
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

		RowDisplay() : replaced(false), outlined(false), kept(false), special(0) {}
	};
}

/** AN OBJECT ROW'S SPECIAL OPERATIONS (docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md - the
    author, 2026-10-17): InDesign's own Object menu commands over the rows selected together, across spreads and documents
    (KFCObjectSpecial). The number crosses the boundary - IKFCRuns::SpecialRows, RowDisplay::special - so it is FIXED:
    a new operation is appended before kKFCSpecialOpCount, none is renumbered. (Create Outlines is not one of them: it has
    its own door and mark - KFCObjectOutline, Hit::outlined.) */
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
	// (No "Run Script on the rows' objects": dropped - the author, 2026-10-17 night, the spec's E13.)
	kKFCSpecialOpCount
};

/** WHAT A SPECIAL OPERATION IS GIVEN besides its rows (IKFCRuns::SpecialRows). x, y: Move - the horizontal and vertical
    distances in points (right and down positive); Scale - the horizontal and vertical percentages; Rotate, Shear - the
    angle in degrees in x (counter-clockwise positive, as InDesign's dialogs have it); Shear - its axis in y, 0 Horizontal
    and 1 Vertical (the Shear dialog's Axis, as InDesign's own - 2026-10-17). text: Move to Active Layer - the layer's
    name. */
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
		kKeptChapterGone	// kept tier `set`'s chapter chapterIdx was emptied - its document closed: its grey copy lets
							// that document's rows go, and its heading reads `replaced` of `hits`
							// (KFCResultHistory::KeptChapterGone)
	};
	Kind				kind;
	int32				chapterIdx;
	const PMString*		text;
	uint32				set;		// kKeptChapterGone: the kept tier's result set
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
