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
//      the user closes a searched document -> its rows go, and the list stays while it is one to keep
//      (KFCTierStore::LiveListIsKeepable - a row left, or a search made it): an All Documents list keeps
//      the other documents' rows, and a list a search made keeps its heading - "(0/0)" once its last
//      document is gone (the author: "only the closed document's part goes"). Only a
//      list that is no tier any more goes, the panel back to empty (Respond). The kept tiers lose that
//      document's rows too, their headings staying (KFCTierStore::ForgetDocument).
//
//  Book scope is deliberately left alone here - for the RESULTS. A book chapter carries its .indd
//  file, so it can be reopened (KFCBookScope::ReopenChapterDoc) and dropping twenty chapters'
//  results because one chapter was closed would throw away work. Book results are retired when the
//  BOOK closes, by KFCBookWatch (an observer: a book close has no signal to respond to).
//
//  Some bookkeeping DOES run for every close, whatever the scope: everything that holds the closing document by address
//  lets it go (the held-chapter list, the jump marker, the rows' text foci, the Undo follow, the kept tiers' chapters,
//  the remembered Find Format, the copy store) - LetGoOfClosingDocument, ahead of every exit in Respond bar the
//  main-thread gate and a signal that names no document.
//
//  Why kBeforeCloseDoc and not kAfterCloseDoc: the signal data still carries a live IDocument
//  before the close, and carries nil after it - the document we have to compare against is only
//  available in the "before" signal (see memory signal-responder-catalog).
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
#include "IDataBase.h"			// GetSysFile - the file a closing document lives in, kept on its emptied chapter
#include "IDocumentSignalData.h"
#include "ISignalMgr.h"

// General includes:
#include "CResponder.h"
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - the gate in Respond

// Project includes:
#include "KFCBookScope.h"		// ForgetHeldDoc
#include "KFCCopyStore.h"		// DocumentClosing - the copy store of a closing document
#include "KFCDiag.h"			// KFC_DIAG_LOG - which way a close went, in a test build (compiled out of a shipping one)
#include "KFCDiagWrittenDocs.h"	// KFC_DIAG_DOC_CLOSING - a test build's written-document check (nothing in a shipping one)
#include "KFCHitMarker.h"		// ForgetDoc - the jump marker lets go of a closing document
#include "KFCID.h"
#include "KFCResultModel.h"
#include "KFCRowFoci.h"			// DetachDocument - the rows' text foci let go of a closing document
#include "KFCModelNotify.h"		// the panel is told, never called (the model/UI split)
#include "KFCRunGuard.h"		// never retire results out from under a run of ours
#include "KFCSearchEngine.h"	// DropResults - the rows, the searched book and the find format, together; ForgetSearchedFindFormatOf
#include "KFCTierStore.h"		// ForgetDocument - the kept tiers' chapters emptied; LiveListIsKeepable - a list that stays
#include "KFCUndoFollow.h"		// DocumentClosing, ForgetBookChapter, ForgetClosingDocument - a closing document let go

/** Takes a closed document's rows out of a document-scope result set - the list itself goes only once it is no tier.

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

// EVERY CLOSE, WHATEVER THE SCOPE: everything that holds the closing document by address lets it go - what the file
// header names, in one place. Respond calls it ahead of every exit it has (only the main-thread gate and a document the
// signal does not name come first): a reused address can hand any of them the next document opened in this one's place.
static void LetGoOfClosingDocument(const UIDRef& closingDocRef)
{
	// The held list. A held chapter someone ELSE closes (the user, after the book panel windowed it; a script) would
	// otherwise stay on gHeldDocs, its (IDataBase*, UID) dangling - and a reused address can make IsDocStillOpen answer
	// YES about a DIFFERENT windowless document, which a later ReleaseHeldDocs would then close (the same address-reuse
	// fault memory uidref-reuse-after-close records, aimed at somebody else's document). Forgetting it here removes the
	// stale entry at its source. Safe on every path: ForgetHeldDoc does nothing when the document is not held, and the
	// closes KFC schedules itself come off the list BEFORE their Close call, so this is a no-op for them - which is why
	// it may run even while a run of ours is going (Respond's run guard comes after it).
	KFCBookScope::ForgetHeldDoc(closingDocRef);

	// The jump marker, for the same reason as the held list. State only - the document is on its way out, so nothing is
	// repainted (KFCHitMarker::ForgetDoc).
	KFCHitMarker::ForgetDoc(closingDocRef.GetDataBase());

	// The rows' text foci on the closing document: it is still alive here (kBeforeCloseDoc) and is not after
	// (KFCRowFoci::DetachDocument).
	KFCRowFoci::DetachDocument(closingDocRef.GetDataBase());

	// The Undo follow takes its observer off the closing document (KFCUndoFollow::DocumentClosing): what was attached is
	// detached.
	KFCUndoFollow::DocumentClosing(closingDocRef);

	// The kept tiers' chapters of the closing document are emptied - found by file while the document is still open here
	// (KFCTierStore::ForgetDocument), as a kept query run's list has them emptied (KFCUndoFollow).
	KFCTierStore::ForgetDocument(closingDocRef.GetDataBase());

	// The Find Format the rows were searched with lets go of the closing database if its UIDs are in it:
	// FindFormatHasChanged tells databases apart by address, and this one's can be handed to the next document opened
	// (KFCSearchEngine::ForgetSearchedFindFormatOf).
	KFCSearchEngine::ForgetSearchedFindFormatOf(closingDocRef.GetDataBase());

	// The copy store of the closing document (Restore from Copy) lets go of its address: one never saved is let go
	// whole - no row can reach that document again - and a saved one's keeps the file it is found again by
	// (KFCCopyStore::DocumentClosing).
	KFCCopyStore::DocumentClosing(closingDocRef.GetDataBase());
}

void KFCCloseDocResponder::Respond(ISignalMgr* signalMgr)
{
	if (signalMgr == nil)
		return;

	// The main thread only (kModelPlugIn - docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 6).
	// A model plug-in's responder is also called on a background task's thread, where the document "closing" is the
	// task's own copy (an export's clone) - and the results are about the user's document, which stays open.
	// (A test build says when the gate turned one away - the thread test counts them: a test that never saw one has
	// not tried the gate.)
	if (!IDThreading::IsMainThreadDomain())
	{
		KFC_DIAG_LOG("CLOSE off the main thread - a background task's copy, left alone");
		return;
	}

	// GetDocument hands back the document's UIDRef (not an IDocument*), and the type allows gNull,
	// so it is tested before it is compared. NO CASE IS KNOWN TO PRODUCE THE NIL - not an unsaved
	// document either: measured, a never-saved document closed unsaved arrived here with a valid
	// UIDRef and its results were cleared like any other (run-unsaved-close-test.ps1;
	// docs/ai-notes/kbs-replace-path-audit-2026-08-08.md). Were it otherwise, its document-scope results
	// would outlive their document, and a row's Replace would ask IsDocStillOpen of a dead UIDRef (the
	// address-reuse fault, memory uidref-reuse-after-close).
	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;
	const UIDRef closingDocRef = signalData->GetDocument();
	if (closingDocRef == UIDRef::gNull)
		return;
	KFC_DIAG_LOG("CLOSE doc=%p uid=%u", (void*)closingDocRef.GetDataBase(), closingDocRef.GetUID().Get());
	// (A test build's written-document check, for every close and while the document is whole: a close KFC asked for that
	// finds work unsaved in it, or a document KFC wrote going with nothing unsaved and its file not written since -
	// KFCDiagWrittenDocs.h.)
	KFC_DIAG_DOC_CLOSING(closingDocRef);

	// Everything that holds the closing document by address lets it go - for every close, ahead of every exit below.
	LetGoOfClosingDocument(closingDocRef);

	// ...and a chapter of a book's list leaves the kept writes, so that a query run that wrote it is still followed
	// in the chapters left open, the closed one keeping the rows it has (KFCUndoFollow::ForgetBookChapter - a document of
	// any other list, or of none, is let go below, its chapter emptied: ForgetClosingDocument). Ahead of the run guard
	// below: a query run closes the chapters it holds at its commit point, before it clears the list, and a write kept
	// from before it may have written one of them (saved since, so it closes). (The chapters a query run hands back at its end
	// close after the list is cleared - IsFromBook is false by then. A write kept from before it wrote none of them, short
	// of a window that could not be opened: KFC gives a chapter it writes a window, and a chapter with a window is no
	// longer held - KFCBookScope::ShowChapterWindow.)
	if (KFCResultModel::IsFromBook())
		KFCUndoFollow::ForgetBookChapter(closingDocRef);

	// NEVER while a run of ours is going. This throws the result model away, and a run is filling
	// that model chapter by chapter - and the closes a run makes ON THE SPOT land here from inside it:
	// each chapter it hands back as it goes (KFCBookScope::HandBackHeldDocNow) and the held chapters it
	// closes on the spot (ReleaseHeldDocs(true) - at its commit point, and where nothing could be run). (A
	// SCHEDULED close lands only once the current tick has unwound - ReleaseHeldDocs from DropResults after the
	// run, and the Hide Previous Chapter sweep, which is a jump's and never inside a run at all.) The run puts
	// its own results up when it finishes, so nothing stale survives being skipped here. Same rule as the
	// book-close watcher's, asked the same way (KFCRunGuard).
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

	// THE KEPT WRITES LET IT GO, WHATEVER THE LIST SHOWS (the author's call - the spec map's UNDO-12 for a query
	// run): All Documents, one document, or no list at all - a query run leaves it empty (NO LIST in KFCQuerySequence.cpp)
	// while its write, kept, still names this document. Its stories and items come off every write and its chapter is
	// emptied in every kept list (KFCUndoFollow::ForgetClosingDocument - a chapter of a kept book's list is frozen
	// instead), so an Undo in a document still open is still followed (case xq-alldocs-close-undo). Past the run guard,
	// not ahead of it: the chapters a query run hands back close before the run is kept, and nothing would yet lead back
	// from the list it leaves to the writes kept for the list it found.
	KFCUndoFollow::ForgetClosingDocument(closingDocRef);

	// Nothing on display, nothing to retire. This signal fires for every document close in the
	// session, so past the held-list bookkeeping above it leaves as early as it can.
	if (KFCResultModel::GetChapterCount() <= 0)
		return;

	// A Document / Story / Selection result set is one chapter - the searched document - and an All Documents one is a
	// chapter per open document: the closing one's, found by its binding (KFCResultModel::GetDocumentChapter).
	const int32 closingChapter = KFCResultModel::GetDocumentChapter(closingDocRef);
	if (closingChapter < 0)
	{
		KFC_DIAG_LOG("CLOSE - not a document of the list");
		return;
	}

	// ONLY THAT DOCUMENT'S ROWS GO (the author's calls: an All Documents list's - the others are still open, their rows
	// and their Undo stay; every list's - "can the title part stay? only the closed document's part goes"). The chapter
	// keeps its place in the model (KFCResultModel::CloseChapter says why); the kept writes let the document go above
	// (KFCUndoFollow::ForgetClosingDocument). Only that row goes, and the others stay as they are (open or closed): the
	// panel draws every row a list holds (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md
	// F9), so taking one out brings no other into view.
	PMString closedName;
	int32 closedCount = 0;
	KFCResultModel::GetChapterDisplay(closingChapter, closedName, closedCount);
	KFC_DIAG_LOG("CLOSE - chapter %d's %d row(s) go", closingChapter, closedCount);
	KFCNotifyChapterRowGoes(closingChapter);
	// ...the file it is closed from kept on its emptied chapter (Chapter::closedFile): the heading's
	// Search These Results Again finds the document open again by it - it never reopens one (LIFE-01).
	IDataBase* const closingDB = closingDocRef.GetDataBase();
	KFCResultModel::CloseChapter(closingChapter, closingDB != nil ? closingDB->GetSysFile() : nil);

	// THE LIST STAYS WHILE IT IS ONE TO KEEP (KFCTierStore::LiveListIsKeepable - the one answer: a row left, or a search
	// made it): the other documents' rows, or its heading alone - "(0/0)" once its last document is gone, as a search
	// that found nothing reads. The heading and the book row read the counts left (RefreshRows repaints them).
	if (KFCTierStore::LiveListIsKeepable())
	{
		KFCNotifyRefreshRows();
		PMString msg(closedName);
		msg.SetTranslatable(kFalse);
		msg.Append(KFCResultModel::GetDisplayChapterCount() > 0
			? " was closed - its rows are gone. The other documents' rows stay." : " was closed - its rows are gone.");
		KFCNotifyStatus(msg);
		return;
	}

	// Back to empty - a list that is no tier any more (no row left, and no search made it: a query run's). Rebuild draws
	// the now-empty model, and the status line says why the rows went - a panel that empties itself without a word reads
	// as a crash.
	//
	// DropResults, without exception: a document-scope result set has no book behind it, so its
	// ReleaseSearchedBook finds no book to let go here (a chapter an earlier book run had to keep is tried
	// again, as at every DropResults) - it is one rule with no exceptions to remember.
	KFC_DIAG_LOG("CLOSE - chapter %d was the list's last, and the list is no tier: the results are dropped", closingChapter);
	KFCSearchEngine::DropResults();
	KFCNotifyRebuild();

	PMString cleared("Results cleared - the document was closed.");
	cleared.SetTranslatable(kFalse);
	KFCNotifyStatus(cleared);
}

// End, KFCCloseDocResponder.cpp.
