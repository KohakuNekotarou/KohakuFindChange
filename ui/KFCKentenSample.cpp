//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KENTEN PART'S SAMPLE (the author, 11-02: "what kind of mark, a sample beside it"): beside Kenten: and Change Kenten To:, the mark the list says, the shape a kenten row draws over its text
//  (KFCKentenMark.h) - no font's glyph, so the two read alike whatever the UI font holds. What it draws is the face's to say (KFCKohakuTab::KentenSampleKind): nothing for [Any Kenten], [Leave As It Is],
//  a list that does not count; the asterisk for Custom, whose glyph is the person's. The face invalidates it when that changes.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IInterfaceColors.h"
#include "IInterfaceFonts.h"

// General includes:
#include "AGMGraphicsContext.h"

// Project includes:
#include "KFCKentenMark.h"
#include "KFCKohakuTab.h"
#include "KFCUIID.h"

class KFCKentenSampleView : public KFCMarkPassView
{
	typedef KFCMarkPassView inherited;
public:
	KFCKentenSampleView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KFCKentenSampleView() {}

	/** DV_Draw, as a row's cell draws its marks: its AGM pass first, and a custom mark's character by drover. Nothing is painted behind it: the face's own fill stands. */
	virtual void DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const;
};

CREATE_PERSIST_PMINTERFACE(KFCKentenSampleView, kKFCKentenSampleViewImpl)

void KFCKentenSampleView::DV_Draw(dvaui::drawbot::Drawbot* drawbotP) const
{
	if (drawbotP == nil)
		return;
	const int32 kind = KFCKohakuTab::KentenSampleKind(this->GetWidgetID());
	if (kind == 0)
		return;
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return;
	const InterfaceFontInfo& fontInfo = fonts->GetFont(kPaletteWindowSystemScriptFontId);
	RealAGMColor colour(0.0, 0.0, 0.0);
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
		colors->GetRealAGMColor(kInterfaceTextColor, colour);
	// One mark, as tall as a row's mark gets in a line of this height, in the square at the view's left.
	const PMRect frame = this->GetInnerContentFrame();
	const PMReal side = frame.Height();
	KFCKentenMark::Marks marks;
	KFCKentenMark::PlanOverRun(fontInfo, kind, frame.Left(), side, 1, side, frame.Top() + (side / PMReal(2.0)), frame.Right(), colour, marks);
	this->PaintMarks(drawbotP, marks);
	KFCKentenMark::WriteGlyphs(drawbotP, fontInfo, marks, frame.Top(), frame.Bottom());
}

// End, KFCKentenSample.cpp.
