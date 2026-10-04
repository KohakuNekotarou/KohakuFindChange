//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChangeUI (the UI half of Kohaku Find/Change)
//
//  ***** THE UI HALF'S IDS (2026-10-01, the model/UI split). *****
//  Everything the panel needs: its widgets, its menus and actions, the observers and services that
//  serve it, its string keys, positions and resources. Moved here from KBSID.h at kKFCUIPrefix + the
//  SAME offset and under the SAME name (docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md
//  section 2), so the C++ that names them did not change - only which header it includes.
//  What both halves must agree on - the company, the version, the model's PluginID, the boundary's
//  interfaces and message - is in KBSBoundaryID.h (model/), included below. The model half's own ids
//  are in model/KBSID.h, and this half does not include it.
//  The string keys keep the model's prefix (kKBSStringPrefix, "0x1EA600"): a key is unique by its value
//  across the application, and not one value had to change, so the tables only moved.
//
//========================================================================================

#ifndef __KFCUIID_h__
#define __KFCUIID_h__

#include "SDKDef.h"
#include "KBSBoundaryID.h"	// the company, the version, kKBSPluginID (this half depends on it), the boundary's ids

// Plug-in:
#define kKFCUIPluginName	"KohakuFindChangeUI"		// Internal name: the ID system and the .rc InternalName. Never change it.
#define kKFCUIFileName		"KohakuFindChangeUI"		// Base name of the build output: KohakuFindChangeUI.pln and its "(KohakuFindChangeUI Resources)" folder. MUST match the vcxproj TargetName.
#define kKFCUIDisplayName	"Kohaku Find/Change UI"		// The .rc FileDescription and the plug-in list. The panel, its tab and the About box keep kKBSDisplayName.

// Plug-in Prefix: the second half of KBS's band (0x1EA600 - 0x1EA6FF, Adobe's grant of 2026-08-15) - the
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
DECLARE_PMID(kClassIDSpace, kKBSActionComponentBoss, kKFCUIPrefix + 0)
DECLARE_PMID(kClassIDSpace, kKBSPanelWidgetBoss, kKFCUIPrefix + 1)
// Result tree (Task 2): the tree-view list, its row (node) boss shared by chapter and hit rows,
// and the custom multi-colour text cell that highlights the matched part of a hit line.
DECLARE_PMID(kClassIDSpace, kKBSResultListWidgetBoss, kKFCUIPrefix + 2)
DECLARE_PMID(kClassIDSpace, kKBSResultNodeWidgetBoss, kKFCUIPrefix + 3)
DECLARE_PMID(kClassIDSpace, kKBSColorTextWidgetBoss, kKFCUIPrefix + 4)
// Task 3 (jump + red marker): the marker-expiry idle task boss. (The startup/shutdown service boss of
// the same task is the model half's; this half's own is + 20 below.)
DECLARE_PMID(kClassIDSpace, kKBSMarkerExpiryIdleTaskBoss, kKFCUIPrefix + 6)
// Replace feature: the hit row's check box. A stock check box (kCheckBoxWidgetBoss, drawn by the
// system so it follows the UI theme) with our observer aggregated on it, the layer panel's eyeball
// pattern. Only hit rows carry one - the chapter row resource has no check box.
DECLARE_PMID(kClassIDSpace, kKBSResultCheckWidgetBoss, kKFCUIPrefix + 8)
// The panel's illustration: the system rollover icon button plus a tooltip of its own, so hovering
// it says where clicking it goes. Same shape as kLinksUIButtonBoss in open/components/linksui, and
// as KCM's kKCMIconWidgetBoss - which is where the panel this copies got it from.
DECLARE_PMID(kClassIDSpace, kKBSIconWidgetBoss, kKFCUIPrefix + 14)
// "Remember Book Panel Placement" (2026-09-25): the palette-manager service boss - registered for
// kPaletteMgrService, the service InDesign's own Book panel hangs off, so it is told when the
// palettes have been laid out and when they are about to close (KBSBookPanelPlacement.cpp).
DECLARE_PMID(kClassIDSpace, kKBSBookPanelServiceBoss, kKFCUIPrefix + 15)
// ...and its command interceptor: a book closing destroys its Book panel WITHOUT a word to the panel
// manager's subject (measured 2026-09-25 - neither kAboutToClosePaletteMsg nor a visibility message
// arrives), so the only moment the panel can still be measured is just BEFORE kCloseBookCmdBoss runs.
DECLARE_PMID(kClassIDSpace, kKBSBookPanelCmdWatchBoss, kKFCUIPrefix + 16)
// The panel's message area, drawn by hand (2026-09-29, KBSStatusTextView.cpp): a generic panel with our
// IControlView and IKBSStatusTextData - the shape of the hit row's cell (+4) and of KCM's message area.
DECLARE_PMID(kClassIDSpace, kKBSStatusTextWidgetBoss, kKFCUIPrefix + 19)
// The UI half's own startup/shutdown service (2026-10-01, the model/UI split - KBSUIStartupShutdown.cpp):
// a startup/shutdown service is declared per boss, so the side the panel's work runs on needs a boss of
// its own. (+ 13 was the first choice of the 2026-08-16 plan; it is a retired id, so it stays spent.)
DECLARE_PMID(kClassIDSpace, kKBSUIStartupShutdownBoss, kKFCUIPrefix + 20)
// RETIRED 2026-10-01 (not reused): the action filter that took the "needs a document" bits off InDesign's
// own Edit > Find/Change, so the menu and Ctrl+F worked with no document open. The user's call the same
// day: KBS leaves InDesign's menu alone - the panel's Open Find/Change... opens the dialog instead.
//DECLARE_PMID(kClassIDSpace, kKBSFindChangeAnywhereBoss, kKFCUIPrefix + 21)


// InterfaceIDs:
// (The boundary's - the notification protocol, the session interfaces and the UI services - and the
//  MessageID are at kKBSPrefix + 3 ... + 7 in KBSBoundaryID.h. The "not + 3" notes below date from when
//  both halves shared kKBSPrefix; kKFCUIPrefix + 3 ... + 7 were never used.)
// Per-row draw data for a hit line's colour cell: the three text segments (before / matched /
// after) the cell paints, the match segment in a highlight colour.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSROWDATA, kKFCUIPrefix + 0)
// The observer that re-applies the "Translucent Panel" alpha when the panel is opened, closed,
// docked or floated (kPaletteVisibilityChangedMessage). Its own IID because it is an AddIn onto
// kActiveContextBoss, which carries observers that are not ours. See KBSPanelAlpha.cpp.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSPANELVISIBILITYOBSERVER, kKFCUIPrefix + 2)
// The observer behind "Remember Book Panel Placement" (2026-09-25): measures InDesign's Book panel as
// it closes and puts it back when it appears. Its own IID for the reason the one above has one - it
// is an AddIn onto kActiveContextBoss, next to that one. See KBSBookPanelPlacement.cpp. (+ 8, not
// + 3: see the note above. It was + 3 for an hour, unshipped, until the second re-check caught it.)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSBOOKPANELOBSERVER, kKFCUIPrefix + 8)
// What the panel's message area draws, split where its colour changes (2026-09-29, IKBSStatusTextData.h).
// (+ 9, not + 3: + 3 ... + 7 are the split plan's - see above.)
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSSTATUSTEXTDATA, kKFCUIPrefix + 9)
// The observer that makes the application bar's search field show what Edit > Find/Change holds (2026-10-03,
// KBSAppBarSearchEnter.cpp). Its own IID for the reason the two observers above have one: an AddIn onto
// kActiveContextBoss.
DECLARE_PMID(kInterfaceIDSpace, IID_IKBSAPPBARMIRROROBSERVER, kKFCUIPrefix + 10)


// ImplementationIDs:
DECLARE_PMID(kImplementationIDSpace, kKBSActionComponentImpl, kKFCUIPrefix + 0 )
// Result tree (Task 2): hierarchy adapter, row widget manager, the colour cell's view and its
// per-row data holder.
DECLARE_PMID(kImplementationIDSpace, kKBSResultListAdapterImpl, kKFCUIPrefix + 1)
DECLARE_PMID(kImplementationIDSpace, kKBSResultListWidgetMgrImpl, kKFCUIPrefix + 2)
DECLARE_PMID(kImplementationIDSpace, kKBSColorTextViewImpl, kKFCUIPrefix + 3)
DECLARE_PMID(kImplementationIDSpace, kKBSRowDataImpl, kKFCUIPrefix + 4)
// Task 3: marker-expiry idle task and the hit row's event handler (click -> jump). (The task's
// startup/shutdown service is the model half's.)
DECLARE_PMID(kImplementationIDSpace, kKBSMarkerExpiryIdleTaskImpl, kKFCUIPrefix + 7)
DECLARE_PMID(kImplementationIDSpace, kKBSResultNodeEHImpl, kKFCUIPrefix + 8)
// Replace feature: the hit row check box's observer (click -> flip that hit's checked flag).
DECLARE_PMID(kImplementationIDSpace, kKBSResultCheckObserverImpl, kKFCUIPrefix + 10)
// The panel's observer (KBSPanelTitle.cpp): on the panel boss, it writes the tab's name, the layout,
// the picture and the message the moment the panel appears, hears the picture's click, and keeps the
// tab's name following the Find/Change settings and the selection (an ActiveSelectionObserver since
// 2026-10-03). "Whose only job is to write the current scope onto the tab" until then.
DECLARE_PMID(kImplementationIDSpace, kKBSPanelObserverImpl, kKFCUIPrefix + 13)
// The result tree's OWN event handler (the list, not a row): up / down arrows that OPEN the row
// they land on, so a book search's closed chapters do not hide their hits from the keyboard.
DECLARE_PMID(kImplementationIDSpace, kKBSResultTreeEHImpl, kKFCUIPrefix + 17)
// The panel illustration's tooltip: hovering the icon shows the URL clicking it opens, so the
// picture is not a mystery button (see KBSIconTip.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSIconTipImpl, kKFCUIPrefix + 19)
// The panel's own IControlView: stock palette behaviour plus a floor under how small the user can
// drag the panel (see KBSPanelView.cpp). +19 is the tooltip above, and +18 is a retired id that is
// deliberately not reused, so this is the next free number.
DECLARE_PMID(kImplementationIDSpace, kKBSPanelViewImpl, kKFCUIPrefix + 20)
// "Translucent Panel" (2026-08-04, brought over from KESCM): the observer that re-applies the alpha
// when the panel's window is rebuilt, and the roll-over that takes it off while the pointer is on
// the panel. Both in KBSPanelAlpha.cpp.
DECLARE_PMID(kImplementationIDSpace, kKBSPanelVisibilityObserverImpl, kKFCUIPrefix + 21)
DECLARE_PMID(kImplementationIDSpace, kKBSPanelRollOverImpl, kKFCUIPrefix + 22)
// ***** + 23 ... + 28: THE MODEL/UI SPLIT'S BOUNDARY (2026-10-01). ***** Reserved for it since 2026-08-16.
// + 23 = the UI half's observer of the model's notifications (KBSModelObserver.cpp);
// + 24 ... + 26 = the model half's three session interfaces (KBSModelServices.cpp);
// + 27 = the UI half's own startup/shutdown service (KBSUIStartupShutdown.cpp);
// + 28 = the UI services the model half asks for (KBSUIServices.cpp).
// (+ 24 ... + 26 are the model half's: model/KBSID.h, at kKBSPrefix.)
DECLARE_PMID(kImplementationIDSpace, kKBSModelObserverImpl, kKFCUIPrefix + 23)
DECLARE_PMID(kImplementationIDSpace, kKBSUIStartupShutdownImpl, kKFCUIPrefix + 27)
DECLARE_PMID(kImplementationIDSpace, kKBSUIServicesImpl, kKFCUIPrefix + 28)
// "Remember Book Panel Placement" (2026-09-25): the observer on kActiveContextBoss, and the two
// halves of the palette-manager service boss (its provider and the IPaletteMgrService itself). All
// three in KBSBookPanelPlacement.cpp. (+ 29 onwards, not + 23: see the note above.)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelObserverImpl, kKFCUIPrefix + 29)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelServiceProviderImpl, kKFCUIPrefix + 30)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelPaletteMgrServiceImpl, kKFCUIPrefix + 31)
DECLARE_PMID(kImplementationIDSpace, kKBSBookPanelCmdWatchImpl, kKFCUIPrefix + 32)
// The panel's message area (2026-09-29, KBSStatusTextView.cpp): its view and its data.
DECLARE_PMID(kImplementationIDSpace, kKBSStatusTextViewImpl, kKFCUIPrefix + 36)
DECLARE_PMID(kImplementationIDSpace, kKBSStatusTextDataImpl, kKFCUIPrefix + 37)
// RETIRED 2026-10-01 (not reused): the action filter's implementation - see kKFCUIPrefix + 21 in the class ids.
//DECLARE_PMID(kImplementationIDSpace, kKBSFindChangeAnywhereImpl, kKFCUIPrefix + 39)
// The application bar's search field following Find/Change (2026-10-03, KBSAppBarSearchEnter.cpp). (+ 40: the
// next number after the retired + 39.)
DECLARE_PMID(kImplementationIDSpace, kKBSAppBarMirrorObserverImpl, kKFCUIPrefix + 40)


// ActionIDs:
DECLARE_PMID(kActionIDSpace, kKBSAboutActionID, kKFCUIPrefix + 0)
DECLARE_PMID(kActionIDSpace, kKBSPanelWidgetActionID, kKFCUIPrefix + 1)
DECLARE_PMID(kActionIDSpace, kKBSSeparator1ActionID, kKFCUIPrefix + 2)
DECLARE_PMID(kActionIDSpace, kKBSPopupAboutThisActionID, kKFCUIPrefix + 3)
DECLARE_PMID(kActionIDSpace, kKBSSearchBookActionID, kKFCUIPrefix + 4)
DECLARE_PMID(kActionIDSpace, kKBSHidePrevChapterActionID, kKFCUIPrefix + 5)
// "Book Scope": search the whole book (ON) or what Find/Change's Search: names (OFF - the front
// document until 2026-09-29). Check-mark toggle,
// the KESCL "Search book" pattern (kKESCLPopupSearchBookActionID).
DECLARE_PMID(kActionIDSpace, kKBSScopeBookActionID, kKFCUIPrefix + 6)
// Separator between the search command and the toggles below it (MenuDef only, no ActionDef).
// (Its MenuDef was removed on 2026-09-27, the user's call - Change Checked sits right under the
// search now. Not reused.)
//DECLARE_PMID(kActionIDSpace, kKBSSeparator2ActionID, kKBSPrefix + 7)
// Replace feature: a separator, the replace command, and the two bulk check commands.
DECLARE_PMID(kActionIDSpace, kKBSSeparator3ActionID, kKFCUIPrefix + 8)
DECLARE_PMID(kActionIDSpace, kKBSReplaceCheckedActionID, kKFCUIPrefix + 9)
DECLARE_PMID(kActionIDSpace, kKBSCheckAllActionID, kKFCUIPrefix + 10)
DECLARE_PMID(kActionIDSpace, kKBSUncheckAllActionID, kKFCUIPrefix + 11)
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
DECLARE_PMID(kActionIDSpace, kKBSHowToActionID, kKFCUIPrefix + 18)
// "Translucent Panel" on the flyout: a check-mark toggle (ON = this panel is drawn translucent while
// it floats). *Windows only. *Selectable while docked, where it has no visible effect - the flag is
// set and applies the moment the panel floats again. OFF by default, and not remembered across
// restarts. See KBSPanelAlpha.cpp.
DECLARE_PMID(kActionIDSpace, kKBSTranslucentPanelActionID, kKFCUIPrefix + 19)
// "Save Panel Settings": write the flyout's SETTINGS toggles to a JSON file of our own in the user's
// preferences folder, read back at startup (KBSPanelState.cpp). A plain command, not a toggle - and
// an explicit one, the way KESCM has it: settings are saved when asked for, never behind the user's
// back. What the file holds is listed in ONE place, KBSPanelState.h (it said "four settings" here,
// and then the fifth - Remember Book Panel Placement and its placement - arrived). *Book Scope is
// deliberately not among them; the reason is there too.
DECLARE_PMID(kActionIDSpace, kKBSSavePanelSettingsActionID, kKFCUIPrefix + 20)
// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog. Check-mark
// toggle, Windows only, OFF by default. The dialog is found through the SDK's window list, not by
// its title, so it works whatever language InDesign is running in (KBSPanelAlpha.cpp).
DECLARE_PMID(kActionIDSpace, kKBSTranslucentFindChangeActionID, kKFCUIPrefix + 21)
// The flyout's fourth rule, below the toggle block (2026-08-04, when the blocks were rearranged and
// three separators no longer parted five blocks). MenuDef only, no ActionDef - like the three above.
DECLARE_PMID(kActionIDSpace, kKBSSeparator4ActionID, kKFCUIPrefix + 22)
// "Minimizable Find/Change": put a MINIMIZE BOX on InDesign's own Find/Change dialog, so it can be
// sent to the taskbar instead of being closed. Check-mark toggle, Windows only, OFF by default.
// The same window as the toggle above, reached the same way; what differs is that this one changes
// the window's STYLE rather than its alpha. See KBSFindChangeMinimize.cpp.
DECLARE_PMID(kActionIDSpace, kKBSMinimizableFindChangeActionID, kKFCUIPrefix + 23)
// "Remember Book Panel Placement" on the flyout (2026-09-25): a check-mark toggle. ON = InDesign's
// own Book panel is measured as it closes (and when InDesign quits) and put back where it was when
// it next appears. OFF by default. *Unlike the toggles above, flipping it WRITES ITS OWN KEY to the
// settings file at once (the user's rule) - see KBSBookPanelPlacement.h.
DECLARE_PMID(kActionIDSpace, kKBSRememberBookPanelActionID, kKFCUIPrefix + 24)
// "Reject Change" on a replaced hit row's right-click menu (2026-09-26): takes back that row's
// replacement by rejecting its tracked change - the name is the Track Changes panel's own item.
DECLARE_PMID(kActionIDSpace, kKBSRejectChangeActionID, kKFCUIPrefix + 25)
// (kKBSPrefix + 26 was "Redo" on the same menu, 2026-09-26 to 2026-09-27 - a row taken back is replaced
//  again with Replace now. Left unused rather than handed to something else.)
// "Accept All Changes by KohakuFindChange in This Document" on a document row's right-click menu
// (2026-09-27): accepts the changes signed "KohakuFindChange" in that chapter's document and leaves everybody
// else's (2026-09-29, the user's call, the author named in the item so it says so; until then it accepted
// every change, as InDesign's own "Accept All Changes in This Document" does).
DECLARE_PMID(kActionIDSpace, kKBSAcceptAllChangesActionID, kKFCUIPrefix + 27)
// The rule between Change Checked and the two scans, from and gone with them on 2026-09-27. Not reused.
//DECLARE_PMID(kActionIDSpace, kKBSSeparator5ActionID, kKBSPrefix + 28)
// "Replace" on a hit row's right-click menu (2026-09-27, the user's call): replaces that one row, with
// no prompt; the list stays a work list (KBSReplaceEngine::ReplaceHit).
DECLARE_PMID(kActionIDSpace, kKBSReplaceHitActionID, kKFCUIPrefix + 29)
// A STORY row's right-click menu (2026-09-27, the story level): Replace (its ticked rows), Reject Change,
// Accept Change (+ 41), Redo, Check All, Uncheck All - each over that story's rows.
DECLARE_PMID(kActionIDSpace, kKBSStoryReplaceActionID, kKFCUIPrefix + 30)
DECLARE_PMID(kActionIDSpace, kKBSStoryRejectActionID, kKFCUIPrefix + 31)
// A story row's Redo (2026-09-27, the user's call C): the story's rows taken back with Reject Change,
// replaced again with what Find/Change holds now - the one way to do them all without ticking them. Its
// menu name is "Replace Again (Current Find/Change Settings)" since 2026-09-29 (the user's call); the
// IDs keep "Redo".
DECLARE_PMID(kActionIDSpace, kKBSStoryRedoActionID, kKFCUIPrefix + 32)
DECLARE_PMID(kActionIDSpace, kKBSStoryCheckAllActionID, kKFCUIPrefix + 33)
DECLARE_PMID(kActionIDSpace, kKBSStoryUncheckAllActionID, kKFCUIPrefix + 34)
// "Replace" on a DOCUMENT row's right-click menu (2026-09-27, the user's call): that document's ticked
// rows, no prompt, the list stays a work list (KBSReplaceEngine::ReplaceChapter). The book row greys it.
DECLARE_PMID(kActionIDSpace, kKBSChapterReplaceActionID, kKFCUIPrefix + 35)
// "Reject Change" on a DOCUMENT row's right-click menu (2026-09-27, the user's call): every replaced row of
// that document whose tracked change is still there (KBSReplaceEngine::RejectChapter).
DECLARE_PMID(kActionIDSpace, kKBSChapterRejectActionID, kKFCUIPrefix + 36)
// "Redo" on a DOCUMENT row's right-click menu (2026-09-27, the user's call): that document's rows taken
// back, replaced again with what Find/Change holds now (KBSReplaceEngine::RedoChapter). Named "Replace
// Again (Current Find/Change Settings)" since 2026-09-29, like the story row's.
DECLARE_PMID(kActionIDSpace, kKBSChapterRedoActionID, kKFCUIPrefix + 37)
// "Show Changes by KohakuFindChange" on the flyout (2026-09-29, the user's design): the list rebuilt from
// the Track Changes records KBS signed (KBSShowChanges). Under Change Checked, a rule between them.
DECLARE_PMID(kActionIDSpace, kKBSShowChangesActionID, kKFCUIPrefix + 38)
DECLARE_PMID(kActionIDSpace, kKBSSeparator6ActionID, kKFCUIPrefix + 39)
// Accept Change by KohakuFindChange on a hit row and on a story row (2026-09-29): the twin of Reject
// Change there - the row's (or the story's rows') tracked changes accepted (KBSReplaceEngine AcceptHit /
// AcceptStory).
DECLARE_PMID(kActionIDSpace, kKBSAcceptChangeActionID, kKFCUIPrefix + 40)
DECLARE_PMID(kActionIDSpace, kKBSStoryAcceptActionID, kKFCUIPrefix + 41)
// A RUN row's right-click menu (2026-09-29, a list rebuilt from the records only): Reject / Accept the
// rows of that run in that document (KBSReplaceEngine RejectRun / AcceptRun).
DECLARE_PMID(kActionIDSpace, kKBSRunRejectActionID, kKFCUIPrefix + 42)
DECLARE_PMID(kActionIDSpace, kKBSRunAcceptActionID, kKFCUIPrefix + 43)
// "Open Find/Change..." on the flyout (2026-10-01, the user's call): InDesign's own Edit > Find/Change dialog,
// opened from the panel - with no document open too. Shortcut-assignable (the user's call, the same day).
// The first number past the split: + 44 was never spent at kKBSPrefix either.
DECLARE_PMID(kActionIDSpace, kKBSOpenFindChangeActionID, kKFCUIPrefix + 44)
// "Link the Application Bar's Search Field to This Panel" on the flyout (2026-10-02, the user's design): a
// check-mark toggle. ON = the search field of InDesign's application bar shows Find/Change's query, and Return in
// it searches with this panel (2026-10-03). OFF by default. See KBSAppBarSearchEnter.h.
DECLARE_PMID(kActionIDSpace, kKBSAppBarSearchEnterActionID, kKFCUIPrefix + 45)


// WidgetIDs:
DECLARE_PMID(kWidgetIDSpace, kKBSPanelWidgetID, kKFCUIPrefix + 0)
DECLARE_PMID(kWidgetIDSpace, kKBSStaticTextWidgetID, kKFCUIPrefix + 1)
// Result tree (Task 2): the tree list widget; the chapter-row container + its label cell; the
// hit-row container + its multi-colour text cell.
DECLARE_PMID(kWidgetIDSpace, kKBSResultListWidgetID, kKFCUIPrefix + 2)
DECLARE_PMID(kWidgetIDSpace, kKBSResultChapterNodeWidgetID, kKFCUIPrefix + 3)
DECLARE_PMID(kWidgetIDSpace, kKBSResultChapterLabelWidgetID, kKFCUIPrefix + 4)
DECLARE_PMID(kWidgetIDSpace, kKBSResultHitNodeWidgetID, kKFCUIPrefix + 5)
DECLARE_PMID(kWidgetIDSpace, kKBSResultTextWidgetID, kKFCUIPrefix + 6)
// Replace feature: the hit row's check box (hit rows only).
DECLARE_PMID(kWidgetIDSpace, kKBSResultCheckWidgetID, kKFCUIPrefix + 7)
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
DECLARE_PMID(kWidgetIDSpace, kKBSIconWidgetID, kKFCUIPrefix + 18)		// nothing run yet
DECLARE_PMID(kWidgetIDSpace, kKBSIconFoundWidgetID, kKFCUIPrefix + 19)	// something has been run
DECLARE_PMID(kWidgetIDSpace, kKBSIconChangedWidgetID, kKFCUIPrefix + 20)	// ...and it was a replace
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
// "Book Scope" toggle: ON = the whole book, OFF = what Edit > Find/Change's Search: names.
#define kKBSBookScopeMenuKey			kKBSStringPrefix "kKBSBookScopeMenuKey"
#define kKBSHidePrevChapterMenuKey		kKBSStringPrefix "kKBSHidePrevChapterMenuKey"
// "Translucent Panel" toggle: ON = the panel is drawn faint while it floats, and comes back to solid
// while the pointer is on it. English in every UI language, like the rest of the flyout.
#define kKBSTranslucentPanelMenuKey		kKBSStringPrefix "kKBSTranslucentPanelMenuKey"
// "Translucent Find/Change": the same, for InDesign's own Find/Change dialog.
#define kKBSTranslucentFindChangeMenuKey	kKBSStringPrefix "kKBSTranslucentFindChangeMenuKey"
// "Minimizable Find/Change": the same window again - a minimize box on InDesign's own dialog.
#define kKBSMinimizableFindChangeMenuKey	kKBSStringPrefix "kKBSMinimizableFindChangeMenuKey"
// "Remember Book Panel Placement": InDesign's own Book panel comes back where it was closed.
#define kKBSRememberBookPanelMenuKey	kKBSStringPrefix "kKBSRememberBookPanelMenuKey"
// "Link the Application Bar's Search Field to This Panel": Return in the application bar's search field searches here.
#define kKBSAppBarSearchEnterMenuKey	kKBSStringPrefix "kKBSAppBarSearchEnterMenuKey"
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
// "How to Use...": the operating reference. English in every UI language, like the rest of the
// flyout - there is one string table, and what KBSLoc.h switches to Japanese at run time is the model
// half's alert and Undo names (kKBSStaleResultsDocKey and friends, KBSID.h) and this page's body.
// The BODY of the reference is not here at all: it lives in KBSHowTo.cpp, because odfrc caps a single
// string at about 3.1KB and this text is several times that.
#define kKBSHowToMenuKey				kKBSStringPrefix "kKBSHowToMenuKey"
// "Open Find/Change..." (2026-10-01).
#define kKBSOpenFindChangeMenuKey		kKBSStringPrefix "kKBSOpenFindChangeMenuKey"

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
// ...and the HIT rows' menu (2026-09-26): Replace, Reject Change and Accept Change, about that one row.
// A subtree of its own because the two menus never share an item.
#define kKBSResultHitMenuName				"KBSRtMenuResultHit"
// A story row's own right-click menu (2026-09-27).
#define kKBSResultStoryMenuName				"KBSRtMenuResultStory"
// A run row's own right-click menu (2026-09-29, Show Changes by KohakuFindChange).
#define kKBSResultRunMenuName				"KBSRtMenuResultRun"

// Menu item positions:
//
// The flyout, as KFCUI.fr lays it out (the user's arrangement of 2026-08-04, reworked as items came
// and went - the scans and Save Results... left on 2026-09-27):
//    0.5 - 1.2    Open Find/Change..., Find in <scope>, Change Checked (the one command that writes
//                 to the DOCUMENTS)
//   ---- 1.3
//    1.4          Show Changes by KohakuFindChange
//   ---- 2.0
//    2.2 - 2.9    the check-mark toggles (Block 3 below)
//   ---- 3.0
//    4.0          Save Panel Settings - writes a FILE of our own
//   ---- 10.0
//   10.5 - 11.0   How to Use..., About
// Positions that a new order allowed to stay were left where they were, so only the items that
// actually moved carry new numbers.

// Block 1 - the search, then Change Checked right under it (the rule between them was removed on
// 2026-09-27, the user's call), then a rule. The scope both run on is set by Book Scope.
// Open Find/Change... leads the block (2026-10-01): open the dialog, type the query, then search.
#define kKBSOpenFindChangeMenuItemPosition	0.5
#define kKBSSearchBookMenuItemPosition		1.0
#define kKBSReplaceCheckedMenuItemPosition	1.2
// (Block 2 - the two scans at 1.3 to 1.5 - was removed on 2026-09-27.)
// Show Changes by KohakuFindChange (2026-09-29, the user's place: under Change Checked, a rule between).
#define kKBSSeparator6MenuItemPosition		1.3
#define kKBSShowChangesMenuItemPosition		1.4
#define kKBSSeparator3MenuItemPosition		2.0

// Block 3 - the check-mark toggles, 2.2 to 2.9. Book Scope leads: it is the one that decides what the
// commands in block 1 run on. Then Hide Previous Chapter; then Link the Application Bar's Search Field
// (2026-10-02), which is about the search as Book Scope is, so it stands before the window ones. The
// next three are window appearance: the two that act on InDesign's OWN Find/Change dialog first
// (translucency, then the minimize box), and this panel's own translucency. Remember Book Panel
// Placement closes the block: it is about InDesign's own Book panel, the other window the plug-in looks
// after.
#define kKBSBookScopeMenuItemPosition		2.2
#define kKBSHidePrevChapterMenuItemPosition	2.4
#define kKBSAppBarSearchEnterMenuItemPosition	2.5		// 2026-10-02: a search toggle, before the window toggles
#define kKBSTranslucentFindChangeMenuItemPosition	2.6
#define kKBSMinimizableFindChangeMenuItemPosition	2.7
#define kKBSTranslucentPanelMenuItemPosition	2.8
#define kKBSRememberBookPanelMenuItemPosition	2.9
#define kKBSSeparator4MenuItemPosition		3.0

// Block 4 - the command that writes a file of our own and touches no document: the toggles above
// (Save Panel Settings). Save Results... (5.0) was removed on 2026-09-27.
#define kKBSSavePanelSettingsMenuItemPosition	4.0
#define	kKBSSeparator1MenuItemPosition		10.0

// Block 5 - the reference items, last. No rule between them: another one right above About would
// draw two dividers with a single item between them.
#define kKBSHowToMenuItemPosition			10.5
#define kKBSAboutThisMenuItemPosition		11.0

// The book and document rows' right-click menu (2026-08-01), not the flyout: Check All / Uncheck All
// at that menu's own 1 and 2 (they were 5.0 and 6.0 while they sat under Change Checked on the flyout),
// and the document row's commands around them.
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
// One number, read from BOTH SIDES: KFCUI.fr gives it to the two row resources above and to the
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

// Initial data format version numbers
#define kKFCUIFirstMajorFormatNumber  RezLong(1)
#define kKFCUIFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKFCUICurrentMajorFormatNumber kKFCUIFirstMajorFormatNumber
#define kKFCUICurrentMinorFormatNumber kKFCUIFirstMinorFormatNumber

#endif // __KFCUIID_h__
