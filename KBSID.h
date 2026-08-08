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

// Company:
#define kKBSCompanyKey	"KohakuNekotarou"		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_". It is a string-table KEY, not what the user sees: both tables map it to "Kohaku Plug-Ins", so the group reads Plug-Ins > Kohaku Plug-Ins > Kohaku Find/Change (measured on the real application 2026-08-06 - this note used to claim the group was called KohakuNekotarou). KESCL/KESCM/KT use the same key and the same value, which is what puts all four under one group.
#define kKBSCompanyValue	"KohakuNekotarou"	// Company name displayed externally.

// Plug-in:
#define kKBSPluginName	"KohakuBookSearch"			// Internal name: the ID system and the .rc InternalName. NEVER change it. It is NOT what the .pln on disk is called - that is kKBSFileName below - and the .fr never spells it out either (PluginVersion carries kKBSPluginID, and the ExtraPluginInfo that documents do store is the company, the URL and the alert text), so the file on disk can be renamed without it moving. Same split as KESCM's kKESCMPluginName.
#define kKBSDisplayName	"Kohaku Find/Change"		// Display name: the About menu item, the About box, the panel and its tab, and the .rc FileDescription. THE one definition - both string tables put it under kKBSPanelTitleKey and KBSPanelTitle.cpp restores the tab from it, so the copies cannot drift apart. The slash is safe here and ONLY here: menu paths are delimited with ":" (SDKDef.h kSDKDefDelimitMenuPath) and this string is a string table VALUE, never a path segment - the paths above are built from kKBSPanelTitleKey and friends, which are prefix-number keys. Do not put a ":" or a bare "&" in it.
#define kKBSFileName	"KohakuFindChange"			// Base name of the build output: KohakuFindChange.pln, its "(KohakuFindChange Resources)" folder, and the .rc OriginalFilename. MUST match the vcxproj TargetName, which is $(ProjectName) - so the VS project carries this name too. No spaces and no slash, unlike kKBSDisplayName: this one IS a file name. Same three-way split as KESCM (kKESCMPluginName / kKESCMFileName / kKESCMDisplayName).
#define kKBSPrefixNumber	0x205698 		// Unique prefix number for this plug-in(*Must* be obtained from Adobe Developer Support).
#define kKBSRepoURL		"https://github.com/KohakuNekotarou/KohakuFindChange"	// Where this plug-in is published. Shown at the foot of the About box, the same role KESCM gives kKESCMRepoURL. If the repo is ever renamed again, this line has to follow it - nothing else in the build does.
#define kKBSVersion		"1.0.0"						// Version of this plug-in. Shows up in three places: the About box, the .rc FileVersion, and the PluginVersion resource. First Adobe Exchange submission = 1.0.0 (2026-07-30). Was kSDKDefPluginVersionString, the SDK template's own version, which said nothing about this plug-in.

// Plug-in Prefix: (please change kKBSPrefixNumber above to modify the prefix.)
#define kKBSPrefix		RezLong(kKBSPrefixNumber)				// The unique numeric prefix for all object model IDs for this plug-in.
#define kKBSStringPrefix	SDK_DEF_STRINGIZE(kKBSPrefixNumber)	// The string equivalent of the unique prefix number for  this plug-in.

// Missing plug-in: (see ExtraPluginInfo resource)
#define kKBSMissingPluginURLValue		kSDKDefPartnersStandardValue_enUS // URL displayed in Missing Plug-in dialog
#define kKBSMissingPluginAlertValue	kSDKDefMissingPluginAlertValue // Message displayed in Missing Plug-in dialog - provide a string that instructs user how to solve their missing plug-in problem

// PluginID:
DECLARE_PMID(kPlugInIDSpace, kKBSPluginID, kKBSPrefix + 0)

// ClassIDs:
DECLARE_PMID(kClassIDSpace, kKBSActionComponentBoss, kKBSPrefix + 0)
DECLARE_PMID(kClassIDSpace, kKBSPanelWidgetBoss, kKBSPrefix + 1)
// Result tree (Task 2): the tree-view list, its row (node) boss shared by chapter and hit rows,
// and the custom multi-colour text cell that highlights the matched part of a hit line.
DECLARE_PMID(kClassIDSpace, kKBSResultListWidgetBoss, kKBSPrefix + 2)
DECLARE_PMID(kClassIDSpace, kKBSResultNodeWidgetBoss, kKBSPrefix + 3)
DECLARE_PMID(kClassIDSpace, kKBSColorTextWidgetBoss, kKBSPrefix + 4)
// Task 3 (jump + red marker): the draw-event service/handler boss, the marker-expiry idle task
// boss, and the startup/shutdown service boss (retires the idle task + clears module state).
DECLARE_PMID(kClassIDSpace, kKBSDrawEventServiceBoss, kKBSPrefix + 5)
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
// Scripting: puts app.kfcStatus on the application object, so the panel's own status line can be
// read from a script (and therefore over COM). Built for verification - the panel says what a
// search or a replace did in one line, and until now the only way to read it was to look at it.
DECLARE_PMID(kClassIDSpace, kKBSScriptProviderBoss, kKBSPrefix + 10)
// The Glyph tab's replace confirmation: the dialog itself, and the widget that draws one glyph in
// the font that defines it. The dialog is the stock kDialogBoss plus our controller (the shape
// basicdialog and KESCL's offset dialog both use); the glyph widget is a generic panel whose
// IControlView is ours, built the same way the hit row's colour cell is.
DECLARE_PMID(kClassIDSpace, kKBSReplaceConfirmDialogBoss, kKBSPrefix + 11)
DECLARE_PMID(kClassIDSpace, kKBSGlyphViewWidgetBoss, kKBSPrefix + 12)
// +13 was the missing-glyph scan's own text-walker client, from the measurement phase. Removed on
// 2026-08-02 with the -2 route it existed to drive: the scan reads the composed wax and needs no
// walker at all. NOT reused - a class id that once shipped stays spent.
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 13)
// The panel's illustration: the system rollover icon button plus a tooltip of its own, so hovering
// it says where clicking it goes. Same shape as kLinksUIButtonBoss in open/components/linksui, and
// as KESCM's kKESCMIconWidgetBoss - which is where the panel this copies got it from.
DECLARE_PMID(kClassIDSpace, kKBSIconWidgetBoss, kKBSPrefix + 14)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 15)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 16)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 17)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 18)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 19)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 20)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 21)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 22)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 23)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 24)
//DECLARE_PMID(kClassIDSpace, kKBSBoss, kKBSPrefix + 25)


// InterfaceIDs:
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
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 3)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 4)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 5)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 6)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 7)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 8)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 9)
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 10)
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
// Task 3: draw-event service provider + draw handler (marker), marker-expiry idle task, the hit
// row's event handler (click -> jump), and the startup/shutdown service.
DECLARE_PMID(kImplementationIDSpace, kKBSDrawEventSrvcImpl, kKBSPrefix + 5)
DECLARE_PMID(kImplementationIDSpace, kKBSDrawEventHandlerImpl, kKBSPrefix + 6)
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
// Scripting: the provider behind app.kfcStatus (see KBSScriptProvider.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSScriptProviderImpl, kKBSPrefix + 14)
// (A commented block claiming + 5 ... + 14 were free sat here until 2026-08-02. It was left over
// from the template and every one of those numbers is taken by the lines just above, so it was an
// invitation to hand out an id twice. Removed rather than corrected - the live declarations are
// the record of what is spent.)
// The Glyph tab's replace confirmation: its dialog controller and its glyph-drawing view.
DECLARE_PMID(kImplementationIDSpace, kKBSReplaceConfirmDialogControllerImpl, kKBSPrefix + 15)
DECLARE_PMID(kImplementationIDSpace, kKBSGlyphViewImpl, kKBSPrefix + 16)
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
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 23)
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 24)
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 25)


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
DECLARE_PMID(kActionIDSpace, kKBSSeparator2ActionID, kKBSPrefix + 7)
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
DECLARE_PMID(kActionIDSpace, kKBSFindMissingGlyphsActionID, kKBSPrefix + 14)
// +15 was a second menu item that ran the same scan through the official find/change engine with
// kAnyNotDefGlyphID. Removed on 2026-08-02: that route takes InDesign down on any document holding
// overset text, by every route there is, so it must not be reachable at all. Like the numbers above
// it is NOT reused - an id that once shipped stays spent.
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 15)
// Find Overset: list the text that did not fit (2026-08-02). + 15 is a burnt number (see above),
// so this is the first genuinely unused one.
DECLARE_PMID(kActionIDSpace, kKBSFindOversetActionID, kKBSPrefix + 16)
// "Save Results...": write the result set to a tab-separated text file (2026-08-03). + 12, + 13 and
// + 15 are burnt numbers (see above), so this is the first genuinely unused one.
DECLARE_PMID(kActionIDSpace, kKBSSaveResultsActionID, kKBSPrefix + 17)
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
// back. The file holds three settings - Translucent Panel, Translucent Find/Change and Hide Previous
// Chapter. *Book Scope is deliberately not among them; the reason is in KBSPanelState.h.
DECLARE_PMID(kActionIDSpace, kKBSSavePanelSettingsActionID, kKBSPrefix + 20)
// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog. Check-mark
// toggle, Windows only, OFF by default. The dialog is found through the SDK's window list, not by
// its title, so it works whatever language InDesign is running in (KBSPanelAlpha.cpp).
DECLARE_PMID(kActionIDSpace, kKBSTranslucentFindChangeActionID, kKBSPrefix + 21)
// The flyout's fourth rule, below the toggle block (2026-08-04, when the blocks were rearranged and
// three separators no longer parted five blocks). MenuDef only, no ActionDef - like the three above.
DECLARE_PMID(kActionIDSpace, kKBSSeparator4ActionID, kKBSPrefix + 22)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 23)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 24)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 25)


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
// The Glyph tab's replace confirmation. The two glyph frames are told apart by their WidgetID -
// that is how KBSGlyphView knows which side it is drawing - so these two are not interchangeable.
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmDialogWidgetID, kKBSPrefix + 8)
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmCountWidgetID, kKBSPrefix + 9)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindGlyphWidgetID, kKBSPrefix + 10)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeGlyphWidgetID, kKBSPrefix + 11)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindFontWidgetID, kKBSPrefix + 12)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeFontWidgetID, kKBSPrefix + 13)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindUnicodeWidgetID, kKBSPrefix + 14)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeUnicodeWidgetID, kKBSPrefix + 15)
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmUnsavedWidgetID, kKBSPrefix + 16)
// UNUSED since 2026-08-01 - the "Don't show again" box it named is no longer in the dialog. Like
// the string key beside it (kKBSGlyphConfirmDontShowKey), it is kept rather than freed: a widget id
// that once shipped stays spent, so a saved workspace referring to it cannot bind to something else.
// ! Until 2026-08-06 the dialog controller went on stamping a label into this id every time the
// confirmation opened. It was harmless - SetTextControlData looks the widget up first and there is
// nothing to find - but it made the id read as live. Removed; nothing writes it now.
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmDontShowWidgetID, kKBSPrefix + 17)
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
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmMessageWidgetID, kKBSPrefix + 21)
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmGlyphBlockWidgetID, kKBSPrefix + 22)
// RETIRED 2026-08-05, NOT TO BE REUSED: the "save after replace" box and the line under it, which
// stood on the confirmation from 2026-08-02 until the feature was removed. A widget id that comes
// back on a DIFFERENT control is read by a saved workspace as the old one; the numbers cost nothing.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmSaveWidgetID, kKBSPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmSaveNoteWidgetID, kKBSPrefix + 24)
// "Take care when you are replacing across several chapters", under the line above. The GLYPH layout
// only: the Text / GREP one carries the same sentence inside its single wrapped block, assembled by
// KBSActionComponent, which is why there is no second widget for it there.
DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmCareWidgetID, kKBSPrefix + 25)
// "If the text has been edited since the search..." - the GLYPH layout's copy of that warning, live
// for one afternoon on 2026-08-08 and off both layouts by the end of it: the question can only be
// answered where the chapters are OPEN, so it moved to the replace itself (see the string keys
// below). Commented out rather than deleted, and its number not reused, for the reason given above
// the two before it.
//DECLARE_PMID(kWidgetIDSpace, kKBSReplaceConfirmEditedWidgetID, kKBSPrefix + 26)
// The glyph dialog's three fixed labels ("Find", the arrow, "Change to"). They carry ids so the
// runtime language switch (KBSLoc) can restamp them Japanese - the jaJP string table that used
// to do it is gone (2026-08-05).
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindLabelWidgetID, kKBSPrefix + 27)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmArrowWidgetID, kKBSPrefix + 28)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeLabelWidgetID, kKBSPrefix + 29)
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 30)


// "About Plug-ins" sub-menu:
#define kKBSAboutMenuKey			kKBSStringPrefix "kKBSAboutMenuKey"
#define kKBSAboutMenuPath		kSDKDefStandardAboutMenuPath kKBSCompanyKey

// A "Plug-ins" sub-menu path (kKBSPluginsMenuKey / kKBSPluginsMenuPath) stood here from the Dolly
// template until 2026-08-06. The .fr never used either one: the panel's own entry under that menu
// is kKBSPanelPluginsMenuPath below, which spells the leaf with kKBSPanelTitleKey, and the flyout
// hangs off kKBSTargetMenuPath. The string value that went with the key is gone from KBS_enUS.fr
// too. Menu path macros are not ids and nothing outside this plug-in can name them, so there is
// nothing to reserve.

// Menu item keys:
#define kKBSSearchBookMenuKey			kKBSStringPrefix "kKBSSearchBookMenuKey"
// "Find Missing Glyphs": scan for notdef glyphs rather than for the Find/Change query.
#define kKBSFindMissingGlyphsMenuKey	kKBSStringPrefix "kKBSFindMissingGlyphsMenuKey"
// "Find Overset": list the text that did not fit rather than searching for anything.
#define kKBSFindOversetMenuKey			kKBSStringPrefix "kKBSFindOversetMenuKey"
// "Book Scope" toggle: ON = the whole book, OFF = the front document.
#define kKBSBookScopeMenuKey			kKBSStringPrefix "kKBSBookScopeMenuKey"
#define kKBSHidePrevChapterMenuKey		kKBSStringPrefix "kKBSHidePrevChapterMenuKey"
// "Translucent Panel" toggle: ON = the panel is drawn faint while it floats, and comes back to solid
// while the pointer is on it. English in both string tables, like the rest of the flyout.
#define kKBSTranslucentPanelMenuKey		kKBSStringPrefix "kKBSTranslucentPanelMenuKey"
// "Translucent Find/Change": the same, for InDesign's own Find/Change dialog.
#define kKBSTranslucentFindChangeMenuKey	kKBSStringPrefix "kKBSTranslucentFindChangeMenuKey"
// "Save Panel Settings": write the settings above to a file of our own, read back at startup.
#define kKBSSavePanelSettingsMenuKey	kKBSStringPrefix "kKBSSavePanelSettingsMenuKey"
// Replace feature menu item keys.
#define kKBSReplaceCheckedMenuKey		kKBSStringPrefix "kKBSReplaceCheckedMenuKey"
#define kKBSCheckAllMenuKey				kKBSStringPrefix "kKBSCheckAllMenuKey"
#define kKBSUncheckAllMenuKey			kKBSStringPrefix "kKBSUncheckAllMenuKey"
// "Save Results...": write what the panel is showing to a text file. The trailing "..." is the
// platform convention for a command that opens a dialog before it does anything.
#define kKBSSaveResultsMenuKey			kKBSStringPrefix "kKBSSaveResultsMenuKey"
// "How to Use...": the operating reference. English in both string tables, like the rest of the
// flyout - only the replace prompts are translated (see kKBSConfirmReplaceOneKey). The BODY of the
// reference is not here at all: it lives in KBSHowTo.cpp, because odfrc caps a single string at
// about 3.1KB and this text is several times that.
#define kKBSHowToMenuKey				kKBSStringPrefix "kKBSHowToMenuKey"

// Other StringKeys:
#define kKBSAboutBoxStringKey	kKBSStringPrefix "kKBSAboutBoxStringKey"
#define kKBSPanelTitleKey					kKBSStringPrefix	"kKBSPanelTitleKey"
// Panel entry under the Plug-Ins menu, like KESCL/KESCM. The parts are KEYS; on the running menu
// tree they read Plug-Ins > Kohaku Plug-Ins > Kohaku Find/Change (measured 2026-08-06):
#define kKBSPanelPluginsMenuPath			kSDKDefPlugInsStandardMenuPath kKBSCompanyKey kSDKDefDelimitMenuPath kKBSPanelTitleKey
#define kKBSPanelPluginsMenuPosition		101.0
#define kKBSStaticTextKey kKBSStringPrefix	"kKBSStaticTextKey"
#define kKBSInternalPopupMenuNameKey kKBSStringPrefix	"kKBSInternalPopupMenuNameKey"
#define kKBSTargetMenuPath kKBSInternalPopupMenuNameKey

// The result rows' right-click context menu (2026-08-01, user request): the popup's internal name.
// KBSResultNodeEH::RButtonDn pops the MenuDef subtree of this name at the cursor with
// IMenuManager::HandlePopupMenu - the same machinery as the real Links / Layers panel row menus, and
// as KESCL's report rows (kKESCLReportRowMenuName). The root name is never displayed, so it is a
// plain literal rather than a translated key.
#define kKBSResultRowMenuName				"KBSRtMenuResultRow"

// The Change Checked confirmation prompt. The ENGLISH wording lives in KBS_enUS.fr under these
// keys; the JAPANESE lives in KBSLoc.h and is switched in at run time by UI language (the jaJP
// string table is gone - 2026-08-05). The keys carry the plug-in's prefix number, so they cannot
// collide with a built-in phrase and come back as somebody else's translation.
//
// Singular and plural are separate keys rather than one "hit(s)": that reads like a placeholder
// nobody filled in, and languages that inflect differently cannot be built from it at all.
#define kKBSConfirmReplaceOneKey	kKBSStringPrefix "kKBSConfirmReplaceOneKey"
#define kKBSConfirmReplaceManyKey	kKBSStringPrefix "kKBSConfirmReplaceManyKey"
#define kKBSConfirmFindKey			kKBSStringPrefix "kKBSConfirmFindKey"
#define kKBSConfirmChangeToKey		kKBSStringPrefix "kKBSConfirmChangeToKey"
// Shown in place of the change string when it is empty - which is a legitimate request (delete
// every match), not a mistake, so it is spelled out rather than left blank. NOT used when a Change
// Format is set: there an empty box changes the FORMAT and leaves the text alone, and this wording
// would state the opposite of what happens.
#define kKBSConfirmEmptyReplaceKey	kKBSStringPrefix "kKBSConfirmEmptyReplaceKey"
// The dialog's own name for its format pane, appended to whichever side has one set: "cat  + Find
// Format", or on its own when the box beside it is empty. WHAT is set follows in parentheses, in
// InDesign's own words and the user's own language: the attributes describe THEMSELVES through
// IAttrReport::AppendDescription - the call behind the Settings line in Style Options - and the two
// styles are added by their full path (KBSSearchEngine::DescribeFormatSetting).
//
// ! Until 2026-08-04 this note read "WHAT is set is not named: TextAttrID.h declares 222 attribute
// bosses and the SDK has no ClassID-to-name call". That was asking the wrong object. The prompt
// shortens the list; the saved report writes it in full.
//
// The two places a format can be set - the attribute list and the style fields beside it - are in
// HasFormatSet, which moved to KBSSearchEngine.cpp the same day.
#define kKBSConfirmFindFormatKey	kKBSStringPrefix "kKBSConfirmFindFormatKey"
#define kKBSConfirmChangeFormatKey	kKBSStringPrefix "kKBSConfirmChangeFormatKey"
// ***** THE EDITED-CHAPTER WARNING - AND IT IS NOT PART OF THIS PROMPT ANY MORE. ***** Since
// 2026-08-05 a replace does NOT check that the match it is about to rewrite is still the one the row
// was found at: the chapter is walked again and the Nth match takes the Nth checked row's
// replacement. Edit the text between searching and replacing and that numbering points somewhere
// else.
//
// A standing conditional disclaimer said so on every prompt until 2026-08-08 - "IF the text has been
// edited" - because nothing knew whether it had been. KBSEditStamp knows, so these state a FACT and
// appear only when there is one.
//
// ***** WHERE THEY ARE SAID CHANGED THE SAME DAY (user's decision). ***** They were a line ON this
// prompt for one afternoon, and that line could only ask about the chapters that happened to be OPEN
// when it was drawn: the counters need the document open, and a book search closes every chapter as
// it finishes with it. So the question moved to the one point where every chapter can be asked - the
// moment the replace itself opens each one - and is now a modal alert per chapter, with Cancel
// stopping the whole run (KBSReplaceEngine::AskEditedChapter). The prompt says nothing about it.
//
// Openings that name what was edited, plus two lines that are the same whichever it was: what it can
// cost, and what Cancel does.
//   - ...Many is UNUSED since the move: the alert names ONE chapter because it is asked once per
//     chapter. Kept because the wording is right and the plural is what a summary would need.
//
// Translated, and for the same reason the prompt is: this is where the user authorises a rewrite of
// their own text. The status line that reports the outcome stays English.
#define kKBSConfirmEditedDocKey		kKBSStringPrefix "kKBSConfirmEditedDocKey"
#define kKBSConfirmEditedOneKey		kKBSStringPrefix "kKBSConfirmEditedOneKey"
#define kKBSConfirmEditedManyKey	kKBSStringPrefix "kKBSConfirmEditedManyKey"
#define kKBSConfirmEditedTailKey	kKBSStringPrefix "kKBSConfirmEditedTailKey"
#define kKBSConfirmEditedCancelAllKey	kKBSStringPrefix "kKBSConfirmEditedCancelAllKey"
// The closing line: what the run LEAVES BEHIND.
//
// ***** ONE key since 2026-08-07 (user's wording). ***** It was two - singular and plural, split by
// how many chapters would be written to, because languages that inflect cannot build both from one
// string. The new sentence states the case rather than counting it ("when a replace covers several
// documents in a book..."), so there is no number in it and nothing left to inflect. With the count
// went the reason for KBSReplaceConfirmDialog::Ask to be told it at all.
//
// It used to end by promising the undo ("a single undo puts it back"). Dropped on 2026-08-05 (user's
// call): the sentence is about what the user is LEFT WITH, and a promise about undo in the same
// breath softens it. The behaviour it described is unchanged and still worth knowing here - the
// whole replace is ONE undo step however many chapters it touches, because KBSReplaceEngine wraps
// the entire run in a single command sequence. (Until 2026-07-28 this said "one undo step per
// chapter", which was both wrong and dangerous: with a sequence per chapter, undoing one document
// silently stripped the step from the others without reverting their text.)
#define kKBSConfirmUnsavedKey	kKBSStringPrefix "kKBSConfirmUnsavedKey"
// The warning that closes the prompt, in the user's own words. Shown WHATEVER the count.
//
// ***** JUST "take care" since 2026-08-07 (user's wording). ***** It used to name the condition too
// - "take care when you are replacing across several chapters" - which is why the key was called
// SeveralChapters. The line ABOVE it now states that condition in full (kKBSConfirmUnsavedKey:
// "when a replace covers several documents in a book..."), so saying it twice only made the closing
// warning longer than the thing it warns about. Renamed with the wording rather than left standing
// as a key that names a condition its string no longer carries.
#define kKBSConfirmCareKey	kKBSStringPrefix "kKBSConfirmCareKey"

// The Glyph tab's own confirmation, the one that draws the glyphs. The count and the closing line
// are shared with the plain alert above - the same sentences, on a different screen - so the only
// new strings are the labels around the two glyph frames.
#define kKBSGlyphConfirmFindLabelKey	kKBSStringPrefix "kKBSGlyphConfirmFindLabelKey"
#define kKBSGlyphConfirmChangeLabelKey	kKBSStringPrefix "kKBSGlyphConfirmChangeLabelKey"
#define kKBSGlyphConfirmArrowKey		kKBSStringPrefix "kKBSGlyphConfirmArrowKey"
// UNUSED since 2026-08-01, when the "Don't show again" box came off both confirmations (it made a
// destructive rewrite suppressible with one tick, and the only way back is Preferences > General >
// Reset All Warning Dialogs). Left in place, with its entry in the string table, so that putting
// the box back is a resource change rather than a translation round. Nothing reads it today - true
// since 2026-08-06, when the controller stopped stamping it into a widget that does not exist.
#define kKBSGlyphConfirmDontShowKey		kKBSStringPrefix "kKBSGlyphConfirmDontShowKey"

// RETIRED 2026-08-05 with the feature they belonged to: the "save after replace" box, its note, and
// the extra warning that went up when the box was ticked. They stood here from 2026-08-02. Removed
// from both string tables as well - unlike kKBSGlyphConfirmDontShowKey above, which is a box that
// could come back, these describe a run this plug-in no longer performs.
//#define kKBSSaveAfterReplaceKey		kKBSStringPrefix "kKBSSaveAfterReplaceKey"
//#define kKBSSaveAfterReplaceNoteKey	kKBSStringPrefix "kKBSSaveAfterReplaceNoteKey"
//#define kKBSSaveAfterReplaceWarningKey	kKBSStringPrefix "kKBSSaveAfterReplaceWarningKey"

// Menu item positions:
//
// The flyout reads in five blocks, parted by four rules (the user's arrangement, 2026-08-04):
//    1.0 - 1.2    the three commands that RUN something over the current scope
//   ---- 1.3
//    1.6          Change Checked - the one command that writes to the DOCUMENTS
//   ---- 2.0
//    2.2 - 2.8    the four check-mark toggles
//   ---- 3.0
//    4.0 - 5.0    the two commands that write a FILE of our own
//   ---- 10.0
//   10.5 - 11.0   the two reference items
// Positions that the new order allowed to stay were left where they were, so only the items that
// actually moved carry new numbers.

// Block 1 - the three "run something over the scope" commands, in the order they were added. The
// scope they run on is set by Book Scope, two blocks below.
#define kKBSSearchBookMenuItemPosition		1.0
#define kKBSFindMissingGlyphsMenuItemPosition	1.1
#define kKBSFindOversetMenuItemPosition		1.2
#define kKBSSeparator2MenuItemPosition		1.3

// Block 2 - Change Checked, alone between two rules. It is the only item on this menu that rewrites
// the documents, so it is kept apart from both the read-only runs above it and the toggles below,
// where it cannot be reached by a slip of the pointer.
#define kKBSReplaceCheckedMenuItemPosition	1.6
#define kKBSSeparator3MenuItemPosition		2.0

// Block 3 - the four check-mark toggles. Book Scope leads: it is the one that decides what the
// commands in block 1 run on. Then Hide Previous Chapter, and the two Translucent toggles last -
// one idea applied to two windows, so they read as a pair (Find/Change first, this panel second).
#define kKBSBookScopeMenuItemPosition		2.2
#define kKBSHidePrevChapterMenuItemPosition	2.4
#define kKBSTranslucentFindChangeMenuItemPosition	2.6
#define kKBSTranslucentPanelMenuItemPosition	2.8
#define kKBSSeparator4MenuItemPosition		3.0

// Block 4 - the two commands that write a file of our own and touch no document: the toggles above
// (Save Panel Settings), and the result set (Save Results...).
#define kKBSSavePanelSettingsMenuItemPosition	4.0
#define kKBSSaveResultsMenuItemPosition		5.0
#define	kKBSSeparator1MenuItemPosition		10.0

// Block 5 - the reference items, the placement KESCM uses (its own is Sep2 9.95 / How to Use 10 /
// About 12). No rule between them: another one right above About would draw two dividers with a
// single item between them.
#define kKBSHowToMenuItemPosition			10.5
#define kKBSAboutThisMenuItemPosition		11.0

// Check All / Uncheck All are the two items of the RESULT ROWS' right-click menu (2026-08-01), not
// of the flyout, so their positions are that menu's own 1 and 2 - they were 5.0 and 6.0 while they
// sat under Change Checked on the flyout.
#define kKBSCheckAllMenuItemPosition		1.0
#define kKBSUncheckAllMenuItemPosition		2.0


// View (kViewRsrcType) resource IDs for the result tree's row widgets (Task 2). Offset from the
// panel's own resource ID (kSDKDefPanelResourceID), like the KESCL report panel's row resources.
#define kKBSResultChapterNodeWidgetRsrcID	(kSDKDefPanelResourceID + 20)
#define kKBSResultHitNodeWidgetRsrcID		(kSDKDefPanelResourceID + 21)

// PNG resource IDs. Their own number space (PNGA/PNGR), so they do not have to dodge the view
// resource ids above. 1001 is where KESCM and KESCL start theirs.
#define kKBSIconResID			1001	// the illustration shown before anything has been run
#define kKBSPaletteIconResID	1002	// the small dock-tab icon, shown when the panel is collapsed
#define kKBSIconFoundResID		1003	// the illustration shown once something HAS been run
#define kKBSIconChangedResID	1004	// ...and the one shown once a replace has written something

// Script element IDs. These name the scripting DEFINITIONS (the entries in KBS.fr's
// VersionedScriptElementInfo); the four-character ScriptIDs a script engine actually matches on live
// in KBSScriptingDefs.h.
DECLARE_PMID(kScriptInfoIDSpace, kKBSStatusPropertyScriptElement, kKBSPrefix + 0)
DECLARE_PMID(kScriptInfoIDSpace, kKBSResultsPropertyScriptElement, kKBSPrefix + 1)

// Initial data format version numbers
#define kKBSFirstMajorFormatNumber  RezLong(1)
#define kKBSFirstMinorFormatNumber  RezLong(0)

// Data format version numbers for the PluginVersion resource 
#define kKBSCurrentMajorFormatNumber kKBSFirstMajorFormatNumber
#define kKBSCurrentMinorFormatNumber kKBSFirstMinorFormatNumber

#endif // __KBSID_h__
