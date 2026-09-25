//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  "Remember Book Panel Placement". What it does and what it leaves alone is in
//  KBSBookPanelPlacement.h. This file holds the three pieces that make it run:
//
//    1. the OBSERVER, AddIn'd onto kActiveContextBoss and attached to the panel manager's subject
//       (IID_IPANELMGR) - the same subject and the same arrangement as the "Translucent Panel"
//       observer in KBSPanelAlpha.cpp. Two messages are acted on:
//         kAboutToClosePaletteMsg          changedBy = the IControlView of the panel being closed
//                                          (PaletteRefUtils.h:525; the product reads it that way in
//                                          PageTransitionsPanelObserver.cpp:444-447)
//         kPaletteVisibilityChangedMessage a panel was opened, closed, docked or pulled out - it
//                                          arrives AFTER the panel's widgets are built (measured for
//                                          KBS's own panel on 2026-07-29, KBSPanelAlpha.cpp)
//
//    2. the PALETTE MANAGER SERVICE (IPaletteMgrService, kPaletteMgrService). It is what the Book
//       panel ITSELF hangs off - kBookPanelStartupShutdownBoss is the product's only implementation
//       (Service_Registry_Memory_Dump.txt:2761) - and it is called at the two moments this needs:
//       "just after Palette manager has started up and opened/positioned it's palettes", and "just
//       before the palettes are closed" (IPaletteMgrService.h:38-41). The startup service is no use
//       for either: the panel manager can still be nil there (KBSPanelAlpha.cpp says so, and why it
//       attaches a second time from the panel's AutoAttach), and by Shutdown the palettes are gone.
//
//    3. a one-shot ICallbackTimer that puts the placement back a moment after the book panel
//       appears, not inside the notification: OWL lays palettes out asynchronously
//       (PaletteRefUtils.h:40-41), so a position written while the new palette is still being laid
//       out can be written over.
//
//  ***** THE DOCK AND THE PANEL ARE MEASURED SEPARATELY, AND ON PURPOSE. *****
//    Position = the FLOATING DOCK's top-left (GetPalettePosition). SetPalettePosition only takes a
//      floating Dock, Toolbar or ControlBar (PaletteRefUtils.h:361-366), so the palette tree is
//      walked up from the panel's container to that dock - the walk KESCM used when it moved its own
//      panel to the cursor (2026-07-10, removed later with that feature).
//    Size = the book PANEL's frame. A floating panel is resized by resizing the panel view - that is
//      what the product does (LinksUIUtils.cpp:650-653, :720-723); SetPaletteSize is its route for a
//      DOCKED palette only. Measured 2026-09-25: moving the dock window itself with SetWindowPos to
//      360x420 left the panel inside at 302x224 - the size has to go through OWL, not the window.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActiveContext.h"		// where the observer implementation lives (kActiveContextBoss)
#include "IApplication.h"		// QueryPanelManager
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "IControlView.h"		// GetFrame / ConstrainDimensions / Resize - the book panel itself
#include "IMonitorInfo.h"		// GetBestScreenRect - is the remembered place still on a screen?
#include "IObserver.h"
#include "IPaletteMgrService.h"	// the palette manager's start and shut-down
#include "IPanelMgr.h"			// the panel list, and the palette holding a panel
#include "ISession.h"			// GetExecutionContextSession (can be nil during shutdown)
#include "ISubject.h"			// AttachObserver / IsAttached / DetachObserver

// General includes:
#include "AppUIID.h"			// kAboutToClosePaletteMsg (:310) / kPaletteVisibilityChangedMessage (:325)
#include "CObserver.h"
#include "CPMUnknown.h"
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)
#include "CServiceProvider.h"
#include "PaletteRef.h"
#include "PaletteRefUtils.h"	// IsPaletteFloating / IsFloatingTabbedPaletteDock / Get/SetPalettePosition
#include "ShuksanID.h"			// kCallbackTimerBoss, IID_ICALLBACKTIMER
#include "WorkspaceID.h"		// kPaletteMgrService, IID_IPALETTEMGRSERVICE

#include <string>
#include <utility>
#include <vector>

// Project includes:
#include "KBSID.h"
#include "KBSBookPanelPlacement.h"
#include "KBSBookScope.h"		// IsBookPanel - the one place that decides what a book panel is
#include "KBSPanelState.h"		// KBSPanelStateWriteKeys - the settings file, key by key
#include "KBSResultTree.h"		// ShowStatus - a write that failed is said, not swallowed

namespace
{

/** The toggle (session flag). */
bool gOn = false;

/** The last placement known this session - from the file at startup, or from a close. */
bool gHaveRemembered = false;
KBSBookPanelPlacement::Placement gRemembered;

/** How many book panels there were at the last look. A restore is due when this goes from zero to
    anything, and ONLY then - a book opened beside another joins that palette as a tab. Counted only
    while the toggle is ON; switching it on and Start() take a fresh count, so a stale number cannot
    make an already-open panel look new. */
int32 gBookPanelCount = 0;

/** The deferred restore. ONE object for the life of the plug-in, released only by
    ShutdownCleanup - never from inside its own callback (KBSBookWatch.cpp explains why). */
ICallbackTimer* gRestoreTimer = nil;

/** How long after the book panel appears the placement is put back. A beat, so OWL's own layout of
    the new palette (asynchronous - PaletteRefUtils.h:40-41) has gone round first. */
const uint32 kKBSBookPanelRestoreDelayMs = 100;

/** How much of the title band has to be on a screen for a remembered placement to be used: enough
    to take hold of it and drag it back. */
const SysCoord kKBSBookPanelMinOnScreen = 40;

/** The four keys, in the order they are written. */
const char* const kKeyLeft   = "bookPanelLeft";
const char* const kKeyTop    = "bookPanelTop";
const char* const kKeyWidth  = "bookPanelWidth";
const char* const kKeyHeight = "bookPanelHeight";
const char* const kKeyToggle = "rememberBookPanelPlacement";

/** The panel manager, AddRef'd - or nil: during startup it may not exist yet, and during teardown
    the session can already be gone. */
IPanelMgr* QueryPanelManager()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;
	return app->QueryPanelManager();
}

/** Walk the registered panels: how many are book panels, and (when asked) the first of them,
    AddRef'd. InDesign makes one book panel per open book and registers each with IPanelMgr; the
    WidgetID is numbered per book at run time, so walking is the only way to find them
    (docs/ai-notes/book-panel-active-tab.md - the same walk KBSBookScope uses). */
int32 WalkBookPanels(IPanelMgr* panelMgr, IControlView** outFirst)
{
	if (outFirst != nil)
		*outFirst = nil;
	if (panelMgr == nil)
		return 0;

	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return 0;

	int32 count = 0;
	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		PMString panelName;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, nil, &panelName))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!KBSBookScope::IsBookPanel(panelView, panelName))
			continue;

		// Only a panel that is in a palette counts. A book panel still on the list after its book
		// closed - not measured either way, but nothing promises the list is pruned before the next
		// notification - would otherwise keep the count at one, and the next book to open would not
		// look like "none, then one" and would never be put back.
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid())
			continue;

		++count;
		if (outFirst != nil && *outFirst == nil)
			*outFirst = panelView.forget();
	}
	return count;
}

/** The registered book panel whose view IS this pointer, AddRef'd - or nil. Only the pointer's value
    is compared: nothing is called on it until it has been found on the panel list. It arrives as a
    notification's void*, and a pointer that is not what it was taken for would take InDesign down
    the moment it was asked its class. */
IControlView* QueryRegisteredBookPanel(IPanelMgr* panelMgr, const void* candidate)
{
	if (panelMgr == nil || candidate == nil)
		return nil;

	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return nil;

	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		PMString panelName;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, nil, &panelName))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (panelView == nil || static_cast<const void*>(panelView.get()) != candidate)
			continue;

		// Found - and now it is safe to ask what it is.
		if (!KBSBookScope::IsBookPanel(panelView, panelName))
			return nil;
		return panelView.forget();
	}
	return nil;
}

/** The floating dock that holds this container, walking up the palette tree - or an invalid
    PaletteRef when there is none (the palette is docked). The guard only stops a malformed tree from
    looping; a real one is a handful of levels deep (container -> tab group -> tab pane -> dock). */
PaletteRef FindFloatingDock(const PaletteRef& container)
{
	PaletteRef pal = container;
	for (int32 guard = 0; guard < 16 && pal.IsValid(); ++guard)
	{
		if (PaletteRefUtils::IsFloatingTabbedPaletteDock(pal))
			return pal;
		pal = PaletteRefUtils::GetParentOfPalette(pal);
	}
	return PaletteRef();
}

/** Measure one book panel. false - and nothing written to out - when it is docked, when its palette
    is minimised to its title bar (its frame then is not the size to come back to), or when any
    step of the palette tree cannot be reached. */
bool Measure(IPanelMgr* panelMgr, IControlView* bookPanel, KBSBookPanelPlacement::Placement& out)
{
	if (panelMgr == nil || bookPanel == nil)
		return false;

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	if (!container.IsValid())
		return false;
	if (!PaletteRefUtils::IsPaletteFloating(container))
		return false;	// docked: its place belongs to the dock (see the header)
	if (PaletteRefUtils::IsPaletteMinimized(container))
		return false;

	const PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
		return false;

	const SysPoint pos = PaletteRefUtils::GetPalettePosition(dock);
	const PMRect frame = bookPanel->GetFrame();

	KBSBookPanelPlacement::Placement measured;
	measured.left   = SysPointH(pos);
	measured.top    = SysPointV(pos);
	measured.width  = ::ToInt32(frame.Width());
	measured.height = ::ToInt32(frame.Height());
	if (measured.width <= 0 || measured.height <= 0)
		return false;

	out = measured;
	return true;
}

/** Would the palette's title band land on a screen if put here? Asked of the screen that holds most
    of the rectangle (IMonitorInfo::GetBestScreenRect handles more than one monitor - KCM's
    KeepPanelOnScreen asks it the same way). "Cannot tell" answers no: a palette left where it
    opened is a smaller fault than one put where nobody can reach it. */
bool TitleBandIsOnScreen(const KBSBookPanelPlacement::Placement& p)
{
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	if (app == nil)
		return false;
	InterfacePtr<const IMonitorInfo> monInfo(app, UseDefaultIID());
	if (monInfo == nil)
		return false;

	SysRect wanted;
	::SetSysRectLTWH(wanted, p.left, p.top, p.width, p.height);
	const GSysRect screen = monInfo->GetBestScreenRect(wanted);

	// Across: this much of the width has to overlap the screen.
	const SysCoord overlapLeft  = (p.left > SysRectLeft(screen)) ? p.left : SysRectLeft(screen);
	const SysCoord wantedRight  = p.left + p.width;
	const SysCoord overlapRight = (wantedRight < SysRectRight(screen)) ? wantedRight : SysRectRight(screen);
	if (overlapRight - overlapLeft < kKBSBookPanelMinOnScreen)
		return false;

	// Down: the top edge - where the title band is - has to be on the screen, with room under it.
	if (p.top < SysRectTop(screen) || p.top > SysRectBottom(screen) - kKBSBookPanelMinOnScreen)
		return false;

	return true;
}

/** Say on the panel's status line that a write went wrong. Only failures are said: a write at every
    book close that reported success would be a line about something the user did not do. */
void SayWriteFailed(const char* reason)
{
	PMString msg("Book panel placement could not be saved (");
	msg.Append(reason);
	msg.Append(").");
	msg.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(msg);
}

/** Keep this placement for the session and write its four keys - nothing else - to the settings
    file. Silent when it works (see SayWriteFailed). */
void RememberAndWrite(const KBSBookPanelPlacement::Placement& p, bool sayFailure)
{
	KBSBookPanelPlacement::SetRemembered(p);

	std::vector<std::pair<std::string, std::string> > keys;
	keys.push_back(std::make_pair(std::string(kKeyLeft),   std::to_string(p.left)));
	keys.push_back(std::make_pair(std::string(kKeyTop),    std::to_string(p.top)));
	keys.push_back(std::make_pair(std::string(kKeyWidth),  std::to_string(p.width)));
	keys.push_back(std::make_pair(std::string(kKeyHeight), std::to_string(p.height)));

	const char* failure = KBSPanelStateWriteKeys(keys);
	if (failure != nil && sayFailure)
		SayWriteFailed(failure);
}

/** Put the remembered placement on the book panel that is open now. Runs from the timer. */
void RestoreNow()
{
	if (!gOn || !gHaveRemembered)
		return;

	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	IControlView* first = nil;
	WalkBookPanels(panelMgr, &first);
	InterfacePtr<IControlView> bookPanel(first);	// takes over the reference WalkBookPanels added
	if (bookPanel == nil)
		return;		// closed again in the meantime

	const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(bookPanel);
	if (!container.IsValid() || !PaletteRefUtils::IsPaletteFloating(container))
		return;		// InDesign put it in a dock - that place is the dock's (see the header)

	const PaletteRef dock = FindFloatingDock(container);
	if (!dock.IsValid())
		return;

	if (!TitleBandIsOnScreen(gRemembered))
		return;

	// Where first, then how big: the panel grows from its top-left, so moving it after a resize
	// would first have grown it somewhere else.
	PaletteRefUtils::SetPalettePosition(dock, gRemembered.left, gRemembered.top);

	// ***** Through ConstrainDimensions: putting the size through it is the CALLER'S job, it is not
	// something Resize does on the way in (IControlView.h:174-176) - and it is the book panel's own
	// view that knows its minimum. The product calls it before resizing a floating panel too
	// (LinksUIUtils.cpp:626-627).
	PMPoint size(PMReal(gRemembered.width), PMReal(gRemembered.height));
	size = bookPanel->ConstrainDimensions(size);
	bookPanel->Resize(size);
}

/** Timer callback. A raw function pointer, so it must never outlive this plug-in (see
    ShutdownCleanup). NOTHING is released in here - the same rule and the same reason as
    KBSBookWatch's RetireTimerCallback. */
uint32 RestoreTimerCallback(void* /*refPtr*/)
{
	RestoreNow();

	// NEVER 0: to the idle task manager 0 means "call me again immediately".
	return IIdleTask::kEndOfTime;
}

/** Arm the deferred restore (or restart its wait if one is pending). */
void ArmRestoreTimer()
{
	if (gRestoreTimer == nil)
		gRestoreTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);

	if (gRestoreTimer == nil)
	{
		// No timer to be had - do it now rather than not at all.
		RestoreNow();
		return;
	}

	gRestoreTimer->StopTimer();
	gRestoreTimer->StartTimer(RestoreTimerCallback, kKBSBookPanelRestoreDelayMs, nil);
}

/** Count the book panels afresh, and arm a restore if there were none at the last look and there are
    some now. The one place the "zero to some" question is asked. */
void RecountAndMaybeRestore()
{
	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	const int32 previous = gBookPanelCount;
	gBookPanelCount = WalkBookPanels(panelMgr, nil);

	if (previous == 0 && gBookPanelCount > 0 && gHaveRemembered)
		ArmRestoreTimer();
}

/** The observer's attach and detach. Regular attachment, the same as the "Translucent Panel"
    observer on the same subject, and undone with the same type (ISubject.h:288). */
void AttachObserver(bool attach)
{
	ISession* session = GetExecutionContextSession();
	IActiveContext* ctx = (session != nil) ? session->GetActiveContext() : nil;
	if (ctx == nil)
		return;

	InterfacePtr<IObserver> obs((IObserver*)ctx->QueryInterface(IID_IKBSBOOKPANELOBSERVER));
	if (obs == nil)
		return;

	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	if (panelMgr == nil)
		return;

	InterfacePtr<ISubject> subject(panelMgr, IID_ISUBJECT);
	if (subject == nil)
		return;

	const bool attached = subject->IsAttached(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSBOOKPANELOBSERVER) != kFalse;
	if (attach && !attached)
		subject->AttachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSBOOKPANELOBSERVER);
	else if (!attach && attached)
		subject->DetachObserver(ISubject::kRegularAttachment, obs, IID_IPANELMGR, IID_IKBSBOOKPANELOBSERVER);
}

/** Drop a pending restore and give the timer back. The ONE place it is released. */
void DisarmRestoreTimer()
{
	if (gRestoreTimer == nil)
		return;
	gRestoreTimer->StopTimer();
	gRestoreTimer->Release();
	gRestoreTimer = nil;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KBSBookPanelPlacement
//----------------------------------------------------------------------------------------

bool KBSBookPanelPlacement::IsOn()
{
	return gOn;
}

void KBSBookPanelPlacement::SetOn(bool on)
{
	gOn = on;
}

void KBSBookPanelPlacement::ToggleAndSave(PMString& outStatus)
{
	gOn = !gOn;

	// A fresh count, so the next look compares against what is open NOW. Without it a count left
	// from before the toggle was switched off would make an already-open book panel look new, and
	// the first notification after ticking the box would move it.
	if (gOn)
	{
		InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
		gBookPanelCount = WalkBookPanels(panelMgr, nil);
	}

	// The toggle's key, and nothing else (the user's rule, 2026-09-25).
	std::vector<std::pair<std::string, std::string> > keys;
	keys.push_back(std::make_pair(std::string(kKeyToggle), std::string(gOn ? "true" : "false")));
	const char* failure = KBSPanelStateWriteKeys(keys);

	outStatus = gOn ? "Remember book panel placement: on" : "Remember book panel placement: off";
	if (failure == nil)
	{
		outStatus.Append(" (saved).");
	}
	else
	{
		outStatus.Append(" - but it could not be saved (");
		outStatus.Append(failure);
		outStatus.Append(").");
	}
	outStatus.SetTranslatable(kFalse);
}

bool KBSBookPanelPlacement::GetRemembered(Placement& out)
{
	if (!gHaveRemembered)
		return false;
	out = gRemembered;
	return true;
}

void KBSBookPanelPlacement::SetRemembered(const Placement& placement)
{
	if (placement.width <= 0 || placement.height <= 0)
		return;
	gRemembered = placement;
	gHaveRemembered = true;
}

bool KBSBookPanelPlacement::MeasureOpenBookPanel(Placement& out)
{
	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	IControlView* first = nil;
	WalkBookPanels(panelMgr, &first);
	InterfacePtr<IControlView> bookPanel(first);	// takes over the reference WalkBookPanels added
	return Measure(panelMgr, bookPanel, out);
}

void KBSBookPanelPlacement::Start()
{
	// The saved settings are read by the startup service - but that one is LAZY
	// (kLazyStartupShutdownProviderImpl), and nothing promises it runs before the palette manager
	// has laid out. Reading here as well costs nothing: the read is guarded to happen once.
	KBSLoadPanelStateIfPresent();

	AttachObserver(true);

	// Whatever book panel is open by now has "just appeared" as far as this feature is concerned -
	// before this call nothing was following - so the count starts from zero. (Whether InDesign
	// reopens books at launch, and whether their panels are up by this point, is NOT measured yet;
	// counting from zero is right either way.)
	gBookPanelCount = 0;
	if (gOn)
		RecountAndMaybeRestore();
}

void KBSBookPanelPlacement::Stop()
{
	// The palettes are about to close, the book panel with them - this is "the book panel is being
	// closed" for the quit, and the last moment it can be measured. Nothing is said on failure:
	// InDesign is quitting, and there is no panel left to say it on that anyone would read.
	if (gOn)
	{
		Placement now;
		if (MeasureOpenBookPanel(now))
			RememberAndWrite(now, false);
	}

	AttachObserver(false);
	DisarmRestoreTimer();
}

void KBSBookPanelPlacement::ShutdownCleanup()
{
	AttachObserver(false);
	DisarmRestoreTimer();
}

//----------------------------------------------------------------------------------------
// The observer
//----------------------------------------------------------------------------------------

/** Follows the panel manager: measures a book panel as it closes, and puts the placement back when
    one appears where there was none. */
class KBSBookPanelObserver : public CObserver
{
public:
	KBSBookPanelObserver(IPMUnknown* boss) : CObserver(boss, IID_IKBSBOOKPANELOBSERVER) {}
	virtual ~KBSBookPanelObserver() {}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy);
};

CREATE_PMINTERFACE(KBSBookPanelObserver, kKBSBookPanelObserverImpl)

void KBSBookPanelObserver::Update(const ClassID& theChange, ISubject* /*theSubject*/, const PMIID& protocol, void* changedBy)
{
	if (protocol != IID_IPANELMGR)
		return;

	// Nothing while OFF: the visibility message arrives several times over merely opening a document
	// (measured for KBSPanelAlpha), and people not using this should not pay for a walk of the panel
	// list each time.
	if (!gOn)
		return;

	if (theChange == kAboutToClosePaletteMsg)
	{
		// The panel being closed arrives as changedBy (PaletteRefUtils.h:525 - "notification contains
		// pointer to panel being closed"; the product casts it to IControlView*). It is looked up on
		// the panel list BEFORE anything is called on it, and only a book panel goes on.
		InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
		InterfacePtr<IControlView> closing(QueryRegisteredBookPanel(panelMgr, changedBy));
		if (closing == nil)
			return;

		KBSBookPanelPlacement::Placement now;
		if (Measure(panelMgr, closing, now))
			RememberAndWrite(now, true);
		return;
	}

	if (theChange == kPaletteVisibilityChangedMessage)
		RecountAndMaybeRestore();
}

//----------------------------------------------------------------------------------------
// The palette manager service
//----------------------------------------------------------------------------------------

/** Registers the boss for kPaletteMgrService, the service the Book panel's own start-up hangs off. */
class KBSBookPanelServiceProvider : public CServiceProvider
{
public:
	KBSBookPanelServiceProvider(IPMUnknown* boss) : CServiceProvider(boss) {}
	virtual ~KBSBookPanelServiceProvider() {}

	virtual ServiceID GetServiceID() { return kPaletteMgrService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	// Per session and main thread only: what the product's own provider for this service is
	// registered as (Service_Registry_Memory_Dump.txt:2761, kBookPanelStartupShutdownBoss).
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// SetCString, not SetKey: an internal name that never reaches the UI (KBSDrawEventSrvc's reason).
	virtual void GetName(PMString* pName) { pName->SetCString("KBSBookPanelService\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KBSBookPanelServiceProvider, kKBSBookPanelServiceProviderImpl)

/** The two moments: the palettes are laid out, and the palettes are about to close. */
class KBSBookPanelPaletteMgrService : public CPMUnknown<IPaletteMgrService>
{
public:
	KBSBookPanelPaletteMgrService(IPMUnknown* boss) : CPMUnknown<IPaletteMgrService>(boss) {}
	virtual ~KBSBookPanelPaletteMgrService() {}

	virtual void PaletteMgrStarted()		{ KBSBookPanelPlacement::Start(); }
	virtual void PaletteMgrAboutToShutdown()	{ KBSBookPanelPlacement::Stop(); }
};

CREATE_PMINTERFACE(KBSBookPanelPaletteMgrService, kKBSBookPanelPaletteMgrServiceImpl)

// End, KBSBookPanelPlacement.cpp.
