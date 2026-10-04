//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The engines' progress bar (the model/UI split). It stands where a RangeProgressBar stood
//  - on the run's stack, with the same four calls - but the bar itself belongs to the UI half: this asks
//  IKBSUIServices for one, and without the UI (a background thread, InDesign Server) there is no bar,
//  nothing to cancel, and every call is a no-op. Not a link matter - RangeProgressBar is PUBLIC_DECL and
//  a model plug-in could call it - but KBS counts the modal bar among the dialogs the guide lists as user-
//  interface components (vol1-06, "UI component content"); the author's call (IKBSUIServices.h says more).
//
//  The calls behave exactly as RangeProgressBar's did, because they ARE its calls: the UI half's object
//  wraps a real one (KBSUIServices.cpp). So the modal bar still lets events in while it is up - which is
//  what KBSRunGuard exists for (which of its calls does it is not measured: KBSAdvanceProgress).
//
//========================================================================================

#ifndef __KBSProgressBar_h__
#define __KBSProgressBar_h__

#include "PMString.h"

class KBSProgressBarUI;

class KBSProgressBar
{
public:
	/** RangeProgressBar's arguments (ProgressBar.h). */
	KBSProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel);
	~KBSProgressBar();

	void	SetTaskText(const PMString& text, bool16 forceRedraw = kTrue);
	void	SetPosition(int32 newPosition);
	/** False when there is no bar: nobody can have pressed a Cancel that is not there. */
	bool16	WasCancelled(bool8 setGlobalErrorState = kTrue);
	void	DisableChildProgressBars(bool16 disable);

private:
	KBSProgressBar(const KBSProgressBar&);
	KBSProgressBar& operator=(const KBSProgressBar&);

	KBSProgressBarUI*	fBar;	// the UI half's bar, or nil when there is no UI
};

#endif // __KBSProgressBar_h__
