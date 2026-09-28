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
#include "KBSBookPanelPlacement.h"	// "Remember Book Panel Placement" and the placement it keeps

// MoveFileEx - the side file put in place (KBSWriteWholeFile). KBS is Windows alone (2026-09-28). After
// the SDK headers, so its macros cannot collide with SDK names (as KBSPanelAlpha.cpp).
#include <windows.h>

// The file name, in the roaming preferences folder itself.
static const char* const kKBSPanelStateFileName = "KBSPanelState.json";
// The side file every write goes to first (KBSWriteWholeFile), next to it.
static const char* const kKBSPanelStateSideFileName = "KBSPanelState.json.tmp";

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

// Find "key" in text and read the integer after the first ':' that follows it. false - out left
// alone - when the key is not there or what follows is not a number: the book panel's placement is
// four of these, and a placement with one of them missing is no placement at all (the caller asks for
// all four before using any).
static bool KBSJsonReadInt(const std::string& text, const char* key, int32& out)
{
	std::string needle("\"");
	needle += key;
	needle += "\"";

	const size_t k = text.find(needle);
	if (k == std::string::npos)
		return false;
	const size_t colon = text.find(':', k + needle.size());
	if (colon == std::string::npos)
		return false;

	size_t p = colon + 1;
	while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n' || text[p] == '\r'))
		++p;

	bool negative = false;
	if (p < text.size() && text[p] == '-')
	{
		negative = true;
		++p;
	}

	// At most nine digits: every coordinate a screen can have fits, and int32 cannot overflow on the
	// way (a tenth digit would be a file this plug-in did not write).
	int32 value = 0;
	int32 digits = 0;
	while (p < text.size() && text[p] >= '0' && text[p] <= '9')
	{
		if (++digits > 9)
			return false;
		value = value * 10 + (text[p] - '0');
		++p;
	}
	if (digits == 0)
		return false;

	out = negative ? -value : value;
	return true;
}

//----------------------------------------------------------------------------------------
// Rewriting some keys and keeping the rest (KBSPanelStateWriteKeys)
//
//   The book panel's placement and its toggle are written WITHOUT "Save Panel Settings" (the user's
//   rules, 2026-09-25 - see KBSPanelState.h), and writing them must not also write the other
//   settings as they happen to stand on the flyout right now: those are saved when the user asks,
//   and a half-way change they have not saved must not be saved behind their back.
//   So the file is READ, the named keys are replaced (or added), and it is written back. The reader
//   is a strict one for the only shape this plug-in writes - one flat object of "key": value pairs.
//   A file it refuses is not guessed at: only the pairs that stand complete in it are kept
//   (KBSJsonSalvagePairs, 2026-09-28 - until then such a file was left alone and not written at all).
//----------------------------------------------------------------------------------------

typedef std::vector<std::pair<std::string, std::string> > KBSJsonPairs;	// key, raw value

static void KBSJsonSkipSpace(const std::string& text, size_t& p)
{
	while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n' || text[p] == '\r'))
		++p;
}

// A quoted string starting at text[p] (which is the opening quote): the text between the quotes goes
// to out, RAW (escapes kept as written), and p ends past the closing quote. false when it never
// closes.
static bool KBSJsonScanQuoted(const std::string& text, size_t& p, std::string& out)
{
	if (p >= text.size() || text[p] != '"')
		return false;
	const size_t start = ++p;
	while (p < text.size())
	{
		if (text[p] == '\\')
		{
			p += 2;		// the escaped character, whatever it is, cannot close the string
			continue;
		}
		if (text[p] == '"')
		{
			out = text.substr(start, p - start);
			++p;
			return true;
		}
		++p;
	}
	return false;
}

// Read the flat object. false for anything that is not exactly { "key": value, ... } with scalar
// values - a nested object or an array is not something this plug-in ever wrote.
static bool KBSJsonParseFlat(const std::string& text, KBSJsonPairs& out)
{
	out.clear();
	size_t p = 0;
	KBSJsonSkipSpace(text, p);
	if (p >= text.size() || text[p] != '{')
		return false;
	++p;
	KBSJsonSkipSpace(text, p);

	if (p < text.size() && text[p] == '}')
	{
		++p;	// {} - an empty object is a valid (if useless) file
	}
	else
	{
		for (;;)
		{
			KBSJsonSkipSpace(text, p);
			std::string key;
			if (!KBSJsonScanQuoted(text, p, key))
				return false;

			KBSJsonSkipSpace(text, p);
			if (p >= text.size() || text[p] != ':')
				return false;
			++p;
			KBSJsonSkipSpace(text, p);

			std::string value;
			if (p < text.size() && text[p] == '"')
			{
				std::string inner;
				if (!KBSJsonScanQuoted(text, p, inner))
					return false;
				value = "\"" + inner + "\"";
			}
			else
			{
				const size_t start = p;
				while (p < text.size() && text[p] != ',' && text[p] != '}' &&
					   text[p] != ' ' && text[p] != '\t' && text[p] != '\n' && text[p] != '\r')
				{
					if (text[p] == '{' || text[p] == '[')
						return false;
					++p;
				}
				value = text.substr(start, p - start);
				if (value.empty())
					return false;
			}
			out.push_back(std::make_pair(key, value));

			KBSJsonSkipSpace(text, p);
			if (p < text.size() && text[p] == ',')
			{
				++p;
				continue;
			}
			if (p < text.size() && text[p] == '}')
			{
				++p;
				break;
			}
			return false;
		}
	}

	// Nothing may follow the object but white space.
	KBSJsonSkipSpace(text, p);
	return p == text.size();
}

// true, false, or a whole number of at most nine digits (KBSJsonReadInt's limit) - the only bare
// values this plug-in ever writes.
static bool KBSJsonIsBareScalar(const std::string& v)
{
	if (v == "true" || v == "false")
		return true;
	size_t i = (!v.empty() && v[0] == '-') ? 1 : 0;
	const size_t digits = v.size() - i;
	if (digits == 0 || digits > 9)
		return false;
	for (; i < v.size(); ++i)
		if (v[i] < '0' || v[i] > '9')
			return false;
	return true;
}

// ***** A FILE THAT DOES NOT READ AS THE FLAT OBJECT IS REPAIRED, NOT LEFT (2026-09-28, the user's
// call: "if it is set to remember, fix what is broken and remember"). ***** Until then such a file was
// never written again, so a file cut short by a crash stopped the book panel's placement from being
// kept at all - with no word unless the toggle was flipped. Now every "key": value pair that stands
// COMPLETE is kept, and the rest is dropped:
//   - the key's quotes are closed and a ':' follows it;
//   - the value is true, false, a whole number, or a closed string;
//   - a ',', a '}' or a line end follows the value (spaces between allowed). This plug-in ends every
//     value that way, so a value running into the end of the text is a write cut short - "12" may
//     have been "123" - and is not kept.
// A key seen twice keeps its last value. out may come back empty (an empty or all-garbage file).
static void KBSJsonSalvagePairs(const std::string& text, KBSJsonPairs& out)
{
	out.clear();
	size_t p = 0;
	while (p < text.size())
	{
		const size_t q = text.find('"', p);
		if (q == std::string::npos)
			break;
		size_t r = q;
		std::string key;
		if (!KBSJsonScanQuoted(text, r, key))
			break;			// a quote never closed runs to the end of the text
		size_t s = r;
		KBSJsonSkipSpace(text, s);
		if (s >= text.size() || text[s] != ':')
		{
			p = r;			// a string that is not a key (a value, or garbage) - look on from after it
			continue;
		}
		++s;
		KBSJsonSkipSpace(text, s);

		std::string value;
		size_t e = s;
		if (e < text.size() && text[e] == '"')
		{
			std::string inner;
			if (!KBSJsonScanQuoted(text, e, inner))
				break;
			value = "\"" + inner + "\"";
		}
		else
		{
			while (e < text.size() && text[e] != ',' && text[e] != '}' && text[e] != '\n' && text[e] != '\r'
				&& text[e] != ' ' && text[e] != '\t')
				++e;
			value = text.substr(s, e - s);
		}
		p = e;

		// what ends the value: a ',', a '}' or a line end, after spaces or tabs
		size_t t = e;
		while (t < text.size() && (text[t] == ' ' || text[t] == '\t'))
			++t;
		const bool ended = t < text.size() && (text[t] == ',' || text[t] == '}' || text[t] == '\n' || text[t] == '\r');
		if (!ended || value.empty() || (value[0] != '"' && !KBSJsonIsBareScalar(value)))
			continue;

		bool replaced = false;
		for (size_t i = 0; i < out.size() && !replaced; ++i)
		{
			if (out[i].first == key)
			{
				out[i].second = value;
				replaced = true;
			}
		}
		if (!replaced)
			out.push_back(std::make_pair(key, value));
	}
}

// The whole file, or false when it could not be read in full (see the read in
// KBSLoadPanelStateIfPresent for why a partial read is never used).
static bool KBSReadWholeFile(const IDFile& file, std::string& out)
{
	out.clear();
	FILE* fp = FileUtils::OpenFile(file, "rb");
	if (fp == nil)
		return false;
	char buf[1024];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
		out.append(buf, n);
	const bool readFailed = (ferror(fp) != 0);
	fclose(fp);
	return !readFailed;
}

// Write text as the whole file. The byte count AND fclose are checked: a full disk must not be
// reported as saved. nil when written, otherwise the reason.
// ***** THROUGH A SIDE FILE, NEVER IN PLACE (2026-09-28, the user's call). ***** Opening the file itself
// with "wb" empties it first, so InDesign going down between that and the last byte left an empty or
// half-written file - and the book panel's placement then stopped being written at all (the strict
// reader below refused the file) without a word. The text goes to KBSPanelState.json.tmp first, and only
// a side file written in full is moved over the real one: the real file is always the old one whole or
// the new one whole. A side file left behind by a crash is simply written over by the next write.
// The move is Win32's MoveFileEx - KBS is Windows alone (the user's call, 2026-09-28) - told to replace
// the file that is there and to return only once the move is on the disk. (FileUtils::SwapFiles, the
// SDK's "moves file1 to file2" (FileUtils.h:132), has no caller in the SDK and does not say whether it
// replaces a file that is already there.)
static const char* KBSWriteWholeFile(const IDFile& file, const std::string& text)
{
	IDFile side;
	if (!FileUtils::GetAppRoamingDataFolder(&side, PMString(kKBSPanelStateSideFileName)))
		return "folder";
	FILE* fp = FileUtils::OpenFile(side, "wb");
	if (fp == nil)
		return "open";
	const size_t wrote = fwrite(text.data(), 1, text.size(), fp);
	const int closed = fclose(fp);
	if (wrote != text.size() || closed != 0)
		return "write";
	IDFile target(file);	// GrabTString is not const
	if (!::MoveFileEx(side.GrabTString(), target.GrabTString(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		return "replace";
	return nil;
}

const char* KBSPanelStateWriteKeys(const KBSJsonPairs& keyValues, bool* outRepaired)
{
	if (outRepaired != nil)
		*outRepaired = false;
	IDFile file;
	if (!KBSPanelStateFile(file))
		return "folder";

	KBSJsonPairs pairs;
	if (FileUtils::DoesFileExist(file))
	{
		std::string text;
		if (!KBSReadWholeFile(file, text))
			return "read";
		if (!KBSJsonParseFlat(text, pairs))
		{
			// A broken file is repaired with what can be read of it (KBSJsonSalvagePairs) - it was left
			// alone, and the write refused, until 2026-09-28.
			KBSJsonSalvagePairs(text, pairs);
			if (outRepaired != nil)
				*outRepaired = true;
		}
	}
	// No file yet - or a repaired one that lost it: the same "version" line "Save Panel Settings" opens with.
	bool haveVersion = false;
	for (size_t i = 0; i < pairs.size() && !haveVersion; ++i)
		haveVersion = (pairs[i].first == "version");
	if (!haveVersion)
		pairs.insert(pairs.begin(), std::make_pair(std::string("version"), std::string("1")));

	for (size_t u = 0; u < keyValues.size(); ++u)
	{
		bool replaced = false;
		for (size_t i = 0; i < pairs.size(); ++i)
		{
			if (pairs[i].first == keyValues[u].first)
			{
				pairs[i].second = keyValues[u].second;
				replaced = true;
				break;
			}
		}
		if (!replaced)
			pairs.push_back(keyValues[u]);
	}

	// Written back in the layout "Save Panel Settings" uses, so the file reads the same whichever of
	// the two wrote it last.
	std::string json("{\n");
	for (size_t i = 0; i < pairs.size(); ++i)
	{
		json += "  \"";
		json += pairs[i].first;
		json += "\": ";
		json += pairs[i].second;
		json += (i + 1 < pairs.size()) ? ",\n" : "\n";
	}
	json += "}\n";

	return KBSWriteWholeFile(file, json);
}

bool KBSPanelStateFilePath(PMString& outPath)
{
	IDFile file;
	if (!KBSPanelStateFile(file))
		return false;
	// The same call KBSSavePanelState uses for its status line, so the two cannot show different paths.
	outPath = FileUtils::SysFileToPMString(file);
	return true;
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
	json += "  \"hidePreviousChapter\": ";    json += KBSBoolLiteral(KBSJump::IsHidePreviousChapterOn());
	// "Remember Book Panel Placement" and the placement (2026-09-25). Its keys are named in
	// KBSBookPanelPlacement.cpp and nowhere else: this only writes out what that file hands over -
	// the book panel as it stands now if one is open, otherwise the placement last known.
	std::vector<std::pair<std::string, std::string> > bookPanelKeys;
	KBSBookPanelPlacement::AppendSaveKeys(bookPanelKeys);
	for (size_t i = 0; i < bookPanelKeys.size(); ++i)
	{
		json += ",\n  \"";
		json += bookPanelKeys[i].first;
		json += "\": ";
		json += bookPanelKeys[i].second;
	}
	json += "\n";
	json += "}\n";

	// *Both the byte count and fclose are checked (KESCM's 2026-07-25 audit): a partial write on a
	// full disk must not be reported as a save, with a path that suggests the settings are safe. And
	// through the side file since 2026-09-28 - see KBSWriteWholeFile.
	const char* failure = KBSWriteWholeFile(file, json);
	if (failure != nil)
	{
		std::string say("Save failed (");
		say += failure;
		say += ").";
		KBSPanelStateSayFailed(say.c_str());
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
// Restore (once per session - from KBSStartupShutdown::Startup or KBSBookPanelPlacement::Start,
// whichever comes first; see KBSPanelState.h)
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

	// *No window is touched here. What actually puts the alpha on is the panel's AutoAttach and the
	//  palette-visibility observer (KBSPanelAlpha.cpp).
	//  *It is not merely "restore a flag", though: restoring ON makes KBSSetPanelTranslucent put up
	//   the Win32 event hook. With no panel yet the callback returns immediately.
	//  !This said "none could be: this runs at startup, before there is a panel" until 2026-09-25.
	//   That was never measured - the startup service is a LAZY one - and since that day this can
	//   also run from the palette manager's PaletteMgrStarted, after the palettes are laid out. If
	//   the panel is already up when the flag is restored, the alpha goes on at the next event the
	//   hook or the visibility observer sees, not here.
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

	// "Remember Book Panel Placement" (2026-09-25): the toggle and the placement. Read by
	// KBSBookPanelPlacement, which owns the keys; nothing is moved here - what puts the placement on
	// a book panel is that file, when one appears.
	KBSBookPanelPlacement::LoadFromSettings(text);
}

//----------------------------------------------------------------------------------------
// The readers, for KBSBookPanelPlacement (which owns its keys but not the JSON handling)
//----------------------------------------------------------------------------------------

bool KBSPanelStateReadInt(const std::string& text, const char* key, int32& out)
{
	return KBSJsonReadInt(text, key, out);
}

bool KBSPanelStateReadBool(const std::string& text, const char* key, bool defVal)
{
	return KBSJsonReadBool(text, key, defVal ? kTrue : kFalse) != kFalse;
}

// End, KBSPanelState.cpp.
