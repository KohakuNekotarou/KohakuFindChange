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
REGISTER_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)
// Replace feature: the hit row check box's observer.
REGISTER_PMINTERFACE(KBSResultCheckObserver, kKBSResultCheckObserverImpl)
// Result invalidation: retire a document-scope result set when its document closes.
REGISTER_PMINTERFACE(KBSCloseDocResponder, kKBSCloseDocResponderImpl)
// INSTRUMENTATION: book-close notification watcher (temporary).
REGISTER_PMINTERFACE(KBSBookWatch, kKBSBookWatchImpl)
