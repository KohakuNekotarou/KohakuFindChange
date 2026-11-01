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

// A triangle standing on its base, pointing up. UP IS MINUS Y HERE: a screen port.
void TrianglePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy, const PMReal& r)
{
	const PMReal halfW = r;
	const PMReal halfH = r * PMReal(0.88);
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

PMPoint TurnAbout(const PMReal& x, const PMReal& y, const PMReal& c, const PMReal& s, const PMReal& cx, const PMReal& cy)
{
	return PMPoint(cx + (x * c) - (y * s), cy + (x * s) + (y * c));
}

// A sesame dot: a teardrop lying over, thin at the top left, swelling to the bottom right (drawn from InDesign's own - KCMKentenMark.cpp).
void SesamePath(IGraphicsPort* gPort, const PMReal& cx, const PMReal& cy, const PMReal& rx, const PMReal& ry)
{
	const PMReal kCos(0.8660254);		// cos(-30 deg)
	const PMReal kSin(-0.5);			// sin(-30 deg)
	const PMPoint tip   = TurnAbout(PMReal(0.0),        -ry,                 kCos, kSin, cx, cy);
	const PMPoint r1    = TurnAbout(rx * PMReal(0.75),  -ry * PMReal(0.45),  kCos, kSin, cx, cy);
	const PMPoint r2    = TurnAbout(rx,                  ry * PMReal(0.25),  kCos, kSin, cx, cy);
	const PMPoint belly = TurnAbout(rx * PMReal(0.45),   ry * PMReal(0.90),  kCos, kSin, cx, cy);
	const PMPoint b1    = TurnAbout(-rx * PMReal(0.20),  ry,                 kCos, kSin, cx, cy);
	const PMPoint b2    = TurnAbout(-rx * PMReal(0.80),  ry * PMReal(0.55),  kCos, kSin, cx, cy);
	const PMPoint l1    = TurnAbout(-rx * PMReal(0.55), -ry * PMReal(0.10),  kCos, kSin, cx, cy);
	const PMPoint l2    = TurnAbout(-rx * PMReal(0.22), -ry * PMReal(0.60),  kCos, kSin, cx, cy);
	gPort->newpath();
	gPort->moveto(tip.X(), tip.Y());
	gPort->curveto(r1.X(), r1.Y(), r2.X(), r2.Y(), belly.X(), belly.Y());
	gPort->curveto(b1.X(), b1.Y(), b2.X(), b2.Y(), l1.X(), l1.Y());
	gPort->curveto(l2.X(), l2.Y(), tip.X(), tip.Y(), tip.X(), tip.Y());
	gPort->closepath();
}

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

// One mark. THE PORT IS HANDED BACK AS IT WAS.
void DrawShape(IGraphicsPort* gPort, Shape shape, const PMReal& centreX, const PMReal& centreY, const PMReal& size, const RealAGMColor& colour)
{
	if (gPort == nil || size <= PMReal(0.0) || shape == kShapeNone)
		return;
	gPort->gsave();
	gPort->setrgbcolor(colour.red, colour.green, colour.blue);
	// Thin enough that a white mark reads as an outline rather than a blob, with a floor so it never disappears on a low-resolution screen.
	PMReal lineWidth = size * PMReal(0.10);
	if (lineWidth < PMReal(0.75))
		lineWidth = PMReal(0.75);
	gPort->setlinewidth(lineWidth);
	const PMReal r = size * PMReal(0.40);
	const PMReal rStroke = StrokeRadius(r, lineWidth);
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
			const PMReal rs = size * PMReal(0.24);
			EllipsePath(gPort, centreX, centreY, rs, rs);
			gPort->fill();
			break;
		}
		case kShapeSmallWhiteCircle:
		{
			const PMReal rs = StrokeRadius(size * PMReal(0.24), lineWidth);
			EllipsePath(gPort, centreX, centreY, rs, rs);
			gPort->stroke();
			break;
		}
		case kShapeFisheye:
		{
			// A ring with a large solid dot inside (measured off InDesign's own, KCMKentenMark.cpp). A small dot would read as a bullseye.
			EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
			gPort->stroke();
			const PMReal inner = size * PMReal(0.22);
			EllipsePath(gPort, centreX, centreY, inner, inner);
			gPort->fill();
			break;
		}
		case kShapeBullseye:
		{
			// Two rings, nothing filled.
			EllipsePath(gPort, centreX, centreY, rStroke, rStroke);
			gPort->stroke();
			const PMReal inner = size * PMReal(0.20);
			EllipsePath(gPort, centreX, centreY, inner, inner);
			gPort->stroke();
			break;
		}
		case kShapeBlackTriangle:
			TrianglePath(gPort, centreX, centreY, r);
			gPort->fill();
			break;
		case kShapeWhiteTriangle:
			TrianglePath(gPort, centreX, centreY, rStroke);
			gPort->stroke();
			break;
		case kShapeBlackSesame:
		case kShapeWhiteSesame:
		{
			const PMReal rx = size * PMReal(0.20);
			const PMReal ry = size * PMReal(0.38);
			if (shape == kShapeBlackSesame)
			{
				SesamePath(gPort, centreX, centreY, rx, ry);
				gPort->fill();
			}
			else
			{
				SesamePath(gPort, centreX, centreY, StrokeRadius(rx, lineWidth), StrokeRadius(ry, lineWidth));
				gPort->stroke();
			}
			break;
		}
		default:
			break;
	}
	gPort->grestore();
}

}	// anonymous namespace

void KFCKentenMark::PlanOverRun(const InterfaceFontInfo& font, int32 kind, const PMReal& runLeft, const PMReal& runWidth, int32 charCount,
	const PMReal& lineHeight, const PMReal& markCentreY, const PMReal& rightEdge, const RealAGMColor& colour, Marks& out)
{
	out = Marks();
	if (kind <= 0 || charCount <= 0 || runWidth <= PMReal(0.0))
		return;
	const bool asShape = (kind >= kShapeBlackSesame && kind <= kShapeSmallWhiteCircle);
	PMString glyph;
	glyph.SetTranslatable(kFalse);
	if (!asShape)
		glyph.AppendW(KFCKentenMarkCharacter(kind));
	// The width one character got: CJK is even enough for this to land the mark over its character.
	const PMReal perChar = runWidth / PMReal(charCount);
	PMReal markSize = lineHeight * PMReal(0.70);
	if (perChar < markSize)
		markSize = perChar;		// a narrow column shrinks the marks rather than overlapping them
	out.fShape = asShape ? kind : 0;
	out.fGlyph = glyph;
	out.fGlyphW = KFCDrawnWidth(glyph, font);
	out.fCentreY = markCentreY;
	out.fSize = markSize;
	out.fRight = rightEdge;
	out.fColour = colour;
	for (int32 i = 0; i < charCount; ++i)
	{
		const PMReal cx = runLeft + perChar * (PMReal(i) + PMReal(0.5));
		if (cx + (markSize / PMReal(2.0)) > rightEdge)
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

void KFCKentenMark::WriteGlyphs(dvaui::drawbot::Drawbot* drawbotP, const InterfaceFontInfo& font, const Marks& marks, const PMReal& lineTop, const PMReal& lineBottom)
{
	if (drawbotP == nil || marks.fGlyph.IsEmpty())
		return;
	for (size_t i = 0; i < marks.fCentreX.size(); ++i)
	{
		const PMReal left = marks.fCentreX[i] - (marks.fGlyphW / PMReal(2.0));
		StringUtils::PMDrawStringRGB(drawbotP, PMRect(left, lineTop, marks.fRight, lineBottom), kAlignLeft, marks.fGlyph, font, marks.fColour,
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
