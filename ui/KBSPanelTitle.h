//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The panel's TAB carries what the next search will use, so it can be read without opening the
//  flyout: the Find/Change dialog's tab and the scope - "Kohaku Find/Change - Text - Book" while Book
//  Scope is ON, "- GREP - Story" and the like (Edit > Find/Change's Search:) while it is OFF.
//
//========================================================================================

#ifndef __KBSPanelTitle_h__
#define __KBSPanelTitle_h__

namespace KBSPanelTitle
{
	/** Write the Find/Change tab and the scope onto the panel's tab.

	    The inputs are the settings a search will use - Book Scope, and Find/Change's tab and Search:.
	    Whether a book or a document is actually open is deliberately NOT shown: that changes outside
	    KBS (a document closes, a book panel tab is brought forward) with nothing to tell us, so a tab
	    drawn from it would go stale without being wrong-looking.

	    Safe to call when the panel has never been opened (does nothing then). */
	void Update();

	/** Put the tab back to the plain name from the .fr. Called at shutdown, so a renamed tab can
	    never be the thing a saved workspace remembers. */
	void Restore();
}

#endif // __KBSPanelTitle_h__
