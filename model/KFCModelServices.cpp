//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The model half's three session interfaces (IKFCResults.h / IKFCRuns.h / IKFCChapters.h), each method
//  forwarding to the function it names - nothing is decided here (the model/UI split).
//  EDITED BY HAND. First generated from one table with the headers
//  (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py); a method appended to a header since is forwarded
//  here by hand, and the table does not know it - the generator is a record only and refuses to write.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#include "CPMUnknown.h"

#include "KFCID.h"				// the implementation ids
#include "IKFCChapters.h"
#include "IKFCResults.h"
#include "IKFCRuns.h"
#include "KFCBookScope.h"
#include "KFCHitMarker.h"
#include "KFCOversetLocator.h"
#include "KFCReplaceEngine.h"
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCSearchEngine.h"
#include "KFCShowChanges.h"
#include "KFCTrackChange.h"

class KFCResultsSession : public CPMUnknown<IKFCResults>
{
public:
	KFCResultsSession(IPMUnknown* boss) : CPMUnknown<IKFCResults>(boss) {}

	virtual int32 GetChapterCount() { return KFCResultModel::GetChapterCount(); }
	virtual int32 GetTotalHitCount() { return KFCResultModel::GetTotalHitCount(); }
	virtual int32 GetDisplayChapterCount() { return KFCResultModel::GetDisplayChapterCount(); }
	virtual int32 GetDisplayHitCount(int32 chapterIdx) { return KFCResultModel::GetDisplayHitCount(chapterIdx); }
	virtual bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount) { return KFCResultModel::GetChapterDisplay(chapterIdx, outName, outHitCount); }
	virtual int32 GetShownChapter(int32 nth) { return KFCResultModel::GetShownChapter(nth); }
	virtual int32 GetShownChapterPos(int32 chapterIdx) { return KFCResultModel::GetShownChapterPos(chapterIdx); }
	virtual int32 GetDisplayFontCount(int32 chapterIdx) { return KFCResultModel::GetDisplayFontCount(chapterIdx); }
	virtual int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx) { return KFCResultModel::GetDisplayFontHitCount(chapterIdx, fontIdx); }
	virtual bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount) { return KFCResultModel::GetFontDisplay(chapterIdx, fontIdx, outName, outHitCount); }
	virtual int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth) { return KFCResultModel::GetFontGroupHit(chapterIdx, fontIdx, nth); }
	virtual int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx) { return KFCResultModel::GetHitFontGroup(chapterIdx, hitIdx); }
	virtual int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx) { return KFCResultModel::GetHitFontGroupPos(chapterIdx, hitIdx); }
	virtual int32 GetDisplayRunCount(int32 chapterIdx) { return KFCResultModel::GetDisplayRunCount(chapterIdx); }
	virtual bool GetRunDisplay(int32 chapterIdx, int32 runIdx, PMString& outLabel, int32& outHitCount) { return KFCResultModel::GetRunDisplay(chapterIdx, runIdx, outLabel, outHitCount); }
	virtual int32 GetDisplayRunGroupCount(int32 chapterIdx, int32 runIdx) { return KFCResultModel::GetDisplayRunGroupCount(chapterIdx, runIdx); }
	virtual int32 GetRunGroup(int32 chapterIdx, int32 runIdx, int32 nth) { return KFCResultModel::GetRunGroup(chapterIdx, runIdx, nth); }
	virtual int32 GetGroupRun(int32 chapterIdx, int32 groupIdx) { return KFCResultModel::GetGroupRun(chapterIdx, groupIdx); }
	virtual int32 GetGroupPosInRun(int32 chapterIdx, int32 groupIdx) { return KFCResultModel::GetGroupPosInRun(chapterIdx, groupIdx); }
	virtual bool IsStoryGroup(int32 chapterIdx, int32 groupIdx) { return KFCResultModel::IsStoryGroup(chapterIdx, groupIdx); }
	virtual int32 GetGroupCheckedCount(int32 chapterIdx, int32 groupIdx) { return KFCResultModel::GetGroupCheckedCount(chapterIdx, groupIdx); }
	virtual bool GetHitRow(int32 chapterIdx, int32 hitIdx, KFCResultModel::RowDisplay& out) { return KFCResultModel::GetHitRow(chapterIdx, hitIdx, out); }
	virtual bool GetHitDisplay(int32 chapterIdx, int32 hitIdx, PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost) { return KFCResultModel::GetHitDisplay(chapterIdx, hitIdx, outLocator, outPre, outMatch, outPost); }
	virtual bool GetRowsBefore(int32 chapterIdx, const std::vector<int32>& rows, PMString& outPre, PMString& outOriginal, PMString& outPost) { return KFCResultModel::GetRowsBefore(chapterIdx, rows, outPre, outOriginal, outPost); }
	virtual void MarkUpBreaksForDisplay(PMString& s) { KFCResultModel::MarkUpBreaksForDisplay(s); }
	virtual bool IsFromBook() { return KFCResultModel::IsFromBook(); }
	virtual bool IsFromRecords() { return KFCResultModel::IsFromRecords(); }
	virtual KFCResultModel::SearchScopeKind GetSearchScope() { return KFCResultModel::GetSearchScope(); }
	virtual PMString GetBookName() { return KFCResultModel::GetBookName(); }
	virtual bool HasRun() { return KFCResultModel::HasRun(); }
	virtual bool IsShowingReplaceOutcome() { return KFCResultModel::IsShowingReplaceOutcome(); }
	virtual bool NoRowHasCheckBox() { return KFCResultModel::NoRowHasCheckBox(); }
	virtual int32 GetCheckedCount() { return KFCResultModel::GetCheckedCount(); }
	virtual int32 GetChapterCheckedCount(int32 chapterIdx) { return KFCResultModel::GetChapterCheckedCount(chapterIdx); }
	virtual int32 GetCheckableCount() { return KFCResultModel::GetCheckableCount(); }
	virtual int32 GetChapterCheckableCount(int32 chapterIdx) { return KFCResultModel::GetChapterCheckableCount(chapterIdx); }
	virtual void SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked) { KFCResultModel::SetHitChecked(chapterIdx, hitIdx, checked); }
	virtual void SetGroupChecked(int32 chapterIdx, int32 groupIdx, bool checked) { KFCResultModel::SetGroupChecked(chapterIdx, groupIdx, checked); }
	virtual void SetChapterChecked(int32 chapterIdx, bool checked) { KFCResultModel::SetChapterChecked(chapterIdx, checked); }
	virtual void SetAllChecked(bool checked) { KFCResultModel::SetAllChecked(checked); }
	virtual void SetContextMenuChapter(int32 chapterIdx) { KFCResultModel::SetContextMenuChapter(chapterIdx); }
	virtual int32 GetContextMenuChapter() { return KFCResultModel::GetContextMenuChapter(); }
	virtual void SetContextMenuGroup(int32 chapterIdx, int32 groupIdx) { KFCResultModel::SetContextMenuGroup(chapterIdx, groupIdx); }
	virtual bool GetContextMenuGroup(int32& outChapterIdx, int32& outGroupIdx) { return KFCResultModel::GetContextMenuGroup(outChapterIdx, outGroupIdx); }
	virtual void SetContextMenuRun(int32 chapterIdx, int32 runIdx) { KFCResultModel::SetContextMenuRun(chapterIdx, runIdx); }
	virtual bool GetContextMenuRun(int32& outChapterIdx, int32& outRunIdx) { return KFCResultModel::GetContextMenuRun(outChapterIdx, outRunIdx); }
	virtual void SetContextMenuHit(int32 chapterIdx, int32 hitIdx) { KFCResultModel::SetContextMenuHit(chapterIdx, hitIdx); }
	virtual bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx) { return KFCResultModel::GetContextMenuHit(outChapterIdx, outHitIdx); }
	virtual bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile) { return KFCResultModel::GetChapterLocation(chapterIdx, outDocRef, outFile); }
	virtual bool GetHitLocation(int32 chapterIdx, int32 hitIdx, UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd) { return KFCResultModel::GetHitLocation(chapterIdx, hitIdx, outDocRef, outFile, outStoryUID, outStart, outEnd); }
	virtual bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked) { return KFCResultModel::GetHitFlags(chapterIdx, hitIdx, outChecked, outReplaced, outLocked); }
	virtual bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden) { return KFCResultModel::GetHitReach(chapterIdx, hitIdx, outLocked, outHidden); }
	virtual bool GetHitInFootnote(int32 chapterIdx, int32 hitIdx) { return KFCResultModel::GetHitInFootnote(chapterIdx, hitIdx); }
	virtual void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef) { KFCResultModel::RebindChapterDoc(chapterIdx, newDocRef); }
	virtual void SetHitOutcome(int32 chapterIdx, int32 hitIdx, KFCResultModel::ChangeOutcome outcome) { KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, outcome); }
	virtual int32 GetGroupCheckableCount(int32 chapterIdx, int32 groupIdx) { return KFCResultModel::GetGroupCheckableCount(chapterIdx, groupIdx); }
	virtual bool GetHitTextUnchanged(int32 chapterIdx, int32 hitIdx) { return KFCResultModel::GetHitTextUnchanged(chapterIdx, hitIdx); }
};

CREATE_PMINTERFACE(KFCResultsSession, kKFCResultsImpl)

class KFCRunsSession : public CPMUnknown<IKFCRuns>
{
public:
	KFCRunsSession(IPMUnknown* boss) : CPMUnknown<IKFCRuns>(boss) {}

	virtual int32 SearchBook(PMString& outSummary) { return KFCSearchEngine::SearchBook(outSummary); }
	virtual int32 ShowChanges(PMString& outSummary) { return KFCShowChanges::Run(outSummary); }
	virtual int32 ReplaceChecked(PMString& outSummary) { return KFCReplaceEngine::ReplaceChecked(outSummary); }
	virtual bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KFCReplaceEngine::ReplaceHit(chapterIdx, hitIdx, outStatus); }
	virtual bool RejectHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KFCReplaceEngine::RejectHit(chapterIdx, hitIdx, outStatus); }
	virtual bool AcceptHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KFCReplaceEngine::AcceptHit(chapterIdx, hitIdx, outStatus); }
	virtual bool CanReplaceHit(int32 chapterIdx, int32 hitIdx) { return KFCReplaceEngine::CanReplaceHit(chapterIdx, hitIdx); }
	virtual bool CanAcceptOrRejectHit(int32 chapterIdx, int32 hitIdx) { return KFCReplaceEngine::CanAcceptOrRejectHit(chapterIdx, hitIdx); }
	virtual bool ReplaceStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KFCReplaceEngine::ReplaceStory(chapterIdx, groupIdx, outStatus); }
	virtual bool RejectStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KFCReplaceEngine::RejectStory(chapterIdx, groupIdx, outStatus); }
	virtual bool AcceptStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KFCReplaceEngine::AcceptStory(chapterIdx, groupIdx, outStatus); }
	virtual bool RedoStory(int32 chapterIdx, int32 groupIdx, PMString& outStatus) { return KFCReplaceEngine::RedoStory(chapterIdx, groupIdx, outStatus); }
	virtual bool CanReplaceStory(int32 chapterIdx, int32 groupIdx) { return KFCReplaceEngine::CanReplaceStory(chapterIdx, groupIdx); }
	virtual bool CanRejectStory(int32 chapterIdx, int32 groupIdx) { return KFCReplaceEngine::CanRejectStory(chapterIdx, groupIdx); }
	virtual bool CanRedoStory(int32 chapterIdx, int32 groupIdx) { return KFCReplaceEngine::CanRedoStory(chapterIdx, groupIdx); }
	virtual bool ReplaceChapter(int32 chapterIdx, PMString& outStatus) { return KFCReplaceEngine::ReplaceChapter(chapterIdx, outStatus); }
	virtual bool RejectChapter(int32 chapterIdx, PMString& outStatus) { return KFCReplaceEngine::RejectChapter(chapterIdx, outStatus); }
	virtual bool RedoChapter(int32 chapterIdx, PMString& outStatus) { return KFCReplaceEngine::RedoChapter(chapterIdx, outStatus); }
	virtual bool AcceptAllInChapter(int32 chapterIdx, PMString& outStatus) { return KFCReplaceEngine::AcceptAllInChapter(chapterIdx, outStatus); }
	virtual bool CanReplaceChapter(int32 chapterIdx) { return KFCReplaceEngine::CanReplaceChapter(chapterIdx); }
	virtual bool CanRejectChapter(int32 chapterIdx) { return KFCReplaceEngine::CanRejectChapter(chapterIdx); }
	virtual bool CanRedoChapter(int32 chapterIdx) { return KFCReplaceEngine::CanRedoChapter(chapterIdx); }
	virtual bool CanAcceptAllInChapter(int32 chapterIdx) { return KFCReplaceEngine::CanAcceptAllInChapter(chapterIdx); }
	virtual bool RejectRun(int32 chapterIdx, int32 runIdx, PMString& outStatus) { return KFCReplaceEngine::RejectRun(chapterIdx, runIdx, outStatus); }
	virtual bool AcceptRun(int32 chapterIdx, int32 runIdx, PMString& outStatus) { return KFCReplaceEngine::AcceptRun(chapterIdx, runIdx, outStatus); }
	virtual bool CanRejectOrAcceptRun(int32 chapterIdx, int32 runIdx) { return KFCReplaceEngine::CanRejectOrAcceptRun(chapterIdx, runIdx); }
	virtual bool IsAnyRunning() { return KFCRunGuard::IsAnyRunning(); }
	virtual const char* BusyMessage() { return KFCRunGuard::BusyMessage(); }
	virtual const char* FindCommandName(bool bookScopeOn) { return KFCSearchEngine::FindCommandName(bookScopeOn); }
	virtual bool CanSearchTab(int32 mode) { return KFCSearchEngine::CanSearchTab(mode); }
	virtual int32 CurrentSearchMode() { return KFCSearchEngine::CurrentSearchMode(); }
	virtual const char* TabName(int32 mode) { return KFCSearchEngine::TabName(mode); }
	virtual int32 CurrentSearchScope() { return KFCSearchEngine::CurrentSearchScope(); }
	virtual int32 SearchScopeForSelection(int32 scope) { return KFCSearchEngine::SearchScopeForSelection(scope); }
	virtual const char* SearchScopeName(int32 scope) { return KFCSearchEngine::SearchScopeName(scope); }
	virtual UID EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos) { return KFCSearchEngine::EditableFrameForMatch(storyRef, pos); }
	virtual bool IsPositionOverset(const UIDRef& storyRef, TextIndex pos) { return KFCSearchEngine::IsPositionOverset(storyRef, pos); }
	virtual KFCOversetLoc FindOversetLocator(const UIDRef& storyRef, TextIndex pos) { return KFCFindOversetLocator(storyRef, pos); }
	virtual bool RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db) { return KFCSearchEngine::RowReadsAsFound(chapterIdx, hitIdx, db); }
	virtual bool RelocateStaleRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID, TextIndex& ioStart, TextIndex& ioEnd) { return KFCSearchEngine::RelocateStaleRow(chapterIdx, hitIdx, docRef, storyUID, ioStart, ioEnd); }
	virtual bool RefreshRowFromRecords(int32 chapterIdx, int32 hitIdx) { return KFCTrackChange::RefreshRowFromRecords(chapterIdx, hitIdx); }
	virtual void CurrentReplacedGroup(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows, bool& outRefreshed) { KFCTrackChange::CurrentReplacedGroup(chapterIdx, hitIdx, outRows, outRefreshed); }
	virtual bool RowChangeIsHidden(int32 chapterIdx, int32 hitIdx) { return KFCTrackChange::RowChangeIsHidden(chapterIdx, hitIdx); }
	virtual bool StoryChangesHidden(int32 chapterIdx, int32 groupIdx) { return KFCReplaceEngine::StoryChangesHidden(chapterIdx, groupIdx); }
	virtual bool SetQuery(const PMString& text, int32 mode) { return KFCSearchEngine::SetQuery(text, mode); }
	virtual TextIndex HiddenTextAnchor(const UIDRef& storyRef, TextIndex pos) { return KFCTrackChange::HiddenTextAnchor(storyRef, pos); }
};

CREATE_PMINTERFACE(KFCRunsSession, kKFCRunsImpl)

class KFCChaptersSession : public CPMUnknown<IKFCChapters>
{
public:
	KFCChaptersSession(IPMUnknown* boss) : CPMUnknown<IKFCChapters>(boss) {}

	virtual bool IsBookScopeOn() { return KFCBookScope::IsBookScopeOn(); }
	virtual void SetBookScopeOn(bool on) { KFCBookScope::SetBookScopeOn(on); }
	virtual bool HasScopeTarget() { return KFCBookScope::HasScopeTarget(); }
	virtual bool IsDocStillOpen(const UIDRef& docRef) { return KFCBookScope::IsDocStillOpen(docRef); }
	virtual bool HasWindow(const UIDRef& docRef) { return KFCBookScope::HasWindow(docRef); }
	virtual void ForgetHeldDoc(const UIDRef& docRef) { KFCBookScope::ForgetHeldDoc(docRef); }
	virtual bool ReachChapterDoc(const IDFile& file, UIDRef& ioDocRef) { return KFCBookScope::ReachChapterDoc(file, ioDocRef); }
	virtual bool FindOpenChapterDoc(const IDFile& file, UIDRef& ioDocRef) { return KFCBookScope::FindOpenChapterDoc(file, ioDocRef); }
	virtual void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc) { KFCBookScope::CloseDisplayedDocsIfClean(exceptDoc); }
	virtual bool GetSearchedBookPath(PMString& outPath) { return KFCBookScope::GetSearchedBookPath(outPath); }
	virtual bool MakeBookActive(const PMString& bookPath) { return KFCBookScope::MakeBookActive(bookPath); }
	virtual bool SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB) { return KFCHitMarker::SetMarker(db, storyUID, start, end, outPreviousDB); }
	virtual bool ClearMarker(IDataBase*& outDB) { return KFCHitMarker::ClearMarker(outDB); }
};

CREATE_PMINTERFACE(KFCChaptersSession, kKFCChaptersImpl)

// End, KFCModelServices.cpp.
