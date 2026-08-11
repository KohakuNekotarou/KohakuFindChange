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
	// clearing itself; ClearMarker cancels that countdown AND any booking made by
	// SetMarkerAfterClickSettles below. Called by KBSJump.
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

	/** Put the marker up only once the click that asked for it is KNOWN to have been a SINGLE click:
	    take the old marker down now, and book this one for the double-click interval from now. A
	    second click landing inside that interval means the marker is never raised at all.

	    ***** WHY A BOOKING AND NOT A TEST. ***** A double click arrives as LButtonDn, LButtonUp,
	    ButtonDblClk, LButtonUp (KBSResultNodeEH.cpp), so at the first button-UP - where the jump runs -
	    there is nothing to ask: ButtonDblClk has not happened yet, and IEvent carries no click count
	    (its double click is a separate event type, IEvent.h:212-302). The only way to know a click was
	    single is to let the interval pass without a second one. That is what this books.

	    ***** WHO CANCELS IT. ***** Nothing here: ClearMarker does, so every existing path that takes
	    the marker down takes the booking with it. Which gives the two double-click outcomes for free:
	      * the selection SUCCEEDS - SelectHitText ends in ClearMarker, so the booking dies and the
	        marker is never seen (the point of the whole thing: an inversion under a highlight);
	      * the selection is REFUSED (overset, locked, hidden, stale) - it returns before that
	        ClearMarker, so the booking stands and the marker appears as it always did, which is the
	        rule that says a refusal still gets pointed at.

	    For the MOUSE only. The keyboard walk (KBSResultTreeEH) calls SetMarker through the ordinary
	    path: there is no such thing as a double arrow-key, so it has nothing to wait for.

	    @param db the marker's document - held as an address, and never dereferenced until the booking
	              fires and the document has been found on the document list again.
	    @param spreadUID the spread the match sits on - see SetMarker, which this eventually calls. */
	static void SetMarkerAfterClickSettles(IDataBase* db, UID spreadUID, const PMRect& pbRect);

	/** Application-shutdown cleanup: forget the marker WITHOUT repainting or touching any document,
	    and stop and release the booking timer.
	    ClearMarker is the wrong call there - it invalidates the views of the marker's document, which
	    by then may be half torn down - and sMarkerDocPath is a static PMString, so it has to be
	    emptied for the same reason KBSResultModel::ShutdownCleanup empties its own. The timer must go
	    for the harder reason: *ICallbackTimer's callback is a raw function pointer that is not
	    reference counted, so a booking left live as this .pln goes down is a crash. */
	static void ShutdownCleanup();
};

#endif // __KBSDrawEventHandler_h__
