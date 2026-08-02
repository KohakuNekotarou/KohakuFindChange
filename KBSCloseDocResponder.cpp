//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result invalidation on document close. A result set names documents, and a result row that
//  names a document nobody has open any more is worse than no row at all: it still jumps, and
//  Change Checked still replaces - by silently reopening the very document the user just closed.
//  So the results go when their document does.
//
//  The rule this implements, for the DOCUMENT scope only:
//
//      the user closes the searched document -> the panel goes back to empty.
//
//  Book scope is deliberately left alone here. A book chapter carries its .indd file, so it can
//  be reopened (KBSBookScope::ReopenChapterDoc) and dropping twenty chapters' results because one
//  chapter was closed would throw away work. Book results are retired by the book-side responder
//  instead, which is a separate step (IID_ICLOSEBOOKMSG has no example anywhere in the SDK, so it
//  gets its own round of testing).
//
//  Why kBeforeCloseDoc and not kAfterCloseDoc: the signal data still carries a live IDocument
//  before the close, and carries nil after it - the document we have to compare against is only
//  available in the "before" signal (see [[signal-responder-catalog]]).
//
//  No "was it us who closed it?" guard is needed at document scope. KBS closes documents in
//  exactly two places, and neither can close the searched document: ReleaseHeldDocs only closes
//  chapters a BOOK search opened windowless (document scope holds none), and the Hide Previous
//  Chapter sweep passes the jumped-to document as its exception. A guard becomes necessary when
//  book results start being retired - it belongs with that step, not this one.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"
#include "IDocumentSignalData.h"
#include "ISignalMgr.h"

// General includes:
#include "CResponder.h"

// Project includes:
#include "KBSID.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"
#include "KBSRunGuard.h"		// never retire results out from under a run of ours

/** Retires a document-scope result set when its document is closed.

	There is no ServiceProvider here on purpose: a boss that answers ONE signal names the API's own
	provider implementation in the .fr (kBeforeCloseDocSignalRespServiceImpl) and writes only the
	responder. CServiceProvider + HasMultipleIDs is for a boss that answers several signals at once,
	the shape linksui/ClosingDocumentsResponder.cpp needs for its three.
*/
class KBSCloseDocResponder : public CResponder
{
public:
	KBSCloseDocResponder(IPMUnknown* boss) : CResponder(boss) {}
	virtual ~KBSCloseDocResponder() {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KBSCloseDocResponder, kKBSCloseDocResponderImpl)

void KBSCloseDocResponder::Respond(ISignalMgr* signalMgr)
{
	if (signalMgr == nil)
		return;

	// NEVER while a run of ours is going. This throws the result model away, and a run is filling
	// that model chapter by chapter - and closes the runs schedule themselves (the held-chapter
	// release, the Hide Previous Chapter sweep) can land here from inside one. The run puts its own
	// results up when it finishes, so nothing stale survives being skipped here. Same rule as the
	// book-close watcher's, asked the same way (KBSRunGuard).
	if (KBSRunGuard::IsAnyRunning())
		return;

	// Book results survive a chapter closing - see the file header.
	if (KBSResultModel::IsFromBook())
		return;

	// Nothing on display, nothing to retire. Also the common case: this signal fires for every
	// document close in the session, so it leaves as early as it can.
	const int32 chapterCount = KBSResultModel::GetChapterCount();
	if (chapterCount <= 0)
		return;

	// GetDocument hands back the document's UIDRef (not an IDocument*), and it may be gNull - an
	// unsaved document being closed carries no reference to compare against.
	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;
	const UIDRef closingDocRef = signalData->GetDocument();
	if (closingDocRef == UIDRef::gNull)
		return;

	// A document-scope result set is one chapter - the searched document - but compare against
	// every chapter rather than assuming index 0: this stays correct if the model is ever filled
	// from more than one document without a book.
	bool ours = false;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		UIDRef chapterDocRef;
		IDFile chapterFile;
		if (!KBSResultModel::GetChapterLocation(ci, chapterDocRef, chapterFile))
			continue;
		if (chapterDocRef == closingDocRef)
		{
			ours = true;
			break;
		}
	}
	if (!ours)
		return;

	// Back to empty. Rebuild draws the now-empty model, and the status line says why the rows
	// went - a panel that empties itself without a word reads as a crash.
	KBSResultModel::Clear();
	KBSResultTree::Rebuild();

	PMString cleared("Results cleared - the document was closed.");
	cleared.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(cleared);
}

// End, KBSCloseDocResponder.cpp.
