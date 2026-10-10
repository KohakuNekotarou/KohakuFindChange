//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE HIDDEN-DOCUMENT CHECK (test builds only - 2026-10-10, the author: "a hidden document left behind would be trouble:
//  build a check for it into the code, and test for it always, the full regression included"). At the end of every
//  operation a person starts on the panel - a click on a row, an arrow or Return in the list, a menu action, the query
//  dialog's Run - one line in the test build's trace (KFCDiag.h):
//
//      HIDDENDOCS <where> held=<n> hidden=<m> [<name>|held or not-held|clean or modified] ...
//
//  m = the open documents with no window, each named; n = how many of them KFC still holds (KFCBookScope's held list - a
//  chapter it reopened windowless). Every operation of KFC's hands back what it reopened, or gives it its window, so a
//  held one when the operation is over is a leftover: work\kbs-regress\run.ps1 reads each case's lines and judge.py fails
//  the case (HIDDEN-LEFT). A document with no window that KFC does not hold is listed too (the user's own hidden
//  document, or one a scheduled close takes a moment later) - the regression's own reading, after the case's steps, tells
//  those apart from a leftover.
//
//  Checked once per operation: one inside another (a run pumps events through its progress bar, and a key or a menu
//  action can be dispatched while it stands) is not checked on its own, and nothing is said while a run of ours is up.
//  In a shipping build KFC_DIAG_HIDDEN_DOCS_CHECK is nothing, and so is the .cpp.
//
//========================================================================================

#ifndef __KFCDiagHiddenDocs_h__
#define __KFCDiagHiddenDocs_h__

#ifdef KFC_DIAG

/** The check, at the end of the scope it stands in - declared first in an event handler, so it runs when the handler
	has done everything else. 'where' names the operation in the trace (a string literal: it is kept, not copied). */
class KFCDiagHiddenDocsCheck
{
public:
	explicit KFCDiagHiddenDocsCheck(const char* where);
	~KFCDiagHiddenDocsCheck();
private:
	const char* fWhere;
	KFCDiagHiddenDocsCheck(const KFCDiagHiddenDocsCheck&);
	KFCDiagHiddenDocsCheck& operator=(const KFCDiagHiddenDocsCheck&);
};

#define KFC_DIAG_HIDDEN_DOCS_CHECK(where) const KFCDiagHiddenDocsCheck kfcDiagHiddenDocsCheck(where)

#else

#define KFC_DIAG_HIDDEN_DOCS_CHECK(where) ((void)0)

#endif // KFC_DIAG

#endif // __KFCDiagHiddenDocs_h__

// End, KFCDiagHiddenDocs.h.
