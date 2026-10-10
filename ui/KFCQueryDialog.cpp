//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The query dialog - see KFCQueryDialog.h. A MODELESS dialog with a minimize box (the author's call: "other
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
#include "IDropDownListController.h"	// Select - a loaded query chosen in Find/Change's own Query menu
#include "IStringListControlData.h"		// GetIndex - that menu's entries, found by name
#include "FindChangeID.h"				// kFindQueryDropDownWidgetID - the Query menu
#include "widgetid.h"				// kTrueStateMessage / kListSelectionChangedMessage

// General includes:
#include "CDialogController.h"
#include "CDialogObserver.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "FileUtils.h"				// SysFileToPMString - the saved / loaded file's path, said
#include "LocaleSetting.h"
#include "RsrcSpec.h"

// Project includes:
#include "KFCQueryDialog.h"
#include "KFCDiag.h"				// the test build's qd-order-reset (Repaint)
#include "KFCDiagHiddenDocs.h"		// the hidden-document check after a Run - test builds only
#include "KFCFindChangeMinimize.h"	// KFCShowFindChangeDialog - a double-clicked query, shown
#include "KFCPanelAlpha.h"			// KFCQueryFindChangeWidget - Find/Change's Query menu, found as the dialog is
#include "KFCModelAccess.h"			// KFCRuns() - the run and the Runs on: line are the model's
#include "KFCPanelTitle.h"
#include "KFCQueryList.h"
#include "KFCQueryOrder.h"
#include "KFCQueryOrderFile.h"		// KFCChooseOrderFile - Save Order... / Load Order...'s file
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
	// ...and the two that take the run order to a file of its own and back (KFCQueryOrderFile.h).
	const WidgetID kFileButtons[] = { kKFCQuerySaveOrderButtonWidgetID, kKFCQueryLoadOrderButtonWidgetID };
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
	    no row of the order picked; Move Up on the first row, Move Down on the last; Clear and Save Order... with an empty
	    order. Load Order... and Run are never grey (G5 as changed: a run that cannot go says why on the panel). */
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
		EnableButton(panel, kKFCQuerySaveOrderButtonWidgetID, rows > 0);
		EnableButton(panel, kKFCQueryLoadOrderButtonWidgetID, true);
	}

	/** The dialog's own message line (the author's call): the words the panel's message line is given, here
	    too. The dialog is modeless and stands while the KFC panel is closed, and a Run's result - or why it could not
	    run - was then shown nowhere; the panel still gets the same line (KFCResultTree::ShowStatus keeps it for its next
	    show). At every open it says how a saved query is seen in Find/Change (Repaint - the author's call). */
	void ShowDialogMessage(IPanelControlData* panel, const PMString& message)
	{
		if (panel == nil)
			return;
		InterfacePtr<ITextControlData> line(panel->FindWidget(kKFCQueryMessageTextWidgetID), UseDefaultIID());
		if (line == nil)
			return;
		PMString words(message);
		words.SetTranslatable(kFalse);
		line->SetString(words);
	}

	/** One message on both lines - the panel's and the dialog's (the dialog stands with the panel closed) - as Run's
	    result goes to both. */
	void SayOnBoth(IPanelControlData* panel, const PMString& message)
	{
		PMString say(message);
		say.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(say);
		ShowDialogMessage(panel, say);
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

	/** Fill the dialog from what is true now: the saved queries read again from their folders, the session's run order
	    with each row's file asked again (a query deleted since reads "(not found)"), both drawn, the buttons greyed to
	    match (nothing is picked after a fill), and the Runs on: line. */
	void Repaint(IPanelControlData* panel)
	{
		if (panel == nil)
			return;
		// A fresh open says nothing of an earlier run - it says how to see a saved query (the author's call: "show the
		// double click in the message when the dialog opens").
		ShowDialogMessage(panel, PMString("Double-click a saved query to load it into Find/Change."));
#ifdef KFC_DIAG
		// TEST BUILDS ONLY: the fault switch qd-order-reset (KFCDiag.h) - a test case starts from an empty order, as a
		// fresh session does (the order outlives a case otherwise: it is the session's). One-shot: taken away here, so the
		// case's later opens keep the order it made.
		if (KFC_DIAG_FAULT("qd-order-reset"))
		{
			KFCQueryOrder::Clear();
			KFCDiagFaultOff("qd-order-reset");
		}
#endif
		KFCQueryOrder::LoadSaved();
		KFCQueryOrder::RefreshOrder();
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
		KFC_DIAG_HIDDEN_DOCS_CHECK("query-run");	// (test builds only - a document left with no window when the run is over)
		KFCPanelTitle::Update();	// the tab's scope written before the run, as Find writes it (this line with it)
		PMString summary;
		(void)KFCRuns()->RunQueries(KFCQueryOrder::OrderFiles(), summary);
		KFCResultTree::Rebuild();
		SayOnBoth(panel, summary);	// the panel's line and the dialog's: the panel may be closed
		ShowRunScope(panel);		// a run can close what it opened, and a book can go: asked again
	}

	/** One of the five buttons pressed: change the session's order (nothing is written - the order is kept as a file only
	    by Save Order...), draw it, and pick the row the change leaves the eye on - the added row, the row that took a
	    removed one's place, the moved row. */
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

		KFCQueryListRebuild(panel, kKFCQueryOrderListWidgetID);
		KFCQueryListSelect(panel, kKFCQueryOrderListWidgetID, pick);
		UpdateButtons(panel);
	}

	/** Save Order... (the author's call): the run order to a file the person names, through InDesign's own Save
	    dialog (KFCChooseOrderFile). Written: the file's full path and nothing else on both lines - Save Panel Settings'
	    way (the author's call: "show where it was saved"). Cancelled: nothing changes, nothing is said. */
	void PressSaveOrder(IPanelControlData* panel)
	{
		if (KFCQueryOrder::Order().empty())
		{
			SayOnBoth(panel, PMString("Save Order: the run order is empty."));
			return;
		}
		IDFile file;
		if (!KFCChooseOrderFile(true, file))
			return;
		const PMString path(FileUtils::SysFileToPMString(file));
		PMString why;
		if (!KFCQueryOrder::SaveOrderTo(file, why))
		{
			PMString say("Save Order: the file could not be written (");
			say.SetTranslatable(kFalse);
			say.Append(why);
			say.Append(") - ");
			say.Append(path);
			SayOnBoth(panel, say);
			return;
		}
		SayOnBoth(panel, path);
	}

	/** Load Order...: the run order the file holds, through InDesign's own Open dialog, put in place of the one shown
	    (KFCQueryOrder::LoadOrderFrom - a query found again by its kind and name when its file has moved, "(not found)"
	    otherwise, and Run then refuses). Loaded: the file's full path on both lines, as Save says it. Not loaded - not
	    there, or not a KFC query order: the order is left as it was, and the lines say why. */
	void PressLoadOrder(IPanelControlData* panel)
	{
		IDFile file;
		if (!KFCChooseOrderFile(false, file))
			return;
		const PMString path(FileUtils::SysFileToPMString(file));
		PMString why;
		if (!KFCQueryOrder::LoadOrderFrom(file, why))
		{
			PMString say("Load Order: ");
			say.SetTranslatable(kFalse);
			say.Append(path);
			say.Append(why == PMString("format") ? " is not a KFC query order." : " could not be read.");
			SayOnBoth(panel, say);
			return;
		}
		KFCQueryListRebuild(panel, kKFCQuerySavedListWidgetID);		// read again on the way (LoadOrderFrom)
		KFCQueryListRebuild(panel, kKFCQueryOrderListWidgetID);
		UpdateButtons(panel);
		SayOnBoth(panel, path);
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
    the five buttons that change the run order, Save Order... and Load Order... (a PUSH button's press: kTrueStateMessage on IID_IBOOLEANCONTROLDATA - how
    CDialogObserver.cpp itself hears OK and Cancel, and the product's ProblemLinksDialogObserver its Fix Links button;
    MEASURED: attached on IID_ITRISTATECONTROLDATA - basicdialog's ICON button's - the presses never arrived)
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
		for (size_t i = 0; i < sizeof(kFileButtons) / sizeof(kFileButtons[0]); ++i)
			this->AttachToWidget(kFileButtons[i], IID_IBOOLEANCONTROLDATA, panel);
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
			for (size_t i = 0; i < sizeof(kFileButtons) / sizeof(kFileButtons[0]); ++i)
				this->DetachFromWidget(kFileButtons[i], IID_IBOOLEANCONTROLDATA, panel);
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
		if (id == kKFCQuerySaveOrderButtonWidgetID)
			PressSaveOrder(panel);
		else if (id == kKFCQueryLoadOrderButtonWidgetID)
			PressLoadOrder(panel);
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
	// with kDontCacheDialog - the dialog minimized, Run Saved Queries... made a SECOND dialog beside it (two
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

// THE LOADED QUERY CHOSEN IN FIND/CHANGE'S OWN QUERY MENU TOO (2026-10-08, the author: take the name and choose that
// query in the official Find/Change dialog). Loaded through kFCQueryXMLReaderCmdBoss alone, the menu went on reading
// [Custom] - the strings were the query's and the menu did not say so (measured, docs/ai-notes/kfc-full-test-2026-10-08.md
// section 1). Returns whether the menu now names it; outWhy says why not, otherwise.
// *BY NAME, the one handle the menu offers: its entries are strings (IStringListControlData). What it keeps beside them
//  (IID_IFCINTLISTDATA, kFCIntListDataImpl) has no public interface, and KFC is on Exchange - no guessed vtable (the author
//  also asked whether an ID would do; there is none to use).
// *ONE MENU FOR EVERY KIND, IN GROUPS. Measured 2026-10-08 (work/kbs-regress case qd-dblclick-query-menu, FCMENU): the
//  menu lists the Text queries, then the GREP ones, then the Object ones, the groups parted by "-" entries (disabled) and
//  [Custom] last - not refilled per tab - and each name as the file is named, bundled ones in English on a Japanese UI
//  too. So a Text query and a GREP query of one name are two entries alike.
// *NAME AND PLACE, THEN (the author, the same day: name + order?): the name is looked for, and when more than one group
//  carries it, each such group is weighed against the names KFC knows of the query's own kind - one point for an entry
//  that is one of them, one off for an entry that is not - and the highest is the query's. Weighed rather than taken by
//  position, because the menu can group queries KFC does not list (the Object ones did, before 1.4.0) and so KFC cannot
//  count the groups before it. InDesign's own queries make the Text and GREP groups unlike; only groups as alike as that can tie,
//  and then nothing is chosen and the line says why.
// *TOLD, AS A PERSON'S PICK IS (notifyOfChange = kTrue): the dialog loads it the way it loads a query picked in that menu
//  (the same file the command above just read). Measured the same day with kFalse, a pick it was not told of: the menu
//  named the query and its delete button stayed grey (FCWIDGET 0x497b, kFCQueryDeleteButtonWidgetID, 0/0).
static bool16 IsMenuSeparator(IStringListControlData* entries, int32 i)
{
	return (!entries->IsEnabled(i) && entries->GetString(i) == PMString("-")) ? kTrue : kFalse;
}

static bool16 IsSavedNameOfMode(const PMString& name, int32 mode)
{
	const std::vector<KFCSavedQuery>& saved = KFCQueryOrder::Saved();
	for (size_t i = 0; i < saved.size(); ++i)
		if (saved[i].mode == mode && saved[i].name == name)
			return kTrue;
	return kFalse;
}

static bool16 ChooseInFindChangeQueryMenu(const KFCSavedQuery& query, PMString& outWhy)
{
	IControlView* menu = KFCQueryFindChangeWidget(kFindQueryDropDownWidgetID);
	InterfacePtr<IStringListControlData> entries(menu, UseDefaultIID());
	InterfacePtr<IDropDownListController> chooser(menu, UseDefaultIID());
	if (entries == nil || chooser == nil)
	{
		outWhy.Append("Find/Change's Query menu could not be reached");
		return kFalse;
	}
	int32 best = -1;			// the entry chosen
	int32 bestScore = 0;
	int32 tied = 0;				// other groups that carry the name and weigh the same as the best
	int32 carrying = 0;			// groups that carry the name
	int32 groupStart = 0;
	const int32 count = entries->Length();
	for (int32 i = 0; i <= count; ++i)
	{
		if (i < count && !IsMenuSeparator(entries, i))
			continue;
		// The group [groupStart, i): where it carries the name, and how much it looks like the query's kind.
		int32 at = -1;
		int32 score = 0;
		for (int32 j = groupStart; j < i; ++j)
		{
			const PMString entry = entries->GetString(j);
			if (at < 0 && entry == query.name)
				at = j;
			score += IsSavedNameOfMode(entry, query.mode) ? 1 : -1;
		}
		if (at >= 0)
		{
			++carrying;
			if (best < 0 || score > bestScore)
			{
				best = at;
				bestScore = score;
				tied = 0;
			}
			else if (score == bestScore)
				++tied;
		}
		groupStart = i + 1;
	}
	if (carrying == 0)
	{
		outWhy.Append("its Query menu does not list it");
		return kFalse;
	}
	if (tied > 0)
	{
		outWhy.Append("its Query menu lists that name under more than one kind, in groups that cannot be told apart");
		return kFalse;
	}
	chooser->Select(best, kTrue, kTrue);
	return kTrue;
}

void KFCQueryDialogShowInFindChange(int32 savedIndex)
{
	const std::vector<KFCSavedQuery>& saved = KFCQueryOrder::Saved();
	if (savedIndex < 0 || savedIndex >= static_cast<int32>(saved.size()))
		return;
	const KFCSavedQuery& query = saved[static_cast<size_t>(savedIndex)];
	PMString say;
	say.SetTranslatable(kFalse);
	// NOT WHILE A RUN IS UP: its modal bar pumps events, so a double click can arrive in the middle of one - and a query
	// run is putting its own queries into Find/Change (KFCRunGuard).
	if (KFCRuns()->IsAnyRunning())
	{
		say.Append(KFCRuns()->BusyMessage());
		ShowDialogMessage(gOpenPanel, say);
		return;
	}
	// The command changes nothing for a file that is not there and still answers success (KFCSavedQueries.h) - asked
	// first, as the run asks: the left list was read at the open, and a query can be deleted since.
	if (!FileUtils::DoesFileExist(query.file))
	{
		say.Append(query.name);
		say.Append(" cannot be found - nothing was loaded into Find/Change.");
		ShowDialogMessage(gOpenPanel, say);
		return;
	}
	// THE DIALOG FIRST, THEN THE QUERY (measured - cases qd-dblclick-closed / qd-dblclick-open). Find/Change opened AFTER
	// the query was loaded came up on the tab it was last on: its open put that tab back over the query's (a GREP query
	// loaded with the dialog last on Text - the GREP strings in, the Text tab shown). Loaded into the dialog once it
	// stands, the query takes its own tab, as a query picked in the dialog's own Query menu does.
	(void)KFCShowFindChangeDialog();
	if (!KFCRuns()->LoadSavedQuery(query.file))
	{
		say.Append(query.name);
		say.Append(" could not be loaded into Find/Change.");
		ShowDialogMessage(gOpenPanel, say);
		return;
	}
	say.Append("Loaded into Find/Change: ");
	say.Append(KFCQueryOrder::SavedRowText(savedIndex));		// "<kind>  <name>", as the row reads
	PMString notChosen;
	if (!ChooseInFindChangeQueryMenu(query, notChosen))
	{
		say.Append(" - ");
		say.Append(notChosen);
		say.Append(", so the menu reads [Custom].");
	}
	ShowDialogMessage(gOpenPanel, say);
}

// End, KFCQueryDialog.cpp.
