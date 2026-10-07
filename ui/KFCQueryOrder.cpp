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

// General includes:
#include "FileUtils.h"			// PMStringToSysFile / SysFileToPMString - a line of the file and the query's file
#include "PMString.h"

#include <string>
#include <utility>				// std::swap - Move Up / Move Down
#include <vector>

// Project includes:
#include "KFCDiag.h"				// KFC_DIAG_LOG - the shutdown's line, test builds only
#include "KFCModelAccess.h"		// KFCRuns - the saved queries, a file's description and the kinds' names are the model's
#include "KFCPanelState.h"		// KFCReadOwnFile / KFCWriteOwnFile - KFC's one way to read and write a file of its own
#include "KFCQueryOrder.h"

namespace
{
	// The run order's file and the side file every write goes through first (InDesign's roaming folder, next to
	// KBSPanelState.json - no sub-folder is made: the author's rule).
	const char* const kOrderFileName = "KFCQueryOrder.txt";
	const char* const kOrderSideFileName = "KFCQueryOrder.txt.tmp";

	std::vector<KFCSavedQuery> gSaved;		// the left list
	std::vector<KFCSavedQuery> gOrder;		// the right list

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

bool KFCQueryOrder::LoadOrder(PMString& outWhy)
{
	gOrder.clear();
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::string bytes;
	if (!KFCReadOwnFile(kOrderFileName, bytes))
	{
		outWhy.Append("read");
		return false;
	}
	// A BOM is passed over and a CR dropped, so a file written elsewhere with either reads the same; a blank line and a
	// "#" line are not paths. Every other line is a query's file as its path says - one that is not there stays in the
	// order, "(not found)", rather than being dropped behind the person's back.
	size_t start = 0;
	if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB
		&& static_cast<unsigned char>(bytes[2]) == 0xBF)
		start = 3;
	std::string line;
	for (size_t i = start; i <= bytes.size(); ++i)
	{
		const char ch = (i < bytes.size()) ? bytes[i] : '\n';
		if (ch == '\r')
			continue;
		if (ch != '\n')
		{
			line += ch;
			continue;
		}
		if (!line.empty() && line[0] != '#')
		{
			PMString path;
			path.SetUTF8String(line);
			path.SetTranslatable(kFalse);
			KFCSavedQuery query;
			KFCRuns()->DescribeQueryFile(FileUtils::PMStringToSysFile(path), query);
			gOrder.push_back(query);
		}
		line.clear();
	}
	return true;
}

bool KFCQueryOrder::SaveOrder(PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::string text;
	for (size_t i = 0; i < gOrder.size(); ++i)
	{
		text += FileUtils::SysFileToPMString(gOrder[i].file).GetUTF8String();
		text += "\r\n";
	}
	const char* failure = KFCWriteOwnFile(kOrderFileName, kOrderSideFileName, text);
	if (failure == nil)
		return true;
	outWhy.Append(failure);
	return false;
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
