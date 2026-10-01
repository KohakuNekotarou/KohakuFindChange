//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The panel's TAB carries the search scope, so which of the two searches the flyout is about to
//  run can be read without opening it: "Kohaku Find/Change - Book" while Book Scope is ON,
//  "- Document" while it is OFF. The words are the flyout's own Find in Book / Find in Document.
//
//========================================================================================

#ifndef __KBSPanelTitle_h__
#define __KBSPanelTitle_h__

namespace KBSPanelTitle
{
	/** Write the current scope onto the panel's tab.

	    KBSBookScope::IsBookScopeOn() is the ONLY input. Whether a book or a document is actually
	    open is deliberately NOT shown: that changes outside KBS (a document closes, a book panel
	    tab is brought forward) with nothing to tell us, so a tab drawn from it would go stale
	    without being wrong-looking. The toggle can only be changed through KBS itself.

	    Safe to call when the panel has never been opened (does nothing then). */
	void Update();

	/** Put the tab back to the plain name from the .fr. Called at shutdown, so a renamed tab can
	    never be the thing a saved workspace remembers. */
	void Restore();
}

#endif // __KBSPanelTitle_h__
