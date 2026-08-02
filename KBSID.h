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
#define kKBSCompanyKey	"KohakuNekotarou"		// Company name used internally for menu paths and the like. Must be globally unique, only A-Z, 0-9, space and "_". (Matches KESCL/KESCM so all group under Plug-Ins > KohakuNekotarou.)
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
// Scripting: puts app.kbsStatus on the application object, so the panel's own status line can be
// read from a script (and therefore over COM). Built for verification - the panel says what a
// search or a replace did in one line, and until now the only way to read it was to look at it.
DECLARE_PMID(kClassIDSpace, kKBSScriptProviderBoss, kKBSPrefix + 10)
// The Glyph tab's replace confirmation: the dialog itself, and the widget that draws one glyph in
// the font that defines it. The dialog is the stock kDialogBoss plus our controller (the shape
// basicdialog and KESCL's offset dialog both use); the glyph widget is a generic panel whose
// IControlView is ours, built the same way the hit row's colour cell is.
DECLARE_PMID(kClassIDSpace, kKBSGlyphConfirmDialogBoss, kKBSPrefix + 11)
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
//DECLARE_PMID(kInterfaceIDSpace, IID_IKBSINTERFACE, kKBSPrefix + 2)
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
// Scripting: the provider behind app.kbsStatus (see KBSScriptProvider.cpp).
DECLARE_PMID(kImplementationIDSpace, kKBSScriptProviderImpl, kKBSPrefix + 14)
// (A commented block claiming + 5 ... + 14 were free sat here until 2026-08-02. It was left over
// from the template and every one of those numbers is taken by the lines just above, so it was an
// invitation to hand out an id twice. Removed rather than corrected - the live declarations are
// the record of what is spent.)
// The Glyph tab's replace confirmation: its dialog controller and its glyph-drawing view.
DECLARE_PMID(kImplementationIDSpace, kKBSGlyphConfirmDialogControllerImpl, kKBSPrefix + 15)
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
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 20)
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 21)
//DECLARE_PMID(kImplementationIDSpace, kKBSImpl, kKBSPrefix + 22)
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
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 17)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 18)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 19)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 20)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 21)
//DECLARE_PMID(kActionIDSpace, kKBSActionID, kKBSPrefix + 22)
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
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmDialogWidgetID, kKBSPrefix + 8)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmCountWidgetID, kKBSPrefix + 9)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindGlyphWidgetID, kKBSPrefix + 10)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeGlyphWidgetID, kKBSPrefix + 11)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindFontWidgetID, kKBSPrefix + 12)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeFontWidgetID, kKBSPrefix + 13)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmFindUnicodeWidgetID, kKBSPrefix + 14)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmChangeUnicodeWidgetID, kKBSPrefix + 15)
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmUnsavedWidgetID, kKBSPrefix + 16)
// UNUSED since 2026-08-01 - the "Don't show again" box it named is no longer in the dialog. Like
// the string key beside it (kKBSGlyphConfirmDontShowKey), it is kept rather than freed: a widget id
// that once shipped stays spent, so a saved workspace referring to it cannot bind to something else.
DECLARE_PMID(kWidgetIDSpace, kKBSGlyphConfirmDontShowWidgetID, kKBSPrefix + 17)
// The panel's illustrations, stacked at ONE frame to the right of the status message - exactly one
// is visible and enabled at a time (KBSPanelIcon picks, and it is the ONLY place that knows which
// state each belongs to). Adding another is one id here, one resource below, one row in kIcons.
DECLARE_PMID(kWidgetIDSpace, kKBSIconWidgetID, kKBSPrefix + 18)		// nothing run yet
DECLARE_PMID(kWidgetIDSpace, kKBSIconFoundWidgetID, kKBSPrefix + 19)	// something has been run
DECLARE_PMID(kWidgetIDSpace, kKBSIconChangedWidgetID, kKBSPrefix + 20)	// ...and it was a replace
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 21)
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 22)
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 23)
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 24)
//DECLARE_PMID(kWidgetIDSpace, kKBSWidgetID, kKBSPrefix + 25)


// "About Plug-ins" sub-menu:
#define kKBSAboutMenuKey			kKBSStringPrefix "kKBSAboutMenuKey"
#define kKBSAboutMenuPath		kSDKDefStandardAboutMenuPath kKBSCompanyKey

// "Plug-ins" sub-menu:
#define kKBSPluginsMenuKey 		kKBSStringPrefix "kKBSPluginsMenuKey"
#define kKBSPluginsMenuPath		kSDKDefPlugInsStandardMenuPath kKBSCompanyKey kSDKDefDelimitMenuPath kKBSPluginsMenuKey

// Menu item keys:
#define kKBSSearchBookMenuKey			kKBSStringPrefix "kKBSSearchBookMenuKey"
// "Find Missing Glyphs": scan for notdef glyphs rather than for the Find/Change query.
#define kKBSFindMissingGlyphsMenuKey	kKBSStringPrefix "kKBSFindMissingGlyphsMenuKey"
// "Find Overset": list the text that did not fit rather than searching for anything.
#define kKBSFindOversetMenuKey			kKBSStringPrefix "kKBSFindOversetMenuKey"
// "Book Scope" toggle: ON = the whole book, OFF = the front document.
#define kKBSBookScopeMenuKey			kKBSStringPrefix "kKBSBookScopeMenuKey"
#define kKBSHidePrevChapterMenuKey		kKBSStringPrefix "kKBSHidePrevChapterMenuKey"
// Replace feature menu item keys.
#define kKBSReplaceCheckedMenuKey		kKBSStringPrefix "kKBSReplaceCheckedMenuKey"
#define kKBSCheckAllMenuKey				kKBSStringPrefix "kKBSCheckAllMenuKey"
#define kKBSUncheckAllMenuKey			kKBSStringPrefix "kKBSUncheckAllMenuKey"

// Other StringKeys:
#define kKBSAboutBoxStringKey	kKBSStringPrefix "kKBSAboutBoxStringKey"
#define kKBSPanelTitleKey					kKBSStringPrefix	"kKBSPanelTitleKey"
// Panel entry under the Plug-Ins menu (Plug-Ins > KohakuNekotarou > KohakuBookSearch), like KESCL/KESCM:
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

// The Change Checked confirmation prompt. Its wording lives in the string tables (KBS_enUS.fr /
// KBS_jaJP.fr) instead of being built from C++ literals, so a Japanese InDesign shows a Japanese
// prompt - KBS.fr already routes k_jaJP to the jaJP table. The keys carry the plug-in's prefix
// number, so they cannot collide with a built-in phrase and come back as somebody else's
// translation.
//
// Singular and plural are separate keys rather than one "hit(s)": that reads like a placeholder
// nobody filled in, and languages that inflect differently cannot be built from it at all.
#define kKBSConfirmReplaceOneKey	kKBSStringPrefix "kKBSConfirmReplaceOneKey"
#define kKBSConfirmReplaceManyKey	kKBSStringPrefix "kKBSConfirmReplaceManyKey"
#define kKBSConfirmFindKey			kKBSStringPrefix "kKBSConfirmFindKey"
#define kKBSConfirmChangeToKey		kKBSStringPrefix "kKBSConfirmChangeToKey"
// Shown in place of the change string when it is empty - which is a legitimate request (delete
// every match), not a mistake, so it is spelled out rather than left blank.
#define kKBSConfirmEmptyReplaceKey	kKBSStringPrefix "kKBSConfirmEmptyReplaceKey"
// The closing line, split by how many chapters will be written to. The whole replace is ONE undo
// step however many chapters it touches - KBSReplaceEngine wraps the entire run in a single command
// sequence, which is what lets one Ctrl+Z put all of it back. (Until 2026-07-28 this said "one undo
// step per chapter", which was both wrong and dangerous: with a sequence per chapter, undoing one
// document silently stripped the step from the others without reverting their text.) The two keys
// differ only in singular/plural, which languages that inflect cannot build from one string.
#define kKBSConfirmUnsavedOneKey	kKBSStringPrefix "kKBSConfirmUnsavedOneKey"
#define kKBSConfirmUnsavedManyKey	kKBSStringPrefix "kKBSConfirmUnsavedManyKey"

// The Glyph tab's own confirmation, the one that draws the glyphs. The count and the closing line
// are shared with the plain alert above - the same sentences, on a different screen - so the only
// new strings are the labels around the two glyph frames.
#define kKBSGlyphConfirmFindLabelKey	kKBSStringPrefix "kKBSGlyphConfirmFindLabelKey"
#define kKBSGlyphConfirmChangeLabelKey	kKBSStringPrefix "kKBSGlyphConfirmChangeLabelKey"
#define kKBSGlyphConfirmArrowKey		kKBSStringPrefix "kKBSGlyphConfirmArrowKey"
// UNUSED since 2026-08-01, when the "Don't show again" box came off both confirmations (it made a
// destructive rewrite suppressible with one tick, and the only way back is Preferences > General >
// Reset All Warning Dialogs). Left in place, with its entry in both string tables, so that putting
// the box back is a resource change rather than a translation round; nothing reads it today.
#define kKBSGlyphConfirmDontShowKey		kKBSStringPrefix "kKBSGlyphConfirmDontShowKey"

// Menu item positions:

// The scope toggle and its separator sit between the search command (1.0) and Hide Previous
// Chapter (2.0), so neither existing position had to move.
#define kKBSSeparator2MenuItemPosition		1.3
#define kKBSBookScopeMenuItemPosition		1.6

#define kKBSSearchBookMenuItemPosition		1.0
// The glyph scan is the search command's sibling, so it sits directly under it - above the scope
// toggle's separator (1.3), which keeps the two "run something" commands together.
#define kKBSFindMissingGlyphsMenuItemPosition	1.1
// The overset scan is the third "run something over the scope" command, so it follows the other two
// and still sits above the scope toggle's separator (1.3).
#define kKBSFindOversetMenuItemPosition		1.2
#define kKBSHidePrevChapterMenuItemPosition	2.0

// The replace block sits below the existing toggles, above the About separator (10.0).
#define kKBSSeparator3MenuItemPosition		3.0
#define kKBSReplaceCheckedMenuItemPosition	4.0
#define	kKBSSeparator1MenuItemPosition		10.0
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
