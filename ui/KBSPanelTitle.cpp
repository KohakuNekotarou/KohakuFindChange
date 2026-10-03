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
#include "IFindChangeOptions.h"	// the Find/Change settings - their subject is where their changes arrive
#include "TextWalkerServiceProviderID.h"	// IID_IFINDCHANGEOPTIONS - the protocol their changes arrive on
#include "ISubject.h"			// AttachObserver / DetachObserver on the illustration
#include "ITriStateControlData.h"	// the protocol a button announces its click on

// General includes:
#include "SelectionObserver.h"	// ActiveSelectionObserver - the panel-boss observer, which follows the selection too
#include "PaletteRefUtils.h"	// GetPaletteLabel / SetPaletteLabel - the tab's own label
#include "PMString.h"
#include "PreferenceUtils.h"	// QuerySessionPreferences - the Find/Change settings, as the engine reads them

// The plug-in's home page is opened through InDesign's own hyperlink plumbing rather than any OS
// call of ours. GoToURL is PUBLIC_DECL, so no boss and no IID are needed to reach it.
//
// !! It is declared HERE rather than by including URLUtils.h, because that header is wrong: it puts
//   GoToURL in "namespace URLUtils", while the exported symbol is
//   "?GoToURL@GoToURLUtils@@YAXAEBVPMString@@F@Z" = GoToURLUtils::GoToURL(const PMString&, bool16).
//   Including the header compiles and then fails to link. KCM carries the same declaration for
//   the same reason (KCMActionComponent.cpp).
namespace GoToURLUtils
{
	PUBLIC_DECL void GoToURL(const PMString& goToURL, bool16 isAGoURL);
}

// Project includes:
#include "KFCUIID.h"
#include "KBSModelAccess.h"		// the model half, through its session interfaces (2026-10-01, the model/UI split)
#include "KBSPanelIcon.h"		// which illustration is showing, and which widgets are illustrations
#include "KBSPanelAlpha.h"		// re-apply "Translucent Panel" when the panel is shown again
#include "KBSPanelMetrics.h"	// how tall the message block has to be in this UI language
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
// display name - the UI half's string table (KFCUI_enUS.fr) puts it under kKBSPanelTitleKey and the
// model half's .rc builds its FileDescription from it - so this cannot drift out of step with the name
// the panel came up with, which three separate literals could.
const char* const kKBSPlainPanelName = kKBSDisplayName;

/** Put a label on the panel's tab. Does nothing unless the panel exists and sits in a palette. */
void SetTabLabel(const PMString& label)
{
	// ***** THE SESSION CAN BE GONE DURING SHUTDOWN. ***** (2026-08-11.)
	//   This used to write GetExecutionContextSession()->QueryApplication() straight out, which
	//   dereferences whatever that call returns. KBSPanelAlpha.cpp says of the same function
	//   "can be nil during shutdown" and takes the pointer into a variable before using it.
	//   *Why this one matters and the dozen others do not: KBSPanelTitle::Restore is called from
	//    the UI half's shutdown (KBSUIStartupShutdown) - it is the only entry here that runs during teardown.
	//    The rest of this plug-in reaches the session from inside UI events (a click, a key, a
	//    draw), where it is certainly alive.
	//   !No failure has been seen: the tab is restored FIRST in Shutdown, "while the UI is still
	//    standing", and the teardown test passes. This is the Shutdown path being made to look
	//    like the other Shutdown path rather than a fix for an observed crash.
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return;

	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(app->QueryPanelManager());
	if (panelMgr == nil)
		return;

	// Non-owning - a Get, not a Query. nil until the panel has been opened once, which is the
	// ordinary state at startup and the reason every caller may fire blindly.
	//
	// ***** WHY THIS DOOR AND NOT THE VISIBLE-ONLY ONES. ***** The rest of the plug-in reaches the
	// panel through Utils<IPalettePanelUtils>()->QueryPanelByWidgetID (KBSPanelIcon.cpp), which
	// hands back nil for a panel that is not on screen, and IPanelMgr has GetVisiblePanel
	// (:115-123) for the same reason - the manager purges panels that are not shown and will not
	// give out pointers to them. That is right for anything that WRITES INTO the panel: nothing
	// there needs doing while it cannot be seen, and its next show rebuilds it anyway.
	//
	// A TAB NAME is the one thing that is not like that. It belongs to the palette, not to the
	// panel's contents, and it stays on screen when the panel's contents do not: a minimized
	// palette counts as not shown (IPanelMgr.h:157-159 says so outright for
	// IsPanelWithWidgetIDShown), and a minimized palette is exactly a strip of tab names. The
	// scope can be toggled from the flyout with the palette in that state, so a visible-only door
	// would refuse the case this function exists for.
	// *Sibling: KESCL picks between the two per call site for the same kind of reason, and says why at
	//  each (KESCLReportPanel::RefreshIfShowing takes the visible-only door, ::SelectFirstHitRow the
	//  other - named, not numbered: a line number in our own code is wrong after the next edit).
	IControlView* panelView = panelMgr->GetPanelFromWidgetID(kKBSPanelWidgetID);
	if (panelView == nil)
		return;

	// The label belongs to the CONTAINER, not to the panel: for a regular tabbed palette that is
	// the kTabPanelContainerType which draws the tab (IPanelMgr.h:197), and SetPaletteLabel is
	// documented as valid only for one of those.
	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
	if (!container.IsValid())
		return;

	// ***** NOT WRITTEN AGAIN WHEN IT IS ALREADY THERE (2026-10-03). ***** Since the observer follows the
	// selection (KBSPanelObserver below), this runs on every caret step, and the label it would write is
	// nearly always the one on the tab. The TAB is asked rather than a copy kept here: a panel moved to
	// another palette, or shown again, sits in a container with a label of its own, and a remembered
	// "last written" would skip exactly that write. A palette never yet laid out answers empty
	// (PaletteRefUtils.h), which is simply written.
	if (PaletteRefUtils::GetPaletteLabel(container, PaletteRefUtils::kTitle_PanelLabel).IsEqual(label))
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
	// ***** THE FIND/CHANGE TAB, THEN THE SCOPE (2026-09-27, the user's call) *****:
	// "Kohaku Find/Change - Text - Book", "... - GREP - Document". The tab is the one the dialog is on NOW,
	// which is what the next Find in ... will search with. Left out when the settings cannot be read.
	const char* const tab = KBSRuns()->TabName(KBSRuns()->CurrentSearchMode());
	if (tab[0] != '\0')
	{
		title.Append(" - ");
		title.Append(tab);
	}
	title.Append(" - ");
	// The whole word, not "Doc" (user's call 2026-08-01, and again on 2026-09-27 after a few hours of
	// "Doc" beside the tab name).
	// With Book Scope off, the Search: KBS follows since 2026-09-29 ("... - Text - Story") as the selection
	// makes it (Document when the selection does not offer it, as the dialog shows); Document for one it refuses.
	const char* const searchWord = KBSRuns()->SearchScopeName(
		KBSRuns()->SearchScopeForSelection(KBSRuns()->CurrentSearchScope()));
	title.Append(KBSChapters()->IsBookScopeOn() ? "Book" : (searchWord[0] != '\0' ? searchWord : "Document"));
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

// ***** THE FIND/CHANGE TAB ON THE PANEL'S NAME (2026-09-27). ***** The dialog's settings are a session
// preference (IFindChangeOptions on the session workspace), and a preference command notifies the
// subject of the boss that holds it on the preference's own IID - so switching the dialog's tab arrives
// here and renames the tab at once. MEASURED on 2026-09-27: a script's findGrep() arrives, and so does
// the user clicking the dialog's tab (memory findchange-tab-switch-notification). No worked example
// observes THESE settings in the SDK; the title is also rewritten on show, on a scope toggle, on
// every search and (2026-10-03) on a selection change.
// ***** REACHED THROUGH THE SETTING ITSELF (2026-10-02, the API re-audit). ***** The subject is asked of
// the preference interface, as the product's panels reach theirs - spellpanel's
// AutoCorrectPanelObserver.cpp:72-75 (QuerySessionPreferences -> ISubject -> AttachObserver, and the
// same in AutoDetach) - and so through the very call the engine reads the settings by. It was asked of
// IWorkspace until then: the same boss (PreferenceUtils.h: "a preferences interface in the session
// workspace"), so nothing that arrives has changed.
void AttachToFindChangeOptions(IObserver* observer, bool attach)
{
	if (GetExecutionContextSession() == nil)
		return;		// the guard this always had (SetTabLabel says why a session is not taken for granted)
	InterfacePtr<IFindChangeOptions> settings(QuerySessionPreferences<IFindChangeOptions>());
	InterfacePtr<ISubject> subject(settings, UseDefaultIID());
	if (subject == nil)
		return;
	const bool attached = subject->IsAttached(observer, IID_IFINDCHANGEOPTIONS) != kFalse;
	if (attach && !attached)
		subject->AttachObserver(observer, IID_IFINDCHANGEOPTIONS);
	else if (!attach && attached)
		subject->DetachObserver(observer, IID_IFINDCHANGEOPTIONS);
}

/** Attach to (or detach from) one of the panel's own widgets on the protocol it reports clicks on.
    Silently does nothing when the widget is not there, which is the ordinary state while the panel
    is being torn down. */
void AttachToWidget(IPanelControlData* panelData, IObserver* observer, const WidgetID& widgetID, bool attach)
{
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

/** Observer on kKBSPanelWidgetBoss. Everything tied to the panel being shown:

      * WRITTEN ON AT EVERY SHOW, because the panel's widgets are built fresh each time (and a palette
        is only laid out while it is visible): the TAB's name, the LAYOUT for the UI language
        (KBSPanelMetrics), which PICTURE is showing (KBSPanelIcon), and the STATUS LINE - whose string
        is persisted in the workspace, so left alone it would come back reading whatever the last
        session put there (KBSResultTree::RestoreStatusOnPanelShow). Also the translucency's safety
        net and the palette-visibility subscription's retry.

      * FOLLOWED WHILE THE PANEL IS UP: what the tab's name is made of - the Find/Change settings
        (their subject) and the SELECTION (this is an ActiveSelectionObserver; see below).

      * The ILLUSTRATION's click. The picture beside the message opens the plug-in's home page (the
        URL its tooltip shows). A button announces a click to whoever is listening on
        ITriStateControlData, and this observer - already on the panel boss, already living exactly
        as long as the widgets do - is who listens.

    ***** AN ActiveSelectionObserver SINCE 2026-10-03 (the block 4 recheck, T-1). ***** The tab names
    Search: as the selection makes it, and a selection changed with the Find/Change dialog CLOSED changes
    no setting - so nothing arrived, and the tab went on naming a scope the next search would not use
    (while the flyout, asked when it opens, already said the other). It was a CObserver until then.
    The shape is the SDK's own for a panel observer that also hears its widgets:
    strokeweightmutator/StrMutSelectionObserver.cpp - the base attached FIRST in AutoAttach and
    detached LAST in AutoDetach, and the widgets' (and here the settings') messages taken in
    HandleSelectionUpdate after the base has seen them; Update itself is the base's
    (SelectionObserver.h: "Do NOT override Update w/o a very good reason"). */
class KBSPanelObserver : public ActiveSelectionObserver
{
public:
	KBSPanelObserver(IPMUnknown* boss) : ActiveSelectionObserver(boss) {}
	virtual ~KBSPanelObserver() {}

	virtual void AutoAttach()
	{
		// The selection first - SelectionObserver.h asks for the inherited method before a subclass
		// attaches to anything of its own.
		ActiveSelectionObserver::AutoAttach();

		KBSPanelTitle::Update();

		// ***** THIS OBSERVER IS ON THE PANEL. ***** It is aggregated onto kKBSPanelWidgetBoss, so it
		// can hand the panel it stands on to what fills the panel in - no need to ask the panel
		// manager for it. This is the product's own shape: ConditionalTextUIPanelDetailController.cpp:162
		// and LayerPanelView.cpp:63 both reach their own widgets with exactly this line. The layout,
		// the picture and the widget subscriptions below are handed it.
		// !NOT EVERYTHING BELOW IS (this said "the panel everything below works on is simply itself"
		//  until 2026-10-03). KBSResultTree::RestoreStatusOnPanelShow still finds the panel itself,
		//  through IPalettePanelUtils::QueryPanelByWidgetID - visible panels only - and the translucency
		//  goes through the panel manager. Whether the visible-only door answers at the moment the
		//  panel is rebuilt by the startup restore has not been measured (the block 4 recheck, S-1).
		// *nil is not expected here (the boss carries IPanelControlData), but each callee checks:
		//  AutoAttach also runs while the panel is being built, and none of this is worth a crash.
		InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());

		// The LAYOUT first, because the two calls below fill in what these frames hold. The .fr
		// carries the English measurements and a Japanese UI draws the palette font half again as
		// tall, so on that UI the message box has to be taller or the last line of every message is
		// cut off mid-glyph (reported 2026-08-06). See KBSPanelMetrics.h.
		KBSPanelMetrics::Update(panelData);

		// The widgets are built fresh every time the panel is shown, so the picture that belongs on
		// screen has to be written on NOW - the .fr's visible flags are only a starting point.
		KBSPanelIcon::Update(panelData);

		// ...and for the same reason, so does the message. The .fr's initial text is used the first
		// time the panel is ever built and never again: after that the widget carries whatever the
		// workspace remembers, which after a restart is a message about results that no longer
		// exist (reported 2026-08-02).
		KBSResultTree::RestoreStatusOnPanelShow();

		for (int32 i = 0; i < KBSPanelIcon::Count(); ++i)
			AttachToWidget(panelData, this, KBSPanelIcon::NthWidgetID(i), true);

		// The Find/Change tab on the name follows the dialog while the panel is up.
		AttachToFindChangeOptions(this, true);

		// *At startup (KBSUIStartupShutdown::Startup) the panel manager may not have come up yet, in
		// which case the subscription failed - so it is tried again here. IsAttached guards it, so
		// this cannot subscribe twice.
		KBSAttachPanelVisibilityObserver();

		// ...and if "Translucent Panel" is ON, put the alpha back: re-opening the panel gives it a
		// different top-level window (OWL.Dock), which is what the alpha was on.
		// *This is the safety net; the following is really done by the observer in KBSPanelAlpha.cpp
		//   (kPaletteVisibilityChangedMessage).
		//   *Note: AutoAttach runs every time the widgets are rebuilt, so it is no place to write a
		//   fixed default - it only reflects whatever KBSGetPanelTranslucent currently says.
		// **The OFF test is HERE and not inside (corrected 2026-08-11). This said "safe to call
		//   unconditionally - OFF, docked and Mac are all rejected inside", and OFF is NOT rejected
		//   inside: KBSApplyPanelTranslucency writes alpha 255 and shows the shadow when the toggle is
		//   off, which is how the OFF menu item does its restoring. Calling it from here regardless
		//   meant every rebuild of the widgets wrote 255 to this panel's top-level window - and a
		//   floating GROUP shares one OWL.Dock, so that lands on any grouped panel whose own
		//   translucency is ON. See the note at KBSPanelRollOver::MouseEnter for the whole account.
		if (KBSGetPanelTranslucent())
			KBSApplyPanelTranslucency();
	}

	virtual void AutoDetach()
	{
		// The same panel, asked for the same way as in AutoAttach - the detach has to reach the
		// very widgets the attach reached.
		InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());

		for (int32 i = 0; i < KBSPanelIcon::Count(); ++i)
			AttachToWidget(panelData, this, KBSPanelIcon::NthWidgetID(i), false);

		AttachToFindChangeOptions(this, false);

		// The selection last - the mirror of AutoAttach (SelectionObserver.h).
		ActiveSelectionObserver::AutoDetach();
	}

protected:
	// ***** THE SELECTION (2026-10-03). ***** A new selection, or different items in it (a frame picked
	// on the layout, the text tool's selection replacing it), and every caret step (the "frequent"
	// change, SelectionObserver.h). Which of the two a caret turning into a selected run of text arrives
	// as is NOT measured, so both are taken. Each can move what Search: comes to (KBSPanelTitle.h), so
	// each asks for the name again; the name is only WRITTEN when it differs from the tab's
	// (SetTabLabel), and with Book Scope on it never does.
	virtual void HandleSelectionChanged(const ISelectionMessage* /*message*/)
	{
		KBSPanelTitle::Update();
	}

	virtual void HandleFrequentSelectionChanged()
	{
		KBSPanelTitle::Update();
	}

	// What is not the selection's: the Find/Change settings and the pictures' clicks. The base first -
	// it routes the selection's own messages to the two above (SelectionObserver.h).
	virtual void HandleSelectionUpdate(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
	{
		ActiveSelectionObserver::HandleSelectionUpdate(theChange, theSubject, protocol, changedBy);

		// A Find/Change setting changed - the tab, among others. Renaming costs a read of the label (and
		// a write only when it differs - SetTabLabel), so every change on this protocol is taken rather
		// than trying to tell the tab from the rest.
		if (protocol == IID_IFINDCHANGEOPTIONS)
		{
			KBSPanelTitle::Update();
			return;
		}

		// kTrueStateMessage is the click - the product reads it as the button coming back UP
		// (linksui/ProblemLinksDialogObserver.cpp:80 "Only respond when the button is going up"),
		// and every product button observer tests this one message and nothing else
		// (linksui/ToggleLinkInfoButtonObserver.cpp:99, conditionaltextui's four button observers).
		// *A TOGGLE would also want kFalseStateMessage - conditionaltextui/ConditionTagEyeball
		//  Observer.cpp:100 takes both - but these pictures are not toggles.
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
