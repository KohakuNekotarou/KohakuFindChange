//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCResults - one of the three doors the UI half reaches the model half through (the model/UI split;
//  docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The RESULT SET - KFCResultModel's questions and the few changes the panel makes to it (the
//  boxes, the row a right-click menu was popped over, where a jump found a row). Each method is the
//  KFCResultModel function of the same name; the contract is written there.
//
//  ON kSessionBoss, NOT A FACADE ON kUtilsBoss. What is behind it is session STATE (the results, the held
//  chapters, the marker), and the guide's facades keep no global or static state (gs-04); the session is
//  where InDesign keeps its own session state (IBookManager, IClipboardController). The UI half reaches it
//  with KFCResults() (KFCModelAccess.h).
//  EDITED BY HAND. The three doors and their implementation (KFCModelServices.cpp) were first generated from
//  one table (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py); methods have been added by hand since,
//  which the table does not know, so the generator is a record only and refuses to write. A new method goes
//  at the END of its interface - a vtable slot is a promise to every built caller. One taken out moves every
//  slot below it, so both halves are built together then (nothing outside KFC calls these doors).
//
//========================================================================================

#ifndef __IKFCResults_h__
#define __IKFCResults_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KFCBoundaryID.h"	// IID_IKFCRESULTS
#include "KFCModelTypes.h"	// the types the methods carry

class IDataBase;

class IKFCResults : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKFCRESULTS };

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
	/** = KFCResultModel::SetContextMenuHit. */
	virtual void SetContextMenuHit(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCResultModel::GetContextMenuHit. */
	virtual bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx) = 0;
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
};

#endif // __IKFCResults_h__
