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
// The model half's implementations (2026-10-01, the model/UI split). The UI half's are in
// ui/KFCUIFactoryList.h.
// Task 3: jump + marker + startup/shutdown. (KBSDrawEventSrvc / KBSDrawEventHandler stood here until
// 2026-09-26; the marker is the global text adornment below - KBSHitMarker.cpp.)
REGISTER_PMINTERFACE(KBSHitMarkerAdornment, kKBSHitMarkerAdornmentImpl)
REGISTER_PMINTERFACE(KBSStartupShutdown, kKBSStartupShutdownImpl)
// Result invalidation: retire a document-scope result set when its document closes.
REGISTER_PMINTERFACE(KBSCloseDocResponder, kKBSCloseDocResponderImpl)
// Result invalidation: retire a book-scope result set when its book closes.
REGISTER_PMINTERFACE(KBSBookWatch, kKBSBookWatchImpl)
// The replace's signature (2026-09-28): the command and its 64-bit data (KBSSignRecordsCmd.cpp).
REGISTER_PMINTERFACE(KBSSignRecordsCmd, kKBSSignRecordsCmdImpl)
REGISTER_PMINTERFACE(KBSInt64Data, kKBSInt64DataImpl)
// The panel follows an Undo and a Redo (2026-09-29): the lazy observer AddIn'd on kTextStoryBoss
// (KBSUndoFollow.cpp).
REGISTER_PMINTERFACE(KBSStoryUndoObserver, kKBSStoryUndoObserverImpl)
// The model half's three session interfaces (KBSModelServices.cpp).
REGISTER_PMINTERFACE(KBSResultsSession, kKBSResultsImpl)
REGISTER_PMINTERFACE(KBSRunsSession, kKBSRunsImpl)
REGISTER_PMINTERFACE(KBSChaptersSession, kKBSChaptersImpl)
