//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  IKBSRuns - one of the three doors the UI half reaches the model half through (2026-10-01, the
//  model/UI split; docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The RUNS and what they ask - the search, the replace family and Show Changes, whether one is
//  running, the Find/Change settings they follow, and the questions the jump asks of a row's text.
//  Each method forwards to the function named beside it in KBSModelServices.cpp; the contract is
//  written there (KBSSearchEngine.h / KBSReplaceEngine.h / KBSShowChanges.h / KBSRunGuard.h /
//  KBSTrackChange.h / KBSOversetLocator.h).
//
//  ***** ON kSessionBoss, NOT A FACADE ON kUtilsBoss. ***** What is behind it is session STATE (the results,
//  the held chapters, the marker), and the guide's facades keep no global or static state (gs-04); the
//  session is where InDesign keeps its own session state (IBookManager, IClipboardController). The UI
//  half reaches it with KBSRuns() (KBSModelAccess.h).
//  ***** EDITED BY HAND SINCE 2026-10-02. ***** Generated on 2026-10-01 with its two siblings and their
//  implementation (KBSModelServices.cpp) from one table (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py).
//  Methods have been added to IKBSRuns by hand since, which the table does not know: the generator is a
//  record now and refuses to write. A new method goes at the END of its interface - a vtable slot is a
//  promise to every built caller.
//
//========================================================================================

#ifndef __IKBSRuns_h__
#define __IKBSRuns_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KBSBoundaryID.h"	// IID_IKBSRUNS
#include "KBSModelTypes.h"	// the types the methods carry

class IDataBase;

class IKBSRuns : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKBSRUNS };

	/** = KBSSearchEngine::SearchBook. */
	virtual int32 SearchBook(PMString& outSummary) = 0;
	/** = KBSShowChanges::Run. */
	virtual int32 ShowChanges(PMString& outSummary) = 0;
	/** = KBSReplaceEngine::ReplaceChecked. */
	virtual int32 ReplaceChecked(PMString& outSummary) = 0;
	/** = KBSReplaceEngine::ReplaceHit. */
	virtual bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::RejectHit. */
	virtual bool RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::AcceptHit. */
	virtual bool AcceptHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::CanReplaceHit. */
	virtual bool CanReplaceHit(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSReplaceEngine::CanAcceptOrRejectHit. */
	virtual bool CanAcceptOrRejectHit(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSReplaceEngine::ReplaceStory. */
	virtual bool ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::RejectStory. */
	virtual bool RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::AcceptStory. */
	virtual bool AcceptStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::RedoStory. */
	virtual bool RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::CanReplaceStory. */
	virtual bool CanReplaceStory(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSReplaceEngine::CanRejectStory. */
	virtual bool CanRejectStory(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSReplaceEngine::CanRedoStory. */
	virtual bool CanRedoStory(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSReplaceEngine::ReplaceChapter. */
	virtual bool ReplaceChapter(int32 chapterIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::RejectChapter. */
	virtual bool RejectChapter(int32 chapterIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::RedoChapter. */
	virtual bool RedoChapter(int32 chapterIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::AcceptAllInChapter. */
	virtual bool AcceptAllInChapter(int32 chapterIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::CanReplaceChapter. */
	virtual bool CanReplaceChapter(int32 chapterIdx) = 0;
	/** = KBSReplaceEngine::CanRejectChapter. */
	virtual bool CanRejectChapter(int32 chapterIdx) = 0;
	/** = KBSReplaceEngine::CanRedoChapter. */
	virtual bool CanRedoChapter(int32 chapterIdx) = 0;
	/** = KBSReplaceEngine::CanAcceptAllInChapter. */
	virtual bool CanAcceptAllInChapter(int32 chapterIdx) = 0;
	/** = KBSReplaceEngine::RejectRun. */
	virtual bool RejectRun(int32 chapterIdx, int32 runIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::AcceptRun. */
	virtual bool AcceptRun(int32 chapterIdx, int32 runIdx, PMString& outStatus) = 0;
	/** = KBSReplaceEngine::CanRejectOrAcceptRun. */
	virtual bool CanRejectOrAcceptRun(int32 chapterIdx, int32 runIdx) = 0;
	/** = KBSRunGuard::IsAnyRunning. */
	virtual bool IsAnyRunning() = 0;
	/** = KBSRunGuard::BusyMessage. */
	virtual const char* BusyMessage() = 0;
	/** = KBSSearchEngine::FindCommandName. */
	virtual const char* FindCommandName(bool bookScopeOn) = 0;
	/** = KBSSearchEngine::CanSearchTab. */
	virtual bool CanSearchTab(int32 mode) = 0;
	/** = KBSSearchEngine::CurrentSearchMode. */
	virtual int32 CurrentSearchMode() = 0;
	/** = KBSSearchEngine::TabName. */
	virtual const char* TabName(int32 mode) = 0;
	/** = KBSSearchEngine::CurrentSearchScope. */
	virtual int32 CurrentSearchScope() = 0;
	/** = KBSSearchEngine::SearchScopeForSelection. */
	virtual int32 SearchScopeForSelection(int32 scope) = 0;
	/** = KBSSearchEngine::SearchScopeName. */
	virtual const char* SearchScopeName(int32 scope) = 0;
	/** = KBSSearchEngine::EditableFrameForMatch. */
	virtual UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos) = 0;
	/** = KBSSearchEngine::IsPositionOverset. */
	virtual bool IsPositionOverset(const UIDRef& storyRef, TextIndex pos) = 0;
	/** = KBSFindOversetLocator. */
	virtual KBSOversetLoc FindOversetLocator(const UIDRef& storyRef, TextIndex pos) = 0;
	/** = KBSSearchEngine::RowReadsAsFound. */
	virtual bool RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db) = 0;
	/** = KBSSearchEngine::RelocateStaleRow. */
	virtual bool RelocateStaleRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID, TextIndex& ioStart, TextIndex& ioEnd) = 0;
	/** = KBSTrackChange::RefreshRowFromRecords. */
	virtual bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSTrackChange::CurrentReplacedGroup. */
	virtual void CurrentReplacedGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows, bool& outRefreshed) = 0;
	// (2026-10-02, appended at the end: a vtable slot is a promise to every built caller.)
	/** = KBSTrackChange::RowChangeIsHidden. */
	virtual bool RowChangeIsHidden(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSReplaceEngine::StoryChangesHidden. */
	virtual bool StoryChangesHidden(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSSearchEngine::SetQuery (2026-10-02, appended at the end like the two above; given its mode on
	    2026-10-03, before any build of it had shipped). */
	virtual bool SetQuery(const PMString& text, int32 mode) = 0;
	/** = KBSTrackChange::HiddenTextAnchor (2026-10-04, appended at the end like the ones above - the jump asks
	    where a row under a hidden condition comes back to). */
	virtual TextIndex HiddenTextAnchor(const UIDRef& storyRef, TextIndex pos) = 0;
};

#endif // __IKBSRuns_h__
