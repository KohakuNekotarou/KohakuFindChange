//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSHitMarkerView.h. UI side. The repaint was KBSHitMarker.cpp's until 2026-10-01 (moved unchanged).
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
#include "KBSModelAccess.h"		// the model half, through its session interfaces (2026-10-01, the model/UI split)
#include "KBSHitMarkerView.h"
#include "KBSMarkerExpiryIdleTask.h"

namespace
{
// Repaint a document so the marker appears or disappears now - an adornment is only consulted while
// text is being drawn. The address is resolved through the document list first, so a document that
// has closed in the meantime is never touched (moved here from KBSDrawEventHandler unchanged).
void KBSHitMarkerRepaint(IDataBase* db)
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

void KBSHitMarkerView::Show(IDataBase* db, UID storyUID, TextIndex start, TextIndex end)
{
	IDataBase* previousDB = nil;
	if (!KBSChapters()->SetMarker(db, storyUID, start, end, previousDB))
		return;
	if (previousDB != nil)
		KBSHitMarkerRepaint(previousDB);
	KBSHitMarkerRepaint(db);

	// A pointer, not a highlight: the countdown takes it away again (restarted by every jump).
	KBSMarkerExpiryIdleTask::Start();
}

void KBSHitMarkerView::Hide()
{
	IDataBase* db = nil;
	if (!KBSChapters()->ClearMarker(db))
		return;
	KBSMarkerExpiryIdleTask::Stop();
	if (db != nil)
		KBSHitMarkerRepaint(db);
}

// End, KBSHitMarkerView.cpp.
