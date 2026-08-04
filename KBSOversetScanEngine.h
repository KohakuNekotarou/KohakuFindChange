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
	/** Does this story hold ANY text that did not fit, RIGHT NOW? Its main thread, or any cell of any
	    table in it (nested tables included) - the same two questions the scan itself asks, asked in
	    the same order, of the same interface (ITextParcelList::GetIsOverset). So this plug-in has ONE
	    answer to "is this overset" rather than two that can drift apart.

	    Shared with the glyph scan, which has to say on its status line whether part of the document
	    could not be looked at. It used to ask IFrameList::GetWasOverset() instead, and that is a
	    different question: the frame list remembers what the LAST COMPOSITION found and the answer
	    is persisted, so a document whose overset has since been fixed still says yes - it survived
	    a close and reopen when this was measured (2026-08-04, adv-index2.indd). The two scans then
	    disagreed about one document, the glyph scan claiming text it could not check while the
	    overset scan correctly reported none.

	    ***** The cells are asked about for the same reason the frames are. ***** Text that did not
	    compose carries no glyphs to read wherever it sits, so a cell overflowing on its own hides
	    missing glyphs exactly as a pushed-out frame does, and the scan has to admit it either way.
	    An earlier version of this function asked only about the main thread, which left the glyph
	    scan silent about documents Find Overset had findings for. */
	bool StoryHasAnyOverset(ITextModel* model);

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
