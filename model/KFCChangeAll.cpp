//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Change All in Book (No List) and Clear Results - see KFCChangeAll.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "ICommandSequence.h"		// IAbortableCmdSeq - the run's one undo step, and a cancel that is stated
#include "IDataBase.h"
#include "IFindChangeCmdData.h"
#include "IFindChangeOptions.h"
#include "IFindChangeService.h"		// FindChangeResult
#include "IK2ServiceProvider.h"
#include "IK2ServiceRegistry.h"
#include "ISession.h"
#include "ITextWalker.h"			// also declares ITextWalkerClient
#include "ITextWalkerScope.h"
#include "ITextWalkerSelectionUtils.h"	// TextWalkerSelections_CriticalSection
#include "IWalkerScopeFactoryUtils.h"

// General includes:
#include "CmdUtils.h"
#include "CreateObject.h"
#include "ErrorUtils.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "TextWalkerServiceProviderID.h"	// kReplaceAllTextCmdBoss / kFindChangeClientBoss / the walker service
#include "Utils.h"

#include <utility>		// std::pair - a chapter and how many it got
#include <vector>

// Project includes:
#include "KFCChangeAll.h"
#include "KFCBookScope.h"
#include "KFCDiag.h"
#include "KFCID.h"				// kKFCChangeAllStepKey
#include "KFCLoc.h"
#include "KFCObjectReplace.h"	// ReplaceAllInDoc - the Object tab's Change All in one chapter (1.4.0)
#include "KFCObjectSearch.h"	// the Object tab's doors and Search:, the shared walker aimed back at the front
#include "KFCProgressBar.h"		// the run's bar - the UI half's, asked for through IKFCUIServices
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCSearchEngine.h"

namespace
{
bool gRunning = false;

struct RunningFlagGuard
{
	RunningFlagGuard()	{ gRunning = true; }
	~RunningFlagGuard()	{ gRunning = false; }
};

/** "  By chapter: <name> (<n>), ... ." - how many each chapter got (the author's addition: Change All in Book's
	result broken down by chapter): the chapters written only, in the book's order, ten at most and then "and N more" - the
	message line is four lines high (spec map PNL-10). It goes last, after the notes, so a chapter that could not be
	opened or was left open is never pushed out of sight by it. Two spaces before it, as before every note
	(KFCBookScope::AppendChapterNote), and the names raw, as there (the line is drawn by hand). */
void AppendByChapter(PMString& outSummary, const std::vector<std::pair<PMString, int32> >& written)
{
	if (written.empty())
		return;
	const size_t kShown = 10;
	outSummary.Append("  By chapter: ");
	for (size_t i = 0; i < written.size() && i < kShown; ++i)
	{
		if (i > 0)
			outSummary.Append(", ");
		PMString name(written[i].first);
		name.SetTranslatable(kFalse);
		outSummary.Append(name);
		outSummary.Append(" (");
		outSummary.AppendNumber(written[i].second);
		outSummary.Append(")");
	}
	if (written.size() > kShown)
	{
		outSummary.Append(" and ");
		outSummary.AppendNumber(static_cast<int32>(written.size() - kShown));
		outSummary.Append(" more");
	}
	outSummary.Append(".");
}

/** " (K partially)" - the items InDesign's object Change All changed only in part (1.4.0, spec O13); nothing when none. */
void AppendPartially(PMString& outSummary, int32 partially)
{
	if (partially <= 0)
		return;
	outSummary.Append(" (");
	outSummary.AppendNumber(partially);
	outSummary.Append(" partially)");
}
}	// anonymous namespace

bool KFCChangeAll::IsRunning()
{
	return gRunning;
}

const char* KFCChangeAll::CommandName()
{
	// One name (F18): a document's Change All is InDesign's own dialog's, so this command is the book's alone - said
	// as the selected documents' when Find/Change Selected Documents (Book) narrows it to the Book panel's selection
	// (asked of the one place that narrows the run, KFCBookScope's, as Find's name is).
	if (KFCBookScope::IsBookScopeOn())
	{
		PMString bookName;
		KFCBookScope::BookSelection selection;
		if (KFCBookScope::DescribeTargetBook(bookName, selection) && selection.selected > 0)
			return "Change All in Selected Documents (No List)";
	}
	return "Change All in Book (No List)";
}

bool KFCChangeAll::WriteDocument(const UIDRef& docRef, int32 selectionScope, const WalkerScopeOptions& scopeOptions,
	int32& outCount)
{
	outCount = 0;
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(registry != nil
		? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss) : nil);
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	InterfacePtr<ITextWalkerScope> scope(
		(selectionScope == static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope))
			? Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions)
			: Utils<IWalkerScopeFactoryUtils>()->QueryWalkerScope_UsingSelections(
				static_cast<IWalkerScopeFactoryUtils::WalkScopeType>(selectionScope), scopeOptions));
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(::CreateObject2<ITextWalkerClient>(kFindChangeClientBoss)));
	if (opts == nil || walker == nil || scope == nil || client == nil)
		return false;
	if (walker->IsWalking())
		walker->Halt();
	walker->Initialize(client, scope, opts, nil);
	InterfacePtr<ITextWalkerSelectionUtils> selUtils(walker, UseDefaultIID());
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kReplaceAllTextCmdBoss));
	InterfacePtr<IFindChangeCmdData> cmdData(cmd, UseDefaultIID());
	bool ok = false;
	if (selUtils != nil && cmd != nil && cmdData != nil)
	{
		const TextWalkerSelections_CriticalSection criticalSection(selUtils);
		cmdData->SetTextWalker(walker);
		if (CmdUtils::ProcessCommand(cmd) == kSuccess)
		{
			// FAILED = its answer is kFailure, OR it left the error state raised: the caller's step would end by that error
			// (rolled back), so a write cleared of it and reported done would stand under a message InDesign did not mean -
			// the Object tab's Change All asks the same two (KFCObjectReplace::ReplaceAllInDoc). The state is cleared below
			// either way; a failure is the caller's to act on - it takes its whole step back (Run, KFCQuerySequence::Run).
			const IFindChangeService::FindChangeResult result = cmdData->GetFindChangeResult();
			ok = (result != IFindChangeService::kFailure) && ErrorUtils::PMGetGlobalErrorCode() == kSuccess;
			const int32 count = cmdData->GetReplacementCount();
			outCount = (count > 0) ? count : 0;
			KFC_DIAG_LOG("CHANGEALL doc=%p scope=%d result=%d count=%d", (void*)docRef.GetDataBase(), (int)selectionScope,
				(int)result, (int)count);
		}
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (walker->IsWalking())
		walker->Halt();
	return ok;
}

bool KFCChangeAll::ClearResults(PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	if (KFCResultModel::GetTotalHitCount() <= 0)
	{
		outStatus.Append("Clear Results: the panel holds no list.");
		return false;
	}
	KFCSearchEngine::DropResults();
	outStatus.Append("Results cleared.");
	return true;
}

int32 KFCChangeAll::Run(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// ===== THE DOORS - nothing touched before the chapters are opened.
	if (gRunning)
	{
		outSummary.Append("A Change All is already in progress.");
		return 0;
	}
	if (KFCRunGuard::IsAnyRunning())
	{
		outSummary.Append(KFCRunGuard::BusyMessage());
		return 0;
	}
	const RunningFlagGuard runningGuard;
	KFC_DIAG_PHASE(phaseRun, "change-all");		// a test build's timer (KFCDiag.h)
	// ONLY WITH NO LIST (the old design's C4): a list describes the documents as they were searched, and a Change All
	// under it would leave it describing text that is gone. The menu greys the item; a caller that never went through
	// the menu (a shortcut, a script firing the action) is turned away here.
	if (KFCResultModel::GetTotalHitCount() > 0)
	{
		outSummary.Append("Change All: the panel holds a list - choose Clear Results first.");
		return 0;
	}
	if (!KFCSearchEngine::CanSearchTab(KFCSearchEngine::CurrentSearchMode()))
	{
		outSummary.Append("Change All: the Colour tab is not supported.");
		return 0;
	}
	// THE OBJECT TAB (1.4.0, spec O13): InDesign's object Change All over each chapter. Refused with nothing to find - an
	// empty Find Object Format would change every frame of the Type in the book - or nothing to change to. (The menu greys
	// the item with nothing to find - HasFindQueryNow - so these doors are for a caller that never went through the menu.)
	const bool objectTab = (KFCSearchEngine::CurrentSearchMode() == IFindChangeOptions::kObjectSearch);
	if (objectTab && !KFCObjectSearch::HasFindObjectFormat())
	{
		outSummary.Append("Change All: nothing to find on the Object tab - set Find Object Format or an object style in Edit > Find/Change first.");
		return 0;
	}
	if (objectTab && !KFCObjectSearch::HasChangeObjectFormat())
	{
		outSummary.Append("Change All: nothing to change to on the Object tab - set Change Object Format or an object style first.");
		return 0;
	}
	if (!KFCSearchEngine::HasFindQueryNow())
	{
		outSummary.Append("Change All: nothing to find - type it in Edit > Find/Change first.");
		return 0;
	}
	// THE BOOK'S ALONE (F18): with Book Scope off, a document's Change All is InDesign's own dialog's. Asked before
	// ResolveRunScope, which with the scope off can refuse first over a Search: it cannot follow - not the reason.
	if (!KFCBookScope::IsBookScopeOn())
	{
		outSummary.Append("Change All in Book (No List) needs Book Scope on.");
		return 0;
	}
	KFCSearchEngine::RunScope scope;
	if (!(objectTab ? KFCObjectSearch::ResolveObjectRunScope(scope, outSummary)
			: KFCSearchEngine::ResolveRunScope(scope, outSummary)))
		return 0;
	// THE TAB AND THE CHANGE SIDE, STATED BEFORE THE SEQUENCE (outside any command sequence - CommitSearchMode's rule):
	// what the command writes is what the dialog shows. Not for the Object tab (1.4.0): its Change All is the service's
	// own object replace, which no text walker mode steers - CommitSearchMode turns that tab away by design, as in
	// KFCSearchEngine::SearchBook.
	if (!objectTab && !KFCSearchEngine::CommitSearchMode())
	{
		outSummary.Append("Change All: the Find/Change tab could not be stated - nothing was changed.");
		return 0;
	}
	if (!objectTab && !KFCSearchEngine::CommitReplaceSide())
	{
		outSummary.Append("Change All: the Change To in Find/Change could not be stated - nothing was changed.");
		return 0;
	}

	// ===== THE COMMIT POINT - the results thrown away (KFCSearchEngine::DropResults: the rows, the searched book and the
	// chapters it holds, the find format) before the book's chapters are listed, as the search and the query run do. What
	// can stand here is a search that found nothing - Change All runs only with no list (above) - and a book search leaves
	// its book's row up then, "<book>  (0/0)". Left standing, that row read "(0/0)" over a run that wrote (the query run's
	// reason for leaving the panel empty - KFCQuerySequence.cpp, "NO LIST"), and ListBookChapters below records THIS
	// run's book as the searched one (its header asks for ReleaseSearchedBook first): the row then named one book and
	// KFC watched another - closing the book Change All wrote cleared the row ("Results cleared - the book was closed."),
	// closing the row's own book left it, and a click on it brought the other book forward (the final audit's D9-3,
	// 2026-10-09 - case ca-after-other-book-empty). The chapters held from before go now, not on a schedule (SearchBook says
	// why). The UI half draws the empty tree when the run returns (KFCActionComponent).
	// THE PANEL'S PICTURE STAYS WHAT IT WAS (spec map PNL-07 / PNL-08): the results go, the fact that a search has run does
	// not - DropResults' Clear() takes it away with them, and only a Change All that WROTE changes the picture (the pencil
	// cat, NoteChangeAllWrote at the end).
	const bool hadRun = KFCResultModel::HasRun();
	KFCBookScope::ReleaseHeldDocs(true /*close now*/);
	KFCSearchEngine::DropResults();
	if (hadRun)
		KFCResultModel::NoteRun();

	// ===== THE CHAPTERS, OPENED AND HELD FOR THE WHOLE RUN, as the query run holds them: an open inside the sequence
	// throws the undo history of what was written away (KFCQuerySequence.h).
	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	bookName.SetTranslatable(kFalse);
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	std::vector<KFCBookScope::ChapterDoc> listed;
	KFCBookScope::BookSelection selection;		// selected 0 = the whole book (Find/Change Selected Documents (Book))
	if (!KFCBookScope::ListBookChapters(listed, bookName, &selection) || listed.empty())
	{
		outSummary.Append("That book has no chapters.");
		return 0;
	}
	for (size_t i = 0; i < listed.size(); ++i)
		if (KFCBookScope::OpenChapterDoc(listed[i], &unopenable))
			targets.push_back(listed[i]);
	if (targets.empty())
	{
		KFCBookScope::ReleaseHeldDocs(true);
		outSummary.Append("No chapter could be opened - nothing was changed.");
		KFCBookScope::AppendUnopenableNote(outSummary, unopenable);
		return 0;
	}
	std::vector<bool> wasModified;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		IDataBase* const db = targets[i].docRef.GetDataBase();
		wasModified.push_back(db != nil && db->IsModified() != kFalse);
	}
	WalkerScopeOptions scopeOptions;
	KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);

	// ===== ONE SEQUENCE AROUND EVERY CHAPTER - one Ctrl+Z for the whole book.
	int32 replaced = 0;
	int32 partially = 0;		// the Object tab's items changed only in part (1.4.0, spec O13)
	std::vector<bool> wroteTo(targets.size(), false);		// by target: did its Change All write anything
	std::vector<std::pair<PMString, int32> > written;		// the By chapter list: each chapter written, how many (book order)
	bool cancelled = false, failed = false;
	PMString why;
	why.SetTranslatable(kFalse);
	IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KFC Change All");
	if (seq == nil)
	{
		KFCBookScope::ReleaseHeldDocs(true);
		outSummary.Append("Could not start an undoable step - nothing was changed.");
		return 0;
	}
	seq->SetName(KFCLoc::Text(kKFCChangeAllStepKey, KFCJa::kChangeAllStep));
	{
		KFCProgressBar bar(KFCLoc::Text(kKFCChangeAllStepKey, KFCJa::kChangeAllStep), 0,
			static_cast<int32>(targets.size()), kTrue, kTrue);
		for (size_t d = 0; d < targets.size(); ++d)
		{
			bar.SetPosition(static_cast<int32>(d));
			bar.SetTaskText(targets[d].shortName);
#ifdef KFC_DIAG
			// (Fault switch changeall-cancel, a test build's only - KFCDiag.h: Cancel taken as pressed once the first
			// chapter is written, so a test stops the run at a known place - case all-cancel-book.)
			if (d == 1 && KFC_DIAG_FAULT("changeall-cancel"))
			{
				cancelled = true;
				break;
			}
#endif
			// between chapters: one document's Change All does not stop half way (the dialog's own rule)
			if (bar.WasCancelled(kFalse))
			{
				cancelled = true;
				break;
			}
			int32 count = 0, partial = 0;
			const bool wrote = objectTab
				? KFCObjectReplace::ReplaceAllInDoc(targets[d].docRef, count, partial)
				: WriteDocument(targets[d].docRef, static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope), scopeOptions, count);
			if (!wrote)
			{
				failed = true;
				why = "InDesign's Change All failed in ";
				why.Append(targets[d].shortName);
				break;
			}
			partially += partial;
			if (count > 0)
			{
				replaced += count;
				wroteTo[d] = true;
				written.push_back(std::make_pair(targets[d].shortName, count));
			}
		}
		// a Cancel pressed during the last chapter (KFCAdvanceProgress's note: it has to be ASKED)
		if (!cancelled && !failed && bar.WasCancelled(kFalse))
			cancelled = true;
	}
	if (!cancelled && !failed && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		failed = true;
		why = ErrorUtils::PMGetGlobalErrorString();
	}
	if (cancelled || failed)
		CmdUtils::AbortCommandSequence(seq);
	else
		CmdUtils::EndCommandSequence(seq);
	seq = nil;
	if (objectTab)
		KFCObjectSearch::AimSharedWalkerAtFront();		// O7 - the walker named the book's chapters

	if (cancelled || failed)
	{
		// the text is back; the "unsaved" flag is put back by hand (AbortCommandSequence leaves it - the replace's rule)
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
		for (size_t i = 0; i < targets.size(); ++i)
		{
			if (wasModified[i] || !KFCBookScope::IsDocStillOpen(targets[i].docRef))
				continue;
			IDataBase* const db = targets[i].docRef.GetDataBase();
			if (db != nil)
				db->SetModified(kFalse);
		}
		KFCBookScope::ReleaseHeldDocs();
		KFCResultModel::NoteChangeAllWrote(false);		// the panel's pencil cat: nothing was written
		if (cancelled)
			outSummary.Append("Cancelled - nothing was changed.");
		else
		{
			outSummary.Append("Stopped - nothing was changed: ");
			outSummary.Append(why);
			outSummary.Append(".");
		}
		return 0;
	}

	// ===== AFTER THE SEQUENCE: a window for each held chapter written to, the rest handed back (the replace's rule).
	std::vector<PMString> unclosed;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		if (wroteTo[i])
		{
			// A WINDOW THAT COULD NOT BE OPENED IS SAID: the chapter stays held and windowless with what was written in it,
			// which is "left open with no window" (measured with the fault switch no-window - case xq-ca-nowindow: it
			// stood open in silence). Not a chapter closed since: that one is not left open.
			if (KFCBookScope::IsHeldDoc(targets[i].docRef) && !KFCBookScope::ShowChapterWindow(targets[i].docRef)
				&& KFCBookScope::IsDocStillOpen(targets[i].docRef))
				unclosed.push_back(targets[i].shortName);
			continue;
		}
		// nothing written: it comes out as it went in (a walk can mark a database modified without changing a character)
		if (!wasModified[i] && KFCBookScope::IsDocStillOpen(targets[i].docRef))
		{
			IDataBase* const db = targets[i].docRef.GetDataBase();
			if (db != nil)
				db->SetModified(kFalse);
		}
		if (!KFCBookScope::HandBackHeldDocNow(targets[i].docRef))
			unclosed.push_back(targets[i].shortName);
	}

	// ===== THE MESSAGE (the spec's section 4; the selected documents' form - the query dialog's spec, section 4-4).
	if (replaced == 0 && selection.selected > 0)
	{
		outSummary.Append("No match in the ");
		outSummary.AppendNumber(selection.selected);
		outSummary.Append(" selected document(s) - nothing was changed.");
	}
	else if (replaced == 0)
		outSummary.Append("No match - nothing was changed.");
	else if (selection.selected > 0)
	{
		// "N replaced in M of S selected document(s) (T in the book)": M written, S the Book panel's selected ones
		outSummary.AppendNumber(replaced);
		outSummary.Append(" replaced in ");
		outSummary.AppendNumber(static_cast<int32>(written.size()));
		outSummary.Append(" of ");
		outSummary.AppendNumber(selection.selected);
		outSummary.Append(" selected document(s) (");
		outSummary.AppendNumber(selection.total);
		outSummary.Append(" in the book)");
		AppendPartially(outSummary, partially);
		outSummary.Append(".");
	}
	else
	{
		outSummary.AppendNumber(replaced);
		outSummary.Append(" replaced in ");
		outSummary.AppendNumber(static_cast<int32>(written.size()));
		outSummary.Append(" chapter(s)");
		AppendPartially(outSummary, partially);
		outSummary.Append(".");		// one undo step; nothing said of Ctrl+Z (spec F20)
	}
	KFCBookScope::AppendUnopenableNote(outSummary, unopenable);
	KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
	AppendByChapter(outSummary, written);		// last - see AppendByChapter
	KFCResultModel::NoteChangeAllWrote(replaced > 0);		// the panel's pencil cat (KFCPanelIcon)
	return replaced;
}

// End, KFCChangeAll.cpp.
