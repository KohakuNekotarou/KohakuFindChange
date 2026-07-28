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
#include "StringUtils.h"		// ::ReplaceStringParameters - fills the ^1 in a translated string
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
			// Already running, reached through the events its own progress bar pumps. The engine
			// stops this a second time on its own, but not before the confirmation prompt would
			// have gone up over a replace that is already under way.
			if (KBSReplaceEngine::IsReplacing())
				break;

			// A replace can touch several documents, so it asks first. It also sits in a flyout
			// that gets opened by accident, which is the other reason the confirmation is not
			// optional.
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

	// The prompt is assembled from string-table entries rather than C++ literals, so a Japanese
	// InDesign asks the question in Japanese (KBS.fr already routes k_jaJP to KBS_jaJP.fr). This is
	// the one place where the user authorises a rewrite of their text, so it is the one place worth
	// translating; the panel, its menu and its status line stay English on purpose, echoing the
	// official Find/Change wording.
	//
	// Each piece is translated BEFORE it is appended: a key only translates while it is the WHOLE
	// string, and what the alert receives is a concatenation. Everything pushed into a ^1 is real
	// data (a count, the user's own find / change string) and is marked untranslatable first - a
	// search for a word that happens to match a built-in phrase would otherwise come back as
	// somebody else's translation.
	PMString msg;
	msg.SetTranslatable(kFalse);

	PMString countStr;
	countStr.AppendNumber(checkedCount);
	countStr.SetTranslatable(kFalse);
	PMString countLine(checkedCount == 1 ? kKBSConfirmReplaceOneKey : kKBSConfirmReplaceManyKey);
	countLine.Translate();
	::ReplaceStringParameters(&countLine, countStr);
	msg.Append(countLine);
	msg.Append(kLineSeparatorString);
	msg.Append(kLineSeparatorString);

	PMString findStr(opts->GetFindString(mode));
	findStr.SetTranslatable(kFalse);
	PMString findLine(kKBSConfirmFindKey);
	findLine.Translate();
	::ReplaceStringParameters(&findLine, findStr);
	msg.Append(findLine);
	if (mode == IFindChangeOptions::kGrepSearch)
		msg.Append("   (GREP)");	// the dialog's own name for the mode, untranslated everywhere
	msg.Append(kLineSeparatorString);

	PMString replaceStr(opts->GetReplaceString(mode));
	replaceStr.SetTranslatable(kFalse);
	if (replaceStr.IsEmpty())
	{
		// An empty change string is a legitimate request - it deletes every match - so it is
		// spelled out instead of leaving a blank line for the user to interpret.
		PMString empty(kKBSConfirmEmptyReplaceKey);
		empty.Translate();
		replaceStr = empty;
		replaceStr.SetTranslatable(kFalse);
	}
	PMString changeLine(kKBSConfirmChangeToKey);
	changeLine.Translate();
	::ReplaceStringParameters(&changeLine, replaceStr);
	msg.Append(changeLine);
	msg.Append(kLineSeparatorString);
	msg.Append(kLineSeparatorString);

	// How many documents this will write to decides what can honestly be promised about undo: one
	// chapter undoes with a single Ctrl+Z, several take one undo each because InDesign's undo
	// history is per document.
	const int32 chapterCount = KBSResultModel::GetCheckedChapterCount();
	PMString chapterStr;
	chapterStr.AppendNumber(chapterCount);
	chapterStr.SetTranslatable(kFalse);
	PMString unsaved(chapterCount <= 1 ? kKBSConfirmUnsavedOneKey : kKBSConfirmUnsavedManyKey);
	unsaved.Translate();
	::ReplaceStringParameters(&unsaved, chapterStr);
	msg.Append(unsaved);

	PMString title(kKBSPanelTitleKey);
	title.Translate();

	// A "Don't show again" check box, the way the built-in warnings have one (user's request,
	// 2026-07-28). The state lives in the application's alert registry under this command's action
	// ID, so it survives a restart and comes back with Preferences > General > Reset All Warning
	// Dialogs - the same switch that revives every other suppressed InDesign warning.
	//
	// returnValueIfHidden = 1 (OK): once the prompt has been switched off, Change Checked runs
	// straight away, which is what switching it off asks for.
	//
	// What this costs: unlike ModalAlert, this call takes no default-button argument, so Cancel can
	// no longer be made the default. The check box was worth more to the user than that guard.
	const int16 answer = CAlert::WarningAlertWithDontShowAgain(msg,
		kKBSReplaceCheckedActionID,		// passed straight through, as linksui does
		kTrue,							// show a Cancel button
		CAlert::eWarningIcon,
		title,
		kNullString,					// stock "OK"
		kNullString,					// stock "Cancel"
		1 /*OK, when the prompt is suppressed*/);
	// 1 = OK, 2 = OK + don't show again, 3 = Cancel, 4 = Cancel + don't show again.
	return answer == 1 || answer == 2;
}

/* UpdateActionStates
*/
void KBSActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	// A search or a replace is running behind its modal progress bar. The bar pumps events, so this
	// list can be asked for its states from inside the run: lock everything until it returns. The
	// replace needs it at least as much as the search - it works with a command sequence standing
	// open, and a second run started underneath would Halt() the first one's walker mid-walk.
	if (KBSSearchEngine::IsSearching() || KBSReplaceEngine::IsReplacing())
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
			// Needs something checked, AND a work list to check it on. After a replace the panel is
			// a report of what that replace did, and no row on it has a check box - but the rows the
			// run never reached (a chapter the safety ceiling cut short, one that would not open)
			// stay checked so the report can hold them, so the count alone would leave this enabled
			// over a list with nothing selectable anywhere on it: a destructive command, offered
			// against something the user cannot see or change. Check All / Uncheck All grey
			// themselves out on the same page through GetCheckableCount, which asks this question
			// for them.
			//
			// The Find/Change strings are deliberately NOT tested here - the confirmation prompt
			// shows them, so an empty change string (a valid "delete the matches" request) still
			// reaches the user instead of being greyed out unexplained.
			const bool16 canReplace = (checkedCount > 0 && !KBSResultModel::IsShowingReplaceOutcome())
				? kTrue : kFalse;
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


