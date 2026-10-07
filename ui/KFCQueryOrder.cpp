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
#include "PMString.h"

#include <vector>

// Project includes:
#include "KFCModelAccess.h"		// KFCRuns - the saved queries and their kinds' names are the model's
#include "KFCQueryOrder.h"

namespace
{
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

PMString KFCQueryOrder::SavedRowText(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(gSaved.size()))
		return NoText();
	return KindAndName(gSaved[index]);
}

PMString KFCQueryOrder::OrderRowText(int32 index)
{
	if (index < 0 || index >= static_cast<int32>(gOrder.size()))
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

// End, KFCQueryOrder.cpp.
