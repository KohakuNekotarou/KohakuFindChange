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
//  Recipe: a DVControlView cell (customdatalinkui's) drawn in DV_Draw with DrawStringUtils.h's drawbot overloads -
//  drover, as the stock rows beside it are, so its letters come out as theirs do (KFCPanelTextDraw.h says why it is
//  not Draw and AGM any more) - the palette SYSTEM SCRIPT font from IInterfaceFonts (the one the branch rows use, and
//  the one the shipping panels use for document text), each run in the cell's box, centred top to bottom by the draw
//  itself. convertAmpersand is kFalse on BOTH the draw and the measure so a literal '&' in the search text is neither
//  underlined nor dropped. Selected rows are drawn in the theme's selected-text colours, which a hand-drawn
//  cell has to ask for itself (a stock StaticText gets all four colours from its .fr and lets the
//  framework choose) - the same isHilited switch the app's own drawing makes. When a line overflows
//  the cell the match is kept at full strength and the context is ellipsized around it (the stock rows
//  ellipsize automatically; this custom cell does it by hand): the leading context loses its HEAD
//  (kEllipsizeBeginning, so the words just before the match survive with a leading "..."), the
//  trailing context loses its TAIL (kEllipsizeEnd).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"		// IsHilited - is this cell's row the selected one?
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "IInterfaceFonts.h"	// the palette window font
#include "ITextControlData.h"	// the row's line as plain text, for a reader that walks the widgets
#include "IWidgetParent.h"		// QueryParentFor - this cell -> the row widget that carries the hilite

// General includes:
#include "CPMUnknown.h"
#include "DVAPublicIncludes.h"	// dvaui::drawbot - what DV_Draw is handed
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB (drawbot) - the cuts go through KFCEllipsize
#include "WidgetDefs.h"			// EllipsizeStyle (kEllipsizeBeginning / kEllipsizeEnd), kAlignLeft
#include "ISession.h"			// GetExecutionContextSession
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId

// Project includes:
#include "KFCUIID.h"
#include "KFCColorTextView.h"
#include "KFCKentenMark.h"		// a kenten row's marks - and KFCMarkPassView, this cell's base
#include "KFCPanelTextDraw.h"	// the context's fade, the '&' flags and the bar - shared with the message area
#include "KFCModelAccess.h"		// the model half, through its session interfaces
#include "KFCTierWords.h"		// what of a tier's name fades (KFCBranchLabelView)

// How far up the widget chain to look for the hilite (see KFCViewOrParentIsHilited). One step is
// all this panel needs (cell -> row); the extra steps only keep it working if the row ever gains
// another wrapper.
static const int32 kKFCHiliteParentSteps = 3;

// True if this view, or a widget above it, is drawn hilited - i.e. this cell belongs to the row the
// user has selected. The tree applies the hilite to the ROW widget (the base
// CTreeViewWidgetMgr::ApplyNodeIDToWidget does it, "for hilite selection"), and this cell is one of
// that row's children, so a cell that only asked itself would never see the selection.
static bool16 KFCViewOrParentIsHilited(const IControlView* view, int32 stepsLeft)
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
	KFCRowData(IPMUnknown* boss) : CPMUnknown<IKFCRowData>(boss), fTwoLines(false) {}
	virtual ~KFCRowData() {}

	virtual void SetTwoLines(bool on, const PMString& drawMatch, const KFCRowUpper& upper)
	{
		fTwoLines = on;
		fDrawMatch = drawMatch;
		fDrawMatch.SetTranslatable(kFalse);
		fUpper = upper;
	}

	virtual bool GetTwoLines(PMString& outDrawMatch, KFCRowUpper& outUpper) const
	{
		outDrawMatch = fDrawMatch;
		outUpper = fUpper;
		return fTwoLines;
	}

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
	bool fTwoLines;			// a two-line row (SetTwoLines)
	PMString fDrawMatch;	// ...what its lower line draws for the match
	KFCRowUpper fUpper;		// ...and what stands over it
};

CREATE_PMINTERFACE(KFCRowData, kKFCRowDataImpl)

//----------------------------------------------------------------------------------------
// KFCColorTextView - the self-drawing cell
//----------------------------------------------------------------------------------------

/** Implements IControlView: draws a hit line with the matched part highlighted. */
class KFCColorTextView : public KFCMarkPassView
{
	typedef KFCMarkPassView inherited;
public:
	KFCColorTextView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KFCColorTextView() {}

	/** DV_Draw, not Draw: drawn by drover as the stock rows are (KFCPanelTextDraw.h - Draw is handed an offscreen AGM
	    port, and text drawn there came out black and white beside the stock rows' ClearType). Nothing is painted
	    behind the text: the row's own fill (the selection's included) is the tree's. */
	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const;
};

CREATE_PERSIST_PMINTERFACE(KFCColorTextView, kKFCColorTextViewImpl)

void KFCColorTextView::DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const
{
	if (drawbotP == nil)
		return;

	InterfacePtr<IKFCRowData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString locator, flag, pre, match, post;
	data->GetSegments(locator, flag, pre, match, post);

	// A TWO-LINE ROW: the lower line draws the match's text alone (the label a reader walks keeps base<<reading>>), what stands over it on the upper line. Offsets over the text, one
	// character to one: MarkUpBreaksForDisplay below puts a mark where a break is.
	PMString twoLineMatch;
	KFCRowUpper upper;
	const bool twoLines = data->GetTwoLines(twoLineMatch, upper);
	if (twoLines)
		match = twoLineMatch;

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
	const PMReal rightEdge = frame.Right();
	PMReal x = frame.Left();
	// THE LINE THE ROW'S RUNS STAND ON: the cell's whole height - on a two-line row, its lower half, the upper half what stands over the match (kKFCResultTallRowHeight).
	const PMReal upperLineHeight = twoLines ? frame.Height() / PMReal(2.0) : PMReal(0.0);
	const PMReal lineTop = frame.Top() + upperLineHeight;
	const PMReal lineBottom = frame.Bottom();

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
	// The matched text - and the "P<page>(<n>)" locator ahead of it (below) - is drawn at the full theme text
	// colour; the context (the line around the match) is that same colour faded toward the background, so the
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
	// A KEPT ROW (a tier of the stacked results - KFCResultHistory): the widget manager disables the cell, and the whole row
	// is drawn one step fainter - the theme's text colour faded once toward the background where a live row has it at full
	// strength, its context faded once more, no accent. A kept row is never selected, so never hilited.
	// The cell's OWN flag (GetEnableState): IsEnabled answers for its ancestors too, and a panel disabled as a whole would
	// then draw every live row faint.
	const bool kept = !this->GetEnableState();
	if (kept)
		fg = KFCBlendColor(bg, fg, PMReal(kKFCContextTextWeight));
	const RealAGMColor kFullColor = fg;									// the theme's text colour
	const RealAGMColor kContextColor = KFCBlendColor(bg, fg, PMReal(kKFCContextTextWeight));	// faded toward bg

	// The emphasised run: the matched text, and on a row this list has replaced the text that REPLACED it
	// (its locator starts with "Changed" - the new text is exactly what the user wants to read, so it gets the
	// same emphasis a match does).
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
	if (colors != nil && !isHilited && !kept)
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
	// keeps that fixed for every row (see KFCResultListWidgetMgr - kHitExtraIndent and kHitCellStep are
	// one number for every hit row).
	// Every call below spells out both flags instead of letting the defaults apply - the defaults in
	// DrawStringUtils.h DISAGREE with each other (KFCPanelTextDraw.h says how, and why '&' survives) - and a run's
	// width is KFCDrawnWidth's, the measure the message area takes as well.
	//
	// Draw one run at the running x and advance past it (an empty run is a no-op) - every run of the row: the locator,
	// its flag word, and the line's three below.
	// Each run in the cell's box from the running x, centred top to bottom by the draw itself (centerVertically) - the
	// baseline used to come from IWidgetUtils::GetViewYPosition, which asks an AGM graphics context DV_Draw has not.
	auto drawRun = [&](const PMString& s, const RealAGMColor& c)
	{
		if (s.IsEmpty())
			return;
		StringUtils::PMDrawStringRGB(drawbotP, PMRect(x, lineTop, rightEdge, lineBottom), kAlignLeft, s, fontInfo, c,
			kKFCDontConvertAmpersand, kTrue /*centerVertically*/, kKFCNoUnderline, kKFCTextSmoothing);
		x += KFCDrawnWidth(s, fontInfo);
	};

	// WHERE THE LINE STARTS, BEFORE ANYTHING IS DRAWN: a kenten row's marks are painted first (KFCMarkPassView - its pass blits over the whole cell), and where they go is where the match lands.
	const PMReal kLocatorGap(8.0);		// space between the locator and the line text
	PMReal lineX = x;
	if (lineX < rightEdge)
		lineX += KFCDrawnWidth(locator, fontInfo);
	// Its own run so it can carry its own colour, with the separating space inside it - the
	// locator is built without this word for exactly that reason (KFCResultModel::BuildHitLocator).
	PMString flagRun;
	flagRun.SetTranslatable(kFalse);
	if (!flag.IsEmpty() && lineX < rightEdge)
	{
		flagRun.Append(" ");
		flagRun.Append(flag);
		lineX += KFCDrawnWidth(flagRun, fontInfo);
	}
	if (lineX > frame.Left())
		lineX += kLocatorGap;

	// The line, left to right. convertAmpersand=kFalse on draw AND measure so a literal '&' is
	// neither underlined nor dropped. If the whole line fits it is drawn as-is; when it overflows
	// the match is kept at full strength and the context is ellipsized around it. The matched run
	// is the full theme text colour; the context runs are faded.
	const PMReal availWidth = rightEdge - lineX;
	if (availWidth <= PMReal(0.0))
	{
		// the locator consumed the cell; no room left for the line
		drawRun(locator, kMatchColor);
		drawRun(flagRun, kAccentColor);
		return;
	}

	// AN EMPTY MATCH IS A PLACE, AND IT IS DRAWN AS A BAR (the author's request: "the bar KCM draws").
	// A row replaced with nothing, and a zero-width match (^, $, a lookaround), have no characters at the
	// match, so without it the line closes up around it and does not say WHERE. The bar stands in the
	// room of one space (KFCPanelTextDraw.h) and everything below treats it as a match of that width - so
	// the context gives way around it exactly as it gives way around characters.
	// EVERY EMPTY MATCH, EVEN WITH NOTHING EITHER SIDE (the author: "no bar when it was deleted at the
	// very end"). Do not hold it back when the line has no other text: that is exactly what the LAST
	// paragraph of a story looks like once its only word is replaced with nothing - a paragraph in the
	// middle keeps its pilcrow in the trailing context, the last one has none - so the bar would go
	// missing there alone. (A match whose story had no text model to
	// read comes with three empty segments and now draws the bar alone - a place with nothing to show,
	// which is what the bar says.)
	// ! The label a reader walks still says "[]" (KFCRowData::SetSegments): the bar is drawn, never
	//   written.
	const bool wantCaret = match.IsEmpty();

	// The widths of the line's runs - the match's is the bar's room when it has no characters.
	const PMReal preW   = KFCDrawnWidth(pre, fontInfo);
	const PMReal matchW = KFCDrawnWidth(wantCaret ? KFCCaretPlaceholder() : match, fontInfo);
	const PMReal postW  = KFCDrawnWidth(post, fontInfo);

	// THE THREE RUNS AS THEY WILL BE DRAWN.
	PMString preRun, matchRun(match), postRun;
	preRun.SetTranslatable(kFalse);
	postRun.SetTranslatable(kFalse);
	if (preW + matchW + postW <= availWidth)
	{
		// The whole line fits: the three runs unchanged.
		preRun = pre;
		postRun = post;
	}
	else if (matchW >= availWidth)
	{
		// The match alone overflows the cell: ellipsize the match itself (tail) and drop the context.
		// (A bar is one space wide and is drawn as it is.)
		// (KFCEllipsize, as the two cuts below: the SDK's call never comes back from some widths, and its new
		// string is marked "do not translate" again - KFCPanelTextDraw.h says why for both)
		if (!wantCaret)
			matchRun = KFCEllipsize(availWidth, match, fontInfo, kEllipsizeEnd);
	}
	else
	{
		// The match fits but the whole line does not: keep the match at full strength and show as
		// much context as fits around it. The LEADING context loses its head (kEllipsizeBeginning,
		// so the words just before the match survive with a leading "..."); the TRAILING context
		// loses its tail (kEllipsizeEnd). Leading context is served first, so the run-up to the
		// match is preferred over what follows it.
		// (Both widths are what a subtraction leaves - the kind KFCEllipsize keeps from the SDK's call when it
		// lands where that call never comes back.)
		const PMReal rem = availWidth - matchW;
		preRun = KFCEllipsize(rem, pre, fontInfo, kEllipsizeBeginning);
		const PMReal postBudget = rem - KFCDrawnWidth(preRun, fontInfo);
		if (!post.IsEmpty() && postBudget > PMReal(0.0))
			postRun = KFCEllipsize(postBudget, post, fontInfo, kEllipsizeEnd);
	}

	// WHERE THE MATCH LANDS, AND THE PART OF IT THAT IS CHARACTERS OF ITS OWN (a cut one ends in dots no mark or reading stands over).
	const PMReal matchX = lineX + KFCDrawnWidth(preRun, fontInfo);
	int32 matchChars = 0;
	if (twoLines && !wantCaret)
	{
		const int32 most = (matchRun.CharCount() < match.CharCount()) ? matchRun.CharCount() : match.CharCount();
		while (matchChars < most && matchRun.GetWChar(matchChars) == match.GetWChar(matchChars))
			++matchChars;
	}
	// How far a character of the match stands from its start, as the line draws it.
	auto widthToChar = [&](int32 n) -> PMReal
	{
		if (n <= 0)
			return PMReal(0.0);
		PMString head(matchRun);
		head.SetTranslatable(kFalse);
		if (n < head.CharCount())
			head.Remove(n, head.CharCount() - n);
		return KFCDrawnWidth(head, fontInfo);
	};

	// THE MARKS, PAINTED FIRST: KFCMarkPassView's pass blits over the whole cell, so anything drawn before it could be painted over. A row whose mark has gone draws a bar instead (below).
	KFCKentenMark::Marks marks;
	if (twoLines && !upper.gone && upper.mark != 0 && matchChars > 0)
		KFCKentenMark::PlanOverRun(fontInfo, upper.mark, matchX, widthToChar(matchChars), matchChars, upperLineHeight,
			frame.Top() + (upperLineHeight / PMReal(2.0)), rightEdge, kFullColor, marks);
	this->PaintMarks(drawbotP, marks);

	// The lower line: the locator, its flag, the line.
	drawRun(locator, kMatchColor);
	drawRun(flagRun, kAccentColor);
	x = lineX;
	drawRun(preRun, kContextColor);
	// The match at the running x: its characters, or the bar in their place - spanning the line a pixel
	// short of each edge, as KCM's one-line change row draws it.
	if (wantCaret)
	{
		KFCDrawCaret(drawbotP, kMatchColor, x, matchW, lineTop + PMReal(1.0), (lineBottom - lineTop) - PMReal(2.0));
		x += matchW;
	}
	else
		drawRun(matchRun, kMatchColor);
	drawRun(postRun, kContextColor);

	if (!twoLines)
		return;

	// THE UPPER LINE (KCM's): its box, a pixel short of the cell's top, the strings centred in it by the draw itself.
	const PMRect upperBox(frame.Left(), frame.Top(), rightEdge, lineTop);
	// A ROW WHOSE RUBY OR KENTEN WAS TAKEN OFF: a bar over the match - what is gone is said where it stood, as KCM's row says it.
	if (upper.gone)
	{
		const PMReal barW = KFCDrawnWidth(KFCCaretPlaceholder(), fontInfo);
		KFCDrawCaret(drawbotP, kFullColor, matchX, barW, frame.Top() + PMReal(1.0), upperLineHeight - PMReal(2.0));
		return;
	}
	KFCKentenMark::WriteGlyphs(drawbotP, fontInfo, marks, upperBox.Top(), upperBox.Bottom());
	// THE READINGS: each over its own characters, centred on them, starting no further left than the cell or the reading before it (spec F12: a long one pushes the next rather than stands on it).
	PMReal nextFree = frame.Left();
	for (size_t r = 0; r < upper.runs.size(); ++r)
	{
		const KFCRowUpperRun& run = upper.runs[r];
		if (run.reading.IsEmpty() || run.offset >= matchChars)
			continue;		// (a run beyond where the cut line ended)
		const int32 runEnd = (run.offset + run.length < matchChars) ? run.offset + run.length : matchChars;
		const PMReal runLeft = matchX + widthToChar(run.offset);
		const PMReal runRight = matchX + widthToChar(runEnd);
		const PMReal readingW = KFCDrawnWidth(run.reading, fontInfo);
		PMReal readingX = (runLeft + runRight - readingW) / PMReal(2.0);
		if (readingX < nextFree)
			readingX = nextFree;
		if (readingX >= rightEdge)
			break;
		PMString shown(run.reading);
		shown.SetTranslatable(kFalse);
		if (readingX + readingW > rightEdge)
			shown = KFCEllipsize(rightEdge - readingX, run.reading, fontInfo, kEllipsizeEnd);
		// (A reading its Replace will give it, not one it has - a ruby list's row: faint, as the context.)
		StringUtils::PMDrawStringRGB(drawbotP, PMRect(readingX, upperBox.Top(), rightEdge, upperBox.Bottom()), kAlignLeft, shown, fontInfo, upper.planned ? kContextColor : kFullColor,
			kKFCDontConvertAmpersand, kTrue /*centerVertically*/, kKFCNoUnderline, kKFCTextSmoothing);
		nextFree = readingX + KFCDrawnWidth(shown, fontInfo) + PMReal(2.0);
	}
}

//----------------------------------------------------------------------------------------
// KFCBranchLabelView - a branch row's label
//----------------------------------------------------------------------------------------

/** Implements IControlView: a book's, a document's, a story's or a tier's row's words, by the hit row's cell's rules - its font, its colours, selected and kept, its '&' as it stands, cut at the end when it overflows - with what of a tier's name says what it asked faded as a hit row's context is (the author, 11-02: KFCTierWords.h). Its words are the generic panel's ITextControlData's, where a reader that walks the widgets looks for them. */
class KFCBranchLabelView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KFCBranchLabelView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KFCBranchLabelView() {}

	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const;
};

CREATE_PERSIST_PMINTERFACE(KFCBranchLabelView, kKFCBranchLabelViewImpl)

void KFCBranchLabelView::DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const
{
	if (drawbotP == nil)
		return;
	InterfacePtr<const ITextControlData> words(this, UseDefaultIID());
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (words == nil || fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);
	const bool16 isHilited = KFCViewOrParentIsHilited(this, kKFCHiliteParentSteps);
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);	// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLight : kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(isHilited ? kInterfaceHighLightText : kInterfaceTextColor, fg);
	}
	// A KEPT ROW (the widget manager disables the cell): one step fainter, as a kept hit row's (KFCColorTextView::DV_Draw's note).
	if (!this->GetEnableState())
		fg = KFCBlendColor(bg, fg, PMReal(kKFCContextTextWeight));
	const RealAGMColor kFadedColor = KFCBlendColor(bg, fg, PMReal(kKFCContextTextWeight));
	const WideString wide(words->GetString());
	std::u32string name;
	for (WideString::const_iterator c = wide.begin(); c != wide.end(); ++c)
		name.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	const std::vector<KFCTierWords::Run> runs(KFCTierWords::RunsOf(name));
	const PMRect frame = this->GetInnerContentFrame();
	PMString upTo;
	upTo.SetTranslatable(kFalse);
	for (size_t r = 0; r < runs.size(); ++r)
	{
		PMString piece;
		piece.SetTranslatable(kFalse);
		for (size_t k = 0; k < runs[r].length; ++k)
			piece.AppendW(UTF32TextChar(static_cast<uint32>(name[runs[r].start + k])));
		upTo.Append(piece);
		// WHERE IT STARTS: the name up to here less this run, not the runs' widths added up. Drover's measure of a string is a little over what it draws, once a string - added up, that stood between the runs as gaps (photographed, 11-02: "Find:" [Ruby Marks] " and the count cut off).
		const PMReal x = frame.Left() + KFCDrawnWidth(upTo, fontInfo) - KFCDrawnWidth(piece, fontInfo);
		if (x >= frame.Right())
			break;
		const PMReal room = frame.Right() - x;
		const bool cut = KFCDrawnWidth(piece, fontInfo) > room;
		const PMString shown(cut ? KFCEllipsize(room, piece, fontInfo, kEllipsizeEnd) : piece);
		StringUtils::PMDrawStringRGB(drawbotP, PMRect(x, frame.Top(), frame.Right(), frame.Bottom()), kAlignLeft, shown, fontInfo, runs[r].faded ? kFadedColor : fg,
			kKFCDontConvertAmpersand, kTrue /*centerVertically*/, kKFCNoUnderline, kKFCTextSmoothing);
		if (cut)
			break;
	}
}

// End, KFCColorTextView.cpp.
