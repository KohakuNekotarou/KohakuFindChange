//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "A document has been saved as another file" (kAfterSaveAsDocSignalRespServiceImpl, KFC.fr): the rows of a
//  document-scope list take the document's new name there and then (the author's call - the spec map's LIFE-14).
//  The rows follow the document itself - its UIDRef, and a Save As keeps the database - so without this they would show
//  the name the search read: with B.indd saved as C.indd and B.indd's file opened again, the row "B.indd" wrote into
//  C.indd, the right document under the wrong name (measured without this responder - case ad-saveas-reopen-replace).
//  A book's list keeps its names: its rows stand for the book's chapters (KFCResultModel::RenameDocumentChapters).
//  The kept tiers' lists take the name as well (KFCTierStore::RenameDocument), and the lists kept for a query run's
//  Undo (KFCUndoFollow::RenameDocument) - a switch back or an Undo does not bring the old name back.
//  The name is read the way the SDK's docwatch reads it (DocWchUtils::DwAlert: IDocumentSignalData::GetDocument, then
//  IDocument::GetName).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"
#include "IDocumentSignalData.h"
#include "ISignalMgr.h"

// General includes:
#include "CResponder.h"
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - the gate in Respond

// Project includes:
#include "KFCDiag.h"			// KFC_DIAG_LOG - what was renamed, in a test build (compiled out of a shipping one)
#include "KFCID.h"
#include "KFCModelNotify.h"		// KFCNotifyRefreshRows - the rows repaint in place
#include "KFCResultModel.h"
#include "KFCRunGuard.h"		// not while a run of ours is going
#include "KFCTierStore.h"		// RenameDocument - the kept tiers' lists
#include "KFCUndoFollow.h"		// RenameDocument - the lists kept for a query run's Undo

/** Gives a document-scope list's rows the new name of their document after a Save As. One signal, so no
	ServiceProvider of its own (KFCCloseDocResponder says why). */
class KFCSaveAsDocResponder : public CResponder
{
public:
	KFCSaveAsDocResponder(IPMUnknown* boss) : CResponder(boss) {}
	virtual ~KFCSaveAsDocResponder() {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KFCSaveAsDocResponder, kKFCSaveAsDocResponderImpl)

void KFCSaveAsDocResponder::Respond(ISignalMgr* signalMgr)
{
	if (signalMgr == nil)
		return;
	// The main thread only: a model plug-in's responder is called on a background task's thread too (KFCCloseDocResponder
	// says why), and the list is the main thread's. (A test build says when the gate turned one away.)
	if (!IDThreading::IsMainThreadDomain())
	{
		KFC_DIAG_LOG("SAVEAS off the main thread - a background task's, left alone");
		return;
	}
	// Not while a run of ours is going - the list is the run's then (KFCCloseDocResponder's rule). The user cannot save
	// while a run's progress bar is up, and KFC itself never saves.
	if (KFCRunGuard::IsAnyRunning())
		return;
	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;
	const UIDRef savedRef = signalData->GetDocument();
	if (savedRef == UIDRef::gNull)
		return;
	// After the Save As, so the document names its new file.
	InterfacePtr<IDocument> doc(savedRef, UseDefaultIID());
	if (doc == nil)
		return;
	PMString name;
	doc->GetName(name);
	const int32 renamed = KFCResultModel::RenameDocumentChapters(savedRef, name);
	KFCUndoFollow::RenameDocument(savedRef, name);
	KFCTierStore::RenameDocument(savedRef, name);
	KFC_DIAG_LOG("SAVEAS doc=%p uid=%u renamed=%d", (void*)savedRef.GetDataBase(), savedRef.GetUID().Get(), renamed);
	if (renamed > 0)
		KFCNotifyRefreshRows();
}

// End, KFCSaveAsDocResponder.cpp.
