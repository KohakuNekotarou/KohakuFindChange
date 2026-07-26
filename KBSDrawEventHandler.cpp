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
#include "IGeometry.h"
#include "IApplication.h"
#include "IDocumentList.h"
#include "IDataBase.h"

// General includes:
#include "GraphicsID.h"			// kDrawEventService, IID_IDRWEVTHANDLER
#include "DocumentContextID.h"	// kEndSpreadMessage
#include "OutPrvID.h"			// kSepPrvOPPEnabledVPAttr (Overprint Preview detection)
#include "AutoGSave.h"
#include "PersistUtils.h"		// ::GetDataBase
#include "TransformUtils.h"		// ::InnerToSpreadMatrix / ::InnerToPasteboardMatrix
#include "ILayoutUIUtils.h"
#include "ILayoutUtils.h"		// InvalidateViews (reliable overlay repaint)
#include "IDocument.h"
#include "ISession.h"
#include "Utils.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMReal.h"
#include "GraphicTypes.h"		// kPMBlendDifference / kPMBlendExclusion (Phase A probe)
#include "PMString.h"

// Project includes:
#include "KBSID.h"
#include "KBSDrawEventHandler.h"
#include "KBSMarkerExpiryIdleTask.h"	// the countdown that takes the marker back off the screen

CREATE_PMINTERFACE(KBSDrawEventHandler, kKBSDrawEventHandlerImpl)

//----------------------------------------------------------------------------------------
// Shared marker state
//----------------------------------------------------------------------------------------
bool16     KBSDrawEventHandler::sHasMarker = kFalse;
PMRect     KBSDrawEventHandler::sMarkerPb  = PMRect(0, 0, 0, 0);
IDataBase* KBSDrawEventHandler::sMarkerDB  = nil;

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

void KBSDrawEventHandler::SetMarker(IDataBase* db, const PMRect& pbRect)
{
	sMarkerDB  = db;
	sMarkerPb  = pbRect;
	sHasMarker = kTrue;
	KBSRepaintViews(db);

	// The marker is a flash, not a highlight: hand it to the timer that takes it away again.
	// Restarting an already-running countdown is that call's job, so each jump shows its marker
	// for the full time.
	KBSMarkerExpiryIdleTask::Start();
}

void KBSDrawEventHandler::ClearMarker()
{
	// Disarm first - this is also the path the timer itself takes, where Stop() is a no-op
	// because the task has already come off the queue by then.
	KBSMarkerExpiryIdleTask::Stop();

	IDataBase* db = sMarkerDB;	// remember the document to repaint before we forget it
	sHasMarker = kFalse;
	sMarkerDB  = nil;
	KBSRepaintViews(db);
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
			return kFalse;
		}
	}

	const int32 np = spread->GetNumPages();
	if (np < 1)
		return kFalse;

	// Pasteboard->spread offset for this spread: map the same inner origin through
	// InnerToSpread and InnerToPasteboard; the difference is the offset. spread = pasteboard - offset.
	InterfacePtr<IGeometry> pg0(db, spread->GetNthPageUID(0), UseDefaultIID());
	if (pg0 == nil)
		return kFalse;
	{
		PMMatrix mS = ::InnerToSpreadMatrix(pg0);
		PMMatrix mP = ::InnerToPasteboardMatrix(pg0);
		PMPoint ps(0.0, 0.0), pp(0.0, 0.0);
		mS.Transform(&ps);
		mP.Transform(&pp);
		const PMReal offX = pp.X() - ps.X();
		const PMReal offY = pp.Y() - ps.Y();

		// Is the marker inside this spread? Test its centre against each page's pasteboard bounds.
		// Spreads do not overlap in pasteboard space, so only the owning spread passes.
		const PMReal cx = (sMarkerPb.Left() + sMarkerPb.Right()) / PMReal(2.0);
		const PMReal cy = (sMarkerPb.Top()  + sMarkerPb.Bottom()) / PMReal(2.0);
		bool16 inThisSpread = kFalse;
		for (int32 i = 0; i < np; ++i)
		{
			InterfacePtr<IGeometry> pgi(db, spread->GetNthPageUID(i), UseDefaultIID());
			if (pgi == nil)
				continue;
			PMRect pr = pgi->GetPathBoundingBox();
			PMMatrix mp = ::InnerToPasteboardMatrix(pgi);
			mp.Transform(&pr);
			if (cx >= pr.Left() && cx <= pr.Right() && cy >= pr.Top() && cy <= pr.Bottom())
			{
				inThisSpread = kTrue;
				break;
			}
		}
		if (!inThisSpread)
			return kFalse;

		// Marker rectangle in this spread's coordinates.
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
	virtual void GetName(PMString* pName) { pName->SetKey("KBSDrawEventSrvc\0"); }
	virtual IPlugIn::ThreadingPolicy GetThreadingPolicy() const { return IPlugIn::kMainThreadOnly; }
};

CREATE_PMINTERFACE(KBSDrawEventSrvc, kKBSDrawEventSrvcImpl)

// End, KBSDrawEventHandler.cpp.
