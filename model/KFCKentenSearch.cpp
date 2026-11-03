//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Kenten kind, listed - see KFCKentenSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAttrReport.h"
#include "ICommand.h"
#include "IComposeScanner.h"			// QueryAttributeAt - a stretch's kind of mark, as the composer reads it
#include "IDataBase.h"
#include "IKentenStyle.h"				// the kinds
#include "IStoryList.h"
#include "ITextAttrInt16.h"				// kTAKentenKindBoss, kTAKentenCharacterBoss, kTAKentenCharacterSetBoss
#include "ITextModel.h"
#include "ITextModelCmds.h"				// ApplyCmd, ClearOverridesCmd
#include "ITextStoryThread.h"
#include "IWalkerScopeFactoryUtils.h"	// its Search:

// General includes:
#include "AttributeBossList.h"
#include "CJKID.h"						// kTAKentenKindBoss
#include "CmdUtils.h"
#include "ConditionalTextID.h"			// kHiddenTextBoss
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "TextID.h"						// kCharAttrStrandBoss
#include "Utils.h"

#include <cstdlib>						// strtol - the fault switch's characters, in hex
#include <string>

// Project includes:
#include "KFCKentenSearch.h"
#include "IKFCUIServices.h"				// the tab itself is the UI half's
#include "KFCDiag.h"
#include "KFCFold.h"					// what Kana Sensitive and Width Sensitive take for the same
#include "KFCOwnScope.h"				// what the icons take in
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line
#include "KFCTextCut.h"					// KFCSafeCut

namespace
{
// A document's share of the bar (KFCQRSearch's number: a walk with nothing to say about its inside).
const int32 kKentenDocSpan = 100;
// What a row draws of a stretch's text, at most (a ruby row's base's number).
const int32 kRowWordsBase = 20;

// IN TEXT THE PAGE DOES NOT SET - hidden by a condition, deleted under Track Changes, in a note (KFCTableSearch.cpp's StandsInSetAsideText's rule).
bool StandsInSetAsideText(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// NO ROW RUNS OVER THESE: a paragraph's end, a table's or an anchor's character, anything of InDesign's in the text (KFCRubySearch.cpp's StopsMark).
bool StopsRow(char32_t c)
{
	return c < 0x20 || c == 0xFEFF || (c >= 0xE000 && c <= 0xF8FF);
}

std::u32string U32Of(const PMString& words)
{
	std::u32string out;
	const WideString wide(words);
	for (WideString::const_iterator c = wide.begin(); c != wide.end(); ++c)
		out.push_back(static_cast<char32_t>(static_cast<uint32>(*c)));
	return out;
}

// THE KIND OF MARK AT `at` AND HOW FAR IT GOES, up to `until` (joined where it is unchanged, by the scanner): as the composer reads it, a character style's too.
int32 MarkAt(IComposeScanner* scanner, TextIndex at, TextIndex until, int32& outLength)
{
	outLength = 0;
	InterfacePtr<const IAttrReport> attr(scanner->QueryAttributeAt(at, until, kTAKentenKindBoss, &outLength));
	InterfacePtr<const ITextAttrInt16> kind(attr, UseDefaultIID());
	return (kind != nil) ? static_cast<int32>(kind->Get()) : static_cast<int32>(IKentenStyle::Kenten_None);
}

// THE CHARACTER OF THE CUSTOM MARK AT `at` AND HOW FAR IT GOES, up to `until` (MarkAt's way): kTAKentenCharacterBoss's 16 bits as a code point; 0 = none.
int32 CustomCharAt(IComposeScanner* scanner, TextIndex at, TextIndex until, int32& outLength)
{
	outLength = 0;
	InterfacePtr<const IAttrReport> attr(scanner->QueryAttributeAt(at, until, kTAKentenCharacterBoss, &outLength));
	InterfacePtr<const ITextAttrInt16> character(attr, UseDefaultIID());
	return (character != nil) ? static_cast<int32>(static_cast<uint16>(character->Get())) : 0;
}

// Is any mark on [from, to)?
bool MarkAnywhereIn(IComposeScanner* scanner, TextIndex from, TextIndex to)
{
	TextIndex at = from;
	while (at < to)
	{
		int32 len = 0;
		if (MarkAt(scanner, at, to, len) != IKentenStyle::Kenten_None)
			return true;
		if (len <= 0)
			return false;
		at += len;
	}
	return false;
}

bool Holds(const std::u32string& words, const std::u32string& part)
{
	return part.empty() || words.find(part) != std::u32string::npos;
}

// ONE STRETCH OF ONE MARK, [at, at + len), listed: cut where StopsRow stands, each piece a row when its text holds Base Text. False = the list is full (outCapped).
bool ListStretch(const UIDRef& storyRef, ITextModel* model, const KFCFold::Flags& flags, const std::u32string& wantedBase, KFCOwnScope& taken,
	const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit, TextIndex at, int32 len, std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const std::u32string text(U32Of(KFCSearchEngine::ReadText(storyRef, at, len)));
	size_t from = 0;
	for (size_t k = 0; k <= text.size(); ++k)
	{
		if (k < text.size() && !StopsRow(text[k]))
			continue;
		const TextIndex start = at + static_cast<TextIndex>(from);
		const TextIndex end = at + static_cast<TextIndex>(k);
		if (k > from && Holds(KFCFold::TextOf(std::u32string(text, from, k - from), flags), wantedBase)
			&& (part == nil || KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), start, end))
			&& taken.Takes(storyRef, model, start))
		{
			if (ioPlaces.size() >= limit)
			{
				outCapped = true;
				return false;
			}
			KFCSearchEngine::ListedPlace place;
			place.story = storyRef.GetUID();
			place.start = start;
			place.end = end;
			ioPlaces.push_back(place);
		}
		from = k + 1;
	}
	return true;
}

// THE STRETCHES OF MARKS OF ONE STORY, by the conditions: each thread's on its own (a table's cells are threads of its own, and the scanner would run on across them), cut where StopsRow stands - and a custom
// mark where its character changes (2026-11-03 - docs/superpowers/specs/2026-11-03-kfc-kenten-face-japanese-design.md C6: two characters side by side are two marks), Kenten: custom with a character in its box
// taking the custom marks of that character alone (C4).
void WalkWithKenten(const UIDRef& storyRef, ITextModel* model, const KFCKentenConditions& asked, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	const std::u32string wantedBase(KFCFold::TextOf(U32Of(asked.base), flags));
	const TextIndex total = model->TotalLength();
	TextIndex at = 0;
	while (at < total)
	{
		TextIndex threadStart = at;
		int32 threadSpan = 0;
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, &threadSpan));
		const TextIndex until = (thread != nil && threadStart + threadSpan > at) ? threadStart + threadSpan : total;
		int32 len = 0;
		const int32 mark = MarkAt(scanner, at, until, len);
		if (len <= 0)
			break;
		const TextIndex runEnd = at + len;
		if (mark != IKentenStyle::Kenten_None && (asked.mark == 0 || asked.mark == mark) && !StandsInSetAsideText(model, at))
		{
			TextIndex pieceAt = at;
			while (pieceAt < runEnd)
			{
				int32 pieceLen = runEnd - pieceAt;
				int32 customChar = 0;
				if (mark == IKentenStyle::Kenten_Custom)
				{
					int32 sameLen = 0;
					customChar = CustomCharAt(scanner, pieceAt, runEnd, sameLen);
					if (sameLen > 0 && sameLen < pieceLen)
						pieceLen = sameLen;
				}
				if ((asked.mark != IKentenStyle::Kenten_Custom || asked.markChar == 0 || asked.markChar == customChar)
					&& !ListStretch(storyRef, model, flags, wantedBase, taken, part, limit, pieceAt, pieceLen, ioPlaces, outCapped))
					return;
				pieceAt += pieceLen;
			}
		}
		at = runEnd;
	}
}

// THE OCCURRENCES OF THE BASE TEXT IN ONE STORY WITH NO MARK ON THEM.
void WalkWithoutKenten(const UIDRef& storyRef, ITextModel* model, const KFCKentenConditions& asked, KFCOwnScope& taken, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit,
	std::vector<KFCSearchEngine::ListedPlace>& ioPlaces, bool& outCapped)
{
	const KFCFold::Flags flags(KFCFoldFlagsOf(asked.switches));
	const KFCFold::Folded folded(KFCFold::Fold(U32Of(KFCSearchEngine::ReadText(storyRef, 0, model->TotalLength())), flags));
	const std::u32string wanted(KFCFold::TextOf(U32Of(asked.base), flags));
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (wanted.empty() || scanner == nil)
		return;
	for (size_t found = folded.text.find(wanted); found != std::u32string::npos; found = folded.text.find(wanted, found + wanted.size()))
	{
		const TextIndex start = static_cast<TextIndex>(folded.from[found]);
		const TextIndex end = static_cast<TextIndex>(folded.from[found + wanted.size()]);
		if (StandsInSetAsideText(model, start))
			continue;
		if (part != nil && !KFCSearchEngine::StretchesHold(*part, storyRef.GetUID(), start, end))
			continue;
		if (!taken.Takes(storyRef, model, start))
			continue;
		if (MarkAnywhereIn(scanner, start, end))
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

// ONE DOCUMENT'S ROWS - of its searched part alone, given one. False = no story list to walk.
bool WalkDoc(const UIDRef& docRef, const KFCKentenConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, size_t limit, std::vector<KFCResultModel::Hit>& outHits,
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
	KFCOwnScope taken(asked.switches);
	const int32 count = stories->GetAllTextModelCount();
	for (int32 s = 0; s < count && !outCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthTextModelUID(s));
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil || (part != nil && !KFCSearchEngine::StretchesTouch(*part, storyRef.GetUID(), 0, kMaxInt32)) || !taken.TakesStory(storyRef))
			continue;
		if (asked.kind == kKFCKentenFindWithout)
			WalkWithoutKenten(storyRef, model, asked, taken, part, limit, places, outCapped);
		else
			WalkWithKenten(storyRef, model, asked, taken, part, limit, places, outCapped);
	}
	KFCSearchEngine::BuildListedHits(docRef, places, outHits, outVersions);
	// What the rows with marks draw there - a custom mark's own character (C6); a row without any draws its text.
	for (size_t h = 0; asked.kind == kKFCKentenFindWith && h < outHits.size() && h < places.size(); ++h)
	{
		const UIDRef storyRef(db, places[h].story);
		const int32 mark = KFCKentenSearch::MarkOn(storyRef, places[h].start, places[h].end);
		outHits[h].runChain = KFCKentenSearch::RowWords(storyRef, places[h].start, places[h].end, mark);
		outHits[h].runChain.SetTranslatable(kFalse);
		outHits[h].upper.mark = mark;
		if (mark == IKentenStyle::Kenten_Custom)
		{
			const int32 customChar = KFCKentenSearch::CustomCharOn(storyRef, places[h].start, places[h].end);
			outHits[h].upper.markChar = KFCIsCustomKentenCharacter(static_cast<uint32>(customChar)) ? customChar : 0;
		}
	}
	return true;
}
}	// anonymous namespace

bool KFCKentenSearch::IsTabCurrent(KFCKentenConditions* outConditions)
{
	KFCKentenConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch kenten-tab (the model's cases, which need no dialog): on = the Kenten kind, its lines its conditions.
	if (KFC_DIAG_FAULT("kenten-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("kenten-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
		{
			const std::string& line = lines[n];
			PMString value;
			value.SetTranslatable(kFalse);
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				value.SetUTF8String(line.substr(eq + 1));
			if (line == "kind=without")
				conditions.kind = kKFCKentenFindWithout;
			else if (line.compare(0, 5, "base=") == 0)
				conditions.base = value;
			else if (line.compare(0, 5, "mark=") == 0)
				conditions.mark = value.GetAsNumber();
			else if (line.compare(0, 7, "change=") == 0)
				conditions.changeTo = value.GetAsNumber();
			else if (line.compare(0, 9, "markchar=") == 0)
				conditions.markChar = static_cast<int32>(std::strtol(line.c_str() + 9, nullptr, 16));
			else if (line.compare(0, 11, "changechar=") == 0)
				conditions.changeChar = static_cast<int32>(std::strtol(line.c_str() + 11, nullptr, 16));
			else if (line == "remove=1")
				conditions.removeKenten = true;
			else if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
			else if (line == "scope=story")
				conditions.scope = IWalkerScopeFactoryUtils::kStoryScope;
			else if (line == "scope=toend")
				conditions.scope = IWalkerScopeFactoryUtils::kToEndOfStoryScope;
			else if (line == "scope=selection")
				conditions.scope = IWalkerScopeFactoryUtils::kSelectionScope;
			else
				KFCOwnScopeDiagLine(line, conditions.switches);
		}
	}
#endif
	// THE TAB ITSELF IS THE UI HALF'S (KFCQRSearch::IsTabCurrent's rule): no UI half is no Kenten kind.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->KentenTabConditions(conditions);
	}
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

void KFCKentenSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCKentenConditions& asked,
	const std::vector<KFCSearchEngine::ListedPlace>* part)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	const int32 barEnd = static_cast<int32>(targets.size()) * kKentenDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;		// the object lists' number, as a ruby list's
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
		const bool walked = WalkDoc(docRef, asked, part, limit - static_cast<size_t>(out.total), hits, versions, capped);
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

bool KFCKentenSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCKentenConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kKentenDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, asked, nil, limit, outHits, outVersions, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kKentenDocSpan);
	return walked && !outCancelled;
}

bool KFCKentenSearch::WriteDocument(const UIDRef& docRef, const KFCKentenConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount)
{
	outCount = 0;
	std::vector<KFCResultModel::Hit> hits;
	std::map<UID, uint32> versions;
	bool capped = false;
	if (!WalkDoc(docRef, asked, part, static_cast<size_t>(kMaxInt32), hits, versions, capped))
		return false;
	if (!asked.ChangeSideAsks())
		return true;		// (custom with no character writes nothing - C5; HasChangeSide keeps a run from asking it)
	IDataBase* const db = docRef.GetDataBase();
	const int32 wanted = asked.removeKenten ? static_cast<int32>(IKentenStyle::Kenten_None) : asked.changeTo;
	const int32 wantedChar = (wanted == IKentenStyle::Kenten_Custom) ? asked.changeChar : 0;
	for (size_t h = 0; h < hits.size(); ++h)
	{
		if (hits[h].isLocked)
			continue;
		const UIDRef story(db, hits[h].storyUID);
		if (MarkOn(story, hits[h].textStart, hits[h].textEnd) == wanted
			&& (wanted != IKentenStyle::Kenten_Custom || CustomCharOn(story, hits[h].textStart, hits[h].textEnd) == wantedChar))
			continue;
		const ErrorCode wrote = asked.removeKenten ? Remove(story, hits[h].textStart, hits[h].textEnd) : Write(story, hits[h].textStart, hits[h].textEnd, wanted, wantedChar);
		if (wrote != kSuccess)
			return false;
		++outCount;
	}
	return true;
}

bool KFCKentenSearch::WalkForFace(const UIDRef& docRef, const KFCKentenConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, std::vector<KFCResultModel::Hit>& outHits)
{
	outHits.clear();
	std::map<UID, uint32> versions;
	bool capped = false;
	return WalkDoc(docRef, asked, part, static_cast<size_t>(kMaxInt32), outHits, versions, capped);
}

int32 KFCKentenSearch::MarkOn(const UIDRef& story, TextIndex start, TextIndex end)
{
	InterfacePtr<IComposeScanner> scanner(story, UseDefaultIID());
	if (scanner == nil || end <= start)
		return IKentenStyle::Kenten_None;
	int32 len = 0;
	const int32 mark = MarkAt(scanner, start, end, len);
	return (len >= end - start) ? mark : -1;
}

int32 KFCKentenSearch::CustomCharOn(const UIDRef& story, TextIndex start, TextIndex end)
{
	InterfacePtr<IComposeScanner> scanner(story, UseDefaultIID());
	if (scanner == nil || end <= start)
		return 0;
	int32 len = 0;
	const int32 customChar = CustomCharAt(scanner, start, end, len);
	return (len >= end - start) ? customChar : -1;
}

PMString KFCKentenSearch::RowWords(const UIDRef& story, TextIndex start, TextIndex end, int32 mark)
{
	if (mark == IKentenStyle::Kenten_None)
		return PMString();		// (no mark: the row draws its own text)
	PMString words;
	words.SetTranslatable(kFalse);
	PMString base(KFCSearchEngine::ReadText(story, start, end - start));
	base.SetTranslatable(kFalse);
	KFCResultModel::MarkUpBreaksForDisplay(base);		// (as a row's match is drawn)
	if (base.CharCount() > kRowWordsBase)
	{
		const int32 keep = KFCSafeCut(base, kRowWordsBase);
		base.Remove(keep > 0 ? keep : kRowWordsBase, kMaxInt32);
		base.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	words.Append(base);
	words.AppendW(UTF32TextChar(0x300A));		// the book title marks: a ruby row's way of writing what goes beside the base
	// A custom mark's own character (C6) - the asterisk when it is none a row can write.
	const int32 customChar = (mark == IKentenStyle::Kenten_Custom) ? CustomCharOn(story, start, end) : 0;
	words.AppendW(KFCKentenMarkCharacter(mark, KFCIsCustomKentenCharacter(static_cast<uint32>(customChar)) ? customChar : 0));
	words.AppendW(UTF32TextChar(0x300B));
	return words;
}

ErrorCode KFCKentenSearch::Remove(const UIDRef& story, TextIndex start, TextIndex end)
{
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	// ALL NINETEEN: CJKID.h's kenten attributes - the same nineteen SnpPerformTextAttrKenten::ApplyKenten writes - less kTAKentenXOffsetBoss_obsolete, no attribute now. KFCRubySearch::Remove's way: only the class IDs matter in
	// this list (ITextModelCmds.h, ClearOverridesCmd), and all of them or none: a partial clear would be said as done.
	static const ClassID kKentenAttrs[] =
	{
		kTAKentenKindBoss,			kTAKentenSizeBoss,			kTAKentenRelatedSizeBoss,
		kTAKentenFontFamilyBoss,	kTAKentenFontStyleBoss,		kTAKentenXScaleBoss,
		kTAKentenYScaleBoss,		kTAKentenYOffsetBoss,		kTAKentenAlignmentBoss,
		kTAKentenPositionBoss,		kTAKentenCharacterBoss,		kTAKentenCharacterSetBoss,
		kTAKentenColorBoss,			kTAKentenTintBoss,			kTAKentenOverprintBoss,
		kTAKentenStrokeColorBoss,	kTAKentenStrokeTintBoss,	kTAKentenStrokeOverprintBoss,
		kTAKentenOutlineBoss
	};
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	if (attrs.get() == nil)
		return kFailure;
	for (size_t i = 0; i < sizeof(kKentenAttrs) / sizeof(kKentenAttrs[0]); ++i)
	{
		InterfacePtr<IAttrReport> report(::CreateObject2<IAttrReport>(kKentenAttrs[i]));
		if (report == nil)
			return kFailure;
		attrs->ApplyAttribute(report);
	}
	InterfacePtr<ICommand> clear(cmds->ClearOverridesCmd(start, end - start, attrs, kCharAttrStrandBoss));
	const ErrorCode cleared = (clear != nil) ? CmdUtils::ProcessCommand(clear) : kFailure;
	if (cleared != kSuccess)
		return cleared;
	// A STYLE STILL GIVES IT A MARK: nothing left to clear, and the style is other text's too. Its kind turned off on this text, as InDesign's own menu does.
	return (MarkOn(story, start, end) != IKentenStyle::Kenten_None) ? Write(story, start, end, IKentenStyle::Kenten_None) : kSuccess;
}

ErrorCode KFCKentenSearch::Write(const UIDRef& story, TextIndex start, TextIndex end, int32 mark, int32 customChar)
{
	InterfacePtr<ITextModelCmds> cmds(story, UseDefaultIID());
	if (cmds == nil)
		return kFailure;
	boost::shared_ptr<AttributeBossList> attrs(new AttributeBossList);
	InterfacePtr<ITextAttrInt16> kind(::CreateObject2<ITextAttrInt16>(kTAKentenKindBoss));
	if (attrs.get() == nil || kind == nil)
		return kFailure;
	kind->Set(static_cast<int16>(mark));
	InterfacePtr<IAttrReport> report(kind, UseDefaultIID());
	attrs->ApplyAttribute(report);
	// A CUSTOM MARK'S CHARACTER, with its kind (SnpPerformTextAttrKenten::ApplyKenten writes the same two ITextAttrInt16s): the character's 16 bits, and the set it was entered in - 0, what the kenten dialog's
	// character input keeps (the DOM's CHARACTER_INPUT; the C++ enum names 0 kShiftJIS, and the snippet's own comment says the set is "not really used").
	if (mark == IKentenStyle::Kenten_Custom)
	{
		if (!KFCIsCustomKentenCharacter(static_cast<uint32>(customChar)))
			return kFailure;
		InterfacePtr<ITextAttrInt16> character(::CreateObject2<ITextAttrInt16>(kTAKentenCharacterBoss));
		InterfacePtr<ITextAttrInt16> characterSet(::CreateObject2<ITextAttrInt16>(kTAKentenCharacterSetBoss));
		if (character == nil || characterSet == nil)
			return kFailure;
		character->Set(static_cast<int16>(static_cast<uint16>(customChar)));
		characterSet->Set(static_cast<int16>(IKentenStyle::kShiftJIS));
		InterfacePtr<IAttrReport> characterReport(character, UseDefaultIID());
		InterfacePtr<IAttrReport> setReport(characterSet, UseDefaultIID());
		attrs->ApplyAttribute(characterReport);
		attrs->ApplyAttribute(setReport);
	}
	InterfacePtr<ICommand> apply(cmds->ApplyCmd(RangeData(start, end), attrs, kCharAttrStrandBoss));
	return (apply != nil) ? CmdUtils::ProcessCommand(apply) : kFailure;
}

// End, KFCKentenSearch.cpp.
