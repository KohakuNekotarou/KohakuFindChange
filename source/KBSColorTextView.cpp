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
//  AGMGraphicsContext + StringUtils::PMDrawString / PMDrawStringRGB, the palette SYSTEM SCRIPT
//  font from IInterfaceFonts (the one the branch rows use, and the one the shipping panels use for
//  document text), the baseline from IWidgetUtils::GetViewYPosition. convertAmpersand is kFalse
//  on BOTH the draw and the measure so a literal '&' in the search text is neither underlined
//  nor dropped. Selected rows are drawn in the theme's selected-text colours, which a hand-drawn
//  cell has to ask for itself (a stock StaticText gets all four colours from its .fr and lets the
//  framework choose) - the same isHilited switch the app's own drawing makes. When a line overflows the cell the match is kept at full strength and the context
//  is ellipsized around it (the stock rows ellipsize automatically; this custom cell does it by
//  hand): the leading context loses its HEAD (kEllipsizeBeginning, so the words just before the
//  match survive with a leading "..."), the trailing context loses its TAIL (kEllipsizeEnd).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"		// IsHilited - is this cell's row the selected one?
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "IInterfaceFonts.h"	// the palette window font
#include "IWidgetParent.h"		// QueryParentFor - this cell -> the row widget that carries the hilite

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawString / PMDrawStringRGB / PMMeasureString / PMEllipsizeString
#include "WidgetDefs.h"			// EllipsizeStyle (kEllipsizeBeginning / kEllipsizeEnd)
#include "ISession.h"			// GetExecutionContextSession
#include "IWidgetUtils.h"		// GetViewYPosition
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId
#include "Utils.h"

// Project includes:
#include "KBSID.h"
#include "KBSColorTextView.h"
#include "KBSResultModel.h"		// MarkUpBreaksForDisplay - the pilcrow / return arrow a break draws as

// How far up the widget chain to look for the hilite (see KBSViewOrParentIsHilited). One step is
// all this panel needs (cell -> row); the extra steps only keep it working if the row ever gains
// another wrapper.
static const int32 kKBSHiliteParentSteps = 3;

// True if this view, or a widget above it, is drawn hilited - i.e. this cell belongs to the row the
// user has selected. The tree applies the hilite to the ROW widget (the base
// CTreeViewWidgetMgr::ApplyNodeIDToWidget does it, "for hilite selection"), and this cell is one of
// that row's children, so a cell that only asked itself would never see the selection.
static bool16 KBSViewOrParentIsHilited(IControlView* view, int32 stepsLeft)
{
	if (view == nil)
		return kFalse;
	if (view->IsHilited())
		return kTrue;
	if (stepsLeft <= 0)
		return kFalse;

	InterfacePtr<IWidgetParent> parent(view, UseDefaultIID());
	if (parent == nil)
		return kFalse;
	InterfacePtr<IControlView> parentView((IControlView*)parent->QueryParentFor(IID_ICONTROLVIEW));
	return KBSViewOrParentIsHilited(parentView, stepsLeft - 1);
}

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

// (The break characters are turned into marks by KBSResultModel::MarkUpBreaksForDisplay, which
//  stood here as a static until 2026-08-04. It moved because the SAVED REPORT has to show a match
//  the same way this cell does, and a second copy of the rule would be a second thing to keep in
//  step - the same reason BuildHitLocator is one function serving two callers.)

//----------------------------------------------------------------------------------------
// KBSRowData - the per-row data holder (three text segments)
//----------------------------------------------------------------------------------------

/** Non-persistent holder for a hit row's three text segments, aggregated on the colour cell's
    boss beside the view. Written by the widget manager on every apply. */
class KBSRowData : public CPMUnknown<IKBSRowData>
{
public:
	KBSRowData(IPMUnknown* boss) : CPMUnknown<IKBSRowData>(boss) {}
	virtual ~KBSRowData() {}

	virtual void SetSegments(const PMString& locator, const PMString& flag, const PMString& pre,
		const PMString& match, const PMString& post)
	{
		fLocator = locator; fLocator.SetTranslatable(kFalse);
		fFlag = flag; fFlag.SetTranslatable(kFalse);
		fPre = pre;   fPre.SetTranslatable(kFalse);
		fMatch = match; fMatch.SetTranslatable(kFalse);
		fPost = post; fPost.SetTranslatable(kFalse);
	}

	virtual void GetSegments(PMString& outLocator, PMString& outFlag, PMString& outPre,
		PMString& outMatch, PMString& outPost) const
	{
		outLocator = fLocator;
		outFlag = fFlag;
		outPre = fPre;
		outMatch = fMatch;
		outPost = fPost;
	}

private:
	PMString fLocator;
	PMString fFlag;
	PMString fPre;
	PMString fMatch;
	PMString fPost;
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

	PMString locator, flag, pre, match, post;
	data->GetSegments(locator, flag, pre, match, post);

	// Break characters become visible marks BEFORE anything is measured or ellipsized below: every
	// width taken from here on has to be the width of what is actually drawn.
	KBSResultModel::MarkUpBreaksForDisplay(pre);
	KBSResultModel::MarkUpBreaksForDisplay(match);
	KBSResultModel::MarkUpBreaksForDisplay(post);

	// The palette window's SYSTEM SCRIPT font - the same one every OTHER row of this tree already
	// draws in: KBS.fr's chapter label declares kPaletteWindowSystemScriptFontId for both its
	// normal and its hilite font (KBS.fr:1205), and the branch rows are stock static texts that
	// take it from there. This cell asked for kPaletteWindowFontId until 2026-08-07, which left one
	// row of one tree wanting a different font from the rows above it.
	//
	// It is also what the shipping panels reach for whenever a widget has to show text that came
	// out of a DOCUMENT, or that a user typed: the layer panel stamps it on the layer-name cell
	// (LayerPanelTreeViewWidgetMgr.cpp:128) and the spell panel's misspelled-word box asks for the
	// dialog-window counterpart (SpellDialogViews_enUS.fr:95). A hit row is exactly that case - it
	// draws the document's own text, in whatever script the document happens to be written in.
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);

	const PMRect frame = this->GetInnerContentFrame();
	const PMReal y = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, frame.Height());
	const PMReal rightEdge = frame.Right();
	PMReal x = frame.Left();

	// Is this cell's row the selected one? A self-drawing cell has to answer that itself: a stock
	// StaticText is handed four colours in the .fr (text / hilite text / background / hilite
	// background) and lets the framework pick, but drawing by hand means the two hilite colours go
	// unused unless they are asked for here. The app's own drawing does exactly this switch - see
	// CRenderingObjectDrawer::DrawRenderObjectUIName ("isHilited ? kInterfaceHighLightText :
	// kInterfaceTextColor"), MSOStateDDLElementView and cellpanel's TableCellView.
	const bool16 isHilited = KBSViewOrParentIsHilited(this, kKBSHiliteParentSteps);

	// Colours, entirely from the current theme so KBS matches whatever colours it uses:
	//   * bg = what this row is painted on - the panel's background fill (kInterfacePaletteFill),
	//          or the selection fill (kInterfaceHighLight) while the row is selected
	//   * fg = the theme's TEXT colour for that background (kInterfaceTextColor / its selected
	//          counterpart kInterfaceHighLightText - exactly what InDesign's own panels draw text
	//          with: black in a light UI, ~0.8 gray in a dark one; it flips with the theme, so
	//          nothing is hardcoded and nothing vanishes when the UI brightness changes)
	// Both have to move together: the context runs are faded TOWARD bg, so leaving bg as the panel
	// fill on a selected row would fade them toward a colour that is not behind them any more.
	// The matched text is drawn at the full theme text colour; the context (the "P<page>(<n>)"
	// locator and the rest of the line) is that same colour faded toward the background, so the
	// match reads at full strength and the context recedes. Tune kContextTextWeight to taste:
	// 0 = fully the background (invisible), 1 = the full text colour (no fade).
	//
	// 0.65 rather than the 0.50 it shipped with (user's call 2026-08-02, "blend it into the
	// background a little less"): half and half made the surrounding line harder to read than it
	// needed to be, and the match still stands out at this weight.
	const PMReal kContextTextWeight(0.65);
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);	// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLight : kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLightText : kInterfaceTextColor, fg);
	}
	const RealAGMColor kFullColor = fg;									// the theme's text colour
	const RealAGMColor kContextColor = BlendColor(bg, fg, kContextTextWeight);	// faded toward bg

	// The emphasised run: the matched text while these are search results, and the text that
	// REPLACED it once a replace has run (the panel then lists only what changed, so the new text
	// is exactly what the user wants to read - it gets the same emphasis a match does).
	const RealAGMColor kMatchColor = kFullColor;

	// The accent run: the one word that says why this row could not be acted on (missing /
	// refused). kInterfaceItemHighLight is the theme's own accent - blue-ish in the light UI,
	// orange in the dark one - so it stands out without a hardcoded colour that would go wrong
	// in one theme or the other. The theme table has no red, and none is invented here.
	//
	// On the SELECTED row the accent is dropped and the word is drawn in the ordinary selected-text
	// colour: the selection fill is itself an accent colour (blue-ish in the light UI), so accent on
	// accent is the one combination that can come out unreadable. Nothing is lost by it - the reason
	// is a WORD ("missing" / "refused"), and the colour only ever emphasised it. It is also the row
	// the user is already looking at.
	RealAGMColor accent = fg;
	if (colors != nil && !isHilited)
		colors->GetRealAGMColor(kInterfaceItemHighLight, accent);
	const RealAGMColor kAccentColor = accent;

	// The page locator ("P1(2)") is drawn at the full theme text colour, then the line text follows
	// straight after it.
	//
	// There used to be a tab stop here - a fixed column from the cell's left edge - so the line
	// text would form a column of its own. It cannot: the locator's width varies by several
	// characters now that it carries "overset", "hidden" and "locked", so a short locator was flung
	// out to the tab while a long one sat right against its text. The same list showed both gaps at
	// once and the wide one read as a mistake (user's call 2026-07-28, from the running panel).
	// The words grew again on 2026-08-04 ("ov" -> "overset", "lock" -> "locked"), which only makes
	// the fixed column less workable - one gap remains the right answer.
	//
	// So: one gap, always. The column that matters is the locator's left edge, and the row widget
	// keeps that fixed for every row (see KBSResultListWidgetMgr - the check box sits in the margin
	// rather than pushing its row's text right).
	// Named rather than passed as bare kFalse, which is how the app's own drawing code writes it
	// (CRenderingObjectDrawer::DrawRenderObjectUIName). Every call below spells both out instead of
	// letting the defaults apply, because the defaults in DrawStringUtils.h DISAGREE with each
	// other: the draw calls default to kFalse but the measure and ellipsize calls default to kTrue.
	// Taking the defaults would measure a string differently from how it is drawn. '&' has to
	// survive verbatim here in any case - this is document text, not a menu label.
	const bool16 kDontConvertAmpersand = kFalse;
	const bool16 kNoUnderline = kFalse;

	const PMReal kLocatorGap(8.0);		// space between the locator and the line text
	if (!locator.IsEmpty() && x < rightEdge)
	{
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), locator, fontInfo, kMatchColor, kDontConvertAmpersand, kNoUnderline);
		x += StringUtils::PMMeasureString(&gc, locator, fontInfo, kDontConvertAmpersand).X();
	}
	// Its own run so it can carry its own colour, with the separating space inside it - the
	// locator is built without this word for exactly that reason (KBSResultModel::BuildHitLocator).
	if (!flag.IsEmpty() && x < rightEdge)
	{
		PMString flagRun(" ");
		flagRun.SetTranslatable(kFalse);
		flagRun.Append(flag);
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), flagRun, fontInfo, kAccentColor, kDontConvertAmpersand, kNoUnderline);
		x += StringUtils::PMMeasureString(&gc, flagRun, fontInfo, kDontConvertAmpersand).X();
	}
	if (x > frame.Left())
		x += kLocatorGap;

	// The line, left to right. convertAmpersand=kFalse on draw AND measure so a literal '&' is
	// neither underlined nor dropped. If the whole line fits it is drawn as-is; when it overflows
	// the match is kept at full strength and the context is ellipsized around it. The matched run
	// is the full theme text colour; the context runs are faded.
	//
	// A missing-glyph row used to give up to a third of this width to the FONT NAME, drawn
	// right-aligned. The tree names the font on the row ABOVE the group now (2026-08-02), so the
	// whole cell goes back to the line.
	const PMReal availWidth = rightEdge - x;
	if (availWidth <= PMReal(0.0))
		return;		// the locator consumed the cell; no room left for the line

	// Draw one run at the running x and advance past it (an empty run is a no-op).
	auto drawRun = [&](const PMString& s, const RealAGMColor& c)
	{
		if (s.IsEmpty())
			return;
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), s, fontInfo, c, kDontConvertAmpersand, kNoUnderline);
		x += StringUtils::PMMeasureString(&gc, s, fontInfo, kDontConvertAmpersand).X();
	};

	const PMReal preW   = pre.IsEmpty()   ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, pre,   fontInfo, kDontConvertAmpersand).X();
	const PMReal matchW = match.IsEmpty() ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, match, fontInfo, kDontConvertAmpersand).X();
	const PMReal postW  = post.IsEmpty()  ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, post,  fontInfo, kDontConvertAmpersand).X();

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
		const PMString m = StringUtils::PMEllipsizeString(&gc, availWidth, match, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);
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
			preCut = StringUtils::PMEllipsizeString(&gc, rem, pre, fontInfo, kEllipsizeBeginning, nil, kDontConvertAmpersand);
		const PMReal preCutW = preCut.IsEmpty() ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, preCut, fontInfo, kDontConvertAmpersand).X();

		const PMReal postBudget = rem - preCutW;
		PMString postCut;
		if (!post.IsEmpty() && postBudget > PMReal(0.0))
			postCut = StringUtils::PMEllipsizeString(&gc, postBudget, post, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);

		drawRun(preCut, kContextColor);
		drawRun(match, kMatchColor);
		drawRun(postCut, kContextColor);
	}
}

// End, KBSColorTextView.cpp.
