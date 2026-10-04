//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  How the model half tells the UI half that something changed (the model/UI split) - the
//  only direction the guide allows a model plug-in to speak in. A model plug-in may not call the panel:
//  the panel lives in the UI plug-in, which a background thread does not even load (guide vol1-07,
//  "Rules for thread safety"). So the model says what happened on the session's subject, under a
//  protocol of its own (IID_IKBSMODELOBSERVER), and whoever is listening draws it - KBSModelObserver in
//  the UI half. Nobody listening, nothing happens: that is what makes it safe off the main thread and
//  under InDesign Server.
//
//  The same shape KBS already used for the book-close watcher (IID_IKBSBOOKWATCH on the session), and
//  the shape KCM uses (KCMModelNotify.h). No ISubject is added to anybody's boss.
//
//  SYNCHRONOUS. ISubject::Change calls the observer before it returns, so a notification lands exactly
//  where the direct call it replaced did - the order of "empty the model, then draw the tree" is unchanged.
//
//  The senders, by name: KBSBookWatch, KBSCloseDocResponder and KBSUndoFollow - the pieces of the model
//  that change the results while no menu command of the panel is running. The runs themselves hand their
//  summary back to the command that started them, which draws it.
//
//========================================================================================

#ifndef __KBSModelNotify_h__
#define __KBSModelNotify_h__

#include "PMString.h"

/** The result set changed shape - build the panel's tree again. */
void KBSNotifyRebuild();

/** Only what the rows draw changed - repaint them in place, keeping what is open. */
void KBSNotifyRefreshRows();

/** Chapter chapterIdx's row is about to go: tell the tree BEFORE the model empties it, so only that row
    leaves (KBSResultTree::BeforeChapterRowGoes says why). */
void KBSNotifyChapterRowGoes(int32 chapterIdx);

/** Say this on the panel's message line. */
void KBSNotifyStatus(const PMString& message);

#endif // __KBSModelNotify_h__
