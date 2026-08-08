//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Jump-marker expiry timer (KBSMarkerExpiryIdleTask.h). One-shot: it fires once, clears the
//  marker and takes itself off the queue. Ported from KESCL's KESCLMarkerExpiryIdleTask - the
//  install/uninstall ordering and the shutdown retire are kept exactly (they are the robust,
//  crash-free teardown this whole task exists to preserve).
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CIdleTask.h"
#include "IIdleTaskMgr.h"

#include "KBSID.h"
#include "KBSMarkerExpiryIdleTask.h"
#include "KBSDrawEventHandler.h"

// How long the marker stays up. Short on purpose: it points at the hit, and the view has already
// been scrolled so the hit is on screen anyway.
static const uint32 kKBSMarkerLifetimeMs = 1000;

// ---- Shared state (private to this translation unit) ----
//
// ***** THERE IS NO "IS IT RUNNING" FLAG HERE, AND THERE MUST NOT BE. ***** Whether the task is
// sitting in the idle queue is the BASE CLASS's business: CIdleTask keeps it in fCurrentlyInstalled
// (CIdleTask.h:64) and InstallTask / UninstallTask maintain it. A second copy in this file could
// only ever disagree with it - and disagreeing in one direction is illegal, not merely untidy:
// IIdleTaskMgr.h:84 says "it is illegal to add the same task twice", which is what a stale "not
// running" would lead to. Calling UninstallTask when nothing is installed is free: RemoveTask
// "returns kEndOfTime" when "the task wasn't installed or it is currently running"
// (IIdleTaskMgr.h:95-98) and does nothing else. So every entry point below simply uninstalls first
// and asks no questions - the shape Adobe's own re-arming code uses
// (spellpanel/DynSpellCheckEventWatcher.cpp:138,145 on every keystroke, :178 to stop).
// A flag lived here until the block 12 API audit, 2026-08-08.
static IIdleTask* sTask     = nil;		// the task object (created once, reused). Released in Shutdown
static bool16     sShutdown = kFalse;	// set at application shutdown: never create/schedule again

//========================================================================================
// KBSMarkerExpiryTask - minimal CIdleTask: only RunTask / TaskName. InstallTask / UninstallTask
// come from CIdleTask (which calls the IdleTaskMgr's AddTask / RemoveTask). Named differently
// from the KBSMarkerExpiryIdleTask namespace, which holds the public entry points.
//========================================================================================
class KBSMarkerExpiryTask : public CIdleTask
{
public:
	KBSMarkerExpiryTask(IPMUnknown* boss) : CIdleTask(boss) {}

	virtual uint32 RunTask(uint32 flags, IdleTimer* idleTimer);
	virtual const char* TaskName() { return "KBSMarkerExpiryTask"; }
};

CREATE_PMINTERFACE(KBSMarkerExpiryTask, kKBSMarkerExpiryIdleTaskImpl)

uint32 KBSMarkerExpiryTask::RunTask(uint32 /*flags*/, IdleTimer* /*idleTimer*/)
{
	// Take ourselves off the queue, then clear. CIdleTask.h:36-38 asks for exactly this - "Don't
	// return kEndOfTime from RunTask, instead you would call UninstallTask and return any value
	// from RunTask as it will be ignored".
	//
	// ClearMarker calls back into Stop(), which uninstalls again. That second call is harmless by
	// the contract quoted at the top of this file (a task that is not installed, or is currently
	// running, costs a return value and nothing more).
	this->UninstallTask();

	if (!sShutdown)
		KBSDrawEventHandler::ClearMarker();

	return 0;	// one-shot: nothing more to do
}

//========================================================================================
// Public entry points
//========================================================================================
void KBSMarkerExpiryIdleTask::Start()
{
	if (sShutdown)
		return;

	if (sTask == nil)
		sTask = ::CreateObject2<IIdleTask>(kKBSMarkerExpiryIdleTaskBoss);
	if (sTask == nil)
		return;		// no timer; the marker simply stays up until the next jump/search clears it.

	// Restart rather than let a running countdown stand: the marker was just (re)shown, so it is
	// owed the full lifetime from now. Uninstall unconditionally - a pending booking has to come off
	// before a new one goes on ("it is illegal to add the same task twice"), and if there is no
	// pending booking this costs nothing.
	sTask->UninstallTask();
	sTask->InstallTask(kKBSMarkerLifetimeMs);
}

void KBSMarkerExpiryIdleTask::Stop()
{
	if (sTask != nil)
		sTask->UninstallTask();
}

void KBSMarkerExpiryIdleTask::Shutdown()
{
	sShutdown = kTrue;	// no re-arming from here on
	if (sTask != nil)
	{
		sTask->UninstallTask();
		sTask->Release();
		sTask = nil;
	}
}

// End, KBSMarkerExpiryIdleTask.cpp.
