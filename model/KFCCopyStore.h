//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  KFC'S OWN COPY STORES (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md C3 - C5). Before a text
//  row's Replace writes, the text it is about to write over is copied - its objects and formatting with it - into a
//  store of KFC's own, so the row's Restore Original can put it back later (KFCReplaceEngine). A store is the scrap
//  database of a data exchange controller of KFC's (kKFCCopyStoreBoss, a CDataExchangeController) - not a document, not
//  the user's clipboard, not in app.documents - ONE PER DOCUMENT (a store shared by many documents once made a copy of
//  a style no document had: docs/ai-notes/private-scrap-store-2026-10-06.md). Copies are never dropped one at a time -
//  an Undo or a Redo can bring back a row that names any of them; a store goes at shutdown, or when its document closes
//  never having been saved (no row can reach that document again).
//  THE COPY ROAD, as measured in KT (memory copy-mechanism-research): ITextUtils' story copies - a new story of the
//  store, the range copied to its end; put back with one command that replaces a range. The ranges are text model
//  positions (a table is 0x16 + 0x17), checked here against the story's PRIMARY thread (GetPrimaryStoryThreadSpan - a
//  range into a table cell's or a footnote's thread crashed InDesign), since the commands check nothing.
//  The main thread only: a row's Replace and its Restore Original run there.
//
//========================================================================================

#ifndef __KFCCopyStore_h__
#define __KFCCopyStore_h__

#include "UIDRef.h"

class IDataBase;

namespace KFCCopyStore
{
	/** One copy: the store it is in (its number in this session - 0 = none) and its story in the store's database. */
	struct CopyRef
	{
		uint32	store;
		UID		story;
		CopyRef() : store(0), story(kInvalidUID) {}
	};

	/** [start, end) of `story` (a document's text model) copied into a new story of its document's store - made now when
	    the document has none. False = refused or failed, outCopy left empty: an error already standing (no command is
	    processed over one), a range outside the story's primary thread (0 <= start <= end <= span - 1), no store could
	    be made, the copy did not come out as long as the range (a test build's fault switch copy-fail too). Writes the
	    store only, never the document; the error state is left as it was found. Outside any command sequence of the
	    document's. */
	bool Take(const UIDRef& story, TextIndex start, TextIndex end, CopyRef& outCopy);

	/** The copy's length - its story's primary thread without the closing return; -1 = not held (no such store or story). */
	int32 Length(const CopyRef& copy);

	/** The copy put over [start, end) of `story` - one command, inside the caller's command sequence: ITextUtils'
	    QueryCopyStoryCommand, or, for a copy with no text, the range deleted (ITextModelCmds::DeleteCmd). False = not held,
	    a range outside the primary thread, or the command did not go through (the error state is left for the caller's
	    step to decide by). */
	bool PasteOver(const CopyRef& copy, const UIDRef& story, TextIndex start, TextIndex end);

	/** A DOCUMENT IS CLOSING (KFCCloseDocResponder): its store lets go of the address - one of a document never saved is
	    let go whole; one of a saved document keeps the file it is found again by when the document opens again (a book's
	    chapter, whose rows stay). The address is compared, never followed. */
	void DocumentClosing(IDataBase* db);

	/** The controlled shutdown (KFCStartupShutdown): every store let go - Cleanup, then Release - while the session is
	    whole. */
	void ShutdownCleanup();
}

#endif // __KFCCopyStore_h__

// End, KFCCopyStore.h.
