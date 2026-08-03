//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Search engine implementation. See KBSSearchEngine.h for the contract. The walker loop is
//  ported from KESCL's CollectMatches (KESCL left untouched); the one change is the point of
//  this plugin: KBS does NOT set the find string or pin the search mode - it walks with the
//  user's current Find/Change options as they are, so the mode (Text or GREP) is followed.
//
//  For each match the finder hands back (story, start, end); BuildHit then reads the containing
//  paragraph (IComposeScanner::FindSurroundingParagraph + CopyText) and splits it, at the exact
//  UTF-16 offsets, into the three segments the colour cell paints. The split is UTF-16-unit
//  exact (GrabUTF16Buffer + SetXString) so it matches TextIndex semantics; match boundaries
//  never fall inside a surrogate pair, so no code point is ever cut.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"		// FindSurroundingParagraph / CopyText (the hit line's text)
#include "IDocument.h"
#include "ILayoutUIUtils.h"
#include "IFindChangeOptions.h"
#include "IFindChangeCmdData.h"
#include "IFindChangeService.h"		// FindChangeResult enum
#include "ICommand.h"
#include "IIntData.h"				// kFindSearchModeCmdBoss carries two of these - see CommitSearchMode
#include "IBoolData.h"				// kFindChangeGlyphIDCmdBoss: which side of the glyph search is being set
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ITextModel.h"
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
// For naming the Glyph tab's query in the saved report ("Glyph 1234 (Kozuka Mincho Pr6N  Regular)"):
#include "IFontFamily.h"			// family UID -> the family's display name, and its faces
#include "IGlyphUtils.h"			// GetUnicodeForGlyphID
#include "IPMFont.h"				// the face, for that lookup
#include "ITextAttrFont.h"			// the font STYLE name  (kTextAttrFontStyleBoss)
#include "ITextAttrUID.h"			// the font FAMILY uid  (kTextAttrFontUIDBoss)

// General includes:
#include "AttributeBossList.h"		// the find attributes the Glyph tab's query carries
#include "TextAttrID.h"				// kTextAttrFontUIDBoss / kTextAttrFontStyleBoss
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss, kFindChangeClientBoss, kTextWalkerService(...)
#include "CTextEnum.h"				// Text::GlyphID / kInvalidGlyphID (the Glyph tab's query)
#include "WalkerScopeOptions.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
#include "ProgressBar.h"			// RangeProgressBar - the search's progress + cancel (both scopes)
#include "CmdUtils.h"
#include "CreateObject.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "PersistUtils.h"			// ::GetUIDRef
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "Utils.h"
#include "WideString.h"

#include <vector>
#include <algorithm>				// std::stable_sort (the matches' page order)
#include <map>						// the per-frame cache one document's walk keeps (FrameFacts)
#include <stdio.h>					// snprintf - the U+ formatting in DescribeGlyphQuery

// Project includes:
#include "KBSSearchEngine.h"
#include "KBSBookScope.h"
#include "KBSResultModel.h"
#include "KBSRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KBSOversetLocator.h"	// the "+" page for an overset hit (locator + sort key)

namespace
{

// The whole-search safety ceiling: the collector stops after this many hits across ALL chapters,
// so a huge or match-everywhere query cannot pile up an unbounded result set. Kept deliberately
// conservative - a small multiple of the panel display cap - so memory stays tiny and the search
// stays fast. Unlike the panel display cap (KBSResultModel::kKBSDisplayHitLimit) this bounds the
// RESULT SET itself, so hitting it caps a future export too. A common word in a large book can
// reach it, and that is intended: the panel shows the first kKBSDisplayHitLimit hits and the
// summary says to narrow the query.
const int32 kKBSCollectHitLimit = 10000;

// The smallest advance worth reporting to the progress bar. DoTask pumps the event queue, which is
// what makes Cancel work at all, but it is not free: calling it once per hit would run the message
// loop thousands of times over a large chapter. Small enough that Cancel still answers promptly.
const int32 kKBSProgressReportStep = 8;

// (Instrumentation removed: the answer came from the user's own observation - "cancelling works in
// the first document but not across documents" - which named the fault exactly. See the
// ask-once-more test after the chapter loop in SearchBook.)

// How much of the progress bar one CHAPTER gets. Every chapter gets the same slice, because the
// run no longer knows how big a chapter is before it opens it: chapters are opened one at a time
// now and closed again straight after, so there is no all-chapters-open moment in which to add up
// their story counts.
//
// Within a chapter the slice is still divided by STORIES - the search cannot know how many MATCHES
// a chapter holds until it has found them all, but a document that IS open can be asked for its
// story count for free, and each story is then subdivided by how far into its text the walk has
// got (CollectHitsInDoc does the moving; see CountSearchableStories).
//
// Large enough that a chapter with many stories still gets whole steps per story (a 500-story
// chapter gets 20 apiece); small enough that a 100-chapter book stays far inside int32.
const int32 kKBSChapterProgressSpan = 10000;

/** How many stories a walk of this document will visit. Counting them is free: IStoryList keeps the
    count, so nothing is loaded here (contrast with adding up their lengths, which would have to
    fetch every story before the search had even started).

    USER-ACCESSIBLE ones only, which is exactly the walk's own population - IStoryList.h:36-39 says
    internal stories "are not subject to search through find change". */
int32 CountSearchableStories(const UIDRef& docRef)
{
	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return 0;
	return storyList->GetUserAccessibleStoryCount();
}

// Why a chapter could not be walked AT ALL - which is a different thing from "walked it and found
// nothing". Returning zero hits for a chapter that was never actually searched reads as "this
// chapter has no matches", and there is no way for the user to see through that. Every failure
// below is therefore counted and named in the summary.
enum ChapterWalkResult
{
	kChapterWalked = 0,		// the walk ran (it may legitimately have found nothing)
	kChapterNoDatabase,		// the chapter's UIDRef carries no database, so there is nothing to walk
	kChapterNoOptions,		// no Find/Change options to search with
	kChapterNoWalker,		// the text-walker service handed out no walker
	kChapterNoScope,		// QueryDocumentWalkerScope refused this document
	kChapterNoClient		// the find/change walker client could not be created
};

/** A short reason to put in the status line. Not translatable - it names internals. */
const char* ChapterWalkResultText(ChapterWalkResult result)
{
	switch (result)
	{
		case kChapterNoDatabase:	return "no database";
		case kChapterNoOptions:		return "no find options";
		case kChapterNoWalker:		return "no text walker";
		case kChapterNoScope:		return "no walker scope";
		case kChapterNoClient:		return "no walker client";
		default:					return "";
	}
}

/** Name the chapters that were skipped rather than searched, and why. Appends nothing when every
    chapter was actually walked, so the ordinary summary is unchanged. */
void AppendUnsearchableNote(PMString& outSummary, int32 count, const PMString& firstName,
	ChapterWalkResult reason)
{
	if (count <= 0)
		return;

	outSummary.Append(" ");
	outSummary.AppendNumber(count);
	outSummary.Append(" chapter(s) could not be searched (");
	// The chapter name is user data and the reason names internals - neither is a translation key.
	PMString name(firstName);
	name.SetTranslatable(kFalse);
	outSummary.Append(name);
	outSummary.Append(": ");
	PMString why(ChapterWalkResultText(reason));
	why.SetTranslatable(kFalse);
	outSummary.Append(why);
	outSummary.Append(").");
}

// (A local AppendUnopenableNote sat here until 2026-08-02. The glyph scan had grown its own copy of
// the same sentence, and the two had already drifted: this one named only the first chapter, used
// one leading space, and did not double up ampersands - so a chapter called "A&B.indd" drew as
// "AB.indd" with the B underlined, the same fault fixed elsewhere on 2026-07-31. Both are gone;
// KBSBookScope::AppendUnopenableNote is the one sentence now. It is a DIFFERENT failure from
// AppendUnsearchableNote's above: there the document was open and the WALK did not run, here there
// is no document at all.)

// A search is running. The progress bar pumps events while it is up, so without this a menu command
// could be dispatched INTO the running search. The panel's actions read it through IsSearching().
bool gSearching = false;

// Raise gSearching for the length of a search, whichever way SearchBook returns.
struct SearchingFlagGuard
{
	SearchingFlagGuard()	{ gSearching = true; }
	~SearchingFlagGuard()	{ gSearching = false; }
};

// The Find/Change dialog's own name for a tab. Used in the status line, so the panel can name the
// tab the user is actually looking at. Not translatable - these are the dialog's labels, which KBS
// echoes in English throughout.
const char* SearchModeName(int32 mode)
{
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

// The Find/Change tab in force right now, as a plain int32; -1 when the options cannot be read.
// Stored on the results so the replace can refuse to re-walk in a different mode.
int32 CurrentSearchModeValue()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	return (opts != nil) ? static_cast<int32>(opts->GetSearchMode()) : -1;
}

// Is there anything to find on the Find/Change panel right now (in the current mode)?
bool HasFindQuery()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// The Glyph tab's query is NOT a find string - it is a glyph ID plus the font it belongs to, so
	// asking whether the find string is empty answers the wrong question there. (It happened to give
	// a usable answer only because picking a glyph whose character exists also leaves that character
	// in the find string; pick a glyph that has no character of its own and the string stays empty.)
	//
	// So ask the options themselves. IFindChangeOptions.h says this interface "is in a unique
	// position to know whether there is adequate information defined on it to allow searching for
	// something" - which for a glyph means the ID and its font attributes, held in the attribute
	// database the options carry (GetUIDAttrDB).
	if (mode == IFindChangeOptions::kGlyphSearch)
		return opts->IsThereSomethingToFind(opts->GetUIDAttrDB(), mode) != kFalse;

	// The find-what for the mode the user is actually in (Text vs GREP each have their own).
	const WideString& findText = opts->GetFindString(mode);
	return !findText.empty();
}

// The Glyph tab's query as one readable line: "Glyph 1234 (Kozuka Mincho Pr6N  Regular) U+845B".
//
// The Glyph tab has no find STRING - its query is a glyph id plus the font that id belongs to, and an
// id on its own names nothing (glyph 1234 is a different character in every font). So the font is
// looked up the same way the replace confirmation does it (KBSReplaceConfirmDialog::ResolveSide): the
// family is a UID into the options' attribute database, the style is a name beside it.
//
// Resolve() itself is deliberately NOT reused: it parks the resolved faces in statics that the dialog
// owns and releases later. This wants a string and nothing else, so it takes its own face and lets it
// go in the same breath. Every step is allowed to fail - a query that cannot be described in full is
// described as far as it goes, because this line is a caption, not a control.
PMString DescribeGlyphQuery(IFindChangeOptions* opts)
{
	PMString description;
	description.SetTranslatable(kFalse);
	if (opts == nil)
		return description;

	const Text::GlyphID glyphID = opts->GetFindGlyphID();
	if (glyphID == kInvalidGlyphID)
		return description;

	description.Append("Glyph ");
	description.AppendNumber(static_cast<int32>(glyphID));

	IDataBase* const db = opts->GetUIDAttrDB();
	const AttributeBossList* const attrs =
		opts->GetFindAttributeBossList(db, IFindChangeOptions::kGlyphSearch);
	if (db == nil || attrs == nil)
		return description;

	InterfacePtr<const ITextAttrUID> familyAttr(static_cast<const ITextAttrUID*>(
		attrs->QueryByClassID(kTextAttrFontUIDBoss, ITextAttrUID::kDefaultIID)));
	if (familyAttr == nil || familyAttr->Get() == kInvalidUID)
		return description;

	PMString styleName;
	InterfacePtr<const ITextAttrFont> styleAttr(static_cast<const ITextAttrFont*>(
		attrs->QueryByClassID(kTextAttrFontStyleBoss, ITextAttrFont::kDefaultIID)));
	if (styleAttr != nil)
		styleName = styleAttr->GetFontName();

	InterfacePtr<IFontFamily> family(db, familyAttr->Get(), UseDefaultIID());
	if (family == nil)
		return description;

	description.Append(" (");
	description.Append(family->GetFamilyName());
	if (!styleName.IsEmpty())
	{
		description.Append("  ");
		description.Append(styleName);
	}
	description.Append(")");

	// The Unicode is a bonus: an ALTERNATE form has none to give, and writing U+0000 there would be a
	// lie. QueryFace hands back a reference this function owns - release it before returning.
	IPMFont* const font = family->QueryFace(styleName);
	if (font != nil)
	{
		if (font->GetFontStatus() == IPMFont::kFontInstalled)
		{
			const UTF32TextChar ch = Utils<IGlyphUtils>()->GetUnicodeForGlyphID(font, glyphID);
			if (ch.GetValue() != 0)
			{
				char buf[16];
				snprintf(buf, sizeof(buf), " U+%04X", static_cast<unsigned int>(ch.GetValue()));
				description.Append(buf);
			}
		}
		font->Release();
	}

	return description;
}

// What the user asked for, as the one line the saved report's heading shows: the query and the tab it
// was typed on, e.g. "cat  (Text)". Recorded ON THE RESULTS at search time - see
// KBSResultModel::SetQueryText for why it must not be read back off the dialog afterwards.
PMString DescribeCurrentQuery()
{
	PMString description;
	description.SetTranslatable(kFalse);

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return description;

	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();
	if (mode == IFindChangeOptions::kGlyphSearch)
		description = DescribeGlyphQuery(opts);
	else
		description.Append(opts->GetFindString(mode));

	// The tab's own name, after the query, so the file says which of the two find strings this was
	// (Text and GREP each have their own, and the same characters mean different things on them).
	const char* const tabName = SearchModeName(static_cast<int32>(mode));
	if (tabName[0] != '\0')
	{
		description.Append("  (");
		description.Append(tabName);
		description.Append(")");
	}
	description.SetTranslatable(kFalse);
	return description;
}

// Re-state one side of the Glyph tab's query on the find/change options. The command carries two
// fields: IIntData is the glyph itself, and IBoolData picks the side - kTrue for the glyph being
// looked for, kFalse for the one that replaces it. This is what SnpFindAndReplace does in
// Do_FindGlyph and Do_ReplaceGlyph, the only worked glyph example in the SDK.
void CommitGlyphID(Text::GlyphID glyphID, bool16 findSide)
{
	// An empty box means different things on the two sides, so they are treated differently.
	//
	// FIND side: nothing to look for. The search is stopped before it ever gets here (HasFindQuery
	// asks IsThereSomethingToFind), so committing -1 would only clear what the dialog already holds.
	// Leave that side alone.
	//
	// REPLACE side: an empty Change To box is a legitimate request - it DELETES every match, exactly
	// as an empty change string does on the Text tab, and the Find/Change dialog itself allows it
	// (confirmed against the dialog, 2026-07-31). There the -1 MUST be stated: leaving it unstated
	// makes the replace command fall back on whatever change glyph was committed last, writing a
	// glyph the user did not choose and cannot see anywhere on screen. Stating it is what overwrites
	// that leftover.
	if (glyphID == kInvalidGlyphID && findSide)
		return;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindChangeGlyphIDCmdBoss));
	if (cmd == nil)
		return;
	InterfacePtr<IIntData>  value(cmd, UseDefaultIID());
	InterfacePtr<IBoolData> side(cmd, UseDefaultIID());
	if (value == nil || side == nil)
		return;
	value->Set(glyphID);
	side->Set(findSide);

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		// Same reasoning as the mode commit below: the walk simply runs with whatever was committed
		// last, but the error state has to be cleared or it fails every find command after it.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
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
// The second one used to be missing here, so a match inside an individually hidden frame came up
// with no "hidden" mark at all. Adobe asks both: spellpanel's DetermineIfTextIsHidden does the layer
// test and then IPageItemVisibilityFacade::IsHidden, and states in its own comment that it is the
// same code as FindChangeClient.cpp - i.e. this is what the Find/Change dialog itself reports.
bool IsFrameHidden(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;

	const UID spreadLayerUID = Utils<ILayerUtils>()->GetLayerUID(frameHier);
	if (spreadLayerUID != kInvalidUID)
	{
		InterfacePtr<ISpreadLayer> spreadLayer(db, spreadLayerUID, UseDefaultIID());
		if (spreadLayer != nil)
		{
			InterfacePtr<IDocumentLayer> docLayer(db, spreadLayer->GetDocLayerUID(), UseDefaultIID());
			if (docLayer != nil && !docLayer->IsVisible())
				return true;
		}
	}

	// The item's own switch. Asked second because it is the rarer of the two, and asked even when the
	// layer could not be resolved - "cannot tell about the layer" is not an answer about the item.
	return Utils<Facade::IPageItemVisibilityFacade>()->IsHidden(UIDRef(db, frameUID)) != kFalse;
}

// Is this frame on a LOCKED layer? Same two-step as the hidden test above (spread layer -> the
// document layer that carries the switch), asked so the replace can leave such a match alone.
// A frame that resolves to no layer reads as unlocked - see IsMatchEditable on why "cannot tell"
// must not turn into a refusal.
bool IsFrameOnLockedLayer(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;

	const UID spreadLayerUID = Utils<ILayerUtils>()->GetLayerUID(frameHier);
	if (spreadLayerUID == kInvalidUID)
		return false;
	InterfacePtr<ISpreadLayer> spreadLayer(db, spreadLayerUID, UseDefaultIID());
	if (spreadLayer == nil)
		return false;

	InterfacePtr<IDocumentLayer> docLayer(db, spreadLayer->GetDocLayerUID(), UseDefaultIID());
	if (docLayer == nil)
		return false;
	return docLayer->IsLocked();
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

// May the text at this position be rewritten, given the frame that decides its layer? Every lock
// InDesign has that bears on the question, asked in ONE place so the SEARCH (which marks a hit
// locked and withholds its check box) and the REPLACE (which refuses to write) can never disagree.
//
// A frame of kInvalidUID means "no page item to be locked" - not "cannot tell, refuse". See the
// header on why an unresolvable position has to read as editable.
bool IsEditableInFrame(const UIDRef& storyRef, UID frameUID)
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
	// (measured on the running application, 2026-07-28). So neither does KBS.
	if (IsPageItemLockedForEdit(db, frameUID))
		return false;

	// (3) The LAYER the frame sits on.
	return !IsFrameOnLockedLayer(db, frameUID);
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
	facts.isLocked = !IsEditableInFrame(storyRef, frameUID);
	return cache.insert(std::make_pair(key, facts)).first->second;
}

// Fill a hit from one match (story, [start, end)): its jump anchors, and the containing
// paragraph's text split into (before / matched / after) at the exact UTF-16 offsets.
void BuildHit(const UIDRef& docRef, const UIDRef& storyRef, TextIndex start, TextIndex end,
	FrameFactsCache& frameFacts, KBSResultModel::Hit& outHit)
{
	outHit.storyUID = storyRef.GetUID();
	outHit.textStart = start;
	outHit.textEnd = end;

	// The frame this match composes into. A POSITION question, so it is asked per hit; everything
	// that follows from the frame comes out of the cache.
	UID matchFrameUID = FrameUIDForPosition(storyRef, start);
	const FrameFacts* facts = &LookUpFrame(docRef, storyRef, matchFrameUID, frameFacts);

	// No page for the match itself (it is overset - composed but placed nowhere - or its frame sits
	// on no page). Name the page of the "+" overset indicator instead (the last placed parcel's
	// frame, climbing out of a pushed-out table) so the hit lists as "P<page>(n)ov" and sorts into
	// that page. If nothing is placed anywhere, leave it pageless: the locator falls back to a bare
	// "ov" and sorts to the end.
	//
	// The overset lookup itself is NOT cached: it climbs out of whatever table pushed the text out,
	// so two overset positions in one story can legitimately land on different frames.
	if (!facts->hasPage)
	{
		outHit.isOverset = true;
		const KBSOversetLoc loc = KBSFindOversetLocator(storyRef, start);
		if (loc.found)
		{
			const FrameFacts& oversetFacts = LookUpFrame(docRef, storyRef, loc.frameUID, frameFacts);
			if (oversetFacts.hasPage)
			{
				matchFrameUID = loc.frameUID;	// kept in step; the FACTS below are what every field is read from
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
	outHit.isLocked = facts->isLocked;
	if (outHit.isLocked)
		outHit.checked = false;		// a locked hit can never be checked. (Every hit STARTS unchecked since 2026-08-02, so this restates it - but the statement is about the lock, not about the default.)

	// The line's three drawn segments. Shared with the replace pass, which rebuilds a replaced
	// row exactly the same way from the range the replace command hands back.
	KBSSearchEngine::SplitLineAroundMatch(storyRef, start, end,
		outHit.preText, outHit.matchText, outHit.postText);
}

// Walk one document with the user's current Find/Change query and collect every match as a Hit.
// Read-only: the whole walk sits inside a SaveRestoreModifiedState dirty guard, so a windowless
// chapter can be closed afterwards without wanting a save. NOTHING is set on opts - the walk uses
// the user's Find/Change settings verbatim, so the search mode (Text or GREP) is followed.
//
// progressBar / progressBase: the run's bar and the point on it where this chapter starts.
// chapterSpan / storiesInDoc: how much of the bar this chapter owns, and how many stories to divide
// that slice between - asked by the caller once the chapter is open, since a chapter's size is not
// knowable before then. The walk moves the bar as it goes: a story at a time, each one subdivided
// by how far into its text the current match sits. nil is allowed for the bar.
void CollectHitsInDoc(const UIDRef& docRef, size_t maxHits, std::vector<KBSResultModel::Hit>& outHits,
	bool& outCapped, ChapterWalkResult& outResult,
	RangeProgressBar* progressBar, int32 progressBase, int32 chapterSpan, int32 storiesInDoc,
	int32& ioProgressReported)
{
	outResult = kChapterWalked;

	// FIRST, before anything touches the database: every step below (SaveRestoreModifiedState,
	// QueryDocumentWalkerScope) takes it, and without this the whole function just falls through
	// its nil checks and returns an empty hit list, which the caller cannot tell apart from "no
	// matches here".
	//
	// This is NOT a liveness test, despite what it looks like: a UIDRef carries the IDataBase*
	// itself, so a document closed underneath us leaves a dangling pointer here, not a nil one.
	// "Is this document still open?" only has one honest answer in KBS and it is
	// KBSBookScope::IsDocStillOpen. What this catches is a UIDRef that never had a database.
	IDataBase* chapterDB = docRef.GetDataBase();
	if (chapterDB == nil)
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

	IDataBase::SaveRestoreModifiedState dirtyGuard(chapterDB);

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
	if (walker->IsWalking())
		walker->Halt();

	// The single shared scope definition - the replace pass re-walks each chapter with exactly
	// these options, or the walk order the hits were numbered by would no longer line up.
	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);

	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions));
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
		outResult = kChapterNoWalker;
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
	// The price of holding it: UI work must not be pumped inside it, which is why the progress bar's
	// cancel is only asked between chapters (see SearchBook). Do not "correct" this to the one-command
	// shape without measuring both. (docs/ai-notes/kbs-book-and-search-api-audit-2026-07-31.md)
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);

	// What each of this document's frames answers about the hits inside it - see FrameFacts. Scoped
	// to this walk, so it can never outlive the state it describes.
	FrameFactsCache frameFacts;

	// Each story's text-change counter as the search finds it. Stamped onto every hit, so the
	// replace can ask "has one character moved in this story since?" instead of re-reading the text
	// under each row. Read once per STORY: the finder does not write, so it cannot move on us.
	std::map<UID, uint32> storyStamps;

	// Walk the whole document. Each ProcessCommand advances the walker to the next match ("find
	// next"), so we keep going until no more hits. prev* is a safety net: if the finder ever hands
	// back the exact same occurrence twice in a row (a query that does not advance the walker, e.g.
	// a zero-width GREP match), stop this walk rather than spin forever.
	TextIndex prevStart = kInvalidTextIndex;
	TextIndex prevEnd   = kInvalidTextIndex;
	UID       prevStory = kInvalidUID;
	// Where the bar stands within this chapter: how many stories the walk has finished, and how long
	// the one it is in now is. The walk visits a story at a time, so a change of story means the one
	// before it is done - whatever order the walker chose to take them in.
	int32 storiesDone = 0;
	UID progressStory = kInvalidUID;
	int32 progressStoryLength = 0;

	while (true)
	{
		InterfacePtr<ICommand> findCmd(CmdUtils::CreateCommand(kFindTextCmdBoss));
		if (findCmd == nil)
			break;
		InterfacePtr<IFindChangeCmdData> cmdData(findCmd, UseDefaultIID());
		if (cmdData == nil)
			break;
		cmdData->SetTextWalker(walker);

		if (CmdUtils::ProcessCommand(findCmd) != kSuccess)
		{
			// End of THIS walk only: the error state a failed find raises would otherwise
			// outlive it and block every later command in the session.
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			break;
		}
		if (cmdData->GetFindChangeResult() != IFindChangeService::kSuccess)
			break;	// kNotFound: no more matches, walk complete.

		TextIndex start = kInvalidTextIndex;
		TextIndex end = kInvalidTextIndex;
		UIDRef story = cmdData->GetRange(start, end);

		// No forward progress since the last match: bail out of this walk to avoid a hang.
		if (story.GetUID() == prevStory && start == prevStart && end == prevEnd)
			break;
		prevStory = story.GetUID();
		prevStart = start;
		prevEnd   = end;

		// Move the bar. A story we have not seen before means the one before it is finished; within a
		// story, the position is how far into its text this match sits. Safe inside the walker's
		// critical section: this only repaints, unlike WasCancelled, which pumps events and is
		// therefore asked between chapters only.
		if (progressBar != nil)
		{
			if (story.GetUID() != progressStory)
			{
				if (progressStory != kInvalidUID)
					++storiesDone;
				progressStory = story.GetUID();
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

			// Through DoTask, not SetPosition: pumping the event queue here is what lets the user
			// cancel at all (see KBSAdvanceProgress).
			KBSAdvanceProgress(progressBar, ioProgressReported, position);
		}

		// Whole-search safety ceiling reached: stop collecting. More matches may exist, but the
		// result set is capped so a match-everywhere query cannot grow it without bound.
		if (outHits.size() >= maxHits)
		{
			outCapped = true;
			break;
		}

		std::map<UID, uint32>::const_iterator stamp = storyStamps.find(story.GetUID());
		if (stamp == storyStamps.end())
		{
			InterfacePtr<ITextModel> storyModel(story, UseDefaultIID());
			const uint32 count = (storyModel != nil) ? storyModel->GetTextChangeCount() : 0;
			stamp = storyStamps.insert(std::make_pair(story.GetUID(), count)).first;
		}

		KBSResultModel::Hit hit;
		BuildHit(docRef, story, start, end, frameFacts, hit);
		hit.storyChangeCount = stamp->second;
		// The walk order within this chapter, stamped BEFORE FinalizeChapterHits sorts the vector
		// into page order. The replace pass re-walks the chapter and matches on this number.
		hit.walkOrder = static_cast<int32>(outHits.size());
		outHits.push_back(hit);
	}

	if (walker->IsWalking())
		walker->Halt();
}

// Put a chapter's hits in PAGE order and bake the "P<page>(<n>) " locator onto each hit line
// (the KESCL convention: page string, section-aware; a within-page ordinal in parens only when
// the page holds more than one match; "ov" for an overset match, which has no page). The locator
// rides on the front of preText, so the colour cell draws it in the normal colour ahead of the
// match. Pure string / index work - no recompose, so no dirty guard needed here.
void FinalizeChapterHits(std::vector<KBSResultModel::Hit>& hits)
{
	// Page order, overset matches to the end (their pageIndex is -1). Stable, so hits on the
	// same page keep their document (walk) order.
	std::stable_sort(hits.begin(), hits.end(),
		[](const KBSResultModel::Hit& a, const KBSResultModel::Hit& b)
		{
			const int32 pa = (a.pageIndex < 0) ? kMaxInt32 : a.pageIndex;
			const int32 pb = (b.pageIndex < 0) ? kMaxInt32 : b.pageIndex;
			return pa < pb;
		});

	// Walk contiguous runs of the same page (equal pageIndex after the sort): the run length is
	// the page's match count, and the position within the run is the within-page ordinal.
	size_t i = 0;
	while (i < hits.size())
	{
		size_t j = i;
		while (j < hits.size() && hits[j].pageIndex == hits[i].pageIndex)
			++j;
		const int32 runCount = static_cast<int32>(j - i);

		for (size_t k = i; k < j; ++k)
		{
			// The within-page ordinal, shown only when the page holds more than one match. The
			// locator string itself is built in ONE place - KBSResultModel::BuildHitLocator -
			// which the post-replace rewrite calls too, so the two cannot drift apart. Every rule
			// about its shape and its flag words lives there.
			hits[k].pageOrdinal = (runCount > 1) ? (static_cast<int32>(k - i) + 1) : 0;
			KBSResultModel::BuildHitLocator(hits[k]);
		}
		i = j;
	}
}

} // anonymous namespace

//========================================================================================
// Entry points for callers that find their ranges some other way than by walking a query.
//
// !! Nothing above this line was changed to make these possible. They only open up the helpers
//   the search itself already calls, so a borrowed hit and a searched hit cannot drift apart.
//   (The missing-glyph scan reads the composed wax; see KBSGlyphScanEngine.)
//========================================================================================

struct KBSSearchEngine::HitCache
{
	FrameFactsCache frames;
};

KBSSearchEngine::HitCache* KBSSearchEngine::NewHitCache()
{
	return new HitCache;
}

void KBSSearchEngine::DeleteHitCache(HitCache* cache)
{
	delete cache;
}

void KBSSearchEngine::BuildHitForRange(const UIDRef& docRef, const UIDRef& storyRef,
	TextIndex start, TextIndex end, HitCache* cache, KBSResultModel::Hit& outHit)
{
	if (cache == nil)
		return;
	BuildHit(docRef, storyRef, start, end, cache->frames, outHit);
}

void KBSSearchEngine::FinalizeHits(std::vector<KBSResultModel::Hit>& hits)
{
	FinalizeChapterHits(hits);
}

void KBSAdvanceProgress(RangeProgressBar* bar, int32& ioReported, int32 target, bool force)
{
	if (bar == nil)
		return;
	const int32 delta = target - ioReported;
	if (delta <= 0)
		return;
	if (!force && delta < kKBSProgressReportStep)
		return;		// too small to be worth pumping the event queue for

	// SetPosition, which takes the absolute position - hence no use for delta beyond the guard above.
	//
	// This was measured against DoTask on 2026-07-31, because a run had become impossible to cancel
	// and this call was the prime suspect. It was not the culprit: the cancel works exactly the same
	// through SetPosition. The fault was that nothing asked WasCancelled after the LAST chapter (see
	// the ask-once-more test in SearchBook and ReplaceChecked).
	//
	// Both forms are in the SDK, chosen by what the caller is counting: linksui advances a
	// TaskProgressBar with DoTask because it processes a list of files, while textimportfilter drives
	// a RangeProgressBar with SetPosition because it is measuring its way through a byte count -
	// and cancels from it perfectly well (TxtImpFilter.cpp:519-548: SetPosition every 32 reads,
	// WasCancelled every read). KBS measures its way through hits and stories, so this is the form
	// that fits.
	bar->SetPosition(target);
	ioReported = target;
}

void KBSSearchEngine::CommitSearchMode(Text::GlyphID overrideFindGlyph)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// The tabs this panel can walk with the TEXT walker. Object and Colour search by attribute
	// through walkers of their own (kObjectWalkerService / kColorSearchWalkerService) and return page
	// items rather than lines of text; SearchBook turns those away before reaching here, so stating
	// their mode would only mislead the engine.
	if (mode != IFindChangeOptions::kTextSearch
		&& mode != IFindChangeOptions::kGrepSearch
		&& mode != IFindChangeOptions::kGlyphSearch)
		return;

	// Read the glyph BEFORE the mode is committed. Committing a mode is a declaration, and there is
	// no promise anywhere that it leaves that mode's other settings untouched - so take the value
	// while it is certainly still there and hand it back afterwards.
	//
	// A caller that brought its own glyph wins over the dialog's. That is how the missing-glyph scan
	// runs: it hands over kAnyNotDefGlyphID, which the engine reads as "any notdef, whatever font
	// this run of text is in". Every other caller passes kInvalidGlyphID and gets the old behaviour
	// exactly - the dialog's own glyph, stated back to the engine unchanged.
	const Text::GlyphID findGlyphID =
		(overrideFindGlyph != kInvalidGlyphID)
			? overrideFindGlyph
			: ((mode == IFindChangeOptions::kGlyphSearch) ? opts->GetFindGlyphID() : kInvalidGlyphID);

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindSearchModeCmdBoss));
	if (cmd == nil)
		return;

	// TWO separate int fields on this boss, both kIntDataImpl behind different IIDs (checked against a
	// live object-model dump): the DEFAULT one is the value being set, and IID_IFINDCHANGEMODEDATA is
	// which mode's settings are being addressed. For this command they are the same mode - exactly how
	// SnpFindAndReplace passes it in all four of its find/replace entry points.
	InterfacePtr<IIntData> value(cmd, UseDefaultIID());
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (value == nil || modeData == nil)
		return;
	value->Set(static_cast<int32>(mode));
	modeData->Set(static_cast<int32>(mode));

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		// Nothing to recover - the walk simply runs in whatever mode was committed last. The error
		// state has to be cleared though: left standing it would fail every find command after it.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}

	// Now the glyph, and for the same reason the mode needed committing at all: what the dialog
	// HOLDS is not what the engine WALKS BY. The Glyph tab issues kFindChangeGlyphIDCmdBoss when the
	// user picks a glyph, so a walk driven from outside the dialog has to issue it again - stating
	// the mode by itself left the engine in glyph mode with no glyph, which is why this panel found
	// nothing at all on a query the dialog handled perfectly well (2026-07-30).
	//
	// Only the FIND side is stated here. The replace side is set where the replacement happens, so
	// a search can never leave a change glyph standing behind the user's back.
	if (mode == IFindChangeOptions::kGlyphSearch)
		CommitGlyphID(findGlyphID, kTrue);
}

bool KBSSearchEngine::CommitReplaceGlyph()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	if (opts->GetSearchMode() != IFindChangeOptions::kGlyphSearch)
		return true;	// Not a glyph replace - nothing to state, nothing to stop.

	// An empty Change To box is NOT refused. It means "delete every match" - the same thing an empty
	// change string means on the Text tab - and that is what the Find/Change dialog does with it.
	// KBS used to stop the run here, which made the panel strictly less capable than the dialog it
	// delegates to (user's report 2026-07-31). What the old code was right about is that the empty
	// box must never be left UNSTATED; CommitGlyphID states it on this side for exactly that reason.
	CommitGlyphID(opts->GetReplaceGlyphID(), kFalse);	// kFalse = the replace side of the query
	return true;
}

void KBSSearchEngine::GetKBSWalkerScopeOptions(WalkerScopeOptions& outOptions)
{
	// The five switches WalkerScopeOptions carries are EXACTLY the five the Find/Change dialog
	// shows under its search options, so they are read from there like everything else about the
	// query - KBS sets no search option of its own.
	//
	// This used to force hidden layers OFF and leave the other four at their stock defaults, which
	// made the panel disagree with the dialog it delegates to: a user who had switched Include
	// Hidden Layers ON still got nothing from a hidden layer (user's report 2026-07-28), and the
	// three "include" boxes they had switched OFF were ignored just as silently.
	//
	// fSearchBackwards is deliberately NOT taken: the walk order is the only key joining a search
	// to its replace pass, so KBS always walks forward.
	//
	// Two of the five are FIND-only in InDesign - "there is no option to change in locked stories /
	// on locked layers" (IFindChangeOptions.h:259, 279), which is why the dialog labels them Search
	// Only. They are still taken here, and still handed to the REPLACE walk, because both walks
	// have to visit the same matches in the same order or the walk order the hits were numbered by
	// stops lining up. The distinction is made where it belongs instead: the replace asks
	// IsMatchEditable before it writes and leaves a locked match alone.
	//
	// (Note for anyone reading this as new behaviour: WalkerScopeOptions defaults every switch to
	// kTrue, so locked layers and locked stories were ALREADY inside both walks before this
	// function started reading the dialog. What changed is that the user can now turn them off.)
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

bool KBSSearchEngine::IsMatchEditable(const UIDRef& storyRef, TextIndex pos)
{
	// The frame that decides the layer, resolved exactly as BuildHit resolves it: the frame the
	// match is composed into, or - for an overset match, composed but placed nowhere - the frame
	// carrying the "+" indicator, which is the frame the hit's own locator already names. Resolving
	// it the same way on both sides is what keeps "the row has no check box" and "the replace
	// refuses" describing the same set of hits.
	return IsEditableInFrame(storyRef, KBSSearchEngine::EditableFrameForMatch(storyRef, pos));
}

UID KBSSearchEngine::EditableFrameForMatch(const UIDRef& storyRef, TextIndex pos)
{
	UID frameUID = FrameUIDForPosition(storyRef, pos);
	if (frameUID == kInvalidUID)
	{
		// Overset: composed but placed in no frame, so the frame that speaks for it is the one
		// showing the "+". Only reached for overset matches, so the locator's cost is not paid on
		// the ordinary path.
		const KBSOversetLoc loc = KBSFindOversetLocator(storyRef, pos);
		if (loc.found)
			frameUID = loc.frameUID;
	}
	return frameUID;
}

bool KBSSearchEngine::IsFrameEditable(const UIDRef& storyRef, UID frameUID)
{
	return IsEditableInFrame(storyRef, frameUID);
}

void KBSSearchEngine::SplitLineAroundMatch(const UIDRef& storyRef, TextIndex start, TextIndex end,
	PMString& outPre, PMString& outMatch, PMString& outPost)
{
	outPre.Clear();		outPre.SetTranslatable(kFalse);
	outMatch.Clear();	outMatch.SetTranslatable(kFalse);
	outPost.Clear();	outPost.SetTranslatable(kFalse);

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return;
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;

	// The paragraph that holds the match; excludeEOS (default) trims the paragraph terminator.
	int32 paraLen = 0;
	const TextIndex paraStart = scanner->FindSurroundingParagraph(start, &paraLen);
	if (paraStart < 0 || paraLen <= 0)
		return;

	WideString para;
	scanner->CopyText(paraStart, paraLen, &para);

	// Split by UTF-16 unit, matching TextIndex: the match sits at [start-paraStart, end-paraStart)
	// in the paragraph. GrabUTF16Buffer + SetXString copies exact unit ranges (a match boundary
	// never falls inside a surrogate pair, so no code point is cut).
	// Read straight out of the WideString. GrabUTF16Buffer lives on UnicodeSavvyString, the base
	// PMString and WideString share, so building a PMString first would copy the whole paragraph a
	// second time to reach the same buffer - once per hit, for nothing.
	int32 n = 0;
	const UTF16TextChar* buf = para.GrabUTF16Buffer(&n);
	if (buf == nil || n <= 0)
		return;

	int32 ms = static_cast<int32>(start - paraStart);
	int32 ml = static_cast<int32>(end - start);
	if (ms < 0)		ms = 0;
	if (ms > n)		ms = n;
	if (ml < 0)		ml = 0;
	if (ms + ml > n)	ml = n - ms;

	outPre.SetXString(buf, ms);
	outPre.SetTranslatable(kFalse);
	outMatch.SetXString(buf + ms, ml);
	outMatch.SetTranslatable(kFalse);
	outPost.SetXString(buf + ms + ml, n - ms - ml);
	outPost.SetTranslatable(kFalse);
}

bool KBSSearchEngine::MatchIsSameOccurrence(const UIDRef& storyRef, TextIndex start, TextIndex end,
	UID expectStoryUID, TextIndex expectStart, const PMString& expectMatch, int32 posDelta)
{
	if (storyRef.GetUID() != expectStoryUID)
		return false;

	if (start != expectStart + posDelta)
		return false;

	// Cut the same way the search cut what it stored (a match spanning paragraphs is trimmed
	// identically on both sides), but read ONLY the matched characters - see CopyMatchText. The
	// model holds RAW text; the ellipsizing happens at draw time, so this compares like with like.
	PMString liveMatch;
	KBSSearchEngine::CopyMatchText(storyRef, start, end, liveMatch);
	return liveMatch == expectMatch;
}

void KBSSearchEngine::CopyMatchText(const UIDRef& storyRef, TextIndex start, TextIndex end,
	PMString& outMatch)
{
	outMatch.Clear();
	outMatch.SetTranslatable(kFalse);

	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return;
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;

	// The paragraph is looked up for its END and nothing else: SplitLineAroundMatch cuts its match
	// segment at the paragraph terminator (it splits a string that stops there), so a match running
	// past one has to be cut in the same place here or the two would disagree about it. None of the
	// paragraph is copied - that is the whole point of this function.
	int32 paraLen = 0;
	const TextIndex paraStart = scanner->FindSurroundingParagraph(start, &paraLen);
	if (paraStart < 0 || paraLen <= 0)
		return;

	TextIndex matchEnd = end;
	if (matchEnd > paraStart + paraLen)
		matchEnd = paraStart + paraLen;
	if (matchEnd <= start)
		return;		// nothing left of the match inside this paragraph

	WideString text;
	scanner->CopyText(start, static_cast<int32>(matchEnd - start), &text);
	outMatch = PMString(text);
	outMatch.SetTranslatable(kFalse);
}

int32 KBSSearchEngine::SearchBook(PMString& outSummary, Text::GlyphID overrideFindGlyph)
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
	// ...and the same door for anything ELSE of ours that is running - a replace, or either scan.
	// Asked separately from the line above so each keeps the message that is actually true: what
	// makes a re-entrant call dangerous is two DIFFERENT runs, one of which hands back the chapters
	// the other is walking (see KBSRunGuard).
	if (KBSRunGuard::IsAnyRunning())
	{
		outSummary.Append(KBSRunGuard::BusyMessage());
		return 0;
	}
	const SearchingFlagGuard searchingGuard;

	KBSResultModel::Clear();
	KBSBookScope::ReleaseSearchedBook();	// the two are one fact - see gSearchedBookPath

	// Tabs that search by ATTRIBUTE rather than by text. InDesign walks those with a different walker
	// altogether (kObjectWalkerService / kColorSearchWalkerService), and what they find are page items,
	// not lines of text - so there is nothing for this panel to list, whatever it did with them.
	//
	// Named explicitly because the alternative is worse than useless: their find string IS empty, so
	// without this the panel answered "No Find/Change text set." and sent the user looking for a field
	// they had not left blank (user's question 2026-07-30).
	const int32 tab = CurrentSearchModeValue();
	if (tab == IFindChangeOptions::kObjectSearch || tab == IFindChangeOptions::kColorSearch)
	{
		outSummary.Append("The Find/Change dialog is on the ");
		PMString tabName(SearchModeName(tab));
		tabName.SetTranslatable(kFalse);
		outSummary.Append(tabName);
		outSummary.Append(" tab, which searches objects rather than text. This panel lists text, so use InDesign's own Find/Change for that.");
		return 0;
	}

	// Transliterate is the CJK character-type conversion (Kanji / kana / half- and full-width), and it
	// is a SECOND axis: IFindChangeOptions carries it both as a search mode of its own and as a
	// ChangeMode that sits alongside the tab. SnpFindAndReplace states outright that it "assumes
	// Change is used, however if you want to search based on Japanese character types, you must first
	// change the mode" with kFindChangeModeCmdBoss - a command this panel does not issue.
	//
	// So it is turned away rather than walked. Left to fall through it would be the Glyph bug over
	// again, and quieter: the mode never gets stated, so the walk runs in whatever mode was committed
	// last, while the find string is non-empty often enough to sail past the check below - a result
	// list that looks ordinary and answers a question the user did not ask. (Nothing would be written
	// wrongly - the replace already refuses any results that did not come from Text or GREP - so what
	// is at stake is the truth of the list, not the text.)
	//
	// Refusing is also what keeps the promise this panel is built on: KBS never writes to the user's
	// Find/Change settings. Committing kChange to force the other axis would do exactly that.
	InterfacePtr<IFindChangeOptions> changeModeOpts(QuerySessionPreferences<IFindChangeOptions>());
	if (tab == IFindChangeOptions::kTransliterateSearch
		|| (changeModeOpts != nil && changeModeOpts->GetChangeMode() == IFindChangeOptions::kTransliterate))
	{
		outSummary.Append("Find/Change is set to transliterate character types, which this panel does not search. Switch it back to Text, GREP or Glyph, or use InDesign's own Find/Change.");
		return 0;
	}

	// A caller with its own glyph query - the missing-glyph scan - has nothing on the dialog that
	// could be missing, so HasFindQuery is the wrong question for it. What it does need is the GLYPH
	// TAB: the engine walks in the mode last committed, and a notdef sentinel is a glyph query.
	// Switching the tab on the user's behalf would change a setting they can see and did not touch,
	// so this says what to do and stops instead.
	if (overrideFindGlyph != kInvalidGlyphID)
	{
		if (tab != IFindChangeOptions::kGlyphSearch)
		{
			outSummary.Append("Set Edit > Find/Change to the Glyph tab first, then run this again.");
			return 0;
		}
	}
	else if (!HasFindQuery())
	{
		// Which tab, so this reads as "nothing set on THIS tab" - each one keeps its own query, so a
		// query on another tab is no help and saying so avoids a hunt. The Glyph tab is worded for
		// what it actually wants: a glyph picked from its grid, not text typed into a field.
		const bool glyphTab = (tab == IFindChangeOptions::kGlyphSearch);
		outSummary.Append(glyphTab ? "No glyph set on the " : "No search text set on the ");
		PMString tabName(SearchModeName(tab));
		tabName.SetTranslatable(kFalse);
		outSummary.Append(tabName);
		outSummary.Append(glyphTab
			? " tab. Choose the glyph to find in Edit > Find/Change, then run the search from the panel flyout."
			: " tab. Type what to find in Edit > Find/Change, then run the search from the panel flyout.");
		return 0;
	}

	// State the tab before anything is walked. A walk runs in the mode last COMMITTED through
	// kFindSearchModeCmdBoss - not in the one IFindChangeOptions merely reports - so without this a
	// search driven from this panel ran as plain Text whatever tab was on screen. See
	// KBSSearchEngine::CommitSearchMode.
	KBSSearchEngine::CommitSearchMode(overrideFindGlyph);

	// Resolve the scope from the Book Scope toggle. NO implicit fallback: ON means the book and
	// nothing else, OFF means the front document and nothing else - so the status line can always
	// state exactly what was searched, and a missing book is reported instead of quietly searching
	// one document behind the user's back.
	std::vector<KBSBookScope::ChapterDoc> targets;
	PMString bookName;
	// Chapters the book could not hand over at all. Declared out here so the summary can name them
	// whichever way this run ends - including the "no matches" and "nothing openable" exits, where
	// they are the only thing that explains what happened.
	std::vector<KBSBookScope::SkippedChapter> unopenable;
	const bool fromBook = KBSBookScope::IsBookScopeOn();
	if (fromBook)
	{
		if (!KBSBookScope::HasActiveBook())
		{
			outSummary.Append("Book Scope is on, but no book is open.");
			return 0;
		}
		// Listed, not opened: each chapter is opened when its turn comes in the loop below and
		// handed straight back once it has been walked, so a book search never holds more than one
		// chapter of its own. Whether a chapter can actually be opened is not known yet - the
		// summary reports the ones that could not, after the walk.
		if (!KBSBookScope::ListBookChapters(targets, bookName) || targets.empty())
		{
			outSummary.Append("The active book has no chapters.");
			return 0;
		}
	}
	else
	{
		IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
		if (doc == nil)
		{
			outSummary.Append("No open document to search.");
			return 0;
		}
		KBSBookScope::ChapterDoc single;
		single.docRef = ::GetUIDRef(doc);
		doc->GetName(single.shortName);
		single.shortName.SetTranslatable(kFalse);
		targets.push_back(single);
	}

	// Record the scope ON THE RESULTS (KBSResultModel::Clear above wiped the previous value): the
	// tree reads it to decide whether the chapter rows come up collapsed, and reading it from here
	// rather than from the toggle keeps an existing result set's display stable if the user flips
	// Book Scope afterwards.
	KBSResultModel::SetFromBook(fromBook);

	// ...and that a search HAPPENED, which the panel's illustration follows. Said separately from
	// the two lines around it because it survives finding nothing: a search that returned no hits
	// has still been run.
	KBSResultModel::NoteRun();

	// ...and WHICH book, for the tree's book row. Empty for a document search, which has no book
	// row at all. This is the panel's permanent answer to "what am I looking at": a status line is
	// one line, gets truncated, and is overwritten by the next message.
	KBSResultModel::SetBookName(bookName);

	// ...and WHICH TAB was searched. The replace pass re-walks each chapter, and a re-walk in another
	// mode returns another set of matches - so Change Checked compares this against the tab in force
	// then and refuses rather than lining rows up with the wrong occurrences.
	KBSResultModel::SetSearchMode(CurrentSearchModeValue());

	// ...and WHAT WAS ASKED FOR, for the heading of the file "Save Results..." writes. Recorded here,
	// beside the tab, because both answers have the same lifetime: they describe THESE rows, and the
	// user is free to retype the query the moment this search returns.
	KBSResultModel::SetQueryText(DescribeCurrentQuery());

	// Walk every target; only chapters that hold a hit go into the model (no empty branches). The
	// model was cleared above; each chapter is APPENDED as it finishes and the panel is refreshed
	// right then, so the tree grows chapter by chapter instead of appearing all at once at the end.
	// The progress bar. Shown for BOTH scopes since 2026-07-31 (user's request). It used to be book
	// scope only, on the reasoning that a one-document search is a single step with nothing to
	// cancel between - but that reasoning was about CHAPTERS, and the bar is sized in stories: a
	// single document with a lot of them takes just as long and had no way to be stopped.
	//
	// DisableChildProgressBars stops anything the walk runs into from putting up a bar of its own.
	// Since 2026-08-02 that covers the windowless chapter opens as well: chapters are opened inside
	// the loop below, not before this bar exists.
	//
	// EQUAL SLICES PER CHAPTER, subdivided by stories inside each one. The slices have to be equal
	// because a chapter's size cannot be asked for before it is opened, and chapters are opened one
	// at a time now. Within a chapter the story count comes from IStoryList and costs nothing, and
	// each story is subdivided by how far into its text the walk has got (CollectHitsInDoc does the
	// moving) - which is what keeps the bar alive through a chapter of a hundred stories.
	//
	// Why not simply ask the walker how far it is: ITextWalkerProgressMonitor is only a place to PARK
	// a bar - the CLIENT's own OnNextPosition is what has to call SetPosition on it, and the stock
	// kFindChangeClientBoss does not (measured 2026-07-31: it registered fine and then never called
	// once in 5270 matches). spellpanel gets a moving bar there because it walks with a client it
	// wrote itself. A plug-in using the stock find/change client has to count its own progress.
	//
	// showImmediate = kTrue: a book search ALWAYS puts the bar up. The default (kFalse) makes the bar
	// wait out an internal delay first, and the search beat that delay even at 5000+ hits (measured
	// 2026-07-27) - so the one thing the bar is really there for, the cancel button, was never on
	// screen. Better a brief flash on a fast book than a search that cannot be stopped.
	const int32 progressTotal = static_cast<int32>(targets.size()) * kKBSChapterProgressSpan;

	// The title names the scope, because the bar no longer implies it: "Searching book..." when it
	// really is a book, plain "Searching..." for a single document.
	PMString progressTitle(fromBook ? "Searching book..." : "Searching...");
	progressTitle.SetTranslatable(kFalse);
	RangeProgressBar progressBar(progressTitle, 0, progressTotal, kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	// Where the bar stands as each chapter starts, and how far it has actually been advanced (DoTask
	// takes a difference, so the position already reported has to be carried along).
	int32 progressBase = 0;
	int32 progressReported = 0;

	int32 total = 0;
	int32 chaptersWithHits = 0;
	bool collectionTruncated = false;
	bool cancelled = false;
	// Chapters that could not be searched at all. Counted separately from "searched, no hits":
	// the summary has to be able to say a chapter was skipped, or a book search silently returns
	// fewer chapters than the book has and looks like the chapters simply held no matches.
	int32 unsearchableCount = 0;
	PMString unsearchableName;
	ChapterWalkResult unsearchableReason = kChapterWalked;
	// A separate flag rather than unsearchableName.IsEmpty(): a chapter whose name is empty would
	// otherwise never count as "the first one", and every later chapter would overwrite the reason.
	// (Same trap the replace engine's firstSkipped already sidesteps.)
	bool haveFirstUnsearchable = false;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		// "Chapter 3 / 12" over the chapter's own name, called BEFORE the chapter is walked so the
		// bar names what is being worked on rather than what has just finished. This is also what
		// keeps the bar moving through chapters that hold no hits at all.
		PMString taskLine;
		taskLine.SetTranslatable(kFalse);
		taskLine.Append("Chapter ");
		taskLine.AppendNumber(static_cast<int32>(i) + 1);
		taskLine.Append(" / ");
		taskLine.AppendNumber(static_cast<int32>(targets.size()));
		taskLine.Append(" - ");
		taskLine.Append(targets[i].shortName);
		// The chapter's name rides on the status line WITH its number. The bar's POSITION is moved
		// separately, through KBSAdvanceProgress - SetTaskText only writes text.
		progressBar.SetTaskText(taskLine);
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Cancel is asked here, and answered by the DoTask calls inside the walk. WasCancelled only
		// reads a flag; what SETS that flag is the event queue being pumped, which DoTask does and
		// SetPosition does not - a bar driven by SetPosition alone looked perfect and could not be
		// cancelled at all (measured 2026-07-31).
		// kFalse = do NOT raise the global error state: it would outlive the search and fail the
		// commands that come after it.
		if (progressBar.WasCancelled(kFalse))
		{
			cancelled = true;
			break;
		}

		// Room left under the whole-search safety ceiling; once it is gone, stop walking further
		// chapters too (the result set is full).
		const int32 remaining = kKBSCollectHitLimit - total;
		if (remaining <= 0)
		{
			collectionTruncated = true;
			break;
		}

		// Open THIS chapter now. Book scope only - a document-scope target is the front document,
		// which is already open and never ours to close.
		if (fromBook && targets[i].docRef == UIDRef::gNull)
		{
			if (!KBSBookScope::OpenChapterDoc(targets[i], &unopenable))
			{
				// The reason is recorded; AppendUnopenableNote names it in the summary. Hand the
				// bar on all the same, so a book whose chapters will not open still fills it.
				progressBase += kKBSChapterProgressSpan;
				KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);
				continue;
			}
		}

		const UIDRef chapterDocRef = targets[i].docRef;

		// Now that it is open, ask how many stories this chapter's slice of the bar has to be
		// divided between. At least one, so a chapter with no stories at all still moves the bar.
		int32 storiesInDoc = CountSearchableStories(chapterDocRef);
		if (storiesInDoc < 1)
			storiesInDoc = 1;

		std::vector<KBSResultModel::Hit> hits;
		bool docCapped = false;
		ChapterWalkResult walkResult = kChapterWalked;
		CollectHitsInDoc(chapterDocRef, static_cast<size_t>(remaining), hits, docCapped, walkResult,
			&progressBar, progressBase, kKBSChapterProgressSpan, storiesInDoc, progressReported);

		// This chapter is done, whatever it found: put the bar exactly where the next one starts, so
		// a chapter whose stories the walk left early still hands the bar on at the right place.
		progressBase += kKBSChapterProgressSpan;
		KBSAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// ***** Hand the chapter back HERE, before any of the continues below can skip it. *****
		// The walk is over and what it produced is plain data - UIDs and text indices, which survive
		// the document being closed (measured 2026-07-17). A jump or a replace that needs this
		// chapter later reopens it through ReopenChapterDoc, the path that already existed for
		// chapters the user closed by hand.
		//
		// Only chapters KBS opened are closed: ReleaseHeldDoc checks the held list itself, so one
		// the user already had open passes through untouched.
		KBSBookScope::ReleaseHeldDoc(chapterDocRef);

		if (docCapped)
			collectionTruncated = true;
		if (walkResult != kChapterWalked)
		{
			// This chapter was never actually searched. Keep the first one's name so the summary
			// can say which chapter and why - a skipped chapter is indistinguishable from a chapter
			// with no matches otherwise, and that is what made this hard to see.
			++unsearchableCount;
			if (!haveFirstUnsearchable)
			{
				unsearchableName = targets[i].shortName;
				unsearchableName.SetTranslatable(kFalse);
				unsearchableReason = walkResult;
				haveFirstUnsearchable = true;
			}
			continue;
		}
		if (hits.empty())
			continue;

		// Page-order the hits and bake the "P<page>(<n>) " locator onto each line. This needs the
		// WHOLE chapter's hits (page order and the within-page ordinal are only known once the
		// chapter is complete), which is why the flush unit is the chapter, not a fixed hit count.
		FinalizeChapterHits(hits);

		KBSResultModel::Chapter chapter;
		chapter.name = targets[i].shortName;
		chapter.name.SetTranslatable(kFalse);
		chapter.docRef = targets[i].docRef;
		chapter.file = targets[i].file;
		chapter.hits.swap(hits);
		const int32 chapterHitCount = static_cast<int32>(chapter.hits.size());
		total += chapterHitCount;
		++chaptersWithHits;

		// Into the model only. The tree is drawn ONCE, by the caller, when the search returns: the
		// progress bar is modal, so while it is up the panel cannot be read or clicked and a
		// per-chapter rebuild would be work nobody sees. (Growing the tree chapter by chapter used
		// to be how the search showed it was alive; the bar does that now, and does it for chapters
		// that hold no hits at all - which the growing tree never could.)
		KBSResultModel::AppendChapter(chapter);
	}

	// ASK ONCE MORE, now that the loop is over. The test inside the loop sits at the TOP of each
	// pass, so a cancel pressed while the LAST chapter was being walked had no next pass to be seen
	// in, and the search finished as though the button had never been touched. Same fault as the
	// replace engine's - see the matching comment there.
	if (!cancelled && progressBar.WasCancelled(kFalse))
		cancelled = true;

	// Cancelled: throw the half-finished result away rather than leave a partial list looking like a
	// complete one, and give the chapters back - the results that would have needed them are gone.
	// ReleaseHeldDocs schedules its closes, so it is safe to call from in here.
	if (cancelled)
	{
		KBSResultModel::Clear();
		KBSBookScope::ReleaseSearchedBook();	// closes the chapters AND forgets the book
		outSummary.Clear();
		outSummary.SetTranslatable(kFalse);
		outSummary.Append("Search cancelled.");
		return 0;
	}

	// Every chapter KBS opened has already been handed back inside the loop - a book search leaves
	// nothing of its own open, and no .indd locked. The rows carry their chapter's file, so a jump
	// or a replace reopens whatever it needs (KBSJump::EnsureChapterReachable and the replace
	// engine's own reopen). What that costs is a document load on the first click into a chapter;
	// what it buys is that searching a book no longer leaves twenty hidden documents behind.

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
		else
		{
			outSummary.Append(" in document \"");
			outSummary.Append(targets[0].shortName);
			outSummary.Append("\".");
		}
		// "No matches" is a lie if a chapter was skipped rather than searched - say so here too.
		AppendUnsearchableNote(outSummary, unsearchableCount, unsearchableName, unsearchableReason);
		KBSBookScope::AppendUnopenableNote(outSummary, unopenable);
		return 0;
	}

	// The one-line summary. The hit count leads, so it stays visible even when the narrow
	// single-line status field truncates the tail.
	//
	// ***** No name here. ***** The book and the document are the first two rows of the tree
	// directly below this line, so repeating them costs room in a field that truncates and says
	// nothing the eye has not already got (user's call, 2026-08-03). The "no matches" wording DOES
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
	else
	{
		outSummary.Append(".");
	}

	// Two separate caps can bite:
	//   * collectionTruncated: the whole-search safety ceiling stopped collection, so the RESULT SET
	//     itself is capped (a future export would be incomplete too) - the strong "narrow it" note.
	//   * total > display limit: every hit is stored, but the panel shows only the first N rows.
	if (collectionTruncated)
	{
		outSummary.Append(" Stopped at the ");
		outSummary.AppendNumber(kKBSCollectHitLimit);
		outSummary.Append(" safety limit - narrow your search. Showing first ");
		outSummary.AppendNumber(KBSResultModel::kKBSDisplayHitLimit);
		outSummary.Append(" in the panel.");
	}
	else if (total > KBSResultModel::kKBSDisplayHitLimit)
	{
		outSummary.Append(" Showing first ");
		outSummary.AppendNumber(KBSResultModel::kKBSDisplayHitLimit);
		outSummary.Append(" in the panel.");
	}

	AppendUnsearchableNote(outSummary, unsearchableCount, unsearchableName, unsearchableReason);
	KBSBookScope::AppendUnopenableNote(outSummary, unopenable);

	// Where the commands are. Check All / Uncheck All live on the ROWS' right-click menu - they moved
	// off the panel flyout on 2026-08-01, because a flyout has no row to ask about and those two have
	// to know whether they mean the whole book or one chapter. Nothing on screen said so, which left
	// the one command that turns a result list into a work list undiscoverable (user's request,
	// 2026-08-03).
	//
	// Last, after the warnings: it is an offer, not something that went wrong, and the status field
	// truncates its tail when it has to.
	outSummary.Append(" Right-click the book or a document row for a menu.");
	return total;
}

bool KBSSearchEngine::IsSearching()
{
	return gSearching;
}

// End, KBSSearchEngine.cpp.
