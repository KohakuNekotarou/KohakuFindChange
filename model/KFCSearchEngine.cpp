//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Search engine implementation. See KFCSearchEngine.h for the contract. The walker loop is
//  ported from KESCL's CollectMatches (KESCL left untouched); the one change is the point of
//  this plugin: the walk does NOT set the find string or pin the search mode - it walks with the
//  user's current Find/Change options as they are, so the mode (Text or GREP) is followed (what KFC
//  does write to the panel, and when: the head of KFCSearchEngine.h).
//
//  For each match the finder hands back (story, start, end); BuildHit then reads the containing
//  paragraph (IComposeScanner::FindSurroundingParagraph + CopyText) and splits it into the three
//  segments the colour cell paints. The offsets are TextIndex throughout, and a TextIndex counts
//  CODE POINTS - a surrogate pair is ONE, the same unit WideString::CharCount counts in (measured
//  on the running application). So no boundary can land inside a pair. A character the reader sees
//  can still be MORE than one code point - a kanji with its variation selector (an IVS), a kana with
//  a combining voiced mark - and the cuts the line budget makes keep such a character whole, marks
//  and all (KFCSafeCut, KFCTextCut.h - in SplitLineWithScanner). A match's own ends are the finder's
//  and are never moved.
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
#include "IIntData.h"				// kFindSearchModeCmdBoss carries two of these - see CreateFindChangeIntCmd
#include "IStringData.h"			// kFindStringCmdBoss's string - SetQuery
#include "IBoolData.h"				// kFindChangeGlyphIDCmdBoss: which side of the glyph search is being set
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ITextModel.h"
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"
#include "ITextFocusList.h"			// a searched part walked again: a focus is its own focus list (QueryPartScope)
#include "ITextFocusManager.h"		// the focus that walk is given (TempFocus)
#include "ISession.h"				// GetExecutionContextSession
#include "IStoryList.h"				// GetUserAccessibleStoryCount - how many stories a walk will visit
// For the frame a match is composed into (FrameUIDForPosition - what the frame says, its page, its layer and its
// locks, is KFCPageItemFacts'):
#include "ITextParcelList.h"		// GetParcelContaining - the parcel a position is in; GetFirstDamagedParcel
#include "ITextParcelListComposer.h"	// RecomposeThruTextIndex - that parcel list composed up to the position (ComposeThrough)
#include "IParcelList.h"			// GetParcelFrameUID - is that parcel placed in a frame?
#include "ParcelKey.h"				// ParcelKey::IsValid
// For refusing to rewrite locked text the way the Find/Change dialog does ("Search Only"):
#include "IItemLockData.h"			// GetInsertLock - the story's own "content cannot be edited"

// General includes:
#include "AttributeBossList.h"		// the Find Format list the search remembers (RememberFindFormat)
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss, kFindChangeClientBoss, kTextWalkerService(...)
#include "CTextEnum.h"				// Text::GlyphID / kInvalidGlyphID (the Glyph tab's query)
#include "TextChar.h"				// kTextChar_Ellipse - the mark a cut segment carries (SplitLineWithScanner)
#include "WalkerScopeOptions.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
#include "KFCProgressBar.h"		// the search's progress + cancel (both scopes) - the bar is the UI half's
#include "CmdUtils.h"
#include "CreateObject.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "PersistUtils.h"			// ::GetDataBase - every open document's, for a Search: walk's dirty guards
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "Utils.h"
#include "WideString.h"
#include "textiterator.h"			// ReadText - the official one-call read of a range

#include <vector>
#include <utility>					// std::move - a finished chapter is handed to the model, not copied
#include <algorithm>				// std::sort, std::equal - a row looked for again (its places, its look-alike rows)
#include "FileUtils.h"				// IsEqual - the row eye's carry: a chapter's document by its file
#include <map>						// the per-frame cache one document's walk keeps (FrameFacts)
#include <memory>					// std::unique_ptr - a Search: walk's dirty guards, one per open document
#include <set>						// the rows a write is handed, the stories it meets

// Project includes:
#include "KFCSearchEngine.h"
#include "KFCPageItemFacts.h"	// the page-item questions (shared with the object search)
#include "KFCBookScope.h"
#include "KFCResultModel.h"
#include "KFCRowFoci.h"			// the rows' text foci - attached when a search ends, read and put back by LocateRow
#include "KFCRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KFCOversetLocator.h"	// the "+" page for an overset hit (locator + sort key)
#include "KFCReplaceEngine.h"	// QueryUnchangedSinceSearch - a look again runs only under the search's own query
#include "KFCDiag.h"			// the fault switch queries-run (SearchBook's head)
#include "KFCDiagCommands.h"	// the test build's command count (KFC_DIAG_COMMANDS)
#include "KFCQuerySequence.h"	// RunFromDiagSwitch - the test build's way into the query run
#include "KFCChangeAll.h"		// CommandName - the limit's note names Change All in Book (No List) (F18)
#include "KFCObjectSearch.h"	// the Object tab's listing, its Search: and its query
#include "KFCTextCut.h"			// KFCSafeCut - where a row's line may be cut (the rule the panel's own cuts follow)
#include "KFCFindChangeCmds.h"	// the find/change settings' commands, one at a time
#include "KFCQueryCopy.h"		// the query copied in memory - onto each new list (the tier switch); the spike (DiagProbe)
#include "KFCTierStore.h"		// ParkCurrent - the list a search replaces, kept as a tier

namespace
{

// (The limit is kKFCCollectHitLimit, in KFCResultModel.h - the number the panel draws (spec F9). Read the contract
// there; this file uses it in SearchBook.)

// THE SEARCH'S BAR (KFCProgressBar.h - the UI half's), defined below with the direction scopes. The search's alone:
// a row's Replace has no bar, and Change All in Book and the query run move theirs directly.
//
// Move the bar to an absolute position. ioReported is the position already sent, so advances too small to be worth
// a repaint can be swallowed; force = true where the bar must land exactly, such as a chapter boundary.
// NOTE: this does NOT make the run cancellable, and neither does any other way of moving the bar (measured both
// ways). WasCancelled has to be ASKED, and asking it only inside the chapter loop misses a cancel pressed during
// the last chapter - see the ask-once-more test that follows the loop in SearchBook.
void KFCAdvanceProgress(KFCProgressBar* bar, int32& ioReported, int32 target, bool force = false);

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
	// story (the author's call: story by story). The run is thrown away, so nothing about this ending is
	// said.
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
// attribute list, taken when the search ran and compared before a row's Replace re-walks. See
// RememberFindFormat for why the list is COPIED rather than described.
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
// FindFormatHasChanged. A raw address, so both are let go if that database closes
// (ForgetSearchedFindFormatOf): the address can be handed to the next document opened.
// Measured (a test build's FINDFORMAT lines): it was NOT a document's - the same database for
// four documents in turn, outliving each, with the style given by name or as a document's style
// (which the options keep by name) - and not the options' own either. So that forget has not been
// seen to fire; it stays for any route that would hand this a document's database.
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

// The find/change settings' commands, one at a time (KFCFindChangeCmds - KFCQueryCopy states them the same way).
using KFCFindChangeCmds::ProcessFindChangeCmd;
using KFCFindChangeCmds::CommitGlyphID;
using KFCFindChangeCmds::CreateFindChangeIntCmd;
using KFCFindChangeCmds::CommitFindChangeInt;
using KFCFindChangeCmds::ScheduleFindChangeInt;

// THE PAGE-ITEM QUESTIONS - KFCPageItemFacts (the object search asks them too).
using KFCPageItemFacts::GetFramePageString;
using KFCPageItemFacts::IsFrameHidden;
using KFCPageItemFacts::IsFrameOnLockedLayer;
using KFCPageItemFacts::IsPageItemLockedForEdit;
using KFCPageItemFacts::StoryLeadText;

// THE COMPOSITION UP TO A POSITION, BROUGHT UP TO DATE BEFORE IT IS READ. The parcels answer from the last composition, and
// text edited since - typed just before a search, or written by KFC itself just before the next row's lock test - is
// composed only when something asks for it: InDesign's background composition is an idle task, and a windowless chapter
// has no view to want it. The find on the session's shared walker - the one every KFC walk runs on (AcquireWalker) -
// composes the text it finds, a frame at a time (measured - the speed-up study, see AcquireWalker; why is not known - not
// by selecting it: the find does not select what it finds, the SDK's snippet selects it itself after the find,
// SnpFindAndReplace.cpp:798-807). A walker of one's own does not - and while KFC walked one (now a test build's fault
// switch, own-walker), a row read its page, its "overset" and its locks off the composition from before the edit. So, as
// the reading's own guard, the position's parcels are composed first when any of them is damaged - nothing to do where
// the find composed them.
// THE OFFICIAL SHAPE: composed through the parcel list the position is then read from - a table cell's or a footnote's as
// well as the frames' (SnpTextModelHelper::QueryParcelContaining, :467-484: GetFirstDamagedParcel, then
// ITextParcelListComposer), only as far as the position (the guide, sol-03 "Recomposing text": the parcel list
// composer's RecomposeThruTextIndex is "the most general approach", for text "displayed in a frame or a table cell") -
// not the story to its end per row a Replace writes. NOT THE FRAME LIST'S COMPOSER: its TextIndex is the primary
// thread's alone (the guide's own example guards it with GetPrimaryStoryThreadSpan; ITextParcelListComposer.h, the
// TextIndex "MUST be part of the TextStoryThread mapped to the ParcelList"), and a table cell's position handed to the
// frame list brought InDesign down in KCM (IFrameList::QueryFrameContaining, twice). A note's or a deletion's
// parcel list composes nothing: text "never composed for display" (the same section), its parcel interfaces empty
// implementations (the guide's figures on Track Changes and on InCopy notes, vol2 chapters 3 and 20).
// Every caller holds a SaveRestoreModifiedState guard or is writing anyway (composing dirties a document).
void ComposeThrough(ITextModel* textModel, ITextParcelList* tpl, TextIndex pos)
{
	// (Fault switch frame-read-uncomposed, a test build's only - KFCDiag.h: the parcels read as they stand, nothing composed
	// first - the old way.)
	if (KFC_DIAG_FAULT("frame-read-uncomposed"))
		return;
	// A position the text model holds (the guide's check), in a parcel list with damage (the snippet's).
	if (pos < 0 || pos >= textModel->TotalLength() || !tpl->GetFirstDamagedParcel().IsValid())
		return;
	InterfacePtr<ITextParcelListComposer> composer(tpl, UseDefaultIID());
	if (composer != nil)
		composer->RecomposeThruTextIndex(pos);
}

// The frame a text position is composed into: position -> parcel -> frame, from the composition as it stands now
// (ComposeThrough). kInvalidUID for an overset position (composed but placed in no frame) and for the query failures
// around it, which read the same to every caller: this position has no frame of its own.
UID FrameUIDForPosition(const UIDRef& storyRef, TextIndex pos)
{
	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return kInvalidUID;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return kInvalidUID;
	ComposeThrough(textModel, tpl, pos);
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
// No cut falls in front of a mark that belongs to the character before it (KFCSafeCut, KFCTextCut.h):
// the cut moves back in front of that character - the pre then keeps it, a character or two over its
// share, and the post and a capped match leave it out - so a line can come out a character or two
// either side of fifty.
//
// NOT THE MATCH'S OWN TEST. The same-occurrence test reads none of the three segments - it compares the
// match WHOLE, through a hash taken with no cap at all (HashMatchText) - so a row clipped for drawing
// can never cost a replace. (RowReadsAsFound compares the segments too, as the line around the match -
// read again by this same budget, so like is compared with like.)
const int32 kKFCMaxLineChars = 50;

// How far back a cut pre looks for the character a mark at its head belongs to (SplitLineWithScanner). Real
// text gives a character one mark or a few - an IVS is one; a character carrying more than this is cut inside
// its marks, where the look stopped.
const int32 kKFCMarkLookBack = 32;

// (The cut mark is the SDK's kTextChar_Ellipse, TextChar.h:188 - what the product uses for this very
//  job: ConditionalTextTips.cpp:117-126 reads a story through IComposeScanner::CopyText, stops at a
//  character limit, and appends kTextChar_Ellipse if it stopped early. A named constant also keeps the
//  file ASCII: a bare UTF-8 symbol in it is garbled to CP932 noise.)

// Where the drawn match stops - before SplitLineWithScanner looks at what stands at the cut. Capped at
// the SAME number the whole line is budgeted with: a match that long owns the entire line, contexts and
// all.
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
// them hashed does not have to copy them out of the story a second time. It is the CAPPED match -
// the characters drawn, never the one read past the cap to look at it - and NEVER carries the cut
// mark (the mark goes on the drawn PMString alone - a mark in here would hash a character the
// document does not hold): equal to (end - start) characters only when the match fitted inside
// kKFCMaxLineChars, which is exactly the test a caller has to make before hashing it.
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
	// full, so a user who replaced that row would lose text that was never on screen (measured: two
	// paragraphs and the break between them replaced by one word, joining what was left to the
	// paragraph below). The breaks inside the match are drawn as marks - a pilcrow for CR, a return
	// arrow for a forced line break (KFCResultModel::MarkUpBreaksForDisplay).
	TextIndex matchEnd = KFCCapMatchEnd(start, end);
	const bool capped = (matchEnd != end);

	// ----- the match first: it owns the line budget -----
	if (matchEnd > start)
	{
		// Read into the caller's buffer, so the hash can be taken from these very characters. A CAPPED
		// match is read one character past the cap, to see what stands at the cut: a mark that belongs to
		// the character before it (an IVS, a combining voiced mark) moves the cut back in front of that
		// character (KFCSafeCut), which goes on, marks and all, with the part of the match not drawn. The
		// buffer is then cut to what is drawn.
		scanner->CopyText(start, static_cast<int32>(matchEnd - start) + (capped ? 1 : 0), &outMatchWide);
		outMatch = PMString(outMatchWide);
		if (capped)
		{
			int32 keep = KFCSafeCut(outMatch, kKFCMaxLineChars);
			if (keep <= 0)
				keep = kKFCMaxLineChars;	// one character carrying the whole budget in marks: cut inside them rather than draw no match
			if (keep < outMatch.CharCount())
			{
				outMatch.Remove(keep, kMaxInt32);		// kMaxInt32: everything from keep on
				outMatchWide.Remove(keep, kMaxInt32);
			}
			matchEnd = start + keep;
		}
		outMatch.SetTranslatable(kFalse);
	}
	// A capped match was CUT, and the row says so. The mark goes on the drawn string ONLY, never
	// into outMatchWide - that buffer is the hash shortcut's material and has to stay exactly the
	// characters read. It is drawn in the match's own colour, which is the truth being told:
	// what follows the mark is more MATCH.
	if (capped)
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
	// the rest of the match as though it were text lying outside it. (The budget below is zero for a
	// capped match as well - but the reason is kept apart from the arithmetic: it is about what a post
	// MEANS, not about how much room is left.)
	int32 postAvail = 0;
	if (!capped)
	{
		int32 endParaLen = 0;
		const TextIndex probe = (matchEnd > start) ? matchEnd - 1 : start;
		const TextIndex endParaStart = scanner->FindSurroundingParagraph(probe, &endParaLen);
		if (endParaStart >= 0 && endParaLen > 0 && endParaStart + endParaLen > matchEnd)
			postAvail = static_cast<int32>(endParaStart + endParaLen - matchEnd);
	}

	// A capped match owns the whole line - said here, not left to the arithmetic: a match capped in front
	// of a mark (above) stops a character or two short of the budget, which would hand that much to the pre.
	int32 remaining = kKFCMaxLineChars - static_cast<int32>(matchEnd - start);
	if (capped || remaining < 0)
		remaining = 0;
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
		// The cut at its head is a TextIndex, so it cannot land inside a surrogate pair (the note at the
		// head of this file) - but it can land on a mark that belongs to the character before it, and
		// then it moves back in front of that character (KFCSafeCut): the character comes along with its
		// marks. Looked into only when the first character read IS such a mark - then the few characters
		// before the cut are read as well, to find the one it belongs to.
		TextIndex preFrom = start - preBudget;
		WideString w;
		scanner->CopyText(preFrom, preBudget, &w);
		PMString pre(w);
		if (preFrom > paraStart && pre.CharCount() > 0 && KFCIsAttachedMark(pre.GetWChar(0)))
		{
			const TextIndex from = (preFrom - paraStart > kKFCMarkLookBack) ? preFrom - kKFCMarkLookBack : paraStart;
			WideString wider;
			scanner->CopyText(from, static_cast<int32>(start - from), &wider);
			pre = PMString(wider);
			const int32 cut = KFCSafeCut(pre, static_cast<int32>(preFrom - from));
			pre.Remove(0, cut);
			preFrom = from + cut;
		}
		if (preFrom > paraStart)
			outPre.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));	// cut at its head, marked at its head
		outPre.Append(pre);
		outPre.SetTranslatable(kFalse);
	}

	if (postBudget > 0)
	{
		// The HEAD is kept here - the mirror of the pre above, for the mirrored reason. A post that is cut
		// is read one character past its budget, for the capped match's look at what stands at the cut: a
		// mark there moves the cut back in front of its character, which goes with the part not drawn.
		const bool postCut = (postBudget < postAvail);
		WideString p;
		scanner->CopyText(matchEnd, postBudget + (postCut ? 1 : 0), &p);
		PMString post(p);
		if (postCut)
		{
			const int32 keep = KFCSafeCut(post, postBudget);
			if (keep < post.CharCount())
				post.Remove(keep, kMaxInt32);
		}
		outPost.Append(post);
		if (postCut)
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

// How much of each match BuildHit fills in. A walk costs the same whatever is asked for; what differs
// is how much is then read about every match it lands on.
enum HitDetail
{
	kHitPlaceAndText,	// story, range, the three drawn segments and the hash - what finding a row
						// again compares (CollectStoryHits)
	kHitEverything		// ...and the page, the overset / hidden / locked flags and the story's first
						// words - a search's row
};

// Fill a hit from one match (story, [start, end)), as far as 'detail' asks: its jump anchors and the
// containing paragraph's text split into (before / matched / after) with the hash of the whole match;
// then the page, the flags and the story's first words. The offsets are CODE POINTS, not UTF-16
// units - see the note at the head of this file.
#ifdef KFC_DIAG
// TEST BUILDS ONLY (docs/ai-notes/kfc-speedup-ideas-2026-10-05.md): where BuildHit spends its time over one
// walk - the line's text, the story's lead, the place (frame, page, overset). Reset and written by CollectHitsInDoc
// (SEARCHTIME).
struct BuildHitTimes
{
	double text, lead, place;
	BuildHitTimes() : text(0), lead(0), place(0) {}
};
BuildHitTimes gBuildHitTimes;
#endif

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
	KFC_CLOCK(cText);
	ReadHitText(storyRef, start, end, outHit);
	KFC_SPENT(gBuildHitTimes.text, cText);
	if (detail == kHitPlaceAndText)
		return;

	// The story's first words, for its row in the tree - read once per story per walk.
	KFC_CLOCK(cLead);
	std::map<UID, PMString>::const_iterator lead = cache.storyLeads.find(outHit.storyUID);
	if (lead == cache.storyLeads.end())
		lead = cache.storyLeads.insert(std::make_pair(outHit.storyUID, StoryLeadText(storyRef))).first;
	outHit.storyLead = lead->second;
	KFC_SPENT(gBuildHitTimes.lead, cLead);
	KFC_CLOCK(cPlace);

	// The frame this match composes into. A POSITION question, so it is asked per hit; everything
	// that follows from the frame comes out of the cache.
	const UID matchFrameUID = FrameUIDForPosition(storyRef, start);
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
		const KFCOversetLoc loc = KFCFindOversetLocator(storyRef, start);
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
	// way to change it. Decided HERE, once, so the row's Replace is greyed (KFCReplaceEngine::CanReplaceHit)
	// instead of offering a write that would quietly do nothing.
	outHit.isLocked = facts->isLocked;
	KFC_SPENT(gBuildHitTimes.place, cPlace);
}

// ---------------------------------------------------------------------------------------------------------------------
// THE PART OF A STORY A SEARCH OVER PART OF A STORY WALKS (Search: To End of Story / Selection) - kept for Search This
// Story Again (the author's idea: "keep where the search started with a text focus").
//
// MEASURED FIRST (a test build's walks - docs/ai-notes/kfc-story-search-again-part-2026-10-09.md): the scope
// InDesign builds for these holds the part as STRETCHES OF TEXT, IN THE ORDER ITS WALKER TAKES THEM - one for plain text
// ([7,31) from a text cursor to the story's end, [7,20) a selection), and around a table one up to its anchor, one for
// its cells, one after it ([7,16) [33,52) [16,33)); table cells selected over two rows are two stretches with a cell
// between them ([22,33) [42,53)). The factory's own forms build the same stretches from the part's start (and end) -
// QueryPartScope - which is what lets the part be walked again on its own; whether they do for this search is checked
// against InDesign's stretches when it ends (MakeSearchedRange).

// One stretch of a walk scope.
struct ScopePiece
{
	IDataBase*	db;
	UID			story;
	TextIndex	start;
	TextIndex	end;
	ScopePiece() : db(nil), story(kInvalidUID), start(kInvalidTextIndex), end(kInvalidTextIndex) {}
};
const int32 kMaxScopePieces = 10000;

// THE STRETCHES A WALK SCOPE HOLDS, in its walker's order: a cursor of the scope's own stepped through it
// (ITextWalkerScope::GetNewCursor, MoveToNextRange / MoveToNextStory / MoveToNextDoc; each stretch read as spellpanel
// reads its walker's, GetCursorRange - SpellWordObserver.cpp:318). The stepping has no example in the SDK: the order is
// the spike's measurement, the walker meeting the same stretches match by match. false = a stretch with no range, or
// more than kMaxScopePieces - out is emptied, and nothing is built on it.
bool ListScopePieces(ITextWalkerScope* scope, std::vector<ScopePiece>& out)
{
	out.clear();
	if (scope == nil)
		return false;
	if (scope->IsEmpty())
		return true;
	void* const cursor = scope->GetNewCursor(nil);
	if (cursor == nil)
		return false;
	bool read = true;
	while (true)
	{
		ITextFocus* const range = scope->GetCursorRange(cursor, nil, nil);		// the scope's own - not AddRef'd
		if (range == nil || static_cast<int32>(out.size()) >= kMaxScopePieces)
		{
			read = false;
			break;
		}
		ScopePiece piece;
		piece.db = scope->GetCursorDoc(cursor).GetDataBase();
		piece.story = scope->GetCursorStory(cursor);
		const RangeData r = range->GetCurrentRange();
		piece.start = r.Start(nil);
		piece.end = r.End();
		out.push_back(piece);
		if (!scope->MoveToNextRange(cursor) && !scope->MoveToNextStory(cursor) && !scope->MoveToNextDoc(cursor))
			break;
	}
	scope->ReleaseCursor(cursor);
	if (!read)
		out.clear();
	return read;
}

bool SameStretches(const std::vector<ScopePiece>& a, const std::vector<ScopePiece>& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (a[i].story != b[i].story || a[i].start != b[i].start || a[i].end != b[i].end)
			return false;
	return true;
}

// A FOCUS OF KFC'S OWN FOR AS LONG AS A WALK NEEDS IT (QueryPartScope's Selection form): made on the story's manager the
// way SnpManipulateTextModel.cpp:475 makes one (ITextFocusManager::NewFocus - AddRef'd), let go with this object
// (RemoveFocus, then Release). It does not dirty the document or add an undo step (spike row 1).
class TempFocus
{
public:
	TempFocus() : fFocus(nil) {}
	~TempFocus() { Let(); }
	ITextFocus* Make(ITextModel* model, TextIndex start, TextIndex end)
	{
		Let();
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		if (mgr == nil || start < 0 || end < start || end > model->TotalLength())
			return nil;
		// a caret for an empty stretch (RangeData.h: a two-index range is not to be empty)
		fFocus = mgr->NewFocus((start == end) ? RangeData(start, RangeData::kLeanForward)
			: RangeData(start, end, RangeData::kLeanForward), kInvalidClass);
		if (fFocus != nil)
			fManager.reset(mgr.forget());
		return fFocus;
	}
private:
	void Let()
	{
		if (fFocus != nil)
		{
			if (fManager != nil)
				fManager->RemoveFocus(fFocus);
			fFocus->Release();
			fFocus = nil;
		}
		fManager.reset(nil);
	}
	ITextFocus*						fFocus;
	InterfacePtr<ITextFocusManager>	fManager;
	TempFocus(const TempFocus&);
	TempFocus& operator=(const TempFocus&);
};

// THE WALK SCOPE OF A SEARCHED PART, built the way InDesign builds its own (above): To End of Story = the factory's To End
// form from a caret at the part's start (IWalkerScopeFactoryUtils::QueryToEndOfStoryWalkerScope - what a text cursor
// gives the dialog's scope); Selection = its focus-list form over a focus on [start, end), asked to take in the tables
// and footnotes inside it as InDesign's Selection walk does (collectStoryRanges, IWalkerScopeFactoryUtils.h:125 - a
// kTextFocusBoss is its own ITextFocusList). focus keeps that focus for as long as the scope is walked. nil = not built.
ITextWalkerScope* QueryPartScope(const UIDRef& storyRef, KFCResultModel::SearchScopeKind kind, TextIndex start,
	TextIndex end, const WalkerScopeOptions& options, TempFocus& focus)
{
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil || start < 0 || start > model->TotalLength())
		return nil;
	if (kind == KFCResultModel::kScopeToEndOfStory)
	{
		Text::StoryRangeList from;
		from.push_back(Text::StoryRange(RangeData(start, RangeData::kLeanForward)));
		return Utils<IWalkerScopeFactoryUtils>()->QueryToEndOfStoryWalkerScope(storyRef, from, options);
	}
	InterfacePtr<ITextFocusList> list(focus.Make(model, start, end), UseDefaultIID());
	if (list == nil)
		return nil;
	return Utils<IWalkerScopeFactoryUtils>()->QueryFocusListWalkerScope(list, options, kTrue);
}

// THE SEARCHED PART OF ONE STORY, recorded when the search ends (KFCResultModel::SearchedRange): InDesign's stretches in
// that story (pieces - read from its scope before the walk) made a start, and for Selection an end - the first
// stretch's start and the last one's end, a table's cells coming between the text around its anchor in the walker's
// order - then built again the factory's way (QueryPartScope): walkable only when that gives InDesign's stretches back
// exactly (not table cells selected over two rows: [first, last) would take in the cells between). With the text just
// outside each edge.
KFCResultModel::SearchedRange MakeSearchedRange(IDataBase* db, UID story, KFCResultModel::SearchScopeKind kind,
	const std::vector<ScopePiece>& pieces, const WalkerScopeOptions& options)
{
	KFCResultModel::SearchedRange part;
	std::vector<ScopePiece> own;
	for (size_t i = 0; i < pieces.size(); ++i)
		if (pieces[i].db == db && pieces[i].story == story)
			own.push_back(pieces[i]);
	const UIDRef storyRef(db, story);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (own.empty() || model == nil || !KFCSearchEngine::ReadStoryVersion(db, story, part.version))
		return part;
	part.start = own.front().start;
	part.end = (kind == KFCResultModel::kScopeToEndOfStory) ? part.start : own.back().end;	// To End: its focus is a caret
	if (part.start < 0 || part.end < part.start)
		return part;
	TempFocus focus;
	InterfacePtr<ITextWalkerScope> again(QueryPartScope(storyRef, kind, part.start, part.end, options, focus));
	std::vector<ScopePiece> againPieces;
	part.walkable = (again != nil) && ListScopePieces(again, againPieces) && SameStretches(againPieces, own);
	const TextIndex from = (part.start > KFCResultModel::kSearchedEdgeChars) ? part.start - KFCResultModel::kSearchedEdgeChars : 0;
	part.before = KFCSearchEngine::ReadText(storyRef, from, part.start - from);
	if (kind == KFCResultModel::kScopeSelection)
		part.after = KFCSearchEngine::ReadText(storyRef, part.end, KFCResultModel::kSearchedEdgeChars);
	KFC_DIAG_LOG("PARTRECORDED story=%u stretches=%d start=%d end=%d walkable=%d version=%u", story.Get(), (int)own.size(),
		(int)part.start, (int)part.end, part.walkable ? 1 : 0, part.version);
	return part;
}

// A STRETCH OF A STORY'S TEXT, [at, at+len) - the one read of a range in this file (ReadText, CodePointsAt). The
// official one-call read (textiterator.h AppendToStringAndIncrement), after the check KCM and KESCL make, that the range
// starts inside the story; a range running past the end reads up to the end. Empty when it cannot be read.
WideString ReadStretch(ITextModel* model, TextIndex at, int32 len)
{
	WideString w;
	if (model == nil || len <= 0 || at < 0 || at >= model->TotalLength())
		return w;
	const int32 n = (len < model->TotalLength() - at) ? len : static_cast<int32>(model->TotalLength() - at);
	TextIterator it(model, at);
	it.AppendToStringAndIncrement(&w, n);
	return w;
}

// Text as code points - one per TextIndex (memory textindex-counts-code-points): a string's, and a stretch of a story's.
std::vector<uint32> CodePointsOf(const WideString& w)
{
	std::vector<uint32> out;
	out.reserve(w.CharCount());
	for (WideString::const_iterator c = w.begin(); c != w.end(); ++c)
		out.push_back(static_cast<uint32>(*c));
	return out;
}

std::vector<uint32> CodePointsOf(const PMString& s)
{
	return CodePointsOf(WideString(s));
}

std::vector<uint32> CodePointsAt(ITextModel* model, TextIndex at, int32 len)
{
	return CodePointsOf(ReadStretch(model, at, len));
}

// WHERE ONE EDGE OF A SEARCHED PART IS NOW: the place nearest `from` - kSearchedEdgeSlack characters either way at most -
// where the text just outside the edge reads `outside`: before the place for a start, after it for an end. An empty
// `outside` is the story's start (a start) or its end (an end). -1 = nowhere that near, or two places as near.
const int32 kSearchedEdgeSlack = 4096;
TextIndex EdgeAt(ITextModel* model, TextIndex from, const PMString& outside, bool isStart)
{
	const TextIndex total = model->TotalLength();
	if (outside.IsEmpty())
		return isStart ? 0 : total;
	const std::vector<uint32> want = CodePointsOf(outside);
	const int32 n = static_cast<int32>(want.size());
	if (from < 0)
		from = 0;
	if (from > total)
		from = total;
	const TextIndex lo = (from > kSearchedEdgeSlack + n) ? from - kSearchedEdgeSlack - n : 0;
	const TextIndex hi = (total - from > kSearchedEdgeSlack + n) ? from + kSearchedEdgeSlack + n : total;
	const std::vector<uint32> text = CodePointsAt(model, lo, hi - lo);
	TextIndex best = -1;
	int32 bestDistance = kSearchedEdgeSlack + 1;
	bool tie = false;
	for (int32 i = 0; i + n <= static_cast<int32>(text.size()); ++i)
	{
		int32 k = 0;
		while (k < n && text[i + k] == want[k])
			++k;
		if (k < n)
			continue;
		const TextIndex edge = lo + i + (isStart ? n : 0);
		const int32 distance = (edge > from) ? edge - from : from - edge;
		if (distance < bestDistance)
		{
			best = edge;
			bestDistance = distance;
			tie = false;
		}
		else if (distance == bestDistance && edge != best)
			tie = true;
	}
	return tie ? -1 : best;
}

// WHERE A STORY'S SEARCHED PART IS NOW (Search This Story Again over part of a story) - from where it is believed to be:
// its recorded place while the story is at the version that place was taken at (an Undo back to it included, which a
// focus does not follow - spike row 8), its focus otherwise (it follows the edits since, KFC's own writes among them),
// the recorded place when it has none; each edge then put where the text just outside it still reads as it did, the
// nearest such place (EdgeAt) - so a focus an Undo left behind lands back on its edge, and text typed right at an edge
// joins the part. false = an edge cannot be told (the text just outside it was rewritten).
bool FindSearchedPartAgain(int32 chapterIdx, IDataBase* db, UID story, KFCResultModel::SearchScopeKind kind,
	const KFCResultModel::SearchedRange& part, TextIndex& outStart, TextIndex& outEnd)
{
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	uint32 now = 0;
	if (model == nil || !KFCSearchEngine::ReadStoryVersion(db, story, now))
		return false;
	TextIndex fromStart = part.start, fromEnd = part.end;
	TextIndex focusStart = kInvalidTextIndex, focusEnd = kInvalidTextIndex;
	const bool byFocus = (now != part.version) && KFCRowFoci::CurrentSearchedRange(chapterIdx, story, db, focusStart, focusEnd);
	if (byFocus)
	{
		fromStart = focusStart;
		fromEnd = focusEnd;
	}
	outStart = EdgeAt(model, fromStart, part.before, true);
	outEnd = (kind == KFCResultModel::kScopeSelection) ? EdgeAt(model, fromEnd, part.after, false) : outStart;
	KFC_DIAG_LOG("PARTFOUND story=%u from=[%d,%d) by=%s found=[%d,%d)", story.Get(), (int)fromStart, (int)fromEnd,
		byFocus ? "focus" : ((now == part.version) ? "record" : "record-no-focus"), (int)outStart, (int)outEnd);
	return outStart >= 0 && outEnd >= outStart;
}

// WHERE ONE WALK MOVES THE SEARCH'S BAR (CollectHitsInDoc): the run's bar, this walk's slice of it - from `base`, `span`
// steps long - cut into one piece per story of the document walked, and the position already reported, carried from
// walk to walk (KFCAdvanceProgress swallows an advance too small to be worth a repaint). The stories are counted when
// the slice is made, from IStoryList for nothing - so a chapter is asked once it is open, since its size is not
// knowable before; a document with none is one step (CollectHitsInDoc). WalkBar() = no bar: a walk that looks a row or
// a story up again, or only asks which story a Search: means, moves nothing.
struct WalkBar
{
	KFCProgressBar*	bar;
	int32			base;
	int32			span;
	int32			stories;
	int32*			reported;
	WalkBar() : bar(nil), base(0), span(0), stories(0), reported(nil) {}
	WalkBar(KFCProgressBar& onBar, int32 sliceBase, int32 sliceSpan, const UIDRef& docRef, int32& ioReported)
		: bar(&onBar), base(sliceBase), span(sliceSpan), stories(CountSearchableStories(docRef)), reported(&ioReported) {}
};

// THE SHARED WALKER IS NOT LEFT HOLDING A CALLER'S SCOPE (CollectHitsInDoc's givenScope). A searched part's scope - Search
// This Story Again over a Selection, QueryPartScope - stands on a text focus of KFC's own, which the caller removes the
// moment the walk returns (TempFocus); the session's shared walker (AcquireWalker) outlives that, and nothing says Halt
// lets go of the scope it was given (ITextWalker.h: QueryScope answers the scope a walker holds; Halt only "halts the
// walking"). Not known to matter - InDesign's own Selection walks leave a scope over focuses the selection may drop - but
// until it is measured (a test build's WALKSCOPE line), the walker is handed the part's story as the walk ends, by any
// exit past Initialize, and stopped: the scope every story walk of KFC's leaves it with, taken while the focus still
// stands. (A walker of KFC's own - the fault switch own-walker - goes with the walk: the question does not arise for it.)
class SharedWalkerLetsGoOfGivenScope
{
public:
	SharedWalkerLetsGoOfGivenScope(ITextWalker* walker, ITextWalkerClient* client, IFindChangeOptions* opts,
		const UIDRef& story, const WalkerScopeOptions& options)
		: fWalker(walker), fClient(client), fOpts(opts), fStory(story), fOptions(options) {}
	~SharedWalkerLetsGoOfGivenScope()
	{
		if (fStory == UIDRef::gNull)
			return;		// no caller's scope (gNull is passed then)
		InterfacePtr<ITextWalkerScope> storyScope(Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(fStory, fOptions));
		if (storyScope == nil)
			return;
		if (fWalker->IsWalking())
			fWalker->Halt();
		fWalker->Initialize(fClient, storyScope, fOpts, nil);
		if (fWalker->IsWalking())
			fWalker->Halt();
	}
private:
	ITextWalker*				fWalker;	// the walk's own references (CollectHitsInDoc's), which outlive this object
	ITextWalkerClient*			fClient;
	IFindChangeOptions*			fOpts;
	const UIDRef				fStory;
	const WalkerScopeOptions	fOptions;
	SharedWalkerLetsGoOfGivenScope(const SharedWalkerLetsGoOfGivenScope&);
	SharedWalkerLetsGoOfGivenScope& operator=(const SharedWalkerLetsGoOfGivenScope&);
};

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
// bar: where this walk moves the run's bar (WalkBar - none by default). The walk moves it as it goes: a story at a
// time, each one subdivided by how far into its text the current match sits.
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
//
// outPieces (a Search: walk only): the stretches InDesign's scope holds (ListScopePieces) - what a search over part of a
// story records (MakeSearchedRange). givenScope: walk this scope instead (a story's searched part, Search This Story
// Again - QueryPartScope), docRef being the document it is in and onlyStory its story - which the shared walker is left
// with in its place (SharedWalkerLetsGoOfGivenScope).
void CollectHitsInDoc(const UIDRef& docRef, size_t maxHits, const WalkerScopeOptions& scopeOptions,
	HitDetail detail, std::vector<KFCResultModel::Hit>& outHits,
	bool& outCapped, ChapterWalkResult& outResult,
	const WalkBar& bar = WalkBar(), const UIDRef& onlyStory = UIDRef::gNull,
	IWalkerScopeFactoryUtils::WalkScopeType searchScope = IWalkerScopeFactoryUtils::kEmptyScope,
	std::vector<IDataBase*>* outHitDBs = nil, std::vector<ScopePiece>* outPieces = nil,
	ITextWalkerScope* givenScope = nil)
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

	// The session's shared walker, and its selection utilities the critical section below is taken on
	// (KFCSearchEngine::AcquireWalker - as the SDK's snippet takes them).
	InterfacePtr<ITextWalker> walker;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils;
	if (!KFCSearchEngine::AcquireWalker(walker, selUtils) || walker == nil)
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
	// What that costs: the walker is the session's shared one (AcquireWalker), so a search started from this panel takes a
	// Find Next sequence the user had going in the Find/Change dialog back to the match it had found - measured,
	// work/note-scripts/2026-10-06-kfc-no-track/s1-findnext2.ps1 - the price of walking the official way (the author's
	// call).
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
	// stands behind those two is the header's contract, not a worked example. (Grepped: no caller in the SDK
	// tree but our own plug-ins' - KFC's and KESCL's.)
	// (A given scope - a searched part's, QueryPartScope - is the caller's: the reference the InterfacePtr gives back is
	// taken here.)
	if (givenScope != nil)
		givenScope->AddRef();
	InterfacePtr<ITextWalkerScope> scope((givenScope != nil) ? givenScope : (bySearchScope
		? Utils<IWalkerScopeFactoryUtils>()->QueryWalkerScope_UsingSelections(searchScope, scopeOptions)
		: (onlyStory == UIDRef::gNull
			? Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions)
			: Utils<IWalkerScopeFactoryUtils>()->QueryStoryWalkerScope(onlyStory, scopeOptions))));
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
	// A caller's scope is not left with the shared walker, whichever exit below is taken (SharedWalkerLetsGoOfGivenScope).
	const SharedWalkerLetsGoOfGivenScope letGoOfGivenScope(walker, client, opts,
		(givenScope != nil) ? onlyStory : UIDRef::gNull, scopeOptions);

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

	// NOTHING TO WALK IS A WALK TO THE END THAT FOUND NOTHING (KFCSearchEngine::ScopeHasNothingToWalk - a document with no
	// text frame, or with master-page text only and Include Master Pages off). The find would answer kFailure, and the loop
	// below would call this chapter a walk that broke off ("stopped with a search error" - measured, case
	// sc-alldocs-notext). Past Initialize, so the walker is stopped as the bottom of this function stops it.
	if (KFCSearchEngine::ScopeHasNothingToWalk(scope))
	{
		if (walker->IsWalking())
			walker->Halt();
		return;		// kChapterWalked, no hits
	}

	// Required critical section around text-walker selection changes, HELD FOR THE WHOLE WALK.
	//
	// Adobe's own examples all wrap a SINGLE ProcessCommand instead (SnpFindAndReplace.cpp:788,
	// spellpanel's SpellSkipObserver.cpp:532-537 and SpellChangeAllObserver.cpp:317). That shape is
	// right for what they do - one Find Next per key press - and wrong here, because of what the
	// section contains. spellpanel's note on its SaveKeyboardEventHandler (SpellCheckWalker.cpp:85-141)
	// reads "SEIssue hmm same code as in TextWalkerSelectionUtils::EnterWalkerSelections_CriticalSection"
	// - hedged (the "hmm" is theirs) - and that code takes the keyboard focus away (RelinquishKeyFocus)
	// and gives it back on the way out (AcquireKeyFocus + SelectRange on an edit box). Entering and
	// leaving it per match would run that dance thousands of times in one search.
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

	// The stretches InDesign's scope holds, read before the walk moves it (a search over part of a story records them -
	// MakeSearchedRange): where the spike read them.
	if (outPieces != nil)
	{
		(void)ListScopePieces(scope, *outPieces);
		KFC_DIAG_LOG("PARTPIECES stretches=%d first=[%d,%d)", (int)outPieces->size(),
			outPieces->empty() ? -1 : (int)outPieces->front().start, outPieces->empty() ? -1 : (int)outPieces->front().end);
	}

	// What each document's frames and stories answer about the hits inside them - see WalkCache. One per
	// document: a Search: walk of All Documents meets several, and UIDs mean nothing across them.
	std::map<IDataBase*, WalkCache> walkCaches;

#ifdef KFC_DIAG
	// TEST BUILDS ONLY: the find command's time against BuildHit's, over this walk (SEARCHTIME, below).
	double tSearchFind = 0, tSearchBuild = 0;
	const size_t hitsBefore = outHits.size();
	gBuildHitTimes = BuildHitTimes();
	KFC_CLOCK(cSearchWalk);
	const KFCDiagPerf searchPerf;		// what the walk made InDesign do (KFCDiag.h)
#endif

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
		KFC_CLOCK(cSearchFind);
		const bool processed = ProcessFindChangeCmd(findCmd);
		KFC_SPENT(tSearchFind, cSearchFind);
		if (!processed)
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
		if (bar.bar != nil)
		{
			if (story != progressStory)
			{
				// CANCEL IS ASKED HERE, WHEN THE WALK MOVES ON TO ANOTHER STORY (the author's call: the middle
				// option - see the note on the critical section above): a Cancel pressed in a large document is
				// heard when the story being walked is done, not once the whole document - or, for an All
				// Documents walk, every open document - has been walked and the hits are thrown away. ! The walk
				// comes back to KFC only with a MATCH, so "another story" means the next story that holds one.
				// Whether WasCancelled itself lets events in is not measured (KFCAdvanceProgress); the "safety
				// first" call is in the note on the critical section above. kFalse = no global error state.
				if (progressStory.GetDataBase() != nil && bar.bar->WasCancelled(kFalse))
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

			// This walk's slice, cut into one piece per story (a document with none: one piece).
			int32 stepsPerStory = (bar.stories > 0) ? (bar.span / bar.stories) : bar.span;
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
			int32 position = bar.base + storiesDone * stepsPerStory + within;
			const int32 chapterEnd = bar.base + bar.span;
			if (position > chapterEnd)
				position = chapterEnd;

			// Moving the bar from inside the walk is what keeps the Cancel button answering at all -
			// see KFCAdvanceProgress.
			KFCAdvanceProgress(bar.bar, *bar.reported, position);
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
		KFC_CLOCK(cSearchBuild);
		BuildHit(hitDocRef, story, start, end, detail, walkCaches[storyDB], hit);
		KFC_SPENT(tSearchBuild, cSearchBuild);
		if (outHitDBs != nil)
			outHitDBs->push_back(storyDB);
	}

	if (walker->IsWalking())
		walker->Halt();
#ifdef KFC_DIAG
	{
		// (Test builds only) Does a walker once halted still hold the scope it was given? SharedWalkerLetsGoOfGivenScope's
		// open question - read before that hands the walker a story's scope.
		InterfacePtr<ITextWalkerScope> held(walker->QueryScope());
		KFC_DIAG_LOG("WALKSCOPE halted holds-scope=%d given=%d", (held != nil) ? 1 : 0, (givenScope != nil) ? 1 : 0);
	}
	{
		double tWalk = 0;
		KFC_SPENT(tWalk, cSearchWalk);
		char counters[300] = { 0 };
		searchPerf.Since(counters, sizeof(counters));
		KFC_DIAG_LOG("SEARCHTIME hits=%d detail=%d walk=%.0f find=%.0f build=%.0f (text=%.0f lead=%.0f place=%.0f) ms %s",
			(int)(outHits.size() - hitsBefore), (int)detail, tWalk, tSearchFind, tSearchBuild, gBuildHitTimes.text,
			gBuildHitTimes.lead, gBuildHitTimes.place, counters);
	}
#endif
}

// HOW MANY MATCHES ONE STORY'S LOOK AGAIN LISTS AT MOST (CollectStoryHits) - a number of its own, not the list's
// (kKFCCollectHitLimit, the rows the panel draws - spec F9). The row looked for can stand past its story's 300th match once
// matches were added in front of it by an edit KFC did not follow, and a walk cut at 300 then lists every candidate but the
// row's own: the row is called missing - or moved onto a look-alike (OneFreePlace), which a Replace then writes once an Undo
// has brought the story's version back. 10000 is the ceiling this walk was written with, before the list's limit came down
// to 300 and took that constant with it. Reaching it is "cannot tell", never a list.
const size_t kKFCLookAgainMatchCeiling = 10000;

// Every match of the current query in one story of an open document, as the search's walk meets them
// there, with the given scope switches - each with its place, its line and its hash (kHitPlaceAndText):
// what a row looked for again is compared with (StaleRowPlaces). The story scope is the one the replace's
// walks take (IWalkerScopeFactoryUtils::QueryStoryWalkerScope). Read-only (the walk's own dirty guard).
// False = it could not be walked, or it holds more matches than one look again lists (kKFCLookAgainMatchCeiling) -
// the row's own may stand past them, so nothing can be told from the list.
bool CollectStoryHits(const UIDRef& storyRef, const WalkerScopeOptions& scopeOptions,
	std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	IDataBase* const db = storyRef.GetDataBase();
	if (db == nil)
		return false;
	bool capped = false;
	ChapterWalkResult result = kChapterWalked;
	// (Fault switch look-again-list-limit, a test build's only - KFCDiag.h: the walk cut at the list's limit and the cut not
	// heard - the old way.)
	const bool listLimit = KFC_DIAG_FAULT("look-again-list-limit");
	CollectHitsInDoc(UIDRef(db, db->GetRootUID()),
		listLimit ? static_cast<size_t>(KFCResultModel::kKFCCollectHitLimit) : kKFCLookAgainMatchCeiling,
		scopeOptions, kHitPlaceAndText, outHits, capped, result, WalkBar(), storyRef);
	return result == kChapterWalked && (listLimit || !capped);
}

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

namespace
{
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
	// WasCancelled after the LAST chapter (see the ask-once-more test in SearchBook).
	//
	// Both forms are in the SDK, chosen by what the caller is counting: linksui advances a
	// TaskProgressBar with DoTask because it processes a list of files, while textexportfilter drives
	// a RangeProgressBar with SetPosition because it is measuring its way through a story's positions -
	// and cancels from it (TxtExpSuiteTextCSB.cpp:225-239: SetPosition every 32 positions, WasCancelled
	// every chunk; its import twin, TxtImpFilter.cpp:536-549, has the same calls inside an
	// if (bDisplayProgress) that never runs). KFC measures its way through hits and stories, so this is
	// the form that fits.
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
}	// anonymous namespace

bool KFCSearchEngine::CommitSearchMode()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// The tabs this panel can walk with the TEXT walker. Object and Colour search by attribute
	// through walkers of their own (kObjectWalkerService / kColorSearchWalkerService) and return page
	// items rather than lines of text; no caller brings them here (the Colour tab is turned away, and the
	// Object tab's search and writes - SearchBook, a row's Replace, Change All in Book, the query run - skip
	// this call: InDesign's object search is aimed by the object walker, not by a mode stated
	// here), so stating their mode would only mislead the engine.
	//
	// false rather than "nothing to do": nothing was stated, so a caller that walked anyway would
	// walk in whatever mode was committed last. Every caller keeps these tabs away from here, so this is
	// the answer for a route that does not exist rather than for one that does.
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

	// The tab, stated at itself: for this command the value and the mode it addresses are the same mode - exactly how
	// SnpFindAndReplace passes it in all four of its find/replace entry points, and how SetQuery states it
	// (CreateFindChangeIntCmd says which of the boss's two int fields is which).
	//
	// THE CALLER IS TOLD. The walk must NOT go ahead on a failure: it would run in whatever mode was
	// committed last, which is a tab the user is not looking at, and the results would then be filed
	// under the tab that IS on screen. The snippet this is modelled on stops too (SnpFindAndReplace.cpp:
	// 511-516 and :598-603, both on this very command).
	if (!CommitFindChangeInt(kFindSearchModeCmdBoss, static_cast<int32>(mode), static_cast<int32>(mode)))
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
// found by, so the verify walk (KFCReplaceEngine's ChapterMovedUnderRows) refuses only a row asked for
// that is no longer a match - a changed value that still matches the rows would let the write go ahead,
// in silence, under a query the panel was not searched with, which is what RefuseChangedQuery is there
// to refuse. There is no generic value READ, but there is a generic COMPARE. (The operators are private -
// AttributeBossList.h:245-252 - which is a normal C++ way of making callers say which comparison they
// mean: IsEqual, deep, beside Intersects and IntersectionContainsDifferences.)
//
// Adobe calls neither IsEqual nor IntersectionContainsDifferences anywhere in the SDK.
// The header's contract is all there is to go on. Measured instead: see the audit note for the two
// searches - identical but for one attribute's value - that this was checked with.
//
// KEEP A COPY OF THE FIND FORMAT this search ran with - the attribute list behind the dialog's format pane, and on the
// Glyph tab the query's own font - so the replace can ask whether it is still the same one (FindFormatHasChanged).
// SearchBook's alone, once per search, beside BuildWalkSignature; a search that cannot read the settings simply
// remembers nothing, and FindFormatHasChanged then says "cannot tell".
namespace
{
void RememberFindFormat()
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
	// Which database that is - the session's own (the options' workspace) or a document's - in a test build.
	KFC_DIAG_LOG("FINDFORMAT remembered attrDB=%p optionsDB=%p", (void*)db, (void*)::GetDataBase(opts));
}
}	// anonymous namespace

void KFCSearchEngine::RememberFindFormatOf(const KFCQueryCopy* copy)
{
	// RememberFindFormat's rule, read off the list's own copy instead of Find/Change (the tier switch leaves Find/Change
	// alone - v2 R1): no copy, no list or no database = nothing remembered, and FindFormatHasChanged says "cannot tell".
	KFCSearchEngine::ForgetSearchedFindFormat();
	if (copy == nil || !copy->valid || copy->findAttrs.get() == nil || copy->attrDB == nil)
		return;
	gSearchedFindAttrs.reset(copy->findAttrs->Duplicate());
	gSearchedFindAttrDB = copy->attrDB;
	KFC_DIAG_LOG("FINDFORMAT remembered from a list's copy attrDB=%p", (void*)copy->attrDB);
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

void KFCSearchEngine::ForgetSearchedFindFormatOf(IDataBase* closingDB)
{
	if (closingDB == nil || closingDB != gSearchedFindAttrDB)
		return;
	KFC_DIAG_LOG("FINDFORMAT forgotten - its attribute database %p is closing", (void*)closingDB);
	KFCSearchEngine::ForgetSearchedFindFormat();
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

	// The tab's five Include switches - the five GetKFCWalkerScopeOptions reads - one bit each, for either tab below.
	auto appendScopeSwitches = [&]()
	{
		const bool16 scopeSwitches[] =
		{
			opts->GetIncludeMasterPages(mode),
			opts->GetIncludeLockedLayersForFind(mode),
			opts->GetIncludeHiddenLayers(mode),
			opts->GetIncludeLockedStoriesForFind(mode),
			opts->GetIncludeFootnotes(mode),
		};
		for (size_t i = 0; i < sizeof(scopeSwitches) / sizeof(scopeSwitches[0]); ++i)
			outSignature.Append(scopeSwitches[i] ? "1" : "0");
	};

	// THE OBJECT TAB: what decides which page items come back - the Type, the tab's own five
	// Include switches, how many conditions Find Object Format holds and the object style it looks for. The conditions'
	// values are RememberFindFormat's list, as for text. The Change side stays out, as for text.
	if (mode == IFindChangeOptions::kObjectSearch)
	{
		outSignature.Append(" ot");
		outSignature.AppendNumber(static_cast<int32>(opts->GetObjectSearchType()));
		outSignature.Append(" o");
		appendScopeSwitches();
		const AttributeBossList* const objectAttrs = opts->GetFindAttributeBossList(db, mode);
		outSignature.Append(" n");
		outSignature.AppendNumber(objectAttrs != nil ? objectAttrs->CountBosses() : 0);
		outSignature.Append(" fs");
		outSignature.AppendNumber(static_cast<int32>(opts->GetObjectFindStyle(db).Get()));
		outSignature.SetTranslatable(kFalse);
		return;
	}

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
	const bool16 matchSwitches[] =
	{
		opts->GetCaseSensitive(mode),
		opts->GetEntireWord(mode),
		opts->GetKanaSensitive(mode),
		opts->GetWidthSensitive(mode),
		// (No direction: KFC searches and replaces forward only - KFCForwardSearchScope - so the
		// dialog's direction changes nothing KFC does.)
	};
	outSignature.Append(" o");
	for (size_t i = 0; i < sizeof(matchSwitches) / sizeof(matchSwitches[0]); ++i)
		outSignature.Append(matchSwitches[i] ? "1" : "0");
	appendScopeSwitches();

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
	// KFCForwardSearchScope, which the searches and the replaces each put round themselves.
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
// keeps "the row says locked" and "the replace refuses" describing the same set of hits.
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
// hit locked and greys its Replace) and the REPLACE (which refuses to write) can never disagree.
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

PMString KFCSearchEngine::ReadText(const UIDRef& story, TextIndex at, int32 len)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	PMString s(ReadStretch(model, at, len));
	s.SetTranslatable(kFalse);
	return s;
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

// THE ROW READ AT A PLACE (spec T1): the test a row's stored place has always had to pass, at any place of the row's
// own story - the whole match by its hash (MatchIsSameOccurrence), and the line around it read the way the search
// read it (ReadHitText). The jump asks it of the stored place and of the row's text focus (LocateRow).
static bool RowReadsAsFoundAt(int32 chapterIdx, int32 hitIdx, IDataBase* db, UID story, TextIndex start, TextIndex end)
{
	UID rowStory = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	KFCResultModel::RowDisplay row;
	if (db == nil || !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, rowStory, a, b, hash)
		|| !KFCResultModel::GetHitRow(chapterIdx, hitIdx, row)
		|| story == kInvalidUID || story != rowStory || !db->IsValidUID(story))
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

bool KFCSearchEngine::RowReadsAsFound(int32 chapterIdx, int32 hitIdx, IDataBase* db)
{
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	uint64 hash = 0;
	if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, start, end, hash))
		return false;
	return RowReadsAsFoundAt(chapterIdx, hitIdx, db, story, start, end);
}

bool KFCSearchEngine::ReadStoryVersion(IDataBase* db, UID story, uint32& outVersion)
{
	// IsValidUID first, for a story an earlier replace deleted (an anchored frame's): the same answer the
	// InterfacePtr below would give - Instantiate returns nil for a deleted UID (IDataBase.h) - said early.
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return false;
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	if (model == nil)
		return false;
	outVersion = model->GetChangeCount();
	return true;
}

void KFCSearchEngine::FileChapter(const KFCBookScope::ChapterDoc& target, std::vector<KFCResultModel::Hit>& hits,
	CollectTally& tally, std::map<UID, uint32>* storyVersions, std::map<UID, KFCResultModel::SearchedRange>* searchedRanges)
{
	// Page order and each row's "P<page>(<n>) " locator - the model's one ordering, a search again's too. It needs the
	// WHOLE target's rows (page order and the within-page ordinal are only known once they are all in), which is why the
	// unit filed is the target, not a fixed number of rows. Pure string and index work - no recompose, so no dirty guard.
	//
	// Into the model only: the tree is drawn ONCE, by the search's caller, when the search returns - the bar is modal,
	// so while it is up the panel can be neither read nor clicked, and a rebuild per target would be work nobody sees.
	KFCResultModel::OrderHitsByPage(hits);
	KFCResultModel::Chapter chapter;
	chapter.name = target.shortName;
	chapter.name.SetTranslatable(kFalse);
	chapter.docRef = target.docRef;
	chapter.file = target.file;
	chapter.hits.swap(hits);
	if (storyVersions != nil)
		chapter.storyVersions.swap(*storyVersions);
	if (searchedRanges != nil)
		chapter.searchedRanges.swap(*searchedRanges);
	// Counted before the handover: the model takes the rows and leaves this Chapter empty.
	tally.total += static_cast<int32>(chapter.hits.size());
	++tally.chaptersWithHits;
	KFCResultModel::AppendChapter(std::move(chapter));
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

// THE ROW EYE'S WORDS (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-2): how many rows with their eye
// on stayed beside what the search found - " 1 kept row stays." Nothing when none did. (Said by both searches again -
// SayFoundAgain - and by Search These Results Again - RunSearch.)
void AppendKeptStay(PMString& io, int32 stayed)
{
	if (stayed <= 0)
		return;
	io.Append(" ");
	io.AppendNumber(stayed);
	io.Append(stayed == 1 ? " kept row stays." : " kept rows stay.");
}

// ONE DOCUMENT, BY ITS FILE OR BY ITS OPEN DATABASE (the row eye's carry - CarryKeptRowsByDocument): a book's chapters have
// files - compared as files (FileUtils::IsEqual, as KFCBookScope compares them); a document of All Documents or of Search:
// Document has none - the same open database. A chapter emptied by a close is unbound, so it matches nothing.
bool SameDocument(const UIDRef& docRef, const IDFile& file, const KFCBookScope::ChapterDoc& target)
{
	const bool hasFile = KFCBookScope::ChapterHasFile(file);
	const bool targetHasFile = KFCBookScope::ChapterHasFile(target.file);
	if (hasFile && targetHasFile)
		return FileUtils::IsEqual(file, target.file) != kFalse;
	if (hasFile || targetHasFile)
		return false;
	return target.docRef.GetDataBase() != nil && docRef.GetDataBase() == target.docRef.GetDataBase();
}

// THE ROW EYE, SEARCH THESE RESULTS AGAIN (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-2, 3-1): the rows
// of the list as it was (`before`) whose eye is on, carried into the new list - into the chapter of their own document beside
// its found rows (KFCResultModel::CarryKeptRows), or into a chapter of their own where the new list has none for that
// document (no match now; or not searched now - a book's chapter the Book panel's selection left out) - and the chapters put
// in the order of `targets` (the new list's own order), a chapter the walk filed outside `targets` after them as it came.
// A chapter of their own is bound to the document open now (FindOpenChapterDoc - by its file, or its open database), else to
// nothing (a book's chapter handed back: its file). The answer = how many kept rows stayed beside the found ones.
int32 CarryKeptRowsByDocument(const KFCResultModel::ModelSnapshot& before,
	const std::vector<KFCBookScope::ChapterDoc>& targets)
{
	int32 stayed = 0;
	const int32 filed = KFCResultModel::GetChapterCount();
	std::vector<bool> placed(static_cast<size_t>(filed > 0 ? filed : 0), false);
	std::vector<bool> carried(before.chapters.size(), false);
	std::vector<int32> order;
	// A chapter of kept rows alone, named and bound as its document - appended, and its place in the order taken.
	auto keptChapter = [&](const PMString& name, const IDFile& file, const UIDRef& boundBefore,
		std::vector<KFCResultModel::Hit>& rows, const std::map<UID, uint32>& versions)
	{
		KFCResultModel::Chapter chapter;
		chapter.name = name;
		chapter.name.SetTranslatable(kFalse);
		chapter.file = file;
		UIDRef open(boundBefore);
		if (KFCBookScope::FindOpenChapterDoc(file, open))
			chapter.docRef = open;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			const std::map<UID, uint32>::const_iterator v = versions.find(rows[i].storyUID);
			if (v != versions.end())
				chapter.storyVersions[v->first] = v->second;
		}
		stayed += static_cast<int32>(rows.size());
		KFCResultModel::OrderHitsByPage(rows);
		chapter.hits.swap(rows);
		KFCResultModel::AppendChapter(std::move(chapter));
		order.push_back(KFCResultModel::GetChapterCount() - 1);
	};
	for (size_t t = 0; t < targets.size(); ++t)
	{
		int32 at = -1;
		for (int32 c = 0; c < filed && at < 0; ++c)
		{
			UIDRef docRef;
			IDFile file;
			if (!placed[static_cast<size_t>(c)] && KFCResultModel::GetChapterLocation(c, docRef, file)
				&& SameDocument(docRef, file, targets[t]))
				at = c;
		}
		std::vector<KFCResultModel::Hit> kept;
		std::map<UID, uint32> versions;
		for (size_t o = 0; o < before.chapters.size(); ++o)
		{
			const KFCResultModel::Chapter& old = before.chapters[o];
			if (carried[o] || !SameDocument(old.docRef, old.file, targets[t]))
				continue;
			carried[o] = true;
			for (size_t i = 0; i < old.hits.size(); ++i)
				if (old.hits[i].kept)
					kept.push_back(old.hits[i]);
			versions = old.storyVersions;
			break;
		}
		if (at >= 0)
		{
			stayed += KFCResultModel::CarryKeptRows(at, kept, versions);
			placed[static_cast<size_t>(at)] = true;
			order.push_back(at);
		}
		else if (!kept.empty())
			keptChapter(targets[t].shortName, targets[t].file, targets[t].docRef, kept, versions);
	}
	for (int32 c = 0; c < filed; ++c)
		if (!placed[static_cast<size_t>(c)])
			order.push_back(c);
	for (size_t o = 0; o < before.chapters.size(); ++o)
	{
		const KFCResultModel::Chapter& old = before.chapters[o];
		if (carried[o])
			continue;
		std::vector<KFCResultModel::Hit> kept;
		for (size_t i = 0; i < old.hits.size(); ++i)
			if (old.hits[i].kept)
				kept.push_back(old.hits[i]);
		if (!kept.empty())
			keptChapter(old.name, old.file, old.docRef, kept, old.storyVersions);
	}
	(void)KFCResultModel::OrderChapters(order);
	return stayed;
}

// EVERY STORY'S VERSION, WHILE ITS DOCUMENT IS STILL OPEN.
// ITextModel::GetChangeCount of each story holding a hit (ReadStoryVersion) - what the replace compares
// before it writes, so a story moved since without KFC (typing, Ctrl+Z of anything but a write of KFC's
// own - which the panel follows, KFCUndoFollow) is not written to. A book's chapter has
// it read in front of the release that closes a chapter this search opened (CollectTargets, and Search This
// Document Again - KFCSearchEngine::SearchDocumentAgain).
void ReadStoryVersions(const UIDRef& docRef, const std::vector<KFCResultModel::Hit>& hits,
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
}

// SEARCHBOOK'S WALK OVER ITS TARGETS. Every target walked and its hits appended to the model chapter by chapter,
// under one bar. (A query run does not come here: it is Change All only - KFCQuerySequence.)
void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, bool allDocuments,
	IWalkerScopeFactoryUtils::WalkScopeType selectionScope,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out,
	int32 reserved)		// rows the list holds before anything is walked - a search again's kept rows (the row eye -
						// RunSearch); 0 for a search. Their room is kept from the walk's.
{
	out = KFCSearchEngine::CollectTally();

	// THE FIVE SCOPE SWITCHES, read ONCE for the whole run. They come off the same dialog as the query the
	// caller stated (SearchBook), they are the same for every chapter by definition (the replace pass has to
	// re-walk with exactly these), and nothing can change them while the run is on: the progress bar
	// below is modal - so not inside CollectHitsInDoc, once per chapter, for an answer that cannot differ.
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);

	// Walk every target; only chapters that hold a hit go into the model (no empty branches). The
	// caller cleared the model at its commit point (SearchBook - DropResults); each chapter is APPENDED as it
	// finishes, and the caller draws the tree once the search returns (KFCSearchEngine::FileChapter).
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

	// WHAT A WALK THAT DID NOT END CLEANLY OWES THE SUMMARY, named for what was walked - in both loops below, the Book
	// Scope OFF walks' and a book's chapters' (the tally's lists, KFCSearchEngine.h). One that broke off keeps its hits:
	// they are real and are filed like any other, and what the summary owes is the other half - the rest was never
	// looked at. One that never ran is named with its reason, since a skipped document is otherwise indistinguishable
	// from one with no matches; the colon goes with the reason, so an ending with none of its own (ChapterWalkResultText)
	// leaves the name standing alone. A cancelled walk says nothing: the run is thrown away.
	auto noteWalk = [&](const PMString& walkName, ChapterWalkResult walkResult)
	{
		PMString name(walkName);
		name.SetTranslatable(kFalse);
		if (walkResult == kChapterWalkFailed)
			out.brokeOff.push_back(name);
		else if (walkResult != kChapterWalked && walkResult != kChapterCancelled)
		{
			const PMString why(ChapterWalkResultText(walkResult));
			if (!why.IsEmpty())
			{
				name.Append(": ");
				name.Append(why);
			}
			out.unsearchable.push_back(name);
		}
	};

	// BOOK SCOPE OFF: THE RANGE IS THE DIALOG'S, THE ORDER IS FROM THE TOP.
	// The RANGE is whatever Search: says - Document, All Documents, Story, To End of Story, Selection - the way
	// Edit > Find/Change reads it (the author: "KFC uses the official Find/Change's settings as they are, so
	// follow the official way as far as it goes").
	//
	// THE ORDER IS FROM THE TOP, WHEREVER THE CARET IS (the author's call).
	// The dialog's own scope, given a selection, starts AT it and loops back to the head
	// (IWalkerScopeFactoryUtils.h:169-171) - Find Next's order, one match at a time from where the user stands.
	// A list keeps its walk's order within a page (KFCResultModel::OrderHitsByPage), so walking that scope numbers
	// the lower story's match P1(1) whenever the caret stands in it (the regression case sc-selection-caret). Where
	// the selection decides only where a walk STARTS, the walk starts at the top:
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
			KFCSetCountedTask(progressBar, "Document", 0, 1, targets[0].shortName);
		KFCAdvanceProgress(&progressBar, progressReported, 0, true /*force*/);

		// One list for every walk below, in walk order, each hit with the database it was found in. The ceiling
		// is the whole search's: CollectHitsInDoc counts what the list already holds.
		std::vector<KFCResultModel::Hit> hits;
		std::vector<IDataBase*> hitDBs;
		const int32 room = KFCResultModel::kKFCCollectHitLimit - reserved;
		const size_t limit = static_cast<size_t>(room > 0 ? room : 0);
		// A search over part of a story (To End of Story, Selection): the stretches InDesign's scope holds.
		const bool overPart = !allDocuments && (selectionScope == IWalkerScopeFactoryUtils::kToEndOfStoryScope
			|| selectionScope == IWalkerScopeFactoryUtils::kSelectionScope);
		const KFCResultModel::SearchScopeKind partKind = (selectionScope == IWalkerScopeFactoryUtils::kToEndOfStoryScope)
			? KFCResultModel::kScopeToEndOfStory : KFCResultModel::kScopeSelection;
		std::vector<ScopePiece> partPieces;
		if (progressBar.WasCancelled(kFalse))
			out.cancelled = true;
		else if (allDocuments)
		{
			// A slice of the bar per document, the way the book's chapters share it.
			for (size_t t = 0; t < targets.size(); ++t)
			{
				const int32 base = static_cast<int32>(t) * kKFCChapterProgressSpan;
				KFCSetCountedTask(progressBar, "Document", t, targets.size(), targets[t].shortName);
				KFCAdvanceProgress(&progressBar, progressReported, base, true /*force*/);
				if (t > 0 && progressBar.WasCancelled(kFalse))
				{
					out.cancelled = true;
					break;
				}
				bool capped = false;
				ChapterWalkResult walkResult = kChapterWalked;
				CollectHitsInDoc(targets[t].docRef, limit, scopeOptions, kHitEverything, hits, capped, walkResult,
					WalkBar(progressBar, base, kKFCChapterProgressSpan, targets[t].docRef, progressReported), UIDRef::gNull,
					IWalkerScopeFactoryUtils::kEmptyScope, &hitDBs);
				if (walkResult == kChapterCancelled)
				{
					out.cancelled = true;
					break;
				}
				noteWalk(targets[t].shortName, walkResult);
				if (capped)
				{
					out.truncated = true;		// the list is full: the documents after this one are not walked
					break;
				}
			}
		}
		else
		{
			// The whole bar is the one document's, cut by its stories.
			const WalkBar wholeBar(progressBar, 0, progressTotal, targets[0].docRef, progressReported);
			bool capped = false;
			ChapterWalkResult walkResult = kChapterWalked;
			if (selectionScope == IWalkerScopeFactoryUtils::kDocumentScope)
			{
				CollectHitsInDoc(targets[0].docRef, limit, scopeOptions, kHitEverything, hits, capped, walkResult,
					wholeBar, UIDRef::gNull, IWalkerScopeFactoryUtils::kEmptyScope, &hitDBs);
			}
			else if (selectionScope == IWalkerScopeFactoryUtils::kStoryScope)
			{
				// Which story: the dialog's own Story scope, to its first match. No match = nothing to list.
				std::vector<KFCResultModel::Hit> first;
				std::vector<IDataBase*> firstDBs;
				bool firstCapped = false;
				CollectHitsInDoc(UIDRef::gNull, 1, scopeOptions, kHitPlaceAndText, first, firstCapped, walkResult,
					WalkBar(), UIDRef::gNull, IWalkerScopeFactoryUtils::kStoryScope, &firstDBs);
				IDataBase* const storyDB = (!first.empty() && !firstDBs.empty()) ? firstDBs[0] : nil;
				if (walkResult == kChapterWalked && storyDB != nil)
				{
					CollectHitsInDoc(UIDRef(storyDB, storyDB->GetRootUID()), limit, scopeOptions, kHitEverything, hits,
						capped, walkResult, wholeBar, UIDRef(storyDB, first[0].storyUID), IWalkerScopeFactoryUtils::kEmptyScope,
						&hitDBs);
				}
			}
			else
			{
				// To End of Story, Selection: InDesign's stretches too - the part each story's rows came from, kept for
				// Search This Story Again (MakeSearchedRange, below).
				CollectHitsInDoc(UIDRef::gNull, limit, scopeOptions, kHitEverything, hits, capped, walkResult,
					wholeBar, UIDRef::gNull, selectionScope, &hitDBs, overPart ? &partPieces : nil);
			}
			if (capped)
				out.truncated = true;
			if (walkResult == kChapterCancelled)
				out.cancelled = true;
			else
				noteWalk(targets[0].shortName, walkResult);
		}
		KFCAdvanceProgress(&progressBar, progressReported, progressTotal, true /*force*/);

		if (!out.cancelled)
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
				ReadStoryVersions(targets[t].docRef, perTarget[t], storyVersions);
				// The searched part of each story holding a row (a search over part of a story).
				std::map<UID, KFCResultModel::SearchedRange> parts;
				for (size_t h = 0; overPart && h < perTarget[t].size(); ++h)
				{
					const UID story = perTarget[t][h].storyUID;
					if (story != kInvalidUID && parts.count(story) == 0)
						parts[story] = MakeSearchedRange(targets[t].docRef.GetDataBase(), story, partKind, partPieces,
							scopeOptions);
				}
				KFCSearchEngine::FileChapter(targets[t], perTarget[t], out, &storyVersions, &parts);
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
		KFCSetCountedTask(progressBar, "Chapter", i, targets.size(), targets[i].shortName);
		KFCAdvanceProgress(&progressBar, progressReported, progressBase, true /*force*/);

		// Cancel is asked here, after the bar has been moved - from inside the walk and just above
		// (KFCAdvanceProgress; which call on the bar takes the click is not measured, see there).
		// kFalse = do NOT raise the global error state: it would outlive the search and fail the
		// commands that come after it.
		if (progressBar.WasCancelled(kFalse))
		{
			out.cancelled = true;
			break;
		}

		// Room left under the whole-search safety ceiling; once it is gone, stop walking further
		// chapters too (the result set is full).
		const int32 remaining = KFCResultModel::kKFCCollectHitLimit - reserved - out.total;
		if (remaining <= 0)
		{
			out.truncated = true;
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

		// The chapter's slice of the bar, cut by its stories - asked now that it is open (WalkBar).
		std::vector<KFCResultModel::Hit> hits;
		bool docCapped = false;
		ChapterWalkResult walkResult = kChapterWalked;
		CollectHitsInDoc(chapterDocRef, static_cast<size_t>(remaining), scopeOptions, kHitEverything, hits, docCapped,
			walkResult, WalkBar(progressBar, progressBase, kKFCChapterProgressSpan, chapterDocRef, progressReported));

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
		// is the entire point of this loop, so the close has to happen here. (Measured on Change
		// Checked, which walked chapters the same way: four chapters, four .idlk files standing at
		// once.) Safe at this point - the walk has halted, the dirty
		// guard inside CollectHitsInDoc has already restored the flag, and everything read after
		// this (the page order, the Chapter it fills in) is plain values, not database work.
		//
		// The release's false cannot be read alone - HandBackHeldDocNow asks whether the chapter was ours
		// and whether it is still open, so a chapter the user closed under the run is not counted as one
		// left open with no window.
		// The story versions are read first, while the chapter is still open (ReadStoryVersions).
		std::map<UID, uint32> storyVersions;
		ReadStoryVersions(chapterDocRef, hits, storyVersions);

		if (!KFCBookScope::HandBackHeldDocNow(chapterDocRef))
			out.unclosed.push_back(targets[i].shortName);

		// Cancel heard inside the walk, at a story's end: the chapter is handed back above, and the
		// caller throws the run away (SearchBook's "Search cancelled.").
		if (walkResult == kChapterCancelled)
		{
			out.cancelled = true;
			break;
		}

		if (docCapped)
			out.truncated = true;
		// A walk that broke off keeps its hits and is filed below; one that never ran has none (noteWalk).
		noteWalk(targets[i].shortName, walkResult);
		if (hits.empty())
			continue;

		KFCSearchEngine::FileChapter(targets[i], hits, out, &storyVersions);
	}

	// ASK ONCE MORE, now that the loop is over. The test inside the loop sits at the TOP of each
	// pass, so a cancel pressed while the LAST chapter was being walked has no next pass to be seen
	// in, and the search would finish as though the button had never been touched. Same rule as
	// Change All in Book's and the query run's (KFCChangeAll.cpp, KFCQuerySequence.cpp). (The walk asks it
	// too, when it moves on to another story; a Cancel pressed during the LAST story walked is seen here.)
	if (!out.cancelled && progressBar.WasCancelled(kFalse))
		out.cancelled = true;
}

// The run's target sentences: its front doors' (KFCSearchEngine::ResolveRunTarget) and SearchBook's safety nets past its
// commit point, which say the same words. (The empty book's, kBookHasNoChapters, is the header's: Change All in Book and
// the query run say it too.)
const char* const kNoBookOpen = "Book Scope is on, but no book is open.";
const char* const kNoDocumentToSearch = "No open document to search.";

}	// anonymous namespace

bool KFCSearchEngine::ResolveRunScope(RunScope& out, PMString& outRefusal)
{
	out = RunScope();
	outRefusal.Clear();
	outRefusal.SetTranslatable(kFalse);
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
		outRefusal.Append("Book Scope is on, and Search: is ");
		outRefusal.Append(*scopeName != '\0' ? scopeName : "not Document");
		outRefusal.Append(". Set Search: to Document in Edit > Find/Change, or turn Book Scope off.");
		return false;
	}
	if (!fromBook && *scopeName == '\0')
	{
		// (Under about 117 characters - the message area's four lines at the panel's floor, KFCPanelMetrics.cpp. Listing the
		// five values it can follow ran it to 147; the dialog's own Search: menu lists them.)
		outRefusal.Append("Search: in Edit > Find/Change is set to something this panel cannot follow - choose another one there.");
		return false;
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

	// What the run would run on - the doors the Object tab asks too (ResolveRunTarget).
	if (!ResolveRunTarget(fromBook, outRefusal))
		return false;
	out.fromBook = fromBook;
	out.allDocuments = allDocuments;
	out.selectionScope = static_cast<int32>(selectionScope);
	out.fellBackNote = fellBackNote;
	return true;
}

bool KFCSearchEngine::ResolveRunTarget(bool fromBook, PMString& outRefusal)
{
	if (fromBook)
	{
		const KFCBookScope::TargetBook targetBook = KFCBookScope::GetTargetBook();
		if (targetBook == KFCBookScope::kNoTargetBook)
		{
			outRefusal.Append(kNoBookOpen);
			return false;
		}
		if (targetBook == KFCBookScope::kTargetBookEmpty)
		{
			outRefusal.Append(kBookHasNoChapters);
			return false;
		}
	}
	else if (KFCBookScope::ActiveDocument() == nil)
	{
		outRefusal.Append(kNoDocumentToSearch);
		return false;
	}
	return true;
}

bool KFCSearchEngine::DescribeRunScope(PMString& outWords)
{
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	RunScope scope;
	PMString refusal;
	if (!ResolveRunScope(scope, refusal))
	{
		outWords.Append("Cannot run: ");
		outWords.Append(refusal);
		return false;
	}
	outWords.Append("Runs on: ");
	if (scope.fromBook)
	{
		PMString bookName;
		KFCBookScope::BookSelection selection;
		(void)KFCBookScope::DescribeTargetBook(bookName, selection);	// ResolveRunScope has just found it
		if (selection.selected > 0)
		{
			// Find/Change Selected Documents (Book): the Book panel's selected documents alone
			outWords.AppendNumber(selection.selected);
			outWords.Append(" selected document(s) of the book \"");
			outWords.Append(bookName);
			outWords.Append("\".");
			return true;
		}
		outWords.Append("the book \"");
		outWords.Append(bookName);
		outWords.Append("\" (");
		outWords.AppendNumber(selection.total);
		outWords.Append(" document(s)).");
		return true;
	}
	if (scope.allDocuments)
	{
		outWords.Append("All Documents (Search: in Edit > Find/Change).");
		return true;
	}
	// Document, or a part of the active document - the Search: as ResolveRunScope settled it (a Story the selection
	// does not offer is Document there, as Edit > Find/Change shows it).
	PMString docName;
	IDocument* const doc = KFCBookScope::ActiveDocument();		// non-owning; ResolveRunScope refused when there is none
	if (doc != nil)
		doc->GetName(docName);
	outWords.Append(KFCSearchEngine::SearchScopeName(scope.selectionScope));
	outWords.Append(scope.selectionScope == IWalkerScopeFactoryUtils::kDocumentScope ? " \"" : " in \"");
	outWords.Append(docName);
	outWords.Append("\" (Search: in Edit > Find/Change).");
	return true;
}

namespace KFCSearchEngine
{
	/** WHAT A LIST SEARCHED AGAIN SEARCHES (SearchListAgain - a tier's heading's Search These Results Again): the scope it
	    was searched with, read off the list, and what that scope searched - not Search: as it stands now. */
	struct AgainTarget
	{
		bool					fromBook;		// a book's list: its book (bookPath), open
		bool					allDocuments;	// All Documents: the documents open now
		KFCBookScope::ChapterDoc	doc;			// Search: Document - the list's document, found open (when hasDoc)
		bool					hasDoc;			// false = the front document, as Find searches (a search that found nothing)
		PMString				bookPath;
		uint32					set;			// the list's result set: kept, so the tier stays where it stands (v2 R4)
		AgainTarget() : fromBook(false), allDocuments(false), hasDoc(false), set(0) {}
	};

	/** THE SEARCH'S ONE BODY - SearchBook's (again nil) and SearchListAgain's: the same doors, the same walk, the same
	    summary. A list searched again is not parked as a tier - it is the same one - and is put back as it was when the
	    search does not go through. outCommitted = past the commit point, and not put back. */
	int32 RunSearch(const AgainTarget* again, PMString& outSummary, bool& outCommitted);
}

int32 KFCSearchEngine::SearchBook(PMString& outSummary)
{
	bool committed = false;
	return RunSearch(nil, outSummary, committed);
}

int32 KFCSearchEngine::RunSearch(const AgainTarget* again, PMString& outSummary, bool& outCommitted)
{
	outCommitted = false;
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

#ifdef KFC_DIAG
	// THE TEST BUILD'S WAY INTO THE QUERY RUN WITHOUT ITS DIALOG: while the fault switch queries-run is on, the Find
	// action runs the queries its file lists (KFCQuerySequence::RunFromDiagSwitch - the regression cases qs-*, xq-*)
	// - ahead of the re-entry stop below, because the run asks the same doors itself and sets its own flag.
	if (again == nil && KFC_DIAG_FAULT("queries-run"))
		return KFCQuerySequence::RunFromDiagSwitch(outSummary);
	// THE TIER SWITCH'S SPIKE (docs/superpowers/plans/2026-10-14-kfc-tier-switch.md Task 3): while one of the fault switches
	// qcopy-roundtrip / qcopy-armed / qcopy-abort is on, the Find copies Find/Change's query in memory, states it back and
	// says what came back (KFCQueryCopier::DiagProbe) - and searches nothing.
	if (again == nil && KFCQueryCopier::DiagProbe(outSummary))
		return 0;
#endif

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
	KFC_DIAG_PHASE(phaseSearch, "search");	// a test build's timer (KFCDiag.h)
	KFC_DIAG_COMMANDS(commandsSearch, "search");	// ...and its command count (KFCDiagCommands.h)

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

	// The tab that searches by COLOUR (the Colour tab - the only one turned away: the Object tab's page items are
	// listed by InDesign's own object search, KFCObjectSearch). InDesign walks it with a walker of its own
	// (kColorSearchWalkerService), whose finds this panel has no rows for - so there is nothing for it to list.
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
		outSummary.Append(" tab. This panel does not search by colour - use InDesign's own Find/Change.");
		return 0;
	}
	const bool objectTab = (tab == IFindChangeOptions::kObjectSearch);	// the Object tab - KFCObjectSearch

	// Transliterate - the CJK character-type conversion (Kanji / kana / half- and full-width) - is
	// walked like any other text tab (the author's call: whatever the official panel is set to, this
	// panel searches and replaces the same way). It rides the same text walker and the same
	// find/replace commands; what it needs is its axes stated before the walk - the ChangeMode and the
	// find character type - which CommitSearchMode does, the exact glyph rule again. (On a
	// Roman-featureset install the tab cannot be reached at all, so the transliterate paths simply lie
	// dormant there.)

	// (The Object tab always has a query: its Type, and Find Object Format when one is set - an empty one lists every
	//  frame of the Type, the way InDesign's own Find Next walks them; the regression case obj-empty-find.)
	if (!objectTab && !HasFindQuery())
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
	RunScope runScope;
	if (again != nil)
	{
		// SEARCH THESE RESULTS AGAIN: the scope the list was searched with, not Search: as it stands (SearchListAgain
		// refused a part of a document, and found its document or its book open).
		runScope.fromBook = again->fromBook;
		runScope.allDocuments = again->allDocuments;
		runScope.selectionScope = IWalkerScopeFactoryUtils::kDocumentScope;
	}
	else if (!(objectTab ? KFCObjectSearch::ResolveObjectRunScope(runScope, outSummary)
			: KFCSearchEngine::ResolveRunScope(runScope, outSummary)))
		return 0;
	const bool fromBook = runScope.fromBook;
	const bool allDocuments = runScope.allDocuments;
	const IWalkerScopeFactoryUtils::WalkScopeType selectionScope =
		static_cast<IWalkerScopeFactoryUtils::WalkScopeType>(runScope.selectionScope);
	const PMString fellBackNote(runScope.fellBackNote);

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
	// NOT FOR THE OBJECT TAB: its walk is the service's own object search (SearchObject) over the shared object walker,
	// which no text walker mode steers - CommitSearchMode states the text walker's tabs only and turns the Object tab
	// away by design. Measured: KT's object walks found the Object tab's matches with no mode committed and the
	// dialog's mode on Text (the object search plan's Task 1 M4 - docs/superpowers/plans/2026-10-09-kfc-object-search.md).
	if (!objectTab && !KFCSearchEngine::CommitSearchMode())
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
	// closed - ReleaseHeldDoc's verdicts, unchanged.
	//
	// THE LIST GOES ON AS A TIER, NOT AWAY (the tier switch - docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md
	// 3-1): kept whole before anything below lets it go - its rows, its result set, its book, the query it was searched with
	// (KFCTierStore::ParkCurrent decides whether it is one to keep).
	// SEARCH THESE RESULTS AGAIN KEEPS THE LIST WHERE IT IS (SearchListAgain): not parked - it is the same tier, searched
	// anew - and copied whole first, its rows on their foci as they stand, to be put back if the search does not go
	// through (putBack, below).
	KFCResultModel::ModelSnapshot before;
	int32 reserved = 0;		// the row eye: the kept rows' room (below)
	PMString beforeBook;
	beforeBook.SetTranslatable(kFalse);
	if (again != nil)
	{
		for (int32 c = 0; c < KFCResultModel::GetChapterCount(); ++c)
			FollowChapterFoci(c);
		KFCResultModel::TakeModelSnapshot(before);
		(void)KFCBookScope::GetSearchedBookPath(beforeBook);
		// THE ROW EYE: the rows with their eye on stay through the search again - their room is kept from the walk's.
		for (size_t c = 0; c < before.chapters.size(); ++c)
			for (size_t i = 0; i < before.chapters[c].hits.size(); ++i)
				if (before.chapters[c].hits[i].kept)
					++reserved;
	}
	else
		KFCTierStore::ParkCurrent();
	KFCBookScope::ReleaseHeldDocs(true /*close now*/);
	KFCSearchEngine::DropResults();
	// ...the same result set again, in a new layout (Clear numbered both anew): the tier keeps its place (v2 R4), and the
	// writes kept for its old rows are no longer followed (UNDO-14, as after Search This Document Again).
	if (again != nil)
		KFCResultModel::ReturnToResultSet(again->set);
	outCommitted = true;
	// WHAT A SEARCH AGAIN THAT DOES NOT GO THROUGH OWES THE LIST: its rows, its result set, its book and its Find Format
	// as they were - the chapters the walk opened handed back first.
	const auto putBack = [&]()
	{
		KFCBookScope::ReleaseSearchedBook();
		KFCResultModel::RestoreModelSnapshot(before);
		KFCResultModel::ReturnToResultSet(again->set);
		if (!beforeBook.IsEmpty())
			KFCBookScope::SetSearchedBookPath(beforeBook);
		RememberFindFormatOf(before.header.query.get());
		outCommitted = false;
	};

	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	// How much of the book the run takes (Find/Change Selected Documents (Book)): selected 0 = all of it.
	KFCBookScope::BookSelection selection;
	// Chapters the book could not hand over at all. Declared out here so the summary can name them
	// whichever way this run ends - including the "no matches" and "nothing openable" exits, where
	// they are the only thing that explains what happened.
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	if (fromBook)
	{
		// Listed, not opened: each chapter is opened when its turn comes in the loop below and
		// handed straight back once it has been walked, so a book search never holds more than one
		// chapter of its own. Whether a chapter can actually be opened is not known yet - the
		// summary reports the ones that could not, after the walk. (Only the Book panel's selected
		// chapters, when the toggle narrows the run - ListBookChapters decides.)
		if (!KFCBookScope::ListBookChapters(targets, bookName, &selection, again != nil ? &again->bookPath : nil)
			|| targets.empty())
		{
			// "That book", not "the active book": a run is against the book the BOOK PANEL is showing,
			// and only falls back to the active one when no panel can be reached
			// (KFCBookScope::ResolveTargetBook).
			// (An empty book is refused at the front door. This is the safety net - see the note at the
			// end of ListBookChapters - and the same sentence.)
			if (again != nil)
				putBack();
			outSummary.Append(kBookHasNoChapters);
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
			if (again != nil)
				putBack();
			outSummary.Append(kNoDocumentToSearch);
			return 0;
		}
	}
	else if (again != nil && again->hasDoc)
		targets.push_back(again->doc);		// SEARCH THESE RESULTS AGAIN: the list's own document, found open
	else
	{
		// Re-read rather than carried down from the check above: a command has been processed since
		// (CommitSearchMode), and a pointer to the active document is not ours to assume survived it.
		// (Story / To End of Story / Selection walk this one too: the selection is the active document's.)
		IDocument* doc = KFCBookScope::ActiveDocument();
		if (doc == nil)
		{
			if (again != nil)
				putBack();
			outSummary.Append(kNoDocumentToSearch);
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

	// ...and WHICH TAB was searched. A row's Replace re-walks the row's story, and a re-walk in another
	// mode returns another set of matches - so the Replace compares this against the tab in force
	// then and refuses rather than lining rows up with the wrong occurrences.
	//
	// Read again rather than reusing `tab` from the top of this function, and that is deliberate:
	// `tab` was taken BEFORE CommitSearchMode, and what belongs on the results is the mode the walk
	// is about to actually run in. The two agree today - the commit states the value it just read -
	// but this is the one that would still be right on the day they stopped agreeing.
	KFCResultModel::SetSearchMode(KFCSearchEngine::CurrentSearchMode());

	// ...and the whole of what this walk was DRIVEN BY - the query plus every switch that decides
	// which matches come back. It is a key: a row's Replace compares it before it re-walks. The tab
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
	// changed. See RememberFindFormat.
	RememberFindFormat();

	// ...and the QUERY ITSELF, copied in memory (the tier switch - KFCQueryCopy): what is stated back into Find/Change when
	// this list, kept as a tier by a later search, is switched to. Read here, beside the signature and the format, for their
	// reason: on this side of CommitSearchMode. A copy that cannot be read is not put on - that tier then refuses a switch.
	// (SEARCH THESE RESULTS AGAIN keeps the list's own copy: the tier keeps its name, and its query - whatever Change to
	// the dialog holds.)
	if (again != nil)
		KFCResultModel::SetQueryCopy(before.header.query);
	else
	{
		KFCQueryCopy copy;
		KFCQueryCopier::Capture(copy);
		if (copy.valid)
			KFCResultModel::SetQueryCopy(std::make_shared<KFCQueryCopy>(copy));
	}

	CollectTally tally;
	if (objectTab)		// THE OBJECT TAB: page items, walked by InDesign's own object search one document at a time.
		KFCObjectSearch::CollectTargets(targets, fromBook, allDocuments, static_cast<int32>(selectionScope), unopenable, tally,
			reserved);
	else
		CollectTargets(targets, fromBook, allDocuments, selectionScope, unopenable, tally, reserved);

	// Cancelled: throw the half-finished result away rather than leave a partial list looking like a
	// complete one, and give the chapters back - the results that would have needed them are gone.
	// ReleaseHeldDocs schedules its closes, so it is safe to call from in here.
	if (tally.cancelled)
	{
		outSummary.Clear();
		outSummary.SetTranslatable(kFalse);
		if (again != nil)
		{
			putBack();		// SEARCH THESE RESULTS AGAIN: the list as it was
			outSummary.Append("Search These Results Again: cancelled - the rows are left as they were.");
			return 0;
		}
		KFCSearchEngine::DropResults();		// the rows, the book (its chapters closed) and the format
		outSummary.Append("Search cancelled.");
		return 0;
	}

	// THE ROW EYE (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-2): Search These Results Again carries
	// the rows of the list as it was whose eye is on into the new one, each to its own document (CarryKeptRowsByDocument)
	// - ahead of the foci, which attach to them with the rest.
	int32 keptStay = 0;
	if (again != nil)
		keptStay = CarryKeptRowsByDocument(before, targets);

	// THE ROWS' FOCI (spec T6 a): every chapter whose document is still open - the documents of a document-scope search,
	// a book's chapters the user has open. The chapters a book search opened itself were handed back in the loop.
	KFCRowFoci::AttachOpenChapters();
	// (SEARCH THESE RESULTS AGAIN says so ahead of the search's own words.)
	if (again != nil)
		outSummary.Append("Searched these results again: ");

	// Every chapter KFC opened has already been handed back inside the loop - a book search leaves
	// nothing of its own open, and no .indd locked. The rows carry their chapter's file, so a jump
	// or a replace reopens whatever it needs (KFCJump's EnsureChapterReachable and the replace
	// engine's own reopen). What that costs is a document load on the first click into a chapter;
	// what it buys is that searching a book does not leave twenty hidden documents behind.

	// The chapters the run could not fully account for, said the same way whichever way the summary
	// ends - "No matches" is a lie too if a chapter was skipped, or a walk broke off before its end.
	PMString chapterNotes;
	chapterNotes.SetTranslatable(kFalse);
	AppendUnsearchableNote(chapterNotes, tally.unsearchable);
	AppendSearchErrorNote(chapterNotes, tally.brokeOff);
	KFCBookScope::AppendUnopenableNote(chapterNotes, unopenable);
	KFCBookScope::AppendUnclosedNote(chapterNotes, tally.unclosed);

	// No matches: a plain, friendly line rather than "0 hit(s) in 0 chapter(s)".
	if (tally.total == 0)
	{
		outSummary.Append("No matches");
		if (fromBook && selection.selected > 0)
		{
			// the selected documents were all that was looked at (Find/Change Selected Documents (Book))
			outSummary.Append(" in the ");
			outSummary.AppendNumber(selection.selected);
			outSummary.Append(" selected document(s) of book \"");
			outSummary.Append(bookName);
			outSummary.Append("\".");
		}
		else if (fromBook)
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
		AppendKeptStay(outSummary, keptStay);	// the row eye: what stayed of the list as it was
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
	outSummary.AppendNumber(tally.total);
	outSummary.Append(" hit(s)");
	if (fromBook && selection.selected > 0)
	{
		// "in M of S selected document(s) (T in the book)" (the query dialog's spec, section 4-4): M held a hit, S were
		// the Book panel's selected documents - all that was looked at - and T is the book, so the narrowing is said.
		outSummary.Append(" in ");
		outSummary.AppendNumber(tally.chaptersWithHits);
		outSummary.Append(" of ");
		outSummary.AppendNumber(static_cast<int32>(targets.size()));
		outSummary.Append(" selected document(s) (");
		outSummary.AppendNumber(selection.total);
		outSummary.Append(" in the book).");
	}
	else if (fromBook)
	{
		// "in M of T chapter(s)": M chapters held a hit, T chapters were looked at. Without the T
		// there is no way to tell a book whose other chapters simply had no matches from a book
		// whose other chapters were never searched.
		outSummary.Append(" in ");
		outSummary.AppendNumber(tally.chaptersWithHits);
		outSummary.Append(" of ");
		outSummary.AppendNumber(static_cast<int32>(targets.size()));
		outSummary.Append(" chapter(s).");
	}
	else if (allDocuments)
	{
		// the same "M of T" for All Documents: the documents that held none are not listed
		outSummary.Append(" in ");
		outSummary.AppendNumber(tally.chaptersWithHits);
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

	// One limit (F9): collected = drawn.
	if (tally.truncated)
	{
		// THE LIMIT (F9): the rows are a list to walk and replace one at a time - more than this is a Change All's
		// work: KFC's own for a book, InDesign's Find/Change dialog's for anything else (F18).
		outSummary.Append(" Stopped at the ");
		outSummary.AppendNumber(KFCResultModel::kKFCCollectHitLimit);
		outSummary.Append(" limit - narrow the search, or use ");
		if (fromBook)
			outSummary.Append(KFCChangeAll::CommandName());				// Change All in Book (No List) - F18
		else
			outSummary.Append("Change All in InDesign's Find/Change");	// a document's: InDesign's own (F18)
		outSummary.Append(".");
	}

	AppendKeptStay(outSummary, keptStay);	// the row eye: what stayed of the list as it was
	outSummary.Append(chapterNotes);

	// How to replace (spec F19): a hit row's Replace - its right-click menu, or Return on a selected row
	// (F17) - is the one write from the list, and nothing else on screen says so (the user's request).
	// Shift+Return - replace, then on to the next row - is said too (the author's call).
	//
	// Last, after the warnings: it is an offer, not something that went wrong, and the status field
	// truncates its tail when it has to.
	outSummary.Append(" Replace a row with its right-click menu, or select it and press Return."
		" Shift+Return replaces it and moves on to the next row.");
	return tally.total;
}

bool KFCSearchEngine::HasFindQueryNow()
{
	// THE OBJECT TAB: its query is Find Object Format - IsThereSomethingToFind answers no there even with one set
	// (measured in KT - the object search plan's Task 1 M7). What Change All in Book's grey state and door, and the query
	// run, ask.
	if (CurrentSearchMode() == IFindChangeOptions::kObjectSearch)
		return KFCObjectSearch::HasFindObjectFormat();
	return HasFindQuery();
}

KFCSearchEngine::WalkerStops::~WalkerStops()
{
	if (fWalker != nil && fWalker->IsWalking())
		fWalker->Halt();
}

bool KFCSearchEngine::AcquireWalker(InterfacePtr<ITextWalker>& outWalker, InterfacePtr<ITextWalkerSelectionUtils>& outSelUtils)
{
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(registry != nil
		? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss) : nil);
	InterfacePtr<ITextWalker> shared(provider, UseDefaultIID());
	if (shared == nil)
		return false;
	InterfacePtr<ITextWalkerSelectionUtils> selUtils(shared, UseDefaultIID());
	outSelUtils.reset(selUtils.forget());
	// (Fault switch own-walker, a test build's only - KFCDiag.h: a kBasicTextWalkerBoss of KFC's own, the way KFC walked
	// before the author's call below - the speed-up's S1, docs/ai-notes/kfc-speedup-ideas-2026-10-05.md section 12.)
	if (KFC_DIAG_FAULT("own-walker"))
	{
		InterfacePtr<ITextWalker> mine(::CreateObject2<ITextWalker>(kBasicTextWalkerBoss));
		if (mine == nil)
			return false;
		outWalker.reset(mine.forget());
		return true;
	}
	// THE SESSION'S SHARED WALKER, THE ONE THE SDK'S SNIPPET WALKS (SnpFindAndReplace.cpp:758-770) - the author's call: "the
	// same way as the official one". Its find composes the text it finds, a frame at a time, as InDesign's own does
	// (measured - the speed-up study above): what a row's frame - its page, "overset", its locks - is then read from.
	// What it costs, both measured: a search, a write or a jump that looks for its row again from this panel takes the
	// dialog's Find Next sequence back to the match it had found (work/note-scripts/2026-10-06-kfc-no-track/s1-findnext2.ps1:
	// Find Next found cat@8 again, not cat@16); and the composition costs the writing walk 20-35 % (the speed-up study, over
	// a write of thousands of rows - a row's Replace writes 300 at most). KFC's own walker (the switch above) composes
	// nothing.
	outWalker.reset(shared.forget());
	return true;
}

bool KFCSearchEngine::ScopeHasNothingToWalk(ITextWalkerScope* scope)
{
	// (See the header.) The cursor is read the way ListScopePieces reads one - a cursor of the scope's own, its range not
	// AddRef'd, the cursor given back - and only to see whether there is a first stretch at all.
	if (scope == nil)
		return false;
	const bool empty = (scope->IsEmpty() != kFalse);
	bool noStretch = false;
	if (!empty)
	{
		void* const cursor = scope->GetNewCursor(nil);
		if (cursor != nil)
		{
			noStretch = (scope->GetCursorRange(cursor, nil, nil) == nil);
			scope->ReleaseCursor(cursor);
		}
	}
	KFC_DIAG_LOG("SCOPEWALK empty=%d chars=%d noStretch=%d", empty ? 1 : 0, (int)scope->GetCharTotal(), noStretch ? 1 : 0);
	// (Fault switch scope-walk-off, a test build's only - KFCDiag.h: no scope is said to be empty, the old way - the walk
	// meets InDesign's kFailure again - the armed cases.)
	if (KFC_DIAG_FAULT("scope-walk-off"))
		return false;
	return empty || noStretch;
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
	{
		// a part of the book selected in the Book panel, with the toggle on: the run takes those alone (asked of the
		// one place that narrows the run, KFCBookScope's, so the name and the run cannot differ)
		PMString bookName;
		KFCBookScope::BookSelection selection;
		if (KFCBookScope::DescribeTargetBook(bookName, selection) && selection.selected > 0)
			return "Find in Selected Documents";
		return "Find in Book";
	}
	// what the search will make of Search: with this selection - the dialog's own display
	// The Object tab's Search: is read its own way (KFCObjectSearch::ObjectSearchScope - page items, not text: Document,
	// All Documents or Selection, which the names below cover).
	PMString note;
	const int32 scope = (CurrentSearchMode() == IFindChangeOptions::kObjectSearch)
		? KFCObjectSearch::ObjectSearchScope(note) : SearchScopeForSelection(CurrentSearchScope());
	switch (scope)
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
	// Colour searches swatch use, with a walker of its own, and finds no text and no frame this panel lists. The Object
	// tab is searched (KFCObjectSearch). Every other value (including -1, settings unreadable) is left for the search to
	// answer.
	return mode != IFindChangeOptions::kColorSearch;
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

namespace
{
typedef std::vector<std::pair<TextIndex, TextIndex> > Places;	// [start, end), in position order

// A place another row of the chapter already stands on is that row's (both ways of looking a row up again).
bool PlaceTakenByAnotherRow(int32 chapterIdx, int32 hitIdx, UID storyUID, TextIndex start, TextIndex end)
{
	const int32 hitCount = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		UID s2 = kInvalidUID;
		TextIndex a2 = kInvalidTextIndex, b2 = kInvalidTextIndex;
		uint64 h2 = 0;
		if (i != hitIdx && KFCResultModel::GetHitMatchIdentity(chapterIdx, i, s2, a2, b2, h2)
			&& s2 == storyUID && a2 == start && b2 == end)
			return true;
	}
	return false;
}

// WHAT "READS AS THE ROW" MEANS WHEN A ROW IS LOOKED FOR AGAIN (spec T1, T14): the whole match the same - its hash and
// its length - and the line around it the same, the text before it and after it. The places a row could stand at now
// (StaleRowPlaces, ReplacedRowPlaces) and the rows that look like it (LookAlikeRows) are told by this one test: the
// order rule (PickByOrder) compares how many of each there are, so the two counts must never be made by different
// tests. (A place the row is read AT - RowReadsAsFoundAt - reads the story there instead, MatchIsSameOccurrence's way.)
bool ReadsAlike(uint64 hash, int32 length, const PMString& pre, const PMString& post, uint64 rowHash, int32 rowLength,
	const KFCResultModel::RowDisplay& row)
{
	return hash == rowHash && length == rowLength && pre == row.preText && post == row.postText;
}

// A REPLACED ROW IS LOOKED FOR AGAIN BY WHAT ITS REPLACE WROTE
// (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F4). The search's query no longer finds it, so
// the candidates are the places in its story where the text it wrote stands (Hit::replacedText, whole), each read the
// way the search reads a hit (ReadHitText) and taken only with the row's own hash and line - both read again when
// its replace was written. The story is read as code points, which is what a TextIndex counts. Every candidate, in
// position order - which of them the row is, is the caller's question (OneFreePlace, PickByOrder).
bool ReplacedRowPlaces(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef, Places& outPlaces)
{
	outPlaces.clear();
	PMString written;
	KFCResultModel::RowDisplay row;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	if (!KFCResultModel::GetHitWrittenText(chapterIdx, hitIdx, written) || written.IsEmpty()
		|| !KFCResultModel::GetHitRow(chapterIdx, hitIdx, row)
		|| !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash))
		return false;
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	if (model == nil)
		return false;
	const std::vector<uint32> key = CodePointsOf(written);
	const std::vector<uint32> text = CodePointsAt(model, 0, model->TotalLength());
	if (key.empty() || text.size() < key.size())
		return false;
	const int32 len = static_cast<int32>(key.size());
	for (size_t p = 0; p + key.size() <= text.size(); ++p)
	{
		if (!std::equal(key.begin(), key.end(), text.begin() + static_cast<std::ptrdiff_t>(p)))
			continue;
		const TextIndex at = static_cast<TextIndex>(p);
		KFCResultModel::Hit cand;
		ReadHitText(storyRef, at, at + len, cand);
		// (The length on both sides is the written text's: every candidate is that text, and a replaced row's range is
		// the one its written text was read from - SetHitWrittenText and MarkHitReplaced are handed the one range.)
		if (ReadsAlike(cand.matchHash, len, cand.preText, cand.postText, hash, len, row))
			outPlaces.push_back(std::make_pair(at, at + len));
	}
	return true;
}

// A ROW NOT REPLACED IS LOOKED FOR AGAIN UNDER ITS QUERY: the row's own story walked again (forward, as the search
// was, with the scope switches), each match given its line and hash, and taken only with the row's own - the same
// text, the same length, the same line. Every candidate, in position order (the walk meets a table's cells in reading
// order - memory walker-scope-options-and-hidden-layers). False when the story cannot be walked or holds more matches
// than one look again lists (CollectStoryHits), and when the query changed since the search: another query would
// find other matches, nothing to compare with - ASKED, NOT REFUSED: QueryUnchangedSinceSearch is RefuseChangedQuery's
// question without its consequences - it states the tab the walk runs in, and clears nothing. (RefuseChangedQuery
// itself would, on a changed query, clear the whole result set and hand the chapters back in the middle of a jump,
// the tree left drawing rows the model no longer held.)
bool StaleRowPlaces(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef, Places& outPlaces)
{
	outPlaces.clear();
	if (!KFCReplaceEngine::QueryUnchangedSinceSearch())
		return false;
	KFCResultModel::RowDisplay row;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	if (!KFCResultModel::GetHitRow(chapterIdx, hitIdx, row)
		|| !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash))
		return false;
	std::vector<KFCResultModel::Hit> hits;
	{
		KFCForwardSearchScope forward;
		WalkerScopeOptions scopeOptions;
		KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
		if (!CollectStoryHits(storyRef, scopeOptions, hits))
			return false;
	}
	for (size_t h = 0; h < hits.size(); ++h)
	{
		const KFCResultModel::Hit& cand = hits[h];
		if (cand.storyUID == storyRef.GetUID()
			&& ReadsAlike(cand.matchHash, cand.textEnd - cand.textStart, cand.preText, cand.postText, hash, b - a, row))
			outPlaces.push_back(std::make_pair(cand.textStart, cand.textEnd));
	}
	std::sort(outPlaces.begin(), outPlaces.end());
	return true;
}

// Every place that reads as the row now, whichever kind of row it is. False = nothing could be collected (the story
// is gone, a replaced row wrote nothing, or the query changed since the search).
bool LookAlikePlacesNow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID story, Places& outPlaces)
{
	outPlaces.clear();
	IDataBase* const db = docRef.GetDataBase();
	bool replaced = false, locked = false;
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story)
		|| !KFCResultModel::GetHitFlags(chapterIdx, hitIdx, replaced, locked))
		return false;
	const UIDRef storyRef(db, story);
	return replaced ? ReplacedRowPlaces(chapterIdx, hitIdx, storyRef, outPlaces)
		: StaleRowPlaces(chapterIdx, hitIdx, storyRef, outPlaces);
}

// THE RULE FOR MOVING A ROW (JMP-14): exactly one candidate that no other row stands on - a guess between two
// look-alikes would be worse than saying so. The one rule that writes a found place into the row (MoveRowTo); a
// look-alike chosen by order is the jump's only (PickByOrder, spec T3).
bool OneFreePlace(int32 chapterIdx, int32 hitIdx, UID story, const Places& places, TextIndex& outStart, TextIndex& outEnd)
{
	int32 count = 0;
	for (size_t i = 0; i < places.size() && count < 2; ++i)
	{
		if (PlaceTakenByAnotherRow(chapterIdx, hitIdx, story, places[i].first, places[i].second))
			continue;
		outStart = places[i].first;
		outEnd = places[i].second;
		++count;
	}
	return count == 1;
}

// The row moved to [start, end): its place, its line read again there (so it draws what stands there), a Missing word
// taken off (found after all - SetHitOutcome turns a replaced row away, which never had one), and its focus with it
// (spec T4).
void MoveRowTo(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef, TextIndex start, TextIndex end)
{
	KFCResultModel::Hit found;
	ReadHitText(storyRef, start, end, found);
	KFCResultModel::SetHitRange(chapterIdx, hitIdx, storyRef.GetUID(), start, end);
	KFCResultModel::SetHitSegments(chapterIdx, hitIdx, found.preText, found.matchText, found.postText, found.matchHash);
	if (KFCResultModel::GetHitOutcome(chapterIdx, hitIdx) == KFCResultModel::kOutcomeMissing)
		KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeNone);
	KFCRowFoci::Reanchor(chapterIdx, hitIdx);
}

// A ROW'S LOOK-ALIKE ROWS (spec T14): the rows of its chapter and story that read the same - the whole match (its hash
// and length), the line around it - and are replaced, or not, as it is; itself included, in the order the search found
// them in the story (Hit::storyOrdinal). outRank = this row's place among them; -1 = not resolvable.
void LookAlikeRows(int32 chapterIdx, int32 hitIdx, std::vector<int32>& outRows, int32& outRank)
{
	outRows.clear();
	outRank = -1;
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	KFCResultModel::RowDisplay me;
	if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash) || !KFCResultModel::GetHitRow(chapterIdx, hitIdx, me))
		return;
	std::vector<std::pair<int32, int32> > byOrder;	// (storyOrdinal, hit)
	const int32 n = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < n; ++i)
	{
		UID s = kInvalidUID;
		TextIndex x = kInvalidTextIndex, y = kInvalidTextIndex;
		uint64 h = 0;
		KFCResultModel::RowDisplay r;
		if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, i, s, x, y, h) || s != story
			|| !KFCResultModel::GetHitRow(chapterIdx, i, r) || !ReadsAlike(h, y - x, r.preText, r.postText, hash, b - a, me)
			|| r.replaced != me.replaced)
			continue;
		byOrder.push_back(std::make_pair(KFCResultModel::GetHitStoryOrdinal(chapterIdx, i), i));
	}
	std::sort(byOrder.begin(), byOrder.end());
	for (size_t k = 0; k < byOrder.size(); ++k)
	{
		outRows.push_back(byOrder[k].second);
		if (byOrder[k].second == hitIdx)
			outRank = static_cast<int32>(k);
	}
}

// ONE OF SEVERAL LOOK-ALIKES, BY ORDER (spec T14 - the author's call). `places` = every place in the row's
// story that reads as the row now, in position order. When there are as many as the row has look-alike rows, the
// row's place is the one at its rank among them (Hit::storyOrdinal - the order the search found them in, which no
// insert or delete changes); when the counts differ, a look-alike came or went and which one cannot be told - none.
// No "another row stands there" test: the other rows' stored places are the search's, stale after the very edit this
// answers (spec T2, 3rd edition), and the ranks are distinct, so two rows are never sent to one place.
bool PickByOrder(int32 chapterIdx, int32 hitIdx, const Places& places, TextIndex& outStart, TextIndex& outEnd)
{
	std::vector<int32> rows;
	int32 rank = -1;
	LookAlikeRows(chapterIdx, hitIdx, rows, rank);
	if (rank < 0 || places.size() != rows.size() || static_cast<size_t>(rank) >= places.size())
		return false;
	outStart = places[static_cast<size_t>(rank)].first;
	outEnd = places[static_cast<size_t>(rank)].second;
	return true;
}
}	// anonymous namespace

// ROWS FOLLOW THEIR FOCI (docs/superpowers/plans/2026-10-14-kfc-row-focus-undo.md D5 - the author's call of 2026-10-14
// night). A row's focus is of KFC's own class (KFCRowFocus.h): it has followed every edit of its story and every Undo
// and Redo since it was made, so while it is not lost it stands where the row's text is - more surely than the row's
// stored place, which follows KFC's own writes only, and more surely than the story's version, which an Undo takes back
// to a number a different later edit can reach again (the experiment of 10-14: an Undo, then typing elsewhere). So the
// rows of the story in this chapter take their foci's places:
//   - a row not replaced whose focus reads its match (hash and length): moved there, its line read again (MoveRowTo -
//     a Missing word taken off);
//   - a row not replaced whose focus reads another text - its own match was edited: Missing put on it, and its
//     stored place kept as found - so the length its focus is compared with stays the match's (measured live: a place
//     moved onto the edited text kept that length, and the row stayed Missing after the edit was undone). An Undo of
//     the edit brings the focus back over the match, and the next follow moves the row there and takes Missing off;
//   - a replaced row: moved where its focus reads what its replace wrote (Hit::replacedText); left where it is
//     otherwise (its jump then looks for what it wrote - LocateRow's old way);
// and the story row's words are read again (RereadStoryLead - the author, 10-14 night: "the Story part's text is
// getting different"), and the story is recorded at its version as it stands. QUICK: at the version on record with
// every focus on its row's
// place, nothing is done. kFollowNone = no row of this story here has a focus (a chapter whose story KFC did not know
// when it opened): the version rule decides, as it always has. kFollowBroken = a row without a usable focus among rows
// that have one, a lost focus, or a version that cannot be read: nothing is moved - a write is refused, a jump takes
// the old way.
KFCSearchEngine::FollowResult KFCSearchEngine::FollowStoryFoci(int32 chapterIdx, IDataBase* db, UID story, bool* outMoved)
{
	if (outMoved != nil)
		*outMoved = false;
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return kFollowBroken;
	struct Row
	{
		int32		hit;
		TextIndex	a, b;		// its stored place
		uint64		hash;
		bool		replaced;
		TextIndex	fs, fe;		// its focus's place
	};
	std::vector<Row> rows;
	int32 focused = 0, lost = 0, none = 0;
	const int32 n = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < n; ++i)
	{
		Row r;
		UID s = kInvalidUID;
		bool locked = false;
		r.hit = i;
		r.a = r.b = r.fs = r.fe = kInvalidTextIndex;
		r.hash = 0;
		r.replaced = false;
		if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, i, s, r.a, r.b, r.hash) || s != story
			|| !KFCResultModel::GetHitFlags(chapterIdx, i, r.replaced, locked))
			continue;
		const KFCRowFoci::FocusState state = KFCRowFoci::Where(chapterIdx, i, db, r.fs, r.fe);
		if (state == KFCRowFoci::kFocused)
			++focused;
		else if (state == KFCRowFoci::kLostFocus)
			++lost;
		else
			++none;
		rows.push_back(r);
	}
	if (focused == 0 && lost == 0)
		return kFollowNone;
	uint32 recorded = 0, now = 0;
	const bool had = KFCResultModel::GetStoryVersion(chapterIdx, story, recorded);
	if (lost > 0 || none > 0 || !ReadStoryVersion(db, story, now))
	{
		KFC_DIAG_LOG("FOLLOW chapter=%d story=%u broken: focused=%d lost=%d none=%d", chapterIdx, story.Get(),
			(int)focused, (int)lost, (int)none);
		return kFollowBroken;
	}
	bool inPlace = true;
	for (size_t k = 0; k < rows.size() && inPlace; ++k)
		inPlace = (rows[k].fs == rows[k].a && rows[k].fe == rows[k].b);
	if (inPlace && had && recorded == now)
		return kFollowed;
	const UIDRef storyRef(db, story);
	int32 moved = 0, edited = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		const Row& r = rows[k];
		if (r.replaced)
		{
			PMString written;
			if (KFCResultModel::GetHitWrittenText(chapterIdx, r.hit, written) && ReadText(storyRef, r.fs, r.fe - r.fs) == written)
			{
				MoveRowTo(chapterIdx, r.hit, storyRef, r.fs, r.fe);
				++moved;
			}
			continue;
		}
		KFCResultModel::Hit there;
		ReadHitText(storyRef, r.fs, r.fe, there);
		if (r.fe - r.fs == r.b - r.a && there.matchHash == r.hash)
		{
			MoveRowTo(chapterIdx, r.hit, storyRef, r.fs, r.fe);
			++moved;
		}
		else
		{
			KFCResultModel::SetHitOutcome(chapterIdx, r.hit, KFCResultModel::kOutcomeMissing);	// its stored place kept
			++edited;
		}
	}
	RereadStoryLead(chapterIdx, storyRef);	// the story row says what the story says now
	KFCResultModel::SetStoryVersion(chapterIdx, story, now);
	KFC_DIAG_LOG("FOLLOW chapter=%d story=%u version %s%u -> %u rows=%d moved=%d edited=%d", chapterIdx, story.Get(),
		had ? "" : "none:", recorded, now, (int)rows.size(), (int)moved, (int)edited);
	if (outMoved != nil)
		*outMoved = true;
	return kFollowed;
}

// A LIST SHOWN AGAIN (a tier switched to - KFCTierStore::SwitchTo): its rows follow their foci at once, story by story,
// so the rows and the story rows draw the text as it is now - while the list was grey the edits moved its foci, not
// its rows (another tier's write among them). Only a chapter whose document is open (found by its file, as the foci
// are attached - KFCRowFoci::AttachChapter); a story that cannot be followed is left as it is (FollowStoryFoci).
void KFCSearchEngine::FollowChapterFoci(int32 chapterIdx)
{
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KFCBookScope::FindOpenChapterDoc(file, docRef))
		return;
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return;
	std::set<UID> stories;
	const int32 n = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < n; ++i)
	{
		UID story = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 hash = 0;
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, i, story, a, b, hash) && story != kInvalidUID)
			stories.insert(story);
	}
	for (std::set<UID>::const_iterator s = stories.begin(); s != stories.end(); ++s)
		(void)FollowStoryFoci(chapterIdx, db, *s);
}

// FIRST, THE ROWS FOLLOW THEIR FOCI (FollowStoryFoci, above - the author's call of 2026-10-14 night): a story whose
// foci all followed every edit and every Undo is where they say, and nothing below is asked. What follows is the
// way for a story with no foci, or with one that met an Undo it could not follow - the focus a hint only.
//
// WHERE IS THIS ROW NOW? (spec T2) One question for the jump and the selection a click makes - HERE, NOT IN THE JUMP
// (KFCJump.cpp): walking a story and putting a row right is the model half's work; the jump asks it through IKFCRuns.
// The row's stored place and its text focus's place are both read against the row (RowReadsAsFoundAt). One of them:
// that one. Both, at different places (look-alikes - one reached by a focus an Undo left behind, or a stored place
// another look-alike slid under): the look-alikes now are counted against the rows - the same count, by order (an
// Undo left the focus behind); not the same, the focus (one came or went before the row, and the focus followed that
// edit). Neither: the story looked through again - the one free candidate moves the row there (OneFreePlace), else
// the order rule.
//
// A ROW WHOSE PLACE HAS MOVED UNDER IT IS LOOKED FOR AGAIN (the author's call). The rows keep their places themselves,
// and follow every change KFC makes - and an Edit > Undo / Redo of a write of KFC's own (KFCUndoFollow). Not the edits
// that are not followed: typing, an Undo of anything else, the Track Changes panel, a script - which move the text
// without telling them, so a row's stored place can be off although its text has not been touched. (Measured before
// Undo was followed: an edit before the second of two matches, taken back by Ctrl+Z - the row's place was one
// character off, and the jump called it "missing".) So before the jump gives up on a row, the story is walked again
// under the same query, and the row moves to the ONE match that is the same text with the same line around it (the
// three segments the row drew) and that no other row stands on - OneFreePlace; MoveRowTo moves its focus with it.
// None, or more than one, and it is not moved: more than one is decided by order (spec T14), for that jump only. A
// replaced row is looked for by what it wrote (ReplacedRowPlaces): the query no longer finds it.
//
// A PLACE FOUND THROUGH THE FOCUS OR BY ORDER IS THE JUMP'S ONLY (spec T3, 3rd edition): the row's stored place stays.
// The Replace trusts that place whenever the story is at the version KFC recorded, and an Undo of the user's edit
// brings the version back - a stored place moved onto a look-alike in between would be written there (case
// tf-undo-replace-right-dup). Whatever decides, the row's focus goes to the place found (T4).
KFCResultModel::RowLocation KFCSearchEngine::LocateRow(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef,
	TextIndex& ioStart, TextIndex& ioEnd)
{
	IDataBase* const db = docRef.GetDataBase();
	UID story = kInvalidUID;
	TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
	uint64 hash = 0;
	if (db == nil || !KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash))
		return KFCResultModel::kRowNotFound;
	// THE ROWS FOLLOW THEIR FOCI FIRST (FollowStoryFoci - the author's call of 2026-10-14 night). A story whose foci
	// are all kept is where they say: the row reads as found at its place, or its own text was edited there (Missing,
	// and the view goes there). Only a story with no foci, or with one that could not follow, takes the old way below.
	bool moved = false;
	if (FollowStoryFoci(chapterIdx, db, story, &moved) == kFollowed)
	{
		if (!KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, story, a, b, hash))
			return KFCResultModel::kRowNotFound;
		ioStart = a;
		ioEnd = b;
		if (!RowReadsAsFoundAt(chapterIdx, hitIdx, db, story, a, b))
		{
			// Its own text was edited (Missing - FollowStoryFoci keeps its stored place as found): the view goes where the
			// focus says that text is now.
			TextIndex fs = kInvalidTextIndex, fe = kInvalidTextIndex;
			if (KFCRowFoci::Where(chapterIdx, hitIdx, db, fs, fe) == KFCRowFoci::kFocused)
			{
				ioStart = fs;
				ioEnd = fe;
			}
			return KFCResultModel::kRowNotFound;
		}
		return moved ? KFCResultModel::kRowMoved : KFCResultModel::kRowAtPlace;
	}
	const bool atStored = RowReadsAsFoundAt(chapterIdx, hitIdx, db, story, a, b);
	TextIndex fs = kInvalidTextIndex, fe = kInvalidTextIndex;
	const bool atFocus = KFCRowFoci::Current(chapterIdx, hitIdx, db, fs, fe) && !(fs == a && fe == b)
		&& RowReadsAsFoundAt(chapterIdx, hitIdx, db, story, fs, fe);
	if (atStored && !atFocus)
	{
		KFCRowFoci::Reanchor(chapterIdx, hitIdx);	// a focus an Undo left behind is put right here
		return KFCResultModel::kRowAtPlace;
	}
	TextIndex toStart = kInvalidTextIndex, toEnd = kInvalidTextIndex;
	if (atFocus && !atStored)
	{
		toStart = fs;
		toEnd = fe;
	}
	else if (atStored && atFocus)
	{
		Places places;
		if (!LookAlikePlacesNow(chapterIdx, hitIdx, docRef, story, places))
		{
			// Nothing to count - the query changed since the search: the stored place.
			KFCRowFoci::Reanchor(chapterIdx, hitIdx);
			return KFCResultModel::kRowAtPlace;
		}
		if (!PickByOrder(chapterIdx, hitIdx, places, toStart, toEnd))
		{
			toStart = fs;	// the count changed: the focus followed the edit that changed it
			toEnd = fe;
		}
		if (toStart == a && toEnd == b)
		{
			KFCRowFoci::Reanchor(chapterIdx, hitIdx);
			return KFCResultModel::kRowAtPlace;
		}
	}
	else
	{
		Places places;
		if (!LookAlikePlacesNow(chapterIdx, hitIdx, docRef, story, places))
			return KFCResultModel::kRowNotFound;
		TextIndex s = kInvalidTextIndex, e = kInvalidTextIndex;
		if (OneFreePlace(chapterIdx, hitIdx, story, places, s, e))
		{
			MoveRowTo(chapterIdx, hitIdx, UIDRef(db, story), s, e);		// OneFreePlace's rule - the row moves
			ioStart = s;
			ioEnd = e;
			return KFCResultModel::kRowMoved;
		}
		if (!PickByOrder(chapterIdx, hitIdx, places, toStart, toEnd))
			return KFCResultModel::kRowNotFound;
	}
	// Found elsewhere - for this jump only (T3): the stored place stays, the focus goes there, a Missing word goes.
	if (KFCResultModel::GetHitOutcome(chapterIdx, hitIdx) == KFCResultModel::kOutcomeMissing)
		KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeNone);	// found after all
	KFCRowFoci::MoveTo(chapterIdx, hitIdx, toStart, toEnd);
	ioStart = toStart;
	ioEnd = toEnd;
	return KFCResultModel::kRowElsewhere;
}

namespace
{
// THE DOOR BOTH SEARCHES AGAIN ASK: has the query changed since the search? Asked, not refused
// (QueryUnchangedSinceSearch - StaleRowPlaces' note): nothing is cleared, and the tab the walk then runs in is stated.
// false = changed, and the sentence said.
bool QueryUnchangedForAgain(PMString& outStatus)
{
	if (KFCReplaceEngine::QueryUnchangedSinceSearch())
		return true;
	outStatus.Append("the Find/Change settings have changed since the search - search again.");
	return false;
}

// WHAT BOTH SEARCHES AGAIN SAY ONCE THE ROWS ARE BACK: what was searched again ("story", "document"), how many matches
// it holds now, how many rows with their eye on stayed beside them (the row eye - AppendKeptStay), and whether the list's
// limit cut it short.
void SayFoundAgain(PMString& outStatus, const char* what, int32 now, bool capped, int32 stayed)
{
	outStatus.Clear();
	outStatus.Append("Searched this ");
	outStatus.Append(what);
	outStatus.Append(" again: ");
	outStatus.AppendNumber(now > 0 ? now : 0);
	outStatus.Append(" match(es).");
	AppendKeptStay(outStatus, stayed);
	if (capped)
		outStatus.Append(" The list's limit of rows was reached - its other matches are not listed.");
}
}	// anonymous namespace

// SEARCH THIS STORY AGAIN (a story row's right-click menu - the author's call). A story edited since the
// search refuses its rows' Replace (REP-13 1: its version is not the one KFC recorded); this takes the edit in without
// searching the whole list again: the story alone is walked with the search's own query and switches - the search's
// own walk, over one story, with everything a row needs (kHitEverything) - its rows are put in the list as that walk
// finds them, and its version is recorded (KFCResultModel::ReplaceStoryRows), so they can be replaced. Rows already
// replaced in it go (the query no longer finds what they wrote - the author's call); the other stories' rows and
// their state stay.
// REFUSED when the query changed since the search (the story's rows would be another query's beside the others').
// A SEARCH OVER PART OF A STORY (Search: To End of Story / Selection - the author's idea: "keep where the
// search started with a text focus") walks that part again, not the whole story - the part the search recorded
// (KFCResultModel::SearchedRange), found again where the story stands now (FindSearchedPartAgain) and walked the way
// InDesign walks its own (QueryPartScope); then recorded again where it was found. Refused when the part cannot be
// walked on its own (InDesign walked it as separate stretches - MakeSearchedRange) or its edges cannot be told (the
// text just outside one was rewritten). A chapter the search handed back is reopened windowless for the walk and
// handed back after it, before the rows are put in (its foci then wait for its next open). The list keeps its limit
// (F9): the story gets the room the other rows leave it.
bool KFCSearchEngine::SearchStoryAgain(int32 chapterIdx, int32 groupIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	outStatus.Append("Search This Story Again: ");
	const UID story = KFCResultModel::GetGroupStory(chapterIdx, groupIdx);
	if (story == kInvalidUID)
	{
		outStatus.Append("that story row is no longer on the list.");
		return false;
	}
	const KFCResultModel::SearchScopeKind scope = KFCResultModel::GetSearchScope();
	const bool overPart = (scope == KFCResultModel::kScopeToEndOfStory || scope == KFCResultModel::kScopeSelection);
	KFCResultModel::SearchedRange part;
	if (overPart && (!KFCResultModel::GetSearchedRange(chapterIdx, story, part) || !part.walkable))
	{
		// (Under about 117 characters with its "Search This Story Again: " - the four lines the message area holds at the
		// panel's floor, KFCPanelMetrics.cpp: "such as table cells selected over more than one row" ran it to 192, and the
		// "search again" went. The case of separate stretches is MakeSearchedRange's.)
		outStatus.Append("the searched part cannot be searched again on its own (separate stretches) - search again.");
		return false;
	}
	// Asked, not refused - and the tab the walk below runs in stated (QueryUnchangedForAgain).
	if (!QueryUnchangedForAgain(outStatus))
		return false;
	// The chapter's document, by its file first (a UIDRef can outlive its document - KFCBookScope::ReachChapterDoc).
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KFCBookScope::ReachChapterDoc(file, docRef))
	{
		outStatus.Append("the document of this story could not be opened.");
		return false;
	}
	KFCResultModel::RebindChapterDoc(chapterIdx, docRef);
	IDataBase* const db = docRef.GetDataBase();
	const bool ours = KFCBookScope::IsHeldDoc(docRef);
	if (db == nil || !db->IsValidUID(story))
	{
		if (ours)
			(void)KFCBookScope::HandBackHeldDocNow(docRef);
		outStatus.Append("the story is no longer in its document - search again.");
		return false;
	}
	// The rows - the kept ones too (the row eye) - where their foci stand now: a kept row the walk does not find again
	// stays at that place (MergeKeptRows).
	(void)FollowStoryFoci(chapterIdx, db, story);
	// The room the list leaves: its limit less every row that is not this story's.
	int32 storyRows = 0;
	const int32 chapterRows = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < chapterRows; ++i)
	{
		UID s = kInvalidUID;
		TextIndex a = kInvalidTextIndex, b = kInvalidTextIndex;
		uint64 h = 0;
		if (KFCResultModel::GetHitMatchIdentity(chapterIdx, i, s, a, b, h) && s == story)
			++storyRows;
	}
	// ...and the room the story's rows with their eye on keep (the row eye - they stay, found or not).
	const int32 room = KFCResultModel::kKFCCollectHitLimit - (KFCResultModel::GetTotalHitCount() - storyRows)
		- KFCResultModel::CountKeptRows(chapterIdx, story);
	// The searched part, where it stands now.
	TextIndex partStart = kInvalidTextIndex, partEnd = kInvalidTextIndex;
	if (overPart && !FindSearchedPartAgain(chapterIdx, db, story, scope, part, partStart, partEnd))
	{
		if (ours)
			(void)KFCBookScope::HandBackHeldDocNow(docRef);
		outStatus.Append("the text at the edge of the searched part of this story has changed - search again.");
		return false;
	}
	std::vector<KFCResultModel::Hit> hits;
	bool capped = false;
	ChapterWalkResult walked = kChapterWalked;
	{
		// forward, as the search was
		KFCForwardSearchScope forward;
		WalkerScopeOptions scopeOptions;
		KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
		// A searched part's own scope - nil for a whole story, which CollectHitsInDoc walks by the story form.
		TempFocus partFocus;
		InterfacePtr<ITextWalkerScope> partScope(overPart
			? QueryPartScope(UIDRef(db, story), scope, partStart, partEnd, scopeOptions, partFocus) : nil);
		if (overPart && partScope == nil)
			walked = kChapterNoScope;
		else
			CollectHitsInDoc(UIDRef(db, db->GetRootUID()), static_cast<size_t>(room > 0 ? room : 0), scopeOptions,
				kHitEverything, hits, capped, walked, WalkBar(), UIDRef(db, story),
				IWalkerScopeFactoryUtils::kEmptyScope, nil, nil, partScope);
	}
	uint32 version = 0;
	const bool versionRead = KFCSearchEngine::ReadStoryVersion(db, story, version);
	// A chapter of ours goes back now - outside any sequence, with the walk over (HandBackHeldDocNow's rule) - and before
	// the rows go in, so they do not get foci on a document about to close.
	if (ours)
		(void)KFCBookScope::HandBackHeldDocNow(docRef);
	if (walked != kChapterWalked || !versionRead)
	{
		outStatus.Append("InDesign's search stopped with an error - the story's rows are left as they were.");
		return false;
	}
	// The part recorded where it was found, at the version it was walked at - before the rows go in, whose foci come
	// back with it (ReplaceStoryRows - KFCRowFoci::AttachChapter).
	if (overPart)
	{
		part.start = partStart;
		part.end = partEnd;
		part.version = version;
		KFCResultModel::SetSearchedRange(chapterIdx, story, part);
	}
	int32 stayed = 0;
	const int32 rows = KFCResultModel::ReplaceStoryRows(chapterIdx, story, hits, version, &stayed);
	SayFoundAgain(outStatus, "story", rows - stayed, capped, stayed);
	return true;
}

// SEARCH THIS DOCUMENT AGAIN (a document row's right-click menu - the author's call: "for a book search over
// several documents - the Object tab and every other search too"). Search This Story Again over a whole document: the
// document walked again with the search's own query and switches - the walk the search makes of each of its documents (a
// book's chapter, a document of All Documents or of Document; the Object tab's by KFCObjectSearch) - and all of its rows
// replaced by what that walk finds (KFCResultModel::ReplaceChapterRows), its stories' versions recorded, so they can be
// replaced. Its replaced rows go (the query no longer finds what they wrote); the other documents' rows and their state
// stay. Nothing found: the document's row stays, empty, to be searched again (the author's call).
// REFUSED - the list as it was - while a run is up; for a search over part of a document (Search: Story, To End of Story,
// Selection - the author's call: whole-document searches only; a story's part is Search This Story Again's); when the
// query changed since the search; when the document cannot be opened; when the walk breaks off or is cancelled.
// A chapter the search handed back is reopened windowless for the walk and handed back after it, before the rows go in.
// The list keeps its limit (F9): the document gets the room the other rows leave it. Under a bar of its own, with Cancel -
// a document can take as long as a search - so it counts as a search for every other door (gSearching).
bool KFCSearchEngine::SearchDocumentAgain(int32 chapterIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	outStatus.Append("Search This Document Again: ");
	UIDRef docRef;
	IDFile file;
	PMString name;
	int32 rowsNow = 0;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KFCResultModel::GetChapterDisplay(chapterIdx, name, rowsNow))
	{
		outStatus.Append("that document row is no longer on the list.");
		return false;
	}
	const char* part = nil;
	switch (KFCResultModel::GetSearchScope())
	{
		case KFCResultModel::kScopeStory:			part = "Story"; break;
		case KFCResultModel::kScopeToEndOfStory:	part = "To End of Story"; break;
		case KFCResultModel::kScopeSelection:		part = "Selection"; break;
		default:									break;
	}
	if (part != nil)
	{
		outStatus.Append("the search was over part of the document (Search: ");
		outStatus.Append(part);
		outStatus.Append(") - search again.");
		return false;
	}
	// Asked, not refused - and the tab the walk runs in stated (QueryUnchangedForAgain).
	if (!QueryUnchangedForAgain(outStatus))
		return false;
	// The document, by its file first (a UIDRef can outlive its document - KFCBookScope::ReachChapterDoc).
	if (!KFCBookScope::ReachChapterDoc(file, docRef))
	{
		outStatus.Append("the document of this row could not be opened.");
		return false;
	}
	KFCResultModel::RebindChapterDoc(chapterIdx, docRef);
	const bool ours = KFCBookScope::IsHeldDoc(docRef);
	// The rows - the kept ones too (the row eye) - where their foci stand now: a kept row the walk does not find again
	// stays at that place (MergeKeptRows).
	FollowChapterFoci(chapterIdx);
	// The room the list leaves: its limit less every row of the other documents, and the document's rows with their eye
	// on (they stay, found or not).
	const int32 room = KFCResultModel::kKFCCollectHitLimit - (KFCResultModel::GetTotalHitCount() - rowsNow)
		- KFCResultModel::CountKeptRows(chapterIdx, kInvalidUID);
	const size_t limit = static_cast<size_t>(room > 0 ? room : 0);
	std::vector<KFCResultModel::Hit> hits;
	std::map<UID, uint32> storyVersions;
	bool capped = false, cancelled = false, walked = false;
	{
		const SearchingFlagGuard searchingGuard;
		KFCForwardSearchScope forward;		// forward, as the search was
		if (KFCResultModel::GetSearchMode() == IFindChangeOptions::kObjectSearch)
			walked = KFCObjectSearch::WalkOneDocument(docRef, name, limit, hits, capped, cancelled);
		else
		{
			// The search's walk of one of its documents (CollectTargets' chapter walk): from the top, the whole of it.
			WalkerScopeOptions scopeOptions;
			KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
			PMString title("Searching...");
			title.SetTranslatable(kFalse);
			KFCProgressBar bar(title, 0, kKFCChapterProgressSpan, kTrue, kTrue);
			bar.DisableChildProgressBars(kTrue);
			KFCSetCountedTask(bar, "Document", 0, 1, name);
			int32 reported = 0;
			KFCAdvanceProgress(&bar, reported, 0, true /*force*/);
			ChapterWalkResult result = kChapterWalked;
			CollectHitsInDoc(docRef, limit, scopeOptions, kHitEverything, hits, capped, result,
				WalkBar(bar, 0, kKFCChapterProgressSpan, docRef, reported));
			cancelled = (result == kChapterCancelled) || bar.WasCancelled(kFalse) != kFalse;
			walked = (result == kChapterWalked) && !cancelled;
			if (walked)
				ReadStoryVersions(docRef, hits, storyVersions);		// while the document is open
		}
	}
	// A chapter of ours goes back now - outside any sequence, with the walk over (HandBackHeldDocNow's rule) - and before
	// the rows go in, so they do not get foci on a document about to close.
	if (ours)
		(void)KFCBookScope::HandBackHeldDocNow(docRef);
	if (cancelled)
	{
		outStatus.Append("cancelled - the document's rows are left as they were.");
		return false;
	}
	if (!walked)
	{
		outStatus.Append("InDesign's search stopped with an error - the document's rows are left as they were.");
		return false;
	}
	int32 stayed = 0;
	const int32 rows = KFCResultModel::ReplaceChapterRows(chapterIdx, hits, storyVersions, &stayed);
	SayFoundAgain(outStatus, "document", rows - stayed, capped, stayed);
	return true;
}

// SEARCH THESE RESULTS AGAIN (a tier's heading's right-click menu - the author, 2026-10-14 night, to the idea of searching a
// tier again from its heading: "let's try it"). The live list's own search run once more - RunSearch, the search's
// one body - over what the list searched, with its own query (the door states it when Find/Change holds another one -
// KFCQueryScope): its document, its book, or the documents open now; its rows replaced in place. See the header.
bool KFCSearchEngine::SearchListAgain(PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	PMString lead("Search These Results Again: ");
	lead.SetTranslatable(kFalse);
	// ITS OWN QUERY, KEPT AND READABLE: what the door states into Find/Change (KFCQueryScope states only a valid copy, and
	// lets the operation through on Find/Change as it is otherwise) - so none, or one that could not be read, is refused
	// here rather than searched with whatever the dialog holds.
	const std::shared_ptr<const KFCQueryCopy> copy(KFCResultModel::GetQueryCopy());
	if (copy == nullptr || !copy->valid)
	{
		outStatus.Append(lead);
		outStatus.Append("these results keep no query of their own - search with Find.");
		return false;
	}
	AgainTarget again;
	again.set = KFCResultModel::GetResultSetId();
	const char* part = nil;
	switch (KFCResultModel::GetSearchScope())
	{
		case KFCResultModel::kScopeStory:			part = "Story"; break;
		case KFCResultModel::kScopeToEndOfStory:	part = "To End of Story"; break;
		case KFCResultModel::kScopeSelection:		part = "Selection"; break;
		case KFCResultModel::kScopeBook:			again.fromBook = true; break;
		case KFCResultModel::kScopeAllDocuments:	again.allDocuments = true; break;
		default:									break;
	}
	// A SEARCH OVER PART OF A DOCUMENT: refused, as Search This Document Again refuses it - a story's part is Search This
	// Story Again's.
	if (part != nil)
	{
		outStatus.Append(lead);
		outStatus.Append("the search was over part of a document (Search: ");
		outStatus.Append(part);
		outStatus.Append(") - use Search This Story Again on a story row.");
		return false;
	}
	if (again.fromBook)
	{
		// ITS BOOK, OPEN - the book these rows were searched in (a switch to this tier refused one that was not).
		if (!KFCBookScope::GetSearchedBookPath(again.bookPath) || !KFCBookScope::IsBookStillOpen(again.bookPath))
		{
			outStatus.Append(lead);
			outStatus.Append("the book these results were searched in is not open - open it, then search again.");
			return false;
		}
	}
	else if (!again.allDocuments && KFCResultModel::GetChapterCount() > 0)
	{
		// ITS DOCUMENT (Search: Document - the searched one, the list's first chapter), FOUND OPEN, NEVER REOPENED (LIFE-01:
		// KFC does not open again what the person closed): by its binding while its rows stand; its rows gone with a close,
		// by the file it was closed from (Chapter::closedFile). No chapter at all - a search that found nothing - searches
		// the front document, as Find does.
		UIDRef docRef;
		IDFile file, closed;
		PMString name;
		int32 rows = 0;
		(void)KFCResultModel::GetChapterLocation(0, docRef, file);
		(void)KFCResultModel::GetChapterClosedFile(0, closed);
		(void)KFCResultModel::GetChapterDisplay(0, name, rows);
		UIDRef open(docRef);
		bool found = (docRef.GetDataBase() != nil || KFCBookScope::ChapterHasFile(file))
			&& KFCBookScope::FindOpenChapterDoc(file, open);
		if (!found && KFCBookScope::ChapterHasFile(closed))
		{
			open = UIDRef::gNull;
			found = KFCBookScope::FindOpenChapterDoc(closed, open);
		}
		if (found)
		{
			InterfacePtr<IDocument> doc(open, UseDefaultIID());
			if (doc != nil)
			{
				again.doc = KFCBookScope::DocAsChapter(doc);
				again.hasDoc = true;
			}
		}
		if (!again.hasDoc)
		{
			outStatus.Append(lead);
			if (name.IsEmpty())
				outStatus.Append("its document");
			else
				outStatus.Append(name);
			outStatus.Append(KFCBookScope::ChapterHasFile(closed) ? " is not open - open it, then search again."
				: " was closed, and its file is not known - search it with Find.");
			return false;
		}
	}
	bool committed = false;
	(void)RunSearch(&again, outStatus, committed);
	return committed;
}

void KFCSearchEngine::RestoreSearchScope(int32 mode, int32 scope)
{
	// kScopeCmdBoss with the tab beside it - how SnpFindAndReplace sets Search: (ProcessFindChangeCommandInt32) -
	// scheduled (ScheduleFindChangeInt): the caller is inside the notification of the dialog's own command.
	if (mode >= 0 && scope >= 0)
		(void)ScheduleFindChangeInt(kScopeCmdBoss, scope, mode);
}

void KFCSearchEngine::RereadStoryLead(int32 chapterIdx, const UIDRef& storyRef)
{
	(void)KFCResultModel::SetStoryLead(chapterIdx, storyRef.GetUID(), StoryLeadText(storyRef));
}

// End, KFCSearchEngine.cpp.
