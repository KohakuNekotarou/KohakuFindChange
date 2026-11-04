//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU TAB'S FACE IN INDESIGN'S FIND/CHANGE DIALOG. One face of KFC's own among the dialog's tabs - "Kohaku+" (the author's word: "Kohaku" alone says little) - offered through kFindChangeMiniTabbedDialogService (KFCUI.fr's IDList /
//  IDListPair). Its Find: chooses what KFC's Find looks for, in the author's order of 11-04: Script Label (2026-11-04-kfc-script-label-design.md), QR Code (docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md),
//  Table (2026-10-31-kfc-table-tab-design.md) and, in a Japanese InDesign alone (the author's call, 10-31), Kenten (model/KFCKentenSearch.h), Ruby (2026-10-31-kfc-ruby-tab-design.md) and Warichu
//  (KFCKohakuTab.cpp's kKindsInOrder). ONE TAB, ON THE AUTHOR'S CALL (10-31): a tab each made nine, and the dialog's strip, which no face can widen (measured), then cut its first and last down to a
//  character or two in the Japanese InDesign.
//  Search: is theirs together. What stands in a story's text - Table, Ruby, Kenten - has InDesign's five (All Documents, Document, Story, To End of Story, Selection: the author, "a table stands in a
//  story"); a QR code's frame the first two, which is what a Story on Table reads as on QR Code. Document at launch. Every entry is always on the list, unlike the dialog's own: what the selection does
//  not offer is searched as Document and said (KFCSearchEngine::ResolveRunScope, the Text tab's words), and the person's pick stays through a selection that moves.
//  THE FACE (docs/superpowers/specs/2026-11-02-kfc-kohaku-face-buttons-design.md): two columns, as InDesign's own faces stand. On the left all that is set - Find: and Search: each over its list, the line under Search:, and the
//  chosen kind's part, the others hidden and disabled (a stacked panel; a hidden widget left enabled still takes a click). On the right every button: Find Next, Change, Change All, Change/Find (InDesign's place and size),
//  Find in KFC (the panel's own Find - KFCResultHistory::Find - the panel shown first), and the part's own. Under both, the face's two lines, every kind's: what a button or the list said, and why what was found could not be selected.
//    QR Code  Find Next (the QR code after the one it found last - the model's KFCQRSearch::Next - landed on as an object row's click lands, KFCJump::LandOnQRCode). Nothing to change: no Change buttons.
//    Table    Find Table Style: ([Any Table Style] and the front document's table styles), With Overrides Only, Change to Table Style: ([Leave As It Is] and the styles), Clear Overrides. A
//             style is held by its full path, untranslated: a list's search and a row's Replace read it by name in each document (KFCTableSearch). The lists follow the front document and its
//             styles while the face is shown, and a style the front document does not have stays on the list while it is asked for.
//    Ruby     Look For: (Text With Ruby / Text Without Ruby / Ruby Marks in Text / Words in a Ruby List / Inconsistent Ruby), and the boxes (the author, 11-01; eight, fixed, 11-02): Find's Ruby: over its Base Text:, a chain between neighbours that makes their reading one - group ruby - and Change Ruby To's, which
//             follow them, column for column and join for join, the column's base text under each as words. A search reads the boxes' words run together. No Change reading at all takes the ruby off (KFCRubyChangePlan.h). The fields are kept at every key stroke: the panel's
//             Find may be pressed with one still focused; a base box's characters are spread over the boxes at the next idle (SettleBaseBoxes). WORDS IN A RUBY LIST (KFCRubyList.h): the boxes away, the file's name, which occurrences, and Include Text With Other Ruby in their place.
//             Its buttons Choose List..., Save List..., How to Use... (a page in a Japanese InDesign's one language - the author, 11-02).
//    Kenten   Base Text:, Kenten: ([Any] / none / the kinds / custom - InDesign's kenten dialog's words), Change Kenten To: ([Leave] / none / the kinds / custom), a box of one character beside each list, a
//             custom mark's (2026-11-03 - docs/superpowers/specs/2026-11-03-kfc-kenten-face-japanese-design.md: Look For and Remove Kenten went into the lists' none; the samples went). Its button How to Use....
//    Script Label  every language's, Find:'s first (docs/superpowers/specs/2026-11-04-kfc-script-label-design.md S1, S2 - first since the author's order of 11-04): Key: (empty: every key - 11-04), Match: (Text (Contains), Text (Whole
//             Label) or GREP - always case by case, 11-04), Find Label: (empty: every label of the key), Change Label: - Search: All Documents or Document, as QR Code's. Its Find Next lands on an item as an object row's click lands
//             (KFCJump::LandOnQRCode); its Change, Change All and Change/Find write labels (the model's KFCScriptLabelSearch).
//  TABLE, RUBY AND KENTEN'S FIND NEXT, CHANGE, CHANGE ALL, CHANGE/FIND (2026-11-01-kfc-kohaku-trial-fixes-design.md F1 - F9; 2026-11-02-kfc-kohaku-face-buttons-design.md B1 - B5): the model's (KFCTableSearch, KFCFaceText), landed on as a
//  row's click lands (KFCJump::LandOnPlace); Change and Change/Find grey until Find Next has found something, and once the selection leaves it or it is written, as the Text tab's.
//  While the face is the dialog's tab - or was when the dialog closed, the face it opens on again - KFC's Find looks for what its Find: says (KFCQRSearch, KFCTableSearch, KFCRubySearch in the model
//  half, which ask through IKFCUIServices).
//  WHAT THE TAB IS IS KEPT FROM THE FACE'S OWN CALLS - NO WINDOW IS WALKED (the QR design's D1, and its measurement's hazard: a face that walked the dialog's windows and widgets while the dialog
//  closed left Find/Change stuck - drawn, but gone). Shown = from its AutoAttach to its AutoDetach; closed on it = its ClosingDialog came while it was shown (or its own parent, the dialog's switcher,
//  said it was the current face), until it is shown again. Switching to it, or its Find:, sends no notification of Find/Change's settings (measured), so the face writes the panel's title (and the
//  Application Bar's linked field) again itself - and from nothing that runs while the dialog closes. The main thread only.
//
//========================================================================================

#ifndef __KFCKohakuTab_h__
#define __KFCKohakuTab_h__

#include "KFCModelTypes.h"		// KFCTableConditions, KFCRubyConditions, KFCKentenConditions
#include "PMTypes.h"			// WidgetID

namespace KFCKohakuTab
{
	/** IS THE QR CODE KIND FIND/CHANGE'S NOW (IKFCUIServices::IsQRCodeTabCurrent): the face shown with QR Code in its Find:, or the dialog closed so - but not while a saved query loaded since stands
	    (YieldToSavedQuery). outScope = what its Search: is for a QR code, either way (IWalkerScopeFactoryUtils::kDocumentScope - at launch - or kAllDocumentScope). */
	bool IsQRCodeCurrent(int32& outScope);

	/** The same for Table (IKFCUIServices::TableTabConditions). outConditions = what the face asks as it stands, either way: a row's Replace reads its Change side whatever is shown. */
	bool IsTableCurrent(KFCTableConditions& outConditions);

	/** The same for Ruby (IKFCUIServices::RubyTabConditions). */
	bool IsRubyCurrent(KFCRubyConditions& outConditions);

	/** The same for Kenten (IKFCUIServices::KentenTabConditions). */
	bool IsKentenCurrent(KFCKentenConditions& outConditions);

	/** IS THE WARICHU KIND FIND/CHANGE'S NOW (docs/superpowers/specs/2026-11-02-kfc-warichu-tcy-design.md A1 - asked by the model through IKFCUIServices::WarichuTabConditions), and what it asks:
	    its icons and Search:. */
	bool IsWarichuCurrent(KFCWarichuConditions& outConditions);

	/** IS THE SCRIPT LABEL KIND FIND/CHANGE'S NOW (docs/superpowers/specs/2026-11-04-kfc-script-label-design.md S1 - asked by the model through IKFCUIServices::ScriptLabelTabConditions), and
	    what it asks as it stands, either way: its Key:, Match:, Find Label:, Change Label: and Search: - a row's Replace reads its Change side whatever is shown. */
	bool IsScriptLabelCurrent(KFCScriptLabelConditions& outConditions);

	/** A SAVED QUERY STATES ITS OWN TAB (the query dialog's Find - KFCQueryDialog, once the query is loaded): the Kohaku tab steps aside until the person shows its face again or picks in its Find:, so
	    the Find searches what the query says. */
	void YieldToSavedQuery();

	/** IS THE QUERY'S KIND ONE THIS INDESIGN OFFERS (Ruby and Kenten: a Japanese InDesign's)? */
	bool OffersQuery(const KFCKohakuQueryData& query);

	/** A SAVED QUERY OF THE TAB STATED (KFCKohakuQueryMenu - a pick in Find/Change's own Query menu; KFC's query dialog's Find and its double click): its kind and its conditions become the face's, both sides - a row's Replace reads
	    the Change side as it stands. forItsFind: until EndStatedFind, KFC's Find takes it, over the Search: given (scope), whatever the dialog shows. */
	void StateQuery(const KFCKohakuQueryData& query, bool forItsFind, int32 scope);

	/** Its Find is over: what Find/Change's tab is is the face's own again. */
	void EndStatedFind();

	/** What the face asks now, as a query (Save Query - KFCKohakuQueryMenu). */
	void QueryOfFace(KFCKohakuQueryData& out);

	/** IS THIS A JAPANESE INDESIGN - what puts Ruby and Kenten on Find: (KOHAKU-03), asked of the one place: a text row's Special Operations > Tate-chu-yoko too (docs/superpowers/specs/
	    2026-11-02-kfc-warichu-tcy-design.md B1). A test build's fault switch not-japanese says no. */
	bool IsJapaneseUI();

	/** Application shutdown: what the face keeps holds PMStrings - released while the session stands (the plug-in's other statics' rule). */
	void ShutdownCleanup();
}

#endif // __KFCKohakuTab_h__

// End, KFCKohakuTab.h.
