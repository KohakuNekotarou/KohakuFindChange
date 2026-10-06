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
#include "KFCModelAccess.h"		// the model half, through its session interfaces (the model/UI split)
#include "KFCResultTree.h"		// rebuild the result tree after a search
#include "KFCJump.h"			// the Hide Previous Chapter toggle lives with the jump logic
#include "KFCPanelTitle.h"		// the panel's tab name carries the current scope
#include "KFCHowTo.h"			// "How to Use..." - the operating reference
#include "KFCPanelAlpha.h"		// "Translucent Panel" - get / set / apply the panel's alpha
#include "KFCFindChangeMinimize.h"	// "Minimizable Find/Change" - the minimize box on InDesign's dialog
#include "KFCAppBarSearchEnter.h"	// "Link the Application Bar's Search Field to This Panel"
#include "KFCPanelState.h"		// "Save Panel Settings" - write the settings toggles to our own file
#include "KFCBookPanelPlacement.h"	// "Remember Book Panel Placement" - InDesign's own Book panel
#include "KFCDiag.h"				// a test build's UIOBS line (KFCDiagCounter)

/** Implements IActionComponent; performs the actions that are executed when the plug-in's
	menu items are selected.

	
	@ingroup kohakubooksearch

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
CREATE_PMINTERFACE(KFCActionComponent, kKFCActionComponentImpl)

namespace
{
// A run of ours is up - its progress bar pumps events, so an action can arrive in the middle of it:
// say so on the status line, and the caller turns the action away. (One place for the row menus'
// cases and Change Checked.)
bool RefusedWhileRunning()
{
	if (!KFCRuns()->IsAnyRunning())
		return false;
	PMString busy(KFCRuns()->BusyMessage());
	busy.SetTranslatable(kFalse);
	KFCResultTree::ShowStatus(busy);
	return true;
}

// The rows after a row menu's command: repainted in place - or the tree rebuilt, when the command threw
// the results away. A row / story / document Replace refused on a changed Find/Change query clears
// them (KFCReplaceEngine::RefuseChangedQuery, whose caller is to redraw the tree), and RefreshRows repaints
// only the chapters the model still holds: none, so the old rows would stay drawn and answer nothing until
// the next search.
void RedrawAfterRowMenu()
{
	if (KFCResults()->HasRun())
		KFCResultTree::RefreshRows();
	else
		KFCResultTree::Rebuild();
}

// A WRITE INTO A DOCUMENT THAT HAS NO WINDOW (Search: = All Documents). It goes through and nothing
// opens one - the user may keep a heavy document hidden on purpose - so the line says
// what the screen cannot show. Asked once the write is over (a book chapter the one-row Replace reopened
// has been given its window by then). Change Checked counts these in its own summary.
void NoteNoWindow(bool wrote, int32 chapter, PMString& status)
{
	if (!wrote)
		return;
	UIDRef docRef;
	IDFile file;
	if (KFCResults()->GetChapterLocation(chapter, docRef, file) && KFCChapters()->IsDocStillOpen(docRef)
		&& !KFCChapters()->HasWindow(docRef))
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
	KFCResultTree::ShowStatus(msg);
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
void KFCActionComponent::DoAction(IActiveContext* ac, ActionID actionID, GSysPoint mousePoint, IPMUnknown* widget)
{
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
			// (Not an IActionFilter taking the "needs a document" bit off the Edit menu's action: the user
			// prefers KFC to leave InDesign's menu alone.)
			// *A MINIMISED DIALOG IS BROUGHT BACK, NOT CLOSED (the author's call): that action is a toggle,
			//  and on a minimised dialog it closes it (measured).
			if (KFCRestoreMinimizedFindChange())
				break;
			InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
			InterfacePtr<IActionManager> actionMgr(app != nil ? app->QueryActionManager() : nil);
			if (actionMgr != nil)
				actionMgr->PerformAction(ac, kFindDialogActionID, mousePoint, widget);
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

			// Before the search rather than after: the progress bar is modal, and the tab stays in
			// view behind it. This is also what puts the name back when the panel has been closed
			// and reopened since the last toggle.
			KFCPanelTitle::Update();

			PMString summary;
#ifdef KFC_DIAG
			// how often the UI half's observers ran while the search did (guide vol2-15: not per match, one hopes)
			const int titleBefore = KFCDiagCounter(0), mirrorBefore = KFCDiagCounter(1);
#endif
			KFCRuns()->SearchBook(summary);
#ifdef KFC_DIAG
			KFC_DIAG_LOG("UIOBS search title=%d mirror=%d", KFCDiagCounter(0) - titleBefore, KFCDiagCounter(1) - mirrorBefore);
#endif
			KFCResultTree::Rebuild();
			KFCResultTree::ShowStatus(summary);
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
			FlipAppearanceToggle(KFCGetPanelTranslucent, KFCSetPanelTranslucent, KFCApplyPanelTranslucency,
				"Translucent panel", ": on - has no effect while the panel is docked.");
			break;

		// "Translucent Find/Change": the same treatment for InDesign's OWN Find/Change dialog - the
		// window this plug-in takes its query from, so having it fade out of the way while the
		// document is read is the point of it. *Windows only, OFF by default.
		// The dialog is found through the SDK's window list (never by its title, which is translated),
		// so this works whatever language InDesign is running in. See KFCPanelAlpha.cpp.
		case kKFCTranslucentFindChangeActionID:
			FlipAppearanceToggle(KFCGetFindChangeTranslucent, KFCSetFindChangeTranslucent, KFCApplyFindChangeTranslucency,
				"Translucent Find/Change", ": on - applies when the Find/Change dialog is open.");
			break;

		// "Minimizable Find/Change": a minimize box on InDesign's OWN Find/Change dialog, so it can
		// be put on the taskbar instead of closed. The same window as the toggle above, found the
		// same way (never by its title, which is translated). *Windows only, OFF by default.
		// What it takes is two bits - see KFCFindChangeMinimize.h for why one of them is not the
		// obvious one.
		case kKFCMinimizableFindChangeActionID:
			FlipAppearanceToggle(KFCGetFindChangeMinimizable, KFCSetFindChangeMinimizable, KFCApplyFindChangeMinimizable,
				"Minimizable Find/Change", ": on - applies when the Find/Change dialog is open.");
			break;

		// "Link the Application Bar's Search Field to This Panel" (the author's design): the search field of
		// InDesign's application bar shows the query of the tab Find/Change is on, and Return in it searches
		// with this panel (the triangle does not choose the tab).
		// *Windows only, OFF by default.
		// Everything - what was measured, the hook, why that field's own menu could not take an item - is in
		// KFCAppBarSearchEnter.h.
		case kKFCAppBarSearchEnterActionID:
			FlipAppearanceToggle(KFCGetAppBarSearchEnter, KFCSetAppBarSearchEnter, KFCApplyAppBarSearchEnter,
				"Application Bar link", ": on - works while the Application Bar's search field is shown.");
			break;

		// "Remember Book Panel Placement": InDesign's own Book panel is measured as it closes (and
		// when InDesign quits) and put back where it was when it next appears. OFF by default.
		// *Flipping it WRITES ITS OWN KEY to the settings file at once, and the status line says
		// whether that worked (the user's rules) - unlike the toggles above, which wait
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

		case kKFCReplaceCheckedActionID:
		{
			// Another run of ours is already up, reached through the events its progress bar pumps.
			// Asked about EVERY run rather than only another replace: a search cancelled underneath
			// this one hands back the chapters it is about to write to (see KFCRunGuard).
			//
			// THIS DOOR AND THE ONES BELOW ARE THE ENGINE'S TOO, ASKED HERE SO A REFUSAL LEAVES THE TREE
			// AS IT IS. A refusal here does not reach the Rebuild after
			// ReplaceChecked, which re-expands the tree and loses what the user had opened or closed. (Has the
			// Find/Change query changed is NOT asked here: its refusal rebuilds the tree anyway, so it is the
			// engine's alone.)
			if (RefusedWhileRunning())
				break;

			// The panel is a REPORT of what the last replace did, not a work list. The menu greys
			// this command out in that state (see UpdateActionStates), but a caller that never went
			// through the menu - a script invoking the action - lands here whatever the menu says.
			// Asked before the checked count: the rows the last run never reached keep their check so
			// the report can account for them, so GetCheckedCount() is still positive. Same wording
			// as the engine's own door.
			if (KFCResults()->NoRowHasCheckBox())		// a report - with no row taken back in it
			{
				PMString report("This is the last replace's report - search again to replace more.");
				report.SetTranslatable(kFalse);
				KFCResultTree::ShowStatus(report);
				break;
			}

			if (KFCResults()->GetCheckedCount() <= 0)
			{
				PMString nothing("Nothing checked.");
				nothing.SetTranslatable(kFalse);
				KFCResultTree::ShowStatus(nothing);
				break;
			}

			// NO PROMPT (the author's call). Not a confirmation before every Change Checked: everything a run
			// does is one undo step and every chapter is left open and unsaved, and the rows' own menus go
			// without one too. What such a prompt would say about Track Changes is said on the status line
			// instead - by the replace's own summary (it counts the rows Track Changes recorded nothing for,
			// which Reject Change cannot take back).
			PMString summary;
#ifdef KFC_DIAG
			const int titleBefore = KFCDiagCounter(0), mirrorBefore = KFCDiagCounter(1);	// (as the search's, above)
#endif
			(void)KFCRuns()->ReplaceChecked(summary);
#ifdef KFC_DIAG
			KFC_DIAG_LOG("UIOBS replace title=%d mirror=%d", KFCDiagCounter(0) - titleBefore, KFCDiagCounter(1) - mirrorBefore);
#endif
			KFCResultTree::Rebuild();		// replaced rows lose their box and fade
			KFCResultTree::ShowStatus(summary);
			break;
		}

		case kKFCChapterReplaceActionID:
		{
			// A DOCUMENT row's Replace: that document's ticked rows, no prompt. The book row (and nothing stashed - a
			// script firing the action by ID) does nothing.
			const int32 chapter = KFCResults()->GetContextMenuChapter();
			if (chapter < 0 || chapter >= KFCResults()->GetChapterCount())
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			const bool wrote = KFCRuns()->ReplaceChapter(chapter, status);
			NoteNoWindow(wrote, chapter, status);
			RedrawAfterRowMenu();
			KFCResultTree::ShowStatus(status);
			break;
		}

		case kKFCStoryReplaceActionID:
		case kKFCStoryCheckAllActionID:
		case kKFCStoryUncheckAllActionID:
		{
			// A story row's menu. Nothing stashed = nobody right-clicked a story row.
			int32 chapter = -1, group = -1;
			if (!KFCResults()->GetContextMenuGroup(chapter, group))
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			status.SetTranslatable(kFalse);
			const uint32 id = actionID.Get();
			bool wrote = false;
			if (id == kKFCStoryReplaceActionID)
				wrote = KFCRuns()->ReplaceStory(chapter, group, status);	// no prompt (the author's call)
			else
			{
				const bool check = (id == kKFCStoryCheckAllActionID);
				KFCResults()->SetGroupChecked(chapter, group, check);
				status = check ? "This story: all checked." : "This story: all unchecked.";
			}
			NoteNoWindow(wrote, chapter, status);
			RedrawAfterRowMenu();
			KFCResultTree::ShowStatus(status);
			break;
		}

		case kKFCReplaceHitActionID:
		{
			// A hit row's right-click menu. Nothing stashed = nobody right-clicked a hit
			// row (a script firing the action by ID): do nothing.
			int32 chapter = -1, hit = -1;
			if (!KFCResults()->GetContextMenuHit(chapter, hit))
				break;
			if (RefusedWhileRunning())
				break;
			PMString status;
			const bool wrote = KFCRuns()->ReplaceHit(chapter, hit, status);	// no prompt (the author's call)
			NoteNoWindow(wrote, chapter, status);
			RedrawAfterRowMenu();
			KFCResultTree::ShowStatus(status);
			break;
		}

		case kKFCCheckAllActionID:
		case kKFCUncheckAllActionID:
		{
			// Both commands live on the result rows' right-click menu, so the row the menu was popped
			// over is what says how far they reach: the BOOK row means every chapter, and a document
			// row means that chapter alone. KFCResultNodeEH stashed it just before popping the menu.
			//
			// Nothing stashed means nobody right-clicked a row: a caller that never went through the
			// menu - a script firing the action by ID - lands here, and there is no row for it to be
			// talking about. Do nothing rather than guess at "everything".
			//
			// Either way this covers every STORED hit, the rows past the panel's display cap as well (a list holds up
			// to kKFCCollectHitLimit over a panel that draws kKFCDisplayHitLimit - again since 2026-10-05) - most of
			// them out of sight, which is why the status line says afterwards which row it was done over.
			const int32 target = KFCResults()->GetContextMenuChapter();
			if (target == KFCResultModel::kNoContextMenuChapter)
				break;
			const bool check = (actionID.Get() == kKFCCheckAllActionID);

			// The row's own name, READ FIRST and changed second. Nothing here renames a row, so the
			// order cannot matter today - it is written this way because the sentence the status line
			// is about ("this row, all checked") names the row as it was ASKED, and a reader should
			// not have to prove that the call in between left it alone. The name comes from the same
			// place the row draws it from.
			PMString targetName;
			if (target == KFCResultModel::kContextMenuBookRow)
			{
				targetName = KFCResults()->GetBookName();
				KFCResults()->SetAllChecked(check);
			}
			else
			{
				int32 targetHits = 0;
				KFCResults()->GetChapterDisplay(target, targetName, targetHits);
				KFCResults()->SetChapterChecked(target, check);
			}
			targetName.SetTranslatable(kFalse);

			// Only what the rows DRAW changed - the tree's shape is untouched - so repaint them in
			// place instead of rebuilding. One notification per chapter, and the expansion state
			// survives (a chapter the user collapsed stays collapsed).
			KFCResultTree::RefreshRows();
			KFCResultTree::ShowCheckAllStatus(targetName, check);
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
		// THE ONE STRING KFC DOES NOT SWITCH BY UI LANGUAGE. The About box is the plug-in's name and version,
		// and it reads the same in every UI language (the author's call), so it comes straight from the string
		// table rather than through KFCLoc::Text. The name and the version are not words - translating them
		// would be translating an identifier.
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
void KFCActionComponent::UpdateActionStates(IActiveContext* /*ac*/, IActionStateList* listToUpdate, GSysPoint /*mousePoint*/, IPMUnknown* /*widget*/)
{
	// A run of ours is standing behind its modal progress bar. The bar pumps events, so this list
	// can be asked for its states from inside the run: lock everything until it returns.
	//
	// Every run, through KFCRunGuard - the search and the replace family. The replace needs
	// it at least as much as the search: it works with a command sequence standing open, and a second walk
	// underneath would Halt() its walker mid-walk.
	if (KFCRuns()->IsAnyRunning())
	{
		for (int32 i = 0; i < listToUpdate->Length(); i++)
			listToUpdate->SetNthActionState(i, kDisabled_Unselected);
		return;
	}

	// Is there anything for the current scope to run on at all - the target book, with a chapter in it,
	// while Book Scope is ON, the active document (IActiveContext) while it is OFF? The command that STARTS a
	// run - Find - asks it, so it is taken once here. See
	// KFCBookScope::HasScopeTarget: it asks what the engines themselves ask, so a command that is offered
	// can always run and one that cannot is visibly grey rather than reporting "No open document to
	// search." after the fact (the author's call).
	//
	// It is asked HERE rather than declared in the .fr as kDisableIfNoFrontDocument for two reasons
	// that are written out beside the action definitions: that flag would grey the commands out with
	// a book open and no document window - the state a book run is FOR - and it skips this hook, so
	// the search command would also lose the name that carries the scope.
	const bool16 haveTarget = KFCChapters()->HasScopeTarget() ? kTrue : kFalse;

	// How many rows still carry a check box in the range Check All / Uncheck All would act on. That
	// range is the row their right-click menu was popped over, so it is read here rather
	// than model-wide: over a document whose every hit is locked or already replaced, both commands
	// are no-ops and go grey - exactly as they do over a book with nothing left anywhere.
	//
	// TAKEN LAZILY: BOTH READERS ARE ON THE RIGHT-CLICK MENU, AND THIS HOOK ALSO RUNS FOR THE FLYOUT.
	// Whichever of the pair is reached first pays for the count (it walks the context row's hits, up to
	// kKFCCollectHitLimit of them), the other reads it, and a menu that holds neither - the flyout - never
	// asks. The CHECKED count has one reader, the replace command, and is taken inside that branch.
	// contextChapter is also what the document row's commands are about.
	const int32 contextChapter = KFCResults()->GetContextMenuChapter();
	int32 contextCheckable = -1;		// not counted yet - see the two commands at the end of the loop

	for (int32 i = 0; i < listToUpdate->Length(); i++)
	{
		const ActionID action = listToUpdate->GetNthAction(i);

		if (action == kKFCSearchBookActionID)
		{
			// The command's own name carries the scope, so it is visible BEFORE running it:
			// "Find in Book" while Book Scope is ON; with it off, the Search: of Edit > Find/Change it
			// follows ("Find in Document", "Find in Story", "Find to End of Story" ...).
			// No check mark - this is the KESCL Start/Stop pattern (a name swap, not a state mark).
			//
			// FIND, not "Search": Adobe's verb for this is Find - the dialog this
			// command takes its query from is Find/Change, its buttons are Find Next and Change All -
			// while "Search" is the label on that dialog's SCOPE popup ("Search: Document"). The
			// action's resource name stays "Search": that is the handle a script reaches this by
			// (app.menuActions.itemByName("Search")), and it is never what the user sees, because this
			// line renames it before the menu is drawn.
			PMString name(KFCRuns()->FindCommandName(KFCChapters()->IsBookScopeOn()));
			name.SetTranslatable(kFalse);
			listToUpdate->SetNthActionName(i, name);
			// The name is written whether or not it can run, so a greyed-out item still says which
			// scope it would have used.
			// ...and grey on the Object and Colour tabs too (the author's call): they find page
			// items, which this panel does not list. The same question the search asks (CanSearchTab).
			const bool canRun = haveTarget
				&& KFCRuns()->CanSearchTab(KFCRuns()->CurrentSearchMode());
			listToUpdate->SetNthActionState(i, canRun ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKFCScopeBookActionID)
		{
			int16 actionState = kEnabledAction;
			if (KFCChapters()->IsBookScopeOn())
				actionState |= kSelectedAction;		// ON: show the check mark (OFF is the default)
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKFCHidePrevChapterActionID)
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
		}
		else if (action == kKFCTranslucentPanelActionID)
		{
			// *Selectable even while the panel is docked - deliberately NOT greyed out (the user's
			// call, in KCM). Where the click has no visible result, DoAction says so on
			// the status line instead.
			int16 actionState = kEnabledAction;
			if (KFCGetPanelTranslucent())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKFCTranslucentFindChangeActionID)
		{
			// Selectable whether or not the Find/Change dialog is open, for the same reason the
			// panel's toggle stays selectable while docked: what is being set is the preference, and
			// it applies the moment the window exists. DoAction says which case it was.
			int16 actionState = kEnabledAction;
			if (KFCGetFindChangeTranslucent())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKFCMinimizableFindChangeActionID)
		{
			// Selectable whether or not the dialog is open, exactly like the toggle above: what is
			// being set is the preference, and it applies the moment the window exists.
			int16 actionState = kEnabledAction;
			if (KFCGetFindChangeMinimizable())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKFCAppBarSearchEnterActionID)
		{
			// Selectable whether or not the field is shown, like the toggles above: the flag is what is being set.
			int16 actionState = kEnabledAction;
			if (KFCGetAppBarSearchEnter())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKFCRememberBookPanelActionID)
		{
			// Selectable with no book open, like the toggles above: the flag is what is being set,
			// and it acts the next time a book panel closes or appears.
			int16 actionState = kEnabledAction;
			if (KFCBookPanelPlacement::IsOn())
				actionState |= kSelectedAction;		// show the check mark when ON
			listToUpdate->SetNthActionState(i, actionState);
		}
		else if (action == kKFCReplaceCheckedActionID)
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
			// THE SECOND HALF IS ONE QUESTION, AND THE MODEL ALREADY OWNS IT. "Can any row of this
			// list be checked at all" is NoRowHasCheckBox() - not spelled out again here.
			//
			// Walks every stored hit - up to kKFCCollectHitLimit of them, the whole-SEARCH ceiling (more than
			// the panel draws since 2026-10-05). Taken here rather than above the loop
			// because this is the only action that reads it.
			const int32 checkedCount = KFCResults()->GetCheckedCount();
			const bool16 canReplace = (checkedCount > 0 && !KFCResults()->NoRowHasCheckBox())
				? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, canReplace ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKFCStoryReplaceActionID || action == kKFCStoryCheckAllActionID
			|| action == kKFCStoryUncheckAllActionID)
		{
			// A story row's menu: each item while it has something to do in that story.
			int32 chapter = -1, group = -1;
			bool enable = KFCResults()->GetContextMenuGroup(chapter, group);
			if (enable)
			{
				if (action == kKFCStoryReplaceActionID)
					enable = KFCRuns()->CanReplaceStory(chapter, group);
				else
					// Check All / Uncheck All: while THIS story has a row with a box - the document row's question
					// (GetChapterCheckableCount below), one level down. Not the whole result set's NoRowHasCheckBox:
					// a story whose rows had all been replaced would still offer them while another story had boxes -
					// changing nothing and saying "This story: all checked." (measured).
					enable = KFCResults()->GetGroupCheckableCount(chapter, group) > 0;
			}
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKFCReplaceHitActionID)
		{
			// A hit row's menu: Replace while the row is a Find/Change match not yet replaced, not locked and
			// with nothing said about it (KFCReplaceEngine::CanReplaceHit).
			int32 chapter = -1, hit = -1;
			bool enable = KFCResults()->GetContextMenuHit(chapter, hit);
			if (enable)
				enable = KFCRuns()->CanReplaceHit(chapter, hit);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKFCChapterReplaceActionID)
		{
			// A document row's Replace: while that document has a ticked row to replace. The book row greys it: it
			// is about one document (Change Checked is the whole book's). The DoAction case asks the same range
			// first.
			const bool enable = contextChapter >= 0 && contextChapter < KFCResults()->GetChapterCount()
				&& KFCRuns()->CanReplaceChapter(contextChapter);
			listToUpdate->SetNthActionState(i, enable ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKFCCheckAllActionID || action == kKFCUncheckAllActionID)
		{
			// Nothing to check without results - and nothing to check after a replace either, where
			// the panel lists what CHANGED and no row has a box left. Both commands would be no-ops
			// there, so they go grey along with the boxes. Not a toggle - no check mark either way.
			//
			// Measured when these two were the WHOLE right-click menu: a popup whose every item
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
				if (contextChapter == KFCResultModel::kContextMenuBookRow)
					contextCheckable = KFCResults()->GetCheckableCount();
				else if (contextChapter != KFCResultModel::kNoContextMenuChapter)
					contextCheckable = KFCResults()->GetChapterCheckableCount(contextChapter);
			}
			const bool16 haveCheckable = (contextCheckable > 0) ? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, haveCheckable ? kEnabledAction : kDisabled_Unselected);
		}
	}
}


