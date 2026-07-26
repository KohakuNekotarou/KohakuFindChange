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
	static IDataBase* sMarkerDB;		// the document the marker belongs to

	// Set / clear the current marker and repaint. SetMarker also starts the marker's countdown to
	// clearing itself; ClearMarker cancels that countdown. Called by KBSJump.
	static void SetMarker(IDataBase* db, const PMRect& pbRect);
	static void ClearMarker();
};

#endif // __KBSDrawEventHandler_h__
