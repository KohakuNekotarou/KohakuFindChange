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
#include "IActionStateList.h"	// UpdateActionStates: check mark for the Hide Previous Chapter toggle
#include "IApplication.h"		// QueryActionManager
#include "FindChangeID.h"		// kFindDialogActionID - Edit > Find/Change

// Project includes:
#include "KFCUIID.h"
#include "KBSModelAccess.h"		// the model half, through its session interfaces (2026-10-01, the model/UI split)
#include "KBSResultTree.h"		// rebuild the result tree after a search
#include "KBSJump.h"			// the Hide Previous Chapter toggle lives with the jump logic
#include "KBSPanelTitle.h"		// the panel's tab name carries the current scope
#include "KBSHowTo.h"			// "How to Use..." - the operating reference
#include "KBSPanelAlpha.h"		// "Translucent Panel" - get / set / apply the panel's alpha
#include "KBSFindChangeMinimize.h"	// "Minimizable Find/Change" - the minimize box on InDesign's dialog
#include "KBSAppBarSearchEnter.h"	// "Link the Application Bar's Search Field to This Panel"
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

			/** Custom-enabled actions get their state here - the toggles their check marks, the search its name. */
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

namespace
{
// A run of ours is up - its progress bar pumps events, so an action can arrive in the middle of it:
// say so on the status line, and the caller turns the action away. (The same lines stood in each of
// the row menus' cases and Change Checked's until 2026-09-29.)
bool RefusedWhileRunning()
{
	if (!KBSRuns()->IsAnyRunning())
		return false;
	PMString busy(KBSRuns()->BusyMessage());
	busy.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(busy);
	return true;
}

// The rows after a row menu's command: repainted in place - or the tree rebuilt, when the command threw
// the results away. A row / story / document Replace or Redo refused on a changed Find/Change query clears
// them (KBSReplaceEngine::RefuseChangedQuery, whose caller is to redraw the tree), and RefreshRows repaints
// only the chapters the model still holds: none, so the old rows stayed drawn and answered nothing until
// the next search (2026-09-29 defect re-check F-1; the jump had the same fault, B-1, fixed the same day).
void RedrawAfterRowMenu()
{
	if (KBSResults()->HasRun())
		KBSResultTree::RefreshRows();
	else
		KBSResultTree::Rebuild();
}

// ***** A WRITE INTO A DOCUMENT THAT HAS NO WINDOW (2026-09-29, Search: = All Documents). ***** It went
// through and nothing opened one - the user may keep a heavy document hidden on purpose - so the line says
// what the screen cannot show. Asked once the write is over (a book chapter the one-row Replace reopened
// has been given its window by then). Change Checked counts these in its own summary.
void NoteNoWindow(bool wrote, int32 chapter, PMString& status)
{
	if (!wrote)
		return;
	UIDRef docRef;
	IDFile file;
	if (KBSResults()->GetChapterLocation(chapter, docRef, file) && KBSChapters()->IsDocStillOpen(docRef)
		&& !KBSChapters()->HasWindow(docRef))
		status.Append(" The document has no window - still hidden.");
}

// One of the three Windows-only appearance toggles (Translucent Panel, Translucent Find/Change,
// Minimizable Find/Change): flipped, put on whatever window it can reach, and the status line says
// which it came to. The wording follows whether a window was actually reached - ticking one with
// nothing to put it on (the panel docked, the dialog closed) is legitimate, so that case is stated
// in words (notReached) rather than left looking broken.
void FlipAppearanceToggle(bool16 (*isOn)(), void (*setOn)(bool16), bool16 (*apply)(),
	const char* name, const char* notReached)
{
	const bool16 on = !isOn();
	setOn(on);
	const bool16 applied = apply();
	PMString msg(name);
	msg.Append(!on ? ": off." : (applied ? ": on." : notReached));
	msg.SetTranslatable(kFalse);
	KBSResultTree::ShowStatus(msg);
}
}

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

		case kKBSOpenFindChangeActionID:
		{
			// Open Find/Change... (2026-10-01, the user's call): InDesign's own Edit > Find/Change dialog,
			// opened from the panel - with no document open as well (a book alone on screen), where the
			// Edit menu greys it out. The dialog itself needs no document; only the menu's enabling asks
			// for one, and a panel item has its own. It runs the product's action through the action
			// manager, the way linksui's buttons run theirs (LinksUIButtonObserver.cpp) - so the dialog
			// opens exactly as Edit > Find/Change opens it, and nothing of InDesign's own menu is changed.
			// (From 2026-10-01 until this item, an IActionFilter took the "needs a document" bit off the
			// Edit menu's action instead; the user preferred KBS to leave InDesign's menu alone.)
			// *A MINIMISED DIALOG IS BROUGHT BACK, NOT CLOSED (2026-10-03, the user's call - the block 15
			//  recheck F-2): that action is a toggle, and on a minimised dialog it closes it (measured).
			if (KBSRestoreMinimizedFindChange())
				break;
			InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
			InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
			if (actionMgr != nil)
				actionMgr->PerformAction(ac, kFindDialogActionID, mousePoint, widget);
			break;
		}

		case kKBSSearchBookActionID:
		{
			// Search the target book - the Book panel's, or the active one as a fallback
			// (KBSBookScope::ResolveTargetBook) - or what Find/Change's Search: names, with the user's
			// current Find/Change query. The engine fills KBSResultModel with the hits (grouped by
			// chapter) behind a modal progress bar; the tree is drawn here, once, when it returns.
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
			KBSRuns()->SearchBook(summary);
			KBSResultTree::Rebuild();
			KBSResultTree::ShowStatus(summary);
			break;
		}

		case kKBSShowChangesActionID:
		{
			// Show Changes by KohakuFindChange (2026-09-29): the list rebuilt from the Track Changes records
			// KBS signed, over the current scope - the search's shape exactly (the engine keeps its own doors
			// and says why on a refusal; the tree is drawn once, when it returns).
			KBSPanelTitle::Update();
			PMString summary;
			KBSRuns()->ShowChanges(summary);
			KBSResultTree::Rebuild();
			KBSResultTree::ShowStatus(summary);
			break;
		}

		// (Find Missing Glyphs and Find Overset stood here until 2026-09-27, when they were removed on the
		//  user's call: the Book panel's preflight reports both over the whole book.)

		case kKBSScopeBookActionID:
		{
			// Toggle the search scope: the whole target book (see ResolveTargetBook), or what Find/Change's
			// Search: names (2026-09-29 - the front document until then). Just the
			// flag - nothing is closed and the current results stay put. Its check mark and the
			// search command's name are drawn in UpdateActionStates.
			KBSChapters()->SetBookScopeOn(!KBSChapters()->IsBookScopeOn());
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
			FlipAppearanceToggle(KBSGetPanelTranslucent, KBSSetPanelTranslucent, KBSApplyPanelTranslucency,
				"Translucent panel", ": on - has no effect while the panel is docked.");
			break;

		// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog - the
		// window this plug-in takes its query from, so having it fade out of the way while the
		// document is read is the point of it. *Windows only, OFF by default.
		// The dialog is found through the SDK's window list (never by its title, which is translated),
		// so this works whatever language InDesign is running in. See KBSPanelAlpha.cpp.
		case kKBSTranslucentFindChangeActionID:
			FlipAppearanceToggle(KBSGetFindChangeTranslucent, KBSSetFindChangeTranslucent, KBSApplyFindChangeTranslucency,
				"Translucent Find/Change", ": on - applies when the Find/Change dialog is open.");
			break;

		// "Minimizable Find/Change": a minimize box on InDesign's OWN Find/Change dialog, so it can
		// be put on the taskbar instead of closed. The same window as the toggle above, found the
		// same way (never by its title, which is translated). *Windows only, OFF by default.
		// What it takes is two bits - see KBSFindChangeMinimize.h for why one of them is not the
		// obvious one.
		case kKBSMinimizableFindChangeActionID:
			FlipAppearanceToggle(KBSGetFindChangeMinimizable, KBSSetFindChangeMinimizable, KBSApplyFindChangeMinimizable,
				"Minimizable Find/Change", ": on - applies when the Find/Change dialog is open.");
			break;

		// "Link the Application Bar's Search Field to This Panel" (2026-10-02, the user's design): the search
		// field of InDesign's application bar shows the query of the tab Find/Change is on, and Return in it
		// searches with this panel (2026-10-03; the triangle no longer chooses the tab).
		// *Windows only, OFF by default.
		// Everything - what was measured, the hook, why that field's own menu could not take an item - is in
		// KBSAppBarSearchEnter.h.
		case kKBSAppBarSearchEnterActionID:
			FlipAppearanceToggle(KBSGetAppBarSearchEnter, KBSSetAppBarSearchEnter, KBSApplyAppBarSearchEnter,
				"Application Bar link", ": on - works while the Application Bar's search field is shown.");
			break;

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
			// ***** THIS DOOR AND THE ONES BELOW ARE THE ENGINE'S TOO, ASKED HERE SO A REFUSAL LEAVES THE
			// ***** TREE AS IT IS (2026-09-28). ***** A refusal here does not reach the Rebuild after
			// ReplaceChecked, which re-expands the tree and loses what the user had opened or closed. (Has the
			// Find/Change query changed is NOT asked here: its refusal rebuilds the tree anyway, so it is the
			// engine's alone.)
			if (RefusedWhileRunning())
				break;

			// A list rebuilt from the records (2026-09-29, Show Changes) offers no replace - the engine's
			// door, asked here for the reason the report's door below is: a refusal leaves the tree as it is.
			// Asked first, because the report's door would call such a list a report.
			if (KBSResults()->IsFromRecords())
			{
				PMString rebuilt("Change Checked: these rows were rebuilt from Track Changes - search again to replace.");
				rebuilt.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(rebuilt);
				break;
			}

			// The panel is a REPORT of what the last replace did, not a work list. The menu greys
			// this command out in that state (see UpdateActionStates), but a caller that never went
			// through the menu - a script invoking the action - lands here whatever the menu says.
			// Asked before the checked count: the rows the last run never reached keep their check so
			// the report can account for them, so GetCheckedCount() is still positive. Same wording
			// as the engine's own door.
			if (KBSResults()->NoRowHasCheckBox())		// a report - with no row taken back in it
			{
				PMString report("This is the last replace's report - search again to replace more.");
				report.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(report);
				break;
			}

			if (KBSResults()->GetCheckedCount() <= 0)
			{
				PMString nothing("Nothing checked.");
				nothing.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(nothing);
				break;
			}

			// ***** NO PROMPT SINCE 2026-09-27 (the user's call). ***** The confirmation (ConfirmReplace -
			// KBSReplaceConfirmDialog) asked before every Change Checked; everything a run does is one undo
			// step and every chapter is left open and unsaved, and the rows' own menus had already gone
			// without one. What the prompt said about Track Changes is said on the status line instead.
			PMString summary;
			const int32 replaced = KBSRuns()->ReplaceChecked(summary);
			if (replaced > 0)
				summary.Append(" Replaced with Track Changes on - Reject Change on a row's right-click menu takes it back.");
			KBSResultTree::Rebuild();		// replaced rows lose their box and fade
			KBSResultTree::ShowStatus(summary);
			break;
		}

		case kKBSChapterRejectActionID:
		case kKBSChapterRedoActionID:
		case kKBSChapterReplaceActionID:
		case kKBSAcceptAllChangesActionID:
		{
			// A DOCUMENT row's menu (2026-09-27): that document's rows, no prompt. The book row (and
			// nothing stashed - a script firing the action by ID) does nothing.
			const int32 chapter = KBSResults()->GetContextMenuChapter();
			if (chapter < 0 || chapter >= KBSResults()->GetChapterCount())
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			bool wrote = false;
			if (actionID.Get() == kKBSChapterRejectActionID)
				wrote = KBSRuns()->RejectChapter(chapter, status);
			else if (actionID.Get() == kKBSChapterRedoActionID)
				wrote = KBSRuns()->RedoChapter(chapter, status);	// no prompt, like Replace
			else if (actionID.Get() == kKBSAcceptAllChangesActionID)
				// the tracked changes signed "KohakuFindChange" in that document (anybody's until 2026-09-29)
				wrote = KBSRuns()->AcceptAllInChapter(chapter, status);
			else
				wrote = KBSRuns()->ReplaceChapter(chapter, status);
			NoteNoWindow(wrote, chapter, status);
			RedrawAfterRowMenu();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSRunRejectActionID:
		case kKBSRunAcceptActionID:
		{
			// A run row's menu (2026-09-29, a list rebuilt from the records). Nothing stashed = nobody
			// right-clicked a run row.
			int32 chapter = -1, run = -1;
			if (!KBSResults()->GetContextMenuRun(chapter, run))
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			if (actionID.Get() == kKBSRunRejectActionID)
				KBSRuns()->RejectRun(chapter, run, status);
			else
				KBSRuns()->AcceptRun(chapter, run, status);
			RedrawAfterRowMenu();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSStoryReplaceActionID:
		case kKBSStoryRejectActionID:
		case kKBSStoryAcceptActionID:
		case kKBSStoryRedoActionID:
		case kKBSStoryCheckAllActionID:
		case kKBSStoryUncheckAllActionID:
		{
			// A story row's menu (2026-09-27). Nothing stashed = nobody right-clicked a story row.
			int32 chapter = -1, group = -1;
			if (!KBSResults()->GetContextMenuGroup(chapter, group))
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			status.SetTranslatable(kFalse);
			const uint32 id = actionID.Get();
			bool wrote = false;
			if (id == kKBSStoryReplaceActionID)
				wrote = KBSRuns()->ReplaceStory(chapter, group, status);	// no prompt (the user's call)
			else if (id == kKBSStoryRejectActionID)
				wrote = KBSRuns()->RejectStory(chapter, group, status);
			else if (id == kKBSStoryAcceptActionID)
				wrote = KBSRuns()->AcceptStory(chapter, group, status);	// 2026-09-29
			else if (id == kKBSStoryRedoActionID)
				wrote = KBSRuns()->RedoStory(chapter, group, status);	// no prompt, like Replace
			else
			{
				const bool check = (id == kKBSStoryCheckAllActionID);
				KBSResults()->SetGroupChecked(chapter, group, check);
				status = check ? "This story: all checked." : "This story: all unchecked.";
			}
			NoteNoWindow(wrote, chapter, status);
			RedrawAfterRowMenu();
			KBSResultTree::ShowStatus(status);
			break;
		}

		case kKBSReplaceHitActionID:
		case kKBSRejectChangeActionID:
		case kKBSAcceptChangeActionID:
		{
			// A hit row's right-click menu (2026-09-26). Nothing stashed = nobody right-clicked a hit
			// row (a script firing the action by ID): do nothing.
			int32 chapter = -1, hit = -1;
			if (!KBSResults()->GetContextMenuHit(chapter, hit))
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			bool wrote = false;
			if (actionID.Get() == kKBSReplaceHitActionID)
				wrote = KBSRuns()->ReplaceHit(chapter, hit, status);	// no prompt (the user's call, 2026-09-27)
			else if (actionID.Get() == kKBSAcceptChangeActionID)
				wrote = KBSRuns()->AcceptHit(chapter, hit, status);	// 2026-09-29
			else
				wrote = KBSRuns()->RejectHit(chapter, hit, status);
			NoteNoWindow(wrote, chapter, status);
			RedrawAfterRowMenu();
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
			const int32 target = KBSResults()->GetContextMenuChapter();
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
				targetName = KBSResults()->GetBookName();
				KBSResults()->SetAllChecked(check);
			}
			else
			{
				int32 targetHits = 0;
				KBSResults()->GetChapterDisplay(target, targetName, targetHits);
				KBSResults()->SetChapterChecked(target, check);
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
	// Every run, through KBSRunGuard - the search, Show Changes and the replace family. The replace needs
	// it at least as much as the search: it works with a command sequence standing open, and a second walk
	// underneath would Halt() its walker mid-walk.
	if (KBSRuns()->IsAnyRunning())
	{
		for (int32 i = 0; i < listToUpdate->Length(); i++)
			listToUpdate->SetNthActionState(i, kDisabled_Unselected);
		return;
	}

	// Is there anything for the current scope to run on at all - the target book while Book Scope is
	// ON, the active document (IActiveContext) while it is OFF? The three commands that START a run share the answer, so
	// it is taken once here. See KBSBookScope::HasScopeTarget: it asks what the engines themselves
	// ask, so a command that is offered can always run and one that cannot is visibly grey rather
	// than reporting "No open document to search." after the fact (user's call 2026-08-02).
	//
	// It is asked HERE rather than declared in the .fr as kDisableIfNoFrontDocument for two reasons
	// that are written out beside the action definitions: that flag would grey the commands out with
	// a book open and no document window - the state a book run is FOR - and it skips this hook, so
	// the search command would also lose the name that carries the scope.
	const bool16 haveTarget = KBSChapters()->HasScopeTarget() ? kTrue : kFalse;

	// How many rows still carry a check box in the range Check All / Uncheck All would act on. That
	// range is the row their right-click menu was popped over (2026-08-01), so it is read here rather
	// than model-wide: over a document whose every hit is locked or already replaced, both commands
	// are no-ops and go grey - exactly as they do over a book with nothing left anywhere.
	//
	// ***** TAKEN LAZILY: BOTH READERS ARE ON THE RIGHT-CLICK MENU, AND THIS HOOK ALSO RUNS FOR THE
	// ***** FLYOUT. ***** Whichever of the pair is reached first pays for the count (it walks the context
	// row's hits, up to kKBSCollectHitLimit of them), the other reads it, and a menu that holds neither -
	// the flyout - never asks (it was taken for every opening of the flyout until 2026-08-11). The CHECKED
	// count has one reader, the replace command, and is taken inside that branch.
	// contextChapter is also what the document row's commands are about.
	const int32 contextChapter = KBSResults()->GetContextMenuChapter();
	int32 contextCheckable = -1;		// not counted yet - see the two commands at the end of the loop

	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);

		if (action == kKBSSearchBookActionID)
		{
			// The command's own name carries the scope, so it is visible BEFORE running it:
			// "Find in Book" while Book Scope is ON; with it off, the Search: of Edit > Find/Change it
			// follows since 2026-09-29 ("Find in Document", "Find in Story", "Find to End of Story" ...).
			// No check mark - this is the KESCL Start/Stop pattern (a name swap, not a state mark).
			//
			// FIND, not "Search" (2026-08-02): Adobe's verb for this is Find - the dialog this
			// command takes its query from is Find/Change, its buttons are Find Next and Change All -
			// while "Search" is the label on that dialog's SCOPE popup ("Search: Document"). The
			// action's resource name stays "Search": that is the handle a script reaches this by
			// (app.menuActions.itemByName("Search")), and it is never what the user sees, because this
			// line has always renamed it before the menu is drawn.
			PMString name(KBSRuns()->FindCommandName(KBSChapters()->IsBookScopeOn()));
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			// The name is written whether or not it can run, so a greyed-out item still says which
			// scope it would have used.
			// ...and grey on the Object and Colour tabs too (2026-09-27, the user's call): they find page
			// items, which this panel does not list. The same question the search asks (CanSearchTab).
			const bool canRun = haveTarget
				&& KBSRuns()->CanSearchTab(KBSRuns()->CurrentSearchMode());
			listToUpdate->SetNthActionState(i, canRun ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSShowChangesActionID)
		{
			// Show Changes by KohakuFindChange (2026-09-29): while the current scope has something to read -
			// the search's own question (runs are greyed above, before this loop).
			listToUpdate->SetNthActionState(i, haveTarget ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSScopeBookActionID)
		{
			int16 actionState = kEnabledAction;
			if (KBSChapters()->IsBookScopeOn())
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
			const bool reachable = KBSChapters()->IsBookScopeOn() || KBSResults()->IsFromBook();
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
		else if (action == kKBSAppBarSearchEnterActionID)
		{
			// Selectable whether or not the field is shown, like the toggles above: the flag is what is being set.
			int16 actionState = kEnabledAction;
			if (KBSGetAppBarSearchEnter())
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
			// The Find/Change strings are deliberately NOT tested here - an empty change string is a
			// valid "delete the matches" request, and greying the command out for it would say nothing
			// about why.
			//
			// ***** THE SECOND HALF IS ONE QUESTION, AND THE MODEL ALREADY OWNS IT. ***** "Can any
			// row of this list be checked at all" is NoRowHasCheckBox(). This line spelled that out by
			// hand until 2026-08-08.
			//
			// Walks every stored hit - up to kKBSCollectHitLimit of them, the whole-SEARCH ceiling
			// rather than the smaller number the panel displays. Taken here rather than above the
			// loop because this is the only action that reads it.
			const int32 checkedCount = KBSResults()->GetCheckedCount();
			const bool16 canReplace = (checkedCount > 0 && !KBSResults()->NoRowHasCheckBox())
				? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, canReplace ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSStoryReplaceActionID || action == kKBSStoryRejectActionID || action == kKBSStoryRedoActionID
			|| action == kKBSStoryCheckAllActionID || action == kKBSStoryUncheckAllActionID
			|| action == kKBSStoryAcceptActionID)
		{
			// A story row's menu (2026-09-27): each item while it has something to do in that story.
			int32 chapter = -1, group = -1;
			bool enable = KBSResults()->GetContextMenuGroup(chapter, group);
			if (enable)
			{
				if (action == kKBSStoryReplaceActionID)
					enable = KBSRuns()->CanReplaceStory(chapter, group);
				else if (action == kKBSStoryRejectActionID || action == kKBSStoryAcceptActionID)
					enable = KBSRuns()->CanRejectStory(chapter, group);	// Accept (2026-09-29): the same rows
				else if (action == kKBSStoryRedoActionID)
					enable = KBSRuns()->CanRedoStory(chapter, group);
				else
					enable = !KBSResults()->NoRowHasCheckBox();
			}
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSReplaceHitActionID || action == kKBSRejectChangeActionID
			|| action == kKBSAcceptChangeActionID)
		{
			// A hit row's menu (2026-09-26): Replace while the row is a Find/Change match not yet replaced,
			// not locked and with nothing said about it (KBSReplaceEngine::CanReplaceHit); Reject Change - and
			// its twin Accept Change since 2026-09-29 - while the row's tracked change is still there.
			int32 chapter = -1, hit = -1;
			bool enable = KBSResults()->GetContextMenuHit(chapter, hit);
			if (enable)
				enable = (action == kKBSReplaceHitActionID) ? KBSRuns()->CanReplaceHit(chapter, hit)
					: KBSRuns()->CanAcceptOrRejectHit(chapter, hit);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSRunRejectActionID || action == kKBSRunAcceptActionID)
		{
			// A run row's menu (2026-09-29): while the run has a replaced row with its change left.
			int32 chapter = -1, run = -1;
			const bool enable = KBSResults()->GetContextMenuRun(chapter, run)
				&& KBSRuns()->CanRejectOrAcceptRun(chapter, run);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSChapterReplaceActionID || action == kKBSChapterRejectActionID
			|| action == kKBSChapterRedoActionID || action == kKBSAcceptAllChangesActionID)
		{
			// A document row's menu (2026-09-27): each item while it has something to do in that document -
			// a ticked row to replace, a replaced row to take back, a row taken back to replace again, a
			// tracked change signed "KohakuFindChange" to accept while the document is open (anybody's until
			// 2026-09-29). The book row greys them all: they are about one document (Change Checked is the
			// whole book's). The DoAction case asks the same range first.
			bool enable = contextChapter >= 0 && contextChapter < KBSResults()->GetChapterCount();
			if (enable)
			{
				if (action == kKBSChapterReplaceActionID)
					enable = KBSRuns()->CanReplaceChapter(contextChapter);
				else if (action == kKBSChapterRejectActionID)
					enable = KBSRuns()->CanRejectChapter(contextChapter);
				else if (action == kKBSChapterRedoActionID)
					enable = KBSRuns()->CanRedoChapter(contextChapter);
				else
					enable = KBSRuns()->CanAcceptAllInChapter(contextChapter);
			}
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSCheckAllActionID || action == kKBSUncheckAllActionID)
		{
			// Nothing to check without results - and nothing to check after a replace either, where
			// the panel lists what CHANGED and no row has a box left. Both commands would be no-ops
			// there, so they go grey along with the boxes. Not a toggle - no check mark either way.
			//
			// Measured 2026-08-01, when these two were the WHOLE right-click menu: a popup whose every item
			// is disabled is not shown at all (the user accepted that as the better behaviour). It also
			// proves this hook runs for the popup menu, which is what lets the enablement follow the
			// right-clicked row at all.
			//
			// The count these two share, taken on whichever of them the loop reaches first. Nothing
			// between the two visits can change it - this method reads the model, it never writes to
			// it - so the second reader gets the same answer the first paid for. See the note above
			// the declaration for why it is not taken before the loop.
			if (contextCheckable < 0)
			{
				contextCheckable = 0;
				if (contextChapter == KBSResultModel::kContextMenuBookRow)
					contextCheckable = KBSResults()->GetCheckableCount();
				else if (contextChapter != KBSResultModel::kNoContextMenuChapter)
					contextCheckable = KBSResults()->GetChapterCheckableCount(contextChapter);
			}
			const bool16 haveCheckable = (contextCheckable > 0) ? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, haveCheckable ? kEnabledAction : kDisabled_Unselected);
		}
	}
}


