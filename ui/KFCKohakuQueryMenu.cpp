//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's queries in Find/Change's own Query menu - see KFCKohakuQueryMenu.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "ICallbackTimer.h"				// the entry chosen again, the handler returned
#include "ICommand.h"
#include "ICommandInterceptor.h"
#include "ICommandProcessor.h"
#include "IControlView.h"
#include "IDropDownListController.h"
#include "IEvent.h"
#include "IEventDispatcher.h"			// Push / Remove - the filter on the application's stack
#include "IIntData.h"
#include "IObserver.h"					// InDesign's menu's own rebuild
#include "ISelectableDialogSwitcher.h"
#include "ISession.h"
#include "IStringListControlData.h"
#include "ISubject.h"
#include "ISysFileData.h"
#include "IWidgetParent.h"
#include "widgetid.h"					// IID_ISTRINGLISTCONTROLDATA

// General includes:
#include "CAlert.h"
#include "CEventHandler.h"				// a filter's base (its header says so)
#include "CPMUnknown.h"
#include "CreateObject.h"
#include "FileUtils.h"
#include "FindChangeID.h"				// the query row's widgets, IID_IFCAPPRESUMEOBSERVER, IID_IFINDCHANGEMSG, kFindChangeValueChangedMessage
#include "IDThreadingPrimitives.h"
#include "ShuksanID.h"					// kApplicationResumeMsg
#include "TextWalkerServiceProviderID.h"	// kFCQueryXMLReaderCmdBoss

#include <string>

// Project includes:
#include "KFCKohakuQueryMenu.h"
#include "KFCDiag.h"
#include "KFCKohakuTab.h"
#include "KFCModelAccess.h"				// KFCRuns() - a query's file is the model's
#include "KFCPanelAlpha.h"				// KFCQueryFindChangeWidget / KFCQueryFindChangeWindow
#include "KFCPanelState.h"				// KFCWriteFileSafely
#include "KFCPanelTitle.h"
#include "KFCResultTree.h"				// ShowStatus
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow
#include "KFCSpecialDialogs.h"			// AskQueryName
#include "KFCUIID.h"

// (windows.h is in already: the precompiled header brings it - KFCPanelAlpha.cpp says how. WindowFromPoint: what is under the pointer.)
#include <windows.h>

namespace
{

ICommandInterceptor* gWatch = nil;
IEventHandler* gFilter = nil;
bool gFilterPushed = false;
bool gShutDown = false;
ICallbackTimer* gTimer = nil;
PMString gPickedEntry;		// the entry to choose again, once InDesign's handler has returned

ICommandProcessor* QueryProcessor()
{
	ISession* const session = GetExecutionContextSession();
	return (session != nil) ? session->QueryCommandProcessor() : nil;
}

IEventDispatcher* QueryDispatcher()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> application((session != nil) ? session->QueryApplication() : nil);
	return (application != nil) ? InterfacePtr<IEventDispatcher>(application, UseDefaultIID()).forget() : nil;
}

// THE ENTRY OF THIS NAME, from the end - a name can stand in a kind's group above the last, which a query of KFC's is in - or -1.
int32 EntryOf(IStringListControlData* entries, const PMString& name)
{
	for (int32 k = (entries != nil) ? entries->Length() - 1 : -1; k >= 0; --k)
		if (entries->GetString(k) == name)
			return k;
	return -1;
}

// THE MENU SAYS THIS ENTRY, as InDesign's own load leaves it: Delete Query available (this file is the user's).
void ChooseEntry(const PMString& name)
{
	IControlView* const menu = KFCQueryFindChangeWidget(kFindQueryDropDownWidgetID);
	InterfacePtr<IStringListControlData> entries(menu, IID_ISTRINGLISTCONTROLDATA);
	InterfacePtr<IDropDownListController> chooser(menu, UseDefaultIID());
	const int32 entry = EntryOf(entries, name);
	if (chooser == nil || entry < 0)
		return;
	chooser->Select(entry, kTrue, kFalse);
	IControlView* const deleteButton = KFCQueryFindChangeWidget(kFCQueryDeleteButtonWidgetID);
	if (deleteButton != nil)
		deleteButton->Enable();
}

// THE HANDLER HAS RETURNED: the entry again, over the [Custom] the "could not load" left (a pick made since - InDesign's own - is left as it reads).
uint32 ChooseAgainProc(void* /*refPtr*/)
{
	IControlView* const menu = KFCQueryFindChangeWidget(kFindQueryDropDownWidgetID);
	InterfacePtr<IStringListControlData> entries(menu, IID_ISTRINGLISTCONTROLDATA);
	InterfacePtr<IDropDownListController> chooser(menu, UseDefaultIID());
	const bool stillCustom = (entries != nil && chooser != nil && chooser->GetSelected() == entries->Length() - 1);
	KFC_DIAG_LOG("QUERYMENU again entry=%s custom=%d", gPickedEntry.GetUTF8String().c_str(), stillCustom ? 1 : 0);
	if (stillCustom)
		ChooseEntry(gPickedEntry);
	return IIdleTask::kEndOfTime;
}

void ChooseAgainSoon(const PMString& entry)
{
	gPickedEntry = entry;
	gPickedEntry.SetTranslatable(kFalse);
	if (gTimer == nil)
		gTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (gTimer == nil)
		return;
	gTimer->StopTimer();
	gTimer->StartTimer(ChooseAgainProc, 0, nil);
}

// THE DIALOG'S SWITCHER (AddRef'd), asked up from its Query menu - or nil: no dialog stands.
ISelectableDialogSwitcher* QuerySwitcher()
{
	InterfacePtr<IWidgetParent> parent(KFCQueryFindChangeWidget(kFindQueryDropDownWidgetID), UseDefaultIID());
	return (parent != nil) ? static_cast<ISelectableDialogSwitcher*>(parent->QueryParentFor(IID_ISELECTABLEDIALOGSWITCHER)) : nil;
}

// A QUERY OF KFC'S WAS ASKED FOR. Stated on the tab; the tab shown, if the dialog stands. True = stated and the dialog stands; outEntry = its entry on the menu.
bool StateAndTurn(const KFCKohakuQueryData& query, const IDFile& file, PMString& outEntry)
{
	PMString entry;
	FileUtils::GetBaseFileName(file, entry);
	entry.SetTranslatable(kFalse);
	outEntry = entry;
	if (!KFCKohakuTab::OffersQuery(query))
	{
		// (Ruby and Kenten are a Japanese InDesign's: a file brought from one.)
		PMString said(entry);
		said.SetTranslatable(kFalse);
		said.Append(" is for a Japanese InDesign - nothing was loaded.");
		KFCResultTree::ShowStatus(said);
		return false;
	}
	KFCKohakuTab::StateQuery(query, false, 0);
	InterfacePtr<ISelectableDialogSwitcher> switcher(QuerySwitcher());
	if (switcher != nil)
	{
		const int32 index = switcher->GetCurrentPanelIndex();
		if (index < 0 || switcher->GetPanelWidgetID(index) != kKFCKohakuTabPanelWidgetID)
			switcher->SwitchDialogPanelByID(kKFCKohakuTabPanelWidgetID);
	}
	KFCPanelTitle::Update();
	KFCMirrorAppBarFieldNow();
	return switcher != nil;
}

// ...PICKED IN THE MENU: its handler goes on after the command, and says [Custom] (the header's note).
void Picked(const KFCKohakuQueryData& query, const IDFile& file)
{
	PMString entry;
	if (StateAndTurn(query, file, entry))
		ChooseAgainSoon(entry);
}

// SAVE QUERY, ON THE TAB. The name asked in KFC's dialog (it says Replace when the name stands); the file written; InDesign's menu rebuilt by its own code, as coming back to InDesign rebuilds it, and the entry chosen.
void SaveFromTab()
{
	KFCKohakuQueryData query;
	KFCKohakuTab::QueryOfFace(query);
	PMString name, why;
	if (!KFCSpecialDialogs::AskQueryName(query.mode, name, why))
	{
		if (!why.IsEmpty())
			KFCResultTree::ShowStatus(why);
		return;
	}
	IDFile file;
	PMString said;
	said.SetTranslatable(kFalse);
	if (!KFCRuns()->KohakuQueryFile(query.mode, name, file))
	{
		said.Append("Save Query: InDesign's queries' folder could not be had - nothing was saved.");
		KFCResultTree::ShowStatus(said);
		return;
	}
	PMString text;
	KFCRuns()->KohakuQueryText(query, text);
	const char* const failure = KFCWriteFileSafely(file, std::string(text.GetUTF8String()));
	PMString entry;
	FileUtils::GetBaseFileName(file, entry);
	entry.SetTranslatable(kFalse);
	KFC_DIAG_LOG("QUERYMENU saved=%d %s", (failure == nil) ? 1 : 0, entry.GetUTF8String().c_str());
	if (failure != nil)
	{
		said.Append("Save Query: the file could not be written (");
		said.Append(failure);
		said.Append(").");
		KFCResultTree::ShowStatus(said);
		return;
	}
	IControlView* const menu = KFCQueryFindChangeWidget(kFindQueryDropDownWidgetID);
	InterfacePtr<IObserver> rebuild(menu, IID_IFCAPPRESUMEOBSERVER);
	if (rebuild != nil)
		rebuild->Update(kApplicationResumeMsg, nil, IID_IFCAPPRESUMEOBSERVER, nil);
	ChooseEntry(entry);
	said.Append("Saved the query: ");
	said.Append(entry);
	said.Append(" - in Find/Change's Query menu.");
	KFCResultTree::ShowStatus(said);
}

// IS THE POINTER ON SAVE QUERY, the dialog's own window under it? Asked of where it is (a panel or a dialog over the button is not the button).
bool OnSaveQuery(IEvent* e)
{
	HWND const dialog = KFCQueryFindChangeWindow();
	IControlView* const button = KFCQueryFindChangeWidget(kFCQuerySaveButtonWidgetID);
	if (dialog == nullptr || button == nil || !button->IsVisible() || !button->IsEnabled())
		return false;
	// (Windows' question in Windows' own point - KFCPanelAlpha's; the button's box, below, in InDesign's.)
	POINT at;
	if (!::GetCursorPos(&at))
		return false;
	HWND const under = ::WindowFromPoint(at);
	if (under == nullptr || ::GetAncestor(under, GA_ROOT) != dialog)
		return false;
	const SysPoint where = ::ToSys(button->GlobalToWindow(e->GlobalWhere()));
	const SysRect box = button->GetBBox();
	return SysPointH(where) >= SysRectLeft(box) && SysPointH(where) < SysRectRight(box) && SysPointV(where) >= SysRectTop(box) && SysPointV(where) < SysRectBottom(box);
}

}	// anonymous namespace

/** THE WATCH: InDesign's menu's load of a file, seen before it runs. "Use with extreme caution from third-party client code" (ICommandInterceptor.h), and the caution: kCmdNotHandled for every other command and for every file
    that is not KFC's - and the test that runs for every command is a class comparison; nothing of a document is read; everything inside try/catch (an exception leaving here lands in InDesign's command processing). Main thread only:
    what it does is UI work. */
class KFCKohakuQueryWatch : public CPMUnknown<ICommandInterceptor>
{
public:
	KFCKohakuQueryWatch(IPMUnknown* boss) : CPMUnknown<ICommandInterceptor>(boss) {}
	virtual ~KFCKohakuQueryWatch() {}

	virtual InterceptResult InterceptProcessCommand(ICommand* cmd)
	{
		try
		{
			if (cmd == nil || ::GetClass(cmd) != kFCQueryXMLReaderCmdBoss || !IDThreading::IsMainThreadDomain())
				return kCmdNotHandled;
			// (Test builds: InDesign's reader on a file of KFC's - what a person with no KFC gets.)
			if (KFC_DIAG_FAULT("query-menu-pass"))
				return kCmdNotHandled;
			InterfacePtr<ISysFileData> fileData(cmd, UseDefaultIID());
			KFCKohakuQueryData query;
			if (fileData == nil || !KFCRuns()->ReadKohakuQuery(fileData->GetSysFile(), query))
				return kCmdNotHandled;
			InterfacePtr<IIntData> answer(cmd, UseDefaultIID());
			if (answer != nil)
				answer->Set(-1);
			KFC_DIAG_LOG("QUERYMENU picked mode=%d", static_cast<int>(query.mode));
			Picked(query, fileData->GetSysFile());
			return kCmdHandled;
		}
		catch (...)
		{
		}
		return kCmdNotHandled;
	}

	// (A scheduled command comes back through InterceptProcessCommand; the other two are deprecated - the book panel's watch says the same.)
	virtual InterceptResult InterceptScheduleCommand(ICommand* /*cmd*/)		{ return kCmdNotHandled; }
	virtual InterceptResult InterceptExecuteDynamic(ICommand* /*cmd*/)		{ return kCmdNotHandled; }
	virtual InterceptResult InterceptExecuteImmediate(ICommand* /*cmd*/)	{ return kCmdNotHandled; }
	virtual void InstallSelf()		{}
	virtual void DeinstallSelf()	{}
};
CREATE_PMINTERFACE(KFCKohakuQueryWatch, kKFCKohakuQueryWatchImpl)

/** THE FILTER (kKFCKohakuSaveFilterBoss): pushed on the application's stack while the face is shown - offered every event before anything below it (IEventDispatcher.h; KFC's Return filter is the same make). It takes the press of
    Save Query and passes on everything else. Taken at the release, where InDesign's own button runs its dialog. */
class KFCKohakuSaveFilterEH : public CEventHandler
{
public:
	KFCKohakuSaveFilterEH(IPMUnknown* boss) : CEventHandler(boss), fPressed(false) {}
	virtual ~KFCKohakuSaveFilterEH() {}

	virtual bool16 LButtonDn(IEvent* e)
	{
		fPressed = OnSaveQuery(e);
		return fPressed ? kTrue : kFalse;
	}

	virtual bool16 LButtonUp(IEvent* e)
	{
		if (!fPressed)
			return kFalse;
		fPressed = false;
		KFC_DIAG_LOG("QUERYMENU save pressed");
		if (OnSaveQuery(e))
			SaveFromTab();
		return kTrue;
	}

private:
	bool fPressed;
};
CREATE_PMINTERFACE(KFCKohakuSaveFilterEH, kKFCKohakuSaveFilterEHImpl)

void KFCKohakuQueryMenu::Startup()
{
	if (gWatch != nil || gShutDown)
		return;
	InterfacePtr<ICommandProcessor> processor(QueryProcessor());
	InterfacePtr<ICommandInterceptor> watch(::CreateObject2<ICommandInterceptor>(kKFCKohakuQueryWatchBoss));
	if (processor == nil || watch == nil)
		return;
	// The reference is handed over: the processor stores a raw pointer (the book panel's watch's note).
	watch->AddRef();
	gWatch = watch;
	processor->InstallInterceptor(watch);
}

void KFCKohakuQueryMenu::Shutdown()
{
	gShutDown = true;
	FaceHidden();
	if (gFilter != nil)
	{
		gFilter->Release();
		gFilter = nil;
	}
	if (gTimer != nil)
	{
		gTimer->StopTimer();
		gTimer->Release();
		gTimer = nil;
	}
	gPickedEntry = PMString();
	if (gWatch != nil)
	{
		InterfacePtr<ICommandProcessor> processor(QueryProcessor());
		if (processor != nil)
			processor->DeinstallInterceptor(gWatch);
		gWatch->Release();
		gWatch = nil;
	}
}

void KFCKohakuQueryMenu::FaceShown()
{
	if (gShutDown || gFilterPushed)
		return;
	if (gFilter == nil)
		gFilter = ::CreateObject2<IEventHandler>(kKFCKohakuSaveFilterBoss, IID_IEVENTHANDLER);
	InterfacePtr<IEventDispatcher> dispatcher(QueryDispatcher());
	if (gFilter == nil || dispatcher == nil)
		return;
	dispatcher->Push(gFilter, IEventDispatcher::kPushOnTop);
	gFilterPushed = true;
}

void KFCKohakuQueryMenu::FaceHidden()
{
	if (!gFilterPushed)
		return;
	InterfacePtr<IEventDispatcher> dispatcher(QueryDispatcher());
	if (dispatcher != nil)
		dispatcher->Remove(gFilter);
	gFilterPushed = false;
}

void KFCKohakuQueryMenu::ShowQuery(const KFCKohakuQueryData& query, const IDFile& file)
{
	PMString entry;
	if (StateAndTurn(query, file, entry))
		ChooseEntry(entry);
}

void KFCKohakuQueryMenu::FaceChanged()
{
	// InDesign's own message for a change on one of its tabs (FindChangeID.h: "trigger to the Query dropdown to switch to the custom query"), to the subject its tabs send it to.
	InterfacePtr<ISelectableDialogSwitcher> switcher(QuerySwitcher());
	InterfacePtr<ISubject> subject(switcher, UseDefaultIID());
	if (subject != nil)
		subject->Change(kFindChangeValueChangedMessage, IID_IFINDCHANGEMSG);
}

// End, KFCKohakuQueryMenu.cpp.
