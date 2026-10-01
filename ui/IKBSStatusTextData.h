//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  What the panel's MESSAGE AREA draws (2026-09-29): an optional heading line, and a body split into
//  the three pieces where its colour changes.
//
//  ***** WHY PIECES AND NOT ONE STRING. ***** Every message the panel raises used to be one string in a
//  stock multi-line static text, which draws its whole string in ONE colour. When a replaced row is
//  selected, this box shows the text as it was BEFORE the replace (KBSJump::ActivateNode), and the
//  reader's question there is "which characters were the ones replaced" - a question one colour
//  cannot answer. So the box is drawn by hand (KBSStatusTextView.cpp), and a hand-drawn box has to be
//  told where the colour changes: label / pre / mid / post is that channel. The same shape as KCM's
//  IKCMStatusTextData, which is where it was brought over from (the user: "the way KCM does it").
//
//  ***** AN ORDINARY MESSAGE IS THE SAME SHAPE, NOT A SPECIAL CASE: ***** the other pieces are empty
//  and mid carries the whole sentence, which comes out as one run at the theme's text colour - what
//  the stock widget drew. That is why no caller of KBSResultTree::ShowStatus had to change.
//
//  ***** THE HEADING IS ITS OWN FIELD RATHER THAN THE HEAD OF pre, ***** for the overflow rule: when the
//  text does not fit, the CONTEXT gives way from its outer ends - so a heading at the head of pre
//  would be the first thing cut away. It is the one piece that must survive.
//
//  ***** NOT PERSISTENT. ***** This is the message raised last in this session; what outlives the panel
//  is kept on the tree's side (KBSResultListWidgetMgr.cpp) and written back when the panel is shown.
//
//========================================================================================

#ifndef __IKBSStatusTextData_h__
#define __IKBSStatusTextData_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KFCUIID.h"		// IID_IKBSSTATUSTEXTDATA

/** Holds what the panel's message area draws, split where the colour changes. */
class IKBSStatusTextData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKBSSTATUSTEXTDATA };

	/** Replace every piece. ***** ALL OF THEM, ALWAYS. ***** There is one message area and one message
	    in it; writing only the pieces a caller happens to have would leave the rest of the previous
	    message standing beside it.

	    The same text is written, as one line, to the widget's own ITextControlData - the one it
	    inherits from kGenericPanelWidgetBoss - which is where a reader that walks the widgets looks
	    for a label (KIDMCP's inspect_ui; the regression suite's PSTATUS). An ordinary message is its
	    mid as it stands; a message with a heading reads "<label>  <pre>[<mid>]<post>", the way a hit
	    row reads.

	    @param label a heading on a line of its own ("Source Text:"). Empty for an ordinary message, and then
	        it costs no line. Drawn at the full text colour: it is not context, it says what the words
	        below are.
	    @param pre the words before the characters that matter - faded. Empty for an ordinary message.
	    @param mid for an ordinary message, the whole message; otherwise the characters that matter -
	        drawn at the theme's full text colour.
	    @param post the words after them, on the same terms as pre.
	    @param wantCaret mid is a PLACE with no characters - draw the bar there (KBSPanelTextDraw.h).
	        PASSED, NEVER GUESSED from an empty mid: an empty ordinary message is nothing at all, and
	        only the caller knows which of the two it has. Ignored while mid is not empty. */
	virtual void SetSegments(const PMString& label, const PMString& pre, const PMString& mid,
		const PMString& post, bool16 wantCaret) = 0;

	/** Read back what was written. Empty strings and kFalse before the first message. */
	virtual void GetSegments(PMString& outLabel, PMString& outPre, PMString& outMid,
		PMString& outPost, bool16& outWantCaret) const = 0;
};

#endif // __IKBSStatusTextData_h__

// End, IKBSStatusTextData.h.
