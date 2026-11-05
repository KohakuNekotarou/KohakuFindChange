//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  SPECIAL OPERATIONS > RUN SCRIPT... ON ROWS (the author, 2026-11-06: "a mechanism on the right-click menu to run a script on that row - a variable result,
//  kfcResult, a script can do things with"; docs/superpowers/specs/2026-11-06-kfc-run-script-on-rows-design.md RS1 - RS10 - every call the author left to me,
//  over the 10-17 decision that had put it off). KFC says WHICH (the rows chosen on the list), a person's ExtendScript says WHAT: run once for each row, with
//  kfcResult the scripting object the row stands for - a match's text (an insertion point for a zero-width one), an object row's page item, a Story row's story
//  (an internal one's XmlStory), a Table row's table, a table row list's row, a script label list's table or cell - made in C++ as a specifier the script's own
//  resolve() turns back into the object (IScriptUtils::GenerateSpecifier; a text range through ITextScriptUtils::QueryTextObject; a table's is its story's and
//  its UID, which GenerateSpecifier does not make - measured 2026-11-06). kfcIndex and kfcCount say the row's place among them.
//  The script is asked in a window of ExtendScript's own (ScriptUI - no widget of the UI band's, which is spent), the last one run kept in InDesign's user
//  folder (KFCRunScript.jsx) for the next. The rows run inside ONE undo step (app.doScript, UndoModes.ENTIRE_SCRIPT, "KFC Run Script"): one row's error is
//  counted and the rest go on. Rows whose documents are not open (a book's closed chapter) are counted, not run. No row is marked: what a script changed is not
//  KFC's to know - the rows are drawn again. The UI half's, as InDesign's own Scripts panel runs scripts; a private engine name keeps it out of the person's
//  own #targetengine sessions. The main thread only.
//
//========================================================================================

#ifndef __KFCRowScript_h__
#define __KFCRowScript_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCRowsByChapter

namespace KFCRowScript
{
	/** RUN SCRIPT... ON THE ROWS (RS2 - RS6): the window asked, the script run on each row the list still finds, one Ctrl+Z. outStatus = what the status line says - ran
	    on how many, the first error, the rows not run and why; "cancelled" when the window's Cancel was pressed. False = nothing was run. A test build's fault switch
	    run-script-source gives the script and no window is shown (RS9). */
	bool Run(const KFCRowsByChapter& rows, PMString& outStatus);
}

#endif // __KFCRowScript_h__

// End, KFCRowScript.h.
