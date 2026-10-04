//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  WHAT THE MODEL HALF AND THE UI HALF MUST AGREE ON (the model/UI split).
//  Both halves include this, and only this, for the values that have to be the same on both sides:
//  who publishes the plug-in, what it is called, which version it is, where it lives, the prefix
//  and the string-key prefix, the model's PluginID (the UI half depends on it), and the ids the
//  UI half uses to reach the model half - its session interfaces, the UI services the model asks
//  for, and the notification. KCM keeps the same header for the same reason (KCMBoundaryID.h):
//  a UI plug-in that named its own version and company once went through four tasks reporting
//  SDK template values (memory model-ui-separation-verification).
//
//========================================================================================

#ifndef __KBSBoundaryID_h__
#define __KBSBoundaryID_h__

#include "SDKDef.h"

// Company:
#define kKBSCompanyKey	"KohakuNekotarou"		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_". It is a string-table KEY, not what the user sees: the UI half's string table (KFCUI_enUS.fr - the only one that has it) maps it to "Kohaku Plug-Ins", so the group reads Kohaku Plug-Ins, under Window (kKBSPanelWindowMenuName) - not KohakuNekotarou (measured on the real application). KCM, KESCL, KT and KIDMCP use the same key, which is what puts them all under one group.
#define kKBSCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

// Plug-in:
#define kKBSPluginName	"KohakuBookSearch"			// Internal name: the ID system and the .rc InternalName. NEVER change it. It is NOT what the .pln on disk is called - that is kKBSFileName below - and the .fr never spells it out either (PluginVersion carries kKBSPluginID, and the ExtraPluginInfo that documents do store is the company, the URL and the alert text), so the file on disk can be renamed without it moving. Same split as KCM's kKCMPluginName.
#define kKBSDisplayName	"Kohaku Find/Change"		// Display name: the About menu item, the About box, the panel and its tab, and the .rc FileDescription. THE one definition - the UI half's string table (KFCUI_enUS.fr, the one table that has the key) puts it under kKBSPanelTitleKey and KBSPanelTitle.cpp restores the tab from it, so the copies cannot drift apart. The slash is safe here and ONLY here: menu paths are delimited with ":" (SDKDef.h kSDKDefDelimitMenuPath) and this string is a string table VALUE, never a path segment - the menu paths (KFCUIID.h) are built from kKBSPanelTitleKey and friends, which are prefix-number keys. Do not put a ":" or a bare "&" in it.
#define kKBSPrefixNumber	0x1EA600 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
													// Issued by Adobe: 0x1EA600 - 0x1EA6FF, 256 ids in every id space. The band is shared the way
													// Adobe's own customdatalink (0xb3300) and customdatalinkui (0xb3380) share theirs: this model
													// plug-in takes +0..127 and the UI plug-in kKFCUIPrefixNumber = 0x1EA680 (KFCUIID.h) takes
													// +128..255 - 128 ids per id space each. 205698, the number in the Exchange URL, is the Adobe
													// Developer Console id, NOT a prefix (0x205698 was once used here by mistake, never registered).
													// Every id is kKBSPrefix + N, so the prefix can move without moving an offset.
													// Procedure: memory id-prefix-256-slot-budget.
#define kKBSRepoURL		"https://github.com/KohakuNekotarou/KohakuFindChange"	// Where this plug-in is published. Shown as the panel icon's tooltip (KBSIconTip.cpp) and opened by the panel title (KBSPanelTitle.cpp). If the repo is ever renamed again, this line has to follow it - nothing else in the build does.
#define kKBSVersion		"1.2.0"						// Version of BOTH halves (one number). Every place it shows up takes it from this line: the About box (KFCUI_enUS.fr), the FileVersion of KBS.rc and of KFCUI.rc, and the PluginVersion resource of KBS.fr and of KFCUI.fr. The Adobe Exchange listing (https://exchange.adobe.com/apps/cc/205698/kohakufindchange) is 1.0.0 = THE 2026-07-30 BUILD: everything after f0a6f48 is NOT in it. 1.1.0 was never submitted - the store goes from 1.0.0 straight to 1.2.0. *1.2.0 has NOT been submitted yet; this line says what the next submission will be called, not what the store has. (KCM learned this the expensive way: "the version number in a history comment does not describe what was submitted" - memory kescm-cpp-panel.)

// Plug-in Prefix: (please change kKBSPrefixNumber above to modify the prefix.)
#define kKBSPrefix		RezLong(kKBSPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKBSStringPrefix	SDK_DEF_STRINGIZE(kKBSPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKBSPluginID, kKBSPrefix + 0)

// MessageIDs:
// What the model half's notifications are sent as (KBSModelNotify.h) - one message, whose payload says
// what happened (KBSNotifyPayload, KBSModelTypes.h).
DECLARE_PMID(kMessageIDSpace, kKBSModelChangedMessage, kKBSPrefix + 0)


// InterfaceIDs (the boundary's - the rest of the model's are in KBSID.h):
// + 3 ... + 7: THE MODEL/UI SPLIT'S BOUNDARY (docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md
// section 4).
// The protocol the model half's notifications travel under (KBSModelNotify.h) - and the IID the UI half's
// observer is AddIn'd on kSessionBoss with (KBSModelObserver.cpp).
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSMODELOBSERVER, kKBSPrefix + 3)
// The three session interfaces the UI half reaches the model half through (IKBSResults.h / IKBSRuns.h /
// IKBSChapters.h, on kSessionBoss).
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSRESULTS, kKBSPrefix + 4)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSRUNS, kKBSPrefix + 5)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSCHAPTERS, kKBSPrefix + 6)
// The UI services the model half asks for and does without when they are not there (IKBSUIServices.h,
// implemented by the UI half on kSessionBoss: the progress bar, the windows, the Book panel, the alert).
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSUISERVICES, kKBSPrefix + 7)

#endif // __KBSBoundaryID_h__
