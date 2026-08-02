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
#include "CTextEnum.h"			// Text::GlyphID / kInvalidGlyphID (the Glyph tab's query)

// General includes:
#include "CActionComponent.h"
#include "CAlert.h"
#include "CoreResTypes.h"		// kLineSeparatorString - the prompt is several lines
#include "IActionStateList.h"	// UpdateActionStates: check mark for the Hide Previous Chapter toggle
#include "PreferenceUtils.h"	// QuerySessionPreferences
#include "StringUtils.h"		// ::ReplaceStringParameters - fills the ^1 in a translated string
#include "Utils.h"

// Interface includes (cont.):
#include "IMenuUtils.h"		// InsertAmpersandForDisplay - the find/change strings are the user's

// Project includes:
#include "KBSID.h"
#include "KBSSearchEngine.h"
#include "KBSResultTree.h"		// rebuild the result tree after a search
#include "KBSJump.h"			// the Hide Previous Chapter toggle lives with the jump logic
#include "KBSBookScope.h"		// the Book Scope toggle's session state
#include "KBSResultModel.h"		// the check state Check All / Uncheck All flips
#include "KBSReplaceEngine.h"	// Change Checked
#include "KBSPanelTitle.h"		// the panel's tab name carries the current scope
#include "KBSGlyphConfirmDialog.h"	// the Glyph tab's confirmation: the fonts behind the two glyphs
#include "KBSGlyphScanEngine.h"	// Find Missing Glyphs

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

		case kKBSFindMissingGlyphsActionID:
		{
			// Scan the same scope for notdef glyphs - the boxes InDesign draws where a font has no
			// glyph for a character. The engine puts its own summary on the status line, so there
			// is nothing to report from here.
			KBSGlyphScanEngine::Run();
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

			// The panel is a REPORT of what the last replace did, not a work list. The menu greys
			// this command out in that state (see UpdateActionStates), but a caller that never went
			// through the menu - a script invoking the action - lands here whatever the menu says.
			// It has to be stopped BEFORE the prompt: the rows the last run never reached keep their
			// check so the report can account for them, so GetCheckedCount() is still positive and
			// the user would otherwise be asked to authorise a rewrite that the engine declines on
			// the far side of the prompt. Same wording as the engine's own door.
			if (KBSResultModel::IsShowingReplaceOutcome())
			{
				PMString report("This is the last replace's report - search again to replace more.");
				report.SetTranslatable(kFalse);
				KBSResultTree::ShowStatus(report);
				break;
			}

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
			// (kKBSDisplayHitLimit), which is why the status line spells the numbers out afterwards.
			const int32 target = KBSResultModel::GetContextMenuChapter();
			if (target == KBSResultModel::kNoContextMenuChapter)
				break;
			const bool check = (actionID.Get() == kKBSCheckAllActionID);
			if (target == KBSResultModel::kContextMenuBookRow)
				KBSResultModel::SetAllChecked(check);
			else
				KBSResultModel::SetChapterChecked(target, check);
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

// Name a glyph the way the confirmation prompt has to name it. The Glyph tab's query is a glyph,
// not a string, so quoting the find / change strings there would describe the wrong thing (and on
// the change side there is no string at all). The ID is always shown, because the ID is what the
// search and the replace actually run on - but an ID alone does not identify a character (a GID
// belongs to one font file, a CID means a different character under a different ROS), so the font
// is named beside it whenever it could be resolved. Pure data: never translated.
static void AppendGlyphDescription(PMString& str, Text::GlyphID glyphID, const PMString& fontLabel)
{
	if (!str.IsEmpty())
		str.Append("  ");
	str.Append("[glyph ");
	str.AppendNumber(glyphID);
	str.Append("]");
	if (!fontLabel.IsEmpty())
	{
		str.Append("  ");
		str.Append(fontLabel);
	}
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
	const bool glyphMode = (mode == IFindChangeOptions::kGlyphSearch);

	// A glyph replace with nothing in the Change To box used to be refused here. It is not: an empty
	// Change To DELETES every match, which is what the Find/Change dialog does with it (confirmed
	// against the dialog, 2026-07-31), and refusing it made this panel less capable than the dialog
	// it delegates to. It is still shown to the user before anything is written - the prompt below
	// simply leaves the "Change to:" line empty, and an empty line after that label is what an empty
	// box looks like.

	// On the Glyph tab, work out which fonts the two glyphs belong to. A glyph id names nothing
	// by itself, so the answer is worth having even here, in the plain prompt: it is what turns
	// "[glyph 7425]" into something the user can check against the Find/Change dialog. Whatever
	// this resolves is released before every exit below.
	const bool glyphResolved = glyphMode && KBSGlyphConfirmDialog::Resolve(opts);

	// On the Glyph tab, show the glyphs THEMSELVES rather than their numbers. Falls through to the
	// alert below whenever the fonts could not be resolved - a ROS-group query carries none - so a
	// confirmation that cannot be drawn is never a reason the replace cannot run.
	if (glyphResolved)
	{
		const bool approved = KBSGlyphConfirmDialog::Ask(checkedCount,
			KBSResultModel::GetCheckedChapterCount());
		KBSGlyphConfirmDialog::ReleaseSides();
		return approved;
	}

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
	if (glyphMode)
		AppendGlyphDescription(findStr, opts->GetFindGlyphID(),
			glyphResolved ? KBSGlyphConfirmDialog::GetFindSide().fFontLabel : PMString());
	findStr.SetTranslatable(kFalse);
	// CAlert draws its message through a widget that reads a lone '&' as a keyboard accelerator -
	// its own check box arrives spelled "&Don't show again". Without this a search for "A&B" is
	// quoted back as "AB", in the ONE place the user checks what is about to be written (reported
	// from the running panel, 2026-07-31). Same doubling the tree rows and the status line do.
	Utils<IMenuUtils>()->InsertAmpersandForDisplay(&findStr);
	PMString findLine(kKBSConfirmFindKey);
	findLine.Translate();
	::ReplaceStringParameters(&findLine, findStr);
	msg.Append(findLine);
	// The dialog's own name for the mode, untranslated everywhere. Named for Glyph as well as GREP:
	// on that tab what is quoted above is a glyph, and the line has to say so.
	if (mode == IFindChangeOptions::kGrepSearch)
		msg.Append("   (GREP)");
	else if (glyphMode)
		msg.Append("   (Glyph)");
	msg.Append(kLineSeparatorString);

	// On the Glyph tab the change side has no string at all - it is the glyph chosen in Change To,
	// which the refusal above has already established is there.
	PMString replaceStr;
	if (glyphMode)
	{
		// An empty Change To box leaves this line EMPTY - no "[glyph -1]", and no sentence
		// explaining it either (user's decision, 2026-07-31). The blank after the label is what an
		// empty box looks like, and it reads the same way in every language.
		if (opts->GetReplaceGlyphID() != kInvalidGlyphID)
			AppendGlyphDescription(replaceStr, opts->GetReplaceGlyphID(),
				glyphResolved ? KBSGlyphConfirmDialog::GetChangeSide().fFontLabel : PMString());
	}
	else
		replaceStr = opts->GetReplaceString(mode);
	replaceStr.SetTranslatable(kFalse);
	// Text and GREP only: on those tabs the change string is a STRING, and a blank line there could
	// as easily be a mistake as a deletion, so it is spelled out. The Glyph tab says it with the
	// blank itself - see above.
	if (replaceStr.IsEmpty() && !glyphMode)
	{
		// An empty change string is a legitimate request - it deletes every match - so it is
		// spelled out instead of leaving a blank line for the user to interpret.
		PMString empty(kKBSConfirmEmptyReplaceKey);
		empty.Translate();
		replaceStr = empty;
		replaceStr.SetTranslatable(kFalse);
	}
	// Same reason as the find string above. Harmless on the "(empty - the matches will be deleted)"
	// wording that replaces it when Change To is blank: that carries no ampersand to double.
	Utils<IMenuUtils>()->InsertAmpersandForDisplay(&replaceStr);
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

	// A plain modal alert, so that CANCEL can be the default button: this starts a destructive
	// rewrite, and a stray Enter must not be what starts it.
	//
	// It carried a "Don't show again" check box from 2026-07-28 until 2026-08-01. That box wrote to
	// the application's alert registry, and the trade turned out to be a bad one: ticking it removed
	// the confirmation permanently and silently (it comes back only through Preferences > General >
	// Reset All Warning Dialogs, which nobody thinks to look for), and the call that draws the box
	// takes no default-button argument, so OK had to be the default for as long as it existed. A
	// suppressible prompt in front of a destructive action was worth less than a default of Cancel.
	const int16 answer = CAlert::ModalAlert(msg,
		kOKString,
		kCancelString,
		kNullString,					// no third button
		2,								// Cancel is the default
		CAlert::eWarningIcon);

	// Drop the fonts taken above. This is the only exit past the resolve, so one call covers it.
	KBSGlyphConfirmDialog::ReleaseSides();

	// 1 = OK, 2 = Cancel.
	return answer == 1;
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

	// Both counts walk every stored hit - up to kKBSCollectHitLimit of them, the whole-SEARCH ceiling
	// rather than the smaller number the panel displays - and this list normally holds more than one
	// action that asks for them, so take each once here instead of per action. The cap is named
	// rather than spelled out: this comment read "5000" long after the ceiling became 10000.
	const int32 checkedCount = KBSResultModel::GetCheckedCount();

	// How many rows still carry a check box in the range Check All / Uncheck All would act on. That
	// range is the row their right-click menu was popped over (2026-08-01), so it is read here rather
	// than model-wide: over a document whose every hit is locked or already replaced, both commands
	// are no-ops and go grey - exactly as they do over a book with nothing left anywhere. Taken once
	// because the two commands ask the same question.
	const int32 contextChapter = KBSResultModel::GetContextMenuChapter();
	int32 contextCheckable = 0;
	if (contextChapter == KBSResultModel::kContextMenuBookRow)
		contextCheckable = KBSResultModel::GetCheckableCount();
	else if (contextChapter != KBSResultModel::kNoContextMenuChapter)
		contextCheckable = KBSResultModel::GetChapterCheckableCount(contextChapter);

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
		else if (action == kKBSFindMissingGlyphsActionID)
		{
			// Always live, for the same reasons the search command above is: it runs over the same
			// scope, and a book scan works with no document window open. Nothing about the current
			// RESULTS decides whether a scan may run - it starts from the document, not from them.
			//
			// It has to be said explicitly all the same: kCustomEnabling means this method owns the
			// state, and an action this loop never names stays DISABLED. (Found on the real
			// application - the item appeared in the flyout but invoke() answered "Action is not
			// enabled".)
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
			//
			// A missing-glyph scan is a report as well: none of it can be replaced, and its rows
			// carry no check boxes (RowHasCheckBox), so checkedCount would already be 0. Stated all
			// the same - for the same reason IsShowingReplaceOutcome is stated - so the rule does
			// not rest on a count that a later change might make non-zero.
			const bool16 canReplace = (checkedCount > 0 && !KBSResultModel::IsShowingReplaceOutcome()
				&& KBSResultModel::GetResultKind() == KBSResultModel::kResultFindChange)
				? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, canReplace ? kEnabledAction : kDisabled_Unselected);
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
			const bool16 haveCheckable = (contextCheckable > 0) ? kTrue : kFalse;
			listToUpdate->SetNthActionState(i, haveCheckable ? kEnabledAction : kDisabled_Unselected);
		}
	}
}


