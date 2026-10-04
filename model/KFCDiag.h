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
//                refused every row - a chapter where nothing lands after the pending changes next to its ticked
//                rows were accepted (work\kbs-regress\cases\fault-replace-refuse-on.jsx / -off.jsx)
//
//========================================================================================

#ifndef __KFCDiag_h__
#define __KFCDiag_h__

#ifdef KFC_DIAG

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>

// Appends one line: "<ms since the epoch> <the formatted text>". Opened and closed per line, so a crash
// right after it still leaves the line on disk.
inline void KFCDiagLog(const char* fmt, ...)
{
	char* temp = nullptr;
	size_t len = 0;
	if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
		return;
	char path[600] = { 0 };
	_snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\kbs-diag.txt", temp);
	free(temp);
	FILE* f = nullptr;
	if (fopen_s(&f, path, "a") != 0 || f == nullptr)
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

#else

#define KFC_DIAG_LOG(...) ((void)0)
#define KFC_DIAG_FAULT(name) false

#endif // KFC_DIAG

#endif // __KFCDiag_h__

// End, KFCDiag.h.
