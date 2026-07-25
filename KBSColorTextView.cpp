//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  KBSColorTextView: a self-drawing tree cell that paints a hit line with the matched part in a
//  highlight colour (VS "Find in Files" style). A stock StaticText draws one colour per row, so
//  the hit rows use this DVControlView-derived cell instead, drawing three runs left to right
//  (before / matched / after). KBSRowData is the tiny data holder aggregated on the same boss.
//
//  Recipe: the multi-colour cell draw proven against customdatalinkui's DVControlView -
//  AGMGraphicsContext + StringUtils::PMDrawString / PMDrawStringRGB, the palette font from
//  IInterfaceFonts, the baseline from IWidgetUtils::GetViewYPosition. convertAmpersand is kFalse
//  on BOTH the draw and the measure so a literal '&' in the search text is neither underlined
//  nor dropped. When a line overflows the cell the match is kept at full strength and the context
//  is ellipsized around it (the stock rows ellipsize automatically; this custom cell does it by
//  hand): the leading context loses its HEAD (kEllipsizeBeginning, so the words just before the
//  match survive with a leading "..."), the trailing context loses its TAIL (kEllipsizeEnd).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "IInterfaceFonts.h"	// the palette window font

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawString / PMDrawStringRGB / PMMeasureString / PMEllipsizeString
#include "WidgetDefs.h"			// EllipsizeStyle (kEllipsizeBeginning / kEllipsizeEnd)
#include "ISession.h"			// GetExecutionContextSession
#include "IWidgetUtils.h"		// GetViewYPosition
#include "ShuksanID.h"			// kPaletteWindowFontId
#include "Utils.h"

// Project includes:
#include "KBSID.h"
#include "KBSColorTextView.h"

// Linear blend of two RGB colours (t = 0 -> bg, t = 1 -> fg). Used to fade the context text
// toward the panel background (the KESCM scrollbar-map trick).
static RealAGMColor BlendColor(const RealAGMColor& bg, const RealAGMColor& fg, const PMReal& t)
{
	const PMReal u = PMReal(1.0) - t;
	return RealAGMColor(
		ToDouble(bg.red   * u + fg.red   * t),
		ToDouble(bg.green * u + fg.green * t),
		ToDouble(bg.blue  * u + fg.blue  * t));
}

//----------------------------------------------------------------------------------------
// KBSRowData - the per-row data holder (three text segments)
//----------------------------------------------------------------------------------------

/** Non-persistent holder for a hit row's three text segments, aggregated on the colour cell's
    boss beside the view. Written by the widget manager on every apply. */
class KBSRowData : public CPMUnknown<IKBSRowData>
{
public:
	KBSRowData(IPMUnknown* boss) : CPMUnknown<IKBSRowData>(boss), fReplaced(false) {}
	virtual ~KBSRowData() {}

	virtual void SetSegments(const PMString& locator, const PMString& pre, const PMString& match,
		const PMString& post, bool replaced)
	{
		fLocator = locator; fLocator.SetTranslatable(kFalse);
		fPre = pre;   fPre.SetTranslatable(kFalse);
		fMatch = match; fMatch.SetTranslatable(kFalse);
		fPost = post; fPost.SetTranslatable(kFalse);
		fReplaced = replaced;
	}

	virtual void GetSegments(PMString& outLocator, PMString& outPre, PMString& outMatch,
		PMString& outPost, bool& outReplaced) const
	{
		outLocator = fLocator;
		outPre = fPre;
		outMatch = fMatch;
		outPost = fPost;
		outReplaced = fReplaced;
	}

private:
	PMString fLocator;
	PMString fPre;
	PMString fMatch;
	PMString fPost;
	bool fReplaced;
};

CREATE_PMINTERFACE(KBSRowData, kKBSRowDataImpl)

//----------------------------------------------------------------------------------------
// KBSColorTextView - the self-drawing cell
//----------------------------------------------------------------------------------------

/** Implements IControlView: draws a hit line with the matched part highlighted. */
class KBSColorTextView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KBSColorTextView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KBSColorTextView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KBSColorTextView, kKBSColorTextViewImpl)

void KBSColorTextView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	InterfacePtr<IKBSRowData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString locator, pre, match, post;
	bool replaced = false;
	data->GetSegments(locator, pre, match, post, replaced);

	// The palette window's font (same one KESCL's report panel measures with).
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowFontId);

	const PMRect frame = this->GetInnerContentFrame();
	const PMReal y = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, frame.Height());
	const PMReal rightEdge = frame.Right();
	PMReal x = frame.Left();

	// Colours, entirely from the current theme so KBS matches whatever colours it uses:
	//   * bg = the panel's background fill (kInterfacePaletteFill)
	//   * fg = the theme's TEXT colour (kInterfaceTextColor - exactly what InDesign's own panels
	//          draw text with: black in a light UI, ~0.8 gray in a dark one; it flips with the
	//          theme, so nothing is hardcoded and nothing vanishes when the UI brightness changes)
	// The matched text is drawn at the full theme text colour; the context (the "P<page>(<n>)"
	// locator and the rest of the line) is that same colour faded toward the background, so the
	// match reads at full strength and the context recedes. Tune kContextTextWeight to taste:
	// 0 = fully the background (invisible), 1 = the full text colour (no fade).
	const PMReal kContextTextWeight(0.50);
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);	// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(kInterfaceTextColor, fg);
	}
	const RealAGMColor kFullColor = fg;									// the theme's text colour
	const RealAGMColor kContextColor = BlendColor(bg, fg, kContextTextWeight);	// faded toward bg

	// A replaced row is history: everything it draws recedes, so the eye goes to the rows that
	// still need attention. (Its check box, a real widget beside this cell, is disabled to match.)
	const RealAGMColor kMatchColor = replaced ? kContextColor : kFullColor;

	// The page locator ("P1(2)") is drawn at the full theme text colour, then the line text
	// starts at a tab stop (a fixed column from the cell's left edge, so the text lines up down
	// the tree; a locator wider than the column just pushes the text past it with a min gap).
	const PMReal kTextTabStop(48.0);	// px from the cell's left edge where the line text begins
	const PMReal kMinGap(8.0);			// min space after an over-wide locator
	if (!locator.IsEmpty() && x < rightEdge)
	{
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), locator, fontInfo, kMatchColor, kFalse, kFalse);
		const PMReal locatorEnd = x + StringUtils::PMMeasureString(&gc, locator, fontInfo, kFalse).X();
		x = frame.Left() + kTextTabStop;
		if (locatorEnd + kMinGap > x)
			x = locatorEnd + kMinGap;
	}

	// The line, left to right. convertAmpersand=kFalse on draw AND measure so a literal '&' is
	// neither underlined nor dropped. If the whole line fits it is drawn as-is; when it overflows
	// the match is kept at full strength and the context is ellipsized around it. The matched run
	// is the full theme text colour; the context runs are faded.
	const PMReal availWidth = rightEdge - x;
	if (availWidth <= PMReal(0.0))
		return;		// the locator consumed the cell; no room left for the line

	// Draw one run at the running x and advance past it (an empty run is a no-op).
	auto drawRun = [&](const PMString& s, const RealAGMColor& c)
	{
		if (s.IsEmpty())
			return;
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), s, fontInfo, c, kFalse, kFalse);
		x += StringUtils::PMMeasureString(&gc, s, fontInfo, kFalse).X();
	};

	const PMReal preW   = pre.IsEmpty()   ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, pre,   fontInfo, kFalse).X();
	const PMReal matchW = match.IsEmpty() ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, match, fontInfo, kFalse).X();
	const PMReal postW  = post.IsEmpty()  ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, post,  fontInfo, kFalse).X();

	if (preW + matchW + postW <= availWidth)
	{
		// The whole line fits: draw the three runs unchanged.
		drawRun(pre, kContextColor);
		drawRun(match, kMatchColor);
		drawRun(post, kContextColor);
	}
	else if (matchW >= availWidth)
	{
		// The match alone overflows the cell: ellipsize the match itself (tail) and drop the context.
		const PMString m = StringUtils::PMEllipsizeString(&gc, availWidth, match, fontInfo, kEllipsizeEnd, nil, kFalse);
		drawRun(m, kMatchColor);
	}
	else
	{
		// The match fits but the whole line does not: keep the match at full strength and show as
		// much context as fits around it. The LEADING context loses its head (kEllipsizeBeginning,
		// so the words just before the match survive with a leading "..."); the TRAILING context
		// loses its tail (kEllipsizeEnd). Leading context is served first, so the run-up to the
		// match is preferred over what follows it.
		const PMReal rem = availWidth - matchW;
		PMString preCut = pre;
		if (!pre.IsEmpty())
			preCut = StringUtils::PMEllipsizeString(&gc, rem, pre, fontInfo, kEllipsizeBeginning, nil, kFalse);
		const PMReal preCutW = preCut.IsEmpty() ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, preCut, fontInfo, kFalse).X();

		const PMReal postBudget = rem - preCutW;
		PMString postCut;
		if (!post.IsEmpty() && postBudget > PMReal(0.0))
			postCut = StringUtils::PMEllipsizeString(&gc, postBudget, post, fontInfo, kEllipsizeEnd, nil, kFalse);

		drawRun(preCut, kContextColor);
		drawRun(match, kMatchColor);
		drawRun(postCut, kContextColor);
	}
}

// End, KBSColorTextView.cpp.
