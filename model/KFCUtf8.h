//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  IS A FILE'S TEXT UTF-8 - asked whole before any string of it goes to PMString::SetUTF8String, which throws
//  std::runtime_error on some broken UTF-8 (a continuation byte where a character starts, 0xF8 and up, past U+10FFFF, a
//  surrogate) and reads a sequence cut short as another character without a word (memory pmstring-from-utf8-bytes).
//  Thrown out of Load Order..., it left the query dialog saying nothing and its next press lost (measured 2026-10-22: a
//  name in CP932 - Notepad's "ANSI" on a Japanese PC - case qd-load-not-utf8, block 19's D19-3). The readers of a file a
//  person can edit: a run order (ui/KFCQueryOrderFile.cpp) and a Kohaku+ query (KFCKohakuQuery::Read - the since-2.1.0
//  re-inspection's D22-a, 2026-11-06: until then it read with no such question).
//  AND THE OTHER WAY - CAN A TEXT BE MADE UTF-8 (KFCHasLoneSurrogate): asked of a text a person typed or pasted before it
//  is written out as UTF-8 (KFCKohakuQuery::FileText - block 0's D0-a, 2026-11-06).
//  STD ONLY.
//
//========================================================================================

#ifndef __KFCUtf8_h__
#define __KFCUtf8_h__

#include <string>

/** By RFC 3629's table: a lead byte and its continuation bytes, no overlong form, no surrogate, nothing past U+10FFFF. */
inline bool KFCIsWellFormedUtf8(const std::string& text)
{
	size_t i = 0;
	while (i < text.size())
	{
		const unsigned char lead = static_cast<unsigned char>(text[i]);
		if (lead < 0x80)
		{
			++i;
			continue;
		}
		size_t length = 0;
		unsigned long code = 0;
		unsigned long smallest = 0;
		if (lead >= 0xC2 && lead <= 0xDF)
		{
			length = 2;
			code = lead & 0x1F;
			smallest = 0x80;
		}
		else if (lead >= 0xE0 && lead <= 0xEF)
		{
			length = 3;
			code = lead & 0x0F;
			smallest = 0x800;
		}
		else if (lead >= 0xF0 && lead <= 0xF4)
		{
			length = 4;
			code = lead & 0x07;
			smallest = 0x10000;
		}
		else
			return false;		// a continuation byte, 0xC0 / 0xC1 (always overlong), or 0xF5 and up
		if (text.size() - i < length)
			return false;
		for (size_t k = 1; k < length; ++k)
		{
			const unsigned char next = static_cast<unsigned char>(text[i + k]);
			if ((next & 0xC0) != 0x80)
				return false;
			code = (code << 6) | (next & 0x3F);
		}
		if (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
			return false;
		i += length;
	}
	return true;
}

/** HALF OF A SURROGATE PAIR in a text, its UTF-16 read as it stands (PMString::GrabUTF16Buffer - UTF16TextChar is an unsigned
    short). No UTF-8 is made of such a text: PMString::GetUTF8String takes a lone high surrogate together with the character after
    it (or, at the end, makes something else of it), and what it makes of a lone low one is UTF-8 that SetUTF8String throws on
    (measured - KT's u8trip probe, 2026-10-23; memory pmstring-from-utf8-bytes). A paste can bring one (block 20's D20-3 - the
    Pages field of a dialog since gone). */
inline bool KFCHasLoneSurrogate(const unsigned short* units, long count)
{
	for (long i = 0; units != nullptr && i < count; ++i)
	{
		if (units[i] >= 0xD800 && units[i] <= 0xDBFF)
		{
			if (i + 1 < count && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF)
			{
				++i;		// a pair: one character
				continue;
			}
			return true;
		}
		if (units[i] >= 0xDC00 && units[i] <= 0xDFFF)
			return true;
	}
	return false;
}

#endif // __KFCUtf8_h__

// End, KFCUtf8.h.
