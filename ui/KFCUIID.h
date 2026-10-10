//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChangeUI (the UI half of Kohaku Find/Change)
//
//  THE UI HALF'S IDS.
//  Everything the panel needs: its widgets, its menus and actions, the observers and services that
//  serve it, its string keys, positions and resources. Moved here from KFCID.h at kKFCUIPrefix + the
//  SAME offset and under the SAME name (docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md
//  section 2), so the C++ that names them did not change - only which header it includes.
//  What both halves must agree on - the company, the version, the model's PluginID, the boundary's
//  interfaces and message - is in KFCBoundaryID.h (model/), included below. The model half's own ids
//  are in model/KFCID.h, and this half does not include it.
//  The string keys keep the model's prefix (kKFCStringPrefix, "0x1EA600"): a key is unique by its value
//  across the application, and not one value had to change, so the tables only moved.
//
//========================================================================================

#ifndef __KFCUIID_h__
#define __KFCUIID_h__

#include "SDKDef.h"
#include "KFCBoundaryID.h"	// the company, the version, kKFCPluginID (this half depends on it), the boundary's ids

// Plug-in:
#define kKFCUIPluginName	"KohakuFindChangeUI"		// Internal name: the ID system and the .rc InternalName. Never change it.
#define kKFCUIFileName		"KohakuFindChangeUI"		// Base name of the build output: KohakuFindChangeUI.pln and its "(KohakuFindChangeUI Resources)" folder. MUST match the vcxproj TargetName.
#define kKFCUIDisplayName	"Kohaku Find/Change UI"		// The .rc FileDescription and the plug-in list. The panel, its tab and the About box keep kKFCDisplayName.

// Plug-in Prefix: the second half of KFC's band (0x1EA600 - 0x1EA6FF, Adobe's grant) - the
// model keeps + 0 ... 127, this half has + 128 ... 255. Adobe splits one band the same way
// (customdatalink 0xb3300 / customdatalinkui 0xb3380), and KCM does (0x1EA500 / 0x1EA580).
#define kKFCUIPrefixNumber	0x1EA680
#define kKFCUIPrefix		RezLong(kKFCUIPrefixNumber)

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKFCUIPluginID, kKFCUIPrefix + 0)

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKFCUIMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS
#define kKFCUIMissingPluginAlertValue	kSDKDefMissingPluginAlertValue


// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKFCActionComponentBoss, kKFCUIPrefix + 0)
DECLARE_PMID(kClassIDSpace, kKFCPanelWidgetBoss, kKFCUIPrefix + 1)
// Result tree: the tree-view list, its row (node) boss shared by chapter and hit rows,
// and the custom multi-colour text cell that highlights the matched part of a hit line.
DECLARE_PMID(kClassIDSpace, kKFCResultListWidgetBoss, kKFCUIPrefix + 2)
DECLARE_PMID(kClassIDSpace, kKFCResultNodeWidgetBoss, kKFCUIPrefix + 3)
DECLARE_PMID(kClassIDSpace, kKFCColorTextWidgetBoss, kKFCUIPrefix + 4)
// The jump marker's expiry idle task boss. (This half's startup/shutdown service boss is + 20 below.)
DECLARE_PMID(kClassIDSpace, kKFCMarkerExpiryIdleTaskBoss, kKFCUIPrefix + 6)
// (+ 8 was the hit row's check box - a stock check box with our observer aggregated on it.)
//DECLARE_PMID(kClassIDSpace, kKFCResultCheckWidgetBoss, kKFCUIPrefix + 8)	// retired (Change Checked - spec F16) - never reuse
// The panel's illustration: the system rollover icon button plus a tooltip of its own, so hovering
// it says where clicking it goes. Same shape as kLinksUIButtonBoss in open/components/linksui, and
// as KCM's kKCMIconWidgetBoss - which is where the panel this copies got it from.
DECLARE_PMID(kClassIDSpace, kKFCIconWidgetBoss, kKFCUIPrefix + 14)
// "Remember Book Panel Placement": the palette-manager service boss - registered for
// kPaletteMgrService, the service InDesign's own Book panel hangs off, so it is told when the
// palettes have been laid out and when they are about to close (KFCBookPanelPlacement.cpp).
DECLARE_PMID(kClassIDSpace, kKFCBookPanelServiceBoss, kKFCUIPrefix + 15)
// ...and its command interceptor: a book closing destroys its Book panel WITHOUT a word to the panel
// manager's subject (measured - neither kAboutToClosePaletteMsg nor a visibility message arrives), so
// the only moment the panel can still be measured is just BEFORE kCloseBookCmdBoss runs.
DECLARE_PMID(kClassIDSpace, kKFCBookPanelCmdWatchBoss, kKFCUIPrefix + 16)
// The panel's message area, drawn by hand (KFCStatusTextView.cpp): a generic panel with our
// IControlView and IKFCStatusTextData - the shape of the hit row's cell (+4) and of KCM's message area.
DECLARE_PMID(kClassIDSpace, kKFCStatusTextWidgetBoss, kKFCUIPrefix + 19)
// The UI half's own startup/shutdown service (KFCUIStartupShutdown.cpp): a startup/shutdown service is
// declared per boss, so the side the panel's work runs on needs a boss of its own. (+ 13 was the first
// choice; it is a retired id, so it stays spent.)
DECLARE_PMID(kClassIDSpace, kKFCUIStartupShutdownBoss, kKFCUIPrefix + 20)
// RETIRED (not reused): the action filter that took the "needs a document" bits off InDesign's own
// Edit > Find/Change, so the menu and Ctrl+F worked with no document open. The author's call: KFC
// leaves InDesign's menu alone - the panel's Open Find/Change... opens the dialog instead.
//DECLARE_PMID(kClassIDSpace, kKFCFindChangeAnywhereBoss, kKFCUIPrefix + 21)
// Run Saved Queries... (docs/superpowers/specs/_done/2026-10-07-kfc-query-dialog-and-selected-documents-design.md):
// the query dialog (a kDialogBoss with our controller and observer) and its two lists (a tree-view widget with our
// flat-list adapter and row maker - the saved queries on the left, the run order on the right). KFCQueryDialog.cpp,
// KFCQueryList.cpp.
DECLARE_PMID(kClassIDSpace, kKFCQueryDialogBoss, kKFCUIPrefix + 22)
DECLARE_PMID(kClassIDSpace, kKFCQueryListWidgetBoss, kKFCUIPrefix + 23)
// ...and the dialog's "Runs on:" line: a static text that EVE may not widen to fit its words (kFixedSizeEVEInfoImpl -
// KCM's kKCMBookPathTextWidgetBoss, which measured a path line growing the whole dialog to 593px).
DECLARE_PMID(kClassIDSpace, kKFCQueryFixedTextWidgetBoss, kKFCUIPrefix + 24)
// The result list's Return, taken before the widget layer gets it (KFCResultTreeEH.cpp): an event handler
// pushed on the application's IEventDispatcher while the list holds the keyboard.
DECLARE_PMID(kClassIDSpace, kKFCReturnFilterBoss, kKFCUIPrefix + 25)
// A row of the query dialog's two lists: the stock tree row with our event handler, so a saved query double-clicked
// goes into Find/Change (KFCQueryRowEH.cpp - the author's call).
DECLARE_PMID(kClassIDSpace, kKFCQueryRowWidgetBoss, kKFCUIPrefix + 26)


// InterfaceIDs:
// (The boundary's - the notification protocol, the session interfaces and the UI services - and the
//  MessageID are at kKFCPrefix + 3 ... + 7 in KFCBoundaryID.h. kKFCUIPrefix + 3 ... + 7 have never been
//  used.)
// Per-row draw data for a hit line's colour cell: the three text segments (before / matched /
// after) the cell paints, the match segment in a highlight colour.
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCROWDATA, kKFCUIPrefix + 0)
// The observer that re-applies the "Translucent Panel" alpha when the panel is opened, closed,
// docked or floated (kPaletteVisibilityChangedMessage). Its own IID because it is an AddIn onto
// kActiveContextBoss, which carries observers that are not ours. See KFCPanelAlpha.cpp.
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCPANELVISIBILITYOBSERVER, kKFCUIPrefix + 2)
// The observer behind "Remember Book Panel Placement": measures InDesign's Book panel as it closes and
// puts it back when it appears. Its own IID for the reason the one above has one - it is an AddIn onto
// kActiveContextBoss, next to that one. See KFCBookPanelPlacement.cpp.
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCBOOKPANELOBSERVER, kKFCUIPrefix + 8)
// What the panel's message area draws, split where its colour changes (IKFCStatusTextData.h).
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCSTATUSTEXTDATA, kKFCUIPrefix + 9)
// The observer that makes the application bar's search field show what Edit > Find/Change holds
// (KFCAppBarSearchEnter.cpp). Its own IID for the reason the two observers above have one: an AddIn onto
// kActiveContextBoss.
DECLARE_PMID(kInterfaceIDSpace, IID_IKFCAPPBARMIRROROBSERVER, kKFCUIPrefix + 10)


// ImplementationIDs:
DECLARE_PMID(kImplementationIDSpace, kKFCActionComponentImpl, kKFCUIPrefix + 0 )
// Result tree: hierarchy adapter, row widget manager, the colour cell's view and its per-row data
// holder.
DECLARE_PMID(kImplementationIDSpace, kKFCResultListAdapterImpl, kKFCUIPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKFCResultListWidgetMgrImpl, kKFCUIPrefix + 2)
DECLARE_PMID(kImplementationIDSpace, kKFCColorTextViewImpl, kKFCUIPrefix + 3)
DECLARE_PMID(kImplementationIDSpace, kKFCRowDataImpl, kKFCUIPrefix + 4)
// The jump marker's expiry idle task and the hit row's event handler (click -> jump).
DECLARE_PMID(kImplementationIDSpace, kKFCMarkerExpiryIdleTaskImpl, kKFCUIPrefix + 7)
DECLARE_PMID(kImplementationIDSpace, kKFCResultNodeEHImpl, kKFCUIPrefix + 8)
//DECLARE_PMID(kImplementationIDSpace, kKFCResultCheckObserverImpl, kKFCUIPrefix + 10)	// retired (Change Checked - spec F16) - never reuse
// The panel's observer (KFCPanelTitle.cpp): on the panel boss, it writes the tab's name, the layout,
// the picture and the message the moment the panel appears, hears the picture's click, and keeps the
// tab's name following the Find/Change settings and the selection (an ActiveSelectionObserver).
DECLARE_PMID(kImplementationIDSpace, kKFCPanelObserverImpl, kKFCUIPrefix + 13)
// The result tree's OWN event handler (the list, not a row): up / down arrows that OPEN the row
// they land on, so a book search's closed chapters do not hide their hits from the keyboard.
DECLARE_PMID(kImplementationIDSpace, kKFCResultTreeEHImpl, kKFCUIPrefix + 17)
// The panel illustration's tooltip: hovering the icon shows the URL clicking it opens, so the
// picture is not a mystery button (see KFCIconTip.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCIconTipImpl, kKFCUIPrefix + 19)
// The panel's own IControlView: stock palette behaviour plus a floor under how small the user can
// drag the panel (see KFCPanelView.cpp). (+ 18 is a retired id, deliberately not reused.)
DECLARE_PMID(kImplementationIDSpace, kKFCPanelViewImpl, kKFCUIPrefix + 20)
// "Translucent Panel" (ported from KCM): the observer that re-applies the alpha
// when the panel's window is rebuilt, and the roll-over that takes it off while the pointer is on
// the panel. Both in KFCPanelAlpha.cpp.
DECLARE_PMID(kImplementationIDSpace, kKFCPanelVisibilityObserverImpl, kKFCUIPrefix + 21)
DECLARE_PMID(kImplementationIDSpace, kKFCPanelRollOverImpl, kKFCUIPrefix + 22)
// + 23 ... + 28: THE MODEL/UI SPLIT'S BOUNDARY.
// + 23 = the UI half's observer of the model's notifications (KFCModelObserver.cpp);
// + 24 ... + 26 = the model half's three session interfaces (KFCModelServices.cpp);
// + 27 = the UI half's own startup/shutdown service (KFCUIStartupShutdown.cpp);
// + 28 = the UI services the model half asks for (KFCUIServices.cpp).
// (+ 24 ... + 26 are the model half's: model/KFCID.h, at kKFCPrefix.)
DECLARE_PMID(kImplementationIDSpace, kKFCModelObserverImpl, kKFCUIPrefix + 23)
DECLARE_PMID(kImplementationIDSpace, kKFCUIStartupShutdownImpl, kKFCUIPrefix + 27)
DECLARE_PMID(kImplementationIDSpace, kKFCUIServicesImpl, kKFCUIPrefix + 28)
// "Remember Book Panel Placement": the observer on kActiveContextBoss, the two halves of the
// palette-manager service boss (its provider and the IPaletteMgrService itself), and the command
// interceptor. All in KFCBookPanelPlacement.cpp. (+ 29 onwards: + 23 ... + 28 are the boundary's.)
DECLARE_PMID(kImplementationIDSpace, kKFCBookPanelObserverImpl, kKFCUIPrefix + 29)
DECLARE_PMID(kImplementationIDSpace, kKFCBookPanelServiceProviderImpl, kKFCUIPrefix + 30)
DECLARE_PMID(kImplementationIDSpace, kKFCBookPanelPaletteMgrServiceImpl, kKFCUIPrefix + 31)
DECLARE_PMID(kImplementationIDSpace, kKFCBookPanelCmdWatchImpl, kKFCUIPrefix + 32)
// The panel's message area (KFCStatusTextView.cpp): its view and its data.
DECLARE_PMID(kImplementationIDSpace, kKFCStatusTextViewImpl, kKFCUIPrefix + 36)
DECLARE_PMID(kImplementationIDSpace, kKFCStatusTextDataImpl, kKFCUIPrefix + 37)
// RETIRED (not reused): the action filter's implementation - see kKFCUIPrefix + 21 in the class ids.
//DECLARE_PMID(kImplementationIDSpace, kKFCFindChangeAnywhereImpl, kKFCUIPrefix + 39)
// The application bar's search field following Find/Change (KFCAppBarSearchEnter.cpp). (+ 40: + 39 is
// retired.)
DECLARE_PMID(kImplementationIDSpace, kKFCAppBarMirrorObserverImpl, kKFCUIPrefix + 40)
// The query dialog: its controller and observer (KFCQueryDialog.cpp), and its two lists' adapter and row
// maker (KFCQueryList.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCQueryDialogControllerImpl, kKFCUIPrefix + 41)
DECLARE_PMID(kImplementationIDSpace, kKFCQueryDialogObserverImpl, kKFCUIPrefix + 42)
DECLARE_PMID(kImplementationIDSpace, kKFCQueryListAdapterImpl, kKFCUIPrefix + 43)
DECLARE_PMID(kImplementationIDSpace, kKFCQueryListWidgetMgrImpl, kKFCUIPrefix + 44)
// The result list's Return filter: its event handler (KFCResultTreeEH.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCReturnFilterEHImpl, kKFCUIPrefix + 45)
// The query dialog's rows: their event handler - the double click on a saved query (KFCQueryRowEH.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCQueryRowEHImpl, kKFCUIPrefix + 46)
// The result list's selection rules - hit rows selected together (KFCResultTreeController.cpp, 1.4.0 O17).
DECLARE_PMID(kImplementationIDSpace, kKFCResultTreeControllerImpl, kKFCUIPrefix + 47)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKFCAboutActionID, kKFCUIPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKFCPanelWidgetActionID, kKFCUIPrefix + 1)
DECLARE_PMID(kActionIDSpace, kKFCSeparator1ActionID, kKFCUIPrefix + 2)
DECLARE_PMID(kActionIDSpace, kKFCPopupAboutThisActionID, kKFCUIPrefix + 3)
DECLARE_PMID(kActionIDSpace, kKFCSearchBookActionID, kKFCUIPrefix + 4)
DECLARE_PMID(kActionIDSpace, kKFCHidePrevChapterActionID, kKFCUIPrefix + 5)
// "Book Scope": search the whole book (ON) or what Find/Change's Search: names (OFF). Check-mark
// toggle, the KESCL "Search book" pattern (kKESCLPopupSearchBookActionID).
DECLARE_PMID(kActionIDSpace, kKFCScopeBookActionID, kKFCUIPrefix + 6)
// RETIRED (not reused): the separator between the search command and the toggles - Change Checked sat
// right under the search then (the author's call).
//DECLARE_PMID(kActionIDSpace, kKFCSeparator2ActionID, kKFCPrefix + 7)
// The rule under block 1 (the search, Change All in Book, Clear Results), above the toggles.
DECLARE_PMID(kActionIDSpace, kKFCSeparator3ActionID, kKFCUIPrefix + 8)
//DECLARE_PMID(kActionIDSpace, kKFCReplaceCheckedActionID, kKFCUIPrefix + 9)	// retired (Change Checked - spec F16) - never reuse
//DECLARE_PMID(kActionIDSpace, kKFCCheckAllActionID, kKFCUIPrefix + 10)	// retired (Change Checked - spec F16) - never reuse
//DECLARE_PMID(kActionIDSpace, kKFCUncheckAllActionID, kKFCUIPrefix + 11)	// retired (Change Checked - spec F16) - never reuse
// RETIRED (not reused - an old workspace referring to an ActionID must not bind to something else):
// + 12 "Undo All Replacements" - not needed: a book-wide write is ONE command sequence across every chapter,
//      so a single Ctrl+Z puts it back.
// + 13 a DoReplaceAll measurement probe.
// + 14 Find Missing Glyphs and + 16 Find Overset - the Book panel's preflight reports both.
// + 15 the same glyph scan through the official find/change engine with kAnyNotDefGlyphID. That route
//      takes InDesign down on any document holding overset text, by every route there is, so it must not
//      be reachable at all.
// + 17 "Save Results..." (the author's call).
//DECLARE_PMID(kActionIDSpace, kKFCActionID, kKFCPrefix + 12)
//DECLARE_PMID(kActionIDSpace, kKFCActionID, kKFCPrefix + 13)
//DECLARE_PMID(kActionIDSpace, kKFCFindMissingGlyphsActionID, kKFCPrefix + 14)
//DECLARE_PMID(kActionIDSpace, kKFCActionID, kKFCPrefix + 15)
//DECLARE_PMID(kActionIDSpace, kKFCFindOversetActionID, kKFCPrefix + 16)
//DECLARE_PMID(kActionIDSpace, kKFCSaveResultsActionID, kKFCPrefix + 17)
// "How to Use..." on the flyout: the plug-in's operating reference, shown in a scrollable dialog
// (KFCHowTo.cpp). Deliberately NOT greyed out by anything - it is the one item that has to stay
// readable when nothing is loaded and while a run is going, which is when it is most wanted.
DECLARE_PMID(kActionIDSpace, kKFCHowToActionID, kKFCUIPrefix + 18)
// "Translucent Panel" on the flyout: a check-mark toggle (ON = this panel is drawn translucent while
// it floats). *Windows only. *Selectable while docked, where it has no visible effect - the flag is
// set and applies the moment the panel floats again. OFF by default; it survives a restart only
// through Save Panel Settings (KFCPanelState.h). See KFCPanelAlpha.cpp.
DECLARE_PMID(kActionIDSpace, kKFCTranslucentPanelActionID, kKFCUIPrefix + 19)
// "Save Panel Settings": write the flyout's SETTINGS toggles to a JSON file of our own in the user's
// preferences folder, read back at startup (KFCPanelState.cpp). A plain command, not a toggle - and
// an explicit one, the way KCM has it: settings are saved when asked for, never behind the user's
// back. What the file holds is listed in ONE place, KFCPanelState.h - do not count it here.
// *Book Scope is deliberately not among them; the reason is there too.
DECLARE_PMID(kActionIDSpace, kKFCSavePanelSettingsActionID, kKFCUIPrefix + 20)
// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog. Check-mark
// toggle, Windows only, OFF by default. The dialog is found through the SDK's window list, not by
// its title, so it works whatever language InDesign is running in (KFCPanelAlpha.cpp).
DECLARE_PMID(kActionIDSpace, kKFCTranslucentFindChangeActionID, kKFCUIPrefix + 21)
// The rule below the toggle block. MenuDef only, no ActionDef - like the other separators.
DECLARE_PMID(kActionIDSpace, kKFCSeparator4ActionID, kKFCUIPrefix + 22)
// "Minimizable Find/Change": put a MINIMIZE BOX on InDesign's own Find/Change dialog, so it can be
// sent to the taskbar instead of being closed. Check-mark toggle, Windows only, OFF by default.
// The same window as the toggle above, reached the same way; what differs is that this one changes
// the window's STYLE rather than its alpha. See KFCFindChangeMinimize.cpp.
DECLARE_PMID(kActionIDSpace, kKFCMinimizableFindChangeActionID, kKFCUIPrefix + 23)
// "Remember Book Panel Placement" on the flyout: a check-mark toggle. ON = InDesign's own Book panel is
// measured as it closes (and when InDesign quits) and put back where it was when it next appears. OFF by
// default. *Unlike the toggles above, flipping it WRITES ITS OWN KEY to the settings file at once (the
// author's rule) - see KFCBookPanelPlacement.h.
DECLARE_PMID(kActionIDSpace, kKFCRememberBookPanelActionID, kKFCUIPrefix + 24)
// RETIRED (never reuse - a keyboard shortcut is recorded against the ActionID): the items that took
// back or accepted KFC's tracked changes, Show Changes by KohakuFindChange and Replace Again, gone with Track
// Changes (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F2 - KCM handles the records):
// + 25 Reject Change (hit row), + 27 Accept All Changes by KohakuFindChange in This Document, + 31 / + 32 the
// story row's Reject and Replace Again, + 36 / + 37 the document row's, + 38 Show Changes, + 39 its rule,
// + 40 / + 41 Accept Change (hit row, story row), + 42 / + 43 the run row's Reject and Accept.
//DECLARE_PMID(kActionIDSpace, kKFCRejectChangeActionID, kKFCUIPrefix + 25)
// (+ 26 was "Redo" on the same menu - a row taken back is replaced again with Replace. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKFCAcceptAllChangesActionID, kKFCUIPrefix + 27)
// RETIRED (not reused): the rule between Change Checked and the two scans, gone with them.
//DECLARE_PMID(kActionIDSpace, kKFCSeparator5ActionID, kKFCPrefix + 28)
// "Replace" on a hit row's right-click menu (the author's call): replaces that one row, with no prompt;
// the list stays a work list (KFCReplaceEngine::ReplaceHit).
DECLARE_PMID(kActionIDSpace, kKFCReplaceHitActionID, kKFCUIPrefix + 29)
// A STORY row's right-click menu was Replace (its ticked rows), Check All, Uncheck All.
//DECLARE_PMID(kActionIDSpace, kKFCStoryReplaceActionID, kKFCUIPrefix + 30)	// retired (Change Checked - spec F16) - never reuse
//DECLARE_PMID(kActionIDSpace, kKFCStoryRejectActionID, kKFCUIPrefix + 31)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCStoryRedoActionID, kKFCUIPrefix + 32)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCStoryCheckAllActionID, kKFCUIPrefix + 33)	// retired (Change Checked - spec F16) - never reuse
//DECLARE_PMID(kActionIDSpace, kKFCStoryUncheckAllActionID, kKFCUIPrefix + 34)	// retired (Change Checked - spec F16) - never reuse
// "Replace" on a DOCUMENT row's right-click menu was that document's ticked rows.
//DECLARE_PMID(kActionIDSpace, kKFCChapterReplaceActionID, kKFCUIPrefix + 35)	// retired (Change Checked - spec F16) - never reuse
//DECLARE_PMID(kActionIDSpace, kKFCChapterRejectActionID, kKFCUIPrefix + 36)	// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCChapterRedoActionID, kKFCUIPrefix + 37)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCShowChangesActionID, kKFCUIPrefix + 38)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCSeparator6ActionID, kKFCUIPrefix + 39)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCAcceptChangeActionID, kKFCUIPrefix + 40)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCStoryAcceptActionID, kKFCUIPrefix + 41)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCRunRejectActionID, kKFCUIPrefix + 42)		// retired (above)
//DECLARE_PMID(kActionIDSpace, kKFCRunAcceptActionID, kKFCUIPrefix + 43)		// retired (above)
// "Open Find/Change..." on the flyout (the author's call): InDesign's own Edit > Find/Change dialog,
// opened from the panel - with no document open too. Shortcut-assignable. (+ 44 was never spent at
// kKFCPrefix either.)
DECLARE_PMID(kActionIDSpace, kKFCOpenFindChangeActionID, kKFCUIPrefix + 44)
// "Link the Application Bar's Search Field to This Panel" on the flyout: a check-mark toggle. ON = the
// search field of InDesign's application bar shows Find/Change's query, and Return in it searches with
// this panel. OFF by default. See KFCAppBarSearchEnter.h.
DECLARE_PMID(kActionIDSpace, kKFCAppBarSearchEnterActionID, kKFCUIPrefix + 45)
// Change All in Book (No List) and Clear Results (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md
// F6 and F18): InDesign's own Change All over the book's chapters, no list; the list emptied so it can run.
DECLARE_PMID(kActionIDSpace, kKFCChangeAllActionID, kKFCUIPrefix + 46)
DECLARE_PMID(kActionIDSpace, kKFCClearResultsActionID, kKFCUIPrefix + 47)
// Run Saved Queries... (the query dialog's spec, G1): the query dialog - the saved Find/Change queries put in an order and
// run with InDesign's own Change All, one after another (KFCQueryDialog.cpp). Shortcut-assignable.
DECLARE_PMID(kActionIDSpace, kKFCRunSavedQueriesActionID, kKFCUIPrefix + 48)
// Find/Change Selected Documents (Book) (the query dialog's spec, G9): a toggle - with Book Scope on, a run takes only
// the documents selected in the Book panel (KFCBookScope::IsSelectedDocumentsOn). Grey with Book Scope off.
DECLARE_PMID(kActionIDSpace, kKFCSelectedDocumentsActionID, kKFCUIPrefix + 49)
// "Search This Story Again" on a STORY row's right-click menu (the author's call of 2026-10-09): that story alone walked
// again with the search's query, its rows put back and its version recorded, so a story edited since the search can be
// replaced in again (KFCSearchEngine::SearchStoryAgain). + 50: the next after the highest spent (+ 30 ... + 34, the
// story row's earlier items, are retired and never reused).
DECLARE_PMID(kActionIDSpace, kKFCSearchStoryAgainActionID, kKFCUIPrefix + 50)


// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKFCPanelWidgetID, kKFCUIPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKFCStaticTextWidgetID, kKFCUIPrefix + 1)
// Result tree: the tree list widget; the chapter-row container + its label cell; the hit-row
// container + its multi-colour text cell.
DECLARE_PMID(kWidgetIDSpace, kKFCResultListWidgetID, kKFCUIPrefix + 2)
DECLARE_PMID(kWidgetIDSpace, kKFCResultChapterNodeWidgetID, kKFCUIPrefix + 3)
DECLARE_PMID(kWidgetIDSpace, kKFCResultChapterLabelWidgetID, kKFCUIPrefix + 4)
DECLARE_PMID(kWidgetIDSpace, kKFCResultHitNodeWidgetID, kKFCUIPrefix + 5)
DECLARE_PMID(kWidgetIDSpace, kKFCResultTextWidgetID, kKFCUIPrefix + 6)
//DECLARE_PMID(kWidgetIDSpace, kKFCResultCheckWidgetID, kKFCUIPrefix + 7)	// retired (Change Checked - spec F16) - never reuse
// RETIRED (not reused), like every commented-out widget id below: the Glyph tab's replace confirmation
// dialog and its parts. A widget id that once shipped stays spent - a saved workspace reads a widget id
// that comes back on a DIFFERENT control as the old one; the numbers cost nothing.
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmDialogWidgetID, kKFCPrefix + 8)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmCountWidgetID, kKFCPrefix + 9)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmFindGlyphWidgetID, kKFCPrefix + 10)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmChangeGlyphWidgetID, kKFCPrefix + 11)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmFindFontWidgetID, kKFCPrefix + 12)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmChangeFontWidgetID, kKFCPrefix + 13)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmFindUnicodeWidgetID, kKFCPrefix + 14)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmChangeUnicodeWidgetID, kKFCPrefix + 15)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmUnsavedWidgetID, kKFCPrefix + 16)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmDontShowWidgetID, kKFCPrefix + 17)
// The panel's illustrations, stacked at ONE frame to the right of the status message - exactly one
// is visible and enabled at a time (KFCPanelIcon picks, and it is the ONLY place that knows which
// state each belongs to). Adding another is one id here, one resource below, one row in kIcons.
DECLARE_PMID(kWidgetIDSpace, kKFCIconWidgetID, kKFCUIPrefix + 18)		// nothing run yet
DECLARE_PMID(kWidgetIDSpace, kKFCIconFoundWidgetID, kKFCUIPrefix + 19)	// something has been run
DECLARE_PMID(kWidgetIDSpace, kKFCIconChangedWidgetID, kKFCUIPrefix + 20)	// ...and it was a replace
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmMessageWidgetID, kKFCPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmGlyphBlockWidgetID, kKFCPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmSaveWidgetID, kKFCPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmSaveNoteWidgetID, kKFCPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmCareWidgetID, kKFCPrefix + 25)
//DECLARE_PMID(kWidgetIDSpace, kKFCReplaceConfirmEditedWidgetID, kKFCPrefix + 26)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmFindLabelWidgetID, kKFCPrefix + 27)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmArrowWidgetID, kKFCPrefix + 28)
//DECLARE_PMID(kWidgetIDSpace, kKFCGlyphConfirmChangeLabelWidgetID, kKFCPrefix + 29)
//DECLARE_PMID(kWidgetIDSpace, kKFCWidgetID, kKFCPrefix + 30)
// The query dialog: the dialog, its two lists, the row both lists are made of and the row's text, the four
// buttons between the lists, and the line under them that says what Run would run on. (+ 31 onwards: none of these
// numbers was ever used at either prefix.) Run and Close are the stock kOKButtonWidgetID / kCancelButton_WidgetID - the
// dialog framework's own OK and Cancel, so Enter and Escape reach them.
DECLARE_PMID(kWidgetIDSpace, kKFCQueryDialogWidgetID, kKFCUIPrefix + 31)
DECLARE_PMID(kWidgetIDSpace, kKFCQuerySavedListWidgetID, kKFCUIPrefix + 32)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryOrderListWidgetID, kKFCUIPrefix + 33)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryRowWidgetID, kKFCUIPrefix + 34)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryRowTextWidgetID, kKFCUIPrefix + 35)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryAddButtonWidgetID, kKFCUIPrefix + 36)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryRemoveButtonWidgetID, kKFCUIPrefix + 37)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryUpButtonWidgetID, kKFCUIPrefix + 38)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryDownButtonWidgetID, kKFCUIPrefix + 39)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryScopeTextWidgetID, kKFCUIPrefix + 40)
// ...and Clear under the four (the author's addition): the run order emptied.
DECLARE_PMID(kWidgetIDSpace, kKFCQueryClearButtonWidgetID, kKFCUIPrefix + 41)
// ...and the dialog's own message line under Runs on: (the author's call, of three offered): what the last Run (or
// the run order's file) said, the panel's message line in the dialog too - the dialog stands with the panel closed.
DECLARE_PMID(kWidgetIDSpace, kKFCQueryMessageTextWidgetID, kKFCUIPrefix + 42)
// ...and Save Order... / Load Order... under the run order (the author's call: the order is the session's,
// and kept as a file of its own - KFCQueryOrderFile.h).
DECLARE_PMID(kWidgetIDSpace, kKFCQuerySaveOrderButtonWidgetID, kKFCUIPrefix + 43)
DECLARE_PMID(kWidgetIDSpace, kKFCQueryLoadOrderButtonWidgetID, kKFCUIPrefix + 44)


// "About Plug-ins" sub-menu:
#define kKFCAboutMenuKey			kKFCStringPrefix "kKFCAboutMenuKey"
#define kKFCAboutMenuPath		kSDKDefStandardAboutMenuPath kKFCCompanyKey
// The flyout's last item, "About This Plug-In..." - KFC's OWN key, not the SDK's kSDKDefAboutThisPlugInMenuKey: that key
// is one string ("About this pl&ug-in...") every SDK-based plug-in shares, and a second value under it would leave
// which one InDesign shows to the load order. Title case, as the rest of the flyout (the author's call: "the correct
// English" - InDesign's own menu reads "Plug-Ins", and this panel's menu group "Kohaku Plug-Ins").
#define kKFCAboutThisPlugInMenuKey	kKFCStringPrefix "kKFCAboutThisPlugInMenuKey"

// Menu item keys:
#define kKFCSearchBookMenuKey			kKFCStringPrefix "kKFCSearchBookMenuKey"
// "Book Scope" toggle: ON = the whole book, OFF = what Edit > Find/Change's Search: names.
#define kKFCBookScopeMenuKey			kKFCStringPrefix "kKFCBookScopeMenuKey"
#define kKFCHidePrevChapterMenuKey		kKFCStringPrefix "kKFCHidePrevChapterMenuKey"
// "Translucent Panel" toggle: ON = the panel is drawn faint while it floats, and comes back to solid
// while the pointer is on it. English in every UI language, like the rest of the flyout.
#define kKFCTranslucentPanelMenuKey		kKFCStringPrefix "kKFCTranslucentPanelMenuKey"
// "Translucent Find/Change": the same, for InDesign's own Find/Change dialog.
#define kKFCTranslucentFindChangeMenuKey	kKFCStringPrefix "kKFCTranslucentFindChangeMenuKey"
// "Minimizable Find/Change": the same window again - a minimize box on InDesign's own dialog.
#define kKFCMinimizableFindChangeMenuKey	kKFCStringPrefix "kKFCMinimizableFindChangeMenuKey"
// "Remember Book Panel Placement": InDesign's own Book panel comes back where it was closed.
#define kKFCRememberBookPanelMenuKey	kKFCStringPrefix "kKFCRememberBookPanelMenuKey"
// "Link the Application Bar's Search Field to This Panel": Return in the application bar's search field searches here.
#define kKFCAppBarSearchEnterMenuKey	kKFCStringPrefix "kKFCAppBarSearchEnterMenuKey"
// "Save Panel Settings": write the settings above to a file of our own, read back at startup.
#define kKFCSavePanelSettingsMenuKey	kKFCStringPrefix "kKFCSavePanelSettingsMenuKey"
// Change All in Book (No List) and Clear Results.
#define kKFCChangeAllMenuKey			kKFCStringPrefix "kKFCChangeAllMenuKey"
#define kKFCClearResultsMenuKey			kKFCStringPrefix "kKFCClearResultsMenuKey"
// The hit row's own right-click menu.
#define kKFCReplaceHitMenuKey			kKFCStringPrefix "kKFCReplaceHitMenuKey"
// The story row's own right-click menu.
#define kKFCSearchStoryAgainMenuKey		kKFCStringPrefix "kKFCSearchStoryAgainMenuKey"
// "How to Use...": the operating reference. English in every UI language, like the rest of the
// flyout - there is one string table, and what KFCLoc.h switches to Japanese at run time is the model
// half's Undo names (kKFCReplaceStepKey and friends, KFCID.h) and this page's body.
// The BODY of the reference is not here at all: it lives in KFCHowTo.cpp, because odfrc caps a single
// string at about 3.1KB and this text is several times that.
#define kKFCHowToMenuKey				kKFCStringPrefix "kKFCHowToMenuKey"
// "Open Find/Change...".
#define kKFCOpenFindChangeMenuKey		kKFCStringPrefix "kKFCOpenFindChangeMenuKey"
// "Run Saved Queries..." - and the query dialog's own words: its title, the two lists' headings, the four
// buttons between them, Run and Close. English in every UI language, like the rest of the flyout.
#define kKFCRunSavedQueriesMenuKey		kKFCStringPrefix "kKFCRunSavedQueriesMenuKey"
#define kKFCQueryDialogTitleKey			kKFCStringPrefix "kKFCQueryDialogTitleKey"
#define kKFCQuerySavedLabelKey			kKFCStringPrefix "kKFCQuerySavedLabelKey"
#define kKFCQueryOrderLabelKey			kKFCStringPrefix "kKFCQueryOrderLabelKey"
#define kKFCQueryAddKey					kKFCStringPrefix "kKFCQueryAddKey"
#define kKFCQueryRemoveKey				kKFCStringPrefix "kKFCQueryRemoveKey"
#define kKFCQueryUpKey					kKFCStringPrefix "kKFCQueryUpKey"
#define kKFCQueryDownKey				kKFCStringPrefix "kKFCQueryDownKey"
#define kKFCQueryClearKey				kKFCStringPrefix "kKFCQueryClearKey"
#define kKFCQueryRunKey					kKFCStringPrefix "kKFCQueryRunKey"
#define kKFCQueryCloseKey				kKFCStringPrefix "kKFCQueryCloseKey"
// Save Order... / Load Order..., the titles of InDesign's Save / Open dialogs they bring up, and the
// file type's name in those dialogs (KFCQueryOrderFile.cpp).
#define kKFCQuerySaveOrderKey			kKFCStringPrefix "kKFCQuerySaveOrderKey"
#define kKFCQueryLoadOrderKey			kKFCStringPrefix "kKFCQueryLoadOrderKey"
#define kKFCQuerySaveOrderTitleKey		kKFCStringPrefix "kKFCQuerySaveOrderTitleKey"
#define kKFCQueryLoadOrderTitleKey		kKFCStringPrefix "kKFCQueryLoadOrderTitleKey"
#define kKFCQueryOrderFileTypeKey		kKFCStringPrefix "kKFCQueryOrderFileTypeKey"
// "Find/Change Selected Documents (Book)" - the toggle under Book Scope.
#define kKFCSelectedDocumentsMenuKey	kKFCStringPrefix "kKFCSelectedDocumentsMenuKey"

// Other StringKeys:
#define kKFCAboutBoxStringKey	kKFCStringPrefix "kKFCAboutBoxStringKey"
#define kKFCPanelTitleKey					kKFCStringPrefix	"kKFCPanelTitleKey"
// THE WINDOW MENU'S NAME FOR THE PANEL, WITH A SUB-MENU IN FRONT OF IT (the author's call: "like KCM").
//  PanelList.fh on the panelName field: "Can also specify a submenu here, as in
//  "MyWindowSubmenu:MyPanelName"" - a colon buys a level, and the sub-menu part is a string KEY, so
//  kKFCCompanyKey reads "Kohaku Plug-Ins".
//  => Window > Kohaku Plug-Ins > Kohaku Find/Change. (KCMUIID.h, kKCMPanelWindowMenuName, is the same.)
#define kKFCPanelWindowMenuName			kKFCCompanyKey kSDKDefDelimitMenuPath kKFCPanelTitleKey
// (The PanelList's alternate menu path is empty. !IF ONE IS EVER PUT BACK beside panelName, drop the
//  title key from its end: a MenuDef path names the menu that HOLDS the item, so with panelName filled
//  in the last part becomes a sub-menu - "Plug-Ins > Kohaku Plug-Ins > Kohaku Change Marker > Kohaku
//  Change Marker", measured on KCM.)
#define kKFCStaticTextKey kKFCStringPrefix	"kKFCStaticTextKey"
#define kKFCInternalPopupMenuNameKey kKFCStringPrefix	"kKFCInternalPopupMenuNameKey"
// The Keyboard Shortcuts editor's area for the shortcut-assignable actions (the author's call:
// "like KCM"). KCM's shape (kKCMPanelMenuActionArea): the ActionDef names this key, the string table
// resolves it to the value, and the actions appear under Product Area "Palette Menus" as
// "Kohaku Find/Change: <name>". "KBSCE " is the prefix a KBSC editor area key carries. A display label
// only - a shortcut is held against the ActionID, so renaming this does not detach an assignment.
#define kKFCPanelMenuActionArea			"KBSCE Palette Menus: Kohaku Find/Change: "
#define kKFCPanelMenuActionAreaValue	"Palette Menus:Kohaku Find/Change"
#define kKFCTargetMenuPath kKFCInternalPopupMenuNameKey

// The HIT rows' right-click menu (Replace, about that one row): the popup's internal name.
// KFCResultNodeEH::RButtonDn pops the MenuDef subtree of this name at the cursor with
// IMenuManager::HandlePopupMenu - the same machinery as the real Links / Layers panel row menus, and
// as KESCL's report rows (kKESCLReportRowMenuName). The root name is never displayed, so it is a
// plain literal rather than a translated key. (The book / document rows' "KFCRtMenuResultRow" went with Change
// Checked - spec F16 - and so did the story rows' first menu, Replace / Check All / Uncheck All.)
#define kKFCResultHitMenuName				"KFCRtMenuResultHit"
// The STORY rows' right-click menu (Search This Story Again - the author's call of 2026-10-09), popped the same way.
#define kKFCResultStoryMenuName				"KFCRtMenuResultStory"

// Menu item positions:
//
// The flyout, as KFCUI.fr lays it out (the author's arrangement):
//    0.5 - 1.3    Open Find/Change..., Find in <scope>, Change All in Book (No List) and Run Saved Queries... (the
//                 commands that write to the DOCUMENTS here), Clear Results between them
//   ---- 2.0
//    2.2 - 2.9    the check-mark toggles (Block 3 below)
//   ---- 3.0
//    4.0          Save Panel Settings - writes a FILE of our own
//   ---- 10.0
//   10.5 - 11.0   How to Use..., About
// Positions that a new order allowed to stay were left where they were, so only the items that
// actually moved carry new numbers.

// Block 1 - the search, then Change All in Book (No List) and Clear Results with no rule between (the
// author's call), then a rule. The scope the search runs on is set by Book Scope (Change All runs on the book
// alone - F18).
// Open Find/Change... leads the block: open the dialog, type the query, then search.
#define kKFCOpenFindChangeMenuItemPosition	0.5
#define kKFCSearchBookMenuItemPosition		1.0
// Change All in Book (No List) under Find, then Clear Results (the spec's section 4).
#define kKFCChangeAllMenuItemPosition		1.1
#define kKFCClearResultsMenuItemPosition	1.25
// Run Saved Queries... last in the block, under Clear Results (the author's call): the other command that writes with
// InDesign's Change All and leaves no list.
#define kKFCRunSavedQueriesMenuItemPosition	1.3
// (Block 2 - the two scans - is gone.)
#define kKFCSeparator3MenuItemPosition		2.0

// Block 3 - the check-mark toggles, 2.2 to 2.9. Book Scope leads: it is the one that decides what the
// commands in block 1 run on, and Find/Change Selected Documents (Book) follows it - it narrows Book Scope, and works
// only with it on. Then Hide Previous Chapter; then Link the Application Bar's Search Field,
// which is about the search as Book Scope is, so it stands before the window ones. The
// next three are window appearance: the two that act on InDesign's OWN Find/Change dialog first
// (translucency, then the minimize box), and this panel's own translucency. Remember Book Panel
// Placement closes the block: it is about InDesign's own Book panel, the other window the plug-in looks
// after.
#define kKFCBookScopeMenuItemPosition		2.2
#define kKFCSelectedDocumentsMenuItemPosition	2.3
#define kKFCHidePrevChapterMenuItemPosition	2.4
#define kKFCAppBarSearchEnterMenuItemPosition	2.5		// a search toggle, before the window toggles
#define kKFCTranslucentFindChangeMenuItemPosition	2.6
#define kKFCMinimizableFindChangeMenuItemPosition	2.7
#define kKFCTranslucentPanelMenuItemPosition	2.8
#define kKFCRememberBookPanelMenuItemPosition	2.9
#define kKFCSeparator4MenuItemPosition		3.0

// Block 4 - the command that writes a file of our own and touches no document: the toggles above
// (Save Panel Settings).
#define kKFCSavePanelSettingsMenuItemPosition	4.0
#define	kKFCSeparator1MenuItemPosition		10.0

// Block 5 - the reference items, last. No rule between them: another one right above About would
// draw two dividers with a single item between them.
#define kKFCHowToMenuItemPosition			10.5
#define kKFCAboutThisMenuItemPosition		11.0

// The hit row's menu: Replace.
#define kKFCReplaceHitMenuItemPosition		0.5
// The story row's menu: Search This Story Again.
#define kKFCSearchStoryAgainMenuItemPosition	0.5


// View (kViewRsrcType) resource IDs for the result tree's row widgets. Offset from the panel's own
// resource ID (kSDKDefPanelResourceID), like the KESCL report panel's row resources.
#define kKFCResultChapterNodeWidgetRsrcID	(kSDKDefPanelResourceID + 20)
#define kKFCResultHitNodeWidgetRsrcID		(kSDKDefPanelResourceID + 21)
// The query dialog: its view - the SDK's place for a plug-in's dialog, which KFC had not used - and the
// row its two lists are made of (next to the result tree's rows).
#define kKFCQueryDialogRsrcID				kSDKDefDialogResourceID
#define kKFCQueryRowRsrcID					(kSDKDefPanelResourceID + 22)
// One row of the query dialog's lists: KCM's book comparison dialog's row height (kKCMBookRowHeight), the one other
// list the Kohaku plug-ins draw in a dialog's font. Read by the row resource, the lists' scroll increments
// and KFCQueryListWidgetMgr::GetNodeWidgetHeight - one number.
#define kKFCQueryRowHeight					22

// HOW TALL ONE ROW OF THE RESULT TREE IS.
//
// One number, read from BOTH SIDES: KFCUI.fr gives it to the two row resources above and to the
// tree's four scroll increments, and KFCResultListWidgetMgr answers GetNodeWidgetHeight with it.
// It must stay ONE number: the panel ROUNDS ITS OWN HEIGHT to a multiple of it
// (KFCPanelView::ConstrainDimensions), so a copy that drifted would leave the framework rounding to
// one number while the rows were drawn at another - a half row at the bottom, which is the very
// thing the rounding exists to prevent.
//
// A dimension constant that a .fr and its C++ both read is the shape Adobe uses:
// StdHeightWidthConstants.h (public/libs/widgetbin/includes) is nothing but this, and nine
// product .fr files #include it - LinksUIViews.fr and TimingPanelViews.fr among them.
//
// ! It is NOT kCC2016PanelTreeNodeHeight. That constant is 22 and this is 19: the number here was
//   measured on this panel's own rows, which carry a palette font at a size the stock tree node
//   was not laid out for. Adobe's constant is the right SHAPE to copy, not the right VALUE.
#define kKFCResultRowHeight		19

// PNG resource IDs. Their own number space (PNGA/PNGR), so they do not have to dodge the view
// resource ids above. 1001 is where KCM and KESCL start theirs.
#define kKFCIconResID			1001	// the illustration shown before anything has been run
#define kKFCPaletteIconResID	1002	// the small dock-tab icon, shown when the panel is collapsed
#define kKFCIconFoundResID		1003	// the illustration shown once something HAS been run
#define kKFCIconChangedResID	1004	// ...and the one shown once a replace has written something

// Initial data format version numbers
#define kKFCUIFirstMajorFormatNumber  RezLong(1)
#define kKFCUIFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKFCUICurrentMajorFormatNumber kKFCUIFirstMajorFormatNumber
#define kKFCUICurrentMinorFormatNumber kKFCUIFirstMinorFormatNumber

#endif // __KFCUIID_h__
