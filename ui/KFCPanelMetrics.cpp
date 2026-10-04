//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KFC)
//
//  The panel's language-dependent measurements. See KFCPanelMetrics.h for why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"		// GetFrame / SetFrame - the widgets being re-placed
#include "IInterfaceFonts.h"	// the palette window's fonts - the one the message block is drawn in
#include "IPanelControlData.h"	// FindWidget

// General includes:
#include "DVPublicUtilities.h"	// dv_utils::FontInfoGetDVAFontMetrics - the font's own line
#include "ISession.h"			// GetExecutionContextSession
#include "PMRect.h"
#include "ShuksanID.h"			// kPaletteWindowSystemScriptFontId

// Project includes:
#include "KFCUIID.h"
#include "KFCPanelIcon.h"		// Count / NthWidgetID - the stacked illustrations move together
#include "KFCPanelMetrics.h"

namespace
{

// How many lines the message block holds: FOUR, in every UI language. The block is a WHOLE NUMBER
// of the drawing's lines - a remainder is room for a part-line, drawn as a sliver of chopped-off
// letters - and the line is asked of the font itself (MessageLineMetrics).
//
// FOUR, NOT THREE - AND THE THIRD WAS NOT A ROUNDING ERROR. Three lines (54 = 18x3 on a Japanese UI)
// were sized against the OPENING message, which draws 535px wide on a Japanese UI and so takes 2.8
// lines in the 217px box the floor gives it. A longer message takes a fourth line, and the fourth was
// clipped: MEASURED on the running panel (PrintWindow over the status widget), the line a stopped
// replace leaves - "Replace cancelled - nothing was changed. The results have been cleared - the
// document has changed since the search. Search again." - drew three lines and STOPPED AT "the
// document has". What the user lost was the end of the sentence, which is the part that says what to
// do about it. 128 characters against a box that holds about 88.
//
// So both halves were fixed together (the author's call): this block holds four lines, and every
// refusal that has to be read whole is cut to fit four - about 117 characters at the floor. The
// messages are the half that matters; this is the headroom that keeps a long one from being
// silently truncated again.
//
// FOUR ON A ROMAN UI TOO, and that is not an oversight (the author's instruction was to raise it "if
// the English version also has a problem"). A smaller palette font puts appreciably more characters
// on each line than the 18px one does, so four Roman lines hold more text than the four Japanese ones
// the messages are cut to fit. NOT measured: the development machine runs a Japanese UI and cannot
// draw the Roman one; if a Roman UI is ever seen to clip, this is the number to raise, and the floor
// moves with it (MinimumPanelHeight).
//
// The block and the floor are a pair: the width decides how many lines a message takes, and this
// decides how many there is room to draw. Narrow the floor without raising this and the last
// line is clipped.
const int32 kMessageLines = 4;

// The .fr's own block (KFCUI.fr: Frame 6..54, four 12px lines). What stands when the font cannot be
// asked, and what the floor's height is stated against (kMinimumHeightAtResourceBlock).
const int32 kMessageHeightResource = 48;

// The gap between the message block and the tree, as the .fr has always had it.
const int32 kGapUnderMessageBlock = 3;

// THE FLOOR. Width, measured on the running panel rather than reasoned about: the opening message
// DRAWS 535px wide on a Japanese UI (its two lines came out 266px and 265px in a 301px box), and the
// box is the panel less 49px of margins and illustration (5 + box + 4 + 32 + 8). So
//
//     box 301 (panel 350) -> 2 lines      <- pointlessly wide
//     box 251 (panel 300) -> 3 lines      <- what the panel opens at
//     box 217 (panel 266) -> 3 lines      <- the floor, and MEASURED there: the opening message
//                                            drew three 18px lines with nothing clipped (Japanese
//                                            UI). THAT IS THE OPENING MESSAGE AND ONLY IT - a
//                                            stopped replace's 128-character line needs five at
//                                            this width, which is why the block holds four and
//                                            the messages are cut to fit them
//     box 216 (panel 265) -> 4 lines      <- where the clipping was reported back when the
//                                            Japanese block was 48px (2 2/3 lines)
//
// THE FLOOR IS THE WIDTH THE PANEL IS ACTUALLY WORKED AT (the author's instruction, given twice:
// "make the minimum width the size it is now" - read off the running panel). Lining up with KCM's
// panel when the two are docked together (224, KCM's width then) is GIVEN UP, deliberately: a floor
// is there to stop the panel being dragged down to where it cannot be read, and the width it is read
// at is this one. Lining up with a sibling was a second job asked of the same number, and the two
// wanted different answers.
//
// ! The floor and the block are measured AT THE SAME WIDTH, and the block is sized against the
//   LONGEST message rather than the opening one: four 18px lines in a 72px block at box 217, which
//   is about 117 characters. Every refusal that has to be read whole is kept under that (see
//   KFCReplaceEngine::RefuseChangedQuery and the search's own refusals). Sizing it against the
//   opening message is exactly what let a 128-character line be cut off in shipping code.
//   The floor is how small the panel MAY be made.
//
// Same floor in every language: a smaller palette font draws the same message in shorter lines, so
// it simply has room to spare rather than a layout of its own.
//
// ! It is the WIDTH that decides the line count, and the block above holds kMessageLines of them in
//   any language. The two numbers are a pair - neither is meaningful without the other.
//
// Height: stated against the .fr's own block (kMessageHeightResource) so that a taller block simply
// moves it. The floor is there to keep about five 19px result rows visible, which has nothing to do
// with language.
const int32 kMinimumWidth                 = 266;	// the width the panel is worked at (measured)
const int32 kMinimumHeightAtResourceBlock = 160;

}

/* MessageFont
*/
const InterfaceFontInfo* KFCPanelMetrics::MessageFont()
{
	InterfacePtr<IInterfaceFonts> fonts(GetExecutionContextSession(), UseDefaultIID());
	if (fonts == nil)
		return nil;
	return &fonts->GetFont(kPaletteWindowSystemScriptFontId);
}

/* MessageLineMetrics
*/
bool KFCPanelMetrics::MessageLineMetrics(PMReal& outLineAdvance, PMReal& outAscent)
{
	const InterfaceFontInfo* const font = MessageFont();
	if (font == nil)
		return false;
	// The product's way of asking a widget's font for its line
	// (dynamicdocumentsui/TimingPanelTreeDDTarget.cpp:582-585). ! The SIZE is no answer: it stays 12.0 on
	// a Japanese UI, where the line is 18 (measured in KCM) - the line is ascent + descent + leading.
	float size = 0.0f, ascent = 0.0f, descent = 0.0f, leading = 0.0f;
	if (!dv_utils::FontInfoGetDVAFontMetrics(*font, &size, &ascent, &descent, &leading))
		return false;
	const PMReal advance(ascent + descent + leading);
	if (advance <= PMReal(0.0))
		return false;
	outLineAdvance = advance;
	outAscent = PMReal(ascent);
	return true;
}

/* MessageBlockHeight
*/
int32 KFCPanelMetrics::MessageBlockHeight()
{
	PMReal lineAdvance(0.0), ascent(0.0);
	if (!MessageLineMetrics(lineAdvance, ascent))
		return kMessageHeightResource;	// the .fr's block - the drawing then measures a string instead
	// Up to a whole pixel: the widgets sit on whole pixels, and less than a pixel over the lines is no
	// room for a part-line.
	return ::ToInt32(::Ceiling(lineAdvance * PMReal(kMessageLines)));
}

/* MinimumPanelWidth
*/
int32 KFCPanelMetrics::MinimumPanelWidth()
{
	return kMinimumWidth;
}

/* MinimumPanelHeight
*/
int32 KFCPanelMetrics::MinimumPanelHeight()
{
	return kMinimumHeightAtResourceBlock + (MessageBlockHeight() - kMessageHeightResource);
}

/* Update
*/
void KFCPanelMetrics::Update(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	IControlView* message = panelData->FindWidget(kKFCStaticTextWidgetID);
	if (message == nil)
		return;

	// Everything below is placed against this one number, so nothing can drift out of step
	// with anything else however many times this runs.
	const PMRect messageFrame = message->GetFrame();
	const PMReal blockBottom = messageFrame.Top() + MessageBlockHeight();

	// The tree first: it is the widget that has to get out of the way when the block grows.
	// Its binding is left alone - the frames are being set directly, and a binding only says
	// how a widget follows its PARENT being resized, which is not happening here.
	IControlView* tree = panelData->FindWidget(kKFCResultListWidgetID);
	if (tree != nil)
	{
		PMRect treeFrame = tree->GetFrame();
		const PMReal treeTop = blockBottom + kGapUnderMessageBlock;
		if (treeFrame.Top() != treeTop)
		{
			treeFrame.Top(treeTop);
			tree->SetFrame(treeFrame);
		}
	}

	// The illustrations keep their size and sit ON the block's bottom edge - a picture hanging
	// off the top of a part-empty message box reads as detached from it (the reasoning the .fr
	// records for the frame it gives them). All of them move: exactly one is visible at a time
	// and which one that is belongs to KFCPanelIcon, not here.
	// NOT wrapped in HideView/ShowView, unlike spellpanel's status text (SpellSkipObserver.cpp
	// :376-378): showing them would override the one KFCPanelIcon chose.
	for (int32 i = 0; i < KFCPanelIcon::Count(); ++i)
	{
		IControlView* icon = panelData->FindWidget(KFCPanelIcon::NthWidgetID(i));
		if (icon == nil)
			continue;

		PMRect iconFrame = icon->GetFrame();
		if (iconFrame.Bottom() == blockBottom)
			continue;

		const PMReal iconHeight = iconFrame.Height();
		iconFrame.Bottom(blockBottom);
		iconFrame.Top(blockBottom - iconHeight);
		icon->SetFrame(iconFrame);
	}

	// The block itself last, so it grows into room that has already been cleared.
	if (messageFrame.Bottom() != blockBottom)
	{
		PMRect newFrame = messageFrame;
		newFrame.Bottom(blockBottom);
		message->SetFrame(newFrame);
	}
}

// End, KFCPanelMetrics.cpp.
