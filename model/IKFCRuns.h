//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IKFCRuns - one of the doors the UI half reaches the model half through (the model/UI split;
//  docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md section 4.1).
//
//  The RUNS and what they ask - the search and the replace family, whether one is
//  running, the Find/Change settings they follow, and the questions the jump asks of a row's text.
//  Each method forwards to the function named beside it (KFCModelServices.cpp); the contract is
//  written where that function is declared.
//
//  ON kSessionBoss, NOT A FACADE ON kUtilsBoss, and EDITED BY HAND - IKFCResults.h says why, for all the doors. The
//  UI half reaches this door with KFCRuns() (KFCModelAccess.h). A new method goes at the END of the interface - a
//  vtable slot is a promise to every built caller; one taken out moves every slot below it, so both halves are
//  built together then. Either way kKFCBoundaryRevision goes up by one (KFCBoundaryID.h), and the stand-in
//  (ui/KFCModelAccess.cpp) takes the new method too.
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
	// No kDefaultIID, on purpose: the UI half reaches this door through KFCRuns() alone, which checks the boundary
	// stamp first (KFCModelAccess.h) - a Query of its own by UseDefaultIID() does not compile.

	/** = KFCSearchEngine::SearchBook. */
	virtual int32 SearchBook(PMString& outSummary) = 0;
	/** = KFCReplaceEngine::ReplaceHit. */
	virtual bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus) = 0;
	/** = KFCReplaceEngine::CanReplaceHit. */
	virtual bool CanReplaceHit(int32 chapterIdx, int32 hitIdx) = 0;
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
	// Added by hand from here on - a new method goes below the last one, never between (a vtable slot is a
	// promise to every built caller).
	/** = KFCSearchEngine::SetQuery. */
	virtual bool SetQuery(const PMString& text, int32 mode) = 0;
	/** = KFCChangeAll::Run - Change All in Book (No List). */
	virtual int32 ChangeAll(PMString& outSummary) = 0;
	/** = KFCChangeAll::ClearResults. */
	virtual bool ClearResults(PMString& outStatus) = 0;
	/** = KFCSearchEngine::HasFindQueryNow - Change All's greying. */
	virtual bool HasFindQueryNow() = 0;
	/** = KFCSavedQueries::List - the query dialog's left list. */
	virtual void ListSavedQueries(std::vector<KFCSavedQuery>& out) = 0;
	/** = KFCSavedQueries::Describe - one query file of the dialog's run order: its name, kind and whether it is there. */
	virtual void DescribeQueryFile(const IDFile& file, KFCSavedQuery& out) = 0;
	/** = KFCSavedQueries::KindName - "Text" / "GREP" / "Glyph" / "Object" / "Transliterate" / "?". */
	virtual const char* QueryKindName(int32 mode) = 0;
	/** = KFCQuerySequence::RunFiles - the query dialog's Run: these query files, in this order. */
	virtual int32 RunQueries(const std::vector<IDFile>& files, PMString& outSummary) = 0;
	/** = KFCSearchEngine::DescribeRunScope - the query dialog's Runs on: line (false = Cannot run: ...). */
	virtual bool DescribeRunScope(PMString& outWords) = 0;
	/** = KFCChangeAll::CommandName - "Change All in Book (No List)" / "Change All in Selected Documents (No List)". */
	virtual const char* ChangeAllCommandName() = 0;
	/** = KFCReplaceEngine::PreviewHit (appended) - a GREP row's after-text, written and thrown away. */
	virtual bool PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter) = 0;
	/** = KFCSavedQueries::LoadIntoFindChange (appended) - a query double-clicked in the query dialog, into Find/Change. */
	virtual bool LoadSavedQuery(const IDFile& file) = 0;
	/** = KFCSearchEngine::LocateRow (appended) - where a row is now: at its place, moved, found for this jump only, or
	    not found. */
	virtual KFCResultModel::RowLocation LocateRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, TextIndex& ioStart, TextIndex& ioEnd) = 0;
	/** = KFCSearchEngine::SearchStoryAgain (appended) - a story row's Search This Story Again. */
	virtual bool SearchStoryAgain(int32 chapterIdx, int32 groupIdx, PMString& outStatus) = 0;
	/** = KFCSearchEngine::RestoreSearchScope (appended) - Search: put back after a row's click (scheduled). */
	virtual void RestoreSearchScope(int32 mode, int32 scope) = 0;
	/** = KFCObjectReplace::CheckRowNow (appended) - an object row's Replace asked before its window is brought
	    forward: false = refused, outStatus says why. */
	virtual bool CheckObjectReplace(int32 chapterIdx, int32 hitIdx, PMString& outStatus) = 0;
	/** = KFCObjectReplace::CheckRowsNow (appended) - the rows selected together, of one document or several,
	    asked before a window is brought forward: true at the first row that would be written. */
	virtual bool CheckObjectReplaceRows(const KFCRowsByChapter& rows, PMString& outStatus) = 0;
	/** = KFCReplaceEngine::ReplaceHits (appended) - the rows selected together, object or text, of one
	    document or several, replaced in one undo step. */
	virtual bool ReplaceRows(const KFCRowsByChapter& rows, PMString& outStatus) = 0;
	/** = KFCSearchEngine::SearchDocumentAgain (appended) - a document row's Search This Document Again. */
	virtual bool SearchDocumentAgain(int32 chapterIdx, PMString& outStatus) = 0;
	/** = KFCTierStore::SwitchTo (appended) - a kept tier made the live list: its rows back as they were kept, Find/Change
	    left as it is (v2). False = refused, nothing changed; outSummary says why. */
	virtual bool SwitchToTier(uint32 resultSet, PMString& outSummary) = 0;
	/** = KFCTierStore::GetTierSets (appended) - the kept tiers' result sets, in search order (the oldest first). */
	virtual void GetKeptTierSets(std::vector<uint32>& out) = 0;
	/** = KFCTierStore::DropAll (appended) - every kept tier let go, and the live list's query copy with them. */
	virtual void DropKeptTiers() = 0;
	/** = KFCTierStore::DropTier (appended) - one tier let go, a kept one or the live list (a tier heading's Clear These
	    Results). False = nothing let go. */
	virtual bool DropTier(uint32 resultSet) = 0;
	/** = KFCSearchEngine::SearchListAgain (appended) - a tier's heading's Search These Results Again: the live list's own
	    search run once more, its rows replaced in place. False = refused or cancelled, the list as it was. */
	virtual bool SearchListAgain(PMString& outStatus) = 0;
	/** = KFCObjectOutline::OutlineRows (appended) - an object row's Special Operations > Create Outlines: the rows' frames
	    outlined, one undo step. False = nothing written; outStatus says why. */
	virtual bool OutlineRows(const KFCRowsByChapter& rows, PMString& outStatus) = 0;
	/** = KFCReplaceEngine::CanRestoreHit (appended) - a text row's Restore Original offered: a row a Replace of the list
	    wrote, its copy held. */
	virtual bool CanRestoreHit(int32 chapterIdx, int32 hitIdx) = 0;
	/** = KFCReplaceEngine::RestoreHits (appended) - Restore Original over text rows: the copies their Replace took put back,
	    one undo step. False = nothing written; outStatus says why. */
	virtual bool RestoreRows(const KFCRowsByChapter& rows, PMString& outStatus) = 0;
	/** = KFCObjectOutline::CanOutlineHit (appended) - an object row's Create Outlines offered: its doors, read only (a
	    document not open now answers true). */
	virtual bool CanOutlineHit(int32 chapterIdx, int32 hitIdx) = 0;
};

#endif // __IKFCRuns_h__
