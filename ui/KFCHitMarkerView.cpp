//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCHitMarkerView.h. UI side.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "ISession.h"

// General includes:
#include "ILayoutUIUtils.h"
#include "ILayoutUtils.h"			// InvalidateViews
#include "Utils.h"

// Project includes:
#include "KFCModelAccess.h"		// the model half, through its session interfaces (the model/UI split)
#include "KFCHitMarkerView.h"
#include "KFCMarkerExpiryIdleTask.h"

namespace
{
// Repaint a document so the marker appears or disappears now - an adornment is only consulted while
// text is being drawn. The address is resolved through the document list first, so a document that
// has closed in the meantime is never touched.
void KFCHitMarkerRepaint(IDataBase* db)
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
}

void KFCHitMarkerView::Show(IDataBase* db, UID storyUID, TextIndex start, TextIndex end)
{
	IDataBase* previousDB = nil;
	if (!KFCChapters()->SetMarker(db, storyUID, start, end, previousDB))
		return;
	if (previousDB != nil)
		KFCHitMarkerRepaint(previousDB);
	KFCHitMarkerRepaint(db);

	// A pointer, not a highlight: the countdown takes it away again (restarted by every jump).
	KFCMarkerExpiryIdleTask::Start();
}

void KFCHitMarkerView::Hide()
{
	IDataBase* db = nil;
	if (!KFCChapters()->ClearMarker(db))
		return;
	KFCMarkerExpiryIdleTask::Stop();
	if (db != nil)
		KFCHitMarkerRepaint(db);
}

// End, KFCHitMarkerView.cpp.
