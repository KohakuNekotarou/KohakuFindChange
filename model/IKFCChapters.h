//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCChapters - one of the three doors the UI half reaches the model half through (the model/UI split;
//  docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The CHAPTERS' DOCUMENTS - Book Scope, which documents are open and held, the windows they have
//  (asked of the UI half through IKFCUIServices), the active book - and the jump marker's place
//  (KFCHitMarker). Each method forwards to the KFCBookScope / KFCHitMarker function of the same
//  name, where the contract is written.
//
//  ON kSessionBoss, NOT A FACADE ON kUtilsBoss. What is behind it is session STATE (the results, the held
//  chapters, the marker), and the guide's facades keep no global or static state (gs-04); the session is
//  where InDesign keeps its own session state (IBookManager, IClipboardController). The UI half reaches it
//  with KFCChapters() (KFCModelAccess.h).
//  EDITED BY HAND. The three doors and their implementation (KFCModelServices.cpp) were first generated from
//  one table (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py); methods have been added by hand since,
//  which the table does not know, so the generator is a record only and refuses to write. A new method goes
//  at the END of its interface - a vtable slot is a promise to every built caller. One taken out moves every
//  slot below it, so both halves are built together then (nothing outside KFC calls these doors).
//
//========================================================================================

#ifndef __IKFCChapters_h__
#define __IKFCChapters_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KFCBoundaryID.h"	// IID_IKFCCHAPTERS
#include "KFCModelTypes.h"	// the types the methods carry

class IDataBase;

class IKFCChapters : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKFCCHAPTERS };

	/** = KFCBookScope::IsBookScopeOn. */
	virtual bool IsBookScopeOn() = 0;
	/** = KFCBookScope::SetBookScopeOn. */
	virtual void SetBookScopeOn(bool on) = 0;
	/** = KFCBookScope::HasScopeTarget. */
	virtual bool HasScopeTarget() = 0;
	/** = KFCBookScope::IsDocStillOpen. */
	virtual bool IsDocStillOpen(const UIDRef& docRef) = 0;
	/** = KFCBookScope::HasWindow. */
	virtual bool HasWindow(const UIDRef& docRef) = 0;
	/** = KFCBookScope::ForgetHeldDoc. */
	virtual void ForgetHeldDoc(const UIDRef& docRef) = 0;
	/** = KFCBookScope::ReachChapterDoc. */
	virtual bool ReachChapterDoc(const IDFile& file, UIDRef& ioDocRef) = 0;
	/** = KFCBookScope::CloseDisplayedDocsIfClean. */
	virtual void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc) = 0;
	/** = KFCBookScope::GetSearchedBookPath. */
	virtual bool GetSearchedBookPath(PMString& outPath) = 0;
	/** = KFCBookScope::MakeBookActive. */
	virtual bool MakeBookActive(const PMString& bookPath) = 0;
	/** = KFCHitMarker::SetMarker. */
	virtual bool SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB) = 0;
	/** = KFCHitMarker::ClearMarker. */
	virtual bool ClearMarker(IDataBase*& outDB) = 0;
	// Added by hand from here on - a new method goes below the last one, never between (a vtable slot is a
	// promise to every built caller).
	/** = KFCBookScope::IsSelectedDocumentsOn - the toggle Find/Change Selected Documents (Book). */
	virtual bool IsSelectedDocumentsOn() = 0;
	/** = KFCBookScope::SetSelectedDocumentsOn - just the flag. */
	virtual void SetSelectedDocumentsOn(bool on) = 0;
	/** = KFCBookScope::HandBackIfHeld (appended, 1.4.0 - 2026-10-10): a landing that reached a chapter and is leaving it
	    without its window hands it back (KFCJump's HandBackChapterOnExit). */
	virtual void HandBackIfHeld(const UIDRef& docRef) = 0;
	/** = KFCBookScope::IsHeldDoc (appended, 1.4.0 - 2026-10-10): asked by the test build's hidden-document check
	    (ui/KFCDiagHiddenDocs.cpp). */
	virtual bool IsHeldDoc(const UIDRef& docRef) = 0;
};

#endif // __IKFCChapters_h__
