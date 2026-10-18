//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHERE A SHOWN STRING MAY BE CUT - the one rule every cut of shown text follows, in either half (the callers of
//  KFCSafeCut and KFCIsAttachedMark are the list): the model's (a hit row's line and its match - KFCSearchEngine.cpp's
//  SplitLineWithScanner; a story row's first words - KFCPageItemFacts.cpp's StoryLeadText; an object row's item name -
//  KFCObjectSearch.cpp's BuildObjectHit) and the UI's (the message area's lines and contexts - KFCStatusTextView.cpp; a
//  GREP row's Preview Text - KFCResultListWidgetMgr.cpp; a tier's name - KFCResultHistory.cpp; a saved query's text in
//  the query dialog - KFCQueryContents.cpp). One place, so the two halves cannot come to cut by two rules.
//  In model/ with the other headers both halves read: header-only - two inline functions on PMString and UnicodeClass,
//  nothing of either plug-in's code - so each half compiles its own copy and nothing crosses the boundary through it.
//
//========================================================================================

#ifndef __KFCTextCut_h__
#define __KFCTextCut_h__

#include "PMString.h"
#include "UnicodeClass.h"		// IsCombiningMark

/** IS c A MARK THAT BELONGS TO THE CHARACTER BEFORE IT - a variation selector (an IVS such as U+E0100 picks the form of
    the kanji before it) or a combining mark (U+3099 voices the kana before it, U+0301 accents a letter)? Cut in front of
    one, the kept half draws the bare character - a DIFFERENT glyph from the document's, not merely a shorter text - and
    the mark goes on alone.
    UnicodeClass::IsCombiningMark answers for both: it says yes to the variation selectors as well (measured against
    Public.dll outside InDesign: U+FE00, U+FE0F, U+E0100, U+E01EF, U+180B; and no to the spacing U+309B and to ZWJ). */
inline bool KFCIsAttachedMark(const UTF32TextChar& c)
{
	return UnicodeClass::IsCombiningMark(c);
}

/** WHERE A SHOWN STRING MAY BE CUT: in front of a character, never in front of a mark that belongs to the character
    before it (KFCIsAttachedMark). So the cut moves back to in front of that character, whichever side is kept - a head
    keeps [0, pos), a tail [pos, end): the character goes with its marks.
    PMString's positions are whole characters - CharCount, Truncate and Remove count them, GetWChar answers the character
    at one - so no cut can fall inside a surrogate pair.
    @return pos, or the position of the character the marks at pos belong to. */
inline int32 KFCSafeCut(const PMString& s, int32 pos)
{
	if (pos >= s.CharCount())
		return pos;
	while (pos > 0 && KFCIsAttachedMark(s.GetWChar(pos)))
		--pos;
	return pos;
}

#endif // __KFCTextCut_h__

// End, KFCTextCut.h.
