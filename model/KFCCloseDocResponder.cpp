//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Result invalidation on document close. A result set names documents, and a result row that
//  names a document nobody has open any more is worse than no row at all: it still jumps, and
//  its Replace still writes - by silently reopening the very document the user just closed.
//  So the results go when their document does.
//
//  The rule this implements, for the DOCUMENT scope only:
//
//      the user closes the searched document -> the panel goes back to empty.
//
//  Book scope is deliberately left alone here - for the RESULTS. A book chapter carries its .indd
//  file, so it can be reopened (KFCBookScope::ReopenChapterDoc) and dropping twenty chapters'
//  results because one chapter was closed would throw away work. Book results are retired when the
//  BOOK closes, by KFCBookWatch (an observer: a book close has no signal to respond to).
//
//  One piece of bookkeeping DOES run for every close, whatever the scope: the closing document
//  comes off the held-chapter list (ForgetHeldDoc) - see the comment in Respond.
//
//  Why kBeforeCloseDoc and not kAfterCloseDoc: the signal data still carries a live IDocument
//  before the close, and carries nil after it - the document we have to compare against is only
//  available in the "before" signal (see [[signal-responder-catalog]]).
//
//  AND "BEFORE" DOES NOT MEAN "BEFORE THE USER HAS DECIDED". The standing warning about this family
//  of signals is that a "before close" is not a close, because the save prompt can still be
//  cancelled - which would leave this having thrown away the results of a document that is still
//  open. Measured (work/kbs-selftest/run-close-cancel-test.ps1): a
//  dirty document searched at document scope, closed with the UI on, put its save prompt up, and
//  pressing CANCEL left the document open with all of its rows still on the panel. So the signal
//  arrives once the close is going through, not while it can still be called off, and the warning
//  does not apply on this path. Worth having measured rather than reasoned about: a panel that
//  emptied itself while its document stayed open would read as work lost at random.
//
//  No "was it us who closed it?" guard is needed at document scope. KFC calls IDocFileHandler::Close
//  in KFCBookScope only, and none of its closes can close a searched document (named, not counted):
//    * ReleaseHeldDoc (which ReleaseHeldDocs goes through) closes only chapters a BOOK run opened
//      windowless, and document scope holds none (nothing goes on the held list without a chapter
//      file to open by) - nor does All Documents list one (KFCSearchEngine leaves held chapters out);
//    * the Hide Previous Chapter sweep (CloseDisplayedDocsIfClean) never runs over document-scope
//      results at all: a jump asks for it only while the results came from a BOOK
//      (KFCJump's ShouldHidePreviousChapter). (That the sweep spares the jumped-to document is no
//      reason: true of one document, and none at all once All Documents puts several on the list.)
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
#include "KFCBookScope.h"		// ForgetHeldDoc
#include "KFCDiag.h"			// KFC_DIAG_LOG - which way a close went, in a test build (compiled out of a shipping one)
#include "KFCHitMarker.h"		// ForgetDoc - the jump marker lets go of a closing document
#include "KFCID.h"
#include "KFCResultModel.h"
#include "KFCModelNotify.h"		// the panel is told, never called (the model/UI split)
#include "KFCRunGuard.h"		// never retire results out from under a run of ours
#include "KFCSearchEngine.h"	// DropResults - the rows, the searched book and the find format, together
#include "KFCUndoFollow.h"		// ForgetDocument / ForgetBookChapter - All Documents and a book let one document go

/** Retires a document-scope result set when its document is closed.

	There is no ServiceProvider here on purpose: a boss that answers ONE signal names the API's own
	provider implementation in the .fr (kBeforeCloseDocSignalRespServiceImpl) and writes only the
	responder. CServiceProvider + HasMultipleIDs is for a boss that answers several signals at once,
	the shape linksui/ClosingDocumentsResponder.cpp needs for its three.
*/
class KFCCloseDocResponder : public CResponder
{
public:
	KFCCloseDocResponder(IPMUnknown* boss) : CResponder(boss) {}
	virtual ~KFCCloseDocResponder() {}

	virtual void Respond(ISignalMgr* signalMgr);
};

CREATE_PMINTERFACE(KFCCloseDocResponder, kKFCCloseDocResponderImpl)

void KFCCloseDocResponder::Respond(ISignalMgr* signalMgr)
{
	if (signalMgr == nil)
		return;

	// The main thread only (kModelPlugIn - the split's design, section 6). A model plug-in's
	// responder is also called on a background task's thread, where the document "closing" is the task's
	// own copy (an export's clone) - and the results are about the user's document, which stays open.
	if (!IDThreading::IsMainThreadDomain())
		return;

	// GetDocument hands back the document's UIDRef (not an IDocument*), and the type allows gNull,
	// so it is tested before it is compared. NO CASE IS KNOWN TO PRODUCE THE NIL - not an unsaved
	// document either: measured, a never-saved document closed unsaved arrived here with a valid
	// UIDRef and its results were cleared like any other (run-unsaved-close-test.ps1;
	// docs/ai-notes/kbs-replace-path-audit-2026-08-08.md). Were it otherwise, its document-scope results
	// would outlive their document, and the replace would ask IsDocStillOpen of a dead UIDRef (the
	// address-reuse fault, [[uidref-reuse-after-close]]).
	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;
	const UIDRef closingDocRef = signalData->GetDocument();
	if (closingDocRef == UIDRef::gNull)
		return;
	KFC_DIAG_LOG("CLOSE doc=%p uid=%u", (void*)closingDocRef.GetDataBase(), closingDocRef.GetUID().Get());

	// THE HELD LIST HEARS ABOUT EVERY CLOSE, ahead of every exit below. A held chapter someone ELSE
	// closes (the user, after the book panel windowed it; a script) would otherwise stay on gHeldDocs,
	// its (IDataBase*, UID) dangling - and a reused address can make IsDocStillOpen answer YES about a
	// DIFFERENT windowless document, which a later ReleaseHeldDocs would then close (the same
	// address-reuse fault [[uidref-reuse-after-close]] records, aimed at somebody else's document). Forgetting it
	// here removes the stale entry at its source. Safe on every path: ForgetHeldDoc does nothing
	// when the document is not held, and the closes KFC schedules itself come off the list BEFORE
	// their Close call, so this is a no-op for them - which is why it may run even while a run of
	// ours is going (the guard below).
	KFCBookScope::ForgetHeldDoc(closingDocRef);

	// The jump marker forgets a closing document too, for every close and ahead of every exit below,
	// for the same reason as the held list: its address can be handed to the next document opened.
	// State only - the document is on its way out, so nothing is repainted (KFCHitMarker::ForgetDoc).
	KFCHitMarker::ForgetDoc(closingDocRef.GetDataBase());

	// ...and the Undo follow takes its observer off the closing document, for every close and ahead of every
	// exit (KFCUndoFollow::DocumentClosing): what was attached is detached.
	KFCUndoFollow::DocumentClosing(closingDocRef);

	// ...and a chapter of a book's list leaves the kept writes, so that a query run that wrote it is still followed
	// in the chapters left open, the closed one keeping the rows it has (KFCUndoFollow::ForgetBookChapter - All
	// Documents' ForgetDocument below empties the chapter instead, its rows leaving the list). Ahead of the run guard
	// below: a run hands back the chapters it left nothing in, closing them inside itself, and a write kept from
	// before it may have written one of them (saved since, so it closes).
	if (KFCResultModel::IsFromBook())
		KFCUndoFollow::ForgetBookChapter(closingDocRef);

	// NEVER while a run of ours is going. This throws the result model away, and a run is filling
	// that model chapter by chapter - and the closes a run makes ON THE SPOT land here from inside it:
	// each chapter it hands back as it goes (KFCBookScope::HandBackHeldDocNow) and the held chapters the
	// search and the query run close at their commit point (ReleaseHeldDocs(true)). (A SCHEDULED close lands
	// only once the current tick has unwound - ReleaseHeldDocs from DropResults after the run, and the Hide
	// Previous Chapter sweep, which is a jump's and never inside a run at all.) The run
	// puts its own results up when it finishes, so nothing stale survives being skipped here. Same rule as
	// the book-close watcher's, asked the same way (KFCRunGuard).
	if (KFCRunGuard::IsAnyRunning())
	{
		KFC_DIAG_LOG("CLOSE - a run of ours is up: the results are left to it");
		return;
	}

	// Book results survive a chapter closing - see the file header (the kept writes let it go above).
	if (KFCResultModel::IsFromBook())
	{
		KFC_DIAG_LOG("CLOSE - book results: they stay");
		return;
	}

	// Nothing on display, nothing to retire. This signal fires for every document close in the
	// session, so past the held-list bookkeeping above it leaves as early as it can.
	const int32 chapterCount = KFCResultModel::GetChapterCount();
	if (chapterCount <= 0)
		return;

	// A Document / Story / Selection result set is one chapter - the searched document - and an All
	// Documents one is a chapter per open document, so every chapter is compared.
	int32 closingChapter = -1;
	for (int32 ci = 0; ci < chapterCount; ++ci)
	{
		UIDRef chapterDocRef;
		IDFile chapterFile;
		if (!KFCResultModel::GetChapterLocation(ci, chapterDocRef, chapterFile))
			continue;
		if (chapterDocRef == closingDocRef)
		{
			closingChapter = ci;
			break;
		}
	}
	if (closingChapter < 0)
	{
		KFC_DIAG_LOG("CLOSE - not a document of the list");
		return;
	}

	// ALL DOCUMENTS: ONLY THAT DOCUMENT'S ROWS GO (the author's call). The list is the
	// open documents', and the others are still open - their rows and their Undo stay. The chapter
	// keeps its place in the model (KFCResultModel::CloseChapter says why) and the kept writes let the
	// document go too (KFCUndoFollow::ForgetDocument). When it was the last document with rows, the list
	// goes the way a Document list always has, below.
	if (KFCResultModel::GetSearchScope() == KFCResultModel::kScopeAllDocuments
		&& KFCResultModel::GetTotalHitCount() > KFCResultModel::GetHitCount(closingChapter))
	{
		PMString closedName;
		int32 closedCount = 0;
		KFCResultModel::GetChapterDisplay(closingChapter, closedName, closedCount);
		KFC_DIAG_LOG("CLOSE - All Documents: chapter %d's %d row(s) go, the others stay", closingChapter, closedCount);
		KFCUndoFollow::ForgetDocument(closingDocRef);
		// Over the display cap, taking one out can bring rows past the cap into view, which only a rebuild
		// draws; under it, only that row goes and the others stay as they are (open or closed). (One limit - spec
		// F9: the panel draws every row a list holds, so this rebuild is not reached now - kept for a cap that ever
		// differs again.)
		const bool overCap = KFCResultModel::GetTotalHitCount() > KFCResultModel::kKFCDisplayHitLimit;
		if (!overCap)
			KFCNotifyChapterRowGoes(closingChapter);
		KFCResultModel::CloseChapter(closingChapter);
		if (overCap)
			KFCNotifyRebuild();
		PMString msg(closedName);
		msg.SetTranslatable(kFalse);
		msg.Append(" was closed - its rows are gone. The other documents' rows stay.");
		KFCNotifyStatus(msg);
		return;
	}

	// Back to empty. Rebuild draws the now-empty model, and the status line says why the rows
	// went - a panel that empties itself without a word reads as a crash.
	//
	// DropResults, without exception: a document-scope result set has no book behind it, so its
	// ReleaseSearchedBook finds nothing to do every time it runs here - it is one rule with no
	// exceptions to remember.
	KFC_DIAG_LOG("CLOSE - chapter %d was the list's last: the results are dropped", closingChapter);
	KFCSearchEngine::DropResults();
	KFCNotifyRebuild();

	PMString cleared("Results cleared - the document was closed.");
	cleared.SetTranslatable(kFalse);
	KFCNotifyStatus(cleared);
}

// End, KFCCloseDocResponder.cpp.
