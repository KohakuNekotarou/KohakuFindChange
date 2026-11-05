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
#include "IScript.h"					// a page item's label - GetTag (not IScriptLabel: memory script-label-read-through-iscript); GetTags - every key's (IScript is an IScriptLabel)
#include "IScriptLabel.h"				// kDefaultScriptLabelKey - the Script Label panel's own key; ScriptLabelKeyValueList
#include "IScriptUtils.h"				// SetScriptingTag - the official door to a label's write (Write)
#include "RequestContext.h"				// EngineContext - default-constructed: no engine is asking
#include "ScriptData.h"					// ScriptList
#include "Utils.h"
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

#include <map>
#include <regex>						// std::wregex - Find Label: as GREP (the author, 11-04: "add regular expressions, please")
#include <set>
#include <string>
#include <utility>

// Project includes:
#include "KFCScriptLabelSearch.h"
#include "IKFCUIServices.h"				// ScriptLabelTabConditions - the kind is the UI half's (KFCQueryUIServices)
#include "KFCDiag.h"
#include "KFCObjectSearch.h"			// BuildRow - a row as an object row is; Fingerprint, ReadItemPlace - a written row's marks
#include "KFCPageItemFacts.h"			// ItemIsThere - the face's Change asks its item first; IsPageItemLockedForEdit, IsFrameOnLockedLayer - IsLockedNow
#include "KFCReplaceEngine.h"			// WriteFaceLabels - the face's Change and Change All write in its step
#include "KFCProgressBar.h"				// KFCSetCountedTask too - the bar's line
#include "KFCTextCut.h"					// KFCSafeCut
// (A table's and a cell's labels - docs/superpowers/specs/2026-11-05-kfc-script-label-tables-cells-design.md L1, L2:)
#include "AttributeBossList.h"			// a cell's label attribute handed to ApplyCellOverrides
#include "IStoryList.h"
#include "ITableAttrAccessor.h"			// QueryCellAttribute - a cell's labels are a cell attribute (KIDMCP's KIDMCPLabels.cpp TagsOf)
#include "IScriptCellRef.h"				// SetTableModel - a table's labels read and written through the scripting object the DOM makes for one (TableScript)
#include "ScriptingDefs.h"				// c_Table - that object's kind (IScriptUtils::CreateProxyScriptObject)
#include "ITableCommands.h"				// ApplyCellOverrides - SnpSetTableFill.cpp's shape
#include "ITableModel.h"
#include "ITableModelList.h"
#include "ITableTextContainer.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextStoryThreadDict.h"
#include "ConditionalTextID.h"			// kHiddenTextBoss - a table in text set aside
#include "InCopySharedID.h"				// kDeletedTextBoss, kNoteDataBoss
#include "TableTypes.h"
#include "TablesID.h"					// kCellAttrScriptLabelBoss, IID_ISCRIPTLABEL
#include "KFCTableRows.h"				// TableCellAt - the cell a cell row's text stands in
#include "KFCTableSearch.h"				// QueryTableAt - the table whose character a table row stands on

namespace
{

// A document's share of the bar (the bar moves between documents: a document's labels are read in one go - KFCQRSearch's).
const int32 kLabelDocSpan = 100;

// A row's label is kept to this many characters - the panel cuts it again to its width when it draws (KFCQRSearch's kQRMaxContentChars) -
// of which the label before its match keeps this many, its end (the match is what the row is for).
const int32 kLabelMaxChars = 200;
const int32 kLabelHeadChars = 80;

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

// [from, from + count) of code points as a PMString (count npos: to the end).
PMString WordsOfU32(const std::u32string& s, size_t from, size_t count)
{
	WideString wide;
	const size_t end = (count == std::u32string::npos || from + count > s.size()) ? s.size() : from + count;
	for (size_t c = from; c < end; ++c)
		wide.Append(UTF32TextChar(static_cast<uint32>(s[c])));
	PMString out(wide);
	out.SetTranslatable(kFalse);
	return out;
}

// A PMString's UTF-16 units as a std::wstring and back - what std::wregex reads and writes (wchar_t is UTF-16: KFC is Windows' alone).
std::wstring WideOf(const PMString& s)
{
	int32 n = 0;
	const UTF16TextChar* buf = s.GrabUTF16Buffer(&n);
	return (buf != nil && n > 0) ? std::wstring(reinterpret_cast<const wchar_t*>(buf), static_cast<size_t>(n)) : std::wstring();
}

PMString WordsOfWide(const std::wstring& w)
{
	PMString out;
	out.SetTranslatable(kFalse);
	if (!w.empty())
		out.AppendW(reinterpret_cast<const UTF16TextChar*>(w.data()), static_cast<int32>(w.size()));
	out.SetTranslatable(kFalse);
	return out;
}

// FIND LABEL: AS GREP (the author, 11-04): ECMAScript's grammar (std::wregex - InDesign's own GREP engine is no public door), Case Sensitive off = icase. Compiled once
// for a pattern and its case - a walk asks it of every label - and kept per thread (a model plug-in keeps no state threads share). valid false: why = std::regex_error's words.
struct GrepCompiled
{
	bool			set;
	std::wstring	pattern;
	bool			icase;
	bool			valid;
	std::string		why;
	std::wregex		re;
	GrepCompiled() : set(false), icase(false), valid(false) {}
};

const GrepCompiled& GrepOf(const KFCScriptLabelConditions& asked)
{
	thread_local GrepCompiled kept;
	const std::wstring pattern(WideOf(asked.find));
	const bool icase = !asked.caseSensitive;
	if (!kept.set || kept.pattern != pattern || kept.icase != icase)
	{
		kept.set = true;
		kept.pattern = pattern;
		kept.icase = icase;
		kept.why.clear();
		try
		{
			kept.re.assign(pattern, icase ? (std::regex_constants::ECMAScript | std::regex_constants::icase) : std::regex_constants::ECMAScript);
			kept.valid = true;
		}
		catch (const std::regex_error& e)
		{
			kept.valid = false;
			kept.why = e.what();
		}
	}
	return kept;
}

// A LABEL AGAINST THE CONDITIONS, SPLIT WHERE IT MATCHES (S4 - and the author, 11-04: "only the found characters dark, the rest faint", as a text row draws its match):
// pre, the match (the first one - every one for a Replace), post. No find: the whole label matches; Whole Label: the whole label or nothing. False = no match (the
// parts are not set) - an empty label never matches, nor a GREP that is no regular expression.
struct LabelParts
{
	PMString	pre;
	PMString	match;
	PMString	post;
};

bool FirstMatch(const PMString& label, const KFCScriptLabelConditions& asked, LabelParts* outParts)
{
	if (label.IsEmpty())
		return false;
	LabelParts parts;
	parts.pre.SetTranslatable(kFalse);
	parts.post.SetTranslatable(kFalse);
	parts.match = label;
	parts.match.SetTranslatable(kFalse);
	if (asked.find.IsEmpty())
	{
		if (outParts != nil)
			*outParts = parts;
		return true;
	}
	if (asked.grep)
	{
		const GrepCompiled& g = GrepOf(asked);
		if (!g.valid)
			return false;
		const std::wstring w(WideOf(label));
		try
		{
			std::wsmatch m;
			if (asked.whole)
			{
				if (!std::regex_match(w, m, g.re))
					return false;
			}
			else
			{
				if (!std::regex_search(w, m, g.re))
					return false;
				const size_t at = static_cast<size_t>(m.position(0));
				const size_t length = static_cast<size_t>(m.length(0));
				parts.pre = WordsOfWide(w.substr(0, at));
				parts.match = WordsOfWide(w.substr(at, length));
				parts.post = WordsOfWide(w.substr(at + length));
			}
		}
		catch (const std::regex_error&)
		{
			return false;		// (a pattern too complex for a label - std::regex's error_complexity / error_stack: no match)
		}
		if (outParts != nil)
			*outParts = parts;
		return true;
	}
	const bool fold = !asked.caseSensitive;
	const std::u32string folded(U32Of(label, fold));
	const std::u32string find(U32Of(asked.find, fold));
	if (asked.whole)
	{
		if (folded != find)
			return false;
	}
	else
	{
		const size_t at = folded.find(find);
		if (at == std::u32string::npos)
			return false;
		const std::u32string original(U32Of(label, false));
		parts.pre = WordsOfU32(original, 0, at);
		parts.match = WordsOfU32(original, at, find.size());
		parts.post = WordsOfU32(original, at + find.size(), std::u32string::npos);
	}
	if (outParts != nil)
		*outParts = parts;
	return true;
}

// THE PARTS KEPT SHORT (kLabelMaxChars in all, never inside a character - KFCSafeCut): the label before the match keeps its end, kLabelHeadChars of it; a match
// longer than all of it is cut and nothing follows; what follows fills the rest.
void CutParts(LabelParts& parts)
{
	if (parts.pre.CharCount() > kLabelHeadChars)
	{
		parts.pre.Remove(0, KFCSafeCut(parts.pre, parts.pre.CharCount() - kLabelHeadChars));
		PMString cut;
		cut.SetTranslatable(kFalse);
		cut.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
		cut.Append(parts.pre);
		parts.pre = cut;
	}
	if (parts.match.CharCount() > kLabelMaxChars)
	{
		parts.match.Remove(KFCSafeCut(parts.match, kLabelMaxChars), kMaxInt32);
		parts.match.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
		parts.post.Clear();
	}
	const int32 room = kLabelMaxChars - parts.pre.CharCount() - parts.match.CharCount();
	if (parts.post.CharCount() > (room > 0 ? room : 0))
	{
		parts.post.Remove(KFCSafeCut(parts.post, room > 0 ? room : 0), kMaxInt32);
		parts.post.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	}
	parts.pre.SetTranslatable(kFalse);
	parts.match.SetTranslatable(kFalse);
	parts.post.SetTranslatable(kFalse);
}

// A ROW'S TEXT FOR THE FACE'S LINE: without its "ID<uid>:" head - "<name>  <label>" (KFCQRSearch::Next's way).
PMString LineOf(const PMString& rowText)
{
	PMString text(rowText);
	text.SetTranslatable(kFalse);
	const CharCounter colon = text.IndexOfWChar(UTF32TextChar(':'), 0);
	if (colon >= 0)
		text.Remove(0, colon + 1);
	return text;
}

// The row's whole line: what it draws, faint and dark run together.
PMString WholeLineOf(const KFCResultModel::Hit& hit)
{
	PMString line(hit.preText);
	line.Append(hit.matchText);
	line.Append(hit.postText);
	line.SetTranslatable(kFalse);
	return line;
}

// THE SCRIPT LABEL PANEL'S OWN KEY? (its labels' rows name no key - every other key's do)
bool IsPanelKey(const PMString& key)
{
	return key.IsEqual(IScriptLabel::kDefaultScriptLabelKey) != kFalse;
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

// AN ITEM'S LABELS THE CONDITIONS LOOK AT, (key, label) - its labels that are not empty: the key asked's alone, or with no key asked EVERY key's (the author, 11-04:
// "a blank ScriptKey finds them all?") - the Script Label panel's first, then each one a script put under a key of its own (IScriptLabel::GetTags; the panel's key
// is not taken twice should the list hold it too).
// (Any holder's - an item's IScript, a table's model's, a cell's label attribute: each an IScriptLabel.)
void PickLabels(const IScriptLabel* script, const KFCScriptLabelConditions& asked, std::vector<std::pair<PMString, PMString> >& out)
{
	out.clear();
	if (script == nil)
		return;
	if (!asked.key.IsEmpty())
	{
		PMString key(asked.key);
		key.SetTranslatable(kFalse);
		PMString label(script->GetTag(key));
		label.SetTranslatable(kFalse);
		if (!label.IsEmpty())
			out.push_back(std::make_pair(key, label));
		return;
	}
	PMString panelKey(IScriptLabel::kDefaultScriptLabelKey);
	panelKey.SetTranslatable(kFalse);
	PMString own(script->GetTag(panelKey));
	own.SetTranslatable(kFalse);
	if (!own.IsEmpty())
		out.push_back(std::make_pair(panelKey, own));
	const IScriptLabel::ScriptLabelKeyValueList all(script->GetTags());
	for (size_t i = 0; i < all.size(); ++i)
	{
		PMString key(all[i].Key());
		key.SetTranslatable(kFalse);
		PMString label(all[i].Value());
		label.SetTranslatable(kFalse);
		if (key.IsEmpty() || label.IsEmpty() || IsPanelKey(key))
			continue;
		out.push_back(std::make_pair(key, label));
	}
}

void LabelsOf(IDataBase* db, UID item, const KFCScriptLabelConditions& asked, std::vector<std::pair<PMString, PMString> >& out)
{
	out.clear();
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return;
	InterfacePtr<IScript> script(db, item, UseDefaultIID());
	PickLabels(script, asked, out);
}

// A TABLE'S SCRIPTING OBJECT - what holds a table's labels (kTableModelBoss holds no IScript of its own - measured 11-05): kScriptTableRefBoss, made as the SDK
// samples make a proxy (IScriptUtils::CreateProxyScriptObject - basicme, candlechart) with the table's story for its PARENT, then its table set
// (IScriptCellRef::SetTableModel). The parent is what gives it the document's database (IScriptObjectParent.h:53-60 - IScript::GetDataBase answers the
// parent's): one made without a parent answered nil, and a label written through it took InDesign down in Command::SetTarget (11-05 13:09, memory
// table-label-write-crash-and-cell-label-write). nil = none, or one not in the table's database - never written through.
IScript* TableScript(ITableModel* table)
{
	InterfacePtr<ITableTextContainer> container(table, UseDefaultIID());
	InterfacePtr<ITextModel> story((container != nil) ? container->QueryTextModel() : nil);
	InterfacePtr<IScript> parent(story, UseDefaultIID());
	Utils<IScriptUtils> scriptUtils;
	if (parent == nil || !scriptUtils)
		return nil;
	InterfacePtr<IScript> script(scriptUtils->CreateProxyScriptObject(EngineContext(), kScriptTableRefBoss, c_Table, parent));
	InterfacePtr<IScriptCellRef> ref(script, UseDefaultIID());
	if (script == nil || ref == nil)
		return nil;
	ref->SetTableModel(table, kFalse);		// (no text focus registered: it lives for this read or write)
	if (script->GetDataBase(EngineContext()) != ::GetDataBase(table))
		return nil;
	return script.forget();
}

// THE LABELS OF A TABLE OR A CELL (docs/superpowers/specs/2026-11-05-kfc-script-label-tables-cells-design.md L1, L2 - KIDMCP measured both, KIDMCPLabels.cpp TagsOf): a cell's on
// the cell attribute kCellAttrScriptLabelBoss (IScriptLabel - the cell's own, or the default the table hands a cell with none); a table's on its scripting object
// (TableScript). nil = none.
IScriptLabel* QueryHolderLabels(IDataBase* db, const KFCScriptLabelSearch::Holder& holder)
{
	if (db == nil)
		return nil;
	if (holder.kind == kKFCHolderNone)
	{
		if (holder.item == kInvalidUID || !db->IsValidUID(holder.item))
			return nil;
		InterfacePtr<IScript> script(db, holder.item, UseDefaultIID());
		return script.forget();
	}
	if (holder.table == kInvalidUID || !db->IsValidUID(holder.table) || db->GetClass(holder.table) != kTableModelBoss)
		return nil;
	InterfacePtr<ITableModel> table(db, holder.table, UseDefaultIID());
	if (table == nil)
		return nil;
	if (holder.kind == kKFCHolderTable)
		return TableScript(table);
	InterfacePtr<ITableAttrAccessor> attrs(table, UseDefaultIID());
	if (attrs == nil)
		return nil;
	return static_cast<IScriptLabel*>(const_cast<IPMUnknown*>(attrs->QueryCellAttribute(holder.cell, kCellAttrScriptLabelBoss, IID_ISCRIPTLABEL)));
}

// A TABLE IN TEXT THE PAGE DOES NOT SET (hidden by a condition, deleted under Track Changes, in a note) - KFCTableSearch's StandsInSetAsideText: not walked, as the Table kind
// does not list it.
bool SetAside(ITextModel* model, TextIndex at)
{
	InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, nil, nil));
	const ClassID kind = (thread != nil) ? ::GetClass(thread) : kInvalidClass;
	return kind == kHiddenTextBoss || kind == kDeletedTextBoss || kind == kNoteDataBoss;
}

// EVERY TABLE'S AND EVERY CELL'S LABEL THE CONDITIONS FIND, AS TEXT ROWS (L1, L2): each story's tables - a table in a cell among them (ITableModelList holds every table its
// text anchors) - the table's labels, then each of its cells' whose top-left it is, top to bottom, left to right; a table's row on its character (the Table kind's row), a
// cell's on its text, its end-of-cell character in it (a row of empty text still has one character). At most `limit` rows in ioHits (ioCapped when there were more).
void WalkTablesAndCells(const UIDRef& docRef, size_t limit, const KFCScriptLabelConditions& asked, std::vector<KFCResultModel::Hit>& ioHits, bool& ioCapped,
	std::map<UID, uint32>& outVersions)
{
	IDataBase* const db = docRef.GetDataBase();
	InterfacePtr<IStoryList> stories(db, (db != nil) ? db->GetRootUID() : kInvalidUID, UseDefaultIID());
	if (stories == nil)
		return;
	struct Found
	{
		KFCSearchEngine::ListedPlace	place;
		PMString						words;
		UID								table;
		PMString						key;
		int32							kind;
	};
	std::vector<Found> found;
	std::vector<std::pair<PMString, PMString> > labels;
	// Composing to read a row's page dirties nothing the person made (a search's walk's rule - BuildListedHits composes).
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);
	const int32 count = stories->GetUserAccessibleStoryCount();		// (the user's stories alone - a table of the XML structure's text stands on no page: KFCRubySearch.cpp's WalkDoc's note, D24-a)
	for (int32 s = 0; s < count && !ioCapped; ++s)
	{
		const UIDRef storyRef(stories->GetNthUserAccessibleStoryUID(s));
		InterfacePtr<ITableModelList> tables(storyRef, UseDefaultIID());
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (tables == nil || model == nil)
			continue;
		for (int32 t = 0; t < tables->GetModelCount() && !ioCapped; ++t)
		{
			InterfacePtr<ITableModel> table(tables->QueryNthModel(t));
			InterfacePtr<ITableTextContainer> text(table, UseDefaultIID());
			InterfacePtr<ITextStoryThreadDict> threads(table, UseDefaultIID());
			if (table == nil || text == nil || SetAside(model, text->GetAnchorTextIndex()))
				continue;
			KFCScriptLabelSearch::Holder holder;
			holder.kind = kKFCHolderTable;
			holder.table = ::GetUID(table);
			// THE TABLE'S OWN, on its character.
			{
				InterfacePtr<IScriptLabel> tags(QueryHolderLabels(db, holder));
				PickLabels(tags, asked, labels);
			}
			for (size_t l = 0; l < labels.size() && !ioCapped; ++l)
			{
				if (!KFCScriptLabelSearch::Matches(labels[l].second, asked))
					continue;
				if (ioHits.size() + found.size() >= limit)
				{
					ioCapped = true;
					break;
				}
				Found one;
				one.place.story = storyRef.GetUID();
				one.place.start = text->GetAnchorTextIndex();
				one.place.end = one.place.start + text->GetAnchorSpan();
				one.words = KFCScriptLabelSearch::HolderRowWords(db, holder, labels[l].first, labels[l].second);
				one.table = holder.table;
				one.key = labels[l].first;
				one.kind = kKFCHolderTable;
				found.push_back(one);
			}
			// ITS CELLS', each on its text - the cells whose top-left they are (a merged cell once).
			const RowRange rows(table->GetTotalRows());
			const ColRange cols(table->GetTotalCols());
			for (int32 r = rows.start; r < rows.start + rows.count && !ioCapped; ++r)
				for (int32 c = cols.start; c < cols.start + cols.count && !ioCapped; ++c)
				{
					const GridAddress anchor(table->FindCellAnchor(GridAddress(r, c)));
					if (anchor.row != r || anchor.col != c)
						continue;
					KFCScriptLabelSearch::Holder cell;
					cell.kind = kKFCHolderCell;
					cell.table = holder.table;
					cell.cell = anchor;
					{
						InterfacePtr<IScriptLabel> tags(QueryHolderLabels(db, cell));
						PickLabels(tags, asked, labels);
					}
					if (labels.empty())
						continue;
					InterfacePtr<ITextStoryThread> thread((threads != nil) ? threads->QueryThread(table->GetGridID(anchor)) : nil);
					if (thread == nil)
						continue;
					int32 span = 0;
					const TextIndex start = thread->GetTextStart(&span);
					for (size_t l = 0; l < labels.size() && !ioCapped; ++l)
					{
						if (!KFCScriptLabelSearch::Matches(labels[l].second, asked))
							continue;
						if (ioHits.size() + found.size() >= limit)
						{
							ioCapped = true;
							break;
						}
						Found one;
						one.place.story = storyRef.GetUID();
						one.place.start = start;
						one.place.end = start + ((span > 0) ? span : 1);
						one.words = KFCScriptLabelSearch::HolderRowWords(db, cell, labels[l].first, labels[l].second);
						one.table = holder.table;
						one.key = labels[l].first;
						one.kind = kKFCHolderCell;
						found.push_back(one);
					}
				}
		}
	}
	if (found.empty())
		return;
	// What a search's row holds, for each - its line and hash, its page and flags (KFCSearchEngine::BuildListedHits, as a table row list's rows).
	std::vector<KFCSearchEngine::ListedPlace> places;
	for (size_t f = 0; f < found.size(); ++f)
		places.push_back(found[f].place);
	std::vector<KFCResultModel::Hit> hits;
	KFCSearchEngine::BuildListedHits(docRef, places, hits, outVersions);
	for (size_t h = 0; h < hits.size() && h < found.size(); ++h)
	{
		hits[h].runChain = found[h].words;
		hits[h].runChain.SetTranslatable(kFalse);
		hits[h].tableUID = found[h].table;
		hits[h].labelKey = found[h].key;
		hits[h].labelKey.SetTranslatable(kFalse);
		hits[h].holderKind = found[h].kind;
		ioHits.push_back(hits[h]);
	}
}

// A LABEL'S ROW (S5): itemUID = the item, labelKey = its label's key; the object rows' head (KFCObjectSearch::BuildRow: "ID<uid>:<the Layers panel's name>", its place,
// its fingerprint), then - two spaces after - "<key>: " for a key that is not the Script Label panel's, then the label as it stands, cut short (CutParts). With the
// conditions: faint up to the match, the match dark, faint after it (preText / matchText / postText - a text row's three). Without (a written row's words - RowWords):
// the whole line in matchText.
void BuildRow(const UIDRef& docRef, UID item, const PMString& key, const PMString& label, const KFCScriptLabelConditions* asked, KFCResultModel::Hit& out)
{
	KFCObjectSearch::BuildRow(docRef, item, out);
	PMString head(out.matchText);
	head.SetTranslatable(kFalse);
	head.Append("  ");
	if (!IsPanelKey(key))
	{
		head.Append(key);
		head.Append(": ");
	}
	LabelParts parts;
	if (asked == nil || !FirstMatch(label, *asked, &parts))
	{
		parts.pre.Clear();
		parts.match = label;
		parts.post.Clear();
	}
	CutParts(parts);
	if (asked == nil)
	{
		out.matchText = head;
		out.matchText.Append(parts.pre);
		out.matchText.Append(parts.match);
		out.matchText.Append(parts.post);
		out.preText.Clear();
		out.postText.Clear();
	}
	else
	{
		out.preText = head;
		out.preText.Append(parts.pre);
		out.matchText = parts.match;
		out.postText = parts.post;
	}
	out.preText.SetTranslatable(kFalse);
	out.matchText.SetTranslatable(kFalse);
	out.postText.SetTranslatable(kFalse);
	out.labelKey = key;
	out.labelKey.SetTranslatable(kFalse);
}

// ONE DOCUMENT'S LABELS AS ROWS, in the walk's order - an item's keys in LabelsOf's: at most `limit` (outCapped when there were more). false = no walker. `place`
// with `placeKey` (Find Next's last found - an empty placeKey: any of the item's): its row too, where the walk meets it, whether its label matches or not - a place to
// go on from (Next); *outPlaceMatches says whether it matched (left as it was when it is not met).
// WITH TABLES (the list's walk - CollectTargets, WalkOneDocument: docs/superpowers/specs/2026-11-05-kfc-script-label-tables-cells-design.md L1, L2): after the items, every table's and
// cell's label as text rows (WalkTablesAndCells), their stories' versions in *outVersions. Without (the face's Find Next, Change, Change All and a query's run - the design's 3.): the
// items alone, as before.
bool WalkDoc(const UIDRef& docRef, size_t limit, const KFCScriptLabelConditions& asked, std::vector<KFCResultModel::Hit>& outHits, bool& outCapped,
	UID place = kInvalidUID, const PMString& placeKey = PMString(), bool* outPlaceMatches = nil, bool withTables = false, std::map<UID, uint32>* outVersions = nil)
{
	outCapped = false;
	std::vector<UID> items;
	if (!CollectItems(docRef, items))
		return false;
	IDataBase* const db = docRef.GetDataBase();
	KFCScriptLabelConditions placeAsked(asked);
	placeAsked.key = placeKey;		// (the place's own label is read under its own key, whatever Script Key: asks)
	std::vector<std::pair<PMString, PMString> > labels;
	for (size_t i = 0; i < items.size(); ++i)
	{
		const bool atPlace = (place != kInvalidUID && items[i] == place);
		LabelsOf(db, items[i], atPlace ? placeAsked : asked, labels);
		for (size_t l = 0; l < labels.size(); ++l)
		{
			const bool matches = KFCScriptLabelSearch::Matches(labels[l].second, asked);
			const bool isPlace = atPlace && (placeKey.IsEmpty() || labels[l].first.IsEqual(placeKey));
			if (isPlace && outPlaceMatches != nil)
				*outPlaceMatches = matches;
			if (!matches && !isPlace)
				continue;
			if (outHits.size() >= limit)
			{
				outCapped = true;
				return true;
			}
			KFCResultModel::Hit hit;
			BuildRow(docRef, items[i], labels[l].first, labels[l].second, &asked, hit);
			outHits.push_back(hit);
		}
	}
	if (withTables && outVersions != nil && !outCapped)
		WalkTablesAndCells(docRef, limit, asked, outHits, outCapped, *outVersions);
	return true;
}

// The documents a face's button walks (S2, S9): every open one over All Documents, the front one over Document - none: outWords says so.
bool RangeDocs(const KFCScriptLabelConditions& asked, std::vector<KFCBookScope::ChapterDoc>& outDocs, PMString& outWords)
{
	outDocs.clear();
	if (asked.scope == IWalkerScopeFactoryUtils::kAllDocumentScope)
		KFCBookScope::ListOpenDocuments(outDocs);
	else
	{
		IDocument* const doc = KFCBookScope::ActiveDocument();		// non-owning
		if (doc != nil)
			outDocs.push_back(KFCBookScope::DocAsChapter(doc));
	}
	if (!outDocs.empty())
		return true;
	outWords.Append("No document is open.");
	return false;
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
			else if (line == "whole=1")
				conditions.whole = true;
			else if (line == "grep=1")
				conditions.grep = true;
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
	return FirstMatch(label, asked, nil);
}

bool KFCScriptLabelSearch::GrepIsValid(const KFCScriptLabelConditions& asked, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	if (!asked.grep || asked.find.IsEmpty())
		return true;
	const GrepCompiled& g = GrepOf(asked);
	if (g.valid)
		return true;
	outWhy.Append("Find Label: is not a GREP expression this kind can read (");
	outWhy.Append(g.why.c_str());
	outWhy.Append(") - nothing was searched.");
	outWhy.SetTranslatable(kFalse);
	return false;
}

PMString KFCScriptLabelSearch::NewValue(const PMString& label, const KFCScriptLabelConditions& asked)
{
	// NO FIND LABEL, OR WHOLE LABEL WITHOUT GREP: Change Label: as it stands (S7). GREP: every match given Change Label:, its $1, $& and $$ read as ECMAScript reads them
	// (std::regex_replace) - Whole Label: the whole label is the match. Otherwise every occurrence of Find Label: in the label, left to right and none over another,
	// given Change Label: - found as Matches finds it (folded unless Case Sensitive: a position in the folded label is the same position in the label - U32Of),
	// the rest of the label kept as it was.
	PMString out;
	if (asked.find.IsEmpty() || (asked.whole && !asked.grep))
		out = asked.change;
	else if (asked.grep)
	{
		out = label;
		const GrepCompiled& g = GrepOf(asked);
		if (g.valid)
		{
			const std::wstring w(WideOf(label));
			const std::wstring format(WideOf(asked.change));
			try
			{
				if (asked.whole)
				{
					std::wsmatch m;
					if (std::regex_match(w, m, g.re))
						out = WordsOfWide(m.format(format));
				}
				else
					out = WordsOfWide(std::regex_replace(w, g.re, format));
			}
			catch (const std::regex_error&)
			{
				out = label;		// (nothing written for a label the pattern cannot be run on - callers count it as already as asked)
			}
		}
	}
	else
	{
		const bool fold = !asked.caseSensitive;
		const std::u32string folded(U32Of(label, fold));
		const std::u32string find(U32Of(asked.find, fold));
		const std::u32string original(U32Of(label, false));
		const std::u32string change(U32Of(asked.change, false));
		std::u32string result;
		size_t at = 0;
		for (size_t hit = folded.find(find, at); hit != std::u32string::npos; hit = folded.find(find, at))
		{
			result.append(original, at, hit - at);
			result.append(change);
			at = hit + find.size();		// (find is not empty: the loop moves on)
		}
		result.append(original, at, std::u32string::npos);
		out = WordsOfU32(result, 0, std::u32string::npos);
	}
	out.SetTranslatable(kFalse);
	return out;
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
		std::map<UID, uint32> versions;		// (the table and cell rows' stories - text rows)
		bool capped = false;
		const bool walked = WalkDoc(docRef, limit - static_cast<size_t>(out.total), asked, hits, capped, kInvalidUID, PMString(), nil, true, &versions);
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
			KFCSearchEngine::FileChapter(targets[i], hits, out, &versions);		// every walk's filing: page order, the tally
		if (capped)
			break;
	}
	if (!out.cancelled && bar.WasCancelled(kFalse))
		out.cancelled = true;
	bar.SetPosition(barEnd);
}

bool KFCScriptLabelSearch::WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCScriptLabelConditions& asked,
	std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled)
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
	const bool walked = WalkDoc(docRef, limit, asked, outHits, outCapped, kInvalidUID, PMString(), nil, true, &outVersions);
	outCancelled = walked && bar.WasCancelled(kFalse);
	bar.SetPosition(kLabelDocSpan);
	return walked && !outCancelled;
}

PMString KFCScriptLabelSearch::RowWords(IDataBase* db, UID item, const PMString& key, const PMString& label)
{
	KFCResultModel::Hit hit;
	if (db != nil)
		BuildRow(UIDRef(db, db->GetRootUID()), item, key, label, nil, hit);
	hit.matchText.SetTranslatable(kFalse);
	return hit.matchText;
}

bool KFCScriptLabelSearch::Next(const KFCScriptLabelLook& look, KFCScriptLabelFound& out)
{
	out = KFCScriptLabelFound();
	out.words.SetTranslatable(kFalse);
	// THE RANGE - the tab's Search: (All Documents or Document - S2), no book: Find Next is the dialog's, as QR Code's (KFCQRSearch::Next).
	KFCScriptLabelConditions asked;
	(void)IsTabCurrent(&asked);
	if (!GrepIsValid(asked, out.words))
		return false;
	const bool allDocuments = (asked.scope == IWalkerScopeFactoryUtils::kAllDocumentScope);
	std::vector<KFCBookScope::ChapterDoc> docs;
	if (!RangeDocs(asked, docs, out.words))
		return false;
	// EVERY LABEL OF THE RANGE THAT MATCHES, IN THE LIST'S ORDER (S9): document by document, each one's in page order (the order the list files them in -
	// KFCSearchEngine::FileChapter), an item's keys in their order. Walked anew at every press: a label written or taken away since is met. The one found last
	// stands among them as a PLACE (WalkDoc's `place`) even when its label no longer matches - Change and Change/Find write it so - and is no candidate then.
	struct Found
	{
		size_t					doc;
		KFCResultModel::Hit		hit;
		bool					placeOnly;		// the one found last, whose label no longer matches
	};
	std::vector<Found> found;
	size_t candidates = 0;
	for (size_t d = 0; d < docs.size(); ++d)
	{
		const bool lastHere = look.lastDoc.GetDataBase() != nil && docs[d].docRef.GetDataBase() == look.lastDoc.GetDataBase() && look.lastItem != kInvalidUID;
		std::vector<KFCResultModel::Hit> hits;
		bool capped = false;
		bool lastMatches = true;
		if (!WalkDoc(docs[d].docRef, static_cast<size_t>(KFCResultModel::kKFCCollectObjectLimit), asked, hits, capped,
				lastHere ? look.lastItem : kInvalidUID, look.lastKey, &lastMatches))
			continue;
		KFCResultModel::OrderHitsByPage(hits);
		for (size_t h = 0; h < hits.size(); ++h)
		{
			Found one;
			one.doc = d;
			one.hit = hits[h];
			one.placeOnly = lastHere && !lastMatches && hits[h].itemUID == look.lastItem
				&& (look.lastKey.IsEmpty() || hits[h].labelKey.IsEqual(look.lastKey));
			if (!one.placeOnly)
				++candidates;
			found.push_back(one);
		}
	}
	if (candidates == 0)
		return false;		// (no words: the face says InDesign's own "Cannot find a match." - S9)
	// THE ONE AFTER THE ONE FOUND LAST - the first when that is not among them (gone, or another document's) - and after the last one the first again, silently (InDesign's own Find Next).
	size_t next = found.size();
	for (size_t k = 0; k < found.size() && next == found.size(); ++k)
		if (!found[k].placeOnly)
			next = k;
	for (size_t k = 0; k < found.size(); ++k)
	{
		if (look.lastDoc.GetDataBase() != nil && docs[found[k].doc].docRef.GetDataBase() == look.lastDoc.GetDataBase()
			&& found[k].hit.itemUID == look.lastItem && (look.lastKey.IsEmpty() || found[k].hit.labelKey.IsEqual(look.lastKey)))
		{
			for (size_t step = 1; step <= found.size(); ++step)
			{
				const size_t j = (k + step) % found.size();
				if (!found[j].placeOnly)
				{
					next = j;
					break;
				}
			}
			break;
		}
	}
	const Found& one = found[next];
	out.doc = docs[one.doc].docRef;
	out.item = one.hit.itemUID;
	out.key = one.hit.labelKey;
	out.key.SetTranslatable(kFalse);
	if (one.hit.isLocked)
		out.reach |= kKFCQRReachLocked;
	if (one.hit.isHidden)
		out.reach |= kKFCQRReachHidden;
	if (one.hit.onHiddenSpread)
		out.reach |= kKFCQRReachHiddenSpread;
	if (one.hit.isOverset)
		out.reach |= kKFCQRReachOverset;
	// THE LINE (QR Code's): the row's text without its "ID<uid>:" head - "<name>  <label>" - then where it stands, and its document over All Documents.
	out.words.Append(LineOf(WholeLineOf(one.hit)));
	if (one.hit.isOverset)
		out.words.Append(" - overset text");
	else if (one.hit.onPasteboard)
		out.words.Append(" - pasteboard");
	else
	{
		out.words.Append(" - page ");
		out.words.Append(one.hit.pageString);
	}
	if (allDocuments)
	{
		out.words.Append(" - ");
		out.words.Append(docs[one.doc].shortName);
	}
	return true;
}

bool KFCScriptLabelSearch::ChangeFound(const KFCScriptLabelFound& found, PMString& outWords)
{
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	KFCScriptLabelConditions asked;
	(void)IsTabCurrent(&asked);
	if (!GrepIsValid(asked, outWords))
		return false;
	// THE DOORS - nothing written yet (KFCTableSearch::ChangeFound's): its document, its item, its lock, its label as Find Next found it.
	if (found.doc == UIDRef::gNull || !KFCBookScope::IsDocStillOpen(found.doc))
	{
		outWords.Append("That item's document has been closed - press Find Next.");
		return false;
	}
	IDataBase* const db = found.doc.GetDataBase();
	if (found.item == kInvalidUID || !KFCPageItemFacts::ItemIsThere(db, found.item))
	{
		outWords.Append("That item is no longer there - press Find Next.");
		return false;
	}
	if (IsLockedNow(db, found.item))		// (read now, as the Table part's Change reads its frame: a lock set or taken off since Find Next counts as it stands)
	{
		outWords.Append("That item is locked - not changed.");
		return false;
	}
	const PMString key(found.key.IsEmpty() ? KeyOf(asked) : found.key);
	const PMString label(LabelOf(db, found.item, key));
	if (!Matches(label, asked))
	{
		outWords.Append("That item's label no longer reads as Find Label: asks - press Find Next.");
		return false;
	}
	KFCReplaceEngine::FaceLabel one;
	one.doc = found.doc;
	one.item = found.item;
	one.key = key;
	one.value = NewValue(label, asked);
	if (one.value == label)
	{
		outWords.Append("That label already reads as the Change side asks.");
		return false;
	}
	if (!KFCReplaceEngine::WriteFaceLabels(std::vector<KFCReplaceEngine::FaceLabel>(1, one), outWords))
		return false;
	outWords.Append("Changed: ");
	outWords.Append(LineOf(RowWords(db, found.item, key, one.value)));
	KFC_DIAG_LOG("LABELBTN change wrote words=%s", outWords.GetUTF8String().c_str());
	return true;
}

bool KFCScriptLabelSearch::ChangeAll(int32& outChanged, int32& outMatched, PMString& outWords)
{
	outChanged = 0;
	outMatched = 0;
	outWords.Clear();
	outWords.SetTranslatable(kFalse);
	KFCScriptLabelConditions asked;
	(void)IsTabCurrent(&asked);
	if (!GrepIsValid(asked, outWords))
		return false;
	// THE RANGE - Find Next's (S2, S9).
	std::vector<KFCBookScope::ChapterDoc> docs;
	if (!RangeDocs(asked, docs, outWords))
		return false;
	// EVERY MATCHING LABEL OF THE RANGE - no rows filed, so no rows' limit - given the Change side, one Ctrl+Z (WriteFaceLabels); a locked item's left, and one that already reads as asked.
	const size_t kNoRowsLimit = 1000000;
	std::vector<KFCReplaceEngine::FaceLabel> labels;
	int32 locked = 0, asAsked = 0;
	for (size_t d = 0; d < docs.size(); ++d)
	{
		std::vector<KFCResultModel::Hit> hits;
		bool capped = false;
		if (!WalkDoc(docs[d].docRef, kNoRowsLimit, asked, hits, capped))
			continue;
		IDataBase* const db = docs[d].docRef.GetDataBase();
		for (size_t h = 0; h < hits.size(); ++h)
		{
			++outMatched;
			if (IsLockedNow(db, hits[h].itemUID))
			{
				++locked;
				continue;
			}
			const PMString label(LabelOf(db, hits[h].itemUID, hits[h].labelKey));
			KFCReplaceEngine::FaceLabel one;
			one.doc = docs[d].docRef;
			one.item = hits[h].itemUID;
			one.key = hits[h].labelKey;
			one.value = NewValue(label, asked);
			if (one.value == label)
			{
				++asAsked;
				continue;
			}
			labels.push_back(one);
		}
	}
	if (!labels.empty() && !KFCReplaceEngine::WriteFaceLabels(labels, outWords))
		return false;
	outChanged = static_cast<int32>(labels.size());
	outWords.AppendNumber(outChanged);
	outWords.Append(" of ");
	outWords.AppendNumber(outMatched);
	outWords.Append(" label(s) changed.");
	if (locked + asAsked > 0)
	{
		outWords.Append(" Left as they were:");
		const char* sep = " ";
		if (locked > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(locked);
			outWords.Append(" locked");
			sep = ", ";
		}
		if (asAsked > 0)
		{
			outWords.Append(sep);
			outWords.AppendNumber(asAsked);
			outWords.Append(" already as asked");
		}
		outWords.Append(".");
	}
	KFC_DIAG_LOG("LABELBTN changeall changed=%d matched=%d", static_cast<int>(outChanged), static_cast<int>(outMatched));
	return true;
}

bool KFCScriptLabelSearch::WriteDocument(const UIDRef& docRef, const KFCScriptLabelConditions& asked, int32& outCount)
{
	// A QUERY RUN'S WRITE OF ONE DOCUMENT (KFCTableSearch::WriteDocument's shape): inside the run's own step, no rows - every label the conditions find, given their Change side; a
	// locked item's left, and one that already reads as asked. False = InDesign would not write one (the run takes its step back) - or a GREP that is no regular expression.
	outCount = 0;
	PMString why;
	if (!GrepIsValid(asked, why))
		return false;
	std::vector<KFCResultModel::Hit> hits;
	bool capped = false;
	if (!WalkDoc(docRef, static_cast<size_t>(kMaxInt32), asked, hits, capped))
		return false;
	IDataBase* const db = docRef.GetDataBase();
	for (size_t h = 0; h < hits.size(); ++h)
	{
		if (IsLockedNow(db, hits[h].itemUID))
			continue;
		const PMString label(LabelOf(db, hits[h].itemUID, hits[h].labelKey));
		const PMString value(NewValue(label, asked));
		if (value == label)
			continue;
		if (Write(db, hits[h].itemUID, hits[h].labelKey, value) != kSuccess)
			return false;
		++outCount;
	}
	return true;
}

ErrorCode KFCScriptLabelSearch::Write(IDataBase* db, UID item, const PMString& key, const PMString& value)
{
	// THROUGH IScriptUtils, THE DOOR ADOBE LEAVES OPEN (IScriptTagCmdData.h: plug-ins do not call its Set APIs; SetScriptingTag processes the same command). The EngineContext is
	// default-constructed - no script engine is asking, KFC is: KCM's KCMWritePageLabels, measured 2026-09-07 - the command takes it and the step lands on the undo stack. The caller's
	// step holds it.
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return kFailure;
	InterfacePtr<IScript> script(db, item, UseDefaultIID());
	Utils<IScriptUtils> scriptUtils;
	if (script == nil || !scriptUtils)
		return kFailure;
	ScriptList list;
	list.push_back(script);
	return scriptUtils->SetScriptingTag(list, EngineContext(), value, key);
}

void KFCScriptLabelSearch::MarkRowWritten(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID item, const PMString& key, const PMString& value)
{
	// (KFCObjectSpecial's E7, for one row: the print and the place as the write left them - an object row's own marks, MarkHitSpecial leaving an object row alone.)
	IDataBase* const db = docRef.GetDataBase();
	uint64 print = 0;
	uint32 length = 0;
	(void)KFCObjectSearch::Fingerprint(UIDRef(db, item), print, length);
	KFCResultModel::Hit place;
	const bool read = KFCObjectSearch::ReadItemPlace(docRef, item, place);
	KFCResultModel::MarkItemOwnChange(chapterIdx, hitIdx, kKFCSpecialScriptLabel, RowWords(db, item, key, value), print, length, read ? &place : nil);
}

void KFCScriptLabelSearch::MarkRowRemoved(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, const Holder& holder, const PMString& key)
{
	IDataBase* const db = docRef.GetDataBase();
	PMString none;
	none.SetTranslatable(kFalse);
	if (holder.kind != kKFCHolderNone)
	{
		KFCResultModel::MarkHitOwnChange(chapterIdx, hitIdx, kKFCSpecialRemoveLabel, HolderRowWords(db, holder, key, none), KFCRowUpper(), false);
		return;
	}
	uint64 print = 0;
	uint32 length = 0;
	(void)KFCObjectSearch::Fingerprint(UIDRef(db, holder.item), print, length);
	KFCResultModel::Hit place;
	const bool read = KFCObjectSearch::ReadItemPlace(docRef, holder.item, place);
	KFCResultModel::MarkItemOwnChange(chapterIdx, hitIdx, kKFCSpecialRemoveLabel, RowWords(db, holder.item, key, none), print, length, read ? &place : nil, false);
}

PMString KFCScriptLabelSearch::LabelNow(const KFCScriptLabelFound& found)
{
	PMString none;
	none.SetTranslatable(kFalse);
	if (found.doc == UIDRef::gNull || found.item == kInvalidUID || !KFCBookScope::IsDocStillOpen(found.doc))
		return none;
	IDataBase* const db = found.doc.GetDataBase();
	if (!KFCPageItemFacts::ItemIsThere(db, found.item))
		return none;
	PMString key(found.key);
	if (key.IsEmpty())
		key = IScriptLabel::kDefaultScriptLabelKey;
	key.SetTranslatable(kFalse);
	return LabelOf(db, found.item, key);
}

bool KFCScriptLabelSearch::IsLockedNow(IDataBase* db, UID item)
{
	// An object row's "locked" (KFCObjectSearch::BuildRow's facts), read at the write.
	return db != nil && item != kInvalidUID
		&& (KFCPageItemFacts::IsPageItemLockedForEdit(db, item) || KFCPageItemFacts::IsFrameOnLockedLayer(db, item));
}

const char* KFCScriptLabelSearch::OnlyReplaceWords()
{
	return "these rows are script labels - only a row's Replace writes them (the Kohaku+ tab's Change Label:).";
}

bool KFCScriptLabelSearch::HolderAt(IDataBase* db, int32 kind, UID story, TextIndex start, Holder& out)
{
	out = Holder();
	out.kind = kind;
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return false;
	const UIDRef storyRef(db, story);
	if (kind == kKFCHolderTable)
	{
		InterfacePtr<ITableModel> table(KFCTableSearch::QueryTableAt(storyRef, start));
		if (table == nil)
			return false;
		out.table = ::GetUID(table);
		return true;
	}
	if (kind == kKFCHolderCell)
	{
		UIDRef tableRef;
		GridAddress cell;
		if (!KFCTableRows::TableCellAt(storyRef, start, tableRef, cell))
			return false;
		out.table = tableRef.GetUID();
		out.cell = cell;
		return true;
	}
	return false;
}

PMString KFCScriptLabelSearch::TableLabelsWords(IDataBase* db, UID table)
{
	PMString words;
	words.SetTranslatable(kFalse);
	if (db == nil || table == kInvalidUID || !db->IsValidUID(table) || db->GetClass(table) != kTableModelBoss)
		return words;
	InterfacePtr<ITableModel> model(db, table, UseDefaultIID());
	if (model == nil)
		return words;
	const auto append = [&words](const IScriptLabel* labels) {
		if (labels == nil)
			return;
		const IScriptLabel::ScriptLabelKeyValueList tags(labels->GetTags());
		for (size_t t = 0; t < tags.size(); ++t)
		{
			words.Append(tags[t].Key());
			words.Append("=");
			words.Append(tags[t].Value());
			words.Append(";");
		}
	};
	Holder holder;
	holder.table = table;
	holder.kind = kKFCHolderTable;
	{
		InterfacePtr<IScriptLabel> labels(QueryHolderLabels(db, holder));
		append(labels);
	}
	holder.kind = kKFCHolderCell;
	const RowRange rows(model->GetTotalRows());
	const ColRange cols(model->GetTotalCols());
	for (int32 r = rows.start; r < rows.start + rows.count; ++r)
		for (int32 c = cols.start; c < cols.start + cols.count; ++c)
		{
			const GridAddress anchor(model->FindCellAnchor(GridAddress(r, c)));
			if (anchor.row != r || anchor.col != c)
				continue;		// (a merged cell's, once - at its anchor)
			holder.cell = anchor;
			InterfacePtr<IScriptLabel> labels(QueryHolderLabels(db, holder));
			if (labels == nil)
				continue;
			words.Append("@");
			words.AppendNumber(r);
			words.Append(",");
			words.AppendNumber(c);
			words.Append(":");
			append(labels);
		}
	words.SetTranslatable(kFalse);
	return words;
}

PMString KFCScriptLabelSearch::HolderLabel(IDataBase* db, const Holder& holder, const PMString& key)
{
	PMString label;
	InterfacePtr<IScriptLabel> tags(QueryHolderLabels(db, holder));
	if (tags != nil)
		label = tags->GetTag(key);
	label.SetTranslatable(kFalse);
	return label;
}

ErrorCode KFCScriptLabelSearch::WriteHolder(IDataBase* db, const Holder& holder, const PMString& key, const PMString& value)
{
	if (db == nil)
		return kFailure;
	if (holder.kind == kKFCHolderNone)
		return Write(db, holder.item, key, value);
	if (holder.table == kInvalidUID || !db->IsValidUID(holder.table) || db->GetClass(holder.table) != kTableModelBoss)
		return kFailure;
	InterfacePtr<ITableModel> table(db, holder.table, UseDefaultIID());
	if (table == nil)
		return kFailure;
	if (holder.kind == kKFCHolderTable)
	{
		// A TABLE'S, THROUGH ITS SCRIPTING OBJECT (TableScript - with its parent, so in the document's database: measured 11-05, the key written, an empty value
		// taking it away, the other keys kept, Undo putting it back, "Set Scripting Tag"): IScriptUtils::SetScriptingTag, an item's door (Write). Read back after.
		InterfacePtr<IScript> script(TableScript(table));
		Utils<IScriptUtils> scriptUtils;
		if (script == nil || !scriptUtils)
			return kFailure;
		ScriptList list;
		list.push_back(script);
		if (scriptUtils->SetScriptingTag(list, EngineContext(), value, key) != kSuccess)
			return kFailure;
		return (HolderLabel(db, holder, key) == value) ? kSuccess : kFailure;
	}
	// A CELL'S, ITS LABEL ATTRIBUTE: its tags now, `key` given `value` (an empty value takes the key away - insertLabel(key, "")'s rule, CScript.cpp), as a fresh
	// kCellAttrScriptLabelBoss handed to ITableCommands::ApplyCellOverrides over the cell (SnpSetTableFill.cpp's way to give a cell an attribute). Read back after: a write
	// InDesign took and did not do is a failure.
	IScriptLabel::ScriptLabelKeyValueList tags;
	{
		InterfacePtr<IScriptLabel> now(QueryHolderLabels(db, holder));
		if (now != nil)
			tags = now->GetTags();
	}
	IScriptLabel::ScriptLabelKeyValueList kept;
	bool placed = false;
	for (size_t i = 0; i < tags.size(); ++i)
	{
		if (tags[i].Key() == key)
		{
			placed = true;
			if (!value.IsEmpty())
				kept.push_back(IScriptLabel::ScriptLabelKeyValuePair(key, value));
		}
		else
			kept.push_back(tags[i]);
	}
	if (!placed && !value.IsEmpty())
		kept.push_back(IScriptLabel::ScriptLabelKeyValuePair(key, value));
	InterfacePtr<IScriptLabel> attr(::CreateObject2<IScriptLabel>(kCellAttrScriptLabelBoss));
	InterfacePtr<ITableCommands> commands(table, UseDefaultIID());
	if (attr == nil || commands == nil)
		return kFailure;
	attr->SetTags(kept);
	AttributeBossList attrs;
	attrs.ApplyAttribute(attr);
	if (commands->ApplyCellOverrides(table->GetCellArea(holder.cell), &attrs) != kSuccess)
		return kFailure;
	return (HolderLabel(db, holder, key) == value) ? kSuccess : kFailure;
}

PMString KFCScriptLabelSearch::HolderRowWords(IDataBase* db, const Holder& holder, const PMString& key, const PMString& label)
{
	// "Table 5x2  <key>: <label>" / "Cell r2 c1  <key>: <label>" - the panel's key not written (an item's row's rule), the label cut where an item's row cuts it (CutParts).
	PMString words;
	words.SetTranslatable(kFalse);
	if (holder.kind == kKFCHolderTable)
	{
		words.Append("Table ");
		if (db != nil && holder.table != kInvalidUID && db->IsValidUID(holder.table))
		{
			InterfacePtr<ITableModel> table(db, holder.table, UseDefaultIID());
			if (table != nil)
			{
				words.AppendNumber(table->GetTotalRows().count);
				words.Append("x");
				words.AppendNumber(table->GetTotalCols().count);
			}
		}
	}
	else
	{
		words.Append("Cell r");
		words.AppendNumber(holder.cell.row + 1);
		words.Append(" c");
		words.AppendNumber(holder.cell.col + 1);
	}
	words.Append("  ");
	if (!IsPanelKey(key))
	{
		words.Append(key);
		words.Append(": ");
	}
	LabelParts parts;
	parts.match = label;
	CutParts(parts);
	words.Append(parts.match);
	words.SetTranslatable(kFalse);
	return words;
}

bool KFCScriptLabelSearch::HolderLockedNow(IDataBase* db, UID story, TextIndex start)
{
	// A text row's lock, read at the write (the story's insert lock, the frame's, its layer's - KFCSearchEngine::IsFrameEditable through the parcel the place is composed in).
	if (db == nil || story == kInvalidUID || !db->IsValidUID(story))
		return true;
	const UIDRef storyRef(db, story);
	return !KFCSearchEngine::IsFrameEditable(storyRef, KFCSearchEngine::EditableFrameForMatch(storyRef, start));
}

// End, KFCScriptLabelSearch.cpp.
