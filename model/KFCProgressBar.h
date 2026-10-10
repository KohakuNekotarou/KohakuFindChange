//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The engines' progress bar (the model/UI split). It stands where a RangeProgressBar stood
//  - on the run's stack, with the same four calls - but the bar itself belongs to the UI half: this asks
//  IKFCUIServices for one, and without the UI (a background thread, InDesign Server) there is no bar,
//  nothing to cancel, and every call is a no-op. Not a link matter - RangeProgressBar is PUBLIC_DECL and
//  a model plug-in could call it - but KFC counts the modal bar among the dialogs the guide lists as user-
//  interface components (vol1-06, "UI component content"); the author's call (IKFCUIServices.h says more).
//
//  The calls behave exactly as RangeProgressBar's did, because they ARE its calls: the UI half's object
//  wraps a real one (KFCUIServices.cpp). So the modal bar still lets events in while it is up - which is
//  what KFCRunGuard exists for (which of its calls does it is not measured: KFCAdvanceProgress).
//
//========================================================================================

#ifndef __KFCProgressBar_h__
#define __KFCProgressBar_h__

#include "PMString.h"

class KFCProgressBarUI;

class KFCProgressBar
{
public:
	/** RangeProgressBar's arguments (ProgressBar.h). */
	KFCProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel);
	~KFCProgressBar();

	void	SetTaskText(const PMString& text, bool16 forceRedraw = kTrue);
	void	SetPosition(int32 newPosition);
	/** False when there is no bar: nobody can have pressed a Cancel that is not there. */
	bool16	WasCancelled(bool8 setGlobalErrorState = kTrue);
	void	DisableChildProgressBars(bool16 disable);

private:
	KFCProgressBar(const KFCProgressBar&);
	KFCProgressBar& operator=(const KFCProgressBar&);

	KFCProgressBarUI*	fBar;	// the UI half's bar, or nil when there is no UI
};

/** "<noun> <index + 1> / <count> - <name>" on the bar ("Chapter 3 / 12 - ch03.indd") - the line every search's bar shows
    for the document or chapter it is in (the text search and the object search alike). Text only: the bar's position is
    the caller's. */
void KFCSetCountedTask(KFCProgressBar& bar, const char* noun, size_t index, size_t count, const PMString& name);

#endif // __KFCProgressBar_h__
