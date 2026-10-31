//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A saved query of the Kohaku+ tab - see KFCKohakuQuery.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IKentenStyle.h"		// the kinds - the Kenten lists' last entries
#include "IPMStream.h"

// General includes:
#include "FileUtils.h"
#include "StreamUtil.h"

#include <cstdlib>

// Project includes:
#include "KFCKohakuQuery.h"
#include "KFCKentenSearch.h"
#include "KFCRubySearch.h"
#include "KFCSavedQueries.h"	// KindName - a kind's word, the one table of them
#include "KFCTableSearch.h"

namespace
{
const char* const kQueriesFolder = "Find-Change Queries";
const char* const kElement = "<KohakuFindChange ";

bool ReadWhole(const IDFile& file, std::string& out)
{
	out.clear();
	if (FileUtils::DoesFileExist(file) == kFalse)
		return false;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamRead(file));
	if (stream == nil)
		return false;
	const int64 size = stream->Seek(0, kSeekFromEnd);
	stream->Seek(0, kSeekFromStart);
	bool ok = (size >= 0 && size <= static_cast<int64>(0x100000));		// (a query is a few lines)
	if (ok && size > 0)
	{
		out.resize(static_cast<size_t>(size));
		ok = (stream->XferByte(reinterpret_cast<uchar*>(&out[0]), static_cast<int32>(size)) == static_cast<int32>(size));
	}
	stream->Close();
	return ok;
}

PMString Words(const std::string& utf8)
{
	PMString words;
	if (!utf8.empty())
		words.SetUTF8String(utf8);
	words.SetTranslatable(kFalse);
	return words;
}

// A value as an attribute holds it.
std::string Escaped(const std::string& value)
{
	std::string out;
	for (size_t k = 0; k < value.size(); ++k)
	{
		const char c = value[k];
		switch (c)
		{
			case '&':	out.append("&amp;"); break;
			case '<':	out.append("&lt;"); break;
			case '>':	out.append("&gt;"); break;
			case '"':	out.append("&quot;"); break;
			case '\n':	out.append("&#10;"); break;
			case '\r':	out.append("&#13;"); break;
			default:	out.push_back(c); break;
		}
	}
	return out;
}

std::string Unescaped(const std::string& value)
{
	std::string out;
	for (size_t k = 0; k < value.size(); ++k)
	{
		if (value[k] != '&')
		{
			out.push_back(value[k]);
			continue;
		}
		const size_t end = value.find(';', k);
		const std::string name = (end == std::string::npos) ? std::string() : value.substr(k + 1, end - k - 1);
		if (name == "amp")
			out.push_back('&');
		else if (name == "lt")
			out.push_back('<');
		else if (name == "gt")
			out.push_back('>');
		else if (name == "quot")
			out.push_back('"');
		else if (name == "#10")
			out.push_back('\n');
		else if (name == "#13")
			out.push_back('\r');
		else
		{
			out.push_back('&');		// (not one this file writes: as it stands)
			continue;
		}
		k = end;
	}
	return out;
}

void Attribute(std::string& io, const char* key, const PMString& value)
{
	io.append(" ");
	io.append(key);
	io.append("=\"");
	io.append(Escaped(value.GetUTF8String()));
	io.append("\"");
}

void Attribute(std::string& io, const char* key, int32 value)
{
	io.append(" ");
	io.append(key);
	io.append("=\"");
	io.append(std::to_string(value));
	io.append("\"");
}
}	// anonymous namespace

bool KFCKohakuQuery::Read(const IDFile& file, KFCKohakuQueryData& out)
{
	out = KFCKohakuQueryData();
	std::string text;
	if (!ReadWhole(file, text))
		return false;
	size_t at = text.find(kElement);
	if (at == std::string::npos)
		return false;
	at += strlen(kElement);
	const size_t end = text.find('>', at);
	if (end == std::string::npos)
		return false;
	// key="value" pairs, up to the element's end.
	while (at < end)
	{
		const size_t eq = text.find("=\"", at);
		if (eq == std::string::npos || eq >= end)
			break;
		size_t keyStart = eq;
		while (keyStart > at && text[keyStart - 1] != ' ')
			--keyStart;
		const size_t close = text.find('"', eq + 2);
		if (close == std::string::npos || close >= end)
			break;
		const std::string key(text, keyStart, eq - keyStart);
		const std::string value(Unescaped(std::string(text, eq + 2, close - eq - 2)));
		const int32 number = std::atoi(value.c_str());
		if (key == "kind")
		{
			const int32 modes[] = { kKFCQRCodeSearchMode, kKFCTableSearchMode, kKFCRubySearchMode, kKFCKentenSearchMode };
			for (size_t m = 0; m < sizeof(modes) / sizeof(modes[0]); ++m)
				if (value == KFCSavedQueries::KindName(modes[m]))
					out.mode = modes[m];
		}
		else if (key == "find")
			out.table.findStyle = Words(value);
		else if (key == "overrides")
			out.table.overridesOnly = (number != 0);
		else if (key == "changeStyle")
			out.table.changeStyle = Words(value);
		else if (key == "clear")
			out.table.clearOverrides = (number != 0);
		else if (key == "rubyLook")
			out.ruby.kind = number;
		else if (key == "rubyBase")
			out.ruby.base = Words(value);
		else if (key == "ruby")
			out.ruby.reading = Words(value);
		else if (key == "rubyChange")
			out.ruby.changeTo = Words(value);
		else if (key == "rubyRemove")
			out.ruby.removeRuby = (number != 0);
		else if (key == "kentenLook")
			out.kenten.kind = number;
		else if (key == "kentenBase")
			out.kenten.base = Words(value);
		else if (key == "kentenMark")
			out.kenten.mark = number;
		else if (key == "kentenChange")
			out.kenten.changeTo = number;
		else if (key == "kentenRemove")
			out.kenten.removeKenten = (number != 0);
		at = close + 1;
	}
	// A FILE IS WHAT ANYONE MADE OF IT, AND THE FACE SELECTS BY THESE: a number its list has no entry for reads as the list's first (the lists - KFCUI.fr: Kenten: is Any and every kind, Change Kenten To: has no Custom).
	if (out.ruby.kind < kKFCRubyFindWith || out.ruby.kind > kKFCRubyFindMarks)
		out.ruby.kind = kKFCRubyFindWith;
	if (out.kenten.kind < kKFCKentenFindWith || out.kenten.kind > kKFCKentenFindWithout)
		out.kenten.kind = kKFCKentenFindWith;
	if (out.kenten.mark < 0 || out.kenten.mark > IKentenStyle::Kenten_Custom)
		out.kenten.mark = 0;
	if (out.kenten.changeTo < 0 || out.kenten.changeTo >= IKentenStyle::Kenten_Custom)
		out.kenten.changeTo = 0;
	return KFCIsKohakuQueryMode(out.mode);
}

std::string KFCKohakuQuery::FileText(const KFCKohakuQueryData& query)
{
	// THE ELEMENT OF KFC'S. Its kind's conditions alone.
	std::string element(kElement);
	element.resize(element.size() - 1);		// (Attribute writes the space before each)
	Attribute(element, "version", 1);
	Attribute(element, "kind", PMString(KFCSavedQueries::KindName(query.mode)));
	if (query.mode == kKFCTableSearchMode)
	{
		Attribute(element, "find", query.table.findStyle);
		Attribute(element, "overrides", query.table.overridesOnly ? 1 : 0);
		Attribute(element, "changeStyle", query.table.changeStyle);
		Attribute(element, "clear", query.table.clearOverrides ? 1 : 0);
	}
	else if (query.mode == kKFCRubySearchMode)
	{
		Attribute(element, "rubyLook", query.ruby.kind);
		Attribute(element, "rubyBase", query.ruby.base);
		Attribute(element, "ruby", query.ruby.reading);
		Attribute(element, "rubyChange", query.ruby.changeTo);
		Attribute(element, "rubyRemove", query.ruby.removeRuby ? 1 : 0);
	}
	else if (query.mode == kKFCKentenSearchMode)
	{
		Attribute(element, "kentenLook", query.kenten.kind);
		Attribute(element, "kentenBase", query.kenten.base);
		Attribute(element, "kentenMark", query.kenten.mark);
		Attribute(element, "kentenChange", query.kenten.changeTo);
		Attribute(element, "kentenRemove", query.kenten.removeKenten ? 1 : 0);
	}
	element.append(">\n</KohakuFindChange>\n");
	// A QUERY OF THE COLOR TAB ASKING NOTHING, as InDesign writes one (measured - app.saveFindChangeQuery), for InDesign to load where KFC is not there.
	std::string text("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Query>\n<Header>\n<Version major=\"5\" minor=\"1\">\n</Version>\n"
		"<Application value=\"Adobe InDesign\">\n</Application>\n<QueryType value=\"Color\" qid=\"5\">\n</QueryType>\n</Header>\n"
		"<Description>\n<Scope value=\"0\">\n</Scope>\n<ObjectType value=\"0\">\n</ObjectType>\n<FindChangeOptions>\n<IncludeLockedLayers value=\"0\">\n</IncludeLockedLayers>\n<IncludeLockedStories value=\"0\">\n</IncludeLockedStories>\n<IncludeMasterPages value=\"0\">\n</IncludeMasterPages>\n<IncludeHiddenLayers value=\"0\">\n</IncludeHiddenLayers>\n<IncludeFootnotes value=\"1\">\n</IncludeFootnotes>\n</FindChangeOptions>\n<FindFormatSettings>\n</FindFormatSettings>\n<ReplaceFormatSettings>\n</ReplaceFormatSettings>\n</Description>\n");
	text.append(element);
	text.append("</Query>\n");
	return text;
}

PMString KFCKohakuQuery::EntryName(int32 mode, const PMString& name)
{
	PMString entry(KFCSavedQueries::KindName(mode));
	entry.SetTranslatable(kFalse);
	entry.Append(" - ");
	entry.Append(name);
	entry.SetTranslatable(kFalse);
	return entry;
}

bool KFCKohakuQuery::FolderOf(IDFile& out)
{
	return FileUtils::GetAppRoamingDataFolder(&out, PMString(kQueriesFolder)) != kFalse;		// (it makes the folder when it is not there - FileUtils.h)
}

bool KFCKohakuQuery::FileOf(int32 mode, const PMString& name, IDFile& out)
{
	if (!FolderOf(out))
		return false;
	PMString fileName(EntryName(mode, name));
	fileName.Append(".xml");
	FileUtils::AppendPath(&out, fileName);
	return true;
}

bool KFCKohakuQuery::IsInItsFolder(const IDFile& file)
{
	IDFile parent;
	if (FileUtils::GetParentDirectory(file, parent) == kFalse)
		return false;
	PMString name;
	FileUtils::GetFileName(parent, name);
	return name.IsEqual(PMString(kQueriesFolder), kFalse) != kFalse;
}

bool KFCKohakuQuery::HasChangeSide(const KFCKohakuQueryData& query)
{
	if (query.mode == kKFCTableSearchMode)
		return !query.table.changeStyle.IsEmpty() || query.table.clearOverrides;
	if (query.mode == kKFCRubySearchMode)
		return query.ruby.kind == kKFCRubyFindMarks || !query.ruby.changeTo.IsEmpty() || query.ruby.removeRuby;		// (ruby from marks: the reading is in the mark)
	if (query.mode == kKFCKentenSearchMode)
		return query.kenten.changeTo != 0 || query.kenten.removeKenten;
	return false;		// (a QR code's list writes nothing)
}

bool KFCKohakuQuery::WriteDocument(const UIDRef& docRef, const KFCKohakuQueryData& query, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount)
{
	outCount = 0;
	if (query.mode == kKFCTableSearchMode)
		return KFCTableSearch::WriteDocument(docRef, query.table, part, outCount);
	if (query.mode == kKFCRubySearchMode)
		return KFCRubySearch::WriteDocument(docRef, query.ruby, part, outCount);
	if (query.mode == kKFCKentenSearchMode)
		return KFCKentenSearch::WriteDocument(docRef, query.kenten, part, outCount);
	return false;
}

// End, KFCKohakuQuery.cpp.
