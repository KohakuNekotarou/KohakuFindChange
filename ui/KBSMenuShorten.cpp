//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS) - "Shorten Main Menu Names". See KBSMenuShorten.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionManager.h"		// the menu manager is aggregated on the same boss (KTMenuBar.cpp's way)
#include "IApplication.h"
#include "IMenuFilter.h"
#include "IMenuManager.h"
#include "ISession.h"

// General includes:
#include "AdobeMenuPositions.h"	// where the six columns stand on the menu bar
#include "CPMUnknown.h"
#include "PMReal.h"
#include "PMString.h"
#include <locale>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Project includes:
#include "KFCUIID.h"
#include "KBSLoc.h"
#include "KBSMenuShorten.h"
#include "KBSMenuShortenNames.h"
#include "KBSPanelState.h"
#include "KBSDiag.h"			// KBS_DIAG_LOG - a test build's trace of every item the filter is handed

namespace
{

// The six columns by the key their menu path is built under. Checked against InDesign's own idrc_MENR
// (2026-10-03) - a title does not tell its key (Table is kTablesMenuTable_&, not T&able).
// ***** AND WHERE EACH STANDS ON THE MENU BAR (AdobeMenuPositions.h). ***** A rebuilt column is put back
// with its own entry at this position. The record cannot be trusted for it: the Plug-ins column's own
// entry never went past the filter (measured 2026-10-03 - five of the six columns' entries were recorded,
// at 20/40/60/110/120, and Plug-ins none), so a column rebuilt from its items alone would stand wherever
// the menu manager puts a column it has no position for.
struct Column { const char* key; const wchar_t* shortTitle; double position; };
const Column kColumns[] = {
	{ "&File",       KBSMenuShortenNames::kFile,    kFileMenuPosition },
	{ "&Layout",     KBSMenuShortenNames::kLayout,  kLayoutMenuPosition },
	{ "&Object",     KBSMenuShortenNames::kObject,  kObjectMenuPosition },
	{ "Plugin_Menu", KBSMenuShortenNames::kPlugins, kPluginMenuPosition },
	{ "&Window",     KBSMenuShortenNames::kWindow,  kWindowMenuPosition },
	{ "&Help",       KBSMenuShortenNames::kHelp,    kHelpMenuPosition },
};
const int32 kColumnCount = static_cast<int32>(sizeof(kColumns) / sizeof(kColumns[0]));

const char* const kRecordFileName     = "KFCMenuColumns.tsv";
const char* const kRecordSideFileName = "KFCMenuColumns.tsv.tmp";
const char* const kRecordHeader       = "# KFCMenuColumns 1";
const char* const kKeyOn              = "shortenMainMenu";
const char* const kKeyApplied         = "shortenMainMenuApplied";

struct Item
{
	std::string column;		// the column's key ("&File")
	uint32 id;				// the ActionID (0 for a sub-menu's own entry)
	std::string suffix;		// the path after "Main:<key>": "", ":", ":-", ":<sub>:" ...
	double pos;
	bool dynamic;
	bool ownerDraw;
};

std::vector<Item> gSeen;		// every item of the six columns the filter saw this start-up
bool gRegistered = false;		// this start-up registered the menus (a column's own entry went past)
bool gSettingsRead = false;
bool gOn = false;				// what the user chose
bool gApplied = false;			// what the columns are built as now
bool gRebuilding = false;		// our own AddMenuItem calls pass the filter untouched
int gRecordState = -1;			// HasRecord's answer: -1 = not asked yet this session

std::string ShortUtf8(int32 c)
{
	return PMString(kColumns[c].shortTitle).GetUTF8String();
}

int32 ColumnIndexOfKey(const std::string& key)
{
	for (int32 c = 0; c < kColumnCount; ++c)
		if (key == kColumns[c].key)
			return c;
	return -1;
}

// The column `path` is on (index into kColumns), or -1; *suffix gets the rest of the path.
int32 ColumnOf(const std::string& path, std::string* suffix)
{
	for (int32 c = 0; c < kColumnCount; ++c)
	{
		const std::string head = std::string("Main:") + kColumns[c].key;
		if (path.compare(0, head.size(), head) != 0)
			continue;
		if (path.size() > head.size() && path[head.size()] != ':')
			continue;
		if (suffix != nil)
			*suffix = path.substr(head.size());
		return c;
	}
	return -1;
}

// A path as the menu manager is given one. TRANSLATABLE on purpose: the sub-menus inside the columns are
// named by keys and must still be looked up, and a token no table holds shows as written (measured, KT).
PMString AsPath(const std::string& utf8)
{
	PMString path;
	path.SetUTF8String(utf8);
	path.SetTranslatable(kTrue);
	return path;
}

std::string ItemKey(const Item& it)
{
	std::ostringstream k;
	k << it.column << '\t' << it.id << '\t' << it.suffix;
	return k.str();
}

void ReadSettingsOnce()
{
	if (gSettingsRead)
		return;
	gSettingsRead = true;
	std::string text;
	if (!KBSPanelStateReadText(text))
		return;		// no file, or a part-read one: OFF
	gOn = KBSPanelStateReadBool(text, kKeyOn, false);
	gApplied = KBSPanelStateReadBool(text, kKeyApplied, false);
}

std::string Serialize(const std::vector<Item>& items)
{
	std::ostringstream o;
	o.imbue(std::locale::classic());
	o.precision(17);
	o << kRecordHeader << ' ' << items.size() << '\n';
	for (const Item& it : items)
		o << it.column << '\t' << it.id << '\t' << it.pos << '\t' << (it.dynamic ? 1 : 0) << '\t'
		  << (it.ownerDraw ? 1 : 0) << '\t' << it.suffix << '\n';
	return o.str();
}

// All or nothing: a header that does not match, a bad line, or a count that is not the header's leaves
// `out` empty - a short record would take items out of InDesign's menus.
bool Parse(const std::string& text, std::vector<Item>& out)
{
	out.clear();
	std::istringstream in(text);
	std::string line;
	if (!std::getline(in, line))
		return false;
	const std::string head = std::string(kRecordHeader) + " ";
	if (line.compare(0, head.size(), head) != 0)
		return false;
	size_t expected = 0;
	{
		std::istringstream n(line.substr(head.size()));
		n.imbue(std::locale::classic());
		if (!(n >> expected))
			return false;
	}
	while (std::getline(in, line))
	{
		if (line.empty())
			continue;
		std::vector<std::string> f;
		size_t start = 0;
		for (int k = 0; k < 5; ++k)
		{
			const size_t tab = line.find('\t', start);
			if (tab == std::string::npos)
			{
				out.clear();
				return false;
			}
			f.push_back(line.substr(start, tab - start));
			start = tab + 1;
		}
		f.push_back(line.substr(start));		// the suffix may be empty
		Item it;
		it.column = f[0];
		std::istringstream id(f[1]), pos(f[2]);
		id.imbue(std::locale::classic());
		pos.imbue(std::locale::classic());
		if (ColumnIndexOfKey(it.column) < 0 || !(id >> it.id) || !(pos >> it.pos) || (f[3] != "0" && f[3] != "1")
			|| (f[4] != "0" && f[4] != "1"))
		{
			out.clear();
			return false;
		}
		it.dynamic = (f[3] == "1");
		it.ownerDraw = (f[4] == "1");
		it.suffix = f[5];
		out.push_back(it);
	}
	if (out.size() != expected)
	{
		out.clear();
		return false;
	}
	return true;
}

bool LoadRecord(std::vector<Item>& out)
{
	std::string text;
	return KBSPanelStateReadSiblingFile(kRecordFileName, text) && Parse(text, out) && !out.empty();
}

// What a rebuild puts back: this start-up's registration when there was one (fresh), otherwise the file;
// plus the items added after start-up this session, each once.
std::vector<Item> ItemsToRebuild()
{
	std::vector<Item> items;
	if (!gRegistered)
		LoadRecord(items);
	std::set<std::string> have;
	for (const Item& it : items)
		have.insert(ItemKey(it));
	for (const Item& it : gSeen)
		if (have.insert(ItemKey(it)).second)
			items.push_back(it);
	return items;
}

// Takes each column down - under its official key and under its short title - and puts every item back
// under the short title (toShort) or the official key. A column with no item is left alone.
// false = nothing to rebuild from, or no menu manager.
bool Rebuild(bool toShort)
{
	const std::vector<Item> items = ItemsToRebuild();
	KBS_DIAG_LOG("MENUSHORTEN REBUILD toShort=%d items=%u registered=%d", toShort ? 1 : 0,
		static_cast<unsigned>(items.size()), gRegistered ? 1 : 0);
	if (items.empty())
		return false;
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	InterfacePtr<IMenuManager> menus(actionMgr, UseDefaultIID());
	if (menus == nil)
		return false;
	gRebuilding = true;
	for (int32 c = 0; c < kColumnCount; ++c)
	{
		bool any = false;
		for (const Item& it : items)
			if (it.column == kColumns[c].key)
			{
				any = true;
				break;
			}
		if (!any)
			continue;
		const std::string official = std::string("Main:") + kColumns[c].key;
		const std::string shortened = "Main:" + ShortUtf8(c);
		menus->RemoveSubmenuAndChildren(AsPath(official + ":"));
		menus->RemoveSubmenuAndChildren(AsPath(shortened + ":"));
		const std::string head = toShort ? shortened : official;
		// The column's own entry first, at its place on the bar (see kColumns) - then its items, with the
		// recorded entry (id 0, ":") skipped so the column is not added twice.
		menus->AddMenuItem(kInvalidActionID, AsPath(head + ":"), PMReal(kColumns[c].position), kFalse);
		for (const Item& it : items)
			if (it.column == kColumns[c].key && !(it.id == 0 && it.suffix == ":"))
				menus->AddMenuItem(ActionID(it.id), AsPath(head + it.suffix), PMReal(it.pos),
					it.dynamic ? kTrue : kFalse, it.ownerDraw ? kTrue : kFalse);
	}
	gRebuilding = false;
	gApplied = toShort;
	return true;
}

// ***** A RE-ADD TAKES THE OLD COPY OFF THE SHORT COLUMN FIRST (2026-10-03, measured). ***** A dynamic menu
// (IDynamicMenu::RebuildMenu, run each time its menu is read or opened) removes its old items by the OFFICIAL
// path - RemoveMenuItem("Main:&Window:...", id), the SDK's own dynamicmenu sample's way
// (DynMnuDynamicMenu.cpp) - and the filter never hears of a removal. Under a short title that removal misses,
// so every re-add came on top of the last: Window > Workspace grew to 12,897 items. Removing the item from
// `path` (the short path it is about to be added under) before the add puts it back once.
void RemoveShortCopy(const PMString& path, ActionID id)
{
	InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
	InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
	InterfacePtr<IMenuManager> menus(actionMgr, UseDefaultIID());
	if (menus == nil)
		return;
	gRebuilding = true;
	menus->RemoveMenuItem(path, id);
	gRebuilding = false;
}

const char* WriteKeys()
{
	std::vector<std::pair<std::string, std::string> > keys;
	keys.push_back(std::make_pair(std::string(kKeyOn), std::string(gOn ? "true" : "false")));
	keys.push_back(std::make_pair(std::string(kKeyApplied), std::string(gApplied ? "true" : "false")));
	return KBSPanelStateWriteKeys(keys);
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// The menu filter
//----------------------------------------------------------------------------------------

/** Records the six columns' items as they are added, keeps them short while the columns stand short, and
	keeps the toggle's own item out of a non-Japanese UI. */
class KBSMenuShortenFilter : public CPMUnknown<IMenuFilter>
{
public:
	KBSMenuShortenFilter(IPMUnknown* boss) : CPMUnknown<IMenuFilter>(boss) {}
	virtual void FilterMenuItem(ActionID* actionID, PMString* menuPath, PMReal* menuPos, bool16 isDynamic, bool16 isOwnerDraw);
};

CREATE_PMINTERFACE(KBSMenuShortenFilter, kKBSMenuShortenFilterImpl)

void KBSMenuShortenFilter::FilterMenuItem(ActionID* actionID, PMString* menuPath, PMReal* menuPos, bool16 isDynamic, bool16 isOwnerDraw)
{
	if (gRebuilding || actionID == nil || menuPath == nil)
		return;
	KBS_DIAG_LOG("MENUFILTER id=%u pos=%.3f dyn=%d own=%d path=%s", actionID->Get(),
		(menuPos != nil) ? ToDouble(*menuPos) : 0.0, isDynamic ? 1 : 0, isOwnerDraw ? 1 : 0,
		menuPath->GetUTF8String().c_str());
	// The panel menu's own item, only in a Japanese UI (the user's call). Registration and its saved data
	// are kept per UI language, so an English InDesign never gets it ("" = not added: IMenuFilter.h).
	if (*actionID == kKBSShortenMainMenuActionID)
	{
		if (!KBSLoc::JapaneseUI())
			*menuPath = PMString();
		return;
	}
	std::string suffix;
	const int32 c = ColumnOf(menuPath->GetUTF8String(), &suffix);
	if (c < 0)
		return;
	Item it;
	it.column = kColumns[c].key;
	it.id = actionID->Get();
	it.suffix = suffix;
	it.pos = (menuPos != nil) ? ToDouble(*menuPos) : 0.0;
	it.dynamic = (isDynamic != kFalse);
	it.ownerDraw = (isOwnerDraw != kFalse);
	gSeen.push_back(it);
	if (it.id == 0 && suffix == ":")		// a column's own entry is added only by a registration
		gRegistered = true;
	// While the columns stand short, an item added to an official column goes to the short one - or a
	// second column with the official title would appear. Read on the first call: this can run before
	// the lazy start-up reads the settings (an extension adding to Window early in a start-up).
	ReadSettingsOnce();
	if (gApplied && KBSLoc::JapaneseUI())
	{
		const std::string head = std::string("Main:") + kColumns[c].key;	// ASCII: characters == bytes
		menuPath->Remove(0, static_cast<CharCounter>(head.size()));
		PMString shortHead;
		shortHead.SetUTF8String("Main:" + ShortUtf8(c));
		menuPath->Insert(shortHead, 0);
		if (it.id != 0)		// not a sub-menu's own entry: removing one would leave its items behind (IMenuManager.h)
			RemoveShortCopy(*menuPath, *actionID);
	}
}

//----------------------------------------------------------------------------------------
// The panel's side
//----------------------------------------------------------------------------------------

void KBSMenuShorten::Startup()
{
	if (!KBSLoc::JapaneseUI())
		return;
	ReadSettingsOnce();
	KBS_DIAG_LOG("MENUSHORTEN STARTUP registered=%d seen=%u on=%d applied=%d", gRegistered ? 1 : 0,
		static_cast<unsigned>(gSeen.size()), gOn ? 1 : 0, gApplied ? 1 : 0);
	if (gRegistered && KBSPanelStateWriteSiblingFile(kRecordFileName, kRecordSideFileName, Serialize(gSeen)) == nil)
		gRecordState = 1;
	bool rebuilt = false;
	if (gOn)
		rebuilt = Rebuild(true);	// every start-up: also folds a column that came up official early
	else if (gApplied)
		rebuilt = Rebuild(false);	// left short (the settings were OFF but the menus were not)
	if (rebuilt)
		WriteKeys();
}

void KBSMenuShorten::ToggleAndSave(PMString& outStatus)
{
	outStatus = PMString();
	if (!KBSLoc::JapaneseUI())
		return;		// a shortcut in another language: the item is not even offered there
	ReadSettingsOnce();
	const bool want = !gOn;
	if (!HasRecord() || !Rebuild(want))
	{
		outStatus = PMString(KBSMenuShortenNames::kStatusNoRecord);
		return;
	}
	gOn = want;
	const char* failure = WriteKeys();
	outStatus = PMString(want ? KBSMenuShortenNames::kStatusOn : KBSMenuShortenNames::kStatusOff);
	if (failure != nil)
		outStatus.Append(PMString(KBSMenuShortenNames::kStatusNotSaved));
}

bool KBSMenuShorten::IsOn()
{
	ReadSettingsOnce();
	return gOn;
}

bool KBSMenuShorten::HasRecord()
{
	if (gRegistered && !gSeen.empty())
		return true;
	if (gRecordState < 0)
	{
		std::vector<Item> items;
		gRecordState = LoadRecord(items) ? 1 : 0;
	}
	return gRecordState == 1;
}

PMString KBSMenuShorten::MenuItemName()
{
	return PMString(KBSMenuShortenNames::kMenuItem);	// PMString(const wchar_t*) = not a key (KBSLoc.h)
}

// End, KBSMenuShorten.cpp.
