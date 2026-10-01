//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  IKBSChapters - one of the three doors the UI half reaches the model half through (2026-10-01, the
//  model/UI split; docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The CHAPTERS' DOCUMENTS - Book Scope, which documents are open and held, the windows they have
//  (asked of the UI half through IKBSUIServices), the active book - and the jump marker's place
//  (KBSHitMarker). Each method forwards to the KBSBookScope / KBSHitMarker function of the same
//  name, where the contract is written.
//
//  ***** ON kSessionBoss, NOT A FACADE ON kUtilsBoss. ***** What is behind it is session STATE (the results,
//  the held chapters, the marker), and the guide's facades keep no global or static state (gs-04); the
//  session is where InDesign keeps its own session state (IBookManager, IClipboardController). The UI
//  half reaches it with KBSChapters() (KBSModelAccess.h). Generated with its two siblings and their
//  implementation (KBSModelServices.cpp) from one table - work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py.
//
//========================================================================================

#ifndef __IKBSChapters_h__
#define __IKBSChapters_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KBSID.h"			// IID_IKBSCHAPTERS
#include "KBSModelTypes.h"	// the types the methods carry

class IDataBase;

class IKBSChapters : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKBSCHAPTERS };

	/** = KBSBookScope::IsBookScopeOn. */
	virtual bool IsBookScopeOn() = 0;
	/** = KBSBookScope::SetBookScopeOn. */
	virtual void SetBookScopeOn(bool on) = 0;
	/** = KBSBookScope::HasScopeTarget. */
	virtual bool HasScopeTarget() = 0;
	/** = KBSBookScope::IsDocStillOpen. */
	virtual bool IsDocStillOpen(const UIDRef& docRef) = 0;
	/** = KBSBookScope::HasWindow. */
	virtual bool HasWindow(const UIDRef& docRef) = 0;
	/** = KBSBookScope::ForgetHeldDoc. */
	virtual void ForgetHeldDoc(const UIDRef& docRef) = 0;
	/** = KBSBookScope::ReachChapterDoc. */
	virtual bool ReachChapterDoc(const IDFile& file, UIDRef& ioDocRef) = 0;
	/** = KBSBookScope::FindOpenChapterDoc. */
	virtual bool FindOpenChapterDoc(const IDFile& file, UIDRef& ioDocRef) = 0;
	/** = KBSBookScope::CloseDisplayedDocsIfClean. */
	virtual void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc) = 0;
	/** = KBSBookScope::GetSearchedBookPath. */
	virtual bool GetSearchedBookPath(PMString& outPath) = 0;
	/** = KBSBookScope::MakeBookActive. */
	virtual bool MakeBookActive(const PMString& bookPath) = 0;
	/** = KBSHitMarker::SetMarker. */
	virtual bool SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB) = 0;
	/** = KBSHitMarker::ClearMarker. */
	virtual bool ClearMarker(IDataBase*& outDB) = 0;
};

#endif // __IKBSChapters_h__
