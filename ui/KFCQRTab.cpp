//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The QR Code tab's face in InDesign's Find/Change dialog - see KFCQRTab.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IDropDownListController.h"	// Search:'s entry
#include "IPanelControlData.h"
#include "ISelectableDialogSwitcher.h"	// the dialog's current face, asked of the face's own parent
#include "ISubject.h"
#include "ITextControlData.h"			// the face's line
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope, kAllDocumentScope
#include "IWidgetParent.h"
#include "widgetid.h"					// IID_IBOOLEANCONTROLDATA / IID_ISTRINGLISTCONTROLDATA and their messages

// General includes:
#include "AbstractDialogObserver.h"		// a selectable dialog face's observer (its header says so)
#include "CDialogController.h"
#include "CPanelCreator.h"

// Project includes:
#include "KFCQRTab.h"
#include "KFCUIID.h"
#include "KFCDiag.h"					// KFC_DIAG_LOG - the face's calls, in a test build's trace (QRTAB lines)
#include "KFCPanelTitle.h"				// Update - the panel's title names the tab and its Search:
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow - the Application Bar's field shows the tab's nothing
#include "KFCJump.h"					// LandOnQRCode - Find Next lands as an object row's click does
#include "KFCModelAccess.h"				// KFCRuns()->QRCodeNext - the QR code after the one found last (the model's)

namespace
{

bool gShown = false;		// the face is shown - from its AutoAttach to its AutoDetach
bool gClosedOnIt = false;	// the dialog closed with the face shown - until the face is shown again
bool gYield = false;		// a saved query loaded since states its own tab (YieldToSavedQuery) - until the face is shown again
int32 gScope = IWalkerScopeFactoryUtils::kDocumentScope;	// its Search: - Document at launch (Q19)

// Search:'s entries, in the face's order (KFCUI.fr).
const int32 kAllDocumentsEntry = 0;
const int32 kDocumentEntry = 1;

// IS THE FACE THE DIALOG'S CURRENT ONE - asked of its own parent, the dialog's selectable switcher (no window is walked:
// the header's hazard). In the face's ClosingDialog the switcher still answers the face the dialog is closing on (measured).
bool SwitcherShowsFace(IPMUnknown* face)
{
	InterfacePtr<IWidgetParent> parent(face, UseDefaultIID());
	InterfacePtr<ISelectableDialogSwitcher> switcher((parent != nil)
		? static_cast<ISelectableDialogSwitcher*>(parent->QueryParentFor(IID_ISELECTABLEDIALOGSWITCHER)) : nil);
	if (switcher == nil)
		return false;
	const int32 index = switcher->GetCurrentPanelIndex();
	return index >= 0 && switcher->GetPanelWidgetID(index) == kKFCQRTabPanelWidgetID;
}

// The face's Search: drop-down (AddRef'd), or nil.
IDropDownListController* QueryScopeList(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(kKFCQRTabScopeWidgetID) : nil;
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	return list.forget();
}

// Search: as the drop-down shows it, kept (an entry it does not show leaves the kept one).
void KeepScopeOf(IDropDownListController* list)
{
	const int32 entry = (list != nil) ? list->GetSelected() : -1;
	if (entry == kAllDocumentsEntry)
		gScope = IWalkerScopeFactoryUtils::kAllDocumentScope;
	else if (entry == kDocumentEntry)
		gScope = IWalkerScopeFactoryUtils::kDocumentScope;
}

// THE QR CODE FIND NEXT FOUND LAST - where its next press goes on from (Q10: not the selection). Kept only while the
// face is shown - let go when it is shown again and when the dialog closes: a closed document's database address can
// be a document opened later (memory uidref-reuse-after-close), and a QR code of the same UID there would be taken for
// it. NOT let go when a document closes and another opens while the face stays shown - tried once, the address was
// not reused (docs/ai-notes/kfc-qr-recheck-2026-10-28.md O21-1).
UIDRef gLastFound;

// One of the face's lines, written.
void SetFaceLine(IPanelControlData* panel, const WidgetID& widget, const PMString& words)
{
	IControlView* const line = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITextControlData> text(line, UseDefaultIID());
	if (text != nil)
		text->SetString(words);
}

// The face's two lines - what Find Next found (or that there is none, or why it did not look), and why it could not select
// it (empty when it did): a line each, so a long content cuts its own line and never the reason (KFCUI.fr).
void SayOnFace(IPMUnknown* face, const PMString& found, const PMString& note)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	SetFaceLine(panel, kKFCQRTabStatusWidgetID, found);
	SetFaceLine(panel, kKFCQRTabNoteWidgetID, note);
}

// FIND NEXT (the design's Q10 / D5): the QR code after the one found last, in the list's own order over the tab's
// Search: (the model's KFCQRSearch::Next - read only), landed on as an object row's click lands (KFCJump::LandOnQRCode) -
// one it cannot select is named on the face's second line, with no alert (Q10); after the last, the first again,
// silently.
void FindNext(IPMUnknown* face)
{
	UIDRef next;
	int32 reach = 0;
	PMString words, note;
	words.SetTranslatable(kFalse);
	note.SetTranslatable(kFalse);
	// NOT WHILE A RUN OF THE MODEL'S IS UNDER WAY (KFCRunGuard - a row's click is turned away the same, KFCJump::ActivateNode):
	// it can be standing inside its own message loop, with a chapter it opened in hand.
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayOnFace(face, words, note);
		return;
	}
	if (KFCRuns()->QRCodeNext(gLastFound, next, reach, words))
	{
		gLastFound = next;
		(void)KFCJump::LandOnQRCode(next, reach, note);
	}
	// (Test builds: both lines in one - what the face shows, the reason after two spaces.)
	KFC_DIAG_LOG("QRTAB findnext %s%s%s", words.GetUTF8String().c_str(), note.IsEmpty() ? "" : "  ",
		note.GetUTF8String().c_str());
	SayOnFace(face, words, note);
}

}	// anonymous namespace

bool KFCQRTab::IsCurrent(int32& outScope)
{
	outScope = gScope;
	return !gYield && (gShown || gClosedOnIt);
}

void KFCQRTab::YieldToSavedQuery()
{
	gYield = true;
	KFC_DIAG_LOG("QRTAB yield (a saved query states its own tab)");
}

//========================================================================================
// The face's three implementations - the shape of InDesign's own faces (kFindMiniTabbedDialogTextPanelBoss and its
// siblings in the 20.5 dump; KT measured the same three).
//========================================================================================

/** Names the resource that describes the face: the IDList / IDListPair id, not the view's (KFCUI.fr). */
class KFCQRTabPanelCreator : public CPanelCreator
{
public:
	KFCQRTabPanelCreator(IPMUnknown* boss) : CPanelCreator(boss) {}
	virtual ~KFCQRTabPanelCreator() {}
	virtual RsrcID GetPanelRsrcID() const
	{
		return kKFCQRTabIDListRsrcID;
	}
};
CREATE_PMINTERFACE(KFCQRTabPanelCreator, kKFCQRTabPanelCreatorImpl)

/** The dialog's calls on the face: its Search: put in when it is first shown, nothing to check or apply (the base's
    own - KFC's Find reads the face as it stands), and - as the dialog closes - whether it closes on this face. */
class KFCQRTabController : public CDialogController
{
public:
	KFCQRTabController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCQRTabController() {}

	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		// SEARCH: AS KEPT - Document at launch (Q19), the session's choice after; not heard (notifyOfChange kFalse).
		InterfacePtr<IDropDownListController> list(QueryScopeList(this));
		if (list != nil)
			list->Select((gScope == IWalkerScopeFactoryUtils::kAllDocumentScope) ? kAllDocumentsEntry : kDocumentEntry, kTrue, kFalse);
	}

	virtual void ClosingDialog(IActiveContext* context)
	{
		// ON EVERY CLOSE, on whatever face (measured): does it close on this one - shown now, or so its switcher says? The
		// dialog opens on it again, so KFC's Find goes on listing QR codes (D1). Nothing is walked here, and nothing of
		// KFC's that walks is run from here (the header's hazard) - the panel's title already names this tab.
		gClosedOnIt = gShown || SwitcherShowsFace(this);
		gLastFound = UIDRef();		// Find Next starts again at the next open (gLastFound's note)
		KFC_DIAG_LOG("QRTAB closing on-it=%d", gClosedOnIt ? 1 : 0);
		CDialogController::ClosingDialog(context);
	}
};
CREATE_PMINTERFACE(KFCQRTabController, kKFCQRTabControllerImpl)

/** The face's widgets heard while it is shown - Search: and Find Next (AbstractDialogObserver: a selectable dialog face's
    base, by its header). */
class KFCQRTabObserver : public AbstractDialogObserver
{
public:
	KFCQRTabObserver(IPMUnknown* boss) : AbstractDialogObserver(boss) {}
	virtual ~KFCQRTabObserver() {}

	virtual void AutoAttach()
	{
		// The base first, as the SDK's face on a dialog of InDesign's does (xdocbookworkflowui's XDocBkUIDialogObserver) -
		// CObserver's, which does nothing (publiclib's CObserver.cpp).
		AbstractDialogObserver::AutoAttach();
		// THE FACE IS SHOWN: the QR Code tab is Find/Change's now, whatever was kept - a close on it, or a saved query's
		// own tab (YieldToSavedQuery) - until it is hidden.
		gShown = true;
		gClosedOnIt = false;
		gYield = false;
		gLastFound = UIDRef();		// Find Next starts from the first again (gLastFound's note)
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel != nil)
		{
			AttachToWidget(kKFCQRTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCQRTabFindNextWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		}
		InterfacePtr<IDropDownListController> list(QueryScopeList(this));
		KeepScopeOf(list);
		KFC_DIAG_LOG("QRTAB shown scope=%d", static_cast<int>(gScope));
		// THE PANEL'S TITLE NAMES THIS TAB NOW, AND THE APPLICATION BAR'S FIELD SHOWS ITS NOTHING (Q17): no notification of
		// Find/Change's settings comes with a switch to it (measured), so both are written from here.
		KFCPanelTitle::Update();
		KFCMirrorAppBarFieldNow();
	}

	virtual void AutoDetach()
	{
		// HIDDEN - by another face, or by the dialog's close (after its ClosingDialog). Only let go of the widgets: nothing is
		// walked here and nothing of KFC's is run from here (the header's hazard). A switch to InDesign's own face writes the
		// panel's title through Find/Change's own notifications.
		AbstractDialogObserver::AutoDetach();		// (the base first, as AutoAttach's)
		gShown = false;
		KFC_DIAG_LOG("QRTAB hidden");
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil)
			return;
		DetachFromWidget(kKFCQRTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCQRTabFindNextWidgetID, IID_IBOOLEANCONTROLDATA, panel);
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& /*protocol*/, void* /*changedBy*/)
	{
		InterfacePtr<IControlView> view(theSubject, UseDefaultIID());
		const WidgetID widget = (view != nil) ? view->GetWidgetID() : kInvalidWidgetID;
		if (widget == kKFCQRTabScopeWidgetID)
		{
			// SEARCH: CHANGED - kept for KFC's Find and its menu's name, and said in the panel's title.
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			KeepScopeOf(list);
			KFC_DIAG_LOG("QRTAB scope=%d", static_cast<int>(gScope));
			KFCPanelTitle::Update();
		}
		else if (widget == kKFCQRTabFindNextWidgetID && theChange == kTrueStateMessage)
			FindNext(this);
	}
};
CREATE_PMINTERFACE(KFCQRTabObserver, kKFCQRTabObserverImpl)

// End, KFCQRTab.cpp.
