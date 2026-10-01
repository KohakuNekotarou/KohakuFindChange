//========================================================================================
//  
//  $File: $
//  
//  Owner: 
//  
//  $Author: $
//  
//  $DateTime: $
//  
//  $Revision: $
//  
//  $Change: $
//  
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//  
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance 
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or 
//  distribution of it requires the prior written permission of Adobe.
//  
//========================================================================================


#ifndef __KBSID_h__
#define __KBSID_h__

#include "SDKDef.h"
#include "KBSBoundaryID.h"	// what the model half and the UI half must agree on (2026-10-01, the model/UI split)

// (Company, the display name, the version, the repository, the prefix, the string-key prefix and the
//  PluginID are in KBSBoundaryID.h since 2026-10-01 - both halves name them.)

// Plug-in: (kKBSPluginName, the internal name, is in KBSBoundaryID.h since 2026-10-01 - the UI half's
// PluginDependency names the model by it.)
#define kKBSFileName	"KohakuFindChange"			// Base name of the build output: KohakuFindChange.pln, its "(KohakuFindChange Resources)" folder, and the .rc OriginalFilename. MUST match the vcxproj TargetName, which is $(ProjectName) - so the VS project carries this name too. No spaces and no slash, unlike kKBSDisplayName: this one IS a file name. Same three-way split as KCM (kKCMPluginName / kKCMFileName / kKCMDisplayName).

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKBSMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKBSMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// ***** THE MODEL HALF'S IDS ONLY (2026-10-01, the model/UI split). *****
// The UI half's ids - the panel, its widgets and menus, the actions, the observers and services that
// serve the panel - moved to ui/KFCUIID.h, each at kKFCUIPrefix (0x1EA680) + the SAME offset and under
// the SAME name. The kKBSPrefix numbers they held are SPENT, not free: a saved workspace or shortcut
// set may still name them, so none is handed to anything else. Spent by the move:
//   ClassID  + 0 ... + 4, + 6, + 8, + 14 ... + 16, + 19 ... + 21
//   IID      + 0, + 2, + 8, + 9
//   ImplID   + 0 ... + 4, + 7, + 8, + 10, + 13, + 17, + 19 ... + 23, + 27 ... + 32, + 36, + 37, + 39
//   ActionID + 0 ... + 43 and WidgetID + 0 ... + 30 - all of them (only the UI half has actions and widgets)
// The retired ClassID / IID / ImplID numbers below (the commented-out lines) are this prefix's history
// and stay here; the retired ActionIDs and WidgetIDs went to KFCUIID.h with the rest of their lists, and
// still read kKBSPrefix there, because that is the prefix they were spent in.


// ClassIDs:
// Task 3 (jump + red marker): the startup/shutdown service boss (clears module state). (The
// marker-expiry idle task boss of the same task is the UI half's.)
// +5 was kKBSDrawEventServiceBoss, the Draw Event marker, retired 2026-09-26 when the marker became
// a global text adornment (kKBSHitMarkerBoss, +17). NOT reused - a class id that once shipped stays spent.
//DECLARE_PMID(kClassIDSpace, kKBSDrawEventServiceBoss, kKBSPrefix + 5)
DECLARE_PMID(kClassIDSpace, kKBSStartupShutdownBoss, kKBSPrefix + 7)
// Result invalidation: the "a document is about to close" responder and its service provider. A
// result row that names a closed document still jumps and still replaces (by reopening it), so the
// results are retired with their document. Document scope only - see KBSCloseDocResponder.cpp.
DECLARE_PMID(kClassIDSpace, kKBSCloseDocResponderBoss, kKBSPrefix + 9)
// RETIRED 2026-09-27 (not reused): the scripting provider behind app.kfcStatus / app.kfcResults.
// Verification now reads the panel itself through KIDMCP; neither property was ever in a published
// build.
//DECLARE_PMID(kClassIDSpace, kKBSScriptProviderBoss, kKBSPrefix + 10)
// RETIRED 2026-09-27 with the confirmation (not reused): the Glyph tab's replace confirmation - the
// dialog itself, and the widget that draws one glyph in
// the font that defines it. The dialog is the stock kDialogBoss plus our controller (the shape
// basicdialog and KESCL's offset dialog both use); the glyph widget is a generic panel whose
// IControlView is ours, built the same way the hit row's colour cell is.
//DECLARE_PMID(kClassIDSpace, kKBSReplaceConfirmDialogBoss, kKBSPrefix + 11)
//DECLARE_PMID(kClassIDSpace, kKBSGlyphViewWidgetBoss, kKBSPrefix + 12)
// +13 was the missing-glyph scan's own text-walker client, from the measurement phase. Removed on
// 2026-08-02 with the -2 route it existed to drive: the scan reads the composed wax and needs no
// walker at all. NOT reused - a class id that once shipped stays spent.
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 13)
// The jump marker (2026-09-26): a global text adornment service - IID_IK2SERVICEPROVIDER =
// kGlobalTextAdornmentServiceImpl + our IGlobalTextAdornment (KBSHitMarker.cpp). Replaces +5.
DECLARE_PMID(kClassIDSpace, kKBSHitMarkerBoss, kKBSPrefix + 17)
// Signs the tracked changes one replace made - "KohakuFindChange" at the row's time (2026-09-28,
// KBSSignRecordsCmd.cpp / KBSTrackChange.h).
DECLARE_PMID(kClassIDSpace, kKBSSignRecordsCmdBoss, kKBSPrefix + 18)


// InterfaceIDs:
// (+ 3 ... + 7 - the boundary's notification protocol, session interfaces and UI services - and the
//  MessageID are in KBSBoundaryID.h since 2026-10-01: the UI half queries them too.)
// The session-attached observer that retires a book-scope result set when its book closes. Its own
// IID because it is an AddIn onto kSessionBoss, which already carries observers of its own.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSBOOKWATCH, kKBSPrefix + 1)
// The lazy observer AddIn'd on kTextStoryBoss that lets the panel follow an Undo and a Redo of KBS's own
// writes (2026-09-29, KBSUndoFollow.cpp). Its own IID because kTextStoryBoss carries other people's
// IID_IOBSERVER. (+ 10, not + 3: + 3 ... + 7 are the split plan's - see above.)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSSTORYUNDOOBSERVER, kKBSPrefix + 10)


// ImplementationIDs:
// Task 3: the startup/shutdown service. (The marker-expiry idle task and the hit row's event handler
// of the same task are the UI half's.)
// +5 kKBSDrawEventSrvcImpl and +6 kKBSDrawEventHandlerImpl were the Draw Event marker, retired
// 2026-09-26 (see kKBSHitMarkerAdornmentImpl, +33). NOT reused.
//DECLARE_PMID(kImplementationIDSpace, kKBSDrawEventSrvcImpl, kKBSPrefix + 5)
//DECLARE_PMID(kImplementationIDSpace, kKBSDrawEventHandlerImpl, kKBSPrefix + 6)
DECLARE_PMID(kImplementationIDSpace, kKBSStartupShutdownImpl, kKBSPrefix + 9)
// Result invalidation: the close-document responder. No service provider of our own - a boss that
// answers ONE signal names the API's own provider implementation instead (see KBS.fr).
DECLARE_PMID(kImplementationIDSpace, kKBSCloseDocResponderImpl, kKBSPrefix + 11)
// Result invalidation: the book-close watcher (see KBSBookWatch.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSBookWatchImpl, kKBSPrefix + 12)
// RETIRED 2026-09-27 (not reused): the scripting provider's implementation (see kKBSPrefix + 10 in the
// class IDs).
//DECLARE_PMID(kImplementationIDSpace, kKBSScriptProviderImpl, kKBSPrefix + 14)
// (A commented block claiming + 5 ... + 14 were free sat here until 2026-08-02. It was left over
// from the template and every one of those numbers is taken by the lines just above, so it was an
// invitation to hand out an id twice. Removed rather than corrected - the live declarations are
// the record of what is spent.)
// RETIRED 2026-09-27 (not reused): the Glyph tab's replace confirmation's controller and glyph view.
//DECLARE_PMID(kImplementationIDSpace, kKBSReplaceConfirmDialogControllerImpl, kKBSPrefix + 15)
//DECLARE_PMID(kImplementationIDSpace, kKBSGlyphViewImpl, kKBSPrefix + 16)
// +18 was the missing-glyph scan's text-walker client implementation - removed 2026-08-02 with the
// boss it implemented (see kKBSPrefix + 13 above). NOT reused.
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 18)
// ***** + 23 ... + 28: THE MODEL/UI SPLIT'S BOUNDARY (2026-10-01). ***** Reserved for it since 2026-08-16.
// + 23 = the UI half's observer of the model's notifications (KBSModelObserver.cpp);
// + 24 ... + 26 = the model half's three session interfaces (KBSModelServices.cpp);
// + 27 = the UI half's own startup/shutdown service (KBSUIStartupShutdown.cpp);
// + 28 = the UI services the model half asks for (KBSUIServices.cpp).
// (+ 23, + 27 and + 28 are the UI half's: KFCUIID.h, at kKFCUIPrefix.)
DECLARE_PMID(kImplementationIDSpace, kKBSResultsImpl, kKBSPrefix + 24)
DECLARE_PMID(kImplementationIDSpace, kKBSRunsImpl, kKBSPrefix + 25)
DECLARE_PMID(kImplementationIDSpace, kKBSChaptersImpl, kKBSPrefix + 26)
DECLARE_PMID(kImplementationIDSpace, kKBSHitMarkerAdornmentImpl, kKBSPrefix + 33)	// IGlobalTextAdornment: the jump marker (KBSHitMarker.cpp)
// The replace's signature (2026-09-28, KBSSignRecordsCmd.cpp). (+ 34 onwards, not + 23: see the note above.)
DECLARE_PMID(kImplementationIDSpace, kKBSSignRecordsCmdImpl, kKBSPrefix + 34)	// ICommand of kKBSSignRecordsCmdBoss
DECLARE_PMID(kImplementationIDSpace, kKBSInt64DataImpl, kKBSPrefix + 35)		// IInt64Data (no stock one in the SDK)
// The panel follows an Undo and a Redo (2026-09-29, KBSUndoFollow.cpp): the lazy observer on each story a
// write of KBS's own moved.
DECLARE_PMID(kImplementationIDSpace, kKBSStoryUndoObserverImpl, kKBSPrefix + 38)


// StringKeys - the model half's (KBS_enUS.fr): the replace's own alert and what Edit > Undo calls a
// KBS write. Every other key is the UI half's (KFCUIID.h, KFCUI_enUS.fr). The English lives in
// KBS_enUS.fr; the Japanese in KBSLoc.h.
// (The Change Checked confirmation prompt's keys - kKBSConfirm*, and the Glyph tab's kKBSGlyphConfirm* -
//  stood here until 2026-09-27, when the prompt was removed.)
// ***** The replace's own alert, shown INSTEAD of running. *****
//
// A replace writes the match standing at each ticked row's place, so the rows only mean what they
// say while the matches are where the search left them, holding the text it found. Since 2026-08-10
// the run makes sure of that before it writes anything (the verify walk in KBSReplaceEngine's
// resolve pass - by place and text since 2026-09-29) and stops if they are not.
//
// The wording history, because it explains why the strings look the way they do: a standing
// conditional disclaimer ("IF the text has been edited") sat on every confirmation prompt until
// 2026-08-08, when a per-chapter fingerprint made it a statement of fact; that became a
// per-chapter "carry on / cancel?" alert; and on 2026-08-10 it became this - no question at all,
// because a work list that has come apart cannot be replaced safely whatever anyone answers.
//
// Translated, because it is about the user's own text. The status line that reports the outcome
// stays English.
// ***** The run STOPPED because the results no longer describe the document. ***** Not a question:
// the verify walk found a ticked match that no longer begins where the search left it, and nothing
// has been written (KBSReplaceEngine::TellResultsWentStale). One wording names the chapter, one
// does not - a document-scope run has no chapter to name.
//
// (Five kKBSConfirmEdited*/kKBSConfirmGates* keys stood here until 2026-08-10. They belonged to a
//  per-chapter "the text has been edited - carry on?" prompt, which the verify walk replaced.)
#define kKBSStaleResultsDocKey		kKBSStringPrefix "kKBSStaleResultsDocKey"
#define kKBSStaleResultsOneKey		kKBSStringPrefix "kKBSStaleResultsOneKey"
// What Edit > Undo calls a Change Checked run (2026-09-26, the user's call: "Replace"; Japanese UI
// through KBSJa::kReplaceStep). See the sequence in KBSReplaceEngine::ReplaceChecked for why it is named.
#define kKBSReplaceStepKey			kKBSStringPrefix "kKBSReplaceStepKey"
// ...and what it calls a Reject Change and an Accept All Changes by KohakuFindChange (2026-09-29: English on
// every UI until then, beside a Replace that was translated).
#define kKBSRejectStepKey			kKBSStringPrefix "kKBSRejectStepKey"
#define kKBSAcceptAllStepKey		kKBSStringPrefix "kKBSAcceptAllStepKey"
// ...and an Accept Change by KohakuFindChange on a row, a story or a run (2026-09-29, Show Changes).
#define kKBSAcceptStepKey			kKBSStringPrefix "kKBSAcceptStepKey"
// (kKBSStaleResultsTailKey - "Please search again." - stood here until 2026-08-10. The user's call:
//  the alert states the outcome in one sentence and the status line carries what to do next.)

// RETIRED 2026-09-27 (not reused): the script element IDs of app.kfcStatus / app.kfcResults, and
// with them the four-character ScriptIDs 'pKBs' / 'pKBr' (docs/ai-notes/kes-scriptid-registry.md).
//DECLARE_PMID(kScriptInfoIDSpace, kKBSStatusPropertyScriptElement, kKBSPrefix + 0)
//DECLARE_PMID(kScriptInfoIDSpace, kKBSResultsPropertyScriptElement, kKBSPrefix + 1)

// Initial data format version numbers
#define kKBSFirstMajorFormatNumber  RezLong(1)
#define kKBSFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKBSCurrentMajorFormatNumber kKBSFirstMajorFormatNumber
#define kKBSCurrentMinorFormatNumber kKBSFirstMinorFormatNumber

#endif // __KBSID_h__
