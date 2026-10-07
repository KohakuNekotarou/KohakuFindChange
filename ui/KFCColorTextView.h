//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCRowData: the per-row draw data for a hit line's colour cell. The line arrives pre-split (by
//  the model half, against the paragraph's wide string at the finder's exact offsets) around the
//  match, so the cell (KFCColorTextView) just paints runs and never does UTF-16 boundary maths at
//  draw time. New to KFC (no KESCL original); the recipe is the multi-colour cell draw pattern proven
//  against customdatalinkui's DVControlView.
//
//========================================================================================

#ifndef __KFCColorTextView_h__
#define __KFCColorTextView_h__

#include "IPMUnknown.h"
#include "PMString.h"
#include "KFCUIID.h"

/** The parts a hit row's colour cell paints: the page LOCATOR ("P1(2)", drawn at the full theme
    text colour), the FLAG word that follows it when the row has one (drawn in the theme's accent -
    the words are KFCResultModel::BuildHitLocator's), then a gap, then the line split around the
    match - the text before it, the matched text (full colour), and the text after (before/after are
    drawn faded).
    Set by the widget manager on every apply, read by KFCColorTextView::Draw. Non-persistent.

    A replaced row draws exactly like a found one: the new text is what the user wants to read, so it
    gets the same emphasis a match does. (Its right-click Replace is greyed instead, and its locator
    starts with "Changed".) */
class IKFCRowData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKFCROWDATA };

	/** Replace this row's parts: the page locator, the accent-coloured flag word that follows it
	    (empty on most rows), and the three line segments. Any may be empty. */
	virtual void SetSegments(const PMString& locator, const PMString& flag, const PMString& pre,
		const PMString& match, const PMString& post) = 0;

	/** Read this row's parts back (for the cell's Draw). */
	virtual void GetSegments(PMString& outLocator, PMString& outFlag, PMString& outPre,
		PMString& outMatch, PMString& outPost) const = 0;
};

#endif // __KFCColorTextView_h__
