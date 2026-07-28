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
#include "KBSMarkerExpiryIdleTask.h"
#include "KBSBookScope.h"
#include "KBSBookWatch.h"
#include "KBSPanelTitle.h"
#include "KBSResultModel.h"

/** Implements IStartupShutdownService for the plug-in. */
class KBSStartupShutdown : public CPMUnknown<IStartupShutdownService>
{
public:
	KBSStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	virtual ~KBSStartupShutdown() {}

	/** The panel and the draw-event service are resource-driven, so the only thing to start is the
	    book-close watcher that retires book-scope results (see KBSBookWatch.cpp). */
	virtual void Startup()
	{
		KBSBookWatchAttach();
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
		KBSMarkerExpiryIdleTask::Shutdown();
		KBSBookScope::ShutdownCleanup();
		KBSResultModel::ShutdownCleanup();
	}
};

/* CREATE_PMINTERFACE
   Binds the C++ implementation class onto its ImplementationID.
*/
CREATE_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)

// End, KBSStartupShutdown.cpp.
