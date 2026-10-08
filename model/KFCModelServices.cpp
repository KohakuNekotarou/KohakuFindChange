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
#include "KFCChangeAll.h"
#include "KFCHitMarker.h"
#include "KFCOversetLocator.h"
#include "KFCQuerySequence.h"
#include "KFCReplaceEngine.h"
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCSavedQueries.h"
#include "KFCSearchEngine.h"

class KFCResultsSession : public CPMUnknown<IKFCResults>
{
public:
	KFCResultsSession(IPMUnknown* boss) : CPMUnknown<IKFCResults>(boss) {}

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
	virtual bool IsStoryGroup(int32 chapterIdx, int32 groupIdx) { return KFCResultModel::IsStoryGroup(chapterIdx, groupIdx); }
	virtual bool GetHitRow(int32 chapterIdx, int32 hitIdx, KFCResultModel::RowDisplay& out) { return KFCResultModel::GetHitRow(chapterIdx, hitIdx, out); }
	virtual void MarkUpBreaksForDisplay(PMString& s) { KFCResultModel::MarkUpBreaksForDisplay(s); }
	virtual bool IsFromBook() { return KFCResultModel::IsFromBook(); }
	virtual KFCResultModel::SearchScopeKind GetSearchScope() { return KFCResultModel::GetSearchScope(); }
	virtual PMString GetBookName() { return KFCResultModel::GetBookName(); }
	virtual bool HasRun() { return KFCResultModel::HasRun(); }
	virtual void SetContextMenuHit(int32 chapterIdx, int32 hitIdx) { KFCResultModel::SetContextMenuHit(chapterIdx, hitIdx); }
	virtual bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx) { return KFCResultModel::GetContextMenuHit(outChapterIdx, outHitIdx); }
	virtual bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile) { return KFCResultModel::GetChapterLocation(chapterIdx, outDocRef, outFile); }
	virtual bool GetHitLocation(int32 chapterIdx, int32 hitIdx, UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd) { return KFCResultModel::GetHitLocation(chapterIdx, hitIdx, outDocRef, outFile, outStoryUID, outStart, outEnd); }
	virtual bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outReplaced, bool& outLocked) { return KFCResultModel::GetHitFlags(chapterIdx, hitIdx, outReplaced, outLocked); }
	virtual bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden) { return KFCResultModel::GetHitReach(chapterIdx, hitIdx, outLocked, outHidden); }
	virtual void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef) { KFCResultModel::RebindChapterDoc(chapterIdx, newDocRef); }
	virtual void SetHitOutcome(int32 chapterIdx, int32 hitIdx, KFCResultModel::ChangeOutcome outcome) { KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, outcome); }
	virtual bool HasChangeAllWritten() { return KFCResultModel::HasChangeAllWritten(); }
	virtual int32 GetChapterReplacedCount(int32 chapterIdx) { return KFCResultModel::GetChapterReplacedCount(chapterIdx); }
	virtual int32 GetFontReplacedCount(int32 chapterIdx, int32 fontIdx) { return KFCResultModel::GetFontReplacedCount(chapterIdx, fontIdx); }
	virtual int32 GetTotalReplacedCount() { return KFCResultModel::GetTotalReplacedCount(); }
};

CREATE_PMINTERFACE(KFCResultsSession, kKFCResultsImpl)

class KFCRunsSession : public CPMUnknown<IKFCRuns>
{
public:
	KFCRunsSession(IPMUnknown* boss) : CPMUnknown<IKFCRuns>(boss) {}

	virtual int32 SearchBook(PMString& outSummary) { return KFCSearchEngine::SearchBook(outSummary); }
	virtual bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) { return KFCReplaceEngine::ReplaceHit(chapterIdx, hitIdx, outStatus); }
	virtual bool CanReplaceHit(int32 chapterIdx, int32 hitIdx) { return KFCReplaceEngine::CanReplaceHit(chapterIdx, hitIdx); }
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
	virtual bool SetQuery(const PMString& text, int32 mode) { return KFCSearchEngine::SetQuery(text, mode); }
	virtual int32 ChangeAll(PMString& outSummary) { return KFCChangeAll::Run(outSummary); }
	virtual bool ClearResults(PMString& outStatus) { return KFCChangeAll::ClearResults(outStatus); }
	virtual bool HasFindQueryNow() { return KFCSearchEngine::HasFindQueryNow(); }
	virtual void ListSavedQueries(std::vector<KFCSavedQuery>& out) { KFCSavedQueries::List(out); }
	virtual void DescribeQueryFile(const IDFile& file, KFCSavedQuery& out) { KFCSavedQueries::Describe(file, out); }
	virtual const char* QueryKindName(int32 mode) { return KFCSavedQueries::KindName(mode); }
	virtual int32 RunQueries(const std::vector<IDFile>& files, PMString& outSummary) { return KFCQuerySequence::RunFiles(files, outSummary); }
	virtual bool DescribeRunScope(PMString& outWords) { return KFCSearchEngine::DescribeRunScope(outWords); }
	virtual const char* ChangeAllCommandName() { return KFCChangeAll::CommandName(); }
	virtual bool PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter) { return KFCReplaceEngine::PreviewHit(chapterIdx, hitIdx, outAfter); }
	virtual bool LoadSavedQuery(const IDFile& file) { return KFCSavedQueries::LoadIntoFindChange(file); }
	virtual KFCResultModel::RowLocation LocateRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, TextIndex& ioStart, TextIndex& ioEnd) { return KFCSearchEngine::LocateRow(chapterIdx, hitIdx, docRef, ioStart, ioEnd); }
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
	virtual void CloseDisplayedDocsIfClean(const UIDRef& exceptDoc) { KFCBookScope::CloseDisplayedDocsIfClean(exceptDoc); }
	virtual bool GetSearchedBookPath(PMString& outPath) { return KFCBookScope::GetSearchedBookPath(outPath); }
	virtual bool MakeBookActive(const PMString& bookPath) { return KFCBookScope::MakeBookActive(bookPath); }
	virtual bool SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB) { return KFCHitMarker::SetMarker(db, storyUID, start, end, outPreviousDB); }
	virtual bool ClearMarker(IDataBase*& outDB) { return KFCHitMarker::ClearMarker(outDB); }
	virtual bool IsSelectedDocumentsOn() { return KFCBookScope::IsSelectedDocumentsOn(); }
	virtual void SetSelectedDocumentsOn(bool on) { KFCBookScope::SetSelectedDocumentsOn(on); }
};

CREATE_PMINTERFACE(KFCChaptersSession, kKFCChaptersImpl)

// End, KFCModelServices.cpp.
