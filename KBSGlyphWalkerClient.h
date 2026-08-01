//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The missing-glyph scan's OWN text-walker client - the walker-driven alternative to the
//  direct scan in KBSGlyphScanEngine.
//
//  Why a client of our own at all. KBS's search walks with the stock kFindChangeClientBoss,
//  which is right there: the find/change command drives the walk and hands the matches back.
//  A notdef scan has no find/change query to run, so there is no command to drive - the walk
//  has to be driven directly (ITextWalker::Walk), and Walk calls back into a CLIENT. The stock
//  one answers a question we are not asking.
//
//  Two things come free with a client of our own, both of which the stock one cannot give:
//    * REAL progress. ITextWalkerProgressMonitor is only a place to PARK a RangeProgressBar -
//      the client's own OnNextPosition is what has to call SetPosition on it, and the stock
//      find/change client does not (measured 2026-07-31: it registered fine and was then never
//      called once in 5270 matches). So a moving bar inside a walk needs this file to exist.
//    * The ranges themselves, which is what the experiment is about - see the .cpp.
//
//  Official shape followed: source/open/components/spellpanel (PRODUCT code, not a sample).
//    * the client class + CREATE_PMINTERFACE + HELPER_METHODS = SpellReplaceWalker.cpp:142-197
//    * the boss carrying IID_ITEXTWALKERCLIENT alongside the stock
//      kTextWalkerProgressMonitorImpl = SpellPanelClass.fr:548-557
//    * a BATCH client moving itself along with pWalker->MoveTo = LinguisticTestMenu.cpp's
//      HyphenateStoryWalker (:1463-1494). The spell check client instead SUSPENDS at each hit,
//      which is right for Find Next and wrong for a scan that collects everything in one go.
//
//========================================================================================
#ifndef __KBSGlyphWalkerClient_h__
#define __KBSGlyphWalkerClient_h__

#include "BaseType.h"		// int32
#include "PMString.h"

/** What the last walk saw. Module state rather than data on the client boss: the boss is created
    for the walk and released straight after, so anything kept on it would die with it - and the
    engine that started the walk is the one that has to report what happened. (KBSResultModel keeps
    its own flags the same way.) */
namespace KBSGlyphWalkerData
{
	/** Forget the previous walk. Call before ITextWalker::Walk(). */
	void Reset();

	/** How many notdef glyphs the walk found. */
	int32 GetHitCount();

	/** How many times the walker handed the client a range to look at. */
	int32 GetRangeCount();

	/** How many of those ranges had a table anchor inside them.

	    * This is the number the experiment turns on. A raw SearchForGlyph call takes InDesign
	    down when the range it is given contains a table anchor and carries on past it (measured
	    2026-08-01). If the walker never offers such a range, the walker-driven scan is safe by
	    construction and nothing has to be split; if it does offer one, this says so. */
	int32 GetRangesWithAnchor();

	/** How many stories the walk visited. */
	int32 GetStoryCount();

	/** The first few hits as "[start-end]", for the status line. */
	const PMString& GetDigest();
}

#endif // __KBSGlyphWalkerClient_h__
