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
#include "IApplication.h"		// QueryPanelManager
#include "IControlView.h"		// GetFrame / SetFrame - the widgets being re-placed
#include "IPanelControlData.h"	// FindWidget
#include "IPanelMgr.h"			// GetPanelFromWidgetID
#include "ISession.h"

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
// language reads only its own line, 54 is fine here (18x3) even though it would be four and
// a half Roman lines - no Roman UI ever gets it.
//
// The two languages get DIFFERENT line budgets, and that is the point of having two numbers:
// four Roman lines and THREE Japanese ones (user's call, 2026-08-07) come to nearly the same
// block - 48px against 54px - so the panel looks the same in both while neither clips.
//
// Why three is enough at the floor: the opening message DRAWS 535px wide on a Japanese UI and
// the box at the floor is 193px (see below), so it takes 2.8 lines. Four was carried from
// 2026-08-06, when the floor was 260 and the arithmetic was measured against a different width;
// the third line has been the last one with ink on it ever since the floor moved to 242.
//
// The block and the floor are a pair: the width decides how many lines a message takes, and this
// decides how many there is room to draw. Narrow the floor without raising this and the last
// line is clipped.
const int32 kMessageHeightRoman = 48;	// 12px x 4 lines
const int32 kMessageHeightCJK   = 54;	// 18px x 3 lines  (= four and a half Roman lines, which
										// no Roman UI ever gets - see the note above)

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
//                                            opening message drew three 18px lines in the 54px
//                                            block with nothing clipped (2026-08-07, Japanese UI)
//     box 216 (panel 265) -> 4 lines      <- where the clipping was reported back when the
//                                            Japanese block was 48px (2 2/3 lines)
//
// ***** THE FLOOR IS THE WIDTH THE PANEL IS ACTUALLY WORKED AT. ***** 224 -> 242 -> 266, each
// time on the same instruction ("make the minimum width the size it is now") and each time
// measured off the running panel rather than reasoned about. 266 is 2026-08-07.
//
// 224 was KESCM's fixed width (KESCM.fr:1105), put here so the two would line up when docked
// together. That is GIVEN UP, deliberately: a floor is there to stop the panel being dragged down
// to where it cannot be read, and the width it is read at is this one. Lining up with a sibling
// was a second job asked of the same number, and the two wanted different answers.
//
// ! The floor and the block are now measured AT THE SAME WIDTH: the opening message takes three
//   18px lines in a 54px block at box 217, with nothing clipped. That is one message, though, not
//   a promise about every message - a longer one takes a fourth line and the fourth is clipped.
//   The floor is how small the panel MAY be made.
//
// Same floor in both languages: a Roman UI draws the same message in 12px lines, so it simply
// has room to spare rather than a layout of its own.
//
// ! It is the WIDTH that decides the line count, and the block above holds four Roman lines or
//   three Japanese ones (see kMessageHeightRoman / kMessageHeightCJK). The two numbers are a pair
//   with this one - neither is meaningful without the other.
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

/** The panel itself, or nil when it has never been opened. Written the way KBSPanelTitle's
    SetTabLabel reaches the same panel - the interfaces are acquired and released inside one
    scope, and only the non-owning IControlView* travels. */
IControlView* GetPanelView()
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return nil;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return nil;

	// Non-owning - a Get, not a Query. nil until the panel has been opened once, which is the
	// ordinary state at startup, so every caller may fire blindly.
	return panelMgr->GetPanelFromWidgetID(kKBSPanelWidgetID);
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
void KBSPanelMetrics::Update()
{
	IControlView* panelView = GetPanelView();
	if (panelView == nil)
		return;

	InterfacePtr<IPanelControlData> panelData(panelView, UseDefaultIID());
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
