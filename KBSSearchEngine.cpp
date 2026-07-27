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
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ITextModel.h"
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"
#include "ISession.h"				// GetExecutionContextSession
// For naming the page a match sits on (the "P<page>(<n>)" hit-row locator):
#include "ITextParcelList.h"		// GetParcelContaining - the parcel a position is in
#include "IParcelList.h"			// GetParcelFrameUID - is that parcel placed in a frame?
#include "ParcelKey.h"				// ParcelKey::IsValid
#include "IHierarchy.h"				// the parcel frame as a page item
#include "ILayoutUtils.h"			// GetOwnerPageUID - frame -> page
#include "IPageList.h"				// GetPageString / GetPageIndex - page -> "12" / "A:1"

// General includes:
#include "TextWalkerServiceProviderID.h"	// kFindTextCmdBoss, kFindChangeClientBoss, kTextWalkerService(...)
#include "WalkerScopeOptions.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
#include "ProgressBar.h"			// TaskProgressBar / SuppressProgressBarDisplay (the book search's progress + cancel)
#include "CmdUtils.h"
#include "CreateObject.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "PersistUtils.h"			// ::GetUIDRef
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "Utils.h"
#include "WideString.h"

#include <vector>
#include <algorithm>				// std::stable_sort (the matches' page order)

// Project includes:
#include "KBSSearchEngine.h"
#include "KBSBookScope.h"
#include "KBSResultModel.h"
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

// A search is running. The progress bar pumps events while it is up, so without this a menu command
// could be dispatched INTO the running search. The panel's actions read it through IsSearching().
bool gSearching = false;

// Raise gSearching for the length of a search, whichever way SearchBook returns.
struct SearchingFlagGuard
{
	SearchingFlagGuard()	{ gSearching = true; }
	~SearchingFlagGuard()	{ gSearching = false; }
};

// Is there any text to find on the Find/Change panel right now (in the current mode)?
bool HasFindQuery()
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return false;
	// The find-what for the mode the user is actually in (Text vs GREP each have their own).
	const WideString& findText = opts->GetFindString(opts->GetSearchMode());
	return !findText.empty();
}

// A frame UID -> its page, named the way the Pages panel names it (section prefix and all). Shared
// by the visible-match path (the match's own frame) and the overset path (the "+" indicator's
// frame). false if the frame has no owner page or the page has no name. outPageIndex is the page's
// plain document order (for sorting; the STRING can be "iv" / "A-1" under a section).
bool GetFramePageString(const UIDRef& docRef, UID frameUID, PMString& outPage, int32& outPageIndex)
{
	outPage.Clear();
	outPage.SetTranslatable(kFalse);
	outPageIndex = -1;
	if (frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(docRef.GetDataBase(), frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;
	const UID pageUID = Utils<ILayoutUtils>()->GetOwnerPageUID(frameHier);
	if (pageUID == kInvalidUID)
		return false;

	InterfacePtr<IPageList> pageList(docRef, UseDefaultIID());
	if (pageList == nil)
		return false;
	pageList->GetPageString(pageUID, &outPage);	// defaults: section-aware, abbreviated
	outPage.SetTranslatable(kFalse);
	outPageIndex = pageList->GetPageIndex(pageUID);
	return !outPage.IsEmpty();
}

// The page a visible match position sits on. Ported from KESCL: position -> parcel -> frame, then
// GetFramePageString. Returns false for an overset position (composed but placed in no frame) and
// the query failures around it, which read the same to the user: no page for the match itself. The
// caller then names the "+" locator's page instead.
bool GetMatchPageString(const UIDRef& docRef, const UIDRef& storyRef, TextIndex pos,
	PMString& outPage, int32& outPageIndex)
{
	outPage.Clear();
	outPage.SetTranslatable(kFalse);
	outPageIndex = -1;

	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return false;
	InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
	if (tpl == nil)
		return false;
	const ParcelKey key = tpl->GetParcelContaining(pos);
	if (!key.IsValid())
		return false;
	InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
	if (pl == nil)
		return false;
	const UID frameUID = pl->GetParcelFrameUID(key);
	if (frameUID == kInvalidUID)
		return false;	// overset: composed but placed in no frame

	return GetFramePageString(docRef, frameUID, outPage, outPageIndex);
}

// Fill a hit from one match (story, [start, end)): its jump anchors, and the containing
// paragraph's text split into (before / matched / after) at the exact UTF-16 offsets.
void BuildHit(const UIDRef& docRef, const UIDRef& storyRef, TextIndex start, TextIndex end, KBSResultModel::Hit& outHit)
{
	outHit.storyUID = storyRef.GetUID();
	outHit.textStart = start;
	outHit.textEnd = end;

	// The page this match sits on (for the "P<page>(<n>) " hit-row locator). Recomposes on demand -
	// fine, the caller's dirty guard is up. When the match is overset (no page of its own), name the
	// page of the "+" overset indicator instead (the last placed parcel's frame, climbing out of a
	// pushed-out table) so the hit lists as "ovP<page>(n)" and sorts into that page. If nothing is
	// placed anywhere, leave it pageless (locator falls back to a bare "ov", sorted to the end).
	if (!GetMatchPageString(docRef, storyRef, start, outHit.pageString, outHit.pageIndex))
	{
		outHit.isOverset = true;
		const KBSOversetLoc loc = KBSFindOversetLocator(storyRef, start);
		if (!loc.found || !GetFramePageString(docRef, loc.frameUID, outHit.pageString, outHit.pageIndex))
		{
			outHit.pageString.Clear();
			outHit.pageIndex = -1;
		}
	}

	// The line's three drawn segments. Shared with the replace pass, which rebuilds a replaced
	// row exactly the same way from the range the replace command hands back.
	KBSSearchEngine::SplitLineAroundMatch(storyRef, start, end,
		outHit.preText, outHit.matchText, outHit.postText);
}

// Walk one document with the user's current Find/Change query and collect every match as a Hit.
// Read-only: the whole walk sits inside a SaveRestoreModifiedState dirty guard, so a windowless
// chapter can be closed afterwards without wanting a save. NOTHING is set on opts - the walk uses
// the user's Find/Change settings verbatim, so the search mode (Text or GREP) is followed.
void CollectHitsInDoc(const UIDRef& docRef, size_t maxHits, std::vector<KBSResultModel::Hit>& outHits, bool& outCapped)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;

	IDataBase::SaveRestoreModifiedState dirtyGuard(docRef.GetDataBase());

	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	if (registry == nil)
		return;
	InterfacePtr<IK2ServiceProvider> provider(registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss));
	if (provider == nil)
		return;

	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	if (walker == nil)
		return;

	// Always start a fresh walk from the top of the document.
	if (walker->IsWalking())
		walker->Halt();

	// The single shared scope definition - the replace pass re-walks each chapter with exactly
	// these options, or the walk order the hits were numbered by would no longer line up.
	WalkerScopeOptions scopeOptions;
	KBSSearchEngine::GetKBSWalkerScopeOptions(scopeOptions);

	InterfacePtr<ITextWalkerScope> scope(Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions));
	if (scope == nil)
		return;

	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (client == nil)
		return;

	walker->Initialize(client, scope, opts, nil);

	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	if (selUtils == nil)
		return;

	// Required critical section around text-walker selection changes.
	const TextWalkerSelections_CriticalSection criticalSection(selUtils);

	// Walk the whole document. Each ProcessCommand advances the walker to the next match ("find
	// next"), so we keep going until no more hits. prev* is a safety net: if the finder ever hands
	// back the exact same occurrence twice in a row (a query that does not advance the walker, e.g.
	// a zero-width GREP match), stop this walk rather than spin forever.
	TextIndex prevStart = kInvalidTextIndex;
	TextIndex prevEnd   = kInvalidTextIndex;
	UID       prevStory = kInvalidUID;
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

		// Whole-search safety ceiling reached: stop collecting. More matches may exist, but the
		// result set is capped so a match-everywhere query cannot grow it without bound.
		if (outHits.size() >= maxHits)
		{
			outCapped = true;
			break;
		}

		KBSResultModel::Hit hit;
		BuildHit(docRef, story, start, end, hit);
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
			PMString locator;
			locator.SetTranslatable(kFalse);
			if (hits[k].pageString.IsEmpty())
			{
				locator.Append("ov");	// overset with nothing placed anywhere: no page to name
			}
			else
			{
				// An overset hit carries the "+" indicator's page and sorts by it; a trailing
				// "ov" (after the page and ordinal) marks it as overset -> e.g. "P1(2)ov".
				locator.Append("P");
				locator.Append(hits[k].pageString);
				if (runCount > 1)
				{
					const int32 ordinal = static_cast<int32>(k - i) + 1;
					locator.Append("(");
					locator.AppendNumber(ordinal);
					locator.Append(")");
				}
				if (hits[k].isOverset)
					locator.Append("ov");
			}
			// The locator is its own part now (drawn at full colour, then a tab stop before the
			// line text) - the colour cell keeps it separate from the faded line segments.
			hits[k].locator = locator;
		}
		i = j;
	}
}

} // anonymous namespace

void KBSSearchEngine::GetKBSWalkerScopeOptions(WalkerScopeOptions& outOptions)
{
	// Whole document: master pages / locked layers / locked stories / footnotes (all defaults),
	// but exclude hidden layers - a match the user cannot see is not a useful result, and the
	// replace pass must not touch one either.
	outOptions.SetIncludeHiddenLayers(kFalse);
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
	PMString paraStr(para);
	int32 n = 0;
	const UTF16TextChar* buf = paraStr.GrabUTF16Buffer(&n);
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

int32 KBSSearchEngine::SearchBook(PMString& outSummary)
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
	const SearchingFlagGuard searchingGuard;

	KBSResultModel::Clear();

	if (!HasFindQuery())
	{
		outSummary.Append("No Find/Change text set. Type what to find in Edit > Find/Change, then run the search from the panel flyout.");
		return 0;
	}

	// Resolve the scope from the Book Scope toggle. NO implicit fallback: ON means the book and
	// nothing else, OFF means the front document and nothing else - so the status line can always
	// state exactly what was searched, and a missing book is reported instead of quietly searching
	// one document behind the user's back.
	std::vector<KBSBookScope::ChapterDoc> targets;
	PMString bookName;
	const bool fromBook = KBSBookScope::IsBookScopeOn();
	if (fromBook)
	{
		if (!KBSBookScope::HasActiveBook())
		{
			outSummary.Append("Book Scope is on, but no book is open.");
			return 0;
		}
		if (!KBSBookScope::GetBookChapterDocs(targets, bookName) || targets.empty())
		{
			outSummary.Append("The active book has no openable chapters.");
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

	// Walk every target; only chapters that hold a hit go into the model (no empty branches). The
	// model was cleared above; each chapter is APPENDED as it finishes and the panel is refreshed
	// right then, so the tree grows chapter by chapter instead of appearing all at once at the end.
	// The progress bar. Book scope only: a one-document search is a single step with nothing to
	// cancel between, so it is suppressed there (SuppressProgressBarDisplay keeps it off screen).
	// DisableChildProgressBars keeps the windowless chapter opens from raising bars of their own.
	//
	// showImmediate = kTrue: a book search ALWAYS puts the bar up. The default (kFalse) makes the bar
	// wait out an internal delay first, and the search beat that delay even at 5000+ hits (measured
	// 2026-07-27) - so the one thing the bar is really there for, the cancel button, was never on
	// screen. Better a brief flash on a fast book than a search that cannot be stopped.
	PMString progressTitle("Searching book...");
	progressTitle.SetTranslatable(kFalse);
	const SuppressProgressBarDisplay suppressBar(fromBook ? kFalse : kTrue);
	TaskProgressBar progressBar(progressTitle, static_cast<int32>(targets.size()), kTrue, kTrue);
	progressBar.DisableChildProgressBars(kTrue);

	int32 total = 0;
	int32 chaptersWithHits = 0;
	bool collectionTruncated = false;
	bool cancelled = false;
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
		progressBar.SetTaskStatus(taskLine);
		progressBar.DoTask(targets[i].shortName);

		// Cancel is looked at HERE ONLY - between chapters. Inside a chapter the walk sits in a
		// TextWalkerSelections critical section and WasCancelled pumps events, so asking in there
		// would run UI work in the middle of a text walk; safety wins over the response time. The
		// cost is that a huge chapter finishes before a press of Cancel takes effect.
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

		std::vector<KBSResultModel::Hit> hits;
		bool docCapped = false;
		CollectHitsInDoc(targets[i].docRef, static_cast<size_t>(remaining), hits, docCapped);
		if (docCapped)
			collectionTruncated = true;
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

	// Cancelled: throw the half-finished result away rather than leave a partial list looking like a
	// complete one, and give the chapters back - the results that would have needed them are gone.
	// ReleaseHeldDocs schedules its closes, so it is safe to call from in here.
	if (cancelled)
	{
		KBSResultModel::Clear();
		KBSBookScope::ReleaseHeldDocs();
		outSummary.Clear();
		outSummary.SetTranslatable(kFalse);
		outSummary.Append("Search cancelled.");
		return 0;
	}

	// Task 3: the windowless chapters stay HELD so a hit-row jump can reach them without a
	// document load. They are released only when a DIFFERENT book is searched (KBSBookScope's
	// book-switch guard) or at shutdown - a same-book re-search reuses them.

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
		return 0;
	}

	// The one-line summary. The hit count leads, so it stays visible even when the narrow
	// single-line status field truncates the tail.
	outSummary.AppendNumber(total);
	outSummary.Append(" hit(s)");
	if (fromBook)
	{
		PMString chapStr;	chapStr.AppendNumber(chaptersWithHits);
		outSummary.Append(" in ");
		outSummary.Append(chapStr);
		outSummary.Append(" chapter(s) - book \"");
		outSummary.Append(bookName);
		outSummary.Append("\".");
	}
	else
	{
		outSummary.Append(" - document \"");
		outSummary.Append(targets[0].shortName);
		outSummary.Append("\".");
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
	return total;
}

bool KBSSearchEngine::IsSearching()
{
	return gSearching;
}

// End, KBSSearchEngine.cpp.
