//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE FIND/CHANGE SETTINGS' COMMANDS, ONE AT A TIME - each in the shape SnpFindAndReplace gives it, each saying whether
//  the value was stated. KFCSearchEngine states the dialog's tab, the replace side, the search direction and a query of
//  its own (SetQuery) with them; KFCQueryCopy states a whole query back with them
//  (docs/superpowers/specs/2026-10-14-kfc-tier-switch-design.md section 3-3). A failure's
//  error state is cleared at once (ProcessFindChangeCmd): left standing it would fail every find command after it.
//  Call them OUTSIDE any command sequence unless the caller means them to be part of it (KFCSearchEngine::CommitSearchMode
//  says why). The main thread only.
//
//========================================================================================

#ifndef __KFCFindChangeCmds_h__
#define __KFCFindChangeCmds_h__

#include "IPMUnknown.h"		// ClassID
#include "PMString.h"
#include "CTextEnum.h"		// Text::GlyphID

class ICommand;

namespace KFCFindChangeCmds
{
	/** Process one of the find/change commands - true when it went through; a failure's error state is cleared. */
	bool ProcessFindChangeCmd(ICommand* cmd);

	/** One side of the Glyph tab's query (kFindChangeGlyphIDCmdBoss): kTrue = the glyph looked for, kFalse = the one
	    that replaces it. An empty FIND side (kInvalidGlyphID) is left alone and answers true - a search has nothing to
	    state there; an empty REPLACE side is stated (it deletes every match). */
	bool CommitGlyphID(Text::GlyphID glyphID, bool16 findSide);

	/** A command that carries one int, made, filled and processed: the value in the default IIntData, and the tab in
	    IID_IFINDCHANGEMODEDATA where the boss carries one. Whether the value was stated. */
	bool CommitFindChangeInt(const ClassID& cmdBoss, int32 value, int32 mode);

	/** That command SCHEDULED rather than processed - for a caller inside another find/change command's notification.
	    Whether it was scheduled. */
	bool ScheduleFindChangeInt(const ClassID& cmdBoss, int32 value, int32 mode);

	/** A switch of one tab (Case Sensitive, Whole Word, Kana / Width Sensitive, the five Include switches, the search
	    direction) - the value in IBoolData, the tab in IID_IFINDCHANGEMODEDATA. Whether it was stated. */
	bool CommitFindChangeBool(const ClassID& cmdBoss, bool16 value, int32 mode);

	/** Find what / Change to of one tab (kFindStringCmdBoss / kReplaceStringCmdBoss). Whether it was stated. */
	bool CommitFindChangeString(const ClassID& cmdBoss, const PMString& value, int32 mode);

	/** One side of the Glyph tab's query stated as it is, kInvalidGlyphID included on either side - for a query put
	    back whole (KFCQueryCopy). Whether it was stated. */
	bool StateGlyphID(Text::GlyphID glyphID, bool16 findSide);
}

#endif // __KFCFindChangeCmds_h__

// End, KFCFindChangeCmds.h.
