//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What the panel's MESSAGE AREA draws: the last message, one string. The box is drawn by hand
//  (KFCStatusTextView.cpp) so that the text is the text - a lone '&' in a file name is not taken as an
//  accelerator (the spec map's ROW-31). Until 2026-10-06 it also carried a heading and two faded pieces for a
//  replaced row's "Source Text:", which went with Track Changes (docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F5).
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

	/** Read back what was written. Empty before the first message. */
	virtual void GetText(PMString& outMessage) const = 0;
};

#endif // __IKFCStatusTextData_h__

// End, IKFCStatusTextData.h.
