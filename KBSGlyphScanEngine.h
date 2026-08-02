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

	    Changes NOTHING in the document: no command, no command sequence, nothing to undo. */
	void Run();
}

#endif // __KBSGlyphScanEngine_h__
