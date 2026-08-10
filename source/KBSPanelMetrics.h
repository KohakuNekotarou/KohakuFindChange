//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  How tall the panel's message block is, and how small the panel may be dragged.
//
//  ***** WHY THIS EXISTS. ***** The palette font's LINE HEIGHT depends on the UI language:
//  measured 12px on an English UI and 18px on a Japanese one (2026-08-06, by counting the
//  baselines in screen captures of the running panel - 18px exactly, three lines at y=218,
//  236, 254). A .fr can only carry one number, and the one it carries is the English one, so
//  on a Japanese UI the 48px box held two and two thirds lines and the last line of the
//  message was cut off mid-glyph. The user reported it the day this machine became a
//  Japanese install.
//
//  The .fr keeps the English layout as its resting state and this writes the real answer on
//  when the panel appears, which is the arrangement KBSPanelIcon and KBSPanelTitle already
//  use for the same reason: the panel's widgets are rebuilt on every show, so what the .fr
//  says is a starting point rather than the truth.
//
//  The alternative was a second panel-view resource selected by LocaleIndex (the official
//  basiclocalization shape). It was not taken: KBS retired its per-locale resources on
//  2026-08-05 in favour of deciding at run time, and a duplicated layout would have to be
//  edited in two places for every future change.
//
//========================================================================================

#ifndef __KBSPanelMetrics_h__
#define __KBSPanelMetrics_h__

#include "PMTypes.h"

class IPanelControlData;

namespace KBSPanelMetrics
{
	/** Re-place the message block, the illustration and the result tree for the current UI
	    language.

	    The PANEL IS PASSED IN, and there is no overload that goes looking for it: the one
	    caller is the panel's own observer (KBSPanelTitle.cpp, AutoAttach), which is aggregated
	    onto the panel boss and so asks itself with
	    InterfacePtr<IPanelControlData>(this, UseDefaultIID()) - the shape the product uses in
	    ConditionalTextUIPanelDetailController.cpp:162 and LayerPanelView.cpp:63. Add the
	    look-it-up overload if a second caller ever appears; do not put the lookup here for a
	    caller that is standing on the answer.

	    Does nothing when panelData is nil. Safe to call repeatedly: every frame is computed as
	    an ABSOLUTE position from the message block's top, never nudged by a delta, so running
	    it twice leaves exactly what running it once did. That matters because it runs on every
	    show and a widget's frame is persisted in the workspace - adding 24px each time would
	    walk the tree off the bottom of the panel. */
	void Update(IPanelControlData* panelData);

	/** The message block's height in pixels: a whole number of lines IN THE LANGUAGE IT IS USED
	    FOR, and FOUR lines in both. 72 = 18x4 (Japanese); 48 = 12x4 (English). A remainder would
	    leave room for a part-line, which is drawn as a sliver of chopped-off letters - and each
	    language only ever reads its own number, so the two need not agree on a common multiple.

	    ***** THE JAPANESE NUMBER WAS 54 = 18x3 UNTIL 2026-08-10. ***** Three lines were sized
	    against the OPENING message; the line a stopped replace leaves is 128 characters and was
	    measurably cut off. The block grew a line and the messages were cut to fit it - the
	    measurement, and the reasoning for leaving the Roman block at four, are in
	    KBSPanelMetrics.cpp over the two constants. **This sentence is the one KBS.fr:1060 means
	      by "if this number changes, change that one too": the .fr, the .cpp and THIS LINE are
	      three statements of one fact, and this line is the one that was left behind in both of
	      the changes so far (2026-08-08, 2026-08-10). */
	int32 MessageBlockHeight();

	/** The floor under a drag. The width is what the panel measures at its usual size (measured
	    off the running panel on 2026-08-07, which is where the number 266 comes from - the
	    instruction was given twice and the full four-step history is in KBSPanelMetrics.cpp);
	    the height moves with the message block, so a taller block does not eat the result rows
	    the floor exists to protect. */
	int32 MinimumPanelWidth();
	int32 MinimumPanelHeight();
}

#endif // __KBSPanelMetrics_h__
