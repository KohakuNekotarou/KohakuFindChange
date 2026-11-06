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
//  panel's Find lists the page items of what it searches whose label - a key the face's Script Key: names (insertLabel's), or with
//  none named every key's: the Script Label panel's (the default key) and each insertLabel key's (IScriptLabel::GetTags - the author,
//  11-04) - its Find Label: finds: every one when it is empty (S4); as GREP too (ECMAScript's, std::wregex - the author, 11-04). A
//  row is a label's: an item with labels under several keys has a row for each (Hit::labelKey). A row is an OBJECT row, as the
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
#include "TableTypes.h"			// GridAddress - a cell holder's
#include "UIDRef.h"

#include <map>
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
	    key=, find=, change=, whole=1, grep=1, scope=all the conditions (case=1 went 11-04 with the face's Case Sensitive: always case by case). */
	bool IsTabCurrent(KFCScriptLabelConditions* outConditions = nil);

	/** A write's key when no row or found item names one: the conditions' as typed, or - empty - the Script Label panel's own (IScriptLabel::kDefaultScriptLabelKey).
	    (A search with no key asked reads every key - a row's own key is Hit::labelKey, a found item's KFCScriptLabelFound::key.) */
	PMString KeyOf(const KFCScriptLabelConditions& asked);

	/** THE ITEM'S LABEL UNDER `key` as the DOM's label / extractLabel(key) reads it (IScript::GetTag) - untranslatable; empty for none, or for an item
	    with no scripting facet. */
	PMString LabelOf(IDataBase* db, UID item, const PMString& key);

	/** DOES THIS LABEL MEET THE CONDITIONS (S4): not empty, and an empty find, or find in it - the whole of it with whole - case by case with
	    caseSensitive (always, since 11-04). With grep: find a regular expression found in it (regex_search) - all of it with whole (regex_match; the face's
	    Match: asks one of the two since 11-04 - a query of that day's earlier builds may still ask both); one that is no regular expression matches nothing
	    (GrepIsValid says so). */
	bool Matches(const PMString& label, const KFCScriptLabelConditions& asked);

	/** IS FIND LABEL: A REGULAR EXPRESSION THIS KIND CAN READ (with grep - always true without it, or with no find)? False: outWhy the sentence to say -
	    std::regex_error's words in it - and nothing to search, list or write. */
	bool GrepIsValid(const KFCScriptLabelConditions& asked, PMString& outWhy);

	/** WHAT A REPLACE WRITES IN PLACE OF `label` (S7): change as it stands with an empty find, or with whole but no grep; with grep every match given
	    change, its $1, $& and $$ read as ECMAScript reads them (the whole label the match with whole); else every find in it (case as Matches compares)
	    swapped for change, left to right, none overlapping. Untranslatable. */
	PMString NewValue(const PMString& label, const KFCScriptLabelConditions& asked);

	/** THE KIND'S WALK OVER A RUN'S TARGETS (KFCQRSearch::CollectTargets' shape): each target's page items whose label meets the conditions, filed a
	    chapter each (KFCSearchEngine::FileChapter: page order, the tally); a book's chapter opened and handed straight back. Cancel is heard between
	    documents. reserved = rows the list holds before the walk; the object lists' limit counts the rest. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCScriptLabelConditions& asked);

	/** One open document's labels again (Search This Document Again): its rows in outHits - at most `limit` (outCapped when there were more) - the table and cell rows'
	    stories' versions in outVersions (text rows). False = no walk, or Cancel was pressed (outCancelled) - the rows are not to be used. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCScriptLabelConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** WHAT A ROW DRAWS FOR AN ITEM AND ITS LABEL (S5): "ID<uid>:<the Layers panel's name>  <label>" - the label as it stands, cut where a row's text
	    is kept short; the panel draws its line breaks as a pilcrow and its tabs as KFC's other rows draw them. Untranslatable. */
	PMString RowWords(IDataBase* db, UID item, const PMString& key, const PMString& label);

	/** THE FACE'S FIND NEXT (S9): the item after the one found last whose label meets the face's conditions, in the list's own order - the documents of
	    its Search: (the front one, or every open one as All Documents lists them), each in page order; after the last the first again, silently. The one
	    found last is a PLACE: its label written since (Change, Change/Find) so that it no longer matches, the next is still the one after it, not the
	    first. out.reach = what keeps it from being selected (kKFCQRReach* bits); out.words = the face's first line, QR Code's shape: "<name>  <label>
	    - page <page>" (" - <document>" over All Documents). False = none (out.words says why, or is empty: InDesign's own Cannot find a match.). Read only. */
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

	/** A LIST'S ROW OF AN ITEM WHOSE LABEL WAS JUST WRITTEN (`value`), marked inside the write's step: Labeled and counted (kKFCSpecialScriptLabel), its words the label it has
	    now, its place and fingerprint read after the write - an object row's marks (KFCResultModel::MarkItemOwnChange), backed up for the step's Undo. Every label writer that
	    holds a row calls it: a row's Replace, the face's Change and Change All. */
	void MarkRowWritten(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID item, const PMString& key, const PMString& value);

	/** THE LABEL OF THE ITEM THE FACE'S FIND NEXT FOUND, AS IT READS NOW (found.key's - IKFCRuns::ScriptLabelNow: the face's box of it, on several lines - the author,
	    11-04). Empty: nothing found, or its document or item is gone. Read only. */
	PMString LabelNow(const KFCScriptLabelFound& found);

	/** IS THE ITEM LOCKED NOW - its own lock or its layer's (KFCPageItemFacts, an object row's "locked")? Every writer of a label asks it at the write
	    (a row's Replace, the face's Change and Change All, a query's run - S7: a locked item's label is not written), so a lock set or taken off since
	    the search or Find Next counts as it stands - as the Table part's and its rows' writers read a frame's lock at the write. The one place S7's
	    lock rule is kept. */
	bool IsLockedNow(IDataBase* db, UID item);

	/** A LABEL'S HOLDER - what a row of a script label list stands on (docs/superpowers/specs/2026-11-05-kfc-script-label-tables-cells-design.md L1, L2): a page item (kind
	    kKFCHolderNone, item), a table (kKFCHolderTable - its labels on the scripting object the DOM makes for it: TableScript) or a cell (kKFCHolderCell -
	    table and the cell's top-left; the cell attribute kCellAttrScriptLabelBoss). */
	struct Holder
	{
		int32		kind;
		UID			item;
		UID			table;
		GridAddress	cell;
		Holder() : kind(kKFCHolderNone), item(kInvalidUID), table(kInvalidUID) {}
	};

	/** The holder a TEXT row of a script label list stands on now (its kind - Hit::holderKind - and its place, followed): a table's row, the table whose character is at
	    `start` (KFCTableSearch::QueryTableAt); a cell's row, the cell its first character is in (KFCTableRows::TableCellAt). False = nothing of that kind there now. */
	bool HolderAt(IDataBase* db, int32 kind, UID story, TextIndex start, Holder& out);

	/** The holder's label under `key` (an item's: LabelOf's) - untranslatable; empty for none. */
	PMString HolderLabel(IDataBase* db, const Holder& holder, const PMString& key);

	/** EVERY LABEL OF THE HOLDER, (key, label), none empty - the Script Label panel's first, then each key a script put one under (docs/superpowers/specs/2026-11-06-
	    kfc-label-spelling-runscript-design.md LB4 - Special Operations > Label's keys). Untranslatable. Empty for none, or for no such holder. */
	void HolderLabels(IDataBase* db, const Holder& holder, std::vector<std::pair<PMString, PMString> >& out);

	/** Is `key` the Script Label panel's own (IScriptLabel::kDefaultScriptLabelKey)? */
	bool IsPanelLabelKey(const PMString& key);

	/** ONE HOLDER'S LABEL WRITTEN inside the caller's step - an empty value takes the key away (insertLabel(key, "")): an item's (Write); a cell's by its label attribute - its tags
	    now, that key set - handed to ITableCommands::ApplyCellOverrides; a table's by IScriptUtils::SetScriptingTag on its scripting object, made with the table's
	    story for its parent (one made without took InDesign down - memory table-label-write-crash-and-cell-label-write). Read back after. kSuccess = done. */
	ErrorCode WriteHolder(IDataBase* db, const Holder& holder, const PMString& key, const PMString& value);

	/** WHAT A TABLE'S OR A CELL'S LABEL ROW DRAWS (L1, L2): "Table 5x2  <key>: <label>" / "Cell r2 c1  <key>: <label>" (the panel's key not written - an item's row's rule; the
	    label cut as an item's row cuts it). Untranslatable. */
	PMString HolderRowWords(IDataBase* db, const Holder& holder, const PMString& key, const PMString& label);

	/** IS A TABLE'S OR A CELL'S LABEL ROW LOCKED NOW - its place's story, frame or layer (a text row's lock: KFCSearchEngine::IsFrameEditable), read at the write. */
	bool HolderLockedNow(IDataBase* db, UID story, TextIndex start);

	/** A LIST'S ROW WHOSE LABEL OF `key` SPECIAL OPERATIONS > REMOVE LABEL JUST TOOK AWAY (the table-cells spec's L8b), marked inside the write's step: "Removed"
	    (kKFCSpecialRemoveLabel - a Special Operation's word, NOT counted as the tab's write), its words read again with no label - an item's row MarkRowWritten's way, a
	    cell's as a text row's own change (KFCResultModel::MarkHitOwnChange). */
	void MarkRowRemoved(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, const Holder& holder, const PMString& key);

	/** EVERY LABEL OF A TABLE AND OF ITS CELLS as one string - the table's "key=value;" pairs, then each cell's after "@row,col:" - for the table's print
	    (KFCObjectSearch::Fingerprint): a label written on a table or a cell moves no story, so its Undo is followed by the table's print (KFCUndoFollow's
	    RecordItem - 11-05: a cell's Remove Label went unfollowed by Undo). Untranslatable; empty for none. */
	PMString TableLabelsWords(IDataBase* db, UID table);

	/** WHY AN OBJECT ROW'S OTHER WRITES REFUSE A SCRIPT LABEL LIST'S ROWS (Special Operations, Create Outlines, InDesign's object replace - the doors
	    a press that never went through the menu meets; the menu greys them): a row's Replace writes its label alone. */
	const char* OnlyReplaceWords();
}

#endif // __KFCScriptLabelSearch_h__

// End, KFCScriptLabelSearch.h.
