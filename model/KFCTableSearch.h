//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE TABLE TAB, LISTED (docs/superpowers/specs/2026-10-31-kfc-table-tab-design.md). InDesign's Find/Change has no condition for a
//  table style. While KFC's own tab in Edit > Find/Change - "Table" - is the tab Find/Change is on (IsTabCurrent; the panel's
//  title, its Find and CurrentSearchScope follow it, as they do the QR Code tab), the panel's Find lists the tables of what it
//  searches: by the style's name (a book's chapters each have their own styles), with overrides or not. A row is a TEXT row
//  standing on the table's own character (ITableTextContainer's anchor): going to it, its page and flags, following an
//  edit, Copy and Clear from Results are every text row's. What it draws for that character is its size and its style
//  (Hit::runChain - "3x4 TS1 +"). Its Replace gives its table the style the tab's Change side names, or clears its overrides
//  (KFCReplaceEngine's WriteOwnTabRows - the facade's ApplyTableStyle, ClearLocalOverrides). Read only here, and no setting of
//  Find/Change's is read or touched. The main thread only.
//
//========================================================================================

#ifndef __KFCTableSearch_h__
#define __KFCTableSearch_h__

#include "PMString.h"
#include "UIDRef.h"

#include <map>
#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCTableConditions
#include "KFCResultModel.h"		// Hit - a row
#include "KFCSearchEngine.h"	// CollectTally - a walk's count

class ITableModel;

namespace KFCTableSearch
{
	/** IS THE TABLE TAB FIND/CHANGE'S TAB NOW, and what it asks (outConditions, when given - the tab's as it stands, whichever tab is
	    shown: a row's Replace reads its Change side). Asked of the UI half, which keeps it from the face's own calls
	    (IKFCUIServices::TableTabConditions - ui/KFCKohakuTab.h); no UI half = not the Table tab. A TEST BUILD asks its fault switch
	    table-tab first: on = the Table tab, its file's lines find=, overrides=1, change=, clear=1, scope=all|story|toend|selection the conditions. */
	bool IsTabCurrent(KFCTableConditions* outConditions = nil);

	/** THE TAB'S WALK OVER A RUN'S TARGETS (the QR Code tab's shape - KFCQRSearch::CollectTargets): each target's tables that meet the
	    conditions, filed a chapter each (KFCSearchEngine::FileChapter); a book's chapter opened and handed straight back. part (Search: Story, To End of Story, Selection - KFCSearchEngine::PartStretches):
	    the tables whose character stands in its stretches, or that a stretch stands in - a text cursor or a selection in a cell; nil = the whole of each target. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const PMString& findStyle, bool overridesOnly, const std::vector<KFCSearchEngine::ListedPlace>* part = nil);

	/** One document's tables again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const PMString& findStyle, bool overridesOnly,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** THE TABLE WHOSE CHARACTER STANDS AT `at` of the story (AddRef'd), or nil. */
	ITableModel* QueryTableAt(const UIDRef& story, TextIndex at);

	/** A QUERY RUN'S WRITE OF ONE DOCUMENT (KFCKohakuQuery::WriteDocument): the tables the conditions list, given their Change side, with no rows. outCount = the tables written. False = InDesign would not write one. */
	bool WriteDocument(const UIDRef& docRef, const KFCTableConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount);

	/** THE PART'S FIND NEXT (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md F3, F4): the next table of its Search: in the list's own order - document by document, each in page order - walked anew at every press, what was added or taken away met. From
	    where (InDesign's Text tab, measured): the selection as the last landing left it = the table after that one; moved = the first after it in its story, else the one after its story's last table, else - no table in that story, or no text selected - the one after the table
	    found last; and after the last the first, silently. Story, To End of Story and Selection are read when the selection has moved and kept while it has not: a landing moves the selection onto a table, and read again they would shrink to it. A Search: the selection does not
	    offer goes over the document (out.note says so, the Text tab's words). Read only; Book Scope is not asked (QR Code's Find Next's rule: the dialog's buttons know no book). False = no table: out.words empty, or why it did not look. */
	bool Next(const KFCTableLook& look, KFCTableFound& out);

	/** THE PART'S CHANGE: the table Find Next found given its Change side, in one undoable step (KFCReplaceEngine::WriteFaceTables). outWords = what it did, or why it did not. */
	bool ChangeFound(const KFCTableFound& found, PMString& outWords);

	/** THE PART'S CHANGE ALL: every table of the Search: (Next's range) given it, in one undoable step - a locked one, one that already reads so and one whose document has no such style left, and counted in outWords. outMatched = the tables the conditions found, outChanged = those written.
	    False = no document, or InDesign would not write one: nothing was changed, outWords says so. */
	bool ChangeAll(const KFCTableLook& look, int32& outChanged, int32& outMatched, PMString& outWords);

	/** What a row draws for a table: "<rows>x<columns> <style>", " +" for overrides of its own. */
	PMString TableWords(ITableModel* table);

	/** The style of this full path in the document (its table style group manager), kInvalidUID for none. */
	UID StyleNamed(IDataBase* db, const PMString& path);

	/** Would Write change the table? False = it already reads as asked: another style than it has, or overrides to clear. */
	bool WouldChange(ITableModel* table, UID style, bool clearOverrides);

	/** A table's write inside the caller's step: `style` given it, its overrides cleared - or, with style kInvalidUID, its overrides
	    cleared alone. kSuccess = done. */
	ErrorCode Write(ITableModel* table, UID style, bool clearOverrides);
}

#endif // __KFCTableSearch_h__

// End, KFCTableSearch.h.
