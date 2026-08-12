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
//  ***** WHY stdio AND NOT IPMStream. ***** (Settled 2026-08-10; do not re-open without reading
//  this.) The SDK's usual way to read or write a file's bytes is
//  StreamUtil::CreateFileStreamRead / CreateFileStreamWrite -> IPMStream, used in dozens of
//  samples and in the product's own libs; SnpShareAppResources.cpp, the very snippet this file
//  cites below for WHERE the file goes, opens it that way (:182, :187). FileUtils::OpenFile is a
//  public, documented API (FileUtils.h:449-455) but has no user anywhere in the SDK.
//
//  Two of the three things guarded here move across cleanly - a short write is XferByte's return
//  value, and a truncated read is GetStreamState() == kStreamStateFailure, which is what ferror
//  is doing below. The THIRD DOES NOT: IPMStream::Close() and IPMStream::Flush() both return void
//  (IPMStream.h:321, 368), so a write that fails while being flushed - the full disk, which is
//  the case the 2026-07-25 audit added the check for - has no documented way of being noticed.
//  fclose reports it. Moving to the mainstream API would quietly weaken the one check that keeps
//  this from saying "saved" when nothing was.
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
#include "KBSFindChangeMinimize.h"	// the get / set of the minimize-box toggle
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
	json += "  \"minimizableFindChange\": ";  json += KBSBoolLiteral(KBSGetFindChangeMinimizable());      json += ",\n";
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

	// The full path and nothing else, so the file can be found, backed up or deleted (user's call
	// 2026-08-08). It said "Settings saved: " in front until then; the line is the answer to "where
	// did it go", and a path is long enough to be worth the whole width of the panel. KESCM says the
	// same thing the same way, though it arrived there for a different reason - its status line is
	// narrow enough that a label in front would have pushed the end of the path out of sight.
	PMString msg;
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
	// *A read that stopped part way through must not be applied. fread returning 0 is how BOTH the
	//  end of the file and an error look, so without ferror a truncated read is indistinguishable
	//  from a complete one - and what would then be restored is "the settings that happened to be
	//  in the part that arrived", the rest silently left at their defaults. All or nothing instead.
	//  **This is KESCM's own fix (KESCMPanelState.cpp:168-172, 2026-08-06) landing here at last:
	//    this file was ported from it on 2026-08-04, so it carried the two WRITE-side audit fixes
	//    (short count, fclose) that were already in - and not this one, which went into KESCM two
	//    days after the port. A fix made in one sibling does not walk to the other by itself.
	const bool16 readFailed = (ferror(fp) != 0);
	fclose(fp);
	if (readFailed)
		return;
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

	// The minimize box on that same dialog. Nothing is applied here either, and for the same reason:
	// the dialog is certainly not open at startup, and the window-list observer puts the style on the
	// moment it is opened (KBSPanelAlpha.cpp).
	KBSSetFindChangeMinimizable(KBSJsonReadBool(text, "minimizableFindChange", KBSGetFindChangeMinimizable()));

	// Hide Previous Chapter (the user's call, 2026-08-04, after the first cut left it out). Restoring
	// it is safe in a way the flag alone does not show: the jump asks ShouldHidePreviousChapter,
	// which also requires the results to have come from a BOOK, so a restored ON cannot start
	// closing documents in document scope. The menu greys the toggle out there for the same reason.
	// *bool16 out of the reader, bool into the setter (KBSJump.h:56,62 speak plain bool while the
	//  two toggles above are bool16) - so the conversion happens once, on its own line, instead of
	//  as a second ternary wrapped round the call.
	const bool16 hidePrev = KBSJsonReadBool(text, "hidePreviousChapter",
		KBSJump::IsHidePreviousChapterOn() ? kTrue : kFalse);
	KBSJump::SetHidePreviousChapter(hidePrev != kFalse);
}

// End, KBSPanelState.cpp.
