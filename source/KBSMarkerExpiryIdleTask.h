//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  One-shot timer that takes the jump marker back off the screen shortly after it appears.
//  Driven by KBSDrawEventHandler: SetMarker arms it, ClearMarker disarms it. This is the plugin's
//  only CIdleTask - the single justified exception to "avoid idle tasks" (a marker has to expire on
//  wall-clock time, which nothing else in this plug-in needs).
//
//  ***** WHY NOT ICallbackTimer, WHICH KBS USES TWICE ELSEWHERE. ***** The header used to say an
//  idle task was the SDK's only main-thread "call me back in n ms". It is not: ICallbackTimer is
//  one too (ICallbackTimer.h:38 - it derives from IIdleTask) and KBSBookWatch.cpp:220 and
//  KBSPanelAlpha.cpp:657 both use it. It is not taken here because its callback is a plain function
//  pointer that nothing reference-counts - its own header spends six words on "Danger!" saying the
//  supplying plug-in must not be unloaded while that pointer is in the timer, and KBSPanelAlpha.h:110
//  records the same hazard from experience. A CIdleTask is an interface on a boss: it can be
//  Released at shutdown and it takes part in KBSStartupShutdown's teardown like everything else.
//  (Corrected in the block 12 API audit, 2026-08-08 - the DECISION was right, the reason given for
//  it was not.)
//
//  Ported from KESCL's KESCLMarkerExpiryIdleTask so the proven, robust teardown is kept exactly.
//
//========================================================================================

#ifndef __KBSMarkerExpiryIdleTask_h__
#define __KBSMarkerExpiryIdleTask_h__

/** Jump-marker expiry timer. Only KBSDrawEventHandler should drive this - going through
    SetMarker / ClearMarker keeps the marker state and the timer in step. */
namespace KBSMarkerExpiryIdleTask
{
	/** (Re)start the countdown to clearing the marker. Called every time a marker is shown, so an
	    already-running countdown starts over: each jump shows its marker for the full time. */
	void Start();

	/** Cancel the countdown. Safe to call when it is not running. */
	void Stop();

	/** Release the idle task for good (application shutdown). After this, Start() is a no-op. */
	void Shutdown();
}

#endif // __KBSMarkerExpiryIdleTask_h__
