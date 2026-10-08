//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  "A document has opened" (kAfterOpenDocSignalRespServiceImpl, KFC.fr): when it is a chapter of the results - a book's
//  chapter the search handed back, opened again by the USER (the Book panel, File > Open) - the chapter is bound to it
//  and its rows get their text foci (KFCResultModel::RebindChapterDoc -> KFCRowFoci::AttachChapter; spec T6 c). Without
//  this, what the user typed between opening it and the first jump into it would not be followed.
//  KFC's own opens (a search, a row's Replace, Change All, a query run) are runs and bind their chapters themselves -
//  skipped; a jump's reopen lands here too, and binding it twice is a no-op. The main thread only (a background task
//  opens its own copies - an export's clone).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocumentSignalData.h"
#include "ISignalMgr.h"

// General includes:
#include "CResponder.h"
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - the gate in Respond

// Project includes:
#include "KFCBookScope.h"		// ChapterHasFile, FindOpenChapterDoc - a chapter is matched by its FILE
#include "KFCID.h"
#include "KFCResultModel.h"
#include "KFCRunGuard.h"		// a run of ours binds its own chapters

/** Binds a results chapter to the document the user has just opened in its file. One signal, so no
	ServiceProvider of its own (KFCCloseDocResponder says why). */
class KFCOpenDocResponder : public CResponder
{
public:
	KFCOpenDocResponder(IPMUnknown* boss) : CResponder(boss) {}
	virtual ~KFCOpenDocResponder() {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KFCOpenDocResponder, kKFCOpenDocResponderImpl)

void KFCOpenDocResponder::Respond(ISignalMgr* signalMgr)
{
	if (signalMgr == nil || !IDThreading::IsMainThreadDomain() || KFCRunGuard::IsAnyRunning())
		return;
	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;
	const UIDRef openedRef = signalData->GetDocument();
	if (openedRef == UIDRef::gNull)
		return;
	const int32 chapters = KFCResultModel::GetChapterCount();
	for (int32 ci = 0; ci < chapters; ++ci)
	{
		UIDRef chapterDocRef;
		IDFile chapterFile;
		// A chapter with a file only - a book's. A document-scope chapter's document was open all along.
		if (!KFCResultModel::GetChapterLocation(ci, chapterDocRef, chapterFile) || !KFCBookScope::ChapterHasFile(chapterFile))
			continue;
		// By its file - the lookup every reach of a chapter makes, conversions of an older InDesign's chapter included
		// (FindOpenChapterDoc). Never by the UIDRef the results hold: that document may be closed, its address reused.
		UIDRef open(chapterDocRef);
		if (KFCBookScope::FindOpenChapterDoc(chapterFile, open) && open == openedRef)
		{
			KFCResultModel::RebindChapterDoc(ci, open);	// binds - and attaches the rows' foci (KFCRowFoci)
			return;
		}
	}
}

// End, KFCOpenDocResponder.cpp.
