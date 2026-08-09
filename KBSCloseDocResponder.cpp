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
//  Book scope is deliberately left alone here - for the RESULTS. A book chapter carries its .indd
//  file, so it can be reopened (KBSBookScope::ReopenChapterDoc) and dropping twenty chapters'
//  results because one chapter was closed would throw away work. Book results are retired by the
//  book-side responder instead, which is a separate step (IID_ICLOSEBOOKMSG has no example
//  anywhere in the SDK, so it gets its own round of testing).
//
//  One piece of bookkeeping DOES run for every close, whatever the scope: the closing document
//  comes off the held-chapter list (ForgetHeldDoc, 2026-08-09) - see the comment in Respond.
//
//  Why kBeforeCloseDoc and not kAfterCloseDoc: the signal data still carries a live IDocument
//  before the close, and carries nil after it - the document we have to compare against is only
//  available in the "before" signal (see [[signal-responder-catalog]]).
//
//  ***** AND "BEFORE" DOES NOT MEAN "BEFORE THE USER HAS DECIDED". ***** The standing warning about
//  this family of signals is that a "before close" is not a close, because the save prompt can still
//  be cancelled - which would leave this having thrown away the results of a document that is still
//  open. Measured 2026-08-08 (block 11 API audit, work/kbs-selftest/run-close-cancel-test.ps1): a
//  dirty document searched at document scope, closed with the UI on, put its save prompt up, and
//  pressing CANCEL left the document open with all of its rows still on the panel. So the signal
//  arrives once the close is going through, not while it can still be called off, and the warning
//  does not apply on this path. Worth having measured rather than reasoned about: a panel that
//  emptied itself while its document stayed open would read as work lost at random.
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
#include "KBSBookScope.h"		// ReleaseSearchedBook - paired with every result Clear()
#include "KBSID.h"
#include "KBSResultModel.h"
#include "KBSResultTree.h"
#include "KBSRunGuard.h"		// never retire results out from under a run of ours
#include "KBSSearchEngine.h"	// ForgetSearchedFindFormat - paired with every result Clear()

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

	// GetDocument hands back the document's UIDRef (not an IDocument*), and the type allows gNull,
	// so it is tested before it is compared. NO CASE IS KNOWN TO PRODUCE THE NIL: this test used to
	// say "an unsaved document being closed carries no reference to compare against", and that was
	// measured to be wrong on 2026-08-08 - a never-saved document, closed unsaved, arrived here
	// with a valid UIDRef and its results were cleared like any other (run-unsaved-close-test.ps1;
	// docs/ai-notes/kbs-replace-path-audit-2026-08-08.md).
	//
	// The distinction earns this much comment because of what the wrong reason concealed. If an
	// unsaved close really did pass through here, its document-scope results would outlive their
	// document - and a document-scope chapter carries no file, so the replace's resolve pass would
	// be left asking IsDocStillOpen of a dead UIDRef, which a reused address can answer YES for
	// about a DIFFERENT document (the 2026-08-04 fault). "This guard skips unsaved documents"
	// described a hole; what it actually is is a nil test in front of a comparison, with nothing
	// known to produce the nil.
	InterfacePtr<IDocumentSignalData> signalData(signalMgr, UseDefaultIID());
	if (signalData == nil)
		return;
	const UIDRef closingDocRef = signalData->GetDocument();
	if (closingDocRef == UIDRef::gNull)
		return;

	// ***** THE HELD LIST HEARS ABOUT EVERY CLOSE, ahead of every exit below. ***** Until
	// 2026-08-09 nothing took a held chapter off gHeldDocs when someone ELSE closed it (the user,
	// after the book panel windowed it; a script), so its (IDataBase*, UID) stayed on the list
	// dangling - and a reused address can make IsDocStillOpen answer YES about a DIFFERENT
	// windowless document, which a later ReleaseHeldDocs would then close (the same address-reuse
	// fault [[uidref-reuse-after-close]] records, aimed at somebody else's document). Forgetting it
	// here removes the stale entry at its source. Safe on every path: ForgetHeldDoc does nothing
	// when the document is not held, and the closes KBS schedules itself come off the list BEFORE
	// their Close call, so this is a no-op for them - which is why it may run even while a run of
	// ours is going (the guard below).
	KBSBookScope::ForgetHeldDoc(closingDocRef);

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

	// Nothing on display, nothing to retire. This signal fires for every document close in the
	// session, so past the held-list bookkeeping above it leaves as early as it can.
	const int32 chapterCount = KBSResultModel::GetChapterCount();
	if (chapterCount <= 0)
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
	//
	// ReleaseSearchedBook alongside, without exception: a document-scope result set has no book
	// behind it, so this call finds nothing to do every time it runs. It is here so that "every
	// KBSResultModel::Clear() is paired with one" stays a rule with no exceptions to remember -
	// the next person to add a Clear() elsewhere should not have to work out whether theirs counts.
	//
	// ...and the Find Format those rows were searched with, by the same rule and for the same
	// reason (2026-08-08: it was the one piece of result-set state that had been left out of it).
	KBSResultModel::Clear();
	KBSBookScope::ReleaseSearchedBook();
	KBSSearchEngine::ForgetSearchedFindFormat();
	KBSResultTree::Rebuild();

	PMString cleared("Results cleared - the document was closed.");
	cleared.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(cleared);
}

// End, KBSCloseDocResponder.cpp.
