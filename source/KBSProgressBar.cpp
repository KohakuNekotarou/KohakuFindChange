//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSProgressBar.h. Model side.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"

// Project includes:
#include "IKBSUIServices.h"
#include "KBSProgressBar.h"

KBSProgressBar::KBSProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel)
	: fBar(nil)
{
	InterfacePtr<IKBSUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	if (ui != nil)
		fBar = ui->NewProgressBar(title, startRange, endRange, showImmediate, showCancel);
}

KBSProgressBar::~KBSProgressBar()
{
	delete fBar;	// the bar comes down here, as a RangeProgressBar did leaving its scope
}

void KBSProgressBar::SetTaskText(const PMString& text, bool16 forceRedraw)
{
	if (fBar != nil)
		fBar->SetTaskText(text, forceRedraw);
}

void KBSProgressBar::SetPosition(int32 newPosition)
{
	if (fBar != nil)
		fBar->SetPosition(newPosition);
}

bool16 KBSProgressBar::WasCancelled(bool8 setGlobalErrorState)
{
	return (fBar != nil) ? fBar->WasCancelled(setGlobalErrorState) : kFalse;
}

void KBSProgressBar::DisableChildProgressBars(bool16 disable)
{
	if (fBar != nil)
		fBar->DisableChildProgressBars(disable);
}

// End, KBSProgressBar.cpp.
