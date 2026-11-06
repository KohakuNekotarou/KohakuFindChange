//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A FIND: DOCUMENT ROW'S SPECIAL OPERATIONS AND THE DOCUMENT FACE'S OPEN HIDDEN... (the author, 2026-11-06: "Special Operations: Close" - "Show, for a hidden
//  document; grey for a shown one" - "open a document without a window, in that tab" - and of the ideas offered, Save;
//  docs/superpowers/specs/2026-11-06-kfc-windowless-documents-design.md W3). Each works on the documents of the rows - the row right-clicked, or the rows
//  selected together with it - each document once, found open by its file (IKFCRuns::ChapterDocNow - never the address the results hold).
//    Show   - a layout window on a document that has none (IKFCUIServices::OpenLayoutWindow - the window the jump opens); grey over one with a layout window.
//    Close  - IDocFileHandler::Close (kFullUI: a document holding changes asks InDesign's own "save?"; scheduled - the official way from a UI's call). Its rows
//             go off the list as it closes (LIFE-01). No undo: a close is none.
//    Save   - IDocFileHandler::Save (grey where its CanSave says no; a document never saved asks for a file - InDesign's own dialog).
//  (Revert was the fourth, the night it was made: taken out - the author: "no Revert". InDesign's own revert opens the document again with a window,
//  and its row goes - measured, docs/ai-notes/kfc-windowless-recheck-2026-11-06.md section 2.)
//  Open Hidden... - InDesign documents chosen (SDKFileOpenChooser) opened WITHOUT a window (IDocumentCommands::Open showInWindow kFalse, kSuppressUI - the way
//  KFC opens a book's chapter; memory open-document-hidden): the person's documents from then on. A test build's fault switch open-hidden-file gives the
//  files instead of the dialog (a line a path). The UI half's; the main thread only.
//
//========================================================================================

#ifndef __KFCDocumentOps_h__
#define __KFCDocumentOps_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCRowsByChapter

namespace KFCDocumentOps
{
	enum Op { kShow = 0, kClose, kSave };

	/** CAN THE OPERATION BE OFFERED ON THIS ROW (the menu's grey - UpdateActionStates): a Document list's row whose document is open, and - Show - has no layout
	    window; - Close, Save - IDocFileHandler's own Can... says yes. Read only. */
	bool CanDo(Op op, int32 chapterIdx, int32 hitIdx);

	/** THE OPERATION ON THE ROWS' DOCUMENTS (each once). outStatus = the status line: how many, and why the rest were not. False = nothing was done. */
	bool Do(Op op, const KFCRowsByChapter& rows, PMString& outStatus);

	/** OPEN HIDDEN... (the Document face's button): the chosen documents opened without a window. outSaid = the face's first line. False = nothing opened
	    (cancelled, or none could be). */
	bool OpenHidden(PMString& outSaid);
}

#endif // __KFCDocumentOps_h__

// End, KFCDocumentOps.h.
