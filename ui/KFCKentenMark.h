//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE EMPHASIS MARKS ON THE UPPER LINE OF A KENTEN LIST'S ROW (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md
//  F10 - the author: "as KCM's"). The shapes and the plan are KCM's (KCMKentenMark.h says why they are drawn and not written), one
//  mark per character, its size held to the character's width; here a kind is IKentenStyle::KentenKind's number, as a row holds it
//  (KFCRowUpper::mark). Custom is whatever glyph the person chose, which a row does not read: its asterisk is written
//  (KFCKentenMarkCharacter), as the list's name draws it.
//  THE SHAPES ARE PAINTED BY AGM, THE TEXT AROUND THEM BY DROVER: the drawbot's public tools fill a rectangle and stroke a polyline
//  and nothing more. A view plans its marks, has them painted by an AGM pass FIRST (KFCMarkPassView) and then draws its text.
//
//========================================================================================

#ifndef __KFCKentenMark_h__
#define __KFCKentenMark_h__

#include "PMReal.h"
#include "PMString.h"

#include "DVAPublicIncludes.h"		// dvaui::drawbot - a custom mark is WRITTEN, by drover
#include "DVControlView.h"			// KFCMarkPassView's base
#include "IInterfaceColors.h"		// RealAGMColor - a plan carries its colour
#include "IInterfaceFonts.h"		// InterfaceFontInfo - the palette font a caller already holds

#include <vector>

class IGraphicsPort;

namespace KFCKentenMark
{
	/** The marks of one run, worked out once and drawn by two hands: the shapes painted by AGM (PaintShapes, from KFCMarkPassView's
	    pass), a custom mark written by drover (WriteGlyphs). */
	struct Marks
	{
		int32				fShape;		// what every mark is painted as (KFCKentenMark.cpp's Shape); 0 = nothing to paint
		PMString			fGlyph;		// a custom mark's character - written, not painted; empty otherwise
		PMReal				fGlyphW;	// how wide that character comes out
		std::vector<PMReal>	fCentreX;	// one per mark that fits, left to right
		PMReal				fCentreY;
		PMReal				fSize;
		PMReal				fRight;		// the right edge the plan was held inside
		RealAGMColor		fColour;

		Marks() : fShape(0), fGlyphW(0.0), fCentreY(0.0), fSize(0.0), fRight(0.0), fColour(0.0, 0.0, 0.0) {}

		/** Is there a shape to paint - the only case a view needs its AGM pass for? */
		bool HasShapes() const { return fShape != 0 && !fCentreX.empty(); }
	};

	/** ONE MARK PER CHARACTER DRAWN, centred over each: runLeft and runWidth are where the characters landed, charCount how many of
	    them are under that width (the drawn ones: a line cut to fit has fewer). A kind it does not know plans nothing. */
	void PlanOverRun(const InterfaceFontInfo& font, int32 kind, const PMReal& runLeft, const PMReal& runWidth, int32 charCount,
		const PMReal& lineHeight, const PMReal& markCentreY, const PMReal& rightEdge, const RealAGMColor& colour, Marks& out);

	/** A plan's shapes on an AGM port - KFCMarkPassView's pass is the one caller. */
	void PaintShapes(IGraphicsPort* gPort, const Marks& marks);

	/** A custom mark's character over each base character, centred top to bottom in the upper line's box. A plan without a glyph writes nothing. */
	void WriteGlyphs(dvaui::drawbot::Drawbot* drawbotP, const InterfaceFontInfo& font, const Marks& marks, const PMReal& lineTop, const PMReal& lineBottom);
}

/** A cell that draws its text by drover and has its kenten shapes painted by AGM first (KCMMarkPassView's shape). DVControlView's own DV_Draw sets up an
    offscreen AGM port, calls Draw on it and blits it back over the view; PaintMarks runs exactly that, with Draw answering by painting the plan. A caller
    runs it BEFORE it draws any text: whatever was drawn before could be painted over. Draw outside the pass is the base's. */
class KFCMarkPassView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KFCMarkPassView(IPMUnknown* boss) : inherited(boss), fInMarkPass(false) {}
	virtual ~KFCMarkPassView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);

protected:
	/** Paint the plan's shapes now (nothing when it has none). From DV_Draw, before any text. */
	void PaintMarks(dvaui::drawbot::Drawbot* drawbotP, const KFCKentenMark::Marks& marks) const;

private:
	mutable KFCKentenMark::Marks	fMarks;			// the plan Draw paints - set by PaintMarks for its pass only
	mutable bool					fInMarkPass;
};

#endif // __KFCKentenMark_h__

// End, KFCKentenMark.h.
