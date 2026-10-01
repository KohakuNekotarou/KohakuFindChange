//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The UI half's startup/shutdown service (2026-10-01, the model/UI split). KBSStartupShutdown was one
//  service for both halves until then; a startup/shutdown service is declared per boss, and a model
//  plug-in's runs on background threads' start and end as well (guide vol1-07, "Threading and
//  startup/shutdown services"), so the panel's share - the saved settings, the windows it follows, the
//  marker's countdown, the ear on the model half - stands here, on a boss of its own. The model's share
//  stays in KBSStartupShutdown.cpp. Neither depends on the other having run: InDesign does not promise
//  the order two services are called in.
//
//  The notes below are KBSStartupShutdown's, carried with the calls.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IStartupShutdownService.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KFCUIID.h"
#include "KBSMarkerExpiryIdleTask.h"
#include "KBSPanelTitle.h"
#include "KBSPanelAlpha.h"		// "Translucent Panel": start following the panel, and stop cleanly
#include "KBSFindChangeMinimize.h"	// "Minimizable Find/Change": put the dialog's style back at the end
#include "KBSPanelState.h"		// the saved settings, read back before anything else runs
#include "KBSBookPanelPlacement.h"	// "Remember Book Panel Placement": stop following at the end
#include "KBSResultTree.h"		// the status line's static PMString
#include "KBSModelObserver.h"	// the UI half's ear on the model half (2026-10-01)

/** Implements IStartupShutdownService for the UI half. */
class KBSUIStartupShutdown : public CPMUnknown<IStartupShutdownService>
{
public:
	KBSUIStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	virtual ~KBSUIStartupShutdown() {}

	/** The panel and the jump marker's text adornment are resource-driven, so what starts here is the
	    subscription that keeps the "Translucent Panel" toggle applied across the panel being re-opened
	    or moved (see KBSPanelAlpha.cpp), and the observer that draws what the model half reports. */
	virtual void Startup()
	{
		// The saved settings first: restoring "Translucent Panel = ON" is what puts up the Win32
		// event hook, and doing it before the subscription below keeps the order the same as a
		// session where the user switches it on by hand.
		// *Not the only caller: KBSBookPanelPlacement::Start reads it too, and whichever comes first
		//  does the read (it is guarded to run once). The order above holds either way - the read
		//  still comes before the subscription below.
		KBSLoadPanelStateIfPresent();
		KBSAttachPanelVisibilityObserver();
		// The UI half's ear on the model half's notifications (2026-10-01, the model/UI split).
		KBSModelObserverAttach();
	}

	/** Put the panel tab's name back, retire the marker idle task (it must leave the queue, and never
	    be re-created, before the app tears down) and release the UI half's static storage. */
	virtual void Shutdown()
	{
		// The tab name first, while the UI is still standing: a tab renamed with the current scope
		// must not be what a saved workspace remembers.
		KBSPanelTitle::Restore();
		// Symmetric with the attach in Startup: while attached the session holds a pointer into this .pln.
		KBSModelObserverDetach();
		// Stop listening before tearing anything down: while attached, the session holds a pointer
		// into this .pln, and the panel being destroyed during teardown raises a notification.
		// *Symmetric with the KBSAttachPanelVisibilityObserver in Startup above - which is what
		//  KBSBookWatchDetach has always done for its own subject (2026-08-08).
		KBSDetachPanelVisibilityObserver();
		// "Remember Book Panel Placement" - the same subject, and the same reason, plus its one-shot
		// timer and its command interceptor (both hold raw pointers into this .pln). Normally already
		// done by the palette manager's PaletteMgrAboutToShutdown; this is the backstop for a
		// shutdown that never went through it. Safe to run twice.
		KBSBookPanelPlacement::ShutdownCleanup();
		// The Win32 event hook and the one-shot timer of the translucency toggle. *ICallbackTimer's
		// callback is a raw function pointer that is not reference counted, and a WinEvent hook left
		// up is a leaked resource - neither may outlive this .pln.
		KBSShutdownPanelAlpha();
		// InDesign's OWN Find/Change dialog again - its window STYLE this time. The same reason as
		// the WS_EX_LAYERED on the line above: it is somebody else's window, and what we put on it
		// must not outlive us. *It also restores a MINIMISED dialog before undoing anything - see
		//  KBSRestoreFindChangeStyle for why that order is not optional.
		KBSShutdownFindChangeMinimize();
		// The marker's countdown. (The marker's own state is the model half's, emptied by its own
		// shutdown - KBSHitMarker::ShutdownCleanup refuses every call after it, so a countdown that fires
		// in between finds nothing to take down, whichever service InDesign calls first.)
		KBSMarkerExpiryIdleTask::Shutdown();
		// ...and the line the panel last reported: a static PMString (see KBSResultTree::ShutdownCleanup).
		KBSResultTree::ShutdownCleanup();
	}
};

/* CREATE_PMINTERFACE
   Binds the C++ implementation class onto its ImplementationID.
*/
CREATE_PMINTERFACE(KBSUIStartupShutdown, kKBSUIStartupShutdownImpl)

// End, KBSUIStartupShutdown.cpp.
