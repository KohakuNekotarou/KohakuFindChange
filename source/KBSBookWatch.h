//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Attach / detach for the session-level observer that retires a book-scope result set when its
//  book is closed. Called from KBSStartupShutdown. See KBSBookWatch.cpp for why this is an
//  observer rather than a responder, and for the subject/protocol it listens on.
//
//========================================================================================

#ifndef __KBSBookWatch_h__
#define __KBSBookWatch_h__

/** Start listening on the session for book-close notifications. Safe to call twice. */
void KBSBookWatchAttach();

/** Stop listening. Safe to call when not attached. */
void KBSBookWatchDetach();

#endif // __KBSBookWatch_h__
