//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A row operation under its own list's query - see KFCQueryScope.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommandSequence.h"		// IAbortableCmdSeq - the list's query stated, rolled back on a refusal
#include "IFindChangeOptions.h"		// kGrepSearch - PreviewHit's first question

// General includes:
#include "CmdUtils.h"				// BeginAbortableCmdSeq / EndCommandSequence / AbortCommandSequence
#include "ErrorUtils.h"				// the error a refusal's abort leaves, cleared before any command
#include <memory>					// std::make_shared - the list's copy taken again (R3)

// Project includes:
#include "KFCQueryScope.h"
#include "IKFCUIServices.h"			// QueryReadsAsTier, LiveQueryRecopied, ShowNotice - the lists' names are the UI half's
#include "KFCDiag.h"				// the QSCOPE lines, and the fault switch query-scope-refuse - test builds only
#include "KFCReplaceEngine.h"		// QueryUnchangedSinceSearch - the replace's own question; CanReplaceHit, PreviewHit
#include "KFCResultModel.h"			// the live list's query copy, walk signature, result set and tab
#include "KFCRunGuard.h"			// nothing is stated while a run of ours is under way
#include "KFCSearchEngine.h"		// BuildWalkSignature - the second check

namespace
{
	// Words that are not translated (every message of KFC is the author's English as it stands).
	void Say(PMString& out, const char* words)
	{
		out = PMString(words);
		out.SetTranslatable(kFalse);
	}

	// The panel's line, for what an operation with no words of its own cannot carry (IKFCUIServices::ShowNotice).
	void Notice(const PMString& words)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			ui->ShowNotice(words);
	}

	// "Find/Change could not be put back as it was (<what>) - check Edit > Find/Change." - the loud form, said wherever
	// the put-back fails (the v1 switch's words: the person has to look before replacing again).
	PMString NotPutBack(const PMString& what)
	{
		PMString say("Find/Change could not be put back as it was (");
		say.SetTranslatable(kFalse);
		say.Append(what);
		say.Append(") - check Edit > Find/Change before replacing.");
		return say;
	}
}	// anonymous namespace

KFCQueryScope::KFCQueryScope()
	: fStated(false), fRefused(false), fUnchanged(false), fFinished(false)
{
	fWhy.SetTranslatable(kFalse);
	fName.SetTranslatable(kFalse);
	// A RUN OF OURS UNDER WAY: nothing asked, nothing stated - the operation's own door turns it away (KFCRunGuard). A
	// command here could land in the middle of the run (its bar pumps events).
	if (KFCRunGuard::IsAnyRunning())
		return;

	// (1) DOES FIND/CHANGE HOLD THE LIST'S QUERY ALREADY - its find side, the replace's own question (the tab, stated on the
	// way; the walk signature; the Find Format - KFCReplaceEngine's CompareQueryWithSearch)? Then nothing is stated: the
	// operation runs as it always has, Change to as the dialog holds it (v2 R2), and a Replace that goes through takes the
	// list's copy again (R3 - Finish).
	if (KFCReplaceEngine::QueryUnchangedSinceSearch())
	{
		fUnchanged = true;
		return;
	}

	// (2) A LIST THAT KEPT NO QUERY (a query run's; one an Undo put back without one): nothing to state - the operation's own
	// door answers as it always has (RefuseChangedQuery: the other tab, or the query changed and the list cleared).
	const std::shared_ptr<const KFCQueryCopy> copy(KFCResultModel::GetQueryCopy());
	if (copy == nullptr || !copy->valid)
		return;

	// (3) WHAT FIND/CHANGE HOLDS, TO PUT BACK: the tab on screen, and the list's tab when that is another - stating the
	// list's query turns to its tab and writes there, over what that tab held.
	KFCQueryCopier::Capture(fBeforeNow);
	if (fBeforeNow.valid && copy->mode != fBeforeNow.mode)
		KFCQueryCopier::CaptureMode(fBeforeListTab, copy->mode);
	if (!fBeforeNow.valid || (copy->mode != fBeforeNow.mode && !fBeforeListTab.valid))
	{
		// Nothing stated yet, so nothing to put back.
		fRefused = true;
		Say(fWhy, "Nothing was done: the Find/Change settings could not be read, so this tier's query could not be put in.");
		KFC_DIAG_LOG("QSCOPE refused set=%u (settings unreadable)", KFCResultModel::GetResultSetId());
		return;
	}

	// TEST BUILDS ONLY: what stating the list's query costs (it runs before each such operation - "PHASE query-scope end
	// <ms> <InDesign's counters>", KFCDiag.h).
	KFC_DIAG_PHASE(scopePhase, "query-scope");

	// (4) THE LIST'S QUERY STATED, INSIDE AN ABORTABLE SEQUENCE, AND CHECKED THREE TIMES (the v1 switch's checks, moved here
	// whole): field by field (Matches - every field the copy's tab uses); by the walk signature the list was searched with
	// (what the walk itself goes by - the switches included); and in the words the person reads - the list's name, asked
	// of the UI half, which named it after its query when the search was made. Any check that fails refuses - aborted, so
	// InDesign rolls every tab's settings back (the spike, 10-14) - and the standing error is asked before the sequence
	// ends, as the abort's own error is cleared before any command after it.
	PMString what;
	what.SetTranslatable(kFalse);
	IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KFC tier query");
	bool good = (seq != nil);
	if (!good)
		Say(what, "a command sequence could not be begun");
	if (good)
		good = KFCQueryCopier::Apply(*copy, what);
	if (good)
		good = KFCQueryCopier::Matches(*copy, what);
	if (good)
	{
		PMString signature;
		KFCSearchEngine::BuildWalkSignature(signature);
		const PMString searched(KFCResultModel::GetWalkSignature());
		// Empty on either side is "cannot tell" - and this is the door before a write, so it fails closed.
		if (signature.IsEmpty() || searched.IsEmpty() || signature != searched)
		{
			good = false;
			Say(what, "the walk signature");
		}
	}
	if (good)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui == nil || !ui->QueryReadsAsTier(KFCResultModel::GetResultSetId(), fName))
		{
			good = false;
			if (fName.IsEmpty())
				Say(what, "no search here named this list, so its query cannot be read against its name - search it again");
			else
			{
				Say(what, "it does not read as the name ");
				what.Append(fName);
			}
		}
	}
#ifdef KFC_DIAG
	// TEST BUILDS ONLY: the fault switch query-scope-refuse (KFCDiag.h) refuses after every check passed - the refusal's
	// whole path, Find/Change put back included, tried on demand (the spec's section 5, item 7). One-shot.
	if (good && KFC_DIAG_FAULT("query-scope-refuse"))
	{
		KFCDiagFaultOff("query-scope-refuse");
		good = false;
		Say(what, "the test build's fault switch query-scope-refuse");
	}
#endif
	if (good && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		good = false;
		what = ErrorUtils::PMGetGlobalErrorString();
		what.SetTranslatable(kFalse);
	}
	if (good)
	{
		const ErrorCode ended = CmdUtils::EndCommandSequence(seq);
		seq = nil;
		if (ended != kSuccess)
		{
			good = false;
			Say(what, "the command sequence did not end");
		}
		else if (!KFCQueryCopier::Matches(*copy, what))
			good = false;
	}
	if (good)
	{
		fStated = true;
		KFC_DIAG_LOG("QSCOPE stated set=%u tab=%d over=%d", KFCResultModel::GetResultSetId(), static_cast<int>(copy->mode),
			static_cast<int>(fBeforeNow.mode));
		return;
	}

	// REFUSED: the sequence aborted (when it is still open), the error it leaves cleared before any command, and Find/Change
	// put back behind it - the abort is not trusted to have put the tab back (the v1 live test: rolledback=0, back=1).
	if (seq != nil)
	{
		CmdUtils::AbortCommandSequence(seq);
		seq = nil;
	}
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	PMString whatBack;
	const bool back = PutBack(whatBack);
	fRefused = true;
	Say(fWhy, "Nothing was done: this tier's query could not be put into Find/Change (");
	fWhy.Append(what);
	if (back)
		fWhy.Append(") - Find/Change is as you left it.");
	else
	{
		fWhy.Append("). ");
		fWhy.Append(NotPutBack(whatBack));
		// Said on the panel's line too: an operation with no words of its own (a GREP row's after-text) drops Ready's.
		Notice(fWhy);
	}
	fWhy.SetTranslatable(kFalse);
	KFC_DIAG_LOG("QSCOPE refused set=%u what=%s back=%d", KFCResultModel::GetResultSetId(), what.GetUTF8String().c_str(),
		back ? 1 : 0);
}

KFCQueryScope::~KFCQueryScope()
{
	if (!fStated || fFinished)
		return;
	// LEFT WITHOUT FINISH - an operation that returned early, or one with no words of its own (PreviewHit): put back all
	// the same, and said on the panel's line when it cannot be.
	fStated = false;
	PMString what;
	if (PutBack(what))
		return;
	KFC_DIAG_LOG("QSCOPE not put back (left early) what=%s", what.GetUTF8String().c_str());
	Notice(NotPutBack(what));
}

bool KFCQueryScope::Ready(PMString& outWhy) const
{
	if (!fRefused)
		return true;
	outWhy = fWhy;
	outWhy.SetTranslatable(kFalse);
	return false;
}

void KFCQueryScope::Finish(bool wrote, PMString& ioStatus)
{
	fFinished = true;
	if (fStated)
	{
		// THE LIST'S QUERY WAS STATED: Find/Change put back, and the words say which query the operation went by - the
		// list's name - and that Find/Change is as it was (the spec's section 2-2), or loudly that it is not.
		fStated = false;
		PMString what;
		const bool back = PutBack(what);
		if (!ioStatus.IsEmpty())
			ioStatus.Append(" ");
		if (back)
		{
			ioStatus.Append("With this tier's query: ");
			ioStatus.Append(fName);
			ioStatus.Append(" - Find/Change is as you left it.");
		}
		else
			ioStatus.Append(NotPutBack(what));
		ioStatus.SetTranslatable(kFalse);
		KFC_DIAG_LOG("QSCOPE put back=%d wrote=%d", back ? 1 : 0, wrote ? 1 : 0);
		return;
	}
	// R3 - THE LIST REMEMBERS WHAT IT LAST REPLACED WITH: a Replace that went through under Find/Change as it is (the find
	// side read as the list's) takes the list's copy again when Find/Change reads otherwise now - Change to, or another
	// change-side setting, edited since the search. A tier switched to later states that (v2 R3); the UI half names the
	// live list again from it, so its heading says it - and the third check above reads that name.
	if (!wrote || !fUnchanged)
		return;
	const std::shared_ptr<const KFCQueryCopy> copy(KFCResultModel::GetQueryCopy());
	if (copy == nullptr || !copy->valid)
		return;
	PMString differs;
	if (KFCQueryCopier::Matches(*copy, differs))
		return;
	KFCQueryCopy again;
	KFCQueryCopier::Capture(again);
	if (!again.valid)
		return;
	KFCResultModel::SetQueryCopy(std::make_shared<KFCQueryCopy>(again));
	KFC_DIAG_LOG("QSCOPE recopied set=%u (%s)", KFCResultModel::GetResultSetId(), differs.GetUTF8String().c_str());
	InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
	if (ui != nil)
		ui->LiveQueryRecopied(KFCResultModel::GetResultSetId());
}

bool KFCQueryScope::PutBack(PMString& outWhat)
{
	outWhat.Clear();
	outWhat.SetTranslatable(kFalse);
	bool back = true;
	// The list's tab first, when it is another: Apply turns to it and puts its fields back as they were...
	if (fBeforeListTab.valid)
	{
		PMString what;
		if (!KFCQueryCopier::Apply(fBeforeListTab, what) || !KFCQueryCopier::Matches(fBeforeListTab, what))
		{
			back = false;
			outWhat = what;
		}
	}
	// ...then the tab that was on screen - last, so Find/Change is left on it (and the walk's committed tab with it).
	PMString what;
	if (!KFCQueryCopier::Apply(fBeforeNow, what) || !KFCQueryCopier::Matches(fBeforeNow, what))
	{
		if (back)
			outWhat = what;
		back = false;
	}
	outWhat.SetTranslatable(kFalse);
	return back;
}

bool KFCQueryScope::PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter)
{
	outAfter.Clear();
	outAfter.SetTranslatable(kFalse);
	// The row's own questions first, with nothing stated (KFCReplaceEngine::PreviewHit asks them again): a GREP list's text
	// row that Return would write, no run of ours under way.
	if (KFCResultModel::GetSearchMode() != IFindChangeOptions::kGrepSearch
		|| KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID
		|| KFCRunGuard::IsAnyRunning() || !KFCReplaceEngine::CanReplaceHit(chapterIdx, hitIdx))
		return false;
	KFCQueryScope scope;
	PMString why;
	if (!scope.Ready(why))
		return false;
	// Put back by the scope's destructor - a preview has no words to add the scope's to (and a failure is said on the
	// panel's line there).
	return KFCReplaceEngine::PreviewHit(chapterIdx, hitIdx, outAfter);
}

// End, KFCQueryScope.cpp.
