//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A RESULT ROW'S TEXT FOCUS, OF KFC'S OWN CLASS (docs/superpowers/plans/2026-10-14-kfc-row-focus-undo.md D1): a focus
//  the user's Undo and Redo put back. InDesign's own focus (kTextFocusBoss) follows typing and deleting but not an Undo
//  or a Redo (measured); a focus whose range is a snapshot VIEW interface, marked dirty before every change and
//  targeting its document's database, follows both (measured in KT - docs/ai-notes/text-focus-undo-spike-2026-10-14.md:
//  four Undos and four Redos, a paragraph pasted and undone, a
//  Change All). kKFCRowFocusBoss is kTextFocusBoss with IID_ITEXTFOCUS replaced and nothing else - the shape of
//  InDesign's own selection and walker foci (kTextSelectionFocusBoss and three more, the 20.5 dump). Made by
//  ITextFocusManager::NewFocus(range, kKFCRowFocusBoss) - KFCRowFoci, which also sets and clears its target database.
//
//  ONE THING IT CANNOT FOLLOW, AND SAYS SO: an Undo or a Redo of a change to its story that it holds no snapshot of - an
//  edit made before the focus was (older than the search) undone, or its story deleted and brought back. InDesign then
//  calls the view interface's ResetView, and the focus is LOST from then on - never trusted again (IsLost).
//
//========================================================================================

#ifndef __KFCRowFocus_h__
#define __KFCRowFocus_h__

class ITextFocus;

namespace KFCRowFocus
{
	/** Has this focus met an Undo or a Redo it could not follow (ResetView)? false for a focus of any other class. */
	bool IsLost(const ITextFocus* focus);
}

#endif // __KFCRowFocus_h__
