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
// The jump marker (a global text adornment - KBSHitMarker.cpp) and the startup/shutdown service.
REGISTER_PMINTERFACE(KBSHitMarkerAdornment, kKBSHitMarkerAdornmentImpl)
REGISTER_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)
// Result invalidation: retire a document-scope result set when its document closes.
REGISTER_PMINTERFACE(KBSCloseDocResponder, kKBSCloseDocResponderImpl)
// Result invalidation: retire a book-scope result set when its book closes.
REGISTER_PMINTERFACE(KBSBookWatch, kKBSBookWatchImpl)
// The replace's signature: the command and its 64-bit data (KBSSignRecordsCmd.cpp).
REGISTER_PMINTERFACE(KBSSignRecordsCmd, kKBSSignRecordsCmdImpl)
REGISTER_PMINTERFACE(KBSInt64Data, kKBSInt64DataImpl)
// The panel follows an Undo and a Redo (KBSUndoFollow.cpp): the mark a write leaves, and the lazy observer
// AddIn'd on kDocBoss that hears it (not on kTextStoryBoss - KBSID.h, IID_IKBSSTORYUNDOOBSERVER, says why).
REGISTER_PMINTERFACE(KBSUndoMarkCmd, kKBSUndoMarkCmdImpl)
REGISTER_PMINTERFACE(KBSDocUndoObserver, kKBSDocUndoObserverImpl)
// The model half's three session interfaces (KBSModelServices.cpp).
REGISTER_PMINTERFACE(KBSResultsSession, kKBSResultsImpl)
REGISTER_PMINTERFACE(KBSRunsSession, kKBSRunsImpl)
REGISTER_PMINTERFACE(KBSChaptersSession, kKBSChaptersImpl)
