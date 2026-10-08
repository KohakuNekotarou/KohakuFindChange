//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The query run - see KFCQuerySequence.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "ICommandSequence.h"		// IAbortableCmdSeq - the run's one undo step, and a cancel that is stated
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope - a book or All Documents writes each document whole

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "TextWalkerServiceProviderID.h"	// kClearFindChangeOptionsCmdBoss

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Project includes:
#include "KFCQuerySequence.h"
#include "KFCBookScope.h"
#include "KFCChangeAll.h"		// WriteDocument - each query a document at a time with InDesign's own Change All (F7)
#include "KFCDiag.h"
#include "KFCDiagCommands.h"	// the test build's command count (KFC_DIAG_COMMANDS)
#include "KFCID.h"				// kKFCRunQueriesStepKey
#include "KFCLoc.h"
#include "KFCProgressBar.h"		// the run's one bar - the UI half's, asked for through IKFCUIServices
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCSavedQueries.h"	// LoadIntoFindChange - each query in turn; Describe - a file's name as the dialog shows it
#include "KFCSearchEngine.h"
#include "KFCUndoFollow.h"

namespace
{

bool gRunning = false;

struct RunningFlagGuard
{
	RunningFlagGuard()	{ gRunning = true; }
	~RunningFlagGuard()	{ gRunning = false; }
};

// EDIT > FIND/CHANGE EMPTIED AFTER THE RUN (the spec's D7 - FindChangeByList.jsx empties the strings and the formats
// after every query). kClearFindChangeOptionsCmdBoss (TextWalkerServiceProviderID.h:109) - measured on a live run
// (docs/ai-notes/kfc-query-sequence-engine-2026-10-04.md, M2): the four strings (Text and GREP, find and change) come
// back empty, and the dialog's Query box reads [Custom]. Outside the run's sequence: a Ctrl+Z of the run must not
// bring the last query back.
void ClearFindChange()
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kClearFindChangeOptionsCmdBoss));
	if (cmd != nil)
		CmdUtils::ProcessCommand(cmd);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
}

// "1: 12, 2: 0, 3: -" - each query by its place, "-" for one that was skipped.
void AppendPerQuery(PMString& s, const std::vector<int32>& perQuery)
{
	for (size_t q = 0; q < perQuery.size(); ++q)
	{
		if (q > 0)
			s.Append(", ");
		s.AppendNumber(static_cast<int32>(q + 1));
		s.Append(": ");
		if (perQuery[q] < 0)
			s.Append("-");
		else
			s.AppendNumber(perQuery[q]);
	}
}

// "Nothing was run - a query in the run order cannot be found (A)." / "... 2 queries in the run order cannot be found
// (A, B)." - the refusal for a run order holding a query whose file is gone.
void AppendNotFound(PMString& s, const std::vector<PMString>& names)
{
	s.Append("Nothing was run - ");
	if (names.size() == 1)
		s.Append("a query in the run order cannot be found (");
	else
	{
		s.AppendNumber(static_cast<int32>(names.size()));
		s.Append(" queries in the run order cannot be found (");
	}
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			s.Append(", ");
		s.Append(names[i]);
	}
	s.Append(").");
}

// " Skipped 2 queries: nothing to find (A, B)." - nothing when `names` is empty.
void AppendSkipped(PMString& s, const char* why, const std::vector<PMString>& names)
{
	if (names.empty())
		return;
	s.Append(" Skipped ");
	s.AppendNumber(static_cast<int32>(names.size()));
	s.Append(names.size() == 1 ? " query: " : " queries: ");
	s.Append(why);
	s.Append(" (");
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			s.Append(", ");
		s.Append(names[i]);
	}
	s.Append(").");
}

}	// anonymous namespace

bool KFCQuerySequence::IsRunning()
{
	return gRunning;
}

int32 KFCQuerySequence::Run(const std::vector<QueryItem>& queries, PMString& outSummary)
{
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);

	// ===== THE DOORS - nothing touched before the commit point (SearchBook's rule).
	if (gRunning)
	{
		outSummary.Append("A query run is already in progress.");
		return 0;
	}
	if (KFCRunGuard::IsAnyRunning())
	{
		outSummary.Append(KFCRunGuard::BusyMessage());
		return 0;
	}
	const RunningFlagGuard runningGuard;
	KFC_DIAG_PHASE(phaseRun, "query-run");		// a test build's timer (KFCDiag.h)
	KFC_DIAG_COMMANDS(commandsRun, "query-run");	// ...and its command count (KFCDiagCommands.h)
	if (queries.empty())
	{
		outSummary.Append("No queries in the run order.");
		return 0;
	}
	// EVERY QUERY'S FILE, OR NO RUN (the author's call: when a query cannot be found, the query run is not done at all -
	// an order made in the dialog or loaded from a file alike - rather than skipping it and running the rest).
	std::vector<PMString> notFound;
	for (size_t q = 0; q < queries.size(); ++q)
		if (!FileUtils::DoesFileExist(queries[q].file))
			notFound.push_back(queries[q].name);
	if (!notFound.empty())
	{
		AppendNotFound(outSummary, notFound);
		return 0;
	}
	KFCSearchEngine::RunScope scope;
	if (!KFCSearchEngine::ResolveRunScope(scope, outSummary))
		return 0;

	// ===== THE COMMIT POINT. The list as it is now is copied for an Undo (RunRecorder) BEFORE it goes.
	KFCUndoFollow::RunRecorder recorder;
	KFCBookScope::ReleaseHeldDocs(true /*close now*/);
	KFCSearchEngine::DropResults();

	// ===== THE RUN'S DOCUMENTS, OPENED AND HELD FOR THE WHOLE RUN: nothing is opened or closed inside the sequence.
	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;
	bookName.SetTranslatable(kFalse);
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	KFCBookScope::BookSelection selection;		// selected 0 = the whole book (Find/Change Selected Documents (Book))
	if (scope.fromBook)
	{
		std::vector<KFCBookScope::ChapterDoc> listed;
		if (!KFCBookScope::ListBookChapters(listed, bookName, &selection) || listed.empty())
		{
			recorder.RestoreBefore();		// the commit point has passed: the list goes back as it was
			outSummary.Append("That book has no chapters.");
			return 0;
		}
		for (size_t i = 0; i < listed.size(); ++i)
			if (KFCBookScope::OpenChapterDoc(listed[i], &unopenable))
				targets.push_back(listed[i]);
	}
	else if (scope.allDocuments)
	{
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
	}
	else
	{
		IDocument* doc = KFCBookScope::ActiveDocument();
		if (doc != nil)
			targets.push_back(KFCBookScope::DocAsChapter(doc));
	}
	if (targets.empty())
	{
		KFCBookScope::ReleaseHeldDocs(true);
		recorder.RestoreBefore();
		outSummary.Append("No document of the run could be opened - nothing was run.");
		KFCBookScope::AppendUnopenableNote(outSummary, unopenable);
		return 0;
	}
	std::vector<bool> wasModified;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		IDataBase* const db = targets[i].docRef.GetDataBase();
		wasModified.push_back(db != nil && db->IsModified() != kFalse);
	}
	recorder.ReadStories(targets);

	// ===== ONE SEQUENCE AROUND EVERY QUERY (one Ctrl+Z - the spec's D5).
	std::vector<int32> perQuery(queries.size(), -1);
	std::vector<PMString> skippedNothing;
	std::vector<bool> wroteTo(targets.size(), false);		// by target: did any query's Change All write anything there
	int32 replaced = 0;
	bool cancelled = false, failed = false;
	PMString why;
	why.SetTranslatable(kFalse);
	IAbortableCmdSeq* seq = CmdUtils::BeginAbortableCmdSeq("KFC Run Queries");
	if (seq == nil)
	{
		KFCBookScope::ReleaseHeldDocs(true);
		recorder.RestoreBefore();
		outSummary.Append("Could not start an undoable step - nothing was run.");
		return 0;
	}
	seq->SetName(KFCLoc::Text(kKFCRunQueriesStepKey, KFCJa::kRunQueriesStep));

	// ONE BAR FOR THE WHOLE RUN: a query x document a step, its Cancel
	// asked between steps - one document's Change All does not stop half way (the dialog's own rule). It comes down
	// before the sequence ends: the chapters are handed back with no bar up.
	{
		const int32 units = static_cast<int32>(queries.size() * targets.size());
		KFCProgressBar runBar(KFCLoc::Text(kKFCRunQueriesStepKey, KFCJa::kRunQueriesStep), 0, units, kTrue, kTrue);
		int32 unit = 0;
#ifdef KFC_DIAG
		// (Fault switch queries-cancel, a test build's only - KFCDiag.h: Cancel taken as pressed while step <n> was being
		// written, heard where the bar is asked next.)
		const int diagCancelStep = KFCDiagFaultValue("queries-cancel", 0, 0);
		bool diagCancelPressed = false;
#endif
		for (size_t q = 0; q < queries.size() && !cancelled && !failed; ++q)
		{
			const QueryItem& query = queries[q];
			// Every file was there at the door; one gone since (deleted while the run went) stops the run like a failed
			// write - the whole of it taken back - by the same rule.
			if (!FileUtils::DoesFileExist(query.file))
			{
				failed = true;
				why = "the query cannot be found: ";
				why.Append(query.name);
				break;
			}
			if (!KFCSavedQueries::LoadIntoFindChange(query.file) || !KFCSearchEngine::CanSearchTab(KFCSearchEngine::CurrentSearchMode())
				|| !KFCSearchEngine::HasFindQueryNow())
			{
				skippedNothing.push_back(query.name);
				unit += static_cast<int32>(targets.size());
				runBar.SetPosition(unit);
				continue;
			}
			if (!KFCSearchEngine::CommitSearchMode())
			{
				failed = true;
				why = "the Find/Change tab could not be set for ";
				why.Append(query.name);
				break;
			}
			perQuery[q] = 0;
			// THE CHANGE SIDE, STATED HERE - inside the run's sequence (the query run's one exception to CommitReplaceSide's
			// rule): a Ctrl+Z of the run takes the whole of it back, the stated side included.
			if (!KFCSearchEngine::CommitReplaceSide())
			{
				failed = true;
				why = "the Change To in Find/Change could not be stated for ";
				why.Append(query.name);
				break;
			}
			// THE QUERY'S OWN FIVE SWITCHES (footnotes, hidden layers, locked layers and stories, master pages): the query
			// just loaded set them, so they are read now - read once for the run, every query would walk with the first's.
			WalkerScopeOptions scopeOptions;
			KFCSearchEngine::GetKFCWalkerScopeOptions(scopeOptions);
			// ONE DOCUMENT AT A TIME - every one already open and held: nothing is opened or closed between the writes.
			for (size_t d = 0; d < targets.size() && !cancelled && !failed; ++d)
			{
				PMString title("Query ");
				title.SetTranslatable(kFalse);
				title.AppendNumber(static_cast<int32>(q + 1));
				title.Append(" of ");
				title.AppendNumber(static_cast<int32>(queries.size()));
				title.Append(": ");
				title.Append(query.name);
				if (targets.size() > 1)
				{
					title.Append(" - ");
					title.Append(targets[d].shortName);
				}
				runBar.SetPosition(unit++);
				runBar.SetTaskText(title);
				bool cancelPressed = runBar.WasCancelled(kFalse) != kFalse;
#ifdef KFC_DIAG
				cancelPressed = cancelPressed || diagCancelPressed;
#endif
				if (cancelPressed)
				{
					cancelled = true;
					break;
				}

				// EACH DOCUMENT WITH InDesign's OWN CHANGE ALL (F7 - docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md
				// section 5): no rows collected, no limit. A book or All Documents writes each document whole; Story / To End
				// of Story / Selection write that part of the front document (the one target).
				const int32 writeScope = (scope.fromBook || scope.allDocuments)
					? static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope) : scope.selectionScope;
				int32 count = 0;
				if (!KFCChangeAll::WriteDocument(targets[d].docRef, writeScope, scopeOptions, count))
				{
					failed = true;
					why = "InDesign's Change All failed for ";
					why.Append(query.name);
					break;
				}
				perQuery[q] += count;
				replaced += count;
				if (count > 0)
					wroteTo[d] = true;
#ifdef KFC_DIAG
				if (diagCancelStep > 0 && unit == diagCancelStep)		// unit = this step's number (1-based), counted above
				{
					diagCancelPressed = true;
					KFC_DIAG_LOG("FAULT queries-cancel: Cancel pressed in step %d", unit);
				}
#endif
			}
		}
		// ASK ONCE MORE, NOW THAT THE LOOP IS OVER. The bar is asked at the head of each step, so a Cancel pressed while the
		// LAST step was written has no next step to be heard at, and the run went through as though the button had never
		// been touched (measured - case xq-cancel-last). SearchBook's and Change All in Book's rule (KFCChangeAll.cpp).
		if (!cancelled && !failed)
		{
			bool cancelPressed = runBar.WasCancelled(kFalse) != kFalse;
#ifdef KFC_DIAG
			cancelPressed = cancelPressed || diagCancelPressed;
#endif
			if (cancelPressed)
				cancelled = true;
		}
	}

	// A standing error is a failure nothing reported (Change All in Book's rule too, KFCChangeAll.cpp): taken back, and said.
	if (!cancelled && !failed && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		failed = true;
		why = ErrorUtils::PMGetGlobalErrorString();
	}
	// THE MARK - inside the sequence, so the run's Undo and Redo are heard (KFCUndoFollow::MarkWrite).
	if (!cancelled && !failed)
		for (size_t t = 0; t < targets.size(); ++t)
			if (wroteTo[t])
				KFCUndoFollow::MarkWrite(targets[t].docRef.GetDataBase());
	if (cancelled || failed)
		CmdUtils::AbortCommandSequence(seq);
	else
		CmdUtils::EndCommandSequence(seq);
	seq = nil;
	ClearFindChange();		// outside the sequence (the spec's section 5)

	if (cancelled || failed)
	{
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
		recorder.RestoreBefore();
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

	// ===== AFTER THE SEQUENCE: windows for the held chapters written to, the rest handed back.
	std::vector<PMString> unclosed;
	for (size_t i = 0; i < targets.size(); ++i)
	{
		if (wroteTo[i])
		{
			// a window that could not be opened is said, as Change All in Book says it (case xq-qs-nowindow)
			if (KFCBookScope::IsHeldDoc(targets[i].docRef) && !KFCBookScope::ShowChapterWindow(targets[i].docRef)
				&& KFCBookScope::IsDocStillOpen(targets[i].docRef))
				unclosed.push_back(targets[i].shortName);
		}
		else if (!KFCBookScope::HandBackHeldDocNow(targets[i].docRef))
			unclosed.push_back(targets[i].shortName);
	}

	// ===== NO LIST (F7 - the spec's section 5: the panel is left EMPTY). No header either: a book's row would read "(0)"
	// over a run that wrote hundreds. NoteRun keeps the panel's icon on "a command ran".
	KFCResultModel::Clear();
	KFCSearchEngine::ForgetSearchedFindFormat();
	KFCResultModel::NoteRun();
	KFCResultModel::NoteChangeAllWrote(replaced > 0);		// the panel's pencil cat (KFCPanelIcon): each query is a Change All
	recorder.Keep();

	// ===== THE MESSAGE (the spec's section 4).
	outSummary.Append("Ran ");
	outSummary.AppendNumber(static_cast<int32>(queries.size()));
	outSummary.Append(queries.size() == 1 ? " query: " : " queries: ");
	outSummary.AppendNumber(replaced);
	outSummary.Append(" replaced (");
	AppendPerQuery(outSummary, perQuery);
	outSummary.Append(")");
	if (selection.selected > 0)
	{
		// the Book panel's selected documents alone (the query dialog's spec, section 4-4 - KFCQuerySequence.h names it):
		// "in S selected document(s) of T"
		outSummary.Append(" in ");
		outSummary.AppendNumber(selection.selected);
		outSummary.Append(" selected document(s) of ");
		outSummary.AppendNumber(selection.total);
	}
	outSummary.Append(".");		// one undo step; nothing said of Ctrl+Z (spec F20)
	AppendSkipped(outSummary, "nothing to find", skippedNothing);
	KFCBookScope::AppendUnopenableNote(outSummary, unopenable);
	KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
	return replaced;
}

int32 KFCQuerySequence::RunFiles(const std::vector<IDFile>& files, PMString& outSummary)
{
	std::vector<QueryItem> queries;
	for (size_t i = 0; i < files.size(); ++i)
	{
		KFCSavedQuery described;
		KFCSavedQueries::Describe(files[i], described);
		QueryItem item;
		item.name = described.name;
		item.file = files[i];
		queries.push_back(item);
	}
	return Run(queries, outSummary);
}

#ifdef KFC_DIAG
// The test build's way in (KFCQuerySequence.h). One path per line, UTF-8; a BOM and CR LF are tolerated. The
// query's name = the file's name without ".xml".
int32 KFCQuerySequence::RunFromDiagSwitch(PMString& outSummary)
{
	std::vector<QueryItem> queries;
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") == 0 && temp != nullptr)
	{
		const std::string path = std::string(temp) + "\\kbs-diag-fault-queries-run";
		free(temp);
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "rb") == 0 && f != nullptr)
		{
			std::string text;
			char buffer[512];
			size_t got = 0;
			while ((got = fread(buffer, 1, sizeof(buffer), f)) > 0 && text.size() < 65536)
				text.append(buffer, got);
			fclose(f);
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF)
				text.erase(0, 3);
			std::string line;
			text.push_back('\n');
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '\r')
					continue;
				if (text[i] != '\n')
				{
					line += text[i];
					continue;
				}
				if (!line.empty() && line[0] != '#')		// a line starting "#" is skipped
				{
					QueryItem item;
					PMString p;
					p.SetUTF8String(line);
					item.file = FileUtils::PMStringToSysFile(p);
					std::string base = line.substr(line.find_last_of("\\/") + 1);
					if (base.size() > 4 && base.compare(base.size() - 4, 4, ".xml") == 0)
						base.erase(base.size() - 4);
					item.name.SetUTF8String(base);
					item.name.SetTranslatable(kFalse);
					queries.push_back(item);
				}
				line.clear();
			}
		}
	}
	KFC_DIAG_LOG("queries-run: %d queries from the switch", (int)queries.size());
	return Run(queries, outSummary);
}
#endif

// End, KFCQuerySequence.cpp.
