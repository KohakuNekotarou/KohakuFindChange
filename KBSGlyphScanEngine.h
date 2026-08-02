//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The missing-glyph (notdef) scanner: finds where InDesign is drawing a box instead of a
//  character, because the font in force has no glyph for it. Separate from KBSSearchEngine
//  because it asks a completely different question - that one runs the user's Find/Change query
//  through a text walker, this one reads the COMPOSED result and asks each run of glyphs whether
//  it came out as notdef.
//
//  Design: docs/superpowers/specs/2026-08-01-kbs-missing-glyph-search-design.md
//  Plan:   docs/superpowers/plans/2026-08-01-kbs-missing-glyph-search.md
//
//========================================================================================

#ifndef __KBSGlyphScanEngine_h__
#define __KBSGlyphScanEngine_h__

namespace KBSGlyphScanEngine
{
	/** Scan the current scope for notdef glyphs and fill KBSResultModel with what was found.
	    Puts its own summary on the panel's status line (so app.kbsStatus can be read back).

	    Changes NOTHING in the document: no command, no command sequence, nothing to undo.

	    Refuses to run while any other KBS run is up - see KBSRunGuard. */
	void Run();

	/** Is a glyph scan running right now? Its progress bar is modal but PUMPS EVENTS, so a menu
	    command can be dispatched while the run is standing in Run() - the same hazard the search
	    guards against with KBSSearchEngine::IsSearching. What it costs here: the scan holds a list
	    of chapter documents, and anything that calls KBSBookScope::ReleaseHeldDocs underneath it
	    closes those very documents, leaving this loop holding a dead IDataBase.

	    Asked through KBSRunGuard::IsAnyRunning rather than directly, so no caller has to remember
	    the whole list of things that can be running. */
	bool IsScanning();
}

#endif // __KBSGlyphScanEngine_h__
