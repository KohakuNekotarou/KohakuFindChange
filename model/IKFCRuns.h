//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCRuns - one of the three doors the UI half reaches the model half through (the model/UI split;
//  docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The RUNS and what they ask - the search and the replace family, whether one is
//  running, the Find/Change settings they follow, and the questions the jump asks of a row's text.
//  Each method forwards to the function named beside it in KFCModelServices.cpp; the contract is
//  written there (KFCSearchEngine.h / KFCReplaceEngine.h / KFCRunGuard.h /
//  KFCTrackChange.h / KFCOversetLocator.h).
//
//  ON kSessionBoss, NOT A FACADE ON kUtilsBoss. What is behind it is session STATE (the results, the held
//  chapters, the marker), and the guide's facades keep no global or static state (gs-04); the session is
//  where InDesign keeps its own session state (IBookManager, IClipboardController). The UI half reaches it
//  with KFCRuns() (KFCModelAccess.h).
//  EDITED BY HAND. The three doors and their implementation (KFCModelServices.cpp) were first generated from
//  one table (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py); methods have been added by hand since,
//  which the table does not know, so the generator is a record only and refuses to write. A new method goes
//  at the END of its interface - a vtable slot is a promise to every built caller.
//
//========================================================================================

#ifndef __IKFCRuns_h__
#define __IKFCRuns_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KFCBoundaryID.h"	// IID_IKFCRUNS
#include "KFCModelTypes.h"	// the types the methods carry

class IDataBase;

class IKFCRuns : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKFCRUNS };

	/** = KFCSearchEngine::SearchBook. */
	virtual int32 SearchBook(PMString& outSummary) = 0;
	/** = KFCReplaceEngine::ReplaceChecked. */
	virtual int32 ReplaceChecked(PMString& outSummary) = 0;
	/** = KFCReplaceEngine::ReplaceHit. */
	virtual bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) = 0;
	/** = KFCReplaceEngine::CanReplaceHit. */
	virtual bool CanReplaceHit(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCReplaceEngine::ReplaceStory. */
	virtual bool ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) = 0;
	/** = KFCReplaceEngine::CanReplaceStory. */
	virtual bool CanReplaceStory(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KFCReplaceEngine::ReplaceChapter. */
	virtual bool ReplaceChapter(int32 chapterIdx, PMString& outStatus) = 0;
	/** = KFCReplaceEngine::CanReplaceChapter. */
	virtual bool CanReplaceChapter(int32 chapterIdx) = 0;
	/** = KFCRunGuard::IsAnyRunning. */
	virtual bool IsAnyRunning() = 0;
	/** = KFCRunGuard::BusyMessage. */
	virtual const char* BusyMessage() = 0;
	/** = KFCSearchEngine::FindCommandName. */
	virtual const char* FindCommandName(bool bookScopeOn) = 0;
	/** = KFCSearchEngine::CanSearchTab. */
	virtual bool CanSearchTab(int32 mode) = 0;
	/** = KFCSearchEngine::CurrentSearchMode. */
	virtual int32 CurrentSearchMode() = 0;
	/** = KFCSearchEngine::TabName. */
	virtual const char* TabName(int32 mode) = 0;
	/** = KFCSearchEngine::CurrentSearchScope. */
	virtual int32 CurrentSearchScope() = 0;
	/** = KFCSearchEngine::SearchScopeForSelection. */
	virtual int32 SearchScopeForSelection(int32 scope) = 0;
	/** = KFCSearchEngine::SearchScopeName. */
	virtual const char* SearchScopeName(int32 scope) = 0;
	/** = KFCSearchEngine::EditableFrameForMatch. */
	virtual UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos) = 0;
	/** = KFCSearchEngine::IsPositionOverset. */
	virtual bool IsPositionOverset(const UIDRef& storyRef, TextIndex pos) = 0;
	/** = KFCFindOversetLocator. */
	virtual KFCOversetLoc FindOversetLocator(const UIDRef& storyRef, TextIndex pos) = 0;
	/** = KFCSearchEngine::RowReadsAsFound. */
	virtual bool RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db) = 0;
	/** = KFCSearchEngine::RelocateStaleRow. */
	virtual bool RelocateStaleRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID, TextIndex& ioStart, TextIndex& ioEnd) = 0;
	/** = KFCTrackChange::RefreshRowFromRecords. */
	virtual bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx) = 0;
	// Added by hand from here on - a new method goes below the last one, never between (a vtable slot is a
	// promise to every built caller).
	/** = KFCSearchEngine::SetQuery. */
	virtual bool SetQuery(const PMString& text, int32 mode) = 0;
	/** = KFCChangeAll::Run (2026-10-06) - Change All in Book (No List). */
	virtual int32 ChangeAll(PMString& outSummary) = 0;
	/** = KFCChangeAll::ClearResults. */
	virtual bool ClearResults(PMString& outStatus) = 0;
	/** = KFCSearchEngine::HasFindQueryNow - Change All's greying. */
	virtual bool HasFindQueryNow() = 0;
};

#endif // __IKFCRuns_h__
