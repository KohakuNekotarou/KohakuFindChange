//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCResults - one of the doors the UI half reaches the model half through (the model/UI split;
//  docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The RESULT SET - KFCResultModel's questions, and the changes the panel makes to it, each named below (a row's
//  document rebound and its outcome - what a jump learns of it; the row eye's click; Clear from Results).
//  Each method is the KFCResultModel function named beside it; the contract is written there.
//
//  ON kSessionBoss, NOT A FACADE ON kUtilsBoss - this note is all the doors' (IKFCRuns.h and IKFCChapters.h point
//  here). What is behind them is session STATE (the results, the held chapters, the marker), and the guide's
//  facades keep no global or static state (guide gs-04); the session is where InDesign keeps its own session state
//  (IBookManager, IClipboardController). The UI half reaches this door with KFCResults() (KFCModelAccess.h).
//  EDITED BY HAND. The doors IKFCResults / IKFCRuns / IKFCChapters and their implementation (KFCModelServices.cpp)
//  were first generated from one table (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py); methods have been
//  added by hand since, which the table does not know, so the generator is a record only and refuses to write. A
//  new method goes at the END of its interface - a vtable slot is a promise to every built caller. One taken out
//  moves every slot below it, so both halves are built together then (nothing outside KFC calls these doors).
//  Either way kKFCBoundaryRevision goes up by one (KFCBoundaryID.h) - a UI half built against the old slots then
//  stands in for the doors instead of calling them - and the stand-in (ui/KFCModelAccess.cpp) gains or loses the
//  method too (nothing complains of a stand-in method its door no longer has).
//
//========================================================================================

#ifndef __IKFCResults_h__
#define __IKFCResults_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KFCModelTypes.h"	// the types the methods carry

class IDataBase;

class IKFCResults : public IPMUnknown
{
public:
	// No kDefaultIID, on purpose: the UI half reaches this door through KFCResults() alone, which checks the boundary
	// stamp first (KFCModelAccess.h) - a Query of its own by UseDefaultIID() does not compile.

	/** = KFCResultModel::GetTotalHitCount. */
	virtual int32 GetTotalHitCount() = 0;
	/** = KFCResultModel::GetDisplayChapterCount. */
	virtual int32 GetDisplayChapterCount() = 0;
	/** = KFCResultModel::GetDisplayHitCount. */
	virtual int32 GetDisplayHitCount(int32 chapterIdx) = 0;
	/** = KFCResultModel::GetChapterDisplay. */
	virtual bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount) = 0;
	/** = KFCResultModel::GetShownChapter. */
	virtual int32 GetShownChapter(int32 nth) = 0;
	/** = KFCResultModel::GetShownChapterPos. */
	virtual int32 GetShownChapterPos(int32 chapterIdx) = 0;
	/** = KFCResultModel::GetDisplayFontCount. */
	virtual int32 GetDisplayFontCount(int32 chapterIdx) = 0;
	/** = KFCResultModel::GetDisplayFontHitCount. */
	virtual int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx) = 0;
	/** = KFCResultModel::GetFontDisplay. */
	virtual bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount) = 0;
	/** = KFCResultModel::GetFontGroupHit. */
	virtual int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth) = 0;
	/** = KFCResultModel::GetHitFontGroup. */
	virtual int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCResultModel::GetHitFontGroupPos. */
	virtual int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCResultModel::IsStoryGroup. */
	virtual bool IsStoryGroup(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KFCResultModel::GetHitRow. */
	virtual bool GetHitRow(int32 chapterIdx, int32 hitIdx, KFCResultModel::RowDisplay& out) = 0;
	/** = KFCResultModel::MarkUpBreaksForDisplay. */
	virtual void MarkUpBreaksForDisplay(PMString& s) = 0;
	/** = KFCResultModel::IsFromBook. */
	virtual bool IsFromBook() = 0;
	/** = KFCResultModel::GetSearchScope. */
	virtual KFCResultModel::SearchScopeKind GetSearchScope() = 0;
	/** = KFCResultModel::GetBookName. */
	virtual PMString GetBookName() = 0;
	/** = KFCResultModel::HasRun. */
	virtual bool HasRun() = 0;
	/** = KFCResultModel::GetChapterLocation. */
	virtual bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile) = 0;
	/** = KFCResultModel::GetHitLocation. */
	virtual bool GetHitLocation(int32 chapterIdx, int32 hitIdx, UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd) = 0;
	/** = KFCResultModel::GetHitFlags. */
	virtual bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outReplaced, bool& outLocked) = 0;
	/** = KFCResultModel::GetHitReach. */
	virtual bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden) = 0;
	/** = KFCResultModel::RebindChapterDoc. */
	virtual void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef) = 0;
	/** = KFCResultModel::SetHitOutcome. */
	virtual void SetHitOutcome(int32 chapterIdx, int32 hitIdx, KFCResultModel::ChangeOutcome outcome) = 0;
	/** = KFCResultModel::HasChangeAllWritten (appended) - the panel's pencil cat. */
	virtual bool HasChangeAllWritten() = 0;
	/** = KFCResultModel::GetChapterReplacedCount / GetFontReplacedCount / GetTotalReplacedCount (appended) -
	    the R of the branch rows' "(R/M)". */
	virtual int32 GetChapterReplacedCount(int32 chapterIdx) = 0;
	virtual int32 GetFontReplacedCount(int32 chapterIdx, int32 fontIdx) = 0;
	virtual int32 GetTotalReplacedCount() = 0;
	/** = KFCResultModel::GetHitItem (appended) - the page item an object row stands for, kInvalidUID for a text row. */
	virtual UID GetHitItem(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCResultModel::GetResultSetId (appended) - a new number every time the list is made anew
	    (KFCResultModel.h says when): what tells the panel a search went through (KFCResultHistory - the results kept in
	    tiers). */
	virtual uint32 GetResultSetId() = 0;
	/** = KFCResultModel::SetHitKept (appended) - the row eye's click: a row's eye on or off. false = no such row. */
	virtual bool SetHitKept(int32 chapterIdx, int32 hitIdx, bool kept) = 0;
	/** = KFCResultModel::NarrowWords (appended) - a narrowed list's conditions in words ("In Tables / Pages 1-5"; empty for
	    any other list): a tier's heading names them after its name. */
	virtual void GetNarrowWords(PMString& out) = 0;
	/** = KFCResultModel::ListHoldsObjects (appended) - the list's rows are object rows (an Object tab search's, a
	    Frames of These Matches list's): the heading's menu greys a text-only condition by it, and greys
	    Search Text in These Objects over text rows. */
	virtual bool ListHoldsObjects() = 0;
	/** = KFCResultModel::ClearRows (appended) - Clear from Results: the rows taken off the list. Answers how many. */
	virtual int32 ClearRows(const KFCRowsByChapter& rows) = 0;
	/** = KFCResultModel::GetObjectRowHit (appended) - an object list's chapter: its nth row shown, as a hit index. */
	virtual int32 GetObjectRowHit(int32 chapterIdx, int32 nth) = 0;
	/** = KFCResultModel::IsHitItemGone (appended) - an object row whose item its document came back without: its UID
	    may name another object by now, so the row's click and its Replace's selection take it for gone. */
	virtual bool IsHitItemGone(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCResultModel::IsFromAllDocuments (appended) - the list was searched over All Documents, whatever its scope says
	    (a text list Search Text in These Objects made from one is kScopeInObjects): its document rows come up closed and a
	    document with no window says so. */
	virtual bool IsFromAllDocuments() = 0;
};

#endif // __IKFCResults_h__
