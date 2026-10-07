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
#include "IOpenFileDialog.h"
#include "ISaveFileDialog.h"		// (and DocumentID.h - kOpenFileDialogBoss / kSaveFileDialogBoss)

// General includes:
#include "CreateObject.h"
#include "FileUtils.h"
#include "PMString.h"
#include "SysFileList.h"

#include <cstdio>

// Project includes:
#include "KFCDiag.h"			// KFCDiagFaultText - the test build's qd-order-file
#include "KFCQueryOrderFile.h"
#include "KFCUIID.h"			// the dialogs' titles and the type's name

namespace
{
	// A string as JSON writes it: " and \ escaped, the controls as \b \f \n \r \t or \u00XX, everything else - the
	// UTF-8 of any script - as it is (the panel's settings file writes its strings the same way).
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

	// A path that names no extension gets ".json" - the type the dialog was asked for (a name typed as "proofing").
	IDFile WithJsonExtension(const IDFile& file)
	{
		PMString path(FileUtils::SysFileToPMString(file));
		path.SetTranslatable(kFalse);
		const std::string utf8 = path.GetUTF8String();
		const size_t slash = utf8.find_last_of("\\/");
		const size_t dot = utf8.find_last_of('.');
		if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
			return file;
		path.Append(".json");
		return FileUtils::PMStringToSysFile(path);
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
	PMString typeName(kKFCQueryOrderFileTypeKey);		// "KFC Query Order"
	typeName.Translate();
	typeName.SetTranslatable(kFalse);
	PMString extension("json");
	extension.SetTranslatable(kFalse);
	if (forSave)
	{
		// kDefaultIID is missing on ISaveFileDialog - named, as SDKFileSaveChooser does.
		InterfacePtr<ISaveFileDialog> dialog(static_cast<ISaveFileDialog*>(::CreateObject(kSaveFileDialogBoss, IID_ISAVEFILEDIALOG)));
		if (dialog == nil)
			return false;
		dialog->AddFileTypeInfo(typeName, extension);
#ifdef WINDOWS
		dialog->SetAdditionalFOSFlags(FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN);	// ask before replacing a file
#endif
		// A name without a folder: the dialog starts where Windows last saved (ISaveFileDialog.h).
		PMString defaultName("Query Order.json");
		defaultName.SetTranslatable(kFalse);
		IDFile defaultFile;
		defaultFile.SetString(defaultName);		// as SDKFileSaveChooser::ShowDialog sets its default
		PMString title(kKFCQuerySaveOrderTitleKey);		// "Save Query Order"
		title.Translate();
		title.SetTranslatable(kFalse);
		IDFile chosen;
		if (!dialog->DoDialog(&defaultFile, &chosen, nil, kFalse /*the folder in defaultFile - none*/, kTrue, &title))
			return false;
		outFile = WithJsonExtension(chosen);
		return true;
	}
	InterfacePtr<IOpenFileDialog> dialog(::CreateObject2<IOpenFileDialog>(kOpenFileDialogBoss));
	if (dialog == nil)
		return false;
	dialog->AddExtension(&typeName, &extension);
#ifdef WINDOWS
	dialog->AppendAllFilesToFilterList();
#endif
	PMString title(kKFCQueryLoadOrderTitleKey);		// "Load Query Order"
	title.Translate();
	title.SetTranslatable(kFalse);
	SysFileList files;
	if (!dialog->DoDialog(nil /*where Windows last opened one*/, files, kFalse /*one file*/, &title))
		return false;
	if (files.GetFileCount() < 1 || files.GetNthFile(0) == nil)
		return false;
	outFile = *files.GetNthFile(0);
	return true;
}

// End, KFCQueryOrderFile.cpp.
