//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The model half's PLAIN TYPES that the UI half reads as well (the model/UI split).
//  Structs, enums and constants only: no function is declared here, so a UI file that includes this
//  cannot reach into the model plug-in's code by accident - every call goes through the session
//  interfaces (IKFCResults / IKFCRuns / IKFCChapters). The namespaces are the ones these types always
//  had, so KFCResultModel::RowDisplay is still spelled KFCResultModel::RowDisplay.
//  Only what crosses stands here: a type the UI half does not read lives in KFCResultModel.h /
//  KFCSearchEngine.h (Hit, Chapter, the groups, the Undo copies, HitDetail).
//
//========================================================================================

#ifndef __KFCModelTypes_h__
#define __KFCModelTypes_h__

#include "BaseType.h"
#include "PMPoint.h"		// PBPMPoint - KFCOversetLoc
#include "PMString.h"
#include "UIDRef.h"

namespace KFCResultModel
{
	/** The panel draws at most this many hit rows (book order), to keep a large result set from flooding the
	    panel. A list holds up to kKFCCollectHitLimit (KFCResultModel.h) - more than this since 2026-10-05 (the
	    author's call, the spec map's GEN-34 as changed): the rows past this one are held and not drawn, Check All on
	    a book or document row ticks them too, and Change Checked writes them. The cap's own machinery (the
	    adapter's counts, the "first N shown" note, the close responder's rebuild) keeps the panel to it. */
	const int32 kKFCDisplayHitLimit = 1000;

	/** What became of a hit when a replace ran over it. Only ever set on rows the replace actually
	    reached; everything else stays kOutcomeNone. Drawn as a word on the end of the locator. */
	enum ChangeOutcome
	{
		kOutcomeNone = 0,	// replaced, or never reached
		kOutcomeMissing,	// the text could not be found where the search left it (moved or deleted)
		kOutcomeLocked,		// it became locked between the search and the replace
		kOutcomeRefused,	// InDesign's own replace command would not run there
		kOutcomeDeleted,	// ticked, and gone WITH the footnote / table / anchored object another
							// ticked row deleted - counted as done; no place to jump to
		kOutcomeEndnoteLeft	// ticked, left alone: the match ends an endnote, and InDesign's replace
							// breaks an endnote at its end (MatchEndsAnEndnote). Only this row is
							// left - the replace goes one match at a time
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
		PMString		accentFlag;	// "missing" / "refused" / "not replaced", or empty - drawn in the accent
									// colour (BuildHitLocator's tests are the list of both strings)
		PMString		preText;	// the line, split around the match
		PMString		matchText;
		PMString		postText;
		bool			replaced;
		bool			locked;
		ChangeOutcome	outcome;
		// (A footnote's row is told apart by GetHitInFootnote, not by a field here.)

		RowDisplay() : replaced(false), locked(false), outcome(kOutcomeNone) {}
	};
}

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
		kStatus				// say *text on the message line (KFCResultTree::ShowStatus)
	};
	Kind				kind;
	int32				chapterIdx;
	const PMString*		text;

	explicit KFCNotifyPayload(Kind k) : kind(k), chapterIdx(-1), text(nil) {}
};

#endif // __KFCModelTypes_h__
