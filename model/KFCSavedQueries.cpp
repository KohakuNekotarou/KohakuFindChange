//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The saved Find/Change queries - see KFCSavedQueries.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "IFindChangeOptions.h"		// SearchMode - the kinds' numbers
#include "ISysFileData.h"
#include "IUIFlagData.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "FileUtils.h"
#include "TextWalkerServiceProviderID.h"	// kFCQueryXMLReaderCmdBoss
#include "LocaleSetting.h"			// the UI language - InDesign's own queries are kept per language

// Published under source/open and reached by a relative path rather than by an include directory: the build files that
// would carry one live outside this plug-in's repository (KCM's KCMBookTreeWidgetMgr.cpp reaches DVPublicUtilities.h the
// same way). The SDK's own folder walk - the paneltreeview sample's (PnlTrvDataModel.cpp) - with WFolderTraverser behind
// it on Windows (PUBLIC_DECL: Public.lib, which a model plug-in links).
#include "../../../open/includes/architecture/PlatformFolderTraverser.h"

#include <algorithm>
#include <vector>

// Project includes:
#include "KFCSavedQueries.h"
#include "KFCDiag.h"			// KFC_DIAG_LOG - what the query reader answered, test builds only
#include "KFCKohakuQuery.h"		// the Kohaku+ tab's queries: their files

namespace
{
	/** A kind of query a run can write: its folder's name (the user's and InDesign's are named alike) and its mode. */
	struct QueryKind
	{
		const char*	folder;
		int32		mode;
	};

	// The kinds, in the order the dialog lists them. Object too (the object search spec's O14 - the run writes it with
	// InDesign's object Change All, a document at a time). Color is not here: the query run skips it
	// (KFCQuerySequence), so offering it would offer a query that does nothing.
	const QueryKind kKinds[] =
	{
		{ "Text",			IFindChangeOptions::kTextSearch },
		{ "GREP",			IFindChangeOptions::kGrepSearch },
		{ "Glyph",			IFindChangeOptions::kGlyphSearch },
		{ "Object",			IFindChangeOptions::kObjectSearch },
		{ "Transliterate",	IFindChangeOptions::kTransliterateSearch },
		// THE KOHAKU+ TAB'S (KFCKohakuQuery.h): no folder of their own - KFC makes none - so they come after the kinds that have one.
		{ "QR Code",		kKFCQRCodeSearchMode },
		{ "Table",			kKFCTableSearchMode },
		{ "Ruby",			kKFCRubySearchMode },
		{ "Kenten",			kKFCKentenSearchMode },
	};
	const int32 kKindCount = static_cast<int32>(sizeof(kKinds) / sizeof(kKinds[0]));
	const int32 kFolderKinds = 5;	// the first five: the kinds with a folder

	const char* const kQueriesFolder = "Find-Change Queries";

	/** kKinds' index for a folder's name - kKindCount for none, as KindIndexOfMode says it. Case-insensitive: these are
	    Windows folder names. */
	int32 KindIndexOfFolder(const PMString& folderName)
	{
		for (int32 i = 0; i < kFolderKinds; ++i)
			if (folderName.IsEqual(PMString(kKinds[i].folder), kFalse))
				return i;
		return kKindCount;
	}

	/** kKinds' index for a mode - kKindCount for none (the list puts such a query after every kind). */
	int32 KindIndexOfMode(int32 mode)
	{
		for (int32 i = 0; i < kKindCount; ++i)
			if (kKinds[i].mode == mode)
				return i;
		return kKindCount;
	}

	/** InDesign's own queries' root, without a language: <InDesign>\Presets\Find-Change Queries (List puts each kind's
	    language folder under the kind's). False when the presets folder cannot be had. The one place List and
	    IsUnderPresets learn where InDesign keeps its queries. */
	bool GetPresetsRoot(IDFile& out)
	{
		return FileUtils::GetPresetsFolder(&out, PMString(kQueriesFolder), PMLocaleId()) != kFalse;
	}

	/** Is this file under InDesign's own queries' root? Compared as path text, case-insensitively (Windows). */
	bool IsUnderPresets(const IDFile& file)
	{
		IDFile rootFolder;
		PMString root = GetPresetsRoot(rootFolder) ? FileUtils::SysFileToPMString(rootFolder) : PMString();
		if (root.IsEmpty())
			return false;
		root.Append("\\");
		const PMString path = FileUtils::SysFileToPMString(file);
		return path.CharCount() > root.CharCount() && path.IsEqual(root, kFalse, kTrue /*the root's length only*/);
	}

	/** Every .xml directly in this folder, described. A folder that is not there adds nothing. */
	void CollectFolder(const IDFile& folder, std::vector<KFCSavedQuery>& out)
	{
		if (FileUtils::IsDirectory(folder) == kFalse)
			return;
		// Files only, this folder only: kFalse = no folder paths returned, kFalse = not into sub-folders, kFalse = aliases
		// as they are, kTrue = no invisible files (the last has an effect on the Mac only - WFolderTraverser.h).
		PlatformFolderTraverser walk(folder, kFalse, kFalse, kFalse, kTrue);
		IDFile file;
		while (walk.Next(&file))
		{
			PMString extension;
			FileUtils::GetExtension(file, extension);
			if (!extension.IsEqual(PMString("xml"), kFalse))
				continue;
			KFCSavedQuery query;
			KFCSavedQueries::Describe(file, query);
			out.push_back(query);
		}
	}

	/** THE KOHAKU+ TAB'S QUERIES, of the files in the queries' folder itself: Ruby and Kenten in a Japanese InDesign alone, as the tab offers them (KFCKohakuTab.cpp's JapaneseKinds, and its test switch). */
	void CollectKohakuQueries(std::vector<KFCSavedQuery>& out)
	{
		IDFile folder;
		if (!KFCKohakuQuery::FolderOf(folder))
			return;
		std::vector<KFCSavedQuery> found;
		CollectFolder(folder, found);
		const bool japanese = !KFC_DIAG_FAULT("not-japanese") && LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
		for (size_t k = 0; k < found.size(); ++k)
			if (found[k].mode == kKFCQRCodeSearchMode || found[k].mode == kKFCTableSearchMode
				|| (japanese && (found[k].mode == kKFCRubySearchMode || found[k].mode == kKFCKentenSearchMode)))
				out.push_back(found[k]);
	}

	/** The dialog's order: kind, then name ignoring case. Stable-sorted, so the user's - collected first - stay before
	    InDesign's of the same name. */
	bool ListedBefore(const KFCSavedQuery& a, const KFCSavedQuery& b)
	{
		const int32 ka = KindIndexOfMode(a.mode), kb = KindIndexOfMode(b.mode);
		if (ka != kb)
			return ka < kb;
		return a.name.Compare(kFalse, b.name) < 0;
	}
}

void KFCSavedQueries::List(std::vector<KFCSavedQuery>& out)
{
	out.clear();

	// The user's: <roaming>\Find-Change Queries\<kind>. (GetAppRoamingDataFolder makes the folder when it is not there -
	// FileUtils.h - which InDesign itself has done long before: the Find/Change dialog keeps the user's queries in it.)
	IDFile userRoot;
	const bool haveUserRoot = FileUtils::GetAppRoamingDataFolder(&userRoot, PMString(kQueriesFolder)) != kFalse;

	// InDesign's own: <InDesign>\Presets\Find-Change Queries\<kind>\<the UI language> - the language's folder comes
	// UNDER the kind's, so it is appended after the kind (AppendLocalizedFolder) rather than through GetPresetsFolder's
	// own locale argument, which would put it straight under Find-Change Queries.
	IDFile presetsRoot;
	const bool havePresetsRoot = GetPresetsRoot(presetsRoot);

	for (int32 k = 0; k < kFolderKinds; ++k)
	{
		const PMString kindFolder(kKinds[k].folder);
		if (haveUserRoot)
		{
			IDFile folder(userRoot);
			FileUtils::AppendPath(&folder, kindFolder);
			CollectFolder(folder, out);
		}
		if (havePresetsRoot)
		{
			IDFile folder(presetsRoot);
			FileUtils::AppendPath(&folder, kindFolder);
			FileUtils::AppendLocalizedFolder(&folder, LocaleSetting::GetLocale());
			CollectFolder(folder, out);
		}
	}
	CollectKohakuQueries(out);
	std::stable_sort(out.begin(), out.end(), ListedBefore);
}

void KFCSavedQueries::Describe(const IDFile& file, KFCSavedQuery& out)
{
	out = KFCSavedQuery();
	out.file = file;
	FileUtils::GetBaseFileName(file, out.name);		// the file's name without its extension
	out.name.SetTranslatable(kFalse);
	out.exists = FileUtils::DoesFileExist(file) != kFalse;
	out.bundled = IsUnderPresets(file);

	// THE KOHAKU+ TAB'S: its kind in the file; its name the file's, its kind's head off (the menu of InDesign's shows the head - that menu has only the name to say it with).
	KFCKohakuQueryData own;
	if (KFCKohakuQuery::IsInItsFolder(file) && KFCKohakuQuery::Read(file, own))
	{
		out.mode = own.mode;
		const PMString head(KFCKohakuQuery::EntryName(own.mode, PMString()));
		if (out.name.CharCount() > head.CharCount() && out.name.IsEqual(head, kFalse, kTrue /*the head's length only*/))
			out.name.Remove(0, head.CharCount());
		out.name.SetTranslatable(kFalse);
		return;
	}

	// The kind is the folder's name: the parent for the user's (Find-Change Queries\<kind>\x.xml), the grandparent for
	// InDesign's (...\<kind>\<language>\x.xml). Asked by name, so a path that names no kind's folder - a run order copied
	// from another machine - answers -1 rather than a guess.
	IDFile parent;
	if (FileUtils::GetParentDirectory(file, parent) == kFalse)
		return;
	PMString parentName;
	FileUtils::GetFileName(parent, parentName);
	int32 kind = KindIndexOfFolder(parentName);
	if (kind == kKindCount)
	{
		IDFile grandparent;
		if (FileUtils::GetParentDirectory(parent, grandparent) != kFalse)
		{
			PMString grandparentName;
			FileUtils::GetFileName(grandparent, grandparentName);
			kind = KindIndexOfFolder(grandparentName);
		}
	}
	if (kind < kKindCount)
		out.mode = kKinds[kind].mode;
}

const char* KFCSavedQueries::KindName(int32 mode)
{
	const int32 kind = KindIndexOfMode(mode);
	return kind < kKindCount ? kKinds[kind].folder : "?";
}

// ONE SAVED QUERY INTO EDIT > FIND/CHANGE. kFCQueryXMLReaderCmdBoss (TextWalkerServiceProviderID.h:135), measured
// (docs/ai-notes/kfc-fcquery-reader-spike-2026-10-04.md): the query's own <QueryType> picks the tab and
// fills it whole - strings, switches and formats. A file that is not there answers kSuccess and changes nothing, so
// a caller asks for the file first (FileUtils::DoesFileExist). False = the command could not be made or reported a
// failure; the error state is left clear.
bool KFCSavedQueries::LoadIntoFindChange(const IDFile& file)
{
	InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kFCQueryXMLReaderCmdBoss));
	InterfacePtr<ISysFileData> fileData(cmd, UseDefaultIID());
	InterfacePtr<IUIFlagData> uiFlag(cmd, UseDefaultIID());
	if (cmd == nil || fileData == nil || uiFlag == nil)
		return false;
	fileData->Set(file);
	uiFlag->Set(kSuppressUI);
	const ErrorCode err = CmdUtils::ProcessCommand(cmd);
	const ErrorCode standing = ErrorUtils::PMGetGlobalErrorCode();
	const bool ok = (err == kSuccess && standing == kSuccess);
	// (Test builds: what the reader answered - a broken file's answer included.)
	KFC_DIAG_LOG("QUERYLOAD err=%d standing=%d ok=%d", static_cast<int>(err), static_cast<int>(standing), ok ? 1 : 0);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	return ok;
}

// End, KFCSavedQueries.cpp.
