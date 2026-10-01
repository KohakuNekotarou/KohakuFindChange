//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ***** WHAT THE MODEL HALF AND THE UI HALF MUST AGREE ON (2026-10-01, the model/UI split). *****
//  Both halves include this, and only this, for the values that have to be the same on both sides:
//  who publishes the plug-in, what it is called, which version it is, where it lives, the prefix
//  and the string-key prefix, the model's PluginID (the UI half depends on it), and the ids the
//  UI half uses to reach the model half - its session interfaces, the UI services the model asks
//  for, and the notification. KCM keeps the same header for the same reason (KCMBoundaryID.h):
//  a UI plug-in that named its own version and company once went through four tasks reporting
//  SDK template values (memory model-ui-separation-verification).
//  Moved here from KBSID.h byte for byte.
//
//========================================================================================

#ifndef __KBSBoundaryID_h__
#define __KBSBoundaryID_h__

#include "SDKDef.h"

// Company:
#define kKBSCompanyKey	"KohakuNekotarou"		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_". It is a string-table KEY, not what the user sees: both tables map it to "Kohaku Plug-Ins", so the group reads Kohaku Plug-Ins - under Window since 2026-09-29 (kKBSPanelWindowMenuName), under Plug-Ins before (measured on the real application 2026-08-06 - this note used to claim the group was called KohakuNekotarou). KESCL/KESCM/KT use the same key and the same value, which is what puts all four under one group.
#define kKBSCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

// Plug-in:
#define kKBSPluginName	"KohakuBookSearch"			// Internal name: the ID system and the .rc InternalName. NEVER change it. It is NOT what the .pln on disk is called - that is kKBSFileName below - and the .fr never spells it out either (PluginVersion carries kKBSPluginID, and the ExtraPluginInfo that documents do store is the company, the URL and the alert text), so the file on disk can be renamed without it moving. Same split as KESCM's kKESCMPluginName.
#define kKBSDisplayName	"Kohaku Find/Change"		// Display name: the About menu item, the About box, the panel and its tab, and the .rc FileDescription. THE one definition - both string tables put it under kKBSPanelTitleKey and KBSPanelTitle.cpp restores the tab from it, so the copies cannot drift apart. The slash is safe here and ONLY here: menu paths are delimited with ":" (SDKDef.h kSDKDefDelimitMenuPath) and this string is a string table VALUE, never a path segment - the paths above are built from kKBSPanelTitleKey and friends, which are prefix-number keys. Do not put a ":" or a bare "&" in it.
#define kKBSPrefixNumber	0x1EA600 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
													// ★★★2026-08-15: Adobe が発行した正規の prefix に差し替えた。原文＝
													// 「Following Prefix ID has been registered as per your request below : **0x1EA600 - 0x1EA6FF** .」
													// ＝**各 ID 空間 256 枠が予約された**(KESCM の 0x1EA500-0x1EA5FF に続く2本目の帯)。
													// ⚠旧値 0x205698 は **Adobe Developer Console のプラグイン ID(10進 205698)に 0x を付けて
													//   16進として読み替えただけの数値**で、**Adobe の採番体系とは無関係＝1枠も予約されていなかった**
													//   (KESCM が旧 0x205515 で踏んだのと同じ形)。⚠**Exchange の URL に出る 205698 は Console の ID
													//   であって prefix ではない**ので、この差し替えでは変わらない(kKBSVersion の行を参照)。
													// ★**オフセットは1つも動かしていない**——全 ID が kKBSPrefix + N のままで、prefix だけが動いた。
													// ★差し替え前に実測した2点(KESCM で通した手順と同じ):
													//   ①**文書に永続データを書いていない**＝KBS.fr の AddIn の宛先は kSessionBoss と
													//     kActiveContextBoss の2つだけで、kDocBoss / kDocWorkspaceBoss / kWorkspaceBoss はゼロ。
													//     ⇒ **既存の .indd は壊れない**(失うのはショートカット割り当てとパネル配置だけ)。
													//   ②**前半 128 枠に収まる**＝最大オフセットは kWidgetIDSpace の **+30**、
													//     ActionID/ClassID/ImplID/IID はいずれも **+25**。⇒ 後半を UI 側に空けておける。
													// ★★**model/UI 分割のときは、この帯を前半・後半で割る**(KESCM と同じ形):
													//     model(KBS) = 0x1EA600 (+0..127) / UI = **0x1EA680** (+128..255)
													//   1 本の帯を 2 プラグインで分けるのは Adobe 自身のやり方
													//   (customdatalink 0xb3300 / customdatalinkui 0xb3380 が両方 0xb33xx に収まっている)
													//   ＝**ID の一意性はプラグイン単位ではなく「値」で決まる**。
													//   ⚠**ID スペースごとに 128 枠**(widget と action は別勘定)。
													// ⚠**利用者側の影響**＝ActionID の値が変わるので、**割り当て済みのキーボードショートカットと
													//   パネル配置がリセットされる**(ガイド vol1-06「Note about moving ActionIDs」)。
													//   **次の Exchange 提出のリリースノートに書くこと。**
													// ⇒ 一次資料＝memory の [[id-prefix-256-slot-budget]] と
													//   docs/superpowers/plans/2026-08-13-kescm-model-ui-split-stage2.md。
#define kKBSRepoURL		"https://github.com/KohakuNekotarou/KohakuFindChange"	// Where this plug-in is published. Shown as the panel icon's tooltip (KBSIconTip.cpp) and opened by the panel title (KBSPanelTitle.cpp). It was also at the foot of the About box until 2026-08-09, when that box became name-and-version only. If the repo is ever renamed again, this line has to follow it - nothing else in the build does.
#define kKBSVersion		"1.2.0"						// Version of this plug-in. Shows up in three places: the About box, the .rc FileVersion, and the PluginVersion resource. First Adobe Exchange submission = 1.0.0 (2026-07-30), APPROVED AND PUBLISHED 2026-08-11 at https://exchange.adobe.com/apps/cc/205698/kohakufindchange. ***** 1.0.0 IS THE 2026-07-30 BUILD. ***** Raised to 1.1.0 on 2026-09-25 (the user's call), so the store's plug-in and this source stop sharing a number: everything after f0a6f48 - the prefix moved to 0x1EA600, Minimizable Find/Change, Remember Book Panel Placement, and the audits in between - is NOT in 1.0.0. Raised again to 1.2.0 on 2026-09-29 (the user's call: many features came after the 1.1.0 candidate of 2026-09-28 - Show Changes by KohakuFindChange, the panel following Undo/Redo and Find/Change's Search:, the Source Text line). 1.1.0 was never submitted, so the store goes from 1.0.0 straight to 1.2.0. *1.2.0 has NOT been submitted yet; this line says what the next submission will be called, not what the store has. (KESCM learned this the expensive way: "the version number in a history comment does not describe what was submitted" - memory kescm-cpp-panel.) Was kSDKDefPluginVersionString, the SDK template's own version, which said nothing about this plug-in.

// Plug-in Prefix: (please change kKBSPrefixNumber above to modify the prefix.)
#define kKBSPrefix		RezLong(kKBSPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKBSStringPrefix	SDK_DEF_STRINGIZE(kKBSPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKBSPluginID, kKBSPrefix + 0)

// MessageIDs:
// What the model half's notifications are sent as (2026-10-01, KBSModelNotify.h) - one message, whose
// payload says what happened (KBSNotifyPayload, KBSModelTypes.h). KBS had no message id before this one.
DECLARE_PMID(kMessageIDSpace, kKBSModelChangedMessage, kKBSPrefix + 0)


// InterfaceIDs (the boundary's - the rest of the model's are in KBSID.h):
// ***** + 3 ... + 7: THE MODEL/UI SPLIT'S BOUNDARY (2026-10-01, docs/superpowers/specs/
// 2026-10-01-kbs-model-ui-split-design.md section 4). ***** Reserved for it since 2026-08-16.
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
