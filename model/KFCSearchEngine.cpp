//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Search engine implementation. See KFCSearchEngine.h for the contract. The walker loop is
//  ported from KESCL's CollectMatches (KESCL left untouched); the one change is the point of
//  this plugin: KFC does NOT set the find string or pin the search mode - it walks with the
//  user's current Find/Change options as they are, so the mode (Text or GREP) is followed.
//
//  For each match the finder hands back (story, start, end); BuildHit then reads the containing
//  paragraph (IComposeScanner::FindSurroundingParagraph + CopyText) and splits it into the three
//  segments the colour cell paints. The offsets are TextIndex throughout, and a TextIndex counts
//  CODE POINTS - a surrogate pair is ONE, the same unit WideString::CharCount counts in (measured
//  on the running application). So a match boundary cannot land inside a pair and no
//  character is ever cut in half.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"		// FindSurroundingParagraph / CopyText (the hit line's text)
#include "IDocument.h"
#include "IDocumentList.h"			// Search: = All Documents - every open document
#include "IFindChangeOptions.h"
#include "IFindChangeCmdData.h"
#include "IFindChangeService.h"		// FindChangeResult enum
#include "ICommand.h"
#include "IIntData.h"				// kFindSearchModeCmdBoss carries two of these - see CommitSearchMode
#include "IStringData.h"			// kFindStringCmdBoss's string - SetQuery
#include "IBoolData.h"				// kFindChangeGlyphIDCmdBoss: which side of the glyph search is being set
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"		// a story row's first words: the threads after the body (StoryLeadText)
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"
#include "ISession.h"				// GetExecutionContextSession
#include "IStoryList.h"				// GetUserAccessibleStoryCount - how many stories a walk will visit
// For naming the page a match sits on (the "P<page>(<n>)" hit-row locator):
#include "ITextParcelList.h"		// GetParcelContaining - the parcel a position is in
#include "IParcelList.h"			// GetParcelFrameUID - is that parcel placed in a frame?
#include "ParcelKey.h"				// ParcelKey::IsValid
#include "IHierarchy.h"				// the parcel frame as a page item
#include "ITextUtils.h"				// GetPageUIDRef - the purpose-built textFrame -> page lookup
#include "ILayoutUtils.h"			// GetOwnerPageUID - frame -> page (the general fallback)
#include "IPageList.h"				// GetPageString / GetPageIndex - page -> "12" / "A:1"
// For marking a match that sits on a switched-off layer ("Hidden" on the hit-row locator):
#include "ILayerUtils.h"			// GetLayerUID - the spread layer a frame sits on
#include "ISpreadLayer.h"			// GetDocLayerUID - spread layer -> the Layers panel's row
#include "IDocumentLayer.h"			// IsVisible / IsLocked - is that layer switched off, or locked?
#include "IPageItemVisibilityFacade.h"	// IsHidden - Object > Hide on the frame itself, which no layer says
// For refusing to rewrite locked text the way the Find/Change dialog does ("Search Only"):
#include "IItemLockData.h"			// GetInsertLock - the story's own "content cannot be edited"
#include "ILockPosition.h"			// IsPageItemLocked - Object > Lock on the frame itself

// General includes:
#include "AttributeBossList.h"		// the Find Format list the search remembers (RememberFindFormat)
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss, kFindChangeClientBoss, kTextWalkerService(...)
#include "CTextEnum.h"				// Text::GlyphID / kInvalidGlyphID (the Glyph tab's query)
#include "TextChar.h"				// kTextChar_Ellipse - the mark a cut segment carries (SplitLineWithScanner, StoryLeadText)
#include "UnicodeClass.h"			// IsWhiteSpace / IsIgnoredCharacter - which characters a story row's first words keep
#include "WalkerScopeOptions.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
#include "KFCProgressBar.h"		// the search's progress + cancel (both scopes) - the bar is the UI half's
#include "CmdUtils.h"
#include "CreateObject.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "PersistUtils.h"			// ::GetClass (StoryLeadText: which thread is a deletion's)
#include "InCopySharedID.h"			// kDeletedTextBoss - the thread Track Changes keeps a deletion's text in
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "Utils.h"
#include "WideString.h"

#include <vector>
#include <utility>					// std::move - a finished chapter is handed to the model, not copied
#include <algorithm>				// std::stable_sort (the matches' page order)
#include <map>						// the per-frame cache one document's walk keeps (FrameFacts)
#include <memory>					// std::unique_ptr - a Search: walk's dirty guards, one per open document
#include <new>						// std::nothrow - HitBuilder's cache

// Project includes:
#include "KFCSearchEngine.h"
#include "KFCTrackChange.h"		// IsInFootnote (a footnote's row cannot be taken back)
#include "KFCBookScope.h"
#include "KFCResultModel.h"
#include "KFCRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KFCOversetLocator.h"	// the "+" page for an overset hit (locator + sort key)
#include "KFCReplaceEngine.h"	// QueryUnchangedSinceSearch - RelocateStaleRow looks again only under its own query

namespace
{

// (The whole-run safety ceiling is kKFCCollectHitLimit, in KFCResultModel.h beside the display cap.
// Read the contract there; this file uses it in SearchBook.)

// The smallest advance worth reporting to the progress bar. Moving the bar keeps Cancel answering (which
// call on the bar takes the click is not measured - see KFCAdvanceProgress), but it is not free: doing it
// once per hit would repaint the bar thousands of times over a large chapter. Small enough that Cancel
// still answers promptly.
const int32 kKFCProgressReportStep = 8;

// How much of the progress bar one CHAPTER gets. Every chapter gets the same slice, because the
// run does not know how big a chapter is before it opens it: chapters are opened one at a time
// and closed again straight after, so there is no all-chapters-open moment in which to add up
// their story counts.
//
// Within a chapter the slice is still divided by STORIES - the search cannot know how many MATCHES
// a chapter holds until it has found them all, but a document that IS open can be asked for its
// story count for free, and each story is then subdivided by how far into its text the walk has
// got (CollectHitsInDoc does the moving; see CountSearchableStories).
//
// Large enough that a chapter with many stories still gets whole steps per story (a 500-story
// chapter gets 20 apiece); small enough that a 100-chapter book stays far inside int32.
const int32 kKFCChapterProgressSpan = 10000;

/** How many stories a walk of this document will visit. Counting them is free: IStoryList keeps the
    count, so nothing is loaded here (contrast with adding up their lengths, which would have to
    fetch every story before the search had even started).

    USER-ACCESSIBLE ones only, which is exactly the walk's own population - IStoryList.h:40-42 says
    internal stories "are not subject to search through find change". */
int32 CountSearchableStories(const UIDRef& docRef)
{
	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return 0;
	return storyList->GetUserAccessibleStoryCount();
}

// How a chapter's walk ended - which is NOT the same question as how many matches it found.
// Returning zero hits for a chapter that was never actually searched reads as "this chapter has no
// matches", and there is no way for the user to see through that. Every ending below except
// kChapterWalked is therefore counted and named in the summary.
enum ChapterWalkResult
{
	kChapterWalked = 0,			// the walk ran to the end (it may legitimately have found nothing)
	kChapterNoDatabase,			// the chapter's UIDRef carries no database, so there is nothing to walk
	kChapterNoOptions,			// no Find/Change options to search with
	kChapterNoWalker,			// the text-walker service handed out no walker
	kChapterNoScope,			// QueryDocumentWalkerScope refused this document
	kChapterNoClient,			// the find/change walker client could not be created
	kChapterNoSelectionUtils,	// the walker has no ITextWalkerSelectionUtils to hold the critical section with

	// ...and the one that is not a "never started" at all: the walk DID start, found what it found,
	// and then broke off with an error partway through. Its hits are real and are kept; what is
	// missing is the rest of the chapter, which is why it gets a sentence of its own rather than
	// being reported as "could not be searched". See CollectHitsInDoc's loop.
	kChapterWalkFailed,

	// ...and the user pressed Cancel while the walk was going - heard when the walk moved on to another
	// story (the author's call: story by story, as the replace). The run is thrown away, so
	// nothing about this ending is said.
	kChapterCancelled
};

/** A short reason to put in the status line. Not translatable - it names internals.
    Answers only for the endings that mean "this chapter was never walked at all". The other two
    have sentences of their own and are turned away by the caller before they reach here:
    kChapterWalked has nothing to report, and kChapterWalkFailed gets AppendSearchErrorNote's
    wording because its hits ARE in the list. Both come back empty, which the caller reads as
    "add no reason" rather than writing a name with a dangling colon after it. */
const char* ChapterWalkResultText(ChapterWalkResult result)
{
	switch (result)
	{
		case kChapterNoDatabase:		return "no database";
		case kChapterNoOptions:			return "no find options";
		case kChapterNoWalker:			return "no text walker";
		case kChapterNoScope:			return "no walker scope";
		case kChapterNoClient:			return "no walker client";
		case kChapterNoSelectionUtils:	return "no walker selection utils";
		default:						return "";
	}
}

/** Name the chapters that were never searched at all, and why. Each entry is already "name: reason"
    - the name is the user's and the reason names internals, so neither is a translation key.
    Appends nothing when every chapter was walked, so the ordinary summary is unchanged. The shape is
    every chapter note's (KFCBookScope::AppendChapterNote). */
void AppendUnsearchableNote(PMString& outSummary, const std::vector<PMString>& entries)
{
	KFCBookScope::AppendChapterNote(outSummary, "could not be searched", entries, ".");
}

/** ...and the chapters whose walk BROKE OFF partway. A different sentence from the one above on
    purpose: these chapters were searched, their hits are in the list, and what is wrong is that the
    search stopped early - so the part of them that is missing from the list is missing because
    nobody looked, not because there was nothing there. Saying "could not be searched" would be
    false in one direction and saying nothing at all would be false in the other. */
void AppendSearchErrorNote(PMString& outSummary, const std::vector<PMString>& names)
{
	KFCBookScope::AppendChapterNote(outSummary, "stopped with a search error", names,
		" - the rest of each was never searched.");
}

// (A chapter that could not be OPENED is KFCBookScope::AppendUnopenableNote's sentence - one copy, so
// the wording cannot drift. It is a DIFFERENT failure from AppendUnsearchableNote's above: there the
// document was open and the WALK did not run, here there is no document at all.)

// A search is running. The progress bar pumps events while it is up, so without this a menu command
// could be dispatched INTO the running search. The panel's actions read it through IsSearching().
bool gSearching = false;

// The FIND FORMAT the results on the panel were searched with - a shallow copy of the dialog's find
// attribute list, taken when the search ran and compared before Change Checked re-walks. See
// KFCSearchEngine::RememberFindFormat for why the list is COPIED rather than described.
//
// WHY IT LIVES HERE AND NOT ON KFCResultModel, beside the walk signature it belongs with: that
// header states its own rule - the search mode is "held as a plain int so this header needs no text
// includes" - and an AttributeBossList member would drag the text headers into every file that
// includes the model. The lifetime is kept in step instead by KFCSearchEngine::DropResults, which
// every place that throws the results away calls (KFCBookWatch.cpp, which hands its book back first
// on its own, pairs the model's Clear() with ForgetSearchedFindFormat itself).
boost::shared_ptr<AttributeBossList> gSearchedFindAttrs;

// The attribute database that list's UIDs are in. Kept beside it because a UID means nothing
// without its database, so a list from a DIFFERENT one must not be compared against it - see
// FindFormatHasChanged.
IDataBase* gSearchedFindAttrDB = nil;

// Raise gSearching for the length of a search, whichever way SearchBook returns.
struct SearchingFlagGuard
{
	SearchingFlagGuard()	{ gSearching = true; }
	~SearchingFlagGuard()	{ gSearching = false; }
};

// Is there anything to find on the Find/Change panel right now (in the current mode)?
bool HasFindQuery()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// The attribute database is the first argument of the call below, so it is tested before it is
	// handed over - the order every reader of the format pane in this file keeps. False with no
	// database, because this door STOPS a search: "cannot tell" must not start one.
	IDataBase* const queryDB = opts->GetUIDAttrDB();
	if (queryDB == nil)
		return false;

	// ASK THE OPTIONS, NEVER THE FIND STRING.
	// IFindChangeOptions.h:684-699 says this interface "is in a unique position to know whether
	// there is adequate information defined on it to allow searching for 'something'", and spells
	// out what that replaced: code that used to ask whether the find string was longer than zero
	// "OR there was a special character or paragraph format to search for". Both halves matter here.
	//
	// TWO tabs need the format half, for different reasons:
	//   - Glyph: its query is NOT a find string at all - it is a glyph ID plus the font it belongs
	//     to, held in the attribute database (GetUIDAttrDB). Asking about the find string happened
	//     to give a usable answer only because picking a glyph whose character exists also leaves
	//     that character in the box; pick one with no character of its own and the string is empty.
	//   - Text / GREP: FIND FORMAT alone is a legitimate query - "every place carrying this
	//     paragraph style" - with the find box deliberately empty. The dialog allows it, and KFC
	//     runs the same kFindTextCmdBoss the dialog does, so the walk handles it; only this door
	//     would be shut. Asking !GetFindString(mode).empty() here turns that away.
	return opts->IsThereSomethingToFind(queryDB, mode) != kFalse;
}

// Process one of the find/change commands, and say whether it went through. A failure's error state is
// cleared at once, whatever the caller then does: left standing it fails every find command after it
// (and rolls back the sequence of whatever runs next). The caller is told, so it can stop rather than
// walk - or write - by a value that is not the one on screen.
bool ProcessFindChangeCmd(ICommand* cmd)
{
	if (CmdUtils::ProcessCommand(cmd) == kSuccess)
		return true;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return false;
}

// Re-state one side of the Glyph tab's query on the find/change options. The command carries two
// fields: IIntData is the glyph itself, and IBoolData picks the side - kTrue for the glyph being
// looked for, kFalse for the one that replaces it. This is what SnpFindAndReplace does in
// Do_FindGlyph and Do_ReplaceGlyph, the only worked glyph example in the SDK.
//
// THE ANSWER IS RETURNED, NOT SWALLOWED. false means the value was NOT stated, and the engine will
// therefore walk - or write - with whatever was committed last. On the replace side that is a glyph
// the user never chose on this run and cannot see anywhere on screen, which is the exact failure
// CommitReplaceSide exists to prevent; on the find side it is a query nobody typed.
bool CommitGlyphID(Text::GlyphID glyphID, bool16 findSide)
{
	// An empty box means different things on the two sides, so they are treated differently.
	//
	// FIND side: nothing to look for, and nothing wrong - the search is stopped before it ever gets
	// here (HasFindQuery asks IsThereSomethingToFind), so committing -1 would only clear what the dialog
	// already holds. That side is left alone, and that is a success, not a failure.
	//
	// REPLACE side: an empty Change To box is a legitimate request - it DELETES every match, exactly
	// as an empty change string does on the Text tab, and the Find/Change dialog itself allows it
	// (confirmed against the dialog). There the -1 MUST be stated: leaving it unstated
	// makes the replace command fall back on whatever change glyph was committed last, writing a
	// glyph the user did not choose and cannot see anywhere on screen. Stating it is what overwrites
	// that leftover.
	if (glyphID == kInvalidGlyphID && findSide)
		return true;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindChangeGlyphIDCmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IIntData>  value(cmd, UseDefaultIID());
	InterfacePtr<IBoolData> side(cmd, UseDefaultIID());
	if (value == nil || side == nil)
		return false;
	value->Set(glyphID);
	side->Set(findSide);
	return ProcessFindChangeCmd(cmd);
}

// Process one of the find/change option commands that carry a single int - the shape of
// SnpFindAndReplace's ProcessFindChangeCommandInt32: the value in the default IIntData, and which
// mode's settings are being addressed in IID_IFINDCHANGEMODEDATA where the boss carries one
// (kFindChangeModeCmdBoss does; the two character-type bosses hold only the value - checked
// against a live object-model dump - so that field is set only when it answers).
//
// Returns whether the value was stated, for the same reason CommitGlyphID does: the snippet this is
// shaped after hands its ErrorCode back too, and every caller of it stops on a failure
// (SnpFindAndReplace.cpp:511-516, :598-603, :623-628).
bool CommitFindChangeInt(const ClassID& cmdBoss, int32 value, int32 mode)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IIntData> valueData(cmd, UseDefaultIID());
	if (valueData == nil)
		return false;
	valueData->Set(value);
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (modeData != nil)
		modeData->Set(mode);
	return ProcessFindChangeCmd(cmd);
}

// A frame UID -> its page, named the way the Pages panel names it (section prefix and all). Shared
// by the visible-match path (the match's own frame) and the overset path (the "+" indicator's
// frame). false only when neither a page nor a spread can be resolved. outPageIndex is the page's
// plain document order (for sorting); the STRING follows the section, so it can read "iv" or "A-1",
// and "PB" for a frame sitting on the pasteboard.
bool GetFramePageString(const UIDRef& docRef, UID frameUID, PMString& outPage, int32& outPageIndex)
{
	outPage.Clear();
	outPage.SetTranslatable(kFalse);
	outPageIndex = -1;
	if (frameUID == kInvalidUID)
		return false;

	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// ITextUtils::GetPageUIDRef is the purpose-built lookup - its contract is "the page the given
	// textFrame is on" - and everything that reaches this function IS a text frame, since it comes
	// from IParcelList::GetParcelFrameUID.
	UID pageUID = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID)).GetUID();

	// Fall back to the general page-item lookup, which answers for anything in the hierarchy.
	//
	// That fallback is also what keeps a PASTEBOARD hit readable, and it is deliberate: a frame that
	// sits on no page makes GetOwnerPageUID return the SPREAD's UID, and GetPageString takes a
	// spread UID too and spells it "PB" (IPageList.h:131, "Given a page UID (or spread UID)"). A
	// match out on the pasteboard should say where it is rather than drop out of the list, so this
	// does NOT check that the UID is a kPageBoss before using it. (Code that wants real pages only
	// has to verify with db->GetClass - see KESCM's overset scan, which does exactly that.)
	if (pageUID == kInvalidUID)
	{
		InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
		if (frameHier == nil)
			return false;
		pageUID = Utils<ILayoutUtils>()->GetOwnerPageUID(frameHier);
	}
	if (pageUID == kInvalidUID)
		return false;

	InterfacePtr<IPageList> pageList(docRef, UseDefaultIID());
	if (pageList == nil)
		return false;

	// bUseIntegerStyle = kFalse, so the page is spelled the way the Pages panel spells it - in the
	// style its SECTION uses (iv, A-1) - instead of being forced to arabic numerals. The default is
	// kTrue (IPageList.h:136: "kTrue == Use arabic numerals in string; kFalse == use the style of
	// this section (eg iv for 4)"), so taking the default printed "4" for a page the panel calls
	// "iv". Sorting is unaffected: that uses outPageIndex, which is the plain document order.
	pageList->GetPageString(pageUID, &outPage, kTrue /*bIncludeSectionName*/, kFalse /*bUseIntegerStyle*/);
	outPage.SetTranslatable(kFalse);
	outPageIndex = pageList->GetPageIndex(pageUID);
	return !outPage.IsEmpty();
}

// Is this frame switched off? Such a match is composed and has a page like any other - only its
// drawing is suppressed - so it can be listed and jumped to; the row just has to say so, the way the
// Find/Change dialog says "Hidden Item".
//
// TWO independent switches, and both have to be asked:
//   * its LAYER is hidden. The layer an item sits on is a SPREAD layer (one per spread); the
//     visibility switch lives on the DOCUMENT layer it points at, which is the row in the Layers
//     panel.
//   * the ITEM ITSELF is hidden (Object > Hide), which no layer says anything about.
//
// Without the second, a match inside an individually hidden frame comes up with no "hidden" mark at
// all. Adobe asks both: spellpanel's DetermineIfTextIsHidden does the layer
// test and then IPageItemVisibilityFacade::IsHidden, and states in its own comment that it is the
// same code as FindChangeClient.cpp - i.e. this is what the Find/Change dialog itself reports.
//
// The layer is found the one way both tests below need it: the frame's SPREAD layer (one per
// spread) points at the DOCUMENT layer that carries the switches - the Layers panel's row. nil when
// any step cannot be resolved.
IDocumentLayer* QueryDocLayerOf(IDataBase* db, IHierarchy* frameHier)
{
	const UID spreadLayerUID = Utils<ILayerUtils>()->GetLayerUID(frameHier);
	if (spreadLayerUID == kInvalidUID)
		return nil;
	InterfacePtr<ISpreadLayer> spreadLayer(db, spreadLayerUID, UseDefaultIID());
	if (spreadLayer == nil)
		return nil;
	InterfacePtr<IDocumentLayer> docLayer(db, spreadLayer->GetDocLayerUID(), UseDefaultIID());
	return docLayer.forget();
}

bool IsFrameHidden(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;

	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, frameHier));
	if (docLayer != nil && !docLayer->IsVisible())
		return true;

	// The item's own switch. Asked second because it is the rarer of the two, and asked even when the
	// layer could not be resolved - "cannot tell about the layer" is not an answer about the item.
	return Utils<Facade::IPageItemVisibilityFacade>()->IsHidden(UIDRef(db, frameUID)) != kFalse;
}

// Is this frame on a LOCKED layer? Same two-step as the hidden test above (spread layer -> the
// document layer that carries the switch), asked so the replace can leave such a match alone.
// A frame that resolves to no layer reads as unlocked - see IsFrameEditable in the header on why
// "cannot tell" must not turn into a refusal.
bool IsFrameOnLockedLayer(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;

	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, frameHier));
	return docLayer != nil && docLayer->IsLocked();
}

// Do this page item's own lock flags refuse an edit? A page item can carry TWO, independently
// (kSplineItemBoss holds both, per a live object-model dump):
//
//   ILockPosition::IsPageItemLocked - Object > Lock (Ctrl+L), the one users reach for. selecting =
//       kFalse asks for the lock itself, not "would a click be refused"; the Prevent Selecting
//       Locked Items preference has no bearing on whether text may be rewritten.
//   IItemLockData::GetInsertLock    - "the content cannot be edited", the insert lock InCopy sets
//       on a managed frame. ILockPosition folds this in for managed frames (ILockPosition.h:53-56)
//       but it is asked outright as well, so the answer does not depend on that folding.
//
// EACH IS ASKED AT TWO LEVELS. The UID this gets is IParcelList::GetParcelFrameUID - the item the
// text is composed INTO, which is not the page item the lock lives on. Asking it alone found
// nothing at all (measured on the running application: locking a text frame left its hits fully
// selectable, while locking the LAYER worked). Nothing had depended on the distinction before,
// because the two existing users of this UID - GetOwnerPageUID and ILayerUtils::GetLayerUID - both
// climb the hierarchy themselves. Adobe climbs for lock interfaces too (CGraphicPlaceBehavior uses
// QueryOutermostParentFor with IID_IITEMLOCKDATA). Self first, then the outermost ancestor, so a
// frame locked on its own and a frame inside a locked GROUP both answer.
bool IsPageItemLockedForEdit(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<ILockPosition> lockPos(db, frameUID, UseDefaultIID());
	if (lockPos != nil && lockPos->IsPageItemLocked(kFalse))
		return true;
	InterfacePtr<IItemLockData> lockData(db, frameUID, UseDefaultIID());
	if (lockData != nil && lockData->GetInsertLock())
		return true;

	InterfacePtr<IHierarchy> hier(db, frameUID, UseDefaultIID());
	if (hier == nil)
		return false;

	// Two separate climbs: QueryOutermostParentFor finds the outermost ancestor supporting THAT
	// interface, and the two need not land on the same item.
	InterfacePtr<ILockPosition> outerLockPos(static_cast<ILockPosition*>(
		Utils<ILayoutUtils>()->QueryOutermostParentFor(hier, IID_ILOCKPOSITION)));
	if (outerLockPos != nil && outerLockPos->IsPageItemLocked(kFalse))
		return true;

	InterfacePtr<IItemLockData> outerLockData(static_cast<IItemLockData*>(
		Utils<ILayoutUtils>()->QueryOutermostParentFor(hier, IID_IITEMLOCKDATA)));
	if (outerLockData != nil && outerLockData->GetInsertLock())
		return true;

	return false;
}

// The frame a text position is composed into: position -> parcel -> frame. kInvalidUID for an
// overset position (composed but placed in no frame) and for the query failures around it, which
// read the same to every caller: this position has no frame of its own.
UID FrameUIDForPosition(const UIDRef& storyRef, TextIndex pos)
{
	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return kInvalidUID;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kInvalidUID;
	const ParcelKey key = tpl->GetParcelContaining(pos);
	if (!key.IsValid())
		return kInvalidUID;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return kInvalidUID;
	return pl->GetParcelFrameUID(key);
}

// Everything about a hit that its FRAME decides rather than its position: the page the frame sits
// on, whether that frame's layer is switched off, and whether its text may be rewritten.
//
// Each of the three walks a structure of its own - the page climbs to the spread and formats a
// section-aware number, and the lock question climbs the page-item hierarchy twice - while a text
// frame usually holds many of a search's matches. So they are resolved ONCE PER FRAME and kept for
// the length of one document's walk. Nothing here can change while that walk runs: it is read-only,
// inside a SaveRestoreModifiedState dirty guard, and processes no commands but the finder's.
struct FrameFacts
{
	PMString	pageString;
	int32		pageIndex;
	bool		hasPage;	// false for a frame on no page, and for "no frame at all"
	bool		isHidden;
	bool		isLocked;

	FrameFacts() : pageIndex(-1), hasPage(false), isHidden(false), isLocked(false) {}
};

// Keyed by story as well as frame. kInvalidUID stands for "no frame", which matches in two
// different stories can both produce, and the answer for it is the STORY's own insert lock.
typedef std::map<std::pair<UID, UID>, FrameFacts> FrameFactsCache;

const FrameFacts& LookUpFrame(const UIDRef& docRef, const UIDRef& storyRef, UID frameUID,
	FrameFactsCache& cache)
{
	const std::pair<UID, UID> key(storyRef.GetUID(), frameUID);
	const FrameFactsCache::const_iterator known = cache.find(key);
	if (known != cache.end())
		return known->second;

	FrameFacts facts;
	if (frameUID != kInvalidUID)
		facts.hasPage = GetFramePageString(docRef, frameUID, facts.pageString, facts.pageIndex);
	if (!facts.hasPage)
	{
		facts.pageString.Clear();
		facts.pageIndex = -1;
	}
	facts.pageString.SetTranslatable(kFalse);
	facts.isHidden = IsFrameHidden(docRef.GetDataBase(), frameUID);
	facts.isLocked = !KFCSearchEngine::IsFrameEditable(storyRef, frameUID);
	return cache.insert(std::make_pair(key, facts)).first->second;
}

// What one document's walk learns once and every hit after it reuses: the frames' answers above, and
// each story's first words (StoryLeadText). Scoped to the walk, so it can never outlive the state it
// describes. (A story's words are the same for every hit in it and only the story's row reads them -
// read per hit, they would cost two hundred characters a hit.)
struct WalkCache
{
	FrameFactsCache				frames;
	std::map<UID, PMString>		storyLeads;
};

//------------------------------------------------------------------------------------
// WHAT A HIT NEEDS READ OUT OF THE STORY'S TEXT - the three drawn segments, and the hash of the
// whole match. Every hit wants both, one straight after the other, so they are taken TOGETHER:
// one ITextModel / IComposeScanner pair for the pair of them, and the matched characters copied
// ONCE.
//
// The doors further down serve a caller holding a single range - RereadRowText (a row that
// has moved) goes through ReadHitText itself, HashMatchText (the same-occurrence test) through the
// same hash - so what the search stored and what those read can never be computed differently.
//------------------------------------------------------------------------------------

// The most characters a hit row's three segments carry BETWEEN THEM - leading context, match and
// trailing context, as ONE line budget - and the one place that limit is applied.
//
// Everything the segments exist for is a single drawn row, so what is kept is sized for reading,
// not for the paragraph it came from. Running them to their paragraph boundaries is not safe: a
// paragraph is only "one line" until somebody pastes text with no breaks in it - a 10,000-character
// paragraph searched for one character stores paragraph-times-hits characters, hundreds of
// megabytes inside the hit ceiling, every one of which the colour cell also MEASURES on every
// repaint.
//
// FIFTY, TOTAL, MATCH FIRST - the author's numbers. The match takes what
// it needs up to the whole budget; what is left is split evenly between the two contexts, and a
// side with less to say than its half hands the remainder to the other side. Every cut end is
// marked with an ellipsis (kTextChar_Ellipse), so a line that was CUT is never mistaken for a
// line that ENDS. The arithmetic lives in SplitLineWithScanner and nowhere else.
//
// NOT THE MATCH'S OWN TEST. The same-occurrence test reads none of the three segments - it compares the
// match WHOLE, through a hash taken with no cap at all (HashMatchText) - so a row clipped for drawing
// can never cost a replace. (RowReadsAsFound compares the segments too, as the line around the match -
// read again by this same budget, so like is compared with like.)
const int32 kKFCMaxLineChars = 50;

// (The cut mark is the SDK's kTextChar_Ellipse, TextChar.h:188 - what the product uses for this very
//  job: ConditionalTextTips.cpp:117-126 reads a story through IComposeScanner::CopyText, stops at a
//  character limit, and appends kTextChar_Ellipse if it stopped early. A named constant also keeps the
//  file ASCII: a bare UTF-8 symbol in it is garbled to CP932 noise.)

// Where the drawn match stops. Capped at the SAME number the whole line is budgeted with: a match
// that long owns the entire line, and the contexts' arithmetic in SplitLineWithScanner then comes
// out at zero by itself.
TextIndex KFCCapMatchEnd(TextIndex start, TextIndex end)
{
	if (end < start)
		return start;
	if (end - start > kKFCMaxLineChars)
		return start + kKFCMaxLineChars;
	return end;
}

// FNV-1a, 64-bit. Taken over the UTF-32 value of each character, byte by byte, so a character's
// high bits count as much as its low ones.
//
// 64-bit, not 32: a collision here means "the text changed and we replaced it anyway", which is the
// one direction this plug-in must not fail in.
const uint64 kKFCHashOffsetBasis = 14695981039346656037ULL;
const uint64 kKFCHashPrime = 1099511628211ULL;

void HashAccumulate(uint64& ioHash, const WideString& text, int32 count)
{
	for (int32 i = 0; i < count; ++i)
	{
		const uint32 ch = static_cast<uint32>(text.GetChar(i).GetValue());
		for (int32 b = 0; b < 4; ++b)
		{
			ioHash ^= static_cast<uint64>((ch >> (b * 8)) & 0xFF);
			ioHash *= kKFCHashPrime;
		}
	}
}

// ...over text somebody has ALREADY read. The caller has to have the whole match in hand - a
// partial read hashes to a number that means nothing (see HashRangeWithScanner's short-read test).
uint64 HashOfWideString(const WideString& text)
{
	uint64 hash = kKFCHashOffsetBasis;
	HashAccumulate(hash, text, text.CharCount());
	// 0 is the "could not read" answer, so a real hash must never be 0.
	return (hash != 0) ? hash : 1;
}

// The line around [start, end) in its three drawn segments - see the contract on
// KFCSearchEngine::RereadRowText, whose segments this makes (and the search's own, through ReadHitText).
//
// outMatchWide hands the matched characters back as they were read, so a caller that also wants
// them hashed does not have to copy them out of the story a second time. It is the CAPPED match,
// and NEVER carries the cut mark (the mark goes on the drawn PMString alone - a mark in here
// would hash a character the document does not hold): equal to (end - start) characters only
// when the match fitted inside kKFCMaxLineChars, which is exactly the test a caller has to make
// before hashing it.
//
// A nil scanner is allowed and answers like an unreadable position: all three segments empty.
void SplitLineWithScanner(IComposeScanner* scanner, TextIndex start, TextIndex end,
	PMString& outPre, PMString& outMatch, PMString& outPost, WideString& outMatchWide)
{
	outPre.Clear();		outPre.SetTranslatable(kFalse);
	outMatch.Clear();	outMatch.SetTranslatable(kFalse);
	outPost.Clear();	outPost.SetTranslatable(kFalse);
	outMatchWide.Clear();

	if (scanner == nil)
		return;

	// The leading context comes from the paragraph the match STARTS in. NOTE what excludeEOS does
	// and does not do (IComposeScanner.h:88-94): it decides whether the STORY END counts when a
	// paragraph ends at the end of the story with no CR - it does NOT trim a paragraph's own CR.
	// A CR-terminated paragraph's length includes its CR, so the trailing segment below carries it
	// and the row draws a pilcrow at the line's end. Deliberately left that way: the search and the
	// replace's row rebuild both come through here, so the two can never disagree about it.
	// (A post cut by the line budget loses that CR along with the rest of its tail - the cut mark
	// stands where it ended.)
	int32 paraLen = 0;
	const TextIndex paraStart = scanner->FindSurroundingParagraph(start, &paraLen);
	if (paraStart < 0 || paraLen <= 0)
		return;

	// *The match itself is taken WHOLE, not cut at the end of that paragraph.
	// A single match can run over any number of paragraphs - a format-only search matches every
	// unbroken run of text carrying the format, and a GREP can be written to cross a break on
	// purpose. Cutting it here would make the row show ONE paragraph of what a replace rewrites in
	// full, so a user who ticked that row would lose text that was never on screen (measured: two
	// paragraphs and the break between them replaced by one word, joining what was left to the
	// paragraph below). The breaks inside the match are drawn as marks - a pilcrow for CR, a return
	// arrow for a forced line break (KFCResultModel::MarkUpBreaksForDisplay).
	const TextIndex matchEnd = KFCCapMatchEnd(start, end);

	// ----- the match first: it owns the line budget -----
	if (matchEnd > start)
	{
		// Read into the caller's buffer, so the hash can be taken from these very characters.
		scanner->CopyText(start, static_cast<int32>(matchEnd - start), &outMatchWide);
		outMatch = PMString(outMatchWide);
		outMatch.SetTranslatable(kFalse);
	}
	// A capped match was CUT, and the row says so. The mark goes on the drawn string ONLY, never
	// into outMatchWide - that buffer is the hash shortcut's material and has to stay exactly the
	// characters read. It is drawn in the match's own colour, which is the truth being told:
	// what follows the mark is more MATCH.
	if (matchEnd != end)
		outMatch.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));

	// ----- then the two contexts, out of what the match left -----
	// How much text each side HAS is measured before the budget is dealt out, so a side with less
	// to say than its half can hand the remainder to the other side - a match at the head of its
	// paragraph still fills the line to the right, instead of spending half the budget on a
	// context one character long.
	const int32 preAvail = (start > paraStart) ? static_cast<int32>(start - paraStart) : 0;

	// The trailing context comes from the paragraph the match ENDS in, which is a DIFFERENT
	// paragraph from the one it started in once the match spans a break. Probed at matchEnd - 1 so
	// a match ending exactly on a paragraph terminator answers with the paragraph it ended, not the
	// one after it.
	//
	// !NONE when the match was capped. Past the cap, what follows matchEnd is more
	// of the MATCH - and this segment is drawn in the normal colour, so writing it here would show
	// the rest of the match as though it were text lying outside it. (The budget arithmetic below
	// comes out at zero for a capped match anyway - KFCCapMatchEnd caps at the very number the
	// line is budgeted with - but the reason is kept apart from the arithmetic: it is about what
	// a post MEANS, not about how much room is left.)
	int32 postAvail = 0;
	if (matchEnd == end)
	{
		int32 endParaLen = 0;
		const TextIndex probe = (matchEnd > start) ? matchEnd - 1 : start;
		const TextIndex endParaStart = scanner->FindSurroundingParagraph(probe, &endParaLen);
		if (endParaStart >= 0 && endParaLen > 0 && endParaStart + endParaLen > matchEnd)
			postAvail = static_cast<int32>(endParaStart + endParaLen - matchEnd);
	}

	int32 remaining = kKFCMaxLineChars - static_cast<int32>(matchEnd - start);
	if (remaining < 0)
		remaining = 0;	// unreachable while KFCCapMatchEnd caps at kKFCMaxLineChars; kept so the two cannot come apart in silence
	int32 preBudget = remaining / 2;
	int32 postBudget = remaining - preBudget;
	if (preAvail < preBudget)
	{
		postBudget += preBudget - preAvail;
		preBudget = preAvail;
	}
	if (postAvail < postBudget)
	{
		preBudget += postBudget - postAvail;
		postBudget = postAvail;
		if (preBudget > preAvail)
			preBudget = preAvail;
	}

	if (preBudget > 0)
	{
		// THE TAIL IS KEPT, NOT THE HEAD. What survives is the text nearest the match,
		// which is the end the reader actually uses - and the end the cell keeps anyway, because an
		// overflowing pre is ellipsized from the beginning (kEllipsizeBeginning in KFCColorTextView).
		// Safe to cut at an arbitrary index: a TextIndex counts code points, so the cut cannot land
		// inside a surrogate pair (the note at the head of this file).
		const TextIndex preFrom = start - preBudget;
		WideString w;
		scanner->CopyText(preFrom, preBudget, &w);
		if (preFrom > paraStart)
			outPre.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));	// cut at its head, marked at its head
		outPre.Append(PMString(w));
		outPre.SetTranslatable(kFalse);
	}

	if (postBudget > 0)
	{
		// The HEAD is kept here - the mirror of the pre above, for the mirrored reason.
		WideString p;
		scanner->CopyText(matchEnd, postBudget, &p);
		outPost.Append(PMString(p));
		if (postBudget < postAvail)
			outPost.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));	// cut at its tail, marked at its tail
		outPost.SetTranslatable(kFalse);
	}
}

// The whole of [start, end) as one number - see HashMatchText, which this implements. A nil scanner
// answers 0, like any other text that could not be read.
uint64 HashRangeWithScanner(IComposeScanner* scanner, TextIndex start, TextIndex end)
{
	if (scanner == nil || end <= start)
		return 0;

	// NO CAP HERE, and that is the whole point. The drawn segments stop at the
	// kKFCMaxLineChars budget because what they produce is held for the life of the result set;
	// this holds nothing but the 64 bits below, so the match is read in full however long it is.
	//
	// Read in blocks rather than in one call: a single CopyText of an enormous match would build
	// one WideString that size, and nothing here needs the whole match in memory at once.
	const int32 kBlock = 4096;
	uint64 hash = kKFCHashOffsetBasis;
	for (TextIndex at = start; at < end; at += kBlock)
	{
		const int32 want = (end - at > kBlock) ? kBlock : static_cast<int32>(end - at);
		WideString block;
		scanner->CopyText(at, want, &block);

		// A short read means the story ended before the range did - the text is not what the range
		// says it is, so refuse to vouch for it rather than hashing a fragment.
		if (block.CharCount() < want)
			return 0;

		HashAccumulate(hash, block, want);
	}

	// 0 is the "could not read" answer, so a real hash must never be 0.
	return (hash != 0) ? hash : 1;
}

// Both text reads for one hit, through one scanner.
//
// The hash is taken from the characters the split just read whenever those ARE the whole match,
// which is every match up to the line budget (kKFCMaxLineChars) - so the ordinary hit copies its
// text out of the story exactly once. A longer match falls back on reading itself in blocks: the
// drawn segment stopped at the cap, and the hash is the one that must cover every character.
//
// A story with no text model leaves the hit as it came: three empty segments and a hash of 0,
// which is what every caller already reads as "this position could not be read". A ZERO-WIDTH
// match (start == end) also stores 0 - MatchIsSameOccurrence accepts those on the length arm
// without ever consulting the hash, so the two meanings of 0 never collide.
void ReadHitText(const UIDRef& storyRef, TextIndex start, TextIndex end, KFCResultModel::Hit& outHit)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());

	WideString matchWide;
	SplitLineWithScanner(scanner, start, end, outHit.preText, outHit.matchText, outHit.postText,
		matchWide);

	const TextIndex matchLength = end - start;
	outHit.matchHash = (matchLength > 0 && matchWide.CharCount() == matchLength)
		? HashOfWideString(matchWide)
		: HashRangeWithScanner(scanner, start, end);
}

// The first words of a story, for its row in the tree: up to 24 characters
// that show, each run of white space read as one space, InDesign's own marker characters (a table's
// anchor, a footnote's reference, an anchored object, a zero-width mark...) left out, and the cut mark
// when the story goes on. Read from the first 200 characters, which is plenty for 24 that show.
//
// IN THE OFFICIAL TERMS. Read through
// IComposeScanner::CopyText, like every other read in this file (and codesnippets/
// SnpCreateCrossReference.cpp, which names stories from their text the same way). A marker is what
// InDesign itself counts as neither white space nor a character, UnicodeClass::IsIgnoredCharacter
// with the spell checker's set (kIgnoreSpellingIgnorable: zero-width marks, discretionary hyphens,
// page numbers and other computed text, table characters, inline graphics, special glyphs, variation
// selectors), plus the rest of the control range and the object placeholder. A gap is one of the
// three breaks InDesign keeps in the control range, or UnicodeClass::IsWhiteSpace (which KCM's story
// list, KCMStoryList.cpp, asks too) - with the two corrections the loop below names. The cut is
// kTextChar_Ellipse, as a hit row's is. (Not a hand-made table of code points: one dropped the whole
// private-use area, so a story opening with gaiji lost them from its row - the regression case's row
// read "a-b-c" with the gaiji gone.)
//
// THE BREAKS ARE KEPT (the author: "show the paragraph mark on the story rows, and the forced line
// break too" - the marks KCM's story list draws). A paragraph's end (CR) and a
// forced line break (LF) stay in the text as the characters they are, and the tree draws them as the
// pilcrow and the return arrow a hit row uses (KFCResultModel::MarkUpBreaksForDisplay, applied where the
// row's text is built). Each counts as one of the 24. Only a break BETWEEN visible characters is kept:
// the story's own last CR, and the empty paragraphs at its end, would only say "the story ends here" -
// which a hit row does not say either (its last paragraph draws no pilcrow) - and the breaks at its
// HEAD (an empty first paragraph, or the one a table or an anchored object stands in) would put a
// pilcrow before the first word, where the row's name is read; leading white space was never shown
// either. White space next to a break is not shown: the break is the separator, and what follows it is
// an indent.
//
// THE STORY'S BODY - ITS PRIMARY THREAD - AND NOTHING ELSE (the author: "the story row shows the text
// with the tracked changes in it - the text from before the change"). A text model holds more than the
// body: the text Track Changes keeps for a deletion (a thread of its own, kDeletedTextBoss), a table's
// cells, footnotes, notes - every thread after the body's last CR. Read from the whole text model, a
// short body is followed on its row by whatever comes next: "kittenkitten dog catcat" named a story
// whose page reads "kittenkitten dog" (the "catcat" was the deletion a replace left).
// ITextModel::GetPrimaryStoryThreadSpan is the body's length (ITextModel.h: "does not include any
// characters that are part of story threads for table cells").
// Only when the body has no WORDS - a frame holding a table and nothing else - are the other threads
// read, in order, and then never a deletion's (so the row still names the story by its cells: "<sign>
// cell text", as KCM's story list reads it). A table's anchor shows as its sign wherever it stands.
PMString StoryLeadText(const UIDRef& storyRef)
{
	PMString out;
	out.SetTranslatable(kFalse);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (model == nil || scanner == nil)
		return out;
	const int32 kReadAtMost = 200;		// plenty for 24 that show
	WideString lead;
	WideString pendingBreaks;	// breaks read since the last visible character - kept once one follows
	int32 shown = 0;			// what the row shows - the 24
	int32 words = 0;			// of those, the ones that are text: a table's sign is not (see below)
	bool pendingSpace = false;
	bool more = false;
	auto scan = [&](const WideString& raw)
	{
		for (int32 i = 0; i < raw.CharCount() && !more; ++i)
		{
			const UTF32TextChar c = raw.GetChar(i);
			const uint32 v = c.GetValue();
			// A TABLE'S ANCHOR IS SHOWN, AS ITS SIGN (the author: "like KCM, a table mark").
			// Kept as the character itself - the row's text is marked up where it is built, and
			// KFCResultModel::MarkUpBreaksForDisplay turns it into U+25A6, as it turns the breaks into their
			// marks. It is one of the 24 but not a WORD (KCM's rule, KCMStoryList.cpp): a body holding a
			// table and nothing else still goes on to the cells below. Its per-row continuations
			// (kTextChar_TableContinued) are dropped with the other control characters: one table, one sign.
			const bool isTableSign = (v == kTextChar_Table);
			// IN THIS ORDER, AND WITH TWO NAMED CHARACTERS - BOTH MEASURED (the story-lead-chars
			// regression case). The breaks first: IsIgnoredCharacter counts CR / LF
			// as markers too. Then the markers, BEFORE white space, because IsWhiteSpace answered TRUE for
			// the zero-width space (U+200B), which then showed as a space between two letters. Then white
			// space - with the ideographic space named, because IsWhiteSpace answered FALSE for it and a
			// Japanese paragraph's indent came through as a character at the head of the row.
			if (v == kTextChar_CR || v == kTextChar_LF)
			{
				if (shown > 0)
					pendingBreaks.Append(c);	// (before the first visible character: not shown - see above)
				pendingSpace = false;		// white space before a break is not shown
				continue;
			}
			if (v == kTextChar_Tab)
			{
				pendingSpace = (shown > 0 && pendingBreaks.CharCount() == 0);
				continue;
			}
			if (!isTableSign && (v < kTextChar_Space || v == kTextChar_ObjectReplacementCharacter
				|| UnicodeClass::IsIgnoredCharacter(c, UnicodeClass::kIgnoreSpellingIgnorable)))
				continue;
			if (UnicodeClass::IsWhiteSpace(c) || v == kTextChar_IdeographicSpace)
			{
				pendingSpace = (shown > 0 && pendingBreaks.CharCount() == 0);	// after a break: an indent
				continue;
			}
			// A visible character: the breaks before it go in first, each one of the 24.
			for (int32 b = 0; b < pendingBreaks.CharCount(); ++b)
			{
				if (shown >= 24)
				{
					more = true;
					break;
				}
				lead.Append(pendingBreaks.GetChar(b));
				++shown;
			}
			pendingBreaks.Clear();
			if (more)
				break;
			if (shown >= 24)
			{
				more = true;
				break;
			}
			if (pendingSpace)
			{
				lead.Append(UTF32TextChar(kTextChar_Space));
				pendingSpace = false;
			}
			lead.Append(c);
			++shown;
			if (!isTableSign)
				++words;
		}
	};
	// Where one thread ends and the next is read: its last break is the thread's own end, not a paragraph
	// mark anybody typed (the story's end, a cell's end) - so it is never shown, and the next thread's words
	// are set off by a gap, the one a tab leaves (KCM reads `<sign> c` the same way).
	auto endThread = [&]()
	{
		pendingBreaks.Clear();
		pendingSpace = (shown > 0);
	};

	// The body first.
	const int32 total = model->TotalLength();
	int32 body = model->GetPrimaryStoryThreadSpan();
	if (body > total)
		body = total;
	WideString raw;
	if (body > 0)
		scanner->CopyText(0, (body < kReadAtMost) ? body : kReadAtMost, &raw);
	scan(raw);
	endThread();

	// No words in it (a table alone - then its sign leads the row - or an anchored object alone): the
	// threads after it, in text order - a deletion's never (what the page does not show must not name the
	// story).
	int32 budget = kReadAtMost;
	for (TextIndex at = body; words == 0 && !more && at < total && budget > 0; )
	{
		TextIndex threadStart = kInvalidTextIndex;
		int32 threadLen = 0;
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, &threadLen));
		if (thread == nil || threadStart < 0 || threadLen <= 0 || threadStart + threadLen <= at)
			break;		// no thread to go on with, or one that would not move the walk on
		if (::GetClass(thread) != kDeletedTextBoss)
		{
			const int32 len = (threadLen < budget) ? threadLen : budget;
			WideString part;
			scanner->CopyText(threadStart, len, &part);
			scan(part);
			endThread();
			budget -= len;
		}
		at = threadStart + threadLen;
	}

	out = PMString(lead);
	if (more)
		out.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	out.SetTranslatable(kFalse);
	return out;
}

// How much of each match BuildHit fills in. A walk costs the same whatever is asked for; what differs
// is how much is then read about every match it lands on.
enum HitDetail
{
	kHitPlaceAndText,	// story, range, the three drawn segments and the hash - what finding a row
						// again compares (CollectStoryHits)
	kHitEverything		// ...and the page, the hidden / locked / footnote flags and the story's first
						// words - a search's row
};

// Fill a hit from one match (story, [start, end)), as far as 'detail' asks: its jump anchors and the
// containing paragraph's text split into (before / matched / after) with the hash of the whole match;
// then the page, the flags and the story's first words. The offsets are CODE POINTS, not UTF-16
// units - see the note at the head of this file.
void BuildHit(const UIDRef& docRef, const UIDRef& storyRef, TextIndex start, TextIndex end,
	HitDetail detail, WalkCache& cache, KFCResultModel::Hit& outHit)
{
	outHit.storyUID = storyRef.GetUID();
	outHit.textStart = start;
	outHit.textEnd = end;

	// The line's three drawn segments, and the whole match as one number for the same-occurrence
	// test the JUMP and the replace's row doors run. Both describe THIS match as the search found it,
	// and both come out of one reading of the story - see ReadHitText. The segments are capped for
	// drawing; the hash never is, because it is the one that gets compared.
	ReadHitText(storyRef, start, end, outHit);
	if (detail == kHitPlaceAndText)
		return;

	// The story's first words, for its row in the tree - read once per story per walk.
	std::map<UID, PMString>::const_iterator lead = cache.storyLeads.find(outHit.storyUID);
	if (lead == cache.storyLeads.end())
		lead = cache.storyLeads.insert(std::make_pair(outHit.storyUID, StoryLeadText(storyRef))).first;
	outHit.storyLead = lead->second;

	// UNDER A HIDDEN CONDITION: ASKED WHERE THE TEXT COMES BACK TO. Such text stands in a thread no frame
	// holds - asked as it stands, the row would read "P1(2) overset". It occurs on a list Show Changes
	// rebuilt from the records, the only kind that can hold one (a search does not walk hidden
	// conditional text). Its frame, page and overset test are those of the place the condition puts
	// the text back (KFCTrackChange::HiddenTextAnchor), and the row says why it shows nothing there.
	const TextIndex hiddenAnchor = KFCTrackChange::HiddenTextAnchor(storyRef, start);
	outHit.inHiddenText = (hiddenAnchor != kInvalidTextIndex);
	const TextIndex placeAt = outHit.inHiddenText ? hiddenAnchor : start;

	// The frame this match composes into. A POSITION question, so it is asked per hit; everything
	// that follows from the frame comes out of the cache.
	const UID matchFrameUID = FrameUIDForPosition(storyRef, placeAt);
	const FrameFacts* facts = &LookUpFrame(docRef, storyRef, matchFrameUID, cache.frames);

	// No page for the match itself (it is overset - composed but placed nowhere - or its frame sits
	// on no page). Name the page of the "+" overset indicator instead (the last placed parcel's
	// frame, climbing out of a pushed-out table) so the hit lists as "P<page>(n) overset" and sorts
	// into that page. If nothing is placed anywhere, leave it pageless: the locator falls back to a
	// bare "overset" and sorts to the end.
	//
	// The overset lookup itself is NOT cached: it climbs out of whatever table pushed the text out,
	// so two overset positions in one story can legitimately land on different frames.
	if (!facts->hasPage)
	{
		outHit.isOverset = true;
		const KFCOversetLoc loc = KFCFindOversetLocator(storyRef, placeAt);
		if (loc.found)
		{
			const FrameFacts& oversetFacts = LookUpFrame(docRef, storyRef, loc.frameUID, cache.frames);
			if (oversetFacts.hasPage)
			{
				// The FACTS are the whole of what the rest of this function reads - the page, the
				// hidden flag, the lock - so switching this pointer is the entire handover.
				facts = &oversetFacts;
			}
		}
	}

	outHit.pageString = facts->pageString;		// empty when the frame has no page
	outHit.pageString.SetTranslatable(kFalse);
	outHit.pageIndex = facts->pageIndex;		// -1 then, which sorts the hit to the end

	// A match on a switched-off layer is only reachable at all because the Find/Change dialog's
	// "Include Hidden Layers" is on. The row has to say so - the text is there and the jump works,
	// but nothing will be visible on arrival until the layer is switched back on.
	outHit.isHidden = facts->isHidden;

	// Locked content: found, listed, jumpable - and never replaceable, because InDesign gives no
	// way to change it. Decided HERE, once, so the row can be built without a check box instead of
	// offering one that would quietly do nothing.
	// (A locked row's box is kept off by KFCResultModel's RowHasCheckBox, the one door every tick goes
	// through - nothing to untick here: a hit starts unticked.)
	outHit.isLocked = facts->isLocked;
	// Inside a footnote? Such a row cannot be taken back - Track Changes records nothing there.
	outHit.inFootnote = KFCTrackChange::IsInFootnote(storyRef, start);
}

// Walk one document with the user's current Find/Change query and collect every match as a Hit.
// Read-only: the whole walk sits inside a SaveRestoreModifiedState dirty guard, so a windowless
// chapter can be closed afterwards without wanting a save. NOTHING is set on opts - the walk uses
// the user's Find/Change settings verbatim, so the search mode (Text or GREP) is followed.
//
// scopeOptions: the five switches every chapter of this run is walked with, read once by the caller
// (see SearchBook). They are the same for every chapter by definition - the replace pass re-walks
// with exactly these, or it meets other matches than the hits list - so reading them here would have
// been the Find/Change settings asked once per chapter for one answer.
//
// progressBar / progressBase: the run's bar and the point on it where this chapter starts.
// chapterSpan / storiesInDoc: how much of the bar this chapter owns, and how many stories to divide
// that slice between - asked by the caller once the chapter is open, since a chapter's size is not
// knowable before then. The walk moves the bar as it goes: a story at a time, each one subdivided
// by how far into its text the current match sits. nil is allowed for the bar.
//
// detail: how much of each hit is filled in (HitDetail) - everything for the search,
// less for a caller that re-walks a changed document only to find a row's place again. The walk is
// the same either way.
//
// onlyStory: walk this one story of the document instead of the whole of it (UIDRef::gNull = the whole
// document) - the scope the replace's walks take a story at a time.
//
// searchScope (the author: "KFC uses the official Find/Change's settings as they are, so follow the
// official way as far as it goes"): a Search: value, and the walk is the one
// Edit > Find/Change itself would walk for it, from the current selection: InDesign builds the scope
// (QueryWalkerScope_UsingSelections, called exactly as SnpFindAndReplace.cpp:774 calls it, with the dialog's
// five switches as spellpanel passes its options), so the range is the dialog's by construction rather than
// KFC's reading of it. docRef is not used then: the hits are filed by the document each one's story is in
// (outHitDBs, one per hit), and every open document gets the dirty guard. ! Such a walk STARTS at the
// selection and loops back (IWalkerScopeFactoryUtils.h:169-171), so SearchBook asks it only where the
// selection decides the range - To End of Story, Selection, and which story a Story search is in - and walks
// the rest from the top (a Document or All Documents search walked by such a scope would list its rows in
// the caret's order).
// kEmptyScope (the default) = docRef / onlyStory: a document, a book's chapter, or one story - from the top.
// outHitDBs is filled on either path.
void CollectHitsInDoc(const UIDRef& docRef, size_t maxHits, const WalkerScopeOptions& scopeOptions,
	HitDetail detail, std::vector<KFCResultModel::Hit>& outHits,
	bool& outCapped, ChapterWalkResult& outResult,
	KFCProgressBar* progressBar, int32 progressBase, int32 chapterSpan, int32 storiesInDoc,
	int32& ioProgressReported, const UIDRef& onlyStory = UIDRef::gNull,
	IWalkerScopeFactoryUtils::WalkScopeType searchScope = IWalkerScopeFactoryUtils::kEmptyScope,
	std::vector<IDataBase*>* outHitDBs = nil)
{
	outResult = kChapterWalked;
	const bool bySearchScope = (searchScope != IWalkerScopeFactoryUtils::kEmptyScope);

	// FIRST, before anything touches the database: every step below (SaveRestoreModifiedState,
	// QueryDocumentWalkerScope) takes it, and without this the whole function just falls through
	// its nil checks and returns an empty hit list, which the caller cannot tell apart from "no
	// matches here". (Not for a Search: walk, which takes no document - see searchScope above.)
	//
	// This is NOT a liveness test, despite what it looks like: a UIDRef carries the IDataBase*
	// itself, so a document closed underneath us leaves a dangling pointer here, not a nil one.
	// "Is this document still open?" only has one honest answer in KFC and it is
	// KFCBookScope::IsDocStillOpen. What this catches is a UIDRef that never had a database.
	IDataBase* chapterDB = docRef.GetDataBase();
	if (!bySearchScope && chapterDB == nil)
	{
		outResult = kChapterNoDatabase;
		return;
	}

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
	{
		outResult = kChapterNoOptions;
		return;
	}

	// The dirty guard (IDataBase.h: "an operation which is logically const on the database") - on the one
	// document, or on every open one for a Search: walk, which can compose in any of them. Held in a vector
	// for the second case only because the count is not known until here; each is the class as it stands.
	std::vector<std::unique_ptr<IDataBase::SaveRestoreModifiedState> > dirtyGuards;
	if (!bySearchScope)
		dirtyGuards.emplace_back(new IDataBase::SaveRestoreModifiedState(chapterDB));
	else
	{
		InterfacePtr<IDocumentList> docList(KFCBookScope::QueryOpenDocumentList());
		const int32 docCount = (docList != nil) ? docList->GetDocCount() : 0;
		for (int32 d = 0; d < docCount; ++d)
		{
			IDocument* const doc = docList->GetNthDoc(d);
			IDataBase* const db = (doc != nil) ? ::GetDataBase(doc) : nil;
			if (db != nil)
				dirtyGuards.emplace_back(new IDataBase::SaveRestoreModifiedState(db));
		}
	}

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
	{
		outResult = kChapterNoWalker;
		return;
	}
	InterfacePtr<IK2ServiceProvider> provider(registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss));
	if (provider == nil)
	{
		outResult = kChapterNoWalker;
		return;
	}

	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	if (walker == nil)
	{
		outResult = kChapterNoWalker;
		return;
	}

	// Always start a fresh walk from the top of the document.
	//
	// THE OPPOSITE OF WHAT THE SNIPPET DOES, AND ON PURPOSE. SnpFindAndReplace.cpp:772
	// initialises only when the walker is NOT walking, because it drives Find Next one key press at a
	// time and wants to carry on the walk it already has. This panel lists a whole document in one
	// run, so it always wants a walk of its own from the top.
	//
	// What that costs, stated because nothing else here states it: the walker comes from the service
	// provider, so it is SHARED with the Find/Change dialog. A search started from this panel
	// therefore throws away a Find Next sequence the user had going there, and their next click on
	// Find Next begins again from the top. Not measured - it follows from the two lines below and
	// from the guard the snippet puts around its own Initialize.
	if (walker->IsWalking())
		walker->Halt();

	// The single shared scope definition - the replace pass re-walks each chapter with exactly
	// these options, or it would meet other matches than the hits list. Handed in by the caller,
	// which reads them once for the whole run.
	//
	// A Search: walk takes the dialog's own scope - QueryWalkerScope_UsingSelections, the call
	// SnpFindAndReplace.cpp:774 makes with the Search: it has just read (see searchScope above).
	//
	// !! A BOOK'S CHAPTER (and one story) TAKE A FORM ADOBE CALLS NOWHERE. The dialog has no Search: for a
	// book, so there is no selection-based scope to ask for: a chapter is walked by the document form
	// IWalkerScopeFactoryUtils.h:102-109 documents for it (one story by the story form beside it) - what
	// stands behind those two is the header's contract, not a worked example. (Grepped: the only callers in
	// the SDK tree are ours - this file, KFCReplaceEngine and KESCL.)
	InterfacePtr<ITextWalkerScope> scope(bySearchScope
		? Utils<IWalkerScopeFactoryUtils>()->QueryWalkerScope_UsingSelections(searchScope, scopeOptions)
		: (onlyStory == UIDRef::gNull
			? Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions)
			: Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(onlyStory, scopeOptions)));
	if (scope == nil)
	{
		outResult = kChapterNoScope;
		return;
	}

	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (client == nil)
	{
		outResult = kChapterNoClient;
		return;
	}

	walker->Initialize(client, scope, opts, nil);

	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	if (selUtils == nil)
	{
		// THE ONE EXIT THAT IS PAST Initialize. Every refusal above this line is before
		// the walker was given anything to walk, so there is nothing to stop; this one is after, and
		// leaving a walker walking is what the Halt at the bottom of this function exists to prevent -
		// the next caller that guards its Initialize with IsWalking CONTINUES the walk left standing,
		// which is exactly what InDesign's own Find/Change does (SnpFindAndReplace.cpp:772).
		// The shape is Adobe's (SpellPreviousObserver.cpp:200-201: ask IsWalking, then Halt), and the
		// replace engine's matching exit does the same.
		if (walker->IsWalking())
			walker->Halt();
		// Named for what actually happened: there IS a walker (kChapterNoWalker would print "no text
		// walker"); what is missing is the interface the critical section is taken on.
		outResult = kChapterNoSelectionUtils;
		return;
	}

	// Required critical section around text-walker selection changes, HELD FOR THE WHOLE WALK.
	//
	// Adobe's own examples all wrap a SINGLE ProcessCommand instead (SnpFindAndReplace.cpp:788,
	// spellpanel's SpellSkipObserver.cpp:532-537 and SpellChangeAllObserver.cpp:317). That shape is
	// right for what they do - one Find Next per key press - and wrong here, because of what the
	// section actually contains: spellpanel says outright that SaveKeyboardEventHandler
	// (SpellCheckWalker.cpp:85-141) "is the same code as EnterWalkerSelections_CriticalSection", and
	// that code takes the keyboard focus away (RelinquishKeyFocus) and gives it back on the way out
	// (AcquireKeyFocus + SelectRange on an edit box). Entering and leaving it per match would run
	// that dance thousands of times in one search.
	//
	// What holding it may not keep out: the walk below moves the bar every few matches, inside this section,
	// and some call on the bar lets events in - which one is not measured (KFCAdvanceProgress). Cancel is
	// asked between chapters and when the walk moves on from one story to the next (the author's call - the
	// middle option between "between chapters only" and "at every move of the bar"; "safety first":
	// WasCancelled was taken to pump events, and UI work was not to run in the middle of the walk). Whether
	// moving the bar already lets the same events in, inside the section, is not known - nor whether
	// SetPosition pumps and WasCancelled only reads a flag. Do not "correct" this to the one-command shape
	// without measuring both.
	// (docs/ai-notes/kbs-book-and-search-api-audit-2026-07-31.md)
	//
	// THE REST OF THIS INTERFACE IS DELIBERATELY LEFT ALONE. It also carries
	// InitTextWalkerTerminator / TerminateTextWalkerTerminator, which spellpanel DOES call
	// (SpellPanelObserver.cpp:336, :349), and the snapshot pair beside them. Those bracket a walk
	// that OUTLIVES the call which started it - spellpanel's runs for as long as its panel is open,
	// across the user's clicks - and the terminator is what halts such a walk when the ground moves
	// under it. This walk begins and ends inside this function with a modal bar up throughout, so
	// there is no window in which anything could move. (Not an oversight - checked, so the next reader
	// does not have to work it out.)
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);

	// What each document's frames and stories answer about the hits inside them - see WalkCache. One per
	// document: a Search: walk of All Documents meets several, and UIDs mean nothing across them.
	std::map<IDataBase*, WalkCache> walkCaches;

	// (No per-story change count is stamped on the hits to let the replace skip its same-occurrence test
	// for a story nobody edited: such a fast path skips the POSITION test as well, which lets a query
	// retyped between the search and the replace rewrite the wrong occurrences. See KFCReplaceEngine.cpp's
	// note on the SAME-OCCURRENCE TEST.)

	// Walk the whole document. Each ProcessCommand advances the walker to the next match ("find
	// next"), so we keep going until no more hits; the maxHits ceiling is the stop of last resort.
	// Where the bar stands within this chapter: how many stories the walk has finished, and how long
	// the one it is in now is. The walk visits a story at a time, so a change of story means the one
	// before it is done - whatever order the walker chose to take them in.
	int32 storiesDone = 0;
	UIDRef progressStory;		// with its database: an All Documents walk crosses documents
	int32 progressStoryLength = 0;

	while (true)
	{
		// A COMMAND THAT COULD NOT BE BUILT IS A FAILED WALK, NOT A FINISHED ONE - kChapterWalked here
		// would report a chapter as searched to the end, a statement about the user's DOCUMENT.
		// Adobe's loop takes the same view: SnpFindAndReplace.cpp:752-765 leaves its result at the
		// kFailure it was initialised to and breaks out. So does the replace engine's RunWalkerCmd
		// (KFCReplaceEngine.cpp) - it is the same loop, written twice; keep the two alike.
		//
		// kChapterWalkFailed rather than a "never started" reason, because either is possible here:
		// this is inside the loop, so the walk may already have collected matches. That is exactly
		// what the failed-walk sentence says - the hits are real, the rest was never looked at.
		InterfacePtr<ICommand> findCmd(CmdUtils::CreateCommand(kFindTextCmdBoss));
		if (findCmd == nil)
		{
			outResult = kChapterWalkFailed;
			break;
		}
		InterfacePtr<IFindChangeCmdData> cmdData(findCmd, UseDefaultIID());
		if (cmdData == nil)
		{
			outResult = kChapterWalkFailed;
			break;
		}
		cmdData->SetTextWalker(walker);

		// WHAT THE COMMAND ANSWERED, not merely whether it landed on something.
		// The contract is IFindChangeService.h:46-49, and it has four answers of which only the
		// first carries a range:
		//   kSuccess         a match, with its position
		//   kNotFound        no match - the walk is over
		//   kFoundCompleted  the walk is over AND matches came up along the way. Start and end are
		//                    kInvalidTextIndex, so there is nothing here to collect either
		//   kFailure         an ERROR. The walk did not finish, it BROKE OFF partway
		//
		// The last one is why this is not "did it find something, yes or no". A chapter whose walk
		// broke off has been searched as far as it got and no further, and letting that end the loop
		// silently says the rest of the chapter holds no matches - a statement about the user's
		// DOCUMENT, and not a true one. kChapterWalkFailed carries the difference out to the summary
		// (AppendSearchErrorNote), while the hits collected before the break are kept, because they
		// are real.
		//
		// The replace engine draws the same distinction from the same header (RunWalkerCmd in
		// KFCReplaceEngine.cpp). Official shape: SnpFindAndReplace.cpp:790-796 folds a failed
		// ProcessCommand into kFailure as well, and its caller (:642-670) turns kFailure - and only
		// kFailure - into a failure.
		if (!ProcessFindChangeCmd(findCmd))
		{
			outResult = kChapterWalkFailed;		// the end of THIS walk only - its error state is cleared
			break;
		}
		const IFindChangeService::FindChangeResult found = cmdData->GetFindChangeResult();
		if (found != IFindChangeService::kSuccess)
		{
			// Cleared on this path too. A command can report kSuccess and still leave an error
			// standing, and one left standing fails the commands that come after it - the replace
			// engine clears at both of these points for exactly that reason.
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			if (found == IFindChangeService::kFailure)
				outResult = kChapterWalkFailed;
			break;		// kNotFound / kFoundCompleted: no more matches, the walk is complete.
		}

		TextIndex start = kInvalidTextIndex;
		TextIndex end = kInvalidTextIndex;
		UIDRef story = cmdData->GetRange(start, end);

		// Move the bar. A story we have not seen before means the one before it is finished; within a
		// story, the position is how far into its text this match sits. Done inside the walker's critical
		// section; WasCancelled is asked when the walk moves on to another story (below - and between
		// chapters, see the note on the section above).
		if (progressBar != nil)
		{
			if (story != progressStory)
			{
				// CANCEL IS ASKED HERE, WHEN THE WALK MOVES ON TO ANOTHER STORY (the author's call: the middle
				// option). The replace stops a story at a time (its walks are a story each), and the search
				// does the same: a Cancel pressed in a large document is heard when the story being walked is
				// done, not once the whole document - or, for an All Documents walk, every open document - has
				// been walked and the hits are thrown away. ! The walk comes back to KFC only with a MATCH, so
				// "another story" means the next story that holds one. Whether WasCancelled itself lets events
				// in is not measured (KFCAdvanceProgress); the "safety first" call is in the note on the
				// critical section above. kFalse = no global error state.
				if (progressStory.GetDataBase() != nil && progressBar->WasCancelled(kFalse))
				{
					outResult = kChapterCancelled;
					break;
				}
				if (progressStory.GetDataBase() != nil)
					++storiesDone;
				progressStory = story;
				// The story is already loaded - the walk is standing in it - so this costs nothing.
				InterfacePtr<ITextModel> progressModel(story, UseDefaultIID());
				progressStoryLength = (progressModel != nil) ? progressModel->TotalLength() : 0;
			}

			// This chapter's slice, cut into one piece per story.
			int32 stepsPerStory = (storiesInDoc > 0) ? (chapterSpan / storiesInDoc) : chapterSpan;
			if (stepsPerStory < 1)
				stepsPerStory = 1;		// more stories than the slice has steps - crawl by ones

			// Divide first, so a long story cannot overflow the multiplication. A story shorter than
			// the number of steps counts as one whole step - it is over before it could be drawn.
			const int32 charsPerStep = progressStoryLength / stepsPerStory;
			int32 within = (charsPerStep > 0) ? (start / charsPerStep) : stepsPerStory;
			if (within > stepsPerStory)
				within = stepsPerStory;

			// Never past this chapter's own slice: with stepsPerStory rounded up (the < 1 guard)
			// the arithmetic can overshoot, and a bar that runs into the next chapter's slice
			// jumps backwards when that chapter starts.
			int32 position = progressBase + storiesDone * stepsPerStory + within;
			const int32 chapterEnd = progressBase + chapterSpan;
			if (position > chapterEnd)
				position = chapterEnd;

			// Moving the bar from inside the walk is what keeps the Cancel button answering at all -
			// see KFCAdvanceProgress.
			KFCAdvanceProgress(progressBar, ioProgressReported, position);
		}

		// Whole-search safety ceiling reached: stop collecting. More matches may exist, but the
		// result set is capped so a match-everywhere query cannot grow it without bound.
		if (outHits.size() >= maxHits)
		{
			outCapped = true;
			break;
		}

		// BUILT WHERE IT IS GOING TO LIVE. A Hit carries its texts as PMStrings, so filling a local one
		// and copying it in would cost a heap allocation per string per match. emplace_back hands back
		// the new element itself (C++17), and BuildHit never touches this vector, so the reference
		// cannot be invalidated under it.
		// The match's own document: docRef for a chapter, the story's for a Search: walk (searchScope).
		IDataBase* const storyDB = story.GetDataBase();
		const UIDRef hitDocRef = bySearchScope
			? UIDRef(storyDB, (storyDB != nil) ? storyDB->GetRootUID() : kInvalidUID) : docRef;
		KFCResultModel::Hit& hit = outHits.emplace_back();
		BuildHit(hitDocRef, story, start, end, detail, walkCaches[storyDB], hit);
		if (outHitDBs != nil)
			outHitDBs->push_back(storyDB);
	}

	if (walker->IsWalking())
		walker->Halt();
}

// Every match of the current query in one story of an open document, as the search's walk meets them
// there, with the given scope switches - each with its place, its line and its hash (kHitPlaceAndText):
// what RelocateStaleRow compares a row with. The story scope is the one the replace's walks take
// (IWalkerScopeFactoryUtils::QueryStoryWalkerScope). Read-only (the walk's own dirty guard). False = it
// could not be walked.
bool CollectStoryHits(const UIDRef& storyRef, const WalkerScopeOptions& scopeOptions,
	std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	IDataBase* const db = storyRef.GetDataBase();
	if (db == nil)
		return false;
	bool capped = false;
	ChapterWalkResult result = kChapterWalked;
	int32 reported = 0;
	CollectHitsInDoc(UIDRef(db, db->GetRootUID()), static_cast<size_t>(KFCResultModel::kKFCCollectHitLimit),
		scopeOptions, kHitPlaceAndText, outHits, capped, result, nil, 0, 0, 1, reported, storyRef);
	return result == kChapterWalked;
}

} // anonymous namespace

KFCSearchEngine::HitBuilder::HitBuilder() : fCache(new (std::nothrow) WalkCache)
{
}

KFCSearchEngine::HitBuilder::~HitBuilder()
{
	delete static_cast<WalkCache*>(fCache);
}

bool KFCSearchEngine::HitBuilder::Build(const UIDRef& docRef, const UIDRef& storyRef, TextIndex start, TextIndex end,
	KFCResultModel::Hit& outHit)
{
	if (fCache == nil)
		return false;
	BuildHit(docRef, storyRef, start, end, kHitEverything, *static_cast<WalkCache*>(fCache), outHit);
	return true;
}

// Put a chapter's hits in PAGE order and give each its "P<page>(<n>)" locator (the KESCL convention:
// page string, section-aware; a within-page ordinal in parens only when the page holds more than one
// match; "overset" for an overset match, which has no page). The locator is a field of its own, drawn
// ahead of the line in the normal colour. Pure string / index work - no recompose, so no dirty guard
// needed here.
void KFCSearchEngine::FinalizeHits(std::vector<KFCResultModel::Hit>& hits)
{
	// Page order, overset matches to the end (their pageIndex is -1). Stable, so hits on the
	// same page keep their document (walk) order.
	std::stable_sort(hits.begin(), hits.end(),
		[](const KFCResultModel::Hit& a, const KFCResultModel::Hit& b)
		{
			const int32 pa = (a.pageIndex < 0) ? kMaxInt32 : a.pageIndex;
			const int32 pb = (b.pageIndex < 0) ? kMaxInt32 : b.pageIndex;
			return pa < pb;
		});

	// The within-page ordinals and the locators, numbered the way a replace's report numbers what it
	// keeps - one definition for both.
	KFCResultModel::NumberHitsWithinPages(hits);
}

namespace
{
// The session's search direction for one tab, through the command the dialog's own radio button
// stands for (the silent one: no panel is told to redraw).
bool SetSessionSearchBackwards(bool16 backwards, IFindChangeOptions::SearchMode mode)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kSearchBackwardsSilentCmdBoss));
	InterfacePtr<IBoolData> value(cmd, UseDefaultIID());
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (cmd == nil || value == nil || modeData == nil)
		return false;
	value->Set(backwards);
	modeData->Set(static_cast<int32>(mode));
	return ProcessFindChangeCmd(cmd);
}

// Point the session's search (for the tab in force) the way wanted. True = it was turned, and has to be
// turned back for outMode; false = it already pointed that way (nothing to turn, nothing to put back),
// or the settings could not be read or set. The two direction scopes below are this, both ways round.
bool TurnSessionDirection(bool16 backwards, int32& outMode)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	outMode = static_cast<int32>(mode);
	if ((opts->GetSearchBackwards(mode) != kFalse) == (backwards != kFalse))
		return false;
	return SetSessionSearchBackwards(backwards, mode);
}
}	// anonymous namespace

KFCForwardSearchScope::KFCForwardSearchScope() : fRestore(false), fMode(0)
{
	fRestore = TurnSessionDirection(kFalse, fMode);
}

KFCForwardSearchScope::~KFCForwardSearchScope()
{
	if (fRestore)
		SetSessionSearchBackwards(kTrue, static_cast<IFindChangeOptions::SearchMode>(fMode));
}

KFCBackwardSearchScope::KFCBackwardSearchScope(bool wanted) : fRestore(false), fMode(0)
{
	if (wanted)
		fRestore = TurnSessionDirection(kTrue, fMode);
}

KFCBackwardSearchScope::~KFCBackwardSearchScope()
{
	if (fRestore)
		SetSessionSearchBackwards(kFalse, static_cast<IFindChangeOptions::SearchMode>(fMode));
}

void KFCAdvanceProgress(KFCProgressBar* bar, int32& ioReported, int32 target, bool force)
{
	if (bar == nil)
		return;
	const int32 delta = target - ioReported;
	if (delta <= 0)
		return;
	if (!force && delta < kKFCProgressReportStep)
		return;		// too small to be worth a call on the bar for

	// SetPosition, which takes the absolute position - hence no use for delta beyond the guard above.
	//
	// THIS IS THE ONLY CALL THAT MOVES ANY OF KFC'S BARS, AND IT IS SetPosition. DoTask belongs to
	// TaskProgressBar (ProgressBar.h:186), while every KFC bar is a RangeProgressBar, which does not
	// have it. Do not "restore" DoTask here; it would mean changing the bar's type and with it the
	// meaning of every position this file computes.
	//
	// It WAS measured against DoTask, when a run had become impossible to cancel: it was not the
	// culprit - the cancel works exactly the same through SetPosition. The fault was that nothing asked
	// WasCancelled after the LAST chapter (see the ask-once-more test in SearchBook and ReplaceChecked).
	//
	// Both forms are in the SDK, chosen by what the caller is counting: linksui advances a
	// TaskProgressBar with DoTask because it processes a list of files, while textimportfilter drives
	// a RangeProgressBar with SetPosition because it is measuring its way through a byte count -
	// and cancels from it perfectly well (TxtImpFilter.cpp:519-548: SetPosition every 32 reads,
	// WasCancelled every read). KFC measures its way through hits and stories, so this is the form
	// that fits.
	//
	// WHICH CALL TAKES THE CLICK ON CANCEL IS NOT MEASURED. Some call on the bar lets events in while it is
	// up - that is how a click on Cancel is taken at all, and why KFCRunGuard exists - but whether it is
	// this SetPosition, WasCancelled, or both, nothing has measured: KCM's notes say WasCancelled pumps
	// (KCMProgressBar.h), and KIDMCP's working cancel makes both calls. The headers say neither
	// (ProgressBar.h, IProgressBarManager.h). The nearest thing on record is a miss: a Cancel posted as KCM's
	// book compare ended was not taken by the one WasCancelled after its loop
	// (docs/ai-notes/kescm-book-comparison-stage3-2026-08-12.md section 4-2, under "could not be measured") -
	// read there as WasCancelled letting events in, an explanation of the miss rather than a measurement of
	// either call. What WAS measured: during a search, with the bar up and moved from inside the walk, a
	// 100 ms WM_TIMER was not delivered once (docs/ai-notes/progress-bar-and-dialog-automation.md)
	// - whatever lets events in, it is not a loop that delivers everything. "Pumps" elsewhere in KFC means
	// "a call on the bar lets events in".
	bar->SetPosition(target);
	ioReported = target;
}

void KFCSetChapterTask(KFCProgressBar& bar, const char* noun, size_t index, size_t count, const PMString& name)
{
	PMString taskLine;
	taskLine.SetTranslatable(kFalse);
	taskLine.Append(noun);
	taskLine.Append(" ");
	taskLine.AppendNumber(static_cast<int32>(index) + 1);
	taskLine.Append(" / ");
	taskLine.AppendNumber(static_cast<int32>(count));
	taskLine.Append(" - ");
	taskLine.Append(name);
	bar.SetTaskText(taskLine);
}

bool KFCSearchEngine::CommitSearchMode()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// The tabs this panel can walk with the TEXT walker. Object and Colour search by attribute
	// through walkers of their own (kObjectWalkerService / kColorSearchWalkerService) and return page
	// items rather than lines of text; SearchBook turns those away before reaching here, so stating
	// their mode would only mislead the engine.
	//
	// false rather than "nothing to do": nothing was stated, so a caller that walked anyway would
	// walk in whatever mode was committed last. Both callers turn these tabs away before they get
	// here, so this is the answer for a route that does not exist yet rather than for one that does.
	if (mode != IFindChangeOptions::kTextSearch
		&& mode != IFindChangeOptions::kGrepSearch
		&& mode != IFindChangeOptions::kGlyphSearch
		&& mode != IFindChangeOptions::kTransliterateSearch)
		return false;

	// Read the glyph BEFORE the mode is committed. Committing a mode is a declaration, and there is
	// no promise anywhere that it leaves that mode's other settings untouched - so take the value
	// while it is certainly still there and hand it back afterwards.
	const Text::GlyphID findGlyphID =
		(mode == IFindChangeOptions::kGlyphSearch) ? opts->GetFindGlyphID() : kInvalidGlyphID;

	// The second axis and the Transliterate tab's query, read before the commits for the same
	// reason as the glyph above.
	const IFindChangeOptions::ChangeMode changeMode = opts->GetChangeMode();
	const IFindChangeOptions::CharacterType findCharType = opts->GetFindCharacterType();

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindSearchModeCmdBoss));
	if (cmd == nil)
		return false;

	// TWO separate int fields on this boss, both kIntDataImpl behind different IIDs (checked against a
	// live object-model dump): the DEFAULT one is the value being set, and IID_IFINDCHANGEMODEDATA is
	// which mode's settings are being addressed. For this command they are the same mode - exactly how
	// SnpFindAndReplace passes it in all four of its find/replace entry points.
	InterfacePtr<IIntData> value(cmd, UseDefaultIID());
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (value == nil || modeData == nil)
		return false;
	value->Set(static_cast<int32>(mode));
	modeData->Set(static_cast<int32>(mode));

	// THE CALLER IS TOLD. The walk must NOT go ahead on a failure: it would run in whatever mode was
	// committed last, which is a tab the user is not looking at, and the results would then be filed
	// under the tab that IS on screen. The snippet this is modelled on stops too (SnpFindAndReplace.cpp:
	// 511-516 and :598-603, both on this very command).
	if (!ProcessFindChangeCmd(cmd))
		return false;

	// The CHANGE MODE - kChange, or kTransliterate for the CJK character-type conversion - stated
	// for every tab, at the value the dialog already holds. It is the axis SnpFindAndReplace's
	// header warns about ("you must first change the mode with this command"), and it is global:
	// left unstated, a Text-tab walk runs with whatever anybody committed last.
	if (!CommitFindChangeInt(kFindChangeModeCmdBoss, static_cast<int32>(changeMode), static_cast<int32>(mode)))
		return false;

	// Now the glyph, and for the same reason the mode needed committing at all: what the dialog
	// HOLDS is not what the engine WALKS BY. The Glyph tab issues kFindChangeGlyphIDCmdBoss when the
	// user picks a glyph, so a walk driven from outside the dialog has to issue it again - stating
	// the mode by itself left the engine in glyph mode with no glyph, which is why this panel found
	// nothing at all on a query the dialog handled perfectly well.
	//
	// Only the FIND side is stated here. The replace side is set where the replacement happens, so
	// a search can never leave a change glyph standing behind the user's back.
	if (mode == IFindChangeOptions::kGlyphSearch && !CommitGlyphID(findGlyphID, kTrue))
		return false;

	// ...and the Transliterate tab's query, which is a character type rather than a find string -
	// the glyph rule over again, find side only for the same reason.
	if (mode == IFindChangeOptions::kTransliterateSearch
		&& !CommitFindChangeInt(kFindCharacterTypeCmdBoss, static_cast<int32>(findCharType), static_cast<int32>(mode)))
		return false;

	return true;
}

bool KFCSearchEngine::CommitReplaceSide()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	if (mode == IFindChangeOptions::kGlyphSearch)
	{
		// An empty Change To box is NOT refused. It means "delete every match" - the same thing an
		// empty change string means on the Text tab - and that is what the Find/Change dialog does
		// with it. Refusing it would make the panel strictly less capable than the dialog it
		// delegates to (the user's report). What matters is that the empty box must never be left
		// UNSTATED; CommitGlyphID states it on this side for exactly that reason.
		//
		// AND WHETHER IT WAS STATED IS THE ANSWER THIS FUNCTION GIVES. The header promises "true
		// when it is safe to replace", and a change glyph that could not be committed is the one
		// case the whole mechanism was built to catch: the command would then write whatever was
		// committed last - a glyph the user never chose on this run and cannot see anywhere on
		// screen.
		return CommitGlyphID(opts->GetReplaceGlyphID(), kFalse);	// kFalse = the replace side
	}
	if (mode == IFindChangeOptions::kTransliterateSearch)
	{
		// The character type the conversion writes, stated at the dialog's own value - the change
		// glyph's rule over again, answered the same way.
		return CommitFindChangeInt(kReplaceCharacterTypeCmdBoss,
			static_cast<int32>(opts->GetReplaceCharacterType()), static_cast<int32>(mode));
	}
	// Text and GREP write a string, which the replace command carries itself: there is no
	// change-side value to state, so there is nothing that could have failed to state.
	return true;
}

// THE FIND FORMAT COMPARES ITSELF.
//
// Find Format is the paragraph style, character style, font, size, colour and the rest that the
// dialog's format pane sets, and on the Glyph tab it is also where the font family and style of the
// query itself are kept. Every one of them changes WHICH matches a walk returns, so a replace that
// re-walks under a changed one lines its stored rows up with other occurrences entirely.
//
// The list knows how to answer that: AttributeBossList::IsEqual is a deep compare over every
// attribute in both lists (AttributeBossList.h:179-182). So the search keeps a copy of the list it
// ran with, and the replace asks the copy.
//
// NOT A HAND-ROLLED FINGERPRINT. One built from each attribute - its class, plus whatever answers out
// of nine value-carrying interfaces (ITextAttrUID / Font / String / WideString / Int32 / Int16 /
// RealNumber / Boolean / ClassID) - takes an attribute answering none of the nine as its CLASS alone,
// so "this condition was added or removed" is seen while "same condition, DIFFERENT VALUE" is not.
// The other doors do not close that gap: a row's place and text say nothing of the format it was
// found by, so the verify walk (KFCReplaceEngine's ChapterMovedUnderRows) refuses only a ticked row
// that is no longer a match - a changed value that still matches the rows would let the run go ahead,
// in silence, under a query the panel was not searched with, which is what RefuseChangedQuery is there
// to refuse. There is no generic value READ, but there is a generic COMPARE. (The operators are private - AttributeBossList.h:245-252 - which is a normal C++ way of
// making callers say which comparison they mean: IsEqual, deep, beside Intersects and
// IntersectionContainsDifferences.)
//
// Adobe calls neither IsEqual nor IntersectionContainsDifferences anywhere in the SDK.
// The header's contract is all there is to go on. Measured instead: see the audit note for the two
// searches - identical but for one attribute's value - that this was checked with.
void KFCSearchEngine::RememberFindFormat()
{
	KFCSearchEngine::ForgetSearchedFindFormat();	// what is remembered below replaces this; a failure remembers nothing

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;

	// The database first, and on its own: it is the argument the call below takes, so it is tested
	// before it is handed over rather than afterwards (as HasFindQuery and BuildWalkSignature do).
	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil)
		return;			// nothing to remember - FindFormatHasChanged then says "cannot tell"

	const AttributeBossList* const attrs = opts->GetFindAttributeBossList(db, opts->GetSearchMode());
	if (attrs == nil)
		return;

	// A SHALLOW copy: the attributes' reference counts go up and the list itself is ours
	// (AttributeBossList.h:153-157). Held in a boost::shared_ptr, which is how the SDK's own callers
	// take the result - chmlfilter/CHMLFiltTextHelper.cpp:134 does exactly this before handing the
	// copy to a command.
	//
	// A copy rather than the pointer: what GetFindAttributeBossList returns is the LIVE list behind
	// the dialog, and the user is free to edit the format pane the moment the search returns.
	gSearchedFindAttrs.reset(attrs->Duplicate());
	gSearchedFindAttrDB = db;
}

bool KFCSearchEngine::FindFormatHasChanged()
{
	if (gSearchedFindAttrs == nil)
		return false;			// nothing remembered - cannot tell, so do not refuse

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;

	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil || db != gSearchedFindAttrDB)
		return false;			// a different attribute database: the UIDs inside do not mean the same thing

	const AttributeBossList* const attrs = opts->GetFindAttributeBossList(db, opts->GetSearchMode());
	if (attrs == nil)
		return false;

	return gSearchedFindAttrs->IsEqual(db, attrs) == kFalse;
}

void KFCSearchEngine::ForgetSearchedFindFormat()
{
	// The two fields are ONE fact, so they are dropped together and in one place: forgetting the list
	// but keeping the database would leave FindFormatHasChanged's "different database" guard comparing
	// against a database no list belongs to.
	gSearchedFindAttrs.reset();
	gSearchedFindAttrDB = nil;
}

void KFCSearchEngine::DropResults()
{
	KFCResultModel::Clear();
	KFCBookScope::ReleaseSearchedBook();	// the book the rows were searched in, and the chapters it holds
	KFCSearchEngine::ForgetSearchedFindFormat();
}

void KFCSearchEngine::BuildWalkSignature(PMString& outSignature)
{
	outSignature.Clear();
	outSignature.SetTranslatable(kFalse);

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;		// empty = "cannot tell" - see the header on why that must not read as "different"

	// The attribute database, taken and tested HERE rather than down where it is first used. The
	// three format questions at the end of this function all take it as their first argument, and
	// the header's contract is that a signature which cannot be read comes back EMPTY - so building
	// the switches into the string first and only then meeting a nil database would leave a
	// signature that LOOKS complete while saying nothing at all about Find Format. That is the one
	// thing that must not happen to a string whose whole job is to be compared: it would compare
	// equal to a run with a different format set.
	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil)
		return;

	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	outSignature.Append("m");
	outSignature.AppendNumber(static_cast<int32>(mode));

	// ----- WHAT is being looked for -----
	if (mode == IFindChangeOptions::kGlyphSearch)
	{
		// The Glyph tab has no find STRING: its query is a glyph id plus the font that id belongs to.
		// Only the ID is read here - the FONT is two attributes in the list below, and goes in with
		// the rest of them rather than being named twice.
		outSignature.Append(" g");
		outSignature.AppendNumber(static_cast<int32>(opts->GetFindGlyphID()));
	}
	else if (mode == IFindChangeOptions::kTransliterateSearch)
	{
		// The Transliterate tab's query is a character type, not a find string.
		outSignature.Append(" t");
		outSignature.AppendNumber(static_cast<int32>(opts->GetFindCharacterType()));
	}
	else
	{
		// Text and GREP keep SEPARATE find strings, so this is asked for the mode in force - the same
		// way HasFindQuery asks it.
		outSignature.Append(" q");
		outSignature.Append(opts->GetFindString(mode));
	}

	// The SECOND axis, kChange versus kTransliterate. The tab test upstream does not cover it: it
	// sits beside the tab, and a walk runs with whichever value was committed last - so results
	// searched under one value must not be replaced under the other.
	outSignature.Append(" cm");
	outSignature.AppendNumber(static_cast<int32>(opts->GetChangeMode()));

	// ----- the switches that decide WHICH occurrences of it come back -----
	// The four matching options first, then the five scope switches GetKFCWalkerScopeOptions reads.
	// Every one of them changes the match SET the replace's walks meet. (Kana and width sensitivity
	// are CJK-only in the dialog, but they are asked for unconditionally: an option that is not on
	// screen can still be set, and a signature that only covers what the current UI shows is a
	// signature with a hole in it.)
	const bool16 switches[] =
	{
		opts->GetCaseSensitive(mode),
		opts->GetEntireWord(mode),
		opts->GetKanaSensitive(mode),
		opts->GetWidthSensitive(mode),
		opts->GetIncludeMasterPages(mode),
		opts->GetIncludeLockedLayersForFind(mode),
		opts->GetIncludeHiddenLayers(mode),
		opts->GetIncludeLockedStoriesForFind(mode),
		opts->GetIncludeFootnotes(mode),
		// (No direction: KFC searches and replaces forward only - KFCForwardSearchScope - so the
		// dialog's direction changes nothing KFC does.)
	};
	outSignature.Append(" o");
	for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); ++i)
		outSignature.Append(switches[i] ? "1" : "0");

	// ----- and FIND FORMAT, which is a search condition like any other -----
	// COUNTED here, not described: how many conditions the dialog's format pane holds (and, on the
	// Glyph tab, the query's own font), so that one added or removed shows up in this string.
	//
	// WHAT they are set to is deliberately not fingerprinted here. That question belongs to the list
	// itself - RememberFindFormat / FindFormatHasChanged, which the replace asks alongside this
	// signature - and asking it there is what catches "same condition, different value".
	const AttributeBossList* const attrs = opts->GetFindAttributeBossList(db, mode);
	outSignature.Append(" n");
	outSignature.AppendNumber(attrs != nil ? attrs->CountBosses() : 0);

	// ...and the two conditions that list does NOT carry. A paragraph or character style set in the
	// format pane sits in a field of its own (GetFindParaStyle / GetFindCharStyle), so without these a query that
	// differs only by "Find Format = Style A" versus "Style B" would carry an IDENTICAL signature
	// and the door that refuses a changed query would let it straight through. The UIDs go in raw:
	// this string is compared, never read.
	outSignature.Append(" s");
	outSignature.AppendNumber(static_cast<int32>(opts->GetFindParaStyle(db, mode).Get()));
	outSignature.Append("/");
	outSignature.AppendNumber(static_cast<int32>(opts->GetFindCharStyle(db, mode).Get()));

	outSignature.SetTranslatable(kFalse);
}

void KFCSearchEngine::GetKFCWalkerScopeOptions(WalkerScopeOptions& outOptions)
{
	// The five switches WalkerScopeOptions carries are EXACTLY the five the Find/Change dialog
	// shows under its search options, so they are read from there like everything else about the
	// query - KFC sets no search option of its own.
	//
	// Forcing any of them would make the panel disagree with the dialog it delegates to (the user's
	// report: with Include Hidden Layers switched ON, a forced OFF still found nothing on a hidden
	// layer).
	//
	// fSearchBackwards is not set here, and that does NOT make the walk go forward: the walker is
	// handed the live options as well as this scope, and follows the options' direction (measured:
	// cat1 cat2 cat3 came back as cat3, cat2, cat1). What makes every KFC walk forward is
	// KFCForwardSearchScope, which the search, the replace and Redo each put round themselves.
	//
	// Two of the five are FIND-only in InDesign - "there is no option to change in locked stories /
	// on locked layers" (IFindChangeOptions.h:259, 279), which is why the dialog labels them Search
	// Only. They are still taken here, and still handed to the REPLACE walk, because both walks
	// have to meet the same matches or the rows stop describing what the replace meets. The
	// distinction is made where it belongs instead: the replace asks
	// EditableFrameForMatch / IsFrameEditable before it writes and leaves a locked match alone.
	//
	// (WalkerScopeOptions defaults every switch to kTrue, so with the settings unreadable, locked
	// layers and locked stories are inside both walks.)
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;		// nothing to read - the stock defaults stand

	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	outOptions.SetIncludeMasterPages(opts->GetIncludeMasterPages(mode));
	outOptions.SetIncludeLockedLayers(opts->GetIncludeLockedLayersForFind(mode));
	outOptions.SetIncludeHiddenLayers(opts->GetIncludeHiddenLayers(mode));
	outOptions.SetIncludeLockedStories(opts->GetIncludeLockedStoriesForFind(mode));
	outOptions.SetIncludeFootnotes(opts->GetIncludeFootnotes(mode));
}

// The frame is resolved exactly as BuildHit resolves it: the frame the match is composed into, or -
// for an overset match, composed but placed nowhere - the frame carrying the "+" indicator, which is
// the frame the hit's own locator already names. Resolving it the same way on both sides is what
// keeps "the row has no check box" and "the replace refuses" describing the same set of hits.
UID KFCSearchEngine::EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos)
{
	UID frameUID = FrameUIDForPosition(storyRef, pos);
	if (frameUID == kInvalidUID)
	{
		// Overset: composed but placed in no frame, so the frame that speaks for it is the one
		// showing the "+". Only reached for overset matches, so the locator's cost is not paid on
		// the ordinary path.
		const KFCOversetLoc loc = KFCFindOversetLocator(storyRef, pos);
		if (loc.found)
			frameUID = loc.frameUID;
	}
	return frameUID;
}

// Every lock InDesign has that bears on the question, asked in ONE place so the SEARCH (which marks a
// hit locked and withholds its check box) and the REPLACE (which refuses to write) can never disagree.
//
// A frame of kInvalidUID means "no page item to be locked" - not "cannot tell, refuse". See the
// header on why an unresolvable position has to read as editable.
bool KFCSearchEngine::IsFrameEditable(const UIDRef& storyRef, UID frameUID)
{
	// (1) The STORY's insert lock. This is the guard the SDK's own text replacers put in front of a
	// write - SpellReplaceWalker.cpp:435 and SpellWordObserver.cpp:258 both ask exactly this of the
	// ITextModel and give up with "can't change the model". IItemLockData sits on kTextStoryBoss
	// (verified against a live object-model dump). The default checkParent = kTrue is wanted: an
	// inline inside a locked story is locked too.
	InterfacePtr<IItemLockData> storyLock(storyRef, UseDefaultIID());
	if (storyLock != nil && storyLock->GetInsertLock())
		return false;

	if (frameUID == kInvalidUID)
		return true;

	IDataBase* db = storyRef.GetDataBase();

	// (2) The PAGE ITEM's own locks - Object > Lock, and the insert lock a managed frame carries.
	// InDesign itself draws no distinction between a locked object and a locked layer: its
	// Find/Change refuses both with one message, "The found object was locked or on a locked layer."
	// (measured on the running application). So neither does KFC.
	if (IsPageItemLockedForEdit(db, frameUID))
		return false;

	// (3) The LAYER the frame sits on.
	return !IsFrameOnLockedLayer(db, frameUID);
}

// NOT EditableFrameForMatch: that one climbs out to the frame carrying the "+" when a position is
// overset, which is exactly the case this has to answer TRUE for. The raw walk is the answer here.
bool KFCSearchEngine::IsPositionOverset(const UIDRef& storyRef, TextIndex pos)
{
	return FrameUIDForPosition(storyRef, pos) == kInvalidUID;
}

void KFCSearchEngine::RereadRowText(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef,
	TextIndex start, TextIndex end)
{
	// ReadHitText, the search's own reading - so a stored row and a rebuilt one are split and hashed
	// identically - into a scratch hit, whose four text fields are then the row's.
	KFCResultModel::Hit read;
	ReadHitText(storyRef, start, end, read);
	KFCResultModel::SetHitSegments(chapterIdx, hitIdx, read.preText, read.matchText, read.postText,
		read.matchHash);
}

namespace
{
// The whole of a match, boiled down to one 64-bit number - read WHOLE, however long the match is (the
// drawn segments are capped; this is the one that gets compared). A capped copy is not enough: a GREP
// match of 2000 judged on its first 500 lets a rewrite past that point through as "the same occurrence".
//
// One range, opened for this call alone. The arithmetic itself is HashRangeWithScanner's, the same FNV
// the search's own hits go through (ReadHitText), so a hash taken here and one taken at search time are
// the same number for the same text. That has to hold: comparing them IS the same-occurrence test.
// @return the hash, or 0 when the text could not be read at all (and for a zero-width range, which never
//         reaches the comparison - see MatchIsSameOccurrence).
uint64 HashMatchText(const UIDRef& storyRef, TextIndex start, TextIndex end)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	return HashRangeWithScanner(scanner, start, end);
}

// Is the match at [start, end) the SAME occurrence a stored hit describes? Four questions, none of which
// may answer no: the same story, the same position, the same LENGTH, the same text WHOLE (by the hash) -
// the last not asked of a zero-width match, which has no text to ask about. expectHash 0 means the search
// could not read that match, so nothing can be vouched for and the answer is false: when in doubt, do not
// write. Asked through RowReadsAsFound, which adds the line around the match.
bool MatchIsSameOccurrence(const UIDRef& storyRef, TextIndex start, TextIndex end,
	UID expectStoryUID, TextIndex expectStart, TextIndex expectEnd, uint64 expectHash)
{
	if (storyRef.GetUID() != expectStoryUID)
		return false;

	// (No offset to add: a row carries its own range past every change KFC makes, so what it asks about
	// is already where it stands.)
	if (start != expectStart)
		return false;

	// THE LENGTH. Compared directly: a match that has been rewritten in place keeps its start.
	if ((end - start) != (expectEnd - expectStart))
		return false;

	// A ZERO-WIDTH MATCH HAS NO TEXT TO COMPARE. GREP's ^ / $ / lookarounds match at a position, not over
	// characters, and the walker hands those through as start == end (measured: ^ returns one hit per
	// paragraph). Their stored hash is 0 - the same number HashRangeWithScanner answers for "could not
	// read" - so without this step the test below would call every one of them missing forever, on a
	// document nobody had touched. The story, start
	// and length arms have all agreed by now, and an empty range has nothing left to disagree about.
	if (start == end)
		return true;

	// THE TEXT, WHOLE. Not the drawn, capped text (see GetHitMatchIdentity): the
	// stored hash covers the entire match, so a rewrite anywhere inside it is caught however long
	// it is. A stored 0 means the search could not read that match - nothing to compare against,
	// so nothing is written.
	if (expectHash == 0)
		return false;
	return HashMatchText(storyRef, start, end) == expectHash;
}
}	// anonymous namespace

bool KFCSearchEngine::RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db)
{
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	KFCResultModel::RowDisplay row;
	if (db == nil || !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash)
		|| !KFCResultModel::GetHitRow(chapterIdx, hitIdx, row)
		|| story == kInvalidUID || !db->IsValidUID(story))
		return false;
	const UIDRef storyRef(db, story);
	// INSIDE THE STORY, before anything reads it. The line's reading asks the scanner for the
	// paragraph around `start` and does not bound the position itself (SplitLineWithScanner), and a stale
	// place can lie past a story that has since grown shorter. A place at the very end (after the story's
	// last return) has no paragraph around it and no match can stand there.
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return false;
	const TextIndex total = model->TotalLength();
	if (start < 0 || start >= total || end < start || end > total)
		return false;
	// the whole match, by its hash (a zero-width row passes here - the line below is its only test)
	if (!MatchIsSameOccurrence(storyRef, start, end, story, start, end, hash))
		return false;
	// ...and the line around it, read the way the search read it
	KFCResultModel::Hit read;
	ReadHitText(storyRef, start, end, read);
	return read.preText == row.preText && read.matchText == row.matchText && read.postText == row.postText;
}

bool KFCSearchEngine::ReadStoryVersion(IDataBase* db, UID story, uint32& outVersion)
{
	// IsValidUID first: a story an earlier replace deleted (an anchored frame's) must not be instantiated.
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return false;
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	if (model == nil)
		return false;
	outVersion = model->GetChangeCount();
	return true;
}

namespace
{
// The part of the document a Search: of Story / To End of Story / Selection searched, as the summary says
// it - the list is not the whole document then. Empty for the document itself.
const char* SelectionScopeWords(IWalkerScopeFactoryUtils::WalkScopeType selectionScope)
{
	switch (selectionScope)
	{
		case IWalkerScopeFactoryUtils::kStoryScope:			return " in the story";
		case IWalkerScopeFactoryUtils::kToEndOfStoryScope:	return " to the end of the story";
		case IWalkerScopeFactoryUtils::kSelectionScope:		return " in the selection";
		default:											return "";
	}
}
}	// anonymous namespace

int32 KFCSearchEngine::SearchBook(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// Last-resort re-entry stop. The panel's actions grey themselves out while a search runs, but
	// the progress bar pumps events, so a command could still find its way in here.
	if (gSearching)
	{
		outSummary.Append("A search is already running.");
		return 0;
	}
	// ...and the same door for anything ELSE of ours that is running - a replace.
	// Asked separately from the line above so each keeps the message that is actually true: what
	// makes a re-entrant call dangerous is two DIFFERENT runs, one of which hands back the chapters
	// the other is walking (see KFCRunGuard).
	if (KFCRunGuard::IsAnyRunning())
	{
		outSummary.Append(KFCRunGuard::BusyMessage());
		return 0;
	}
	const SearchingFlagGuard searchingGuard;

	// EVERY REFUSAL BELOW COMES BEFORE THE MODEL IS TOUCHED.
	// A run that is turned away has to leave the panel exactly as it found it - a Clear() up here would
	// let "No search text set on the Text tab." throw away the results of the search before it, a
	// command that did nothing but destroy the previous answer. Nothing between here and the commit
	// point writes to the model, the book scope, or the Find/Change settings.
	//
	// The commit point cannot sit lower than ListBookChapters, either: that records which book the run
	// is against (gSearchedBookPath) and ReleaseSearchedBook is what forgets it, so clearing AFTER the
	// book is resolved wipes the record the run has just made (measured). So the commit point sits just
	// above it, and the checks above the commit point.

	// Tabs that search by ATTRIBUTE rather than by text. InDesign walks those with a different walker
	// altogether (kObjectWalkerService / kColorSearchWalkerService), and what they find are page items,
	// not lines of text - so there is nothing for this panel to list, whatever it did with them.
	//
	// Named explicitly because the alternative is worse than useless: their find string IS empty, so
	// without this the panel would answer "No Find/Change text set." and send the user looking for a
	// field they had not left blank (the user's question).
	// THE MENU GREYS THE COMMAND ON THESE TABS (the author's call) through the same
	// question (CanSearchTab); this stays for a caller that never opened the menu - a script invoking
	// the action by its ID reaches here whatever the menu says.
	const int32 tab = KFCSearchEngine::CurrentSearchMode();
	if (!KFCSearchEngine::CanSearchTab(tab))
	{
		outSummary.Append("The Find/Change dialog is on the ");
		PMString tabName(KFCSearchEngine::TabName(tab));
		tabName.SetTranslatable(kFalse);
		outSummary.Append(tabName);
		// Short enough to be read whole at the panel's floor - this one composes a tab name into the
		// middle of it, so it is longer than it looks here (see KFCPanelMetrics on the four-line
		// budget, and what a message that outgrew it cost).
		outSummary.Append(" tab. This panel lists text - use InDesign's own Find/Change.");
		return 0;
	}

	// Transliterate - the CJK character-type conversion (Kanji / kana / half- and full-width) - is
	// walked like any other text tab (the author's call: whatever the official panel is set to, this
	// panel searches and replaces the same way). It rides the same text walker and the same
	// find/replace commands; what it needs is its axes stated before the walk - the ChangeMode and the
	// find character type - which CommitSearchMode does, the exact glyph rule again. (On a
	// Roman-featureset install the tab cannot be reached at all, so the transliterate paths simply lie
	// dormant there.)

	if (!HasFindQuery())
	{
		// Which tab, so this reads as "nothing set on THIS tab" - each one keeps its own query, so a
		// query on another tab is no help and saying so avoids a hunt.
		//
		// The HEAD names what is missing, one wording per tab. The TAIL says how to supply it, and
		// there it is CHOOSE against TYPE rather than a wording per tab: the Glyph and Transliterate
		// tabs both offer a thing to pick (a glyph from a grid, a character type from a dropdown)
		// where Text and GREP have a field to type into, and "type what to find" points at a field
		// neither of the first two has. Naming the thing twice - once in the head and again in the
		// tail - runs these past what the panel can draw (see KFCPanelMetrics for the four-line budget
		// and for what a message that outgrew it cost).
		const bool glyphTab = (tab == IFindChangeOptions::kGlyphSearch);
		const bool translitTab = (tab == IFindChangeOptions::kTransliterateSearch);
		outSummary.Append(glyphTab ? "No glyph set on the "
			: (translitTab ? "No character type set on the " : "No search text set on the "));
		PMString tabName(KFCSearchEngine::TabName(tab));
		tabName.SetTranslatable(kFalse);
		outSummary.Append(tabName);
		outSummary.Append((glyphTab || translitTab)
			? " tab. Choose one in Edit > Find/Change, then search again."
			: " tab. Type what to find in Edit > Find/Change, then search again.");
		return 0;
	}

	// Is there anything for the CURRENT scope to run on? Asked HERE, ahead of the commit point, so a
	// run with no target leaves the previous results on the panel. NO implicit fallback: ON means the
	// book and nothing else, OFF means what Search: names and nothing else - so the status line can
	// always state exactly what was searched, and a missing book is reported instead of quietly
	// searching one document behind the user's back.
	//
	// The same two questions KFCBookScope::HasScopeTarget asks for the menu's grey state; asked
	// separately here because each one has its own sentence to say.
	const bool fromBook = KFCBookScope::IsBookScopeOn();

	// Search: (the author's call: KFC follows Edit > Find/Change's Search:).
	// Document, All Documents, Story, To End of Story, Selection - with Book Scope OFF. Book Scope ON is
	// the whole book, which only Document can mean, so any other value is refused rather than quietly
	// read as Document ("the scope is never changed behind the user's back" - the rule the toggle has
	// always kept). An unset Search: reads as Document (CurrentSearchScope); a list of stories is
	// something only a script can set, and the dialog cannot show - refused too.
	const int32 searchScope = KFCSearchEngine::CurrentSearchScope();
	const char* const scopeName = KFCSearchEngine::SearchScopeName(searchScope);	// "" = none this panel follows
	// (asked of Search: as the selection makes it - what the dialog shows: Story with nothing selected IS
	//  Document there, so it is no reason to refuse a book)
	if (fromBook && KFCSearchEngine::SearchScopeForSelection(searchScope) != IWalkerScopeFactoryUtils::kDocumentScope)
	{
		outSummary.Append("Book Scope is on, and Search: is ");
		outSummary.Append(*scopeName != '\0' ? scopeName : "not Document");
		outSummary.Append(". Set Search: to Document in Edit > Find/Change, or turn Book Scope off.");
		return 0;
	}
	if (!fromBook && *scopeName == '\0')
	{
		outSummary.Append("Search: in Edit > Find/Change is set to something this panel cannot follow. Set it to Document, All Documents, Story, To End of Story or Selection.");
		return 0;
	}
	const bool allDocuments = !fromBook && searchScope == IWalkerScopeFactoryUtils::kAllDocumentScope;
	// Story / To End of Story / Selection start from the selection (and walk the active document).
	IWalkerScopeFactoryUtils::WalkScopeType selectionScope = (!fromBook
		&& (searchScope == IWalkerScopeFactoryUtils::kStoryScope || searchScope == IWalkerScopeFactoryUtils::kToEndOfStoryScope
			|| searchScope == IWalkerScopeFactoryUtils::kSelectionScope))
		? static_cast<IWalkerScopeFactoryUtils::WalkScopeType>(searchScope) : IWalkerScopeFactoryUtils::kDocumentScope;
	// ONE THE SELECTION DOES NOT OFFER IS SEARCHED AS DOCUMENT - AND SAID (the author's call: "the same as
	// InDesign"). The dialog offers Search: values by the selection (SearchScopeForSelection): with none that
	// fits - Story and nothing selected, Selection and only a caret - it shows Document and searches the
	// document (measured). So does this; the status line names what happened, so the scope is not changed out
	// of sight.
	PMString fellBackNote;
	fellBackNote.SetTranslatable(kFalse);
	if (selectionScope != IWalkerScopeFactoryUtils::kDocumentScope
		&& KFCSearchEngine::SearchScopeForSelection(selectionScope) != selectionScope)
	{
		fellBackNote.Append(" Search: is ");
		fellBackNote.Append(scopeName);
		fellBackNote.Append(selectionScope == IWalkerScopeFactoryUtils::kSelectionScope ? ", but no text is selected"
			: selectionScope == IWalkerScopeFactoryUtils::kToEndOfStoryScope ? ", but there is no text cursor"
			: ", but no text or text frame is selected");
		fellBackNote.Append(" - the whole document was searched, as Edit > Find/Change does.");
		selectionScope = IWalkerScopeFactoryUtils::kDocumentScope;
	}

	// The target book, asked once - and an EMPTY one refused here, ahead of the commit point like every
	// other refusal: refused past it (by ListBookChapters below), the previous results would already be
	// gone - a whole book's results, for a book with nothing in it (KFCBookScope::GetTargetBook says which
	// case was in mind; the regression case book-empty-keeps-results measures it).
	const KFCBookScope::TargetBook targetBook = fromBook ? KFCBookScope::GetTargetBook() : KFCBookScope::kNoTargetBook;
	if (fromBook && targetBook == KFCBookScope::kNoTargetBook)
	{
		outSummary.Append("Book Scope is on, but no book is open.");
		return 0;
	}
	if (fromBook && targetBook == KFCBookScope::kTargetBookEmpty)
	{
		outSummary.Append("That book has no chapters.");
		return 0;
	}
	if (!fromBook && KFCBookScope::ActiveDocument() == nil)
	{
		outSummary.Append("No open document to search.");
		return 0;
	}

	// Forward, whatever the dialog says - and put back as the function ends. Turned here, past every
	// refusal above, so a search that is turned away touches no setting at all, and still ahead of the
	// mode commit below, in the order the two always had.
	KFCForwardSearchScope forward;

	// State the tab before anything is walked. A walk runs in the mode last COMMITTED through
	// kFindSearchModeCmdBoss - not in the one IFindChangeOptions merely reports - so without this a
	// search driven from this panel ran as plain Text whatever tab was on screen. See
	// KFCSearchEngine::CommitSearchMode.
	//
	// ABOVE THE COMMIT POINT, AND ITS ANSWER IS READ. A tab that could not be stated is a refusal like
	// every other one in this function, so it has to leave the previous results standing - and it must
	// not search on in whatever mode had been committed last. Asking it this early changes nothing the
	// user can see: it writes back the value it has just read.
	if (!KFCSearchEngine::CommitSearchMode())
	{
		outSummary.Append("The Find/Change tab could not be set - nothing was searched. Try reopening Edit > Find/Change.");
		return 0;
	}

	// THE COMMIT POINT. Past this line the run owns the panel: the old results are gone whatever happens
	// next - and the book and the format they were found with go with them (the format is remembered again
	// a few lines below; DropResults is one rule with no exceptions to remember).
	//
	// THE CHAPTERS THE OLD RESULTS HELD GO NOW, NOT ON A SCHEDULE. DropResults hands them back with
	// kSchedule - it is called from notifications too - and a scheduled close runs only once this run is
	// over. Until then such a chapter is still open but no longer held, so the All Documents list below
	// would take it for a document of the user's: walked, listed, and its rows gone a moment after the
	// search when the close went through. Closed here, before anything is listed, in the context the book
	// loop below closes its own chapters in. A chapter with unsaved work, or one with a window, is not
	// closed - ReleaseHeldDoc's verdicts, unchanged. (Show Changes' commit point has the same two lines -
	// KFCShowChanges::Run.)
	KFCBookScope::ReleaseHeldDocs(true /*close now*/);
	KFCSearchEngine::DropResults();

	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	// Chapters the book could not hand over at all. Declared out here so the summary can name them
	// whichever way this run ends - including the "no matches" and "nothing openable" exits, where
	// they are the only thing that explains what happened.
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	if (fromBook)
	{
		// Listed, not opened: each chapter is opened when its turn comes in the loop below and
		// handed straight back once it has been walked, so a book search never holds more than one
		// chapter of its own. Whether a chapter can actually be opened is not known yet - the
		// summary reports the ones that could not, after the walk.
		if (!KFCBookScope::ListBookChapters(targets, bookName) || targets.empty())
		{
			// "That book", not "the active book": a run is against the book the BOOK PANEL is showing,
			// and only falls back to the active one when no panel can be reached
			// (KFCBookScope::ResolveTargetBook).
			// (An empty book is refused at the front door. This is the safety net - see the note at the
			// end of ListBookChapters - and the same sentence.)
			outSummary.Append("That book has no chapters.");
			return 0;
		}
	}
	else if (allDocuments)
	{
		// EVERY OPEN DOCUMENT, WINDOW OR NOT. InDesign's own All Documents searches a document opened
		// without a window too (measured: app.findText() counted one), so this does. Each is a "chapter"
		// of the run - its rows kept apart from the others' - but none is ours to open or close: no file
		// is recorded (a chapter with no file is found again by its docRef, as the Document search's
		// always has been), and a chapter KFC holds open from a BOOK search is left out - it is not a
		// document the user opened. Each is walked from the top by the document form, in this order (the
		// walk below - not by the dialog's own All Documents scope, which starts at the caret), and this
		// list is what the matches are filed under, and the T of "M of T document(s)".
		InterfacePtr<IDocumentList> docList(KFCBookScope::QueryOpenDocumentList());
		const int32 docCount = (docList != nil) ? docList->GetDocCount() : 0;
		for (int32 d = 0; d < docCount; ++d)
		{
			IDocument* doc = docList->GetNthDoc(d);
			if (doc == nil)
				continue;
			const KFCBookScope::ChapterDoc one = KFCBookScope::DocAsChapter(doc);
			if (!KFCBookScope::IsHeldDoc(one.docRef))
				targets.push_back(one);
		}
		if (targets.empty())
		{
			outSummary.Append("No open document to search.");
			return 0;
		}
	}
	else
	{
		// Re-read rather than carried down from the check above: a command has been processed since
		// (CommitSearchMode), and a pointer to the active document is not ours to assume survived it.
		// (Story / To End of Story / Selection walk this one too: the selection is the active document's.)
		IDocument* doc = KFCBookScope::ActiveDocument();
		if (doc == nil)
		{
			outSummary.Append("No open document to search.");
			return 0;
		}
		targets.push_back(KFCBookScope::DocAsChapter(doc));
	}

	// Record the scope ON THE RESULTS (KFCResultModel::Clear above wiped the previous value): the
	// tree reads it to decide whether the chapter rows come up collapsed, and reading it from here
	// rather than from the toggle keeps an existing result set's display stable if the user flips
	// Book Scope afterwards.
	KFCResultModel::SetFromBook(fromBook);
	// ...and the Search: beside it, for the same reason: All Documents draws its document rows
	// closed and says which has no window; a Search: changed afterwards changes nothing on screen.
	KFCResultModel::SetSearchScope(fromBook ? KFCResultModel::kScopeBook
		: allDocuments ? KFCResultModel::kScopeAllDocuments
		: (selectionScope == IWalkerScopeFactoryUtils::kStoryScope) ? KFCResultModel::kScopeStory
		: (selectionScope == IWalkerScopeFactoryUtils::kToEndOfStoryScope) ? KFCResultModel::kScopeToEndOfStory
		: (selectionScope == IWalkerScopeFactoryUtils::kSelectionScope) ? KFCResultModel::kScopeSelection
		: KFCResultModel::kScopeDocument);

	// ...and that a search HAPPENED, which the panel's illustration follows. Said separately from
	// the two lines around it because it survives finding nothing: a search that returned no hits
	// has still been run.
	KFCResultModel::NoteRun();

	// ...and WHICH book, for the tree's book row. Empty for a document search, which has no book
	// row at all. This is the panel's permanent answer to "what am I looking at": a status line is
	// one line, gets truncated, and is overwritten by the next message.
	KFCResultModel::SetBookName(bookName);

	// ...and WHICH TAB was searched. The replace pass re-walks each chapter, and a re-walk in another
	// mode returns another set of matches - so Change Checked compares this against the tab in force
	// then and refuses rather than lining rows up with the wrong occurrences.
	//
	// Read again rather than reusing `tab` from the top of this function, and that is deliberate:
	// `tab` was taken BEFORE CommitSearchMode, and what belongs on the results is the mode the walk
	// is about to actually run in. The two agree today - the commit states the value it just read -
	// but this is the one that would still be right on the day they stopped agreeing.
	KFCResultModel::SetSearchMode(KFCSearchEngine::CurrentSearchMode());

	// ...and the whole of what this walk was DRIVEN BY - the query plus every switch that decides
	// which matches come back. It is a key: Change Checked compares it before it re-walks. The tab
	// alone is not enough: retyping the find string, or turning Include Footnotes off, changes the
	// match set without changing the tab, and the re-walk would then meet other occurrences where the
	// hits below stand. See KFCSearchEngine::BuildWalkSignature.
	{
		PMString walkSignature;
		KFCSearchEngine::BuildWalkSignature(walkSignature);
		KFCResultModel::SetWalkSignature(walkSignature);
	}

	// ...and the FIND FORMAT itself, kept as a list rather than described in that string, because the
	// list compares itself properly and a hand-written fingerprint of it did not. Taken here, on the
	// same side of CommitSearchMode as the signature, for the same reason: a value read on one side
	// of a mode commit and compared against one read on the other could differ with nothing having
	// changed. See KFCSearchEngine::RememberFindFormat.
	KFCSearchEngine::RememberFindFormat();

	// ...and the five scope switches, read ONCE for the whole run. They come off the same dialog as
	// everything above, they are the same for every chapter by definition (the replace pass has to
	// re-walk with exactly these), and nothing can change them while the run is on: the progress bar
	// below is modal - so not inside CollectHitsInDoc, once per chapter, for an answer that cannot differ.
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);

	// Walk every target; only chapters that hold a hit go into the model (no empty branches). The
	// model was cleared above; each chapter is APPENDED as it finishes, and the caller draws the
	// tree once the search returns (see the AppendChapter below).
	// The progress bar, shown for EVERY scope (the user's request) - not for a book only: a one-document
	// search is a single CHAPTER with nothing to cancel between, but the bar is sized in stories, and a
	// single document with a lot of them takes just as long and would have no way to be stopped.
	//
	// DisableChildProgressBars stops anything the walk runs into from putting up a bar of its own.
	// That covers the windowless chapter opens as well: chapters are opened inside the loop below, not
	// before this bar exists.
	//
	// EQUAL SLICES PER CHAPTER, subdivided by stories inside each one. The slices have to be equal
	// because a chapter's size cannot be asked for before it is opened, and chapters are opened one
	// at a time. Within a chapter the story count comes from IStoryList and costs nothing, and
	// each story is subdivided by how far into its text the walk has got (CollectHitsInDoc does the
	// moving) - which is what keeps the bar alive through a chapter of a hundred stories.
	//
	// Why not simply ask the walker how far it is: ITextWalkerProgressMonitor is only a place to PARK
	// a bar - the CLIENT's own OnNextPosition is what has to call SetPosition on it, and the stock
	// kFindChangeClientBoss does not (measured: it registered fine and then never called once in
	// 5270 matches). spellpanel gets a moving bar there because it walks with a client it
	// wrote itself. A plug-in using the stock find/change client has to count its own progress.
	//
	// showImmediate = kTrue: a search ALWAYS puts the bar up. The default (kFalse) makes the bar wait
	// out an internal delay first, and the search beat that delay even at 5000+ hits (measured) - so
	// the one thing the bar is really there for, the cancel button, was never on screen. Better a
	// brief flash on a fast search than a search that cannot be stopped.
	const int32 progressTotal = static_cast<int32>(targets.size()) * kKFCChapterProgressSpan;

	// The title names the scope, because the bar does not imply it: "Searching book..." when it
	// really is a book, plain "Searching..." for a single document.
	PMString progressTitle(fromBook ? "Searching book..." : "Searching...");
	progressTitle.SetTranslatable(kFalse);
	KFCProgressBar progressBar(progressTitle, 0, progressTotal, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	// Where the bar stands as each chapter starts, and how far it has actually been advanced (that
	// second number is what lets KFCAdvanceProgress swallow an advance too small to repaint for, so
	// it has to be carried along rather than recomputed).
	int32 progressBase = 0;
	int32 progressReported = 0;

	int32 total = 0;
	int32 chaptersWithHits = 0;
	bool collectionTruncated = false;
	bool cancelled = false;
	// Chapters that could not be searched at all. Counted separately from "searched, no hits":
	// the summary has to be able to say a chapter was skipped, or a book search silently returns
	// fewer chapters than the book has and looks like the chapters simply held no matches.
	// Each entry is already spelled "name: why", ready for the note.
	std::vector<PMString> unsearchable;
	// ...and the chapters whose walk STARTED and then broke off. Kept apart from the list above
	// because they need a different sentence: these chapters were searched, their hits are in the
	// result set, and what is missing is the part of them the walk never reached.
	std::vector<PMString> brokeOff;
	// ...and the chapters this run OPENED and could not hand back - windowless, so the user can
	// neither see them nor close them, and each holds its .indd locked (KFCBookScope::AppendUnclosedNote).
	std::vector<PMString> unclosed;

	// EVERY STORY'S VERSION, WHILE ITS DOCUMENT IS STILL OPEN.
	// ITextModel::GetChangeCount of each story holding a hit (ReadStoryVersion) - what the replace compares
	// before it writes, so a story moved since without KFC (typing, Ctrl+Z of anything but a write of KFC's
	// own - which the panel follows, KFCUndoFollow) is not written to. A book's chapter has
	// it read in front of the release that closes a chapter this search opened.
	auto readStoryVersions = [](const UIDRef& docRef, const std::vector<KFCResultModel::Hit>& hits,
		std::map<UID, uint32>& outVersions)
	{
		IDataBase* const db = docRef.GetDataBase();
		for (size_t h = 0; h < hits.size(); ++h)
		{
			const UID story = hits[h].storyUID;
			uint32 version = 0;
			if (outVersions.count(story) == 0 && KFCSearchEngine::ReadStoryVersion(db, story, version))
				outVersions[story] = version;
		}
	};
	// One target's hits into the model as a chapter - the book's chapters and the Book Scope OFF walk's
	// documents alike. Page-orders the hits and bakes the "P<page>(<n>) " locator onto each line: this needs
	// the WHOLE chapter's hits (page order and the within-page ordinal are only known once it is complete),
	// which is why the flush unit is the chapter, not a fixed hit count.
	//
	// Into the model only. The tree is drawn ONCE, by the caller, when the search returns: the progress bar
	// is modal, so while it is up the panel cannot be read or clicked and a per-chapter rebuild would be work
	// nobody sees. Handed over rather than copied: the model takes the hits and leaves the Chapter empty
	// (the count is taken before the handover).
	auto fileChapter = [&](const KFCBookScope::ChapterDoc& target, std::vector<KFCResultModel::Hit>& hits,
		std::map<UID, uint32>& storyVersions)
	{
		KFCSearchEngine::FinalizeHits(hits);
		KFCResultModel::Chapter chapter;
		chapter.name = target.shortName;
		chapter.name.SetTranslatable(kFalse);
		chapter.docRef = target.docRef;
		chapter.file = target.file;
		chapter.hits.swap(hits);
		chapter.storyVersions.swap(storyVersions);
		total += static_cast<int32>(chapter.hits.size());
		++chaptersWithHits;
		KFCResultModel::AppendChapter(std::move(chapter));
	};

	// BOOK SCOPE OFF: THE RANGE IS THE DIALOG'S, THE ORDER IS FROM THE TOP.
	// The RANGE is whatever Search: says - Document, All Documents, Story, To End of Story, Selection - the way
	// Edit > Find/Change reads it (the author: "KFC uses the official Find/Change's settings as they are, so
	// follow the official way as far as it goes").
	//
	// THE ORDER IS FROM THE TOP, WHEREVER THE CARET IS (the author's call).
	// The dialog's own scope, given a selection, starts AT it and loops back to the head
	// (IWalkerScopeFactoryUtils.h:169-171) - Find Next's order, one match at a time from where the user stands.
	// A list keeps its walk's order within a page (FinalizeHits), so walking that scope numbers the lower
	// story's match P1(1) whenever the caret stands in it (the regression cases sc-selection-caret and
	// sc-show-ignores-scope). Where the selection decides only where a walk STARTS, the walk starts at the top:
	//   Document         the active document, by the document form - its range does not depend on the selection;
	//   All Documents    each open document by it in turn, in `targets`' order - each one a walk of its own, named
	//                    on its own in the summary's notes;
	//   Story            the dialog's own Story scope is asked WHICH story - its first match answers, since every
	//                    match of a Story walk is in that story - and that story is walked by the story form;
	//   To End of Story, Selection   the dialog's own scope: both run forward from the selection, no loop back.
	// Every match is filed under the document its story is in, in the order of `targets`. A match in a document
	// `targets` does not hold - the scope walking somewhere KFC did not expect, never seen - gets a chapter of its
	// own after them; one in a chapter KFC holds from a book search is left out, as `targets` leaves it out.
	if (!fromBook)
	{
		if (allDocuments)
		{
			PMString taskLine("All Documents - ");
			taskLine.SetTranslatable(kFalse);
			taskLine.AppendNumber(static_cast<int32>(targets.size()));
			taskLine.Append(" open document(s)");
			progressBar.SetTaskText(taskLine);
		}
		else
			KFCSetChapterTask(progressBar, "Document", 0, 1, targets[0].shortName);
		KFCAdvanceProgress(&progressBar, progressReported, 0, true /*force*/);

		// What a walk that did not end cleanly owes the summary, named for what was walked: one that broke off
		// keeps its hits (they are real, and filed below), one that never ran is named with its reason.
		auto noteWalk = [&](const PMString& walkName, ChapterWalkResult walkResult)
		{
			PMString name(walkName);
			name.SetTranslatable(kFalse);
			if (walkResult == kChapterWalkFailed)
				brokeOff.push_back(name);
			else if (walkResult != kChapterWalked && walkResult != kChapterCancelled)
			{
				const PMString why(ChapterWalkResultText(walkResult));
				if (!why.IsEmpty())
				{
					name.Append(": ");
					name.Append(why);
				}
				unsearchable.push_back(name);
			}
		};

		// One list for every walk below, in walk order, each hit with the database it was found in. The ceiling
		// is the whole search's: CollectHitsInDoc counts what the list already holds.
		std::vector<KFCResultModel::Hit> hits;
		std::vector<IDataBase*> hitDBs;
		const size_t limit = static_cast<size_t>(KFCResultModel::kKFCCollectHitLimit);
		if (progressBar.WasCancelled(kFalse))
			cancelled = true;
		else if (allDocuments)
		{
			// A slice of the bar per document, the way the book's chapters share it.
			for (size_t t = 0; t < targets.size(); ++t)
			{
				const int32 base = static_cast<int32>(t) * kKFCChapterProgressSpan;
				KFCSetChapterTask(progressBar, "Document", t, targets.size(), targets[t].shortName);
				KFCAdvanceProgress(&progressBar, progressReported, base, true /*force*/);
				if (t > 0 && progressBar.WasCancelled(kFalse))
				{
					cancelled = true;
					break;
				}
				int32 storiesInDoc = CountSearchableStories(targets[t].docRef);
				if (storiesInDoc < 1)
					storiesInDoc = 1;
				bool capped = false;
				ChapterWalkResult walkResult = kChapterWalked;
				CollectHitsInDoc(targets[t].docRef, limit, scopeOptions, kHitEverything, hits, capped, walkResult,
					&progressBar, base, kKFCChapterProgressSpan, storiesInDoc, progressReported, UIDRef::gNull,
					IWalkerScopeFactoryUtils::kEmptyScope, &hitDBs);
				if (walkResult == kChapterCancelled)
				{
					cancelled = true;
					break;
				}
				noteWalk(targets[t].shortName, walkResult);
				if (capped)
				{
					collectionTruncated = true;		// the list is full: the documents after this one are not walked
					break;
				}
			}
		}
		else
		{
			int32 storiesTotal = CountSearchableStories(targets[0].docRef);
			if (storiesTotal < 1)
				storiesTotal = 1;
			bool capped = false;
			ChapterWalkResult walkResult = kChapterWalked;
			if (selectionScope == IWalkerScopeFactoryUtils::kDocumentScope)
			{
				CollectHitsInDoc(targets[0].docRef, limit, scopeOptions, kHitEverything, hits, capped, walkResult,
					&progressBar, 0, progressTotal, storiesTotal, progressReported, UIDRef::gNull,
					IWalkerScopeFactoryUtils::kEmptyScope, &hitDBs);
			}
			else if (selectionScope == IWalkerScopeFactoryUtils::kStoryScope)
			{
				// Which story: the dialog's own Story scope, to its first match. No match = nothing to list.
				std::vector<KFCResultModel::Hit> first;
				std::vector<IDataBase*> firstDBs;
				bool firstCapped = false;
				int32 noBar = 0;
				CollectHitsInDoc(UIDRef::gNull, 1, scopeOptions, kHitPlaceAndText, first, firstCapped, walkResult,
					nil, 0, 0, 1, noBar, UIDRef::gNull, IWalkerScopeFactoryUtils::kStoryScope, &firstDBs);
				IDataBase* const storyDB = (!first.empty() && !firstDBs.empty()) ? firstDBs[0] : nil;
				if (walkResult == kChapterWalked && storyDB != nil)
				{
					CollectHitsInDoc(UIDRef(storyDB, storyDB->GetRootUID()), limit, scopeOptions, kHitEverything, hits,
						capped, walkResult, &progressBar, 0, progressTotal, storiesTotal, progressReported,
						UIDRef(storyDB, first[0].storyUID), IWalkerScopeFactoryUtils::kEmptyScope, &hitDBs);
				}
			}
			else
			{
				CollectHitsInDoc(UIDRef::gNull, limit, scopeOptions, kHitEverything, hits, capped, walkResult,
					&progressBar, 0, progressTotal, storiesTotal, progressReported, UIDRef::gNull, selectionScope,
					&hitDBs);
			}
			if (capped)
				collectionTruncated = true;
			if (walkResult == kChapterCancelled)
				cancelled = true;
			else
				noteWalk(targets[0].shortName, walkResult);
		}
		KFCAdvanceProgress(&progressBar, progressReported, progressTotal, true /*force*/);

		if (!cancelled)
		{
			std::vector<std::vector<KFCResultModel::Hit> > perTarget(targets.size());
			for (size_t k = 0; k < hits.size() && k < hitDBs.size(); ++k)
			{
				IDataBase* const db = hitDBs[k];
				size_t t = 0;
				while (t < targets.size() && targets[t].docRef.GetDataBase() != db)
					++t;
				if (t == targets.size())
				{
					const UIDRef docRef(db, (db != nil) ? db->GetRootUID() : kInvalidUID);
					InterfacePtr<IDocument> doc(docRef, UseDefaultIID());
					if (doc == nil || KFCBookScope::IsHeldDoc(docRef))
						continue;
					targets.push_back(KFCBookScope::DocAsChapter(doc));
					perTarget.push_back(std::vector<KFCResultModel::Hit>());
				}
				perTarget[t].push_back(std::move(hits[k]));
			}
			for (size_t t = 0; t < targets.size(); ++t)
			{
				if (perTarget[t].empty())
					continue;
				std::map<UID, uint32> storyVersions;
				readStoryVersions(targets[t].docRef, perTarget[t], storyVersions);
				fileChapter(targets[t], perTarget[t], storyVersions);
			}
		}
	}

	// BOOK SCOPE ON: CHAPTER BY CHAPTER - the dialog has no Search: for a book, so each chapter is opened,
	// walked by the document form and handed back in turn (the `fromBook &&` keeps this loop the book's
	// alone).
	for (size_t i = 0; fromBook && i < targets.size(); ++i)
	{
		// "Chapter 3 / 12" over the chapter's own name, called BEFORE the chapter is walked so the
		// bar names what is being worked on rather than what has just finished. This is also what
		// keeps the bar moving through chapters that hold no hits at all.
		KFCSetChapterTask(progressBar, "Chapter", i, targets.size(), targets[i].shortName);
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Cancel is asked here, after the bar has been moved - from inside the walk and just above
		// (KFCAdvanceProgress; which call on the bar takes the click is not measured, see there).
		// kFalse = do NOT raise the global error state: it would outlive the search and fail the
		// commands that come after it.
		if (progressBar.WasCancelled(kFalse))
		{
			cancelled = true;
			break;
		}

		// Room left under the whole-search safety ceiling; once it is gone, stop walking further
		// chapters too (the result set is full).
		const int32 remaining = KFCResultModel::kKFCCollectHitLimit - total;
		if (remaining <= 0)
		{
			collectionTruncated = true;
			break;
		}

		// Open THIS chapter now (one the user already had open has its docRef).
		if (targets[i].docRef == UIDRef::gNull)
		{
			if (!KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			{
				// The reason is recorded; AppendUnopenableNote names it in the summary. Hand the
				// bar on all the same, so a book whose chapters will not open still fills it.
				progressBase += kKFCChapterProgressSpan;
				KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
				continue;
			}
		}

		const UIDRef chapterDocRef = targets[i].docRef;

		// Now that it is open, ask how many stories this chapter's slice of the bar has to be
		// divided between. At least one, so a chapter with no stories at all still moves the bar.
		int32 storiesInDoc = CountSearchableStories(chapterDocRef);
		if (storiesInDoc < 1)
			storiesInDoc = 1;

		std::vector<KFCResultModel::Hit> hits;
		bool docCapped = false;
		ChapterWalkResult walkResult = kChapterWalked;
		CollectHitsInDoc(chapterDocRef, static_cast<size_t>(remaining), scopeOptions, kHitEverything, hits, docCapped,
			walkResult, &progressBar, progressBase, kKFCChapterProgressSpan, storiesInDoc,
			progressReported);

		// This chapter is done, whatever it found: put the bar exactly where the next one starts, so
		// a chapter whose stories the walk left early still hands the bar on at the right place.
		progressBase += kKFCChapterProgressSpan;
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Hand the chapter back HERE, before any of the continues below can skip it.
		// The walk is over and what it produced is plain data - UIDs and text indices, which survive
		// the document being closed (measured). A jump or a replace that needs this
		// chapter later reopens it through ReopenChapterDoc, the path that already existed for
		// chapters the user closed by hand.
		//
		// Only chapters KFC opened are closed: ReleaseHeldDoc checks the held list itself, so one
		// the user already had open passes through untouched.
		//
		// CLOSED ON THE SPOT, not scheduled. A scheduled close does not run until the current tick
		// has unwound, and this run IS that tick - so every chapter handed back here would stay open,
		// and keep its .indd locked, until the whole search was over. Holding one chapter at a time
		// is the entire point of this loop, so the close has to happen here. (Measured on the
		// replace, which walks chapters the same way: four chapters, four .idlk files standing at
		// once.) Safe at this point - the walk has halted, the dirty
		// guard inside CollectHitsInDoc has already restored the flag, and everything read after
		// this (FinalizeHits, the Chapter it fills in) is plain values, not database work.
		//
		// The release's false cannot be read alone - HandBackHeldDocNow asks whether the chapter was ours
		// and whether it is still open, so a chapter the user closed under the run is not counted as one
		// left open with no window.
		// The story versions are read first, while the chapter is still open (readStoryVersions).
		std::map<UID, uint32> storyVersions;
		readStoryVersions(chapterDocRef, hits, storyVersions);

		if (!KFCBookScope::HandBackHeldDocNow(chapterDocRef))
			unclosed.push_back(targets[i].shortName);

		// Cancel heard inside the walk, at a story's end: the chapter is handed back above, the
		// run is thrown away below.
		if (walkResult == kChapterCancelled)
		{
			cancelled = true;
			break;
		}

		if (docCapped)
			collectionTruncated = true;
		if (walkResult == kChapterWalkFailed)
		{
			// The walk ran and then broke off. What it found before that is real, so this does NOT
			// skip the chapter - it falls through and files the hits like any other. What the
			// summary owes the user is the other half: the rest of this chapter was never looked at,
			// and its matches are missing from the list because nobody searched for them.
			PMString name(targets[i].shortName);
			name.SetTranslatable(kFalse);
			brokeOff.push_back(name);
		}
		else if (walkResult != kChapterWalked)
		{
			// This chapter was never searched at all. Named with its reason, because a skipped
			// chapter is indistinguishable from a chapter with no matches otherwise - which is
			// exactly what made this class of failure hard to see.
			PMString entry(targets[i].shortName);
			// The colon belongs to the reason, so an ending with no name of its own leaves the
			// chapter standing on its own rather than "chapter.indd: " with nothing after it.
			// See ChapterWalkResultText, which answers empty for the two endings this branch
			// does not handle.
			PMString why(ChapterWalkResultText(walkResult));
			if (!why.IsEmpty())
			{
				entry.Append(": ");
				entry.Append(why);
			}
			entry.SetTranslatable(kFalse);
			unsearchable.push_back(entry);
			continue;
		}
		if (hits.empty())
			continue;

		fileChapter(targets[i], hits, storyVersions);
	}

	// ASK ONCE MORE, now that the loop is over. The test inside the loop sits at the TOP of each
	// pass, so a cancel pressed while the LAST chapter was being walked has no next pass to be seen
	// in, and the search would finish as though the button had never been touched. Same rule as the
	// replace engine's - see the matching comment there. (The walk asks it too, when it moves on to
	// another story; a Cancel pressed during the LAST story walked is seen here.)
	if (!cancelled && progressBar.WasCancelled(kFalse))
		cancelled = true;

	// Cancelled: throw the half-finished result away rather than leave a partial list looking like a
	// complete one, and give the chapters back - the results that would have needed them are gone.
	// ReleaseHeldDocs schedules its closes, so it is safe to call from in here.
	if (cancelled)
	{
		KFCSearchEngine::DropResults();		// the rows, the book (its chapters closed) and the format
		outSummary.Clear();
		outSummary.SetTranslatable(kFalse);
		outSummary.Append("Search cancelled.");
		return 0;
	}

	// Every chapter KFC opened has already been handed back inside the loop - a book search leaves
	// nothing of its own open, and no .indd locked. The rows carry their chapter's file, so a jump
	// or a replace reopens whatever it needs (KFCJump's EnsureChapterReachable and the replace
	// engine's own reopen). What that costs is a document load on the first click into a chapter;
	// what it buys is that searching a book does not leave twenty hidden documents behind.

	// The chapters the run could not fully account for, said the same way whichever way the summary
	// ends - "No matches" is a lie too if a chapter was skipped, or a walk broke off before its end.
	PMString chapterNotes;
	chapterNotes.SetTranslatable(kFalse);
	AppendUnsearchableNote(chapterNotes, unsearchable);
	AppendSearchErrorNote(chapterNotes, brokeOff);
	KFCBookScope::AppendUnopenableNote(chapterNotes, unopenable);
	KFCBookScope::AppendUnclosedNote(chapterNotes, unclosed);

	// No matches: a plain, friendly line rather than "0 hit(s) in 0 chapter(s)".
	if (total == 0)
	{
		outSummary.Append("No matches");
		if (fromBook)
		{
			outSummary.Append(" in book \"");
			outSummary.Append(bookName);
			outSummary.Append("\".");
		}
		else if (allDocuments)
		{
			// how many were looked at, as the book's "of T chapter(s)" says
			outSummary.Append(" in ");
			outSummary.AppendNumber(static_cast<int32>(targets.size()));
			outSummary.Append(" open document(s).");
		}
		else
		{
			// the part Search: named: nothing past it was looked at
			outSummary.Append(SelectionScopeWords(selectionScope));
			outSummary.Append(" in document \"");
			outSummary.Append(targets[0].shortName);
			outSummary.Append("\".");
			outSummary.Append(fellBackNote);	// a Search: the selection did not offer (empty otherwise)
		}
		outSummary.Append(chapterNotes);
		return 0;
	}

	// The one-line summary. The hit count leads, so it stays visible even when the narrow
	// single-line status field truncates the tail.
	//
	// No name here. The book and the document are the first two rows of the tree directly below
	// this line, so repeating them costs room in a field that truncates and says nothing the eye
	// has not already got (the author's call). The "no matches" wording DOES
	// still name what was searched - there is no tree under it to read.
	outSummary.AppendNumber(total);
	outSummary.Append(" hit(s)");
	if (fromBook)
	{
		// "in M of T chapter(s)": M chapters held a hit, T chapters were looked at. Without the T
		// there is no way to tell a book whose other chapters simply had no matches from a book
		// whose other chapters were never searched.
		PMString chapStr;	chapStr.AppendNumber(chaptersWithHits);
		PMString totalChapStr;	totalChapStr.AppendNumber(static_cast<int32>(targets.size()));
		outSummary.Append(" in ");
		outSummary.Append(chapStr);
		outSummary.Append(" of ");
		outSummary.Append(totalChapStr);
		outSummary.Append(" chapter(s).");
	}
	else if (allDocuments)
	{
		// the same "M of T" for All Documents: the documents that held none are not listed
		outSummary.Append(" in ");
		outSummary.AppendNumber(chaptersWithHits);
		outSummary.Append(" of ");
		outSummary.AppendNumber(static_cast<int32>(targets.size()));
		outSummary.Append(" document(s).");
	}
	else
	{
		outSummary.Append(SelectionScopeWords(selectionScope));
		outSummary.Append(".");
		outSummary.Append(fellBackNote);	// a Search: the selection did not offer (empty otherwise)
	}

	// One cap: the whole-search safety ceiling IS the panel's display cap (kKFCCollectHitLimit, KFCResultModel.h),
	// so a search that stops there has every row it holds on the panel - "narrow it" is the one note.
	if (collectionTruncated)
	{
		outSummary.Append(" Stopped at the ");
		outSummary.AppendNumber(KFCResultModel::kKFCCollectHitLimit);
		outSummary.Append(" safety limit - narrow your search.");
	}

	outSummary.Append(chapterNotes);

	// Where the commands are. Check All / Uncheck All live on the ROWS' right-click menu - not on the
	// panel flyout, which has no row to ask about, and those two have to know whether they mean the
	// whole book or one chapter. Nothing else on screen says so, which would leave the one command
	// that turns a result list into a work list undiscoverable (the user's request).
	//
	// Last, after the warnings: it is an offer, not something that went wrong, and the status field
	// truncates its tail when it has to.
	outSummary.Append(" Right-click the book or a document row for a menu.");
	return total;
}

bool KFCSearchEngine::IsSearching()
{
	return gSearching;
}

const char* KFCSearchEngine::TabName(int32 mode)
{
	// The Find/Change dialog's own name for a tab, for the status line and the panel's tab
	// (KFCPanelTitle). Not translatable - these are the dialog's labels, which KFC echoes in English
	// throughout.
	switch (mode)
	{
		case IFindChangeOptions::kTextSearch:			return "Text";
		case IFindChangeOptions::kGrepSearch:			return "GREP";
		case IFindChangeOptions::kGlyphSearch:			return "Glyph";
		case IFindChangeOptions::kObjectSearch:			return "Object";
		case IFindChangeOptions::kTransliterateSearch:	return "Transliterate";
		case IFindChangeOptions::kColorSearch:			return "Colour";
		default:										return "";
	}
}

int32 KFCSearchEngine::CurrentSearchMode()
{
	// Stored on the results too, so the replace can refuse to re-walk in a different mode.
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	return (opts != nil) ? static_cast<int32>(opts->GetSearchMode()) : -1;
}

int32 KFCSearchEngine::CurrentSearchScope()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return -1;
	const IWalkerScopeFactoryUtils::WalkScopeType scope = opts->GetFindChangeScope(opts->GetSearchMode());
	// (an unset Search: - see the header)
	return (scope == IWalkerScopeFactoryUtils::kEmptyScope) ? static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope)
		: static_cast<int32>(scope);
}

namespace
{
// Where a Search: value stands in the dialog's menu, narrowest last - the order the selection unlocks them
// (SnpFindAndReplace.cpp builds its menu the same way from GetActiveSelectionScope). -1 = not on the menu.
int32 SearchScopeBreadth(int32 scope)
{
	switch (scope)
	{
		case IWalkerScopeFactoryUtils::kAllDocumentScope:	return 0;
		case IWalkerScopeFactoryUtils::kDocumentScope:		return 1;
		case IWalkerScopeFactoryUtils::kStoryScope:
		case IWalkerScopeFactoryUtils::kStoryListScope:		return 2;
		case IWalkerScopeFactoryUtils::kToEndOfStoryScope:	return 3;
		case IWalkerScopeFactoryUtils::kSelectionScope:		return 4;
		default:											return -1;
	}
}
}	// anonymous namespace

int32 KFCSearchEngine::SearchScopeForSelection(int32 scope)
{
	const int32 wanted = SearchScopeBreadth(scope);
	if (wanted <= SearchScopeBreadth(IWalkerScopeFactoryUtils::kDocumentScope))
		return scope;	// All Documents / Document need no selection (and a value off the menu is not ours to change)
	const int32 offered = SearchScopeBreadth(Utils<IWalkerScopeFactoryUtils>()->GetActiveSelectionScope());
	return (wanted <= offered) ? scope : static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope);
}

const char* KFCSearchEngine::SearchScopeName(int32 scope)
{
	// The dialog's own words in English, as TabName gives the tabs'.
	switch (scope)
	{
		case IWalkerScopeFactoryUtils::kDocumentScope:		return "Document";
		case IWalkerScopeFactoryUtils::kAllDocumentScope:	return "All Documents";
		case IWalkerScopeFactoryUtils::kStoryScope:			return "Story";
		case IWalkerScopeFactoryUtils::kToEndOfStoryScope:	return "To End of Story";
		case IWalkerScopeFactoryUtils::kSelectionScope:		return "Selection";
		default:											return "";
	}
}

const char* KFCSearchEngine::FindCommandName(bool bookScopeOn)
{
	if (bookScopeOn)
		return "Find in Book";
	// what the search will make of Search: with this selection - the dialog's own display
	switch (SearchScopeForSelection(CurrentSearchScope()))
	{
		case IWalkerScopeFactoryUtils::kAllDocumentScope:	return "Find in All Documents";
		case IWalkerScopeFactoryUtils::kStoryScope:			return "Find in Story";
		case IWalkerScopeFactoryUtils::kToEndOfStoryScope:	return "Find to End of Story";
		case IWalkerScopeFactoryUtils::kSelectionScope:		return "Find in Selection";
		default:											return "Find in Document";
	}
}

bool KFCSearchEngine::SetQuery(const PMString& text, int32 mode)
{
	// The Text and GREP tabs only - the two whose Find what is a plain string.
	if (mode != IFindChangeOptions::kTextSearch && mode != IFindChangeOptions::kGrepSearch)
		return false;
	// The tab first, then the string - the snippet's order (Do_FindText states the mode before the find string).
	if (!CommitFindChangeInt(kFindSearchModeCmdBoss, mode, mode))
		return false;
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindStringCmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IStringData> value(cmd, UseDefaultIID());
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (value == nil || modeData == nil)
		return false;
	value->Set(text);
	modeData->Set(mode);
	return ProcessFindChangeCmd(cmd);
}

bool KFCSearchEngine::CanSearchTab(int32 mode)
{
	// Object and Colour search by ATTRIBUTE, with walkers of their own, and find page items - not lines
	// of text. Every other value (including -1, settings unreadable) is left for the search to answer.
	return mode != IFindChangeOptions::kObjectSearch && mode != IFindChangeOptions::kColorSearch;
}

void KFCSearchEngine::ShutdownCleanup()
{
	// The remembered Find Format is the only thing this file keeps that is not a plain value: an
	// AttributeBossList holding references to the dialog's attributes, and the raw IDataBase* those
	// UIDs belong to. Neither may still be standing when the .pln unloads - dropping the list runs
	// its destructor, which lets go of every attribute in it, and that is database work no static
	// destructor should be doing against an application that has already torn itself down - the same
	// rule, and the same reason, as the cleanups beside this one in KFCStartupShutdown::Shutdown.
	KFCSearchEngine::ForgetSearchedFindFormat();
}

// HERE, NOT IN THE JUMP (KFCJump.cpp): walking a story and putting a row right is the model half's work.
// The jump asks it through IKFCRuns.
// A ROW WHOSE PLACE HAS MOVED UNDER IT IS LOOKED FOR AGAIN (the author's call).
// The rows keep their places themselves, and follow every change KFC makes - and an Edit > Undo / Redo
// of a write of KFC's own (KFCUndoFollow). Not the edits that are not followed: typing, an Undo of
// anything else, the Track Changes panel, a script - which move the text without telling them, so a
// row's stored place can be off although its text has not been touched. (Measured, case
// del-jump-undo-reject, before Undo was followed: delete the first of two matches, take it back with
// Reject Change, press Ctrl+Z, click the second row - its place was one character off, and the jump
// called it "missing".)
//
// So before a jump gives up on a row, the story is walked again under the same query, and the row moves
// to the ONE match that is the same text with the same line around it (the three segments the row
// drew) and that no other row stands on. None, or more than one, and it is missing as before - a guess
// between two look-alikes would be worse than saying so. Rows not replaced only: a replaced row is found
// by its tracked change (KFCTrackChange::RefreshRowFromRecords), before this is asked. True = the row was
// moved; ioStart / ioEnd are its new place.
bool KFCSearchEngine::RelocateStaleRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID,
	TextIndex& ioStart, TextIndex& ioEnd)
{
	bool checked = false, replaced = false, locked = false;
	if (!KFCResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked) || replaced)
		return false;
	// Another query would find other matches - nothing to compare with. ASKED, NOT REFUSED: this is
	// RefuseChangedQuery's question without its consequences - it states the tab the walk below runs in,
	// and clears nothing. (RefuseChangedQuery itself would, on a changed query, clear the whole result set
	// and hand the chapters back in the middle of a jump, the tree left drawing rows the model no longer
	// held.)
	if (!KFCReplaceEngine::QueryUnchangedSinceSearch())
		return false;
	KFCResultModel::RowDisplay row;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	if (!KFCResultModel::GetHitRow(chapterIdx, hitIdx, row)
		|| !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash))
		return false;
	// The row's own story, and only that one - not the whole document, every match in it given its line
	// and hash, to look in one story.
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil || !db->IsValidUID(storyUID))
		return false;
	std::vector<KFCResultModel::Hit> hits;
	{
		// forward, as the search was
		KFCForwardSearchScope forward;
		WalkerScopeOptions scopeOptions;
		KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
		// the places and the line: a candidate is compared by its text and the line around it
		if (!CollectStoryHits(UIDRef(db, storyUID), scopeOptions, hits))
			return false;
	}
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	int32 found = -1;
	int32 count = 0;
	for (size_t h = 0; h < hits.size() && count < 2; ++h)
	{
		const KFCResultModel::Hit& cand = hits[h];
		if (cand.storyUID != storyUID || cand.matchHash != hash
			|| (cand.textEnd - cand.textStart) != (ioEnd - ioStart)
			|| cand.preText != row.preText || cand.postText != row.postText)
			continue;
		// a place another row already stands on is that row's
		bool taken = false;
		for (int32 i = 0; i < hitCount && !taken; ++i)
		{
			UID s2 = kInvalidUID;
			TextIndex a2 = kInvalidTextIndex, b2 = kInvalidTextIndex;
			uint64 h2 = 0;
			if (i != hitIdx && KFCResultModel::GetHitMatchIdentity(chapterIdx, i, s2, a2, b2, h2)
				&& s2 == cand.storyUID && a2 == cand.textStart && b2 == cand.textEnd)
				taken = true;
		}
		if (taken)
			continue;
		found = static_cast<int32>(h);
		++count;
	}
	if (count != 1)
		return false;
	const KFCResultModel::Hit& to = hits[static_cast<size_t>(found)];
	KFCResultModel::SetHitRange(chapterIdx, hitIdx, to.storyUID, to.textStart, to.textEnd);
	KFCResultModel::SetHitSegments(chapterIdx, hitIdx, to.preText, to.matchText, to.postText, to.matchHash);
	if (KFCResultModel::GetHitOutcome(chapterIdx, hitIdx) == KFCResultModel::kOutcomeMissing)
		KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeNone);	// found after all
	ioStart = to.textStart;
	ioEnd = to.textEnd;
	return true;
}

// End, KFCSearchEngine.cpp.
