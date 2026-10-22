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

class IObserver;

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

	/** THE FIND/CHANGE SETTINGS' SUBJECT: attach `observer` to it on IID_IFINDCHANGEOPTIONS (attach false: detach it),
	    the subject calling back through `observerIID` - how the panel's observer hears the tab's settings, and the
	    query dialog's scope observer its Runs on: line's Search: (KFCQueryScopeObserver.cpp). Asked first: attaching
	    twice does nothing. */
	void AttachToFindChangeOptions(IObserver* observer, bool attach, const PMIID& observerIID);

	// NOTHING PUTS THE TAB BACK AT SHUTDOWN. A label lives only in the running palette: the saved workspace
	// names a panel by its resource KEY (OWLPaletteLeaf name="0x1EA600kKFCPanelTitleKey") and the session's
	// SavedData holds no label. Measured with KT's palettelabel probe: a built-in panel's label set at run
	// time was in neither file after a quit, and the next start showed the panel's own name. A Restore() did
	// it until then, first thing in the UI half's shutdown - a call into palettes being torn down, for nothing.
}

#endif // __KFCPanelTitle_h__
