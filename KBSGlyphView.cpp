//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSGlyphView.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IFontInstance.h"		// GetGlyphBBox - how much room the glyph actually takes
#include "IFontMgr.h"			// QueryFontInstance
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "IPMFont.h"

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "DVControlView.h"
#include "ISession.h"			// GetExecutionContextSession
#include "Utils.h"

// Project includes:
#include "KBSID.h"
#include "KBSGlyphConfirmDialog.h"
#include "KBSGlyphView.h"

// Linear blend of two RGB colours (t = 0 -> a, t = 1 -> b). The frame is drawn part-way between
// the panel fill and the text colour, so it reads as a container rather than as content, and it
// follows the theme in both directions the way everything else KBS draws does.
static RealAGMColor KBSBlendColor(const RealAGMColor& a, const RealAGMColor& b, const PMReal& t)
{
	const PMReal u = PMReal(1.0) - t;
	return RealAGMColor(
		ToDouble(a.red   * u + b.red   * t),
		ToDouble(a.green * u + b.green * t),
		ToDouble(a.blue  * u + b.blue  * t));
}

/** Implements IControlView: draws one glyph of the Glyph tab's replace confirmation. */
class KBSGlyphView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KBSGlyphView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KBSGlyphView() {}

	/** Draw this frame and, when there is one, the glyph inside it.
		@param viewPort the port to draw through.
		@param updateRgn the region being repainted.
	*/
	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KBSGlyphView, kKBSGlyphViewImpl)

void KBSGlyphView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	// Colours entirely from the current theme, so this matches whatever the rest of the UI is
	// using. Sane fallbacks if the query fails - a frame in mid grey is still a frame.
	RealAGMColor fill(1.0, 1.0, 1.0);
	RealAGMColor text(0.0, 0.0, 0.0);
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(kInterfacePaletteFill, fill);
		colors->GetRealAGMColor(kInterfaceTextColor, text);
	}
	const RealAGMColor frameColor = KBSBlendColor(fill, text, PMReal(0.5));

	// Half a line width in from the edge, so the whole stroke lands inside the widget instead of
	// half of it falling outside and being clipped away.
	const PMReal kLineWidth(1.0);
	PMRect frame = this->GetInnerContentFrame();
	frame.Inset(kLineWidth / 2, kLineWidth / 2);

	gPort->setlinewidth(kLineWidth);
	gPort->setrgbcolor(PMReal(frameColor.red), PMReal(frameColor.green), PMReal(frameColor.blue));
	gPort->rectstroke(frame);
	gPort->newpath();

	// An absent side is a valid state and stays an empty frame: it is what an empty Change To box -
	// "delete every match" - looks like.
	const KBSGlyphConfirmDialog::Side* side =
		KBSGlyphConfirmDialog::GetSideForWidget(this->GetWidgetID());
	if (side == nil || side->fFont == nil)
		return;

	// Size the glyph to the frame rather than picking a point size and hoping. A ONE-POINT font
	// instance measures the glyph in em units, so the ratio between the frame and that box is the
	// point size that just fits. The matrix IS the size - PMMatrix(pt, 0, 0, pt, 0, 0) - which is
	// how buttonui's label drawer asks for an instance (FormFieldLabelDrawer.cpp:365-366).
	InterfacePtr<IFontMgr> fontMgr(GetExecutionContextSession(), IID_IFONTMGR);
	if (fontMgr == nil)
		return;
	const PMMatrix onePoint(1.0, 0.0, 0.0, 1.0, 0.0, 0.0);
	InterfacePtr<IFontInstance> emInstance(fontMgr->QueryFontInstance(side->fFont, onePoint));
	if (emInstance == nil)
		return;

	const PMRect emBox = emInstance->GetGlyphBBox(side->fGlyphID);
	const PMReal emW = emBox.Width();
	const PMReal emH = emBox.Height();
	if (emW <= PMReal(0.0) || emH <= PMReal(0.0))
		return;					// an empty glyph, or a font that will not answer for it

	const PMReal kGlyphPadding(6.0);
	const PMReal availW = frame.Width() - kGlyphPadding * 2;
	const PMReal availH = frame.Height() - kGlyphPadding * 2;
	if (availW <= PMReal(0.0) || availH <= PMReal(0.0))
		return;					// no room - the frame itself is the whole picture

	PMReal ptSize = availW / emW;
	if (availH / emH < ptSize)
		ptSize = availH / emH;

	// Centre the glyph's own bounding box in the frame. The box is measured FROM THE PEN, so the
	// pen goes at (centre of the frame) minus (where the box sits relative to the pen), scaled.
	const PMReal drawnW = emW * ptSize;
	const PMReal drawnH = emH * ptSize;
	const PMReal penX = frame.Left() + (frame.Width() - drawnW) / 2 - emBox.Left() * ptSize;
	const PMReal penY = frame.Top() + (frame.Height() - drawnH) / 2 - emBox.Top() * ptSize;

	gPort->setrgbcolor(PMReal(text.red), PMReal(text.green), PMReal(text.blue));
	gPort->selectfont(side->fFont, ptSize);

	// xshow takes GLYPH IDS, not characters - which is the entire reason this widget exists. Drawn
	// as a character, an alternate form would come out as the standard form its Unicode maps back
	// to; drawn by id, it comes out as itself. One glyph needs no advance width, so the widths
	// array is a single zero.
	//
	// No worked example of xshow exists in the SDK, so the geometry above is confirmed by running
	// it rather than by reading: if the glyph lands somewhere unexpected, the sign convention of
	// GetGlyphBBox is the first thing to question.
	const int32 glyphs[1] = { side->fGlyphID };
	const float widths[1] = { 0.0f };
	gPort->xshow(penX, penY, 1, glyphs, widths);
	gPort->newpath();
}

// End, KBSGlyphView.cpp.
