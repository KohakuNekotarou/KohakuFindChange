//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  IKBSRowData: the per-row draw data for a hit line's colour cell. The line is pre-split (in
//  KBSSearchEngine, against the paragraph's wide string at the finder's exact offsets) into
//  three segments - the text before the match, the matched text, and the text after - so the
//  cell (KBSColorTextView) just paints three runs and never does UTF-16 boundary maths at draw
//  time. The matched run is drawn in a highlight colour; the rest follows the palette text
//  colour (theme-adaptive). New to KBS (no KESCL original); the recipe is the multi-colour cell
//  draw pattern proven against customdatalinkui's DVControlView.
//
//========================================================================================

#ifndef __KBSColorTextView_h__
#define __KBSColorTextView_h__

#include "IPMUnknown.h"
#include "PMString.h"
#include "KBSID.h"

/** The parts a hit row's colour cell paints: the page LOCATOR ("P1(2)", drawn at the full theme
    text colour and followed by a tab-stop gap), then the line split around the match - the text
    before it, the matched text (full colour), and the text after (before/after are drawn faded).
    Set by the widget manager on every apply, read by KBSColorTextView::Draw. Non-persistent.

    The row's check box is NOT drawn here - it is a real check box widget beside this cell
    (kKBSResultCheckWidgetBoss), so it follows the UI theme and can be reached from outside.

    A replaced row draws exactly like a found one: after a replace the panel lists only what was
    changed, and the new text is what the user wants to read, so it gets the same emphasis a match
    does. (The check box next to it is hidden instead - there is nothing left to select.) */
class IKBSRowData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKBSROWDATA };

	/** Replace this row's parts: the page locator, the accent-coloured flag word that follows it
	    (empty on most rows), and the three line segments. Any may be empty. */
	virtual void SetSegments(const PMString& locator, const PMString& flag, const PMString& pre,
		const PMString& match, const PMString& post) = 0;

	/** Read this row's parts back (for the cell's Draw). */
	virtual void GetSegments(PMString& outLocator, PMString& outFlag, PMString& outPre,
		PMString& outMatch, PMString& outPost) const = 0;
};

#endif // __KBSColorTextView_h__
