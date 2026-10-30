//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A MATCH'S OVERRIDES (docs/superpowers/specs/2026-10-30-kfc-next-overrides-restore-design.md B): read for Narrow Down >
//  With Overrides, cleared for a text row's Special Operations > Clear Overrides. "An override" is InDesign's own word for
//  it - what its paragraph styles panel marks with "+" and the DOM's styleOverridden answers - not every attribute the text
//  carries: measured on 10-30 (the design's B0, case overrides-narrow-rows), a scale, a size, kenten, tate-chu-yoko and a
//  paragraph's own attribute are overrides; ruby, a condition, a character style and the style's own value set again
//  locally are not.
//
//========================================================================================

#pragma once
#ifndef __KFCTextOverrides_h__
#define __KFCTextOverrides_h__

#include "AttributeBossList.h"
#include "boost/shared_ptr.hpp"

class ITextModel;
class UIDRef;

namespace KFCTextOverrides
{
	/** IS THE TEXT ON [start, end) OVERRIDDEN - by attributes of its own (character level) or of the paragraphs it stands
	    in (paragraph level), as InDesign itself says (ITextAttrUtils::GetTotalAttributesAtIndex - its paragraph style's
	    and character style's "overridden", run by run)? false for an empty range or one the model does not hold. */
	bool HasOverrides(ITextModel* model, TextIndex start, TextIndex end);

	/** THE OVERRIDES CLEAR OVERRIDES TAKES OFF [start, end): the character-level ones on the range, and the paragraph-level
	    ones of the paragraphs it stands in [outParaStart, outParaStart + outParaLength) - each list the attributes' names
	    (ITextModelCmds::ClearOverridesCmd reads no value), ruby's thirty left out (not overrides to InDesign - the header's
	    note). false = nothing could be read. */
	bool CollectOverrides(ITextModel* model, TextIndex start, TextIndex end, boost::shared_ptr<AttributeBossList>& outChar,
		boost::shared_ptr<AttributeBossList>& outPara, TextIndex& outParaStart, int32& outParaLength);

	/** CLEAR OVERRIDES ON [start, end) of the story, as InDesign's paragraph styles panel's Clear Overrides does with that
	    text selected: its character-level overrides and its paragraphs' paragraph-level ones taken off, through
	    ITextModelCmds::ClearOverridesCmd - inside the caller's command sequence (one undoable step). outHadAny = there were
	    overrides to clear (HasOverrides); nothing is written when there were none. */
	ErrorCode ClearOverrides(const UIDRef& story, TextIndex start, TextIndex end, bool& outHadAny);
}

#endif // __KFCTextOverrides_h__
