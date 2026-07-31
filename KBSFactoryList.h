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
REGISTER_PMINTERFACE(KBSActionComponent, kKBSActionComponentImpl)
// Result tree (Task 2): hierarchy adapter, row widget manager, the colour cell's view + data.
REGISTER_PMINTERFACE(KBSResultListAdapter, kKBSResultListAdapterImpl)
REGISTER_PMINTERFACE(KBSResultListWidgetMgr, kKBSResultListWidgetMgrImpl)
REGISTER_PMINTERFACE(KBSColorTextView, kKBSColorTextViewImpl)
REGISTER_PMINTERFACE(KBSRowData, kKBSRowDataImpl)
// Task 3: jump + red marker + startup/shutdown.
REGISTER_PMINTERFACE(KBSDrawEventSrvc, kKBSDrawEventSrvcImpl)
REGISTER_PMINTERFACE(KBSDrawEventHandler, kKBSDrawEventHandlerImpl)
REGISTER_PMINTERFACE(KBSMarkerExpiryTask, kKBSMarkerExpiryIdleTaskImpl)
REGISTER_PMINTERFACE(KBSResultNodeEH, kKBSResultNodeEHImpl)
// The result LIST's own handler: up / down arrows that open the row they land on. A boss in the
// .fr naming an implementation that is not registered here takes InDesign down at load time.
REGISTER_PMINTERFACE(KBSResultTreeEH, kKBSResultTreeEHImpl)
REGISTER_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)
// Replace feature: the hit row check box's observer.
REGISTER_PMINTERFACE(KBSResultCheckObserver, kKBSResultCheckObserverImpl)
// Result invalidation: retire a document-scope result set when its document closes.
REGISTER_PMINTERFACE(KBSCloseDocResponder, kKBSCloseDocResponderImpl)
// Result invalidation: retire a book-scope result set when its book closes.
REGISTER_PMINTERFACE(KBSBookWatch, kKBSBookWatchImpl)
// Panel tab name: writes the scope onto the tab when the panel appears.
REGISTER_PMINTERFACE(KBSPanelObserver, kKBSPanelObserverImpl)
// Scripting: app.kbsStatus - the panel's status line, readable from a script or over COM.
REGISTER_PMINTERFACE(KBSScriptProvider, kKBSScriptProviderImpl)
// The Glyph tab's replace confirmation: the dialog's controller and the view that draws one glyph.
REGISTER_PMINTERFACE(KBSGlyphConfirmDialogController, kKBSGlyphConfirmDialogControllerImpl)
REGISTER_PMINTERFACE(KBSGlyphView, kKBSGlyphViewImpl)
