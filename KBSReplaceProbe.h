//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  TEMPORARY measurement probe for the "A" experiment: can IReplaceAllTextData + DoReplaceAll
//  (Adobe's own Change All route) carry KBS's "replace the checked rows only"?
//
//  This file is NOT part of the shipping feature set. It writes to the document but never to
//  KBSResultModel, so the panel keeps showing what the search found and Ctrl+Z is the way back.
//  Delete this file, its .cpp, and their ID / menu / project entries once the experiment has
//  been decided either way. See docs/superpowers/specs/2026-07-31-kbs-replace-all-probe-design.md
//
//========================================================================================

#ifndef __KBSReplaceProbe_h__
#define __KBSReplaceProbe_h__

#include "PMString.h"

namespace KBSReplaceProbe
{
	/** Replace the checked hits of ONE story through DoReplaceAll and report what was measured.
	    Writes the measurements into outSummary as a single line for the panel's status read-out -
	    which is also how they are read back over COM, through app.kbsStatus.

	    Leaves KBSResultModel untouched: the rows go on describing what the SEARCH found, so the
	    panel and the document disagree until the user undoes or searches again. That is deliberate -
	    the experiment is about the document and the API's return values, and updating the model
	    would only add failure paths that have nothing to do with what is being measured. */
	void Run(PMString& outSummary);
}

#endif // __KBSReplaceProbe_h__
