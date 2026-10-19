//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE ROW EYE'S ROW, ASKED IN ONE PLACE (memory one-question-one-place): by the click that turns one eye
//  (KFCResultEyeEH.cpp) and by the drag that turns the eyes it passes over (KFCResultEyeDrag.cpp - the author's ask:
//  "like the Layers panel, by tracing"). Every eye asks the same question - is this a live hit row KFC has written, and
//  is it kept - and draws the answer the same way (Draw - the list's rows too, KFCResultListWidgetMgr), with the same
//  words on the status line (SayKept).
//
//========================================================================================

#ifndef __KFCResultEye_h__
#define __KFCResultEye_h__

#include "NodeID.h"
#include "KFCModelTypes.h"		// KFCResultModel::RowDisplay

class IControlView;
class IPMUnknown;

namespace KFCResultEye
{
	/** The live hit row the eye `eye` (any interface on its boss) sits in: its chapter and hit, and its node when asked.
		false for a kept tier's row (ROW-42), or none. */
	bool LiveHitRow(const IPMUnknown* eye, int32& outChapter, int32& outHit, NodeID* outNode = nil);

	/** ...one KFC has written - replaced, outlined, or given a special operation: the only rows with an eye to turn
		(the author's call). outRow = its display. */
	bool WrittenRow(const IPMUnknown* eye, int32& outChapter, int32& outHit, KFCResultModel::RowDisplay& outRow,
					NodeID* outNode = nil);

	/** Whether a row's display is a written row's (WrittenRow's test, for a row reached through the list, not an eye). */
	bool IsWritten(const KFCResultModel::RowDisplay& row);

	/** An eye drawn as its row is: InDesign's own eyeball while kept, nothing while not. now = drawn at once - while a drag
		holds the button nothing else draws it in time (the Layers panel's ForceRedraw); else invalidated, as a click's. */
	void Draw(IControlView* eye, bool kept, bool now);

	/** The status line after eyes are turned: `what` - the row's locator as the list reads it, or "<n> rows:" - and
		what the eyes mean now (kept = on). */
	void SayKept(const PMString& what, bool kept);

	/** The drag the eye `eye` started is over (IDragDropController::StartDrag has returned): what it turned is said on the
		status line, and the keyboard goes to the list as after a click. A drag that crossed no eye - or never began - is a
		click on the first one: it turns it (KFCResultEyeDrag.cpp). */
	void DragEnded(const IPMUnknown* eye);
}

#endif // __KFCResultEye_h__

// End, KFCResultEye.h.
