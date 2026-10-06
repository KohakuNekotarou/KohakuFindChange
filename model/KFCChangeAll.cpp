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

#include <vector>

// Project includes:
#include "KFCChangeAll.h"
#include "KFCBookScope.h"
#include "KFCDiag.h"
#include "KFCID.h"				// kKFCChangeAllStepKey
#include "KFCLoc.h"
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

bool SameDoc(const UIDRef& a, const UIDRef& b)
{
	return a.GetDataBase() == b.GetDataBase();
}
}	// anonymous namespace

bool KFCChangeAll::IsRunning()
{
	return gRunning;
}

const char* KFCChangeAll::CommandName()
{
	// One name (F18): a document's Change All is InDesign's own dialog's, so this command is the book's alone.
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
			const IFindChangeService::FindChangeResult result = cmdData->GetFindChangeResult();
			ok = (result != IFindChangeService::kFailure);
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
		outSummary.Append("Change All: the Object and Colour tabs are not supported.");
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
	if (!KFCSearchEngine::ResolveRunScope(scope, outSummary))
		return 0;
	// THE TAB AND THE CHANGE SIDE, STATED BEFORE THE SEQUENCE (outside any command sequence - CommitSearchMode's rule):
	// what the command writes is what the dialog shows.
	if (!KFCSearchEngine::CommitSearchMode())
	{
		outSummary.Append("Change All: the Find/Change tab could not be stated - nothing was changed.");
		return 0;
	}
	if (!KFCSearchEngine::CommitReplaceSide())
	{
		outSummary.Append("Change All: the Change To in Find/Change could not be stated - nothing was changed.");
		return 0;
	}

	// ===== THE CHAPTERS, OPENED AND HELD FOR THE WHOLE RUN, as the query run holds them: an open inside the sequence
	// throws the undo history of what was written away (KFCQuerySequence.h).
	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	bookName.SetTranslatable(kFalse);
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	std::vector<KFCBookScope::ChapterDoc> listed;
	if (!KFCBookScope::ListBookChapters(listed, bookName) || listed.empty())
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
	std::vector<UIDRef> touched;
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
			int32 count = 0;
			if (!WriteDocument(targets[d].docRef, static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope), scopeOptions,
					count))
			{
				failed = true;
				why = "InDesign's Change All failed in ";
				why.Append(targets[d].shortName);
				break;
			}
			if (count > 0)
			{
				replaced += count;
				touched.push_back(targets[d].docRef);
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
		bool wrote = false;
		for (size_t k = 0; k < touched.size() && !wrote; ++k)
			wrote = SameDoc(touched[k], targets[i].docRef);
		if (wrote)
		{
			if (KFCBookScope::IsHeldDoc(targets[i].docRef))
				KFCBookScope::ShowChapterWindow(targets[i].docRef);
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

	// ===== THE MESSAGE (the spec's section 4).
	if (replaced == 0)
		outSummary.Append("No match - nothing was changed.");
	else
	{
		outSummary.AppendNumber(replaced);
		outSummary.Append(" replaced in ");
		outSummary.AppendNumber(static_cast<int32>(touched.size()));
		outSummary.Append(" chapter(s). Ctrl+Z undoes all of them.");
	}
	KFCBookScope::AppendUnopenableNote(outSummary, unopenable);
	KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
	KFCResultModel::NoteChangeAllWrote(replaced > 0);		// the panel's pencil cat (KFCPanelIcon)
	return replaced;
}

// End, KFCChangeAll.cpp.
