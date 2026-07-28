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

// Interface includes:
#include "IFindChangeOptions.h"	// the find / change strings the confirmation prompt names

// General includes:
#include "CActionComponent.h"
#include "CAlert.h"
#include "CoreResTypes.h"		// kLineSeparatorString - the prompt is several lines
#include "IActionStateList.h"	// UpdateActionStates: check mark for the Hide Previous Chapter toggle
#include "PreferenceUtils.h"	// QuerySessionPreferences
#include "Utils.h"

// Project includes:
#include "KBSID.h"
#include "KBSSearchEngine.h"
#include "KBSResultTree.h"		// rebuild the result tree after a search
#include "KBSJump.h"			// the Hide Previous Chapter toggle lives with the jump logic
#include "KBSBookScope.h"		// the Book Scope toggle's session state
#include "KBSResultModel.h"		// the check state Check All / Uncheck All flips
#include "KBSReplaceEngine.h"	// Change Checked
#include "KBSPanelTitle.h"		// the panel's tab name carries the current scope

/** Implements IActionComponent; performs the actions that are executed when the plug-in's
	menu items are selected.

	
	@ingroup kohakubooksearch

*/
class KBSActionComponent : public CActionComponent
{
public:
/**
 Constructor.
 @param boss interface ptr from boss object on which this interface is aggregated.
 */
		KBSActionComponent(IPMUnknown* boss);

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

			/** Custom-enabled actions (the Hide Previous Chapter toggle) get their check mark here. */
			virtual void UpdateActionStates(IActiveContext* ac, IActionStateList* listToUpdate, GSysPoint mousePoint, IPMUnknown* widget);

	private:
		/** Encapsulates functionality for the about menu item. */
		void DoAbout();

		/** Ask before replacing. The prompt names the find and change strings - they come from the
		    Find/Change dialog, not from this panel, so this is the only place the user sees what is
		    about to be written - and how many hits will change. An empty change string is spelled
		    out: deleting the matches is a legitimate request, but never a surprise.
		    @return true to go ahead. */
		bool ConfirmReplace(int32 checkedCount);
		


};

/* CREATE_PMINTERFACE
 Binds the C++ implementation class onto its
 ImplementationID making the C++ code callable by the
 application.
*/
CREATE_PMINTERFACE(KBSActionComponent, kKBSActionComponentImpl)

/* KBSActionComponent Constructor
*/
KBSActionComponent::KBSActionComponent(IPMUnknown* boss)
: CActionComponent(boss)
{
}

/* DoAction
*/
void KBSActionComponent::DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint, IPMUnknown* widget)
{
	switch (actionID.Get())
	{

		case kKBSPopupAboutThisActionID:
		case kKBSAboutActionID:
		{
			this->DoAbout();
			break;
		}

		case kKBSSearchBookActionID:
		{
			// Search the active book (or the front document) with the user's current Find/Change
			// query. The engine fills KBSResultModel with the hits (grouped by chapter) behind a
			// modal progress bar; the tree is drawn here, once, when it returns.
			if (KBSSearchEngine::IsSearching())
				break;		// already running - the bar pumps events, so this can be reached

			// Before the search rather than after: the progress bar is modal, and the tab stays in
			// view behind it. This is also what puts the name back when the panel has been closed
			// and reopened since the last toggle.
			KBSPanelTitle::Update();

			PMString summary;
			KBSSearchEngine::SearchBook(summary);
			KBSResultTree::Rebuild();
			KBSResultTree::ShowStatus(summary);
			break;
		}

		case kKBSScopeBookActionID:
		{
			// Toggle the search scope: the whole active book, or just the front document. Just the
			// flag - nothing is closed and the current results stay put. Its check mark and the
			// search command's name are drawn in UpdateActionStates.
			KBSBookScope::SetBookScopeOn(!KBSBookScope::IsBookScopeOn());
			// The flyout closes with the click, so the scope it just set is written where it stays
			// readable: the panel's own tab.
			KBSPanelTitle::Update();
			break;
		}

		case kKBSHidePrevChapterActionID:
		{
			// Toggle the session flag that JumpToHit reads. Its check mark is drawn in
			// UpdateActionStates.
			KBSJump::ToggleHidePreviousChapter();
			break;
		}

		case kKBSReplaceCheckedActionID:
		{
			// A replace can touch several documents and only undoes chapter by chapter, so it asks
			// first. It also sits in a flyout that gets opened by accident, which is the other
			// reason the confirmation is not optional.
			const int32 checkedCount = KBSResultModel::GetCheckedCount();
			if (checkedCount <= 0)
			{
				PMString nothing("Nothing checked.");
				nothing.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(nothing);
				break;
			}
			if (!this->ConfirmReplace(checkedCount))
				break;

			PMString summary;
			KBSReplaceEngine::ReplaceChecked(summary);
			KBSResultTree::Rebuild();		// replaced rows lose their box and fade
			KBSResultTree::ShowStatus(summary);
			break;
		}

		case kKBSCheckAllActionID:
		case kKBSUncheckAllActionID:
		{
			// Select / deselect every stored hit - including the ones past the panel's 500-row
			// display cap, which is why the status line spells the numbers out afterwards.
			KBSResultModel::SetAllChecked(actionID.Get() == kKBSCheckAllActionID);
			// Only what the rows DRAW changed - the tree's shape is untouched - so repaint them in
			// place instead of rebuilding. One notification per chapter, and the expansion state
			// survives (a chapter the user collapsed stays collapsed).
			KBSResultTree::RefreshRows();
			KBSResultTree::ShowCheckedStatus();
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
void KBSActionComponent::DoAbout()
{
	CAlert::ModalAlert
	(
		kKBSAboutBoxStringKey,				// Alert string
		kOKString, 						// OK button
		kNullString, 						// No second button
		kNullString, 						// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon				// Information icon.
	);
}

/* ConfirmReplace
*/
bool KBSActionComponent::ConfirmReplace(int32 checkedCount)
{
	InterfacePtr<IFindChangeOptions> opts(QuerySessionPreferences<IFindChangeOptions>());
	if (opts == nil)
	{
		// Without the Find/Change settings there is nothing to name in the prompt, and nothing to
		// replace with either. Say so on the status line: a menu command that produces no prompt
		// and no message reads as a broken plug-in.
		PMString unavailable("Find/Change settings are unavailable - nothing was changed.");
		unavailable.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(unavailable);
		return false;
	}
	// Read only - KBS never writes to the user's Find/Change settings.
	const IFindChangeOptions::SearchMode mode = opts->GetSearchMode();

	PMString msg;
	msg.SetTranslatable(kFalse);
	msg.Append("Change ");
	msg.AppendNumber(checkedCount);
	msg.Append(" checked hit(s)?");
	msg.Append(kLineSeparatorString);
	msg.Append(kLineSeparatorString);

	msg.Append("Find: ");
	PMString findStr(opts->GetFindString(mode));
	findStr.SetTranslatable(kFalse);
	msg.Append(findStr);
	if (mode == IFindChangeOptions::kGrepSearch)
		msg.Append("   (GREP)");
	msg.Append(kLineSeparatorString);

	msg.Append("Change to: ");
	PMString replaceStr(opts->GetReplaceString(mode));
	replaceStr.SetTranslatable(kFalse);
	if (replaceStr.IsEmpty())
		msg.Append("(empty - the matches will be deleted)");
	else
		msg.Append(replaceStr);
	msg.Append(kLineSeparatorString);
	msg.Append(kLineSeparatorString);
	msg.Append("The chapters are opened and left UNSAVED. Undo is one step per chapter.");

	// Cancel is the DEFAULT button: a stray Return on a flyout opened by accident must not rewrite
	// the book.
	return CAlert::ModalAlert(msg, kOKString, kCancelString, kNullString, 2, CAlert::eWarningIcon) == 1;
}

/* UpdateActionStates
*/
void KBSActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	// A search is running behind the modal progress bar. The bar pumps events, so this list can be
	// asked for its states from inside the search: lock everything until it returns.
	if (KBSSearchEngine::IsSearching())
	{
		for (int32 i = 0; i < listToUpdate->Length(); i++)
			listToUpdate->SetNthActionState(i, kDisabled_Unselected);
		return;
	}

	// Both counts walk every stored hit (up to the 5000 collect cap), and this list normally holds
	// more than one action that asks for them, so take each once here instead of per action.
	const int32 checkedCount = KBSResultModel::GetCheckedCount();
	const int32 checkableCount = KBSResultModel::GetCheckableCount();

	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);

		if (action == kKBSSearchBookActionID)
		{
			// The command's own name carries the scope, so it is visible BEFORE running it:
			// "Search Book" while Book Scope is ON, "Search Document" while it is OFF. No check
			// mark - this is the KESCL Start/Stop pattern (a name swap, not a state mark).
			PMString name(KBSBookScope::IsBookScopeOn() ? "Search Book" : "Search Document");
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			listToUpdate->SetNthActionState(i, kEnabledAction);
		}
		else if (action == kKBSScopeBookActionID)
		{
			int16 actionState = kEnabledAction;
			if (KBSBookScope::IsBookScopeOn())
				actionState |= kSelectedAction;		// ON: show the check mark (OFF is the default)
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSHidePrevChapterActionID)
		{
			// Only meaningful in book scope (it closes the chapter a previous jump landed in), so
			// it is greyed out in document scope. The check mark stays visible through the lock
			// (kSelectedAction without kEnabledAction), like KESCL's locked "Search book".
			int16 actionState = KBSBookScope::IsBookScopeOn() ? kEnabledAction : kDisabled_Unselected;
			if (KBSJump::IsHidePreviousChapterOn())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSReplaceCheckedActionID)
		{
			// Needs something checked. The Find/Change strings are deliberately NOT tested here -
			// the confirmation prompt shows them, so an empty change string (a valid "delete the
			// matches" request) still reaches the user instead of being greyed out unexplained.
			const bool16 canReplace = (checkedCount > 0) ? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, canReplace ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSCheckAllActionID || action == kKBSUncheckAllActionID)
		{
			// Nothing to check without results - and nothing to check after a replace either, where
			// the panel lists what CHANGED and no row has a box left. Both commands would be no-ops
			// there, so they go grey along with the boxes. Not a toggle - no check mark either way.
			const bool16 haveCheckable = (checkableCount > 0) ? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, haveCheckable ? kEnabledAction : kDisabled_Unselected);
		}
	}
}


