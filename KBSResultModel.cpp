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

#include <algorithm>	// std::lower_bound - where the display cap falls inside one font group
#include <utility>		// std::move - the thinning below hands whole hits over instead of copying
#include <string>		// std::string - DescribeAllRows builds its block in UTF-8 bytes

// Project includes:
#include "KBSResultModel.h"

namespace
{
	std::vector<KBSResultModel::Chapter> gChapters;

	// Were these results produced by a book search? Decides whether the tree opens its chapters.
	bool gFromBook = false;

	// Find/Change hits, or a missing-glyph scan's findings? See KBSResultModel::SetResultKind.
	KBSResultModel::ResultKind gResultKind = KBSResultModel::kResultFindChange;

	// The book those results came from (file name only). Drawn on the tree's book row.
	PMString gBookName;

	// The Find/Change tab the search ran in (an IFindChangeOptions::SearchMode value; -1 = nothing
	// searched yet). See KBSResultModel::SetSearchMode for why the replace has to compare against it.
	int32 gSearchMode = -1;

	// Is the panel showing a replace's aftermath rather than a search's results? See
	// KBSResultModel::IsShowingReplaceOutcome.
	bool gShowingOutcome = false;

	// Has a command been run since the results were last discarded? See KBSResultModel::NoteRun.
	// Deliberately NOT "are there any chapters": a search that found nothing has still been run.
	bool gHasRun = false;

	// One row copied aside before a replace changed it. See KBSResultModel::BeginRowBackup.
	struct BackedUpRow
	{
		int32					chapter;
		int32					hit;
		KBSResultModel::Hit		row;
	};

	// Only true while a replace is running; empty at every other moment.
	bool gBackingUpRows = false;
	std::vector<BackedUpRow> gRowBackup;

	// Copy a row aside before it is written to, if a replace is running. Every change is kept,
	// including a second one to the same row - RollBackRows walks the copies backwards, so the
	// oldest is applied last and wins.
	void BackUpRow(int32 chapterIdx, int32 hitIdx, const KBSResultModel::Hit& row)
	{
		if (!gBackingUpRows)
			return;
		BackedUpRow saved;
		saved.chapter = chapterIdx;
		saved.hit = hitIdx;
		saved.row = row;
		gRowBackup.push_back(saved);
	}

	// Which row the result tree's right-click menu was popped over (KBSResultNodeEH stashes it just
	// before HandlePopupMenu; Check All / Uncheck All read it back). See the header for the two
	// negative values it can hold.
	int32 gContextMenuChapter = KBSResultModel::kNoContextMenuChapter;

	// Does this row carry a check box? THE one definition of the question, so the commands that set
	// the boxes and the counts that decide whether to offer those commands can no longer drift apart:
	// a row the model quietly checked but the panel drew no box for would be replaced without ever
	// having been asked for. Replaced = the text it matched is gone; locked = InDesign offers no way
	// to change it; an outcome already says why it was left alone.
	bool RowHasCheckBox(const KBSResultModel::Hit& hit)
	{
		// A scan reports; it does not offer work. Asked first because it is a property of the
		// RESULT SET, not of the row: no row of a scan carries a box, whatever that row holds.
		if (gResultKind == KBSResultModel::kResultMissingGlyph)
			return false;

		return !hit.replaced && !hit.isLocked && hit.outcome == KBSResultModel::kOutcomeNone;
	}

	// Group a chapter's hits by the font that had no glyph for them - the tree's font level.
	//
	// A chapter is grouped only when at least one of its hits NAMES a font: a Find/Change result
	// names none, and its tree stays the three levels it has always had. The question is asked of the
	// HITS rather than of gResultKind on purpose - SetResultKind happens to run before the chapters
	// are appended today (KBSGlyphScanEngine.cpp:505-512), and a rule resting on that order is a rule
	// that breaks silently the day the order changes.
	//
	// Once a chapter IS grouped, EVERY hit joins a group - including one whose font name came back
	// empty, which gets a group of its own rather than being left without a parent. A hit with no
	// parent is a hit that vanishes from the tree; a row reading "(unknown font)" is one the user can
	// still see and click.
	void BuildFontGroups(KBSResultModel::Chapter& chapter)
	{
		chapter.fontGroups.clear();

		bool anyFont = false;
		for (size_t i = 0; i < chapter.hits.size(); ++i)
		{
			if (!chapter.hits[i].fontName.IsEmpty())
			{
				anyFont = true;
				break;
			}
		}

		if (!anyFont)
		{
			for (size_t i = 0; i < chapter.hits.size(); ++i)
			{
				chapter.hits[i].fontGroup = -1;
				chapter.hits[i].fontGroupPos = -1;
			}
			return;
		}

		// The hits arrive in PAGE order, so walking them in order leaves the groups in
		// first-appearance order - which is the order the panel shows them in (user's call
		// 2026-08-02). The linear search over the groups runs over a handful of entries: a document
		// broken in dozens of different fonts is not a case worth carrying a map for.
		for (size_t i = 0; i < chapter.hits.size(); ++i)
		{
			KBSResultModel::Hit& hit = chapter.hits[i];

			int32 found = -1;
			for (size_t g = 0; g < chapter.fontGroups.size(); ++g)
			{
				if (chapter.fontGroups[g].fontName.Compare(kTrue, hit.fontName) == 0)
				{
					found = static_cast<int32>(g);
					break;
				}
			}
			if (found < 0)
			{
				KBSResultModel::FontGroup group;
				group.fontName = hit.fontName;
				group.fontName.SetTranslatable(kFalse);
				chapter.fontGroups.push_back(group);
				found = static_cast<int32>(chapter.fontGroups.size()) - 1;
			}

			KBSResultModel::FontGroup& group = chapter.fontGroups[found];
			hit.fontGroup = found;
			hit.fontGroupPos = static_cast<int32>(group.hitIndices.size());
			group.hitIndices.push_back(static_cast<int32>(i));
		}
	}

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

void KBSResultModel::AppendChapter(const Chapter& chapter)
{
	gChapters.push_back(chapter);
	// Grouped on the way in, on the copy the model owns - the argument is const, and the groups index
	// the hits they are built from, so they have to be built where those hits are going to live.
	BuildFontGroups(gChapters.back());
}

void KBSResultModel::Clear()
{
	gChapters.clear();
	gShowingOutcome = false;
	gFromBook = false;
	gResultKind = kResultFindChange;
	gBookName.Clear();
	gSearchMode = -1;
	// The right-click target is an index into the chapters that just went away - keeping it would let
	// the next search's Check All reach a chapter the user never right-clicked.
	gContextMenuChapter = kNoContextMenuChapter;
	// Discarding the results puts the panel back to the state it started in, illustration included.
	gHasRun = false;
}

void KBSResultModel::SetFromBook(bool fromBook)
{
	gFromBook = fromBook;
}

void KBSResultModel::NoteRun()
{
	gHasRun = true;
}

bool KBSResultModel::HasRun()
{
	return gHasRun;
}

bool KBSResultModel::IsFromBook()
{
	return gFromBook;
}

void KBSResultModel::SetResultKind(ResultKind kind)
{
	gResultKind = kind;
}

KBSResultModel::ResultKind KBSResultModel::GetResultKind()
{
	return gResultKind;
}

void KBSResultModel::SetSearchMode(int32 mode)
{
	gSearchMode = mode;
}

int32 KBSResultModel::GetSearchMode()
{
	return gSearchMode;
}

void KBSResultModel::SetBookName(const PMString& name)
{
	gBookName = name;
	gBookName.SetTranslatable(kFalse);
}

PMString KBSResultModel::GetBookName()
{
	PMString name(gBookName);
	name.SetTranslatable(kFalse);
	return name;
}

void KBSResultModel::ShutdownCleanup()
{
	// Assigning a fresh vector releases the storage too, not just the contents, so the static
	// destructor at DLL unload finds nothing left to do (the KESCL ShutdownCleanup rule).
	gChapters = std::vector<Chapter>();

	// Normally already empty - a replace clears it on both of its exits - but a shutdown during
	// one would leave copies behind, and these hold PMStrings like the chapters do.
	ForgetRowBackup();
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

int32 KBSResultModel::GetDisplayFontHitCount(int32 chapterIdx, int32 fontIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const Chapter& c = gChapters[chapterIdx];
	if (fontIdx < 0 || fontIdx >= static_cast<int32>(c.fontGroups.size()))
		return 0;

	// The chapter's own share of the cap, then this group's share of that. hitIndices is ascending,
	// so the count is simply where the cap falls inside it.
	const int32 shown = GetDisplayHitCount(chapterIdx);
	const std::vector<int32>& idx = c.fontGroups[fontIdx].hitIndices;
	return static_cast<int32>(std::lower_bound(idx.begin(), idx.end(), shown) - idx.begin());
}

int32 KBSResultModel::GetDisplayFontCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;
	const Chapter& c = gChapters[chapterIdx];

	int32 shownGroups = 0;
	for (int32 g = 0; g < static_cast<int32>(c.fontGroups.size()); ++g)
	{
		if (GetDisplayFontHitCount(chapterIdx, g) > 0)
			++shownGroups;
	}
	return shownGroups;
}

bool KBSResultModel::GetFontDisplay(int32 chapterIdx, int32 fontIdx, PMString& outName, int32& outHitCount)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (fontIdx < 0 || fontIdx >= static_cast<int32>(c.fontGroups.size()))
		return false;

	const FontGroup& group = c.fontGroups[fontIdx];
	outName = group.fontName;
	if (outName.IsEmpty())
		outName = "(unknown font)";		// the font could not be named - see BuildFontGroups
	outName.SetTranslatable(kFalse);
	outHitCount = static_cast<int32>(group.hitIndices.size());
	return true;
}

int32 KBSResultModel::GetFontGroupHit(int32 chapterIdx, int32 fontIdx, int32 nth)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (fontIdx < 0 || fontIdx >= static_cast<int32>(c.fontGroups.size()))
		return -1;
	const std::vector<int32>& idx = c.fontGroups[fontIdx].hitIndices;
	if (nth < 0 || nth >= static_cast<int32>(idx.size()))
		return -1;
	return idx[nth];
}

int32 KBSResultModel::GetHitFontGroup(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return -1;
	return c.hits[hitIdx].fontGroup;
}

int32 KBSResultModel::GetHitFontGroupPos(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return -1;
	return c.hits[hitIdx].fontGroupPos;
}

bool KBSResultModel::GetHitRow(int32 chapterIdx, int32 hitIdx, RowDisplay& out)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	out.locator = h.locator;
	out.accentFlag = h.accentFlag;
	out.preText = h.preText;
	out.matchText = h.matchText;
	out.postText = h.postText;
	out.fontName = h.fontName;
	out.checked = h.checked;
	out.replaced = h.replaced;
	out.locked = h.isLocked;
	out.outcome = h.outcome;
	return true;
}

namespace
{
	// One column's text, with the separators escaped, so a hit holding a tab or a paragraph break
	// still occupies exactly one line and one column. Escaping walks the UTF-8 BYTES: in UTF-8 no
	// byte of a multi-byte character can be mistaken for an ASCII one, so this is safe whatever
	// script the text is written in.
	void AppendEscapedUTF8(std::string& out, const PMString& s)
	{
		const std::string utf8 = s.GetUTF8String();
		for (std::string::size_type i = 0; i < utf8.size(); ++i)
		{
			const char c = utf8[i];
			if (c == '\t')
				out += "\\t";
			else if (c == '\r' || c == '\n')
				out += "\\n";
			else if (c == '\\')
				out += "\\\\";
			else
				out += c;
		}
	}

	void AppendNumberUTF8(std::string& out, int32 n)
	{
		PMString num;
		num.AppendNumber(n);
		out += num.GetUTF8String();
	}

	// The outcome as the WORD the locator shows, not as an enumerator's number: a test then reads
	// the same vocabulary the user does, and inserting an outcome later cannot silently renumber
	// what an existing test compares against.
	const char* OutcomeWord(KBSResultModel::ChangeOutcome outcome)
	{
		switch (outcome)
		{
			case KBSResultModel::kOutcomeMissing:	return "missing";
			case KBSResultModel::kOutcomeLocked:	return "locked";
			case KBSResultModel::kOutcomeRefused:	return "refused";
			case KBSResultModel::kOutcomeNone:		break;
		}
		return "";
	}
}

void KBSResultModel::DescribeAllRows(PMString& out)
{
	std::string buf;

	// The header: what this result set IS, before any row of it.
	buf += "#\t";
	AppendEscapedUTF8(buf, GetBookName());
	buf += "\t";
	AppendNumberUTF8(buf, IsFromBook() ? 1 : 0);
	buf += "\t";
	AppendNumberUTF8(buf, IsShowingReplaceOutcome() ? 1 : 0);
	buf += "\t";
	AppendNumberUTF8(buf, GetChapterCount());
	buf += "\t";
	AppendNumberUTF8(buf, GetTotalHitCount());

	const int32 chapters = GetChapterCount();
	for (int32 ci = 0; ci < chapters; ++ci)
	{
		PMString chapterName;
		int32 chapterHits = 0;
		if (!GetChapterDisplay(ci, chapterName, chapterHits))
			continue;

		// Every STORED hit, not GetDisplayHitCount. The display cap is a limit on what the tree
		// draws; a test that could only see the rows before it would report a pass for the ones it
		// never looked at.
		const int32 hits = GetHitCount(ci);
		for (int32 hi = 0; hi < hits; ++hi)
		{
			RowDisplay row;
			if (!GetHitRow(ci, hi, row))
				continue;

			buf += "\n";
			AppendNumberUTF8(buf, ci);
			buf += "\t";
			AppendEscapedUTF8(buf, chapterName);
			buf += "\t";
			AppendNumberUTF8(buf, hi);
			buf += "\t";
			AppendEscapedUTF8(buf, row.locator);
			buf += "\t";
			AppendEscapedUTF8(buf, row.accentFlag);
			buf += "\t";
			AppendEscapedUTF8(buf, row.preText);
			buf += "\t";
			AppendEscapedUTF8(buf, row.matchText);
			buf += "\t";
			AppendEscapedUTF8(buf, row.postText);
			buf += "\t";
			AppendEscapedUTF8(buf, row.fontName);
			buf += "\t";
			AppendNumberUTF8(buf, row.checked ? 1 : 0);
			buf += "\t";
			AppendNumberUTF8(buf, row.replaced ? 1 : 0);
			buf += "\t";
			AppendNumberUTF8(buf, row.locked ? 1 : 0);
			buf += "\t";
			buf += OutcomeWord(row.outcome);
			buf += "\t";
			// Which FONT row of the tree this hit hangs under (-1 = this chapter has no font level).
			// The row already carries the font's NAME, so this adds one thing the name cannot prove:
			// that the grouping and its order are what the panel is actually drawing.
			AppendNumberUTF8(buf, GetHitFontGroup(ci, hi));
		}
	}

	// SetUTF8String marks the string not translatable, which is what this needs - it is data.
	out.SetUTF8String(buf);
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
	if (h.isLocked)
		return;		// locked content cannot be changed at all - the row has no box to click either
	if (h.outcome != kOutcomeNone)
		return;		// it already says why it was left alone
	if (gShowingOutcome)
		return;		// the panel is a report right now, not a work list
	h.checked = checked;
}

bool KBSResultModel::GetHitFlags(int32 chapterIdx, int32 hitIdx, bool& outChecked, bool& outReplaced, bool& outLocked)
{
	outChecked = false;
	outReplaced = false;
	outLocked = false;
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	outChecked = c.hits[hitIdx].checked;
	outReplaced = c.hits[hitIdx].replaced;
	outLocked = c.hits[hitIdx].isLocked;
	return true;
}

void KBSResultModel::SetAllChecked(bool checked)
{
	if (gShowingOutcome)
		return;		// nothing on a report is selectable

	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			// The rows that carry no check box are not touched by Check All either - otherwise
			// the model would hold checked hits the panel shows no box for.
			if (!RowHasCheckBox(hits[hi]))
				continue;
			hits[hi].checked = checked;
		}
	}
}

void KBSResultModel::SetChapterChecked(int32 chapterIdx, bool checked)
{
	if (gShowingOutcome)
		return;		// nothing on a report is selectable
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;

	std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	for (size_t hi = 0; hi < hits.size(); ++hi)
	{
		if (!RowHasCheckBox(hits[hi]))
			continue;
		hits[hi].checked = checked;
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

int32 KBSResultModel::GetCheckedChapterCount()
{
	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (hits[hi].checked && !hits[hi].replaced)
			{
				++count;
				break;		// this chapter counts once, however many of its hits are checked
			}
		}
	}
	return count;
}

int32 KBSResultModel::GetCheckableCount()
{
	if (gShowingOutcome)
		return 0;	// every row has lost its box, so Check All / Uncheck All grey out

	int32 count = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (RowHasCheckBox(hits[hi]))
				++count;
		}
	}
	return count;
}

int32 KBSResultModel::GetChapterCheckableCount(int32 chapterIdx)
{
	if (gShowingOutcome)
		return 0;	// every row has lost its box, whichever chapter the menu was popped over
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;

	int32 count = 0;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	for (size_t hi = 0; hi < hits.size(); ++hi)
	{
		if (RowHasCheckBox(hits[hi]))
			++count;
	}
	return count;
}

void KBSResultModel::SetContextMenuChapter(int32 chapterIdx)
{
	gContextMenuChapter = chapterIdx;
}

int32 KBSResultModel::GetContextMenuChapter()
{
	return gContextMenuChapter;
}

int32 KBSResultModel::GetHitWalkOrder(int32 chapterIdx, int32 hitIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return -1;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return -1;
	return c.hits[hitIdx].walkOrder;
}

bool KBSResultModel::GetChapterLocation(int32 chapterIdx, UIDRef& outDocRef, IDFile& outFile)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	outDocRef = c.docRef;
	outFile = c.file;
	return true;
}

bool KBSResultModel::GetHitMatchIdentity(int32 chapterIdx, int32 hitIdx, UID& outStoryUID,
	TextIndex& outStart, PMString& outMatch)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outMatch = h.matchText;
	outMatch.SetTranslatable(kFalse);
	return true;
}

bool KBSResultModel::GetHitStoryStamp(int32 chapterIdx, int32 hitIdx, UID& outStoryUID,
	uint32& outChangeCount)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	outStoryUID = c.hits[hitIdx].storyUID;
	outChangeCount = c.hits[hitIdx].storyChangeCount;
	return true;
}

void KBSResultModel::MarkHitReplaced(int32 chapterIdx, int32 hitIdx,
	TextIndex newStart, TextIndex newEnd)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	BackUpRow(chapterIdx, hitIdx, h);

	// The locator (page) is kept: a replacement does not move the line to another page in any
	// case worth chasing here - if the text reflowed that far, the result set is stale anyway and
	// the user is told to search again.
	//
	// The displayed segments are left as the search wrote them, on purpose: they are read back
	// when the chapter is done, by which time no later replacement can still change them.
	h.textStart = newStart;
	h.textEnd = newEnd;
	h.replaced = true;
	h.checked = false;
}

bool KBSResultModel::GetHitReplacedRange(int32 chapterIdx, int32 hitIdx, UID& outStoryUID,
	TextIndex& outStart, TextIndex& outEnd)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	if (!h.replaced)
		return false;
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outEnd = h.textEnd;
	return true;
}

void KBSResultModel::SetHitSegments(int32 chapterIdx, int32 hitIdx, const PMString& newPre,
	const PMString& newMatch, const PMString& newPost)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];

	// Backed up again even though MarkHitReplaced already copied this row aside: a cancel has to
	// put back what the search left, and RollBackRows applies the copies oldest-last, so an extra
	// copy costs one Hit and cannot change the outcome. Nothing here relies on the earlier call
	// having happened.
	BackUpRow(chapterIdx, hitIdx, h);

	h.preText = newPre;			h.preText.SetTranslatable(kFalse);
	h.matchText = newMatch;		h.matchText.SetTranslatable(kFalse);
	h.postText = newPost;		h.postText.SetTranslatable(kFalse);
}

void KBSResultModel::BuildHitLocator(Hit& hit)
{
	hit.locator.Clear();
	hit.locator.SetTranslatable(kFalse);
	hit.accentFlag.Clear();
	hit.accentFlag.SetTranslatable(kFalse);

	if (hit.pageString.IsEmpty())
	{
		hit.locator.Append("ov");	// overset with nothing placed anywhere: no page to name
	}
	else
	{
		// An overset hit carries the "+" indicator's page and sorts by it; a trailing "ov" (after
		// the page and ordinal) marks it as overset -> e.g. "P1(2)ov".
		hit.locator.Append("P");
		hit.locator.Append(hit.pageString);
		if (hit.pageOrdinal > 0)
		{
			hit.locator.Append("(");
			hit.locator.AppendNumber(hit.pageOrdinal);
			hit.locator.Append(")");
		}
		if (hit.isOverset)
			hit.locator.Append("ov");
	}

	// What the row cannot show any other way, each separated by a space, in this order:
	//   hidden  - on a switched-off layer, so the page will look empty on arrival
	//   lock    - locked, so the row carries no check box and the replace will not touch it
	//   missing - the same text could not be found where the search left it
	//   refused - InDesign's own replace command would not run there
	// The last two are put there by a replace, or by a jump that finds the text gone, so they never
	// appear on a fresh search's rows. They stack on either shape: "P1(2)ov hidden lock",
	// "ov missing", "P7 hidden".
	//
	// A space, not a "+": InDesign's own overset marker IS a "+", so "P5+lock" reads as "page 5,
	// overset". Spelled out rather than clipped to "hid" / "lck" - these are what explain a row the
	// user cannot act on, so they are worth the characters, unlike "ov", which merely qualifies a
	// page number. ("loc" was never an option: English reads it as "location".)
	if (hit.isHidden)
		hit.locator.Append(" hidden");
	if (hit.isLocked || hit.outcome == kOutcomeLocked)
		hit.locator.Append(" lock");

	// NOT chained onto the test above. A locked row can be jumped to and found changed, and then it
	// has both things to say - "P4(1) lock missing" - where an else left it saying only that it was
	// locked, which is not why the jump landed on different text. Missing and refused do exclude
	// each other: outcome holds one value.
	//
	// These two go into their own string rather than onto the locator because the cell draws them
	// as a separate run in the theme's accent colour; the space in front of them belongs to that
	// run and is put there when it is drawn (KBSColorTextView).
	if (hit.outcome == kOutcomeMissing)
		hit.accentFlag.Append("missing");	// its own run, in the accent colour
	else if (hit.outcome == kOutcomeRefused)
		hit.accentFlag.Append("refused");	// same run, same colour: same kind of reason
}

void KBSResultModel::SetHitOutcome(int32 chapterIdx, int32 hitIdx, ChangeOutcome outcome)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return;
	Hit& h = c.hits[hitIdx];
	if (h.replaced)
		return;		// it WAS replaced - nothing went wrong with it
	BackUpRow(chapterIdx, hitIdx, h);
	h.outcome = outcome;
	h.checked = false;
	BuildHitLocator(h);
}

bool KBSResultModel::IsShowingReplaceOutcome()
{
	return gShowingOutcome;
}

void KBSResultModel::BeginRowBackup()
{
	gRowBackup.clear();
	gBackingUpRows = true;
}

void KBSResultModel::RollBackRows()
{
	gBackingUpRows = false;

	// Backwards: a row written to more than once has several copies, and the one taken FIRST is
	// the one the search left, so it has to be applied last.
	for (size_t i = gRowBackup.size(); i > 0; --i)
	{
		const BackedUpRow& saved = gRowBackup[i - 1];
		if (saved.chapter < 0 || saved.chapter >= static_cast<int32>(gChapters.size()))
			continue;	// the result set changed underneath - nothing to put the row back into
		std::vector<Hit>& hits = gChapters[saved.chapter].hits;
		if (saved.hit < 0 || saved.hit >= static_cast<int32>(hits.size()))
			continue;
		hits[saved.hit] = saved.row;
	}

	// Swapping against a temporary releases the storage as well as the contents.
	std::vector<BackedUpRow>().swap(gRowBackup);
}

void KBSResultModel::ForgetRowBackup()
{
	gBackingUpRows = false;
	std::vector<BackedUpRow>().swap(gRowBackup);
}

void KBSResultModel::DropChapter(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return;
	gChapters.erase(gChapters.begin() + chapterIdx);
}

int32 KBSResultModel::KeepCheckedRows()
{
	// A replace that was asked for nothing must not empty the panel, so check before touching
	// anything. A row counts as asked about when any of these hold:
	//   replaced - it was changed (its check was cleared when it was written)
	//   outcome  - it was reached and left alone, and says why (its check was cleared then too)
	//   checked  - still selected, so the run never reached it: the safety ceiling, a chapter that
	//              would not open, or a cancel. Those rows carry no reason, on purpose.
	//   isLocked - found by the search and never offerable. Kept so the list can account for a
	//              search that turned up more than the replace was allowed to touch.
	bool anyAsked = false;
	for (size_t ci = 0; ci < gChapters.size() && !anyAsked; ++ci)
	{
		const std::vector<Hit>& hits = gChapters[ci].hits;
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (hits[hi].replaced || hits[hi].checked || hits[hi].isLocked
				|| hits[hi].outcome != kOutcomeNone)
			{
				anyAsked = true;
				break;
			}
		}
	}
	if (!anyAsked)
		return GetTotalHitCount();

	// Thin each chapter down to the rows the replace was asked about...
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		std::vector<Hit>& hits = gChapters[ci].hits;
		std::vector<Hit> keep;
		keep.reserve(hits.size());
		for (size_t hi = 0; hi < hits.size(); ++hi)
		{
			if (!hits[hi].replaced && !hits[hi].checked && !hits[hi].isLocked
				&& hits[hi].outcome == kOutcomeNone)
				continue;
			// The source vector is thrown away at the swap below, so the hit is moved out rather
			// than copied - a Hit carries six PMStrings.
			Hit hit = std::move(hits[hi]);

			// Rebuild the locator WITHOUT the within-page ordinal. That ordinal counted a hit's
			// place among the matches on its page; once the rows that were left alone are gone the
			// numbers are full of gaps (P1(1), P1(3)) and say nothing true, so the page alone is
			// what the row shows. Everything about the shape lives in BuildHitLocator now.
			hit.pageOrdinal = 0;
			BuildHitLocator(hit);
			keep.push_back(std::move(hit));
		}
		hits.swap(keep);
	}

	// ...then drop the chapters left with nothing.
	std::vector<Chapter> remaining;
	remaining.reserve(gChapters.size());
	int32 kept = 0;
	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		if (gChapters[ci].hits.empty())
			continue;
		kept += static_cast<int32>(gChapters[ci].hits.size());	// counted BEFORE the move
		remaining.push_back(std::move(gChapters[ci]));
	}
	gChapters.swap(remaining);

	// From here the panel is a report, not a work list: no row offers a check box.
	gShowingOutcome = true;
	return kept;
}

// End, KBSResultModel.cpp.
