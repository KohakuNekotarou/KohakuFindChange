//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  TEST-BUILD TRACING, BOTH HALVES (the author's call: "keep it, behind a build flag"). One line per
//  event to %TEMP%\kbs-diag.txt, each led by the time in milliseconds.
//
//  OFF unless the build defines KFC_DIAG:
//      msbuild build\win\prj\KohakuFindChange.vcxproj /p:Configuration=Release /p:Platform=x64
//              /p:KFCExtraDefines=KFC_DIAG
//  (both projects - KohakuFindChange and KohakuFindChangeUI - pass $(KFCExtraDefines) to the compiler;
//  work\kbs-regress\cycle-build.ps1 -Diag does it).
//  In a build without it every KFC_DIAG_LOG compiles to nothing - its arguments are not evaluated either -
//  so the shipping .pln holds no call, no format string and no file name. Anything a trace needs that the
//  product does not (a helper that walks state to print it) goes inside #ifdef KFC_DIAG with it.
//
//  Why it exists: the panel's following of Undo (KFCUndoFollow) depends on notifications InDesign delivers
//  at idle, and when one does not come there is nothing on screen to say so. A trace of what arrived and
//  what was decided found a lost observer (a watched story purged from memory) in one run, after eighteen
//  runs of reading the panel alone could not.
//
//  AND FAULT SWITCHES. KFC_DIAG_FAULT("name") is true while
//  the file %TEMP%\kbs-diag-fault-<name> exists - a test creates it and deletes it again - so a test build can
//  reach a state the product only gets into after something ELSE has failed (a window that would not open).
//  Asked at each call, so the switch takes effect at once. In a build without KFC_DIAG it is the constant false;
//  every use sits inside #ifdef KFC_DIAG all the same, so no name of a switch reaches a shipping .pln.
//    keep-held   KFCBookScope::HandBackHeldDocNow keeps a held chapter instead of closing it
//                (work\kbs-regress\cases\fault-keep-held-on.jsx / -off.jsx)
//    fcmin-decoy (UI half) the file holds a window handle in hex; ui/KFCFindChangeMinimize.cpp reads it
//                itself and points its record at that window before putting the dialog's style back - a
//                handle the OS has since given to another window (work\kbs-fcmin\decoy.ps1 makes one)
//    jump-no-front (UI half) ui/KFCJump.cpp's EnsureDocFrontmost reports that the hit's window
//                could not be brought forward (work\kbs-jump\j2-select-unfronted.ps1)
//    replace-refuse KFCReplaceEngine.cpp's WalkStoryReplacing takes InDesign's replace command as having
//                refused every row - a chapter where nothing lands (work\kbs-regress\cases\fault-replace-refuse-on.jsx
//                / -off.jsx)
//    queries-run  the file holds one query path per line (UTF-8); the panel's Find runs the query run over them
//                (KFCQuerySequence::RunFromDiagSwitch) - the way in until the panel of its own exists
//                (work\kbs-regress\cases\qs-*.jsx write it, qs-off.jsx takes it off)
//    perf-fresh-walker  the file holds "<mode> <n>": WalkStoryReplacing starts its walk again after every n writes -
//                mode 2 from where the last write ended, mode 3 from the top of the story - with a new walker, client and
//                scope (2026-10-05, docs/ai-notes/kfc-speedup-ideas-2026-10-05.md: is the find's slowing down kept in the
//                walk?). Throwaway documents only.
//    perf-commands  every command InDesign processes is counted, by class, over a search, a row's Replace and a query
//                run (KFCDiagCommands.h - "COMMANDS" lines, and cmds= on WALKSTEP). The count itself costs time:
//                not on a timing run.
//    perf-backward  KFCReplaceEngine.cpp's WriteBackward answers yes: every write walks backward (the same study:
//                what the direction does to the find's time). Throwaway documents only.
//    tree-expand  (UI half) the file holds "<mode>": KFCResultTree::Rebuild opens the rows another way - mode 1 one
//                ExpandNode with all its descendants per open document row, mode 2 every row opened before ChangeRoot
//                (the same note: what opening the story rows one at a time costs). Throwaway documents only.
//    changeall-cancel  KFCChangeAll::Run takes Cancel as pressed after its first chapter - every chapter must come
//                back as it was (case all-cancel-book, ca-cases.tsv). Throwaway documents only.
//
//  AND TIMERS (2026-10-05): KFC_CLOCK / KFC_SPENT add up the milliseconds a stretch of code takes, and KFCDiagPhase
//  writes "PHASE <name> begin" and "PHASE <name> end <ms>" around a scope - all of it nothing without KFC_DIAG.
//
//========================================================================================

#ifndef __KFCDiag_h__
#define __KFCDiag_h__

#ifdef KFC_DIAG

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <mutex>
#include <thread>
#include "PerformanceStats.h"			// InDesign's own counters (KFCDiagPerf) - guide vol2-16
#include "PerformanceMetricsID.h"

// Appends one line: "<ms since the epoch> <the formatted text>". Opened and closed per line, so a crash
// right after it still leaves the line on disk.
//
// ONE WRITER AT A TIME (2026-10-05 - guide vol1-07: a file a plug-in writes is synchronised like a global). The model
// half's code is called on a background task's thread too (an export's - KFCUndoFollow writes its line before its own
// main-thread gate), and fopen_s opens for this caller alone: a second append while the file is open fails, and its
// line was lost without a word. The lock puts this half's threads in a queue; the other half (the UI plug-in, main
// thread only) can still meet a background write, so a refused open is tried again a few times before the line goes.
inline void KFCDiagLog(const char* fmt, ...)
{
	static std::mutex writer;
	const std::lock_guard<std::mutex> lock(writer);
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag.txt", temp);
	free(temp);
	FILE* f = nullptr;
	for (int attempt = 0; attempt < 20; ++attempt)
	{
		if (fopen_s(&f, path, "a") == 0 && f != nullptr)
			break;
		f = nullptr;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (f == nullptr)
		return;
	const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	fprintf(f, "%lld ", ms);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}

#define KFC_DIAG_LOG(...) KFCDiagLog(__VA_ARGS__)

// Is the fault switch <name> on - does %TEMP%\kbs-diag-fault-<name> exist? (See the header.)
inline bool KFCDiagFault(const char* name)
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return false;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag-fault-%s", temp, name);
	free(temp);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "r") != 0 || f == nullptr)
		return false;
	fclose(f);
	return true;
}

#define KFC_DIAG_FAULT(name) KFCDiagFault(name)

// The nth whole number (from 0) the fault switch <name>'s file holds, separated by white space - `fallback` when the
// switch is off, or holds fewer numbers, or one that is not a number.
inline int KFCDiagFaultValue(const char* name, int nth, int fallback)
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return fallback;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag-fault-%s", temp, name);
	free(temp);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "r") != 0 || f == nullptr)
		return fallback;
	int value = fallback;
	for (int i = 0; i <= nth; ++i)
	{
		int read = 0;
		if (fscanf_s(f, "%d", &read) != 1)
		{
			value = fallback;
			break;
		}
		value = read;
	}
	fclose(f);
	return value;
}

// Milliseconds on a clock that only goes forward - for the timers below.
inline double KFCDiagNowMs()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

#define KFC_CLOCK(var) const double var = KFCDiagNowMs()
#define KFC_SPENT(slot, var) ((slot) += KFCDiagNowMs() - (var))

// WHAT A STRETCH OF KFC MADE INDESIGN DO (2026-10-05, the speed-up study - guide vol2-16, Performance Metrics API):
// InDesign's own counters (PerformanceMetricsID.h), read with PerformanceStats::GetValue when this is made and again
// when Since is asked - composition in the layout and in galley (count, time), the change manager's Update calls (the
// observers notified: count, time), undo snapshots (count, time to make), new UIDs and instantiations, and drawing.
// Since writes "lcomp=<n>/<t> gcomp=<n>/<t> notify=<n>/<t> snap=<n>/<t> uid=<n> inst=<n> draw=<t>": the change, in
// whatever unit each counter keeps.
class KFCDiagPerf
{
public:
	KFCDiagPerf() { Take(); }
	void Take()
	{
		for (int i = 0; i < kCount; ++i)
			fV[i] = PerformanceStats::GetValue(Id(i));
	}
	void Since(char* buf, size_t n) const
	{
		unsigned long long d[kCount];
		for (int i = 0; i < kCount; ++i)
			d[i] = (unsigned long long)(PerformanceStats::GetValue(Id(i)) - fV[i]);
		_snprintf_s(buf, n, _TRUNCATE, "lcomp=%llu/%llu gcomp=%llu/%llu notify=%llu/%llu snap=%llu/%llu uid=%llu inst=%llu draw=%llu",
			d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], d[8], d[9], d[10]);
	}
private:
	enum { kCount = 11 };
	static PerformanceMetricID Id(int i)
	{
		switch (i)
		{
			case 0: return kLayoutCompositionCountPerfID;
			case 1: return kLayoutCompositionTimePerfID;
			case 2: return kGalleyCompositionCountPerfID;
			case 3: return kGalleyCompositionTimePerfID;
			case 4: return kChangeMgrUpdateCallCountPerfID;
			case 5: return kChangeMgrUpdateCallTimePerfID;
			case 6: return kSnapshotCountPerfID;
			case 7: return kNewSnapshotTimePerfID;
			case 8: return kDBNewUIDCountPerfID;
			case 9: return kDBInstantiateCountPerfID;
			default: return kDrawMgrDrawTimePerfID;
		}
	}
	uint64	fV[kCount];
};

// "PHASE <name> begin" now and "PHASE <name> end <ms> <InDesign's counters over it>" when the scope ends.
class KFCDiagPhase
{
public:
	explicit KFCDiagPhase(const char* name) : fName(name), fStart(KFCDiagNowMs())
	{
		KFCDiagLog("PHASE %s begin", fName);
	}
	~KFCDiagPhase()
	{
		char perf[300] = { 0 };
		fPerf.Since(perf, sizeof(perf));
		KFCDiagLog("PHASE %s end %.0f ms %s", fName, KFCDiagNowMs() - fStart, perf);
	}
	KFCDiagPhase(const KFCDiagPhase&) = delete;
	KFCDiagPhase& operator=(const KFCDiagPhase&) = delete;
private:
	const char*	fName;
	double		fStart;
	KFCDiagPerf	fPerf;
};

#define KFC_DIAG_PHASE(var, name) const KFCDiagPhase var(name)

// Counters a test build bumps where something may run more often than it should (2026-10-05, guide vol2-15 "do not
// update the user interface from an observer"): 0 = the panel's tab name recomputed (KFCPanelTitle::Update), 1 = the
// app bar's mirror of the Find/Change field (KFCAppBarSearchEnter). One set per plug-in (both are the UI half's).
inline int& KFCDiagCounter(int which)
{
	static int counters[8] = { 0 };
	return counters[(which >= 0 && which < 8) ? which : 7];
}

#else

#define KFC_DIAG_LOG(...) ((void)0)
#define KFC_DIAG_FAULT(name) false
#define KFC_CLOCK(var) ((void)0)
#define KFC_SPENT(slot, var) ((void)0)
#define KFC_DIAG_PHASE(var, name) ((void)0)

#endif // KFC_DIAG

#endif // __KFCDiag_h__

// End, KFCDiag.h.
