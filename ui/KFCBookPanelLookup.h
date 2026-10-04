//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  InDesign's Book panel, OBSERVED - the UI half's part of "which book" (the model/UI split).
//  Reading the panels needs IBookUIUtils, IPanelMgr and PaletteRefUtils (WidgetBin), which a model plug-in
//  may not reach, so the walks live here and not in KFCBookScope. The DECISION stays with the
//  model: KFCBookScope asks for the front tab's book (through IKFCUIServices::GetPanelBookFile) and falls
//  back to the active book when there is none - KCM's KCMBookPanelLookup is the same split.
//
//========================================================================================

#ifndef __KFCBookPanelLookup_h__
#define __KFCBookPanelLookup_h__

#include "IDFile.h"
#include "PMString.h"

#include <functional>

class IControlView;
class IPanelMgr;

namespace KFCBookPanelLookup
{
	/** The panel manager, AddRef'd - or nil: during startup it may not exist yet, and during teardown
	    the session can already be gone. */
	IPanelMgr* QueryPanelManager();

	/** THE ONE WALK OF THE BOOK PANELS - GetPanelBookFile, BringBookTabForward and KFCBookPanelPlacement
	    all go through it rather than writing the loop out. Every panel registered with
	    panelMgr that is one of InDesign's book panels (IsBookPanel), in the manager's order, is handed to
	    `visit` with its WidgetID, until `visit` answers true. InDesign makes one book panel per open book
	    and registers each with IPanelMgr, and the WidgetID is numbered per book at run time - so walking
	    is the only way to find them (docs/ai-notes/book-panel-active-tab.md). Nothing is walked for a nil
	    manager. */
	void ForEachBookPanel(IPanelMgr* panelMgr,
		const std::function<bool(IControlView* panelView, const WidgetID& panelWidgetID)>& visit);

	/** The file of the book whose tab is FRONTMOST in the book panel, which is NOT necessarily
	    IBookManager::GetCurrentActiveBook: selecting a book's tab switches the panel but does not make
	    that book active - only touching a chapter inside it does (measured).
	    Found by walking IPanelMgr: InDesign creates one book panel per open book, and the front tab is
	    the one whose containing palette is visible (docs/ai-notes/book-panel-active-tab.md).
	    @return false when no book panel is frontmost - the panel is iconised, its palette is closed, or
	            no book is open - leaving outFile untouched. */
	bool GetPanelBookFile(IDFile& outFile);

	/** Is this registered panel one of InDesign's book panels? The ONE place that answers it - the class
	    is learned from a live book panel (IBookUIUtils::QueryActiveBookPanel) and compared, never a name.
	    Asked by ForEachBookPanel and by KFCBookPanelPlacement. */
	bool IsBookPanel(IControlView* panelView);

	/** Bring the tab of the open book at 'bookPath' to the front of the book panel, WITHOUT taking the
	    key focus (a keyboard walk over the result tree must not end at the book panel). Does nothing when
	    no book panel shows that book. The caller has made the book the active one first
	    (KFCBookScope::MakeBookActive): the active book and the front tab are separate states, and the
	    user who clicks a book row asks for both. */
	void BringBookTabForward(const PMString& bookPath);
}

#endif // __KFCBookPanelLookup_h__
