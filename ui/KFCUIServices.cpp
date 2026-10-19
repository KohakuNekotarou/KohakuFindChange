//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The UI half's side of IKFCUIServices.h (the model/UI split): what the model half asks the UI half for.
//  AddIn'd on kSessionBoss from the UI half's resource, so a background thread - which loads no UI plug-in -
//  finds nothing there and the model does without.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"			// SetItemList - kOpenLayoutCmdBoss takes the document as its item
#include "IDataBase.h"
#include "IDocumentUIUtils.h"	// FindPresentationForDocument (has-a-window test), FindPresentationsForDocument
#include "IDocumentPresentation.h"	// ...the windows it lists - a background document's every layout window
#include "IPanelControlData.h"	// FindWidget - a window's layout pane(s), by WidgetID
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear?
#include "IWindow.h"			// the window kOpenLayoutCmdBoss is supposed to have produced
#include "IConcreteSelection.h"	// ReadSelectedItems - a selection's concrete selection
#include "ILayoutHitTestSuite.h"	// ...reached through its layout hit-test suite
#include "ILayoutTarget.h"		// GetUIDList(kStripStandoffs) - the selected page items
#include "ISelectionUtils.h"	// GetActiveSelection, QueryLayoutHitTestSuite
#include "ISelectionManager.h"	// ActiveSelectionExists - SelectionExists by the CSB's class
#include "TextEditorID.h"		// kTextSelectionBoss
#include "IActiveContext.h"		// GetContextSelection - the front view's selection (the background selections leave it)
#include "IApplication.h"		// QueryDocumentList - the documents behind the front one
#include "IDocument.h"
#include "IDocumentList.h"
#include "ILayoutSelectionSuite.h"	// SelectPageItems - a background document's items selected again
#include "IControlView.h"		// a window's layout pane, as FindWidget hands it back
#include "PersistUtils.h"		// ::GetDataBase - a document's database

// General includes:
#include "CmdUtils.h"
#include "CPMUnknown.h"
#include "DocumentPresFindCriteria.h"	// FindPresCriteria::accept_all / is_layout (WidgetBin - this is the UI half)
#include "ErrorUtils.h"			// GlobalErrorStatePreserver / PMSetGlobalErrorCode
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss; kLayoutWidgetID / kLayoutSecondaryPanelWidgetID (panes)
#include "ProgressBar.h"		// RangeProgressBar
#include "UIDList.h"
#include "Utils.h"

#include <new>					// std::nothrow

// Project includes:
#include "KFCUIID.h"				// kKFCUIServicesImpl, kKFCUIBoundaryImpl
#include "IKFCBoundary.h"
#include "IKFCUIServices.h"
#include "KFCBookPanelLookup.h"
#include "KFCJump.h"				// ClearFrontSelection - ClearSelection's door; IsLanding - IsLanding's
#include "KFCResultHistory.h"		// QueryReadsAsTier, RenameLiveFromQuery - the lists' names are this half's
#include "KFCDiag.h"				// KFC_DIAG_FAULT - test builds only (bg-first-view)
#ifdef KFC_DIAG
#include "ILayoutViewUtils.h"		// GetAllLayoutViews - bg-first-view's old way, a test build's only
#endif

namespace
{
	/** One RangeProgressBar, held for the model half. Its calls are the bar's own - while the manager holds the bar.

	    A BAR THE MANAGER TURNED AWAY IS NOT DRIVEN. Another bar's DisableChildProgressBars makes the manager refuse a new
	    one (ProgressBar.h, WasRegisterSuccessful) - KFC's own searches set it on theirs, and any bar standing when a run
	    starts may have. A refused bar never comes on screen, so its calls give up nothing the user could see; what they
	    would do is call through a bar the manager does not hold. Asked once, as the bar is made - the guard KCM
	    (KCMProgressBar.h) and KIDMCP put in: KIDMCP after a crash on its own bar's path (seen once; the
	    refusal as its cause is a guess, not a diagnosis), KCM after a refusal that showed only as a missing bar. */
	class KFCUIProgressBar : public KFCProgressBarUI
	{
	public:
		KFCUIProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel)
			: fBar(title, startRange, endRange, showImmediate, showCancel), fRegistered(fBar.WasRegisterSuccessful() != kFalse) {}
		virtual ~KFCUIProgressBar() {}

		virtual void	SetTaskText(const PMString& text, bool16 forceRedraw)
		{
			if (fRegistered)
				fBar.SetTaskText(text, forceRedraw);
		}
		virtual void	SetPosition(int32 newPosition)
		{
			if (fRegistered)
				fBar.SetPosition(newPosition);
		}
		/** False for a refused bar: nobody can have pressed a Cancel that was never on screen. */
		virtual bool16	WasCancelled(bool8 setGlobalErrorState)
		{
			return fRegistered ? fBar.WasCancelled(setGlobalErrorState) : kFalse;
		}
		virtual void	DisableChildProgressBars(bool16 disable)
		{
			if (fRegistered)
				fBar.DisableChildProgressBars(disable);
		}

	private:
		RangeProgressBar	fBar;			// declared first: fRegistered reads it as it is made
		const bool			fRegistered;	// the manager took the bar (WasRegisterSuccessful)
	};

	/** Does the front view's selection hold one of this concrete selection boss - kInvalidClass = of any
	    (ISelectionManager::SelectionExists, public; the class names the CSB, ISelectionManager.h)? */
	bool ActiveSelectionExists(const ClassID& csb)
	{
		Utils<ISelectionUtils> selectionUtils;
		if (!selectionUtils.Exists())
			return false;
		ISelectionManager* const selectionManager = selectionUtils->GetActiveSelection();
		return selectionManager != nil
			&& selectionManager->SelectionExists(csb, ISelectionManager::kAnySelection) != kFalse;
	}

	/** The page items a selection manager holds, standoffs stripped - how the SDK's snippets read a selection
	    (SnpInspectLayoutModel.cpp: ILayoutTarget::GetUIDList(kStripStandoffs)), reached through the manager's layout
	    hit-test suite's concrete selection (csxsdemo's VDActionComponent.cpp; KT measured it) - into outItems. False =
	    no layout selection to read, outItems left as it was. The one reading of a selection's items here: the active
	    selection's (GetSelectedPageItems) and each background window's (Snapshot / RestoreBackgroundSelections). */
	bool ReadSelectedItems(ISelectionManager* manager, UIDList& outItems)
	{
		Utils<ISelectionUtils> selectionUtils;
		if (manager == nil || !selectionUtils)
			return false;
		InterfacePtr<ILayoutHitTestSuite> hitSuite(selectionUtils->QueryLayoutHitTestSuite(manager));
		InterfacePtr<IConcreteSelection> concrete(hitSuite, UseDefaultIID());
		InterfacePtr<ILayoutTarget> target(concrete, UseDefaultIID());
		if (target == nil)
			return false;
		outItems = target->GetUIDList(kStripStandoffs);
		return true;
	}

	// ----- THE SELECTIONS OF THE WINDOWS BEHIND THE FRONT VIEW (IKFCUIServices::SnapshotBackgroundSelections) -----

	/** One layout window's selection as an object search found it: its page items, or nothing, or something else - a
	    window of a document behind the front one, or one of the front document's other than the front view (whose
	    selection is the snapshot's own, KFCObjectSearch's SelectionKeeper). */
	struct BackSelection
	{
		IDataBase*						db;		// checked against the open documents before anything reads it
		const IDocumentPresentation*	window;	// which window - only compared with the document's windows
		int32							pane;	// kLayoutWidgetID 0, a split window's second pane 1
		UIDList							items;	// the page items selected then - none: nothing, or `other`
		bool							other;	// something else was selected then - text, a table's cells
		BackSelection() : db(nil), window(nil), pane(0), other(false) {}
	};
	std::vector<BackSelection> gBackSelections;

	/** Each layout window of a document - a minimised one too, a split window's second pane too - with its
	    selection manager: fn(window, pane, manager) once for each manager but skip (the front view's). EVERY
	    WINDOW, not the document's first layout view alone: an object search's walk selects in a window of each
	    document it walks (IFindChangeService::SearchObject), and measured (block 0's O-1 / O-2) it left its last
	    item selected in a minimised window - ILayoutViewUtils::GetAllLayoutViews lists no view of one - and in a
	    document's second window. The windows as IDocumentUIUtils lists them (is_layout: a story editor or a galley
	    holds no layout selection), each window's panes by WidgetID - the product's way (spellpanel's
	    PrivateSpellingUtils.cpp; KCM's KCMViewLookup.cpp the same): the window's kLayoutWidgetID, and in a split
	    window the kLayoutWidgetID inside the kLayoutSecondaryPanelWidgetID panel, while that view is shown. The
	    manager is the pane's view's (ISelectionUtils::QueryViewSelectionManager - Adobe's trackers' way, KCM's
	    QuerySelectionManagerFor in KCMStoryJump.cpp). Measured with KT's panesel probe (2026-10-19): a split
	    window's second pane has a manager of its own (what is selected there is not the window's DOM selection);
	    the PANEL answers the first pane's manager, so handed over as it stands it never reached the second pane
	    (1b4e6a9 did that); and every window has a hidden second pane with a manager of its own - left out, as the
	    product leaves it. */
	template <typename Fn>
	void ForEachWindowSelection(IDataBase* db, const ISelectionManager* skip, Fn fn)
	{
		Utils<IDocumentUIUtils> docUI;
		Utils<ISelectionUtils> selectionUtils;
		if (db == nil || !docUI || !selectionUtils)
			return;
#ifdef KFC_DIAG
		// (Fault switch bg-first-view, a test build's only - KFCDiag.h: the document's first layout view alone, the
		// old way - a minimised window and a second window passed over: obj-bg-sel-minimised-armed and
		// obj-bg-sel-second-window-armed.)
		if (KFC_DIAG_FAULT("bg-first-view"))
		{
			Utils<ILayoutViewUtils> viewUtils;
			K2Vector<IControlView*> views;
			if (viewUtils)
				viewUtils->GetAllLayoutViews(views, nil, db);
			for (size_t i = 0; i < views.size(); ++i)
			{
				if (views[i] == nil)
					continue;
				InterfacePtr<ISelectionManager> manager(selectionUtils->QueryViewSelectionManager(views[i]));
				ISelectionManager* const m = manager;
				if (m != nil && m != skip)
					fn(nil, 0, m);
				return;
			}
			return;
		}
#endif
		PresentationSequence windows;
		FindPresentation_PreferCriteria noPreference;
		(void)docUI->FindPresentationsForDocument(db, windows, &FindPresCriteria::is_layout, noPreference);
		for (size_t w = 0; w < windows.size(); ++w)
		{
			InterfacePtr<IPanelControlData> panel(windows[w], UseDefaultIID());
			if (panel == nil)
				continue;
			IControlView* panes[2] = { panel->FindWidget(kLayoutWidgetID), nil };
			InterfacePtr<IPanelControlData> split(panel->FindWidget(kLayoutSecondaryPanelWidgetID), UseDefaultIID());
			IControlView* const second = (split != nil) ? split->FindWidget(kLayoutWidgetID) : nil;
			if (second != nil && second->IsVisible())
				panes[1] = second;
			const ISelectionManager* seen = nil;
			for (int32 p = 0; p < 2; ++p)
			{
				InterfacePtr<ISelectionManager> manager(
					(panes[p] != nil) ? selectionUtils->QueryViewSelectionManager(panes[p]) : nil);
				ISelectionManager* const m = manager;
				if (m == nil || m == skip || m == seen)
					continue;		// (never seen shared - this only keeps a manager from being given back twice)
				seen = m;
				fn(windows[w], p, m);
			}
		}
	}

	/** One window's selection given back as the snapshot found it (RestoreBackgroundSelections). */
	void GiveBack(const BackSelection& kept, ISelectionManager* manager)
	{
		// A selection is no change to the document - the guard is SelectItem's (KFCJump.cpp), for the same reason.
		IDataBase::SaveRestoreModifiedState dirtyGuard(kept.db);
		UIDList now(kept.db);
		(void)ReadSelectedItems(manager, now);
		if (kept.items.Length() > 0)
		{
			// THE ITEMS SELECTED THEN, selected again - the ones still there - when the walks changed them.
			UIDList again(kept.db);
			for (int32 i = 0; i < kept.items.Length(); ++i)
				if (kept.db->IsValidUID(kept.items[i]))
					again.Append(kept.items[i]);
			if (again == now)
				return;
			InterfacePtr<ILayoutSelectionSuite> layoutSuite(
				Utils<ISelectionUtils>()->QueryLayoutSelectionSuite(manager));
			manager->DeselectAll(nil);
			if (layoutSuite != nil && again.Length() > 0)
				layoutSuite->SelectPageItems(again, Selection::kReplace, Selection::kDontScrollLayoutSelection);
		}
		else if (!kept.other)
		{
			// NOTHING SELECTED THEN: whatever a walk selected, taken away.
			if (manager->SelectionExists(kInvalidClass, ISelectionManager::kAnySelection) != kFalse)
				manager->DeselectAll(nil);
		}
		else if (now.Length() > 0)
		{
			// TEXT OR CELLS SELECTED THEN - not read back here: the items a walk selected over them taken away, rather
			// than left selected for the next key to act on.
			manager->DeselectAll(nil);
		}
	}

	/** Is this database an open document's now? Compared, never dereferenced (a closed document's pointer dangles): the
	    document list's own comparison, IDocumentList::FindDocByDataBase - KFC's liveness test (KFCHitMarkerView.cpp). */
	bool IsOpenDocDB(IDataBase* db)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docs((app != nil) ? app->QueryDocumentList() : nil);
		return docs != nil && docs->FindDocByDataBase(db) != nil;
	}
}

class KFCUIServices : public CPMUnknown<IKFCUIServices>
{
public:
	KFCUIServices(IPMUnknown* boss) : CPMUnknown<IKFCUIServices>(boss) {}
	virtual ~KFCUIServices() {}

	virtual KFCProgressBarUI* NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
		bool8 showImmediate, bool8 showCancel)
	{
		return new (std::nothrow) KFCUIProgressBar(title, startRange, endRange, showImmediate, showCancel);
	}

	/** Does this document have a WINDOW anywhere - front, or behind another tab? The
	    all-presentations search, because GetFrontmostPresentationForDocument answers nil for a
	    document sitting behind another tab (KFCBookScope::ShowChapterWindow asks this too).

	    Asked by the held-chapter releases: a window makes a chapter the USER'S,
	    whoever raised the window. ShowChapterWindow and the jump take a chapter off the held list
	    when they raise one themselves, but a window can be raised behind KFC's back - the
	    book panel lists every chapter, and double-clicking one there windows the very document being
	    held. A release that closed it then would take a window the user is looking at; and once they
	    had saved their work, not even the unsaved-work door would stand in the way.

	    The SDK's own "any presentation" predicate, FindPresCriteria::accept_all, as KCM's UI half uses
	    it (KCMStoryJump.cpp). This test cannot move back to the model half: the predicate's
	    implementation is in WidgetBin (DocumentPresFindCriteria.h:40-46). */
	virtual bool DocHasAnyWindow(const UIDRef& docRef)
	{
		IDataBase* db = docRef.GetDataBase();
		if (db == nil)
			return false;
		FindPresentation_PreferCriteria noPreference;
		return Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, &FindPresCriteria::accept_all, noPreference) != nil;
	}

	/** The window half of KFCBookScope::ShowChapterWindow. */
	virtual bool OpenLayoutWindow(const UIDRef& docRef)
	{
		// THE WINDOW IS ALLOWED NOT TO APPEAR - this function's false says so - so its error
		// state stays in here. Preserve, then clear, exactly as the open in KFCBookScope::ReopenChapterDoc
		// does (its note has the SDK's own shape and the contract at ErrorUtils.h:115-117). Placed
		// ahead of the command rather than inside the failure branch so that all THREE ways this can
		// end without a window are covered: the command that would not build, the one that failed, and
		// the one that reported success without producing a presentation.
		GlobalErrorStatePreserver windowErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
		if (cmd == nil)
			return false;
		cmd->SetItemList(UIDList(docRef));

		// The command's data interface, taken BEFORE processing so the result can be read back off it
		// afterwards. Nothing is set on it - the defaults are what a chapter window should get.
		//
		// NO DATA INTERFACE = FAILURE, and the command is not run at all. The recipe this follows breaks
		// off here too (SDKLayoutHelper.cpp:268-272). Running the command anyway would have to skip the
		// window test below, and return true without having established the one thing this function's
		// true means - that the chapter now has a window.
		InterfacePtr<IOpenLayoutPresentationCmdData> openData(cmd, IID_IOPENLAYOUTCMDDATA);
		if (openData == nil)
			return false;

		if (CmdUtils::ProcessCommand(cmd) != kSuccess)
			return false;		// whatever it raised goes back with the preserver above

		// Did a window actually appear? SDKLayoutHelper::OpenLayoutWindow (the SDK's own recipe for this
		// command) does not stop at the return code: it reads GetResultingPresentation() and checks an
		// IWindow comes out of it, because "the command succeeded" and "there is a window" are two
		// different statements. Saying so here matters - the caller reports this chapter as shown.
		InterfacePtr<IWindow> window(openData->GetResultingPresentation(), UseDefaultIID());
		return window != nil;
	}

	virtual bool GetPanelBookFile(IDFile& outFile)
	{
		return KFCBookPanelLookup::GetPanelBookFile(outFile);
	}

	virtual bool GetPanelBookSelection(const IDFile& bookFile, std::vector<UID>& outContents, int32& outTotal)
	{
		return KFCBookPanelLookup::GetPanelBookSelection(bookFile, outContents, outTotal);
	}

	virtual bool GetSelectedPageItems(UIDList& outItems)
	{
		// The active selection's items (ReadSelectedItems).
		outItems.Clear();
		Utils<ISelectionUtils> selectionUtils;
		return selectionUtils.Exists() && ReadSelectedItems(selectionUtils->GetActiveSelection(), outItems)
			&& outItems.Length() > 0;
	}

	virtual bool HasTextSelection()
	{
		// The layout's text selection only: a caret in the Story Editor (kGalleyTextSelectionBoss) or in a note
		// (kNoteTextSelectionBoss) is not counted - whether Find/Change's own Find counts it is not measured.
		return ActiveSelectionExists(kTextSelectionBoss);	// the text concrete selection boss's own answer
	}

	virtual bool HasAnySelection()
	{
		return ActiveSelectionExists(kInvalidClass);		// every concrete selection asked at once
	}

	virtual void ClearSelection()
	{
		KFCJump::ClearFrontSelection();
	}

	virtual bool IsLanding()
	{
		return KFCJump::IsLanding();
	}

	virtual bool QueryReadsAsTier(uint32 resultSet, PMString& outName)
	{
		return KFCResultHistory::QueryReadsAsTier(resultSet, outName);
	}

	virtual void LiveQueryRecopied(uint32 resultSet)
	{
		KFCResultHistory::RenameLiveFromQuery(resultSet);
	}

	virtual void SnapshotBackgroundSelections()
	{
		gBackSelections = std::vector<BackSelection>();
		// THE FRONT VIEW'S SELECTION IS LEFT OUT - it is the snapshot's own (KFCObjectSearch's SelectionKeeper); every
		// other layout window of every open document is taken, the front document's other windows too.
		IActiveContext* const context = GetExecutionContextSession()->GetActiveContext();
		const ISelectionManager* const front = (context != nil) ? context->GetContextSelection() : nil;
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docs((app != nil) ? app->QueryDocumentList() : nil);
		const int32 count = (docs != nil) ? docs->GetDocCount() : 0;
		for (int32 d = 0; d < count; ++d)
		{
			IDocument* const doc = docs->GetNthDoc(d);
			IDataBase* const db = (doc != nil) ? ::GetDataBase(doc) : nil;
			// (a document with no layout window lists none: a walk selects nothing there to give back)
			ForEachWindowSelection(db, front, [&](const IDocumentPresentation* window, int32 pane,
				ISelectionManager* manager)
			{
				BackSelection kept;
				kept.db = db;
				kept.window = window;
				kept.pane = pane;
				kept.items = UIDList(db);
				(void)ReadSelectedItems(manager, kept.items);
				kept.other = kept.items.Length() == 0
					&& manager->SelectionExists(kInvalidClass, ISelectionManager::kAnySelection) != kFalse;
				KFC_DIAG_LOG("BGSEL keep window=%p pane=%d manager=%p items=%d other=%d", (const void*)window, pane,
					(void*)manager, kept.items.Length(), kept.other ? 1 : 0);
				gBackSelections.push_back(kept);
			});
		}
	}

	virtual void RestoreBackgroundSelections()
	{
		for (size_t k = 0; k < gBackSelections.size(); ++k)
		{
			const BackSelection& kept = gBackSelections[k];
			if (!IsOpenDocDB(kept.db))
				continue;
			// THE SAME WINDOW AND PANE, among the document's windows now (one closed since is not there).
			ForEachWindowSelection(kept.db, nil, [&](const IDocumentPresentation* window, int32 pane,
				ISelectionManager* manager)
			{
				if (window == kept.window && pane == kept.pane)
				{
					KFC_DIAG_LOG("BGSEL give window=%p pane=%d manager=%p", (const void*)window, pane, (void*)manager);
					GiveBack(kept, manager);
				}
			});
		}
		gBackSelections = std::vector<BackSelection>();		// a fresh one: the storage goes too
	}
};

CREATE_PMINTERFACE(KFCUIServices, kKFCUIServicesImpl)

/** This half's boundary stamp (IKFCBoundary.h), under IID_IKFCUIBOUNDARY - what the model half checks before it asks
    the services above for anything (KFCQueryUIServices). Compiled in, so it says which doors this .pln was built with. */
class KFCUIBoundary : public CPMUnknown<IKFCBoundary>
{
public:
	KFCUIBoundary(IPMUnknown* boss) : CPMUnknown<IKFCBoundary>(boss) {}

	virtual const char* GetBoundaryStamp() { return kKFCBoundaryStamp; }
};

CREATE_PMINTERFACE(KFCUIBoundary, kKFCUIBoundaryImpl)

// End, KFCUIServices.cpp.
