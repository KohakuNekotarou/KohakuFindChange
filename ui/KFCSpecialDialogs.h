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
	/** THE VALUES OF A TRANSFORM, ASKED FIRST (E12), in dialogs laid out as InDesign's own (the author, 2026-10-17 night).
	    Move: Horizontal / Vertical - a number alone is in the front document's ruler units, as InDesign's own Move dialog
	    reads one, or a number with a unit typed (pt, mm, cm, in, pc, px, Q, H) - with Distance and Angle, each pair worked
	    out from the other; answered in points, right and down positive, every object moved by the same distance. Scale:
	    Scale X / Scale Y in percent, the chain (Constrain proportions) on by default. Rotate: an angle in degrees,
	    counter-clockwise positive (InDesign's own). Shear: an angle in degrees between -90 and 90, on the Horizontal or the
	    Vertical axis (y: 0 or 1). Each object is transformed about its own centre (KFCObjectSpecial). What was typed last
	    is offered again for the session. io: x and y set on OK. false = Cancel (outWhy empty), or the dialog could not be
	    shown (outWhy says so). */
	bool AskTransformValues(int32 op, KFCSpecialParams& io, PMString& outWhy);

	/** Values that would change nothing - Move 0 and 0, Scale 100% and 100%, Rotate 0 degrees, Shear 0 degrees: said
	    rather than written. */
	bool NothingToDo(int32 op, const KFCSpecialParams& params);

	/** The operation's name - "Move", "Scale", "Rotate", "Shear" ("" for any other) - for the status line. */
	const char* OpName(int32 op);
}

#endif // __KFCSpecialDialogs_h__
