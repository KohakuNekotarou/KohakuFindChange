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
#include "ICommand.h"
#include "ICommandInterceptor.h"
#include "ICommandProcessor.h"
#include "ISession.h"

// General includes:
#include "CPMUnknown.h"
#include "CreateObject.h"
#include "PMString.h"

// Project includes:
#include "KFCID.h"
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
			if (cmd != nil)
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

// End, KFCDiagCommands.cpp.
