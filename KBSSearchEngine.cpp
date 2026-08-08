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
//  paragraph (IComposeScanner::FindSurroundingParagraph + CopyText) and splits it into the three
//  segments the colour cell paints. The offsets are TextIndex throughout, and a TextIndex counts
//  CODE POINTS - a surrogate pair is ONE, the same unit WideString::CharCount counts in (measured
//  on the running application, 2026-08-05). So a match boundary cannot land inside a pair and no
//  character is ever cut in half.
//
//  (That paragraph read "The split is UTF-16-unit exact (GrabUTF16Buffer + SetXString)" until
//  2026-08-07, and both halves were wrong: the split goes through WideString, and the unit is the
//  code point. The wrong unit is not harmless bookkeeping - it sent an audit hunting a surrogate
//  bug that did not exist, and the "fix" drafted for it would have hashed only the high half of
//  every surrogate pair.)
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
// (Seven more text-attribute headers stood here until 2026-08-07 - ITextAttrString / WideString /
// Int32 / Int16 / RealNumber / Boolean / ClassID. BuildWalkSignature used to ask every find
// attribute for all of them to fingerprint its value by hand. AttributeBossList::IsEqual does that
// comparison properly, so the probe and its includes are gone; see RememberFindFormat.)

// General includes:
#include "IAttrReport.h"			// AppendDescription - a text attribute states itself, in the user's language
#include "IObjectModel.h"			// GetIDName - the receiver for an attribute that states nothing
#include "IStyleInfo.h"				// a paragraph / character style's own name
#include "IStyleGroupHierarchy.h"	// ...and its FULL path, group names included
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
#include <utility>					// std::move - a finished chapter is handed to the model, not copied
// (<string> stood here for the leading-separator test in DescribeFormatSetting, which used to run
// the whole description through GetUTF8String to look at three characters. It takes the three
// characters now - 2026-08-08 - and nothing else in this file wants a std::string.)
#include <algorithm>				// std::stable_sort (the matches' page order)
#include <map>						// the per-frame cache one document's walk keeps (FrameFacts)
#include <stdio.h>					// snprintf - the U+ formatting in DescribeGlyphQuery

// Project includes:
#include "KBSSearchEngine.h"
#include "KBSBookScope.h"
#include "KBSResultModel.h"
#include "KBSRunGuard.h"		// is anything ELSE of ours running? (the modal bar pumps events)
#include "KBSOversetLocator.h"	// the "+" page for an overset hit (locator + sort key)
#include "KBSEditStamp.h"		// stamp each chapter while its document is open

namespace
{

// The whole-run safety ceiling lived here until 2026-08-03. It moved to KBSResultModel.h
// (kKBSCollectHitLimit, beside the display cap) when the two scans were given the same ceiling -
// three commands reading one number, in the header they all already include. Read the contract
// there; this file uses it in SearchBook.

// The smallest advance worth reporting to the progress bar. Moving the bar is what makes Cancel
// work at all, but it is not free: doing it once per hit would repaint and run the message loop
// thousands of times over a large chapter. Small enough that Cancel still answers promptly.
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
	kChapterWalkFailed
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

// The shape every chapter note in KBS shares: a count, then up to three names, then "...".
// KBSBookScope::AppendUnopenableNote sets it and says so outright ("deliberately built to the same
// shape"); this is the search side's copy, shared by both notes below so they cannot drift from
// each other the way this file's single-name version had already drifted from its two siblings.
void AppendChapterNames(PMString& outSummary, const std::vector<PMString>& names)
{
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			outSummary.Append(", ");
		if (i >= 3)								// a status line stays short, even at three lines
		{
			outSummary.Append("...");
			break;
		}
		// RAW, with its ampersands as the user typed them: the one place that draws a status line
		// doubles the ampersands of the WHOLE line on its way to the widget. See the long note in
		// KBSBookScope::AppendUnopenableNote for what doubling twice looked like.
		PMString name(names[i]);
		name.SetTranslatable(kFalse);
		outSummary.Append(name);
	}
}

/** Name the chapters that were never searched at all, and why. Each entry is already "name: reason"
    - the name is the user's and the reason names internals, so neither is a translation key.
    Appends nothing when every chapter was walked, so the ordinary summary is unchanged. */
void AppendUnsearchableNote(PMString& outSummary, const std::vector<PMString>& entries)
{
	if (entries.empty())
		return;

	outSummary.Append("  ");
	outSummary.AppendNumber(static_cast<int32>(entries.size()));
	outSummary.Append(" chapter(s) could not be searched (");
	AppendChapterNames(outSummary, entries);
	outSummary.Append(").");
}

/** ...and the chapters whose walk BROKE OFF partway. A different sentence from the one above on
    purpose: these chapters were searched, their hits are in the list, and what is wrong is that the
    search stopped early - so the part of them that is missing from the list is missing because
    nobody looked, not because there was nothing there. Saying "could not be searched" would be
    false in one direction and saying nothing at all would be false in the other. */
void AppendSearchErrorNote(PMString& outSummary, const std::vector<PMString>& names)
{
	if (names.empty())
		return;

	outSummary.Append("  ");
	outSummary.AppendNumber(static_cast<int32>(names.size()));
	outSummary.Append(" chapter(s) stopped with a search error (");
	AppendChapterNames(outSummary, names);
	outSummary.Append(") - the rest of each was never searched.");
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

// The FIND FORMAT the results on the panel were searched with - a shallow copy of the dialog's find
// attribute list, taken when the search ran and compared before Change Checked re-walks. See
// KBSSearchEngine::RememberFindFormat for why the list is COPIED rather than described.
//
// WHY IT LIVES HERE AND NOT ON KBSResultModel, beside the walk signature it belongs with: that
// header states its own rule - the search mode is "held as a plain int so this header needs no text
// includes" - and an AttributeBossList member would drag the text headers into every file that
// includes the model. The lifetime is kept in step by hand instead: every caller of
// KBSResultModel::Clear() calls KBSSearchEngine::ForgetSearchedFindFormat() beside it, which is
// the rule KBSBookScope::ReleaseSearchedBook already follows and for the same reason.
//
// This comment said the model was cleared at "the two points on this path" until 2026-08-08. There
// are NINE, in six files: the two on this path (the commit point and the cancelled exit), the same
// two in each of the two scans, the replace's "the query has changed" exit, and one each in the
// close-document responder and the book watch - and the seven outside this file left the memory
// standing over a result set it had nothing to do with.
//
// (That sentence was written saying EIGHT. The nine were counted mechanically on the fourth audit
// of this block; every one of them does have its pairing, so what was wrong was the count in the
// comment and not the code. Counting by hand is how the "two" got there in the first place.)
boost::shared_ptr<AttributeBossList> gSearchedFindAttrs;

// The attribute database that list's UIDs are in. Kept beside it because a UID means nothing
// without its database, so a list from a DIFFERENT one must not be compared against it - see
// FindFormatHasChanged.
IDataBase* gSearchedFindAttrDB = nil;

// Let the remembered format go. The two fields are ONE fact, so they are dropped together and in
// one place: forgetting the list but keeping the database would leave FindFormatHasChanged's
// "different database" guard comparing against a database no list belongs to.
void ForgetFindFormat()
{
	gSearchedFindAttrs.reset();
	gSearchedFindAttrDB = nil;
}

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

	// The attribute database is the first argument of the call below, so it is tested before it is
	// handed over - the order the four other readers of the format pane in this file were given on
	// 2026-08-08. This one was not: that pass counted the places it had opened rather than the places
	// that ask the question, and BuildWalkSignature was missed the same way (found on the fourth
	// audit of this block). False with no database, because this door STOPS a search: "cannot tell"
	// must not start one.
	IDataBase* const queryDB = opts->GetUIDAttrDB();
	if (queryDB == nil)
		return false;

	// ***** ASK THE OPTIONS, NEVER THE FIND STRING. *****
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
	//     paragraph style" - with the find box deliberately empty. The dialog allows it, and KBS
	//     runs the same kFindTextCmdBoss the dialog does, so the walk handles it; only this door
	//     was shut. It used to ask !GetFindString(mode).empty() here, which turned that away.
	//
	// (Until 2026-08-03 the Glyph tab asked this and Text / GREP asked the find string - one
	// function answering the same question two ways. The format-only search was what that cost.)
	return opts->IsThereSomethingToFind(queryDB, mode) != kFalse;
}

// Is anything set in one side of the dialog's format pane? See the header for what this is for; what
// follows is why it has to look in two places.
//
// ***** A FORMAT IS NOT ALWAYS IN THE ATTRIBUTE LIST. ***** Most conditions are - a point size, a
// colour, a language - and AttributeBossList::CountBosses finds those. Paragraph and character
// STYLES are not: IFindChangeOptions carries them in fields of their own, reached through
// GetFindParaStyle / GetFindCharStyle (and the change-side pair, which take allowCreation).
//
// Measured 2026-08-04 on the running application, which is how the split came to light: searching by
// a paragraph style alone RAN (IsThereSomethingToFind says yes to it) and returned the right rows,
// while the list this used to count was still empty - so the replace prompt captioned that very
// search "Find: ^1" and promised to DELETE the matches it was only going to restyle.
//
// WHAT IS NOT ASKED HERE: which style, or which attributes - this answers yes or no and nothing
// else. That question has an answer of its own since later the same day: DescribeFormatSetting,
// where the attributes describe themselves. A caller wanting both asks this one first, because an
// empty description does NOT mean nothing is set (see that function's note).
static bool HasFormatSet(IFindChangeOptions* opts, IFindChangeOptions::SearchMode mode, bool findSide)
{
	if (opts == nil)
		return false;

	// The database is the first argument of every call below, so it is tested before being handed
	// over - the same order DescribeGlyphQuery and RememberFindFormat use since 2026-08-08. With no
	// attribute database there is nothing to answer from, and "cannot tell" is false here: this
	// question captions a search and gates a sentence in the replace prompt, and both of those are
	// better left unsaid than said wrongly.
	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil)
		return false;

	// allowCreation = kFalse on the change side: the default is kTrue and would CREATE what it hands
	// back. This is a read, and a prompt must not write to the user's settings.
	const AttributeBossList* const attrs = findSide
		? opts->GetFindAttributeBossList(db, mode)
		: opts->GetChangeAttributeBossList(db, mode, kFalse);
	if (attrs != nil && attrs->CountBosses() > 0)
		return true;

	const UID paraStyle = findSide ? opts->GetFindParaStyle(db, mode)
								   : opts->GetChangeParaStyle(db, mode, kFalse);
	const UID charStyle = findSide ? opts->GetFindCharStyle(db, mode)
								   : opts->GetChangeCharStyle(db, mode, kFalse);
	return paraStyle != kInvalidUID || charStyle != kInvalidUID;
}

// ***** AN ATTRIBUTE STATES ITSELF. *****
//
// IAttrReport::AppendDescription is the call that builds the "Settings" text in the Style Options
// dialog and the tooltip behind the "+" beside an overridden style name (IAttrReport.h:122-133).
// What comes back is therefore the wording InDesign ALREADY shows this user, in this user's
// language - nothing here has to know what the 222 attribute bosses in TextAttrID.h are, and the
// note that used to stand in this file ("the SDK offers no way to turn a ClassID into a readable
// name") was asking the wrong object: the attributes name themselves.
//
// THREE TIERS, copied from SnpInspectTextStyles::reportAttribute, because an attribute is allowed
// to say nothing at all:
//   1. AppendDescription
//   2. it appended nothing -> the boss's internal name (IObjectModel::GetIDName)
//   3. no name registered  -> the hex ClassID, so the line is never silently empty
static void AppendOneAttribute(const AttributeBossList* attrs, int32 n, IDataBase* db,
							   PMString& out, bool needSeparator)
{
	const int32 lengthBefore = out.CharCount();

	InterfacePtr<const IAttrReport> report(static_cast<const IAttrReport*>(
		attrs->QueryBossN(n, IAttrReport::kDefaultIID)));
	if (report != nil)
		report->AppendDescription(&out, db, attrs);

	if (out.CharCount() != lengthBefore)
		return;			// it spoke for itself, separator included - see DescribeFormatSetting

	// Neither receiver below brings a separator, so this is the one place that adds one. It is asked
	// for rather than worked out from `out`, which is a piece being built on its own, not the list.
	if (needSeparator)
		out.Append(" + ");

	const ClassID cls = attrs->GetClassN(n);
	InterfacePtr<IObjectModel> objectModel(GetExecutionContextSession(), UseDefaultIID());
	const char* const name = (objectModel != nil) ? objectModel->GetIDName(kClassIDSpace, cls.Get()) : nil;
	if (name != nil)
		out.Append(name);
	else
	{
		char buf[24];
		snprintf(buf, sizeof(buf), "0x%x", static_cast<unsigned int>(cls.Get()));
		out.Append(buf);
	}
}

// How much format detail the PROMPT is willing to carry. The prompt is a question, not a report:
// past this the reader is skimming rather than checking, and the line's job is to let them recognise
// the settings they made. Whole pieces only, and what was left out is SAID (" + ...") rather than
// silently cut - a truncated list that does not admit it reads as "this is everything".
//
// The saved report carries no limit at all (user's decision, 2026-08-04) - it is read later and
// matched against a document, so there is nothing to gain by cutting it. See DescribeFormatSetting's
// `limited` argument, which is the only thing this number is reached through.
static const int32 kKBSFormatDetailLimit = 100;

// Append one piece unless it would take the line past that limit. The first piece always goes in:
// a single long description is still better than nothing at all.
//
// @param limited false to take every piece however long the line becomes - the caller is writing a
//        record rather than asking a question, and then this can never answer false.
// @return false when the piece was left out, which is the caller's cue to stop and say "...".
static bool AppendWithinLimit(PMString& out, const PMString& piece, bool limited)
{
	if (piece.IsEmpty())
		return true;			// nothing to add, and nothing was dropped either
	if (limited && !out.IsEmpty() && (out.CharCount() + piece.CharCount()) > kKBSFormatDetailLimit)
		return false;
	out.Append(piece);
	return true;
}

// A style set in the format pane. Not in the attribute list above - styles are not text attributes
// (see HasFormatSet) - so they are named separately, and by their FULL path: a style inside a group
// loses the group from IStyleInfo::GetName, and two groups may hold the same style name
// (SnpManipulateTextStyle reads GetFullPath everywhere for that reason).
static void AppendStyleName(IDataBase* db, const UID& style, const char* label, PMString& out,
							bool needSeparator)
{
	if (db == nil || style == kInvalidUID)
		return;
	InterfacePtr<IStyleInfo> info(db, style, UseDefaultIID());
	if (info == nil)
		return;

	// Same separator the attributes use, so one list reads as one list.
	if (needSeparator)
		out.Append(" + ");
	out.Append(label);
	out.Append(": ");

	InterfacePtr<IStyleGroupHierarchy> hierarchy(db, style, UseDefaultIID());
	out.Append((hierarchy != nil) ? hierarchy->GetFullPath() : info->GetName());
}

// The two public wrappers are defined further down, OUTSIDE this file's anonymous namespace - a
// KBSSearchEngine:: definition is not allowed in here (C2888).

// One side of the Glyph tab's query as a readable line: "Glyph 1234 (Kozuka Mincho Pr6N  Regular)
// U+845B".
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
//
// @param findSide true for the glyph being looked for, false for the one that replaces it. An empty
//        Change To box - a legitimate request that deletes every match - has no glyph and comes back
//        EMPTY, which is the caller's cue to write no line at all rather than "Glyph -1".
PMString DescribeGlyphQuery(IFindChangeOptions* opts, bool findSide)
{
	PMString description;
	description.SetTranslatable(kFalse);
	if (opts == nil)
		return description;

	const Text::GlyphID glyphID = findSide ? opts->GetFindGlyphID() : opts->GetReplaceGlyphID();
	if (glyphID == kInvalidGlyphID)
		return description;

	description.Append("Glyph ");
	description.AppendNumber(static_cast<int32>(glyphID));

	// ***** ASKED BEFORE IT IS HANDED OVER. ***** Every attribute call below takes this database as
	// its first argument, so a nil one has to be turned away here rather than passed in and judged
	// afterwards - which is what this did until 2026-08-08, and what three sibling readers of the
	// format pane in this file did with it.
	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil)
		return description;

	// Each side keeps its own font: the glyph being replaced is in the font it was found in, and the
	// one written in its place is in whatever font the Change To box was set from. kFalse on the
	// change side because KBS never writes to the user's Find/Change settings, and the default of
	// this call is to CREATE the list when it does not exist (IFindChangeOptions.h:506).
	const AttributeBossList* const attrs = findSide
		? opts->GetFindAttributeBossList(db, IFindChangeOptions::kGlyphSearch)
		: opts->GetChangeAttributeBossList(db, IFindChangeOptions::kGlyphSearch, kFalse);
	if (attrs == nil)
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
	// lie.
	//
	// The face is taken through an InterfacePtr, which is how Adobe takes it every time
	// (SnpModifyLayoutGrid.cpp:1050, SnpInspectLayoutGrid.cpp:465) - QueryFace hands back a reference
	// and this function is done with it before it returns, so nothing here should be holding one by
	// hand. (Its sibling in KBSReplaceConfirmDialog::ResolveSide is a raw pointer on purpose and must
	// stay one: that face is kept for the dialog's lifetime and let go in ReleaseSides.)
	//
	// IFontFamily.h:201 asks for BOTH checks - a face can come back non-nil and still not be
	// installed - and an uninstalled one has no glyph to look a character up in.
	InterfacePtr<IPMFont> font(family->QueryFace(styleName));
	if (font != nil && font->GetFontStatus() == IPMFont::kFontInstalled)
	{
		const UTF32TextChar ch = Utils<IGlyphUtils>()->GetUnicodeForGlyphID(font, glyphID);
		if (ch.GetValue() != 0)
		{
			char buf[16];
			snprintf(buf, sizeof(buf), " U+%04X", static_cast<unsigned int>(ch.GetValue()));
			description.Append(buf);
		}
	}

	return description;
}

// ...and WHAT that format is, in parentheses after it: "Find Format (size: 14 pt + Paragraph style:
// Body)". The same detail the replace prompt shows, from the same call - but written IN FULL, because
// this one goes into a file (user's decision, 2026-08-04: "the export, with no character limit").
//
// Empty means the settings said nothing about themselves, NOT that no format is set - that question
// was answered by HasFormatSet before this is reached (KBSSearchEngine.h). Then the bare pane name
// stands on its own, which is what this line said in full until 2026-08-04.
static void AppendFormatDetail(PMString& into, bool findSide)
{
	const PMString detail(KBSSearchEngine::DescribeFormatSetting(findSide, false /*limited*/));
	if (detail.IsEmpty())
		return;

	into.Append(" (");
	into.Append(detail);
	into.Append(")");
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
		description = DescribeGlyphQuery(opts, true /*findSide*/);
	else if (mode == IFindChangeOptions::kTransliterateSearch)
	{
		// The query is a character type; the tab name below says the rest.
		description.Append(KBSSearchEngine::CharacterTypeName(
			static_cast<int32>(opts->GetFindCharacterType())));
	}
	else
	{
		description.Append(opts->GetFindString(mode));

		// ***** FIND FORMAT IS PART OF WHAT WAS ASKED FOR. ***** Without this a search by format
		// alone - which the find box is empty for - captions itself with nothing at all, and the
		// saved report's heading reads "Query:  (Text)". "cat  + Find Format" when both are set.
		//
		// NOT translated, unlike the replace prompt: this line goes into the saved report, and the
		// report is English throughout, like the panel it reports on.
		//
		// Not on the Glyph tab either - its attribute list holds the QUERY's own font, so the note
		// would be on every glyph search, saying nothing. DescribeGlyphQuery states that font.
		//
		// Asked through HasFormatSet rather than by counting the attribute list here: a paragraph or
		// character style is a format the list does not carry (2026-08-04), and this caption is the
		// one the format-only search exists for.
		if (HasFormatSet(opts, mode, true /*findSide*/))
		{
			if (!description.IsEmpty())
				description.Append("  + ");
			description.Append("Find Format");
			AppendFormatDetail(description, true /*findSide*/);
		}
	}

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
//
// ***** THE ANSWER IS RETURNED, NOT SWALLOWED. ***** false means the value was NOT stated, and the
// engine will therefore walk - or write - with whatever was committed last. On the replace side
// that is a glyph the user never chose on this run and cannot see anywhere on screen, which is the
// exact failure CommitReplaceSide exists to prevent; on the find side it is a query nobody typed.
// This returned void until 2026-08-08, so every caller believed it had succeeded.
bool CommitGlyphID(Text::GlyphID glyphID, bool16 findSide)
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
	// Nothing to state, and nothing wrong: the find side of an empty box is left as the dialog has
	// it (the search is stopped long before this by HasFindQuery). That is a success, not a failure.
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

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		// The error state has to be cleared whatever the caller does with the answer, or it fails
		// every find command after it - and the caller is told, so it can stop rather than run with
		// a value that is not the one on screen.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return false;
	}
	return true;
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

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		// Cleared whatever the caller does with the answer - left standing it fails every find
		// command after it - and reported, so the caller can stop instead of walking by a value the
		// dialog does not hold.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return false;
	}
	return true;
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
// A frame that resolves to no layer reads as unlocked - see IsFrameEditable in the header on why
// "cannot tell" must not turn into a refusal.
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

//------------------------------------------------------------------------------------
// WHAT A HIT NEEDS READ OUT OF THE STORY'S TEXT - the three drawn segments, and the hash of the
// whole match. Every hit wants both, one straight after the other, so they are taken TOGETHER:
// one ITextModel / IComposeScanner pair for the pair of them, and the matched characters copied
// ONCE. They used to open the story separately and copy the same range twice over (2026-08-08).
//
// The public wrappers further down do the same work for a caller holding a single range - the
// replace pass rebuilding one row, the jump checking one - and go through the same two helpers,
// so what the search stored and what those two read can never be computed differently.
//------------------------------------------------------------------------------------

// The most characters a match segment carries, and the ONE place that limit is applied.
//
// A format-only search matches every unbroken run of text carrying the format, which can be dozens
// of paragraphs. Copying all of it per hit and holding it for the life of the result set would cost
// far more than the single line a row can ever draw, so it is capped. The cap is far past what any
// cell shows, so what is dropped was never going to be read - and the row still shows that the
// match continues, because the break marks are inside what IS kept.
//
// !! DISPLAY ONLY since 2026-08-04. The cap used to bind the same-occurrence test as well:
// SplitLineAroundMatch and a CopyMatchText beside it had to cut in the SAME place, or a match cut
// differently on the two sides read as "the text moved since the search ran" and every replace was
// refused with 'missing'. That test now compares the match WHOLE, through a hash taken with no cap
// at all (HashMatchText), so nothing but the drawn row passes through here - and a row that is
// clipped for drawing can no longer cost a replace.
const int32 kKBSMaxMatchChars = 500;

TextIndex KBSCapMatchEnd(TextIndex start, TextIndex end)
{
	if (end < start)
		return start;
	if (end - start > kKBSMaxMatchChars)
		return start + kKBSMaxMatchChars;
	return end;
}

// FNV-1a, 64-bit. Taken over the UTF-32 value of each character, byte by byte, so a character's
// high bits count as much as its low ones.
//
// 64-bit, not 32: a collision here means "the text changed and we replaced it anyway", which is the
// one direction this plug-in must not fail in.
const uint64 kKBSHashOffsetBasis = 14695981039346656037ULL;
const uint64 kKBSHashPrime = 1099511628211ULL;

void HashAccumulate(uint64& ioHash, const WideString& text, int32 count)
{
	for (int32 i = 0; i < count; ++i)
	{
		const uint32 ch = static_cast<uint32>(text.GetChar(i).GetValue());
		for (int32 b = 0; b < 4; ++b)
		{
			ioHash ^= static_cast<uint64>((ch >> (b * 8)) & 0xFF);
			ioHash *= kKBSHashPrime;
		}
	}
}

// ...over text somebody has ALREADY read. The caller has to have the whole match in hand - a
// partial read hashes to a number that means nothing (see HashRangeWithScanner's short-read test).
uint64 HashOfWideString(const WideString& text)
{
	uint64 hash = kKBSHashOffsetBasis;
	HashAccumulate(hash, text, text.CharCount());
	// 0 is the "could not read" answer, so a real hash must never be 0.
	return (hash != 0) ? hash : 1;
}

// The line around [start, end) in its three drawn segments - see the contract on
// KBSSearchEngine::SplitLineAroundMatch, which this implements.
//
// outMatchWide hands the matched characters back as they were read, so a caller that also wants
// them hashed does not have to copy them out of the story a second time. It is the CAPPED match:
// equal to (end - start) characters only when the match fitted inside kKBSMaxMatchChars, which is
// exactly the test a caller has to make before hashing it.
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

	// The leading context comes from the paragraph the match STARTS in; excludeEOS (default) trims
	// the paragraph terminator.
	int32 paraLen = 0;
	const TextIndex paraStart = scanner->FindSurroundingParagraph(start, &paraLen);
	if (paraStart < 0 || paraLen <= 0)
		return;

	// *The match itself is taken WHOLE, not cut at the end of that paragraph (2026-08-04).
	// A single match can run over any number of paragraphs - a format-only search matches every
	// unbroken run of text carrying the format, and a GREP can be written to cross a break on
	// purpose. Cutting it here made the row show ONE paragraph of what a replace would rewrite in
	// full, so a user who ticked that row lost text that was never on screen (measured: two
	// paragraphs and the break between them replaced by one word, joining what was left to the
	// paragraph below). The breaks inside the match are drawn as marks by the cell - see
	// KBSColorTextView, which turns CR into a pilcrow and a forced line break into a return arrow.
	const TextIndex matchEnd = KBSCapMatchEnd(start, end);

	if (start > paraStart)
	{
		WideString w;
		scanner->CopyText(paraStart, static_cast<int32>(start - paraStart), &w);
		outPre = PMString(w);
		outPre.SetTranslatable(kFalse);
	}
	if (matchEnd > start)
	{
		// Read into the caller's buffer, so the hash can be taken from these very characters.
		scanner->CopyText(start, static_cast<int32>(matchEnd - start), &outMatchWide);
		outMatch = PMString(outMatchWide);
		outMatch.SetTranslatable(kFalse);
	}

	// The trailing context comes from the paragraph the match ENDS in, which is a DIFFERENT
	// paragraph from the one it started in once the match spans a break. Probed at matchEnd - 1 so
	// a match ending exactly on a paragraph terminator answers with the paragraph it ended, not the
	// one after it.
	//
	// !ONLY when the match was not capped (2026-08-04). Past the cap, what follows matchEnd is more
	// of the MATCH - and this segment is drawn in the normal colour, so writing it here would show
	// the rest of the match as though it were text lying outside it. Nothing is lost by leaving it
	// out: a capped match is 500 characters, already far wider than the cell, so the row ellipsizes
	// inside the match itself and the reader can see that it continues.
	if (matchEnd != end)
		return;

	int32 endParaLen = 0;
	const TextIndex probe = (matchEnd > start) ? matchEnd - 1 : start;
	const TextIndex endParaStart = scanner->FindSurroundingParagraph(probe, &endParaLen);
	if (endParaStart >= 0 && endParaLen > 0)
	{
		const TextIndex endParaEnd = endParaStart + endParaLen;
		if (endParaEnd > matchEnd)
		{
			WideString p;
			scanner->CopyText(matchEnd, static_cast<int32>(endParaEnd - matchEnd), &p);
			outPost = PMString(p);
			outPost.SetTranslatable(kFalse);
		}
	}
}

// The whole of [start, end) as one number - see the contract on KBSSearchEngine::HashMatchText,
// which this implements. A nil scanner answers 0, like any other text that could not be read.
uint64 HashRangeWithScanner(IComposeScanner* scanner, TextIndex start, TextIndex end)
{
	if (scanner == nil || end <= start)
		return 0;

	// ***** NO CAP HERE, and that is the whole point. ***** The drawn segment stops at
	// kKBSMaxMatchChars because what it produces is held for the life of the result set; this holds
	// nothing but the 64 bits below, so the match is read in full however long it is.
	//
	// Read in blocks rather than in one call: a single CopyText of an enormous match would build
	// one WideString that size, and nothing here needs the whole match in memory at once.
	const int32 kBlock = 4096;
	uint64 hash = kKBSHashOffsetBasis;
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
// which is every match up to the 500-character display cap - so the ordinary hit copies its text
// out of the story exactly once. A longer match falls back on reading itself in blocks: the drawn
// segment stopped at the cap, and the hash is the one that must cover every character.
//
// A story with no text model leaves the hit as it came: three empty segments and a hash of 0,
// which is what every caller already reads as "this position could not be read".
void ReadHitText(const UIDRef& storyRef, TextIndex start, TextIndex end, KBSResultModel::Hit& outHit)
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
	const UID matchFrameUID = FrameUIDForPosition(storyRef, start);
	const FrameFacts* facts = &LookUpFrame(docRef, storyRef, matchFrameUID, frameFacts);

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
		const KBSOversetLoc loc = KBSFindOversetLocator(storyRef, start);
		if (loc.found)
		{
			const FrameFacts& oversetFacts = LookUpFrame(docRef, storyRef, loc.frameUID, frameFacts);
			if (oversetFacts.hasPage)
			{
				// The FACTS are the whole of what the rest of this function reads - the page, the
				// hidden flag, the lock - so switching this pointer is the entire handover. (The
				// overset frame's UID was also assigned to matchFrameUID here until 2026-08-08,
				// where nothing ever read it again: a value kept "in step" for no reader is one
				// more thing to keep right.)
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

	// The line's three drawn segments, and the whole match as one number for the same-occurrence
	// test the JUMP runs. (The replace ran that test too until 2026-08-05 - see KBSReplaceEngine.h.)
	// Both describe THIS match as the search found it, and both come out of one reading of the
	// story - see ReadHitText. The segments are capped for drawing; the hash never is, because it
	// is the one that gets compared.
	//
	// The same two helpers serve the replace pass, which rebuilds a replaced row exactly this way
	// from the range the replace command hands back.
	ReadHitText(storyRef, start, end, outHit);
}

// Walk one document with the user's current Find/Change query and collect every match as a Hit.
// Read-only: the whole walk sits inside a SaveRestoreModifiedState dirty guard, so a windowless
// chapter can be closed afterwards without wanting a save. NOTHING is set on opts - the walk uses
// the user's Find/Change settings verbatim, so the search mode (Text or GREP) is followed.
//
// scopeOptions: the five switches every chapter of this run is walked with, read once by the caller
// (see SearchBook). They are the same for every chapter by definition - the replace pass re-walks
// with exactly these, or the walk order the hits were numbered by no longer lines up - so reading
// them here would have been the Find/Change settings asked once per chapter for one answer.
//
// progressBar / progressBase: the run's bar and the point on it where this chapter starts.
// chapterSpan / storiesInDoc: how much of the bar this chapter owns, and how many stories to divide
// that slice between - asked by the caller once the chapter is open, since a chapter's size is not
// knowable before then. The walk moves the bar as it goes: a story at a time, each one subdivided
// by how far into its text the current match sits. nil is allowed for the bar.
void CollectHitsInDoc(const UIDRef& docRef, size_t maxHits, const WalkerScopeOptions& scopeOptions,
	std::vector<KBSResultModel::Hit>& outHits,
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
	//
	// ***** THE OPPOSITE OF WHAT THE SNIPPET DOES, AND ON PURPOSE. ***** SnpFindAndReplace.cpp:772
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
	// these options, or the walk order the hits were numbered by would no longer line up. Handed in
	// by the caller, which reads them once for the whole run.
	//
	// !! ADOBE CALLS THIS NOWHERE. Every other step of this walk is the shape SnpFindAndReplace
	// uses, but the snippet builds its scope from the SELECTION
	// (QueryWalkerScope_UsingSelections, :774) because it is driving Find Next from a dialog. A
	// panel that lists a whole document has to say so instead, and the document form is the one
	// IWalkerScopeFactoryUtils.h:102-109 documents for it - so what stands behind this line is the
	// header's contract, not a worked example. (Grepped 2026-08-08: the only callers in the SDK
	// tree are this file, KBSReplaceEngine and KESCL, all three ours.)
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
		// ***** THE ONE EXIT THAT IS PAST Initialize. ***** Every refusal above this line is before
		// the walker was given anything to walk, so there is nothing to stop; this one is after, and
		// leaving a walker walking is what the Halt at the bottom of this function exists to prevent -
		// the next caller that guards its Initialize with IsWalking CONTINUES the walk left standing,
		// which is exactly what InDesign's own Find/Change does (SnpFindAndReplace.cpp:772).
		// The shape is Adobe's (SpellPreviousObserver.cpp:200-201: ask IsWalking, then Halt).
		//
		// This said "and the replace engine's two walks are already symmetric this way" until
		// 2026-08-08. They were not: KBSReplaceEngine's matching exit returned without halting, and
		// the claim had been written without opening the file it was about. Both sides halt now.
		if (walker->IsWalking())
			walker->Halt();
		// Named for what actually happened. It used to answer kChapterNoWalker - "no text walker" -
		// which the summary would then print, and there IS a walker: what is missing is the interface
		// the critical section is taken on.
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
	// The price of holding it: UI work must not be pumped inside it, which is why the progress bar's
	// cancel is only asked between chapters (see SearchBook). Do not "correct" this to the one-command
	// shape without measuring both. (docs/ai-notes/kbs-book-and-search-api-audit-2026-07-31.md)
	//
	// ***** THE REST OF THIS INTERFACE IS DELIBERATELY LEFT ALONE. ***** It also carries
	// InitTextWalkerTerminator / TerminateTextWalkerTerminator, which spellpanel DOES call
	// (SpellPanelObserver.cpp:336, :349), and the snapshot pair beside them. Those bracket a walk
	// that OUTLIVES the call which started it - spellpanel's runs for as long as its panel is open,
	// across the user's clicks - and the terminator is what halts such a walk when the ground moves
	// under it. This walk begins and ends inside this function with a modal bar up throughout, so
	// there is no window in which anything could move. (Checked on the fourth audit of this block,
	// 2026-08-08, so the next reader does not have to work out whether they were an oversight.)
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);

	// What each of this document's frames answers about the hits inside it - see FrameFacts. Scoped
	// to this walk, so it can never outlive the state it describes.
	FrameFactsCache frameFacts;

	// (A per-story ITextModel::GetTextChangeCount was collected here until 2026-08-03 and stamped
	// onto every hit, so the replace could skip its same-occurrence test for a story nobody had
	// edited. That fast path is gone - it was skipping the POSITION test as well, which is what let a
	// query retyped between the search and the replace rewrite the wrong occurrences. See the note
	// over MatchStillStandsHere in KBSReplaceEngine.cpp.)

	// Walk the whole document. Each ProcessCommand advances the walker to the next match ("find
	// next"), so we keep going until no more hits; the maxHits ceiling is the stop of last resort.
	// Where the bar stands within this chapter: how many stories the walk has finished, and how long
	// the one it is in now is. The walk visits a story at a time, so a change of story means the one
	// before it is done - whatever order the walker chose to take them in.
	int32 storiesDone = 0;
	UID progressStory = kInvalidUID;
	int32 progressStoryLength = 0;

	while (true)
	{
		// ***** A COMMAND THAT COULD NOT BE BUILT IS A FAILED WALK, NOT A FINISHED ONE. *****
		// These two exits left outResult at kChapterWalked until 2026-08-08, so a chapter whose find
		// command could not be created reported itself as searched to the end - the same statement
		// about the user's DOCUMENT that the four walker answers below were flattened into until the
		// day before. Adobe's loop takes the other view: SnpFindAndReplace.cpp:752-765 leaves its
		// result at the kFailure it was initialised to and breaks out. So does the replace engine's
		// RunWalkerCmd (KBSReplaceEngine.cpp:94-99), which had the answer right on both of them while
		// this side did not - it is the same loop, written twice.
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

		// ***** WHAT THE COMMAND ANSWERED, not merely whether it landed on something. *****
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
		// KBSReplaceEngine.cpp, 2026-08-07); this side was still flattening all four answers into one
		// until 2026-08-07. Official shape: SnpFindAndReplace.cpp:790-796 folds a failed
		// ProcessCommand into kFailure as well, and its caller (:642-670) turns kFailure - and only
		// kFailure - into a failure.
		if (CmdUtils::ProcessCommand(findCmd) != kSuccess)
		{
			// End of THIS walk only: the error state a failed find raises would otherwise
			// outlive it and block every later command in the session.
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);
			outResult = kChapterWalkFailed;
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

			// Moving the bar from inside the walk is what keeps the Cancel button answering at all -
			// see KBSAdvanceProgress, which also explains why this is SetPosition and not the DoTask
			// this comment used to name (RangeProgressBar has no DoTask; that is TaskProgressBar's).
			KBSAdvanceProgress(progressBar, ioProgressReported, position);
		}

		// Whole-search safety ceiling reached: stop collecting. More matches may exist, but the
		// result set is capped so a match-everywhere query cannot grow it without bound.
		if (outHits.size() >= maxHits)
		{
			outCapped = true;
			break;
		}

		// ***** BUILT WHERE IT IS GOING TO LIVE. ***** A Hit carries six PMStrings, so filling a
		// local one and copying it in here cost six heap allocations per match that nothing asked
		// for (2026-08-08). emplace_back hands back the new element itself (C++17), and BuildHit
		// never touches this vector, so the reference cannot be invalidated under it.
		KBSResultModel::Hit& hit = outHits.emplace_back();
		BuildHit(docRef, story, start, end, frameFacts, hit);
		// The walk order within this chapter, stamped BEFORE FinalizeChapterHits sorts the vector
		// into page order. The replace pass re-walks the chapter and matches on this number.
		hit.walkOrder = static_cast<int32>(outHits.size()) - 1;
	}

	if (walker->IsWalking())
		walker->Halt();
}

// Put a chapter's hits in PAGE order and bake the "P<page>(<n>) " locator onto each hit line
// (the KESCL convention: page string, section-aware; a within-page ordinal in parens only when
// the page holds more than one match; "overset" for an overset match, which has no page). The locator
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
	// ***** THIS IS THE ONLY CALL THAT MOVES ANY OF KBS'S BARS, AND IT IS SetPosition. *****
	// Said that plainly because it was not: several comments around the engines claimed the bar was
	// driven by DoTask and that SetPosition could not be cancelled from. Neither is true, and one of
	// them is not even reachable - DoTask belongs to TaskProgressBar (ProgressBar.h:186), while every
	// KBS bar is a RangeProgressBar, which does not have it. Do not "restore" DoTask here; it would
	// mean changing the bar's type and with it the meaning of every position this file computes.
	//
	// It WAS measured against DoTask on 2026-07-31, because a run had become impossible to cancel and
	// this call was the prime suspect. It was not the culprit: the cancel works exactly the same
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

bool KBSSearchEngine::CommitSearchMode(Text::GlyphID overrideFindGlyph)
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
	//
	// A caller that brought its own glyph wins over the dialog's. That is how the missing-glyph scan
	// runs: it hands over kAnyNotDefGlyphID, which the engine reads as "any notdef, whatever font
	// this run of text is in". Every other caller passes kInvalidGlyphID and gets the old behaviour
	// exactly - the dialog's own glyph, stated back to the engine unchanged.
	const Text::GlyphID findGlyphID =
		(overrideFindGlyph != kInvalidGlyphID)
			? overrideFindGlyph
			: ((mode == IFindChangeOptions::kGlyphSearch) ? opts->GetFindGlyphID() : kInvalidGlyphID);

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

	if (CmdUtils::ProcessCommand(cmd) != kSuccess)
	{
		// ***** THE CALLER IS TOLD. ***** The error state is cleared either way - left standing it
		// would fail every find command after it - but the walk must NOT go ahead: it would run in
		// whatever mode was committed last, which is a tab the user is not looking at, and the
		// results would then be filed under the tab that IS on screen. This swallowed the failure
		// and carried on until 2026-08-08; the snippet this is modelled on stops instead
		// (SnpFindAndReplace.cpp:511-516 and :598-603, both on this very command).
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		return false;
	}

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
	// nothing at all on a query the dialog handled perfectly well (2026-07-30).
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

bool KBSSearchEngine::CommitReplaceSide()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	if (mode == IFindChangeOptions::kGlyphSearch)
	{
		// An empty Change To box is NOT refused. It means "delete every match" - the same thing an
		// empty change string means on the Text tab - and that is what the Find/Change dialog does
		// with it. KBS used to stop the run here, which made the panel strictly less capable than
		// the dialog it delegates to (user's report 2026-07-31). What the old code was right about
		// is that the empty box must never be left UNSTATED; CommitGlyphID states it on this side
		// for exactly that reason.
		//
		// ***** AND WHETHER IT WAS STATED IS THE ANSWER THIS FUNCTION GIVES. ***** The header
		// promises "true when it is safe to replace", and a change glyph that could not be
		// committed is the one case the whole mechanism was built to catch: the command would then
		// write whatever was committed last - a glyph the user never chose on this run and cannot
		// see anywhere on screen. This returned true unconditionally until 2026-08-08, so the
		// promise held only for the settings being unreadable, never for the stating failing.
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

const char* KBSSearchEngine::CharacterTypeName(int32 characterType)
{
	switch (characterType)
	{
		case IFindChangeOptions::kKanji:				return "Kanji";
		case IFindChangeOptions::kHalfWidthKatakana:	return "Half-Width Katakana";
		case IFindChangeOptions::kHalfWidthRoman:		return "Half-Width Roman";
		case IFindChangeOptions::kFullWidthHiragana:	return "Full-Width Hiragana";
		case IFindChangeOptions::kFullWidthKatakana:	return "Full-Width Katakana";
		case IFindChangeOptions::kFullWidthRoman:		return "Full-Width Roman";
		case IFindChangeOptions::kNone:					return "None";
		case IFindChangeOptions::kWesternArabicDigits:	return "Western Arabic Digits";
		case IFindChangeOptions::kArabicIndicDigits:	return "Arabic-Indic Digits";
		case IFindChangeOptions::kFarsiDigits:			return "Farsi Digits";
		default:										return "";
	}
}

// ***** THE FIND FORMAT COMPARES ITSELF. *****
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
// ***** WHAT THIS REPLACED, AND WHY IT HAD TO GO. ***** Until 2026-08-07 the signature carried a
// hand-rolled fingerprint of each attribute instead: its class, plus whatever answered out of nine
// value-carrying interfaces (ITextAttrUID / Font / String / WideString / Int32 / Int16 / RealNumber
// / Boolean / ClassID). An attribute answering none of the nine went in as its CLASS alone - so
// "this condition was added or removed" was always seen while "same condition, DIFFERENT VALUE" was
// not. The note above it said so, and said it was survivable because the per-hit same-occurrence
// test stood behind it. That test was removed on 2026-08-05 (user's decision, see
// KBSReplaceEngine.h), which turned the gap into a wrong replacement made in silence - and the
// reason given for living with it, "there is no generic value read to widen it with", was wrong:
// there was no generic value READ, but there has always been a generic COMPARE.
//
// The comment that sent that search down the wrong path is worth naming, because it was half right:
// "AttributeBossList keeps operator== and operator!= PRIVATE, so two lists cannot be compared". They
// are private (:245-252). Sealing the operators and publishing a named method is a normal C++ way of
// making callers say which comparison they mean - here IsEqual (deep) sits beside Intersects and
// IntersectionContainsDifferences - and "the operator is private" was read as "the question cannot
// be asked".
//
// ⚠ Adobe calls neither IsEqual nor IntersectionContainsDifferences anywhere in the SDK, so the
// header's contract is all there is to go on. Measured instead: see the audit note for the two
// searches - identical but for one attribute's value - that this was checked with.
void KBSSearchEngine::RememberFindFormat()
{
	ForgetFindFormat();		// whatever is remembered below replaces this, and a failure remembers nothing

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;

	// The database first, and on its own: it is the argument the call below takes, so it is tested
	// before it is handed over rather than afterwards (see DescribeGlyphQuery).
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

bool KBSSearchEngine::FindFormatHasChanged()
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

void KBSSearchEngine::ForgetSearchedFindFormat()
{
	// The public door onto the file-static pair - see the header for the rule it exists to make
	// keepable, and ForgetFindFormat above for why the list and its database go together.
	ForgetFindFormat();
}

PMString KBSSearchEngine::DescribeFormatSetting(bool findSide, bool limited)
{
	PMString out;
	// Everything appended below is DATA - either InDesign's own description of a setting or a style
	// the user named - so it must never be looked up in a string table.
	out.SetTranslatable(kFalse);

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return out;

	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// Tested before it is handed over, like every other reader of the format pane in this file
	// (2026-08-08). Empty is the honest answer with no database: the caller's contract already says
	// that empty means "say nothing extra", never "nothing is set".
	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil)
		return out;

	const AttributeBossList* const attrs = findSide
		? opts->GetFindAttributeBossList(db, mode)
		: opts->GetChangeAttributeBossList(db, mode, kFalse);	// kFalse: read, never create
	// ***** THE SEPARATOR BELONGS TO THE ATTRIBUTE, NOT TO THIS LIST. *****
	// AppendDescription writes " + size: 14 pt" - separator IN FRONT - because that is how the
	// Settings line in Style Options reads: "+ size: 14 pt + leading: 24 pt". So nothing is added
	// between entries here, and the first one arrives carrying a stray separator, dropped below.
	// (Measured 2026-08-04: adding ", " here as well produced "Find Format ( + size: 14 pt)".)
	//
	// Each piece is built on its own first, so that one that would take the line past
	// kKBSFormatDetailLimit can be left out WHOLE rather than cut in the middle of a word.
	// (With limited false nothing is ever left out, and `dropped` stays false throughout.)
	bool dropped = false;
	if (attrs != nil)
	{
		const int32 count = attrs->CountBosses();
		for (int32 i = 0; i < count && !dropped; ++i)
		{
			PMString piece;
			piece.SetTranslatable(kFalse);
			AppendOneAttribute(attrs, i, db, piece, !out.IsEmpty());
			dropped = !AppendWithinLimit(out, piece, limited);
		}
	}

	// The styles last, and still under the limit: they are the two conditions a user is most likely
	// to have set on purpose, but a line that runs off the dialog helps nobody.
	if (!dropped)
	{
		PMString piece;
		piece.SetTranslatable(kFalse);
		AppendStyleName(db, findSide ? opts->GetFindParaStyle(db, mode)
									 : opts->GetChangeParaStyle(db, mode, kFalse),
						"Paragraph style", piece, !out.IsEmpty());
		dropped = !AppendWithinLimit(out, piece, limited);
	}
	if (!dropped)
	{
		PMString piece;
		piece.SetTranslatable(kFalse);
		AppendStyleName(db, findSide ? opts->GetFindCharStyle(db, mode)
									 : opts->GetChangeCharStyle(db, mode, kFalse),
						"Character style", piece, !out.IsEmpty());
		dropped = !AppendWithinLimit(out, piece, limited);
	}

	// Say that something was left out. Silently cutting the list would read as "this is everything"
	// (user's decision, 2026-08-04) - the same reason the scans announce their own display cap.
	if (dropped)
		out.Append(" + ...");

	// Drop the leading separator the first attribute brought with it. " + " is ASCII whatever
	// language the description itself came back in, and a description that does NOT start with it is
	// left exactly as it came.
	//
	// ***** THREE CHARACTERS ARE TAKEN, NOT THE WHOLE LINE CONVERTED. ***** Append's second argument
	// is a character count (PMString.h:300-304), so the test copies the three it is about to look
	// at. This asked for out.GetUTF8String() until 2026-08-08 - a conversion of an entire settings
	// description, however long, to read its first three ASCII characters. (It asked
	// GetPlatformString for them until 2026-08-07, which PMString.h:863-866 names as the call to
	// avoid when UTF-8 will do; neither encoding was ever wanted here.)
	PMString leading;
	leading.SetTranslatable(kFalse);
	leading.Append(out, 3);
	PMString separator(" + ");
	separator.SetTranslatable(kFalse);
	if (leading.Compare(kTrue /*casesensitive*/, separator) == 0)
		out.Remove(0, 3);

	out.SetTranslatable(kFalse);
	return out;
}

PMString KBSSearchEngine::DescribeCurrentChange()
{
	PMString description;
	description.SetTranslatable(kFalse);

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return description;

	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	// The Glyph tab replaces one glyph with another - there is no change STRING at all - so the whole
	// line is the glyph, stated the same way the find side is. An empty Change To box gives an empty
	// line here, and the report leaves the heading out rather than writing "Glyph -1".
	if (mode == IFindChangeOptions::kGlyphSearch)
		return DescribeGlyphQuery(opts, false /*findSide*/);

	// The Transliterate tab writes a character type, stated the same way its find side is.
	if (mode == IFindChangeOptions::kTransliterateSearch)
	{
		description.Append(KBSSearchEngine::CharacterTypeName(
			static_cast<int32>(opts->GetReplaceCharacterType())));
		return description;
	}

	description.Append(opts->GetReplaceString(mode));

	// "dog  + Change Format (size: 20 pt)", or "Change Format (...)" alone when the box is empty and
	// only the formatting changes. Asked through HasFormatSet for the same reason the find side is:
	// a paragraph or character style is a format the attribute list does not carry.
	if (HasFormatSet(opts, mode, false /*findSide*/))
	{
		if (!description.IsEmpty())
			description.Append("  + ");
		description.Append("Change Format");
		AppendFormatDetail(description, false /*findSide*/);
	}

	// ***** NO "the matches will be deleted" HERE. ***** An empty Change To with no Change Format
	// does delete every match, and the prompt says so - but the prompt is ASKING, and this is a
	// record of what was set. The rows themselves are where a reader sees what became of the text.
	//
	// No tab name either: the Query: line directly above this one in the report has already said it.
	description.SetTranslatable(kFalse);
	return description;
}

bool KBSSearchEngine::HasFindFormatSet()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	return (opts != nil) && HasFormatSet(opts, opts->GetSearchMode(), true /*findSide*/);
}

bool KBSSearchEngine::HasChangeFormatSet()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	return (opts != nil) && HasFormatSet(opts, opts->GetSearchMode(), false /*findSide*/);
}

void KBSSearchEngine::BuildWalkSignature(PMString& outSignature)
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
	//
	// (Missed on 2026-08-08 along with HasFindQuery, when the four other readers of the pane in this
	// file were given this order; found on the fourth audit of this block.)
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
		// way HasFindQuery and DescribeCurrentQuery ask it.
		outSignature.Append(" q");
		outSignature.Append(opts->GetFindString(mode));
	}

	// The SECOND axis, kChange versus kTransliterate. The tab test upstream does not cover it: it
	// sits beside the tab, and a walk runs with whichever value was committed last - so results
	// searched under one value must not be replaced under the other.
	outSignature.Append(" cm");
	outSignature.AppendNumber(static_cast<int32>(opts->GetChangeMode()));

	// ----- the switches that decide WHICH occurrences of it come back -----
	// The four matching options first, then the five scope switches GetKBSWalkerScopeOptions reads.
	// Every one of them changes the match SET, and therefore the walk order the stored hits are
	// numbered by. (Kana and width sensitivity are CJK-only in the dialog, but they are asked for
	// unconditionally: an option that is not on screen can still be set, and a signature that only
	// covers what the current UI shows is a signature with a hole in it.)
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
	// signature - and asking it there is what closed the "same condition, different value" gap this
	// file carried until 2026-08-07.
	const AttributeBossList* const attrs = opts->GetFindAttributeBossList(db, mode);
	outSignature.Append(" n");
	outSignature.AppendNumber(attrs != nil ? attrs->CountBosses() : 0);

	// ...and the two conditions that list does NOT carry. A paragraph or character style set in the
	// format pane sits in a field of its own (see HasFormatSet), so without these a query that
	// differs only by "Find Format = Style A" versus "Style B" would carry an IDENTICAL signature
	// and the door that refuses a changed query would let it straight through. The UIDs go in raw:
	// this string is compared, never read.
	outSignature.Append(" s");
	outSignature.AppendNumber(static_cast<int32>(opts->GetFindParaStyle(db, mode).Get()));
	outSignature.Append("/");
	outSignature.AppendNumber(static_cast<int32>(opts->GetFindCharStyle(db, mode).Get()));

	outSignature.SetTranslatable(kFalse);
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
	// EditableFrameForMatch / IsFrameEditable before it writes and leaves a locked match alone.
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

// The frame is resolved exactly as BuildHit resolves it: the frame the match is composed into, or -
// for an overset match, composed but placed nowhere - the frame carrying the "+" indicator, which is
// the frame the hit's own locator already names. Resolving it the same way on both sides is what
// keeps "the row has no check box" and "the replace refuses" describing the same set of hits.
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

// NOT EditableFrameForMatch: that one climbs out to the frame carrying the "+" when a position is
// overset, which is exactly the case this has to answer TRUE for. The raw walk is the answer here.
bool KBSSearchEngine::IsPositionOverset(const UIDRef& storyRef, TextIndex pos)
{
	return FrameUIDForPosition(storyRef, pos) == kInvalidUID;
}

void KBSSearchEngine::SplitLineAroundMatch(const UIDRef& storyRef, TextIndex start, TextIndex end,
	PMString& outPre, PMString& outMatch, PMString& outPost)
{
	// One range, opened for this call alone - the shape a caller holding a single hit wants (the
	// replace pass rebuilding a row, and nothing else today). The search's own hits go through
	// ReadHitText instead, which reads the segments and the hash off ONE scanner; both end up in
	// SplitLineWithScanner, so a stored row and a rebuilt one are split identically.
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());

	WideString matchWide;		// the hash's shortcut, which this caller has no use for
	SplitLineWithScanner(scanner, start, end, outPre, outMatch, outPost, matchWide);
}

bool KBSSearchEngine::MatchIsSameOccurrence(const UIDRef& storyRef, TextIndex start, TextIndex end,
	UID expectStoryUID, TextIndex expectStart, TextIndex expectEnd, uint64 expectHash)
{
	if (storyRef.GetUID() != expectStoryUID)
		return false;

	// A posDelta was added in here until 2026-08-05 - how far the REPLACE pass had moved this story
	// ahead of this point, its own work cancelled out. The replace no longer asks this question at
	// all, and the jump (now the only caller) moves nothing, so there is nothing left to cancel.
	if (start != expectStart)
		return false;

	// ***** THE LENGTH. ***** Compared directly: a match that has been rewritten in place keeps its
	// start. Nothing asked this until 2026-08-04: the text comparison stood in for it, and that
	// comparison was capped at 500 characters - so a GREP match that had grown or shrunk past the
	// cap read as unchanged.
	if ((end - start) != (expectEnd - expectStart))
		return false;

	// ***** THE TEXT, WHOLE. ***** Not the drawn 500 characters (see GetHitMatchIdentity): the
	// stored hash covers the entire match, so a rewrite anywhere inside it is caught however long
	// it is. A stored 0 means the search could not read that match - nothing to compare against,
	// so nothing is written.
	if (expectHash == 0)
		return false;
	return KBSSearchEngine::HashMatchText(storyRef, start, end) == expectHash;
}

uint64 KBSSearchEngine::HashMatchText(const UIDRef& storyRef, TextIndex start, TextIndex end)
{
	// One range, opened for this call alone - see SplitLineAroundMatch above on who wants that
	// shape. The arithmetic itself is HashRangeWithScanner's, which is also what the search's own
	// hits go through (ReadHitText), so a hash taken here and one taken at search time are the same
	// number for the same text. That has to hold: comparing them IS the same-occurrence test.
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	return HashRangeWithScanner(scanner, start, end);
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

	// ***** EVERY REFUSAL BELOW COMES BEFORE THE MODEL IS TOUCHED. *****
	// A run that is turned away has to leave the panel exactly as it found it. The Clear() further
	// down used to sit up here, which meant "No search text set on the Text tab." also threw away the
	// results of the search before it - a command that did nothing but destroy the previous answer
	// (found 2026-08-03 in the defect audit). Nothing between here and the commit point writes to the
	// model, the book scope, or the Find/Change settings.
	//
	// Why the Clear() was ever this high: ListBookChapters records which book the run is against
	// (gSearchedBookPath) and ReleaseSearchedBook is what forgets it, so clearing AFTER the book is
	// resolved wipes the record the run has just made (measured 2026-08-02). That constraint is about
	// ListBookChapters, not about these checks - so the commit point simply moved down to just above
	// it, and the checks moved above the commit point.

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

	// Transliterate - the CJK character-type conversion (Kanji / kana / half- and full-width) - is
	// walked like any other text tab since 2026-08-05 (user's call: whatever the official panel is
	// set to, this panel searches and replaces the same way). It rides the same text walker and the
	// same find/replace commands; what it needs is its axes stated before the walk - the ChangeMode
	// and the find character type - which CommitSearchMode now does, the exact glyph rule again.
	// (It was refused here until the Japanese version came into use; on a Roman-featureset install
	// the tab cannot be reached at all, so the transliterate paths simply lie dormant there.)

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
		// query on another tab is no help and saying so avoids a hunt. The Glyph and Transliterate
		// tabs are worded for what they actually want - a glyph picked from a grid, a character
		// type chosen from a dropdown - because "type what to find" points at a field neither
		// tab has.
		const bool glyphTab = (tab == IFindChangeOptions::kGlyphSearch);
		const bool translitTab = (tab == IFindChangeOptions::kTransliterateSearch);
		outSummary.Append(glyphTab ? "No glyph set on the "
			: (translitTab ? "No character type set on the " : "No search text set on the "));
		PMString tabName(SearchModeName(tab));
		tabName.SetTranslatable(kFalse);
		outSummary.Append(tabName);
		outSummary.Append(glyphTab
			? " tab. Choose the glyph to find in Edit > Find/Change, then run the search from the panel flyout."
			: (translitTab
				? " tab. Choose the character type to find in Edit > Find/Change, then run the search from the panel flyout."
				: " tab. Type what to find in Edit > Find/Change, then run the search from the panel flyout."));
		return 0;
	}

	// Is there anything for the CURRENT scope to run on? Asked HERE, ahead of the commit point, so a
	// run with no target leaves the previous results on the panel. NO implicit fallback: ON means the
	// book and nothing else, OFF means the front document and nothing else - so the status line can
	// always state exactly what was searched, and a missing book is reported instead of quietly
	// searching one document behind the user's back.
	//
	// The same two questions KBSBookScope::HasScopeTarget asks for the menu's grey state; asked
	// separately here because each one has its own sentence to say.
	const bool fromBook = KBSBookScope::IsBookScopeOn();
	if (fromBook && !KBSBookScope::HasActiveBook())
	{
		outSummary.Append("Book Scope is on, but no book is open.");
		return 0;
	}
	if (!fromBook && Utils<ILayoutUIUtils>()->GetFrontDocument() == nil)
	{
		outSummary.Append("No open document to search.");
		return 0;
	}

	// State the tab before anything is walked. A walk runs in the mode last COMMITTED through
	// kFindSearchModeCmdBoss - not in the one IFindChangeOptions merely reports - so without this a
	// search driven from this panel ran as plain Text whatever tab was on screen. See
	// KBSSearchEngine::CommitSearchMode.
	//
	// ***** ABOVE THE COMMIT POINT, AND ITS ANSWER IS READ. ***** A tab that could not be stated is
	// a refusal like every other one in this function, so it has to leave the previous results
	// standing - which is why this sits here rather than three lines lower, where it swallowed its
	// own failure until 2026-08-08 and searched on in whatever mode had been committed last. Moving
	// it up changes nothing the user can see: it writes back the value it has just read.
	if (!KBSSearchEngine::CommitSearchMode(overrideFindGlyph))
	{
		outSummary.Append("The Find/Change tab could not be set, so nothing was searched. Try again, or reopen Edit > Find/Change.");
		return 0;
	}

	// ***** THE COMMIT POINT. ***** Past this line the run owns the panel: the old results are gone
	// whatever happens next.
	KBSResultModel::Clear();
	KBSBookScope::ReleaseSearchedBook();	// the two are one fact - see gSearchedBookPath
	// ...and the format the CLEARED rows were found with. RememberFindFormat overwrites it a few
	// lines below, so this line changes nothing on this path - it is here so that "every Clear()
	// forgets the format" is a rule with no exceptions to remember, the way ReleaseSearchedBook is.
	ForgetFindFormat();

	std::vector<KBSBookScope::ChapterDoc> targets;
	PMString bookName;
	// Chapters the book could not hand over at all. Declared out here so the summary can name them
	// whichever way this run ends - including the "no matches" and "nothing openable" exits, where
	// they are the only thing that explains what happened.
	std::vector<KBSBookScope::SkippedChapter> unopenable;
	if (fromBook)
	{
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
		// Re-read rather than carried down from the check above: a command has been processed since
		// (CommitSearchMode), and a pointer to the front document is not ours to assume survived it.
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
	//
	// Read again rather than reusing `tab` from the top of this function, and that is deliberate:
	// `tab` was taken BEFORE CommitSearchMode, and what belongs on the results is the mode the walk
	// is about to actually run in. The two agree today - the commit states the value it just read -
	// but this is the one that would still be right on the day they stopped agreeing.
	KBSResultModel::SetSearchMode(CurrentSearchModeValue());

	// ...and WHAT WAS ASKED FOR, for the heading of the file "Save Results..." writes. Recorded here,
	// beside the tab, because both answers have the same lifetime: they describe THESE rows, and the
	// user is free to retype the query the moment this search returns.
	KBSResultModel::SetQueryText(DescribeCurrentQuery());

	// ...and the whole of what this walk was DRIVEN BY - the query plus every switch that decides
	// which matches come back. The line above is a caption; this one is a key, and Change Checked
	// compares it before it re-walks. The tab alone is not enough: retyping the find string, or
	// turning Include Footnotes off, changes the match set without changing the tab, and the walk
	// order the hits below are numbered by would then point at other occurrences entirely.
	// See KBSSearchEngine::BuildWalkSignature.
	{
		PMString walkSignature;
		KBSSearchEngine::BuildWalkSignature(walkSignature);
		KBSResultModel::SetWalkSignature(walkSignature);
	}

	// ...and the FIND FORMAT itself, kept as a list rather than described in that string, because the
	// list compares itself properly and a hand-written fingerprint of it did not. Taken here, on the
	// same side of CommitSearchMode as the signature, for the same reason: a value read on one side
	// of a mode commit and compared against one read on the other could differ with nothing having
	// changed. See KBSSearchEngine::RememberFindFormat.
	KBSSearchEngine::RememberFindFormat();

	// ...and the five scope switches, read ONCE for the whole run. They come off the same dialog as
	// everything above, they are the same for every chapter by definition (the replace pass has to
	// re-walk with exactly these), and nothing can change them while the run is on: the progress bar
	// below is modal. Read inside CollectHitsInDoc until 2026-08-08, which asked the Find/Change
	// settings once per chapter for an answer that could not differ.
	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);

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

	// Where the bar stands as each chapter starts, and how far it has actually been advanced (that
	// second number is what lets KBSAdvanceProgress swallow an advance too small to repaint for, so
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
	// neither see them nor close them, and each holds its .indd locked. The two scans have always
	// counted these (KBSBookScope::AppendUnclosedNote); the search discarded the release's answer
	// until 2026-08-08, the one run of the four that did.
	std::vector<PMString> unclosed;
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

		// Cancel is asked here, and answered by the bar being moved from inside the walk
		// (KBSAdvanceProgress). WasCancelled only reads a flag; something has to have given the
		// button a chance to set it.
		// kFalse = do NOT raise the global error state: it would outlive the search and fail the
		// commands that come after it.
		if (progressBar.WasCancelled(kFalse))
		{
			cancelled = true;
			break;
		}

		// Room left under the whole-search safety ceiling; once it is gone, stop walking further
		// chapters too (the result set is full).
		const int32 remaining = KBSResultModel::kKBSCollectHitLimit - total;
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
		CollectHitsInDoc(chapterDocRef, static_cast<size_t>(remaining), scopeOptions, hits, docCapped,
			walkResult, &progressBar, progressBase, kKBSChapterProgressSpan, storiesInDoc,
			progressReported);

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
		//
		// ***** CLOSED ON THE SPOT, not scheduled. ***** A scheduled close does not run until the
		// current tick has unwound, and this run IS that tick - so every chapter handed back here
		// stayed open, and kept its .indd locked, until the whole search was over. Holding one
		// chapter at a time is the entire point of this loop, so the close has to happen here.
		// (Measured 2026-08-04 on the replace, which walks chapters the same way: four chapters,
		// four .idlk files standing at once.) Safe at this point - the walk has halted, the dirty
		// guard inside CollectHitsInDoc has already restored the flag, and everything read after
		// this (FinalizeChapterHits, the Chapter it fills in) is plain values, not database work.
		//
		// ***** ASKED FIRST WHETHER IT IS OURS - THE RELEASE'S false CANNOT BE READ ALONE. *****
		// The scans' shape (KBSGlyphScanEngine / KBSOversetScanEngine), with one more question on
		// the end: IsDocStillOpen tells "the user closed it under the run" - which is theirs to do
		// and nothing being left behind - from a chapter that is genuinely still standing. Without
		// it, that ordinary close was counted as "left open with no window" about a chapter that is
		// not open at all (the scans counted exactly that until 2026-08-08).
		const bool wasOurs = KBSBookScope::IsHeldDoc(chapterDocRef);
		if (!KBSBookScope::ReleaseHeldDoc(chapterDocRef, true /*close now*/)
			&& wasOurs && KBSBookScope::IsDocStillOpen(chapterDocRef))
			unclosed.push_back(targets[i].shortName);

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
		//
		// Handed over rather than copied: the model takes the hits and leaves this Chapter empty.
		// Safe because it is a fresh one per pass of this loop and the count above was taken before
		// the handover - and it is the point of the swap() four lines up, which used to be undone by
		// a copy on this line (2026-08-08).
		KBSResultModel::AppendChapter(std::move(chapter));

		// Stamp the chapter NOW, while its document is certainly open. The replace may run long
		// afterwards, with the chapter closed and reopened in between - which is exactly the case
		// the stamp is built to survive, but only if it was taken while there was something to read.
		//
		// The document comes from targets[] rather than the Chapter: the move above has just
		// emptied that one. The index is the position the append filled, which is the last.
		KBSEditStamp::CaptureChapter(KBSResultModel::GetChapterCount() - 1, targets[i].docRef);
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
		ForgetFindFormat();						// ...and the format those results were found with
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
		// "No matches" is a lie if a chapter was skipped, or if a walk broke off before it reached
		// the end of one - say so here too.
		AppendUnsearchableNote(outSummary, unsearchable);
		AppendSearchErrorNote(outSummary, brokeOff);
		KBSBookScope::AppendUnopenableNote(outSummary, unopenable);
		KBSBookScope::AppendUnclosedNote(outSummary, unclosed);
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
		outSummary.AppendNumber(KBSResultModel::kKBSCollectHitLimit);
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

	AppendUnsearchableNote(outSummary, unsearchable);
	AppendSearchErrorNote(outSummary, brokeOff);
	KBSBookScope::AppendUnopenableNote(outSummary, unopenable);
	KBSBookScope::AppendUnclosedNote(outSummary, unclosed);

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

void KBSSearchEngine::ShutdownCleanup()
{
	// The remembered Find Format is the only thing this file keeps that is not a plain value: an
	// AttributeBossList holding references to the dialog's attributes, and the raw IDataBase* those
	// UIDs belong to. Neither may still be standing when the .pln unloads - dropping the list runs
	// its destructor, which lets go of every attribute in it, and that is database work no static
	// destructor should be doing against an application that has already torn itself down.
	//
	// The same rule, and the same reason, as the three cleanups beside this one in
	// KBSStartupShutdown::Shutdown: KBSDrawEventHandler's marker holds a static PMString and a raw
	// IDataBase*, KBSBookScope holds its chapters, KBSResultModel holds the rows. This file joined
	// them on 2026-08-08 - the list arrived on 2026-08-07 and nothing was added here for it, so it
	// was the one piece of module state with no controlled point to be let go at.
	ForgetFindFormat();
}

// End, KBSSearchEngine.cpp.
