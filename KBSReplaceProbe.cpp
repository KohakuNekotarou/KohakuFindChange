//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  TEMPORARY measurement probe - see KBSReplaceProbe.h.
//
//  What it measures, and why each one can end the experiment:
//    1. Does GetNthReplacementPosition/Len come back holding the range that was WRITTEN, or the
//       one that was registered? The panel's post-replace list reads the replaced text back from
//       that range, and a GREP change string makes its length impossible to predict, so a probe
//       that comes back unchanged means the delta bookkeeping the current engine does would have
//       to be kept.
//    2. Does a batch absorb the shift each replacement causes for the ones after it?
//    3. Does the whole batch come back on one Ctrl+Z?
//    4. Is a GREP back-reference expanded? (Read back through first="..." below.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommandSequence.h"		// IAbortableCmdSeq
#include "IFindChangeOptions.h"
#include "IReplaceAllTextData.h"	// source/open/interfaces/text - on the include path since 2026-07-31
#include "IReplaceAllTextUtil.h"
#include "ITextModel.h"

// General includes:
#include "CAlert.h"
#include "CmdUtils.h"
#include "CreateObject.h"
#include "ErrorUtils.h"
#include "PreferenceUtils.h"		// QuerySessionPreferences
#include "TextWalkerServiceProviderID.h"	// kReplaceAllTextDataBoss
#include "Utils.h"
#include "WideString.h"

#include <algorithm>
#include <map>
#include <vector>

// Project includes:
#include "KBSReplaceProbe.h"
#include "KBSBookScope.h"
#include "KBSResultModel.h"
#include "KBSSearchEngine.h"

namespace
{

// One checked row of the story under measurement.
struct ProbeHit
{
	TextIndex	start;
	TextIndex	end;

	ProbeHit() : start(kInvalidTextIndex), end(kInvalidTextIndex) {}
};

// Sort the staged positions into document order. The rows are held in PAGE order, which is not the
// same thing, and Adobe stages forwards (SpellReplaceWalker.cpp:748 - it only goes backwards when
// the search itself is running backwards).
bool EarlierInStory(const ProbeHit& a, const ProbeHit& b)
{
	return a.start < b.start;
}

// Append "[a,b,c]" - or the first five and how many were left out. Saying so matters: a silently
// truncated list reads as "that was all of them".
void AppendList(PMString& out, const std::vector<int32>& values)
{
	const size_t kShow = 5;
	out.Append("[");
	for (size_t i = 0; i < values.size() && i < kShow; ++i)
	{
		if (i > 0)
			out.Append(",");
		out.AppendNumber(values[i]);
	}
	if (values.size() > kShow)
	{
		out.Append(",...+");
		out.AppendNumber(static_cast<int32>(values.size() - kShow));
	}
	out.Append("]");
}

} // anonymous namespace

void KBSReplaceProbe::Run(PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	if (KBSResultModel::GetChapterCount() <= 0 || KBSResultModel::IsShowingReplaceOutcome())
	{
		outSummary.Append("Probe: nothing to measure - run a search first.");
		return;
	}

	// The Glyph tab is out of scope for the whole experiment: IReplaceAllTextData has no glyph
	// entry, so that route stays on kTWReplaceTextCmdBoss whatever this measures.
	const int32 mode = KBSResultModel::GetSearchMode();
	if (mode != IFindChangeOptions::kTextSearch && mode != IFindChangeOptions::kGrepSearch)
	{
		outSummary.Append("Probe: Text or GREP tab only.");
		return;
	}

	// The first chapter only. A book-wide measurement would say nothing this does not.
	const int32 chapterIdx = 0;
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file)
		|| !KBSBookScope::IsDocStillOpen(docRef))
	{
		outSummary.Append("Probe: the first chapter is not open - reopen it and search again.");
		return;
	}

	// Group the checked rows by story, then take the story with the most of them. NOT simply the
	// first story: measuring how a batch handles the shift it causes needs more than one position
	// in the same story. Ties go to the story whose first ROW comes first, which - the rows being
	// in page order - means the earlier page.
	std::map<UID, std::vector<ProbeHit> > byStory;
	std::map<UID, int32> firstRowOf;
	const int32 hitCount = KBSResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < hitCount; ++i)
	{
		bool checked = false, replaced = false, locked = false;
		if (!KBSResultModel::GetHitFlags(chapterIdx, i, checked, replaced, locked) || !checked || replaced)
			continue;

		UIDRef rowDoc;
		IDFile rowFile;
		UID story = kInvalidUID;
		ProbeHit hit;
		if (!KBSResultModel::GetHitLocation(chapterIdx, i, rowDoc, rowFile, story, hit.start, hit.end)
			|| story == kInvalidUID || hit.start == kInvalidTextIndex || hit.end <= hit.start)
			continue;

		byStory[story].push_back(hit);
		if (firstRowOf.find(story) == firstRowOf.end())
			firstRowOf[story] = i;
	}

	UID bestStory = kInvalidUID;
	size_t bestCount = 0;
	int32 bestFirstRow = 0;
	for (std::map<UID, std::vector<ProbeHit> >::const_iterator s = byStory.begin(); s != byStory.end(); ++s)
	{
		const size_t n = s->second.size();
		const int32 firstRow = firstRowOf[s->first];
		if (n > bestCount || (n == bestCount && bestStory != kInvalidUID && firstRow < bestFirstRow))
		{
			bestStory = s->first;
			bestCount = n;
			bestFirstRow = firstRow;
		}
	}
	if (bestStory == kInvalidUID)
	{
		outSummary.Append("Probe: no checked hit in the first chapter.");
		return;
	}

	std::vector<ProbeHit> hits = byStory[bestStory];
	std::sort(hits.begin(), hits.end(), EarlierInStory);

	InterfacePtr<ITextModel> model(docRef.GetDataBase(), bestStory, UseDefaultIID());
	if (model == nil)
	{
		outSummary.Append("Probe: the story could not be reached.");
		return;
	}

	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
	{
		outSummary.Append("Probe: no Find/Change options.");
		return;
	}
	const IFindChangeOptions::SearchMode searchMode = static_cast<IFindChangeOptions::SearchMode>(mode);
	const WideString replaceString = opts->GetReplaceString(searchMode);

	// It writes to the document, and it sits in a flyout that gets opened by accident. Cancel is the
	// default button for the same reason Change Checked's prompt has one.
	//
	// The SIX-argument form: ModalAlert has no title parameter at all (CAlert.h:132-138), and the
	// seven-argument overload takes a UIDRef for managed errors, not a title. Stock button strings -
	// on Windows the platform alert only supports those, whatever is passed (CAlert.h:102-114).
	{
		PMString prompt("Probe: replace ");
		prompt.SetTranslatable(kFalse);
		prompt.AppendNumber(static_cast<int32>(hits.size()));
		prompt.Append(" hit(s) in ONE story through DoReplaceAll? The panel rows are NOT updated - Ctrl+Z is the way back.");
		if (CAlert::ModalAlert(prompt, kOKString, kCancelString, kNullString,
				2 /*Cancel is the default*/, CAlert::eWarningIcon) != 1)
		{
			outSummary.Append("Probe: cancelled.");
			return;
		}
	}

	// State the tab before anything runs, exactly as the search and the replace do - a walk (and, by
	// the look of the Use* setters below, a replace-all) runs in the mode last COMMITTED, not the one
	// IFindChangeOptions reports. Outside the command sequence: it processes a command of its own,
	// which must not become part of the undo step.
	KBSSearchEngine::CommitSearchMode();

	int32 stagedOK = 0;
	int32 err = 0;
	int32 countAfter = 0;
	std::vector<int32> posB, posA, lenA;

	{
		// One sequence, so Ctrl+Z can be measured. Abortable to match what the real engine uses.
		IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KBS Probe");

		InterfacePtr<IPMUnknown> dataBoss(::CreateObject(kReplaceAllTextDataBoss, IID_IUNKNOWN));
		InterfacePtr<IReplaceAllTextData> data(dataBoss, IID_IREPLACEALLTEXTDATA);
		if (data != nil)
		{
			data->Clear();
			data->SetTextModel(model);
			data->UseReplacementString(boost::shared_ptr<WideString>(new WideString(replaceString)));
			data->UseGrep(searchMode == IFindChangeOptions::kGrepSearch ? kTrue : kFalse);
			data->UseSearchMode(searchMode);
			data->UseCaseSensitivity(opts->GetCaseSensitive(searchMode));

			for (size_t k = 0; k < hits.size(); ++k)
			{
				if (data->SetNextReplacement(hits[k].start, static_cast<int32>(hits[k].end - hits[k].start)))
					++stagedOK;
			}

			// What the data holds BEFORE anything is written. Half of measurement 1.
			const int32 stagedCount = data->GetReplacementCount();
			for (int32 n = 0; n < stagedCount; ++n)
				posB.push_back(static_cast<int32>(data->GetNthReplacementPosition(n)));

			err = static_cast<int32>(Utils<IReplaceAllTextUtil>()->DoReplaceAll(data));

			// ...and after. If these differ from posB, the data holds the ranges that were WRITTEN.
			countAfter = data->GetReplacementCount();
			for (int32 n = 0; n < countAfter; ++n)
			{
				posA.push_back(static_cast<int32>(data->GetNthReplacementPosition(n)));
				lenA.push_back(data->GetNthReplacementLen(n));
			}
		}

		// A standing error would roll the sequence back on the way out, taking the measurement with
		// it - the same reason KBSReplaceEngine::RunWalkerCmd clears it. The CODE is already recorded
		// in err above, so nothing is lost by clearing it here.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		if (seq != nil)
			CmdUtils::EndCommandSequence(seq);
	}

	// What actually landed at the first replaced position. With a GREP change string this is the only
	// way to see whether the back-reference was expanded - and it can only be asked at all if
	// posA/lenA hold the range that was written, which is measurement 1.
	PMString firstText;
	firstText.SetTranslatable(kFalse);
	if (!posA.empty() && !lenA.empty())
	{
		PMString pre, match, post;
		KBSSearchEngine::SplitLineAroundMatch(UIDRef(docRef.GetDataBase(), bestStory),
			static_cast<TextIndex>(posA[0]), static_cast<TextIndex>(posA[0] + lenA[0]),
			pre, match, post);
		firstText = match;
		firstText.SetTranslatable(kFalse);
	}

	outSummary.Append("Probe: staged=");
	outSummary.AppendNumber(stagedOK);
	outSummary.Append("/");
	outSummary.AppendNumber(static_cast<int32>(hits.size()));
	outSummary.Append(" err=");
	outSummary.AppendNumber(err);
	outSummary.Append(" count=");
	outSummary.AppendNumber(countAfter);
	outSummary.Append(" posB=");
	AppendList(outSummary, posB);
	outSummary.Append(" posA=");
	AppendList(outSummary, posA);
	outSummary.Append(" lenA=");
	AppendList(outSummary, lenA);
	outSummary.Append(" first=\"");
	outSummary.Append(firstText);
	outSummary.Append("\"");
}

// End, KBSReplaceProbe.cpp.
