//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A copy of Edit > Find/Change's query, in memory - see KFCQueryCopy.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IFindChangeOptions.h"
#include "IFindChangeFormatCmdData.h"	// the formats stated back (kFindChangeFormatCmdBoss)
#include "ICommand.h"
#include "ICommandSequence.h"			// the test build's abort probe (IAbortableCmdSeq)
#include "IIntData.h"					// the tab of the format commands (IID_IFINDCHANGEMODEDATA)

// General includes:
#include "TextWalkerServiceProviderID.h"	// the find/change command bosses
#include "CmdUtils.h"
#include "PreferenceUtils.h"			// QuerySessionPreferences - the settings the search reads
#include "CTextEnum.h"					// kInvalidGlyphID

// Project includes:
#include "KFCQueryCopy.h"
#include "KFCModelTypes.h"				// kKFCQRCodeSearchMode - a QR code list's copy, the mode alone
#include "KFCFindChangeCmds.h"			// the settings' commands, one at a time
#include "KFCDiag.h"					// the spike's fault switches and its QCOPY line - test builds only

KFCQueryCopy::KFCQueryCopy()
	: mode(-1), changeMode(0), caseSensitive(kFalse), entireWord(kFalse), kanaSensitive(kFalse), widthSensitive(kFalse),
	  includeLockedStories(kFalse), includeLockedLayers(kFalse), includeHiddenLayers(kFalse), includeMasterPages(kFalse),
	  includeFootnotes(kFalse), findCharType(0), replaceCharType(0), findGlyph(kInvalidGlyphID),
	  replaceGlyph(kInvalidGlyphID), objectType(0), findParaStyle(kInvalidUID), findCharStyle(kInvalidUID),
	  changeParaStyle(kInvalidUID), changeCharStyle(kInvalidUID), objectFindStyle(kInvalidUID),
	  objectChangeStyle(kInvalidUID), attrDB(nil), changeConditionsMode(0), useFindROS(kFalse), useReplaceROS(kFalse),
	  valid(false)
{
}

namespace
{
	typedef IFindChangeOptions FCO;

	// WHICH FIELDS A TAB USES - the one answer Apply (what to state) and FirstDifference (what to compare) both read.
	// The switches and the format lists are every tab's: each tab keeps its own, and the walk signature reads them all
	// (KFCSearchEngine::BuildWalkSignature - "an option that is not on screen can still be set").
	bool UsesStrings(int32 mode)		{ return mode == FCO::kTextSearch || mode == FCO::kGrepSearch; }
	bool UsesCharTypes(int32 mode)		{ return mode == FCO::kTransliterateSearch; }
	bool UsesGlyphs(int32 mode)			{ return mode == FCO::kGlyphSearch; }
	bool UsesObjectType(int32 mode)		{ return mode == FCO::kObjectSearch; }
	bool UsesTextStyles(int32 mode)		{ return mode != FCO::kObjectSearch; }
	bool UsesObjectStyles(int32 mode)	{ return mode == FCO::kObjectSearch; }
	bool UsesChangeExtras(int32 mode)	{ return UsesStrings(mode); }	// the change side's XML tag, case option, conditions
	bool UsesROS(int32 mode)			{ return mode == FCO::kGlyphSearch; }

	// TWO FORMAT LISTS THE SAME? nil and an empty list are the same. Lists of different databases are NOT - their UIDs do
	// not mean the same thing, and this is the door before a write, so "cannot tell" is "different". The list compares
	// itself (AttributeBossList::IsEqual - a deep compare; RememberFindFormat's note in KFCSearchEngine.cpp says why).
	bool SameList(const boost::shared_ptr<AttributeBossList>& a, IDataBase* aDB,
		const boost::shared_ptr<AttributeBossList>& b, IDataBase* bDB)
	{
		const int32 na = (a.get() != nil) ? a->CountBosses() : 0;
		const int32 nb = (b.get() != nil) ? b->CountBosses() : 0;
		if (na == 0 && nb == 0)
			return true;
		if (na != nb || aDB == nil || aDB != bDB)
			return false;
		return a->IsEqual(aDB, b.get()) != kFalse;
	}

	// THE FIRST FIELD `want`'s TAB USES THAT `now` HOLDS DIFFERENTLY - nil when none does. Its words name the field for the
	// status line (Could not switch: ... (<this>)).
	const char* FirstDifference(const KFCQueryCopy& want, const KFCQueryCopy& now)
	{
		if (!now.valid)
			return "the Find/Change settings could not be read";
		if (now.mode != want.mode)
			return "the tab";
		if (now.changeMode != want.changeMode)
			return "the change mode";
		if (UsesStrings(want.mode))
		{
			if (now.findString != want.findString)
				return "Find what";
			if (now.replaceString != want.replaceString)
				return "Change to";
		}
		if (now.caseSensitive != want.caseSensitive)
			return "Case Sensitive";
		if (now.entireWord != want.entireWord)
			return "Whole Word";
		if (now.kanaSensitive != want.kanaSensitive)
			return "Kana Sensitive";
		if (now.widthSensitive != want.widthSensitive)
			return "Width Sensitive";
		if (now.includeLockedStories != want.includeLockedStories)
			return "Include Locked Stories";
		if (now.includeLockedLayers != want.includeLockedLayers)
			return "Include Locked Layers";
		if (now.includeHiddenLayers != want.includeHiddenLayers)
			return "Include Hidden Layers";
		if (now.includeMasterPages != want.includeMasterPages)
			return "Include Master Pages";
		if (now.includeFootnotes != want.includeFootnotes)
			return "Include Footnotes";
		if (UsesCharTypes(want.mode))
		{
			if (now.findCharType != want.findCharType)
				return "the character type looked for";
			if (now.replaceCharType != want.replaceCharType)
				return "the character type changed to";
		}
		if (UsesGlyphs(want.mode))
		{
			if (now.findGlyph != want.findGlyph)
				return "the glyph looked for";
			if (now.replaceGlyph != want.replaceGlyph)
				return "the glyph changed to";
		}
		if (UsesObjectType(want.mode) && now.objectType != want.objectType)
			return "the object type";
		if (!SameList(want.findAttrs, want.attrDB, now.findAttrs, now.attrDB))
			return "Find Format";
		if (!SameList(want.changeAttrs, want.attrDB, now.changeAttrs, now.attrDB))
			return "Change Format";
		if (UsesTextStyles(want.mode))
		{
			if (now.findParaStyle != want.findParaStyle)
				return "the paragraph style looked for";
			if (now.findCharStyle != want.findCharStyle)
				return "the character style looked for";
			if (now.changeParaStyle != want.changeParaStyle)
				return "the paragraph style changed to";
			if (now.changeCharStyle != want.changeCharStyle)
				return "the character style changed to";
		}
		if (UsesObjectStyles(want.mode))
		{
			if (now.objectFindStyle != want.objectFindStyle)
				return "the object style looked for";
			if (now.objectChangeStyle != want.objectChangeStyle)
				return "the object style changed to";
		}
		if (UsesChangeExtras(want.mode))
		{
			if (now.changeXMLTag != want.changeXMLTag)
				return "the change XML tag";
			if (now.changeCaseOption != want.changeCaseOption)
				return "the change case option";
			if (now.changeConditionsMode != want.changeConditionsMode)
				return "the change conditions mode";
		}
		if (UsesROS(want.mode))
		{
			if (now.useFindROS != want.useFindROS || now.findROSRegistry != want.findROSRegistry
				|| now.findROSOrdering != want.findROSOrdering)
				return "the CJK font group looked for";
			if (now.useReplaceROS != want.useReplaceROS || now.replaceROSRegistry != want.replaceROSRegistry
				|| now.replaceROSOrdering != want.replaceROSOrdering)
				return "the CJK font group changed to";
		}
		return nil;
	}

	// A step that could not be stated: its name for the caller's sentence.
	bool Failed(PMString& outWhat, const char* what)
	{
		outWhat.Append(what);
		return false;
	}

	// kClearFindFormatCmdBoss / kClearChangeFormatCmdBoss: their one field is the tab (IID_IFINDCHANGEMODEDATA - the SDK's
	// command list; there is no default IIntData on them, so CommitFindChangeInt does not fit).
	bool CommitClearFormat(const ClassID& cmdBoss, int32 mode)
	{
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(cmdBoss));
		if (cmd == nil)
			return false;
		InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
		if (modeData == nil)
			return false;
		modeData->Set(mode);
		return KFCFindChangeCmds::ProcessFindChangeCmd(cmd);
	}

	// Does the copy hold a format on that side - a list with something in it, or a style?
	bool HasFindFormat(const KFCQueryCopy& c)
	{
		return (c.findAttrs.get() != nil && c.findAttrs->CountBosses() > 0) || c.findParaStyle != kInvalidUID
			|| c.findCharStyle != kInvalidUID || c.objectFindStyle != kInvalidUID;
	}
	bool HasChangeFormat(const KFCQueryCopy& c)
	{
		return (c.changeAttrs.get() != nil && c.changeAttrs->CountBosses() > 0) || c.changeParaStyle != kInvalidUID
			|| c.changeCharStyle != kInvalidUID || c.objectChangeStyle != kInvalidUID;
	}
}	// anonymous namespace

void KFCQueryCopier::Capture(KFCQueryCopy& out)
{
	out = KFCQueryCopy();
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;
	CaptureMode(out, static_cast<int32>(opts->GetSearchMode()));
}

void KFCQueryCopier::CaptureMode(KFCQueryCopy& out, int32 tab)
{
	out = KFCQueryCopy();
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
		return;
	// The lists' database first: every format question below takes it (BuildWalkSignature's rule - a copy that reads
	// complete while saying nothing of the formats must never be made).
	IDataBase* const db = opts->GetUIDAttrDB();
	if (db == nil)
		return;
	// The tab asked for, whichever is on screen: every setting below is read for it (the getters take the tab), and the
	// few that belong to no tab (the character types, the glyphs, the object type and styles, the CJK font groups) are
	// read as they stand.
	const FCO::SearchMode mode = static_cast<FCO::SearchMode>(tab);
	out.mode = static_cast<int32>(mode);
	out.changeMode = static_cast<int32>(opts->GetChangeMode());
	out.findString = PMString(opts->GetFindString(mode));
	out.findString.SetTranslatable(kFalse);
	out.replaceString = PMString(opts->GetReplaceString(mode));
	out.replaceString.SetTranslatable(kFalse);
	out.caseSensitive = opts->GetCaseSensitive(mode);
	out.entireWord = opts->GetEntireWord(mode);
	out.kanaSensitive = opts->GetKanaSensitive(mode);
	out.widthSensitive = opts->GetWidthSensitive(mode);
	out.includeLockedStories = opts->GetIncludeLockedStoriesForFind(mode);
	out.includeLockedLayers = opts->GetIncludeLockedLayersForFind(mode);
	out.includeHiddenLayers = opts->GetIncludeHiddenLayers(mode);
	out.includeMasterPages = opts->GetIncludeMasterPages(mode);
	out.includeFootnotes = opts->GetIncludeFootnotes(mode);
	out.findCharType = static_cast<int32>(opts->GetFindCharacterType());
	out.replaceCharType = static_cast<int32>(opts->GetReplaceCharacterType());
	out.findGlyph = static_cast<int32>(opts->GetFindGlyphID());
	out.replaceGlyph = static_cast<int32>(opts->GetReplaceGlyphID());
	out.objectType = static_cast<int32>(opts->GetObjectSearchType());
	// THE LISTS ARE COPIES, not the live lists behind the dialog (the person is free to edit the format pane the moment
	// this returns - RememberFindFormat's reason). The change side is asked for WITHOUT creating one (allowCreation kFalse,
	// as KFCObjectSearch asks): reading must not add to the dialog's settings. The styles the same way.
	const AttributeBossList* const findList = opts->GetFindAttributeBossList(db, mode);
	if (findList != nil)
		out.findAttrs.reset(findList->Duplicate());
	const AttributeBossList* const changeList = opts->GetChangeAttributeBossList(db, mode, kFalse);
	if (changeList != nil)
		out.changeAttrs.reset(changeList->Duplicate());
	out.findParaStyle = opts->GetFindParaStyle(db, mode);
	out.findCharStyle = opts->GetFindCharStyle(db, mode);
	out.changeParaStyle = opts->GetChangeParaStyle(db, mode, kFalse);
	out.changeCharStyle = opts->GetChangeCharStyle(db, mode, kFalse);
	out.objectFindStyle = opts->GetObjectFindStyle(db);
	out.objectChangeStyle = opts->GetObjectChangeStyle(db, kFalse);
	out.attrDB = db;
	out.changeXMLTag = opts->GetChangeXMLTag(mode);
	out.changeCaseOption = opts->GetChangeCaseOption(mode);
	out.changeConditionsMode = static_cast<int32>(opts->GetChangeConditionsMode(mode));
	out.useFindROS = opts->GetUseFindROSFontGroup();
	out.useReplaceROS = opts->GetUseReplaceROSFontGroup();
	opts->GetFindROSFontGroup(out.findROSRegistry, out.findROSOrdering);
	opts->GetReplaceROSFontGroup(out.replaceROSRegistry, out.replaceROSOrdering);
	out.valid = true;
}

void KFCQueryCopier::CaptureQRCodes(KFCQueryCopy& out)
{
	out = KFCQueryCopy();
	out.mode = static_cast<int32>(kKFCQRCodeSearchMode);
	out.valid = true;
}

void KFCQueryCopier::CaptureRunList(KFCQueryCopy& out)
{
	out = KFCQueryCopy();
	out.mode = static_cast<int32>(kKFCRunListSearchMode);
	out.valid = true;
}

bool KFCQueryCopier::Apply(const KFCQueryCopy& copy, PMString& outWhat)
{
	outWhat.Clear();
	outWhat.SetTranslatable(kFalse);
	if (!copy.valid)
		return Failed(outWhat, "there is no query to put back");
	// A QR CODE LIST'S COPY STATES NOTHING: InDesign has no such mode to state (KFCModelTypes.h - never handed to a command),
	// and no setting of Find/Change's steers its walk (KFCQRSearch). Nor a query run's list's: it has no query (KFCRunList).
	if (copy.mode == kKFCQRCodeSearchMode || copy.mode == kKFCRunListSearchMode)
		return true;
	using namespace KFCFindChangeCmds;
	const int32 mode = copy.mode;

	// THE TAB FIRST - every setting below is the tab's, and SnpFindAndReplace states the mode before the rest.
	if (!CommitFindChangeInt(kFindSearchModeCmdBoss, mode, mode))
		return Failed(outWhat, "the tab");

	// Then only what differs from what the dialog holds now: a field that reads the same is left as it is (fewer commands,
	// and nothing moves on an open dialog that does not have to).
	KFCQueryCopy now;
	Capture(now);
	if (!now.valid)
		return Failed(outWhat, "the Find/Change settings could not be read");
	if (now.changeMode != copy.changeMode && !CommitFindChangeInt(kFindChangeModeCmdBoss, copy.changeMode, mode))
		return Failed(outWhat, "the change mode");
	if (UsesStrings(mode))
	{
		if (now.findString != copy.findString && !CommitFindChangeString(kFindStringCmdBoss, copy.findString, mode))
			return Failed(outWhat, "Find what");
		if (now.replaceString != copy.replaceString && !CommitFindChangeString(kReplaceStringCmdBoss, copy.replaceString, mode))
			return Failed(outWhat, "Change to");
	}
	struct Switch
	{
		bool16		now;
		bool16		want;
		ClassID		cmd;
		const char*	name;
	};
	const Switch switches[] =
	{
		{ now.caseSensitive, copy.caseSensitive, kCaseSensitiveCmdBoss, "Case Sensitive" },
		{ now.entireWord, copy.entireWord, kEntireWordCmdBoss, "Whole Word" },
		{ now.kanaSensitive, copy.kanaSensitive, kKanaSensitiveCmdBoss, "Kana Sensitive" },
		{ now.widthSensitive, copy.widthSensitive, kWidthSensitiveCmdBoss, "Width Sensitive" },
		{ now.includeLockedStories, copy.includeLockedStories, kIncludeLockedStoriesForFindCmdBoss, "Include Locked Stories" },
		{ now.includeLockedLayers, copy.includeLockedLayers, kIncludeLockedLayersForFindCmdBoss, "Include Locked Layers" },
		{ now.includeHiddenLayers, copy.includeHiddenLayers, kIncludeHiddenLayersCmdBoss, "Include Hidden Layers" },
		{ now.includeMasterPages, copy.includeMasterPages, kIncludeMasterPagesCmdBoss, "Include Master Pages" },
		{ now.includeFootnotes, copy.includeFootnotes, kIncludeFootnotesCmdBoss, "Include Footnotes" },
	};
	for (size_t i = 0; i < sizeof(switches) / sizeof(switches[0]); ++i)
		if (switches[i].now != switches[i].want && !CommitFindChangeBool(switches[i].cmd, switches[i].want, mode))
			return Failed(outWhat, switches[i].name);
	if (UsesCharTypes(mode))
	{
		if (now.findCharType != copy.findCharType && !CommitFindChangeInt(kFindCharacterTypeCmdBoss, copy.findCharType, mode))
			return Failed(outWhat, "the character type looked for");
		if (now.replaceCharType != copy.replaceCharType
			&& !CommitFindChangeInt(kReplaceCharacterTypeCmdBoss, copy.replaceCharType, mode))
			return Failed(outWhat, "the character type changed to");
	}
	if (UsesGlyphs(mode))
	{
		if (now.findGlyph != copy.findGlyph && !StateGlyphID(static_cast<Text::GlyphID>(copy.findGlyph), kTrue))
			return Failed(outWhat, "the glyph looked for");
		if (now.replaceGlyph != copy.replaceGlyph && !StateGlyphID(static_cast<Text::GlyphID>(copy.replaceGlyph), kFalse))
			return Failed(outWhat, "the glyph changed to");
	}
	if (UsesObjectType(mode) && now.objectType != copy.objectType
		&& !CommitFindChangeInt(kObjectSearchTypeCmdBoss, copy.objectType, mode))
		return Failed(outWhat, "the object type");

	// THE FORMATS: a side that differs is cleared, then the copy's list and styles applied - one format command for both
	// sides (IFindChangeFormatCmdData carries both). The Glyph tab's font is two attributes of its find list, so it comes
	// back with the list.
	const bool findDiffers = !SameList(copy.findAttrs, copy.attrDB, now.findAttrs, now.attrDB)
		|| now.findParaStyle != copy.findParaStyle || now.findCharStyle != copy.findCharStyle
		|| now.objectFindStyle != copy.objectFindStyle;
	const bool changeDiffers = !SameList(copy.changeAttrs, copy.attrDB, now.changeAttrs, now.attrDB)
		|| now.changeParaStyle != copy.changeParaStyle || now.changeCharStyle != copy.changeCharStyle
		|| now.objectChangeStyle != copy.objectChangeStyle;
	const bool applyFind = findDiffers && HasFindFormat(copy);
	const bool applyChange = changeDiffers && HasChangeFormat(copy);
	// A FORMAT GOES BACK ONLY INTO THE DATABASE IT WAS COPIED FROM - the one Find/Change's lists are in now (GetUIDAttrDB;
	// measured the session's, which outlives every document - the note over gSearchedFindAttrDB in KFCSearchEngine.cpp).
	// Another one, its UIDs mean nothing there; and the copy's address is a raw one that may be a closed database's, free
	// to be handed to the next document opened (memory uidref-reuse-after-close) - so it is compared, never handed to a
	// command, and the switch is refused before any format is touched (SameList reads a list only when the addresses
	// agree, for the same reason).
	if ((applyFind || applyChange) && copy.attrDB != now.attrDB)
		return Failed(outWhat, "the formats - they were copied from another attribute database");
	if (findDiffers && !CommitClearFormat(kClearFindFormatCmdBoss, mode))
		return Failed(outWhat, "Find Format (clearing it)");
	if (changeDiffers && !CommitClearFormat(kClearChangeFormatCmdBoss, mode))
		return Failed(outWhat, "Change Format (clearing it)");
	if (applyFind || applyChange)
	{
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFindChangeFormatCmdBoss));
		InterfacePtr<IIntData> modeData(cmd, IID_IFINDCHANGEMODEDATA);
		InterfacePtr<IFindChangeFormatCmdData> data(cmd, IID_IFINDCHANGEFORMATCMDDATA);
		if (cmd == nil || modeData == nil || data == nil)
			return Failed(outWhat, "the formats");
		modeData->Set(mode);
		data->SetTargetDB(copy.attrDB);
		if (applyFind)
		{
			if (copy.findAttrs.get() != nil && copy.findAttrs->CountBosses() > 0)
				data->ApplyFindAttributeBossList(copy.findAttrs.get());
			if (copy.findParaStyle != kInvalidUID)
				data->SetFindParaStyle(copy.findParaStyle);
			if (copy.findCharStyle != kInvalidUID)
				data->SetFindCharStyle(copy.findCharStyle);
			if (copy.objectFindStyle != kInvalidUID)
				data->SetObjectFindStyle(copy.objectFindStyle);
		}
		if (applyChange)
		{
			if (copy.changeAttrs.get() != nil && copy.changeAttrs->CountBosses() > 0)
				data->ApplyChangeAttributeBossList(copy.changeAttrs.get());
			if (copy.changeParaStyle != kInvalidUID)
				data->SetChangeParaStyle(copy.changeParaStyle);
			if (copy.changeCharStyle != kInvalidUID)
				data->SetChangeCharStyle(copy.changeCharStyle);
			if (copy.objectChangeStyle != kInvalidUID)
				data->SetObjectChangeStyle(copy.objectChangeStyle);
		}
		if (!ProcessFindChangeCmd(cmd))
			return Failed(outWhat, "the formats");
	}
	return true;
}

bool KFCQueryCopier::Matches(const KFCQueryCopy& copy, PMString& outWhat)
{
	outWhat.Clear();
	outWhat.SetTranslatable(kFalse);
	if (!copy.valid)
		return Failed(outWhat, "there is no query to compare");
	if (copy.mode == kKFCQRCodeSearchMode || copy.mode == kKFCRunListSearchMode)
		return true;		// a QR code list's copy, a query run's list's: no field of Find/Change's is its (Apply's note)
	KFCQueryCopy now;
	Capture(now);
	const char* const differs = FirstDifference(copy, now);
	if (differs == nil)
		return true;
	return Failed(outWhat, differs);
}

#ifdef KFC_DIAG
namespace
{
	// Field n of `c` made different - the armed probe's mutations (KFCQueryCopy.h lists them).
	void MakeDifferent(KFCQueryCopy& c, int n)
	{
		switch (n)
		{
			case 1: c.replaceString.Append("#"); break;
			case 2: c.caseSensitive = c.caseSensitive ? kFalse : kTrue; break;
			case 3: c.changeAttrs.reset(); break;
			case 4: c.findGlyph = c.findGlyph + 1; break;
			case 5: c.findParaStyle = kInvalidUID; break;
			case 6: c.includeFootnotes = c.includeFootnotes ? kFalse : kTrue; break;
			case 7: c.replaceCharType = (c.replaceCharType == FCO::kFullWidthKatakana) ? FCO::kHalfWidthKatakana : FCO::kFullWidthKatakana; break;
			case 8: c.objectType = (c.objectType == FCO::kTextFrames) ? FCO::kGraphicFrames : FCO::kTextFrames; break;
			case 9: c.replaceGlyph = c.replaceGlyph + 1; break;
			default: break;
		}
	}

	// Everything Find/Change holds cleared - the roundtrip's "a different query" (KFCQuerySequence clears the same way).
	bool ClearFindChange()
	{
		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kClearFindChangeOptionsCmdBoss));
		return cmd != nil && KFCFindChangeCmds::ProcessFindChangeCmd(cmd);
	}
}

bool KFCQueryCopier::DiagProbe(PMString& outSummary)
{
	char line[700] = { 0 };
	if (KFC_DIAG_FAULT("qcopy-roundtrip"))
	{
		KFCDiagFaultOff("qcopy-roundtrip");
		KFCQueryCopy a;
		Capture(a);
		const bool cleared = ClearFindChange();
		PMString whatApply, whatMatch;
		const bool applied = Apply(a, whatApply);
		const bool matched = Matches(a, whatMatch);
		_snprintf_s(line, sizeof(line), _TRUNCATE, "QCOPY roundtrip mode=%d valid=%d cleared=%d apply=%d match=%d what=%s|%s",
			static_cast<int>(a.mode), a.valid ? 1 : 0, cleared ? 1 : 0, applied ? 1 : 0, matched ? 1 : 0,
			whatApply.GetUTF8String().c_str(), whatMatch.GetUTF8String().c_str());
	}
	else if (KFC_DIAG_FAULT("qcopy-armed"))
	{
		const int field = KFCDiagFaultValue("qcopy-armed", 0, 0);
		KFCDiagFaultOff("qcopy-armed");
		KFCQueryCopy a;
		Capture(a);
		KFCQueryCopy b = a;
		MakeDifferent(b, field);
		PMString whatB, whatCaught, whatA, whatRestored;
		const bool appliedB = Apply(b, whatB);
		const bool caught = !Matches(a, whatCaught);
		const bool appliedA = Apply(a, whatA);
		const bool restored = Matches(a, whatRestored);
		_snprintf_s(line, sizeof(line), _TRUNCATE,
			"QCOPY armed field=%d mode=%d applyB=%d caught=%d what=%s applyA=%d restored=%d what=%s|%s",
			field, static_cast<int>(a.mode), appliedB ? 1 : 0, caught ? 1 : 0, whatCaught.GetUTF8String().c_str(),
			appliedA ? 1 : 0, restored ? 1 : 0, whatA.GetUTF8String().c_str(), whatRestored.GetUTF8String().c_str());
	}
	else if (KFC_DIAG_FAULT("qcopy-abort"))
	{
		KFCDiagFaultOff("qcopy-abort");
		KFCQueryCopy a;
		Capture(a);
		KFCQueryCopy b = a;
		MakeDifferent(b, UsesStrings(a.mode) ? 1 : 6);
		PMString whatB, whatMid, whatAfter;
		IAbortableCmdSeq* const seq = CmdUtils::BeginAbortableCmdSeq("KFC qcopy-abort");
		const bool appliedB = (seq != nil) && Apply(b, whatB);
		const bool midDiffers = !Matches(a, whatMid);
		if (seq != nil)
			CmdUtils::AbortCommandSequence(seq);
		const bool rolledBack = Matches(a, whatAfter);
		if (!rolledBack)
		{
			PMString ignored;
			(void)Apply(a, ignored);		// the person's query put back, whatever the abort did
		}
		_snprintf_s(line, sizeof(line), _TRUNCATE, "QCOPY abort mode=%d seq=%d applyB=%d middiffers=%d rolledback=%d what=%s",
			static_cast<int>(a.mode), (seq != nil) ? 1 : 0, appliedB ? 1 : 0, midDiffers ? 1 : 0, rolledBack ? 1 : 0,
			whatAfter.GetUTF8String().c_str());
	}
	else
		return false;
	KFC_DIAG_LOG("%s", line);
	outSummary.Clear();
	outSummary.SetTranslatable(kFalse);
	outSummary.Append(line);
	return true;
}
#endif

// End, KFCQueryCopy.cpp.
