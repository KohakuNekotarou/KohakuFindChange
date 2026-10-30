//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Table tab's face in InDesign's Find/Change dialog - see KFCTableTab.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActiveContext.h"
#include "IControlView.h"
#include "IDropDownListController.h"	// the face's lists' entries
#include "IPanelControlData.h"
#include "ISelectableDialogSwitcher.h"	// the dialog's current face, asked of the face's own parent
#include "ISession.h"
#include "IStringListControlData.h"		// the style lists' strings
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"
#include "ISubject.h"
#include "ITriStateControlData.h"		// the face's two boxes
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope, kAllDocumentScope
#include "IWidgetParent.h"
#include "IWorkspace.h"
#include "widgetid.h"					// IID_ISTRINGLISTCONTROLDATA / IID_ITRISTATECONTROLDATA

// General includes:
#include "AbstractDialogObserver.h"		// a selectable dialog face's observer (its header says so)
#include "CDialogController.h"
#include "CPanelCreator.h"
#include "TablesID.h"					// IID_ITABLESTYLEGROUPMANAGER
#include "UIDList.h"

#include <vector>

// Project includes:
#include "KFCTableTab.h"
#include "KFCUIID.h"
#include "KFCDiag.h"					// KFC_DIAG_LOG - the face's calls, in a test build's trace (TBLTAB lines)
#include "KFCPanelTitle.h"				// Update - the panel's title names the tab and its Search:
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow - the Application Bar's field shows the tab's nothing

namespace
{

bool gShown = false;		// the face is shown - from its AutoAttach to its AutoDetach
bool gClosedOnIt = false;	// the dialog closed with the face shown - until the face is shown again
bool gYield = false;		// a saved query loaded since states its own tab (YieldToSavedQuery) - until the face is shown again
KFCTableConditions gAsked;	// what the face asks, kept from its widgets (scope: Document at launch - the constructor's 0 set on first use)
bool gAskedSet = false;

// THE FULL PATHS BEHIND THE STYLE LISTS' ENTRIES (entry n = gFindPaths[n - 1] - entry 0 asks for nothing), as FillFace wrote them.
std::vector<PMString> gFindPaths;
std::vector<PMString> gChangePaths;

// Search:'s entries, in the face's order (KFCUI.fr).
const int32 kAllDocumentsEntry = 0;
const int32 kDocumentEntry = 1;

KFCTableConditions& Asked()
{
	if (!gAskedSet)
	{
		gAsked.scope = IWalkerScopeFactoryUtils::kDocumentScope;
		gAskedSet = true;
	}
	return gAsked;
}

// IS THE FACE THE DIALOG'S CURRENT ONE - asked of its own parent, the dialog's selectable switcher (KFCQRTab.cpp's SwitcherShowsFace).
bool SwitcherShowsFace(IPMUnknown* face)
{
	InterfacePtr<IWidgetParent> parent(face, UseDefaultIID());
	InterfacePtr<ISelectableDialogSwitcher> switcher((parent != nil)
		? static_cast<ISelectableDialogSwitcher*>(parent->QueryParentFor(IID_ISELECTABLEDIALOGSWITCHER)) : nil);
	if (switcher == nil)
		return false;
	const int32 index = switcher->GetCurrentPanelIndex();
	return index >= 0 && switcher->GetPanelWidgetID(index) == kKFCTableTabPanelWidgetID;
}

// One of the face's drop-downs (AddRef'd), or nil.
IDropDownListController* QueryList(IPMUnknown* face, const WidgetID& widget)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	return list.forget();
}

// Search: as the drop-down shows it, kept (an entry it does not show leaves the kept one).
void KeepScopeOf(IDropDownListController* list)
{
	const int32 entry = (list != nil) ? list->GetSelected() : -1;
	if (entry == kAllDocumentsEntry)
		Asked().scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
	else if (entry == kDocumentEntry)
		Asked().scope = IWalkerScopeFactoryUtils::kDocumentScope;
}

// A style list as the drop-down shows it, kept (entry 0 asks for nothing; an entry past the paths leaves the kept one).
void KeepStyleOf(IDropDownListController* list, const std::vector<PMString>& paths, PMString& ioStyle)
{
	const int32 entry = (list != nil) ? list->GetSelected() : -1;
	if (entry == 0)
		ioStyle.Clear();
	else if (entry > 0 && entry <= static_cast<int32>(paths.size()))
		ioStyle = paths[static_cast<size_t>(entry - 1)];
	ioStyle.SetTranslatable(kFalse);
}

// A box of the face set from what is kept - not heard (notifyOfChange kFalse).
void SetBox(IPanelControlData* panel, const WidgetID& widget, bool on)
{
	IControlView* const view = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITriStateControlData> box(view, UseDefaultIID());
	if (box == nil)
		return;
	if (on)
		box->Select(kTrue, kFalse);
	else
		box->Deselect(kTrue, kFalse);
}

// A style list written: its first entry stays (the .fr's - it is translated there), the styles after it, and the style asked for
// selected - added at the end when this document has none of that name: it is still what the face asks (a list's search goes by the
// name, in every document).
void FillStyleList(IPMUnknown* face, const WidgetID& widget, const std::vector<PMString>& paths, const std::vector<PMString>& names,
	const PMString& asked, std::vector<PMString>& outPaths)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<IStringListControlData> strings(dropDown, IID_ISTRINGLISTCONTROLDATA);
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	if (strings == nil || list == nil)
		return;
	while (strings->Length() > 1)
		strings->RemoveString(strings->Length() - 1, kFalse, kFalse);
	outPaths = paths;
	for (size_t k = 0; k < names.size(); ++k)
		strings->AddString(names[k], IStringListControlData::kEnd, kFalse, kFalse);
	int32 entry = 0;
	if (!asked.IsEmpty())
	{
		size_t at = 0;
		while (at < outPaths.size() && outPaths[at] != asked)
			++at;
		if (at == outPaths.size())
		{
			outPaths.push_back(asked);
			strings->AddString(KFCStylePathWords(asked), IStringListControlData::kEnd, kFalse, kFalse);
		}
		entry = static_cast<int32>(at) + 1;
	}
	list->Select(entry, kTrue, kFalse);
}

// THE FACE WRITTEN FROM WHAT IS KEPT AND THE FRONT DOCUMENT'S TABLE STYLES (the session's defaults with no document: the context's
// workspace). Its own widgets only - nothing is walked. Not heard (notifyOfChange kFalse).
void FillFace(IPMUnknown* face)
{
	std::vector<PMString> paths, names;
	IActiveContext* const context = GetExecutionContextSession()->GetActiveContext();
	IWorkspace* const workspace = (context != nil) ? context->GetContextWorkspace() : nil;
	InterfacePtr<IStyleGroupManager> styles(workspace, IID_ITABLESTYLEGROUPMANAGER);
	if (styles != nil)
	{
		IDataBase* const db = ::GetDataBase(styles);
		UIDList found(db);
		styles->GetRootHierarchy()->GetDescendents(&found, IID_ISTYLEINFO);
		for (int32 k = 0; k < found.Length(); ++k)
		{
			InterfacePtr<IStyleGroupHierarchy> style(db, found[k], UseDefaultIID());
			if (style == nil)
				continue;
			PMString path(style->GetFullPath(kFalse));
			path.SetTranslatable(kFalse);
			paths.push_back(path);
			names.push_back(KFCStylePathWords(style->GetFullPath(kTrue)));
		}
	}
	FillStyleList(face, kKFCTableTabFindStyleWidgetID, paths, names, Asked().findStyle, gFindPaths);
	FillStyleList(face, kKFCTableTabChangeStyleWidgetID, paths, names, Asked().changeStyle, gChangePaths);
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	SetBox(panel, kKFCTableTabOverridesOnlyWidgetID, Asked().overridesOnly);
	SetBox(panel, kKFCTableTabClearOverridesWidgetID, Asked().clearOverrides);
	InterfacePtr<IDropDownListController> scope(QueryList(face, kKFCTableTabScopeWidgetID));
	if (scope != nil)
		scope->Select((Asked().scope == IWalkerScopeFactoryUtils::kAllDocumentScope) ? kAllDocumentsEntry : kDocumentEntry, kTrue, kFalse);
	KFC_DIAG_LOG("TBLTAB filled styles=%d find=%s change=%s", static_cast<int>(names.size()),
		Asked().findStyle.GetUTF8String().c_str(), Asked().changeStyle.GetUTF8String().c_str());
}

}	// anonymous namespace

bool KFCTableTab::IsCurrent(KFCTableConditions& outConditions)
{
	outConditions = Asked();
	return !gYield && (gShown || gClosedOnIt);
}

void KFCTableTab::YieldToSavedQuery()
{
	gYield = true;
	KFC_DIAG_LOG("TBLTAB yield (a saved query states its own tab)");
}

void KFCTableTab::ShutdownCleanup()
{
	// Fresh values, so the storage goes too.
	gAsked = KFCTableConditions();
	gAskedSet = false;
	gFindPaths = std::vector<PMString>();
	gChangePaths = std::vector<PMString>();
}

//========================================================================================
// The face's three implementations - the QR Code tab's (KFCQRTab.cpp), the shape of InDesign's own faces.
//========================================================================================

/** Names the resource that describes the face: the IDList / IDListPair id, not the view's (KFCUI.fr). */
class KFCTableTabPanelCreator : public CPanelCreator
{
public:
	KFCTableTabPanelCreator(IPMUnknown* boss) : CPanelCreator(boss) {}
	virtual ~KFCTableTabPanelCreator() {}
	virtual RsrcID GetPanelRsrcID() const
	{
		return kKFCTableTabIDListRsrcID;
	}
};
CREATE_PMINTERFACE(KFCTableTabPanelCreator, kKFCTableTabPanelCreatorImpl)

/** The dialog's calls on the face: nothing to check or apply (the base's own - KFC's Find reads what the face keeps), and - as the dialog
    closes - whether it closes on this face. */
class KFCTableTabController : public CDialogController
{
public:
	KFCTableTabController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCTableTabController() {}

	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		FillFace(this);
	}

	virtual void ClosingDialog(IActiveContext* context)
	{
		// ON EVERY CLOSE, on whatever face (KFCQRTab.cpp's measurement): does it close on this one? Nothing is walked here, and nothing
		// of KFC's that walks is run from here (the header's hazard).
		gClosedOnIt = gShown || SwitcherShowsFace(this);
		KFC_DIAG_LOG("TBLTAB closing on-it=%d", gClosedOnIt ? 1 : 0);
		CDialogController::ClosingDialog(context);
	}
};
CREATE_PMINTERFACE(KFCTableTabController, kKFCTableTabControllerImpl)

/** The face's widgets heard while it is shown, and the front document: its workspace's table styles fill the lists (the shape of Adobe's
    ConditionSetDropDownObserver - the active context, regular; the workspace's list, lazy). */
class KFCTableTabObserver : public AbstractDialogObserver
{
public:
	KFCTableTabObserver(IPMUnknown* boss) : AbstractDialogObserver(boss) {}
	virtual ~KFCTableTabObserver() {}

	virtual void AutoAttach()
	{
		AbstractDialogObserver::AutoAttach();
		// THE FACE IS SHOWN: the Table tab is Find/Change's now, whatever was kept - until it is hidden.
		gShown = true;
		gClosedOnIt = false;
		gYield = false;
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel != nil)
		{
			AttachToWidget(kKFCTableTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabFindStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabOverridesOnlyWidgetID, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCTableTabClearOverridesWidgetID, IID_ITRISTATECONTROLDATA, panel);
		}
		// The front document, followed while shown (Update): its workspace's styles fill the lists, now and as they change.
		IActiveContext* const context = GetExecutionContextSession()->GetActiveContext();
		InterfacePtr<ISubject> contextSubject(context, UseDefaultIID());
		if (contextSubject != nil && !contextSubject->IsAttached(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER))
			contextSubject->AttachObserver(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER);
		fWorkspace = UIDRef::gNull;
		FollowWorkspace(context);
		KFC_DIAG_LOG("TBLTAB shown scope=%d", static_cast<int>(Asked().scope));
		// THE PANEL'S TITLE NAMES THIS TAB NOW, AND THE APPLICATION BAR'S FIELD SHOWS ITS NOTHING: no notification of Find/Change's settings
		// comes with a switch to it (KFCQRTab.cpp's measurement), so both are written from here.
		KFCPanelTitle::Update();
		KFCMirrorAppBarFieldNow();
	}

	virtual void AutoDetach()
	{
		// HIDDEN - by another face, or by the dialog's close (after its ClosingDialog). Only let go of what is attached: nothing is walked
		// here and nothing of KFC's is run from here (the header's hazard).
		AbstractDialogObserver::AutoDetach();
		gShown = false;
		KFC_DIAG_LOG("TBLTAB hidden");
		StopHearingWorkspace();
		// (The dialog can go with the application: no session, no context - nothing is attached to a context that has gone.)
		ISession* const session = GetExecutionContextSession();
		IActiveContext* const context = (session != nil) ? session->GetActiveContext() : nil;
		InterfacePtr<ISubject> contextSubject(context, UseDefaultIID());
		if (contextSubject != nil && contextSubject->IsAttached(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER))
			contextSubject->DetachObserver(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER);
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil)
			return;
		DetachFromWidget(kKFCTableTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabFindStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabOverridesOnlyWidgetID, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabClearOverridesWidgetID, IID_ITRISTATECONTROLDATA, panel);
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* /*changedBy*/)
	{
		// THE FRONT DOCUMENT MAY BE ANOTHER: its styles on the lists.
		if (protocol == IID_IACTIVECONTEXT)
		{
			InterfacePtr<IActiveContext> context(theSubject, UseDefaultIID());
			FollowWorkspace(context);
			return;
		}
		InterfacePtr<IControlView> view(theSubject, UseDefaultIID());
		const WidgetID widget = (view != nil) ? view->GetWidgetID() : kInvalidWidgetID;
		if (widget == kKFCTableTabScopeWidgetID)
		{
			// SEARCH: CHANGED - kept for KFC's Find and its menu's name, and said in the panel's title.
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			KeepScopeOf(list);
			KFC_DIAG_LOG("TBLTAB scope=%d", static_cast<int>(Asked().scope));
			KFCPanelTitle::Update();
		}
		else if (widget == kKFCTableTabFindStyleWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			KeepStyleOf(list, gFindPaths, Asked().findStyle);
			KFC_DIAG_LOG("TBLTAB find=%s", Asked().findStyle.GetUTF8String().c_str());
		}
		else if (widget == kKFCTableTabChangeStyleWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			KeepStyleOf(list, gChangePaths, Asked().changeStyle);
			KFC_DIAG_LOG("TBLTAB change=%s", Asked().changeStyle.GetUTF8String().c_str());
		}
		else if (widget == kKFCTableTabOverridesOnlyWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			Asked().overridesOnly = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("TBLTAB overrides=%d", Asked().overridesOnly ? 1 : 0);
		}
		else if (widget == kKFCTableTabClearOverridesWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			Asked().clearOverrides = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("TBLTAB clear=%d", Asked().clearOverrides ? 1 : 0);
		}
	}

	/** THE FRONT DOCUMENT'S TABLE STYLES CHANGED (the workspace's list - a style made, renamed, deleted, or an Undo of one). */
	virtual void LazyUpdate(ISubject* /*theSubject*/, const PMIID& protocol, const LazyNotificationData* /*data*/)
	{
		if (protocol == IID_ITABLESTYLEGROUPMANAGER)
			FillFace(this);
	}

private:
	// The workspace the lists are from, and heard for its table styles (IDHAObserver's protocol, in the SDK's hostadapter sample).
	void FollowWorkspace(IActiveContext* context)
	{
		IWorkspace* const workspace = (context != nil) ? context->GetContextWorkspace() : nil;
		const UIDRef now = (workspace != nil) ? ::GetUIDRef(workspace) : UIDRef::gNull;
		if (now == fWorkspace)
			return;
		StopHearingWorkspace();
		fWorkspace = now;
		InterfacePtr<ISubject> subject(workspace, UseDefaultIID());
		if (subject != nil && !subject->IsAttached(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER))
			subject->AttachObserver(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER);
		FillFace(this);
	}

	void StopHearingWorkspace()
	{
		if (fWorkspace == UIDRef::gNull)
			return;
		InterfacePtr<ISubject> subject(fWorkspace, UseDefaultIID());
		if (subject != nil && subject->IsAttached(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER))
			subject->DetachObserver(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER);
		fWorkspace = UIDRef::gNull;
	}

	UIDRef fWorkspace;
};
CREATE_PMINTERFACE(KFCTableTabObserver, kKFCTableTabObserverImpl)

// End, KFCTableTab.cpp.
