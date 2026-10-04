//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  KFCColorTextView: a self-drawing tree cell that paints a hit line with the matched part in a
//  highlight colour (VS "Find in Files" style). A stock StaticText draws one colour per row, so
//  the hit rows use this DVControlView-derived cell instead, drawing up to FIVE runs left to right:
//  the page locator, the accent-coloured flag word, then the line split around the match (before /
//  matched / after). KFCRowData is the tiny data holder aggregated on the same boss.
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
#include "ITextControlData.h"	// the row's line as plain text, for a reader that walks the widgets
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
#include "KFCUIID.h"
#include "KFCColorTextView.h"
#include "KFCPanelTextDraw.h"	// the context's fade, the '&' flags and the bar - shared with the message area
#include "KFCModelAccess.h"		// the model half, through its session interfaces

// How far up the widget chain to look for the hilite (see KFCViewOrParentIsHilited). One step is
// all this panel needs (cell -> row); the extra steps only keep it working if the row ever gains
// another wrapper.
static const int32 kKFCHiliteParentSteps = 3;

// True if this view, or a widget above it, is drawn hilited - i.e. this cell belongs to the row the
// user has selected. The tree applies the hilite to the ROW widget (the base
// CTreeViewWidgetMgr::ApplyNodeIDToWidget does it, "for hilite selection"), and this cell is one of
// that row's children, so a cell that only asked itself would never see the selection.
static bool16 KFCViewOrParentIsHilited(IControlView* view, int32 stepsLeft)
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
	return KFCViewOrParentIsHilited(parentView, stepsLeft - 1);
}

// (The blend that fades the context toward the background is KFCBlendColor in KFCPanelTextDraw.h, with
//  the 0.65 it is used at: the panel's message area fades its context the same way, and two copies would
//  be two things to keep in step.)

// (The break characters are turned into marks by KFCResultModel::MarkUpBreaksForDisplay - one rule for
//  this cell, the story rows, the message area and the label a reader walks.)

//----------------------------------------------------------------------------------------
// KFCRowData - the per-row data holder (five strings)
//----------------------------------------------------------------------------------------

/** Non-persistent holder for a hit row's strings - locator, flag, and the line split into before /
    matched / after - aggregated on the colour cell's boss beside the view. Written by the widget
    manager on every apply. */
class KFCRowData : public CPMUnknown<IKFCRowData>
{
public:
	KFCRowData(IPMUnknown* boss) : CPMUnknown<IKFCRowData>(boss) {}
	virtual ~KFCRowData() {}

	virtual void SetSegments(const PMString& locator, const PMString& flag, const PMString& pre,
		const PMString& match, const PMString& post)
	{
		fLocator = locator; fLocator.SetTranslatable(kFalse);
		fFlag = flag; fFlag.SetTranslatable(kFalse);
		fPre = pre;   fPre.SetTranslatable(kFalse);
		fMatch = match; fMatch.SetTranslatable(kFalse);
		fPost = post; fPost.SetTranslatable(kFalse);

		// The same line as plain text on this boss's ITextControlData, which is where a reader that
		// walks the widgets looks for a label (KIDMCP's inspect_ui). Laid out as the cell draws it -
		// locator, flag, then the line with its breaks marked - and the match in [ ] where the cell
		// uses colour. Written here, the one place every row's parts arrive, so the two cannot drift.
		//
		// THE STOCK ONE, INHERITED FROM kGenericPanelWidgetBoss - DO NOT AGGREGATE ANOTHER.
		// It is persistent and reads the resource's "Panel name" field; nothing draws it, since this
		// cell paints itself. A non-persistent implementation of our own replacing it crashed InDesign
		// the first time a row was built: DVPanelControlData::ReadWrite called the
		// ReadWrite it did not have (RIP 0, under KFCResultListWidgetMgr::CreateWidgetForNode).
		InterfacePtr<ITextControlData> label(this, UseDefaultIID());
		if (label != nil)
		{
			PMString line(fLocator);
			if (!fFlag.IsEmpty())
			{
				line.Append(" ");
				line.Append(fFlag);
			}
			PMString preShown(fPre), matchShown(fMatch), postShown(fPost);
			KFCResults()->MarkUpBreaksForDisplay(preShown);
			KFCResults()->MarkUpBreaksForDisplay(matchShown);
			KFCResults()->MarkUpBreaksForDisplay(postShown);
			line.Append("  ");
			line.Append(preShown);
			line.Append("[");
			line.Append(matchShown);
			line.Append("]");
			line.Append(postShown);
			line.SetTranslatable(kFalse);
			label->SetString(line, kFalse /*invalidate: the view draws from the segments*/,
				kFalse /*notify: nothing observes it*/);
		}
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

CREATE_PMINTERFACE(KFCRowData, kKFCRowDataImpl)

//----------------------------------------------------------------------------------------
// KFCColorTextView - the self-drawing cell
//----------------------------------------------------------------------------------------

/** Implements IControlView: draws a hit line with the matched part highlighted. */
class KFCColorTextView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KFCColorTextView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KFCColorTextView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KFCColorTextView, kKFCColorTextViewImpl)

void KFCColorTextView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	InterfacePtr<IKFCRowData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString locator, flag, pre, match, post;
	data->GetSegments(locator, flag, pre, match, post);

	// Break characters become visible marks BEFORE anything is measured or ellipsized below: every
	// width taken from here on has to be the width of what is actually drawn.
	KFCResults()->MarkUpBreaksForDisplay(pre);
	KFCResults()->MarkUpBreaksForDisplay(match);
	KFCResults()->MarkUpBreaksForDisplay(post);

	// The palette window's SYSTEM SCRIPT font - the same one every OTHER row of this tree already
	// draws in: the branch row resource's label widget (kKFCResultChapterLabelWidgetID, in KFCUI.fr's
	// KFCResultNodeWidget for kKFCResultChapterNodeWidgetRsrcID) declares
	// kPaletteWindowSystemScriptFontId for both its normal and its hilite font, and the branch rows
	// are stock static texts that take it from there. (Not kPaletteWindowFontId: that left this one
	// row of the tree wanting a different font from the rows above it.)
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
	const bool16 isHilited = KFCViewOrParentIsHilited(this, kKFCHiliteParentSteps);

	// Colours, entirely from the current theme so KFC matches whatever colours it uses:
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
	// match reads at full strength and the context recedes. How far it recedes is
	// kKFCContextTextWeight (KFCPanelTextDraw.h, with its history) - one number for this cell and the
	// panel's message area.
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);	// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLight : kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLightText : kInterfaceTextColor, fg);
	}
	const RealAGMColor kFullColor = fg;									// the theme's text colour
	const RealAGMColor kContextColor = KFCBlendColor(bg, fg, PMReal(kKFCContextTextWeight));	// faded toward bg

	// The emphasised run: the matched text while these are search results, and the text that
	// REPLACED it once a replace has run (the panel then lists only what changed, so the new text
	// is exactly what the user wants to read - it gets the same emphasis a match does).
	const RealAGMColor kMatchColor = kFullColor;

	// The accent run: the one word that says why this row could not be acted on
	// (KFCResultModel::BuildHitLocator's words). kInterfaceItemHighLight is the theme's own accent -
	// blue-ish in the light UI, orange in the dark one - so it stands out without a hardcoded colour
	// that would go wrong in one theme or the other. The theme table has no red, and none is invented
	// here.
	//
	// On the SELECTED row the accent is dropped and the word is drawn in the ordinary selected-text
	// colour: the selection fill is itself an accent colour (blue-ish in the light UI), so accent on
	// accent is the one combination that can come out unreadable. Nothing is lost by it - the reason
	// is a WORD, and the colour only ever emphasised it. It is also the row
	// the user is already looking at.
	RealAGMColor accent = fg;
	if (colors != nil && !isHilited)
		colors->GetRealAGMColor(kInterfaceItemHighLight, accent);
	const RealAGMColor kAccentColor = accent;

	// The page locator ("P1(2)") is drawn at the full theme text colour, then the line text follows
	// straight after it.
	//
	// NO TAB STOP - a fixed column from the cell's left edge - for the line text. The locator's width
	// varies by several characters ("overset", "hidden", "locked"), so a short locator was flung out
	// to the tab while a long one sat right against its text; the same list showed both gaps at once
	// and the wide one read as a mistake (the author's call, from the running panel).
	//
	// So: one gap, always. The column that matters is the locator's left edge, and the row widget
	// keeps that fixed for every row (see KFCResultListWidgetMgr - the check box sits in the margin
	// rather than pushing its row's text right).
	// Every call below spells out both flags instead of letting the defaults apply - the defaults in
	// DrawStringUtils.h DISAGREE with each other (KFCPanelTextDraw.h says how, and why '&' survives).
	const bool16 kDontConvertAmpersand = kKFCDontConvertAmpersand;
	const bool16 kNoUnderline = kKFCNoUnderline;

	const PMReal kLocatorGap(8.0);		// space between the locator and the line text
	if (!locator.IsEmpty() && x < rightEdge)
	{
		StringUtils::PMDrawStringRGB(&gc, PMPoint(x, y), locator, fontInfo, kMatchColor, kDontConvertAmpersand, kNoUnderline);
		x += StringUtils::PMMeasureString(&gc, locator, fontInfo, kDontConvertAmpersand).X();
	}
	// Its own run so it can carry its own colour, with the separating space inside it - the
	// locator is built without this word for exactly that reason (KFCResultModel::BuildHitLocator).
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

	// AN EMPTY MATCH IS A PLACE, AND IT IS DRAWN AS A BAR (the author's request: "the bar KCM draws").
	// A row replaced with nothing, and a zero-width match (^, $, a lookaround), have no characters at the
	// match, so without it the line closes up around it and does not say WHERE. The bar stands in the
	// room of one space (KFCPanelTextDraw.h) and everything below treats it as a match of that width - so
	// the context gives way around it exactly as it gives way around characters.
	// EVERY EMPTY MATCH, EVEN WITH NOTHING EITHER SIDE (the author: "no bar when it was deleted at the
	// very end"). Do not hold it back when the line has no other text: that is exactly what the LAST
	// paragraph of a story looks like once its only word is replaced with nothing - a paragraph in the
	// middle keeps its pilcrow in the trailing context, the last one has none - so the bar would go
	// missing there alone. (A "deleted" row keeps the match it was found with. A match whose story had no text model to
	// read comes with three empty segments and now draws the bar alone - a place with nothing to show,
	// which is what the bar says.)
	// ! The label a reader walks still says "[]" (KFCRowData::SetSegments): the bar is drawn, never
	//   written.
	const bool wantCaret = match.IsEmpty();

	const PMReal preW   = pre.IsEmpty()   ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, pre,   fontInfo, kDontConvertAmpersand).X();
	const PMReal matchW = StringUtils::PMMeasureString(&gc, wantCaret ? KFCCaretPlaceholder() : match, fontInfo,
		kDontConvertAmpersand).X();		// (wantCaret is match.IsEmpty(): otherwise there are characters to measure)
	const PMReal postW  = post.IsEmpty()  ? PMReal(0.0) : StringUtils::PMMeasureString(&gc, post,  fontInfo, kDontConvertAmpersand).X();

	// The match at the running x: its characters, or the bar in their place - spanning the cell a pixel
	// short of each edge, as KCM's one-line change row draws it.
	auto drawMatch = [&](const PMString& s)
	{
		if (!wantCaret)
		{
			drawRun(s, kMatchColor);
			return;
		}
		KFCDrawCaret(gPort, kMatchColor, x, matchW, frame.Top() + PMReal(1.0), frame.Height() - PMReal(2.0));
		x += matchW;
	};

	if (preW + matchW + postW <= availWidth)
	{
		// The whole line fits: draw the three runs unchanged.
		drawRun(pre, kContextColor);
		drawMatch(match);
		drawRun(post, kContextColor);
	}
	else if (matchW >= availWidth)
	{
		// The match alone overflows the cell: ellipsize the match itself (tail) and drop the context.
		// (A bar is one space wide and is drawn as it is.)
		if (wantCaret)
			drawMatch(match);
		else
		{
			const PMString m = StringUtils::PMEllipsizeString(&gc, availWidth, match, fontInfo, kEllipsizeEnd, nil, kDontConvertAmpersand);
			drawRun(m, kMatchColor);
		}
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
		drawMatch(match);
		drawRun(postCut, kContextColor);
	}
}

// End, KFCColorTextView.cpp.
