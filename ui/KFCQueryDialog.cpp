//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The query dialog - see KFCQueryDialog.h. A MODELESS dialog with a minimize box (the author's call, 2026-10-07: "other
//  work can go on while it is open, with a minimize button" - the spec's G4 as changed), made the way KCM's book
//  comparison dialog is (KCMBookDialog.cpp): Open(nil, kFalse), and the minimize box put on the platform window by hand.
//  Its controller fills it when the framework builds it; KFCQueryDialogOpen fills it again on every open, because a
//  modeless dialog opened while it is already open never sees InitializeDialogFields (memory
//  modeless-dialog-reopen-not-reinitialized - KCM's book dialog showed the previous comparison's rows that way).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IDialog.h"
#include "IDialogMgr.h"
#include "IPanelControlData.h"
#include "ISession.h"
#include "IWindow.h"				// GetSysWindow - the platform window behind the dialog (the minimize box)

// General includes:
#include "CDialogController.h"
#include "CDialogObserver.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "LocaleSetting.h"
#include "RsrcSpec.h"

// Project includes:
#include "KFCQueryDialog.h"
#include "KFCQueryList.h"
#include "KFCQueryOrder.h"
#include "KFCUIID.h"

#ifdef WINDOWS
// windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names (KCMBookDialog.cpp, KFCFindChangeMinimize.cpp).
#include <windows.h>
#endif

namespace
{
	/** The open dialog's own panel, while it is open: set when its observer is attached (the dialog opening) and taken
	    away when it is detached (the dialog closing) - not AddRef'd, so it never outlives the dialog it points into.
	    What lets a change made elsewhere reach the dialog that is open (KFCQueryDialogRefresh). */
	IPanelControlData* gOpenPanel = nil;

	/** Fill the dialog from what is true now: both lists read again (the saved queries from their folders) and drawn. */
	void Repaint(IPanelControlData* panel)
	{
		if (panel == nil)
			return;
		KFCQueryOrder::LoadSaved();
		KFCQueryListRebuild(panel, kKFCQuerySavedListWidgetID);
		KFCQueryListRebuild(panel, kKFCQueryOrderListWidgetID);
	}

	/** The dialog's platform window: a minimize box, and back from the taskbar if it sits minimized. The SDK cannot give
	    an existing window a minimize box (a modeless dialog's controls are the close box alone - IWindow.h), so it is
	    put on with Win32, as KCM's book dialog has it: WS_MINIMIZEBOX alone shows nothing - the box appears only once
	    WS_EX_TOOLWINDOW is off (Windows draws none on a tool window's frame) - and WS_EX_APPWINDOW puts the minimized
	    dialog on the taskbar rather than leaving a stub at the screen's bottom left (KCM, measured 2026-08-12). The
	    window is ours - made by KFCQueryDialogOpen and destroyed when the dialog closes - so nothing is put back.
	    Open() on a dialog that sits minimized does not bring it back (KCM, measured): ShowWindow(SW_RESTORE) does. */
	void PrepareWindow(IDialog* dialog)
	{
#ifdef WINDOWS
		InterfacePtr<IWindow> window(dialog, IID_IWINDOW);
		if (window == nil)
			return;
		HWND hwnd = (HWND)window->GetSysWindow();
		if (hwnd == nullptr || !::IsWindow(hwnd))
			return;
		::SetWindowLongPtr(hwnd, GWL_STYLE, ::GetWindowLongPtr(hwnd, GWL_STYLE) | WS_MINIMIZEBOX);
		::SetWindowLongPtr(hwnd, GWL_EXSTYLE, (::GetWindowLongPtr(hwnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
		// SWP_NOACTIVATE is ours; the other four are what Microsoft's SetWindowPos remarks prescribe for a style change.
		::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
		if (::IsIconic(hwnd))
			::ShowWindow(hwnd, SW_RESTORE);
#else
		(void)dialog;
#endif
	}
}

/** Fills the dialog when the framework builds it - its first open (KFCQueryDialogOpen fills it again on every open). */
class KFCQueryDialogController : public CDialogController
{
public:
	KFCQueryDialogController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCQueryDialogController() {}

protected:
	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		Repaint(panel);
	}
};

CREATE_PMINTERFACE(KFCQueryDialogController, kKFCQueryDialogControllerImpl)

/** The dialog's observer. It stands in for the stock CDialogObserver kDialogBoss carries, and calls it FIRST in each
    method - that is what keeps Close (Cancel) and the close box working (basicdialog's BscDlgDialogObserver). It also
    says, through gOpenPanel, whether the dialog is open. */
class KFCQueryDialogObserver : public CDialogObserver
{
public:
	KFCQueryDialogObserver(IPMUnknown* boss) : CDialogObserver(boss) {}
	virtual ~KFCQueryDialogObserver() {}

	virtual void AutoAttach()
	{
		CDialogObserver::AutoAttach();
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		gOpenPanel = panel;
	}

	virtual void AutoDetach()
	{
		gOpenPanel = nil;
		CDialogObserver::AutoDetach();
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
	{
		CDialogObserver::Update(theChange, theSubject, protocol, changedBy);
	}
};

CREATE_PMINTERFACE(KFCQueryDialogObserver, kKFCQueryDialogObserverImpl)

void KFCQueryDialogOpen()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;
	InterfacePtr<IApplication> application(session->QueryApplication());
	if (application == nil)
		return;
	InterfacePtr<IDialogMgr> dialogMgr(application, UseDefaultIID());
	if (dialogMgr == nil)
		return;

	RsrcSpec dialogSpec
	(
		LocaleSetting::GetLocale(),		// Locale index
		kKFCUIPluginID,					// This plug-in
		kViewRsrcType,
		kKFCQueryDialogRsrcID,			// The dialog's view resource
		kTrue							// Initially visible
	);

	// kModeless (G4 as changed): other work goes on while it is open. One copy, and kCacheDialog FOR THAT: MEASURED
	// 2026-10-07 with kDontCacheDialog - the dialog minimized, Run Saved Queries... made a SECOND dialog beside it (two
	// windows), whatever IDialogMgr.h says of a modeless dialog being single-copy. With kCacheDialog the open one comes
	// back - KCM's book dialog relies on the same. What it shows is not the cache's: Repaint below reads the lists anew
	// on every open. Fixed size. The pointer is not held: the dialog destroys itself when it closes.
	IDialog* dialog = dialogMgr->CreateNewDialog(dialogSpec, IDialog::kModeless,
		IDialogMgr::kDontAllowMultipleCopies, IDialogMgr::kCacheDialog, IDialogMgr::kDontAllowUserResize);
	if (dialog == nil)
		return;

	// doWait = kFalse: Open() waits by default (IDialog.h), which would make a modeless dialog behave as a modal one.
	dialog->Open(nil, kFalse);
	PrepareWindow(dialog);

	// Filled again on EVERY open: one opened while it is already open (to bring it forward, or to list a query saved
	// since) is not given InitializeDialogFields. The panel is the dialog boss's own (KCMBookDialog.cpp reaches it so).
	InterfacePtr<IPanelControlData> panel(dialog, UseDefaultIID());
	Repaint(panel);
}

// End, KFCQueryDialog.cpp.
