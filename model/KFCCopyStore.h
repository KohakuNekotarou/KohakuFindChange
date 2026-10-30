//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  KFC'S OWN COPY STORES (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md C3 - C5). Before a text
//  row's Replace writes, the text it is about to write over is copied - its objects and formatting with it - into a
//  store of KFC's own, so the row's Restore from Copy can put it back later (KFCReplaceEngine). A store is the scrap
//  database of a data exchange controller of KFC's (kKFCCopyStoreBoss, a CDataExchangeController) - not a document, not
//  the user's clipboard, not in app.documents - ONE PER DOCUMENT (a store shared by many documents once made a copy of
//  a style no document had: docs/ai-notes/private-scrap-store-2026-10-06.md). Copies are never dropped one at a time -
//  an Undo or a Redo can bring back a row that names any of them; a store goes at shutdown, or when its document closes
//  never having been saved (no row can reach that document again).
//  THE COPY ROAD, as measured in KT (memory copy-mechanism-research): ITextUtils' story copies - a new story of the
//  store, the range copied to its end; put back with one command that replaces a range. The ranges are text model
//  positions (a table is 0x16 + 0x17), checked here to lie inside ONE thread, short of its closing character
//  (CopyableThread - a range run across threads crashed InDesign), since the commands check nothing: the story's own
//  text, a table cell's or a footnote's (each measured in KT 2026-10-24 to copy and come back whole - memory
//  copy-mechanism-research; until 2026-10-30 the primary thread alone was taken - the next-overrides-restore spec's C).
//  The main thread only: a row's Replace and its Restore from Copy run there.
//
//========================================================================================

#ifndef __KFCCopyStore_h__
#define __KFCCopyStore_h__

#include "UIDRef.h"

class IDataBase;
class ITextModel;

namespace KFCCopyStore
{
	/** One copy: the store it is in (its number in this session - 0 = none) and its story in the store's database. */
	struct CopyRef
	{
		uint32	store;
		UID		story;
		CopyRef() : store(0), story(kInvalidUID) {}
	};

	/** THE THREAD A COPY MAY BE TAKEN AND PUT BACK IN at `at` (the next-overrides-restore spec's C1): the story's own text
	    (its primary thread), a table cell's or a footnote's - the threads measured; any other (a note's - unmeasured) is
	    not one. True: outFirst = the thread's first position, outLast = its closing character's - a copy and a paste stay
	    in [outFirst, outLast]. False: not one (the outs left as they were). */
	bool CopyableThread(ITextModel* model, TextIndex at, TextIndex& outFirst, TextIndex& outLast);

	/** [start, end) of `story` (a document's text model) copied into a new story of its document's store - made now when
	    the document has none. False = refused or failed, outCopy left empty: an error already standing (no command is
	    processed over one), a range outside one copyable thread (CopyableThread: first <= start <= end <= last), no store
	    could be made, the copy did not come out as long as the range (a test build's fault switch copy-fail too). Writes the
	    store only, never the document; the error state is left as it was found. Outside any command sequence of the
	    document's. */
	bool Take(const UIDRef& story, TextIndex start, TextIndex end, CopyRef& outCopy);

	/** The copy's length - its story's primary thread without the closing return; -1 = not held (no such store or story). */
	int32 Length(const CopyRef& copy);

	/** The copy put over [start, end) of `story` - one command, inside the caller's command sequence: ITextUtils'
	    QueryCopyStoryCommand, or, for a copy with no text, the range deleted (ITextModelCmds::DeleteCmd). False = not held,
	    a range outside one copyable thread (CopyableThread), or the command did not go through (the error state is left for
	    the caller's step to decide by). */
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
