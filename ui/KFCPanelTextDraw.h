//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What every hand-drawn text widget in this panel has to agree about: how far the CONTEXT around a
//  match is faded toward the background, how that faded colour is worked out, the two flags every
//  draw and every measure passes and how wide a string comes out with them, and the BAR that stands
//  where a change has a place and no characters. (Where a shown string may be CUT is KFCTextCut.h's,
//  in model/: the model half cuts the rows' texts by the same rule.)
//
//  WHY THIS FILE EXISTS. Two widgets draw "the words that matter at full strength, the words around
//  them faded": a hit row's cell (KFCColorTextView.cpp) and the panel's MESSAGE AREA
//  (KFCStatusTextView.cpp), which shows a selected GREP row's Preview Text - what its Replace would write. If each
//  kept its own 0.65 and its own blend, the two would answer the same question in two places and drift
//  apart the first time one of them was tuned.
//
//  THE SAME SHAPE AS KCM's KCMPanelTextDraw.h, which the message area was brought over from (the
//  author: "the way KCM does it"). Header-only for the reason KCM gives: constants, pure
//  functions and a few lines of drawing - no state, no SDK object to hold.
//  ! What is NOT shared is the colour LOOKUP: the row cell asks whether its row is selected and
//    switches both colours to the selection pair, while the message area is never selected. Each
//    asks IInterfaceColors its own question.
//
//========================================================================================

#ifndef __KFCPanelTextDraw_h__
#define __KFCPanelTextDraw_h__

// Interface includes:
#include "IInterfaceColors.h"	// RealAGMColor

// General includes:
#include "DVAPublicIncludes.h"	// dvaui::drawbot - what both widgets draw with (DV_Draw)
#include "DVPublicUtilities.h"	// dv_utils::DVFillRect - the bar is filled, not written (source/open/includes/widgets)
#include "DrawStringUtils.h"	// StringUtils::PMMeasureStringDroverized - how wide a string comes out
#include "PMReal.h"
#include "PMString.h"			// the bar's placeholder

/** The two flags every draw and every measure passes, spelled out rather than left to a default.
    THE DEFAULTS IN DrawStringUtils.h DISAGREE WITH EACH OTHER: the draw calls default to kFalse, the
    measure and ellipsize calls to kTrue - so taking them would measure a string differently from how
    it is drawn. '&' has to survive verbatim in any case: the rows draw document text and the
    message area draws file names the user chose ("A&B.indd"). The app's own drawing names them the
    same way (CRenderingObjectDrawer::DrawRenderObjectUIName). */
const bool16 kKFCDontConvertAmpersand = kFalse;
const bool16 kKFCNoUnderline = kFalse;

/** DRAWN BY DROVER, AS INDESIGN'S OWN ROWS ARE (2026-10-17 - the author, on the Bright theme: the panel's message "looks
    blurred, hard to read", and after a first fix "still crushed, not sharp" beside the document row). Both widgets used to
    draw in Draw, with an AGM graphics context - and Draw is handed an OFFSCREEN AGM port that DVAbstractControlView blits
    back onto the panel (DVAbstractControlView.h): text drawn there came out in black and white (photographed on the
    Bright theme: one grey at the draws' default Med, solid and swollen; still one at High, thinner), while the stock rows
    beside them - drawn by drover straight onto the panel - came out in ClearType (1594 of their 2287 inked pixels
    coloured; KFC's 0). So both draw in DV_Draw now, with DrawStringUtils.h's drawbot overloads - the shipping code's way
    (open/components/dynamicdocumentsui/TimingPanelTreeDDTarget.cpp: a row's name drawn with
    StringUtils::PMDrawStringRGB(drawbotP, ...)). (The raster port's text bit, IRasterPort::setantialiaspolicy - the AGM
    samples' way - was tried first and changed nothing.) This is the policy the drawbot draws are handed. */
const dv_utils::AntiAliasPolicy kKFCTextSmoothing = dv_utils::kAntiAliasPolicy_Med;

/** How wide `s` comes out as drawn here - drover's measure (the draws are drover's), with the flag the draws pass
    (above); 0 for nothing. The one measure of both widgets: a hit row's cell lays its runs out by it, the message area
    its lines. */
inline PMReal KFCDrawnWidth(const PMString& s, const InterfaceFontInfo& font)
{
	if (s.IsEmpty())
		return PMReal(0.0);
	return StringUtils::PMMeasureStringDroverized(s, font, kKFCDontConvertAmpersand).X();
}

/** How much of the theme's text colour the CONTEXT keeps: 0 = the background itself (invisible),
    1 = the full text colour (no fade at all).

    0.65, not 0.50 (the author's call, "blend it into the background a little less"): half and half
    made the surrounding line harder to read than it needed to be, and the match still stands out at
    this weight. (KCM took the same 0.65 from here.) */
const double kKFCContextTextWeight = 0.65;

/** Linear blend of two RGB colours (t = 0 -> bg, t = 1 -> fg) - how the context is faded toward
    whatever stands behind it (KCM's scrollbar-map trick). RealAGMColor's components are PMReal,
    hence the ToDouble on the way back into its constructor. */
inline RealAGMColor KFCBlendColor(const RealAGMColor& bg, const RealAGMColor& fg, const PMReal& t)
{
	const PMReal u = PMReal(1.0) - t;
	return RealAGMColor(
		ToDouble(bg.red   * u + fg.red   * t),
		ToDouble(bg.green * u + fg.green * t),
		ToDouble(bg.blue  * u + fg.blue  * t));
}

/** THE PLACE WORDS LEFT, OR WHERE THEY WENT IN (the author's request - "the bar KCM draws; bring it
    to KFC").

    A row whose match was replaced with NOTHING shows the line with the words simply gone: the
    context closes up and nothing says WHERE. A zero-width match (GREP ^ / $ / a lookaround) is the
    same case from the search's side - a place with no characters. And the message area meets it in a
    GREP row's Preview Text: a Replace that would write nothing (an empty Change To).
    All three are "a place, and nothing to show", and KCM draws that as a
    thin bar the full height of the line.

    NOTHING IS ADDED TO ANY STRING. The bar is DRAWN; the placeholder below only reserves the room, so
    nothing a reader walks (KIDMCP's inspect_ui reads the rows' "[]") gains a character.

    1.0 WIDE, and on WHOLE PIXELS - both KCM's findings, taken as they stand. KCM showed 2.0 first and
    was asked for thinner; and a centred 1px fill landed on x.5 as often as on a whole number, which the
    renderer smooths into two paler pixels - the same bar read as a thin dark line in one row and a
    wide grey one in the next. Rounding the left edge makes
    every bar the same pixel. ! At a UI scale above 100% a view unit is more than one pixel, so this
    promises only that every bar is drawn the same way. */
const PMReal kKFCCaretWidth(1.0);

/** The room the bar stands in: ONE SPACE, not an empty string. The message area wraps its text run
    by run, and an empty run is indistinguishable there from "nothing left to place"; a space is
    carried through the wrap like any other text, and the bar is drawn over it instead of it. */
inline PMString KFCCaretPlaceholder()
{
	PMString s(" ");
	s.SetTranslatable(kFalse);
	return s;
}

/** Draw the bar, centred in the room the placeholder reserved.
    @param x the left edge of that room, @param roomW how wide it came out.
    @param top the top of the line's box and @param height its height - the bar spans the WHOLE of it,
           which is what makes it read as "between these two characters" rather than as a character
           of its own. */
inline void KFCDrawCaret(dvaui::drawbot::Drawbot* drawbotP, const RealAGMColor& colour,
						 const PMReal& x, const PMReal& roomW,
						 const PMReal& top, const PMReal& height)
{
	if (drawbotP == nil || height <= PMReal(0.0))
		return;
	const PMReal left = ::Round(x + (roomW - kKFCCaretWidth) / PMReal(2.0));
	const PMReal y = ::Round(top);
	dv_utils::DVFillRect(drawbotP, colour, PMRect(left, y, left + kKFCCaretWidth, y + height));
}

#endif // __KFCPanelTextDraw_h__

// End, KFCPanelTextDraw.h.
