//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The model half's PLAIN TYPES that the UI half reads as well (2026-10-01, the model/UI split).
//  Structs, enums and constants only: no function is declared here, so a UI file that includes this
//  cannot reach into the model plug-in's code by accident - every call goes through the session
//  interfaces (IKBSResults / IKBSRuns / IKBSChapters). The namespaces are the ones these types always
//  had, so KBSResultModel::RowDisplay is still spelled KBSResultModel::RowDisplay.
//  Moved here verbatim from KBSResultModel.h and KBSOversetLocator.h. Only what crosses stands here:
//  Hit, Chapter, the groups, the Undo copies and HitDetail went back to KBSResultModel.h /
//  KBSSearchEngine.h on 2026-10-01, when the UI half was measured reading none of them.
//
//========================================================================================

#ifndef __KBSModelTypes_h__
#define __KBSModelTypes_h__

#include "BaseType.h"
#include "PMPoint.h"		// PBPMPoint - KBSOversetLoc
#include "PMString.h"
#include "UIDRef.h"

namespace KBSResultModel
{
	/** The panel shows at most this many hit rows (book order). The model still HOLDS every hit -
	    a same-book re-search reuses them, and a replace consumes them ALL - only the tree display is
	    capped, to keep a huge result set from flooding the panel. */
	const int32 kKBSDisplayHitLimit = 5000;

	/** What became of a hit when a replace ran over it. Only ever set on rows the replace actually
	    reached; everything else stays kOutcomeNone. Drawn as a word on the end of the locator. */
	enum ChangeOutcome
	{
		kOutcomeNone = 0,	// replaced, or never reached
		kOutcomeMissing,	// the text could not be found where the search left it (moved or deleted)
		kOutcomeLocked,		// it became locked between the search and the replace
		kOutcomeRefused,	// InDesign's own replace command would not run there
		kOutcomeRejected,	// replaced, then taken back with Reject Change (2026-09-26): the row
							// shows the original text again and can be replaced once more (Redo)
		kOutcomeDeleted,	// ticked, and gone WITH the footnote / table / anchored object another
							// ticked row deleted (2026-09-26) - Change All's own result; no place to jump to
		kOutcomeEndnoteLeft,// ticked, in the endnote story, left alone: a match there ends an endnote,
							// and InDesign's replace breaks an endnote at its end (2026-09-27, the
							// user's call - the whole endnote story is left, Change All works by story)
		kOutcomeAccepted	// replaced, then its tracked change ACCEPTED with Accept Change by
							// KohakuFindChange (2026-09-29): the replace is final, nothing is left to
							// take back or accept - the locator says "accepted"
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
		bool			checked;
		bool			replaced;
		bool			locked;
		ChangeOutcome	outcome;
		bool			hasCheckBox;	// does THIS row carry a check box? RowHasCheckBox's own answer,
										// so the panel does not have to re-derive it from the four
										// fields above - see GetHitRow.
		// (An inFootnote stood here from 2026-09-26 - a footnote's box drawn ticked and greyed, while
		// the replace was Change All. Nothing read it after the one-at-a-time replace of 2026-09-27;
		// removed 2026-09-28. A footnote's row is told apart by GetHitInFootnote now.)

		RowDisplay() : checked(false), replaced(false), locked(false), outcome(kOutcomeNone),
					   hasCheckBox(false) {}
	};

	enum
	{
		kContextMenuBookRow		= -1,	// the BOOK row: the commands reach every chapter
		kNoContextMenuChapter	= -2	// nothing has been right-clicked: they do nothing at all
	};
}

/** Where the overset "+" locator for a text position is. 'found' is false when nothing in the
    thread (or any enclosing thread) is placed, so there is no on-page location to point at. */
struct KBSOversetLoc
{
	bool		found;		// true if an outport location was resolved
	UID			frameUID;	// the frame carrying the "+" (for naming its page)
	PBPMPoint	outportPb;	// the "+" point in pasteboard coordinates (for scrolling)

	KBSOversetLoc() : found(false), frameUID(kInvalidUID), outportPb(0.0, 0.0) {}
};

/** What one notification from the model half to the UI half carries (2026-10-01, the model/UI split;
    sent by KBSModelNotify.h, received by KBSModelObserver.cpp). The model never calls the panel: it
    says what happened on the session's subject, and the panel - when there is one listening - does the
    drawing. Handed over as ISubject::Change's changedBy and read during delivery only, so `text` may
    point at the sender's own string. */
struct KBSNotifyPayload
{
	enum Kind
	{
		kRebuild = 0,		// the result set changed shape: build the tree again (KBSResultTree::Rebuild)
		kRefreshRows,		// only what the rows draw changed (KBSResultTree::RefreshRows)
		kChapterRowGoes,	// chapterIdx's row is about to go (KBSResultTree::BeforeChapterRowGoes)
		kStatus				// say *text on the message line (KBSResultTree::ShowStatus)
	};
	Kind				kind;
	int32				chapterIdx;
	const PMString*		text;

	explicit KBSNotifyPayload(Kind k) : kind(k), chapterIdx(-1), text(nil) {}
};

#endif // __KBSModelTypes_h__
