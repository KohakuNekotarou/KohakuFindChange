//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The UI half's ear on the model half (2026-10-01, the model/UI split): the observer that receives what
//  KBSModelNotify.h sends on the session's subject and turns it into drawing - the tree rebuilt, the rows
//  repainted, a chapter row taken out, the message line written. An observer that keeps the user
//  interface up to date is a UI component (guide vol1-06, "UI component content"), so it lives here and
//  the model only speaks.
//
//  Attached by the UI's startup and detached by its shutdown (the session holds a pointer into this .pln
//  while attached).
//
//========================================================================================

#ifndef __KBSModelObserver_h__
#define __KBSModelObserver_h__

void KBSModelObserverAttach();
void KBSModelObserverDetach();

#endif // __KBSModelObserver_h__
