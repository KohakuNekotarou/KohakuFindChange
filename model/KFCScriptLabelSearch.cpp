//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Script Label kind, listed and written - see KFCScriptLabelSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScriptLabel.h"				// kDefaultScriptLabelKey - the Script Label panel's own key

// Project includes:
#include "KFCScriptLabelSearch.h"

bool KFCScriptLabelSearch::IsTabCurrent(KFCScriptLabelConditions* outConditions)
{
	if (outConditions != nil)
		*outConditions = KFCScriptLabelConditions();
	return false;
}

PMString KFCScriptLabelSearch::KeyOf(const KFCScriptLabelConditions& asked)
{
	PMString key(asked.key.IsEmpty() ? IScriptLabel::kDefaultScriptLabelKey : asked.key);
	key.SetTranslatable(kFalse);
	return key;
}

PMString KFCScriptLabelSearch::LabelOf(IDataBase* db, UID item, const PMString& key)
{
	(void)db;
	(void)item;
	(void)key;
	PMString none;
	none.SetTranslatable(kFalse);
	return none;
}

bool KFCScriptLabelSearch::Matches(const PMString& label, const KFCScriptLabelConditions& asked)
{
	(void)label;
	(void)asked;
	return false;
}

PMString KFCScriptLabelSearch::NewValue(const PMString& label, const KFCScriptLabelConditions& asked)
{
	(void)asked;
	PMString same(label);
	same.SetTranslatable(kFalse);
	return same;
}

void KFCScriptLabelSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCScriptLabelConditions& asked)
{
	(void)targets;
	(void)fromBook;
	(void)unopenable;
	(void)reserved;
	(void)asked;
	out = KFCSearchEngine::CollectTally();
}

bool KFCScriptLabelSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCScriptLabelConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled)
{
	(void)docRef;
	(void)name;
	(void)limit;
	(void)asked;
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	return false;
}

PMString KFCScriptLabelSearch::RowWords(IDataBase* db, UID item, const PMString& label)
{
	(void)db;
	(void)item;
	PMString words(label);
	words.SetTranslatable(kFalse);
	return words;
}

bool KFCScriptLabelSearch::Next(const KFCScriptLabelLook& look, KFCScriptLabelFound& out)
{
	(void)look;
	out = KFCScriptLabelFound();
	return false;
}

bool KFCScriptLabelSearch::ChangeFound(const KFCScriptLabelFound& found, PMString& outWords)
{
	(void)found;
	outWords.SetTranslatable(kFalse);
	return false;
}

bool KFCScriptLabelSearch::ChangeAll(int32& outChanged, int32& outMatched, PMString& outWords)
{
	outChanged = 0;
	outMatched = 0;
	outWords.SetTranslatable(kFalse);
	return false;
}

bool KFCScriptLabelSearch::WriteDocument(const UIDRef& docRef, const KFCScriptLabelConditions& asked, int32& outCount)
{
	(void)docRef;
	(void)asked;
	outCount = 0;
	return false;
}

ErrorCode KFCScriptLabelSearch::Write(IDataBase* db, UID item, const PMString& key, const PMString& value)
{
	(void)db;
	(void)item;
	(void)key;
	(void)value;
	return kFailure;
}

// End, KFCScriptLabelSearch.cpp.
