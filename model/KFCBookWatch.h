//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Attach / detach for the session-level observer that retires a book-scope result set when its
//  book is closed. Called from KFCStartupShutdown. See KFCBookWatch.cpp for why this is an
//  observer rather than a responder, and for the subject/protocol it listens on.
//
//========================================================================================

#ifndef __KFCBookWatch_h__
#define __KFCBookWatch_h__

/** Start listening on the session for book-close notifications. Safe to call twice. */
void KFCBookWatchAttach();

/** Stop listening. Safe to call when not attached. */
void KFCBookWatchDetach();

#endif // __KFCBookWatch_h__
