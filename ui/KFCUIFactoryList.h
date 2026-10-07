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
// The UI half's implementations. The model half's are in model/KFCFactoryList.h.
REGISTER_PMINTERFACE(KFCActionComponent, kKFCActionComponentImpl)
// Result tree: hierarchy adapter, row widget manager, the colour cell's view + data.
REGISTER_PMINTERFACE(KFCResultListAdapter, kKFCResultListAdapterImpl)
REGISTER_PMINTERFACE(KFCResultListWidgetMgr, kKFCResultListWidgetMgrImpl)
REGISTER_PMINTERFACE(KFCColorTextView, kKFCColorTextViewImpl)
REGISTER_PMINTERFACE(KFCRowData, kKFCRowDataImpl)
// The jump marker's countdown and the hit row's click (the jump).
REGISTER_PMINTERFACE(KFCMarkerExpiryTask, kKFCMarkerExpiryIdleTaskImpl)
REGISTER_PMINTERFACE(KFCResultNodeEH, kKFCResultNodeEHImpl)
// The result LIST's own handler: up / down arrows that open the row they land on. A boss in the
// .fr naming an implementation that is not registered here takes InDesign down at load time.
REGISTER_PMINTERFACE(KFCResultTreeEH, kKFCResultTreeEHImpl)
// The UI half's own startup/shutdown service.
REGISTER_PMINTERFACE(KFCUIStartupShutdown, kKFCUIStartupShutdownImpl)
// Replace feature: the hit row check box's observer.
// The panel's observer (KFCPanelTitle.cpp): the tab's name, the layout, the picture and the message as the panel
// appears, the picture's click, and the tab's name following the Find/Change settings and the selection.
REGISTER_PMINTERFACE(KFCPanelObserver, kKFCPanelObserverImpl)
// The panel's own view: a floor under how small it can be dragged, and a height rounded to whole result rows.
REGISTER_PMINTERFACE(KFCPanelView, kKFCPanelViewImpl)
// The panel illustration's tooltip (the URL a click on it opens).
REGISTER_PMINTERFACE(KFCIconTip, kKFCIconTipImpl)
// "Translucent Panel": the observer that re-applies the alpha when the panel's window is rebuilt,
// and the roll-over that takes it off while the pointer is on the panel (KFCPanelAlpha.cpp).
// !The roll-over fails SILENTLY without its line here - CREATE_PMINTERFACE alone is not enough.
REGISTER_PMINTERFACE(KFCPanelVisibilityObserver, kKFCPanelVisibilityObserverImpl)
REGISTER_PMINTERFACE(KFCPanelRollOver, kKFCPanelRollOverImpl)
// "Remember Book Panel Placement": the observer on kActiveContextBoss, the palette-manager service
// boss's two halves and the command interceptor (KFCBookPanelPlacement.cpp) - the observer here, the
// other three after the next line. A boss in the .fr naming an implementation that is not registered
// here takes InDesign down at load time.
REGISTER_PMINTERFACE(KFCBookPanelObserver, kKFCBookPanelObserverImpl)
// "Link the Application Bar's Search Field to This Panel": the observer on kActiveContextBoss that
// makes the field follow Find/Change (KFCAppBarSearchEnter.cpp).
REGISTER_PMINTERFACE(KFCAppBarMirrorObserver, kKFCAppBarMirrorObserverImpl)
// ...Remember Book Panel Placement again: the palette-manager service boss's two halves and the command
// interceptor.
REGISTER_PMINTERFACE(KFCBookPanelServiceProvider, kKFCBookPanelServiceProviderImpl)
REGISTER_PMINTERFACE(KFCBookPanelPaletteMgrService, kKFCBookPanelPaletteMgrServiceImpl)
REGISTER_PMINTERFACE(KFCBookPanelCmdWatch, kKFCBookPanelCmdWatchImpl)
// The panel's message area: its view and its data (KFCStatusTextView.cpp).
REGISTER_PMINTERFACE(KFCStatusTextView, kKFCStatusTextViewImpl)
REGISTER_PMINTERFACE(KFCStatusTextData, kKFCStatusTextDataImpl)
// The model/UI split's boundary: the UI half's observer of the model half's notifications.
REGISTER_PMINTERFACE(KFCModelObserver, kKFCModelObserverImpl)
// ...and the UI services the model half asks for (the bar, the windows, the Book panel, the alert).
REGISTER_PMINTERFACE(KFCUIServices, kKFCUIServicesImpl)
REGISTER_PMINTERFACE(KFCQueryDialogController, kKFCQueryDialogControllerImpl)
REGISTER_PMINTERFACE(KFCQueryDialogObserver, kKFCQueryDialogObserverImpl)
REGISTER_PMINTERFACE(KFCQueryListAdapter, kKFCQueryListAdapterImpl)
REGISTER_PMINTERFACE(KFCQueryListWidgetMgr, kKFCQueryListWidgetMgrImpl)
