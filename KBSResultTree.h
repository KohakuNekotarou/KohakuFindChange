//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result tree rebuild entry point. Called after KBSResultModel has been filled by a search:
//  reloads the panel's tree widget from the model and expands every chapter so the hit rows are
//  visible (expanding also PRIMES each chapter's expander arrow so it is actually drawn - the
//  tree framework draws no arrow for a node whose children were never materialized). No-op when
//  the panel is closed. Implemented in KBSResultListWidgetMgr.cpp (it lives with the tree).
//
//========================================================================================

#ifndef __KBSResultTree_h__
#define __KBSResultTree_h__

#include "PMString.h"

namespace KBSResultTree
{
	/** (Re)load the panel's result tree from KBSResultModel and expand every chapter. Safe to
	    call when the panel is closed (does nothing then). */
	void Rebuild();

	/** Repaint the existing rows from the model WITHOUT rebuilding the tree. For changes that touch
	    only what a row DRAWS - the check boxes behind Check All / Uncheck All - where the tree's
	    shape (which chapters, how many hits) is untouched.

	    Costs one notification per CHAPTER, not per hit: NodeChanged carries childrenChangedAlso, so
	    the framework refreshes each chapter's hit rows itself. Rebuild() by contrast tears the whole
	    tree down and re-expands it, which is what made a large result set expensive.

	    It also KEEPS the expansion state, so a chapter the user collapsed stays collapsed (Rebuild
	    re-expands everything). Safe to call when the panel is closed (does nothing then). */
	void RefreshRows();

	/** Write a one-line message to the panel's status read-out (its single-line StaticText). Safe
	    to call when the panel is closed (does nothing then). Lives with the tree because it reaches
	    the panel exactly the way Rebuild does. */
	void ShowStatus(const PMString& message);

	/** Write "<checked> / <total> checked." to the panel's status line (and note the display cap
	    when the result set is bigger than the panel shows). Called after any check change. */
	void ShowCheckedStatus();
}

#endif // __KBSResultTree_h__
