//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A RUBY LIST'S FILE, READ (docs/superpowers/specs/2026-11-02-kfc-ruby-list-design.md - what its lines say is KFCRubyList.h, std only). UTF-8, UTF-16 with its BOM, or what Notepad's ANSI and Excel's
//  plain CSV write on a Japanese Windows, CP932. The main thread only.
//
//========================================================================================

#ifndef __KFCRubyListFile_h__
#define __KFCRubyListFile_h__

#include "PMString.h"

#include <memory>

namespace KFCRubyList { struct Words; }

namespace KFCRubyListFile
{
	/** THE WORDS OF THE FILE AT `path`. False = nothing to look for: outWhy says why, whole - no path, a file that cannot be read, no words in it, or the first line that is wrong and how many are
	    (a list with a wrong line is not used: what its line was to write would be left out without a word). True = outWords the words, outCount how many. */
	bool Load(const PMString& path, std::shared_ptr<const KFCRubyList::Words>& outWords, int32& outCount, PMString& outWhy);
}

#endif // __KFCRubyListFile_h__

// End, KFCRubyListFile.h.
