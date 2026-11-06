//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A SAVED QUERY OF THE KOHAKU+ TAB (the author, 10-31: pick it in Find/Change's own Query menu, and its tab opens and is set - docs/superpowers/specs/2026-10-31-kfc-kohaku-tab-design.md section 5): what the
//  QR Code, Table, Ruby or Kenten kind asks, kept in a query file of InDesign's own shape where InDesign's Query menu lists it.
//  THE FILE: <roaming>\Find-Change Queries\<Kind> - <Name>.xml. InDesign's menu lists every .xml of its kinds' folders, a group each, and of that folder itself, the last group, above [Custom] - read from its panel plug-in,
//  docs/ai-notes/kfc-native-query-menu-2026-10-31.md - and no other (a new folder, a new extension, a new type: not listed). So KFC's stand together, and no folder of KFC's is made (the author's rule, KFCPanelState.h).
//  The file is a query of the Color tab asking nothing, with one element more: InDesign's readers ask for the elements they know, so with no KFC installed the entry loads an empty query and harms nothing.
//  Its Find states it on the Kohaku+ tab and lists; a query run writes it with no rows (WriteDocument - KFCQuerySequence), a document at a time.
//
//========================================================================================

#ifndef __KFCKohakuQuery_h__
#define __KFCKohakuQuery_h__

#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include <set>
#include <string>
#include <vector>

#include "KFCModelTypes.h"		// KFCKohakuQueryData
#include "KFCSearchEngine.h"	// ListedPlace

namespace KFCKohakuQuery
{
	/** The file's conditions. False = it is not there, or is no query of KFC's (a query of InDesign's: most files this is asked of). */
	bool Read(const IDFile& file, KFCKohakuQueryData& out);

	/** The whole of a query's file, as UTF-8 - empty when one of its words holds half of a surrogate pair (KFCHasLoneSurrogate, KFCUtf8.h - a box's
	    paste): no UTF-8 is made of it, and no file is to be written (Save Query says so - IKFCRuns::KohakuQueryText). */
	std::string FileText(const KFCKohakuQueryData& query);

	/** "<Kind> - <name>": the file's name without .xml, and the entry InDesign's menu shows. */
	PMString EntryName(int32 mode, const PMString& name);

	/** The folder they are kept in - InDesign's queries' own, which it makes as it lists. False = it cannot be had. */
	bool FolderOf(IDFile& out);

	/** The file a query of this kind and name is kept in. False = InDesign's queries' folder cannot be had. */
	bool FileOf(int32 mode, const PMString& name, IDFile& out);

	/** Is this file in the folder a query of KFC's is kept in? (Asked before a file is read for one: its kind's folder says what any other file is.) */
	bool IsInItsFolder(const IDFile& file);

	/** Does it write anything? (A query with nothing on its Change side is searched by a run, not written - FindDocument; until 2026-11-06 it was skipped,
	    "nothing to change to".) */
	bool HasChangeSide(const KFCKohakuQueryData& query);

	/** THE RUN'S SEARCH OF ONE DOCUMENT for a query that changes nothing (2026-11-06 - docs/superpowers/specs/2026-11-06-kfc-run-search-only-queries-design.md RF2): the
	    document walked as its kind's own search walks one (WalkOneDocument - Search This Document Again's door), with the query's conditions, up to
	    `limit` rows, into outHits. QR Code, Table, Ruby, Kenten, Warichu, Script Label, Story and Spelling; false for another kind, or a walk that stopped
	    (outCancelled = its Cancel). The rows are the document's whole - the caller keeps the part a run over part of it asked for. */
	bool FindDocument(const UIDRef& docRef, const PMString& name, const KFCKohakuQueryData& query, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
		bool& outCancelled);

	/** THE RUN'S WRITE OF ONE DOCUMENT: what the query's kind lists - of part alone, given one (KFCSearchEngine::PartStretches) - given its Change side, a locked one and one that already reads so left. Inside
	    the caller's command sequence. outCount = what was written. False = InDesign would not write one. ioBookSeen: the query's one set over a book's documents - a ruby list's First in Book (KFCRubySearch::WriteDocument). */
	bool WriteDocument(const UIDRef& docRef, const KFCKohakuQueryData& query, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount, std::set<size_t>* ioBookSeen = nil);
}

#endif // __KFCKohakuQuery_h__

// End, KFCKohakuQuery.h.
