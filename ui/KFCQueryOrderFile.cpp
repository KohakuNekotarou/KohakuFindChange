//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A run order as a file of its own - see KFCQueryOrderFile.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ISaveFileDialog.h"	// (and DocumentID.h - kSaveFileDialogBoss) Save Order...'s dialog, one level below the chooser

// General includes:
#include "CreateObject.h"
#include "FileUtils.h"
#include "PMString.h"
#include "SDKFileHelper.h"		// SDKFileOpenChooser - Load Order...'s file

#include <cstdio>

// Project includes:
#include "KFCDiag.h"			// KFCDiagFaultText - the test build's qd-order-file
#include "KFCQueryOrderFile.h"
#include "KFCUIID.h"			// the dialogs' titles and the type's name

namespace
{
	// A string as JSON writes it: " and \ escaped, the controls as \b \f \n \r \t or \u00XX, everything else - the
	// UTF-8 of any script - as it is. (KFC's one JSON string writer: the panel's settings file holds only true / false
	// and numbers.)
	void AppendJsonString(std::string& out, const std::string& utf8)
	{
		out += '"';
		for (size_t i = 0; i < utf8.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(utf8[i]);
			switch (c)
			{
				case '"':	out += "\\\"";	break;
				case '\\':	out += "\\\\";	break;
				case '\b':	out += "\\b";	break;
				case '\f':	out += "\\f";	break;
				case '\n':	out += "\\n";	break;
				case '\r':	out += "\\r";	break;
				case '\t':	out += "\\t";	break;
				default:
					if (c < 0x20)
					{
						char escaped[8] = { 0 };
						_snprintf_s(escaped, sizeof(escaped), _TRUNCATE, "\\u%04x", static_cast<unsigned int>(c));
						out += escaped;
					}
					else
						out += static_cast<char>(c);
			}
		}
		out += '"';
	}

	void AppendUtf8(std::string& out, uint32 code)
	{
		if (code < 0x80)
			out += static_cast<char>(code);
		else if (code < 0x800)
		{
			out += static_cast<char>(0xC0 | (code >> 6));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
		else if (code < 0x10000)
		{
			out += static_cast<char>(0xE0 | (code >> 12));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (code >> 18));
			out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (code & 0x3F));
		}
	}

	// A small JSON reader, for this file's shape and nothing more: objects, lists, strings, and any other value passed
	// over (a number, true, false, null). Every read answers false on text that is not JSON.
	class JsonReader
	{
	public:
		JsonReader(const std::string& text) : fText(text), fAt(0)
		{
			if (fText.size() >= 3 && static_cast<unsigned char>(fText[0]) == 0xEF
				&& static_cast<unsigned char>(fText[1]) == 0xBB && static_cast<unsigned char>(fText[2]) == 0xBF)
				fAt = 3;
		}

		bool Peek(char c)
		{
			SkipSpace();
			return fAt < fText.size() && fText[fAt] == c;
		}

		bool Take(char c)
		{
			if (!Peek(c))
				return false;
			++fAt;
			return true;
		}

		bool String(std::string& out)
		{
			out.clear();
			if (!Take('"'))
				return false;
			while (fAt < fText.size())
			{
				const char c = fText[fAt++];
				if (c == '"')
					return true;
				if (c != '\\')
				{
					out += c;
					continue;
				}
				if (fAt >= fText.size())
					return false;
				const char e = fText[fAt++];
				switch (e)
				{
					case '"': case '\\': case '/':	out += e;		break;
					case 'b':						out += '\b';	break;
					case 'f':						out += '\f';	break;
					case 'n':						out += '\n';	break;
					case 'r':						out += '\r';	break;
					case 't':						out += '\t';	break;
					case 'u':
					{
						uint32 code = 0;
						if (!Hex4(code))
							return false;
						// a surrogate pair is one character
						if (code >= 0xD800 && code <= 0xDBFF && fAt + 1 < fText.size() && fText[fAt] == '\\'
							&& fText[fAt + 1] == 'u')
						{
							fAt += 2;
							uint32 low = 0;
							if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF)
								return false;
							code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
						}
						AppendUtf8(out, code);
						break;
					}
					default:
						return false;
				}
			}
			return false;		// no closing quote
		}

		// Any value, passed over: a string, an object, a list, or a bare word / number.
		bool SkipValue()
		{
			SkipSpace();
			if (fAt >= fText.size())
				return false;
			const char c = fText[fAt];
			if (c == '"')
			{
				std::string ignored;
				return String(ignored);
			}
			if (c == '{' || c == '[')
			{
				const char close = (c == '{') ? '}' : ']';
				++fAt;
				if (Take(close))
					return true;
				for (;;)
				{
					if (c == '{')
					{
						std::string key;
						if (!String(key) || !Take(':'))
							return false;
					}
					if (!SkipValue())
						return false;
					if (Take(','))
						continue;
					return Take(close);
				}
			}
			const size_t start = fAt;
			while (fAt < fText.size() && fText[fAt] != ',' && fText[fAt] != '}' && fText[fAt] != ']'
				&& fText[fAt] != ' ' && fText[fAt] != '\t' && fText[fAt] != '\r' && fText[fAt] != '\n')
				++fAt;
			return fAt > start;
		}

	private:
		void SkipSpace()
		{
			while (fAt < fText.size() && (fText[fAt] == ' ' || fText[fAt] == '\t' || fText[fAt] == '\r' || fText[fAt] == '\n'))
				++fAt;
		}

		bool Hex4(uint32& out)
		{
			if (fAt + 4 > fText.size())
				return false;
			out = 0;
			for (int i = 0; i < 4; ++i)
			{
				const char h = fText[fAt++];
				out <<= 4;
				if (h >= '0' && h <= '9')
					out |= static_cast<uint32>(h - '0');
				else if (h >= 'a' && h <= 'f')
					out |= static_cast<uint32>(h - 'a' + 10);
				else if (h >= 'A' && h <= 'F')
					out |= static_cast<uint32>(h - 'A' + 10);
				else
					return false;
			}
			return true;
		}

		const std::string&	fText;
		size_t				fAt;
	};

	// One "queries" entry: its three strings (any other key passed over).
	bool ReadEntry(JsonReader& reader, KFCOrderFileEntry& out)
	{
		if (!reader.Take('{'))
			return false;
		if (reader.Take('}'))
			return true;
		for (;;)
		{
			std::string key;
			if (!reader.String(key) || !reader.Take(':'))
				return false;
			if (reader.Peek('"'))
			{
				std::string value;
				if (!reader.String(value))
					return false;
				if (key == "kind")
					out.kind = value;
				else if (key == "name")
					out.name = value;
				else if (key == "file")
					out.file = value;
			}
			else if (!reader.SkipValue())
				return false;
			if (reader.Take(','))
				continue;
			return reader.Take('}');
		}
	}

}

std::string KFCOrderFileText(const std::vector<KFCOrderFileEntry>& entries)
{
	std::string text("{\n\t\"kfcQueryOrder\": 1,\n\t\"queries\": [");
	for (size_t i = 0; i < entries.size(); ++i)
	{
		text += (i == 0) ? "\n\t\t{ \"kind\": " : ",\n\t\t{ \"kind\": ";
		AppendJsonString(text, entries[i].kind);
		text += ", \"name\": ";
		AppendJsonString(text, entries[i].name);
		text += ", \"file\": ";
		AppendJsonString(text, entries[i].file);
		text += " }";
	}
	text += entries.empty() ? "]\n}\n" : "\n\t]\n}\n";
	return text;
}

bool KFCOrderFileParse(const std::string& text, std::vector<KFCOrderFileEntry>& outEntries)
{
	outEntries.clear();
	JsonReader reader(text);
	if (!reader.Take('{'))
		return false;
	bool haveMarker = false, haveList = false;
	std::vector<KFCOrderFileEntry> entries;
	if (!reader.Take('}'))
	{
		for (;;)
		{
			std::string key;
			if (!reader.String(key) || !reader.Take(':'))
				return false;
			if (key == "kfcQueryOrder")
			{
				haveMarker = true;
				if (!reader.SkipValue())
					return false;
			}
			else if (key == "queries")
			{
				if (!reader.Take('['))
					return false;
				haveList = true;
				if (!reader.Take(']'))
				{
					for (;;)
					{
						KFCOrderFileEntry entry;
						if (!ReadEntry(reader, entry))
							return false;
						entries.push_back(entry);
						if (reader.Take(','))
							continue;
						if (!reader.Take(']'))
							return false;
						break;
					}
				}
			}
			else if (!reader.SkipValue())
				return false;
			if (reader.Take(','))
				continue;
			if (!reader.Take('}'))
				return false;
			break;
		}
	}
	if (!haveMarker || !haveList)
		return false;
	outEntries.swap(entries);
	return true;
}

bool KFCChooseOrderFile(bool forSave, IDFile& outFile)
{
#ifdef KFC_DIAG
	// TEST BUILDS ONLY: the fault switch qd-order-file's first line is the file (KFCDiag.h) - Windows' dialogs cannot be
	// pressed by a test.
	{
		std::string path;
		if (KFCDiagFaultText("qd-order-file", path))
		{
			PMString chosen;
			chosen.SetUTF8String(path);
			chosen.SetTranslatable(kFalse);
			outFile = FileUtils::PMStringToSysFile(chosen);
			KFC_DIAG_LOG("QUERYORDER %s through the fault switch: %s", forSave ? "save" : "load", path.c_str());
			return true;
		}
	}
#endif
	const PMString typeName(kKFCQueryOrderFileTypeKey);		// "KFC Query Order"
	if (forSave)
	{
		// THE NAME ALWAYS ENDS IN .json - THE DIALOG SEES TO IT (the author: "Test.A" saves as "Test.A.indd" in
		// InDesign, so it should here). Without help it does not: Windows' Save dialog adds the type's extension only to
		// a name whose own extension Windows does not know - "Test.aaa" -> "Test.aaa.json", but "Test.A" stayed
		// "Test.A" on a PC where .a is registered (measured), and Load Order... lists .json alone. FOS_STRICTFILETYPES
		// is Windows' own flag for exactly this ("only allow ... one of the file name extensions" of the types), so the
		// overwrite question is asked about the name that is written - adding ".json" after the dialog would replace an
		// existing "Test.A.json" unasked.
		// So kSaveFileDialogBoss itself, the SDK chooser's steps one level down (SDKFileSaveChooser::ShowDialog): the
		// chooser fixes its flags at FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN. KCM's Task Start takes the same level
		// for its own reason (KCMTaskStartSave.cpp).
		// kDefaultIID is missing on ISaveFileDialog - named, as the chooser does.
		InterfacePtr<ISaveFileDialog> dialog(static_cast<ISaveFileDialog*>(::CreateObject(kSaveFileDialogBoss, IID_ISAVEFILEDIALOG)));
		if (dialog == nil)
			return false;
		// The type's name TRANSLATED first, as the chooser's Filter does (SDKFileHelper.cpp) - AddFileTypeInfo shows
		// what it is given: handed the key, the dialog read "0x1EA600kKFCQueryOrderFileTypeKey (*.json)" (measured).
		PMString shownType(typeName);
		shownType.Translate();
		dialog->AddFileTypeInfo(shownType, PMString("json"));
#ifdef WINDOWS
		dialog->SetAdditionalFOSFlags(FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN | FOS_STRICTFILETYPES);
#endif
		// A name without a folder: the dialog starts where Windows last saved (ISaveFileDialog.h).
		IDFile defaultFile;
		defaultFile.SetString(PMString("Query Order.json"));
		PMString title(kKFCQuerySaveOrderTitleKey);		// "Save Query Order"
		title.Translate();
		int32 selectedIndex = 0;
		return dialog->DoDialog(&defaultFile, &outFile, &selectedIndex, kTrue /*the last used folder*/,
			kTrue /*the type menu*/, &title) != kFalse;
	}
	// THE SDK'S OWN CHOOSER (sdksamples/common/SDKFileHelper.h - SnpChooseFile.cpp). JSON ONLY (the author's call: "not
	// all files - only JSON can be loaded"): one type and no All Files - the chooser's own one-type road
	// (SDKFileOpenChooser::ShowDialog: AddExtension alone), as KCM's Import Story Text offers Word's files only. The
	// type's name and the title go in as string-table keys: the chooser translates them. The Mac type ('TEXT', as the
	// SDK's text-writing snippets pass) means nothing on Windows.
	SDKFileOpenChooser chooser;
	chooser.SetTitle(PMString(kKFCQueryLoadOrderTitleKey));		// "Load Query Order"
	chooser.AddFilter('TEXT', PMString("json"), typeName);
	chooser.ShowDialog();		// where Windows last opened one; one file
	if (!chooser.IsChosen())
		return false;
	outFile = chooser.GetIDFile();
	return true;
}

// End, KFCQueryOrderFile.cpp.
