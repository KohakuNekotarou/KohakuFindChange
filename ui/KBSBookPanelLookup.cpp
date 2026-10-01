//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSBookPanelLookup.h. UI side. Carried out of KBSBookScope.cpp on 2026-10-01 (the model/UI
//  split) as it stood - the notes below are that file's, and "this module" in them was KBSBookScope.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager - the panel walk starts here
#include "IBook.h"
#include "IBookManager.h"
#include "IBookUIUtils.h"		// GetBookFileFromBookPanel (panel vs active book)
#include "IControlView.h"		// a panel IS a control view - what GetNthPanelInfo's UID resolves to
#include "IPanelMgr.h"			// GetPanelCount / GetNthPanelInfo - one book panel per open book
#include "IPanelControlData.h"	// what QueryActiveBookPanel hands over - the book panel's class is read off it
#include "ISession.h"

// General includes:
#include "FileUtils.h"			// IsEqual - a panel's book and the searched book, compared as files
#include "PaletteRefUtils.h"	// IsPaletteVisible - the front tab is decided on the container
#include "PersistUtils.h"		// ::GetClass / ::GetDataBase
#include "SDKFileHelper.h"
#include "Utils.h"

// Project includes:
#include "KBSBookPanelLookup.h"

namespace
{
	/** The ClassID of InDesign's book panel as THIS build numbers it, learned from a live one - or
	    kInvalidClass while none could be asked yet.

	    ***** Why it is learned rather than written down (2026-09-25). ***** kBookPanelBoss lives in
	    BOOK PANEL.APLN and is declared in no public header, so until this date the file compared
	    against 0x10101 - a number read off a 20.5 DEBUG build's object-model dump (2026-07-28,
	    docs/ai-notes/book-panel-active-tab.md) - backed up by matching the panel's NAME against the
	    open books' titles. Neither held:
	      * the number is one build's numbering, and nothing promised the release build of another
	        version kept it;
	      * the name half could pick the WRONG PANEL: it accepted any panel whose name merely STARTED
	        with a book's title, so a book called "Book" made the Bookmarks panel a book panel, and a
	        book called "Info" the Info panel (the user asked "how do you tell them apart - can it not
	        get it wrong?", and it could).
	    A wrong answer was cheap while the only callers read a book FILE off the panel; since Remember
	    Book Panel Placement it would move and resize somebody else's palette. Both are gone - the
	    number too, at the user's word: no hard-coded fallback.
	    IBookUIUtils::QueryActiveBookPanel hands over the active book's own panel (IBookUIUtils.h:83-87),
	    so its class is the book panel's class by construction, whatever the build numbers it.
	    Learned once and kept: a class does not change within a session. Nothing is asked while no
	    book is open - there is no book panel to learn from, and no book panel to find either. */
	ClassID gLearnedBookPanelClass = kInvalidClass;

	ClassID LearnedBookPanelClass()
	{
		if (gLearnedBookPanelClass != kInvalidClass)
			return gLearnedBookPanelClass;

		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		if (bookMgr == nil || bookMgr->GetBookCount() == 0)
			return kInvalidClass;
		if (!Utils<IBookUIUtils>().Exists())
			return kInvalidClass;

		InterfacePtr<IPanelControlData> activeBookPanel(Utils<IBookUIUtils>()->QueryActiveBookPanel());
		if (activeBookPanel == nil)
			return kInvalidClass;	// no active book right now - asked again next time

		gLearnedBookPanelClass = ::GetClass(activeBookPanel);
		return gLearnedBookPanelClass;
	}

	/** Is this registered panel one of InDesign's book panels?

	    ***** ONE PLACE. ***** Every walk of the panel list in this plug-in asks it here (ForEachBookPanel,
	    and KBSBookPanelPlacement through IsBookPanel), so how a book panel is recognised is decided once.
	    The class learned from a live book panel decides, and nothing else (see LearnedBookPanelClass).
	    Before one could be asked, the answer is "no": the callers here then fall back to the active
	    book, and KBSBookPanelPlacement finds no book panel to measure or move. With no book open that
	    is simply right. With books open but NONE ACTIVE - QueryActiveBookPanel then has nothing to
	    hand over - their panels go unrecognised until one is; whether InDesign lets that state last
	    is not measured. */
	bool IsBookPanelView(IControlView* panelView)
	{
		if (panelView == nil)
			return false;
		const ClassID learned = LearnedBookPanelClass();
		if (learned == kInvalidClass)
			return false;
		return ::GetClass(panelView) == learned;
	}

	/** The book file THIS panel is showing; false when the panel could not be resolved.

	    Handing the panel itself in is the whole point: with a real widget IBookUIUtils resolves that
	    panel's book, where a nil widget falls through to QueryActiveBookPanel - the active book, which
	    is precisely the value both callers exist to avoid.

	    An empty result is refused here rather than passed on, because further up it would read as
	    "no book at all" instead of "this panel could not be asked". */
	bool GetBookFileFromPanelView(IControlView* panelView, IDFile& outFile)
	{
		IDFile panelBookFile;
		Utils<IBookUIUtils>()->GetBookFileFromBookPanel(panelBookFile, panelView);

		SDKFileHelper panelFileHelper(panelBookFile);
		if (panelFileHelper.GetPath().empty())
			return false;

		outFile = panelBookFile;
		return true;
	}
}

IPanelMgr* KBSBookPanelLookup::QueryPanelManager()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;
	return app->QueryPanelManager();
}

void KBSBookPanelLookup::ForEachBookPanel(IPanelMgr* panelMgr,
	const std::function<bool(IControlView* panelView, const WidgetID& panelWidgetID)>& visit)
{
	if (panelMgr == nil)
		return;
	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return;

	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		WidgetID panelWidgetID;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, &panelWidgetID))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!IsBookPanelView(panelView))
			continue;
		if (visit(panelView, panelWidgetID))
			return;
	}
}

bool KBSBookPanelLookup::GetPanelBookFile(IDFile& outFile)
{
	if (!Utils<IBookUIUtils>().Exists())
		return false;

	// Walk every registered panel instead of asking for "the" book panel. Two earlier attempts
	// failed and are not worth repeating (measured 2026-07-27/28):
	//   - GetBookPanelWidget() returns nil for us. It is fed by the book panel's OWN actions
	//     (SetBookPanelWidget), so a command from another panel's flyout finds nothing stored.
	//   - GetBookFileFromBookPanel(file, nil) falls through to QueryActiveBookPanel(), i.e. the
	//     ACTIVE book - which is exactly the value we are trying not to use.
	// The walk works because InDesign creates one book panel per open book (tab count == panel
	// count) and registers each with IPanelMgr. Details: docs/ai-notes/book-panel-active-tab.md.
	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	bool found = false;
	ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID&) -> bool
	{
		// The front tab is decided on the CONTAINER, never on the panel. A book panel sitting
		// behind another tab still reports itself visible - all three panels came back
		// "Visible state 1" in the measurement - while only the front tab's kTabPanelContainerType
		// is visible. Asking panelView->IsVisible() here would match every book panel and pick
		// whichever came first.
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid() || !PaletteRefUtils::IsPaletteVisible(container))
			return false;

		// Keep looking when this panel could not be asked, rather than handing back a blank file.
		found = GetBookFileFromPanelView(panelView, outFile);
		return found;
	});

	// None: no visible book panel - it is iconised, its palette is closed, or no book is open at all.
	// The caller falls back to the active book, which is what the user expects in that state.
	return found;
}

bool KBSBookPanelLookup::IsBookPanel(IControlView* panelView)
{
	// A door onto the anonymous-namespace test, not a second copy of it: a copy here would be a
	// second place where a book panel is recognised.
	return IsBookPanelView(panelView);
}

void KBSBookPanelLookup::BringBookTabForward(const PMString& bookPath)
{
	// (Until 2026-10-01 this was the second half of KBSBookScope::ActivateBook, which made the book the
	//  active one first - that half is the model's, KBSBookScope::MakeBookActive, and the caller asks it
	//  before this. The numbering below is ActivateBook's.)
	// 2. The tab the user can SEE. One panel per open book, each registered with IPanelMgr, so the
	//    panel is found by walking the list and asking each candidate which book it belongs to -
	//    the WidgetID is numbered per book at runtime, which is why no name can be used here.
	if (!Utils<IBookUIUtils>().Exists())
		return;	// the active book was still set; the tab is the part we could not do

	// ***** THE BOOKS ARE COMPARED AS FILES, NOT AS PATH STRINGS (API re-audit, 2026-10-02). *****
	// FileUtils::IsEqual - the rule KBSBookScope's KBSDocumentLivesInFile keeps, and the model half's
	// MakeBookActive asks IBookManager::FindOpenBookByName with the same IDFile. The panel's path string
	// was compared with == until then.
	const IDFile wanted(SDKFileHelper(bookPath).GetIDFile());

	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID& panelWidgetID) -> bool
	{
		// Unlike GetPanelBookFile, visibility is NOT a filter here: the tab we are looking for is
		// precisely the one that is NOT in front yet.
		IDFile panelBookFile;
		if (!GetBookFileFromPanelView(panelView, panelBookFile))
			return false;

		if (FileUtils::IsEqual(panelBookFile, wanted) == kFalse)
			return false;

		// kFalse = do not take the key focus. The keyboard walk calls this while the user is
		// holding an arrow key on the result tree; handing the focus to the book panel would end
		// the walk at the first book row.
		panelMgr->ShowPanelByWidgetID(panelWidgetID, kFalse);
		return true;
	});
}

// End, KBSBookPanelLookup.cpp.
