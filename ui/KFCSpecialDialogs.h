//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE VALUES AN OBJECT ROW'S TRANSFORM ASKS FIRST (docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md
//  E12): Move..., Scale..., Rotate..., Shear... in the object row's Special Operations > Transform - and the page range
//  Narrow Down > Pages... asks. Dialogs made of InDesign's own widgets, laid out as InDesign's own
//  (docs/superpowers/specs/2026-10-23-kfc-native-value-dialogs-design.md - KFCSpecialDialogs.cpp says how). The UI
//  half's: the model is handed the values alone (KFCSpecialParams - KFCObjectSpecial; a page range - KFCNarrow).
//
//========================================================================================

#ifndef __KFCSpecialDialogs_h__
#define __KFCSpecialDialogs_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCSpecialParams, KFCSpecialOp

namespace KFCSpecialDialogs
{
	/** THE VALUES OF A TRANSFORM, ASKED FIRST (the special operations spec's E12), in dialogs of InDesign's own
	    widgets, laid out as InDesign's own (the author's call). Move: Horizontal / Vertical in InDesign's measure
	    fields - the front document's ruler units, and any unit or way of writing one InDesign reads - with Distance
	    and Angle, each pair worked out from the other; answered in points, right and down positive, every object
	    moved by the same distance. Scale: Scale X / Scale Y in percent, InDesign's chain between them (Constrain
	    proportions) linked by default. Rotate: an angle in degrees, counter-clockwise positive (InDesign's own).
	    Shear: an angle in degrees between -90 and 90, on the Horizontal or the Vertical axis (y: 0 or 1). Each
	    object is transformed about its own centre (KFCObjectSpecial). What was answered last is offered again for
	    the session. io: x and y set on OK. false = Cancel (outWhy empty), or the dialog could not be shown, or it
	    answered a value that is not finite (outWhy says so - the fields' ranges keep one from being typed). */
	bool AskTransformValues(int32 op, KFCSpecialParams& io, PMString& outWhy);

	/** Values that would change nothing - Move 0 and 0, Scale 100% and 100%, Rotate 0 degrees, Shear 0 degrees: said
	    rather than written. */
	bool NothingToDo(int32 op, const KFCSpecialParams& params);

	/** The operation's name - "Move", "Scale", "Rotate", "Shear" ("" for any other) - for the status line. */
	const char* OpName(int32 op);

	/** NARROW DOWN > PAGES... (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md N5): the page range asked, in a
	    dialog of the same make as the transforms' - one field, "Pages:", written as the Print dialog's Range ("1-5, 8",
	    a section's pages by its name, absolute numbers with +). What was typed last is offered again for the session.
	    io: the range on OK, its leading and trailing spaces taken off. false = Cancel (outWhy empty), or the dialog
	    could not be shown (outWhy says so). The model reads the range in each document (KFCNarrow::ReadPageRange). */
	bool AskPageRange(PMString& io, PMString& outWhy);

	/** SAVE QUERY ON THE KOHAKU+ TAB (KFCKohakuQueryMenu): a query's name asked, in a dialog of the same make - offered the next free "Query <n>" of the kind, held to the rules of InDesign's own dialog for it (nothing blank, none of
	    the characters a file's name cannot have), and asked Replace? when the kind has a query of that name. outName on OK. false = Cancel (outWhy empty), or the dialog could not be shown (outWhy says so). */
	bool AskQueryName(int32 mode, PMString& outName, PMString& outWhy);
}

#endif // __KFCSpecialDialogs_h__
