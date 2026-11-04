//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku+ tab's Script Label kind, listed and written - see KFCScriptLabelSearch.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IHierarchy.h"
#include "IObjectWalker.h"
#include "IScript.h"					// a page item's label - GetTag (not IScriptLabel: memory script-label-read-through-iscript)
#include "IScriptLabel.h"				// kDefaultScriptLabelKey - the Script Label panel's own key
#include "IWalkerScopeFactoryUtils.h"	// kDocumentScope, kAllDocumentScope

// General includes:
#include "GroupID.h"					// kGroupItemBoss
#include "LayerID.h"					// kSpreadLayerBoss
#include "ObjectWalkerScopeOptions.h"
#include "SpreadID.h"					// kSpreadBoss
#include "TextChar.h"					// kTextChar_Ellipse
#include "TextID.h"						// kInlineBoss
#include "TextWalkerServiceProviderID.h"	// kObjectWalkerBoss
#include "WideString.h"

#include <set>
#include <string>

// Project includes:
#include "KFCScriptLabelSearch.h"
#include "IKFCUIServices.h"				// ScriptLabelTabConditions - the kind is the UI half's (KFCQueryUIServices)
#include "KFCDiag.h"
#include "KFCObjectSearch.h"			// BuildRow - a row as an object row is
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line
#include "KFCTextCut.h"					// KFCSafeCut

namespace
{

// A document's share of the bar (the bar moves between documents: a document's labels are read in one go - KFCQRSearch's).
const int32 kLabelDocSpan = 100;

// A row's label is kept to this many characters - the panel cuts it again to its width when it draws (KFCQRSearch's kQRMaxContentChars).
const int32 kLabelMaxChars = 200;

// A LABEL AS CODE POINTS, case-folded or not (Matches and NewValue compare these). Folded one code point at a time, each to its own lowercase
// where that is one code point too - so a position here is a position in the label (a string's ToLower can change its length: U+0130).
std::u32string U32Of(const PMString& words, bool fold)
{
	std::u32string out;
	const WideString wide(words);
	for (WideString::const_iterator c = wide.begin(); c != wide.end(); ++c)
	{
		uint32 v = static_cast<uint32>(*c);
		if (fold && v >= 0x41)
		{
			WideString one;
			one.Append(UTF32TextChar(v));
			one.ToLower();
			if (one.CharCount() == 1)
				v = static_cast<uint32>(*one.begin());
		}
		out.push_back(static_cast<char32_t>(v));
	}
	return out;
}

// NOT A PAGE ITEM OF ITS OWN, going up from one: a spread's layer (the top of an item's own tree), the spread, and the wrapper an inline or
// anchored item stands in (kInlineBoss - the story's, not the page's).
bool IsAboveItems(ClassID kind)
{
	return kind == kSpreadLayerBoss || kind == kSpreadBoss || kind == kInlineBoss;
}

void AddOnce(UID item, std::set<UID>& seen, std::vector<UID>& out)
{
	if (item != kInvalidUID && seen.insert(item).second)
		out.push_back(item);
}

// WHAT AN ITEM HOLDS, all the way down (IHierarchy): a frame's placed graphic, a frame pasted inside it, a group's members.
void AddBelow(IDataBase* db, UID item, std::set<UID>& seen, std::vector<UID>& out, int32 depth)
{
	if (depth > 64)
		return;
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	if (hier == nil)
		return;
	const int32 count = hier->GetChildCount();
	for (int32 i = 0; i < count; ++i)
	{
		const UID child = hier->GetChildUID(i);
		if (child == kInvalidUID)
			continue;
		AddOnce(child, seen, out);
		AddBelow(db, child, seen, out, depth + 1);
	}
}

// THE ITEMS A LABEL CAN STAND ON (the design's S3): every spline InDesign's own object walk hands back with every Include on - locked and
// hidden layers, locked stories, parent pages, footnotes: inline and anchored items too (KFCQRCodes::CollectFrames' options) - each with the
// items it stands in (IHierarchy up: a group or a button is no spline; a spread's layer stops it) and what it holds (IHierarchy down: a
// placed graphic, a frame pasted inside). The DOM's allPageItems' set, each once, in the order met (FileChapter puts the rows in page order).
// An item of InDesign's own inside a text frame (its columns) can be met too: it holds no label of a person's, so it is no row.
bool CollectItems(const UIDRef& docRef, std::vector<UID>& outItems)
{
	outItems.clear();
	IDataBase* const db = docRef.GetDataBase();
	if (db == nil)
		return false;
	InterfacePtr<IObjectWalker> walker(::CreateObject2<IObjectWalker>(kObjectWalkerBoss));
	if (walker == nil)
		return false;
	ObjectWalkerScopeOptions options(kTrue, kTrue, kTrue, kTrue, kTrue, ObjectWalkerScopeOptions::kAllFrames, ObjectWalkerScopeOptions::kDocument);
	options.SetDocumentToBeSearched(docRef);
	walker->Initialize(options);

	std::set<UID> seen;
	// (A guard on a walk that never ends - KFCQRCodes::CollectFrames' bound.)
	for (int32 step = 0; step < 100000; ++step)
	{
		UIDRef met;
		const IObjectWalker::WalkerState state = walker->GetNextItem(met);
		if (met.GetDataBase() == db && met.GetUID() != kInvalidUID)
		{
			// UP: the items it stands in, the outermost first (a group before its members, as the Layers panel shows them).
			std::vector<UID> above;
			UID at = met.GetUID();
			for (int32 depth = 0; depth < 64; ++depth)
			{
				InterfacePtr<IHierarchy> hier(db, at, UseDefaultIID());
				const UID parent = (hier != nil) ? hier->GetParentUID() : kInvalidUID;
				if (parent == kInvalidUID || IsAboveItems(db->GetClass(parent)))
					break;
				above.push_back(parent);
				at = parent;
			}
			for (size_t u = above.size(); u-- > 0;)
				AddOnce(above[u], seen, outItems);
			AddOnce(met.GetUID(), seen, outItems);
			AddBelow(db, met.GetUID(), seen, outItems, 0);
		}
		if (state == IObjectWalker::kWalkCompleted || state == IObjectWalker::kEndOfDocumentReached)
			break;
	}
	return true;
}

// A LABEL'S ROW (S5): itemUID = the item, matchText = "ID<uid>:<the Layers panel's name>  <label>" - the object rows' head (BuildRow: its
// name, its place, its fingerprint), then the label as it stands, cut where a row's text is kept short (never inside a character -
// KFCSafeCut). The panel draws its line breaks as marks (KFCResultModel::MarkUpBreaksForDisplay) and cuts it again to its width.
void BuildRow(const UIDRef& docRef, UID item, const PMString& label, KFCResultModel::Hit& out)
{
	KFCObjectSearch::BuildRow(docRef, item, out);
	PMString shown(label);
	shown.SetTranslatable(kFalse);
	if (shown.CharCount() > kLabelMaxChars)
	{
		shown.Remove(KFCSafeCut(shown, kLabelMaxChars), kMaxInt32);
		shown.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	out.matchText.Append("  ");
	out.matchText.Append(shown);
	out.matchText.SetTranslatable(kFalse);
}

// ONE DOCUMENT'S LABELS AS ROWS, in the walk's order: at most `limit` (outCapped when there were more). false = no walker.
bool WalkDoc(const UIDRef& docRef, size_t limit, const KFCScriptLabelConditions& asked, std::vector<KFCResultModel::Hit>& outHits, bool& outCapped)
{
	outCapped = false;
	std::vector<UID> items;
	if (!CollectItems(docRef, items))
		return false;
	IDataBase* const db = docRef.GetDataBase();
	const PMString key(KFCScriptLabelSearch::KeyOf(asked));
	for (size_t i = 0; i < items.size(); ++i)
	{
		const PMString label(KFCScriptLabelSearch::LabelOf(db, items[i], key));
		if (!KFCScriptLabelSearch::Matches(label, asked))
			continue;
		if (outHits.size() >= limit)
		{
			outCapped = true;
			break;
		}
		KFCResultModel::Hit hit;
		BuildRow(docRef, items[i], label, hit);
		outHits.push_back(hit);
	}
	return true;
}

}	// anonymous namespace

bool KFCScriptLabelSearch::IsTabCurrent(KFCScriptLabelConditions* outConditions)
{
	KFCScriptLabelConditions conditions;
	conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	bool current = false;
	bool answered = false;
#ifdef KFC_DIAG
	// TEST BUILDS - the fault switch label-tab (the model's cases, which need no dialog): on = the kind, its lines its conditions
	// (KFCTableSearch::IsTabCurrent's shape).
	if (KFC_DIAG_FAULT("label-tab"))
	{
		current = true;
		answered = true;
		std::vector<std::string> lines;
		(void)KFCDiagFaultLines("label-tab", lines);
		for (size_t n = 0; n < lines.size(); ++n)
		{
			const std::string& line = lines[n];
			PMString value;
			value.SetTranslatable(kFalse);
			const size_t eq = line.find('=');
			if (eq != std::string::npos)
				value.SetUTF8String(line.substr(eq + 1));
			if (line.compare(0, 4, "key=") == 0)
				conditions.key = value;
			else if (line.compare(0, 5, "find=") == 0)
				conditions.find = value;
			else if (line.compare(0, 7, "change=") == 0)
				conditions.change = value;
			else if (line == "case=1")
				conditions.caseSensitive = true;
			else if (line == "whole=1")
				conditions.whole = true;
			else if (line == "scope=all")
				conditions.scope = IWalkerScopeFactoryUtils::kAllDocumentScope;
		}
	}
#endif
	// THE KIND ITSELF IS THE UI HALF'S (KFCQRSearch::IsTabCurrent's rule): no UI half is no Script Label kind.
	if (!answered)
	{
		InterfacePtr<IKFCUIServices> ui(KFCQueryUIServices());
		if (ui != nil)
			current = ui->ScriptLabelTabConditions(conditions);
	}
	// SEARCH: IS DOCUMENT OR ALL DOCUMENTS (S2 - the QR Code kind's two): anything else is searched as Document.
	if (conditions.scope != IWalkerScopeFactoryUtils::kAllDocumentScope)
		conditions.scope = IWalkerScopeFactoryUtils::kDocumentScope;
	if (outConditions != nil)
		*outConditions = conditions;
	return current;
}

PMString KFCScriptLabelSearch::KeyOf(const KFCScriptLabelConditions& asked)
{
	PMString key(asked.key.IsEmpty() ? IScriptLabel::kDefaultScriptLabelKey : asked.key);
	key.SetTranslatable(kFalse);
	return key;
}

PMString KFCScriptLabelSearch::LabelOf(IDataBase* db, UID item, const PMString& key)
{
	PMString label;
	if (db != nil && item != kInvalidUID && db->IsValidUID(item))
	{
		InterfacePtr<IScript> script(db, item, UseDefaultIID());
		if (script != nil)
			label = script->GetTag(key);
	}
	label.SetTranslatable(kFalse);
	return label;
}

bool KFCScriptLabelSearch::Matches(const PMString& label, const KFCScriptLabelConditions& asked)
{
	if (label.IsEmpty())
		return false;
	if (asked.find.IsEmpty())
		return true;
	const bool fold = !asked.caseSensitive;
	const std::u32string words(U32Of(label, fold));
	const std::u32string find(U32Of(asked.find, fold));
	return asked.whole ? (words == find) : (words.find(find) != std::u32string::npos);
}

PMString KFCScriptLabelSearch::NewValue(const PMString& label, const KFCScriptLabelConditions& asked)
{
	(void)asked;
	PMString same(label);
	same.SetTranslatable(kFalse);
	return same;
}

void KFCScriptLabelSearch::CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook,
	std::vector<KFCBookScope::SkippedChapter>& unopenable, KFCSearchEngine::CollectTally& out, int32 reserved, const KFCScriptLabelConditions& asked)
{
	out = KFCSearchEngine::CollectTally();
	PMString title(fromBook ? "Searching book..." : "Searching...");
	title.SetTranslatable(kFalse);
	// A document's slice dealt out by its size, as every walk's bar is (KFCBookScope::ProgressBases - the progress-share spec).
	const int32 barEnd = static_cast<int32>(targets.size()) * kLabelDocSpan;
	const std::vector<int32> shares(KFCBookScope::ProgressBases(targets, barEnd));
	KFCProgressBar bar(title, 0, barEnd, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	const int32 room = KFCResultModel::kKFCCollectObjectLimit - reserved;	// the object lists' limit; the kept rows keep theirs
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
		bool capped = false;
		const bool walked = WalkDoc(docRef, limit - static_cast<size_t>(out.total), asked, hits, capped);
		// A book's chapter goes back now, closed on the spot (one chapter of ours at a time - the other walks' rule).
		if (fromBook && !KFCBookScope::HandBackHeldDocNow(docRef))
			out.unclosed.push_back(targets[i].shortName);
		if (!walked)
		{
			PMString name(targets[i].shortName);
			name.SetTranslatable(kFalse);
			name.Append(": no object walker");
			out.unsearchable.push_back(name);
			continue;
		}
		if (capped)
			out.truncated = true;
		if (!hits.empty())
			KFCSearchEngine::FileChapter(targets[i], hits, out);		// every walk's filing: page order, the tally
		if (capped)
			break;
	}
	if (!out.cancelled && bar.WasCancelled(kFalse))
		out.cancelled = true;
	bar.SetPosition(barEnd);
}

bool KFCScriptLabelSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCScriptLabelConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, bool& outCapped, bool& outCancelled)
{
	outHits.clear();
	outCapped = false;
	outCancelled = false;
	PMString title("Searching...");
	title.SetTranslatable(kFalse);
	KFCProgressBar bar(title, 0, kLabelDocSpan, kTrue, kTrue);
	bar.DisableChildProgressBars(kTrue);
	KFCSetCountedTask(bar, "Document", 0, 1, name);
	bar.SetPosition(0);
	const bool walked = WalkDoc(docRef, limit, asked, outHits, outCapped);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kLabelDocSpan);
	return walked && !outCancelled;
}

PMString KFCScriptLabelSearch::RowWords(IDataBase* db, UID item, const PMString& label)
{
	KFCResultModel::Hit hit;
	if (db != nil)
		BuildRow(UIDRef(db, db->GetRootUID()), item, label, hit);
	hit.matchText.SetTranslatable(kFalse);
	return hit.matchText;
}

bool KFCScriptLabelSearch::Next(const KFCScriptLabelLook& look, KFCScriptLabelFound& out)
{
	(void)look;
	out = KFCScriptLabelFound();
	return false;
}

bool KFCScriptLabelSearch::ChangeFound(const KFCScriptLabelFound& found, PMString& outWords)
{
	(void)found;
	outWords.SetTranslatable(kFalse);
	return false;
}

bool KFCScriptLabelSearch::ChangeAll(int32& outChanged, int32& outMatched, PMString& outWords)
{
	outChanged = 0;
	outMatched = 0;
	outWords.SetTranslatable(kFalse);
	return false;
}

bool KFCScriptLabelSearch::WriteDocument(const UIDRef& docRef, const KFCScriptLabelConditions& asked, int32& outCount)
{
	(void)docRef;
	(void)asked;
	outCount = 0;
	return false;
}

ErrorCode KFCScriptLabelSearch::Write(IDataBase* db, UID item, const PMString& key, const PMString& value)
{
	(void)db;
	(void)item;
	(void)key;
	(void)value;
	return kFailure;
}

// End, KFCScriptLabelSearch.cpp.
