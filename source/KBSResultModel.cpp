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
#include "TextChar.h"	// kTextChar_CR / kTextChar_LF / kTextChar_PilchrowSign - see MarkUpBreaksForDisplay
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

	// The query the results were found with, as one ready-made line. See KBSResultModel::SetQueryText.
	PMString gQueryText;

	// What the replace that produced this aftermath was told to write. Empty until a replace runs -
	// see KBSResultModel::SetChangeText.
	PMString gChangeText;

	// The whole of what those results were WALKED by - query plus every switch that decides the match
	// set - as one opaque key. See KBSResultModel::SetWalkSignature.
	PMString gWalkSignature;

	// The sentence the RUN that produced these results reported ("9 hit(s) in 3 of 3 chapter(s)...").
	// Kept apart from the panel's status line, which anything may overwrite - ticking a row writes
	// "P1(2)  checked" there - because the saved report's heading wants the run's own summary, not
	// whatever the panel happened to say last (found 2026-08-09: a search-tick-save sequence wrote
	// "Summary: P1(2)  checked" at the head of the file). See KBSResultModel::NoteRunSummary.
	PMString gRunSummary;

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
		// Is this a list that offers work at all? Asked first because it is a property of the RESULT
		// SET, not of the row: when the answer is no, no row carries a box whatever that row holds.
		//
		// ***** THE WHOLE QUESTION, NOT HALF OF IT. ***** This asked IsReportOnlyKind alone until
		// 2026-08-07, which left the OTHER half - a replace's aftermath - for every caller to
		// remember on its own, and all five of them did (SetHitChecked, SetAllChecked,
		// SetChapterChecked, GetCheckableCount, GetChapterCheckableCount). Nothing was wrong with
		// the answers; what was wrong is that the sixth caller would have had to know. The two
		// halves are ORed in one place - NoRowHasCheckBox - and this is that place's customer, not
		// its rival. The callers still take their early exit, but they take it on the same
		// question (see there).
		if (KBSResultModel::NoRowHasCheckBox())
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

		// No font level for this chapter, and nothing to write: -1 / -1 is what Hit's own constructor
		// sets, this is the only code in the plug-in that ever writes those two fields, and the hits
		// arriving here have just been built. Walking the whole chapter to store the values it
		// already holds is what this did until 2026-08-08.
		//
		// !! That rests on the hits being NEW. A caller that ever hands over hits carried across from
		// an earlier result set has to reset the pair itself - the alternative is this loop back, and
		// it costs one pass over every hit of every ungrouped chapter to defend against a caller that
		// does not exist.
		if (!anyFont)
			return;

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

void KBSResultModel::AppendChapter(Chapter&& chapter)
{
	// ***** THE HITS ARE TAKEN, NOT COPIED. ***** A chapter of a large search holds thousands of
	// Hits and each Hit holds six PMStrings, so copying the vector in here doubled the cost of
	// filling the model for nothing: every caller builds a Chapter, hands it over and drops it.
	// Copied until 2026-08-08 - and the search had used swap() to keep the same hits from being
	// copied into that Chapter one line earlier, which this then undid.
	gChapters.push_back(std::move(chapter));
	// Grouped on the way in, on the chapter the model now owns: the groups index the hits they are
	// built from, so they have to be built where those hits are going to live.
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
	gQueryText.Clear();
	gChangeText.Clear();
	gWalkSignature.Clear();
	gRunSummary.Clear();
	// (KBSEditStamp::Forget was called from here, and the file is gone: the replace checks the
	//  stored positions against a fresh walk rather than fingerprinting each chapter, so nothing
	//  outside this model describes these rows any more.)
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

bool KBSResultModel::IsReportOnlyKind()
{
	// Every kind EXCEPT the Find/Change query, which is the one that offers work. Written as the
	// list of scans rather than as "not kResultFindChange" so that adding a kind is a decision
	// rather than a default: a new kind that DOES offer work would otherwise quietly lose its
	// check boxes here.
	return gResultKind == kResultMissingGlyph || gResultKind == kResultOverset;
}

bool KBSResultModel::NoRowHasCheckBox()
{
	// gShowingOutcome rather than IsShowingReplaceOutcome() only because this file owns the flag.
	// The two are the same question - see the header for why both halves have to be asked.
	return KBSResultModel::IsReportOnlyKind() || gShowingOutcome;
}

bool KBSResultModel::MatchTextIsLiveText()
{
	// Named the ONE kind that departs from it, not "is a scan": the glyph scan is every bit as much
	// a report, and its rows DO hold the story's own characters - so folding this into
	// IsReportOnlyKind would silently switch off a check that works there.
	return gResultKind != kResultOverset;
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

// The two recorded lines are READ inside this file only - BuildReportText writes them into the
// saved report's heading and nothing else asks for them. Getters for them lived here until
// 2026-08-08 and had no callers at all.
void KBSResultModel::NoteRunSummary(const PMString& summary)
{
	gRunSummary = summary;
	gRunSummary.SetTranslatable(kFalse);
}

PMString KBSResultModel::GetRunSummary()
{
	PMString summary(gRunSummary);
	summary.SetTranslatable(kFalse);
	return summary;
}

void KBSResultModel::SetQueryText(const PMString& query)
{
	gQueryText = query;
	gQueryText.SetTranslatable(kFalse);
}

void KBSResultModel::SetChangeText(const PMString& change)
{
	gChangeText = change;
	gChangeText.SetTranslatable(kFalse);
}

void KBSResultModel::SetWalkSignature(const PMString& signature)
{
	gWalkSignature = signature;
	gWalkSignature.SetTranslatable(kFalse);
}

PMString KBSResultModel::GetWalkSignature()
{
	PMString signature(gWalkSignature);
	signature.SetTranslatable(kFalse);
	return signature;
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

	// The static PMStrings, emptied for the same reason the vectors are: nothing of ours should
	// still be holding storage when the DLL unloads (the KESCL ShutdownCleanup rule).
	//
	// ALL FIVE of them. gChangeText was added on 2026-08-04 and did not get a line here, so the one
	// string that is only ever filled by a replace was the one left holding storage at unload. When
	// a static is added above, it is added here too - that is what this list is (gRunSummary joined
	// with its line already written, 2026-08-09).
	gBookName.Clear();
	gQueryText.Clear();
	gChangeText.Clear();
	gWalkSignature.Clear();
	gRunSummary.Clear();

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

	// One cell of the SAVED report: the text with its tabs and line breaks flattened to a single
	// space. Deliberately NOT AppendEscapedUTF8's "\t" / "\n": that block is split back into fields by
	// a script, this file is pasted into a spreadsheet by a person - where a literal backslash-n is
	// noise, and a real newline would split the row. A run of them collapses to ONE space, so a CRLF
	// does not become two.
	void AppendFlattenedUTF8(std::string& out, const PMString& s)
	{
		const std::string utf8 = s.GetUTF8String();
		bool folded = false;
		for (std::string::size_type i = 0; i < utf8.size(); ++i)
		{
			const char c = utf8[i];
			if (c == '\t' || c == '\r' || c == '\n')
			{
				if (!folded)
					out += ' ';
				folded = true;
				continue;
			}
			out += c;
			folded = false;
		}
	}

	// Add one word to a space-separated cell.
	void AppendWord(std::string& cell, const char* word)
	{
		if (!cell.empty())
			cell += ' ';
		cell += word;
	}

	// The row's flags as one cell, in the SAME WORDS AND THE SAME ORDER the panel's locator uses
	// (KBSResultModel::BuildHitLocator) - so the file and the panel never call one thing by two names.
	// "replaced" is the one word the locator has no place for: on the panel a replaced row shows it by
	// having lost its check box, which a text file cannot show.
	std::string BuildFlagCell(const KBSResultModel::Hit& hit)
	{
		std::string flags;
		if (hit.isOverset)
			AppendWord(flags, "overset");
		if (hit.isHidden)
			AppendWord(flags, "hidden");
		if (hit.isLocked || hit.outcome == KBSResultModel::kOutcomeLocked)
			AppendWord(flags, "locked");
		// Missing and refused exclude each other (two values of one field); locked is already said
		// above, in the word the locator uses for it.
		if (hit.outcome == KBSResultModel::kOutcomeMissing)
			AppendWord(flags, "missing");
		else if (hit.outcome == KBSResultModel::kOutcomeRefused)
			AppendWord(flags, "refused");
		if (hit.replaced)
			AppendWord(flags, "replaced");
		return flags;
	}

	// The report heading's first line: WHICH command produced these rows. Reads the module's own state
	// directly - it is in the same translation unit - rather than going back through the getters.
	const char* ReportKindHeading()
	{
		switch (gResultKind)
		{
			case KBSResultModel::kResultMissingGlyph:	return "Find Missing Glyphs";
			case KBSResultModel::kResultOverset:		return "Find Overset";
			case KBSResultModel::kResultFindChange:		break;
		}
		// A replace turns the result set into a report of what it did, which is a different thing to
		// have in front of you than a search's hits - so the heading says which one this is.
		return gShowingOutcome ? "Kohaku Find/Change (after Change Checked)" : "Kohaku Find/Change";
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

// U+21B5 DOWNWARDS ARROW WITH CORNER LEFTWARDS - the mark for a forced line break. TextChar.h names
// the pilcrow (kTextChar_PilchrowSign, :122) but carries no constant for this one, so it is named
// here rather than left as a bare number in the loop below.
static const UTF32TextChar kKBSReturnArrow = 0x21B5;

// See KBSResultModel.h for what this is for and why it is DISPLAY ONLY.
//
// The two marks are the ones InDesign itself draws with Show Hidden Characters on: a pilcrow for a
// paragraph end, a return arrow for a forced line break (Shift+Enter). They are two different things
// to a replace, so a row spells them differently.
//
// Whole runs are copied between the marks rather than one character at a time, so a surrogate pair
// is never split. Most strings hold no break at all, so the string is scanned once before anything
// is built: the common row pays one pass and no allocation.
void KBSResultModel::MarkUpBreaksForDisplay(PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* buf = s.GrabUTF16Buffer(&n);
	if (buf == nil || n <= 0)
		return;

	bool16 any = kFalse;
	for (int32 i = 0; i < n && !any; ++i)
		any = (buf[i] == kTextChar_CR || buf[i] == kTextChar_LF);
	if (!any)
		return;

	PMString out;
	out.SetTranslatable(kFalse);
	int32 runStart = 0;
	for (int32 i = 0; i < n; ++i)
	{
		if (buf[i] != kTextChar_CR && buf[i] != kTextChar_LF)
			continue;
		if (i > runStart)
			out.AppendW(buf + runStart, i - runStart);
		out.AppendW(buf[i] == kTextChar_CR
			? static_cast<UTF32TextChar>(kTextChar_PilchrowSign)
			: kKBSReturnArrow);
		runStart = i + 1;
	}
	if (n > runStart)
		out.AppendW(buf + runStart, n - runStart);

	s = out;
	s.SetTranslatable(kFalse);
}

void KBSResultModel::BuildReportText(const PMString& summaryLine, PMString& out)
{
	std::string buf;

	// ----- The heading: what this result set IS, before any row of it -----
	buf += ReportKindHeading();

	// The query, on the Find/Change results only - neither scan has one. Kept on the replace's
	// aftermath as well: "what was searched for" is exactly what a report of a replace needs to name.
	if (gResultKind == kResultFindChange && !gQueryText.IsEmpty())
	{
		buf += "\nQuery: ";
		AppendFlattenedUTF8(buf, gQueryText);
	}

	// ...and what was WRITTEN, on the aftermath of a replace only. A report of a search names what was
	// looked for and nothing else: the Change To box may hold anything at all at that point, and
	// naming it would read as something that has already been done (user's decision, 2026-08-04).
	//
	// Asked of IsShowingReplaceOutcome rather than of the string alone, so that the line follows the
	// same flag every other "this is a replace's report" decision in this plug-in follows - the file
	// name (KBSReportSave::ActionNamePart) and the panel's own illustration among them.
	if (gResultKind == kResultFindChange && IsShowingReplaceOutcome() && !gChangeText.IsEmpty())
	{
		buf += "\nChange: ";
		AppendFlattenedUTF8(buf, gChangeText);
	}

	// What was run over. A book search names the book; a document search names its one chapter.
	if (gFromBook && !gBookName.IsEmpty())
	{
		buf += "\nBook: ";
		AppendFlattenedUTF8(buf, gBookName);
	}
	else if (!gChapters.empty())
	{
		buf += "\nDocument: ";
		AppendFlattenedUTF8(buf, gChapters[0].name);
	}

	// The panel's own status line, verbatim. Every kind of run words its summary differently
	// ("9 hit(s) in 3 of 3 chapter(s)", "55 missing glyphs in 6 places."), and taking the sentence
	// rather than re-counting is what keeps the file from ever contradicting the panel.
	if (!summaryLine.IsEmpty())
	{
		buf += "\nSummary: ";
		AppendFlattenedUTF8(buf, summaryLine);
	}

	// How many lines follow. NOT the same number as the panel shows: the display cap stops at
	// kKBSDisplayHitLimit rows, and this file carries every stored hit.
	buf += "\nRows: ";
	AppendNumberUTF8(buf, GetTotalHitCount());

	// ----- The table -----
	buf += "\n\n";
	buf += "<Document>\t<Page>\t<No>\t<Text>\t<Font>\t<Flags>";

	for (size_t ci = 0; ci < gChapters.size(); ++ci)
	{
		const Chapter& chapter = gChapters[ci];
		for (size_t hi = 0; hi < chapter.hits.size(); ++hi)
		{
			const Hit& hit = chapter.hits[hi];

			buf += "\n";
			AppendFlattenedUTF8(buf, chapter.name);
			buf += "\t";
			// The page NUMBER alone, so a spreadsheet can sort on it - the "overset" / "locked" that
			// ride the panel's locator are in the Flags cell instead. A hit that is on no page at all
			// (pasteboard, or overset with nothing placed anywhere) leaves the cell empty rather than
			// writing the "PB" the page list spells for it: an empty cell sorts and filters, two
			// letters in a number column do not.
			if (hit.pageIndex >= 0)
				AppendFlattenedUTF8(buf, hit.pageString);
			buf += "\t";
			// Which hit this is ON ITS PAGE - the "(2)" the panel's locator carries. Without it several
			// matches in one paragraph write IDENTICAL lines (measured on the first real save, three
			// matches deep in one paragraph), and the file cannot be lined up against the panel at all.
			// Its own column rather than part of the page, so the page still sorts as a number. 0 means
			// the row does not show one - the cell is left empty rather than writing a zero.
			if (hit.pageOrdinal > 0)
				AppendNumberUTF8(buf, hit.pageOrdinal);
			buf += "\t";
			// The line as the panel draws it: the three segments joined back together, and carrying
			// the SAME break marks the panel puts on them (2026-08-04), so a match that spans
			// paragraphs reads as one row in both places rather than as two different rows.
			//   *The flattening still runs, after the marks: it folds the TABS a line can carry -
			//    which would split this cell into extra columns - and by then the marks have taken
			//    the breaks out of its way, so it has nothing else left to fold.
			//   *A mark is one CHARACTER, so nothing here can put a line break into the file.
			// (An overset finding has its report - "Frame (370)" - in the match segment, so this
			// writes that.)
			PMString seg(hit.preText);	MarkUpBreaksForDisplay(seg);	AppendFlattenedUTF8(buf, seg);
			seg = hit.matchText;		MarkUpBreaksForDisplay(seg);	AppendFlattenedUTF8(buf, seg);
			seg = hit.postText;			MarkUpBreaksForDisplay(seg);	AppendFlattenedUTF8(buf, seg);
			buf += "\t";
			AppendFlattenedUTF8(buf, hit.fontName);
			buf += "\t";
			buf += BuildFlagCell(hit);
		}
	}

	// A text file ends with a line break: without it the last row is a partial line, and some tools
	// drop it.
	buf += "\n";

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
	// The same question the panel asks before it draws a box, asked here so the model can never hold
	// a checked hit that no row offered. It covers the whole list as well as the row - a scan has
	// nothing to replace, and neither has a replace's report - which the per-row flags cannot say
	// anything about.
	if (!RowHasCheckBox(h))
		return;
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

bool KBSResultModel::GetHitReach(int32 chapterIdx, int32 hitIdx, bool& outLocked, bool& outHidden)
{
	outLocked = false;
	outHidden = false;
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	outLocked = c.hits[hitIdx].isLocked;
	outHidden = c.hits[hitIdx].isHidden;
	return true;
}

void KBSResultModel::SetAllChecked(bool checked)
{
	// Nothing on a list that offers no work is selectable. A short cut, not a second opinion: the
	// per-row test below asks the same question, and this only saves walking every hit to be told
	// so once per row.
	if (NoRowHasCheckBox())
		return;

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
	if (NoRowHasCheckBox())
		return;		// the same short cut SetAllChecked takes, over one chapter
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

int32 KBSResultModel::GetChapterCheckedCount(int32 chapterIdx)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return 0;

	// Same rule as GetCheckedCount, applied to one chapter: a REPLACED row does not count, because
	// it is no longer waiting to be done.
	int32 count = 0;
	const std::vector<Hit>& hits = gChapters[chapterIdx].hits;
	for (size_t hi = 0; hi < hits.size(); ++hi)
	{
		if (hits[hi].checked && !hits[hi].replaced)
			++count;
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
	if (NoRowHasCheckBox())
		return 0;	// no row of this list has a box, so Check All / Uncheck All grey out

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
	if (NoRowHasCheckBox())
		return 0;	// no row has a box, whichever chapter the menu was popped over
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
	TextIndex& outStart, TextIndex& outEnd, uint64& outHash)
{
	if (chapterIdx < 0 || chapterIdx >= static_cast<int32>(gChapters.size()))
		return false;
	const Chapter& c = gChapters[chapterIdx];
	if (hitIdx < 0 || hitIdx >= static_cast<int32>(c.hits.size()))
		return false;
	const Hit& h = c.hits[hitIdx];
	outStoryUID = h.storyUID;
	outStart = h.textStart;
	outEnd = h.textEnd;
	outHash = h.matchHash;
	return true;
}

// (GetHitAnchor and GetHitStoryStamp were defined here until 2026-08-03 - see the note where they
// were declared in KBSResultModel.h.)

void KBSResultModel::MarkHitReplaced(int32 chapterIdx, int32 hitIdx, UID newStoryUID,
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
	//
	// The STORY comes from the command as well - see the header. It is normally the story the row
	// already named; when it is not, the row is describing a match somewhere else and the range
	// alone would be read against the wrong text.
	h.storyUID = newStoryUID;
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
	const PMString& newMatch, const PMString& newPost, uint64 newMatchHash)
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

	// ***** AND the hash, in the same call. ***** See the header for why the two cannot be set
	// apart from one another.
	h.matchHash = newMatchHash;
}

void KBSResultModel::BuildHitLocator(Hit& hit)
{
	hit.locator.Clear();
	hit.locator.SetTranslatable(kFalse);
	hit.accentFlag.Clear();
	hit.accentFlag.SetTranslatable(kFalse);

	if (hit.pageString.IsEmpty())
	{
		hit.locator.Append("overset");	// overset with nothing placed anywhere: no page to name
	}
	else
	{
		// An overset hit carries the "+" indicator's page and sorts by it; a trailing " overset"
		// (after the page and ordinal) marks it as overset -> e.g. "P1(2) overset".
		hit.locator.Append("P");
		hit.locator.Append(hit.pageString);
		if (hit.pageOrdinal > 0)
		{
			hit.locator.Append("(");
			hit.locator.AppendNumber(hit.pageOrdinal);
			hit.locator.Append(")");
		}
		if (hit.isOverset)
			hit.locator.Append(" overset");
	}

	// What the row cannot show any other way, each separated by a space, in this order:
	//   hidden  - on a switched-off layer, so the page will look empty on arrival
	//   locked  - locked, so the row carries no check box and the replace will not touch it
	//   missing - the same text could not be found where the search left it
	//   refused - InDesign's own replace command would not run there
	// The last two are put there by a replace, or by a jump that finds the text gone, so they never
	// appear on a fresh search's rows. They stack on either shape: "P1(2) overset hidden locked",
	// "overset missing", "P7 hidden".
	//
	// A space, not a "+": InDesign's own overset marker IS a "+", so "P5+locked" reads as "page 5,
	// overset". EVERY word is spelled out in full (user's call, 2026-08-04): these are what explain
	// a row the user cannot act on, so they are worth the characters. Clipped forms were tried and
	// dropped - "hid" / "lck" are hard to read, "loc" reads as "location" in English, and "ov" left
	// the one word a reader most needs to recognise as the least legible of the set.
	if (hit.isHidden)
		hit.locator.Append(" hidden");
	if (hit.isLocked || hit.outcome == kOutcomeLocked)
		hit.locator.Append(" locked");

	// NOT chained onto the test above. A locked row can be jumped to and found changed, and then it
	// has both things to say - "P4(1) locked missing" - where an else left it saying only that it
	// was locked, which is not why the jump landed on different text. Missing and refused do exclude
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

// (DropChapter - erase one chapter and leave the others - was defined here until 2026-08-07. See
// the note where it was declared in KBSResultModel.h.)

int32 KBSResultModel::KeepCheckedRows()
{
	// A replace that was asked for nothing must not empty the panel, so check before touching
	// anything. A row counts as asked about when any of these hold:
	//   replaced - it was changed (its check was cleared when it was written)
	//   outcome  - it was reached and left alone, and says why (its check was cleared then too)
	//   checked  - still selected, so the run never reached it: a chapter that would not open, or
	//              a cancel. Those rows carry no reason, on purpose.
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

			keep.push_back(std::move(hit));
		}
		hits.swap(keep);

		// RENUMBER the within-page ordinals over what is left, so the rows read "the first
		// replacement on this page, the second, the third" (2026-08-03, user's call).
		//
		// The ordinal was CLEARED here until then, on the reasoning that thinning the list leaves the
		// search's numbers full of gaps - which left every row on a page reading a bare "P1", saying
		// nothing at all about which of them it was. Keeping the search's numbers was tried in
		// between; the user asked for the count to follow the REPLACEMENTS rather than the matches
		// they came from, which is this.
		//
		// Same rule and same shape as the search's own numbering (KBSSearchEngine::FinalizeChapterHits):
		// contiguous runs of equal pageIndex are one page, and a page holding ONE row shows no ordinal
		// at all, since there is nothing to tell apart. Thinning preserves the page order the search
		// sorted into, so one pass over each run does it.
		size_t runStart = 0;
		while (runStart < hits.size())
		{
			size_t runEnd = runStart;
			while (runEnd < hits.size() && hits[runEnd].pageIndex == hits[runStart].pageIndex)
				++runEnd;
			const int32 runCount = static_cast<int32>(runEnd - runStart);

			for (size_t k = runStart; k < runEnd; ++k)
			{
				hits[k].pageOrdinal = (runCount > 1) ? (static_cast<int32>(k - runStart) + 1) : 0;
				// Rebuilt here rather than as each row was kept: the flags may have changed too, and
				// this is the one pass that has the final ordinal to bake in.
				BuildHitLocator(hits[k]);
			}
			runStart = runEnd;
		}

		// ***** AND THE FONT GROUPS, because the thinning renumbered the hits they point AT. *****
		// A group holds POSITIONS in the chapter's hits vector (FontGroup::hitIndices), and every
		// hit holds the group it is in and its place inside it - all three of which were true of
		// the vector this pass has just replaced. Left alone, GetFontGroupHit would hand the tree
		// positions that name a different row or none at all, and KBSResultNodeID::Create(chapter,
		// hit) would stamp a stale group onto the node: two nodes naming one hit while carrying
		// different fonts, which is the one thing that header says must never happen.
		//
		// Not reachable today - a chapter only HAS groups when its hits name a font, which only the
		// glyph scan does, and a scan is a report whose rows carry no check box for a replace to be
		// asked about (RowHasCheckBox), so KBSReplaceEngine turns back at its door before it can
		// get here. It is rebuilt anyway because the rule this pass has to keep is "the model is
		// consistent when it returns", not "nothing calls it in the one arrangement we have today".
		BuildFontGroups(gChapters[ci]);
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
