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
#include "ICommandSequence.h"		// IAbortableCmdSeq - one undo step across documents, its abort stated outright
#include "IDataBase.h"
#include "IDocument.h"
#include "IFindChangeOptions.h"		// kObjectSearch - an Object query
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope - each document written whole (WHAT EACH WRITE COVERS)

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "TextWalkerServiceProviderID.h"	// kClearFindChangeOptionsCmdBoss

#include <string>
#include <vector>

// Project includes:
#include "KFCQuerySequence.h"
#include "KFCBookScope.h"
#include "KFCChangeAll.h"		// WriteDocument (a query in one document), PutRunDocumentsBack, LeaveRunDocuments
#include "KFCDiag.h"
#include "KFCDiagCommands.h"	// the test build's command count (KFC_DIAG_COMMANDS)
#include "KFCDiagWrittenDocs.h"	// the test build's written-document check (nothing in a shipping one)
#include "KFCID.h"				// kKFCRunQueriesStepKey - the key the Undo step and the bar are named by
#include "KFCObjectReplace.h"	// ReplaceAllInDoc - an Object query's write, a document at a time
#include "KFCObjectSearch.h"	// HasChangeObjectFormat, the shared walker aimed back at the front
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

/** One query of a run: its name as the list shows it, and its .xml. */
struct QueryItem
{
	PMString	name;
	IDFile		file;
};

// One query file onto a run's list, named as the dialog names it (KFCSavedQueries::Describe - the file's name without
// .xml): the dialog's files (RunFiles) and the test build's (RunFromDiagSwitch). Copied in: a std::vector of IDFile
// does not link in this plug-in (KFCBookScope.cpp, KFCSearchedBookChapterFiles).
void AppendItem(std::vector<QueryItem>& queries, const IDFile& file)
{
	KFCSavedQuery described;
	KFCSavedQueries::Describe(file, described);
	QueryItem item;
	item.name = described.name;
	item.file = file;
	queries.push_back(item);
}

// EDIT > FIND/CHANGE EMPTIED AFTER THE RUN (the query-sequence spec's D7 - FindChangeByList.jsx empties the strings
// and the formats after every query). kClearFindChangeOptionsCmdBoss (TextWalkerServiceProviderID.h:109) - measured on
// a live run (docs/ai-notes/kfc-query-sequence-engine-2026-10-04.md, M2): the four strings (Text and GREP, find and
// change) come back empty, and the dialog's Query box reads [Custom]. Outside the run's sequence: a Ctrl+Z of the run
// must not bring the last query back.
void ClearFindChange()
{
	// Never processed over an error left standing (a command processed while the global error code is set is
	// InDesign's protective shutdown - CmdUtils.h): this runs once the run's sequence has ended, and what stands then
	// is the run's own, taken back and said already.
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kClearFindChangeOptionsCmdBoss));
	if (cmd != nil)
		CmdUtils::ProcessCommand(cmd);
	// Its own failure is cleared without a word: it is outside the run's undo step and touches no document - only
	// Edit > Find/Change, where a failure shows as the last query still standing - and an error left standing would
	// bring on that shutdown at the next command anyone processes.
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

// "A, B" - the queries a message names, in the run's order (the two below).
void AppendNames(PMString& s, const std::vector<PMString>& names)
{
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (i > 0)
			s.Append(", ");
		s.Append(names[i]);
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
	AppendNames(s, names);
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
	AppendNames(s, names);
	s.Append(").");
}

// WHY A QUERY IS SKIPPED, in the order the message says them - the run collects each skipped query under one of these
// and the message walks the table, so no reason is collected without its words: nothing to find (a Colour query
// included - the spec map's QRY-22 - and an Object query with no Find Object Format); a file InDesign's query reader
// could not read (it read "nothing to find" too); and an Object query on a run over part of a document and one with
// nothing to change to (O14 - docs/superpowers/specs/2026-10-09-kfc-object-search-design.md).
enum SkipReason
{
	kSkipNothingToFind,
	kSkipUnread,
	kSkipPartOfDocument,
	kSkipNoChangeTo,
	kSkipReasons			// how many there are
};
const char* const kSkipWords[] =
{
	"nothing to find",
	"the file could not be read",
	"an Object query runs on whole documents",
	"nothing to change to",
};
static_assert(sizeof(kSkipWords) / sizeof(kSkipWords[0]) == kSkipReasons, "each skip reason has its words");

// THE RUN: these queries, in this order - each one with InDesign's own Change All, a document at a time. outSummary =
// the panel's message line. Returns how many were replaced (0 for a refusal, a cancel or a failure). Its ways in are
// RunFiles (the query dialog's Run) and, in a test build, RunFromDiagSwitch - both name each query as the dialog does.
int32 Run(const std::vector<QueryItem>& queries, PMString& outSummary)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("query-run");	// (test builds: when the run is over - KFCDiagWrittenDocs.h)
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

	// ===== THE COMMIT POINT. The list as it is now and its book are copied (RunRecorder - what a take-back or an Undo
	// puts back) BEFORE DropResults lets them go.
	KFCUndoFollow::RunRecorder recorder;
	KFCBookScope::ReleaseHeldDocs(true /*close now*/);
	KFCSearchEngine::DropResults();

	// ===== THE RUN'S DOCUMENTS, OPENED AND HELD FOR THE WHOLE RUN: nothing is opened or closed inside the sequence.
	std::vector<KFCBookScope::ChapterDoc> targets;
	PMString bookName;		// ListBookChapters' to fill (it marks the name itself) - this run's words do not name it
	std::vector<KFCBookScope::SkippedChapter> unopenable;
	KFCBookScope::BookSelection selection;		// selected 0 = the whole book (Find/Change Selected Documents (Book))
	if (scope.fromBook)
	{
		std::vector<KFCBookScope::ChapterDoc> listed;
		if (!KFCBookScope::ListBookChapters(listed, bookName, &selection) || listed.empty())
		{
			recorder.RestoreBefore();		// the commit point has passed: the list goes back as it was
			outSummary.Append(KFCSearchEngine::kBookHasNoChapters);		// the door's own words (ResolveRunTarget)
			return 0;
		}
		for (size_t i = 0; i < listed.size(); ++i)
			if (KFCBookScope::OpenChapterDoc(listed[i], &unopenable))
				targets.push_back(listed[i]);
	}
	else if (scope.allDocuments)
		KFCBookScope::ListOpenDocuments(targets);		// as the search's All Documents lists them
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

	// ===== ONE SEQUENCE AROUND EVERY QUERY (one Ctrl+Z - the query-sequence spec's D5).
	std::vector<int32> perQuery(queries.size(), -1);
	std::vector<PMString> skipped[kSkipReasons];		// the queries skipped, by their reason (kSkipWords)
	bool objectRan = false;					// an Object query aimed the shared object walker at the run's documents
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
	seq->SetName(kKFCRunQueriesStepKey);

	// WHAT EACH WRITE COVERS, from the run's scope once: a book, All Documents and Search: Document write each document
	// whole; Story / To End of Story / Selection write that part of the front document (the one target).
	const bool wholeDocuments = scope.fromBook || scope.allDocuments
		|| scope.selectionScope == static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope);
	const int32 writeScope = wholeDocuments
		? static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope) : scope.selectionScope;

	// ONE BAR FOR THE WHOLE RUN: a query x document a step, its Cancel asked between steps - one document's Change All
	// does not stop half way (the dialog's own rule). It comes down before the sequence ends: the chapters are handed
	// back with no bar up.
	{
		// A query's stretch of the bar split among the documents by their sizes
		// (docs/superpowers/specs/2026-10-18-kfc-copy-results-progress-share-design.md - KFCBookScope::ProgressBases):
		// a document's step is as long as its size makes it, not one each. `unit` still counts the steps (the test
		// build's queries-cancel names one); the bar's place is Place(q, d).
		const int32 perQueryBar = static_cast<int32>(targets.size()) * 100;
		const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, perQueryBar));
		const int32 units = static_cast<int32>(queries.size()) * perQueryBar;
		auto Place = [&](size_t q, size_t d) { return static_cast<int32>(q) * perQueryBar + shares[d]; };
		KFCProgressBar runBar(kKFCRunQueriesStepKey, 0, units, kTrue, kTrue);
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
			// A QUERY SKIPPED is named under its reason, and the bar moves on past its steps (one per document).
			const auto skip = [&](SkipReason reason)
			{
				skipped[reason].push_back(query.name);
				unit += static_cast<int32>(targets.size());
				runBar.SetPosition(Place(q + 1, 0));
			};
			// Every file was there at the door; one gone since (deleted while the run went) stops the run like a failed
			// write - the whole of it taken back - by the same rule.
			if (!FileUtils::DoesFileExist(query.file))
			{
				failed = true;
				why = "the query cannot be found: ";
				why.Append(query.name);
				break;
			}
			const bool loaded = KFCSavedQueries::LoadIntoFindChange(query.file);
			const int32 mode = KFCSearchEngine::CurrentSearchMode();		// the tab the query turned Find/Change to
			if (!loaded || !KFCSearchEngine::CanSearchTab(mode) || !KFCSearchEngine::HasFindQueryNow())
			{
				skip(loaded ? kSkipNothingToFind : kSkipUnread);
				continue;
			}
			// AN OBJECT QUERY (the object search spec's O14) writes WHOLE DOCUMENTS with InDesign's object Change All:
			// the object walker has no story, and a text Selection names no page item - so on a run over part of the
			// front document it is skipped and said; and one with nothing to change to is skipped as Change All in Book
			// refuses it.
			const bool objectQuery = (mode == IFindChangeOptions::kObjectSearch);
			if (objectQuery && !wholeDocuments)
			{
				skip(kSkipPartOfDocument);
				continue;
			}
			if (objectQuery && !KFCObjectSearch::HasChangeObjectFormat())
			{
				skip(kSkipNoChangeTo);
				continue;
			}
			// The tab and the change side are stated for a text query only: an Object query's write is the service's own
			// object replace, which no text walker mode steers (CommitSearchMode turns that tab away by design - as in
			// KFCSearchEngine::SearchBook and Change All in Book).
			if (!objectQuery && !KFCSearchEngine::CommitSearchMode())
			{
				failed = true;
				why = "the Find/Change tab could not be set for ";
				why.Append(query.name);
				break;
			}
			perQuery[q] = 0;
			// THE CHANGE SIDE, STATED HERE - inside the run's sequence (the query run's one exception to CommitReplaceSide's
			// rule): a Ctrl+Z of the run takes the whole of it back, the stated side included.
			if (!objectQuery && !KFCSearchEngine::CommitReplaceSide())
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
				runBar.SetPosition(Place(q, d));
				++unit;
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

				// A DOCUMENT GONE SINCE THE RUN TOOK IT IS NOT WRITTEN: its docRef would be followed into a database that is no
				// longer there. InDesign can close one of the run's documents by itself - the document a placed .indd
				// keeps open behind its frame, which All Documents takes as one of the user's: seen to go a quarter of
				// a second after some writes into it (memory placed-indd-headless-document), though not after this
				// run's own - two queries' Change All with idle between left it open
				// (docs/ai-notes/kfc-fatal-recheck-2026-10-12.md); what closes it is not known, and the run's bar lets
				// idle work through (KFCRunGuard.h). Asked by comparing alone (KFCBookScope::IsDocStillOpen - nothing
				// is read through the address).
				if (!KFCBookScope::IsDocStillOpen(targets[d].docRef))
				{
					KFC_DIAG_LOG("QUERYRUN step %d: %s is no longer open - not written", unit, targets[d].shortName.GetUTF8String().c_str());
					continue;
				}

				// EACH DOCUMENT WITH InDesign's OWN CHANGE ALL (F7 and section 5 of
				// docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md): no rows collected, no
				// limit - over writeScope (asked once, above the loops).
				int32 count = 0, partial = 0;
				const bool wrote = objectQuery
					? KFCObjectReplace::ReplaceAllInDoc(targets[d].docRef, count, partial)
					: KFCChangeAll::WriteDocument(targets[d].docRef, writeScope, scopeOptions, count);
				if (objectQuery)
					objectRan = true;
				if (!wrote)
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

#ifdef KFC_DIAG
	// (Fault switch queries-error-stands, a test build's only - KFCDiag.h: an error left standing by the run's last step with
	// nothing reported - the road the line below takes back. Raised after the loop, so no command of the loop meets it.)
	if (!cancelled && !failed && KFC_DIAG_FAULT("queries-error-stands"))
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
#endif
	// A standing error is a failure nothing reported (Change All in Book's rule too, KFCChangeAll.cpp): taken back, and said.
	if (!cancelled && !failed && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
	{
		failed = true;
		why = ErrorUtils::PMGetGlobalErrorString();
	}
	// THE MARK - inside the sequence, so the run's Undo and Redo are heard (KFCUndoFollow::MarkWrite). Not on a document
	// gone since it was written (the step above says how one goes): MarkWrite reads the database it is handed.
	if (!cancelled && !failed)
		for (size_t t = 0; t < targets.size(); ++t)
			if (wroteTo[t] && KFCBookScope::IsDocStillOpen(targets[t].docRef))
				KFCUndoFollow::MarkWrite(targets[t].docRef.GetDataBase());
	if (cancelled || failed)
	{
		CmdUtils::AbortCommandSequence(seq);
		// NO ERROR LEFT STANDING PAST THE ABORT - before ClearFindChange below, which PROCESSES A COMMAND: one processed while
		// the global error code is set brings on InDesign's protective shutdown (CmdUtils.h, ProcessCommand). The run that
		// failed on a standing error (above) has taken its words already (why); every other road out of the loop cleared
		// its own. The same line follows the aborts of KFC's other writes, ahead of any command - KFCReplaceEngine
		// (a write step, the preview), KFCChangeAll, KFCQueryScope.
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	}
	else
		CmdUtils::EndCommandSequence(seq);
	seq = nil;
	// The shared walker named the run's documents: aimed back at the front (the object search spec's O7).
	if (objectRan)
		KFCObjectSearch::AimSharedWalkerAtFront();
	ClearFindChange();		// outside the sequence (the query-sequence spec's section 5)

	if (cancelled || failed)
	{
		KFCChangeAll::PutRunDocumentsBack(targets, wasModified);		// the flags the abort left up, the held chapters released
		recorder.RestoreBefore();
		if (cancelled)
			outSummary.Append("Cancelled - nothing was changed.");
		else
		{
			outSummary.Append("Stopped - nothing was changed");
			if (!why.IsEmpty())		// the reason only when it has words (Change All in Book's rule, KFCChangeAll.cpp)
			{
				outSummary.Append(": ");
				outSummary.Append(why);
			}
			outSummary.Append(".");
		}
		return 0;
	}

	// ===== AFTER THE SEQUENCE: windows for the held chapters written to, the rest handed back - Change All in Book's rule
	// (KFCChangeAll::LeaveRunDocuments - its header says why a document not written is made clean again first).
	std::vector<PMString> unclosed;
	KFCChangeAll::LeaveRunDocuments(targets, wasModified, wroteTo, unclosed);

	// ===== NO LIST (F7 - the no-track spec's section 5: the panel is left EMPTY). No header either: a book's row would
	// read "(0)" over a run that wrote hundreds. NoteRun keeps the panel's icon on "a command ran".
	KFCResultModel::Clear();
	KFCSearchEngine::ForgetSearchedFindFormat();
	KFCResultModel::NoteRun();
	KFCResultModel::NoteChangeAllWrote(replaced > 0);		// the panel's pencil cat (KFCPanelIcon): each query is a Change All
	recorder.Keep();

	// ===== THE MESSAGE (the no-track spec's section 5).
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
	outSummary.Append(".");		// one undo step; nothing said of Ctrl+Z (the no-track spec's F20)
	for (int32 r = 0; r < kSkipReasons; ++r)
		AppendSkipped(outSummary, kSkipWords[r], skipped[r]);
	KFCBookScope::AppendUnopenableNote(outSummary, unopenable);
	KFCBookScope::AppendUnclosedNote(outSummary, unclosed);
	return replaced;
}

}	// anonymous namespace

bool KFCQuerySequence::IsRunning()
{
	return gRunning;
}

int32 KFCQuerySequence::RunFiles(const std::vector<IDFile>& files, PMString& outSummary)
{
	std::vector<QueryItem> queries;
	for (size_t i = 0; i < files.size(); ++i)
		AppendItem(queries, files[i]);
	return Run(queries, outSummary);
}

#ifdef KFC_DIAG
// The test build's way in (KFCQuerySequence.h). One path per line, UTF-8; a BOM and CR LF are tolerated. Each query is
// named as the dialog names it (AppendItem).
int32 KFCQuerySequence::RunFromDiagSwitch(PMString& outSummary)
{
	std::vector<QueryItem> queries;
	std::vector<std::string> lines;
	(void)KFCDiagFaultLines("queries-run", lines);		// none when the switch is off: an empty run
	for (size_t n = 0; n < lines.size(); ++n)
	{
		const std::string& line = lines[n];
		if (line[0] == '#')		// a line starting "#" is skipped
			continue;
		PMString p;
		p.SetUTF8String(line);
		AppendItem(queries, FileUtils::PMStringToSysFile(p));
	}
	KFC_DIAG_LOG("queries-run: %d queries from the switch", (int)queries.size());
	return Run(queries, outSummary);
}
#endif

// End, KFCQuerySequence.cpp.
