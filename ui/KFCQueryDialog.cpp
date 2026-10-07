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
#include "IControlView.h"			// Enable / Disable - the buttons' grey; GetWidgetID - which one was pressed
#include "IDialog.h"
#include "IDialogMgr.h"
#include "IPanelControlData.h"
#include "ISession.h"
#include "ISubject.h"
#include "ITextControlData.h"		// the Runs on: line
#include "ITreeViewController.h"	// IID_ITREEVIEWCONTROLLER - the lists' selection, observed
#include "IBooleanControlData.h"	// IID_IBOOLEANCONTROLDATA - a push button's press, observed (CDialogObserver.cpp's OK / Cancel)
#include "IWindow.h"				// GetSysWindow - the platform window behind the dialog (the minimize box)
#include "widgetid.h"				// kTrueStateMessage / kListSelectionChangedMessage

// General includes:
#include "CDialogController.h"
#include "CDialogObserver.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "LocaleSetting.h"
#include "RsrcSpec.h"

// Project includes:
#include "KFCQueryDialog.h"
#include "KFCModelAccess.h"			// KFCRuns() - the run and the Runs on: line are the model's
#include "KFCPanelTitle.h"
#include "KFCQueryList.h"
#include "KFCQueryOrder.h"
#include "KFCResultTree.h"			// Rebuild / ShowStatus - the panel, told of a run as Find tells it
#include "KFCUIID.h"

#ifdef WINDOWS
// windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names (KCMBookDialog.cpp, KFCFindChangeMinimize.cpp).
#include <windows.h>
#endif

namespace
{
	/** The open dialog's own panel, while it is open: set when its observer is attached (the dialog opening) and taken
	    away when it is detached (the dialog closing) - not AddRef'd, so it never outlives the dialog it points into.
	    What lets a change made elsewhere reach the dialog that is open (KFCQueryDialogRefreshScope). */
	IPanelControlData* gOpenPanel = nil;

	// The buttons the observer hears, and what each changes in the run order (Clear: the author's addition).
	const WidgetID kOrderButtons[] = { kKFCQueryAddButtonWidgetID, kKFCQueryRemoveButtonWidgetID, kKFCQueryUpButtonWidgetID,
		kKFCQueryDownButtonWidgetID, kKFCQueryClearButtonWidgetID };
	const WidgetID kLists[] = { kKFCQuerySavedListWidgetID, kKFCQueryOrderListWidgetID };

	void EnableButton(IPanelControlData* panel, const WidgetID& id, bool on)
	{
		IControlView* button = panel->FindWidget(id);
		if (button == nil)
			return;
		if (on)
			button->Enable();
		else
			button->Disable();
	}

	/** Grey the buttons that have nothing to act on (the spec's section 2-2): Add with no saved query picked; Remove with
	    no row of the order picked; Move Up on the first row, Move Down on the last; Clear with an empty order. Run is
	    never grey (G5 as changed: a run that cannot go says why on the panel). */
	void UpdateButtons(IPanelControlData* panel)
	{
		if (panel == nil)
			return;
		const int32 left = KFCQueryListSelectedIndex(panel, kKFCQuerySavedListWidgetID);
		const int32 right = KFCQueryListSelectedIndex(panel, kKFCQueryOrderListWidgetID);
		const int32 rows = static_cast<int32>(KFCQueryOrder::Order().size());
		EnableButton(panel, kKFCQueryAddButtonWidgetID, left >= 0);
		EnableButton(panel, kKFCQueryRemoveButtonWidgetID, right >= 0);
		EnableButton(panel, kKFCQueryUpButtonWidgetID, right > 0);
		EnableButton(panel, kKFCQueryDownButtonWidgetID, right >= 0 && right + 1 < rows);
		EnableButton(panel, kKFCQueryClearButtonWidgetID, rows > 0);
	}

	/** Say on the panel's message line that the run order file could not be read or written - the dialog stays open,
	    and what it shows is then not what the file holds. */
	void SayOrderFileFailed(const char* what, const PMString& why)
	{
		PMString say;
		say.SetTranslatable(kFalse);
		say.Append("The run order could not be ");
		say.Append(what);
		say.Append(" (");
		say.Append(why);
		say.Append(").");
		KFCResultTree::ShowStatus(say);
	}

	/** The Runs on: line: what Run would run on now, or why it cannot - the model's words (DescribeRunScope), made from the
	    very answer the run asks (the spec's section 4-4). Written only when they read differently: KFCPanelTitle::Update
	    asks on every caret step while the dialog is open. */
	void ShowRunScope(IPanelControlData* panel)
	{
		if (panel == nil)
			return;
		IControlView* lineView = panel->FindWidget(kKFCQueryScopeTextWidgetID);
		InterfacePtr<ITextControlData> line(lineView, UseDefaultIID());
		if (line == nil)
			return;
		PMString words;
		(void)KFCRuns()->DescribeRunScope(words);
		words.SetTranslatable(kFalse);
		if (line->GetString().IsEqual(words))
			return;
		line->SetString(words);
	}

	/** Fill the dialog from what is true now: both lists read again - the saved queries from their folders, the run
	    order from its file - and drawn, the buttons greyed to match (nothing is picked after a fill), and the Runs on:
	    line. */
	void Repaint(IPanelControlData* panel)
	{
		if (panel == nil)
			return;
		KFCQueryOrder::LoadSaved();
		PMString why;
		if (!KFCQueryOrder::LoadOrder(why))
			SayOrderFileFailed("read", why);
		KFCQueryListRebuild(panel, kKFCQuerySavedListWidgetID);
		KFCQueryListRebuild(panel, kKFCQueryOrderListWidgetID);
		UpdateButtons(panel);
		ShowRunScope(panel);
	}

	/** Run (G5 as changed by the author: "Run can always be pressed - with results on the panel too, pressing it runs"):
	    the run order as the dialog shows it, run by the model - InDesign's Change All, query by query, all of it one undo
	    step - and the panel told as Find tells it (KFCActionComponent). The dialog stays open for the next run. A run
	    that cannot go - no queries, no query's file, a scope it cannot run on, another run going - says why on the
	    panel's message line (the model's refusals); nothing is greyed for it. */
	void PressRun(IPanelControlData* panel)
	{
		KFCPanelTitle::Update();	// the tab's scope written before the run, as Find writes it (this line with it)
		PMString summary;
		(void)KFCRuns()->RunQueries(KFCQueryOrder::OrderFiles(), summary);
		KFCResultTree::Rebuild();
		KFCResultTree::ShowStatus(summary);
		ShowRunScope(panel);		// a run can close what it opened, and a book can go: asked again
	}

	/** One of the five buttons pressed: change the order, write it to its file at once (G2), draw it, and pick the row
	    the change leaves the eye on - the added row, the row that took a removed one's place, the moved row. */
	void PressOrderButton(IPanelControlData* panel, const WidgetID& button)
	{
		const int32 left = KFCQueryListSelectedIndex(panel, kKFCQuerySavedListWidgetID);
		const int32 right = KFCQueryListSelectedIndex(panel, kKFCQueryOrderListWidgetID);
		int32 pick = -1;
		if (button == kKFCQueryAddButtonWidgetID)
		{
			if (left < 0)
				return;
			KFCQueryOrder::Add(left);
			pick = static_cast<int32>(KFCQueryOrder::Order().size()) - 1;
		}
		else if (button == kKFCQueryRemoveButtonWidgetID)
		{
			if (right < 0)
				return;
			KFCQueryOrder::Remove(right);
			const int32 rows = static_cast<int32>(KFCQueryOrder::Order().size());
			pick = right < rows ? right : rows - 1;
		}
		else if (button == kKFCQueryUpButtonWidgetID)
		{
			if (right <= 0)
				return;
			KFCQueryOrder::MoveUp(right);
			pick = right - 1;
		}
		else if (button == kKFCQueryDownButtonWidgetID)
		{
			if (right < 0 || right + 1 >= static_cast<int32>(KFCQueryOrder::Order().size()))
				return;
			KFCQueryOrder::MoveDown(right);
			pick = right + 1;
		}
		else if (button == kKFCQueryClearButtonWidgetID)
		{
			if (KFCQueryOrder::Order().empty())
				return;
			KFCQueryOrder::Clear();
		}
		else
			return;

		PMString why;
		if (!KFCQueryOrder::SaveOrder(why))
			SayOrderFileFailed("saved", why);
		KFCQueryListRebuild(panel, kKFCQueryOrderListWidgetID);
		KFCQueryListSelect(panel, kKFCQueryOrderListWidgetID, pick);
		UpdateButtons(panel);
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
    method - that is what keeps Close (Cancel) and the close box working (basicdialog's BscDlgDialogObserver) - except
    for Run (the OK button, Enter), which it takes itself and does not hand on: the base's OK closes the dialog, and
    runs ApplyDialog inside a command sequence of its own (CDialogObserver.cpp) that the query run's abortable sequence
    would nest in. It hears
    the five buttons that change the run order (a PUSH button's press: kTrueStateMessage on IID_IBOOLEANCONTROLDATA - how
    CDialogObserver.cpp itself hears OK and Cancel, and the product's ProblemLinksDialogObserver its Fix Links button;
    MEASURED 2026-10-07: attached on IID_ITRISTATECONTROLDATA - basicdialog's ICON button's - the presses never arrived)
    and the two lists' selection (kListSelectionChangedMessage on IID_ITREEVIEWCONTROLLER -
    the paneltreeview sample's PnlTrvTreeObserver), and says, through gOpenPanel, whether the dialog is open. */
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
		if (panel == nil)
			return;
		for (size_t i = 0; i < sizeof(kOrderButtons) / sizeof(kOrderButtons[0]); ++i)
			this->AttachToWidget(kOrderButtons[i], IID_IBOOLEANCONTROLDATA, panel);
		for (size_t i = 0; i < sizeof(kLists) / sizeof(kLists[0]); ++i)
			this->AttachToWidget(kLists[i], IID_ITREEVIEWCONTROLLER, panel);
	}

	virtual void AutoDetach()
	{
		gOpenPanel = nil;
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel != nil)
		{
			for (size_t i = 0; i < sizeof(kOrderButtons) / sizeof(kOrderButtons[0]); ++i)
				this->DetachFromWidget(kOrderButtons[i], IID_IBOOLEANCONTROLDATA, panel);
			for (size_t i = 0; i < sizeof(kLists) / sizeof(kLists[0]); ++i)
				this->DetachFromWidget(kLists[i], IID_ITREEVIEWCONTROLLER, panel);
		}
		CDialogObserver::AutoDetach();
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
	{
		// Run - ahead of the base, and not handed to it (above). The base attached the OK button, on this protocol.
		if (protocol == IID_IBOOLEANCONTROLDATA && theChange == kTrueStateMessage)
		{
			InterfacePtr<IControlView> pressed(theSubject, UseDefaultIID());
			if (pressed != nil && pressed->GetWidgetID() == kOKButtonWidgetID)
			{
				InterfacePtr<IPanelControlData> runPanel(this, UseDefaultIID());
				PressRun(runPanel);
				return;
			}
		}
		CDialogObserver::Update(theChange, theSubject, protocol, changedBy);
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil)
			return;
		if (protocol == IID_ITREEVIEWCONTROLLER && theChange == kListSelectionChangedMessage)
		{
			UpdateButtons(panel);
			return;
		}
		if (protocol != IID_IBOOLEANCONTROLDATA || theChange != kTrueStateMessage)
			return;
		// One of OUR five, asked before anything else is touched: Close (Cancel) arrives here too, after the base above
		// has begun closing the dialog, and its lists must not be read then.
		InterfacePtr<IControlView> pressed(theSubject, UseDefaultIID());
		if (pressed == nil)
			return;
		const WidgetID id = pressed->GetWidgetID();
		for (size_t i = 0; i < sizeof(kOrderButtons) / sizeof(kOrderButtons[0]); ++i)
			if (kOrderButtons[i] == id)
			{
				PressOrderButton(panel, id);
				return;
			}
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

void KFCQueryDialogRefreshScope()
{
	ShowRunScope(gOpenPanel);		// nil while no dialog is open - nothing then
}

// End, KFCQueryDialog.cpp.
