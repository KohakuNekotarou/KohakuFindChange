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
// FOUR lines (user's call, 2026-08-06), which is the same budget the Roman block has always
// had. It was briefly three - the panel was going to be kept wide enough that three sufficed -
// but the floor below ended up at 260, where the opening message needs four. The block and the
// floor are a pair: the width decides how many lines a message takes, and this decides how many
// there is room to draw. Narrow the floor without widening this and the last line is clipped.
const int32 kMessageHeightRoman = 48;	// 12px x 4 lines
const int32 kMessageHeightCJK   = 72;	// 18px x 4 lines  (= 12px x 6 on a Roman UI)

// The gap between the message block and the tree, as the .fr has always had it.
const int32 kGapUnderMessageBlock = 3;

// ***** THE FLOOR. ***** Width, measured on the running panel rather than reasoned about:
// the opening message DRAWS 535px wide on a Japanese UI (its two lines came out 266px and
// 265px in a 301px box, 2026-08-06), and the box is the panel less 49px of margins and
// illustration (5 + box + 4 + 32 + 8). So
//
//     box 301 (panel 350) -> 2 lines      <- pointlessly wide
//     box 251 (panel 300) -> 3 lines      <- what the panel opens at
//     box 216 (panel 265) -> 4 lines      <- where the clipping was reported when the block
//                                            still held three; this is what the block is sized for
//     box 193 (panel 242) -> the floor    <- user's call, 2026-08-07
//
// ***** THE FLOOR IS THE WIDTH THE PANEL IS ACTUALLY WORKED AT. ***** Raised 224 -> 242 on
// 2026-08-07 ("make the minimum width about the size it is now"), measured off the running panel.
//
// 224 was KESCM's fixed width (KESCM.fr:1105), put here so the two would line up when docked
// together. That is GIVEN UP, deliberately: a floor is there to stop the panel being dragged down
// to where it cannot be read, and the width it is read at is this one. Lining up with a sibling
// was a second job asked of the same number, and the two wanted different answers.
//
// ! 535 / 193 = 2.8, so the opening message needs three lines at the floor and the block holds
//   four - but that is arithmetic, NOT a measurement: the widths above were measured and this
//   one was not. If a message ever takes five lines down here, the fifth is clipped.
//   The floor is how small the panel MAY be made; it is not a promise about every message.
//
// Same floor in both languages: a Roman UI draws the same message in 12px lines, so it simply
// has room to spare rather than a layout of its own.
//
// ! It is the WIDTH that decides the line count, and the block above holds four lines in either
//   language (see kMessageHeightRoman / kMessageHeightCJK). The two numbers are a pair - neither
//   is meaningful without the other. (This read "only holds three lines" until 2026-08-07, left
//   over from before the block was sized to four.)
//
// Height: stated against the Roman block so that a taller block simply moves it. The floor is
// there to keep about five 19px result rows visible, which has nothing to do with language.
const int32 kMinimumWidth       = 242;	// the width the panel is worked at (measured 2026-08-07)
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
