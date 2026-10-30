//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A match's overrides - read and cleared. See KFCTextOverrides.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"
#include "IAttributeStrand.h"		// GetLocalOverrides - a run's own attributes, character or paragraph level
#include "ICommand.h"
#include "IComposeScanner.h"		// FindSurroundingParagraph - the paragraphs a match stands in
#include "ITextAttrUtils.h"			// GetTotalAttributesAtIndex - InDesign's own "is the style overridden here"
#include "ITextModel.h"
#include "ITextModelCmds.h"			// ClearOverridesCmd - the attributes named taken off, one command a strand

// General includes:
#include "CJKID.h"					// ruby's thirty attributes
#include "CmdUtils.h"
#include "ConditionalTextID.h"		// kConditionalTextAttributeBoss - the conditions applied to text
#include "CreateObject.h"			// ::CreateObject2 - an attribute boss to name a class in a list
#include "TextID.h"					// kCharAttrStrandBoss, kParaAttrStrandBoss
#include "UIDRef.h"
#include "Utils.h"

#include <cstdio>
#include <string>

// Project includes:
#include "KFCTextOverrides.h"
#include "KFCDiag.h"				// KFC_DIAG_LOG - the OVERRIDES trace, test builds only

namespace
{

// RUBY IS NOT AN OVERRIDE TO INDESIGN (measured 10-30: the DOM's styleOverridden reads false for a word whose only own
// attributes are its ruby's), so Clear Overrides leaves it where InDesign's own leaves it (case overrides-clear-twin holds
// the two side by side). The thirty attributes CJKID.h holds for ruby - the list KIDMCP's ruby tool clears
// (KIDMCPRuby.cpp ClearRubyOn: checked there against SnpPerformTextAttrRuby::ApplyRuby's, both ways).
const ClassID kRubyAttrs[] =
{
	kTARubyAttrBoss,			kTARubyStringBoss,			kTAMojiRubyBoss,
	kTARubyOTProBoss,			kTARubyPointSizeBoss,		kTARubyRelativeSizeBoss,
	kTARubyAlignmentBoss,		kTARubyFontUIDBoss,			kTARubyFontStyleBoss,
	kTARubyAdjustParentBoss,	kTARubyXScaleBoss,			kTARubyYScaleBoss,
	kTARubyXOffsetBoss,			kTARubyYOffsetBoss,			kTARubyPositionBoss,
	kTARubyEdgeSpaceBoss,		kTARubyOverhangBoss,		kTARubyOverhangFlagBoss,
	kTARubyAutoScalingBoss,		kTARubyAutoScaleMinBoss,	kTARubyColorBoss,
	kTARubyTintBoss,			kTARubyOverprintBoss,		kTARubyStrokeColorBoss,
	kTARubyStrokeTintBoss,		kTARubyStrokeOverprintBoss,	kTARubyOutlineBoss,
	kTARubyAutoTCYNumDigitsBoss,kTARubyAutoTCYIncludeRomanBoss,
	kTARubyAutoTCYAutoScaleBoss
};

// IS IT AN ATTRIBUTE INDESIGN DOES NOT COUNT AS AN OVERRIDE - ruby's thirty, and the conditions applied to the text: kept on
// the text "as a style override" (IConditionalTextFacade.h - kConditionalTextAttributeBoss), but the DOM's styleOverridden
// reads false for a word whose only one is a condition (B0), and the DOM's clearOverrides(ALL) leaves the condition on a word
// whose 80% it takes off (measured 10-30, case overrides-clear-twin-mixed: KFC's Clear Overrides had taken the condition
// along). Clear Overrides leaves both where InDesign's own leaves them.
bool IsNoOverride(ClassID attr)
{
	if (attr == kConditionalTextAttributeBoss)
		return true;
	for (size_t i = 0; i < sizeof(kRubyAttrs) / sizeof(kRubyAttrs[0]); ++i)
		if (kRubyAttrs[i] == attr)
			return true;
	return false;
}

// THE NAMES OF `own`'S ATTRIBUTES, ruby's and the conditions left out (IsNoOverride), added to `into` once each. A new boss
// of each class names it - the list's values are never read (ITextModelCmds.h: "attributes to clear"; KIDMCPRuby.cpp
// builds its list the same way).
// false = an attribute's boss could not be made (the list would then clear less than it says).
bool AddNames(const AttributeBossList* own, AttributeBossList& into)
{
	if (own == nil)
		return true;
	for (int32 n = 0; n < own->CountBosses(); ++n)
	{
		const ClassID attr = own->GetClassN(n);
		if (IsNoOverride(attr) || into.ContainsAttribute(attr))
			continue;
		InterfacePtr<IAttrReport> name(::CreateObject2<IAttrReport>(attr));
		if (name == nil)
			return false;
		into.ApplyAttribute(name);
	}
	return true;
}

#ifdef KFC_DIAG
// (Test builds only) A list's classes as "0x...,0x..." - the OVERRIDES trace (which attribute a kind of formatting is).
std::string DiagClasses(const AttributeBossList& list)
{
	std::string out;
	for (int32 n = 0; n < list.CountBosses(); ++n)
	{
		char one[16] = {};
		std::snprintf(one, sizeof(one), "%s0x%x", n ? "," : "", static_cast<unsigned>(list.GetClassN(n).Get()));
		out += one;
	}
	return out;
}
#endif

}	// anonymous namespace

bool KFCTextOverrides::HasOverrides(ITextModel* model, TextIndex start, TextIndex end)
{
	Utils<ITextAttrUtils> attrUtils;
	if (model == nil || !attrUtils || start < 0 || end <= start || end > model->TotalLength())
		return false;
	// RUN BY RUN - a run is text of the same attributes (the utility's start / end), so each is asked once.
	for (TextIndex at = start; at < end; )
	{
		AttributeBossList total;
		int32 runStart = at, runEnd = at;
		bool16 paraOverridden = kFalse, charOverridden = kFalse;
		attrUtils->GetTotalAttributesAtIndex(model, at, kFalse /*includeTextState*/, kFalse /*leanleft*/, total,
			&runStart, &runEnd, nil, nil, &paraOverridden, nil, nil, &charOverridden);
		KFC_DIAG_LOG("OVERRIDES ask at=%d run=%d..%d para=%d char=%d", static_cast<int>(at), static_cast<int>(runStart),
			static_cast<int>(runEnd), paraOverridden ? 1 : 0, charOverridden ? 1 : 0);
		if (paraOverridden || charOverridden)
			return true;
		// ON TO THE NEXT RUN, at least one character on: whether `runEnd` is the run's end (past its last character) or its
		// last character is not written in ITextAttrUtils.h and nothing in the SDK calls it - read as the last character,
		// stepping to it would ask the same run again; one character past it is the next run either way.
		at = (runEnd > at) ? runEnd : at + 1;
	}
	return false;
}

bool KFCTextOverrides::CollectOverrides(ITextModel* model, TextIndex start, TextIndex end,
	boost::shared_ptr<AttributeBossList>& outChar, boost::shared_ptr<AttributeBossList>& outPara, TextIndex& outParaStart,
	int32& outParaLength)
{
	outChar.reset(new AttributeBossList);
	outPara.reset(new AttributeBossList);
	outParaStart = start;
	outParaLength = 0;
	if (model == nil || outChar.get() == nil || outPara.get() == nil || start < 0 || end <= start || end > model->TotalLength())
		return false;
	InterfacePtr<IAttributeStrand> chars(static_cast<IAttributeStrand*>(model->QueryStrand(kCharAttrStrandBoss,
		IID_IATTRIBUTESTRAND)));
	InterfacePtr<IAttributeStrand> paras(static_cast<IAttributeStrand*>(model->QueryStrand(kParaAttrStrandBoss,
		IID_IATTRIBUTESTRAND)));
	if (chars == nil || paras == nil)
		return false;
	// The character level: every run of the range.
	for (TextIndex at = start; at < end; )
	{
		int32 count = 0;
		const DataWrapper<AttributeBossList> own = chars->GetLocalOverrides(at, &count);
		if (!AddNames(own.get(), *outChar))
			return false;
		at += (count > 0) ? count : 1;		// (one character on at least - a run of no length is not asked again)
	}
	// The paragraph level: the paragraphs the range stands in, from the first one's start to the last one's return - read by
	// IComposeScanner::FindSurroundingParagraph (the story's end counted: KFCReplaceEngine's ParagraphOf reading), NOT by the
	// strand's runs: the paragraph attribute strand keeps neighbouring paragraphs of the same attributes as ONE run (measured
	// 10-30 - one paragraph's match read a run over eight paragraphs), and a run's range took a neighbour's override along
	// (case overrides-clear-para-neighbour: the next paragraph's centring went with the match's).
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return false;
	int32 firstLength = 0, lastLength = 0;
	const TextIndex first = scanner->FindSurroundingParagraph(start, &firstLength, kFalse);
	const TextIndex last = scanner->FindSurroundingParagraph(end - 1, &lastLength, kFalse);
	if (first < 0 || last < first || firstLength <= 0 || lastLength <= 0)
		return false;
	outParaStart = first;
	outParaLength = (last + lastLength) - first;
	for (TextIndex at = first; at < outParaStart + outParaLength; )
	{
		int32 count = 0;
		const DataWrapper<AttributeBossList> own = paras->GetLocalOverrides(at, &count);
		if (!AddNames(own.get(), *outPara))
			return false;
		at += (count > 0) ? count : 1;		// (one character on at least, as above)
	}
	KFC_DIAG_LOG("OVERRIDES own at=%d..%d char=[%s] para=[%s] paras=%d..%d", static_cast<int>(start), static_cast<int>(end),
		DiagClasses(*outChar).c_str(), DiagClasses(*outPara).c_str(), static_cast<int>(outParaStart),
		static_cast<int>(outParaStart + outParaLength));
	return true;
}

ErrorCode KFCTextOverrides::ClearOverrides(const UIDRef& story, TextIndex start, TextIndex end, bool& outHadAny)
{
	outHadAny = false;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (model == nil || cmds == nil)
		return kFailure;
	// NOTHING WRITTEN WHERE INDESIGN SEES NO OVERRIDE (the design's B3 - the row counted, not written).
	if (!HasOverrides(model, start, end))
		return kSuccess;
	boost::shared_ptr<AttributeBossList> chars, paras;
	TextIndex paraStart = start;
	int32 paraLength = 0;
	if (!CollectOverrides(model, start, end, chars, paras, paraStart, paraLength))
		return kFailure;
	if (chars->CountBosses() == 0 && (paras->CountBosses() == 0 || paraLength <= 0))
		return kSuccess;		// overridden by nothing it may clear (ruby's alone): left as it is
	outHadAny = true;
	if (chars->CountBosses() > 0)
	{
		InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(start, end - start, chars, kCharAttrStrandBoss));
		if (clear == nil || CmdUtils::ProcessCommand(clear) != kSuccess)
			return kFailure;
	}
	if (paras->CountBosses() > 0 && paraLength > 0)
	{
		InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(paraStart, paraLength, paras, kParaAttrStrandBoss));
		if (clear == nil || CmdUtils::ProcessCommand(clear) != kSuccess)
			return kFailure;
	}
	return kSuccess;
}

// End, KFCTextOverrides.cpp.
