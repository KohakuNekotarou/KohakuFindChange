//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Screen overlay that inverts the pixels under a jumped-to search hit. A boss
//  carries both IK2ServiceProvider (registers kDrawEventService, so the app hooks the sibling
//  IDrwEvtHandler into the draw-event dispatcher at startup) and IDrwEvtHandler (draws on the
//  spread front, in spread coordinates). The marker is a single rectangle in pasteboard
//  coordinates; it takes itself off the screen about a second after it appears
//  (KBSMarkerExpiryIdleTask) - a pointer to the match, not a highlight. Non-printing,
//  non-persistent. Ported from KESCL's KESCLDrawEventHandler (KESCL left untouched).
//
//========================================================================================
#ifndef __KBSDrawEventHandler_h__
#define __KBSDrawEventHandler_h__

#include "CPMUnknown.h"
#include "IDrwEvtHandler.h"
#include "PMRect.h"
#include "PMString.h"		// sMarkerDocPath - the marker's document, named in a way an address cannot be

class IDataBase;
class IDrwEvtDispatcher;

class KBSDrawEventHandler : public CPMUnknown<IDrwEvtHandler>
{
public:
	KBSDrawEventHandler(IPMUnknown* boss) : CPMUnknown<IDrwEvtHandler>(boss) {}
	~KBSDrawEventHandler() {}

	virtual void Register(IDrwEvtDispatcher* d);
	virtual void UnRegister(IDrwEvtDispatcher* d);
	virtual bool16 HandleDrawEvent(ClassID eventID, void* eventData);

	// ---- Shared marker state (a single rectangle for the current hit) ----
	static bool16     sHasMarker;		// true when sMarkerPb / sMarkerDB are valid
	static PMRect     sMarkerPb;		// the marker rectangle, in pasteboard coordinates
	static IDataBase* sMarkerDB;		// the document the marker belongs to - an ADDRESS, never an
										// identity: once its document is closed the same address can
										// be handed to the next document opened. See sMarkerDocPath.
	static PMString   sMarkerDocPath;	// the file that document lived in when the marker was set, so
										// a reused address can be told from the real thing. Empty for
										// a document that has never been saved (then the address is
										// all there is, as before).
	static UID        sMarkerSpread;	// WHICH SPREAD the marker belongs to. kInvalidUID when the
										// jump could not name one, which falls back to the geometric
										// test - see HandleDrawEvent.

	// Set / clear the current marker and repaint. SetMarker also starts the marker's countdown to
	// clearing itself, and repaints the document the marker is leaving when it moves to another one;
	// ClearMarker cancels that countdown. Called by KBSJump - at once from both doors, the mouse and
	// the keyboard (a mouse click's marker used to be booked for the double-click interval; since
	// 2026-09-25 it is raised on the same beat as KCM's Story-mode jump flash, and a double click's
	// successful selection takes it down with ClearMarker).
	//
	// @param spreadUID the spread the match sits on, which the caller has already resolved. It is
	//        asked for rather than worked out here because "which spread does this rectangle belong
	//        to" cannot be answered from the rectangle alone: the drawing side used to decide it by
	//        testing whether the marker's centre fell inside the spread's bounding box, and those
	//        boxes CAN overlap - ISpread::GetPagesAndItemsBounds grows to enclose page items sitting
	//        out on the pasteboard, so an item pulled far enough reaches the next spread's box and
	//        both spreads then paint the same marker. Pass kInvalidUID when there is no answer.
	static void SetMarker(IDataBase* db, UID spreadUID, const PMRect& pbRect);
	static void ClearMarker();

	// (SetMarkerAfterClickSettles was declared here from 2026-08-09 to 2026-09-25: the mouse's marker
	//  waited out the double-click interval so a double click that selects never flashed one. Why it
	//  had to be a wait rather than a test still holds - at the first button-up nothing says a second
	//  click is coming, IEvent carries no click count - but the user asked for the marker on KCM's
	//  beat instead, which shows at once and lets the double click's selection take it down.)

	/** Application-shutdown cleanup: forget the marker WITHOUT repainting or touching any document,
	    and refuse every SetMarker / ClearMarker from then on.
	    ClearMarker is the wrong call there - it invalidates the views of the marker's document, which
	    by then may be half torn down - and sMarkerDocPath is a static PMString, so it has to be
	    emptied for the same reason KBSResultModel::ShutdownCleanup empties its own. (It also stopped
	    and released the mouse's booking timer until 2026-09-25, when the booking went.) */
	static void ShutdownCleanup();
};

#endif // __KBSDrawEventHandler_h__
