//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT THE MODEL HALF AND THE UI HALF MUST AGREE ON (the model/UI split).
//  Both halves include this, and only this, for the values that have to be the same on both sides:
//  who publishes the plug-in, what it is called, which version it is, where it lives, the prefix
//  and the string-key prefix, the model's PluginID (the UI half depends on it), and the ids the
//  UI half uses to reach the model half - its session interfaces, the UI services the model asks
//  for, the notification, and the stamp that tells the two halves are one build's. KCM keeps the
//  same header for the same reason (KCMBoundaryID.h): a UI plug-in that named its own version and
//  company once went through four tasks reporting SDK template values (memory
//  model-ui-separation-verification).
//
//========================================================================================

#ifndef __KFCBoundaryID_h__
#define __KFCBoundaryID_h__

#include "SDKDef.h"

// Company:
#define kKFCCompanyKey	"KohakuNekotarou"		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_". It is a string-table KEY, not what the user sees: the UI half's string table (KFCUI_enUS.fr - the only one that has it) maps it to "Kohaku Plug-Ins", so the group reads Kohaku Plug-Ins, under Window (kKFCPanelWindowMenuName) - not KohakuNekotarou (measured on the real application). All Kohaku plug-ins share the key (one group) - and must share its value (KFCUI_enUS.fr).
#define kKFCCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

// Plug-in:
#define kKFCPluginName	"KohakuBookSearch"			// Internal name: the ID system and the .rc InternalName. NEVER change it. It is NOT what the .pln on disk is called - that is kKFCFileName (KFCID.h) - and the .fr never spells it out either (PluginVersion carries kKFCPluginID, and the ExtraPluginInfo that documents do store is the company, the URL and the alert text), so the file on disk can be renamed without it moving. Same split as KCM's kKCMPluginName.
#define kKFCDisplayName	"Kohaku Find/Change"		// Display name: the About menu item, the About box, the panel and its tab, and the .rc FileDescription. THE one definition - the UI half's string table (KFCUI_enUS.fr, the one table that has the key) puts it under kKFCPanelTitleKey and KFCPanelTitle.cpp restores the tab from it, so the copies cannot drift apart. The slash is safe here and ONLY here: menu paths are delimited with ":" (SDKDef.h kSDKDefDelimitMenuPath) and this string is a string table VALUE, never a path segment - the menu paths (KFCUIID.h) are built from kKFCPanelTitleKey and friends, which are prefix-number keys. Do not put a ":" or a bare "&" in it.
#define kKFCPrefixNumber	0x1EA600 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
													// Issued by Adobe: 0x1EA600 - 0x1EA6FF, 256 ids in every id space. The band is shared the way
													// Adobe's own customdatalink (0xb3300) and customdatalinkui (0xb3380) share theirs: this model
													// plug-in takes +0..127 and the UI plug-in kKFCUIPrefixNumber = 0x1EA680 (KFCUIID.h) takes
													// +128..255 - 128 ids per id space each. 205698, the number in the Exchange URL, is the Adobe
													// Developer Console id, NOT a prefix (0x205698 was once used here by mistake, never registered).
													// Every id is kKFCPrefix + N, so the prefix can move without moving an offset.
													// Procedure: memory id-prefix-256-slot-budget.
#define kKFCRepoURL		"https://github.com/KohakuNekotarou/KohakuFindChange"	// Where this plug-in is published. Shown as the panel icon's tooltip (KFCIconTip.cpp) and opened when that icon is clicked (the panel's observer, KFCPanelTitle.cpp). If the repo is ever renamed again, this line has to follow it - nothing else in the build does.
#define kKFCVersion		"1.4.0"						// Version of BOTH halves (one number). Every place it shows up takes it from this line: the About box (KFCUI_enUS.fr), the FileVersion of KFC.rc and of KFCUI.rc, and the PluginVersion resource of KFC.fr and of KFCUI.fr. *This line names THIS build, not what the Adobe Exchange listing (https://exchange.adobe.com/apps/cc/205698/kohakufindchange) has: a submitted version is the tag of that name (from v1.2.0 on; the .pln hashes are in README.md). 1.0.0, the first listing, has no tag - it was the 2026-07-30 build (everything after f0a6f48 was not in it); 1.1.0 was never submitted. (KCM learned this the expensive way: "the version number in a history comment does not describe what was submitted" - memory kescm-cpp-panel.)

// Plug-in Prefix: (please change kKFCPrefixNumber above to modify the prefix.)
#define kKFCPrefix		RezLong(kKFCPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKFCStringPrefix	SDK_DEF_STRINGIZE(kKFCPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKFCPluginID, kKFCPrefix + 0)

// MessageIDs:
// What the model half's notifications are sent as (KFCModelNotify.h) - one message, whose payload says
// what happened (KFCNotifyPayload, KFCModelTypes.h).
DECLARE_PMID(kMessageIDSpace, kKFCModelChangedMessage, kKFCPrefix + 0)


// InterfaceIDs (the boundary's - the rest of the model's are in KFCID.h):
// + 3 ... + 7: THE MODEL/UI SPLIT'S BOUNDARY (docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md
// section 4).
// The protocol the model half's notifications travel under (KFCModelNotify.h) - and the IID the UI half's
// observer is AddIn'd on kSessionBoss with (KFCModelObserver.cpp).
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCMODELOBSERVER, kKFCPrefix + 3)
// The session interfaces the UI half reaches the model half through (IKFCResults.h / IKFCRuns.h /
// IKFCChapters.h, on kSessionBoss).
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCRESULTS, kKFCPrefix + 4)
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCRUNS, kKFCPrefix + 5)
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCCHAPTERS, kKFCPrefix + 6)
// The UI services the model half asks for and does without when they are not there (IKFCUIServices.h, which lists
// them; implemented by the UI half on kSessionBoss).
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCUISERVICES, kKFCPrefix + 7)
// Which build of the boundary each half was compiled with (kKFCBoundaryStamp): the model half's answer, and the UI
// half's. IIDs of their own, not slots at the end of a door - IKFCBoundary.h says why. + 13 and + 14: the next after
// the highest spent (+ 3 ... + 7 are full).
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCBOUNDARY, kKFCPrefix + 13)
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCUIBOUNDARY, kKFCPrefix + 14)

// THE BOUNDARY STAMP - what each half checks the other against before it calls the other's door: the one question,
// KFCOtherHalfIsThisBuild (IKFCBoundary.h), asked by the UI half in ui/KFCModelAccess.cpp and by the model half in
// KFCQueryUIServices (IKFCUIServices.h).
// The doors are vtables (IKFCResults.h / IKFCRuns.h / IKFCChapters.h / IKFCUIServices.h - and the progress bar
// IKFCUIServices hands over, KFCProgressBarUI) and the types they carry are layouts (KFCModelTypes.h): two halves
// built from different revisions of them load together all the same - the UI half's PluginDependency (KFCUI.fr) asks
// for the SDK's version only - and a call then lands in another method's slot (an old UI half's CheckObjectReplace on
// a new model half's ReplaceRows: memory facade-vtable-slot-append-only, where KCM's two halves did exactly that). So
// a UI half that does not find its own stamp in the model half calls nothing at all, and a model half that does not
// find its own in the UI half takes it for no UI half.
// kKFCBoundaryRevision GOES UP BY ONE whenever a door gains, loses or reorders a method, or a type that crosses changes
// its layout - or only what one of its fields means (KFCSpecialParams::y became Shear's axis as well: the layout the
// same, but a half that did not read it would shear the other way); kKFCVersion is in the stamp too, so two releases
// never pass for one. What each revision changed is this file's history (git log -p -- model/KFCBoundaryID.h).
#define kKFCBoundaryRevision	"21"
#define kKFCBoundaryStamp		kKFCVersion "/" kKFCBoundaryRevision

#endif // __KFCBoundaryID_h__
