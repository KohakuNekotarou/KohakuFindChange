//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A RUN ORDER AS A FILE OF ITS OWN (2026-10-07 night, the author: the query dialog's run order is kept only while
//  InDesign runs - empty after a restart - and an order is saved to a file and loaded from one, with Save Order... and
//  Load Order...). JSON, as the panel's settings file is (KBSPanelState.json), one entry a query - its kind, its name
//  and its file - so that a query can be found again by its kind and name when its file has moved (InDesign keeps the
//  user's queries in a folder of each version):
//
//    {
//    	"kfcQueryOrder": 1,
//    	"queries": [
//    		{ "kind": "GREP", "name": "...", "file": "C:\\...\\x.xml" }
//    	]
//    }
//
//  Where it goes is the person's choice, through InDesign's own Save / Open dialogs (the SDK's SDKFileSaveChooser /
//  SDKFileOpenChooser - SnpChooseFile.cpp), which start where Windows last saved / opened one:
//  KFC makes no folder of its own in InDesign's roaming folder (the author's rule - KFCPanelState.h).
//
//========================================================================================

#ifndef __KFCQueryOrderFile_h__
#define __KFCQueryOrderFile_h__

#include "IDFile.h"

#include <string>
#include <vector>

/** One entry of an order file, as written (UTF-8). */
struct KFCOrderFileEntry
{
	std::string	kind;	// "Text" / "GREP" / "Glyph" / "Transliterate" (KFCSavedQueries::KindName); "?" = none
	std::string	name;	// the query's name - its file's name without ".xml"
	std::string	file;	// the query's file, its full path
};

/** The order file's text for these entries. */
std::string KFCOrderFileText(const std::vector<KFCOrderFileEntry>& entries);

/** The entries of an order file's text (a BOM is passed over). false = it is not a KFC query order - no "kfcQueryOrder",
    no "queries" list, or text that does not read as JSON - and outEntries is empty. */
bool KFCOrderFileParse(const std::string& text, std::vector<KFCOrderFileEntry>& outEntries);

/** Ask the person for the file to save the order to (forSave) or to load one from, through InDesign's own dialog: a type
    "KFC Query Order (*.json)" and no other, for Save and Load alike; Save asks before replacing a file. false = cancelled, or the
    dialog could not be made. A test build's fault switch qd-order-file names the file instead (KFCDiag.h), since a
    test cannot press Windows' dialogs. */
bool KFCChooseOrderFile(bool forSave, IDFile& outFile);

#endif // __KFCQueryOrderFile_h__

// End, KFCQueryOrderFile.h.
