//========================================================================================
//  
//  $File: $
//  
//  Owner: 
//  
//  $Author: $
//  
//  $DateTime: $
//  
//  $Revision: $
//  
//  $Change: $
//  
//  Copyright 1997-2012 Adobe Systems Incorporated. All rights reserved.
//  
//  NOTICE:  Adobe permits you to use, modify, and distribute this file in accordance 
//  with the terms of the Adobe license agreement accompanying it.  If you have received
//  this file from a source other than Adobe, then your use, modification, or 
//  distribution of it requires the prior written permission of Adobe.
//  
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:

// General includes:
#include "CActionComponent.h"
#include "CAlert.h"
#include "IActionManager.h"		// PerformAction - Open Find/Change runs InDesign's own Edit > Find/Change
#include "IActionStateList.h"	// UpdateActionStates: the items' states, check marks and names
#include "IApplication.h"		// QueryActionManager
#include "ITreeNodeIDData.h"	// the row a right-click menu was popped over - its widget's NodeID
#include "FindChangeID.h"		// kFindDialogActionID - Edit > Find/Change

// Project includes:
#include "KFCUIID.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces (the model/UI split)
#include "KFCResultTree.h"		// rebuild the result tree after a search
#include "KFCResultHistory.h"	// the search's one door and the tiers it keeps
#include "KFCResultNodeID.h"		// the row right-clicked - a hit, story, document or book row
#include "KFCJump.h"			// the Hide Previous Chapter toggle lives with the jump logic
#include "KFCPanelTitle.h"		// the panel's tab name carries the current scope
#include "KFCHowTo.h"			// "How to Use..." - the operating reference
#include "KFCPanelAlpha.h"		// "Translucent Panel" - get / set / apply the panel's alpha
#include "KFCFindChangeMinimize.h"	// "Minimizable Find/Change" - the minimize box on InDesign's dialog
#include "KFCAppBarSearchEnter.h"	// "Link the Application Bar's Search Field to This Panel"
#include "KFCPanelState.h"		// "Save Panel Settings" - write the settings toggles to our own file
#include "KFCBookPanelPlacement.h"	// "Remember Book Panel Placement" - InDesign's own Book panel
#include "KFCQueryDialog.h"		// "Run Saved Queries..." - the query dialog
#include "KFCDiagHiddenDocs.h"		// the hidden-document check after an action - test builds only

/** Implements IActionComponent; performs the actions that are executed when the plug-in's
	menu items are selected.

	
	@ingroup KFC

*/
class KFCActionComponent : public CActionComponent
{
public:
/**
 Constructor.
 @param boss interface ptr from boss object on which this interface is aggregated.
 */
		KFCActionComponent(IPMUnknown* boss);

		/** The action component should perform the requested action.
			This is where the menu item's action is taken.
			When a menu item is selected, the Menu Manager determines
			which plug-in is responsible for it, and calls its DoAction
			with the ID for the menu item chosen.

			@param actionID identifies the menu item that was selected.
			@param ac active context
			@param mousePoint contains the global mouse location at time of event causing action (e.g. context menus). kInvalidMousePoint if not relevant.
			@param widget contains the widget that invoked this action. May be nil. 
			*/
		virtual void DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint, IPMUnknown* widget);

			/** Custom-enabled actions get their state here - the toggles their check marks, Find and Change All their names. */
			virtual void UpdateActionStates(IActiveContext* ac, IActionStateList* listToUpdate, GSysPoint mousePoint, IPMUnknown* widget);

	private:
		/** Encapsulates functionality for the about menu item. */
		void DoAbout();
};

/* CREATE_PMINTERFACE
 Binds the C++ implementation class onto its
 ImplementationID making the C++ code callable by the
 application.
*/
CREATE_PMINTERFACE(KFCActionComponent, kKFCActionComponentImpl)

namespace
{
// A Windows-only toggle that acts on one of InDesign's windows (each case below that calls this): flipped, put on
// whatever window it can reach, and the status line says which it came to. The wording follows whether a window was
// actually reached - ticking one with nothing to put it on (the panel docked, the dialog closed, the search field
// hidden) is legitimate, so that case is stated in words (notReached) rather than left looking broken.
void FlipWindowToggle(bool16 (*isOn)(), void (*setOn)(bool16), bool16 (*apply)(),
	const char* name, const char* notReached)
{
	const bool16 on = !isOn();
	setOn(on);
	const bool16 applied = apply();
	PMString msg(name);
	msg.Append(!on ? ": off." : (applied ? ": on." : notReached));
	msg.SetTranslatable(kFalse);
	KFCResultTree::ShowStatus(msg);
}

// THE ROW A RIGHT-CLICK MENU WAS POPPED OVER is the row's own widget: KFCResultNodeEH::RButtonDn hands it to
// HandlePopupMenu, and the menu hands it on to this component's widget argument - in UpdateActionStates as the menu is
// drawn and in DoAction as an item is chosen. It is how the product's row menus read their row (conditionaltextui's
// ConditionTagTVNodeContextMenu.cpp, buttonui's MSOPanelMenuAction.cpp: ITreeNodeIDData off the widget). An item
// pressed by its ActionID - a script's invoke - comes with no widget and so with no row: it is grey and does nothing
// (cases ctx-hit-stash-story-rclick and ctx-book-stash-docsearch).
NodeID RightClickedRow(IPMUnknown* widget)
{
	InterfacePtr<const ITreeNodeIDData> nodeData(widget, UseDefaultIID());
	return (nodeData != nil) ? nodeData->Get() : NodeID();
}

// The HIT row right-clicked, while the list still holds it. false for any other row, and for none.
bool RightClickedHit(IPMUnknown* widget, int32& outChapter, int32& outHit)
{
	const NodeID node(RightClickedRow(widget));
	TreeNodePtr<KFCResultNodeID> row(node);
	bool replaced = false, locked = false;
	if (row == nil || !row->IsHitRow() || !KFCResults()->GetHitFlags(row->GetChapter(), row->GetHit(), replaced, locked))
		return false;
	outChapter = row->GetChapter();
	outHit = row->GetHit();
	return true;
}

// The STORY row right-clicked, while the list still holds it. false for any other row, and for none.
bool RightClickedStory(IPMUnknown* widget, int32& outChapter, int32& outGroup)
{
	const NodeID node(RightClickedRow(widget));
	TreeNodePtr<KFCResultNodeID> row(node);
	if (row == nil || !row->IsFontRow() || !KFCResults()->IsStoryGroup(row->GetChapter(), row->GetFont()))
		return false;
	outChapter = row->GetChapter();
	outGroup = row->GetFont();
	return true;
}

// The DOCUMENT row right-clicked, while the list still holds it. false for any other row, and for none.
bool RightClickedDocument(IPMUnknown* widget, int32& outChapter)
{
	const NodeID node(RightClickedRow(widget));
	TreeNodePtr<KFCResultNodeID> row(node);
	PMString name;
	int32 hits = 0;
	if (row == nil || !row->IsDocumentRow() || !KFCResults()->GetChapterDisplay(row->GetChapter(), name, hits))
		return false;
	outChapter = row->GetChapter();
	return true;
}

// The row Select All Rows is for: the book, document or story row right-clicked; an empty NodeID for any other row, and
// for none. The menu's enabled state and the action ask it - one answer for both.
NodeID SelectAllRowsTarget(IPMUnknown* widget)
{
	int32 chapter = -1, group = -1;
	if (RightClickedStory(widget, chapter, group))
		return KFCResultNodeID::CreateFont(chapter, group);
	if (RightClickedDocument(widget, chapter))
		return KFCResultNodeID::Create(chapter);
	const NodeID node(RightClickedRow(widget));
	TreeNodePtr<KFCResultNodeID> row(node);
	if (row != nil && row->IsBookRow() && KFCResults()->IsFromBook())
		return KFCResultNodeID::CreateBook();
	return NodeID();
}

// The TIER whose own row was right-clicked (v2 R9) - a grey one's number, or -1 for the live tier - while the list still
// has it. false for any other row, and for none.
bool RightClickedTier(IPMUnknown* widget, int32& outTier)
{
	const NodeID node(RightClickedRow(widget));
	TreeNodePtr<KFCResultNodeID> row(node);
	if (row == nil || !row->IsTierRow())
		return false;
	const int32 tier = row->GetTier();
	if (tier >= 0 ? tier >= KFCResultHistory::KeptTierCount() : !KFCResultHistory::HasLiveList())
		return false;
	outTier = tier;
	return true;
}
}

/* KFCActionComponent Constructor
*/
KFCActionComponent::KFCActionComponent(IPMUnknown* boss)
: CActionComponent(boss)
{
}

/* DoAction
*/
void KFCActionComponent::DoAction(IActiveContext* ac, ActionID actionID, GSysPoint /*mousePoint*/, IPMUnknown* widget)
{
	KFC_DIAG_HIDDEN_DOCS_CHECK("action");		// (test builds only - a document left with no window when the action is over)
	switch (actionID.Get())
	{

		case kKFCPopupAboutThisActionID:
		case kKFCAboutActionID:
		{
			this->DoAbout();
			break;
		}

		case kKFCHowToActionID:
		{
			// The whole reference, in a scrollable dialog. Everything about it - which language,
			// the ScriptUI window, the CAlert fallback - is in KFCHowTo.cpp; nothing to decide here.
			KFCHowTo::Show();
			break;
		}

		case kKFCOpenFindChangeActionID:
		{
			// Open Find/Change... (the author's call): InDesign's own Edit > Find/Change dialog,
			// opened from the panel - with no document open as well (a book alone on screen), where the
			// Edit menu greys it out. The dialog itself needs no document; only the menu's enabling asks
			// for one, and a panel item has its own. It runs the product's action through the action
			// manager, the way linksui's buttons run theirs (LinksUIButtonObserver.cpp) - so the dialog
			// opens exactly as Edit > Find/Change opens it, and nothing of InDesign's own menu is changed.
			// The context and the action alone, as every product caller passes them: the flyout's mouse point
			// and widget are not the dialog's (IActionManager.h - kInvalidMousePoint when not relevant, and a
			// widget never assumed to be the receiver's own).
			// (Not an IActionFilter taking the "needs a document" bit off the Edit menu's action: the
			// author's call - KFC leaves InDesign's menu alone.)
			// *A MINIMISED DIALOG IS BROUGHT BACK, NOT CLOSED (the author's call): that action is a toggle,
			//  and on a minimised dialog it closes it (measured).
			if (KFCRestoreMinimizedFindChange())
				break;
			InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
			InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
			if (actionMgr != nil)
				actionMgr->PerformAction(ac, kFindDialogActionID);
			break;
		}

		case kKFCSearchBookActionID:
		{
			// Search the target book - the Book panel's, or the active one as a fallback
			// (KFCBookScope::ResolveTargetBook) - or what Find/Change's Search: names, with the user's
			// current Find/Change query. The engine fills KFCResultModel with the hits (grouped by
			// chapter) behind a modal progress bar; the tree is drawn here, once, when it returns.
			//
			// No re-entry test here: the ENGINE has one (and so does every other run of ours), and
			// it puts a reason on the status line. A command reaching here while a run is up - the
			// bar pumps events, so it can - says why instead of looking broken. See KFCRunGuard.
			// A LANDING IS THE UI HALF'S, AND THE ENGINE CANNOT SEE IT (KFCJump::IsLanding): its message loop can dispatch this
			// command (a shortcut, the Application Bar's Return), and the search's commit point closes the windowless chapter
			// the landing is giving a window. Dropped, as a second click is.
			if (KFCJump::IsLanding())
				break;

			// The search through the one door a search into the panel takes (KFCResultHistory::Find): the tab's scope
			// written first, the list drawn once it returns, the status line set - and the list it replaces kept as a tier
			// above it, named after the query (the author's calls, 10-14: always kept, no toggle).
			PMString summary;
			(void)KFCResultHistory::Find(KFCResultHistory::QueryLabel(), summary);
			break;
		}

		case kKFCChangeAllActionID:
		{
			// Change All in Book (No List) (the author's calls - F6 and F18): InDesign's own Change All over
			// the book's chapters, no list. The engine keeps its own doors and says why on a refusal (KFCChangeAll::Run).
			// Not while a landing is under way - Find's reason (KFCJump::IsLanding).
			if (KFCJump::IsLanding())
				break;
			KFCPanelTitle::Update();
			PMString summary;
			const uint32 setBefore = KFCResults()->GetResultSetId();
			(void)KFCRuns()->ChangeAll(summary);
			// A run that went through made the list anew (its commit point): the tiers kept above it go with it (the stacked
			// results' spec, section 2-4).
			KFCResultHistory::DropStackIfRemade(setBefore);
			// Drawn again: a run that got past its doors threw the results away (a search's book row that found nothing -
			// KFCChangeAll::Run's commit point), and a refusal left them as they were, which a rebuild draws the same.
			KFCResultTree::Rebuild();
			KFCResultTree::ShowStatus(summary);
			break;
		}

		case kKFCRunSavedQueriesActionID:
		{
			// Run Saved Queries... (docs/superpowers/specs/_done/2026-10-07-kfc-query-dialog-and-selected-documents-design.md):
			// the query dialog, modeless - this returns as soon as it stands. Its Run runs from inside it (KFCQueryDialog.cpp).
			KFCQueryDialogOpen();
			break;
		}

		case kKFCClearResultsActionID:
		{
			// Clear Results: the list emptied, so Change All can run (C8 of
			// docs/superpowers/specs/_done/2026-10-05-kfc-change-all-no-list-design.md). The documents are not touched.
			// Not while a landing is under way either (KFCJump::IsLanding): the rows it is landing on would go from under it.
			if (KFCJump::IsLanding() || KFCResultTree::RefusedWhileRunning())
				break;
			PMString status;
			// The tiers kept above the list go (the stacked results' spec, section 2-4 - Clear Results is how the stack starts
			// again), and the list through the model's door, which clears a list with a match in it and says so. A stack over a
			// list with none (a query that found nothing) goes alone - the model keeps a list of none as it is.
			const bool stacked = KFCResultHistory::DropStack();
			if (KFCResults()->GetTotalHitCount() > 0 || !stacked)
				(void)KFCRuns()->ClearResults(status);
			else
			{
				status = PMString("Results cleared.");
				status.SetTranslatable(kFalse);
			}
			KFCResultTree::Rebuild();
			KFCResultTree::ShowStatus(status);
			break;
		}

		// (No Find Missing Glyphs / Find Overset: removed on the author's call - the Book panel's
		//  preflight reports both over the whole book.)

		case kKFCScopeBookActionID:
		{
			// Toggle the search scope: the whole target book (see ResolveTargetBook), or what Find/Change's
			// Search: names. Just the flag - nothing is closed and the current results stay put. Its check mark and the
			// search command's name are drawn in UpdateActionStates.
			KFCChapters()->SetBookScopeOn(!KFCChapters()->IsBookScopeOn());
			// The flyout closes with the click, so the scope it just set is written where it stays
			// readable: the panel's own tab.
			KFCPanelTitle::Update();
			break;
		}

		case kKFCSelectedDocumentsActionID:
		{
			// Find/Change Selected Documents (Book) (the query dialog's spec, G9): just the flag, like Book Scope - which
			// documents a run takes is read when it starts, from the Book panel's selection at that moment. Its check mark
			// and its grey (Book Scope off) are drawn in UpdateActionStates; the query dialog's Runs on: line follows
			// through KFCPanelTitle::Update.
			KFCChapters()->SetSelectedDocumentsOn(!KFCChapters()->IsSelectedDocumentsOn());
			KFCPanelTitle::Update();
			break;
		}

		case kKFCHidePrevChapterActionID:
		{
			// Toggle the session flag that JumpToHit reads. Its check mark is drawn in
			// UpdateActionStates.
			KFCJump::ToggleHidePreviousChapter();
			break;
		}

		// "Translucent Panel": draw this panel faint (alpha kKFCPanelAlphaValue = 77, about 30%) so
		// the document underneath stays readable, and bring it back to solid while the pointer is on
		// it. *Windows only, OFF by default. It takes effect while the panel FLOATS, and while it is
		// pulled out of an icon as a drawer; docked and expanded it can still be ticked but nothing
		// looks different - the flag is set, and it applies the moment the panel floats again (that
		// following is done by the observer in KFCPanelAlpha.cpp, on kPaletteVisibilityChangedMessage).
		case kKFCTranslucentPanelActionID:
			FlipWindowToggle(KFCGetPanelTranslucent, KFCSetPanelTranslucent, KFCApplyPanelTranslucency,
				"Translucent panel", ": on - has no effect while the panel is docked.");
			break;

		// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog - the
		// window this plug-in takes its query from, so having it fade out of the way while the
		// document is read is the point of it. *Windows only, OFF by default.
		// The dialog is found through the SDK's window list (never by its title, which is translated),
		// so this works whatever language InDesign is running in. See KFCPanelAlpha.cpp.
		case kKFCTranslucentFindChangeActionID:
			FlipWindowToggle(KFCGetFindChangeTranslucent, KFCSetFindChangeTranslucent, KFCApplyFindChangeTranslucency,
				"Translucent Find/Change", ": on - applies when the Find/Change dialog is open.");
			break;

		// "Minimizable Find/Change": a minimize box on InDesign's OWN Find/Change dialog, so it can
		// be put on the taskbar instead of closed. The same window as the toggle above, found the
		// same way (never by its title, which is translated). *Windows only, OFF by default.
		// What it takes is two bits - see KFCFindChangeMinimize.h for why one of them is not the
		// obvious one.
		case kKFCMinimizableFindChangeActionID:
			FlipWindowToggle(KFCGetFindChangeMinimizable, KFCSetFindChangeMinimizable, KFCApplyFindChangeMinimizable,
				"Minimizable Find/Change", ": on - applies when the Find/Change dialog is open.");
			break;

		// "Link the Application Bar's Search Field to This Panel" (the author's design): the search field of
		// InDesign's application bar shows the query of the tab Find/Change is on, and Return in it searches
		// with this panel (the triangle does not choose the tab).
		// *Windows only, OFF by default.
		// Everything - what was measured, the hook, why that field's own menu could not take an item - is in
		// KFCAppBarSearchEnter.h.
		case kKFCAppBarSearchEnterActionID:
			FlipWindowToggle(KFCGetAppBarSearchEnter, KFCSetAppBarSearchEnter, KFCApplyAppBarSearchEnter,
				"Application Bar link", ": on - works while the Application Bar's search field is shown.");
			break;

		// "Remember Book Panel Placement": InDesign's own Book panel is measured as it closes (and
		// when InDesign quits) and put back where it was when it next appears. OFF by default.
		// *Flipping it WRITES ITS OWN KEY to the settings file at once, and the status line says
		// whether that worked (the author's rules) - unlike the toggles above, which wait
		// for "Save Panel Settings". Everything is in KFCBookPanelPlacement.cpp.
		case kKFCRememberBookPanelActionID:
		{
			PMString msg;
			KFCBookPanelPlacement::ToggleAndSave(msg);
			KFCResultTree::ShowStatus(msg);
			break;
		}

		// "Save Panel Settings": write the settings toggles above to a JSON file of our own in the
		// user's preferences folder; it is read back at startup. Explicit rather than automatic, the
		// way KCM has it - a setting the user did not ask to keep is one they cannot account for
		// later. Everything, including where the file goes and what it reports, is in
		// KFCPanelState.cpp.
		case kKFCSavePanelSettingsActionID:
		{
			KFCSavePanelState();
			break;
		}

		case kKFCReplaceHitActionID:
		{
			// A hit row's right-click menu - the row it was popped over (RightClickedHit). No hit row (the action pressed by
			// its ActionID): do nothing. Return on a selected row writes through the same door (KFCResultTreeEH::KeyDown).
			// The row right-clicked is one of several rows selected together: all of them (the author's calls - object
			// rows, then text rows). Any other row: that row alone (KFCResultTree::RowsOfRightClick).
			int32 chapter = -1, hit = -1;
			if (RightClickedHit(widget, chapter, hit))
				(void)KFCResultTree::ReplaceRows(KFCResultTree::RowsOfRightClick(chapter, hit));
			break;
		}

		case kKFCSearchStoryAgainActionID:
		{
			// A story row's right-click menu - the row it was popped over. No story row: do nothing.
			int32 chapter = -1, group = -1;
			if (RightClickedStory(widget, chapter, group))
				(void)KFCResultTree::SearchStoryAgain(chapter, group);
			break;
		}

		case kKFCSearchDocumentAgainActionID:
		{
			// A document row's right-click menu - the row it was popped over. No document row: do nothing.
			int32 chapter = -1;
			if (RightClickedDocument(widget, chapter))
				(void)KFCResultTree::SearchDocumentAgain(chapter);
			break;
		}

		case kKFCSelectAllRowsActionID:
		{
			// A book, document or story row's right-click menu - the row it was popped over. None of those: do nothing.
			const NodeID row(SelectAllRowsTarget(widget));
			if (row.IsValid())
				(void)KFCResultTree::SelectAllRows(row);
			break;
		}

		case kKFCClearTheseResultsActionID:
		{
			// A tier's own row's right-click menu (v2 R9) - the tier it was popped over, alone: a grey tier, or the live list
			// (the grey tiers stay). Not under a landing or a run, as Clear Results.
			int32 tier = -1;
			if (KFCJump::IsLanding() || KFCResultTree::RefusedWhileRunning() || !RightClickedTier(widget, tier))
				break;
			(void)KFCResultHistory::ClearTier(tier);
			break;
		}

		case kKFCSearchTierAgainActionID:
		{
			// A tier's own row's right-click menu (the author, 2026-10-14 night): that tier's own search run once more. A grey
			// tier is switched to first - the live list, as a click on its heading makes it (a refusal says why on the status
			// line) - then searched again (KFCResultTree::SearchListAgain - the model reads the list's query, scope and
			// documents). Not under a landing or a run: both doors refuse.
			int32 tier = -1;
			if (!RightClickedTier(widget, tier))
				break;
			if (tier >= 0)
			{
				NodeID liveRow;
				if (!KFCResultHistory::SwitchToKeptRow(tier, -1, liveRow))
					break;
			}
			(void)KFCResultTree::SearchListAgain();
			break;
		}

		case kKFCCreateOutlinesActionID:
		{
			// An OBJECT row's Special Operations > Create Outlines (the author, 2026-10-17): the row it was popped over - or the
			// rows selected together, when it is one of them (RowsOfRightClick, as Replace) - outlined wherever they stand
			// (KFCResultTree::OutlineRows). No object row (the action pressed by its ActionID): do nothing.
			int32 chapter = -1, hit = -1;
			if (RightClickedHit(widget, chapter, hit) && KFCResults()->GetHitItem(chapter, hit) != kInvalidUID)
				(void)KFCResultTree::OutlineRows(KFCResultTree::RowsOfRightClick(chapter, hit));
			break;
		}

		case kKFCRestoreOriginalActionID:
		{
			// A TEXT row's Restore Original (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md): the row it was
			// popped over - or the rows selected together, when it is one of them (RowsOfRightClick, as Replace) - their
			// copies put back (KFCResultTree::RestoreRows). No hit row (the action pressed by its ActionID): do nothing.
			int32 chapter = -1, hit = -1;
			if (RightClickedHit(widget, chapter, hit))
				(void)KFCResultTree::RestoreRows(KFCResultTree::RowsOfRightClick(chapter, hit));
			break;
		}

		default:
		{
			break;
		}
	}
}

/* DoAbout
*/
void KFCActionComponent::DoAbout()
{
	CAlert::ModalAlert
	(
		// The About box is the plug-in's name and version, the same in every UI language (the author's call) - as is
		// everything from the string table; only the How to Use page's body is switched (KFCLoc::JapaneseUI). The
		// name and the version are not words - translating them would be translating an identifier.
		//
		// The KEY goes in as a key, and the alert translates it: CAlert.h:70 says the message
		// "will be translated unless the string has been translated already or isn't
		// translatable". That is the shape every About box in the SDK samples has
		// (candlechartui/CdlChtUIActionComponent.cpp:192, and the same line in every other
		// Dolly-generated component). (A PMString(key, kTranslateDuringCall) built first gives the
		// same answer by a longer road.)
		kKFCAboutBoxStringKey,					// Alert string
		kOKString, 						// OK button
		kNullString, 						// No second button
		kNullString, 						// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon				// Information icon.
	);
}


/* UpdateActionStates
*/
void KFCActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* widget)
{
	// A run of ours is standing - which ones count is KFCRunGuard::IsAnyRunning's to say (a row's Replace among them,
	// with no bar of its own). Each runs the message loop - a modal progress bar pumps events, a chapter being opened
	// does too - so this list can be asked for its states from inside the run: lock everything until it returns. A
	// second walk underneath one would Halt() its walker mid-walk.
	if (KFCRuns()->IsAnyRunning())
	{
		for (int32 i = 0; i < listToUpdate->Length(); i++)
			listToUpdate->SetNthActionState(i, kDisabled_Unselected);
		return;
	}

	// Is there anything for the current scope to run on at all - the target book, with a chapter in it,
	// while Book Scope is ON, the active document (IActiveContext) while it is OFF? The commands below that start a
	// run - Find, and Change All in Book - ask it, so it is taken once here. See
	// KFCBookScope::HasScopeTarget: it asks what the engines themselves ask, so a command that is offered
	// can always run and one that cannot is visibly grey rather than reporting "No open document to
	// search." after the fact (the author's call).
	//
	// It is asked HERE rather than declared in the .fr as kDisableIfNoFrontDocument for two reasons
	// that are written out beside the action definitions: that flag would grey the commands out with
	// a book open and no document window - the state a book run is FOR - and it skips this hook, so
	// the search command would also lose the name that carries the scope.
	const bool16 haveTarget = KFCChapters()->HasScopeTarget() ? kTrue : kFalse;

	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);

		switch (action.Get())
		{
			case kKFCSearchBookActionID:
			{
				// The command's own name carries the scope, so it is visible BEFORE running it:
				// "Find in Book" while Book Scope is ON; with it off, the Search: of Edit > Find/Change it
				// follows ("Find in Document", "Find in Story", "Find to End of Story" ...).
				// No check mark - this is the KESCL Start/Stop pattern (a name swap, not a state mark).
				//
				// FIND, not "Search": Adobe's verb for this is Find - the dialog this
				// command takes its query from is Find/Change, its buttons are Find Next and Change All -
				// while "Search" is the label on that dialog's SCOPE popup ("Search: Document"). The
				// action's resource name stays "Search" - the menu shows it only while this line has not run (a run's grey,
				// above, leaves the names as they were). The new name is the action's own, not this drawing's (measured), so
				// a script reaches this by its ActionID, as the regression does - app.menuActions.itemByName("Search") finds
				// it only until the menu is first drawn or its state first read.
				PMString name(KFCRuns()->FindCommandName(KFCChapters()->IsBookScopeOn()));
				name.SetTranslatable(kFalse);
				listToUpdate->SetNthActionName(i, name);
				// The name is written whether or not it can run, so a greyed-out item still says which
				// scope it would have used.
				// ...and grey on the Colour tab too (the author's call): it finds by colour, which this panel
				// does not list (the Object tab's page items it does). The same question the search asks
				// (CanSearchTab).
				const bool canRun = haveTarget
					&& KFCRuns()->CanSearchTab(KFCRuns()->CurrentSearchMode());
				listToUpdate->SetNthActionState(i, canRun ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCChangeAllActionID:
			{
				// Change All in Book (No List): the book's alone (F18 - a document's Change All is InDesign's own dialog's).
				// Its name says when the run takes only the Book panel's selected documents - "Change All in Selected
				// Documents (No List)" (the query dialog's spec, section 4-4) - from the model, as Find's is. Grey with
				// Book Scope off, while the panel holds a list (Clear Results first), with no book or chapter to run on
				// (haveTarget), on the Colour tab, and with nothing to find (on the Object tab: an empty Find Object
				// Format) - the spec's section 4; a run standing up greys everything above.
				const bool canRun = KFCChapters()->IsBookScopeOn()
					&& haveTarget
					&& KFCResults()->GetTotalHitCount() <= 0
					&& KFCRuns()->CanSearchTab(KFCRuns()->CurrentSearchMode())
					&& KFCRuns()->HasFindQueryNow();
				listToUpdate->SetNthActionState(i, canRun ? kEnabledAction : kDisabled_Unselected);
				PMString name(KFCRuns()->ChangeAllCommandName());
				name.SetTranslatable(kFalse);
				listToUpdate->SetNthActionName(i, name);
				break;
			}
			case kKFCRunSavedQueriesActionID:
			{
				// Run Saved Queries...: always - putting queries in an order needs no document, and the dialog
				// itself says what Run would run on. (A run standing greys it with everything else, above.)
				listToUpdate->SetNthActionState(i, kEnabledAction);
				break;
			}
			case kKFCClearResultsActionID:
			{
				// Clear Results: while the panel holds a list - or tiers kept above one (the stacked results' spec, section
				// 2-4: a query that found nothing leaves a list of none, and the stack must still be clearable).
				const bool something = KFCResults()->GetTotalHitCount() > 0 || KFCResultHistory::IsStacking();
				listToUpdate->SetNthActionState(i, something ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCScopeBookActionID:
			{
				int16 actionState = kEnabledAction;
				if (KFCChapters()->IsBookScopeOn())
					actionState |= kSelectedAction;		// ON: show the check mark (OFF is the default)
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCSelectedDocumentsActionID:
			{
				// Find/Change Selected Documents (Book): it narrows Book Scope, so it is offered only with Book Scope
				// on (the author: it takes effect only then). Its check mark stays visible while it is grey, as Hide Previous
				// Chapter's does, so a setting kept for the next book run is not hidden.
				int16 actionState = KFCChapters()->IsBookScopeOn() ? kEnabledAction : kDisabled_Unselected;
				if (KFCChapters()->IsSelectedDocumentsOn())
					actionState |= kSelectedAction;
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCHidePrevChapterActionID:
			{
				// Reachable whenever the sweep it gates can RUN - which is the sweep's own question,
				// KFCJump's ShouldHidePreviousChapter, asked of the results on screen (IsFromBook) -
				// and also while Book Scope is on, so it can be set up before the search. Not
				// IsBookScopeOn() ALONE: after a book search with the scope since switched off, the
				// sweep would keep closing documents on every jump while the menu that stops it sat
				// grey (the lock-out KFCJump.cpp's ShouldHidePreviousChapter describes). The check mark
				// stays visible through the lock
				// (kSelectedAction without kEnabledAction), like KESCL's locked "Search book".
				const bool reachable = KFCChapters()->IsBookScopeOn() || KFCResults()->IsFromBook();
				int16 actionState = reachable ? kEnabledAction : kDisabled_Unselected;
				if (KFCJump::IsHidePreviousChapterOn())
					actionState |= kSelectedAction;		// show the check mark when ON
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCTranslucentPanelActionID:
			{
				// *Selectable even while the panel is docked - deliberately NOT greyed out (the author's
				// call, in KCM). Where the click has no visible result, DoAction says so on
				// the status line instead.
				int16 actionState = kEnabledAction;
				if (KFCGetPanelTranslucent())
					actionState |= kSelectedAction;		// show the check mark when ON
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCTranslucentFindChangeActionID:
			{
				// Selectable whether or not the Find/Change dialog is open, for the same reason the
				// panel's toggle stays selectable while docked: what is being set is the preference, and
				// it applies the moment the window exists. DoAction says which case it was.
				int16 actionState = kEnabledAction;
				if (KFCGetFindChangeTranslucent())
					actionState |= kSelectedAction;		// show the check mark when ON
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCMinimizableFindChangeActionID:
			{
				// Selectable whether or not the dialog is open, exactly like the toggle above: what is
				// being set is the preference, and it applies the moment the window exists.
				int16 actionState = kEnabledAction;
				if (KFCGetFindChangeMinimizable())
					actionState |= kSelectedAction;		// show the check mark when ON
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCAppBarSearchEnterActionID:
			{
				// Selectable whether or not the field is shown, like the toggles above: the flag is what is being set.
				int16 actionState = kEnabledAction;
				if (KFCGetAppBarSearchEnter())
					actionState |= kSelectedAction;		// show the check mark when ON
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCRememberBookPanelActionID:
			{
				// Selectable with no book open, like the toggles above: the flag is what is being set,
				// and it acts the next time a book panel closes or appears.
				int16 actionState = kEnabledAction;
				if (KFCBookPanelPlacement::IsOn())
					actionState |= kSelectedAction;		// show the check mark when ON
				listToUpdate->SetNthActionState(i, actionState);
				break;
			}
			case kKFCReplaceHitActionID:
			{
				// A hit row's menu: Replace while the row is a Find/Change match not yet replaced, not locked and
				// with nothing said about it (KFCReplaceEngine::CanReplaceHit).
				// Several rows selected together, the right-clicked among them: while any of them can be - the rows and the
				// question the Replace itself takes (RowsOfRightClick, CanReplaceAny).
				int32 chapter = -1, hit = -1;
				const bool enable = RightClickedHit(widget, chapter, hit)
					&& KFCResultTree::CanReplaceAny(KFCResultTree::RowsOfRightClick(chapter, hit));
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCSearchStoryAgainActionID:
			{
				// A story row's menu: Search This Story Again over a story row (a run standing greys everything, above).
				// What the engine refuses (a changed query, a search over part of a story) it says on the status line
				// rather than greying the item, so the user learns why (KFCSearchEngine::SearchStoryAgain).
				int32 chapter = -1, group = -1;
				const bool enable = RightClickedStory(widget, chapter, group);
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCSearchDocumentAgainActionID:
			{
				// A document row's menu: Search This Document Again over a document row - GREYED OVER A SEARCH OF PART OF A
				// DOCUMENT (Search: Story, To End of Story, Selection - the author, 2026-10-17 evening: "greyed may be
				// fine"): it walks a whole document, and such a list is searched again as it was searched by its story rows
				// and its heading. Its other refusals (a changed query) said on the status line, as the story row's are
				// (KFCSearchEngine::SearchDocumentAgain - which still refuses a part, for a caller that never opened the menu).
				int32 chapter = -1;
				const KFCResultModel::SearchScopeKind scope = KFCResults()->GetSearchScope();
				const bool overPart = (scope == KFCResultModel::kScopeStory || scope == KFCResultModel::kScopeToEndOfStory
					|| scope == KFCResultModel::kScopeSelection);
				const bool enable = !overPart && RightClickedDocument(widget, chapter);
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCSelectAllRowsActionID:
			{
				// A book, document or story row's menu: Select All Rows over one of those rows with a hit row under it -
				// disabled on one with none (a document row Search This Document Again left at (0/0), a book search's that
				// found nothing) rather than doing nothing: the author's call.
				const NodeID row(SelectAllRowsTarget(widget));
				const bool enable = row.IsValid() && KFCResultTree::CanSelectAllRows(row);
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCClearTheseResultsActionID:
			{
				// A tier's own row's menu: Clear These Results over a tier's row (a run standing greys everything, above).
				int32 tier = -1;
				const bool enable = RightClickedTier(widget, tier);
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCSearchTierAgainActionID:
			{
				// A tier's own row's menu: Search These Results Again over a tier's row - its refusals (a part of a document, a
				// document or book not open, a list no search made) said on the status line, as Search This Document Again's.
				int32 tier = -1;
				const bool enable = RightClickedTier(widget, tier);
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCCreateOutlinesActionID:
			{
				// An object row's menu: Create Outlines while the row - or one of the rows selected together with it - can be
				// outlined (CanOutlineAny - its doors, read only: greyed when none can, the author's wish of 2026-10-17; a run
				// standing greys everything, above). A row of a closed chapter counts as one that can: pressing reaches it, and
				// what the write then refuses it says on the status line, row by row counted.
				int32 chapter = -1, hit = -1;
				const bool enable = RightClickedHit(widget, chapter, hit)
					&& KFCResultTree::CanOutlineAny(KFCResultTree::RowsOfRightClick(chapter, hit));
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			case kKFCRestoreOriginalActionID:
			{
				// A text row's menu: Restore Original while the row - or one of the rows selected together with it - is a text
				// row a Replace wrote and its copy is held (CanRestoreAny - the spec's C7: greyed when none is, the author's
				// wish of 2026-10-17). What only the document can tell it says on the status line when pressed (edited since,
				// the rest changed, locked), as Create Outlines says its refusals.
				int32 chapter = -1, hit = -1;
				const bool enable = RightClickedHit(widget, chapter, hit)
					&& KFCResultTree::CanRestoreAny(KFCResultTree::RowsOfRightClick(chapter, hit));
				listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
				break;
			}
			default:
			{
				break;
			}
		}
	}
}


