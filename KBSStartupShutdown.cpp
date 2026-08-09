//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Startup/shutdown service. Its job is to retire the marker idle task and empty the module's
//  file-static state during InDesign's controlled shutdown (on the main thread), so nothing can
//  fire into - or be destructed against - a half-torn-down application at DLL unload. Ported from
//  KESCL's KESCLStartupShutdown, minus the Excel machinery KBS does not have.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IStartupShutdownService.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KBSID.h"
#include "KBSDrawEventHandler.h"	// the jump marker's static state, emptied at shutdown
#include "KBSMarkerExpiryIdleTask.h"
#include "KBSBookScope.h"
#include "KBSBookWatch.h"
#include "KBSPanelTitle.h"
#include "KBSPanelAlpha.h"		// "Translucent Panel": start following the panel, and stop cleanly
#include "KBSPanelState.h"		// the saved settings, read back before anything else runs
#include "KBSReplaceConfirmDialog.h"	// the last prompt's text and fonts, emptied at shutdown
#include "KBSResultModel.h"
#include "KBSResultTree.h"		// the status line's static PMString
#include "KBSSearchEngine.h"	// the remembered Find Format: an attribute list and a raw IDataBase*

/** Implements IStartupShutdownService for the plug-in. */
class KBSStartupShutdown : public CPMUnknown<IStartupShutdownService>
{
public:
	KBSStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	virtual ~KBSStartupShutdown() {}

	/** The panel and the draw-event service are resource-driven, so the only things to start are the
	    book-close watcher that retires book-scope results (see KBSBookWatch.cpp) and the subscription
	    that keeps the "Translucent Panel" toggle applied across the panel being re-opened or moved
	    (see KBSPanelAlpha.cpp). */
	virtual void Startup()
	{
		// The saved settings first: restoring "Translucent Panel = ON" is what puts up the Win32
		// event hook, and doing it before the subscription below keeps the order the same as a
		// session where the user switches it on by hand.
		KBSLoadPanelStateIfPresent();
		KBSBookWatchAttach();
		KBSAttachPanelVisibilityObserver();
	}

	/** Put the panel tab's name back, retire the marker idle task (it must leave the queue, and
	    never be re-created, before the app tears down) and release the module's static storage, so
	    every static destructor at DLL unload finds nothing left to do. */
	virtual void Shutdown()
	{
		// The tab name first, while the UI is still standing: a tab renamed with the current scope
		// must not be what a saved workspace remembers.
		KBSPanelTitle::Restore();
		KBSBookWatchDetach();
		// Stop listening before tearing anything down: while attached, the session holds a pointer
		// into this .pln, and the panel being destroyed during teardown raises a notification.
		// *Symmetric with the KBSAttachPanelVisibilityObserver in Startup above - which is what
		//  KBSBookWatchDetach on the line before has always done for its own subject (2026-08-08).
		KBSDetachPanelVisibilityObserver();
		// The Win32 event hook and the one-shot timer of the translucency toggle. *ICallbackTimer's
		// callback is a raw function pointer that is not reference counted, and a WinEvent hook left
		// up is a leaked resource - neither may outlive this .pln.
		KBSShutdownPanelAlpha();
		KBSMarkerExpiryIdleTask::Shutdown();
		// After the task that would clear it, and state-only - the marker holds a static PMString
		// (its document's file) as well as a raw IDataBase*, and neither may still be standing at
		// DLL unload. Not ClearMarker: that repaints, and the document may be going away already.
		KBSDrawEventHandler::ShutdownCleanup();
		KBSBookScope::ShutdownCleanup();
		KBSResultModel::ShutdownCleanup();
		// ...and the line the panel last reported: a static PMString of exactly the kind the marker
		// holds. It joined this list on 2026-08-08, having stood through both of the sweeps that
		// wrote the rule down (see KBSResultTree::ShutdownCleanup).
		KBSResultTree::ShutdownCleanup();
		// ...and the search engine's own: the Find Format it remembers is an AttributeBossList
		// holding references to the dialog's attributes, so letting it go is database work and
		// belongs here rather than in a static destructor at DLL unload. It was the one piece of
		// module state with no cleanup of its own until 2026-08-08.
		KBSSearchEngine::ShutdownCleanup();
		// ...and the replace confirmation's: the fonts its last prompt resolved (already empty
		// between prompts) and the prompt text itself, which had no emptier at all until the
		// 2026-08-09 sweep found it - the fifth static PMString this list has gained one by one.
		KBSReplaceConfirmDialog::ShutdownCleanup();
	}
};

/* CREATE_PMINTERFACE
   Binds the C++ implementation class onto its ImplementationID.
*/
CREATE_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)

// End, KBSStartupShutdown.cpp.
