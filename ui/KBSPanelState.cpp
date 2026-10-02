//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KBS)
//
//  Saves and restores the flyout's settings toggles as a JSON file of our own, in the user's
//  preferences folder (see KBSPanelState.h). Nothing is written into InDesign's own data.
//
//  Ported from KESCM's KESCMPanelState.cpp, including its two audit fixes: a short write and a write
//  that fails on the way to the disk are both caught, so a full disk cannot be reported as "saved".
//
//  ***** THE FILE IS READ AND WRITTEN THROUGH THE SDK'S FILE STREAM (2026-10-02, the user's call:
//  ***** "for the panel settings and the like, use the official one"). ***** StreamUtil::
//  CreateFileStreamRead / CreateFileStreamWrite -> IPMStream, the SDK's way to read or write a file's
//  bytes - dozens of samples and the product's own libs, and SnpShareAppResources.cpp, the very snippet
//  this file cites for WHERE the file goes, opens its file that way (:182, :187). From 2026-08-10 until
//  then this was stdio (FileUtils::OpenFile, fread / fwrite / fclose), kept for one reason:
//  IPMStream::Close() and Flush() return void (IPMStream.h), so a write that fails while being flushed -
//  the full disk, which the 2026-07-25 audit added a check for - has no documented way of being
//  noticed, and fclose reports it. That check is now made another way, which does not need Close to
//  answer: the side file is READ BACK and must come out exactly as written before it is moved over the
//  real one (KBSWriteWholeFile). A short write is XferByte's count, and a truncated read is
//  GetStreamState() == kStreamStateFailure, as before.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"		// XferByte / Flush / GetStreamState / Close - the file's bytes

// General includes:
#include "PMString.h"
#include "FileUtils.h"		// GetAppRoamingDataFolder / DoesFileExist / SysFileToPMString
#include "IDFile.h"
#include "StreamUtil.h"		// CreateFileStreamRead / CreateFileStreamWrite

#include <string>

// Project includes (the state accessors of every setting saved here):
#include "KBSPanelState.h"
#include "KBSResultTree.h"		// ShowStatus - where the result, or the failure, is reported
#include "KBSPanelAlpha.h"		// the get / set of BOTH translucency toggles (panel and Find/Change)
#include "KBSFindChangeMinimize.h"	// the get / set of the minimize-box toggle
#include "KBSAppBarSearchEnter.h"	// the get / set of the Application Bar search toggle
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
//
//   ***** WHY NOT THE SDK'S JSON CLASS. ***** (Settled 2026-10-02, the API re-audit; this said only
//   "a flat set of booleans and numbers" until then, which is a reason it is easy, not a reason not to.)
//   The official one is `class PUBLIC_DECL JSON` in public/interfaces/utils/IJsonUtils.h, a wrapper
//   around boost::property_tree; the product reads with it (linksui's ChromiumImportHelperAEMLinks.cpp,
//   read_json in a try/catch) and writes with it (publiclib's HTTPAssetLinkResourceStateUpdater.cpp,
//   addValue -> write_json). The dependency is not the obstacle: KCM measured that it compiles and
//   links with no build change (its KCMPageCheck.cpp said so until KCM 57b1278). Two things here would
//   be lost:
//     1. THE REPAIR OF A BROKEN FILE (the user's call, 2026-09-28 - KBSJsonSalvagePairs). read_json
//        throws on the whole text when any of it is broken, so a file cut short by a crash would still
//        need this hand-written reader to keep what stands complete in it - and the format would then
//        be known in two places.
//     2. THE VALUES AS THEY ARE WRITTEN. property_tree keeps every value as a string and writes it back
//        quoted - true comes out as "true" (KIDMCP's KIDMCPMcp.cpp builds its replies by hand for the
//        same reason: 1 and "1" cannot be told apart on the way out). KBSPanelStateWriteKeys promises
//        to leave every key it is not writing exactly as the file has it, and an older KBS reads a bare
//        true; both would break on the first write.
//----------------------------------------------------------------------------------------

typedef std::vector<std::pair<std::string, std::string> > KBSJsonPairs;	// key, raw value

static std::pair<std::string, std::string> KBSJsonPair(const char* key, const char* rawValue)
{
	return std::make_pair(std::string(key), std::string(rawValue));
}

static const char* KBSBoolLiteral(bool16 b)
{
	return b ? "true" : "false";
}

static void KBSJsonSkipSpace(const std::string& text, size_t& p)
{
	while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\n' || text[p] == '\r'))
		++p;
}

// Where the value of "key" begins: past the first ':' that follows the quoted key, white space
// skipped. false when the key, or a ':' after it, is not there.
static bool KBSJsonValueStart(const std::string& text, const char* key, size_t& outPos)
{
	const std::string needle = std::string("\"") + key + "\"";
	const size_t k = text.find(needle);
	if (k == std::string::npos)
		return false;
	const size_t colon = text.find(':', k + needle.size());
	if (colon == std::string::npos)
		return false;
	outPos = colon + 1;
	KBSJsonSkipSpace(text, outPos);
	return true;
}

// The true/false after "key". defVal when it is not there - which is what makes an OLDER file
// readable: a setting added later simply keeps its default instead of the read failing.
static bool16 KBSJsonReadBool(const std::string& text, const char* key, bool16 defVal)
{
	size_t p = 0;
	if (!KBSJsonValueStart(text, key, p))
		return defVal;
	if (text.compare(p, 4, "true") == 0)
		return kTrue;
	if (text.compare(p, 5, "false") == 0)
		return kFalse;
	return defVal;
}

// The integer after "key". false - out left alone - when the key is not there or what follows is not
// a number: the book panel's placement is four of these, and a placement with one of them missing is
// no placement at all (the caller asks for all four before using any). For KBSBookPanelPlacement,
// which names its keys but reads them through here.
bool KBSPanelStateReadInt(const std::string& text, const char* key, int32& out)
{
	size_t p = 0;
	if (!KBSJsonValueStart(text, key, p))
		return false;

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

// The bool reader - for KBSBookPanelPlacement and Hide Previous Chapter, which speak bool where the
// translucency and minimize toggles speak bool16.
bool KBSPanelStateReadBool(const std::string& text, const char* key, bool defVal)
{
	return KBSJsonReadBool(text, key, defVal ? kTrue : kFalse) != kFalse;
}

// The file's text from its pairs - the one layout both writers use ("Save Panel Settings" and
// KBSPanelStateWriteKeys), so the file reads the same whichever of them wrote it last.
static std::string KBSJsonFlatText(const KBSJsonPairs& pairs)
{
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
	return json;
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

// The whole file, or false when it could not be read in full. *A read that stopped part way through
// must not be used: what would then be restored is "the settings that happened to be in the part that
// arrived", the rest silently left at their defaults. All or nothing instead (KESCM's fix of 2026-08-06,
// made with fread and ferror). Through the SDK's file stream since 2026-10-02 (see the top of this file),
// the way the SDK's samples read a whole file: the size first - Seek to the end answers where it got to -
// then exactly that many bytes from the start (textimportfilter/TxtImpFilter.cpp:602-611,
// pdfvt/PDFVTUtils.cpp:141-147). The read never asks past the end of the file, so a short count, or the
// stream in kStreamStateFailure, can only be a read that broke off.
static bool KBSReadWholeFile(const IDFile& file, std::string& out)
{
	out.clear();
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamRead(file));
	if (stream == nil)
		return false;
	const int64 size = stream->Seek(0, kSeekFromEnd);
	stream->Seek(0, kSeekFromStart);
	bool ok = (size >= 0 && size <= static_cast<int64>(0x7FFFFFFF));	// XferByte counts in int32
	if (ok && size > 0)
	{
		out.resize(static_cast<size_t>(size));
		const int32 n = stream->XferByte(reinterpret_cast<uchar*>(&out[0]), static_cast<int32>(size));
		ok = (n == static_cast<int32>(size)) && (stream->GetStreamState() != kStreamStateFailure);
	}
	stream->Close();
	if (!ok)
		out.clear();
	return ok;
}

// Write text as the whole file. A full disk must not be reported as saved. nil when written, otherwise
// the reason.
// ***** THROUGH A SIDE FILE, NEVER IN PLACE (2026-09-28, the user's call). ***** Opening the file itself
// for writing empties it first, so InDesign going down between that and the last byte left an empty or
// half-written file - and the book panel's placement then stopped being written at all (the strict
// reader below refused the file) without a word. The text goes to KBSPanelState.json.tmp first, and only
// a side file written in full is moved over the real one: the real file is always the old one whole or
// the new one whole. A side file left behind by a crash is simply written over by the next write.
// ***** AND THE SIDE FILE IS READ BACK BEFORE IT IS MOVED (2026-10-02). ***** The write goes through the
// SDK's file stream (StreamUtil::CreateFileStreamWrite with kOpenOut | kOpenTrunc, as
// SnpShareAppResources.cpp:187 opens its own preferences file), whose Flush and Close return nothing
// (IPMStream.h) - so a write that fails as it is flushed, the full disk, says nothing there. What it
// leaves is a side file shorter than the text, and reading it back finds that: only a side file that
// reads back as exactly what was written is put in place. (stdio's fclose reported it until then.)
// The move is Win32's MoveFileEx - KBS is Windows alone (the user's call, 2026-09-28) - told to replace
// the file that is there and to return only once the move is on the disk.
// ***** NOT FileUtils::SwapFiles, AND NOW THAT IS MEASURED (2026-10-02). ***** The SDK's "moves file1 to
// file2" (FileUtils.h:132) does replace a file2 that is there (KT's app.ktProbe "swapfiles": file2 read
// "old-B" before and "new-B" after) - but in TWO steps: Public.dll's SwapFiles is file2.Exists() ->
// file2.Delete() -> afl::CoreFileUtils::MoveFile(file1, file2, false) (read off its machine code). A crash
// between the delete and the move leaves NO settings file, only the side file - the very gap the side
// file exists to close. MoveFileEx with MOVEFILE_REPLACE_EXISTING replaces in one step on one volume (and
// AFL's MoveFile is itself a wrapper around MoveFileExW).
static const char* KBSWriteWholeFile(const IDFile& file, const std::string& text)
{
	IDFile side;
	if (!FileUtils::GetAppRoamingDataFolder(&side, PMString(kKBSPanelStateSideFileName)))
		return "folder";
	{
		InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamWrite(side, kOpenOut | kOpenTrunc));
		if (stream == nil)
			return "open";
		const int32 size = static_cast<int32>(text.size());
		const int32 wrote = stream->XferByte(reinterpret_cast<uchar*>(const_cast<char*>(text.data())), size);
		stream->Flush();
		const bool failed = (wrote != size) || (stream->GetStreamState() == kStreamStateFailure);
		stream->Close();
		if (failed)
			return "write";
	}
	std::string readBack;
	if (!KBSReadWholeFile(side, readBack) || readBack != text)
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
		pairs.insert(pairs.begin(), KBSJsonPair("version", "1"));

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

	return KBSWriteWholeFile(file, KBSJsonFlatText(pairs));
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

	// The current settings. "version" is written but not read: it is there so a future format change
	// has something to test, and writing it costs nothing now.
	KBSJsonPairs pairs;
	pairs.push_back(KBSJsonPair("version", "1"));
	pairs.push_back(KBSJsonPair("translucentPanel",      KBSBoolLiteral(KBSGetPanelTranslucent())));
	pairs.push_back(KBSJsonPair("translucentFindChange", KBSBoolLiteral(KBSGetFindChangeTranslucent())));
	pairs.push_back(KBSJsonPair("minimizableFindChange", KBSBoolLiteral(KBSGetFindChangeMinimizable())));
	pairs.push_back(KBSJsonPair("appBarSearchEnter",     KBSBoolLiteral(KBSGetAppBarSearchEnter())));
	pairs.push_back(KBSJsonPair("hidePreviousChapter",   KBSBoolLiteral(KBSJump::IsHidePreviousChapterOn())));
	// "Remember Book Panel Placement" and the placement (2026-09-25). Its keys are named in
	// KBSBookPanelPlacement.cpp and nowhere else: this only writes out what that file hands over -
	// the book panel as it stands now if one is open, otherwise the placement last known.
	KBSBookPanelPlacement::AppendSaveKeys(pairs);

	// *A partial write on a full disk must not be reported as a save, with a path that suggests the
	// settings are safe (KESCM's 2026-07-25 audit): the byte count is checked, and since 2026-10-02 the
	// side file is read back before it is put in place (stdio's fclose was asked until then). Through
	// the side file since 2026-09-28. See KBSWriteWholeFile.
	const char* failure = KBSWriteWholeFile(file, KBSJsonFlatText(pairs));
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
// Restore (once per session - from KBSUIStartupShutdown::Startup or KBSBookPanelPlacement::Start,
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

	std::string text;
	if (!KBSReadWholeFile(file, text) || text.empty())
		return;		// all or nothing - a part-read file is not applied (see KBSReadWholeFile)

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

	// Return in the application bar's search field (2026-10-02). Setting it ON is what puts the hook on - read
	// back here at startup, on the main thread, the thread the hook watches (KBSAppBarSearchEnter.h).
	KBSSetAppBarSearchEnter(KBSJsonReadBool(text, "appBarSearchEnter", KBSGetAppBarSearchEnter()));

	// Hide Previous Chapter (the user's call, 2026-08-04, after the first cut left it out). Restoring
	// it is safe in a way the flag alone does not show: the jump asks ShouldHidePreviousChapter,
	// which also requires the results to have come from a BOOK, so a restored ON cannot start
	// closing documents in document scope. The menu greys the toggle out there for the same reason.
	// (KBSJump speaks bool, so the bool reader.)
	KBSJump::SetHidePreviousChapter(KBSPanelStateReadBool(text, "hidePreviousChapter", KBSJump::IsHidePreviousChapterOn()));

	// "Remember Book Panel Placement" (2026-09-25): the toggle and the placement. Read by
	// KBSBookPanelPlacement, which owns the keys; nothing is moved here - what puts the placement on
	// a book panel is that file, when one appears.
	KBSBookPanelPlacement::LoadFromSettings(text);
}

// End, KBSPanelState.cpp.
