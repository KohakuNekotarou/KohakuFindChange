//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCModelAccess.h. UI side: the boundary check, and the doors' stand-ins for a model half of another build.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISession.h"

// General includes:
#include <string>

// Project includes:
#include "IKFCBoundary.h"		// KFCOtherHalfIsThisBuild
#include "KFCModelAccess.h"

namespace
{
	// -1 = not asked yet; 0 = the loaded model half is another build's; 1 = this build's.
	int gBoundary = -1;
	std::string gMismatchText;

	// The message line's words (the busy message's place - BusyMessage below): both files by name, both stamps, and
	// what to do. ASCII, as every door's text is.
	void BuildMismatchText(const char* modelStamp)
	{
		gMismatchText = "Kohaku Find/Change cannot run: its two plug-in files are not from the same build (KohakuFindChange.pln ";
		gMismatchText += (modelStamp != nil) ? modelStamp : "is an older build";
		gMismatchText += ", KohakuFindChangeUI.pln ";
		gMismatchText += kKFCBoundaryStamp;
		gMismatchText += "). Quit InDesign and install both files again, from the same release.";
	}

	// What a stand-in's out-string says: the reason, untranslated (a string the table never held would be looked up as
	// a key all the same - memory menu-string-translation-traps).
	void SayWhy(PMString& out)
	{
		out = PMString(KFCBoundaryMismatchText());
		out.SetTranslatable(kFalse);
	}

	// THE STAND-INS. Every door answered the way "nothing here, and a run already standing" reads - counts 0, finds
	// false, outs emptied (an IDFile out left as it is: it is read only after a true), a run's words the reason - so
	// the panel's commands are grey (KFCActionComponent's UpdateActionStates greys every kCustomEnabling one while
	// IsAnyRunning) and a command that is tried anyway says why on the message line (KFCResultTree::RefusedWhileRunning
	// shows BusyMessage). Statics, so AddRef and Release count nothing.
	// A METHOD ADDED TO A DOOR IS ADDED HERE TOO - the stand-in is abstract, and so does not compile, until it is.
	// A METHOD TAKEN OUT OF A DOOR IS TAKEN OUT HERE TOO - left here, it still compiles (nothing here says override)
	// and stands in for nothing.
	// A COMMAND THAT READS A DOOR BUT IS NOT kCustomEnabling ASKS RefusedWhileRunning ITSELF - no grey reaches it
	// (Save Panel Settings, KFCSavePanelState).

	class KFCResultsStandInImpl : public IKFCResults
	{
	public:
		virtual IPMUnknown* QueryInterface(PMIID iid) const
			{ return (iid == IID_IKFCRESULTS) ? const_cast<KFCResultsStandInImpl*>(this) : nil; }
		virtual void AddRef() const {}
		virtual void Release() const {}

		virtual int32 GetTotalHitCount() { return 0; }
		virtual int32 GetDisplayChapterCount() { return 0; }
		virtual int32 GetDisplayHitCount(int32) { return 0; }
		virtual bool GetChapterDisplay(int32, PMString& outName, int32& outHitCount) { outName.clear(); outHitCount = 0; return false; }
		virtual int32 GetShownChapter(int32) { return -1; }
		virtual int32 GetShownChapterPos(int32) { return -1; }
		virtual int32 GetDisplayFontCount(int32) { return 0; }
		virtual int32 GetDisplayFontHitCount(int32, int32) { return 0; }
		virtual bool GetFontDisplay(int32, int32, PMString& outName, int32& outHitCount) { outName.clear(); outHitCount = 0; return false; }
		virtual int32 GetFontGroupHit(int32, int32, int32) { return -1; }
		virtual int32 GetHitFontGroup(int32, int32) { return -1; }
		virtual int32 GetHitFontGroupPos(int32, int32) { return -1; }
		virtual bool IsStoryGroup(int32, int32) { return false; }
		virtual bool GetHitRow(int32, int32, KFCResultModel::RowDisplay& out) { out = KFCResultModel::RowDisplay(); return false; }
		virtual void MarkUpBreaksForDisplay(PMString&) {}
		virtual bool IsFromBook() { return false; }
		virtual KFCResultModel::SearchScopeKind GetSearchScope() { return KFCResultModel::kScopeDocument; }
		virtual PMString GetBookName() { PMString none; none.SetTranslatable(kFalse); return none; }
		virtual bool HasRun() { return false; }
		virtual bool GetChapterLocation(int32, UIDRef& outDocRef, IDFile&) { outDocRef = UIDRef::gNull; return false; }
		virtual bool GetHitLocation(int32, int32, UIDRef& outDocRef, IDFile&, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd)
			{ outDocRef = UIDRef::gNull; outStoryUID = kInvalidUID; outStart = 0; outEnd = 0; return false; }
		virtual bool GetHitFlags(int32, int32, bool& outReplaced, bool& outLocked) { outReplaced = false; outLocked = false; return false; }
		virtual bool GetHitReach(int32, int32, bool& outLocked, bool& outHidden) { outLocked = false; outHidden = false; return false; }
		virtual void RebindChapterDoc(int32, const UIDRef&) {}
		virtual void SetHitOutcome(int32, int32, KFCResultModel::ChangeOutcome) {}
		virtual bool HasChangeAllWritten() { return false; }
		virtual int32 GetChapterReplacedCount(int32) { return 0; }
		virtual int32 GetFontReplacedCount(int32, int32) { return 0; }
		virtual int32 GetTotalReplacedCount() { return 0; }
		virtual UID GetHitItem(int32, int32) { return kInvalidUID; }
		virtual uint32 GetResultSetId() { return 0; }
		virtual bool SetHitKept(int32, int32, bool) { return false; }
		virtual void GetNarrowWords(PMString& out) { out.Clear(); }
		virtual bool ListHoldsObjects() { return false; }
		virtual int32 ClearRows(const KFCRowsByChapter&) { return 0; }
		virtual int32 GetObjectRowHit(int32, int32) { return -1; }
		virtual bool IsHitItemGone(int32, int32) { return false; }
		virtual bool IsFromAllDocuments() { return false; }
	};

	class KFCRunsStandInImpl : public IKFCRuns
	{
	public:
		virtual IPMUnknown* QueryInterface(PMIID iid) const
			{ return (iid == IID_IKFCRUNS) ? const_cast<KFCRunsStandInImpl*>(this) : nil; }
		virtual void AddRef() const {}
		virtual void Release() const {}

		virtual int32 SearchBook(PMString& outSummary) { SayWhy(outSummary); return 0; }
		virtual bool ReplaceHit(int32, int32, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool CanReplaceHit(int32, int32) { return false; }
		virtual bool IsAnyRunning() { return true; }
		virtual const char* BusyMessage() { return KFCBoundaryMismatchText(); }
		virtual const char* FindCommandName(bool) { return "Find"; }
		virtual bool CanSearchTab(int32) { return false; }
		virtual int32 CurrentSearchMode() { return -1; }
		virtual const char* TabName(int32) { return ""; }
		virtual int32 CurrentSearchScope() { return 0; }
		virtual int32 SearchScopeForSelection(int32 scope) { return scope; }
		virtual const char* SearchScopeName(int32) { return ""; }
		virtual UID EditableFrameForMatch(const UIDRef&, TextIndex) { return kInvalidUID; }
		virtual bool IsPositionOverset(const UIDRef&, TextIndex) { return false; }
		virtual KFCOversetLoc FindOversetLocator(const UIDRef&, TextIndex) { return KFCOversetLoc(); }
		virtual bool SetQuery(const PMString&, int32) { return false; }
		virtual int32 ChangeAll(PMString& outSummary) { SayWhy(outSummary); return 0; }
		virtual bool ClearResults(PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool HasFindQueryNow() { return false; }
		virtual void ListSavedQueries(std::vector<KFCSavedQuery>& out) { out.clear(); }
		virtual void DescribeQueryFile(const IDFile& file, KFCSavedQuery& out) { out = KFCSavedQuery(); out.file = file; }
		virtual const char* QueryKindName(int32) { return "?"; }
		virtual int32 RunQueries(const std::vector<IDFile>&, PMString& outSummary) { SayWhy(outSummary); return 0; }
		virtual bool DescribeRunScope(PMString& outWords) { SayWhy(outWords); return false; }
		virtual const char* ChangeAllCommandName() { return "Change All in Book (No List)"; }
		virtual bool PreviewHit(int32, int32, PMString& outAfter) { outAfter.clear(); return false; }
		virtual bool LoadSavedQuery(const IDFile&) { return false; }
		virtual KFCResultModel::RowLocation LocateRow(int32, int32, const UIDRef&, TextIndex&, TextIndex&) { return KFCResultModel::kRowNotFound; }
		virtual bool SearchStoryAgain(int32, int32, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual void RestoreSearchScope(int32, int32) {}
		virtual bool CheckObjectReplace(int32, int32, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool CheckObjectReplaceRows(const KFCRowsByChapter&, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool ReplaceRows(const KFCRowsByChapter&, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool SearchDocumentAgain(int32, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool SwitchToTier(uint32, PMString& outSummary) { SayWhy(outSummary); return false; }
		virtual void GetKeptTierSets(std::vector<uint32>& out) { out.clear(); }
		virtual void DropKeptTiers() {}
		virtual bool DropTier(uint32) { return false; }
		virtual bool SearchListAgain(PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool OutlineRows(const KFCRowsByChapter&, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool CanRestoreHit(int32, int32) { return false; }
		virtual bool RestoreRows(const KFCRowsByChapter&, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool CanOutlineHit(int32, int32) { return false; }
		virtual bool SpecialRows(int32, const KFCRowsByChapter&, const KFCSpecialParams&, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool CanSpecialHit(int32, int32, int32) { return false; }
		virtual bool NarrowList(const KFCNarrowStep&, PMString& outStatus) { SayWhy(outStatus); return false; }
		virtual bool IsAnsweringAlerts() { return false; }
		virtual int16 AnswerAlert(const PMString&, const PMString&, const PMString&, const PMString&, int16)
		{
			return kKFCAlertLeaveToDefault;
		}
		virtual bool SearchTextInObjects(PMString& outStatus) { SayWhy(outStatus); return false; }
	};

	class KFCChaptersStandInImpl : public IKFCChapters
	{
	public:
		virtual IPMUnknown* QueryInterface(PMIID iid) const
			{ return (iid == IID_IKFCCHAPTERS) ? const_cast<KFCChaptersStandInImpl*>(this) : nil; }
		virtual void AddRef() const {}
		virtual void Release() const {}

		virtual bool IsBookScopeOn() { return false; }
		virtual void SetBookScopeOn(bool) {}
		virtual bool HasScopeTarget() { return false; }
		virtual bool IsDocStillOpen(const UIDRef&) { return false; }
		virtual bool HasWindow(const UIDRef&) { return false; }
		virtual void ForgetHeldDoc(const UIDRef&) {}
		virtual bool ReachChapterDoc(const IDFile&, UIDRef&) { return false; }
		virtual void CloseDisplayedDocsIfClean(const UIDRef&) {}
		virtual bool GetSearchedBookPath(PMString& outPath) { outPath.clear(); return false; }
		virtual bool MakeBookActive(const PMString&) { return false; }
		virtual bool SetMarker(IDataBase*, UID, TextIndex, TextIndex, IDataBase*& outPreviousDB) { outPreviousDB = nil; return false; }
		virtual bool ClearMarker(IDataBase*& outDB) { outDB = nil; return false; }
		virtual bool IsSelectedDocumentsOn() { return false; }
		virtual void SetSelectedDocumentsOn(bool) {}
		virtual void HandBackIfHeld(const UIDRef&) {}
		virtual bool IsHeldDoc(const UIDRef&) { return false; }
	};

	KFCResultsStandInImpl	gResultsStandIn;
	KFCRunsStandInImpl		gRunsStandIn;
	KFCChaptersStandInImpl	gChaptersStandIn;
}

bool KFCBoundaryMatches()
{
	if (gBoundary >= 0)
		return gBoundary == 1;

	// No session - the application going away: nothing to compare with, and nothing kept. The doors come back nil then,
	// as they always did.
	ISession* const session = GetExecutionContextSession();
	if (session == nil)
		return true;

	// Asked once, so the test build's boundary-mismatch switch (KFCDiag.h) is read once too - it takes effect from the
	// first door of a session.
	const char* modelStamp = nil;
	const bool same = KFCOtherHalfIsThisBuild(session, IID_IKFCBOUNDARY, &modelStamp);
	if (!same)
		BuildMismatchText(modelStamp);
	gBoundary = same ? 1 : 0;
	return same;
}

const char* KFCBoundaryMismatchText()
{
	return gMismatchText.c_str();
}

IKFCResults* KFCResultsStandIn()
{
	return &gResultsStandIn;
}

IKFCRuns* KFCRunsStandIn()
{
	return &gRunsStandIn;
}

IKFCChapters* KFCChaptersStandIn()
{
	return &gChaptersStandIn;
}

// End, KFCModelAccess.cpp.
