//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KBS)
//
//  The panel's language-dependent measurements. See KBSPanelMetrics.h for why.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"		// GetFrame / SetFrame - the widgets being re-placed
#include "IPanelControlData.h"	// FindWidget

// General includes:
#include "LocaleSetting.h"
#include "PMLocaleIds.h"
#include "PMRect.h"

// Project includes:
#include "KBSID.h"
#include "KBSPanelIcon.h"		// Count / NthWidgetID - the stacked illustrations move together
#include "KBSPanelMetrics.h"

namespace
{

// The message block's height in each UI. Each is a WHOLE NUMBER OF LINES in the language it
// is used for - that is the rule, and it is why they are two numbers rather than one: a
// single value cannot divide by both 12 and 18 without being 36, 72, 108...  Because each
// language reads only its own line, 72 is fine here (18x4) even though it would be six Roman
// lines - no Roman UI ever gets it.
//
// ***** FOUR LINES ON A JAPANESE UI SINCE 2026-08-10, AND THE THIRD WAS NOT A ROUNDING ERROR.
// ***** It was 54 (18x3) from 2026-08-07, sized against the OPENING message - which draws 535px
// wide on a Japanese UI and therefore takes 2.8 lines in the 217px box the floor gives it. The
// note that stood here said so, and the .fr's said the same thing with the warning attached:
// "that is one message, though, not a promise about every message - a longer one takes a fourth
// line and the fourth is clipped."
//
// It was clipped. MEASURED on the running panel (2026-08-10, PrintWindow over the status widget):
// the line a stopped replace leaves - "Replace cancelled - nothing was changed. The results have
// been cleared - the document has changed since the search. Search again." - drew three lines and
// STOPPED AT "the document has". What the user lost was the end of the sentence, which is the
// part that says what to do about it. 128 characters against a box that holds about 88.
//
// So both halves were fixed together (user's call): this block grew a line, and every refusal
// that has to be read whole was cut to fit four - about 117 characters at the floor. The
// messages are the half that matters; this is the headroom that keeps a long one from being
// silently truncated again.
//
// ***** THE ROMAN BLOCK IS LEFT AT FOUR, and that is not an oversight (user's instruction was to
// ***** raise it too "if the English version also has a problem"). It has not: the SAME box holds
// four 12px lines there, and a 12px palette font puts appreciably more characters on each of them
// than an 18px one does - so four Roman lines hold strictly more text than the four Japanese ones
// this file now budgets for. The messages were cut to fit the Japanese four, which is the tighter
// of the two. NOT measured on this machine, which runs a Japanese UI and cannot draw the Roman
// one; if a Roman UI is ever seen to clip, this is the number to raise (to 60 = 12x5) and
// kMinimumHeightRoman moves with it.
//
// The block and the floor are a pair: the width decides how many lines a message takes, and this
// decides how many there is room to draw. Narrow the floor without raising this and the last
// line is clipped.
const int32 kMessageHeightRoman = 48;	// 12px x 4 lines
const int32 kMessageHeightCJK   = 72;	// 18px x 4 lines  (= six Roman lines, which no Roman UI
										// ever gets - see the note above)

// The gap between the message block and the tree, as the .fr has always had it.
const int32 kGapUnderMessageBlock = 3;

// ***** THE FLOOR. ***** Width, measured on the running panel rather than reasoned about:
// the opening message DRAWS 535px wide on a Japanese UI (its two lines came out 266px and
// 265px in a 301px box, 2026-08-06), and the box is the panel less 49px of margins and
// illustration (5 + box + 4 + 32 + 8). So
//
//     box 301 (panel 350) -> 2 lines      <- pointlessly wide
//     box 251 (panel 300) -> 3 lines      <- what the panel opens at
//     box 217 (panel 266) -> 3 lines      <- ***** the floor *****, and MEASURED there: the
//                                            opening message drew three 18px lines with nothing
//                                            clipped (2026-08-07, Japanese UI). ***** THAT IS THE
//                                            OPENING MESSAGE AND ONLY IT ***** - a stopped
//                                            replace's line needs five at this width, which is
//                                            what the block being 3 lines tall cost (2026-08-10)
//     box 216 (panel 265) -> 4 lines      <- where the clipping was reported back when the
//                                            Japanese block was 48px (2 2/3 lines)
//
// ***** THE FLOOR IS THE WIDTH THE PANEL IS ACTUALLY WORKED AT. ***** The whole history is
// 250 -> 224 -> 242 -> 266, and only the last two came from that instruction:
//
//     250  2026-08-04  the first floor, in KBSPanelView itself, measured BY EYE (b307ee1)
//     224  2026-08-06  KESCM's fixed width, to line the two panels up when docked (ee0f870)
//     242  2026-08-07  "make the minimum width the size it is now" - off the running panel
//     266  2026-08-07  the same instruction again (aaf1ba2). This is the number here.
//
// 224 was KESCM's fixed width (KESCM.fr:1105), put here so the two would line up when docked
// together. That is GIVEN UP, deliberately: a floor is there to stop the panel being dragged down
// to where it cannot be read, and the width it is read at is this one. Lining up with a sibling
// was a second job asked of the same number, and the two wanted different answers.
//
// ! The floor and the block are measured AT THE SAME WIDTH, and the block is now sized against the
//   LONGEST message rather than the opening one: four 18px lines in a 72px block at box 217, which
//   is about 117 characters. Every refusal that has to be read whole is kept under that (see
//   KBSReplaceEngine::RefuseChangedQuery and the search's own refusals). Sizing it against the
//   opening message is exactly what let a 128-character line be cut off in shipping code.
//   The floor is how small the panel MAY be made.
//
// Same floor in both languages: a Roman UI draws the same message in 12px lines, so it simply
// has room to spare rather than a layout of its own.
//
// ! It is the WIDTH that decides the line count, and the block above holds four lines in either
//   language (see kMessageHeightRoman / kMessageHeightCJK - 48px of them Roman, 72px Japanese).
//   The two numbers are a pair with this one - neither is meaningful without the other.
//
// Height: stated against the Roman block so that a taller block simply moves it. The floor is
// there to keep about five 19px result rows visible, which has nothing to do with language.
const int32 kMinimumWidth       = 266;	// the width the panel is worked at (measured 2026-08-07)
const int32 kMinimumHeightRoman = 160;

/** Does this UI language draw the palette font tall? Measured for Japanese (18px vs 12px).
    The other three are the same CJK fonts and are included on that reasoning - they are NOT
    measured, and this machine cannot measure them. If one of them ever turns out to differ,
    this is the one place that decides. */
bool IsTallLineUI()
{
	const int16 ui = LocaleSetting::GetLocale().GetUserInterfaceId();
	return ui == k_jaJP || ui == k_koKR || ui == k_zhCN || ui == k_zhTW;
}

}

/* MessageBlockHeight
*/
int32 KBSPanelMetrics::MessageBlockHeight()
{
	return IsTallLineUI() ? kMessageHeightCJK : kMessageHeightRoman;
}

/* MinimumPanelWidth
*/
int32 KBSPanelMetrics::MinimumPanelWidth()
{
	return kMinimumWidth;
}

/* MinimumPanelHeight
*/
int32 KBSPanelMetrics::MinimumPanelHeight()
{
	return kMinimumHeightRoman + (MessageBlockHeight() - kMessageHeightRoman);
}

/* Update
*/
void KBSPanelMetrics::Update(IPanelControlData* panelData)
{
	if (panelData == nil)
		return;

	IControlView* message = panelData->FindWidget(kKBSStaticTextWidgetID);
	if (message == nil)
		return;

	// Everything below is placed against this one number, so nothing can drift out of step
	// with anything else however many times this runs.
	const PMRect messageFrame = message->GetFrame();
	const PMReal blockBottom = messageFrame.Top() + MessageBlockHeight();

	// The tree first: it is the widget that has to get out of the way when the block grows.
	// Its binding is left alone - the frames are being set directly, and a binding only says
	// how a widget follows its PARENT being resized, which is not happening here.
	IControlView* tree = panelData->FindWidget(kKBSResultListWidgetID);
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
	// and which one that is belongs to KBSPanelIcon, not here.
	// NOT wrapped in HideView/ShowView, unlike spellpanel's status text (SpellSkipObserver.cpp
	// :376-378): showing them would override the one KBSPanelIcon chose.
	for (int32 i = 0; i < KBSPanelIcon::Count(); ++i)
	{
		IControlView* icon = panelData->FindWidget(KBSPanelIcon::NthWidgetID(i));
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

// End, KBSPanelMetrics.cpp.
