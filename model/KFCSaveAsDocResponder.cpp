//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "A document has been saved as another file" (kAfterSaveAsDocSignalRespServiceImpl, KFC.fr): the rows of a
//  document-scope list take the document's new name there and then (the author's call, 2026-10-11 - LIFE-14). The rows
//  always followed the document itself - its UIDRef, and a Save As keeps the database - but their name was the one the
//  search read, until the next search: with B.indd saved as C.indd and B.indd's file opened again, the row "B.indd"
//  wrote into C.indd (measured - case ad-saveas-reopen-replace), the right document under the wrong name.
//  A book's rows keep theirs: they stand for the book's chapters, and a chapter saved as another file is a copy the book
//  does not list (KFCResultModel::RenameDocumentChapters). The main thread only - a background task saves its own copies.
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
#include "KFCRunGuard.h"		// a run of ours puts its own list up (and reads every name afresh)
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
	// (A test build says when the main-thread gate turned one away, as KFCCloseDocResponder does.)
	if (!IDThreading::IsMainThreadDomain())
	{
		KFC_DIAG_LOG("SAVEAS off the main thread - a background task's copy, left alone");
		return;
	}
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
	name.SetTranslatable(kFalse);
	const int32 renamed = KFCResultModel::RenameDocumentChapters(savedRef, name);
	KFCUndoFollow::RenameDocument(savedRef, name);
	KFC_DIAG_LOG("SAVEAS doc=%p uid=%u renamed=%d", (void*)savedRef.GetDataBase(), savedRef.GetUID().Get(), renamed);
	if (renamed > 0)
		KFCNotifyRefreshRows();
}

// End, KFCSaveAsDocResponder.cpp.
