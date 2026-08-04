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

class ITextModel;

namespace KBSOversetScanEngine
{
	/** Is this story's MAIN thread overset RIGHT NOW?

	    Shared with the glyph scan, which has to say on its status line whether part of the document
	    could not be looked at. It used to ask IFrameList::GetWasOverset() instead, and that is a
	    different question: the frame list remembers what the LAST COMPOSITION found and the answer
	    is persisted, so a document whose overset has since been fixed still says yes - it survived
	    a close and reopen when this was measured (2026-08-04, adv-index2.indd). The two scans then
	    disagreed about one document, the glyph scan claiming text it could not check while the
	    overset scan correctly reported none.

	    Asked of the same interface the overset scan itself uses (ITextParcelList::GetIsOverset), so
	    there is one answer to "is this overset" in this plug-in rather than two.

	    The MAIN thread only - position 0 - which is the same reach the frame-list question had. A
	    table cell overflowing on its own is a separate thread and is NOT included here; the overset
	    scan reports those, and the glyph scan reads cells through the wax like any other text. */
	bool IsStoryOverset(ITextModel* model);

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
