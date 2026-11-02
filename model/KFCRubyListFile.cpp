//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A ruby list's file, read - see KFCRubyListFile.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"

// General includes:
#include "FileUtils.h"
#include "StreamUtil.h"

#include <string>
// (windows.h is in already: the precompiled header brings it - KFCPanelAlpha.cpp says how. MultiByteToWideChar: CP932.)
#include <windows.h>

// Project includes:
#include "KFCRubyListFile.h"
#include "KFCRubyList.h"

namespace
{
// Far beyond any list's size: a file this big is some other file.
const int64 kMostBytes = 0x1000000;

bool ReadBytes(const IDFile& file, std::string& out)
{
	out.clear();
	if (FileUtils::DoesFileExist(file) == kFalse)
		return false;
	InterfacePtr<IPMStream> stream(StreamUtil::CreateFileStreamRead(file));
	if (stream == nil)
		return false;
	const int64 size = stream->Seek(0, kSeekFromEnd);
	stream->Seek(0, kSeekFromStart);
	bool ok = (size >= 0 && size <= kMostBytes);
	if (ok && size > 0)
	{
		out.resize(static_cast<size_t>(size));
		ok = (stream->XferByte(reinterpret_cast<uchar*>(&out[0]), static_cast<int32>(size)) == static_cast<int32>(size));
	}
	stream->Close();
	return ok;
}

// What a Japanese Windows writes: its two-byte code page, whole - a byte it has no character for is no list.
bool DecodeCP932(const std::string& bytes, std::u32string& out)
{
	out.clear();
	if (bytes.empty())
		return true;
	const int size = static_cast<int>(bytes.size());
	const int units = ::MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes.data(), size, nullptr, 0);
	if (units <= 0)
		return false;
	std::wstring wide(static_cast<size_t>(units), L'\0');
	if (::MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, bytes.data(), size, &wide[0], units) != units)
		return false;
	out.reserve(wide.size());
	for (size_t k = 0; k < wide.size(); ++k)		// (no character of it stands past the BMP)
		out.push_back(static_cast<char32_t>(wide[k]));
	return true;
}

void SayNamed(PMString& outWhy, const PMString& name, const char* words)
{
	outWhy.Append("Ruby List: ");
	outWhy.Append(name);
	outWhy.Append(words);
	outWhy.SetTranslatable(kFalse);
}
}	// anonymous namespace

bool KFCRubyListFile::Load(const PMString& path, std::shared_ptr<const KFCRubyList::Words>& outWords, int32& outCount, PMString& outWhy)
{
	outWords.reset();
	outCount = 0;
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	if (path.IsEmpty())
	{
		outWhy.Append("Words in a Ruby List reads a file: press Choose List... on the Kohaku+ tab in Edit > Find/Change.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	const IDFile file(FileUtils::PMStringToSysFile(path));
	PMString name;
	FileUtils::GetFileName(file, name);
	name.SetTranslatable(kFalse);
	std::string bytes;
	if (!ReadBytes(file, bytes))
	{
		SayNamed(outWhy, name, " could not be read.");
		return false;
	}
	std::u32string text;
	if (!KFCRubyList::DecodeUnicode(bytes, text) && !DecodeCP932(bytes, text))
	{
		SayNamed(outWhy, name, " is not text in UTF-8, UTF-16 or Shift_JIS.");
		return false;
	}
	std::shared_ptr<KFCRubyList::Words> words(std::make_shared<KFCRubyList::Words>());
	KFCRubyList::Problem problem;
	size_t problems = 0;
	KFCRubyList::Parse(text, *words, problem, problems);
	if (problems > 0)
	{
		SayNamed(outWhy, name, ": line ");
		outWhy.AppendNumber(static_cast<int32>(problem.line));
		switch (problem.why)
		{
			case KFCRubyList::kNoReading:
				outWhy.Append(" has no reading.");
				break;
			case KFCRubyList::kBadMark:
				outWhy.Append(": a reading needs kanji just before it, or a bar where its base starts.");
				break;
			default:
				outWhy.Append(" gives the word of line ");
				outWhy.AppendNumber(static_cast<int32>(problem.otherLine));
				outWhy.Append(" another reading.");
				break;
		}
		if (problems > 1)
		{
			outWhy.Append(" (");
			outWhy.AppendNumber(static_cast<int32>(problems));
			outWhy.Append(" lines are wrong.)");
		}
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	if (words->entries.empty())
	{
		SayNamed(outWhy, name, " holds no words.");
		return false;
	}
	outCount = static_cast<int32>(words->entries.size());
	outWords = words;
	return true;
}

// End, KFCRubyListFile.cpp.
