//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Panel tab name. See KBSPanelTitle.h for the contract.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager
#include "IControlView.h"		// a panel IS a control view - what GetPanelFromWidgetID hands back
#include "IPanelControlData.h"	// FindWidget - reaching the illustration inside the panel
#include "IPanelMgr.h"			// GetPanelFromWidgetID / GetPaletteRefContainingPanel
#include "ISession.h"
#include "ISubject.h"			// AttachObserver / DetachObserver on the illustration
#include "ITriStateControlData.h"	// the protocol a button announces its click on

// General includes:
#include "CObserver.h"			// the panel-boss observer that writes the tab on show
#include "PaletteRefUtils.h"	// SetPaletteLabel - the tab's own label
#include "PMString.h"

// The plug-in's home page is opened through InDesign's own hyperlink plumbing rather than any OS
// call of ours. GoToURL is PUBLIC_DECL, so no boss and no IID are needed to reach it.
//
// !! It is declared HERE rather than by including URLUtils.h, because that header is wrong: it puts
//   GoToURL in "namespace URLUtils", while the exported symbol is
//   "?GoToURL@GoToURLUtils@@YAXAEBVPMString@@F@Z" = GoToURLUtils::GoToURL(const PMString&, bool16).
//   Including the header compiles and then fails to link. KESCM carries the same declaration for
//   the same reason (KESCMActionComponent.cpp).
namespace GoToURLUtils
{
	PUBLIC_DECL void GoToURL(const PMString& goToURL, bool16 isAGoURL);
}

// Project includes:
#include "KBSID.h"
#include "KBSBookScope.h"		// IsBookScopeOn - the only input to the name
#include "KBSPanelIcon.h"		// which illustration is showing, and which widgets are illustrations
#include "KBSPanelAlpha.h"		// re-apply "Translucent Panel" when the panel is shown again
#include "KBSPanelTitle.h"
#include "KBSResultTree.h"		// RestoreStatusOnPanelShow - the message the workspace persisted

namespace
{

// The plain panel name. It has to be spelled out on THIS side, not read back from the panel, because
// there is no way to read it back at run time: PaletteRefUtils::GetPaletteLabel returns an empty
// string for a palette that has never been visible, and IWindow::GetTitle only ever hands back the
// last value SET (both stated in the headers). So "remember the old name, restore it later" is not
// available.
//
// It is kKBSDisplayName rather than a literal of its own: that macro is the ONE definition of the
// display name - both string tables put it under kKBSPanelTitleKey and the .rc builds its
// FileDescription from it - so this cannot drift out of step with the name the panel came up with,
// which three separate literals could.
const char* const kKBSPlainPanelName = kKBSDisplayName;

/** Put a label on the panel's tab. Does nothing unless the panel exists and sits in a palette. */
void SetTabLabel(const PMString& label)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	if (app == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;

	// Non-owning - a Get, not a Query. nil until the panel has been opened once, which is the
	// ordinary state at startup and the reason every caller may fire blindly.
	IControlView* panelView = panelMgr->GetPanelFromWidgetID(kKBSPanelWidgetID);
	if (panelView == nil)
		return;

	// The label belongs to the CONTAINER, not to the panel: for a regular tabbed palette that is
	// the kTabPanelContainerType which draws the tab (IPanelMgr.h:197), and SetPaletteLabel is
	// documented as valid only for one of those.
	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
	if (!container.IsValid())
		return;

	PaletteRefUtils::SetPaletteLabel(container, label, PaletteRefUtils::kTitle_PanelLabel);
}

}

void KBSPanelTitle::Update()
{
	// A plain ASCII hyphen, not an em dash: on a tab this size the long dash reads as a gap
	// (user's call 2026-07-28). Staying inside ASCII also keeps this file free of the CP932
	// mangling a non-ASCII literal in a BOM-less .cpp would bring.
	PMString title(kKBSPlainPanelName);
	title.Append(" - ");
	// The whole word, not the "Doc" this used to say (user's call 2026-08-01). A tab is narrow and
	// truncates, which is why it was shortened in the first place, but the name in front of it lost
	// a word in the same rename and can carry the longer one now.
	title.Append(KBSBookScope::IsBookScopeOn() ? "Book" : "Document");
	// A palette label is a candidate translation key like any other UI string, so an untranslated
	// name would be swapped for whatever the string table happens to hold under it.
	title.SetTranslatable(kFalse);

	SetTabLabel(title);
}

void KBSPanelTitle::Restore()
{
	PMString title(kKBSPlainPanelName);
	title.SetTranslatable(kFalse);

	SetTabLabel(title);
}

namespace
{

/** Attach to (or detach from) one of the panel's own widgets on the protocol it reports clicks on.
    Silently does nothing when the widget is not there, which is the ordinary state while the panel
    is being torn down. */
void AttachToWidget(IPMUnknown* panelBoss, IObserver* observer, const WidgetID& widgetID, bool attach)
{
	InterfacePtr<IPanelControlData> panelData(panelBoss, UseDefaultIID());
	if (panelData == nil)
		return;

	IControlView* view = panelData->FindWidget(widgetID);
	if (view == nil)
		return;

	InterfacePtr<ISubject> subject(view, UseDefaultIID());
	if (subject == nil)
		return;

	if (attach)
		subject->AttachObserver(observer, ITriStateControlData::kDefaultIID);
	else
		subject->DetachObserver(observer, ITriStateControlData::kDefaultIID);
}

}

/** Observer on kKBSPanelWidgetBoss. Three jobs, all tied to the panel being shown:

      * The TAB's name. The panel's widgets are built fresh every time it is shown, and a palette is
        only laid out while it is visible - so opening the panel is the moment the scope has to be
        written onto the tab. Without this the tab reads the plain name until the user happens to
        toggle the scope or run a search.

      * The STATUS LINE. Every read-out on a panel has to be written here, because a widget's string
        is persisted in the workspace: left alone, the line comes back reading whatever the last
        session put there. See KBSResultTree::RestoreStatusOnPanelShow.

      * The ILLUSTRATION's click. The picture beside the message opens the plug-in's home page (the
        URL its tooltip shows). A button announces a click to whoever is listening on
        ITriStateControlData, and this observer - already on the panel boss, already living exactly
        as long as the widgets do - is who listens. */
class KBSPanelObserver : public CObserver
{
public:
	KBSPanelObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KBSPanelObserver() {}

	virtual void AutoAttach()
	{
		KBSPanelTitle::Update();

		// The widgets are built fresh every time the panel is shown, so the picture that belongs on
		// screen has to be written on NOW - the .fr's visible flags are only a starting point.
		KBSPanelIcon::Update();

		// ...and for the same reason, so does the message. The .fr's initial text is used the first
		// time the panel is ever built and never again: after that the widget carries whatever the
		// workspace remembers, which after a restart is a message about results that no longer
		// exist (reported 2026-08-02).
		KBSResultTree::RestoreStatusOnPanelShow();

		for (int32 i = 0; i < KBSPanelIcon::Count(); ++i)
			AttachToWidget(this, this, KBSPanelIcon::NthWidgetID(i), true);

		// *At startup (KBSStartupShutdown::Startup) the panel manager may not have come up yet, in
		// which case the subscription failed - so it is tried again here. IsAttached guards it, so
		// this cannot subscribe twice.
		KBSAttachPanelVisibilityObserver();

		// ...and if "Translucent Panel" is ON, put the alpha back: re-opening the panel gives it a
		// different top-level window (OWL.Dock), which is what the alpha was on. Safe to call
		// unconditionally - OFF, docked and Mac are all rejected inside.
		// *This is the safety net; the following is really done by the observer in KBSPanelAlpha.cpp
		//   (kPaletteVisibilityChangedMessage).
		//   *Note: AutoAttach runs every time the widgets are rebuilt, so it is no place to write a
		//   fixed default - it only reflects whatever KBSGetPanelTranslucent currently says.
		KBSApplyPanelTranslucency();
	}

	virtual void AutoDetach()
	{
		for (int32 i = 0; i < KBSPanelIcon::Count(); ++i)
			AttachToWidget(this, this, KBSPanelIcon::NthWidgetID(i), false);
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& /*protocol*/, void* /*changedBy*/)
	{
		// kTrueStateMessage is the button going down; anything else here is not a click.
		if (theChange != kTrueStateMessage || theSubject == nil)
			return;

		// Any of the illustrations - they are alternatives showing the same picture's worth of
		// information, and all of them lead to the same place.
		InterfacePtr<IControlView> view(theSubject, UseDefaultIID());
		if (view == nil || !KBSPanelIcon::IsIconWidget(view->GetWidgetID()))
			return;

		// Nothing in the document is touched - this is a request to the OS to open a browser - so
		// there is no command and nothing to undo.
		PMString url(kKBSRepoURL);
		url.SetTranslatable(kFalse);
		GoToURLUtils::GoToURL(url, kFalse);
	}
};

CREATE_PMINTERFACE(KBSPanelObserver, kKBSPanelObserverImpl)

// End, KBSPanelTitle.cpp.
