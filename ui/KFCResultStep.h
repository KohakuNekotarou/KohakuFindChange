//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  NEXT RESULT / PREVIOUS RESULT (docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md A - my call, the
//  author having left the design to my recommendation): the panel menu's two actions, a key of the person's own assigned to them - the live
//  tier's next / previous hit row selected and landed on, as the list's own down / up arrow lands on a row
//  (KFCResultTreeEH's StockMoveThenLand), but from wherever InDesign's keyboard is: typing in the text, the person goes on
//  to the next match without touching the panel. Past the last row it goes round to the first (and the first's previous is
//  the last), as InDesign's Find Next and the QR Code tab's Find Next do (the spec's A4).
//
//========================================================================================

#pragma once
#ifndef __KFCResultStep_h__
#define __KFCResultStep_h__

namespace KFCResultStep
{
	/** ONE STEP (the spec's A2 - A5): the live tier's hit row after the rows selected (forward - after the lowest of them) or
	    before them (backward - before the highest), the rows between opened, that row selected alone and in view
	    (KFCResultTree::SelectAloneInView) and landed on (KFCJump::ActivateNode - its match selected, or marked when it cannot
	    be); none selected: the first (forward) or the last. The story, document and book rows are passed over, and so are a
	    grey tier's. The keyboard left where it was: on the list when the list held it, in the text when the text did.
	    Nothing under a landing or a run of ours (as the arrows - the status line says a run's why). false = no row to go to,
	    or the panel closed. */
	bool Step(bool forward);

	/** Can a step be taken - the panel open (the tree is reached through it - KFCResultTree::ResultListView) and a hit row
	    in the live tier? The two actions' grey (the spec's A6). */
	bool CanStep();
}

#endif // __KFCResultStep_h__
