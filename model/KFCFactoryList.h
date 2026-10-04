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
// The model half's implementations. The UI half's are in ui/KFCUIFactoryList.h.
// The jump marker (a global text adornment - KFCHitMarker.cpp) and the startup/shutdown service.
REGISTER_PMINTERFACE(KFCHitMarkerAdornment, kKFCHitMarkerAdornmentImpl)
REGISTER_PMINTERFACE(KFCStartupShutdown, kKFCStartupShutdownImpl)
// Result invalidation: retire a document-scope result set when its document closes.
REGISTER_PMINTERFACE(KFCCloseDocResponder, kKFCCloseDocResponderImpl)
// Result invalidation: retire a book-scope result set when its book closes.
REGISTER_PMINTERFACE(KFCBookWatch, kKFCBookWatchImpl)
// The replace's signature: the command and its 64-bit data (KFCSignRecordsCmd.cpp).
REGISTER_PMINTERFACE(KFCSignRecordsCmd, kKFCSignRecordsCmdImpl)
REGISTER_PMINTERFACE(KFCInt64Data, kKFCInt64DataImpl)
// The panel follows an Undo and a Redo (KFCUndoFollow.cpp): the mark a write leaves, and the lazy observer
// AddIn'd on kDocBoss that hears it (not on kTextStoryBoss - KFCID.h, IID_IKFCSTORYUNDOOBSERVER, says why).
REGISTER_PMINTERFACE(KFCUndoMarkCmd, kKFCUndoMarkCmdImpl)
REGISTER_PMINTERFACE(KFCDocUndoObserver, kKFCDocUndoObserverImpl)
// The model half's three session interfaces (KFCModelServices.cpp).
REGISTER_PMINTERFACE(KFCResultsSession, kKFCResultsImpl)
REGISTER_PMINTERFACE(KFCRunsSession, kKFCRunsImpl)
REGISTER_PMINTERFACE(KFCChaptersSession, kKFCChaptersImpl)
