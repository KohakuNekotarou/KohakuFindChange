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
// manager's subject while it still stands (measured - no kAboutToClosePaletteMsg, and a visibility
// message at most once the panel is out of its palette: KFCBookPanelPlacement.cpp's header), so the
// only moment the panel can still be measured is just BEFORE kCloseBookCmdBoss runs.
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
// What a picked query does, read off its file (KFCQueryContents.cpp): the content handler InDesign's XML parser calls as it
// reads the file - the SDK's shape for reading an XML file (chlinguistic's kCHLUserDictContentHandlerBoss). + 27: the next
// after the highest spent.
DECLARE_PMID(kClassIDSpace, kKFCQueryContentsHandlerBoss, kKFCUIPrefix + 27)
// ...and the text cell of a row of the query dialog's lists: the stock cell with a tooltip of its own - the whole of what
// the row's query does (KFCQueryRowTip.cpp).
DECLARE_PMID(kClassIDSpace, kKFCQueryRowTextWidgetBoss, kKFCUIPrefix + 28)
// THE ROW EYE (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md): a hit row's eye - an icon button with a
// click handler of its own (KFCResultEyeEH).
DECLARE_PMID(kClassIDSpace, kKFCResultEyeWidgetBoss, kKFCUIPrefix + 29)
// ...and its TRACE - a drag over the column (KFCResultEyeDrag.cpp): the drag's data exchange handler, the Layers panel's
// kLayerEyeDEHandlerBoss in shape. + 30: the next after the highest spent.
DECLARE_PMID(kClassIDSpace, kKFCResultEyeDEHandlerBoss, kKFCUIPrefix + 30)
// InDesign's hidden-spread alert answered OK inside a walk of ours (KFCAlertAnswer.cpp - the spec map's OBJ-23): an
// alert handler registered as a service (kAlertHandlerService). + 31: the next after the highest spent.
DECLARE_PMID(kClassIDSpace, kKFCAlertAnswerBoss, kKFCUIPrefix + 31)


// InterfaceIDs:
// (The boundary's - the notification protocol, the session interfaces and the UI services at kKFCPrefix + 3 ... + 7,
//  the two halves' boundary stamps at kKFCPrefix + 13 and + 14 - and the MessageID are in KFCBoundaryID.h.
//  kKFCUIPrefix + 3 ... + 7 have never been used.)
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
// The result tree's OWN event handler (the list, not a row) - the keys the list takes, listed at the head of
// KFCResultTreeEH.cpp (the arrows among them open a closed row they land on: a book search's chapters come up closed).
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
// The result list's selection rules - hit rows selected together (KFCResultTreeController.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCResultTreeControllerImpl, kKFCUIPrefix + 47)
// The UI half's boundary stamp the model half checks before it asks the UI services for anything (KFCUIServices.cpp,
// model/IKFCBoundary.h). + 48: the next after the highest spent.
DECLARE_PMID(kImplementationIDSpace, kKFCUIBoundaryImpl, kKFCUIPrefix + 48)
// The result list's row observer: a row CLOSED takes the rows selected under it out of the selection
// (KFCResultNodeObserver.cpp). + 49: the next after the highest spent.
DECLARE_PMID(kImplementationIDSpace, kKFCResultNodeObserverImpl, kKFCUIPrefix + 49)
// What a picked query does: the XML parser's content handler (KFCQueryContents.cpp) and the row cell's tooltip
// (KFCQueryRowTip.cpp). + 50 and + 51: the next after the highest spent.
DECLARE_PMID(kImplementationIDSpace, kKFCQueryContentsHandlerImpl, kKFCUIPrefix + 50)
DECLARE_PMID(kImplementationIDSpace, kKFCQueryRowTipImpl, kKFCUIPrefix + 51)
// The row eye's click (KFCResultEyeEH.cpp).
DECLARE_PMID(kImplementationIDSpace, kKFCResultEyeEHImpl, kKFCUIPrefix + 52)
// The row eye's trace (KFCResultEyeDrag.cpp): the drag's data exchange handler, the eye as the drag's source, and every
// eye as its target. + 53 ... + 55: the next after the highest spent.
DECLARE_PMID(kImplementationIDSpace, kKFCResultEyeDEHandlerImpl, kKFCUIPrefix + 53)
DECLARE_PMID(kImplementationIDSpace, kKFCResultEyeDragSourceImpl, kKFCUIPrefix + 54)
DECLARE_PMID(kImplementationIDSpace, kKFCResultEyeDropTargetImpl, kKFCUIPrefix + 55)
// The hidden-spread alert's handler and the service provider that registers it (KFCAlertAnswer.cpp). + 56 and + 57: the
// next after the highest spent.
DECLARE_PMID(kImplementationIDSpace, kKFCAlertAnswerImpl, kKFCUIPrefix + 56)
DECLARE_PMID(kImplementationIDSpace, kKFCAlertAnswerServiceImpl, kKFCUIPrefix + 57)


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
// The rule under block 1 (the commands - "Menu item positions" below), above the toggles.
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
// (+ 26 was "Redo" on the same menu. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKFCAcceptAllChangesActionID, kKFCUIPrefix + 27)
// RETIRED (not reused): the rule between Change Checked and the two scans, gone with them.
//DECLARE_PMID(kActionIDSpace, kKFCSeparator5ActionID, kKFCPrefix + 28)
// "Replace" on a hit row's right-click menu (the author's call): replaces that row - or, when it is one of rows
// selected together, all of them - with no prompt; the list stays a work list (KFCReplaceEngine::ReplaceHit
// / ReplaceHits).
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
// "Search This Story Again" on a STORY row's right-click menu (the author's call): that story alone walked
// again with the search's query, its rows put back and its version recorded, so a story edited since the search can be
// replaced in again (KFCSearchEngine::SearchStoryAgain). + 50: the next after the highest spent (+ 30 ... + 34, the
// story row's earlier items, are retired and never reused).
DECLARE_PMID(kActionIDSpace, kKFCSearchStoryAgainActionID, kKFCUIPrefix + 50)
// "Search This Document Again" on a DOCUMENT row's right-click menu (the author's call): that document walked
// again whole with the search's query, its rows put back (KFCSearchEngine::SearchDocumentAgain). + 51: the next after the
// highest spent.
DECLARE_PMID(kActionIDSpace, kKFCSearchDocumentAgainActionID, kKFCUIPrefix + 51)
// "Select All Rows" on a BOOK, DOCUMENT or STORY row's right-click menu (the author's call): every hit row
// under that row selected - for Return to replace them all together. One action in the three menus: the row
// right-clicked is the menu's widget (KFCActionComponent's RightClickedRow). + 52: the next after the highest spent.
DECLARE_PMID(kActionIDSpace, kKFCSelectAllRowsActionID, kKFCUIPrefix + 52)
// RETIRED (never reuse - a keyboard shortcut is recorded against the ActionID): "Keep Previous Results", the flyout's
// toggle for the panel's Find keeping the list it replaced as a tier (the stacked results' spec, section 2-3). The
// author then made keeping the default and dropped the toggle, so every Find keeps (KFCResultHistory).
//DECLARE_PMID(kActionIDSpace, kKFCKeepPreviousResultsActionID, kKFCUIPrefix + 53)
// "Clear These Results" on a TIER's own row's right-click menu (docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md
// R9 - the author's call): that tier alone let go - a grey one, or the live list (the grey tiers stay). The flyout's Clear
// Results still lets every tier go. + 54: the next after the highest spent (+ 53 is retired).
DECLARE_PMID(kActionIDSpace, kKFCClearTheseResultsActionID, kKFCUIPrefix + 54)
// "Search These Results Again" on a TIER's own row's right-click menu (the author's call): that tier's own
// search run once more - a grey one switched to first (KFCResultTree::SearchListAgain). + 55: the next after the highest
// spent.
DECLARE_PMID(kActionIDSpace, kKFCSearchTierAgainActionID, kKFCUIPrefix + 55)
// "Create Outlines" in an OBJECT row's right-click menu, under Special Operations (the author's call - the rows'
// objects outlined across spreads: KFCResultTree::OutlineRows, KFCObjectOutline). + 56: the next after the highest spent.
DECLARE_PMID(kActionIDSpace, kKFCCreateOutlinesActionID, kKFCUIPrefix + 56)
// "Restore from Copy" on a TEXT row's right-click menu, under Replace (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md
// C1 - the author: "copy back, from the right-click menu"; the name is the author's later word,
// "put back with the copy" - the IDs and keys keep the first name, Restore Original): the copy the row's Replace took put back
// (KFCResultTree::RestoreRows, KFCReplaceEngine::RestoreHits). + 57: the next after the highest spent.
DECLARE_PMID(kActionIDSpace, kKFCRestoreOriginalActionID, kKFCUIPrefix + 57)
// An OBJECT row's other Special Operations (docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md - the
// author's call): InDesign's own Object menu commands over the rows' objects across spreads (KFCResultTree::SpecialRows,
// KFCObjectSpecial). Each action stands for one KFCSpecialOp (KFCActionComponent.cpp's table). + 58 on: the next after the
// highest spent.
// ...Fitting:
DECLARE_PMID(kActionIDSpace, kKFCFillFramePropActionID, kKFCUIPrefix + 58)
DECLARE_PMID(kActionIDSpace, kKFCFitContentPropActionID, kKFCUIPrefix + 59)
DECLARE_PMID(kActionIDSpace, kKFCContentAwareFitActionID, kKFCUIPrefix + 60)
DECLARE_PMID(kActionIDSpace, kKFCFitFrameToContentActionID, kKFCUIPrefix + 61)
DECLARE_PMID(kActionIDSpace, kKFCFitContentToFrameActionID, kKFCUIPrefix + 62)
DECLARE_PMID(kActionIDSpace, kKFCCenterContentActionID, kKFCUIPrefix + 63)
// ...Arrange:
DECLARE_PMID(kActionIDSpace, kKFCBringToFrontActionID, kKFCUIPrefix + 64)
DECLARE_PMID(kActionIDSpace, kKFCBringForwardActionID, kKFCUIPrefix + 65)
DECLARE_PMID(kActionIDSpace, kKFCSendBackwardActionID, kKFCUIPrefix + 66)
DECLARE_PMID(kActionIDSpace, kKFCSendToBackActionID, kKFCUIPrefix + 67)
// ...Transform, with no value to ask:
DECLARE_PMID(kActionIDSpace, kKFCRotate90CWActionID, kKFCUIPrefix + 68)
DECLARE_PMID(kActionIDSpace, kKFCRotate90CCWActionID, kKFCUIPrefix + 69)
DECLARE_PMID(kActionIDSpace, kKFCRotate180ActionID, kKFCUIPrefix + 70)
DECLARE_PMID(kActionIDSpace, kKFCFlipHorizontalActionID, kKFCUIPrefix + 71)
DECLARE_PMID(kActionIDSpace, kKFCFlipVerticalActionID, kKFCUIPrefix + 72)
DECLARE_PMID(kActionIDSpace, kKFCRedefineScalingActionID, kKFCUIPrefix + 73)
DECLARE_PMID(kActionIDSpace, kKFCClearTransformationsActionID, kKFCUIPrefix + 74)
// ...Lock / Unlock, Hide / Show:
DECLARE_PMID(kActionIDSpace, kKFCLockActionID, kKFCUIPrefix + 75)
DECLARE_PMID(kActionIDSpace, kKFCUnlockActionID, kKFCUIPrefix + 76)
DECLARE_PMID(kActionIDSpace, kKFCHideActionID, kKFCUIPrefix + 77)
DECLARE_PMID(kActionIDSpace, kKFCShowActionID, kKFCUIPrefix + 78)
// ...Move to Active Layer, Clear Object Style Overrides, Delete:
DECLARE_PMID(kActionIDSpace, kKFCMoveToLayerActionID, kKFCUIPrefix + 79)
DECLARE_PMID(kActionIDSpace, kKFCClearStyleOverridesActionID, kKFCUIPrefix + 80)
DECLARE_PMID(kActionIDSpace, kKFCDeleteActionID, kKFCUIPrefix + 81)
// RETIRED (never reuse): the rules between their groups, + 82 ... + 86 and + 91 below - in Special Operations and in
// Transform. The row menus have had no rules since they went into abc order (Menu item positions below).
//DECLARE_PMID(kActionIDSpace, kKFCSpecialSep1ActionID, kKFCUIPrefix + 82)
//DECLARE_PMID(kActionIDSpace, kKFCSpecialSep2ActionID, kKFCUIPrefix + 83)
//DECLARE_PMID(kActionIDSpace, kKFCSpecialSep3ActionID, kKFCUIPrefix + 84)
//DECLARE_PMID(kActionIDSpace, kKFCSpecialSep4ActionID, kKFCUIPrefix + 85)
//DECLARE_PMID(kActionIDSpace, kKFCTransformSep2ActionID, kKFCUIPrefix + 86)
// ...Transform with a value asked first (the spec's E12 - a dialog of KFCSpecialDialogs).
DECLARE_PMID(kActionIDSpace, kKFCMoveActionID, kKFCUIPrefix + 87)
DECLARE_PMID(kActionIDSpace, kKFCScaleActionID, kKFCUIPrefix + 88)
DECLARE_PMID(kActionIDSpace, kKFCRotateActionID, kKFCUIPrefix + 89)
DECLARE_PMID(kActionIDSpace, kKFCShearActionID, kKFCUIPrefix + 90)
//DECLARE_PMID(kActionIDSpace, kKFCTransformSep1ActionID, kKFCUIPrefix + 91)	// retired (above) - never reuse
// FIND OBJECTS ON ACTIVE LAYER (the panel menu, under Find - the author: "the same layer";
// docs/superpowers/specs/2026-10-17-kfc-object-special-operations-design.md E14): the Object tab's search kept to
// the objects on the Layers panel's layer. + 92. RETIRED (the author moved it into a tier heading's Narrow Down as
// On Active Layer, text tiers too - kKFCNarrowOnLayerActionID) - never reuse.
//DECLARE_PMID(kActionIDSpace, kKFCFindOnActiveLayerActionID, kKFCUIPrefix + 92)
// CLEAR FROM RESULTS (docs/superpowers/specs/2026-10-18-kfc-clear-from-results-design.md K2): the rows of a hit, object, story or
// document row taken off the list (+ 93: the next after the highest spent). + 94 was the rule above it in the four
// menus - retired with the row menus' other rules (above) - never reuse.
DECLARE_PMID(kActionIDSpace, kKFCClearFromResultsActionID, kKFCUIPrefix + 93)
//DECLARE_PMID(kActionIDSpace, kKFCClearRowsSepActionID, kKFCUIPrefix + 94)
// NARROW DOWN (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md N1, N2): a tier heading's submenu, its
// items + 95 ... + 102 - + 98 and + 101, its two rules, retired with the row menus' other rules (above): never reuse.
DECLARE_PMID(kActionIDSpace, kKFCNarrowInTablesActionID, kKFCUIPrefix + 95)
DECLARE_PMID(kActionIDSpace, kKFCNarrowInFootnotesActionID, kKFCUIPrefix + 96)
DECLARE_PMID(kActionIDSpace, kKFCNarrowInEndnotesActionID, kKFCUIPrefix + 97)
//DECLARE_PMID(kActionIDSpace, kKFCNarrowSep1ActionID, kKFCUIPrefix + 98)
DECLARE_PMID(kActionIDSpace, kKFCNarrowPagesActionID, kKFCUIPrefix + 99)
DECLARE_PMID(kActionIDSpace, kKFCNarrowOnPasteboardActionID, kKFCUIPrefix + 100)
//DECLARE_PMID(kActionIDSpace, kKFCNarrowSep2ActionID, kKFCUIPrefix + 101)
DECLARE_PMID(kActionIDSpace, kKFCNarrowFramesActionID, kKFCUIPrefix + 102)
// COPY RESULTS (docs/superpowers/specs/2026-10-18-kfc-copy-results-progress-share-design.md): the panel menu's (every row of
// the live list), a row's Copy Rows (the rows Clear from Results would take off; the book row's: every row) and a tier
// heading's Copy These Results - the rows as text on the clipboard (KFCCopyResults). + 103 ... + 105.
DECLARE_PMID(kActionIDSpace, kKFCCopyResultsActionID, kKFCUIPrefix + 103)
DECLARE_PMID(kActionIDSpace, kKFCCopyRowsActionID, kKFCUIPrefix + 104)
DECLARE_PMID(kActionIDSpace, kKFCCopyTheseResultsActionID, kKFCUIPrefix + 105)
// NARROW DOWN > ON ACTIVE LAYER (the author's call: Find Objects on Active Layer moved off the panel menu into Narrow
// Down, text tiers too): the tier's rows kept to the front document's active layer, by its name. + 106.
DECLARE_PMID(kActionIDSpace, kKFCNarrowOnLayerActionID, kKFCUIPrefix + 106)
// SEARCH TEXT IN THESE OBJECTS (docs/superpowers/specs/2026-10-21-kfc-text-in-object-results-design.md T2): a tier
// heading's item - the text inside a tier of objects searched with Find/Change's query, into a new tier
// (KFCResultHistory::SearchTextInObjects). + 107.
DECLARE_PMID(kActionIDSpace, kKFCSearchTextInObjectsActionID, kKFCUIPrefix + 107)


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
// ...and Find beside Run (docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md section 2-1 - the author's call
// (a)): the run order's selected query searched (the author's call), its list a new tier on the panel
// (KFCResultHistory), and the selection moved down one. + 45: never used at either prefix.
DECLARE_PMID(kWidgetIDSpace, kKFCQueryFindButtonWidgetID, kKFCUIPrefix + 45)
// A hit row's eye (the row eye) - found by the widget manager in each hit row (+ 46: the next after the highest spent;
// the retired check box's + 7 stays unused - the regression runner still reads 0x1ea687).
DECLARE_PMID(kWidgetIDSpace, kKFCResultEyeWidgetID, kKFCUIPrefix + 46)


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
// The document row's own right-click menu.
#define kKFCSearchDocumentAgainMenuKey	kKFCStringPrefix "kKFCSearchDocumentAgainMenuKey"
// The book, document and story rows' menus.
#define kKFCSelectAllRowsMenuKey		kKFCStringPrefix "kKFCSelectAllRowsMenuKey"
// A tier's own row's menu. NOT "kKFCClearTheseResultsMenuKey" (measured): that key has the length and the
// character sum of kKFCInternalPopupMenuNameKey (24 characters, 2448 each after the prefix), and the menu drew the
// internal popup's key in its place - InDesign's string lookup took the one for the other. A new key is checked against
// the untranslated table's (work/note-scripts/2026-10-14-kfc-tier-switch/key_collisions.py).
#define kKFCClearTierResultsMenuKey	kKFCStringPrefix "kKFCClearTierResultsMenuKey"
// ...and its Search These Results Again (measured against every other key - key_collisions.py: none reads alike).
#define kKFCSearchTierAgainMenuKey	kKFCStringPrefix "kKFCSearchTierAgainMenuKey"
// ...and its Search Text in These Objects (measured against every other key - key_collisions.py: none reads alike).
#define kKFCSearchTextInObjectsMenuKey	kKFCStringPrefix "kKFCSearchTextInObjectsMenuKey"
// ...and an object row's Create Outlines (measured against every other key - key_collisions.py: none reads alike).
#define kKFCCreateOutlinesMenuKey	kKFCStringPrefix "kKFCCreateOutlinesMenuKey"
// ...and a text row's Restore from Copy (measured against every other key - key_collisions.py).
#define kKFCRestoreOriginalMenuKey	kKFCStringPrefix "kKFCRestoreOriginalMenuKey"
// ...and Clear from Results - a hit, object, story or document row's. NOT "kKFCClearFromResultsMenuKey": the length and the
// character sum of kKFCClearTierResultsMenuKey ("From" and "Tier" add up alike - key_collisions.py).
#define kKFCClearRowsMenuKey		kKFCStringPrefix "kKFCClearRowsMenuKey"
// ...and a tier heading's Narrow Down - the submenu's name and its items (measured against every other key -
// key_collisions.py: none reads alike).
#define kKFCNarrowDownSubmenuKey		kKFCStringPrefix "kKFCNarrowDownSubmenuKey"
#define kKFCNarrowInTablesMenuKey		kKFCStringPrefix "kKFCNarrowInTablesMenuKey"
#define kKFCNarrowInFootnotesMenuKey	kKFCStringPrefix "kKFCNarrowInFootnotesMenuKey"
#define kKFCNarrowInEndnotesMenuKey		kKFCStringPrefix "kKFCNarrowInEndnotesMenuKey"
#define kKFCNarrowPagesMenuKey			kKFCStringPrefix "kKFCNarrowPagesMenuKey"
#define kKFCNarrowOnPasteboardMenuKey	kKFCStringPrefix "kKFCNarrowOnPasteboardMenuKey"
#define kKFCNarrowFramesMenuKey			kKFCStringPrefix "kKFCNarrowFramesMenuKey"
#define kKFCNarrowOnLayerMenuKey		kKFCStringPrefix "kKFCNarrowOnLayerMenuKey"		// "On Active Layer"
// ...and Copy Results: the panel menu's, a row's Copy Rows, a tier heading's Copy These Results (measured against every
// other key - key_collisions.py).
#define kKFCCopyResultsMenuKey			kKFCStringPrefix "kKFCCopyResultsMenuKey"
#define kKFCCopyRowsMenuKey				kKFCStringPrefix "kKFCCopyRowsMenuKey"
#define kKFCCopyTierResultsMenuKey		kKFCStringPrefix "kKFCCopyTierResultsMenuKey"
// ...and an object row's other Special Operations - InDesign's own English command names ("Degrees" for the degree sign: the
// string table is ASCII) (measured against every other key - key_collisions.py).
#define kKFCFillFramePropMenuKey			kKFCStringPrefix "kKFCFillFramePropMenuKey"
// (Not "FitContentPropMenuKey" - one length, one character sum with kKFCQueryOrderFileTypeKey; nor "FitFrameToContent"
// below - the same letters as Fit Content to Frame's key: a pair a string lookup has been seen to take for each other -
// key_collisions.py.)
#define kKFCContentProportionalMenuKey		kKFCStringPrefix "kKFCContentProportionalMenuKey"
#define kKFCContentAwareFitMenuKey			kKFCStringPrefix "kKFCContentAwareFitMenuKey"
#define kKFCFrameToContentMenuKey			kKFCStringPrefix "kKFCFrameToContentMenuKey"
#define kKFCFitContentToFrameMenuKey		kKFCStringPrefix "kKFCFitContentToFrameMenuKey"
#define kKFCCenterContentMenuKey			kKFCStringPrefix "kKFCCenterContentMenuKey"
#define kKFCBringToFrontMenuKey				kKFCStringPrefix "kKFCBringToFrontMenuKey"
#define kKFCBringForwardMenuKey				kKFCStringPrefix "kKFCBringForwardMenuKey"
#define kKFCSendBackwardMenuKey				kKFCStringPrefix "kKFCSendBackwardMenuKey"
#define kKFCSendToBackMenuKey				kKFCStringPrefix "kKFCSendToBackMenuKey"
#define kKFCRotate90CWMenuKey				kKFCStringPrefix "kKFCRotate90CWMenuKey"
#define kKFCRotate90CCWMenuKey				kKFCStringPrefix "kKFCRotate90CCWMenuKey"
#define kKFCRotate180MenuKey				kKFCStringPrefix "kKFCRotate180MenuKey"
#define kKFCFlipHorizontalMenuKey			kKFCStringPrefix "kKFCFlipHorizontalMenuKey"
#define kKFCFlipVerticalMenuKey				kKFCStringPrefix "kKFCFlipVerticalMenuKey"
#define kKFCRedefineScalingMenuKey			kKFCStringPrefix "kKFCRedefineScalingMenuKey"
#define kKFCClearTransformationsMenuKey		kKFCStringPrefix "kKFCClearTransformationsMenuKey"
#define kKFCLockMenuKey						kKFCStringPrefix "kKFCLockMenuKey"
#define kKFCUnlockMenuKey					kKFCStringPrefix "kKFCUnlockMenuKey"
#define kKFCHideMenuKey						kKFCStringPrefix "kKFCHideMenuKey"
#define kKFCShowMenuKey						kKFCStringPrefix "kKFCShowMenuKey"
#define kKFCMoveToLayerMenuKey				kKFCStringPrefix "kKFCMoveToLayerMenuKey"
#define kKFCClearStyleOverridesMenuKey		kKFCStringPrefix "kKFCClearStyleOverridesMenuKey"
#define kKFCDeleteMenuKey					kKFCStringPrefix "kKFCDeleteMenuKey"
#define kKFCMoveMenuKey						kKFCStringPrefix "kKFCMoveMenuKey"
#define kKFCScaleMenuKey					kKFCStringPrefix "kKFCScaleMenuKey"
#define kKFCRotateMenuKey					kKFCStringPrefix "kKFCRotateMenuKey"
#define kKFCShearMenuKey					kKFCStringPrefix "kKFCShearMenuKey"
// ...and the names of their submenus: Special Operations, and Fitting / Arrange / Transform in it
// (kKFCSpecialOpsSubmenuPath below - the plain words of the three are InDesign's own keys), each measured against
// every other key (key_collisions.py).
#define kKFCSpecialOpsSubmenuKey			kKFCStringPrefix "kKFCSpecialOpsSubmenuKey"
#define kKFCFittingSubmenuKey				kKFCStringPrefix "kKFCFittingSubmenuKey"
#define kKFCArrangeSubmenuKey				kKFCStringPrefix "kKFCArrangeSubmenuKey"
#define kKFCTransformSubmenuKey				kKFCStringPrefix "kKFCTransformSubmenuKey"
// "How to Use...": the operating reference. English in every UI language, like the rest of the
// flyout - there is one string table, and the one thing switched to Japanese at run time is this page's
// body (KFCHowTo::Show).
// The BODY of the reference is not here at all: it lives in KFCHowTo.cpp, because odfrc caps a single
// string at about 3.1KB and this text is several times that.
#define kKFCHowToMenuKey				kKFCStringPrefix "kKFCHowToMenuKey"
// "Open Find/Change...".
#define kKFCOpenFindChangeMenuKey		kKFCStringPrefix "kKFCOpenFindChangeMenuKey"
// "Run Saved Queries..." - and the query dialog's own words: its title, the two lists' headings, the four
// buttons between them, Find, Run and Close. English in every UI language, like the rest of the flyout.
#define kKFCRunSavedQueriesMenuKey		kKFCStringPrefix "kKFCRunSavedQueriesMenuKey"
#define kKFCQueryDialogTitleKey			kKFCStringPrefix "kKFCQueryDialogTitleKey"
#define kKFCQuerySavedLabelKey			kKFCStringPrefix "kKFCQuerySavedLabelKey"
#define kKFCQueryOrderLabelKey			kKFCStringPrefix "kKFCQueryOrderLabelKey"
#define kKFCQueryAddKey					kKFCStringPrefix "kKFCQueryAddKey"
#define kKFCQueryRemoveKey				kKFCStringPrefix "kKFCQueryRemoveKey"
#define kKFCQueryUpKey					kKFCStringPrefix "kKFCQueryUpKey"
#define kKFCQueryDownKey				kKFCStringPrefix "kKFCQueryDownKey"
#define kKFCQueryClearKey				kKFCStringPrefix "kKFCQueryClearKey"
#define kKFCQueryFindKey				kKFCStringPrefix "kKFCQueryFindKey"
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

// The HIT rows' right-click menu: the popup's internal name. What it and the menus below hold is their MenuDef in
// KFCUI.fr, in abc order by the positions under "Menu item positions" below.
// KFCResultNodeEH::RButtonDn pops the MenuDef subtree of this name at the cursor with
// IMenuManager::HandlePopupMenu - the same machinery as the real Links / Layers panel row menus, and
// as KESCL's report rows (kKESCLReportRowMenuName). The root name is never displayed, so it is a
// plain literal rather than a translated key. (The book / document rows' "KFCRtMenuResultRow" went with Change
// Checked - spec F16 - and so did the story rows' first menu, Replace / Check All / Uncheck All.)
#define kKFCResultHitMenuName				"KFCRtMenuResultHit"
// The STORY rows' right-click menu, popped the same way.
#define kKFCResultStoryMenuName				"KFCRtMenuResultStory"
// The DOCUMENT rows' right-click menu, popped the same way.
// A name of its own, not the retired "KFCRtMenuResultRow" above.
#define kKFCResultDocumentMenuName			"KFCRtMenuResultDocument"
// The BOOK row's right-click menu, popped the same way.
#define kKFCResultBookMenuName				"KFCRtMenuResultBook"
// A TIER's own row's right-click menu, popped the same way - a grey tier's and the live one's.
#define kKFCResultTierMenuName				"KFCRtMenuResultTier"
// An OBJECT row's right-click menu (the Object tab's rows - the author's call; a text row's is kKFCResultHitMenuName),
// popped the same way.
#define kKFCResultObjectMenuName			"KFCRtMenuResultObject"
// ...its Special Operations submenu (the author's word): the parent node is ActionID 0 with the path and a
// delimiter (KCM's submenus' Adobe idiom), its items take the path. Named by KFC's own key, as the submenus in it are
// (basicmenu's idiom - every SDK sample's menu path ends in a key of its own): a plain word in a path is looked up as a
// key all the same, and shows as itself only while no key reads the same.
#define kKFCSpecialOpsSubmenuPath			kKFCResultObjectMenuName kSDKDefDelimitMenuPath kKFCSpecialOpsSubmenuKey
// ...and a TEXT row's (the text-outlines spec's X1): the same submenu under the hit row's menu, Create Outlines in it.
#define kKFCHitSpecialOpsSubmenuPath		kKFCResultHitMenuName kSDKDefDelimitMenuPath kKFCSpecialOpsSubmenuKey
// ...and the submenus in it, after InDesign's own Object menu (the spec's E2): Fitting, Arrange, Transform - the same idiom
// one level down. NAMED BY KFC'S OWN KEYS (basicmenu's idiom - BscMnuID.h, kBscMnuPluginsMenuKey): the plain words are
// InDesign's own string keys, and the menu showed them in InDesign's language ("Fitting" as Japanese InDesign's
// Fitting submenu - measured live); KFC's words are English in every language (the author's call).
#define kKFCFittingSubmenuPath				kKFCSpecialOpsSubmenuPath kSDKDefDelimitMenuPath kKFCFittingSubmenuKey
#define kKFCArrangeSubmenuPath				kKFCSpecialOpsSubmenuPath kSDKDefDelimitMenuPath kKFCArrangeSubmenuKey
#define kKFCTransformSubmenuPath			kKFCSpecialOpsSubmenuPath kSDKDefDelimitMenuPath kKFCTransformSubmenuKey
// A tier heading's Narrow Down submenu (the narrow-down spec's N1) - named by KFC's own key, as Fitting is.
#define kKFCNarrowDownSubmenuPath			kKFCResultTierMenuName kSDKDefDelimitMenuPath kKFCNarrowDownSubmenuKey

// Menu item positions:
//
// The flyout, as KFCUI.fr lays it out (the author's arrangement):
//    0.5 - 1.3    the commands: Open Find/Change..., Find in <scope>, Change All in Book (No List), Clear Results,
//                 Copy Results, Run Saved Queries... (the two that write to the DOCUMENTS among them)
//   ---- 2.0
//    2.2 - 2.9    the check-mark toggles (Block 3 below)
//   ---- 3.0
//    4.0          Save Panel Settings - writes a FILE of our own
//   ---- 10.0
//   10.5 - 11.0   How to Use..., About
// Positions that a new order allowed to stay were left where they were, so only the items that
// actually moved carry new numbers.

// Block 1 - the search, then Change All in Book (No List), Clear Results, Copy Results and Run Saved Queries...
// with no rule between (the author's call), then a rule. The scope the search runs on is set by Book Scope
// (Change All runs on the book alone - F18).
// Open Find/Change... leads the block: open the dialog, type the query, then search.
#define kKFCOpenFindChangeMenuItemPosition	0.5
#define kKFCSearchBookMenuItemPosition		1.0
// Change All in Book (No List) under Find, then Clear Results (the spec's section 4).
#define kKFCChangeAllMenuItemPosition		1.1
#define kKFCClearResultsMenuItemPosition	1.25
// Copy Results under Clear Results: the list's rows as text on the clipboard (the copy-results spec).
#define kKFCCopyResultsMenuItemPosition		1.27
// Run Saved Queries... last in the block (the author's call): the other command that writes with InDesign's Change
// All and leaves no list.
#define kKFCRunSavedQueriesMenuItemPosition	1.3
// (No block 2: the scans are the Book panel's preflight's - the author's call.)
#define kKFCSeparator3MenuItemPosition		2.0

// Block 3 - the check-mark toggles, 2.2 to 2.9, in alphabetical order (the author's arrangement). That
// keeps Book Scope first - it is the one that decides what the commands in block 1 run on - and Find/Change Selected
// Documents (Book) right under it: it narrows Book Scope, and works only with it on. Then Hide Previous Chapter, Link the
// Application Bar's Search Field to This Panel, Minimizable Find/Change, Remember Book Panel Placement, Translucent
// Find/Change and Translucent Panel.
#define kKFCBookScopeMenuItemPosition		2.2
#define kKFCSelectedDocumentsMenuItemPosition	2.3
#define kKFCHidePrevChapterMenuItemPosition	2.4
#define kKFCAppBarSearchEnterMenuItemPosition	2.5
#define kKFCMinimizableFindChangeMenuItemPosition	2.6
#define kKFCRememberBookPanelMenuItemPosition	2.7
#define kKFCTranslucentFindChangeMenuItemPosition	2.8
#define kKFCTranslucentPanelMenuItemPosition	2.9
#define kKFCSeparator4MenuItemPosition		3.0

// Block 4 - the command that writes a file of our own and touches no document: the toggles above
// (Save Panel Settings).
#define kKFCSavePanelSettingsMenuItemPosition	4.0
#define	kKFCSeparator1MenuItemPosition		10.0

// Block 5 - the reference items, last. No rule between them: another one right above About would
// draw two dividers with a single item between them.
#define kKFCHowToMenuItemPosition			10.5
#define kKFCAboutThisMenuItemPosition		11.0

// THE RIGHT-CLICK MENUS OF THE ROWS AND THE TIER HEADINGS, IN ABC ORDER (the author's call: "sort the menus on the
// panel's rows in abc order"): every item ONE position by its NAME, the same number wherever it appears - so each menu,
// holding a few of them, comes out in abc order whichever it is (a hit, object, story, document or book row; a tier
// heading) - and no rules between them. Each submenu is in abc order within itself. This replaces the order the specs
// give (the searches first and the one that takes away last under a rule; Special Operations in the Object menu's order).
#define kKFCClearFromResultsMenuItemPosition	1.0		// Clear from Results
#define kKFCClearTheseResultsMenuItemPosition	2.0		// Clear These Results
#define kKFCCopyRowsMenuItemPosition			3.0		// Copy Rows
#define kKFCCopyTheseResultsMenuItemPosition	4.0		// Copy These Results
#define kKFCNarrowDownSubmenuPosition			5.0		// Narrow Down >
#define kKFCReplaceHitMenuItemPosition			6.0		// Replace (a hit row's)
#define kKFCObjectReplaceMenuItemPosition		6.0		// Replace (an object row's - the same action)
#define kKFCRestoreOriginalMenuItemPosition		7.0		// Restore from Copy
#define kKFCSearchTextInObjectsMenuItemPosition	7.5		// Search Text in These Objects
#define kKFCSearchTierAgainMenuItemPosition		8.0		// Search These Results Again
#define kKFCSearchDocumentAgainMenuItemPosition	9.0		// Search This Document Again
#define kKFCSearchStoryAgainMenuItemPosition	10.0	// Search This Story Again
#define kKFCSelectAllRowsMenuItemPosition		11.0	// Select All Rows
#define kKFCHitSpecialOpsSubmenuPosition		12.0	// Special Operations > (a text row's)
#define kKFCSpecialOpsSubmenuPosition			12.0	// Special Operations > (an object row's)
// (The rules those menus had are drawn no more: their ActionIDs - + 82 ... + 86, + 91, + 94, + 98, + 101 - are
//  retired in the ActionIDs above.)
// Narrow Down, in abc order.
#define kKFCNarrowFramesMenuItemPosition		1.0		// Frames of These Matches
#define kKFCNarrowInEndnotesMenuItemPosition	2.0		// In Endnotes
#define kKFCNarrowInFootnotesMenuItemPosition	3.0		// In Footnotes
#define kKFCNarrowInTablesMenuItemPosition		4.0		// In Tables
#define kKFCNarrowOnLayerMenuItemPosition		5.0		// On Active Layer
#define kKFCNarrowOnPasteboardMenuItemPosition	6.0		// On Pasteboard
#define kKFCNarrowPagesMenuItemPosition			7.0		// Pages...
// Special Operations, in abc order (an object row's whole; a text row's holds Create Outlines alone).
#define kKFCArrangeSubmenuPosition				1.0		// Arrange >
#define kKFCClearStyleOverridesMenuItemPosition	2.0		// Clear Object Style Overrides
#define kKFCCreateOutlinesMenuItemPosition		3.0		// Create Outlines
#define kKFCDeleteMenuItemPosition				4.0		// Delete
#define kKFCFittingSubmenuPosition				5.0		// Fitting >
#define kKFCHideMenuItemPosition				6.0		// Hide
#define kKFCLockMenuItemPosition				7.0		// Lock
#define kKFCMoveToLayerMenuItemPosition			8.0		// Move to Active Layer
#define kKFCShowMenuItemPosition				9.0		// Show
#define kKFCTransformSubmenuPosition			10.0	// Transform >
#define kKFCUnlockMenuItemPosition				11.0	// Unlock
// Fitting, in abc order.
#define kKFCCenterContentMenuItemPosition		1.0		// Center Content
#define kKFCContentAwareFitMenuItemPosition		2.0		// Content-Aware Fit
#define kKFCFillFramePropMenuItemPosition		3.0		// Fill Frame Proportionally
#define kKFCFitContentPropMenuItemPosition		4.0		// Fit Content Proportionally
#define kKFCFitContentToFrameMenuItemPosition	5.0		// Fit Content to Frame
#define kKFCFitFrameToContentMenuItemPosition	6.0		// Fit Frame to Content
// Arrange, in abc order.
#define kKFCBringForwardMenuItemPosition		1.0		// Bring Forward
#define kKFCBringToFrontMenuItemPosition		2.0		// Bring to Front
#define kKFCSendBackwardMenuItemPosition		3.0		// Send Backward
#define kKFCSendToBackMenuItemPosition			4.0		// Send to Back
// Transform, in abc order - by the characters, as a sort reads them: "Rotate 180" before "Rotate 90", both before "Rotate...".
#define kKFCClearTransformationsMenuItemPosition	1.0		// Clear Transformations
#define kKFCFlipHorizontalMenuItemPosition		2.0		// Flip Horizontal
#define kKFCFlipVerticalMenuItemPosition		3.0		// Flip Vertical
#define kKFCMoveMenuItemPosition				4.0		// Move...
#define kKFCRedefineScalingMenuItemPosition		5.0		// Redefine Scaling as 100%
#define kKFCRotate180MenuItemPosition			6.0		// Rotate 180 Degrees
#define kKFCRotate90CWMenuItemPosition			7.0		// Rotate 90 Degrees Clockwise
#define kKFCRotate90CCWMenuItemPosition			8.0		// Rotate 90 Degrees Counter-clockwise
#define kKFCRotateMenuItemPosition				9.0		// Rotate...
#define kKFCScaleMenuItemPosition				10.0	// Scale...
#define kKFCShearMenuItemPosition				11.0	// Shear...


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

// THE MESSAGE BLOCK AT THE TOP OF THE PANEL, AND THE EDGE THE ILLUSTRATIONS AND THE TREE LINE UP ON.
//
// Read from both sides, as the row height is: KFCUI.fr places the message block, the illustrations and the result
// tree with these, and KFCPanelMetrics places them again from the same numbers when the panel appears - at the
// palette font's own line, which is taller than the English one on a Japanese UI (KFCPanelMetrics.h says why).
//
// The block is a WHOLE NUMBER of lines - FOUR, in every UI language (KFCPanelMetrics.cpp carries the measurement
// behind four) - and the .fr rests at the English line, 12px. The illustrations sit ON the block's bottom edge, and
// the tree starts the gap below it.
#define kKFCMessageLines		4
#define kKFCMessageLineHeight	12
#define kKFCMessageTop			6
#define kKFCMessageBottom		(kKFCMessageTop + kKFCMessageLines * kKFCMessageLineHeight)
#define kKFCGapUnderMessage		3

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
