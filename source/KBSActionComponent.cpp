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
#include "KBSRunGuard.h"		// "is anything of ours running?" - one question, four runs
#include "KBSTrackChange.h"		// Reject Change / Redo: is this row's tracked change still there?
#include "KBSHowTo.h"			// "How to Use..." - the operating reference
#include "KBSPanelAlpha.h"		// "Translucent Panel" - get / set / apply the panel's alpha
#include "KBSFindChangeMinimize.h"	// "Minimizable Find/Change" - the minimize box on InDesign's dialog
#include "KBSPanelState.h"		// "Save Panel Settings" - write the settings toggles to our own file
#include "KBSBookPanelPlacement.h"	// "Remember Book Panel Placement" - InDesign's own Book panel

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

		case kKBSHowToActionID:
		{
			// The whole reference, in a scrollable dialog. Everything about it - which language,
			// the ScriptUI window, the CAlert fallback - is in KBSHowTo.cpp; nothing to decide here.
			KBSHowTo::Show();
			break;
		}

		case kKBSSearchBookActionID:
		{
			// Search the target book - the Book panel's, or the active one as a fallback
			// (KBSBookScope::ResolveTargetBook) - or the front document, with the user's current Find/Change
			// query. The engine fills KBSResultModel with the hits (grouped by chapter) behind a
			// modal progress bar; the tree is drawn here, once, when it returns.
			//
			// No re-entry test here any more: the ENGINE has one (and so does every other run of
			// ours), and it puts a reason on the status line where this one silently did nothing.
			// A command reaching here while a run is up - the bar pumps events, so it can - now
			// says why instead of looking broken. See KBSRunGuard.

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

		// (Find Missing Glyphs and Find Overset stood here until 2026-09-27, when they were removed on the
		//  user's call: the Book panel's preflight reports both over the whole book.)

		case kKBSScopeBookActionID:
		{
			// Toggle the search scope: the whole target book (see ResolveTargetBook), or just the front document. Just the
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

		// "Translucent Panel": draw this panel faint (alpha kKBSPanelAlphaValue = 77, about 30%) so
		// the document underneath stays readable, and bring it back to solid while the pointer is on
		// it. *Windows only, OFF by default. It takes effect while the panel FLOATS, and while it is
		// pulled out of an icon as a drawer; docked and expanded it can still be ticked but nothing
		// looks different - the flag is set, and it applies the moment the panel floats again (that
		// following is done by the observer in KBSPanelAlpha.cpp, on kPaletteVisibilityChangedMessage).
		case kKBSTranslucentPanelActionID:
		{
			const bool16 on = !KBSGetPanelTranslucent();
			KBSSetPanelTranslucent(on);

			// The wording follows whether an alpha actually reached a window. Ticking it while docked
			// changes nothing on screen, so the reason is said in words rather than left a mystery.
			const bool16 applied = KBSApplyPanelTranslucency();
			PMString msg;
			if (!on)
				msg = "Translucent panel: off.";
			else if (applied)
				msg = "Translucent panel: on.";
			else
				msg = "Translucent panel: on - has no effect while the panel is docked.";
			msg.SetTranslatable(kFalse);
			KBSResultTree::ShowStatus(msg);
			break;
		}

		// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog - the
		// window this plug-in takes its query from, so having it fade out of the way while the
		// document is read is the point of it. *Windows only, OFF by default.
		// The dialog is found through the SDK's window list (never by its title, which is translated),
		// so this works whatever language InDesign is running in. See KBSPanelAlpha.cpp.
		case kKBSTranslucentFindChangeActionID:
		{
			const bool16 on = !KBSGetFindChangeTranslucent();
			KBSSetFindChangeTranslucent(on);

			// As with the panel, the wording follows whether an alpha actually reached a window.
			// Toggling it with the dialog closed is legitimate - it applies when the dialog opens -
			// so that case is stated rather than left looking broken.
			const bool16 applied = KBSApplyFindChangeTranslucency();
			PMString msg;
			if (!on)
				msg = "Translucent Find/Change: off.";
			else if (applied)
				msg = "Translucent Find/Change: on.";
			else
				msg = "Translucent Find/Change: on - applies when the Find/Change dialog is open.";
			msg.SetTranslatable(kFalse);
			KBSResultTree::ShowStatus(msg);
			break;
		}

		// "Minimizable Find/Change": a minimize box on InDesign's OWN Find/Change dialog, so it can
		// be put on the taskbar instead of closed. The same window as the toggle above, found the
		// same way (never by its title, which is translated). *Windows only, OFF by default.
		// What it takes is two bits - see KBSFindChangeMinimize.h for why one of them is not the
		// obvious one.
		case kKBSMinimizableFindChangeActionID:
		{
			const bool16 on = !KBSGetFindChangeMinimizable();
			KBSSetFindChangeMinimizable(on);

			// As with the two toggles above, the wording follows whether a window was actually
			// reached. Toggling it with the dialog closed is legitimate - it applies when the
			// dialog opens - so that case is stated rather than left looking broken.
			const bool16 applied = KBSApplyFindChangeMinimizable();
			PMString msg;
			if (!on)
				msg = "Minimizable Find/Change: off.";
			else if (applied)
				msg = "Minimizable Find/Change: on.";
			else
				msg = "Minimizable Find/Change: on - applies when the Find/Change dialog is open.";
			msg.SetTranslatable(kFalse);
			KBSResultTree::ShowStatus(msg);
			break;
		}

		// "Remember Book Panel Placement": InDesign's own Book panel is measured as it closes (and
		// when InDesign quits) and put back where it was when it next appears. OFF by default.
		// *Flipping it WRITES ITS OWN KEY to the settings file at once, and the status line says
		// whether that worked (the user's rules, 2026-09-25) - unlike the toggles above, which wait
		// for "Save Panel Settings". Everything is in KBSBookPanelPlacement.cpp.
		case kKBSRememberBookPanelActionID:
		{
			PMString msg;
			KBSBookPanelPlacement::ToggleAndSave(msg);
			KBSResultTree::ShowStatus(msg);
			break;
		}

		// "Save Panel Settings": write the settings toggles above to a JSON file of our own in the
		// user's preferences folder; it is read back at startup. Explicit rather than automatic, the
		// way KESCM has it - a setting the user did not ask to keep is one they cannot account for
		// later. Everything, including where the file goes and what it reports, is in
		// KBSPanelState.cpp.
		case kKBSSavePanelSettingsActionID:
		{
			KBSSavePanelState();
			break;
		}

		case kKBSReplaceCheckedActionID:
		{
			// Another run of ours is already up, reached through the events its progress bar pumps.
			// Asked about EVERY run rather than only another replace: a search cancelled underneath
			// this one hands back the chapters it is about to write to (see KBSRunGuard).
			//
			// ***** THIS DOOR AND THE TWO BELOW ARE THE ENGINE'S TOO, ASKED HERE SO A REFUSAL LEAVES THE
			// ***** TREE AS IT IS (2026-09-28). ***** They stood here to answer BEFORE the confirmation prompt
			// until the prompt went on 2026-09-27; what they still buy is that a refusal does not reach the
			// Rebuild after ReplaceChecked, which re-expands the tree and loses what the user had opened
			// or closed. (A third - has the Find/Change query changed - stood here too until 2026-09-28. It
			// rebuilt the tree on a refusal as the engine's path does, with the same words, so it was the
			// engine's door asked twice: ReplaceChecked asks it.)
			if (KBSRunGuard::IsAnyRunning())
			{
				PMString busy(KBSRunGuard::BusyMessage());
				busy.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(busy);
				break;
			}

			// The panel is a REPORT of what the last replace did, not a work list. The menu greys
			// this command out in that state (see UpdateActionStates), but a caller that never went
			// through the menu - a script invoking the action - lands here whatever the menu says.
			// Asked before the checked count: the rows the last run never reached keep their check so
			// the report can account for them, so GetCheckedCount() is still positive. Same wording
			// as the engine's own door.
			if (KBSResultModel::IsShowingReplaceOutcome() && !KBSResultModel::AnyRejectedRowOpen())
			{
				PMString report("This is the last replace's report - search again to replace more.");
				report.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(report);
				break;
			}

			if (KBSResultModel::GetCheckedCount() <= 0)
			{
				PMString nothing("Nothing checked.");
				nothing.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(nothing);
				break;
			}
			// (Results that stopped short - the safety limit, or a search error - were refused here from
			//  the 2026-09-27 defect sweep (D-5) until the same day's cleanup: Change All would have written
			//  the matches past where the search stopped. One match at a time writes the ticked rows only.)

			// ***** NO PROMPT SINCE 2026-09-27 (the user's call). ***** The confirmation (ConfirmReplace -
			// KBSReplaceConfirmDialog) asked before every Change Checked; everything a run does is one undo
			// step and every chapter is left open and unsaved, and the rows' own menus had already gone
			// without one. What the prompt said about Track Changes is said on the status line instead.
			PMString summary;
			const int32 replaced = KBSReplaceEngine::ReplaceChecked(summary);
			if (replaced > 0)
				summary.Append(" Replaced with Track Changes on - Reject Change on a row's right-click menu takes it back.");
			KBSResultTree::Rebuild();		// replaced rows lose their box and fade
			KBSResultTree::ShowStatus(summary);
			break;
		}

		case kKBSChapterRejectActionID:
		case kKBSChapterRedoActionID:
		case kKBSChapterReplaceActionID:
		{
			// A DOCUMENT row's menu (2026-09-27): that document's ticked rows, no prompt. The book row (and
			// nothing stashed - a script firing the action by ID) does nothing.
			const int32 chapter = KBSResultModel::GetContextMenuChapter();
			if (chapter < 0 || chapter >= KBSResultModel::GetChapterCount())
				break;
			if (KBSRunGuard::IsAnyRunning())
			{
				PMString busy(KBSRunGuard::BusyMessage());
				busy.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(busy);
				break;
			}
			PMString status;
			if (actionID.Get() == kKBSChapterRejectActionID)
				KBSReplaceEngine::RejectChapter(chapter, status);
			else if (actionID.Get() == kKBSChapterRedoActionID)
				KBSReplaceEngine::RedoChapter(chapter, status);	// no prompt, like Replace
			else
				KBSReplaceEngine::ReplaceChapter(chapter, status);
			KBSResultTree::RefreshRows();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSStoryReplaceActionID:
		case kKBSStoryRejectActionID:
		case kKBSStoryRedoActionID:
		case kKBSStoryCheckAllActionID:
		case kKBSStoryUncheckAllActionID:
		{
			// A story row's menu (2026-09-27). Nothing stashed = nobody right-clicked a story row.
			int32 chapter = -1, group = -1;
			if (!KBSResultModel::GetContextMenuGroup(chapter, group))
				break;
			if (KBSRunGuard::IsAnyRunning())
			{
				PMString busy(KBSRunGuard::BusyMessage());
				busy.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(busy);
				break;
			}
			PMString status;
			status.SetTranslatable(kFalse);
			const uint32 id = actionID.Get();
			if (id == kKBSStoryReplaceActionID)
				KBSReplaceEngine::ReplaceStory(chapter, group, status);	// no prompt (the user's call)
			else if (id == kKBSStoryRejectActionID)
				KBSReplaceEngine::RejectStory(chapter, group, status);
			else if (id == kKBSStoryRedoActionID)
				KBSReplaceEngine::RedoStory(chapter, group, status);	// no prompt, like Replace
			else
			{
				const bool check = (id == kKBSStoryCheckAllActionID);
				KBSResultModel::SetGroupChecked(chapter, group, check);
				status = check ? "This story: all checked." : "This story: all unchecked.";
			}
			KBSResultTree::RefreshRows();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSReplaceHitActionID:
		case kKBSRejectChangeActionID:
		{
			// A hit row's right-click menu (2026-09-26). Nothing stashed = nobody right-clicked a hit
			// row (a script firing the action by ID): do nothing.
			int32 chapter = -1, hit = -1;
			if (!KBSResultModel::GetContextMenuHit(chapter, hit))
				break;
			if (KBSRunGuard::IsAnyRunning())
			{
				PMString busy(KBSRunGuard::BusyMessage());
				busy.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(busy);
				break;
			}
			PMString status;
			if (actionID.Get() == kKBSReplaceHitActionID)
				KBSReplaceEngine::ReplaceHit(chapter, hit, status);	// no prompt (the user's call, 2026-09-27)
			else
				KBSReplaceEngine::RejectHit(chapter, hit, status);
			KBSResultTree::RefreshRows();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSAcceptAllChangesActionID:
		{
			// A document row's right-click menu (2026-09-27): KBS's own tracked changes in that chapter's
			// document. The book row (and nothing stashed - a script firing the action by ID) does nothing.
			const int32 chapter = KBSResultModel::GetContextMenuChapter();
			if (chapter < 0 || chapter >= KBSResultModel::GetChapterCount())
				break;
			if (KBSRunGuard::IsAnyRunning())
			{
				PMString busy(KBSRunGuard::BusyMessage());
				busy.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(busy);
				break;
			}
			PMString status;
			KBSReplaceEngine::AcceptAllInChapter(chapter, status);
			KBSResultTree::RefreshRows();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSCheckAllActionID:
		case kKBSUncheckAllActionID:
		{
			// Both commands live on the result rows' right-click menu (2026-08-01; they were on the
			// flyout until then), so the row the menu was popped over is what says how far they reach:
			// the BOOK row means every chapter - the flyout's old behaviour - and a document row means
			// that chapter alone. KBSResultNodeEH stashed it just before popping the menu.
			//
			// Nothing stashed means nobody right-clicked a row: a caller that never went through the
			// menu - a script firing the action by ID - lands here, and there is no row for it to be
			// talking about. Do nothing rather than guess at "everything".
			//
			// Either way this covers every STORED hit, including the ones past the panel's display cap
			// (kKBSDisplayHitLimit) - most of them scrolled out of sight, which is why the status line
			// says afterwards which row it was done over.
			const int32 target = KBSResultModel::GetContextMenuChapter();
			if (target == KBSResultModel::kNoContextMenuChapter)
				break;
			const bool check = (actionID.Get() == kKBSCheckAllActionID);

			// The row's own name, READ FIRST and changed second. Nothing here renames a row, so the
			// order cannot matter today - it is written this way because the sentence the status line
			// is about ("this row, all checked") names the row as it was ASKED, and a reader should
			// not have to prove that the call in between left it alone. The name comes from the same
			// place the row draws it from.
			PMString targetName;
			if (target == KBSResultModel::kContextMenuBookRow)
			{
				targetName = KBSResultModel::GetBookName();
				KBSResultModel::SetAllChecked(check);
			}
			else
			{
				int32 targetHits = 0;
				KBSResultModel::GetChapterDisplay(target, targetName, targetHits);
				KBSResultModel::SetChapterChecked(target, check);
			}
			targetName.SetTranslatable(kFalse);

			// Only what the rows DRAW changed - the tree's shape is untouched - so repaint them in
			// place instead of rebuilding. One notification per chapter, and the expansion state
			// survives (a chapter the user collapsed stays collapsed).
			KBSResultTree::RefreshRows();
			KBSResultTree::ShowCheckAllStatus(targetName, check);
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
		// ***** THE ONE STRING KBS DOES NOT SWITCH BY UI LANGUAGE. ***** The About box is the
		// plug-in's name and version, and it reads the same in every UI language (user's call,
		// 2026-08-09), so it comes straight from the string table rather than through
		// KBSLoc::Text. The name and the version are not words - translating them would be
		// translating an identifier.
		//
		// The KEY goes in as a key, and the alert translates it: CAlert.h:70 says the message
		// "will be translated unless the string has been translated already or isn't
		// translatable". That is the shape every About box in the SDK samples has
		// (candlechartui/CdlChtUIActionComponent.cpp:192, and the same line in every other
		// Dolly-generated component). It used to arrive here already translated, through a
		// PMString(key, kTranslateDuringCall) built one line up - the same answer by a longer
		// road.
		// !KESCM was named here as "still written that way" until 2026-08-11, and it is not:
		//  KESCMActionComponent.cpp:647 hands over PMString(kKESCMAboutBoxStringKey) with NO
		//  translate flag, and the comment above it says the alert translates it - the same shape as
		//  this line, one redundant wrapper apart. The LINE NUMBER was right and the claim about what
		//  is on it was not, which is the harder half of a citation to check.
		kKBSAboutBoxStringKey,					// Alert string
		kOKString, 						// OK button
		kNullString, 						// No second button
		kNullString, 						// No third button
		1,							// Set OK button to default
		CAlert::eInformationIcon				// Information icon.
	);
}


/* UpdateActionStates
*/
void KBSActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	// A run of ours is standing behind its modal progress bar. The bar pumps events, so this list
	// can be asked for its states from inside the run: lock everything until it returns.
	//
	// ALL FOUR runs, through KBSRunGuard - it used to name only the search and the replace, which
	// left both scans able to start a second run on top of themselves and on top of each other. The
	// replace needs it at least as much as the search (it works with a command sequence standing
	// open, and a second walk underneath would Halt() its walker mid-walk), and a scan needs it
	// because a run cancelled underneath it closes the very chapters it is walking.
	if (KBSRunGuard::IsAnyRunning())
	{
		for (int32 i = 0; i < listToUpdate->Length(); i++)
			listToUpdate->SetNthActionState(i, kDisabled_Unselected);
		return;
	}

	// Is there anything for the current scope to run on at all - the target book while Book Scope is
	// ON, a front document while it is OFF? The three commands that START a run share the answer, so
	// it is taken once here. See KBSBookScope::HasScopeTarget: it asks what the engines themselves
	// ask, so a command that is offered can always run and one that cannot is visibly grey rather
	// than reporting "No open document to search." after the fact (user's call 2026-08-02).
	//
	// It is asked HERE rather than declared in the .fr as kDisableIfNoFrontDocument for two reasons
	// that are written out beside the action definitions: that flag would grey the commands out with
	// a book open and no document window - the state a book run is FOR - and it skips this hook, so
	// the search command would also lose the name that carries the scope.
	const bool16 haveTarget = KBSBookScope::HasScopeTarget() ? kTrue : kFalse;

	// How many rows still carry a check box in the range Check All / Uncheck All would act on. That
	// range is the row their right-click menu was popped over (2026-08-01), so it is read here rather
	// than model-wide: over a document whose every hit is locked or already replaced, both commands
	// are no-ops and go grey - exactly as they do over a book with nothing left anywhere.
	//
	// Hoisted out of the loop because TWO commands ask it, which is the only thing that justifies
	// hoisting anything here. The CHECKED count stood beside it on the same grounds until 2026-08-08,
	// long after it had only one reader left (the replace command, since 2026-08-07): a right-click
	// menu, which holds nothing but these two commands, was walking every stored hit to answer a
	// question no action on it asks. It is taken inside that one branch now.
	//
	// ***** BOTH READERS ARE ON THE RIGHT-CLICK MENU, AND THIS HOOK ALSO RUNS FOR THE FLYOUT. *****
	// So the count is taken lazily: whichever of the pair is reached first pays for it, the other
	// reads it, and a menu that holds neither never asks. It was taken unconditionally here until
	// 2026-08-11, which made every opening of the flyout - nine commands, none of them a reader -
	// walk the context row's hits, up to kKBSCollectHitLimit of them. That is the 2026-08-08 finding
	// above seen from the other menu: hoisting is justified by two readers, and the readers have to
	// be on the LIST BEING ENABLED. Only the walk moved; the answer is the same one it always was.
	const int32 contextChapter = KBSResultModel::GetContextMenuChapter();
	int32 contextCheckable = -1;		// not counted yet - see the two commands at the end of the loop

	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);

		if (action == kKBSSearchBookActionID)
		{
			// The command's own name carries the scope, so it is visible BEFORE running it:
			// "Find in Book" while Book Scope is ON, "Find in Document" while it is OFF. No check
			// mark - this is the KESCL Start/Stop pattern (a name swap, not a state mark).
			//
			// FIND, not "Search" (2026-08-02): Adobe's verb for this is Find - the dialog this
			// command takes its query from is Find/Change, its buttons are Find Next and Change All -
			// while "Search" is the label on that dialog's SCOPE popup ("Search: Document"). The old
			// name mixed the two, and did not match the two commands under it either (Find Missing
			// Glyphs, Find Overset). The action's resource name stays "Search": that is the handle a
			// script reaches this by (app.menuActions.itemByName("Search")), and it is never what the
			// user sees, because this line has always renamed it before the menu is drawn.
			PMString name(KBSBookScope::IsBookScopeOn() ? "Find in Book" : "Find in Document");
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			// The name is written whether or not it can run, so a greyed-out item still says which
			// scope it would have used.
			// ...and grey on the Object and Colour tabs too (2026-09-27, the user's call): they find page
			// items, which this panel does not list. The same question the search asks (CanSearchTab).
			const bool canRun = haveTarget
				&& KBSSearchEngine::CanSearchTab(KBSSearchEngine::CurrentSearchMode());
			listToUpdate->SetNthActionState(i, canRun ? kEnabledAction : kDisabled_Unselected);
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
			// Reachable whenever the sweep it gates can RUN - which is the sweep's own question,
			// KBSJump's ShouldHidePreviousChapter, asked of the results on screen (IsFromBook) -
			// and also while Book Scope is on, so it can be set up before the search. It asked
			// IsBookScopeOn() ALONE until 2026-08-09: after a book search with the scope since
			// switched off, the sweep kept closing documents on every jump while the menu that
			// stops it sat grey - the same lock-out the 2026-08-03 note in KBSJump.cpp records
			// closing from the other side. The check mark stays visible through the lock
			// (kSelectedAction without kEnabledAction), like KESCL's locked "Search book".
			const bool reachable = KBSBookScope::IsBookScopeOn() || KBSResultModel::IsFromBook();
			int16 actionState = reachable ? kEnabledAction : kDisabled_Unselected;
			if (KBSJump::IsHidePreviousChapterOn())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSTranslucentPanelActionID)
		{
			// *Selectable even while the panel is docked - deliberately NOT greyed out (the user's
			// call in KESCM, 2026-07-29). Where the click has no visible result, DoAction says so on
			// the status line instead.
			int16 actionState = kEnabledAction;
			if (KBSGetPanelTranslucent())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSTranslucentFindChangeActionID)
		{
			// Selectable whether or not the Find/Change dialog is open, for the same reason the
			// panel's toggle stays selectable while docked: what is being set is the preference, and
			// it applies the moment the window exists. DoAction says which case it was.
			int16 actionState = kEnabledAction;
			if (KBSGetFindChangeTranslucent())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSMinimizableFindChangeActionID)
		{
			// Selectable whether or not the dialog is open, exactly like the toggle above: what is
			// being set is the preference, and it applies the moment the window exists.
			int16 actionState = kEnabledAction;
			if (KBSGetFindChangeMinimizable())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSRememberBookPanelActionID)
		{
			// Selectable with no book open, like the toggles above: the flag is what is being set,
			// and it acts the next time a book panel closes or appears.
			int16 actionState = kEnabledAction;
			if (KBSBookPanelPlacement::IsOn())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKBSReplaceCheckedActionID)
		{
			// Needs something checked, AND a work list to check it on. After a replace the panel is
			// a report of what that replace did, and no row on it has a check box - but the rows the
			// run never reached (a chapter that would not open, a cancelled run)
			// stay checked so the report can hold them, so the count alone would leave this enabled
			// over a list with nothing selectable anywhere on it: a destructive command, offered
			// against something the user cannot see or change. Check All / Uncheck All grey
			// themselves out on the same page through GetCheckableCount, which asks this question
			// for them.
			//
			// The Find/Change strings are deliberately NOT tested here - the confirmation prompt
			// shows them, so an empty change string (a valid "delete the matches" request) still
			// reaches the user instead of being greyed out unexplained.
			//
			// ***** THE SECOND HALF IS ONE QUESTION, AND THE MODEL ALREADY OWNS IT. ***** "Can any
			// row of this list be checked at all" is NoRowHasCheckBox(). This line spelled that out by
			// hand until 2026-08-08. (It also asked about the two scans' report-only kinds, removed with
			// the scans on 2026-09-27.)
			// !Named by FUNCTION, not by line. This read "KBSResultModel.cpp:259-263", which was
			//  correct on the day it was written (2026-08-08, where the definition sat at :257) and
			//  pointed at HasRun() by 2026-08-11: the same definition had moved to :273 as that file
			//  grew. A line number is the part of a citation that goes stale without anybody touching
			//  either end of it - the same swap block 12 made in KBSJump.cpp.
			//
			// Walks every stored hit - up to kKBSCollectHitLimit of them, the whole-SEARCH ceiling
			// rather than the smaller number the panel displays. Taken here rather than above the
			// loop because this is the only action that reads it. The cap is named rather than
			// spelled out: this comment read "5000" long after the ceiling became 10000.
			const int32 checkedCount = KBSResultModel::GetCheckedCount();
			const bool16 canReplace = (checkedCount > 0 && !KBSResultModel::NoRowHasCheckBox())
				? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, canReplace ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSStoryReplaceActionID || action == kKBSStoryRejectActionID || action == kKBSStoryRedoActionID
			|| action == kKBSStoryCheckAllActionID || action == kKBSStoryUncheckAllActionID)
		{
			// A story row's menu (2026-09-27): each item while it has something to do in that story.
			int32 chapter = -1, group = -1;
			bool enable = KBSResultModel::GetContextMenuGroup(chapter, group);
			if (enable)
			{
				if (action == kKBSStoryReplaceActionID)
					enable = KBSReplaceEngine::CanReplaceStory(chapter, group);
				else if (action == kKBSStoryRejectActionID)
					enable = KBSReplaceEngine::CanRejectStory(chapter, group);
				else if (action == kKBSStoryRedoActionID)
					enable = KBSReplaceEngine::CanRedoStory(chapter, group);
				else
					enable = !KBSResultModel::NoRowHasCheckBox();
			}
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSReplaceHitActionID)
		{
			// A hit row's menu (2026-09-27): Replace while the row is a Find/Change match not yet replaced,
			// not locked and with nothing said about it (KBSReplaceEngine::CanReplaceHit).
			int32 chapter = -1, hit = -1;
			const bool enable = KBSResultModel::GetContextMenuHit(chapter, hit)
				&& KBSReplaceEngine::CanReplaceHit(chapter, hit);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSRejectChangeActionID)
		{
			// A hit row's menu (2026-09-26): Reject Change while the row's tracked change is still there.
			// (Runs are greyed out above, before this loop. Redo shared this branch until 2026-09-27.)
			int32 chapter = -1, hit = -1;
			bool enable = false;
			if (KBSResultModel::GetContextMenuHit(chapter, hit))
			{
				UIDRef storyRef;
				KBSTrackChange::Change change;
				enable = KBSTrackChange::FindRowChangeForHit(chapter, hit, storyRef, change);
			}
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSChapterRedoActionID)
		{
			// A document row's menu (2026-09-27): while that document has a row taken back.
			const int32 chapter = KBSResultModel::GetContextMenuChapter();
			const bool enable = chapter >= 0 && KBSReplaceEngine::CanRedoChapter(chapter);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSChapterRejectActionID)
		{
			// A document row's menu (2026-09-27): while that document has a replaced row to take back.
			const int32 chapter = KBSResultModel::GetContextMenuChapter();
			const bool enable = chapter >= 0 && KBSReplaceEngine::CanRejectChapter(chapter);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSChapterReplaceActionID)
		{
			// A document row's menu (2026-09-27): while that document has a ticked row to replace. The book
			// row greys it (Change Checked, with its prompt, is the whole book's).
			const int32 chapter = KBSResultModel::GetContextMenuChapter();
			const bool enable = chapter >= 0 && KBSReplaceEngine::CanReplaceChapter(chapter);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSAcceptAllChangesActionID)
		{
			// A document row's menu (2026-09-27): only while that document is open and holds a change
			// of ours. The book row greys it (the command is about one document).
			const int32 chapter = KBSResultModel::GetContextMenuChapter();
			const bool enable = chapter >= 0 && chapter < KBSResultModel::GetChapterCount()
				&& KBSReplaceEngine::CanAcceptAllInChapter(chapter);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSCheckAllActionID || action == kKBSUncheckAllActionID)
		{
			// Nothing to check without results - and nothing to check after a replace either, where
			// the panel lists what CHANGED and no row has a box left. Both commands would be no-ops
			// there, so they go grey along with the boxes. Not a toggle - no check mark either way.
			//
			// Measured 2026-08-01: these two are the WHOLE right-click menu, so disabling both does
			// not grey a menu out - no menu appears at all (an empty popup is not shown). Right-
			// clicking a row while the panel shows a replace's report therefore does nothing visible,
			// which the user accepted as the better behaviour. It also proves this hook runs for the
			// popup menu, which is what lets the enablement follow the right-clicked row at all.
			//
			// The count these two share, taken on whichever of them the loop reaches first. Nothing
			// between the two visits can change it - this method reads the model, it never writes to
			// it - so the second reader gets the same answer the first paid for. See the note above
			// the declaration for why it is not taken before the loop.
			if (contextCheckable < 0)
			{
				contextCheckable = 0;
				if (contextChapter == KBSResultModel::kContextMenuBookRow)
					contextCheckable = KBSResultModel::GetCheckableCount();
				else if (contextChapter != KBSResultModel::kNoContextMenuChapter)
					contextCheckable = KBSResultModel::GetChapterCheckableCount(contextChapter);
			}
			const bool16 haveCheckable = (contextCheckable > 0) ? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, haveCheckable ? kEnabledAction : kDisabled_Unselected);
		}
	}
}


