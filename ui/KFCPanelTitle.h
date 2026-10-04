//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The panel's TAB carries what the next search will use, so it can be read without opening the
//  flyout: the Find/Change dialog's tab and the scope - "Kohaku Find/Change - Text - Book" while Book
//  Scope is ON, "- GREP - Story" and the like (Edit > Find/Change's Search:) while it is OFF.
//
//========================================================================================

#ifndef __KFCPanelTitle_h__
#define __KFCPanelTitle_h__

namespace KFCPanelTitle
{
	/** Write the Find/Change tab and the scope onto the panel's tab.

	    The inputs are the settings a search will use - Book Scope, and Find/Change's tab and Search:
	    AS THE SELECTION MAKES IT (Story, To End of Story and Selection need a selection that offers
	    them; without one the dialog shows Document, and so does this). Every input is followed while
	    the panel is up: Book Scope by the flyout that toggles it, the Find/Change settings through
	    their subject, and the selection through the panel's observer - an ActiveSelectionObserver,
	    because a selection changed with the dialog closed leaves the settings untouched, and the tab
	    would go on naming a scope the next search would not use.
	    Whether a book or a document is actually open is deliberately NOT shown: that changes outside
	    KFC (a document closes, a book panel tab is brought forward) with nothing to tell us, so a tab
	    drawn from it would go stale without being wrong-looking.

	    A label already on the tab is not written again (the selection moves with every caret step).
	    Safe to call when the panel has never been opened (does nothing then). */
	void Update();

	/** Put the tab back to the plain name from the .fr. Called at shutdown, so a renamed tab can
	    never be the thing a saved workspace remembers. */
	void Restore();
}

#endif // __KFCPanelTitle_h__
