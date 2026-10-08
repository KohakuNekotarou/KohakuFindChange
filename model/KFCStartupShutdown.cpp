//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Startup/shutdown service - THE MODEL HALF'S (the model/UI split; the panel's share is
//  KFCUIStartupShutdown.cpp). Its job is to start the book-close watcher and to empty the model's
//  file-static state during InDesign's controlled shutdown (on the main thread), so nothing can be
//  destructed against a half-torn-down application at DLL unload. Ported from KESCL's
//  KESCLStartupShutdown, minus the Excel machinery KFC does not have.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IStartupShutdownService.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KFCID.h"
#include "KFCHitMarker.h"		// the jump marker's static state, emptied at shutdown
#include "KFCBookScope.h"
#include "KFCBookWatch.h"
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"	// the remembered Find Format: an attribute list and a raw IDataBase*
#include "KFCUndoFollow.h"		// the writes kept for the panel's following of Undo

/** Implements IStartupShutdownService for the model half. */
class KFCStartupShutdown : public CPMUnknown<IStartupShutdownService>
{
public:
	KFCStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	virtual ~KFCStartupShutdown() {}

	/** The only thing the model half starts is the book-close watcher that retires book-scope results
	    (see KFCBookWatch.cpp). */
	virtual void Startup()
	{
		KFCBookWatchAttach();
	}

	/** Release the model's static storage, so every static destructor at DLL unload finds nothing left
	    to do. */
	virtual void Shutdown()
	{
		KFCBookWatchDetach();
		// State-only - the marker holds a static IDFile (its document's file) as well as a raw
		// IDataBase*, and neither may still be standing at DLL unload. Not ClearMarker: there is nothing to
		// repaint, and the document may be going away already. (Its countdown is the UI half's, retired by
		// KFCUIStartupShutdown; after this the marker refuses every call, so a countdown that fires in
		// between finds nothing to take down.)
		KFCHitMarker::ShutdownCleanup();
		KFCBookScope::ShutdownCleanup();
		KFCResultModel::ShutdownCleanup();
		// ...and the writes kept so that the panel can follow an Undo: each holds rows, and a query
		// run's the whole result set (KFCUndoFollow::RunRecorder) - PMStrings, the kind this list exists for.
		KFCUndoFollow::ShutdownCleanup();
		// (Every clean-up is a line of its own here, never nested inside another one: a nested one
		//  cannot be found by READING this list, which is the only way anyone ever checks.)
		// (The line the panel last reported - KFCResultTree::ShutdownCleanup - is the UI half's:
		//  KFCUIStartupShutdown.)
		// ...and the search engine's own: the Find Format it remembers is an AttributeBossList
		// holding references to the dialog's attributes, so letting it go is database work and
		// belongs here rather than in a static destructor at DLL unload.
		KFCSearchEngine::ShutdownCleanup();
	}
};

/* CREATE_PMINTERFACE
   Binds the C++ implementation class onto its ImplementationID.
*/
CREATE_PMINTERFACE(KFCStartupShutdown, kKFCStartupShutdownImpl)

// End, KFCStartupShutdown.cpp.
