//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A COPY OF EDIT > FIND/CHANGE'S QUERY, IN MEMORY (docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md section
//  3-3 - the tier switch). Taken when a search goes through - the tab Find/Change is on: its strings, its switches, its
//  character types or glyphs, its object type, its Find / Change Format - stated back with the dialog's own commands
//  (KFCFindChangeCmds) for the length of a row operation when Find/Change is on another query (KFCQueryScope - v2:
//  docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md section 3-2), and compared field by field after: a
//  query that does not read back as the copy is refused and put back (the author: "if this goes wrong it is very
//  frightening"). A replace made with Find/Change as it is takes the copy again (v2 R3).
//  NO FILE IS WRITTEN (the author's call): not a saved query, not a temporary one.
//  Left out on purpose: Search: (KFC never states it - kScopeCmdBoss) and the direction (KFC walks forward only).
//  What the copy does not state back (the XML tag, the case option and the conditions mode of the change side, the CJK
//  font groups) is copied to be COMPARED: a switch is refused when one of them differs. Commands for them exist -
//  kFindChangeFormatCmdBoss's IFindChangeFormatCmdData carries the first three (SetChangeXMLTag, SetChangeCaseOption,
//  SetChangeConditionsMode), kFindROSFontGroupCmdBoss / kUseROSFontGroupCmdBoss the font groups - but none is measured
//  yet, so none is used.
//  The main thread only.
//
//========================================================================================

#ifndef __KFCQueryCopy_h__
#define __KFCQueryCopy_h__

#include "PMString.h"
#include "UIDRef.h"				// UID
#include "AttributeBossList.h"	// the format lists, and boost::shared_ptr with them

class IDataBase;

struct KFCQueryCopy
{
	int32		mode;					// IFindChangeOptions::SearchMode (-1 = nothing read)
	int32		changeMode;				// IFindChangeOptions::ChangeMode
	PMString	findString;				// the tab's Find what (Text / GREP)
	PMString	replaceString;			// the tab's Change to (Text / GREP)
	bool16		caseSensitive;
	bool16		entireWord;
	bool16		kanaSensitive;
	bool16		widthSensitive;
	bool16		includeLockedStories;
	bool16		includeLockedLayers;
	bool16		includeHiddenLayers;
	bool16		includeMasterPages;
	bool16		includeFootnotes;
	int32		findCharType;			// IFindChangeOptions::CharacterType (Transliterate)
	int32		replaceCharType;
	int32		findGlyph;				// Text::GlyphID (Glyph; kInvalidGlyphID = none)
	int32		replaceGlyph;
	int32		objectType;				// IFindChangeOptions::ObjectType (Object)
	// Find / Change Format: the lists - shallow copies (AttributeBossList::Duplicate), held as the SDK's own callers hold
	// them (KFCSearchEngine's RememberFindFormat) - and the styles kept beside them.
	boost::shared_ptr<AttributeBossList>	findAttrs;
	boost::shared_ptr<AttributeBossList>	changeAttrs;
	UID			findParaStyle;
	UID			findCharStyle;
	UID			changeParaStyle;
	UID			changeCharStyle;
	UID			objectFindStyle;
	UID			objectChangeStyle;
	IDataBase*	attrDB;					// the database the lists' and styles' UIDs are in (GetUIDAttrDB - the session's); a raw
										// address, compared only - never handed to a command unless Find/Change's is the same (Apply)
	// Compared only - not stated back (the head of this file):
	PMString	changeXMLTag;
	PMString	changeCaseOption;
	int32		changeConditionsMode;	// IFindChangeOptions::ChangeConditionsMode
	bool16		useFindROS;
	bool16		useReplaceROS;
	PMString	findROSRegistry;
	PMString	findROSOrdering;
	PMString	replaceROSRegistry;
	PMString	replaceROSOrdering;
	bool		valid;					// false = the settings could not be read; never stated back

	KFCQueryCopy();
};

namespace KFCQueryCopier
{
	/** The query of the tab Find/Change is on now. out.valid is false when the settings cannot be read. */
	void Capture(KFCQueryCopy& out);
	/** The query of tab `mode` (IFindChangeOptions::SearchMode), whichever tab Find/Change is on - what a tab holds while
	    another is shown, read so it can be put back after a list's query of that tab was stated over it (KFCQueryScope).
	    Stated back by Apply, which turns to that tab. out.valid is false when the settings cannot be read. */
	void CaptureMode(KFCQueryCopy& out, int32 mode);

	/** State `copy` back into Find/Change: the tab, then every field that differs from what the dialog holds now, with the
	    dialog's own commands (KFCFindChangeCmds; the formats with kClearFindFormatCmdBoss / kClearChangeFormatCmdBoss and
	    kFindChangeFormatCmdBoss). False = a command failed - outWhat names the field; the ones before it went through
	    (putting everything back is the caller's - KFCQueryScope). Call it outside any command sequence unless the caller
	    means to abort that sequence on a failure. */
	bool Apply(const KFCQueryCopy& copy, PMString& outWhat);

	/** Does Find/Change hold `copy` now - every field the copy's tab uses? False = outWhat names the first that differs.
	    "Cannot tell" (the settings unreadable, a format list in another database) is "differs": this is the door before a
	    write, so it fails closed. */
	bool Matches(const KFCQueryCopy& copy, PMString& outWhat);

#ifdef KFC_DIAG
	/** TEST BUILDS ONLY - the tier switch's spike (docs/superpowers/plans/2026-10-14-kfc-tier-switch.md Task 3). While one of
	    the fault switches is on, the panel's Find does this instead of searching (one-shot - the switch is taken away):
	      qcopy-roundtrip  Capture, Find/Change cleared (kClearFindChangeOptionsCmdBoss), Apply, Matches
	      qcopy-armed N    the copy with field N made different, Apply it, Matches against the TRUE copy must fail; then
	                       the true copy stated back and matched (1 Change to +"#", 2 Case Sensitive, 3 Change Format
	                       emptied, 4 the find glyph +1, 5 the find paragraph style dropped, 6 Include Footnotes, 7 the
	                       change character type, 8 the object type, 9 the change glyph +1)
	      qcopy-abort      inside an abortable command sequence: Apply the copy with Change to +"#", abort, Matches
	    The answer goes to the trace (QCOPY) and to outSummary. True = a switch was on and acted. */
	bool DiagProbe(PMString& outSummary);
#endif
}

#endif // __KFCQueryCopy_h__

// End, KFCQueryCopy.h.
