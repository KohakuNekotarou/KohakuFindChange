//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The find/change settings' commands, one at a time - see KFCFindChangeCmds.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IIntData.h"				// the value, and the mode in IID_IFINDCHANGEMODEDATA
#include "IBoolData.h"				// a switch; the glyph command's side
#include "IStringData.h"			// Find what / Change to

// General includes:
#include "TextWalkerServiceProviderID.h"	// the command bosses, IID_IFINDCHANGEMODEDATA
#include "CmdUtils.h"
#include "ErrorUtils.h"				// PMSetGlobalErrorCode

// Project includes:
#include "KFCFindChangeCmds.h"

// Process one of the find/change commands, and say whether it went through. A failure's error state is
// cleared at once, whatever the caller then does: left standing it fails every find command after it
// (and rolls back the sequence of whatever runs next). The caller is told, so it can stop rather than
// walk - or write - by a value that is not the one on screen.
bool KFCFindChangeCmds::ProcessFindChangeCmd(ICommand* cmd)
{
	if (CmdUtils::ProcessCommand(cmd) == kSuccess)
		return true;
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return false;
}

// Re-state one side of the Glyph tab's query on the find/change options. The command carries two
// fields: IIntData is the glyph itself, and IBoolData picks the side - kTrue for the glyph being
// looked for, kFalse for the one that replaces it. This is what SnpFindAndReplace does in
// Do_FindGlyph and Do_ReplaceGlyph, the only worked glyph example in the SDK.
//
// THE ANSWER IS RETURNED, NOT SWALLOWED. false means the value was NOT stated, and the engine will
// therefore walk - or write - with whatever was committed last. On the replace side that is a glyph
// the user never chose on this run and cannot see anywhere on screen, which is the exact failure
// CommitReplaceSide exists to prevent; on the find side it is a query nobody typed.
bool KFCFindChangeCmds::CommitGlyphID(Text::GlyphID glyphID, bool16 findSide)
{
	// An empty box means different things on the two sides, so they are treated differently.
	//
	// FIND side: nothing to look for, and nothing wrong - the search is stopped before it ever gets
	// here (HasFindQuery asks IsThereSomethingToFind), so committing -1 would only clear what the dialog
	// already holds. That side is left alone, and that is a success, not a failure.
	//
	// REPLACE side: an empty Change To box is a legitimate request - it DELETES every match, exactly
	// as an empty change string does on the Text tab, and the Find/Change dialog itself allows it
	// (confirmed against the dialog). There the -1 MUST be stated: leaving it unstated
	// makes the replace command fall back on whatever change glyph was committed last, writing a
	// glyph the user did not choose and cannot see anywhere on screen. Stating it is what overwrites
	// that leftover.
	if (glyphID == kInvalidGlyphID && findSide)
		return true;

	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindChangeGlyphIDCmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IIntData>  value(cmd, UseDefaultIID());
	InterfacePtr<IBoolData> side(cmd, UseDefaultIID());
	if (value == nil || side == nil)
		return false;
	value->Set(glyphID);
	side->Set(findSide);
	return ProcessFindChangeCmd(cmd);
}

// One of the find/change option commands that carry a single int, made and filled - the shape of
// SnpFindAndReplace's ProcessFindChangeCommandInt32: the value in the default IIntData, and which
// mode's settings are being addressed in IID_IFINDCHANGEMODEDATA where the boss carries one
// (kFindSearchModeCmdBoss and kFindChangeModeCmdBoss do, both fields kIntDataImpl; the two character-type
// bosses hold only the value - checked against a live object-model dump - so that field is set only when it
// answers). nil = not made.
ICommand* KFCFindChangeCmds::CreateFindChangeIntCmd(const ClassID& cmdBoss, int32 value, int32 mode)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
	if (cmd == nil)
		return nil;
	InterfacePtr<IIntData> valueData(cmd, UseDefaultIID());
	if (valueData == nil)
		return nil;
	valueData->Set(value);
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (modeData != nil)
		modeData->Set(mode);
	return cmd.forget();
}

// That command processed. Returns whether the value was stated, for the same reason CommitGlyphID does: the snippet
// this is shaped after hands its ErrorCode back too, and every caller of it stops on a failure
// (SnpFindAndReplace.cpp:511-516, :598-603, :623-628).
bool KFCFindChangeCmds::CommitFindChangeInt(const ClassID& cmdBoss, int32 value, int32 mode)
{
	InterfacePtr<ICommand> cmd(CreateFindChangeIntCmd(cmdBoss, value, mode));
	return cmd != nil && ProcessFindChangeCmd(cmd);
}

// The same command SCHEDULED rather than processed (CmdUtils::ScheduleCommand - the queue keeps its own reference, as
// snippetrunner's SnipRunLog::Resize hands one over): for a caller inside the notification of another find/change
// command, where processing one of its own would nest it in the command being notified. Whether it was scheduled.
bool KFCFindChangeCmds::ScheduleFindChangeInt(const ClassID& cmdBoss, int32 value, int32 mode)
{
	InterfacePtr<ICommand> cmd(CreateFindChangeIntCmd(cmdBoss, value, mode));
	return cmd != nil && CmdUtils::ScheduleCommand(cmd) == kSuccess;
}

// A command that carries a switch for one tab - the value in the default IBoolData, the tab in IID_IFINDCHANGEMODEDATA:
// Case Sensitive, Whole Word, Kana / Width Sensitive and the five Include switches (the bosses' data, checked against the
// SDK's command list - docs/ai-notes/cmdboss-dictionary.md). Whether it was stated, as CommitFindChangeInt says.
bool KFCFindChangeCmds::CommitFindChangeBool(const ClassID& cmdBoss, bool16 value, int32 mode)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IBoolData> valueData(cmd, UseDefaultIID());
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (valueData == nil || modeData == nil)
		return false;
	valueData->Set(value);
	modeData->Set(mode);
	return ProcessFindChangeCmd(cmd);
}

// Find what or Change to of one tab (kFindStringCmdBoss / kReplaceStringCmdBoss) - the string in the default IStringData,
// the tab in IID_IFINDCHANGEMODEDATA: KFCSearchEngine::SetQuery's shape (SnpFindAndReplace's ProcessFindChangeCommandString).
bool KFCFindChangeCmds::CommitFindChangeString(const ClassID& cmdBoss, const PMString& value, int32 mode)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IStringData> valueData(cmd, UseDefaultIID());
	InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
	if (valueData == nil || modeData == nil)
		return false;
	valueData->Set(value);
	modeData->Set(mode);
	return ProcessFindChangeCmd(cmd);
}

// One side of the Glyph tab's query stated AS IT IS - -1 (no glyph) included, on either side. CommitGlyphID leaves an
// empty find side alone, which is right for a search; a query put back (KFCQueryCopy) must state the empty side too, or
// the glyph standing there before would stay.
bool KFCFindChangeCmds::StateGlyphID(Text::GlyphID glyphID, bool16 findSide)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindChangeGlyphIDCmdBoss));
	if (cmd == nil)
		return false;
	InterfacePtr<IIntData> value(cmd, UseDefaultIID());
	InterfacePtr<IBoolData> side(cmd, UseDefaultIID());
	if (value == nil || side == nil)
		return false;
	value->Set(glyphID);
	side->Set(findSide);
	return ProcessFindChangeCmd(cmd);
}

// End, KFCFindChangeCmds.cpp.
