//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Result model implementation. See KBSResultModel.h for the contract. All state is a single
//  file-static vector of chapters; the getters are bounds-checked so a repaint racing a rebuild
//  (or a stale node id) reads "nothing" rather than crashing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// General includes:
#include "Utils.h"

// Project includes:
#include "KBSResultModel.h"

namespace
{
	std::vector<KBSResultModel::Chapter> gChapters;

	// Hits stored in the chapters BEFORE 'chapterIdx' (book order). The display cap is applied in
	// book order, and every chapter before the boundary chapter is shown in full, so counting full
	// hits here is the budget consumed before this chapter.
	int32 HitsBeforeChapter(int32 chapterIdx)
	{
		int32 sum = 0;
		const int32 n = static_cast<int32>(gChapters.size());
		for (int32 i = 0; i < chapterIdx && i < n; ++i)
			sum += static_cast<int32>(gChapters[i].hits.size());
		return sum;
	}
}

void KBSResultModel::SetResults(const std::vector<Chapter>& chapters)
{
	gChapters = chapters;
}

void KBSResultModel::AppendChapter(const Chapter& chapter)
{
	gChapters.push_back(chapter);
}

void KBSResultModel::Clear()
{
	gChapters.clear();
}

void KBSResultModel::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do (the KESCL ShutdownCleanup rule).
	gChapters = std::vector<Chapter>();
}

int32 KBSResultModel::GetChapterCount()
{
	return static_cast<int32>(gChapters.size());
}

int32 KBSResultModel::GetHitCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	return static_cast<int32>(gChapters[chapterIdx].hits.size());
}

int32 KBSResultModel::GetTotalHitCount()
{
	int32 total = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
		total += static_cast<int32>(gChapters[i].hits.size());
	return total;
}

int32 KBSResultModel::GetDisplayChapterCount()
{
	// The displayed chapters are the book-order prefix whose hits fit under the cap: a chapter is
	// shown when the hits before it have not already used up the whole budget.
	int32 before = 0;
	int32 shown = 0;
	for (size_t i = 0; i < gChapters.size(); ++i)
	{
		if (before >= kKBSDisplayHitLimit)
			break;
		++shown;
		before += static_cast<int32>(gChapters[i].hits.size());
	}
	return shown;
}

int32 KBSResultModel::GetDisplayHitCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const int32 before = HitsBeforeChapter(chapterIdx);
	if (before >= kKBSDisplayHitLimit)
		return 0;	// the cap ran out before this chapter
	const int32 remaining = kKBSDisplayHitLimit - before;
	const int32 full = static_cast<int32>(gChapters[chapterIdx].hits.size());
	return (full < remaining) ? full : remaining;
}

bool KBSResultModel::GetChapterDisplay(int32 chapterIdx, PMString& outName, int32& outHitCount)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	outName = c.name;
	outName.SetTranslatable(kFalse);
	outHitCount = static_cast<int32>(c.hits.size());
	return true;
}

bool KBSResultModel::GetHitDisplay(int32 chapterIdx, int32 hitIdx,
	PMString& outLocator, PMString& outPre, PMString& outMatch, PMString& outPost)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outLocator = h.locator;
	outPre = h.preText;
	outMatch = h.matchText;
	outPost = h.postText;
	return true;
}

bool KBSResultModel::GetHitLocation(int32 chapterIdx, int32 hitIdx,
	UIDRef& outDocRef, IDFile& outFile, UID& outStoryUID, TextIndex& outStart, TextIndex& outEnd)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outDocRef = c.docRef;
	outFile = c.file;
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outEnd = h.textEnd;
	return true;
}

void KBSResultModel::RebindChapterDoc(int32 chapterIdx, const UIDRef& newDocRef)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	gChapters[chapterIdx].docRef = newDocRef;
}

void KBSResultModel::SetHitChecked(int32 chapterIdx, int32 hitIdx, bool checked)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	if (h.replaced)
		return;		// already done: it cannot come back into the selection
	h.checked = checked;
}

bool KBSResultModel::IsHitChecked(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	return c.hits[hitIdx].checked;
}

bool KBSResultModel::GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced)
{
	outChecked = false;
	outReplaced = false;
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	outChecked = c.hits[hitIdx].checked;
	outReplaced = c.hits[hitIdx].replaced;
	return true;
}

void KBSResultModel::SetAllChecked(bool checked)
{
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (hits[hi].replaced)
				continue;
			hits[hi].checked = checked;
		}
	}
}

int32 KBSResultModel::GetCheckedCount()
{
	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (hits[hi].checked && !hits[hi].replaced)
				++count;
		}
	}
	return count;
}

// End, KBSResultModel.cpp.
