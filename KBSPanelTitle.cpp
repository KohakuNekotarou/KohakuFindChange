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
#include "IPanelMgr.h"			// GetPanelFromWidgetID / GetPaletteRefContainingPanel
#include "ISession.h"

// General includes:
#include "CObserver.h"			// the panel-boss observer that writes the tab on show
#include "PaletteRefUtils.h"	// SetPaletteLabel - the tab's own label
#include "PMString.h"

// Project includes:
#include "KBSID.h"
#include "KBSBookScope.h"		// IsBookScopeOn - the only input to the name
#include "KBSPanelTitle.h"

namespace
{

// The plain panel name, exactly as it reads in KBS_enUS.fr / KBS_jaJP.fr under kKBSPanelTitleKey.
// It has to be repeated here because there is no way to read it back at run time:
// PaletteRefUtils::GetPaletteLabel returns an empty string for a palette that has never been
// visible, and IWindow::GetTitle only ever hands back the last value SET (both stated in the
// headers). So "remember the old name, restore it later" is not available - the original must be a
// literal on this side too. Keep the three copies in step.
const char* const kKBSPlainPanelName = "Kohaku Search Panel";

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
	// "Doc", not "Document": a tab is narrow and truncates, and the pair only has to be told apart
	// from each other.
	title.Append(KBSBookScope::IsBookScopeOn() ? "Book" : "Doc");
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

/** Observer on kKBSPanelWidgetBoss. Its only job is the tab: the panel's widgets are built fresh
    every time it is shown, and a palette is only laid out while it is visible - so opening the
    panel is the moment the scope has to be written onto the tab. Without this the tab reads the
    plain name until the user happens to toggle the scope or run a search.

    Nothing is subscribed to, so Update is never called. */
class KBSPanelObserver : public CObserver
{
public:
	KBSPanelObserver(IPMUnknown* boss) : CObserver(boss) {}
	virtual ~KBSPanelObserver() {}

	virtual void AutoAttach() { KBSPanelTitle::Update(); }
	virtual void AutoDetach() {}
	virtual void Update(const ClassID& /*theChange*/, ISubject* /*theSubject*/, const PMIID& /*protocol*/, void* /*changedBy*/) {}
};

CREATE_PMINTERFACE(KBSPanelObserver, kKBSPanelObserverImpl)

// End, KBSPanelTitle.cpp.
