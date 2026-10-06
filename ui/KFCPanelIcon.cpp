//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The panel's illustration. See KFCPanelIcon.h for the contract.
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
#include "KFCUIID.h"
#include "KFCPanelIcon.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces

namespace
{

/** The pictures. A new one is a row here plus its ids, its widget and its PNG lines (KFCPanelIcon.h
    lists them) and its test in Choose().

    Choose() asks the more specific state first (a replace has also been "run"); the order here is
    the order the pictures were added, not the order Choose() tests them. */
const WidgetID kIcons[] =
{
	kKFCIconWidgetID,		// nothing has been run yet
	kKFCIconFoundWidgetID,	// something has
	kKFCIconChangedWidgetID	// ...and it was a Change All that changed something
};
const int32 kIconCount = static_cast<int32>(sizeof(kIcons) / sizeof(kIcons[0]));

/** Which picture belongs on screen right now. */
WidgetID Choose()
{
	// THE PENCIL CAT (the author's call, 2026-10-06: "Change All only") - after a Change All in Book (No List) or a
	// query run that changed something (KFCResultModel::HasChangeAllWritten). A row's Replace leaves the list a work
	// list and the picture as it was. Asked FIRST: such a run has also been RUN, so HasRun ahead of it would answer
	// the searching cat every time. (Until 2026-10-06 the pencil cat was Change Checked's report's.)
	if (KFCResults()->HasChangeAllWritten())
		return kKFCIconChangedWidgetID;

	// One question, asked of the model rather than of the status line: the close responders put a
	// message on that line ("Results cleared - the document was closed.") while throwing the
	// results away, so an empty-or-not test on the text would leave the panel showing the wrong
	// picture after a close. HasRun is cleared by Clear(), which is exactly what those responders
	// call.
	return KFCResults()->HasRun() ? kKFCIconFoundWidgetID : kKFCIconWidgetID;
}

}	// anonymous namespace

void KFCPanelIcon::Update()
{
	// nil when the panel is closed, which is an ordinary state - there is nothing to show then.
	InterfacePtr<IPanelControlData> panelData(
		Utils<IPalettePanelUtils>()->QueryPanelByWidgetID(kKFCPanelWidgetID));
	if (panelData == nil)
		return;

	Update(panelData);
}

void KFCPanelIcon::Update(IPanelControlData* panelData)
{
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

		// Enable as well as show. A hidden widget still takes clicks - ShowView stops the drawing,
		// not the hit test - so with the pictures stacked at one frame a single click would reach
		// every one of them and open a browser tab for each. Measured in KCM, and hit again when the
		// same panel shape was carried into KESCL.
		if (on)
			view->Enable();
		else
			view->Disable();
	}
}

bool KFCPanelIcon::IsIconWidget(const WidgetID& widgetID)
{
	for (int32 i = 0; i < kIconCount; ++i)
	{
		if (kIcons[i] == widgetID)
			return true;
	}
	return false;
}

int32 KFCPanelIcon::Count()
{
	return kIconCount;
}

WidgetID KFCPanelIcon::NthWidgetID(int32 n)
{
	if (n < 0 || n >= kIconCount)
		return kInvalidWidgetID;
	return kIcons[n];
}

// End, KFCPanelIcon.cpp.
