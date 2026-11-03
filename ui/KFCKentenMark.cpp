//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Paints the emphasis marks on the upper line of a kenten list's row - see KFCKentenMark.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGraphicsPort.h"		// newpath / moveto / curveto / fill / stroke / gsave

// General includes:
#include "AGMGraphicsContext.h"	// KFCMarkPassView::Draw - the pass's AGM port
#include "AutoGSave.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB (drawbot) - a custom mark is WRITTEN

// Project includes:
#include "KFCKentenMark.h"
#include "KFCModelTypes.h"		// KFCKentenMarkCharacter - what a custom mark is written as
#include "KFCPanelTextDraw.h"	// KFCDrawnWidth, the '&' flags, the smoothing

namespace
{

// The constant that turns four cubic beziers into a circle: IGraphicsPort has no arc, oval or ellipse (KCMKentenMark.cpp).
const PMReal kKFCCircleK(0.5522847498);

// An ellipse centred on (cx, cy). THE PATH IS LEFT ON THE PORT: the caller chooses fill or stroke - the whole difference between a black mark and a white one.
void EllipsePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy, const PMReal& rx, const PMReal& ry)
{
	const PMReal kx = rx * kKFCCircleK;
	const PMReal ky = ry * kKFCCircleK;
	gPort->newpath();
	gPort->moveto(cx + rx, cy);
	gPort->curveto(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
	gPort->curveto(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
	gPort->curveto(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
	gPort->curveto(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
	gPort->closepath();
}

// A triangle standing on its base, pointing up, its box centred on (cx, cy). UP IS MINUS Y HERE: a screen port.
void TrianglePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy, const PMReal& halfW, const PMReal& halfH)
{
	gPort->newpath();
	gPort->moveto(cx, cy - halfH);
	gPort->lineto(cx + halfW, cy + halfH);
	gPort->lineto(cx - halfW, cy + halfH);
	gPort->closepath();
}

// The radius a HOLLOW mark is drawn at, to end up the size of a filled one: a stroke straddles its path. WITH A FLOOR: a negative radius draws the curve inside out.
PMReal StrokeRadius(const PMReal& r, const PMReal& lineWidth)
{
	const PMReal pulled = r - (lineWidth / PMReal(2.0));
	return (pulled < PMReal(0.25)) ? PMReal(0.25) : pulled;
}

// A SESAME DOT, as InDesign draws it (2026-11-03 - its outline traced off the page at 300 dpi, Kozuka Mincho Pr6N R, and laid over it - work/note-scripts/2026-11-03-kfc-kenten-face/sesame_overlay.py):
// a bent drop from a blunt tail at the top left, along a convex upper edge to a round head at the bottom right, back up a nearly straight lower edge. Points in its box, [-1, 1] each way, y down; the box
// w x h centred on (cx, cy).
void SesamePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy, const PMReal& w, const PMReal& h)
{
	const PMReal hw = w / PMReal(2.0);
	const PMReal hh = h / PMReal(2.0);
	auto X = [&](double v) { return cx + hw * PMReal(v); };
	auto Y = [&](double v) { return cy + hh * PMReal(v); };
	gPort->newpath();
	gPort->moveto(X(-0.81), Y(-1.00));
	gPort->curveto(X(-0.28), Y(-0.94), X(0.84), Y(-0.42), X(1.00), Y(0.50));
	gPort->curveto(X(1.05), Y(0.85), X(0.80), Y(1.02), X(0.55), Y(1.00));
	gPort->curveto(X(0.35), Y(0.98), X(0.25), Y(0.75), X(0.15), Y(0.45));
	gPort->curveto(X(-0.15), Y(-0.10), X(-0.55), Y(-0.55), X(-1.00), Y(-0.84));
	gPort->closepath();
}

// THE MARKS' SIZES, INDESIGN'S OWN (2026-11-03 - measured on the page at 600 dpi: Kozuka Mincho Pr6N R at 24 pt, its kenten at the default size, half the base character - the kenten's em, K), each a share of K:
// a circle 0.69 across (white: a stroke of 0.06), the small black circle 0.43, the small white one 0.47, the fisheye a ring of 0.69 round a dot of 0.38, the bullseye rings of 0.69 and 0.38, a triangle 0.76 wide
// and 0.66 high, a sesame dot 0.65 x 0.67 - every mark's centre on one line.
const double kCircleK = 0.69;
const double kSmallBlackK = 0.43;
const double kSmallWhiteK = 0.47;
const double kInnerK = 0.38;
const double kTriangleWK = 0.76;
const double kTriangleHK = 0.66;
const double kSesameWK = 0.65;
const double kSesameHK = 0.67;
const double kStrokeK = 0.06;
// A STROKE NO THINNER THAN THE SCREEN SHOWS: at the panel's size InDesign's 0.06 K is under a pixel.
const PMReal kMinStroke(1.0);
// ...AND A PIXEL BETWEEN A RING AND WHAT IS INSIDE IT: at the panel's size InDesign's own proportions leave a fisheye's ring and dot half a pixel apart, one dot on the screen (measured 11-03). The inside shrinks
// to keep it; at a size where they part anyway, InDesign's own.
const PMReal kMinRingGap(1.0);

// A kind's shape: IKentenStyle::KentenKind's numbers (KFCKentenMarkCharacter's table). None for Custom and anything else.
enum Shape
{
	kShapeNone = 0,
	kShapeBlackSesame = 1,
	kShapeWhiteSesame = 2,
	kShapeFisheye = 3,
	kShapeBlackCircle = 4,
	kShapeSmallBlackCircle = 5,
	kShapeBullseye = 6,
	kShapeBlackTriangle = 7,
	kShapeWhiteTriangle = 8,
	kShapeWhiteCircle = 9,
	kShapeSmallWhiteCircle = 10
};

// One mark, the kenten's em K given (InDesign's sizes - above). A stroked shape is drawn half its stroke inside its outline, so it ends up the size of the filled one. THE PORT IS HANDED BACK AS IT WAS.
void DrawShape(IGraphicsPort* gPort, Shape shape, const PMReal& centreX, const PMReal& centreY, const PMReal& em, const RealAGMColor& colour)
{
	if (gPort == nil || em <= PMReal(0.0) || shape == kShapeNone)
		return;
	gPort->gsave();
	gPort->setrgbcolor(colour.red, colour.green, colour.blue);
	PMReal lineWidth = em * PMReal(kStrokeK);
	if (lineWidth < kMinStroke)
		lineWidth = kMinStroke;
	gPort->setlinewidth(lineWidth);
	const PMReal half(0.5);
	const PMReal r = em * PMReal(kCircleK) * half;
	const PMReal rStroke = StrokeRadius(r, lineWidth);
	// What stands inside a ring - a fisheye's dot, a bullseye's inner ring: InDesign's size, or less to keep kMinRingGap from the ring.
	PMReal insideR = em * PMReal(kInnerK) * half;
	const PMReal insideMost = rStroke - (lineWidth * half) - kMinRingGap;
	if (insideR > insideMost)
		insideR = (insideMost > PMReal(0.5)) ? insideMost : PMReal(0.5);
	switch (shape)
	{
		case kShapeBlackCircle:
			EllipsePath(gPort, centreX, centreY, r, r);
			gPort->fill();
			break;
		case kShapeWhiteCircle:
			EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
			gPort->stroke();
			break;
		case kShapeSmallBlackCircle:
		{
			const PMReal rs = em * PMReal(kSmallBlackK) * half;
			EllipsePath(gPort, centreX, centreY, rs, rs);
			gPort->fill();
			break;
		}
		case kShapeSmallWhiteCircle:
		{
			const PMReal rs = StrokeRadius(em * PMReal(kSmallWhiteK) * half, lineWidth);
			EllipsePath(gPort, centreX, centreY, rs, rs);
			gPort->stroke();
			break;
		}
		case kShapeFisheye:
		{
			// A ring round a solid dot a little over half its width.
			EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
			gPort->stroke();
			EllipsePath(gPort, centreX, centreY, insideR, insideR);
			gPort->fill();
			break;
		}
		case kShapeBullseye:
		{
			// Two rings, nothing filled - the inner as wide as a fisheye's dot.
			EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
			gPort->stroke();
			const PMReal inner = StrokeRadius(insideR, lineWidth);
			EllipsePath(gPort, centreX, centreY, inner, inner);
			gPort->stroke();
			break;
		}
		case kShapeBlackTriangle:
			TrianglePath(gPort, centreX, centreY, em * PMReal(kTriangleWK) * half, em * PMReal(kTriangleHK) * half);
			gPort->fill();
			break;
		case kShapeWhiteTriangle:
			TrianglePath(gPort, centreX, centreY, StrokeRadius(em * PMReal(kTriangleWK) * half, lineWidth), StrokeRadius(em * PMReal(kTriangleHK) * half, lineWidth));
			gPort->stroke();
			break;
		case kShapeBlackSesame:
			SesamePath(gPort, centreX, centreY, em * PMReal(kSesameWK), em * PMReal(kSesameHK));
			gPort->fill();
			break;
		case kShapeWhiteSesame:
			SesamePath(gPort, centreX, centreY, em * PMReal(kSesameWK) - lineWidth, em * PMReal(kSesameHK) - lineWidth);
			gPort->stroke();
			break;
		default:
			break;
	}
	gPort->grestore();
}

}	// anonymous namespace

// THE KENTEN'S EM IN THE PANEL: a share of the width a character got. InDesign's own is half the base character (0.5); at the panel's size that makes a fisheye, a bullseye and a black circle one dot of four pixels,
// so the panel's is 0.8 - the shapes and their sizes to one another stay InDesign's (2026-11-03 - drawn at 0.5, 0.65, 0.8 and 1.0 and looked at side by side: work/note-scripts/2026-11-03-kfc-kenten-face/
// kenten_marks_proto.py).
const double kEmOfChar = 0.8;
// WHERE THEY STAND: InDesign leaves 0.24 K between a circle's foot and the top of the character's ink (0.12 of the base character, its kenten half of it) - kept as a share of K here, so the marks, larger, stand off as
// far for their size. The character stands centred in the lower line, as tall as the upper (the cell is cut in two), its ink about 0.03 of its width under its em's top (11-03: the palette font's box 19 px for
// 12.75 px characters in a line of 17 - the ink began 2.5 px into the line, which the em centred in it says).
const double kGapOfK = 0.24;
const double kInkTopOfChar = 0.03;

void KFCKentenMark::PlanOverRun(const InterfaceFontInfo& font, int32 kind, int32 customChar, const PMReal& runLeft, const PMReal& runWidth, int32 charCount,
	const PMReal& lineTop, const PMReal& lineBottom, const PMReal& rightEdge, const RealAGMColor& colour, Marks& out)
{
	out = Marks();
	if (kind <= 0 || charCount <= 0 || runWidth <= PMReal(0.0) || lineBottom <= lineTop)
		return;
	const bool asShape = (kind >= kShapeBlackSesame && kind <= kShapeSmallWhiteCircle);
	// The width one character got: CJK is even enough for this to land the mark over its character.
	const PMReal perChar = runWidth / PMReal(charCount);
	const PMReal lineHeight = lineBottom - lineTop;
	PMReal em = perChar * PMReal(kEmOfChar);
	if (em > lineHeight)
		em = lineHeight;		// (a mark no taller than the line it stands in)
	// The centre: a circle's foot the gap over the ink of the character under it, in the lower line - as high as the upper line keeps the tallest mark (a custom one's em) inside it.
	const PMReal inkTop = lineBottom + ((lineHeight - perChar) / PMReal(2.0)) + perChar * PMReal(kInkTopOfChar);
	PMReal centreY = inkTop - em * PMReal(kGapOfK) - em * PMReal(kCircleK) / PMReal(2.0);
	const PMReal highest = lineTop + em / PMReal(2.0);
	if (centreY < highest)
		centreY = highest;
	out.fShape = asShape ? kind : 0;
	if (!asShape)
	{
		// A custom mark (or a kind unknown): its character, written at the kenten's em - the palette font at that share of its size.
		out.fGlyph.SetTranslatable(kFalse);
		out.fGlyph.AppendW(KFCKentenMarkCharacter(kind, customChar));
		out.fGlyphPts = static_cast<int32>(::ToDouble(PMReal(font.GetFontSize()) * PMReal(kEmOfChar)) + 0.5);
		if (out.fGlyphPts < 1)
			out.fGlyphPts = 1;
		const InterfaceFontInfo glyphFont(font.GetFontName(), out.fGlyphPts, font.GetFontAttributes());
		out.fGlyphW = KFCDrawnWidth(out.fGlyph, glyphFont);
	}
	out.fCentreY = centreY;
	out.fSize = em;
	out.fRight = rightEdge;
	out.fColour = colour;
	for (int32 i = 0; i < charCount; ++i)
	{
		const PMReal cx = runLeft + perChar * (PMReal(i) + PMReal(0.5));
		if (cx + (em / PMReal(2.0)) > rightEdge)
			break;				// ran out of room - the rest are simply not shown
		out.fCentreX.push_back(cx);
	}
}

void KFCKentenMark::PaintShapes(IGraphicsPort* gPort, const Marks& marks)
{
	if (gPort == nil || !marks.HasShapes())
		return;
	const Shape shape = static_cast<Shape>(marks.fShape);
	for (size_t i = 0; i < marks.fCentreX.size(); ++i)
		DrawShape(gPort, shape, marks.fCentreX[i], marks.fCentreY, marks.fSize, marks.fColour);
}

void KFCKentenMark::WriteGlyphs(dvaui::drawbot::Drawbot* drawbotP, const InterfaceFontInfo& font, const Marks& marks)
{
	if (drawbotP == nil || marks.fGlyph.IsEmpty() || marks.fCentreX.empty())
		return;
	// The palette font at the plan's size (KohakuGraph's canvas makes its zoomed words the same way); made here, for the few rows a custom mark has.
	const InterfaceFontInfo glyphFont(font.GetFontName(), marks.fGlyphPts, font.GetFontAttributes());
	const PMReal top = marks.fCentreY - marks.fSize;
	const PMReal bottom = marks.fCentreY + marks.fSize;
	for (size_t i = 0; i < marks.fCentreX.size(); ++i)
	{
		const PMReal left = marks.fCentreX[i] - (marks.fGlyphW / PMReal(2.0));
		StringUtils::PMDrawStringRGB(drawbotP, PMRect(left, top, marks.fRight, bottom), kAlignLeft, marks.fGlyph, glyphFont, marks.fColour,
			kKFCDontConvertAmpersand, kTrue /*centerVertically*/, kKFCNoUnderline, kKFCTextSmoothing);
	}
}

void KFCMarkPassView::PaintMarks(dvaui::drawbot::Drawbot* drawbotP, const KFCKentenMark::Marks& marks) const
{
	if (drawbotP == nil || !marks.HasShapes())
		return;
	fMarks = marks;
	fInMarkPass = true;
	inherited::DV_Draw(drawbotP);		// sets up the offscreen AGM port, calls Draw below, blits it back
	fInMarkPass = false;
	fMarks = KFCKentenMark::Marks();
}

void KFCMarkPassView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	if (!fInMarkPass)
	{
		inherited::Draw(viewPort, updateRgn);
		return;
	}
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);
	KFCKentenMark::PaintShapes(gPort, fMarks);
}

// End, KFCKentenMark.cpp.
