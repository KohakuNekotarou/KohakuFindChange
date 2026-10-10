//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCProgressBar.h. Model side.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"

// Project includes:
#include "IKFCUIServices.h"
#include "KFCProgressBar.h"

KFCProgressBar::KFCProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel)
	: fBar(nil)
{
	InterfacePtr<IKFCUIServices> ui(GetExecutionContextSession(), UseDefaultIID());
	if (ui != nil)
		fBar = ui->NewProgressBar(title, startRange, endRange, showImmediate, showCancel);
}

KFCProgressBar::~KFCProgressBar()
{
	delete fBar;	// the bar comes down here, as a RangeProgressBar did leaving its scope
}

void KFCProgressBar::SetTaskText(const PMString& text, bool16 forceRedraw)
{
	if (fBar != nil)
		fBar->SetTaskText(text, forceRedraw);
}

void KFCProgressBar::SetPosition(int32 newPosition)
{
	if (fBar != nil)
		fBar->SetPosition(newPosition);
}

bool16 KFCProgressBar::WasCancelled(bool8 setGlobalErrorState)
{
	return (fBar != nil) ? fBar->WasCancelled(setGlobalErrorState) : kFalse;
}

void KFCProgressBar::DisableChildProgressBars(bool16 disable)
{
	if (fBar != nil)
		fBar->DisableChildProgressBars(disable);
}

void KFCSetCountedTask(KFCProgressBar& bar, const char* noun, size_t index, size_t count, const PMString& name)
{
	PMString taskLine;
	taskLine.SetTranslatable(kFalse);
	taskLine.Append(noun);
	taskLine.Append(" ");
	taskLine.AppendNumber(static_cast<int32>(index) + 1);
	taskLine.Append(" / ");
	taskLine.AppendNumber(static_cast<int32>(count));
	taskLine.Append(" - ");
	taskLine.Append(name);
	bar.SetTaskText(taskLine);
}

// End, KFCProgressBar.cpp.
