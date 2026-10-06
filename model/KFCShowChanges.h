//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A QUERY RUN'S LIST FROM ITS OWN SIGNED RECORDS (KFCQuerySequence) - what is left of "Show Changes by
//  KohakuFindChange" (spec docs/superpowers/specs/2026-09-29-kbs-show-changes-design.md), which went with
//  Track Changes on 2026-10-06 (docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md F2). Every
//  replace KFC makes is still signed "KohakuFindChange" at a time KFC hands out (KFCTrackChange.h, the head), so a
//  query run's records say which rows it wrote. Until Task 6 of
//  docs/superpowers/plans/2026-10-06-kfc-no-track-change-all.md, which writes a query run with InDesign's Change All
//  and lists nothing.
//
//========================================================================================

#ifndef __KFCShowChanges_h__
#define __KFCShowChanges_h__

#include "PMString.h"
#include "KFCBookScope.h"		// ChapterDoc - a query run's documents (ListOwnRun)

#include <vector>

namespace KFCShowChanges
{
	/** A QUERY RUN'S LIST (KFCQuerySequence, 2026-10-04): every record signed "KohakuFindChange" at or after `floor`
	    in these OPEN documents, in ONE list: each row reads the text before the run and after it. Nothing is opened
	    or closed; the caller cleared the model and set its header. Stops at kKFCCollectHitLimit (outCapped). Returns
	    how many rows went in. */
	int32 ListOwnRun(const std::vector<KFCBookScope::ChapterDoc>& docs, uint64 floor, bool& outCapped);
}

#endif // __KFCShowChanges_h__
