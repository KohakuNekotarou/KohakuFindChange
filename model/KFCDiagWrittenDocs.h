//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE WRITTEN-DOCUMENT CHECK (test builds only - the author, after a Replace's chapter was closed with
//  the Replace in it: "check that the documents a replace wrote are still open - by name or ID, not by count: the user
//  has documents of their own open"). Every document a write of KFC's went into is listed with its name and its file
//  (NoteWrite), and every close KFC asks InDesign for is noted (NoteCloseAsked). Then, in the test build's trace
//  (KFCDiag.h):
//
//      WRITTENDOCS LOST <name> by=kfc ...      a close KFC asked for found work unsaved in the document - thrown away
//                                              (KFC asks to close a document only when nothing in it is unsaved, so
//                                              something was written between the asking and the close - the shape of the
//                                              Replace whose chapter closed with it)
//      WRITTENDOCS CLEANED <name> at=<where>   a document KFC wrote reads "nothing unsaved" although its file has not
//                                              been written since: its "unsaved" flag was taken down over the write, so
//                                              any close - the user's included - throws the write away without asking.
//                                              NOT when the write was UNDONE (fewer undo steps and more redo steps than
//                                              just after it - ICmdHistory; fewer undo steps alone is also what
//                                              InDesign's memory purge leaves): an Undo back to the save point takes the
//                                              flag down as well, rightly, the document then being as its file is
//                                              (measured: qs-book-undo) - that close is "closed ... undone=1"
//      WRITTENDOCS <where> docs=<n> [<name>|unsaved, undone or CLEAN] ...   at the end of every write: each document
//                                              KFC wrote that is still open and not saved since, by name
//      WRITTENDOCS closed <name> by=<kfc|other> ...                 a written document closing: the user's or a
//                                              script's own close is their choice, and is said, not failed
//
//  work\kbs-regress\run.ps1 reads the lines of each case and judge.py fails the case on LOST or CLEANED (WRITTEN-LOST /
//  WRITTEN-CLEANED), as it fails a hidden document left behind (KFCDiagHiddenDocs, the UI half).
//
//  A document is told by its database and UID while it is open (a closed one is taken off the list at its close, so an
//  address handed to the next document opened is never matched), and NAMED by its name and file in every line. "Saved
//  since" = its file is another, or was written after the write (FileUtils::GetModificationDate - in seconds, so a
//  save in the same second as the write before it can only read as a CLEANED that is not; ReadDoc says why).
//
//  In a shipping build every macro below is nothing, and so is the .cpp.
//
//========================================================================================

#ifndef __KFCDiagWrittenDocs_h__
#define __KFCDiagWrittenDocs_h__

#ifdef KFC_DIAG

#include "UIDRef.h"

namespace KFCDiagWrittenDocs
{
	/** A write of KFC's went through into this document (its step ended, not rolled back). */
	void NoteWrite(const UIDRef& docRef);

	/** KFC asks InDesign to close this document (KFCBookScope - right before IDocFileHandler::Close, scheduled or now). */
	void NoteCloseAsked(const UIDRef& docRef);

	/** This document is closing (KFCCloseDocResponder - kBeforeCloseDoc, on the main thread: the document is still whole). */
	void Closing(const UIDRef& docRef);

	/** The check at the end of a write: declared first in the write's function, so it runs when everything after the write
		- the chapters' windows, their flags put back, the hand-backs - is done. One inside another is checked with the outer
		one. 'where' names the write in the trace (a string literal: kept, not copied). */
	class Check
	{
	public:
		explicit Check(const char* where);
		~Check();
	private:
		const char* fWhere;
		Check(const Check&);
		Check& operator=(const Check&);
	};
}

#define KFC_DIAG_NOTE_WRITE(docRef) KFCDiagWrittenDocs::NoteWrite(docRef)
#define KFC_DIAG_NOTE_CLOSE_ASKED(docRef) KFCDiagWrittenDocs::NoteCloseAsked(docRef)
#define KFC_DIAG_DOC_CLOSING(docRef) KFCDiagWrittenDocs::Closing(docRef)
#define KFC_DIAG_WRITTEN_DOCS_CHECK(where) const KFCDiagWrittenDocs::Check kfcDiagWrittenDocsCheck(where)

#else

#define KFC_DIAG_NOTE_WRITE(docRef) ((void)0)
#define KFC_DIAG_NOTE_CLOSE_ASKED(docRef) ((void)0)
#define KFC_DIAG_DOC_CLOSING(docRef) ((void)0)
#define KFC_DIAG_WRITTEN_DOCS_CHECK(where) ((void)0)

#endif // KFC_DIAG

#endif // __KFCDiagWrittenDocs_h__

// End, KFCDiagWrittenDocs.h.
