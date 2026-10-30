//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Ruby tab, listed - see KFCRubySearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"
#include "IClassIDData.h"				// which strand kPrivateCreateStrandCmdBoss makes
#include "ICommand.h"
#include "IComposeScanner.h"			// QueryAttributeAt - a run's reading
#include "IDataBase.h"
#include "IRubyStrand.h"				// IRubyAttrStrand - the file is IRubyStrand.h, the class is not
#include "IStoryList.h"
#include "ITextAttrBoolean.h"			// kTARubyAttrBoss, kTAMojiRubyBoss
#include "ITextAttrWideString.h"		// kTARubyStringBoss - the reading itself
#include "ITextModel.h"
#include "ITextModelCmds.h"				// ApplyCmd, ClearOverridesCmd
#include "ITextStoryThread.h"
#include "IUIDData.h"
#include "IWalkerScopeFactoryUtils.h"	// kAllDocumentScope - the tab's Search:

// General includes:
#include "AttributeBossList.h"
#include "CJKID.h"						// kRubyAttrStrandBoss and the ruby attributes
#include "CmdUtils.h"
#include "ConditionalTextID.h"			// kHiddenTextBoss
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "TextID.h"						// kCharAttrStrandBoss, kPrivateCreateStrandCmdBoss
#include "Utils.h"

#include <string>

// Project includes:
#include "KFCRubySearch.h"
#include "IKFCUIServices.h"				// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line
#include "KFCTextCut.h"					// KFCSafeCut

namespace
{
// A document's share of the bar (KFCQRSearch's number: a walk with nothing to say about its inside).
const int32 kRubyDocSpan = 100;
// What a row draws for a run, at most: the base and the reading, each cut (a run list's row holds about as much - KFCRunList.cpp).
const int32 kRowWordsBase = 20;
const int32 kRowWordsReading = 28;

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note (KFCTableSearch.cpp's StandsInSetAsideText's rule).
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// Can a person see anything of the reading? (A footnote's marker inherits a control character as its reading - KIDMCPRuby.cpp's HasVisible, measured there.)
bool HasVisible(const WideString& reading)
{
	for (WideString::const_iterator c = reading.begin(); c != reading.end(); ++c)
	{
		const uint32 v = static_cast<uint32>(*c);
		if (v >= 0x20 && v != 0x7F)
			return true;
	}
	return false;
}

// The reading of [at, end), read as the composer reads it - empty when it carries none a person can see.
WideString ReadingOfRun(IComposeScanner* scanner, TextIndex at, TextIndex end)
{
	WideString none;
	InterfacePtr<const IAttrReport> attr(scanner->QueryAttributeAt(at, end, kTARubyStringBoss));
	InterfacePtr<ITextAttrWideString> reading(attr, UseDefaultIID());
	if (reading == nil || !HasVisible(reading->Get()))
		return none;
	return reading->Get();
}

// Does the strand have a run on, with a reading a person can see, anywhere in [from, to)?
bool RubyAnywhereIn(IRubyAttrStrand* strand, IComposeScanner* scanner, TextIndex from, TextIndex to)
{
	TextIndex at = from;
	while (at < to)
	{
		int32 len = 0;
		TextIndex runBegin = at;
		const bool16 on = strand->GetRubyRun(at, &len, &runBegin);
		if (len <= 0)
			return false;
		const TextIndex runEnd = at + len;
		if (on != kFalse && !ReadingOfRun(scanner, at, runEnd).empty())
			return true;
		at = runEnd;
	}
	return false;
}

PMString Plain(const WideString& words)
{
	PMString out(words);
	out.SetTranslatable(kFalse);
	return out;
}

// Does `words` hold `part` (an empty part is in everything)? Character for character: kana and case as they stand.
bool Holds(const WideString& words, const PMString& part)
{
	return part.IsEmpty() || Plain(words).IndexOfString(part) >= 0;
}

// THE RUNS OF ONE STORY WITH RUBY, by the conditions.
void WalkWithRuby(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (strand == nil || scanner == nil)
		return;		// no strand: no ruby anywhere in this story (SnpPerformTextAttrRuby's rule)
	const TextIndex total = model->TotalLength();
	TextIndex at = 0;
	while (at < total)
	{
		int32 len = 0;
		TextIndex runBegin = at;
		const bool16 on = strand->GetRubyRun(at, &len, &runBegin);
		if (len <= 0)
			break;
		const TextIndex runEnd = at + len;
		if (on != kFalse)
		{
			const WideString reading(ReadingOfRun(scanner, at, runEnd));
			if (!reading.empty() && !StandsInSetAsideText(model, at) && Holds(reading, asked.reading))
			{
				const WideString base(KFCSearchEngine::ReadText(storyRef, at, len));
				if (Holds(base, asked.base))
				{
					if (ioPlaces.size() >= limit)
					{
						outCapped = true;
						return;
					}
					KFCSearchEngine::ListedPlace place;
					place.story = storyRef.GetUID();
					place.start = at;
					place.end = runEnd;
					ioPlaces.push_back(place);
				}
			}
		}
		at = runEnd;
	}
}

// THE OCCURRENCES OF THE BASE TEXT IN ONE STORY WITH NO RUBY ON THEM.
void WalkWithoutRuby(const UIDRef& storyRef, ITextModel* model, const KFCRubyConditions& asked, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const std::u32string text([&] {
		std::u32string out;
		const WideString all(KFCSearchEngine::ReadText(storyRef, 0, model->TotalLength()));
		out.reserve(static_cast<size_t>(model->TotalLength()));
		for (WideString::const_iterator c = all.begin(); c != all.end(); ++c)
			out.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
		return out; }());
	std::u32string wanted;
	const WideString wantedWide(asked.base);
	for (WideString::const_iterator c = wantedWide.begin(); c != wantedWide.end(); ++c)
		wanted.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	if (wanted.empty())
		return;
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	for (size_t at = text.find(wanted); at != std::u32string::npos; at = text.find(wanted, at + wanted.size()))
	{
		const TextIndex start = static_cast<TextIndex>(at);
		const TextIndex end = start + static_cast<int32>(wanted.size());
		if (StandsInSetAsideText(model, start))
			continue;
		if (strand != nil && scanner != nil && RubyAnywhereIn(strand, scanner, start, end))
			continue;
		if (ioPlaces.size() >= limit)
		{
			outCapped = true;
			return;
		}
		KFCSearchEngine::ListedPlace place;
		place.story = storyRef.GetUID();
		place.start = start;
		place.end = end;
		ioPlaces.push_back(place);
	}
}

// ONE DOCUMENT'S ROWS. False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const KFCRubyConditions& asked, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
	std::map<UID, uint32>& outVersions, bool& outCapped)
{
	outCapped = false;
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return false;
	// Composing to read a row's page dirties nothing the person made (a search's walk's rule).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	std::vector<KFCSearchEngine::ListedPlace> places;
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count && !outCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;
		if (asked.withoutRuby)
			WalkWithoutRuby(storyRef, model, asked, limit, places, outCapped);
		else
			WalkWithRuby(storyRef, model, asked, limit, places, outCapped);
	}
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	// What the rows with ruby draw there; a row without any draws its text.
	for (size_t h = 0; !asked.withoutRuby && h < outHits.size() && h < places.size(); ++h)
	{
		const UIDRef storyRef(db, places[h].story);
		outHits[h].runChain = KFCRubySearch::RowWords(storyRef, places[h].start, places[h].end, KFCRubySearch::ReadingOn(storyRef, places[h].start, places[h].end));
		outHits[h].runChain.SetTranslatable(kFalse);
	}
	return true;
}

ErrorCode CreateStrandIfNeeded(ITextModel* model)
{
	InterfacePtr<IRubyAttrStrand> existing(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	if (existing != nil)
		return kSuccess;
	// (SnpPerformTextAttrRuby: without it "Resulting ruby composition will be unstable!")
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kPrivateCreateStrandCmdBoss));
	InterfacePtr<IUIDData> modelData(cmd, UseDefaultIID());
	InterfacePtr<IClassIDData> strandData(cmd, UseDefaultIID());
	if (cmd == nil || modelData == nil || strandData == nil)
		return kFailure;
	modelData->Set(model);
	strandData->Set(kRubyAttrStrandBoss);
	return CmdUtils::ProcessCommand(cmd);
}
}	// anonymous namespace

bool KFCRubySearch::IsTabCurrent(KFCRubyConditions* outConditions)
{
	KFCRubyConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch ruby-tab (the model's cases, which need no dialog): on = the Ruby tab, its lines its conditions.
	if (KFC_DIAG_FAULT("ruby-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("ruby-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
		{
			const std::string& line = lines[n];
			PMString value;
			value.SetTranslatable(kFalse);
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				value.SetUTF8String(line.substr(eq + 1));
			if (line == "kind=without")
				conditions.withoutRuby = true;
			else if (line.compare(0, 5, "base=") == 0)
				conditions.base = value;
			else if (line.compare(0, 5, "ruby=") == 0)
				conditions.reading = value;
			else if (line.compare(0, 7, "change=") == 0)
				conditions.changeTo = value;
			else if (line == "remove=1")
				conditions.removeRuby = true;
			else if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
		}
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCQRSearch::IsTabCurrent's rule): no UI half is no Ruby tab.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->RubyTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCRubySearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCRubyConditions& asked)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kRubyDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;		// the object lists' number (the spec's R5)
	const size_t limit = static_cast<size_t>(room > 0 ? room : 0);
	for (size_t i = 0; i < targets.size(); ++i)
	{
		KFCSetCountedTask(bar, fromBook ? "Chapter" : "Document", i, targets.size(), targets[i].shortName);
		bar.SetPosition(shares[i]);
		if (bar.WasCancelled(kFalse))
		{
			out.cancelled = true;
			break;
		}
		if (static_cast<size_t>(out.total) >= limit)
		{
			out.truncated = true;
			break;
		}
		if (fromBook && targets[i].docRef == UIDRef::gNull && !KFCBookScope::OpenChapterDoc(targets[i], &unopenable))
			continue;
		const UIDRef docRef = targets[i].docRef;
		std::vector<KFCResultModel::Hit> hits;
		std::map<UID, uint32> versions;
		bool capped = false;
		const bool walked = WalkDoc(docRef, asked, limit - static_cast<size_t>(out.total), hits, versions, capped);
		// A book's chapter goes back now, closed on the spot (one chapter of ours at a time - the other walks' rule).
		if (fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
			out.unclosed.push_back(targets[i].shortName);
		if (!walked)
		{
			PMString name(targets[i].shortName);
			name.SetTranslatable(kFalse);
			name.Append(": no story list");
			out.unsearchable.push_back(name);
			continue;
		}
		if (capped)
			out.truncated = true;
		if (!hits.empty())
			KFCSearchEngine::FileChapter(targets[i], hits, out, &versions);		// every walk's filing: page order, the tally
		if (capped)
			break;
	}
	if (!out.cancelled && bar.WasCancelled(kFalse))
		out.cancelled = true;
	bar.SetPosition(barEnd);
}

bool KFCRubySearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCRubyConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kRubyDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, asked, limit, outHits, outVersions, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kRubyDocSpan);
	return walked && !outCancelled;
}

WideString KFCRubySearch::ReadingOn(const UIDRef& story, TextIndex start, TextIndex end)
{
	WideString none;
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(story, UseDefaultIID());
	if (model == nil || scanner == nil || end <= start)
		return none;
	InterfacePtr<IRubyAttrStrand> strand(static_cast<IRubyAttrStrand*>(model->QueryStrand(kRubyAttrStrandBoss, IRubyAttrStrand::kDefaultIID)));
	if (strand == nil)
		return none;
	int32 len = 0;
	TextIndex runBegin = start;
	if (strand->GetRubyRun(start, &len, &runBegin) == kFalse || len <= 0)
		return none;
	return ReadingOfRun(scanner, start, start + len);
}

PMString KFCRubySearch::RowWords(const UIDRef& story, TextIndex start, TextIndex end, const WideString& reading)
{
	PMString words;
	words.SetTranslatable(kFalse);
	PMString base(KFCSearchEngine::ReadText(story, start, end - start));
	base.SetTranslatable(kFalse);
	if (reading.empty())
		return PMString();		// (no reading: the row draws its own text)
	KFCResultModel::MarkUpBreaksForDisplay(base);		// (as a row's match is drawn - a run's break or anchor is no character to draw)
	if (base.CharCount() > kRowWordsBase)
	{
		const int32 keep = KFCSafeCut(base, kRowWordsBase);
		base.Remove(keep > 0 ? keep : kRowWordsBase, kMaxInt32);
		base.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	PMString said(Plain(reading));
	KFCResultModel::MarkUpBreaksForDisplay(said);
	if (said.CharCount() > kRowWordsReading)
	{
		const int32 keep = KFCSafeCut(said, kRowWordsReading);
		said.Remove(keep > 0 ? keep : kRowWordsReading, kMaxInt32);
		said.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	words.Append(base);
	words.AppendW(UTF32TextChar(0x300A));		// the book title marks: the page's own way of writing a reading beside its base
	words.Append(said);
	words.AppendW(UTF32TextChar(0x300B));
	return words;
}

ErrorCode KFCRubySearch::Apply(const UIDRef& story, TextIndex start, TextIndex end, const WideString& reading)
{
	InterfacePtr<ITextModel> model(story, UseDefaultIID());
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (model == nil || cmds == nil)
		return kFailure;
	const bool hadRuby = !ReadingOn(story, start, end).empty();
	const ErrorCode made = CreateStrandIfNeeded(model);
	if (made != kSuccess)
		return made;
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	if (attrs.get() == nil)
		return kFailure;
	InterfacePtr<ITextAttrBoolean> on(::CreateObject2<ITextAttrBoolean>(kTARubyAttrBoss));
	InterfacePtr<ITextAttrWideString> said(::CreateObject2<ITextAttrWideString>(kTARubyStringBoss));
	if (on == nil || said == nil)
		return kFailure;
	on->Set(kTrue);
	said->Set(reading);
	InterfacePtr<IAttrReport> onReport(on, UseDefaultIID());
	InterfacePtr<IAttrReport> saidReport(said, UseDefaultIID());
	attrs->ApplyAttribute(onReport);
	attrs->ApplyAttribute(saidReport);
	if (!hadRuby && end - start > 1)
	{
		// NEW RUBY OVER SEVERAL CHARACTERS: A GROUP (the reading over the whole - kTAMojiRubyBoss kFalse; InDesign's default mono would lay it out character by character).
		InterfacePtr<ITextAttrBoolean> mono(::CreateObject2<ITextAttrBoolean>(kTAMojiRubyBoss));
		if (mono == nil)
			return kFailure;
		mono->Set(kFalse);
		InterfacePtr<IAttrReport> monoReport(mono, UseDefaultIID());
		attrs->ApplyAttribute(monoReport);
	}
	InterfacePtr<ICommand> apply(cmds->ApplyCmd(RangeData(start, end), attrs, kCharAttrStrandBoss));
	return (apply != nil) ? CmdUtils::ProcessCommand(apply) : kFailure;
}

ErrorCode KFCRubySearch::Remove(const UIDRef& story, TextIndex start, TextIndex end)
{
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	// ALL THIRTY, NOT THE FEW A REPLACE WRITES: a person's font, size or colour for the ruby left on the text is "a set of instructions for ruby that is not there"
	// (KIDMCPRuby.cpp's ClearRubyOn). Only the class IDs matter in this list (ITextModelCmds.h, ClearOverridesCmd).
	static const ClassID kRubyAttrs[] =
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
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	if (attrs.get() == nil)
		return kFailure;
	for (size_t i = 0; i < sizeof(kRubyAttrs) / sizeof(kRubyAttrs[0]); ++i)
	{
		InterfacePtr<IAttrReport> report(::CreateObject2<IAttrReport>(kRubyAttrs[i]));
		if (report == nil)
			return kFailure;		// all of them or none: a partial clear would be said as done (KIDMCPRuby.cpp's note)
		attrs->ApplyAttribute(report);
	}
	InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(start, end - start, attrs, kCharAttrStrandBoss));
	return (clear != nil) ? CmdUtils::ProcessCommand(clear) : kFailure;
}

// End, KFCRubySearch.cpp.
