//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What every hand-drawn text widget in this panel has to agree about: how far the CONTEXT around a
//  match is faded toward the background, how that faded colour is worked out, the two flags every
//  draw and every measure passes and how wide a string comes out with them, how a run is ellipsized to
//  a width (KFCEllipsize - the SDK's call has widths it never comes back from), and the BAR that stands
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
#include "DrawStringUtils.h"	// PMMeasureStringDroverized - how wide a string comes out / PMEllipsizeString
#include "PMReal.h"
#include "PMString.h"			// the bar's placeholder
#include "WidgetDefs.h"			// EllipsizeStyle - KFCEllipsize

// Project includes:
#include "KFCDiag.h"			// KFC_DIAG_LOG - KFCEllipsize's line in a test build

/** The two flags every draw and every measure passes, spelled out rather than left to a default.
    THE DEFAULTS IN DrawStringUtils.h DISAGREE WITH EACH OTHER: the draw calls default to kFalse, the
    measure and ellipsize calls to kTrue - so taking them would measure a string differently from how
    it is drawn. '&' has to survive verbatim in any case: the rows draw document text and the
    message area draws file names the user chose ("A&B.indd"). The app's own drawing names them the
    same way (CRenderingObjectDrawer::DrawRenderObjectUIName). */
const bool16 kKFCDontConvertAmpersand = kFalse;
const bool16 kKFCNoUnderline = kFalse;

/** DRAWN BY DROVER, AS INDESIGN'S OWN ROWS ARE (the author, on the Bright theme: the panel's message "looks blurred,
    hard to read", and after a first fix "still crushed, not sharp" beside the document row). NOT IN Draw, WITH AN AGM
    GRAPHICS CONTEXT: Draw is handed an OFFSCREEN AGM port that DVAbstractControlView blits back onto the panel
    (DVAbstractControlView.h), and text drawn there comes out in black and white (photographed on the Bright theme: one
    grey at the draws' default Med, solid and swollen; still one at High, thinner), while the stock rows beside it -
    drawn by drover straight onto the panel - come out in ClearType (1594 of their 2287 inked pixels coloured; KFC's 0
    when it drew so). So both widgets draw in DV_Draw, with DrawStringUtils.h's drawbot overloads - the shipping code's
    way (open/components/dynamicdocumentsui/TimingPanelTreeDDTarget.cpp: a row's name drawn with
    StringUtils::PMDrawStringRGB(drawbotP, ...)). (The raster port's text bit, IRasterPort::setantialiaspolicy - the AGM
    samples' way - changes nothing: tried.) This is the policy the drawbot draws are handed. */
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

/** StringUtils::PMEllipsizeString, never handed a width at which it does not come back. Every cut of a hit row's cell
    goes through here (KFCColorTextView.cpp: the match, the context before it, the context after it).

    THE SDK CALL NEVER RETURNS for a width a little wider than "..." (found by KCM, 2026-10-24 - KCM's KCMEllipsize is
    this, brought over). Its loop (DV_WidgetBin.dll 21.0, read from the disassembly): if the string does not fit,
    room = width - the width of "..."; cut characters and measure again WHILE what is left is wider than room. Cut down
    to nothing, what is left is the empty string - which measures 1, not 0, in the palette font - so for a room above 0
    and below that 1 nothing can ever be narrow enough to stop it: InDesign stops answering. A capped copy of the loop
    swept over two row strings in 0.25 px steps never ended at 15.25 - 15.75 and nowhere else, and KCM's panel froze
    InDesign at 377 px (its width was halved). The cell's cuts are all worked out by subtraction - the room the match
    leaves, then what the context before it leaves of that - so any of them can land there once the widths are not
    whole pixels: on this PC's UI at 100 % they are, and the band (between two of them) was not met in 235 panel widths
    x 28 rows (2026-10-25); a scaled UI measures in fractions.
    => Below "..." plus the empty string (plus half a pixel) nothing but the dots can fit anyway: the answer is the
    dots when they fit and nothing when they do not - what the SDK itself answers on either side of the band. Measured
    the way the SDK measures inside (the context-free PMMeasureString, convertAmpersand on for the dots and the empty
    string), so the band tested is the one its loop meets. Whether `str` fits at all is asked of KFCDrawnWidth - how it
    comes out as drawn. A test build says each width it kept from the SDK that would not have come back (ELLIPSIZE),
    and its fault switch ellipsize-half widens every width by half a pixel - the way into the band on a whole-pixel UI.
    ! THE CUT IS MARKED "do not translate": PMEllipsizeString hands back a NEW string, a PMString starts out
      translatable (PMString.h), and the draw translates what it is given (DrawStringUtils.h: "It will be translated
      before drawing to screen") - so document text that happens to spell a key of the string table would be drawn as
      that key's translation. @see memory pmellipsizestring-dead-band */
inline PMString KFCEllipsize(const PMReal& widthAsked, const PMString& str, const InterfaceFontInfo& fontInfo,
							 EllipsizeStyle where)
{
	PMReal width = widthAsked;
#ifdef KFC_DIAG
	// (Test builds only) Fault switch ellipsize-half: every width half a pixel wider. This PC's UI (100 %) measures
	// in whole pixels, so the cell's subtractions hand only whole widths - and the band lies BETWEEN two of them
	// (none met in 235 panel widths x 28 rows, 2026-10-25); a scaled UI need not. The way into the band for the
	// guard's own test.
	if (KFC_DIAG_FAULT("ellipsize-half"))
		width += PMReal(0.5);
#endif
	if (str.IsEmpty() || KFCDrawnWidth(str, fontInfo) <= width)
		return str;
	PMString dots("...", PMString::kDontTranslateDuringCall);
	dots.Translate();
	const PMReal dotsW = StringUtils::PMMeasureString(dots, fontInfo, kTrue).X();
	const PMReal emptyW = StringUtils::PMMeasureString(PMString(), fontInfo, kTrue).X();
	if (width < dotsW + emptyW + PMReal(0.5))
	{
		if (width > dotsW && width < dotsW + emptyW)
			KFC_DIAG_LOG("ELLIPSIZE band width=%.3f dots=%.3f empty=%.3f - not handed to PMEllipsizeString",
						 ToDouble(width), ToDouble(dotsW), ToDouble(emptyW));
		PMString shown;
		if (width >= dotsW)
			shown = dots;
		shown.SetTranslatable(kFalse);
		return shown;
	}
	PMString cut = StringUtils::PMEllipsizeString(width, str, fontInfo, where, nil, kKFCDontConvertAmpersand);
	cut.SetTranslatable(kFalse);
	return cut;
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
