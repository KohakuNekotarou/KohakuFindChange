//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Draw-event-handler service that inverts the pixels under a jumped-to hit's first text chunk.
//  Ported from KESCL's KESCLDrawEventHandler (KESCL left untouched):
//    * KBSDrawEventSrvc (CServiceProvider) registers kDrawEventService, so the app finds it at
//      startup and hooks the same boss's IDrwEvtHandler into the draw-event dispatcher.
//    * KBSDrawEventHandler draws on kEndSpreadMessage (spread front, spread coordinates).
//
//  The marker rectangle is kept in pasteboard coordinates; on each spread draw we convert it to
//  that spread's coordinates (only the owning spread paints it) and fill it with white through the
//  Difference blending mode, which inverts whatever is underneath - so the marker is visible on a
//  red page, a photo or a black box alike, which a tinted rectangle was not.
//  Screen only (printing / Overprint-Preview are skipped), non-persistent.
//  Never dereferences the marker's IDataBase* - it resolves the document through the document
//  list, so a marker whose document the user closed simply stops painting instead of crashing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDrwEvtHandler.h"
#include "IDrwEvtDispatcher.h"
#include "CServiceProvider.h"
#include "GraphicsData.h"
#include "IGraphicsPort.h"
#include "IViewPortAttributes.h"
#include "IShape.h"
#include "ISpread.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "IDataBase.h"

// General includes:
#include "GraphicsID.h"			// kDrawEventService, IID_IDRWEVTHANDLER
#include "DocumentContextID.h"	// kEndSpreadMessage
#include "OutPrvID.h"			// kSepPrvOPPEnabledVPAttr (Overprint Preview detection)
#include "AutoGSave.h"
#include "SDKFileHelper.h"		// the marker's document, named by its file - see KBSMarkerDocPath
#include "PersistUtils.h"		// ::GetDataBase
// (Transform::PasteboardCoordinates / SpreadCoordinates come in with ISpread.h, which includes
// TransformTypes.h - the same way snapshot/SnapTracker.cpp gets them.)
#include "ILayoutUIUtils.h"
#include "ILayoutUtils.h"		// InvalidateViews (reliable overlay repaint)
#include "IDocument.h"
#include "ISession.h"
#include "Utils.h"
#include "PMPoint.h"
#include "PMReal.h"
#include "GraphicTypes.h"		// kPMBlendDifference / kPMBlendExclusion (Phase A probe)
#include "PMString.h"

// A marker asked for by the MOUSE is booked rather than raised - see SetMarkerAfterClickSettles:
#include "ICallbackTimer.h"		// StartTimer / StopTimer (an IIdleTask; kEndOfTime comes with it)
#include "CreateObject.h"		// ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER)
#include "ShuksanID.h"			// kCallbackTimerBoss / IID_ICALLBACKTIMER

// Project includes:
#include "KBSID.h"
#include "KBSDrawEventHandler.h"
#include "KBSMarkerExpiryIdleTask.h"	// the countdown that takes the marker back off the screen

// *windows.h goes AFTER the SDK headers, so its macros cannot collide with SDK names (the same order
//  KBSPanelAlpha.cpp:84-87 keeps). Wanted here for ::GetDoubleClickTime alone.
#ifdef WINDOWS
#include <windows.h>
#endif

CREATE_PMINTERFACE(KBSDrawEventHandler, kKBSDrawEventHandlerImpl)

//----------------------------------------------------------------------------------------
// Shared marker state
//----------------------------------------------------------------------------------------
bool16     KBSDrawEventHandler::sHasMarker = kFalse;
PMRect     KBSDrawEventHandler::sMarkerPb  = PMRect(0, 0, 0, 0);
IDataBase* KBSDrawEventHandler::sMarkerDB  = nil;
PMString   KBSDrawEventHandler::sMarkerDocPath;
UID        KBSDrawEventHandler::sMarkerSpread = kInvalidUID;

// The file a database's document lives in - empty when it has none (never saved) or cannot be asked.
//
// ***** WHY THE MARKER NEEDS THIS AT ALL. ***** sMarkerDB is an IDataBase*, and an address is not an
// identity: when a document is closed its address is free to be handed to the next one opened, so a
// marker left over from a closed document can match a document that has nothing to do with it. KBS
// has met exactly this before - a UIDRef is (IDataBase*, UID), and reusing one across a close is what
// made a book replace report a whole chapter 'missing' (2026-08-04, see KBSBookScope). The answer
// there was to ask the FILE, and it is the answer here.
//
// Called only where the database is known to be alive: at SetMarker time (the jump has just been
// there) and inside a draw event (drawing only happens for open documents). It is never called on
// sMarkerDB itself, which is the pointer that may be stale.
static PMString KBSMarkerDocPath(IDataBase* db)
{
	PMString path;
	path.SetTranslatable(kFalse);
	if (db == nil)
		return path;
	const IDFile* sysFile = db->GetSysFile();
	if (sysFile == nil)
		return path;
	SDKFileHelper helper(*sysFile);
	path = helper.GetPath();
	path.SetTranslatable(kFalse);
	return path;
}

// Repaint the layout so the marker appears / disappears immediately. 'db' is a non-owning pointer
// whose document may have been closed since the marker was set (the expiry task fires up to a
// second later, and the user can close the document within that second), so it is NEVER
// dereferenced: the document is resolved through the document list. A db the list does not know
// falls through to a front-document repaint. Called only from the set/clear helpers (never from
// inside a draw event).
static void KBSRepaintViews(IDataBase* db)
{
	if (db != nil)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		IDocument* doc = (docList != nil) ? docList->FindDocByDataBase(db) : nil;
		if (doc != nil)
		{
			Utils<ILayoutUtils>()->InvalidateViews(doc);
			return;
		}
	}
	IDocument* fdoc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	if (fdoc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(fdoc);
}

//----------------------------------------------------------------------------------------
// The booking: a marker the MOUSE asked for waits until the click is known to have been single
//----------------------------------------------------------------------------------------
//
// ***** WHAT THIS EXISTS FOR. ***** A double click on a hit row jumps on the FIRST button-up and
// selects on the second (KBSResultNodeEH.cpp). The jump raised the marker as it landed and the
// selection took it straight back down again, so every double click showed a red flash of a marker
// that was never meant to be seen at all (user's call, 2026-08-09: when a double click selects, the
// single click's marker should not appear). Whether a click is single cannot be TESTED for at the
// moment the jump runs - see the header - so the marker waits the double-click interval out instead,
// and a second click inside that interval calls the wait off.
//
// File statics rather than members, like the marker they belong to: this is one booking for "the
// click going on right now", and one click happens at a time.
static ICallbackTimer* sPendingTimer  = nil;
static bool16          sHasPending    = kFalse;
static IDataBase*      sPendingDB     = nil;				// an ADDRESS - see sPendingDocPath
static PMRect          sPendingPb     = PMRect(0, 0, 0, 0);
static PMString        sPendingDocPath;						// the file that document lived in when booked
static UID             sPendingSpread = kInvalidUID;		// the spread, carried through the wait

// **Never book again once ShutdownCleanup has run - belt and braces with the release it does. The
//   callback below is a raw function pointer into this .pln, and nothing may be holding one as the
//   module goes down (KBSPanelAlpha.cpp's sPanelAlphaShutdown keeps the same guard for the same
//   reason - named rather than numbered, the line reference having drifted to a paragraph about a
//   removed diagnostic).
static bool16          sMarkerShutdown = kFalse;

// Windows' default, used where the real setting cannot be had.
static const uint32 kKBSDoubleClickIntervalFallbackMs = 500;

// How long "not a double click" takes. The USER'S OWN setting: someone who has set a slow double
// click would otherwise get the marker up in the middle of one, which is the whole thing this is
// here to stop. A zero would mean "never wait", so it is floored.
static uint32 KBSDoubleClickInterval()
{
#ifdef WINDOWS
	const uint32 ms = static_cast<uint32>(::GetDoubleClickTime());
	return (ms > 0) ? ms : kKBSDoubleClickIntervalFallbackMs;
#else
	// Mac: the platform's own setting lives behind NSEvent's doubleClickInterval, which is not
	// reachable from here; the Windows default stands in until this is ported.
	return kKBSDoubleClickIntervalFallbackMs;
#endif
}

// Call the booking off. Two callers, each one door of "the newest display wins": ClearMarker
// (whose place in the double-click machinery the header explains) and SetMarker (the keyboard
// walk's immediate marker, which must not be overwritten by a booking an earlier mouse click left
// armed - 2026-08-09).
static void KBSCancelPendingMarker()
{
	// Nothing armed, nothing to stop. This is not just a shortcut: the timer's own callback clears
	// sHasPending and then raises the marker through SetMarker, and this early exit is what keeps
	// that path from calling StopTimer on the very timer it is running inside.
	if (!sHasPending)
		return;

	if (sPendingTimer != nil)
		sPendingTimer->StopTimer();
	sHasPending    = kFalse;
	sPendingDB     = nil;
	sPendingSpread = kInvalidUID;
	sPendingDocPath.Clear();
}

// The interval has passed with no second click, so that was a single click after all: up it goes.
static uint32 KBSPendingMarkerProc(void* /*refPtr*/)
{
	// *Do not Release the timer in here - releasing itself from inside its own callback is
	//  self-destruction. The release is in ShutdownCleanup and nowhere else (the same rule
	//  KBSShutdownPanelAlpha follows for its own timer).
	if (!sHasPending)
		return IIdleTask::kEndOfTime;

	IDataBase*     db     = sPendingDB;
	const PMRect   pb     = sPendingPb;
	const PMString path   = sPendingDocPath;
	const UID      spread = sPendingSpread;
	sHasPending    = kFalse;
	sPendingDB     = nil;
	sPendingSpread = kInvalidUID;
	sPendingDocPath.Clear();

	// ***** THE DOCUMENT MAY HAVE GONE IN THE MEANTIME. ***** Half a second is ample time to close
	// one, and sPendingDB is an address that is then free to be handed to the next document opened.
	// So it is not dereferenced until the document list has been asked for it AND the file agrees -
	// the two tests HandleDrawEvent below makes, for the same reason and in the same order.
	if (db == nil)
		return IIdleTask::kEndOfTime;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
	if (docList == nil || docList->FindDocByDataBase(db) == nil)
		return IIdleTask::kEndOfTime;		// closed since the click - nothing left to point at
	if (!(KBSMarkerDocPath(db) == path))
		return IIdleTask::kEndOfTime;		// same address wearing a different document

	KBSDrawEventHandler::SetMarker(db, spread, pb);

	// **kEndOfTime, not 0. The return value is IIdleTask::RunTask's reschedule, and **0 means "call
	//   me again immediately"** - which has frozen InDesign before (KESCM's tracker, 2026-07-26).
	return IIdleTask::kEndOfTime;
}

void KBSDrawEventHandler::SetMarker(IDataBase* db, UID spreadUID, const PMRect& pbRect)
{
	// Nothing at all once ShutdownCleanup has run - this repaints, and the marker's document may be
	// half torn down by then. Same door SetMarkerAfterClickSettles has kept since 2026-08-09; it is
	// here now because the rule belongs to the functions that touch a document, not to one of them.
	// See ClearMarker for how the same guarantee was being made from outside instead.
	if (sMarkerShutdown)
		return;

	// ***** AN OUTSTANDING BOOKING DIES HERE TOO. ***** The immediate marker's one caller is the
	// keyboard walk (JumpToHit with defer off), and an arrow key pressed within the double-click
	// interval of a mouse click used to leave that click's booking armed - it fired half a second
	// later and stamped the PREVIOUS row's rectangle over the row the user had walked to (found in
	// the 2026-08-09 pre-submission sweep). The deferred sibling below has always cancelled through
	// its leading ClearMarker; this is the same rule at the other door: the newest display wins.
	KBSCancelPendingMarker();

	sMarkerDB     = db;
	sMarkerPb     = pbRect;
	sMarkerSpread = spreadUID;
	sHasMarker    = kTrue;
	// Taken NOW, while the document is certainly alive - the jump has just been in it. See
	// KBSMarkerDocPath for what it is for.
	sMarkerDocPath = KBSMarkerDocPath(db);
	KBSRepaintViews(db);

	// The marker is a flash, not a highlight: hand it to the timer that takes it away again.
	// Restarting an already-running countdown is that call's job, so each jump shows its marker
	// for the full time.
	KBSMarkerExpiryIdleTask::Start();
}

void KBSDrawEventHandler::SetMarkerAfterClickSettles(IDataBase* db, UID spreadUID, const PMRect& pbRect)
{
	// ***** ONCE ShutdownCleanup HAS RUN, NOTHING AT ALL - not even the clear. ***** ClearMarker
	// repaints the marker's document, and that is precisely what this file's ShutdownCleanup says
	// must not happen from then on: the document may be half torn down by the time teardown reaches
	// here. Asked BEFORE the clear rather than beside the nil test after it, so this function cannot
	// do the one thing the header forbids (2026-08-09 re-check). No path is known to arrive after
	// shutdown - the mouse is not being dispatched by then - and one line makes it impossible.
	if (sMarkerShutdown)
		return;

	// ***** THE OLD MARKER GOES NOW, not when the booking fires. ***** The view has just jumped
	// somewhere else, so a marker left standing over the previous hit for half a second would be
	// pointing at a place the user has left. This is also what cancels any booking still outstanding
	// (ClearMarker calls KBSCancelPendingMarker), so the newest click always wins.
	ClearMarker();

	if (db == nil)
		return;

	if (sPendingTimer == nil)
		sPendingTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (sPendingTimer == nil)
	{
		// No timer to be had: raise it now rather than lose it. A marker that flashes on a double
		// click is the behaviour this replaced; a marker that never appears would be worse than both.
		SetMarker(db, spreadUID, pbRect);
		return;
	}

	sPendingDB      = db;
	sPendingPb      = pbRect;
	sPendingSpread  = spreadUID;
	sPendingDocPath = KBSMarkerDocPath(db);	// taken NOW, while the document is certainly alive
	sHasPending     = kTrue;
	sPendingTimer->StartTimer(KBSPendingMarkerProc, KBSDoubleClickInterval(), nil);
}

void KBSDrawEventHandler::ClearMarker()
{
	// ***** NOTHING ONCE ShutdownCleanup HAS RUN. ***** This file's own header says ClearMarker is
	// the wrong call at teardown because it repaints the marker's document, and that document may be
	// going away - yet the only thing stopping it from being called then was a SECOND shutdown flag,
	// owned by the expiry task and tested at its call site
	// (KBSMarkerExpiryIdleTask.cpp: "if (!sShutdown) ClearMarker()"). One rule, guarded in two
	// places, by two flags: whichever of them a future caller failed to know about, the guarantee
	// would be gone. The rule lives here now, where the repaint is (block 12 defect re-check,
	// 2026-08-11). The task keeps its own test as well - it is free, and it also stops the task
	// doing anything else on the way past.
	if (sMarkerShutdown)
		return;

	// ***** THE BOOKING GOES FIRST. ***** Taking an outstanding marker off here, rather than at the
	// place that knows about double clicks, is what lets every existing caller of ClearMarker do the
	// right thing without being changed - including the two that decide a double click's outcome. See
	// SetMarkerAfterClickSettles in the header.
	KBSCancelPendingMarker();

	// Disarm first - this is also the path the timer itself takes, where Stop() is a no-op
	// because the task has already come off the queue by then.
	KBSMarkerExpiryIdleTask::Stop();

	IDataBase* db = sMarkerDB;	// remember the document to repaint before we forget it
	sHasMarker    = kFalse;
	sMarkerDB     = nil;
	sMarkerSpread = kInvalidUID;
	sMarkerDocPath.Clear();
	KBSRepaintViews(db);
}

void KBSDrawEventHandler::ShutdownCleanup()
{
	// **The booking timer, and no more bookings ever. *Its callback is a raw function pointer into
	//   this .pln, so a live booking as the module goes down is a crash - the one thing here that is
	//   not merely tidiness. ClearMarker is still the wrong call (it repaints), so the booking is
	//   dropped by hand instead.
	sMarkerShutdown = kTrue;
	if (sPendingTimer != nil)
	{
		sPendingTimer->StopTimer();
		sPendingTimer->Release();		// the reference ::CreateObject handed over
		sPendingTimer = nil;
	}
	sHasPending    = kFalse;
	sPendingDB     = nil;
	sPendingSpread = kInvalidUID;
	sPendingDocPath.Clear();			// a static PMString - emptied for the reason sMarkerDocPath is

	// State only - no repaint, nothing asked of any document. See the header: at this point the
	// marker's document may already be going away, and KBSRepaintViews would go looking for it.
	// The idle task that would otherwise clear the marker has been retired just before this
	// (KBSMarkerExpiryIdleTask::Shutdown), so nothing is left to fire either.
	sHasMarker    = kFalse;
	sMarkerDB     = nil;
	sMarkerSpread = kInvalidUID;
	sMarkerDocPath.Clear();
}

//----------------------------------------------------------------------------------------
// Registration
//----------------------------------------------------------------------------------------
void KBSDrawEventHandler::Register(IDrwEvtDispatcher* d)
{
	// Per-spread, drawn on the spread front, port in spread coordinates.
	d->RegisterHandler(ClassID(kEndSpreadMessage), this, kDEHLowestPriority);
}

void KBSDrawEventHandler::UnRegister(IDrwEvtDispatcher* d)
{
	d->UnRegisterHandler(ClassID(kEndSpreadMessage), this);
}

//----------------------------------------------------------------------------------------
// Draw
//----------------------------------------------------------------------------------------
bool16 KBSDrawEventHandler::HandleDrawEvent(ClassID eventID, void* eventData)
{
	if (eventID != ClassID(kEndSpreadMessage))
		return kFalse;
	if (!sHasMarker || sMarkerDB == nil)
		return kFalse;

	DrawEventData* ded = static_cast<DrawEventData*>(eventData);
	if (ded == nil || ded->gd == nil)
		return kFalse;

	// Screen only: never draw when printing.
	//
	// Print preview (IShape::kPreviewMode) is deliberately NOT excluded here, and that is a departure
	// from how the app draws its own screen-only marks: DynamicSpellCheckAdornment (the dynamic spell
	// check squiggle) tests kPrinting and kPreviewMode together, in its Draw and again in its
	// GetIsActive. The marker is a navigation aid rather than a mark on the artwork, so it should
	// stay visible while previewing (user's call, 2026-07-31). Do not "fix" this to match the
	// adornments. Overprint Preview is a different flag and IS excluded, just below.
	if ((ded->flags & IShape::kPrinting) != 0)
		return kFalse;

	// Overprint Preview simulates printed output on screen; the marker is a screen-only aid, so
	// hide it there. kSepPrvOPPEnabledVPAttr is set while Overprint Preview is active.
	IViewPortAttributes* vpa = ded->gd->GetViewPortAttributes();
	if (vpa != nil && vpa->GetAttr(kSepPrvOPPEnabledVPAttr, 0) != 0)
		return kFalse;

	IGraphicsPort* gPort = ded->gd->GetGraphicsPort();
	if (gPort == nil)
		return kFalse;

	// changedBy = the spread being drawn.
	InterfacePtr<ISpread> spread(ded->changedBy, UseDefaultIID());
	if (spread == nil)
		return kFalse;
	IDataBase* db = ::GetDataBase(ded->changedBy);
	if (db == nil || db != sMarkerDB)
		return kFalse;

	// Drop the marker if its document has been closed (draw only fires for open documents). Do
	// NOT invalidate from inside a draw event.
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		if (docList != nil && docList->FindDocByDataBase(sMarkerDB) == nil)
		{
			sHasMarker = kFalse;
			sMarkerDB  = nil;
			sMarkerDocPath.Clear();
			return kFalse;
		}
	}

	// ***** SAME ADDRESS IS NOT SAME DOCUMENT. ***** The test above finds the address on the
	// document list, which is exactly what a REUSED address does too: close the document the marker
	// was set in, open another, and this spread's database can be the marker's old pointer wearing a
	// different document. Both tests then pass and the marker is painted over somebody else's page.
	//
	// A book run makes this reachable rather than theoretical - it opens and closes a chapter at a
	// time, so addresses are being handed round throughout (KBSBookScope::ReleaseHeldDoc), and a
	// jump can leave a marker standing while the next run does it.
	//
	// So the file decides. 'db' is the spread being drawn, which is open by definition, so asking it
	// is safe; sMarkerDB is never asked, only compared. Two empty paths mean neither document has
	// ever been saved - there is nothing to tell them apart with, and the address stands alone as it
	// always did.
	{
		const PMString drawnPath(KBSMarkerDocPath(db));
		if ((!drawnPath.IsEmpty() || !sMarkerDocPath.IsEmpty()) && !(drawnPath == sMarkerDocPath))
		{
			sHasMarker = kFalse;
			sMarkerDB  = nil;
			sMarkerDocPath.Clear();
			return kFalse;
		}
	}

	if (spread->GetNumPages() < 1)
		return kFalse;

	// ***** WHICH SPREAD OWNS THE MARKER IS THE JUMP'S ANSWER, NOT A MEASUREMENT. ***** The jump has
	// already resolved the match's frame and asked IPasteboardUtils which spread holds it
	// (KBSJump::SpreadForMatch); that UID rides along in sMarkerSpread, and a UID comparison cannot
	// be ambiguous.
	//
	// It WAS a measurement - "is the marker's centre inside this spread's bounding box" - and the
	// sentence justifying that read "spreads do not overlap in pasteboard space, so only the owning
	// spread passes". That is true of GetPagesBounds. It stopped being true on 2026-08-08, in the
	// very change that made this box include items: GetPagesAndItemsBounds encloses "all the pages on
	// the spread PLUS any page items sitting on the pasteboard" (ISpread.h:229-238), so an item
	// dragged far enough off its own spread stretches that spread's box over the NEXT one - and both
	// spreads then paint the same marker, one of them in the wrong place. The reason was written for
	// the pages-only box and was not re-read when the box changed (block 12 defect re-check,
	// 2026-08-11).
	//
	// kInvalidUID means the jump could not name a spread. The geometric test is kept for that case
	// alone: it is what the marker had before, so nothing that used to be drawn stops being drawn.
	const PMRect pbBounds     = spread->GetPagesAndItemsBounds(Transform::PasteboardCoordinates());
	const PMRect spreadBounds = spread->GetPagesAndItemsBounds(Transform::SpreadCoordinates());
	{
		if (sMarkerSpread != kInvalidUID)
		{
			if (::GetUID(spread) != sMarkerSpread)
				return kFalse;
		}
		else
		{
			// The marker's centre is the point tested - a rectangle lying across a spread boundary
			// belongs to the spread holding most of it.
			const PMPoint centre((sMarkerPb.Left() + sMarkerPb.Right()) / PMReal(2.0),
				(sMarkerPb.Top() + sMarkerPb.Bottom()) / PMReal(2.0));
			if (!pbBounds.PointIn(centre))
				return kFalse;
		}

		// ***** THE TWO BOXES ARE STILL BOTH NEEDED - for the OFFSET. ***** ISpread returns the same
		// box in whichever coordinate space is asked for, so the difference between them IS the
		// pasteboard->spread offset: the same box measured twice. That is unaffected by which box is
		// used (any box measured in both spaces gives the same difference), so it stays on the
		// and-items one. The worked example for asking both ways is snapshot/SnapTracker.cpp:599-603.
		// Until the block 12 API audit (2026-08-08) this was thirty lines building two matrices off
		// page 0 and walking every page by hand - see the api-official-examples ledger.
		//
		// ***** AND-ITEMS, not GetPagesBounds - which is why the marker appears on the pasteboard at
		// all. ***** The pages-only box is what the hand-written walk tested against, so a hit in a
		// frame sitting outside every page failed it and its marker was never drawn: the jump
		// scrolled there correctly and then pointed at nothing. Guides are left out (includeGuides
		// defaults to kFalse): a guide cannot hold text. Confirmed on the running application
		// 2026-08-08, by eye, on a document holding one hit inside a page, one on the right-hand page
		// of a facing-pages spread, and one out on the pasteboard: all three are marked. (By eye
		// because there is no other way - this drawing does not appear in a screen capture at all,
		// which cost an afternoon to establish.)

		// Marker rectangle in this spread's coordinates. spread = pasteboard - offset.
		const PMReal offX = pbBounds.Left() - spreadBounds.Left();
		const PMReal offY = pbBounds.Top()  - spreadBounds.Top();

		const PMReal left   = sMarkerPb.Left()   - offX;
		const PMReal top    = sMarkerPb.Top()    - offY;
		const PMReal right  = sMarkerPb.Right()  - offX;
		const PMReal bottom = sMarkerPb.Bottom() - offY;
		const PMReal w = right - left;
		const PMReal h = bottom - top;
		if (w <= 0 || h <= 0)
			return kFalse;

		// Invert the pixels under the marker so it shows up on ANY background. A red rectangle is
		// invisible on a red page, which is what this replaced; an inversion cannot be lost in the
		// artwork because it is defined by whatever is underneath it. White over Difference gives
		// (1 - backdrop) = a full inversion. Text stays readable because the glyphs and their
		// background invert separately (black text on red becomes white text on cyan).
		//
		// The blending mode IS part of the graphics state, so AutoGSave restores it. Do NOT wrap
		// this in a transparency group: the group would be composited in isolation, leaving no
		// backdrop to invert against. (XOR via IRasterPort::SetXORMode was tried first and
		// rejected - it inverts glyphs and background as one, so the text stopped being readable.)
		AutoGSave ag(gPort);
		gPort->setblendingmode(kPMBlendDifference);
		gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
		gPort->rectfill(left, top, w, h);
	}

	return kFalse;	// let other handlers / drawing continue
}

//========================================================================================
// KBSDrawEventSrvc
//   Registers this boss as a kDrawEventService provider so the app hooks the sibling
//   IDrwEvtHandler into the draw-event dispatcher at startup.
//========================================================================================
class KBSDrawEventSrvc : public CServiceProvider
{
public:
	KBSDrawEventSrvc(IPMUnknown* boss) : CServiceProvider(boss) {}
	~KBSDrawEventSrvc() {}

	virtual ServiceID GetServiceID() { return kDrawEventService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// SetCString, not SetKey: this is an internal service name that never reaches the UI, so it must
	// not be handed to the string table as a translation key (MotionPathDrawService, the app's own
	// draw-event service, spells it this way; the SDK sample uses SetKey).
	virtual void GetName(PMString* pName) { pName->SetCString("KBSDrawEventSrvc\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KBSDrawEventSrvc, kKBSDrawEventSrvcImpl)

// End, KBSDrawEventHandler.cpp.
