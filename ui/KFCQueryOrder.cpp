//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The query dialog's two lists - see KFCQueryOrder.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IFindChangeOptions.h"	// the five kinds' SearchMode - an order file's "kind" read back

// General includes:
#include "FileUtils.h"			// PMStringToSysFile / SysFileToPMString / DoesFileExist - a query's file
#include "PMString.h"

#include <string>
#include <utility>				// std::swap - Move Up / Move Down
#include <vector>

// Project includes:
#include "KFCDiag.h"				// KFC_DIAG_LOG - the shutdown's line, test builds only
#include "KFCModelAccess.h"		// KFCRuns - the saved queries, a file's description and the kinds' names are the model's
#include "KFCPanelState.h"		// KFCReadFileWhole / KFCWriteFileSafely - KFC's one way to read and write a file of its own
#include "KFCQueryOrder.h"
#include "KFCQueryOrderFile.h"	// the order file's text, both ways

namespace
{
	std::vector<KFCSavedQuery> gSaved;		// the left list
	std::vector<KFCSavedQuery> gOrder;		// the right list - the session's (KFCQueryOrder.h)

	/** "<kind>  <name>" - two spaces, so the kind reads as a column of its own. */
	PMString KindAndName(const KFCSavedQuery& query)
	{
		PMString text;
		text.SetTranslatable(kFalse);
		text.Append(KFCRuns()->QueryKindName(query.mode));
		text.Append("  ");
		text.Append(query.name);
		return text;
	}

	PMString NoText()
	{
		PMString text;
		text.SetTranslatable(kFalse);
		return text;
	}

	bool InOrder(int32 index)
	{
		return index >= 0 && index < static_cast<int32>(gOrder.size());
	}

	PMString FromUtf8(const std::string& utf8)
	{
		PMString s;
		s.SetUTF8String(utf8);
		s.SetTranslatable(kFalse);
		return s;
	}

	// An order file's "kind" as a SearchMode - the model's own names for the five (KFCSavedQueries::KindName); -1 = none.
	// (Object since 1.4.0 - without it an Object entry whose file had moved was never found again by its kind and name:
	// case obj-qd-load-by-name.)
	int32 ModeOfKind(const std::string& kind)
	{
		const int32 modes[] = { IFindChangeOptions::kTextSearch, IFindChangeOptions::kGrepSearch,
			IFindChangeOptions::kGlyphSearch, IFindChangeOptions::kObjectSearch, IFindChangeOptions::kTransliterateSearch };
		for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i)
			if (kind == KFCRuns()->QueryKindName(modes[i]))
				return modes[i];
		return -1;
	}

	// One entry of an order file as a row of the order: its file when that is there; otherwise the saved query of the
	// same kind and name (the user's own before InDesign's - a user's query can share a name with one of InDesign's);
	// otherwise the entry as it was written, not found. gSaved must be filled (LoadSaved).
	KFCSavedQuery RowOfEntry(const KFCOrderFileEntry& entry)
	{
		KFCSavedQuery row;
		if (!entry.file.empty())
		{
			// Its own variable: Describe empties its output before it reads the file it was given.
			const IDFile file = FileUtils::PMStringToSysFile(FromUtf8(entry.file));
			KFCRuns()->DescribeQueryFile(file, row);	// the name and kind its path says, and whether it is there
			if (row.exists)
				return row;
		}
		const PMString name = entry.name.empty() ? row.name : FromUtf8(entry.name);
		const int32 mode = entry.kind.empty() ? row.mode : ModeOfKind(entry.kind);
		const KFCSavedQuery* found = nil;
		for (size_t i = 0; i < gSaved.size(); ++i)
		{
			const KFCSavedQuery& saved = gSaved[i];
			if (!saved.exists || saved.mode != mode || saved.name != name)
				continue;
			if (!saved.bundled)
			{
				found = &saved;
				break;
			}
			if (found == nil)
				found = &saved;
		}
		if (found != nil)
			return *found;
		row.name = name;
		row.mode = mode;
		row.exists = false;
		return row;
	}
}

const std::vector<KFCSavedQuery>& KFCQueryOrder::Saved()
{
	return gSaved;
}

void KFCQueryOrder::LoadSaved()
{
	gSaved.clear();
	KFCRuns()->ListSavedQueries(gSaved);
}

const std::vector<KFCSavedQuery>& KFCQueryOrder::Order()
{
	return gOrder;
}

std::vector<IDFile> KFCQueryOrder::OrderFiles()
{
	std::vector<IDFile> files;
	for (size_t i = 0; i < gOrder.size(); ++i)
		files.push_back(gOrder[i].file);
	return files;
}

void KFCQueryOrder::RefreshOrder()
{
	for (size_t i = 0; i < gOrder.size(); ++i)
	{
		if (FileUtils::DoesFileExist(gOrder[i].file))
		{
			KFCSavedQuery now;
			KFCRuns()->DescribeQueryFile(gOrder[i].file, now);
			gOrder[i] = now;
		}
		else
			gOrder[i].exists = false;		// gone since: its name and kind stay as the row had them
	}
}

bool KFCQueryOrder::SaveOrderTo(const IDFile& file, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::vector<KFCOrderFileEntry> entries;
	for (size_t i = 0; i < gOrder.size(); ++i)
	{
		KFCOrderFileEntry entry;
		entry.kind = KFCRuns()->QueryKindName(gOrder[i].mode);
		entry.name = gOrder[i].name.GetUTF8String();
		entry.file = FileUtils::SysFileToPMString(gOrder[i].file).GetUTF8String();
		entries.push_back(entry);
	}
	const char* failure = KFCWriteFileSafely(file, KFCOrderFileText(entries));
	if (failure == nil)
		return true;
	outWhy.Append(failure);
	return false;
}

bool KFCQueryOrder::LoadOrderFrom(const IDFile& file, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::string text;
	if (!KFCReadFileWhole(file, text))
	{
		outWhy.Append("read");
		return false;
	}
	std::vector<KFCOrderFileEntry> entries;
	if (!KFCOrderFileParse(text, entries))
	{
		outWhy.Append("format");
		return false;
	}
	LoadSaved();		// the queries there are now - an entry whose file has moved is found among them by its kind and name
	std::vector<KFCSavedQuery> order;
	for (size_t i = 0; i < entries.size(); ++i)
		order.push_back(RowOfEntry(entries[i]));
	gOrder.swap(order);
	return true;
}

void KFCQueryOrder::Add(int32 savedIndex)
{
	if (savedIndex >= 0 && savedIndex < static_cast<int32>(gSaved.size()))
		gOrder.push_back(gSaved[savedIndex]);
}

void KFCQueryOrder::Remove(int32 orderIndex)
{
	if (InOrder(orderIndex))
		gOrder.erase(gOrder.begin() + orderIndex);
}

void KFCQueryOrder::MoveUp(int32 orderIndex)
{
	if (InOrder(orderIndex) && orderIndex > 0)
		std::swap(gOrder[orderIndex], gOrder[orderIndex - 1]);
}

void KFCQueryOrder::MoveDown(int32 orderIndex)
{
	if (InOrder(orderIndex) && InOrder(orderIndex + 1))
		std::swap(gOrder[orderIndex], gOrder[orderIndex + 1]);
}

void KFCQueryOrder::Clear()
{
	gOrder.clear();
}

PMString KFCQueryOrder::SavedRowText(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(gSaved.size()))
		return NoText();
	return KindAndName(gSaved[index]);
}

PMString KFCQueryOrder::OrderRowText(int32 index)
{
	if (!InOrder(index))
		return NoText();
	PMString text;
	text.SetTranslatable(kFalse);
	text.AppendNumber(index + 1);
	text.Append("  ");
	text.Append(KindAndName(gOrder[index]));
	if (!gOrder[index].exists)
		text.Append(" (not found)");
	return text;
}

void KFCQueryOrder::ShutdownCleanup()
{
	// Swapped with empty ones, not just cleared: the rows' PMStrings and IDFiles are destroyed now, while InDesign stands,
	// and the storage goes with them (clear() would keep the capacity for the unload to free).
	KFC_DIAG_LOG("QUERYORDER shutdown emptied saved=%d order=%d", static_cast<int>(gSaved.size()), static_cast<int>(gOrder.size()));
	std::vector<KFCSavedQuery>().swap(gSaved);
	std::vector<KFCSavedQuery>().swap(gOrder);
}

// End, KFCQueryOrder.cpp.
