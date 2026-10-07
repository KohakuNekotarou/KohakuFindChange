//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What the panel's MESSAGE AREA draws: the last message. The box is drawn by hand (KFCStatusTextView.cpp) so that
//  the text is the text - a lone '&' in a file name is not taken as an accelerator (the spec map's ROW-31) - and so
//  that it can show more than one colour: a heading on a line of its own, and the characters that matter at full
//  colour between two faded pieces of context (KCM's IKCMStatusTextData shape). Until 2026-10-06 that carried a
//  replaced row's "Source Text:" (gone with Track Changes - docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md
//  F5); since 2026-10-07 a selected GREP row's "Preview Text:" (SetSegments, appended).
//
//  NOT PERSISTENT. This is the message raised last in this session; what outlives the panel
//  is kept on the tree's side (KFCResultListWidgetMgr.cpp) and written back when the panel is shown.
//
//========================================================================================

#ifndef __IKFCStatusTextData_h__
#define __IKFCStatusTextData_h__

// Interface includes:
#include "IPMUnknown.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KFCUIID.h"		// IID_IKFCSTATUSTEXTDATA

/** Holds what the panel's message area draws. */
class IKFCStatusTextData : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKFCSTATUSTEXTDATA };

	/** Replace the message. The same text is written to the widget's own ITextControlData - the one it inherits
	    from kGenericPanelWidgetBoss - which is where a reader that walks the widgets looks for a label
	    (KIDMCP's inspect_ui; the regression suite's PSTATUS). */
	virtual void SetText(const PMString& message) = 0;

	/** Read back what was written, as one line (a message with pieces reads "<label>  <pre>[<mid>]<post>").
	    Empty before the first message. */
	virtual void GetText(PMString& outMessage) const = 0;

	// APPENDED 2026-10-07 - a new method goes at the END (a vtable slot is a promise to every built caller).

	/** Replace every piece - ALL OF THEM, ALWAYS: there is one message area and one message in it. SetText is this
	    with label / pre / post empty. The one-line form goes to the widget's ITextControlData as SetText's does.
	    @param label a heading on a line of its own ("Preview Text:") - full colour: it says what the words below are.
	    @param pre the words before the characters that matter - faded.
	    @param mid the characters that matter - full colour (for an ordinary message, the whole message).
	    @param post the words after them - faded.
	    @param wantCaret mid is a PLACE with no characters (a replace that writes nothing) - draw the bar there
	        (KFCPanelTextDraw.h). Passed, never guessed from an empty mid. */
	virtual void SetSegments(const PMString& label, const PMString& pre, const PMString& mid,
		const PMString& post, bool16 wantCaret) = 0;

	/** Read back every piece. Empty strings and kFalse before the first message. */
	virtual void GetSegments(PMString& outLabel, PMString& outPre, PMString& outMid,
		PMString& outPost, bool16& outWantCaret) const = 0;
};

#endif // __IKFCStatusTextData_h__

// End, IKFCStatusTextData.h.
