//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  Saves and restores the flyout's settings toggles as a JSON file of our own, in the user's
//  preferences folder (see KBSPanelState.h). Nothing is written into InDesign's own data.
//
//  Ported from KESCM's KESCMPanelState.cpp, including its two audit fixes: the write is checked
//  for a short count AND for fclose failing, so a full disk cannot be reported as "saved".
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "PMString.h"
#include "FileUtils.h"		// GetAppRoamingDataFolder / OpenFile / DoesFileExist / SysFileToPMString
#include "IDFile.h"

#include <string>
#include <cstdio>			// FILE / fread / fwrite / fclose

// Project includes (the state accessors of every setting saved here):
#include "KBSPanelState.h"
#include "KBSResultTree.h"		// ShowStatus - where the result, or the failure, is reported
#include "KBSPanelAlpha.h"		// the get / set of BOTH translucency toggles (panel and Find/Change)
#include "KBSJump.h"			// IsHidePreviousChapterOn / SetHidePreviousChapter

// The file name, in the roaming preferences folder itself.
static const char* const kKBSPanelStateFileName = "KBSPanelState.json";

//----------------------------------------------------------------------------------------
// Where the file goes
//----------------------------------------------------------------------------------------

// An IDFile for KBSPanelState.json in the roaming preferences folder (the one with the locale in
// its path). *No sub-folder is created: passing a FILE name as GetAppRoamingDataFolder's
// subFolderName hands back an IDFile for that file inside the folder (the SDK does this in
// SnpShareAppResources.cpp and SuppUISysFileData.cpp). InDesign has already made the parent for
// its own preferences, so there is nothing to create. kFalse when the folder cannot be had.
static bool16 KBSPanelStateFile(IDFile& outFile)
{
	return FileUtils::GetAppRoamingDataFolder(&outFile, PMString(kKBSPanelStateFileName));
}

//----------------------------------------------------------------------------------------
// A minimal JSON (written by hand, read leniently)
//   What is stored is a flat set of booleans, so this avoids the boost-backed IJsonUtils entirely.
//----------------------------------------------------------------------------------------

static const char* KBSBoolLiteral(bool16 b)
{
	return b ? "true" : "false";
}

// Find "key" in text and read the true/false after the first ':' that follows it. defVal when it
// is not there - which is what makes an OLDER file readable: a setting added later simply keeps
// its default instead of the read failing.
static bool16 KBSJsonReadBool(const std::string& text, const char* key, bool16 defVal)
{
	std::string needle("\"");
	needle += key;
	needle += "\"";

	const size_t k = text.find(needle);
	if (k == std::string::npos)
		return defVal;
	const size_t colon = text.find(':', k + needle.size());
	if (colon == std::string::npos)
		return defVal;

	size_t p = colon + 1;
	while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n' || text[p] == '\r'))
		++p;

	if (text.compare(p, 4, "true") == 0)
		return kTrue;
	if (text.compare(p, 5, "false") == 0)
		return kFalse;
	return defVal;
}

// One place for the failure messages, so every exit says something rather than failing silently.
static void KBSPanelStateSayFailed(const char* what)
{
	PMString err(what);
	err.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(err);
}

//----------------------------------------------------------------------------------------
// Save (from "Save Panel Settings" on the flyout)
//----------------------------------------------------------------------------------------

void KBSSavePanelState()
{
	IDFile file;
	if (!KBSPanelStateFile(file))
	{
		KBSPanelStateSayFailed("Save failed (folder).");
		return;
	}

	// The current settings, as JSON. "version" is written but not read: it is there so a future
	// format change has something to test, and writing it costs nothing now.
	std::string json;
	json += "{\n";
	json += "  \"version\": 1,\n";
	json += "  \"translucentPanel\": ";       json += KBSBoolLiteral(KBSGetPanelTranslucent());           json += ",\n";
	json += "  \"translucentFindChange\": ";  json += KBSBoolLiteral(KBSGetFindChangeTranslucent());      json += ",\n";
	json += "  \"hidePreviousChapter\": ";    json += KBSBoolLiteral(KBSJump::IsHidePreviousChapterOn()); json += "\n";
	json += "}\n";

	FILE* fp = FileUtils::OpenFile(file, "wb");
	if (fp == nil)
	{
		KBSPanelStateSayFailed("Save failed (open).");
		return;
	}

	// *Both the byte count and fclose are checked (KESCM's 2026-07-25 audit): a partial write on a
	// full disk must not be reported as a save, with a path that suggests the settings are safe.
	const size_t wrote = fwrite(json.data(), 1, json.size(), fp);
	const int closed = fclose(fp);
	if (wrote != json.size() || closed != 0)
	{
		KBSPanelStateSayFailed("Save failed (write).");
		return;
	}

	// The full path, so the file can be found, backed up or deleted. The panel's status line wraps,
	// so unlike KESCM's narrower one this can afford a word in front of it.
	PMString msg("Settings saved: ");
	msg.SetTranslatable(kFalse);
	msg.Append(FileUtils::SysFileToPMString(file));
	KBSResultTree::ShowStatus(msg);
}

//----------------------------------------------------------------------------------------
// Restore (from startup = KBSStartupShutdown::Startup; once per session)
//----------------------------------------------------------------------------------------

void KBSLoadPanelStateIfPresent()
{
	static bool16 sLoaded = kFalse;
	if (sLoaded)
		return;
	sLoaded = kTrue;	// tried once per session, whether or not it worked

	IDFile file;
	if (!KBSPanelStateFile(file))
		return;
	if (!FileUtils::DoesFileExist(file))
		return;		// nothing saved yet = first run. Defaults stand.

	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return;
	std::string text;
	char buf[1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		text.append(buf, n);
	fclose(fp);
	if (text.empty())
		return;

	// *No window is touched here, and none could be: this runs at startup, before there is a panel.
	//  What actually puts the alpha on is the panel's AutoAttach and the palette-visibility
	//  observer (KBSPanelAlpha.cpp).
	//  *It is not merely "restore a flag", though: restoring ON makes KBSSetPanelTranslucent put up
	//   the Win32 event hook. With no panel yet the callback returns immediately, so this has no
	//   effect on the startup sequence.
	KBSSetPanelTranslucent(KBSJsonReadBool(text, "translucentPanel", KBSGetPanelTranslucent()));

	// The same for InDesign's own Find/Change dialog. Nothing is applied here either - the dialog is
	// certainly not open at startup - and the observer on the application's window list puts the
	// alpha on the moment it is opened (KBSPanelAlpha.cpp).
	KBSSetFindChangeTranslucent(KBSJsonReadBool(text, "translucentFindChange", KBSGetFindChangeTranslucent()));

	// Hide Previous Chapter (the user's call, 2026-08-04, after the first cut left it out). Restoring
	// it is safe in a way the flag alone does not show: the jump asks ShouldHidePreviousChapter,
	// which also requires the results to have come from a BOOK, so a restored ON cannot start
	// closing documents in document scope. The menu greys the toggle out there for the same reason.
	KBSJump::SetHidePreviousChapter(KBSJsonReadBool(text, "hidePreviousChapter",
		KBSJump::IsHidePreviousChapterOn() ? kTrue : kFalse) ? true : false);
}

// End, KBSPanelState.cpp.
