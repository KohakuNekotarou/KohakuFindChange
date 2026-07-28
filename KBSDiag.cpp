//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Diagnostic channel implementation. See KBSDiag.h for what it is for and how to switch it on.
//
//  Deliberately plain C file I/O rather than the SDK's stream classes: this has to work at any
//  moment, including from inside a command sequence or while a modal progress bar is up, and it
//  must never be the thing that fails. Nothing here can throw, and every failure is silent - a
//  diagnostic that breaks the run it is diagnosing is worse than no diagnostic.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "Utils.h"

// Project includes:
#include "KBSDiag.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{

#ifdef MACINTOSH
const char kKBSDiagPathSeparator = '/';
#else
const char kKBSDiagPathSeparator = '\\';
#endif

// The temp folder, because it is writable without a prompt and its path can be resolved from
// outside the application too - which is the whole point: whoever reads the log has to be able to
// find it without asking InDesign anything.
bool DiagFolder(std::string& outFolder)
{
	const char* folder = std::getenv("TEMP");
	if (folder == nullptr || *folder == 0)
		folder = std::getenv("TMPDIR");		// the Mac spelling
	if (folder == nullptr || *folder == 0)
		return false;
	outFolder = folder;
	if (!outFolder.empty() && outFolder[outFolder.size() - 1] != kKBSDiagPathSeparator)
		outFolder += kKBSDiagPathSeparator;
	return true;
}

bool DiagPath(const char* leafName, std::string& outPath)
{
	std::string folder;
	if (!DiagFolder(folder))
		return false;
	outPath = folder + leafName;
	return true;
}

} // anonymous namespace

bool KBSDiag::IsOn()
{
	std::string marker;
	if (!DiagPath("kbs-diag.on", marker))
		return false;

	FILE* probe = std::fopen(marker.c_str(), "rb");
	if (probe == nullptr)
		return false;
	std::fclose(probe);
	return true;
}

void KBSDiag::Log(const char* line)
{
	if (line == nullptr || !IsOn())
		return;

	std::string path;
	if (!DiagPath("kbs-diag.log", path))
		return;

	// Appended, and closed again immediately: the log stays readable from outside while InDesign
	// is still running, which a held-open handle would not guarantee.
	FILE* f = std::fopen(path.c_str(), "ab");
	if (f == nullptr)
		return;
	std::fputs(line, f);
	std::fputc('\n', f);
	std::fclose(f);
}

void KBSDiag::Log(const PMString& line)
{
	if (!IsOn())
		return;
	// UTF-8 so a Japanese chapter name survives the trip out to whoever reads the file.
	const std::string utf8 = line.GetUTF8String();
	Log(utf8.c_str());
}

// End, KBSDiag.cpp.
