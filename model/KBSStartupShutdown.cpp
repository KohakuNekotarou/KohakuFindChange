//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Startup/shutdown service - THE MODEL HALF'S (the model/UI split; the panel's share is
//  KBSUIStartupShutdown.cpp). Its job is to start the book-close watcher and to empty the model's
//  file-static state during InDesign's controlled shutdown (on the main thread), so nothing can be
//  destructed against a half-torn-down application at DLL unload. Ported from KESCL's
//  KESCLStartupShutdown, minus the Excel machinery KBS does not have.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IStartupShutdownService.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KBSID.h"
#include "KBSHitMarker.h"		// the jump marker's static state, emptied at shutdown
#include "KBSBookScope.h"
#include "KBSBookWatch.h"
#include "KBSResultModel.h"
#include "KBSSearchEngine.h"	// the remembered Find Format: an attribute list and a raw IDataBase*
#include "KBSUndoFollow.h"		// the writes kept for the panel's following of Undo

/** Implements IStartupShutdownService for the model half. */
class KBSStartupShutdown : public CPMUnknown<IStartupShutdownService>
{
public:
	KBSStartupShutdown(IPMUnknown* boss) : CPMUnknown<IStartupShutdownService>(boss) {}
	virtual ~KBSStartupShutdown() {}

	/** The only thing the model half starts is the book-close watcher that retires book-scope results
	    (see KBSBookWatch.cpp). */
	virtual void Startup()
	{
		KBSBookWatchAttach();
	}

	/** Release the model's static storage, so every static destructor at DLL unload finds nothing left
	    to do. */
	virtual void Shutdown()
	{
		KBSBookWatchDetach();
		// State-only - the marker holds a static IDFile (its document's file) as well as a raw
		// IDataBase*, and neither may still be standing at DLL unload. Not ClearMarker: there is nothing to
		// repaint, and the document may be going away already. (Its countdown is the UI half's, retired by
		// KBSUIStartupShutdown; after this the marker refuses every call, so a countdown that fires in
		// between finds nothing to take down.)
		KBSHitMarker::ShutdownCleanup();
		KBSBookScope::ShutdownCleanup();
		KBSResultModel::ShutdownCleanup();
		// ...and the writes kept so that the panel can follow an Undo: each holds rows, and Change
		// Checked's the whole result set - PMStrings, the kind this list exists for.
		KBSUndoFollow::ShutdownCleanup();
		// (Every clean-up is a line of its own here, never nested inside another one: a nested one
		//  cannot be found by READING this list, which is the only way anyone ever checks.)
		// (The line the panel last reported - KBSResultTree::ShutdownCleanup - is the UI half's:
		//  KBSUIStartupShutdown.)
		// ...and the search engine's own: the Find Format it remembers is an AttributeBossList
		// holding references to the dialog's attributes, so letting it go is database work and
		// belongs here rather than in a static destructor at DLL unload.
		KBSSearchEngine::ShutdownCleanup();
	}
};

/* CREATE_PMINTERFACE
   Binds the C++ implementation class onto its ImplementationID.
*/
CREATE_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)

// End, KBSStartupShutdown.cpp.
