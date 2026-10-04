//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The model half's three session interfaces (IKBSResults.h / IKBSRuns.h / IKBSChapters.h), each method
//  forwarding to the function it names - nothing is decided here (2026-10-01, the model/UI split).
//  ***** EDITED BY HAND SINCE 2026-10-02. ***** Generated on 2026-10-01 from one table with the headers
//  (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py); a method appended to a header since is forwarded
//  here by hand, and the table does not know it - the generator is a record now and refuses to write.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CPMUnknown.h"

#include "KBSID.h"				// the implementation ids
#include "IKBSChapters.h"
#include "IKBSResults.h"
#include "IKBSRuns.h"
#include "KBSBookScope.h"
#include "KBSHitMarker.h"
#include "KBSOversetLocator.h"
#include "KBSReplaceEngine.h"
#include "KBSResultModel.h"
#include "KBSRunGuard.h"
#include "KBSSearchEngine.h"
#include "KBSShowChanges.h"
#include "KBSTrackChange.h"

class KBSResultsSession : public CPMUnknown<IKBSResults>
{
public:
	KBSResultsSession(IPMUnknown* boss) : CPMUnknown<IKBSResults>(boss) {}

	virtual int32 GetChapterCount() { return KBSResultModel::GetChapterCount(); }
	virtual int32 GetTotalHitCount() { return KBSResultModel::GetTotalHitCount(); }
	virtual int32 GetDisplayChapterCount() { return KBSResultModel::GetDisplayChapterCount(); }
	virtual int32 GetDisplayHitCount(int32 chapterIdx) { return KBSResultModel::GetDisplayHitCount(chapterIdx); }
	virtual bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount) { return KBSResultModel::GetChapterDisplay(chapterIdx, outName, outHitCount); }
	virtual int32 GetShownChapter(int32 nth) { return KBSResultModel::GetShownChapter(nth); }
	virtual int32 GetShownChapterPos(int32 chapterIdx) { return KBSResultModel::GetShownChapterPos(chapterIdx); }
	virtual int32 GetDisplayFontCount(int32 chapterIdx) { return KBSResultModel::GetDisplayFontCount(chapterIdx); }
	virtual int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx) { return KBSResultModel::GetDisplayFontHitCount(chapterIdx, fontIdx); }
	virtual bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount) { return KBSResultModel::GetFontDisplay(chapterIdx, fontIdx, outName, outHitCount); }
	virtual int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth) { return KBSResultModel::GetFontGroupHit(chapterIdx, fontIdx, nth); }
	virtual int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx) { return KBSResultModel::GetHitFontGroup(chapterIdx, hitIdx); }
	virtual int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx) { return KBSResultModel::GetHitFontGroupPos(chapterIdx, hitIdx); }
	virtual int32 GetDisplayRunCount(int32 chapterIdx) { return KBSResultModel::GetDisplayRunCount(chapterIdx); }
	virtual bool GetRunDisplay(int32 chapterIdx, int32 runIdx, PMString& outLabel, int32& outHitCount) { return KBSResultModel::GetRunDisplay(chapterIdx, runIdx, outLabel, outHitCount); }
	virtual int32 GetDisplayRunGroupCount(int32 chapterIdx, int32 runIdx) { return KBSResultModel::GetDisplayRunGroupCount(chapterIdx, runIdx); }
	virtual int32 GetRunGroup(int32 chapterIdx, int32 runIdx, int32 nth) { return KBSResultModel::GetRunGroup(chapterIdx, runIdx, nth); }
	virtual int32 GetGroupRun(int32 chapterIdx, int32 groupIdx) { return KBSResultModel::GetGroupRun(chapterIdx, groupIdx); }
	virtual int32 GetGroupPosInRun(int32 chapterIdx, int32 groupIdx) { return KBSResultModel::GetGroupPosInRun(chapterIdx, groupIdx); }
	virtual bool IsStoryGroup(int32 chapterIdx, int32 groupIdx) { return KBSResultModel::IsStoryGroup(chapterIdx, groupIdx); }
	virtual int32 GetGroupCheckedCount(int32 chapterIdx, int32 groupIdx) { return KBSResultModel::GetGroupCheckedCount(chapterIdx, groupIdx); }
	virtual bool GetHitRow(int32 chapterIdx, int32 hitIdx, KBSResultModel::RowDisplay& out) { return KBSResultModel::GetHitRow(chapterIdx, hitIdx, out); }
	virtual bool GetHitDisplay(int32 chapterIdx, int32 hitIdx, PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost) { return KBSResultModel::GetHitDisplay(chapterIdx, hitIdx, outLocator, outPre, outMatch, outPost); }
	virtual bool GetRowsBefore(int32 chapterIdx, const std::vector<int32>& rows, PMString& outPre, PMString& outOriginal, PMString& outPost) { return KBSResultModel::GetRowsBefore(chapterIdx, rows, outPre, outOriginal, outPost); }
	virtual void MarkUpBreaksForDisplay(PMString& s) { KBSResultModel::MarkUpBreaksForDisplay(s); }
	virtual bool IsFromBook() { return KBSResultModel::IsFromBook(); }
	virtual bool IsFromRecords() { return KBSResultModel::IsFromRecords(); }
	virtual KBSResultModel::SearchScopeKind GetSearchScope() { return KBSResultModel::GetSearchScope(); }
	virtual PMString GetBookName() { return KBSResultModel::GetBookName(); }
	virtual bool HasRun() { return KBSResultModel::HasRun(); }
	virtual bool IsShowingReplaceOutcome() { return KBSResultModel::IsShowingReplaceOutcome(); }
	virtual bool NoRowHasCheckBox() { return KBSResultModel::NoRowHasCheckBox(); }
	virtual int32 GetCheckedCount() { return KBSResultModel::GetCheckedCount(); }
	virtual int32 GetChapterCheckedCount(int32 chapterIdx) { return KBSResultModel::GetChapterCheckedCount(chapterIdx); }
	virtual int32 GetCheckableCount() { return KBSResultModel::GetCheckableCount(); }
	virtual int32 GetChapterCheckableCount(int32 chapterIdx) { return KBSResultModel::GetChapterCheckableCount(chapterIdx); }
	virtual void SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked) { KBSResultModel::SetHitChecked(chapterIdx, hitIdx, checked); }
	virtual void SetGroupChecked(int32 chapterIdx, int32 groupIdx, bool checked) { KBSResultModel::SetGroupChecked(chapterIdx, groupIdx, checked); }
	virtual void SetChapterChecked(int32 chapterIdx, bool checked) { KBSResultModel::SetChapterChecked(chapterIdx, checked); }
	virtual void SetAllChecked(bool checked) { KBSResultModel::SetAllChecked(checked); }
	virtual void SetContextMenuChapter(int32 chapterIdx) { KBSResultModel::SetContextMenuChapter(chapterIdx); }
	virtual int32 GetContextMenuChapter() { return KBSResultModel::GetContextMenuChapter(); }
	virtual void SetContextMenuGroup(int32 chapterIdx, int32 groupIdx) { KBSResultModel::SetContextMenuGroup(chapterIdx, groupIdx); }
	virtual bool GetContextMenuGroup(int32& outChapterIdx, int32& outGroupIdx) { return KBSResultModel::GetContextMenuGroup(outChapterIdx, outGroupIdx); }
	virtual void SetContextMenuRun(int32 chapterIdx, int32 runIdx) { KBSResultModel::SetContextMenuRun(chapterIdx, runIdx); }
	virtual bool GetContextMenuRun(int32& outChapterIdx, int32& outRunIdx) { return KBSResultModel::GetContextMenuRun(outChapterIdx, outRunIdx); }
	virtual void SetContextMenuHit(int32 chapterIdx, int32 hitIdx) { KBSResultModel::SetContextMenuHit(chapterIdx, hitIdx); }
	virtual bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx) { return KBSResultModel::GetContextMenuHit(outChapterIdx, outHitIdx); }
	virtual bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile) { return KBSResultModel::GetChapterLocation(chapterIdx, outDocRef, outFile); }
	virtual bool GetHitLocation(int32 chapterIdx, int32 hitIdx, UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd) { return KBSResultModel::GetHitLocation(chapterIdx, hitIdx, outDocRef, outFile, outStoryUID, outStart, outEnd); }
	virtual bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked) { return KBSResultModel::GetHitFlags(chapterIdx, hitIdx, outChecked, outReplaced, outLocked); }
	virtual bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden) { return KBSResultModel::GetHitReach(chapterIdx, hitIdx, outLocked, outHidden); }
	virtual bool GetHitInFootnote(int32 chapterIdx, int32 hitIdx) { return KBSResultModel::GetHitInFootnote(chapterIdx, hitIdx); }
	virtual void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef) { KBSResultModel::RebindChapterDoc(chapterIdx, newDocRef); }
	virtual void SetHitOutcome(int32 chapterIdx, int32 hitIdx, KBSResultModel::ChangeOutcome outcome) { KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, outcome); }
	virtual int32 GetGroupCheckableCount(int32 chapterIdx, int32 groupIdx) { return KBSResultModel::GetGroupCheckableCount(chapterIdx, groupIdx); }
};

CREATE_PMINTERFACE(KBSResultsSession, kKBSResultsImpl)

class KBSRunsSession : public CPMUnknown<IKBSRuns>
{
public:
	KBSRunsSession(IPMUnknown* boss) : CPMUnknown<IKBSRuns>(boss) {}

	virtual int32 SearchBook(PMString& outSummary) { return KBSSearchEngine::SearchBook(outSummary); }
	virtual int32 ShowChanges(PMString& outSummary) { return KBSShowChanges::Run(outSummary); }
	virtual int32 ReplaceChecked(PMString& outSummary) { return KBSReplaceEngine::ReplaceChecked(outSummary); }
	virtual bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KBSReplaceEngine::ReplaceHit(chapterIdx, hitIdx, outStatus); }
	virtual bool RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KBSReplaceEngine::RejectHit(chapterIdx, hitIdx, outStatus); }
	virtual bool AcceptHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KBSReplaceEngine::AcceptHit(chapterIdx, hitIdx, outStatus); }
	virtual bool CanReplaceHit(int32 chapterIdx, int32 hitIdx) { return KBSReplaceEngine::CanReplaceHit(chapterIdx, hitIdx); }
	virtual bool CanAcceptOrRejectHit(int32 chapterIdx, int32 hitIdx) { return KBSReplaceEngine::CanAcceptOrRejectHit(chapterIdx, hitIdx); }
	virtual bool ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KBSReplaceEngine::ReplaceStory(chapterIdx, groupIdx, outStatus); }
	virtual bool RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KBSReplaceEngine::RejectStory(chapterIdx, groupIdx, outStatus); }
	virtual bool AcceptStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KBSReplaceEngine::AcceptStory(chapterIdx, groupIdx, outStatus); }
	virtual bool RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KBSReplaceEngine::RedoStory(chapterIdx, groupIdx, outStatus); }
	virtual bool CanReplaceStory(int32 chapterIdx, int32 groupIdx) { return KBSReplaceEngine::CanReplaceStory(chapterIdx, groupIdx); }
	virtual bool CanRejectStory(int32 chapterIdx, int32 groupIdx) { return KBSReplaceEngine::CanRejectStory(chapterIdx, groupIdx); }
	virtual bool CanRedoStory(int32 chapterIdx, int32 groupIdx) { return KBSReplaceEngine::CanRedoStory(chapterIdx, groupIdx); }
	virtual bool ReplaceChapter(int32 chapterIdx, PMString& outStatus) { return KBSReplaceEngine::ReplaceChapter(chapterIdx, outStatus); }
	virtual bool RejectChapter(int32 chapterIdx, PMString& outStatus) { return KBSReplaceEngine::RejectChapter(chapterIdx, outStatus); }
	virtual bool RedoChapter(int32 chapterIdx, PMString& outStatus) { return KBSReplaceEngine::RedoChapter(chapterIdx, outStatus); }
	virtual bool AcceptAllInChapter(int32 chapterIdx, PMString& outStatus) { return KBSReplaceEngine::AcceptAllInChapter(chapterIdx, outStatus); }
	virtual bool CanReplaceChapter(int32 chapterIdx) { return KBSReplaceEngine::CanReplaceChapter(chapterIdx); }
	virtual bool CanRejectChapter(int32 chapterIdx) { return KBSReplaceEngine::CanRejectChapter(chapterIdx); }
	virtual bool CanRedoChapter(int32 chapterIdx) { return KBSReplaceEngine::CanRedoChapter(chapterIdx); }
	virtual bool CanAcceptAllInChapter(int32 chapterIdx) { return KBSReplaceEngine::CanAcceptAllInChapter(chapterIdx); }
	virtual bool RejectRun(int32 chapterIdx, int32 runIdx, PMString& outStatus) { return KBSReplaceEngine::RejectRun(chapterIdx, runIdx, outStatus); }
	virtual bool AcceptRun(int32 chapterIdx, int32 runIdx, PMString& outStatus) { return KBSReplaceEngine::AcceptRun(chapterIdx, runIdx, outStatus); }
	virtual bool CanRejectOrAcceptRun(int32 chapterIdx, int32 runIdx) { return KBSReplaceEngine::CanRejectOrAcceptRun(chapterIdx, runIdx); }
	virtual bool IsAnyRunning() { return KBSRunGuard::IsAnyRunning(); }
	virtual const char* BusyMessage() { return KBSRunGuard::BusyMessage(); }
	virtual const char* FindCommandName(bool bookScopeOn) { return KBSSearchEngine::FindCommandName(bookScopeOn); }
	virtual bool CanSearchTab(int32 mode) { return KBSSearchEngine::CanSearchTab(mode); }
	virtual int32 CurrentSearchMode() { return KBSSearchEngine::CurrentSearchMode(); }
	virtual const char* TabName(int32 mode) { return KBSSearchEngine::TabName(mode); }
	virtual int32 CurrentSearchScope() { return KBSSearchEngine::CurrentSearchScope(); }
	virtual int32 SearchScopeForSelection(int32 scope) { return KBSSearchEngine::SearchScopeForSelection(scope); }
	virtual const char* SearchScopeName(int32 scope) { return KBSSearchEngine::SearchScopeName(scope); }
	virtual UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos) { return KBSSearchEngine::EditableFrameForMatch(storyRef, pos); }
	virtual bool IsPositionOverset(const UIDRef& storyRef, TextIndex pos) { return KBSSearchEngine::IsPositionOverset(storyRef, pos); }
	virtual KBSOversetLoc FindOversetLocator(const UIDRef& storyRef, TextIndex pos) { return KBSFindOversetLocator(storyRef, pos); }
	virtual bool RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db) { return KBSSearchEngine::RowReadsAsFound(chapterIdx, hitIdx, db); }
	virtual bool RelocateStaleRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID, TextIndex& ioStart, TextIndex& ioEnd) { return KBSSearchEngine::RelocateStaleRow(chapterIdx, hitIdx, docRef, storyUID, ioStart, ioEnd); }
	virtual bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx) { return KBSTrackChange::RefreshRowFromRecords(chapterIdx, hitIdx); }
	virtual void CurrentReplacedGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows, bool& outRefreshed) { KBSTrackChange::CurrentReplacedGroup(chapterIdx, hitIdx, outRows, outRefreshed); }
	virtual bool RowChangeIsHidden(int32 chapterIdx, int32 hitIdx) { return KBSTrackChange::RowChangeIsHidden(chapterIdx, hitIdx); }
	virtual bool StoryChangesHidden(int32 chapterIdx, int32 groupIdx) { return KBSReplaceEngine::StoryChangesHidden(chapterIdx, groupIdx); }
	virtual bool SetQuery(const PMString& text, int32 mode) { return KBSSearchEngine::SetQuery(text, mode); }
	virtual TextIndex HiddenTextAnchor(const UIDRef& storyRef, TextIndex pos) { return KBSTrackChange::HiddenTextAnchor(storyRef, pos); }
};

CREATE_PMINTERFACE(KBSRunsSession, kKBSRunsImpl)

class KBSChaptersSession : public CPMUnknown<IKBSChapters>
{
public:
	KBSChaptersSession(IPMUnknown* boss) : CPMUnknown<IKBSChapters>(boss) {}

	virtual bool IsBookScopeOn() { return KBSBookScope::IsBookScopeOn(); }
	virtual void SetBookScopeOn(bool on) { KBSBookScope::SetBookScopeOn(on); }
	virtual bool HasScopeTarget() { return KBSBookScope::HasScopeTarget(); }
	virtual bool IsDocStillOpen(const UIDRef& docRef) { return KBSBookScope::IsDocStillOpen(docRef); }
	virtual bool HasWindow(const UIDRef& docRef) { return KBSBookScope::HasWindow(docRef); }
	virtual void ForgetHeldDoc(const UIDRef& docRef) { KBSBookScope::ForgetHeldDoc(docRef); }
	virtual bool ReachChapterDoc(const IDFile& file, UIDRef& ioDocRef) { return KBSBookScope::ReachChapterDoc(file, ioDocRef); }
	virtual bool FindOpenChapterDoc(const IDFile& file, UIDRef& ioDocRef) { return KBSBookScope::FindOpenChapterDoc(file, ioDocRef); }
	virtual void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc) { KBSBookScope::CloseDisplayedDocsIfClean(exceptDoc); }
	virtual bool GetSearchedBookPath(PMString& outPath) { return KBSBookScope::GetSearchedBookPath(outPath); }
	virtual bool MakeBookActive(const PMString& bookPath) { return KBSBookScope::MakeBookActive(bookPath); }
	virtual bool SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB) { return KBSHitMarker::SetMarker(db, storyUID, start, end, outPreviousDB); }
	virtual bool ClearMarker(IDataBase*& outDB) { return KBSHitMarker::ClearMarker(outDB); }
};

CREATE_PMINTERFACE(KBSChaptersSession, kKBSChaptersImpl)

// End, KBSModelServices.cpp.
