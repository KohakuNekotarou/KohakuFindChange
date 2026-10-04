//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  One-shot timer that takes the jump marker back off the screen shortly after it appears.
//  Driven by KFCHitMarkerView (the UI half of KFCHitMarker): Show arms it, Hide disarms it. This is
//  the plug-in's only CIdleTask - the single justified exception to "avoid idle tasks" (a marker has
//  to expire on wall-clock time).
//
//  WHY NOT ICallbackTimer, WHICH KFC USES ELSEWHERE. It is a main-thread "call me back in n ms" too
//  (ICallbackTimer.h:38 - it derives from IIdleTask), and this plug-in uses it elsewhere (a grep for
//  ICallbackTimer lists them - KFCBookWatch, KFCPanelAlpha and others). It is not taken here
//  because its callback is a plain function pointer that nothing reference-counts - its own header
//  spends six words on "Danger!" saying the supplying plug-in must not be unloaded while that
//  pointer is in the timer, and the note on KFCShutdownPanelAlpha (KFCPanelAlpha.h) records the same
//  hazard. A CIdleTask is an interface on
//  a boss: it can be Released at shutdown, and it takes part in KFCUIStartupShutdown's teardown like
//  everything else.
//
//  Ported from KESCL's KESCLMarkerExpiryIdleTask so the proven, robust teardown is kept exactly.
//
//========================================================================================

#ifndef __KFCMarkerExpiryIdleTask_h__
#define __KFCMarkerExpiryIdleTask_h__

/** Jump-marker expiry timer. Only KFCHitMarkerView should drive this - going through
    Show / Hide keeps the marker state and the timer in step. */
namespace KFCMarkerExpiryIdleTask
{
	/** (Re)start the countdown to clearing the marker. Called every time a marker is shown, so an
	    already-running countdown starts over: each jump shows its marker for the full time. */
	void Start();

	/** Cancel the countdown. Safe to call when it is not running. */
	void Stop();

	/** Release the idle task for good (application shutdown). After this, Start() is a no-op. */
	void Shutdown();
}

#endif // __KFCMarkerExpiryIdleTask_h__
