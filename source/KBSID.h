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

// Plug-in:
#define kKBSPluginName	"KohakuBookSearch"			// Internal name: the ID system and the .rc InternalName. NEVER change it. It is NOT what the .pln on disk is called - that is kKBSFileName below - and the .fr never spells it out either (PluginVersion carries kKBSPluginID, and the ExtraPluginInfo that documents do store is the company, the URL and the alert text), so the file on disk can be renamed without it moving. Same split as KESCM's kKESCMPluginName.
#define kKBSFileName	"KohakuFindChange"			// Base name of the build output: KohakuFindChange.pln, its "(KohakuFindChange Resources)" folder, and the .rc OriginalFilename. MUST match the vcxproj TargetName, which is $(ProjectName) - so the VS project carries this name too. No spaces and no slash, unlike kKBSDisplayName: this one IS a file name. Same three-way split as KESCM (kKESCMPluginName / kKESCMFileName / kKESCMDisplayName).

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKBSMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKBSMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem


// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKBSActionComponentBoss, kKBSPrefix + 0)
DECLARE_PMID(kClassIDSpace, kKBSPanelWidgetBoss, kKBSPrefix + 1)
// Result tree (Task 2): the tree-view list, its row (node) boss shared by chapter and hit rows,
// and the custom multi-colour text cell that highlights the matched part of a hit line.
DECLARE_PMID(kClassIDSpace, kKBSResultListWidgetBoss, kKBSPrefix + 2)
DECLARE_PMID(kClassIDSpace, kKBSResultNodeWidgetBoss, kKBSPrefix + 3)
DECLARE_PMID(kClassIDSpace, kKBSColorTextWidgetBoss, kKBSPrefix + 4)
// Task 3 (jump + red marker): the marker-expiry idle task boss, and the startup/shutdown service
// boss (retires the idle task + clears module state).
// +5 was kKBSDrawEventServiceBoss, the Draw Event marker, retired 2026-09-26 when the marker became
// a global text adornment (kKBSHitMarkerBoss, +17). NOT reused - a class id that once shipped stays spent.
//DECLARE_PMID(kClassIDSpace, kKBSDrawEventServiceBoss, kKBSPrefix + 5)
DECLARE_PMID(kClassIDSpace, kKBSMarkerExpiryIdleTaskBoss, kKBSPrefix + 6)
DECLARE_PMID(kClassIDSpace, kKBSStartupShutdownBoss, kKBSPrefix + 7)
// Replace feature: the hit row's check box. A stock check box (kCheckBoxWidgetBoss, drawn by the
// system so it follows the UI theme) with our observer aggregated on it, the layer panel's eyeball
// pattern. Only hit rows carry one - the chapter row resource has no check box.
DECLARE_PMID(kClassIDSpace, kKBSResultCheckWidgetBoss, kKBSPrefix + 8)
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
// The panel's illustration: the system rollover icon button plus a tooltip of its own, so hovering
// it says where clicking it goes. Same shape as kLinksUIButtonBoss in open/components/linksui, and
// as KESCM's kKESCMIconWidgetBoss - which is where the panel this copies got it from.
DECLARE_PMID(kClassIDSpace, kKBSIconWidgetBoss, kKBSPrefix + 14)
// "Remember Book Panel Placement" (2026-09-25): the palette-manager service boss - registered for
// kPaletteMgrService, the service InDesign's own Book panel hangs off, so it is told when the
// palettes have been laid out and when they are about to close (KBSBookPanelPlacement.cpp).
DECLARE_PMID(kClassIDSpace, kKBSBookPanelServiceBoss, kKBSPrefix + 15)
// ...and its command interceptor: a book closing destroys its Book panel WITHOUT a word to the panel
// manager's subject (measured 2026-09-25 - neither kAboutToClosePaletteMsg nor a visibility message
// arrives), so the only moment the panel can still be measured is just BEFORE kCloseBookCmdBoss runs.
DECLARE_PMID(kClassIDSpace, kKBSBookPanelCmdWatchBoss, kKBSPrefix + 16)
// The jump marker (2026-09-26): a global text adornment service - IID_IK2SERVICEPROVIDER =
// kGlobalTextAdornmentServiceImpl + our IGlobalTextAdornment (KBSHitMarker.cpp). Replaces +5.
DECLARE_PMID(kClassIDSpace, kKBSHitMarkerBoss, kKBSPrefix + 17)
// Signs the tracked changes one replace made - "KohakuFindChange" at the row's time (2026-09-28,
// KBSSignRecordsCmd.cpp / KBSTrackChange.h).
DECLARE_PMID(kClassIDSpace, kKBSSignRecordsCmdBoss, kKBSPrefix + 18)
// The panel's message area, drawn by hand (2026-09-29, KBSStatusTextView.cpp): a generic panel with our
// IControlView and IKBSStatusTextData - the shape of the hit row's cell (+4) and of KCM's message area.
DECLARE_PMID(kClassIDSpace, kKBSStatusTextWidgetBoss, kKBSPrefix + 19)
// The UI half's own startup/shutdown service (2026-10-01, the model/UI split - KBSUIStartupShutdown.cpp):
// a startup/shutdown service is declared per boss, so the side the panel's work runs on needs a boss of
// its own. (+ 13 was the first choice of the 2026-08-16 plan; it is a retired id, so it stays spent.)
DECLARE_PMID(kClassIDSpace, kKBSUIStartupShutdownBoss, kKBSPrefix + 20)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 21)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 22)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 23)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 24)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 25)


// InterfaceIDs:
// (+ 3 ... + 7 - the boundary's notification protocol, session interfaces and UI services - and the
//  MessageID are in KBSBoundaryID.h since 2026-10-01: the UI half queries them too.)
// Per-row draw data for a hit line's colour cell: the three text segments (before / matched /
// after) the cell paints, the match segment in a highlight colour.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSROWDATA, kKBSPrefix + 0)
// The session-attached observer that retires a book-scope result set when its book closes. Its own
// IID because it is an AddIn onto kSessionBoss, which already carries observers of its own.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSBOOKWATCH, kKBSPrefix + 1)
// The observer that re-applies the "Translucent Panel" alpha when the panel is opened, closed,
// docked or floated (kPaletteVisibilityChangedMessage). Its own IID because it is an AddIn onto
// kActiveContextBoss, which carries observers that are not ours. See KBSPanelAlpha.cpp.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSPANELVISIBILITYOBSERVER, kKBSPrefix + 2)
// The observer behind "Remember Book Panel Placement" (2026-09-25): measures InDesign's Book panel as
// it closes and puts it back when it appears. Its own IID for the reason the one above has one - it
// is an AddIn onto kActiveContextBoss, next to that one. See KBSBookPanelPlacement.cpp. (+ 8, not
// + 3: see the note above. It was + 3 for an hour, unshipped, until the second re-check caught it.)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSBOOKPANELOBSERVER, kKBSPrefix + 8)
// What the panel's message area draws, split where its colour changes (2026-09-29, IKBSStatusTextData.h).
// (+ 9, not + 3: + 3 ... + 7 are the split plan's - see above.)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSSTATUSTEXTDATA, kKBSPrefix + 9)
// The lazy observer AddIn'd on kTextStoryBoss that lets the panel follow an Undo and a Redo of KBS's own
// writes (2026-09-29, KBSUndoFollow.cpp). Its own IID because kTextStoryBoss carries other people's
// IID_IOBSERVER. (+ 10, not + 3: + 3 ... + 7 are the split plan's - see above.)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSSTORYUNDOOBSERVER, kKBSPrefix + 10)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 11)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 12)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 13)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 14)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 15)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 16)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 17)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 18)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 19)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 20)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 21)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 22)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 23)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 24)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 25)


// ImplementationIDs:
DECLARE_PMID(kImplementationIDSpace, kKBSActionComponentImpl, kKBSPrefix + 0 )
// Result tree (Task 2): hierarchy adapter, row widget manager, the colour cell's view and its
// per-row data holder.
DECLARE_PMID(kImplementationIDSpace, kKBSResultListAdapterImpl, kKBSPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKBSResultListWidgetMgrImpl, kKBSPrefix + 2)
DECLARE_PMID(kImplementationIDSpace, kKBSColorTextViewImpl, kKBSPrefix + 3)
DECLARE_PMID(kImplementationIDSpace, kKBSRowDataImpl, kKBSPrefix + 4)
// Task 3: marker-expiry idle task, the hit row's event handler (click -> jump), and the
// startup/shutdown service.
// +5 kKBSDrawEventSrvcImpl and +6 kKBSDrawEventHandlerImpl were the Draw Event marker, retired
// 2026-09-26 (see kKBSHitMarkerAdornmentImpl, +33). NOT reused.
//DECLARE_PMID(kImplementationIDSpace, kKBSDrawEventSrvcImpl, kKBSPrefix + 5)
//DECLARE_PMID(kImplementationIDSpace, kKBSDrawEventHandlerImpl, kKBSPrefix + 6)
DECLARE_PMID(kImplementationIDSpace, kKBSMarkerExpiryIdleTaskImpl, kKBSPrefix + 7)
DECLARE_PMID(kImplementationIDSpace, kKBSResultNodeEHImpl, kKBSPrefix + 8)
DECLARE_PMID(kImplementationIDSpace, kKBSStartupShutdownImpl, kKBSPrefix + 9)
// Replace feature: the hit row check box's observer (click -> flip that hit's checked flag).
DECLARE_PMID(kImplementationIDSpace, kKBSResultCheckObserverImpl, kKBSPrefix + 10)
// Result invalidation: the close-document responder. No service provider of our own - a boss that
// answers ONE signal names the API's own provider implementation instead (see KBS.fr).
DECLARE_PMID(kImplementationIDSpace, kKBSCloseDocResponderImpl, kKBSPrefix + 11)
// Result invalidation: the book-close watcher (see KBSBookWatch.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSBookWatchImpl, kKBSPrefix + 12)
// The panel tab's name: an observer on the panel boss whose only job is to write the current
// scope onto the tab the moment the panel appears (see KBSPanelTitle.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSPanelObserverImpl, kKBSPrefix + 13)
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
// The result tree's OWN event handler (the list, not a row): up / down arrows that OPEN the row
// they land on, so a book search's closed chapters do not hide their hits from the keyboard.
DECLARE_PMID(kImplementationIDSpace, kKBSResultTreeEHImpl, kKBSPrefix + 17)
// +18 was the missing-glyph scan's text-walker client implementation - removed 2026-08-02 with the
// boss it implemented (see kKBSPrefix + 13 above). NOT reused.
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 18)
// The panel illustration's tooltip: hovering the icon shows the URL clicking it opens, so the
// picture is not a mystery button (see KBSIconTip.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSIconTipImpl, kKBSPrefix + 19)
// The panel's own IControlView: stock palette behaviour plus a floor under how small the user can
// drag the panel (see KBSPanelView.cpp). +19 is the tooltip above, and +18 is a retired id that is
// deliberately not reused, so this is the next free number.
DECLARE_PMID(kImplementationIDSpace, kKBSPanelViewImpl, kKBSPrefix + 20)
// "Translucent Panel" (2026-08-04, brought over from KESCM): the observer that re-applies the alpha
// when the panel's window is rebuilt, and the roll-over that takes it off while the pointer is on
// the panel. Both in KBSPanelAlpha.cpp.
DECLARE_PMID(kImplementationIDSpace, kKBSPanelVisibilityObserverImpl, kKBSPrefix + 21)
DECLARE_PMID(kImplementationIDSpace, kKBSPanelRollOverImpl, kKBSPrefix + 22)
// ***** + 23 ... + 28: THE MODEL/UI SPLIT'S BOUNDARY (2026-10-01). ***** Reserved for it since 2026-08-16.
// + 23 = the UI half's observer of the model's notifications (KBSModelObserver.cpp);
// + 24 ... + 26 = the model half's three session interfaces (KBSModelServices.cpp);
// + 27 = the UI half's own startup/shutdown service (KBSUIStartupShutdown.cpp);
// + 28 = the UI services the model half asks for (KBSUIServices.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSModelObserverImpl, kKBSPrefix + 23)
DECLARE_PMID(kImplementationIDSpace, kKBSResultsImpl, kKBSPrefix + 24)
DECLARE_PMID(kImplementationIDSpace, kKBSRunsImpl, kKBSPrefix + 25)
DECLARE_PMID(kImplementationIDSpace, kKBSChaptersImpl, kKBSPrefix + 26)
DECLARE_PMID(kImplementationIDSpace, kKBSUIStartupShutdownImpl, kKBSPrefix + 27)
DECLARE_PMID(kImplementationIDSpace, kKBSUIServicesImpl, kKBSPrefix + 28)
// "Remember Book Panel Placement" (2026-09-25): the observer on kActiveContextBoss, and the two
// halves of the palette-manager service boss (its provider and the IPaletteMgrService itself). All
// three in KBSBookPanelPlacement.cpp. (+ 29 onwards, not + 23: see the note above.)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelObserverImpl, kKBSPrefix + 29)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelServiceProviderImpl, kKBSPrefix + 30)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelPaletteMgrServiceImpl, kKBSPrefix + 31)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelCmdWatchImpl, kKBSPrefix + 32)
DECLARE_PMID(kImplementationIDSpace, kKBSHitMarkerAdornmentImpl, kKBSPrefix + 33)	// IGlobalTextAdornment: the jump marker (KBSHitMarker.cpp)
// The replace's signature (2026-09-28, KBSSignRecordsCmd.cpp). (+ 34 onwards, not + 23: see the note above.)
DECLARE_PMID(kImplementationIDSpace, kKBSSignRecordsCmdImpl, kKBSPrefix + 34)	// ICommand of kKBSSignRecordsCmdBoss
DECLARE_PMID(kImplementationIDSpace, kKBSInt64DataImpl, kKBSPrefix + 35)		// IInt64Data (no stock one in the SDK)
// The panel's message area (2026-09-29, KBSStatusTextView.cpp): its view and its data.
DECLARE_PMID(kImplementationIDSpace, kKBSStatusTextViewImpl, kKBSPrefix + 36)
DECLARE_PMID(kImplementationIDSpace, kKBSStatusTextDataImpl, kKBSPrefix + 37)
// The panel follows an Undo and a Redo (2026-09-29, KBSUndoFollow.cpp): the lazy observer on each story a
// write of KBS's own moved.
DECLARE_PMID(kImplementationIDSpace, kKBSStoryUndoObserverImpl, kKBSPrefix + 38)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKBSAboutActionID, kKBSPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKBSPanelWidgetActionID, kKBSPrefix + 1)
DECLARE_PMID(kActionIDSpace, kKBSSeparator1ActionID, kKBSPrefix + 2)
DECLARE_PMID(kActionIDSpace, kKBSPopupAboutThisActionID, kKBSPrefix + 3)
DECLARE_PMID(kActionIDSpace, kKBSSearchBookActionID, kKBSPrefix + 4)
DECLARE_PMID(kActionIDSpace, kKBSHidePrevChapterActionID, kKBSPrefix + 5)
// "Book Scope": search the whole book (ON) or just the front document (OFF). Check-mark toggle,
// the KESCL "Search book" pattern (kKESCLPopupSearchBookActionID).
DECLARE_PMID(kActionIDSpace, kKBSScopeBookActionID, kKBSPrefix + 6)
// Separator between the search command and the toggles below it (MenuDef only, no ActionDef).
// (Its MenuDef was removed on 2026-09-27, the user's call - Change Checked sits right under the
// search now. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKBSSeparator2ActionID, kKBSPrefix + 7)
// Replace feature: a separator, the replace command, and the two bulk check toggles. The replace
// command is declared here but only wired up in Phase 2 - reserving its number now keeps the
// numbering from shifting later.
DECLARE_PMID(kActionIDSpace, kKBSSeparator3ActionID, kKBSPrefix + 8)
DECLARE_PMID(kActionIDSpace, kKBSReplaceCheckedActionID, kKBSPrefix + 9)
DECLARE_PMID(kActionIDSpace, kKBSCheckAllActionID, kKBSPrefix + 10)
DECLARE_PMID(kActionIDSpace, kKBSUncheckAllActionID, kKBSPrefix + 11)
// + 12 was briefly an "Undo All Replacements" command (2026-07-28). It was dropped once the replace
// itself was fixed to be ONE command sequence across every chapter: a single Ctrl+Z now puts a
// book-wide replace back, so a command of our own had nothing left to add. Left commented rather
// than reused, so an old workspace referring to that ActionID cannot bind to something else.
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 12)
// + 13 was a temporary DoReplaceAll measurement probe (2026-07-31), removed once the experiment
// was decided. Left commented rather than reused, for the same reason as + 12 above.
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 13)
// Find Missing Glyphs: scan the scope for notdef glyphs (2026-08-01). + 12 and + 13 are burnt
// numbers (see above), so this is the first genuinely unused one.
// (Removed with the scan on 2026-09-27 - the Book panel's preflight reports missing glyphs. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKBSFindMissingGlyphsActionID, kKBSPrefix + 14)
// +15 was a second menu item that ran the same scan through the official find/change engine with
// kAnyNotDefGlyphID. Removed on 2026-08-02: that route takes InDesign down on any document holding
// overset text, by every route there is, so it must not be reachable at all. Like the numbers above
// it is NOT reused - an id that once shipped stays spent.
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 15)
// Find Overset: list the text that did not fit (2026-08-02). + 15 is a burnt number (see above),
// so this is the first genuinely unused one.
// (Removed with the scan on 2026-09-27 - the Book panel's preflight reports overset text. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKBSFindOversetActionID, kKBSPrefix + 16)
// "Save Results...": write the result set to a tab-separated text file (2026-08-03). + 12, + 13 and
// + 15 are burnt numbers (see above), so this is the first genuinely unused one.
// (Removed on 2026-09-27, the user's call. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKBSSaveResultsActionID, kKBSPrefix + 17)
// "How to Use..." on the flyout: the plug-in's operating reference, shown in a scrollable dialog
// (KBSHowTo.cpp). Deliberately NOT greyed out by anything - it is the one item that has to stay
// readable when nothing is loaded and while a run is going, which is when it is most wanted.
DECLARE_PMID(kActionIDSpace, kKBSHowToActionID, kKBSPrefix + 18)
// "Translucent Panel" on the flyout: a check-mark toggle (ON = this panel is drawn translucent while
// it floats). *Windows only. *Selectable while docked, where it has no visible effect - the flag is
// set and applies the moment the panel floats again. OFF by default, and not remembered across
// restarts. See KBSPanelAlpha.cpp.
DECLARE_PMID(kActionIDSpace, kKBSTranslucentPanelActionID, kKBSPrefix + 19)
// "Save Panel Settings": write the flyout's SETTINGS toggles to a JSON file of our own in the user's
// preferences folder, read back at startup (KBSPanelState.cpp). A plain command, not a toggle - and
// an explicit one, the way KESCM has it: settings are saved when asked for, never behind the user's
// back. What the file holds is listed in ONE place, KBSPanelState.h (it said "four settings" here,
// and then the fifth - Remember Book Panel Placement and its placement - arrived). *Book Scope is
// deliberately not among them; the reason is there too.
DECLARE_PMID(kActionIDSpace, kKBSSavePanelSettingsActionID, kKBSPrefix + 20)
// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog. Check-mark
// toggle, Windows only, OFF by default. The dialog is found through the SDK's window list, not by
// its title, so it works whatever language InDesign is running in (KBSPanelAlpha.cpp).
DECLARE_PMID(kActionIDSpace, kKBSTranslucentFindChangeActionID, kKBSPrefix + 21)
// The flyout's fourth rule, below the toggle block (2026-08-04, when the blocks were rearranged and
// three separators no longer parted five blocks). MenuDef only, no ActionDef - like the three above.
DECLARE_PMID(kActionIDSpace, kKBSSeparator4ActionID, kKBSPrefix + 22)
// "Minimizable Find/Change": put a MINIMIZE BOX on InDesign's own Find/Change dialog, so it can be
// sent to the taskbar instead of being closed. Check-mark toggle, Windows only, OFF by default.
// The same window as the toggle above, reached the same way; what differs is that this one changes
// the window's STYLE rather than its alpha. See KBSFindChangeMinimize.cpp.
DECLARE_PMID(kActionIDSpace, kKBSMinimizableFindChangeActionID, kKBSPrefix + 23)
// "Remember Book Panel Placement" on the flyout (2026-09-25): a check-mark toggle. ON = InDesign's
// own Book panel is measured as it closes (and when InDesign quits) and put back where it was when
// it next appears. OFF by default. *Unlike the toggles above, flipping it WRITES ITS OWN KEY to the
// settings file at once (the user's rule) - see KBSBookPanelPlacement.h.
DECLARE_PMID(kActionIDSpace, kKBSRememberBookPanelActionID, kKBSPrefix + 24)
// "Reject Change" on a replaced hit row's right-click menu (2026-09-26): takes back that row's
// replacement by rejecting its tracked change - the name is the Track Changes panel's own item.
DECLARE_PMID(kActionIDSpace, kKBSRejectChangeActionID, kKBSPrefix + 25)
// (kKBSPrefix + 26 was "Redo" on the same menu, 2026-09-26 to 2026-09-27 - a row taken back is replaced
//  again with Replace now. Left unused rather than handed to something else.)
// "Accept All Changes by KohakuFindChange in This Document" on a document row's right-click menu
// (2026-09-27): accepts the changes signed "KohakuFindChange" in that chapter's document and leaves everybody
// else's (2026-09-29, the user's call, the author named in the item so it says so; until then it accepted
// every change, as InDesign's own "Accept All Changes in This Document" does).
DECLARE_PMID(kActionIDSpace, kKBSAcceptAllChangesActionID, kKBSPrefix + 27)
// The rule between Change Checked and the two scans (2026-09-27, when the scans moved below it).
// (The rule between Change Checked and the scans, gone with them on 2026-09-27. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKBSSeparator5ActionID, kKBSPrefix + 28)
// "Replace" on a hit row's right-click menu (2026-09-27, the user's call): replaces that one row, with
// no prompt; the list stays a work list (KBSReplaceEngine::ReplaceHit).
DECLARE_PMID(kActionIDSpace, kKBSReplaceHitActionID, kKBSPrefix + 29)
// A STORY row's right-click menu (2026-09-27, the story level): Replace (its ticked rows), Reject Change,
// Redo, Check All, Uncheck All - each over that story's rows.
DECLARE_PMID(kActionIDSpace, kKBSStoryReplaceActionID, kKBSPrefix + 30)
DECLARE_PMID(kActionIDSpace, kKBSStoryRejectActionID, kKBSPrefix + 31)
// A story row's Redo (2026-09-27, the user's call C): the story's rows taken back with Reject Change,
// replaced again with what Find/Change holds now - the one way to do them all without ticking them. Its
// menu name is "Replace Again (Current Find/Change Settings)" since 2026-09-29 (the user's call); the
// IDs keep "Redo".
DECLARE_PMID(kActionIDSpace, kKBSStoryRedoActionID, kKBSPrefix + 32)
DECLARE_PMID(kActionIDSpace, kKBSStoryCheckAllActionID, kKBSPrefix + 33)
DECLARE_PMID(kActionIDSpace, kKBSStoryUncheckAllActionID, kKBSPrefix + 34)
// "Replace" on a DOCUMENT row's right-click menu (2026-09-27, the user's call): that document's ticked
// rows, no prompt, the list stays a work list (KBSReplaceEngine::ReplaceChapter). The book row greys it.
DECLARE_PMID(kActionIDSpace, kKBSChapterReplaceActionID, kKBSPrefix + 35)
// "Reject Change" on a DOCUMENT row's right-click menu (2026-09-27, the user's call): every replaced row of
// that document whose tracked change is still there (KBSReplaceEngine::RejectChapter).
DECLARE_PMID(kActionIDSpace, kKBSChapterRejectActionID, kKBSPrefix + 36)
// "Redo" on a DOCUMENT row's right-click menu (2026-09-27, the user's call): that document's rows taken
// back, replaced again with what Find/Change holds now (KBSReplaceEngine::RedoChapter). Named "Replace
// Again (Current Find/Change Settings)" since 2026-09-29, like the story row's.
DECLARE_PMID(kActionIDSpace, kKBSChapterRedoActionID, kKBSPrefix + 37)
// "Show Changes by KohakuFindChange" on the flyout (2026-09-29, the user's design): the list rebuilt from
// the Track Changes records KBS signed (KBSShowChanges). Under Change Checked, a rule between them.
DECLARE_PMID(kActionIDSpace, kKBSShowChangesActionID, kKBSPrefix + 38)
DECLARE_PMID(kActionIDSpace, kKBSSeparator6ActionID, kKBSPrefix + 39)
// Accept Change by KohakuFindChange on a hit row and on a story row (2026-09-29): the twin of Reject
// Change there - the row's (or the story's rows') tracked changes accepted (KBSReplaceEngine AcceptHit /
// AcceptStory).
DECLARE_PMID(kActionIDSpace, kKBSAcceptChangeActionID, kKBSPrefix + 40)
DECLARE_PMID(kActionIDSpace, kKBSStoryAcceptActionID, kKBSPrefix + 41)
// A RUN row's right-click menu (2026-09-29, a list rebuilt from the records only): Reject / Accept the
// rows of that run in that document (KBSReplaceEngine RejectRun / AcceptRun).
DECLARE_PMID(kActionIDSpace, kKBSRunRejectActionID, kKBSPrefix + 42)
DECLARE_PMID(kActionIDSpace, kKBSRunAcceptActionID, kKBSPrefix + 43)


// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKBSPanelWidgetID, kKBSPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKBSStaticTextWidgetID, kKBSPrefix + 1)
// Result tree (Task 2): the tree list widget; the chapter-row container + its label cell; the
// hit-row container + its multi-colour text cell.
DECLARE_PMID(kWidgetIDSpace, kKBSResultListWidgetID, kKBSPrefix + 2)
DECLARE_PMID(kWidgetIDSpace, kKBSResultChapterNodeWidgetID, kKBSPrefix + 3)
DECLARE_PMID(kWidgetIDSpace, kKBSResultChapterLabelWidgetID, kKBSPrefix + 4)
DECLARE_PMID(kWidgetIDSpace, kKBSResultHitNodeWidgetID, kKBSPrefix + 5)
DECLARE_PMID(kWidgetIDSpace, kKBSResultTextWidgetID, kKBSPrefix + 6)
// Replace feature: the hit row's check box (hit rows only).
DECLARE_PMID(kWidgetIDSpace, kKBSResultCheckWidgetID, kKBSPrefix + 7)
// RETIRED 2026-09-27 with the confirmation, like every kKBSReplaceConfirm* / kKBSGlyphConfirm* widget
// id below (commented out, numbers not reused). The Glyph tab's replace confirmation. The two glyph
// frames are told apart by their WidgetID -
// that is how KBSGlyphView knows which side it is drawing - so these two are not interchangeable.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmDialogWidgetID, kKBSPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmCountWidgetID, kKBSPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindGlyphWidgetID, kKBSPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeGlyphWidgetID, kKBSPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindFontWidgetID, kKBSPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeFontWidgetID, kKBSPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindUnicodeWidgetID, kKBSPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeUnicodeWidgetID, kKBSPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmUnsavedWidgetID, kKBSPrefix + 16)
// UNUSED since 2026-08-01 - the "Don't show again" box it named is no longer in the dialog. Like
// the string key beside it (kKBSGlyphConfirmDontShowKey), it is kept rather than freed: a widget id
// that once shipped stays spent, so a saved workspace referring to it cannot bind to something else.
// ! Until 2026-08-06 the dialog controller went on stamping a label into this id every time the
// confirmation opened. It was harmless - SetTextControlData looks the widget up first and there is
// nothing to find - but it made the id read as live. Removed; nothing writes it now.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmDontShowWidgetID, kKBSPrefix + 17)
// The panel's illustrations, stacked at ONE frame to the right of the status message - exactly one
// is visible and enabled at a time (KBSPanelIcon picks, and it is the ONLY place that knows which
// state each belongs to). Adding another is one id here, one resource below, one row in kIcons.
DECLARE_PMID(kWidgetIDSpace, kKBSIconWidgetID, kKBSPrefix + 18)		// nothing run yet
DECLARE_PMID(kWidgetIDSpace, kKBSIconFoundWidgetID, kKBSPrefix + 19)	// something has been run
DECLARE_PMID(kWidgetIDSpace, kKBSIconChangedWidgetID, kKBSPrefix + 20)	// ...and it was a replace
// The replace confirmation's two layouts. The dialog is ONE resource that shows one of them: Text
// and GREP put the whole prompt into a single wrapped block (the same sentences the plain alert
// used to draw), and Glyph shows the two glyph frames instead. The frames are hidden as a BLOCK
// rather than child by child, so EVE closes the gap they leave.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmMessageWidgetID, kKBSPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmGlyphBlockWidgetID, kKBSPrefix + 22)
// RETIRED 2026-08-05, NOT TO BE REUSED: the "save after replace" box and the line under it, which
// stood on the confirmation from 2026-08-02 until the feature was removed. A widget id that comes
// back on a DIFFERENT control is read by a saved workspace as the old one; the numbers cost nothing.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmSaveWidgetID, kKBSPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmSaveNoteWidgetID, kKBSPrefix + 24)
// "Take care when you are replacing across several chapters", under the line above. The GLYPH layout
// only: the Text / GREP one carries the same sentence inside its single wrapped block, assembled by
// KBSActionComponent, which is why there is no second widget for it there.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmCareWidgetID, kKBSPrefix + 25)
// "If the text has been edited since the search..." - the GLYPH layout's copy of that warning, live
// for one afternoon on 2026-08-08 and off both layouts by the end of it: the question can only be
// answered where the chapters are OPEN, so it moved to the replace itself (see the string keys
// below). Commented out rather than deleted, and its number not reused, for the reason given above
// the two before it.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmEditedWidgetID, kKBSPrefix + 26)
// The glyph dialog's three fixed labels ("Find", the arrow, "Change to"). They carry ids so the
// runtime language switch (KBSLoc) can restamp them Japanese - the jaJP string table that used
// to do it is gone (2026-08-05).
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindLabelWidgetID, kKBSPrefix + 27)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmArrowWidgetID, kKBSPrefix + 28)
//DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeLabelWidgetID, kKBSPrefix + 29)
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 30)


// "About Plug-ins" sub-menu:
#define kKBSAboutMenuKey			kKBSStringPrefix "kKBSAboutMenuKey"
#define kKBSAboutMenuPath		kSDKDefStandardAboutMenuPath kKBSCompanyKey

// A "Plug-ins" sub-menu path (kKBSPluginsMenuKey / kKBSPluginsMenuPath) stood here from the Dolly
// template until 2026-08-06. The .fr never used either one: the panel's own entry under that menu
// was kKBSPanelPluginsMenuPath (on the Window menu since 2026-09-29 - kKBSPanelWindowMenuName), and the flyout
// hangs off kKBSTargetMenuPath. The string value that went with the key is gone from KBS_enUS.fr
// too. Menu path macros are not ids and nothing outside this plug-in can name them, so there is
// nothing to reserve.

// Menu item keys:
#define kKBSSearchBookMenuKey			kKBSStringPrefix "kKBSSearchBookMenuKey"
// "Book Scope" toggle: ON = the whole book, OFF = the front document.
#define kKBSBookScopeMenuKey			kKBSStringPrefix "kKBSBookScopeMenuKey"
#define kKBSHidePrevChapterMenuKey		kKBSStringPrefix "kKBSHidePrevChapterMenuKey"
// "Translucent Panel" toggle: ON = the panel is drawn faint while it floats, and comes back to solid
// while the pointer is on it. English in both string tables, like the rest of the flyout.
#define kKBSTranslucentPanelMenuKey		kKBSStringPrefix "kKBSTranslucentPanelMenuKey"
// "Translucent Find/Change": the same, for InDesign's own Find/Change dialog.
#define kKBSTranslucentFindChangeMenuKey	kKBSStringPrefix "kKBSTranslucentFindChangeMenuKey"
// "Minimizable Find/Change": the same window again - a minimize box on InDesign's own dialog.
#define kKBSMinimizableFindChangeMenuKey	kKBSStringPrefix "kKBSMinimizableFindChangeMenuKey"
// "Remember Book Panel Placement": InDesign's own Book panel comes back where it was closed.
#define kKBSRememberBookPanelMenuKey	kKBSStringPrefix "kKBSRememberBookPanelMenuKey"
// "Save Panel Settings": write the settings above to a file of our own, read back at startup.
#define kKBSSavePanelSettingsMenuKey	kKBSStringPrefix "kKBSSavePanelSettingsMenuKey"
// Replace feature menu item keys.
#define kKBSReplaceCheckedMenuKey		kKBSStringPrefix "kKBSReplaceCheckedMenuKey"
#define kKBSCheckAllMenuKey				kKBSStringPrefix "kKBSCheckAllMenuKey"
#define kKBSUncheckAllMenuKey			kKBSStringPrefix "kKBSUncheckAllMenuKey"
// The hit row's own right-click menu (2026-09-26).
#define kKBSReplaceHitMenuKey			kKBSStringPrefix "kKBSReplaceHitMenuKey"
// The Reject items, one name per level since 2026-09-29 (the user's call: "by KohakuFindChange" in the
// name, so it says it acts on KBS's changes only). The hit row's keeps the key it always had.
#define kKBSRejectChangeMenuKey			kKBSStringPrefix "kKBSRejectChangeMenuKey"		// "Reject Change by KohakuFindChange"
#define kKBSStoryRejectMenuKey			kKBSStringPrefix "kKBSStoryRejectMenuKey"		// "...in This Story"
#define kKBSChapterRejectMenuKey		kKBSStringPrefix "kKBSChapterRejectMenuKey"		// "Reject All ...in This Document"
#define kKBSRunRejectMenuKey			kKBSStringPrefix "kKBSRunRejectMenuKey"			// "...in This Run"
// ...and their Accept twins (2026-09-29). The document row's is Accept All Changes, below.
#define kKBSAcceptChangeMenuKey			kKBSStringPrefix "kKBSAcceptChangeMenuKey"		// "Accept Change by KohakuFindChange"
#define kKBSStoryAcceptMenuKey			kKBSStringPrefix "kKBSStoryAcceptMenuKey"		// "...in This Story"
#define kKBSRunAcceptMenuKey			kKBSStringPrefix "kKBSRunAcceptMenuKey"			// "...in This Run"
#define kKBSAcceptAllChangesMenuKey		kKBSStringPrefix "kKBSAcceptAllChangesMenuKey"
// "Show Changes by KohakuFindChange" (2026-09-29).
#define kKBSShowChangesMenuKey			kKBSStringPrefix "kKBSShowChangesMenuKey"
#define kKBSRedoMenuKey					kKBSStringPrefix "kKBSRedoMenuKey"	// the story and document rows' Redo (2026-09-27) - "Replace Again (Current Find/Change Settings)" since 2026-09-29
// "How to Use...": the operating reference. English in both string tables, like the rest of the
// flyout - only the replace's own alerts are translated (see kKBSStaleResultsDocKey). The BODY of the
// reference is not here at all: it lives in KBSHowTo.cpp, because odfrc caps a single string at
// about 3.1KB and this text is several times that.
#define kKBSHowToMenuKey				kKBSStringPrefix "kKBSHowToMenuKey"

// Other StringKeys:
#define kKBSAboutBoxStringKey	kKBSStringPrefix "kKBSAboutBoxStringKey"
#define kKBSPanelTitleKey					kKBSStringPrefix	"kKBSPanelTitleKey"
// *THE WINDOW MENU'S NAME FOR THE PANEL, WITH A SUB-MENU IN FRONT OF IT (2026-09-29, the user's call:
//  "like KCM", which moved there on 2026-08-27). PanelList.fh on the panelName field: "Can also specify
//  a submenu here, as in "MyWindowSubmenu:MyPanelName"" - a colon buys a level, and the sub-menu part is
//  a string KEY, so kKBSCompanyKey reads "Kohaku Plug-Ins" as it did on the Plug-Ins side.
//  => Window > Kohaku Plug-Ins > Kohaku Find/Change. (KCMUIID.h, kKCMPanelWindowMenuName, is the same.)
#define kKBSPanelWindowMenuName			kKBSCompanyKey kSDKDefDelimitMenuPath kKBSPanelTitleKey
// (The panel sat on the Plug-Ins menu until then, through kKBSPanelPluginsMenuPath and
//  kKBSPanelPluginsMenuPosition; the PanelList's alternate path is empty now and they had no other
//  reader. !IF ONE IS EVER PUT BACK beside panelName, drop the title key from its end: a MenuDef path
//  names the menu that HOLDS the item, so with panelName filled in the last part becomes a sub-menu -
//  "Plug-Ins > Kohaku Plug-Ins > Kohaku Change Marker > Kohaku Change Marker", measured on KCM.)
#define kKBSStaticTextKey kKBSStringPrefix	"kKBSStaticTextKey"
#define kKBSInternalPopupMenuNameKey kKBSStringPrefix	"kKBSInternalPopupMenuNameKey"
// The Keyboard Shortcuts editor's area for the shortcut-assignable actions (2026-09-27, the user's call:
// "like KCM"). KCM's shape (kKCMPanelMenuActionArea): the ActionDef names this key, the string table
// resolves it to the value, and the actions appear under Product Area "Palette Menus" as
// "Kohaku Find/Change: <name>". "KBSCE " is the prefix a KBSC editor area key carries. A display label
// only - a shortcut is held against the ActionID, so renaming this does not detach an assignment.
#define kKBSPanelMenuActionArea			"KBSCE Palette Menus: Kohaku Find/Change: "
#define kKBSPanelMenuActionAreaValue	"Palette Menus:Kohaku Find/Change"
#define kKBSTargetMenuPath kKBSInternalPopupMenuNameKey

// The result rows' right-click context menu (2026-08-01, user request): the popup's internal name.
// KBSResultNodeEH::RButtonDn pops the MenuDef subtree of this name at the cursor with
// IMenuManager::HandlePopupMenu - the same machinery as the real Links / Layers panel row menus, and
// as KESCL's report rows (kKESCLReportRowMenuName). The root name is never displayed, so it is a
// plain literal rather than a translated key.
#define kKBSResultRowMenuName				"KBSRtMenuResultRow"
// ...and the HIT rows' menu (2026-09-26): Reject Change and Redo, about that one row. A subtree of its
// own because the two menus never share an item.
#define kKBSResultHitMenuName				"KBSRtMenuResultHit"
// A story row's own right-click menu (2026-09-27).
#define kKBSResultStoryMenuName				"KBSRtMenuResultStory"
// A run row's own right-click menu (2026-09-29, Show Changes by KohakuFindChange).
#define kKBSResultRunMenuName				"KBSRtMenuResultRun"

// (The Change Checked confirmation prompt's keys - kKBSConfirm* - stood here until 2026-09-27, when
//  the prompt was removed. The English lives in KBS_enUS.fr; the Japanese in KBSLoc.h.)
// ***** NOT PART OF THE CONFIRMATION PROMPT - the replace's own alert, shown INSTEAD of running.
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
// Translated, for the reason the confirmation prompt is: it is about the user's own text. The
// status line that reports the outcome stays English.
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
// (kKBSConfirmUnsavedKey, kKBSConfirmCareKey and the Glyph tab confirmation's labels -
//  kKBSGlyphConfirm*Key - went with the prompt on 2026-09-27.)

// RETIRED 2026-08-05 with the feature they belonged to: the "save after replace" box, its note, and
// the extra warning that went up when the box was ticked. They stood here from 2026-08-02. Removed
// from both string tables as well: these describe a run this plug-in no longer performs.
//#define kKBSSaveAfterReplaceKey		kKBSStringPrefix "kKBSSaveAfterReplaceKey"
//#define kKBSSaveAfterReplaceNoteKey	kKBSStringPrefix "kKBSSaveAfterReplaceNoteKey"
//#define kKBSSaveAfterReplaceWarningKey	kKBSStringPrefix "kKBSSaveAfterReplaceWarningKey"

// Menu item positions:
//
// The flyout reads in six blocks, parted by five rules (the user's arrangement, 2026-08-04; the two
// scans moved below Change Checked on 2026-09-27, the user's "B"):
//    1.0          Find in Document / Find in Book
//   ---- 1.1
//    1.2          Change Checked - the one command that writes to the DOCUMENTS
//   ---- 1.3
//    1.4 - 1.5    the two scans (Find Missing Glyphs, Find Overset)
//   ---- 2.0
//    2.2 - 2.9    the check-mark toggles (listed under "Block 3" below - counted there, not here)
//   ---- 3.0
//    4.0 - 5.0    the two commands that write a FILE of our own
//   ---- 10.0
//   10.5 - 11.0   the two reference items
// Positions that the new order allowed to stay were left where they were, so only the items that
// actually moved carry new numbers.

// Block 1 - the search, then Change Checked right under it (the rule between them was removed on
// 2026-09-27, the user's call), then a rule. The scope both run on is set by Book Scope.
#define kKBSSearchBookMenuItemPosition		1.0
#define kKBSReplaceCheckedMenuItemPosition	1.2
// (Block 2 - the two scans at 1.3 to 1.5 - was removed on 2026-09-27.)
// Show Changes by KohakuFindChange (2026-09-29, the user's place: under Change Checked, a rule between).
#define kKBSSeparator6MenuItemPosition		1.3
#define kKBSShowChangesMenuItemPosition		1.4
#define kKBSSeparator3MenuItemPosition		2.0

// Block 3 - the six check-mark toggles (the six positions from 2.2 to 2.9; it said "four" at the top of this
// list and "five" here until 2026-09-25). Book Scope leads: it is the one that decides what the
// commands in block 1 run on. Then Hide Previous Chapter. The next three are window appearance: the
// two that act on InDesign's OWN Find/Change dialog first (translucency, then the minimize box),
// and this panel's own translucency. Remember Book Panel Placement closes the block: it is about
// InDesign's own Book panel, the other window the plug-in looks after.
#define kKBSBookScopeMenuItemPosition		2.2
#define kKBSHidePrevChapterMenuItemPosition	2.4
#define kKBSTranslucentFindChangeMenuItemPosition	2.6
#define kKBSMinimizableFindChangeMenuItemPosition	2.7
#define kKBSTranslucentPanelMenuItemPosition	2.8
#define kKBSRememberBookPanelMenuItemPosition	2.9
#define kKBSSeparator4MenuItemPosition		3.0

// Block 4 - the command that writes a file of our own and touches no document: the toggles above
// (Save Panel Settings). Save Results... (5.0) was removed on 2026-09-27.
#define kKBSSavePanelSettingsMenuItemPosition	4.0
#define	kKBSSeparator1MenuItemPosition		10.0

// Block 5 - the reference items, the placement KESCM uses (its own is Sep2 9.95 / How to Use 10 /
// About 12). No rule between them: another one right above About would draw two dividers with a
// single item between them.
#define kKBSHowToMenuItemPosition			10.5
#define kKBSAboutThisMenuItemPosition		11.0

// Check All / Uncheck All are the two items of the RESULT ROWS' right-click menu (2026-08-01), not
// of the flyout, so their positions are that menu's own 1 and 2 - they were 5.0 and 6.0 while they
// sat under Change Checked on the flyout.
#define kKBSChapterReplaceMenuItemPosition	0.5		// a document row's only (2026-09-27; the book row greys it)
#define kKBSChapterRejectMenuItemPosition	0.6		// a document row's only (2026-09-27; the book row greys it)
#define kKBSChapterRedoMenuItemPosition		0.7		// a document row's only (2026-09-27; the book row greys it)
#define kKBSCheckAllMenuItemPosition		1.0
#define kKBSUncheckAllMenuItemPosition		2.0
#define kKBSAcceptAllChangesMenuItemPosition	3.0		// a document row's only (the book row greys it)
// The story row's menu (2026-09-27): the three commands, then the two check commands.
#define kKBSStoryReplaceMenuItemPosition	1.0
#define kKBSStoryRejectMenuItemPosition		2.0
#define kKBSStoryAcceptMenuItemPosition		2.5		// 2026-09-29: beside its Reject
#define kKBSStoryRedoMenuItemPosition		3.0
#define kKBSStoryCheckAllMenuItemPosition	4.0
#define kKBSStoryUncheckAllMenuItemPosition	5.0
// The hit row's menu: Replace first (2026-09-27), then its own 1 and 2.
#define kKBSReplaceHitMenuItemPosition		0.5
#define kKBSRejectChangeMenuItemPosition	1.0
#define kKBSAcceptChangeMenuItemPosition	2.0		// 2026-09-29
// The run row's menu (2026-09-29).
#define kKBSRunRejectMenuItemPosition		1.0
#define kKBSRunAcceptMenuItemPosition		2.0


// View (kViewRsrcType) resource IDs for the result tree's row widgets (Task 2). Offset from the
// panel's own resource ID (kSDKDefPanelResourceID), like the KESCL report panel's row resources.
#define kKBSResultChapterNodeWidgetRsrcID	(kSDKDefPanelResourceID + 20)
#define kKBSResultHitNodeWidgetRsrcID		(kSDKDefPanelResourceID + 21)

// ***** HOW TALL ONE ROW OF THE RESULT TREE IS. *****
//
// One number, read from BOTH SIDES: KBS.fr gives it to the two row resources above and to the
// tree's four scroll increments, and KBSResultListWidgetMgr answers GetNodeWidgetHeight with it.
// It was written out six times until 2026-08-09, and the panel now ROUNDS ITS OWN HEIGHT to a
// multiple of it (KBSPanelView::ConstrainDimensions), so a copy that drifted would leave the
// framework rounding to one number while the rows were drawn at another - a half row at the
// bottom, which is the very thing the rounding exists to prevent.
//
// A dimension constant that a .fr and its C++ both read is the shape Adobe uses:
// StdHeightWidthConstants.h (public/libs/widgetbin/includes) is nothing but this, and nine
// product .fr files #include it - LinksUIViews.fr and TimingPanelViews.fr among them.
//
// ! It is NOT kCC2016PanelTreeNodeHeight. That constant is 22 and this is 19: the number here was
//   measured on this panel's own rows, which carry a palette font at a size the stock tree node
//   was not laid out for. Adobe's constant is the right SHAPE to copy, not the right VALUE.
#define kKBSResultRowHeight		19

// PNG resource IDs. Their own number space (PNGA/PNGR), so they do not have to dodge the view
// resource ids above. 1001 is where KESCM and KESCL start theirs.
#define kKBSIconResID			1001	// the illustration shown before anything has been run
#define kKBSPaletteIconResID	1002	// the small dock-tab icon, shown when the panel is collapsed
#define kKBSIconFoundResID		1003	// the illustration shown once something HAS been run
#define kKBSIconChangedResID	1004	// ...and the one shown once a replace has written something

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
