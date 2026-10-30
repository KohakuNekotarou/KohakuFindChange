//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Ruby tab's face in InDesign's Find/Change dialog - see KFCRubyTab.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IDropDownListController.h"	// the face's lists' entries
#include "IPanelControlData.h"
#include "ISelectableDialogSwitcher.h"	// the dialog's current face, asked of the face's own parent
#include "ISubject.h"
#include "ITextControlData.h"			// the face's text fields
#include "ITriStateControlData.h"		// Remove Ruby
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope, kAllDocumentScope
#include "IWidgetParent.h"
#include "widgetid.h"					// IID_ISTRINGLISTCONTROLDATA / IID_ITRISTATECONTROLDATA / IID_ITEXTCONTROLDATA

// General includes:
#include "AbstractDialogObserver.h"		// a selectable dialog face's observer (its header says so)
#include "CDialogController.h"
#include "CPanelCreator.h"

// Project includes:
#include "KFCRubyTab.h"
#include "KFCUIID.h"
#include "KFCDiag.h"					// KFC_DIAG_LOG - the face's calls, in a test build's trace (RUBYTAB lines)
#include "KFCPanelTitle.h"				// Update - the panel's title names the tab and its Search:
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow - the Application Bar's field shows the tab's nothing

namespace
{

bool gShown = false;		// the face is shown - from its AutoAttach to its AutoDetach
bool gClosedOnIt = false;	// the dialog closed with the face shown - until the face is shown again
bool gYield = false;		// a saved query loaded since states its own tab (YieldToSavedQuery) - until the face is shown again
KFCRubyConditions gAsked;	// what the face asks, kept from its widgets
bool gAskedSet = false;

// The drop-downs' entries, in the face's order (KFCUI.fr).
const int32 kAllDocumentsEntry = 0;
const int32 kDocumentEntry = 1;
const int32 kWithRubyEntry = 0;
const int32 kWithoutRubyEntry = 1;

KFCRubyConditions& Asked()
{
	if (!gAskedSet)
	{
		gAsked.scope = IWalkerScopeFactoryUtils::kDocumentScope;
		gAskedSet = true;
	}
	return gAsked;
}

// IS THE FACE THE DIALOG'S CURRENT ONE - asked of its own parent (KFCQRTab.cpp's SwitcherShowsFace).
bool SwitcherShowsFace(IPMUnknown* face)
{
	InterfacePtr<IWidgetParent> parent(face, UseDefaultIID());
	InterfacePtr<ISelectableDialogSwitcher> switcher((parent != nil)
		? static_cast<ISelectableDialogSwitcher*>(parent->QueryParentFor(IID_ISELECTABLEDIALOGSWITCHER)) : nil);
	if (switcher == nil)
		return false;
	const int32 index = switcher->GetCurrentPanelIndex();
	return index >= 0 && switcher->GetPanelWidgetID(index) == kKFCRubyTabPanelWidgetID;
}

IDropDownListController* QueryList(IPMUnknown* face, const WidgetID& widget)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	return list.forget();
}

void SetField(IPanelControlData* panel, const WidgetID& widget, const PMString& words)
{
	IControlView* const view = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITextControlData> field(view, UseDefaultIID());
	if (field != nil)
		field->SetString(words, kTrue, kFalse);
}

// Ruby: means nothing to Text Without Ruby: grey there.
void ShowKind(IPanelControlData* panel)
{
	IControlView* const reading = (panel != nil) ? panel->FindWidget(kKFCRubyTabReadingWidgetID) : nil;
	if (reading != nil)
		reading->Enable(Asked().withoutRuby ? kFalse : kTrue);
}

// THE FACE WRITTEN FROM WHAT IS KEPT. Its own widgets only - nothing is walked. Not heard (notifyOfChange kFalse).
void FillFace(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	InterfacePtr<IDropDownListController> scope(QueryList(face, kKFCRubyTabScopeWidgetID));
	if (scope != nil)
		scope->Select((Asked().scope == IWalkerScopeFactoryUtils::kAllDocumentScope) ? kAllDocumentsEntry : kDocumentEntry, kTrue, kFalse);
	InterfacePtr<IDropDownListController> kind(QueryList(face, kKFCRubyTabKindWidgetID));
	if (kind != nil)
		kind->Select(Asked().withoutRuby ? kWithoutRubyEntry : kWithRubyEntry, kTrue, kFalse);
	SetField(panel, kKFCRubyTabBaseWidgetID, Asked().base);
	SetField(panel, kKFCRubyTabReadingWidgetID, Asked().reading);
	SetField(panel, kKFCRubyTabChangeWidgetID, Asked().changeTo);
	IControlView* const remove = (panel != nil) ? panel->FindWidget(kKFCRubyTabRemoveWidgetID) : nil;
	InterfacePtr<ITriStateControlData> box(remove, UseDefaultIID());
	if (box != nil)
	{
		if (Asked().removeRuby)
			box->Select(kTrue, kFalse);
		else
			box->Deselect(kTrue, kFalse);
	}
	ShowKind(panel);
}

// A text field's words as they stand now, not translated.
PMString WordsOf(IControlView* view)
{
	PMString words;
	InterfacePtr<ITextControlData> field(view, UseDefaultIID());
	if (field != nil)
		words = field->GetString();
	words.SetTranslatable(kFalse);
	return words;
}

}	// anonymous namespace

bool KFCRubyTab::IsCurrent(KFCRubyConditions& outConditions)
{
	outConditions = Asked();
	return !gYield && (gShown || gClosedOnIt);
}

void KFCRubyTab::YieldToSavedQuery()
{
	gYield = true;
	KFC_DIAG_LOG("RUBYTAB yield (a saved query states its own tab)");
}

void KFCRubyTab::ShutdownCleanup()
{
	// A fresh value, so the storage goes too.
	gAsked = KFCRubyConditions();
	gAskedSet = false;
}

//========================================================================================
// The face's three implementations - the QR Code tab's (KFCQRTab.cpp), the shape of InDesign's own faces.
//========================================================================================

/** Names the resource that describes the face: the IDList / IDListPair id, not the view's (KFCUI.fr). */
class KFCRubyTabPanelCreator : public CPanelCreator
{
public:
	KFCRubyTabPanelCreator(IPMUnknown* boss) : CPanelCreator(boss) {}
	virtual ~KFCRubyTabPanelCreator() {}
	virtual RsrcID GetPanelRsrcID() const
	{
		return kKFCRubyTabIDListRsrcID;
	}
};
CREATE_PMINTERFACE(KFCRubyTabPanelCreator, kKFCRubyTabPanelCreatorImpl)

/** The dialog's calls on the face: nothing to check or apply (the base's own - KFC's Find reads what the face keeps), and - as the dialog closes - whether it closes on this face. */
class KFCRubyTabController : public CDialogController
{
public:
	KFCRubyTabController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCRubyTabController() {}

	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		FillFace(this);
	}

	virtual void ClosingDialog(IActiveContext* context)
	{
		// ON EVERY CLOSE, on whatever face (KFCQRTab.cpp's measurement): does it close on this one? Nothing is walked here, and nothing of KFC's that walks is run from here.
		gClosedOnIt = gShown || SwitcherShowsFace(this);
		KFC_DIAG_LOG("RUBYTAB closing on-it=%d", gClosedOnIt ? 1 : 0);
		CDialogController::ClosingDialog(context);
	}
};
CREATE_PMINTERFACE(KFCRubyTabController, kKFCRubyTabControllerImpl)

/** The face's widgets heard while it is shown. */
class KFCRubyTabObserver : public AbstractDialogObserver
{
public:
	KFCRubyTabObserver(IPMUnknown* boss) : AbstractDialogObserver(boss) {}
	virtual ~KFCRubyTabObserver() {}

	virtual void AutoAttach()
	{
		AbstractDialogObserver::AutoAttach();
		// THE FACE IS SHOWN: the Ruby tab is Find/Change's now, whatever was kept - until it is hidden.
		gShown = true;
		gClosedOnIt = false;
		gYield = false;
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel != nil)
		{
			AttachToWidget(kKFCRubyTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabBaseWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabReadingWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabChangeWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabRemoveWidgetID, IID_ITRISTATECONTROLDATA, panel);
		}
		FillFace(this);
		KFC_DIAG_LOG("RUBYTAB shown scope=%d", static_cast<int>(Asked().scope));
		// THE PANEL'S TITLE NAMES THIS TAB NOW, AND THE APPLICATION BAR'S FIELD SHOWS ITS NOTHING (KFCQRTab.cpp's measurement).
		KFCPanelTitle::Update();
		KFCMirrorAppBarFieldNow();
	}

	virtual void AutoDetach()
	{
		// HIDDEN - by another face, or by the dialog's close (after its ClosingDialog). Only let go of what is attached.
		AbstractDialogObserver::AutoDetach();
		gShown = false;
		KFC_DIAG_LOG("RUBYTAB hidden");
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil)
			return;
		DetachFromWidget(kKFCRubyTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabBaseWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabReadingWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabChangeWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabRemoveWidgetID, IID_ITRISTATECONTROLDATA, panel);
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& /*protocol*/, void* /*changedBy*/)
	{
		InterfacePtr<IControlView> view(theSubject, UseDefaultIID());
		const WidgetID widget = (view != nil) ? view->GetWidgetID() : kInvalidWidgetID;
		if (widget == kKFCRubyTabScopeWidgetID)
		{
			// SEARCH: CHANGED - kept for KFC's Find and its menu's name, and said in the panel's title.
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry == kAllDocumentsEntry)
				Asked().scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
			else if (entry == kDocumentEntry)
				Asked().scope = IWalkerScopeFactoryUtils::kDocumentScope;
			KFC_DIAG_LOG("RUBYTAB scope=%d", static_cast<int>(Asked().scope));
			KFCPanelTitle::Update();
		}
		else if (widget == kKFCRubyTabKindWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry == kWithRubyEntry || entry == kWithoutRubyEntry)
				Asked().withoutRuby = (entry == kWithoutRubyEntry);
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowKind(panel);
			KFC_DIAG_LOG("RUBYTAB without=%d", Asked().withoutRuby ? 1 : 0);
		}
		else if (widget == kKFCRubyTabBaseWidgetID)
		{
			Asked().base = WordsOf(view);
			KFC_DIAG_LOG("RUBYTAB base=%s", Asked().base.GetUTF8String().c_str());
		}
		else if (widget == kKFCRubyTabReadingWidgetID)
		{
			Asked().reading = WordsOf(view);
			KFC_DIAG_LOG("RUBYTAB reading=%s", Asked().reading.GetUTF8String().c_str());
		}
		else if (widget == kKFCRubyTabChangeWidgetID)
		{
			Asked().changeTo = WordsOf(view);
			KFC_DIAG_LOG("RUBYTAB change=%s", Asked().changeTo.GetUTF8String().c_str());
		}
		else if (widget == kKFCRubyTabRemoveWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			Asked().removeRuby = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("RUBYTAB remove=%d", Asked().removeRuby ? 1 : 0);
		}
	}
};
CREATE_PMINTERFACE(KFCRubyTabObserver, kKFCRubyTabObserverImpl)

// End, KFCRubyTab.cpp.
