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
// The UI half's implementations (2026-10-01, the model/UI split - moved from KBSFactoryList.h).
// The model half's are in model/KBSFactoryList.h.
REGISTER_PMINTERFACE(KBSActionComponent, kKBSActionComponentImpl)
// Result tree (Task 2): hierarchy adapter, row widget manager, the colour cell's view + data.
REGISTER_PMINTERFACE(KBSResultListAdapter, kKBSResultListAdapterImpl)
REGISTER_PMINTERFACE(KBSResultListWidgetMgr, kKBSResultListWidgetMgrImpl)
REGISTER_PMINTERFACE(KBSColorTextView, kKBSColorTextViewImpl)
REGISTER_PMINTERFACE(KBSRowData, kKBSRowDataImpl)
// Task 3: the marker's countdown and the hit row's click (the jump).
REGISTER_PMINTERFACE(KBSMarkerExpiryTask, kKBSMarkerExpiryIdleTaskImpl)
REGISTER_PMINTERFACE(KBSResultNodeEH, kKBSResultNodeEHImpl)
// The result LIST's own handler: up / down arrows that open the row they land on. A boss in the
// .fr naming an implementation that is not registered here takes InDesign down at load time.
REGISTER_PMINTERFACE(KBSResultTreeEH, kKBSResultTreeEHImpl)
// The UI half's own startup/shutdown service (2026-10-01, the model/UI split).
REGISTER_PMINTERFACE(KBSUIStartupShutdown, kKBSUIStartupShutdownImpl)
// Replace feature: the hit row check box's observer.
REGISTER_PMINTERFACE(KBSResultCheckObserver, kKBSResultCheckObserverImpl)
// Panel tab name: writes the scope onto the tab when the panel appears.
REGISTER_PMINTERFACE(KBSPanelObserver, kKBSPanelObserverImpl)
// The panel's own view: the minimum size the panel can be dragged to.
REGISTER_PMINTERFACE(KBSPanelView, kKBSPanelViewImpl)
// The panel illustration's tooltip (the URL a click on it opens).
REGISTER_PMINTERFACE(KBSIconTip, kKBSIconTipImpl)
// "Translucent Panel": the observer that re-applies the alpha when the panel's window is rebuilt,
// and the roll-over that takes it off while the pointer is on the panel (KBSPanelAlpha.cpp).
// !The roll-over fails SILENTLY without its line here - CREATE_PMINTERFACE alone is not enough.
REGISTER_PMINTERFACE(KBSPanelVisibilityObserver, kKBSPanelVisibilityObserverImpl)
REGISTER_PMINTERFACE(KBSPanelRollOver, kKBSPanelRollOverImpl)
// "Remember Book Panel Placement": the observer on kActiveContextBoss, and the palette-manager
// service boss's two halves (KBSBookPanelPlacement.cpp). A boss in the .fr naming an implementation
// that is not registered here takes InDesign down at load time.
REGISTER_PMINTERFACE(KBSBookPanelObserver, kKBSBookPanelObserverImpl)
// "Link the Application Bar's Search Field to This Panel" (2026-10-03): the observer on kActiveContextBoss that
// makes the field follow Find/Change (KBSAppBarSearchEnter.cpp).
REGISTER_PMINTERFACE(KBSAppBarMirrorObserver, kKBSAppBarMirrorObserverImpl)
REGISTER_PMINTERFACE(KBSBookPanelServiceProvider, kKBSBookPanelServiceProviderImpl)
REGISTER_PMINTERFACE(KBSBookPanelPaletteMgrService, kKBSBookPanelPaletteMgrServiceImpl)
REGISTER_PMINTERFACE(KBSBookPanelCmdWatch, kKBSBookPanelCmdWatchImpl)
// The panel's message area (2026-09-29): its view and its data (KBSStatusTextView.cpp).
REGISTER_PMINTERFACE(KBSStatusTextView, kKBSStatusTextViewImpl)
REGISTER_PMINTERFACE(KBSStatusTextData, kKBSStatusTextDataImpl)
// The model/UI split's boundary (2026-10-01): the UI half's observer of the model half's notifications.
REGISTER_PMINTERFACE(KBSModelObserver, kKBSModelObserverImpl)
// ...and the UI services the model half asks for (the bar, the windows, the Book panel, the alert).
REGISTER_PMINTERFACE(KBSUIServices, kKBSUIServicesImpl)
