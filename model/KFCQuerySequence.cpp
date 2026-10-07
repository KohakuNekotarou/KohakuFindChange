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
#include "ISysFileData.h"
#include "IUIFlagData.h"
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope - a book or All Documents writes each document whole

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "TextWalkerServiceProviderID.h"	// kFCQueryXMLReaderCmdBoss, kClearFindChangeOptionsCmdBoss

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
#include "KFCSavedQueries.h"	// Describe - a query file's name as the dialog shows it (RunFiles)
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

// ONE SAVED QUERY INTO EDIT > FIND/CHANGE. kFCQueryXMLReaderCmdBoss (TextWalkerServiceProviderID.h:135), measured
// 2026-10-04 (docs/ai-notes/kfc-fcquery-reader-spike-2026-10-04.md): the query's own <QueryType> picks the tab and
// fills it whole - strings, switches and formats. A file that is not there answers kSuccess and changes nothing, so
// the run asks for the file first (FileUtils::DoesFileExist). False = the command could not be made or reported a
// failure; the error state is left clear.
bool LoadQuery(const IDFile& file)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFCQueryXMLReaderCmdBoss));
	InterfacePtr<ISysFileData> fileData(cmd, UseDefaultIID());
	InterfacePtr<IUIFlagData> uiFlag(cmd, UseDefaultIID());
	if (cmd == nil || fileData == nil || uiFlag == nil)
		return false;
	fileData->Set(file);
	uiFlag->Set(kSuppressUI);
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	const bool ok = (err == kSuccess && ErrorUtils::PMGetGlobalErrorCode() == kSuccess);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return ok;
}

// EDIT > FIND/CHANGE EMPTIED AFTER THE RUN (the spec's D7 - FindChangeByList.jsx empties the strings and the formats
// after every query). kClearFindChangeOptionsCmdBoss (TextWalkerServiceProviderID.h:109) - what it reaches is measured
// on the first live run (plan A, Task 9 - M2), and this is where that answer is written. Outside the run's sequence:
// a Ctrl+Z of the run must not bring the last query back.
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

// " Skipped 2 queries: file not found (A, B)." - nothing when `names` is empty.
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

bool SameDoc(const UIDRef& a, const UIDRef& b)
{
	return a.GetDataBase() == b.GetDataBase();
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
	bool anyFile = false;
	for (size_t q = 0; q < queries.size(); ++q)
		if (FileUtils::DoesFileExist(queries[q].file))
			anyFile = true;
	if (!anyFile)
	{
		outSummary.Append("None of the queries' files was found - nothing was run.");
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
	if (scope.fromBook)
	{
		std::vector<KFCBookScope::ChapterDoc> listed;
		if (!KFCBookScope::ListBookChapters(listed, bookName) || listed.empty())
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
	std::vector<PMString> skippedNoFile, skippedNothing;
	std::vector<UIDRef> touched;
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

	// ONE BAR FOR THE WHOLE RUN (2026-10-05, the deferred minor of the re-check): a query x document a step, its Cancel
	// asked between steps - one document's Change All does not stop half way (the dialog's own rule). It comes down
	// before the sequence ends: the chapters are handed back with no bar up.
	{
		const int32 units = static_cast<int32>(queries.size() * targets.size());
		KFCProgressBar runBar(KFCLoc::Text(kKFCRunQueriesStepKey, KFCJa::kRunQueriesStep), 0, units, kTrue, kTrue);
		int32 unit = 0;
		for (size_t q = 0; q < queries.size() && !cancelled && !failed; ++q)
		{
			const QueryItem& query = queries[q];
			if (!FileUtils::DoesFileExist(query.file))
			{
				skippedNoFile.push_back(query.name);
				unit += static_cast<int32>(targets.size());
				runBar.SetPosition(unit);
				continue;
			}
			if (!LoadQuery(query.file) || !KFCSearchEngine::CanSearchTab(KFCSearchEngine::CurrentSearchMode())
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
				if (runBar.WasCancelled(kFalse))
				{
					cancelled = true;
					break;
				}

				// EACH DOCUMENT WITH InDesign's OWN CHANGE ALL (F7 - docs/superpowers/specs/2026-10-06-kfc-no-track-change-all-design.md
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
				{
					bool known = false;
					for (size_t k = 0; k < touched.size() && !known; ++k)
						known = SameDoc(touched[k], targets[d].docRef);
					if (!known)
						touched.push_back(targets[d].docRef);
				}
			}
		}
	}

	// A standing error is a failure nothing reported (ReplaceChecked's rule): taken back, and said.
	if (!cancelled && !failed && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		failed = true;
		why = ErrorUtils::PMGetGlobalErrorString();
	}
	// THE MARK - inside the sequence, so the run's Undo and Redo are heard (KFCUndoFollow::MarkWrite).
	if (!cancelled && !failed)
		for (size_t t = 0; t < touched.size(); ++t)
			KFCUndoFollow::MarkWrite(touched[t].GetDataBase());
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
		bool wrote = false;
		for (size_t k = 0; k < touched.size() && !wrote; ++k)
			wrote = SameDoc(touched[k], targets[i].docRef);
		if (wrote)
		{
			if (KFCBookScope::IsHeldDoc(targets[i].docRef))
				KFCBookScope::ShowChapterWindow(targets[i].docRef);
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
	outSummary.Append(").");		// one undo step; nothing said of Ctrl+Z (spec F20)
	AppendSkipped(outSummary, "file not found", skippedNoFile);
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
