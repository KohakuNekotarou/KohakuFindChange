//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE SCRIPT LABEL KIND, LISTED (docs/superpowers/specs/2026-11-04-kfc-script-label-design.md - the author, 11-04: "list the
//  script labels in the results, and search and replace them by their words", after UserVoice 32098792 "Script Label
//  Manager": "a window, similar to LINKS, that displays ALL script labels in the file, along with a link to where it is in
//  the page - selects the item that's labelled"). While the Kohaku+ tab's Find: is on Script Label (IsTabCurrent), the
//  panel's Find lists the page items of what it searches whose label - the Script Label panel's (the default key) or a key
//  the face names (insertLabel's) - its Find Label: finds: every one when it is empty (S4). A row is an OBJECT row, as the
//  QR Code kind's (KFCResultModel::Hit::itemUID, its place read as an object row's - KFCObjectSearch::ReadItemPlace): going
//  to it selects the item. Its Replace writes the label the face's Change side asks (S7) through the one door InDesign
//  opens to its command - IScriptUtils::SetScriptingTag (IScriptTagCmdData.h: "Use IScriptUtils::SetScriptingTag() ...
//  plug-in developers should not invoke any of the 'Set' APIs in this interface"). A label is read as the DOM's
//  pageItem.label is: IScript::GetTag(key) - not through IScriptLabel, which a page item's boss does not answer for (memory
//  script-label-read-through-iscript). Nothing of Find/Change's is read or touched. The main thread only.
//
//========================================================================================

#ifndef __KFCScriptLabelSearch_h__
#define __KFCScriptLabelSearch_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCScriptLabelConditions, KFCScriptLabelLook, KFCScriptLabelFound
#include "KFCResultModel.h"		// Hit - a row
#include "KFCSearchEngine.h"	// CollectTally - a walk's count

class IDataBase;

namespace KFCScriptLabelSearch
{
	/** IS THE KOHAKU+ TAB ON SCRIPT LABEL NOW, and what it asks (outConditions, when given - the face's as it stands, whichever kind is shown: a row's
	    Replace reads its Change side). Asked of the UI half, which keeps it from the face's own calls (IKFCUIServices::ScriptLabelTabConditions -
	    ui/KFCKohakuTab.h); no UI half = not the Script Label kind. A TEST BUILD asks its fault switch label-tab first: on = the kind, its file's lines
	    key=, find=, change=, case=1, whole=1, scope=all the conditions. */
	bool IsTabCurrent(KFCScriptLabelConditions* outConditions = nil);

	/** The key a search and a write use: the conditions' as typed, or - empty - the Script Label panel's own (IScriptLabel::kDefaultScriptLabelKey). */
	PMString KeyOf(const KFCScriptLabelConditions& asked);

	/** THE ITEM'S LABEL UNDER `key` as the DOM's label / extractLabel(key) reads it (IScript::GetTag) - untranslatable; empty for none, or for an item
	    with no scripting facet. */
	PMString LabelOf(IDataBase* db, UID item, const PMString& key);

	/** DOES THIS LABEL MEET THE CONDITIONS (S4): not empty, and an empty find, or find in it - the whole of it with whole - case by case only with
	    caseSensitive. */
	bool Matches(const PMString& label, const KFCScriptLabelConditions& asked);

	/** WHAT A REPLACE WRITES IN PLACE OF `label` (S7): change as it stands with whole or an empty find; else every find in it (case as Matches compares)
	    swapped for change, left to right, none overlapping. Untranslatable. */
	PMString NewValue(const PMString& label, const KFCScriptLabelConditions& asked);

	/** THE KIND'S WALK OVER A RUN'S TARGETS (KFCQRSearch::CollectTargets' shape): each target's page items whose label meets the conditions, filed a
	    chapter each (KFCSearchEngine::FileChapter: page order, the tally); a book's chapter opened and handed straight back. Cancel is heard between
	    documents. reserved = rows the list holds before the walk; the object lists' limit counts the rest. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCScriptLabelConditions& asked);

	/** One open document's labels again (Search This Document Again): its rows in outHits - at most `limit` (outCapped when there were more).
	    False = no walk, or Cancel was pressed (outCancelled) - the rows are not to be used. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCScriptLabelConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled);

	/** WHAT A ROW DRAWS FOR AN ITEM AND ITS LABEL (S5): "ID<uid>:<the Layers panel's name>  <label>" - the label's line breaks as a pilcrow, its
	    tabs as \t. Untranslatable. */
	PMString RowWords(IDataBase* db, UID item, const PMString& label);

	/** THE FACE'S FIND NEXT (S9): the item after the one found last whose label meets the face's conditions, in the list's own order - the documents of
	    its Search: (the front one, or every open one as All Documents lists them), each in page order; after the last the first again, silently.
	    out.reach = what keeps it from being selected (kKFCQRReach* bits); out.words = the face's first line: "<label> - page <page>". False = none
	    (out.words says so). Read only. */
	bool Next(const KFCScriptLabelLook& look, KFCScriptLabelFound& out);

	/** THE FACE'S CHANGE: the label Find Next found given the face's Change side, in one undoable step. outWords = what it did, or why it did not. */
	bool ChangeFound(const KFCScriptLabelFound& found, PMString& outWords);

	/** THE FACE'S CHANGE ALL: every label of the Search: the conditions find given their Change side, in one undoable step - one already as asked
	    counted, not written. outMatched = the labels found, outChanged = those written. False = no document, or InDesign would not write one:
	    nothing was changed, outWords says so. */
	bool ChangeAll(int32& outChanged, int32& outMatched, PMString& outWords);

	/** A QUERY RUN'S WRITE OF ONE DOCUMENT (KFCKohakuQuery's run): the labels the conditions find, given their Change side, with no rows. outCount =
	    the labels written. False = InDesign would not write one. */
	bool WriteDocument(const UIDRef& docRef, const KFCScriptLabelConditions& asked, int32& outCount);

	/** ONE ITEM'S LABEL WRITTEN inside the caller's step (IScriptUtils::SetScriptingTag - the command's own door). kSuccess = done. */
	ErrorCode Write(IDataBase* db, UID item, const PMString& key, const PMString& value);
}

#endif // __KFCScriptLabelSearch_h__

// End, KFCScriptLabelSearch.h.
