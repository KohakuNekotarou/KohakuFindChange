//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A style submenu of a row's right-click menu - see KFCStyleMenu.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActiveContext.h"
#include "IMenuManager.h"
#include "IMenuUtils.h"			// InsertAmpersandForDisplay - a style's "&" drawn as itself
#include "ISession.h"
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"
#include "IWorkspace.h"

// General includes:
#include "GenericID.h"			// IID_IOBJECTSTYLEGROUPMANAGER, IID_IOBJECTSTYLEINFO - an object style (not its groups)
#include "TablesID.h"			// IID_ICELLSTYLEGROUPMANAGER - the cell styles
#include "TextID.h"				// IID_ISTYLEINFO - a cell style (not its groups)
#include "UIDList.h"
#include "Utils.h"

#include <vector>

// Project includes:
#include "KFCModelTypes.h"		// KFCStylePathWords - a style's path as a person reads it
#include "KFCStyleMenu.h"
#include "KFCUIID.h"

namespace
{
// One style of the list: what it is found by in each document, and what a person reads.
struct Entry
{
	PMString	path;		// IStyleGroupHierarchy::GetFullPath(kFalse) - untranslated, a group's separator in it
	PMString	words;		// GetFullPath(kTrue) read out (KFCStylePathWords)
};

KFCStyleMenu::Kind gKind = KFCStyleMenu::kNone;
std::vector<Entry> gItems;		// the items on the menu now (at most kKFCStyleItems), in their order
PMString gNote;					// the note's words; empty = no note on the menu
PMString gPath;					// the path the last menu put them on (taken off it when the next one pops)

// THE STYLES OF A KIND the active context holds - the front document's workspace, or the session's with no document (the
// Table part's style lists' source: KFCKohakuTab's FillStyles) - in the order of their hierarchy, the panel's.
void ReadList(KFCStyleMenu::Kind kind, std::vector<Entry>& out)
{
	out.clear();
	if (kind != KFCStyleMenu::kObjectStyles && kind != KFCStyleMenu::kCellStyles)
		return;
	ISession* const session = GetExecutionContextSession();
	IActiveContext* const context = (session != nil) ? session->GetActiveContext() : nil;
	IWorkspace* const workspace = (context != nil) ? context->GetContextWorkspace() : nil;
	InterfacePtr<IStyleGroupManager> styles(workspace, (kind == KFCStyleMenu::kObjectStyles) ? IID_IOBJECTSTYLEGROUPMANAGER : IID_ICELLSTYLEGROUPMANAGER);
	if (styles == nil || styles->GetRootHierarchy() == nil)
		return;
	IDataBase* const db = ::GetDataBase(styles);
	UIDList found(db);
	// AN OBJECT STYLE IS FOUND BY IID_IOBJECTSTYLEINFO (kObjectStyleBoss's), not by the text and table styles' IID_ISTYLEINFO:
	// asked with that, the Object Styles' hierarchy gave none (2026-11-05, the first GREEN run - KT's KTScrapStore StyleNames
	// asks object styles so). A CELL STYLE BY IID_ISTYLEINFO - kCellStyleBoss's (iid-boss-dictionary; the SDK's
	// SnpManipulateTableStyle.cpp ChooseStyle asks the cell styles' hierarchy so; IID_ICELLSTYLEINFO is on no boss).
	styles->GetRootHierarchy()->GetDescendents(&found, (kind == KFCStyleMenu::kObjectStyles) ? IID_IOBJECTSTYLEINFO : IID_ISTYLEINFO);
	for (int32 k = 0; k < found.Length(); ++k)
	{
		InterfacePtr<IStyleGroupHierarchy> style(db, found[k], UseDefaultIID());
		if (style == nil)
			continue;
		Entry e;
		e.path = style->GetFullPath(kFalse);
		e.path.SetTranslatable(kFalse);
		e.words = KFCStylePathWords(style->GetFullPath(kTrue));
		e.words.SetTranslatable(kFalse);
		out.push_back(e);
	}
}

// A MENU TAKES "&" FOR ITS MNEMONIC: InDesign's own doubling, as its menus name a user's style or layer
// (conditionaltextui/ConditionTagTVNodeContextMenu.cpp SetMenuName, layerpanel/LayerPanelMenuComponent.cpp - "Prevents it
// from being interpreted as an accelerator").
PMString MenuWords(const PMString& words)
{
	PMString out(words);
	out.SetTranslatable(kFalse);
	Utils<IMenuUtils> menuUtils;
	if (menuUtils)
		menuUtils->InsertAmpersandForDisplay(&out);
	out.SetTranslatable(kFalse);
	return out;
}

const char* NoneWords(KFCStyleMenu::Kind kind)
{
	return (kind == KFCStyleMenu::kObjectStyles) ? "No Object Styles" : (kind == KFCStyleMenu::kCellStyles) ? "No Cell Styles" : "No Styles";
}

const char* PanelWords(KFCStyleMenu::Kind kind)
{
	return (kind == KFCStyleMenu::kObjectStyles) ? " more - use the Object Styles panel"
		: (kind == KFCStyleMenu::kCellStyles) ? " more - use the Cell Styles panel" : " more";
}

// The items and the note taken off `path`.
void TakeOff(IMenuManager* menuMgr, const PMString& path)
{
	for (int32 k = 0; k < kKFCStyleItems; ++k)
		menuMgr->RemoveMenuItem(path, ActionID(kKFCStyleItem0ActionID + k));
	menuMgr->RemoveMenuItem(path, kKFCStyleNoteActionID);
}
}

void KFCStyleMenu::PutOnMenu(IMenuManager* menuMgr, const PMString& path, Kind kind)
{
	if (menuMgr == nil)
		return;
	// Taken off first, then put (Tate-chu-yoko's and Unify Reading's way - never twice on the menu): off this path, and off the one
	// the last menu put them on (an object row's Apply Object Style, a table row's Apply Cell Style - one action on one path).
	TakeOff(menuMgr, path);
	if (!gPath.IsEmpty() && gPath != path)
		TakeOff(menuMgr, gPath);
	gPath = path;
	gPath.SetTranslatable(kFalse);
	std::vector<Entry> all;
	ReadList(kind, all);
	gKind = kind;
	gItems.clear();
	for (size_t k = 0; k < all.size() && k < static_cast<size_t>(kKFCStyleItems); ++k)
		gItems.push_back(all[k]);
	gNote.Clear();
	gNote.SetTranslatable(kFalse);
	if (kind == kNone)
		return;		// (a menu that offers no style list: taken off alone)
	if (all.empty())
		gNote.Append(NoneWords(kind));
	else if (all.size() > static_cast<size_t>(kKFCStyleItems))
	{
		gNote.AppendNumber(static_cast<int32>(all.size()) - kKFCStyleItems);
		gNote.Append(PanelWords(kind));
	}
	for (size_t k = 0; k < gItems.size(); ++k)
		menuMgr->AddMenuItem(ActionID(kKFCStyleItem0ActionID + static_cast<int32>(k)), path,
			PMReal(kKFCStyleItemFirstPosition + static_cast<double>(k)), kFalse);
	if (!gNote.IsEmpty())
		menuMgr->AddMenuItem(kKFCStyleNoteActionID, path, PMReal(kKFCStyleNotePosition), kFalse);
}

bool KFCStyleMenu::ItemName(int32 k, PMString& outName)
{
	if (k < 0 || static_cast<size_t>(k) >= gItems.size())
		return false;
	outName = MenuWords(gItems[k].words);
	outName.SetTranslatable(kFalse);
	return true;
}

bool KFCStyleMenu::NoteName(PMString& outName)
{
	if (gNote.IsEmpty())
		return false;
	outName = gNote;
	outName.SetTranslatable(kFalse);
	return true;
}

bool KFCStyleMenu::ItemStyle(int32 k, Kind& outKind, PMString& outPath, PMString& outWords)
{
	if (k < 0 || static_cast<size_t>(k) >= gItems.size())
		return false;
	outKind = gKind;
	outPath = gItems[k].path;
	outPath.SetTranslatable(kFalse);
	outWords = gItems[k].words;
	outWords.SetTranslatable(kFalse);
	return true;
}

bool KFCStyleMenu::IsStyleItem(const ActionID& action, int32& outK)
{
	const int32 a = static_cast<int32>(action.Get());
	if (a < kKFCStyleItem0ActionID || a >= kKFCStyleItem0ActionID + kKFCStyleItems)
		return false;
	outK = a - kKFCStyleItem0ActionID;
	return true;
}

void KFCStyleMenu::ShutdownCleanup()
{
	std::vector<Entry>().swap(gItems);
	gNote = PMString();
	gPath = PMString();
	gKind = kNone;
}

// End, KFCStyleMenu.cpp.
