//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The UI half's startup/shutdown service (the model/UI split). A startup/shutdown service is declared
//  per boss, and the panel's share - the saved settings, the windows it follows, the marker's countdown,
//  the ear on the model half - is UI work: it stands here, on a boss of its own in the UI half. (A model
//  plug-in's service can also be called at each background thread's start and end - guide vol1-07,
//  "Threading and startup/shutdown services"; KFC's model one is main-thread only, model/KFC.fr says why.)
//  The model's share is in KFCStartupShutdown.cpp. Neither depends on the other having run: InDesign
//  does not promise the order two services are called in.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IStartupShutdownService.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KFCUIID.h"
#include "KFCMarkerExpiryIdleTask.h"
#include "KFCPanelAlpha.h"		// "Translucent Panel": start following the panel, and stop cleanly
#include "KFCFindChangeMinimize.h"	// "Minimizable Find/Change": put the dialog's style back at the end
#include "KFCAppBarSearchEnter.h"	// "Link the Application Bar's Search Field to This Panel": its hooks off at the end
#include "KFCPanelState.h"		// the saved settings, read back before anything else runs
#include "KFCBookPanelPlacement.h"	// "Remember Book Panel Placement": stop following at the end
#include "KFCResultTree.h"		// the status line's static PMString
#include "KFCQueryOrder.h"		// the query dialog's two lists - static vectors
#include "KFCResultHistory.h"	// the results kept in tiers - static vectors of copied rows
#include "KFCModelObserver.h"	// the UI half's ear on the model half

/** Implements IStartupShutdownService for the UI half. */
class KFCUIStartupShutdown : public CPMUnknown<IStartupShutdownService>
{
public:
	KFCUIStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	virtual ~KFCUIStartupShutdown() {}

	/** The panel and the jump marker's text adornment are resource-driven, so what starts here is the
	    subscription that keeps the "Translucent Panel" toggle applied across the panel being re-opened
	    or moved (see KFCPanelAlpha.cpp), and the observer that draws what the model half reports. */
	virtual void Startup()
	{
		// The saved settings first: restoring "Translucent Panel = ON" is what puts up the Win32
		// event hook, and doing it before the subscription below keeps the order the same as a
		// session where the user switches it on by hand.
		// *Not the only caller: KFCBookPanelPlacement::Start reads it too, and whichever comes first
		//  does the read (it is guarded to run once). The order above holds either way - the read
		//  still comes before the subscription below.
		KFCLoadPanelStateIfPresent();
		KFCAttachPanelVisibilityObserver();
		// The UI half's ear on the model half's notifications (the model/UI split).
		KFCModelObserverAttach();
	}

	/** Take down everything that holds a pointer into this .pln - the observers, the hooks, the timers,
	    the Return filter and the marker's idle task (which must leave the queue, and never be re-created,
	    before the app tears down) - put back what KFC changed on InDesign's own windows, and release the
	    UI half's static storage. The panel tab's scoped name is not put back: a label lives only in the
	    running palette (KFCPanelTitle.h). */
	virtual void Shutdown()
	{
		// Symmetric with the attach in Startup: while attached the session holds a pointer into this .pln.
		KFCModelObserverDetach();
		// Stop listening before tearing anything down: while attached, the panel manager and the
		// application each hold a pointer to an observer whose code is in this .pln (KFCPanelAlpha.cpp,
		// KFCAttachPanelVisibilityObserver), and the panel being destroyed during teardown raises a
		// notification.
		// *Symmetric with the KFCAttachPanelVisibilityObserver in Startup above - as KFCBookWatchDetach
		//  is for its own subject.
		KFCDetachPanelVisibilityObserver();
		// "Remember Book Panel Placement" - the same subject, and the same reason, plus its timers (the
		// restore's and the Home screen's) and its command interceptor, all holding raw pointers into
		// this .pln. Normally already done by the palette manager's PaletteMgrAboutToShutdown; this is
		// the backstop for a shutdown that never went through it. Safe to run twice.
		KFCBookPanelPlacement::ShutdownCleanup();
		// The translucency toggles': the Win32 event hook, the reapply timer, and the WS_EX_LAYERED KFC
		// put on InDesign's own Find/Change dialog, taken back off. *ICallbackTimer's callback is a raw
		// function pointer that is not reference counted, and a WinEvent hook left up is a leaked
		// resource - neither may outlive this .pln.
		KFCShutdownPanelAlpha();
		// InDesign's OWN Find/Change dialog again - its window STYLE this time, after the retry timer that
		// books it is let go. The same reason as the WS_EX_LAYERED on the line above: it is somebody
		// else's window, and what we put on it must not outlive us. *It also restores a MINIMISED dialog
		// before undoing anything - see KFCRestoreFindChangeStyle for why that order is not optional.
		KFCShutdownFindChangeMinimize();
		// The Application Bar link's hooks, its observer and its pending search and write-back: the
		// hook procedures and the timers' callbacks are raw pointers into this .pln -
		// KFCShutdownAppBarSearchEnter.
		KFCShutdownAppBarSearchEnter();
		// The marker's countdown. (The marker's own state is the model half's - KFCStartupShutdown.cpp
		// says why either order of the two services is safe.)
		KFCMarkerExpiryIdleTask::Shutdown();
		// ...and the line the panel last reported - a static PMString - and the Return filter the list pushes on
		// the application's event dispatcher: both in KFCResultTree::ShutdownCleanup.
		KFCResultTree::ShutdownCleanup();
		// ...and the query dialog's two lists: static vectors of PMStrings and IDFiles.
		KFCQueryOrder::ShutdownCleanup();
		// ...and the results kept in tiers: their copies of the rows are PMStrings in static vectors, and the tier
		// switch's booking timer (KFCResultHistory).
		KFCResultHistory::ShutdownCleanup();
	}
};

/* CREATE_PMINTERFACE
   Binds the C++ implementation class onto its ImplementationID.
*/
CREATE_PMINTERFACE(KFCUIStartupShutdown, kKFCUIStartupShutdownImpl)

// End, KFCUIStartupShutdown.cpp.
