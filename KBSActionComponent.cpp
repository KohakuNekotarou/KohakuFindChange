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
#include "KBSReplaceConfirmDialog.h"	// the Glyph tab's confirmation: the fonts behind the two glyphs
#include "KBSGlyphScanEngine.h"	// Find Missing Glyphs
#include "KBSOversetScanEngine.h"	// Find Overset
#include "KBSRunGuard.h"		// "is anything of ours running?" - one question, four runs
#include "KBSHowTo.h"			// "How to Use..." - the operating reference

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
		    @param outSaveAfterReplace OUT: did the user tick "save after replace"? Only written when
		           they approve; false is the every-time default.
		    @return true to go ahead. */
		bool ConfirmReplace(int32 checkedCount, bool& outSaveAfterReplace);
		


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
			// Search the active book (or the front document) with the user's current Find/Change
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

		case kKBSFindMissingGlyphsActionID:
		{
			// Scan the same scope for notdef glyphs - the boxes InDesign draws where a font has no
			// glyph for a character. The engine puts its own summary on the status line, so there
			// is nothing to report from here.
			KBSGlyphScanEngine::Run();
			break;
		}

		case kKBSFindOversetActionID:
		{
			// List the text that did not fit, over the same scope. The engine puts its own summary
			// on the status line, so there is nothing to report from here.
			KBSOversetScanEngine::Run();
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
			// Another run of ours is already up, reached through the events its progress bar pumps.
			// The engine turns this away as well - but not before the confirmation prompt would
			// have gone up over a run that is already under way, which is the whole reason the test
			// is here too. Asked about EVERY run rather than only another replace: a scan cancelled
			// underneath this one hands back the chapters it is about to write to (see KBSRunGuard).
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
			// Do the Find/Change settings still describe these rows - the tab, and the query with
			// every option that decides the match set? Asked HERE, ahead of the prompt, for the same
			// reason the report test above is: the prompt asks the user to authorise a rewrite, and
			// asking for that and then declining on the far side of it is the one thing a
			// confirmation must not do. (It was doing exactly that when this door first landed, an
			// hour before this line: the prompt went up, OK was pressed, and the engine then said the
			// query had changed.)
			//
			// The engine keeps the same door for a caller that never came through this menu, and
			// asking twice is safe - see KBSReplaceEngine::RefuseChangedQuery, which states the tab
			// by writing back the value it just read.
			//
			// ***** It can CLEAR the results (only when the query itself moved), so the tree is
			// redrawn here. ***** Nothing else on this path does: the ordinary route redraws after
			// ReplaceChecked returns, and this exit never reaches it.
			{
				PMString queryMoved;
				if (KBSReplaceEngine::RefuseChangedQuery(queryMoved))
				{
					KBSResultTree::Rebuild();
					KBSResultTree::ShowStatus(queryMoved);
					break;
				}
			}

			// false unless the user ticks the box, and it starts false on every run - the prompt
			// draws the box off and nothing remembers it.
			bool saveAfterReplace = false;
			if (!this->ConfirmReplace(checkedCount, saveAfterReplace))
				break;

			// ***** Ticking that box is the one choice here that nothing can take back. *****
			// A replace is undoable and a cancelled run rolls itself back - but a SAVED file is on
			// disk, and neither Ctrl+Z nor the progress bar's Cancel puts it back. It also changes
			// what Cancel MEANS: a saving run stops where it stands and leaves the chapters it
			// finished saved, where a run that does not save puts the whole book back.
			//
			// So it is asked once more, on its own, with CANCEL as the default button - a stray
			// Enter must not be what overwrites the user's files.
			//
			// Translated like the confirmation it follows: this is the moment the user authorises
			// overwriting their files, and the rule in this plug-in is that the prompts before a
			// rewrite are translated while the panel and its status line stay English.
			//
			// ***** No Translate() and no SetTranslatable() here. ***** ModalAlert translates
			// everything it is handed, including the message (CAlert.h:99-100), so the key goes
			// through as it stands.
			//
			// CAlert::SetShowAlerts(kFalse) - what a script running with NEVER_INTERACT does -
			// makes ModalAlert return the DEFAULT button (CAlert.h:220-222). That is Cancel here,
			// so an unattended script does not overwrite anything. Safe way round.
			if (saveAfterReplace)
			{
				PMString warning(kKBSSaveAfterReplaceWarningKey);
				const int16 answer = CAlert::ModalAlert(warning,
					kOKString, kCancelString, kNullString,
					2 /*Cancel is the default*/, CAlert::eWarningIcon);
				if (answer != 1)
					break;
			}

			PMString summary;
			KBSReplaceEngine::ReplaceChecked(summary, saveAfterReplace);
			KBSResultTree::Rebuild();		// replaced rows lose their box and fade
			KBSResultTree::ShowStatus(summary);
			break;
		}

		case kKBSSaveResultsActionID:
		{
			// Write the current result set to a text file. Everything - the busy test, the empty test,
			// the chooser, the encoding - is in KBSResultTree::SaveResultsAsText, which is where the
			// panel's own file-facing work lives; nothing to report from here.
			KBSResultTree::SaveResultsAsText();
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

// "Is anything set in the format pane?" now lives in KBSSearchEngine (HasFindFormatSet /
// HasChangeFormatSet) - the search's own caption asks the same question, and the two answers must
// not drift apart. It also looks in one more place than the version that used to sit here: a
// paragraph or character STYLE is not in the attribute list this counted, which is what made the
// prompt print "Find: ^1" and threaten to delete matches it was only going to restyle (2026-08-04).
//
// WHAT is set is still not named - see the note on HasFormatSet in KBSSearchEngine.cpp.

// ***** ReplaceStringParameters LEAVES ^1 STANDING WHEN THE STRING IS EMPTY. ***** The header says
// the parameter "may be empty"; what the prompt actually printed for a format-only search was the
// literal "Find: ^1" (measured 2026-08-04). Both sides of this prompt can legitimately be empty - a
// search by formatting alone, and a Glyph-tab replace with an empty Change To box, where the blank
// after the label IS the message - so an empty side is handed over as a single space.
static void SpaceIfEmpty(PMString& str)
{
	if (str.IsEmpty())
	{
		str = PMString(" ");
		str.SetTranslatable(kFalse);
	}
}

// Append the dialog's name for the format pane to one side of the prompt, as the user asked it to
// read: "cat  + Find Format" when both are set, "Find Format" on its own when the box is empty.
// Translated - this prompt is the one place KBS translates (see the note in ConfirmReplace).
static void AppendFormatNote(PMString& str, const char* formatKey, const PMString& detail)
{
	PMString note(formatKey);
	note.Translate();
	if (str.IsEmpty())
		str = note;
	else
	{
		str.Append("  + ");
		str.Append(note);
	}
	// ...and WHAT is set, in InDesign's own words (KBSSearchEngine::DescribeFormatSetting). Empty
	// means "nothing extra to say" - never "nothing is set", which is HasFindFormatSet's answer and
	// was decided before this line is reached.
	//
	// NOTE: this goes on AFTER InsertAmpersandForDisplay has doubled the user's own strings, so an
	// '&' inside a style or swatch name would be eaten by the alert's accelerator handling. Worth
	// doubling here too if this detail is kept.
	if (!detail.IsEmpty())
	{
		str.Append(" (");
		str.Append(detail);
		str.Append(")");
	}
	str.SetTranslatable(kFalse);
}

/* ConfirmReplace
*/
bool KBSActionComponent::ConfirmReplace(int32 checkedCount, bool& outSaveAfterReplace)
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
	const bool glyphResolved = glyphMode && KBSReplaceConfirmDialog::Resolve(opts);

	// On the Glyph tab, show the glyphs THEMSELVES rather than their numbers - but the choice of
	// layout is made at the single call at the bottom of this function, not here, so the message
	// below is assembled either way. It is what the glyph layout falls back to when the fonts
	// could not be resolved (a ROS-group query carries none), and a confirmation that cannot be
	// DRAWN is never a reason the replace cannot RUN.

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
			glyphResolved ? KBSReplaceConfirmDialog::GetFindSide().fFontLabel : PMString());
	findStr.SetTranslatable(kFalse);
	// CAlert draws its message through a widget that reads a lone '&' as a keyboard accelerator -
	// its own check box arrives spelled "&Don't show again". Without this a search for "A&B" is
	// quoted back as "AB", in the ONE place the user checks what is about to be written (reported
	// from the running panel, 2026-07-31). Same doubling the tree rows and the status line do.
	Utils<IMenuUtils>()->InsertAmpersandForDisplay(&findStr);
	// Find Format, appended AFTER the ampersand doubling: that guards the USER's string, and what
	// goes on here is ours (it carries no ampersand to double).
	//
	// Not on the Glyph tab. Its attribute list is where the QUERY's own font and style live - the
	// glyph id names nothing without them - so the list is never empty there and the note would be
	// on every glyph prompt, saying nothing. That tab states its query by DRAWING the glyphs.
	if (!glyphMode && KBSSearchEngine::HasFindFormatSet())
		AppendFormatNote(findStr, kKBSConfirmFindFormatKey,
			KBSSearchEngine::DescribeFormatSetting(true /*findSide*/));
	PMString findLine(kKBSConfirmFindKey);
	findLine.Translate();
	SpaceIfEmpty(findStr);
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
				glyphResolved ? KBSReplaceConfirmDialog::GetChangeSide().fFontLabel : PMString());
	}
	else
		replaceStr = opts->GetReplaceString(mode);
	replaceStr.SetTranslatable(kFalse);
	// ***** AN EMPTY CHANGE BOX MEANS TWO DIFFERENT THINGS. ***** With no Change Format set it
	// DELETES every match; with one set it changes the FORMAT and leaves the text alone. Saying
	// "the matches will be deleted" about the second is the prompt telling the user the opposite of
	// what is about to happen, so the format is asked about first.
	const bool changeHasFormat = !glyphMode && KBSSearchEngine::HasChangeFormatSet();

	// Text and GREP only: on those tabs the change string is a STRING, and a blank line there could
	// as easily be a mistake as a deletion, so it is spelled out. The Glyph tab says it with the
	// blank itself - see above.
	if (replaceStr.IsEmpty() && !glyphMode && !changeHasFormat)
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
	// "dog  + Change Format", or "Change Format" on its own when the box is empty.
	if (changeHasFormat)
		AppendFormatNote(replaceStr, kKBSConfirmChangeFormatKey,
			KBSSearchEngine::DescribeFormatSetting(false /*findSide*/));
	PMString changeLine(kKBSConfirmChangeToKey);
	changeLine.Translate();
	SpaceIfEmpty(replaceStr);
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

	// ONE prompt for every tab (2026-08-02). It used to be CAlert::ModalAlert here and the glyph
	// dialog above; both are the same dialog now, because an alert cannot carry a check box with a
	// label of our own - the SDK offers "Don't show again" and "Apply to All", both fixed wording
	// (CAlert.h:185,209) - and the "save after replace" box had to go somewhere.
	//
	// Keeping CANCEL as the default button was the other reason. This starts a destructive rewrite
	// and a stray Enter must not be what starts it; WarningAlertWithDontShowAgain, the one alert
	// that draws a box at all, takes no default-button argument.
	//
	// An empty message asks with the GLYPH layout, so this is where the two part company: the
	// glyphs when they resolved, the assembled sentences when they did not.
	PMString glyphLayout;
	const bool approved = KBSReplaceConfirmDialog::Ask(checkedCount, chapterCount,
		glyphResolved ? glyphLayout : msg, outSaveAfterReplace);

	// Drop the fonts taken above. This is the only exit past the resolve, so one call covers it.
	KBSReplaceConfirmDialog::ReleaseSides();

	return approved;
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
	// Is there anything for the current scope to run on at all - the active book while Book Scope is
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
			listToUpdate->SetNthActionState(i, haveTarget ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSFindMissingGlyphsActionID)
		{
			// The same one question the search command above asks, because it runs over the same
			// scope: something to scan, or grey. Nothing about the current RESULTS decides whether a
			// scan may run - it starts from the document, not from them.
			//
			// It has to be said explicitly either way: kCustomEnabling means this method owns the
			// state, and an action this loop never names stays DISABLED. (Found on the real
			// application - the item appeared in the flyout but invoke() answered "Action is not
			// enabled".)
			listToUpdate->SetNthActionState(i, haveTarget ? kEnabledAction : kDisabled_Unselected);
		}
		else if (action == kKBSFindOversetActionID)
		{
			// The same one question the two commands above ask, because it runs over the same scope:
			// something to scan, or grey. Nothing about the current RESULTS decides whether a scan
			// may run - it starts from the document, not from them.
			//
			// Named here explicitly for the same reason as the glyph scan: kCustomEnabling means
			// this method owns the state, and an action this loop never names stays DISABLED.
			listToUpdate->SetNthActionState(i, haveTarget ? kEnabledAction : kDisabled_Unselected);
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
		else if (action == kKBSSaveResultsActionID)
		{
			// Nothing to write without results. Asked of the STORED count rather than the displayed
			// one: the file carries every hit, including those past the panel's display cap, so a
			// result set that is mostly cap is still worth writing.
			//
			// Nothing else is asked. Unlike the three commands above it, this one does not run over the
			// document - it writes down what already happened - so an empty desk does not stop it, and
			// neither does a replace's aftermath, which is exactly the list a user wants to keep.
			listToUpdate->SetNthActionState(i,
				(KBSResultModel::GetTotalHitCount() > 0) ? kEnabledAction : kDisabled_Unselected);
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


