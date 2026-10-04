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


#ifndef __KFCID_h__
#define __KFCID_h__

#include "SDKDef.h"
#include "KFCBoundaryID.h"	// what the model half and the UI half must agree on

// (Company, the display name, the version, the repository, the prefix, the string-key prefix and the
//  PluginID are in KFCBoundaryID.h - both halves name them.)

// Plug-in: (kKFCPluginName, the internal name, is in KFCBoundaryID.h - the UI half's PluginDependency
// names the model by it.)
#define kKFCFileName	"KohakuFindChange"			// Base name of the build output: KohakuFindChange.pln, its "(KohakuFindChange Resources)" folder, and the .rc OriginalFilename. MUST match the vcxproj TargetName, which is $(ProjectName) - so the VS project carries this name too. No spaces and no slash, unlike kKFCDisplayName: this one IS a file name. Same three-way split as KCM (kKCMPluginName / kKCMFileName / kKCMDisplayName).

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKFCMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKFCMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// THE MODEL HALF'S IDS ONLY.
// The UI half's ids - the panel, its widgets and menus, the actions, the observers and services that
// serve the panel - moved to ui/KFCUIID.h, each at kKFCUIPrefix (0x1EA680) + the SAME offset and under
// the SAME name. The kKFCPrefix numbers they held are SPENT, not free: a saved workspace or shortcut
// set may still name them, so none is handed to anything else. Spent by the move:
//   ClassID  + 0 ... + 4, + 6, + 8, + 14 ... + 16, + 19 ... + 21
//   IID      + 0, + 2, + 8, + 9
//   ImplID   + 0 ... + 4, + 7, + 8, + 10, + 13, + 17, + 19 ... + 23, + 27 ... + 32, + 36, + 37, + 39
//   ActionID + 0 ... + 43 and WidgetID + 0 ... + 30 - all of them (only the UI half has actions and widgets)
// The retired ClassID / IID / ImplID numbers below (the commented-out lines) are this prefix's history
// and stay here; the retired ActionIDs and WidgetIDs went to KFCUIID.h with the rest of their lists, and
// still read kKFCPrefix there, because that is the prefix they were spent in.


// ClassIDs:
// The startup/shutdown service boss (clears module state).
// +5 was kKFCDrawEventServiceBoss, the Draw Event marker, which the global text adornment
// (kKFCHitMarkerBoss, +17) replaced. NOT reused - a class id that once shipped stays spent.
//DECLARE_PMID(kClassIDSpace, kKFCDrawEventServiceBoss, kKFCPrefix + 5)
DECLARE_PMID(kClassIDSpace, kKFCStartupShutdownBoss, kKFCPrefix + 7)
// Result invalidation: the "a document is about to close" responder and its service provider. A
// result row that names a closed document still jumps and still replaces (by reopening it), so the
// results are retired with their document. Document scope only - see KFCCloseDocResponder.cpp.
DECLARE_PMID(kClassIDSpace, kKFCCloseDocResponderBoss, kKFCPrefix + 9)
// RETIRED (not reused): the scripting provider behind app.kfcStatus / app.kfcResults. Verification
// reads the panel itself through KIDMCP; neither property was ever in a published build.
//DECLARE_PMID(kClassIDSpace, kKFCScriptProviderBoss, kKFCPrefix + 10)
// RETIRED (not reused): the Glyph tab's replace confirmation - the dialog, and the widget that drew one
// glyph in the font that defines it.
//DECLARE_PMID(kClassIDSpace, kKFCReplaceConfirmDialogBoss, kKFCPrefix + 11)
//DECLARE_PMID(kClassIDSpace, kKFCGlyphViewWidgetBoss, kKFCPrefix + 12)
// +13 was the missing-glyph scan's own text-walker client (the scan is gone as well). NOT reused - a
// class id that once shipped stays spent.
//DECLARE_PMID(kClassIDSpace, kKFCBoss, kKFCPrefix + 13)
// The jump marker: a global text adornment service - IID_IK2SERVICEPROVIDER =
// kGlobalTextAdornmentServiceImpl + our IGlobalTextAdornment (KFCHitMarker.cpp). Replaces +5.
DECLARE_PMID(kClassIDSpace, kKFCHitMarkerBoss, kKFCPrefix + 17)
// Signs the tracked changes one replace made - "KohakuFindChange" at the row's time
// (KFCSignRecordsCmd.cpp / KFCTrackChange.h).
DECLARE_PMID(kClassIDSpace, kKFCSignRecordsCmdBoss, kKFCPrefix + 18)
// The mark every write of KFC's own leaves in its undo step (KFCUndoFollow.cpp): a command that changes
// nothing and raises a ModelChange on its document's subject, so the panel hears that step's Undo and
// Redo through an observer on the document. (+ 22: + 19 ... + 21 went to the UI half at the split.)
DECLARE_PMID(kClassIDSpace, kKFCUndoMarkCmdBoss, kKFCPrefix + 22)


// InterfaceIDs:
// (+ 3 ... + 7 - the boundary's notification protocol, session interfaces and UI services - and the
//  MessageID are in KFCBoundaryID.h: the UI half queries them too.)
// The session-attached observer that retires a book-scope result set when its book closes. Its own
// IID because it is an AddIn onto kSessionBoss, which already carries observers of its own.
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCBOOKWATCH, kKFCPrefix + 1)
// RETIRED (not reused): the lazy observer AddIn'd on kTextStoryBoss that let the panel follow an Undo and a
// Redo. A story InDesign purged from memory came back without it (the second document's Ctrl+Z went
// unheard 12 times in 18) - the observer rides on the document now (+ 11, + 12).
//DECLARE_PMID(kInterfaceIDSpace, IID_IKFCSTORYUNDOOBSERVER, kKFCPrefix + 10)
// The panel follows an Undo and a Redo (KFCUndoFollow.cpp): the lazy observer AddIn'd on kDocBoss -
// its own IID, because kDocBoss carries other people's IID_IOBSERVER - and the protocol the mark of a KFC write
// (kKFCUndoMarkCmdBoss) raises its ModelChange under.
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCDOCUNDOOBSERVER, kKFCPrefix + 11)
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCUNDOMARK, kKFCPrefix + 12)


// ImplementationIDs:
// The startup/shutdown service.
// +5 kKFCDrawEventSrvcImpl and +6 kKFCDrawEventHandlerImpl were the Draw Event marker (replaced by
// kKFCHitMarkerAdornmentImpl, +33). NOT reused.
//DECLARE_PMID(kImplementationIDSpace, kKFCDrawEventSrvcImpl, kKFCPrefix + 5)
//DECLARE_PMID(kImplementationIDSpace, kKFCDrawEventHandlerImpl, kKFCPrefix + 6)
DECLARE_PMID(kImplementationIDSpace, kKFCStartupShutdownImpl, kKFCPrefix + 9)
// Result invalidation: the close-document responder. No service provider of our own - a boss that
// answers ONE signal names the API's own provider implementation instead (see KFC.fr).
DECLARE_PMID(kImplementationIDSpace, kKFCCloseDocResponderImpl, kKFCPrefix + 11)
// Result invalidation: the book-close watcher (see KFCBookWatch.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCBookWatchImpl, kKFCPrefix + 12)
// RETIRED (not reused): the scripting provider's implementation (see kKFCPrefix + 10 in the class IDs).
//DECLARE_PMID(kImplementationIDSpace, kKFCScriptProviderImpl, kKFCPrefix + 14)
// (No list of free numbers is kept here on purpose: the declarations, live and retired, are the record
//  of what is spent - a "these are free" note goes stale and invites handing out an id twice.)
// RETIRED (not reused): the Glyph tab's replace confirmation's controller and glyph view.
//DECLARE_PMID(kImplementationIDSpace, kKFCReplaceConfirmDialogControllerImpl, kKFCPrefix + 15)
//DECLARE_PMID(kImplementationIDSpace, kKFCGlyphViewImpl, kKFCPrefix + 16)
// +18 was the missing-glyph scan's text-walker client implementation (see kKFCPrefix + 13 above).
// NOT reused.
//DECLARE_PMID(kImplementationIDSpace, kKFCImpl, kKFCPrefix + 18)
// + 23 ... + 28: THE MODEL/UI SPLIT'S BOUNDARY.
// + 23 = the UI half's observer of the model's notifications (KFCModelObserver.cpp);
// + 24 ... + 26 = the model half's three session interfaces (KFCModelServices.cpp);
// + 27 = the UI half's own startup/shutdown service (KFCUIStartupShutdown.cpp);
// + 28 = the UI services the model half asks for (KFCUIServices.cpp).
// (+ 23, + 27 and + 28 are the UI half's: KFCUIID.h, at kKFCUIPrefix.)
DECLARE_PMID(kImplementationIDSpace, kKFCResultsImpl, kKFCPrefix + 24)
DECLARE_PMID(kImplementationIDSpace, kKFCRunsImpl, kKFCPrefix + 25)
DECLARE_PMID(kImplementationIDSpace, kKFCChaptersImpl, kKFCPrefix + 26)
DECLARE_PMID(kImplementationIDSpace, kKFCHitMarkerAdornmentImpl, kKFCPrefix + 33)	// IGlobalTextAdornment: the jump marker (KFCHitMarker.cpp)
// The replace's signature (KFCSignRecordsCmd.cpp). (+ 34 onwards: + 23 ... + 28 are the boundary's.)
DECLARE_PMID(kImplementationIDSpace, kKFCSignRecordsCmdImpl, kKFCPrefix + 34)	// ICommand of kKFCSignRecordsCmdBoss
DECLARE_PMID(kImplementationIDSpace, kKFCInt64DataImpl, kKFCPrefix + 35)		// IInt64Data, a command's plain one (the SDK ships only kPersistInt64DataImpl - KFCSignRecordsCmd.cpp)
// RETIRED (not reused): the lazy observer on each story a write of KFC's own moved - see
// IID_IKFCSTORYUNDOOBSERVER above for why it went.
//DECLARE_PMID(kImplementationIDSpace, kKFCStoryUndoObserverImpl, kKFCPrefix + 38)
// The panel follows an Undo and a Redo (KFCUndoFollow.cpp): the mark command, and the lazy observer
// on the document that hears it. (+ 40: + 39 went to the UI half at the split.)
DECLARE_PMID(kImplementationIDSpace, kKFCUndoMarkCmdImpl, kKFCPrefix + 40)
DECLARE_PMID(kImplementationIDSpace, kKFCDocUndoObserverImpl, kKFCPrefix + 41)


// StringKeys - the model half's (KFC_enUS.fr): the replace's own alert and what Edit > Undo calls a
// KFC write. Every other key is the UI half's (KFCUIID.h, KFCUI_enUS.fr). The English lives in
// KFC_enUS.fr; the Japanese in KFCLoc.h.
// The replace's own alert, shown INSTEAD of running.
//
// A replace writes the match standing at each ticked row's place, so the rows only mean what they
// say while the matches are where the search left them, holding the text it found. The run makes
// sure of that before it writes anything (the verify walk in KFCReplaceEngine's resolve pass, by place
// and text) and stops if they are not.
//
// A STATEMENT, NOT A QUESTION: a work list that has come apart cannot be replaced safely whatever
// anyone answers, so there is nothing to ask. (A per-chapter "the text has been edited - carry on /
// cancel?" alert stood here once and was dropped for exactly that reason.)
//
// Translated, because it is about the user's own text. The status line that reports the outcome
// stays English.
// The run STOPPED because the results no longer describe the document: the verify walk found a
// ticked match that no longer begins where the search left it, and nothing has been written
// (KFCReplaceEngine::TellResultsWentStale). One wording names the chapter, one does not - a
// document-scope run has no chapter to name.
#define kKFCStaleResultsDocKey		kKFCStringPrefix "kKFCStaleResultsDocKey"
#define kKFCStaleResultsOneKey		kKFCStringPrefix "kKFCStaleResultsOneKey"
// What Edit > Undo calls a Change Checked run ("Replace" - the author's call; the Japanese UI's is
// KFCJa::kReplaceStep). See the sequence in KFCReplaceEngine::ReplaceChecked for why it is named.
#define kKFCReplaceStepKey			kKFCStringPrefix "kKFCReplaceStepKey"
// ...and what it calls a Reject Change and an Accept All Changes by KohakuFindChange.
#define kKFCRejectStepKey			kKFCStringPrefix "kKFCRejectStepKey"
#define kKFCAcceptAllStepKey		kKFCStringPrefix "kKFCAcceptAllStepKey"
// ...and an Accept Change by KohakuFindChange on a row, a story or a run (Show Changes).
#define kKFCAcceptStepKey			kKFCStringPrefix "kKFCAcceptStepKey"
// (No "Please search again." tail, on purpose: the alert states the outcome in one sentence and the
//  status line carries what to do next - the author's call.)

// RETIRED (not reused): the script element IDs of app.kfcStatus / app.kfcResults, and
// with them the four-character ScriptIDs 'pKBs' / 'pKBr' (docs/ai-notes/kes-scriptid-registry.md).
//DECLARE_PMID(kScriptInfoIDSpace, kKFCStatusPropertyScriptElement, kKFCPrefix + 0)
//DECLARE_PMID(kScriptInfoIDSpace, kKFCResultsPropertyScriptElement, kKFCPrefix + 1)

// Initial data format version numbers
#define kKFCFirstMajorFormatNumber  RezLong(1)
#define kKFCFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKFCCurrentMajorFormatNumber kKFCFirstMajorFormatNumber
#define kKFCCurrentMinorFormatNumber kKFCFirstMinorFormatNumber

#endif // __KFCID_h__
