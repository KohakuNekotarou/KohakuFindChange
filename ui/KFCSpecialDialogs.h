//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE VALUES AN OBJECT ROW'S TRANSFORM ASKS FIRST (docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md
//  E12): Move..., Scale..., Rotate..., Shear... in the object row's Special Operations > Transform. A ScriptUI dialog run
//  through the plug-in's own script engine - How to Use's way (KFCHowTo.cpp) - its answer read back as a string. The UI
//  half's: the model is handed the values alone (KFCSpecialParams - KFCObjectSpecial).
//
//========================================================================================

#ifndef __KFCSpecialDialogs_h__
#define __KFCSpecialDialogs_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCSpecialParams, KFCSpecialOp

namespace KFCSpecialDialogs
{
	/** THE VALUES OF A TRANSFORM, ASKED FIRST (the special operations spec's E12), in dialogs laid out as InDesign's
	    own (the author's call). Move: Horizontal / Vertical - a number alone is in the front document's ruler units, as
	    InDesign's own Move dialog reads one, or a number with a unit typed (pt, mm, cm, in, pc, px, Q, H - a document
	    whose ruler is in another unit, ciceros say, has a number alone read and shown in points) - with
	    Distance and Angle, each pair worked out from the other; answered in points, right and down positive, every
	    object moved by the same distance. Scale: Scale X / Scale Y in percent, the chain (Constrain proportions) on by
	    default. Rotate: an angle in degrees, counter-clockwise positive (InDesign's own). Shear: an angle in degrees
	    between -90 and 90, on the Horizontal or the Vertical axis (y: 0 or 1). Each object is transformed about its own
	    centre (KFCObjectSpecial). What was typed last is offered again for the session. io: x and y set on OK. false =
	    Cancel (outWhy empty), or the dialog could not be shown, or it answered a value that is not finite (outWhy says
	    so - each dialog's OK refuses one first). */
	bool AskTransformValues(int32 op, KFCSpecialParams& io, PMString& outWhy);

	/** Values that would change nothing - Move 0 and 0, Scale 100% and 100%, Rotate 0 degrees, Shear 0 degrees: said
	    rather than written. */
	bool NothingToDo(int32 op, const KFCSpecialParams& params);

	/** The operation's name - "Move", "Scale", "Rotate", "Shear" ("" for any other) - for the status line. */
	const char* OpName(int32 op);

	/** NARROW DOWN > PAGES... (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md N5): the page range asked, in a
	    dialog of the same make as the transforms' - one field, "Pages:", written as the Print dialog's Range ("1-5, 8",
	    a section's pages by its name, absolute numbers with +). What was typed last is offered again for the session.
	    io: the range on OK. false = Cancel (outWhy empty), or the dialog could not be shown, or the text holds a lone
	    surrogate (outWhy says so - the dialog's OK refuses one first; no UTF-8 is made of it). The model reads the
	    range in each document (KFCNarrow::ReadPageRange). */
	bool AskPageRange(PMString& io, PMString& outWhy);
}

#endif // __KFCSpecialDialogs_h__
