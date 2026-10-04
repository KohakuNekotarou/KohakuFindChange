//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  IKBSResults - one of the three doors the UI half reaches the model half through (2026-10-01, the
//  model/UI split; docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The RESULT SET - KBSResultModel's questions and the few changes the panel makes to it (the
//  boxes, the row a right-click menu was popped over, where a jump found a row). Each method is the
//  KBSResultModel function of the same name; the contract is written there.
//
//  ***** ON kSessionBoss, NOT A FACADE ON kUtilsBoss. ***** What is behind it is session STATE (the results,
//  the held chapters, the marker), and the guide's facades keep no global or static state (gs-04); the
//  session is where InDesign keeps its own session state (IBookManager, IClipboardController). The UI
//  half reaches it with KBSResults() (KBSModelAccess.h).
//  ***** EDITED BY HAND SINCE 2026-10-02. ***** Generated on 2026-10-01 with its two siblings and their
//  implementation (KBSModelServices.cpp) from one table (work/sdd/2026-10-01-kbs-model-ui-split/gen_ifaces.py).
//  Methods have been added by hand since - to IKBSRuns, and to this one (2026-10-04) - which the table does
//  not know: the generator is a record now and refuses to write. A new method goes at the END of its interface - a vtable slot is a
//  promise to every built caller.
//
//========================================================================================

#ifndef __IKBSResults_h__
#define __IKBSResults_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KBSBoundaryID.h"	// IID_IKBSRESULTS
#include "KBSModelTypes.h"	// the types the methods carry

class IDataBase;

class IKBSResults : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKBSRESULTS };

	/** = KBSResultModel::GetChapterCount. */
	virtual int32 GetChapterCount() = 0;
	/** = KBSResultModel::GetTotalHitCount. */
	virtual int32 GetTotalHitCount() = 0;
	/** = KBSResultModel::GetDisplayChapterCount. */
	virtual int32 GetDisplayChapterCount() = 0;
	/** = KBSResultModel::GetDisplayHitCount. */
	virtual int32 GetDisplayHitCount(int32 chapterIdx) = 0;
	/** = KBSResultModel::GetChapterDisplay. */
	virtual bool GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount) = 0;
	/** = KBSResultModel::GetShownChapter. */
	virtual int32 GetShownChapter(int32 nth) = 0;
	/** = KBSResultModel::GetShownChapterPos. */
	virtual int32 GetShownChapterPos(int32 chapterIdx) = 0;
	/** = KBSResultModel::GetDisplayFontCount. */
	virtual int32 GetDisplayFontCount(int32 chapterIdx) = 0;
	/** = KBSResultModel::GetDisplayFontHitCount. */
	virtual int32 GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx) = 0;
	/** = KBSResultModel::GetFontDisplay. */
	virtual bool GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount) = 0;
	/** = KBSResultModel::GetFontGroupHit. */
	virtual int32 GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth) = 0;
	/** = KBSResultModel::GetHitFontGroup. */
	virtual int32 GetHitFontGroup(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSResultModel::GetHitFontGroupPos. */
	virtual int32 GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSResultModel::GetDisplayRunCount. */
	virtual int32 GetDisplayRunCount(int32 chapterIdx) = 0;
	/** = KBSResultModel::GetRunDisplay. */
	virtual bool GetRunDisplay(int32 chapterIdx, int32 runIdx, PMString& outLabel, int32& outHitCount) = 0;
	/** = KBSResultModel::GetDisplayRunGroupCount. */
	virtual int32 GetDisplayRunGroupCount(int32 chapterIdx, int32 runIdx) = 0;
	/** = KBSResultModel::GetRunGroup. */
	virtual int32 GetRunGroup(int32 chapterIdx, int32 runIdx, int32 nth) = 0;
	/** = KBSResultModel::GetGroupRun. */
	virtual int32 GetGroupRun(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSResultModel::GetGroupPosInRun. */
	virtual int32 GetGroupPosInRun(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSResultModel::IsStoryGroup. */
	virtual bool IsStoryGroup(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSResultModel::GetGroupCheckedCount. */
	virtual int32 GetGroupCheckedCount(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSResultModel::GetHitRow. */
	virtual bool GetHitRow(int32 chapterIdx, int32 hitIdx, KBSResultModel::RowDisplay& out) = 0;
	/** = KBSResultModel::GetHitDisplay. */
	virtual bool GetHitDisplay(int32 chapterIdx, int32 hitIdx, PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost) = 0;
	/** = KBSResultModel::GetRowsBefore. */
	virtual bool GetRowsBefore(int32 chapterIdx, const std::vector<int32>& rows, PMString& outPre, PMString& outOriginal, PMString& outPost) = 0;
	/** = KBSResultModel::MarkUpBreaksForDisplay. */
	virtual void MarkUpBreaksForDisplay(PMString& s) = 0;
	/** = KBSResultModel::IsFromBook. */
	virtual bool IsFromBook() = 0;
	/** = KBSResultModel::IsFromRecords. */
	virtual bool IsFromRecords() = 0;
	/** = KBSResultModel::GetSearchScope. */
	virtual KBSResultModel::SearchScopeKind GetSearchScope() = 0;
	/** = KBSResultModel::GetBookName. */
	virtual PMString GetBookName() = 0;
	/** = KBSResultModel::HasRun. */
	virtual bool HasRun() = 0;
	/** = KBSResultModel::IsShowingReplaceOutcome. */
	virtual bool IsShowingReplaceOutcome() = 0;
	/** = KBSResultModel::NoRowHasCheckBox. */
	virtual bool NoRowHasCheckBox() = 0;
	/** = KBSResultModel::GetCheckedCount. */
	virtual int32 GetCheckedCount() = 0;
	/** = KBSResultModel::GetChapterCheckedCount. */
	virtual int32 GetChapterCheckedCount(int32 chapterIdx) = 0;
	/** = KBSResultModel::GetCheckableCount. */
	virtual int32 GetCheckableCount() = 0;
	/** = KBSResultModel::GetChapterCheckableCount. */
	virtual int32 GetChapterCheckableCount(int32 chapterIdx) = 0;
	/** = KBSResultModel::SetHitChecked. */
	virtual void SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked) = 0;
	/** = KBSResultModel::SetGroupChecked. */
	virtual void SetGroupChecked(int32 chapterIdx, int32 groupIdx, bool checked) = 0;
	/** = KBSResultModel::SetChapterChecked. */
	virtual void SetChapterChecked(int32 chapterIdx, bool checked) = 0;
	/** = KBSResultModel::SetAllChecked. */
	virtual void SetAllChecked(bool checked) = 0;
	/** = KBSResultModel::SetContextMenuChapter. */
	virtual void SetContextMenuChapter(int32 chapterIdx) = 0;
	/** = KBSResultModel::GetContextMenuChapter. */
	virtual int32 GetContextMenuChapter() = 0;
	/** = KBSResultModel::SetContextMenuGroup. */
	virtual void SetContextMenuGroup(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSResultModel::GetContextMenuGroup. */
	virtual bool GetContextMenuGroup(int32& outChapterIdx, int32& outGroupIdx) = 0;
	/** = KBSResultModel::SetContextMenuRun. */
	virtual void SetContextMenuRun(int32 chapterIdx, int32 runIdx) = 0;
	/** = KBSResultModel::GetContextMenuRun. */
	virtual bool GetContextMenuRun(int32& outChapterIdx, int32& outRunIdx) = 0;
	/** = KBSResultModel::SetContextMenuHit. */
	virtual void SetContextMenuHit(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSResultModel::GetContextMenuHit. */
	virtual bool GetContextMenuHit(int32& outChapterIdx, int32& outHitIdx) = 0;
	/** = KBSResultModel::GetChapterLocation. */
	virtual bool GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile) = 0;
	/** = KBSResultModel::GetHitLocation. */
	virtual bool GetHitLocation(int32 chapterIdx, int32 hitIdx, UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd) = 0;
	/** = KBSResultModel::GetHitFlags. */
	virtual bool GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked) = 0;
	/** = KBSResultModel::GetHitReach. */
	virtual bool GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden) = 0;
	/** = KBSResultModel::GetHitInFootnote. */
	virtual bool GetHitInFootnote(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KBSResultModel::RebindChapterDoc. */
	virtual void RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef) = 0;
	/** = KBSResultModel::SetHitOutcome. */
	virtual void SetHitOutcome(int32 chapterIdx, int32 hitIdx, KBSResultModel::ChangeOutcome outcome) = 0;
	/** = KBSResultModel::GetGroupCheckableCount (2026-10-04, D-1 - at the end, as every new method goes). */
	virtual int32 GetGroupCheckableCount(int32 chapterIdx, int32 groupIdx) = 0;
	/** = KBSResultModel::GetHitTextUnchanged (2026-10-04, scenario cross-check 5 - at the end). */
	virtual bool GetHitTextUnchanged(int32 chapterIdx, int32 hitIdx) = 0;
};

#endif // __IKBSResults_h__
