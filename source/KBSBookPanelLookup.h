//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  InDesign's Book panel, OBSERVED - the UI half's part of "which book" (2026-10-01, the model/UI split).
//  Reading the panels needs IBookUIUtils, IPanelMgr and PaletteRefUtils (WidgetBin), which a model plug-in
//  may not reach, so the walks that used to stand in KBSBookScope live here. The DECISION stays with the
//  model: KBSBookScope asks for the front tab's book (through IKBSUIServices::GetPanelBookFile) and falls
//  back to the active book when there is none - KCM's KCMBookPanelLookup is the same split.
//
//========================================================================================

#ifndef __KBSBookPanelLookup_h__
#define __KBSBookPanelLookup_h__

#include "IDFile.h"
#include "PMString.h"

class IControlView;

namespace KBSBookPanelLookup
{
	/** The file of the book whose tab is FRONTMOST in the book panel, which is NOT necessarily
	    IBookManager::GetCurrentActiveBook: selecting a book's tab switches the panel but does not make
	    that book active - only touching a chapter inside it does (measured 2026-07-27).
	    Found by walking IPanelMgr: InDesign creates one book panel per open book, and the front tab is
	    the one whose containing palette is visible (docs/ai-notes/book-panel-active-tab.md).
	    @return false when no book panel is frontmost - the panel is iconised, its palette is closed, or
	            no book is open - leaving outFile untouched. */
	bool GetPanelBookFile(IDFile& outFile);

	/** Is this registered panel one of InDesign's book panels? The ONE place that answers it - the class
	    is learned from a live book panel (IBookUIUtils::QueryActiveBookPanel) and compared, never a name.
	    Asked by the walks in this file and by KBSBookPanelPlacement. */
	bool IsBookPanel(IControlView* panelView);

	/** Bring the tab of the open book at 'bookPath' to the front of the book panel, WITHOUT taking the
	    key focus (a keyboard walk over the result tree must not end at the book panel). Does nothing when
	    no book panel shows that book. The caller has made the book the active one first
	    (KBSBookScope::MakeBookActive): the active book and the front tab are separate states, and the
	    user who clicks a book row asks for both. */
	void BringBookTabForward(const PMString& bookPath);
}

#endif // __KBSBookPanelLookup_h__
