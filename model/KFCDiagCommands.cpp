//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The test build's command counter - see KFCDiagCommands.h. The interceptor's shape is KIDMCP's
//  (KIDMCPCmdWatch.cpp): the session's command processor (ISession::QueryCommandProcessor), installed and taken
//  out through ICommandProcessor::InstallInterceptor / DeinstallInterceptor, the reference this file holds for
//  the processor's raw pointer, kCmdNotHandled on every path and every callback inside try/catch.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICmdHistory.h"			// the undo-watch's picture: each document's undo and redo steps
#include "ICommand.h"
#include "ICommandInterceptor.h"
#include "ICommandProcessor.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "ISession.h"

// General includes:
#include "CPMUnknown.h"
#include "CreateObject.h"
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - the undo-watch reads the main thread's state only
#include "PMString.h"
#include "UIDList.h"

// Project includes:
#include "KFCID.h"
#include "KFCBookScope.h"			// QueryOpenDocumentList - the undo-watch's documents
#include "KFCDiag.h"
#include "KFCDiagCommands.h"

#ifdef KFC_DIAG
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace
{

// What has been counted since Arm, by command class: how many, and the name of the first one seen.
struct Counted
{
	int			count;
	std::string	name;
	Counted() : count(0) {}
};
std::mutex gMutex;
std::map<ClassID, Counted> gCounts;
int gTotal = 0;
ICommandInterceptor* gInstalled = nil;		// held for the processor's raw pointer - released after DeinstallInterceptor

// The undo-watch (KFCDiagCommands.h): its own instance of the same interceptor, told apart from the counter by this
// pointer; the picture last written and a running number for its lines. Main thread only.
ICommandInterceptor* gUndoWatcher = nil;
std::string gLastPicture;
int gWatchSeq = 0;

// Every open document's undo and redo history as one line: "<name>@<database>:u<steps>[<top>]/r<steps>[<top>] ...".
// Read the way KIDMCP reads it (KIDMCPVerify.cpp ReadHistory: ICmdHistory on the session's command processor, the
// document's own UIDRef as the target) - names and counts only, nothing changed.
std::string HistoryPicture()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<ICommandProcessor> processor(session != nil ? session->QueryCommandProcessor() : nil);
	InterfacePtr<ICmdHistory> history(processor, UseDefaultIID());
	InterfacePtr<IDocumentList> docs(KFCBookScope::QueryOpenDocumentList());
	if (history == nil || docs == nil)
		return "(no history to read)";
	std::string out;
	const int32 n = docs->GetDocCount();
	for (int32 i = 0; i < n; ++i)
	{
		IDocument* const doc = docs->GetNthDoc(i);
		if (doc == nil)
			continue;
		const UIDRef target = ::GetUIDRef(doc);
		PMString docName;
		doc->GetName(docName);
		const int32 undo = history->GetUndoStepCount(target);
		const int32 redo = history->GetRedoStepCount(target);
		PMString undoTop, redoTop;
		if (undo > 0)
			history->GetNthUndoStepName(&undoTop, 0, target);
		if (redo > 0)
			history->GetNthRedoStepName(&redoTop, 0, target);
		undoTop.Translate();
		redoTop.Translate();
		char at[48] = { 0 };
		_snprintf_s(at, sizeof(at), _TRUNCATE, "@%p:u%d[", (void*)target.GetDataBase(), (int)undo);
		char mid[24] = { 0 };
		_snprintf_s(mid, sizeof(mid), _TRUNCATE, "]/r%d[", (int)redo);
		out += docName.GetUTF8String();
		out += at;
		out += undoTop.GetUTF8String();
		out += mid;
		out += redoTop.GetUTF8String();
		out += "] ";
	}
	return out;
}

// One UNDOW line for a command about to be processed - with the picture when it is not the one written last.
void NoteUndoWatch(ICommand* cmd)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	PMString name;
	cmd->GetName(&name);
	const UIDList* const items = cmd->GetItemList();
	IDataBase* const db = (items != nil) ? items->GetDataBase() : nil;
	const std::string picture = HistoryPicture();
	++gWatchSeq;
	if (picture != gLastPicture)
	{
		gLastPicture = picture;
		KFC_DIAG_LOG("UNDOW #%d %s[0x%x] undo=%d db=%p | %s", gWatchSeq, name.GetUTF8String().c_str(),
			(unsigned)::GetClass(cmd).Get(), (int)cmd->GetUndoability(), (void*)db, picture.c_str());
	}
	else
		KFC_DIAG_LOG("UNDOW #%d %s[0x%x] undo=%d db=%p", gWatchSeq, name.GetUTF8String().c_str(),
			(unsigned)::GetClass(cmd).Get(), (int)cmd->GetUndoability(), (void*)db);
}

}	// anonymous namespace
#endif

/** The interceptor: counts and passes every command on. Without KFC_DIAG it is never created (KFCDiagCommands.h). */
class KFCDiagCmdCount : public CPMUnknown<ICommandInterceptor>
{
public:
	KFCDiagCmdCount(IPMUnknown* boss) : CPMUnknown<ICommandInterceptor>(boss) {}
	virtual ~KFCDiagCmdCount() {}

	virtual InterceptResult InterceptProcessCommand(ICommand* cmd)
	{
#ifdef KFC_DIAG
		try
		{
			if (cmd != nil && gUndoWatcher != nil && static_cast<ICommandInterceptor*>(this) == gUndoWatcher)
				NoteUndoWatch(cmd);
			else if (cmd != nil)
			{
				const ClassID cls = ::GetClass(cmd);
				std::lock_guard<std::mutex> lock(gMutex);
				++gTotal;
				Counted& c = gCounts[cls];
				if (c.count++ == 0)
				{
					PMString name;
					cmd->GetName(&name);
					c.name = name.GetPlatformString();
				}
			}
		}
		catch (...)
		{
		}
#else
		(void)cmd;
#endif
		return kCmdNotHandled;
	}
	// Scheduled commands are counted when they are processed, above.
	virtual InterceptResult InterceptScheduleCommand(ICommand* /*cmd*/)		{ return kCmdNotHandled; }
	virtual InterceptResult InterceptExecuteDynamic(ICommand* /*cmd*/)		{ return kCmdNotHandled; }
	virtual InterceptResult InterceptExecuteImmediate(ICommand* /*cmd*/)	{ return kCmdNotHandled; }
	// Installed through the processor (KFCDiagCommands::Arm), not by itself - one way in, as KIDMCP's.
	virtual void InstallSelf()		{}
	virtual void DeinstallSelf()	{}
};

CREATE_PMINTERFACE(KFCDiagCmdCount, kKFCDiagCmdCountImpl)

void KFCDiagCommands::Arm()
{
#ifdef KFC_DIAG
	if (gInstalled != nil || !KFC_DIAG_FAULT("perf-commands"))
		return;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<ICommandProcessor> processor(session != nil ? session->QueryCommandProcessor() : nil);
	InterfacePtr<ICommandInterceptor> counter(
		static_cast<ICommandInterceptor*>(::CreateObject(kKFCDiagCmdCountBoss, IID_ICOMMANDINTERCEPTOR)));
	if (processor == nil || counter == nil)
	{
		KFC_DIAG_LOG("COMMANDS could not arm (processor=%p counter=%p)", (void*)processor.get(), (void*)counter.get());
		return;
	}
	{
		std::lock_guard<std::mutex> lock(gMutex);
		gCounts.clear();
		gTotal = 0;
	}
	counter->AddRef();				// the processor keeps a raw pointer: this file holds the reference for it
	gInstalled = counter;
	processor->InstallInterceptor(counter);
#endif
}

int KFCDiagCommands::Count()
{
#ifdef KFC_DIAG
	std::lock_guard<std::mutex> lock(gMutex);
	return gTotal;
#else
	return 0;
#endif
}

void KFCDiagCommands::DisarmAndLog(const char* what)
{
#ifdef KFC_DIAG
	if (gInstalled == nil)
		return;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<ICommandProcessor> processor(session != nil ? session->QueryCommandProcessor() : nil);
	if (processor != nil)
		processor->DeinstallInterceptor(gInstalled);
	gInstalled->Release();
	gInstalled = nil;

	std::vector<std::pair<int, std::string> > rows;
	int total = 0;
	{
		std::lock_guard<std::mutex> lock(gMutex);
		total = gTotal;
		for (std::map<ClassID, Counted>::const_iterator it = gCounts.begin(); it != gCounts.end(); ++it)
		{
			char cls[32] = { 0 };
			_snprintf_s(cls, sizeof(cls), _TRUNCATE, "0x%x", (unsigned)it->first.Get());
			rows.push_back(std::make_pair(it->second.count, it->second.name + "[" + cls + "]"));
		}
	}
	std::sort(rows.rbegin(), rows.rend());
	std::string line;
	for (size_t i = 0; i < rows.size() && i < 25; ++i)
	{
		char n[24] = { 0 };
		_snprintf_s(n, sizeof(n), _TRUNCATE, "=%d", rows[i].first);
		line += " ";
		line += rows[i].second;
		line += n;
	}
	KFC_DIAG_LOG("COMMANDS %s total=%d classes=%d%s", what, total, (int)rows.size(), line.c_str());
#else
	(void)what;
#endif
}

void KFCDiagCommands::WatchUndoArm()
{
#ifdef KFC_DIAG
	if (gUndoWatcher != nil || !KFC_DIAG_FAULT("undo-watch"))
		return;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<ICommandProcessor> processor(session != nil ? session->QueryCommandProcessor() : nil);
	InterfacePtr<ICommandInterceptor> watcher(
		static_cast<ICommandInterceptor*>(::CreateObject(kKFCDiagCmdCountBoss, IID_ICOMMANDINTERCEPTOR)));
	if (processor == nil || watcher == nil)
	{
		KFC_DIAG_LOG("UNDOW could not arm (processor=%p watcher=%p)", (void*)processor.get(), (void*)watcher.get());
		return;
	}
	watcher->AddRef();				// the processor keeps a raw pointer: this file holds the reference for it
	gUndoWatcher = watcher;
	gLastPicture.clear();
	gWatchSeq = 0;
	processor->InstallInterceptor(watcher);
	KFC_DIAG_LOG("UNDOW armed");
#endif
}

void KFCDiagCommands::WatchUndoDisarm()
{
#ifdef KFC_DIAG
	if (gUndoWatcher == nil)
		return;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<ICommandProcessor> processor(session != nil ? session->QueryCommandProcessor() : nil);
	if (processor != nil)
		processor->DeinstallInterceptor(gUndoWatcher);
	gUndoWatcher->Release();
	gUndoWatcher = nil;
	KFC_DIAG_LOG("UNDOW disarmed after %d command(s)", gWatchSeq);
#endif
}

void KFCDiagCommands::LogHistory(const char* where)
{
#ifdef KFC_DIAG
	if (gUndoWatcher == nil || !IDThreading::IsMainThreadDomain())
		return;
	try
	{
		gLastPicture = HistoryPicture();
		KFC_DIAG_LOG("UNDOH %s | %s", where, gLastPicture.c_str());
	}
	catch (...)
	{
	}
#else
	(void)where;
#endif
}

// End, KFCDiagCommands.cpp.
