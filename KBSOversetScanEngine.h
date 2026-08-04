//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The overset scanner: finds text that did not fit - the red "+" on a frame and the red dot on a
//  table cell. Separate from KBSSearchEngine and from KBSGlyphScanEngine because it asks a third
//  question. The search runs the user's query through a text walker; the glyph scan reads the
//  COMPOSED result and asks each run of glyphs whether it came out as notdef; this one asks what
//  was not composed at all.
//
//  Design: docs/superpowers/specs/2026-08-02-kbs-book-overset-scan-design.md
//  Plan:   docs/superpowers/plans/2026-08-02-kbs-book-overset-scan.md
//
//========================================================================================

#ifndef __KBSOversetScanEngine_h__
#define __KBSOversetScanEngine_h__

namespace KBSOversetScanEngine
{
	/** Scan the current scope for overset text and fill KBSResultModel with what was found.
	    Puts its own summary on the panel's status line (so app.kfcStatus can be read back).

	    Changes NOTHING in the document: no command, no command sequence, nothing to undo.

	    Refuses to run while any other KBS run is up - see KBSRunGuard. */
	void Run();

	/** Is an overset scan running right now? Same reasoning as the glyph scan's own IsScanning -
	    the modal progress bar pumps events, so a command can be dispatched into this run, and
	    anything that hands the held chapters back underneath it leaves this loop holding a dead
	    IDataBase. Asked through KBSRunGuard::IsAnyRunning. */
	bool IsScanning();
}

#endif // __KBSOversetScanEngine_h__
