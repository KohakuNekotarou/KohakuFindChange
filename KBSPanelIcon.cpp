//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The panel's illustration. See KBSPanelIcon.h for the contract.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IPanelControlData.h"
#include "IPalettePanelUtils.h"		// QueryPanelByWidgetID - the same reach Rebuild uses

// General includes:
#include "Utils.h"

// Project includes:
#include "KBSID.h"
#include "KBSPanelIcon.h"
#include "KBSResultModel.h"		// HasRun - the one input to the choice

namespace
{

/** The pictures, in the order Choose() tests them. A new one is a row here plus its id and PNG
    resource; nothing else changes.

    Order matters only in that Choose() returns the FIRST match, so the more specific state has to
    come before the more general one - which is what a "replaced" picture will need when it arrives
    (a replace has also been "run"). */
const WidgetID kIcons[] =
{
	kKBSIconWidgetID,		// nothing has been run yet
	kKBSIconFoundWidgetID	// something has
};
const int32 kIconCount = static_cast<int32>(sizeof(kIcons) / sizeof(kIcons[0]));

/** Which picture belongs on screen right now. */
WidgetID Choose()
{
	// One question, asked of the model rather than of the status line: the close responders put a
	// message on that line ("Results cleared - the document was closed.") while throwing the
	// results away, so an empty-or-not test on the text would leave the panel showing the wrong
	// picture after a close. HasRun is cleared by Clear(), which is exactly what those responders
	// call.
	return KBSResultModel::HasRun() ? kKBSIconFoundWidgetID : kKBSIconWidgetID;
}

}	// anonymous namespace

void KBSPanelIcon::Update()
{
	// nil when the panel is closed, which is an ordinary state - there is nothing to show then.
	InterfacePtr<IPanelControlData> panelData(
		Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKBSPanelWidgetID));
	if (panelData == nil)
		return;

	const WidgetID wanted = Choose();

	for (int32 i = 0; i < kIconCount; ++i)
	{
		IControlView* view = panelData->FindWidget(kIcons[i]);
		if (view == nil)
			continue;

		const bool16 on = (kIcons[i] == wanted) ? kTrue : kFalse;
		view->ShowView(on);

		// ***** Enable as well as show. ***** A hidden widget still takes clicks - ShowView stops
		// the drawing, not the hit test - so with the pictures stacked at one frame a single click
		// would reach every one of them and open a browser tab for each. Measured in KESCM
		// (2026-07-03) and hit again when the same panel shape was carried into KESCL (2026-07-15).
		if (on)
			view->Enable();
		else
			view->Disable();
	}
}

bool KBSPanelIcon::IsIconWidget(const WidgetID& widgetID)
{
	for (int32 i = 0; i < kIconCount; ++i)
	{
		if (kIcons[i] == widgetID)
			return true;
	}
	return false;
}

int32 KBSPanelIcon::Count()
{
	return kIconCount;
}

WidgetID KBSPanelIcon::NthWidgetID(int32 n)
{
	if (n < 0 || n >= kIconCount)
		return kInvalidWidgetID;
	return kIcons[n];
}

// End, KBSPanelIcon.cpp.
