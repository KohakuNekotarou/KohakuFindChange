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
#include "IFindChangeOptions.h"		// SearchMode - the kinds' numbers

// General includes:
#include "FileUtils.h"
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

namespace
{
	/** A kind of query a run can write: its folder's name (the user's and InDesign's are named alike) and its mode. */
	struct QueryKind
	{
		const char*	folder;
		int32		mode;
	};

	// The four, in the order the dialog lists them. Object and Color are not here: the query run skips both
	// (KFCQuerySequence), so offering them would offer a query that does nothing.
	const QueryKind kKinds[] =
	{
		{ "Text",			IFindChangeOptions::kTextSearch },
		{ "GREP",			IFindChangeOptions::kGrepSearch },
		{ "Glyph",			IFindChangeOptions::kGlyphSearch },
		{ "Transliterate",	IFindChangeOptions::kTransliterateSearch },
	};
	const int32 kKindCount = static_cast<int32>(sizeof(kKinds) / sizeof(kKinds[0]));

	const char* const kQueriesFolder = "Find-Change Queries";

	/** kKinds' index for a folder's name, -1 for none. Case-insensitive: these are Windows folder names. */
	int32 KindIndexOfFolder(const PMString& folderName)
	{
		for (int32 i = 0; i < kKindCount; ++i)
			if (folderName.IsEqual(PMString(kKinds[i].folder), kFalse))
				return i;
		return -1;
	}

	int32 KindIndexOfMode(int32 mode)
	{
		for (int32 i = 0; i < kKindCount; ++i)
			if (kKinds[i].mode == mode)
				return i;
		return kKindCount;		// no kind: after all four
	}

	/** InDesign's own queries' root, without a language: <InDesign>\Presets\Find-Change Queries. Empty when the presets
	    folder cannot be had. */
	PMString PresetsRootPath()
	{
		IDFile root;
		if (FileUtils::GetPresetsFolder(&root, PMString(kQueriesFolder), PMLocaleId()) == kFalse)
			return PMString();
		return FileUtils::SysFileToPMString(root);
	}

	/** Is this file under InDesign's own queries' root? Compared as path text, case-insensitively (Windows). */
	bool IsUnderPresets(const IDFile& file)
	{
		PMString root = PresetsRootPath();
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
	const bool havePresetsRoot = FileUtils::GetPresetsFolder(&presetsRoot, PMString(kQueriesFolder), PMLocaleId()) != kFalse;

	for (int32 k = 0; k < kKindCount; ++k)
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

	// The kind is the folder's name: the parent for the user's (Find-Change Queries\<kind>\x.xml), the grandparent for
	// InDesign's (...\<kind>\<language>\x.xml). Asked by name, so a path that names no kind's folder - a run order copied
	// from another machine - answers -1 rather than a guess.
	IDFile parent;
	if (FileUtils::GetParentDirectory(file, parent) == kFalse)
		return;
	PMString parentName;
	FileUtils::GetFileName(parent, parentName);
	int32 kind = KindIndexOfFolder(parentName);
	if (kind < 0)
	{
		IDFile grandparent;
		if (FileUtils::GetParentDirectory(parent, grandparent) != kFalse)
		{
			PMString grandparentName;
			FileUtils::GetFileName(grandparent, grandparentName);
			kind = KindIndexOfFolder(grandparentName);
		}
	}
	if (kind >= 0)
		out.mode = kKinds[kind].mode;
}

const char* KFCSavedQueries::KindName(int32 mode)
{
	const int32 kind = KindIndexOfMode(mode);
	return kind < kKindCount ? kKinds[kind].folder : "?";
}

// End, KFCSavedQueries.cpp.
