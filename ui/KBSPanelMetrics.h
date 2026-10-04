//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  How tall the panel's message block is, and how small the panel may be dragged.
//
//  WHY THIS EXISTS. The palette font's LINE HEIGHT depends on the UI language: measured 12px on
//  an English UI and 18px on a Japanese one (by counting the baselines in screen captures of the
//  running panel - 18px exactly, three lines at y=218, 236, 254). A .fr can only carry one number,
//  and the one it carries is the English one, so on a Japanese UI the 48px box held two and two
//  thirds lines and the last line of the message was cut off mid-glyph.
//
//  The .fr keeps the English layout as its resting state and this writes the real answer on
//  when the panel appears, which is the arrangement KBSPanelIcon and KBSPanelTitle already
//  use for the same reason: the panel's widgets are rebuilt on every show, so what the .fr
//  says is a starting point rather than the truth.
//
//  THE LINE IS ASKED OF THE FONT, and only here - MessageFont and MessageLineMetrics. The box's own
//  drawing (KBSStatusTextView) asks the same two, so the height and the drawing cannot disagree: one
//  question, one place. It is the way the product measures a widget's font
//  (dynamicdocumentsui/TimingPanelTreeDDTarget.cpp:582-585, dv_utils::FontInfoGetDVAFontMetrics), so
//  the box is as many of the drawing's lines tall as it is meant to hold, in any UI language. (A table
//  of line heights by UI language would assume what was never measured - Korean and Chinese.)
//
//  The alternative was a second panel-view resource selected by LocaleIndex (the official
//  basiclocalization shape). It was not taken: KBS has no per-locale resources, deciding at run
//  time instead, and a duplicated layout would have to be edited in two places for every future
//  change.
//
//========================================================================================

#ifndef __KBSPanelMetrics_h__
#define __KBSPanelMetrics_h__

#include "PMTypes.h"
#include "PMReal.h"

class IPanelControlData;
class InterfaceFontInfo;

namespace KBSPanelMetrics
{
	/** The font the message block is drawn in: the palette window's SYSTEM SCRIPT font
	    (kPaletteWindowSystemScriptFontId, the one the hit rows use - KBSStatusTextView says why).
	    The reference behind it is the session's (IInterfaceFonts), so it outlives this call.
	    nil when the session has no fonts to give. */
	const InterfaceFontInfo* MessageFont();

	/** One line of the message block, from that font's own metrics: the advance from one line to
	    the next (ascent + descent + leading) and the ascent (where the first baseline sits under
	    the top). false when the font is not there or its metrics are refused - the box's height
	    then stays the .fr's, and the drawing measures a string instead (KBSStatusTextView).
	    ! Not "how tall is this ink": a measured string answers a pixel more (KCM measured 19.0
	    against 18.0), and that pixel costs a line in a box sized to whole lines. */
	bool MessageLineMetrics(PMReal& outLineAdvance, PMReal& outAscent);

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

	/** The message block's height in pixels: FOUR of MessageLineMetrics' lines, rounded UP to a
	    whole pixel - less than a pixel over, which no part-line can be drawn into. On a Japanese UI
	    that is 4 x 18.0 = 72 (KCM measured the 18.0). When the font cannot be asked, the .fr's own
	    block (48) stands.

	    WHY FOUR. The line a stopped replace leaves is 128 characters, and three lines cut it off
	    (measured); the block holds four and the messages are cut to fit four at the floor's width -
	    see KBSPanelMetrics.cpp over kMessageLines.
	    THE SAME FACT IS STATED ELSEWHERE - change them together. The message widget's
	    note in KFCUI.fr ("if this number changes, change that one too"), kMessageLines and its note
	    in KBSPanelMetrics.cpp, and KBSPanelView::ConstrainDimensions, which explains why it
	    SUBTRACTS the fixed part instead of adding it up. (The list is by name, not by line: a line
	    number in this plug-in's own files is wrong the next time either file is edited.) */
	int32 MessageBlockHeight();

	/** The floor under a drag. The width is what the panel measures at its usual size (266,
	    measured off the running panel - KBSPanelMetrics.cpp says why it is not narrower);
	    the height moves with the message block, so a taller block does not eat the result rows
	    the floor exists to protect. */
	int32 MinimumPanelWidth();
	int32 MinimumPanelHeight();
}

#endif // __KBSPanelMetrics_h__
