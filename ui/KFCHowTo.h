//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  Kohaku Find/Change (KFC)
//
//  "How to Use..." on the panel's flyout menu: the operating reference for the whole plug-in,
//  shown in a ScriptUI dialog so it can be scrolled, selected and copied.
//
//  The text lives in the .cpp rather than in the string tables - see there for why.
//
//========================================================================================

#pragma once
#ifndef __KFCHowTo_h__
#define __KFCHowTo_h__

#include <string>

namespace KFCHowTo
{
	/** Show the operating reference: Japanese when InDesign's UI language is Japanese, English
	    otherwise - the one text of KFC's own that follows the UI language (everything from the
	    string tables is English in every UI language), asked in Show() itself. */
	void Show();

	/** The Kohaku+ tab's How to Use... on Ruby (the author, 11-02): how ruby is written out, for Ruby Marks in Text and a ruby list. In Japanese alone: the part stands in a Japanese InDesign alone. */
	void ShowRubyMarks();

	/** The same button's page on Kenten (the author, 11-02): what the part's fields ask and what Replace does. In Japanese alone, as Ruby's. */
	void ShowKenten();

	/** RUN SCRIPT'S HELP... (the author, 11-06: "explain it with a Help button, not with //" - docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md
	    RH1, RH3): its text in the UI language - Japanese in a Japanese InDesign, English otherwise, as Show() asks it - appended to 'out' as the body of a JavaScript
	    double-quoted string (KFCRowScript puts it in its window's script, which shows it in a window of its own). Returns its length in UTF-16 code units;
	    outJapanese = which it was. */
	int32 AppendRunScriptHelpJS(std::string& out, bool& outJapanese);
}

#endif // __KFCHowTo_h__
