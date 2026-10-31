//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE KOHAKU TAB'S FACE IN INDESIGN'S FIND/CHANGE DIALOG. One face of KFC's own among the dialog's tabs - "Kohaku+" (the author's word: "Kohaku" alone says little) - offered through kFindChangeMiniTabbedDialogService (KFCUI.fr's IDList /
//  IDListPair). Its Find: chooses what KFC's Find looks for: QR Code (docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md), Table (2026-10-31-kfc-table-tab-design.md) or, in a Japanese
//  InDesign alone (the author's call, 10-31), Ruby (2026-10-31-kfc-ruby-tab-design.md) or Kenten (model/KFCKentenSearch.h). ONE TAB, ON THE AUTHOR'S CALL (10-31): a tab each made nine, and the dialog's strip, which no face can widen (measured), then cut its first and last down to a
//  character or two in the Japanese InDesign.
//  Search: is theirs together. What stands in a story's text - Table, Ruby, Kenten - has InDesign's five (All Documents, Document, Story, To End of Story, Selection: the author, "a table stands in a
//  story"); a QR code's frame the first two, which is what a Story on Table reads as on QR Code. Document at launch. Every entry is always on the list, unlike the dialog's own: what the selection does
//  not offer is searched as Document and said (KFCSearchEngine::ResolveRunScope, the Text tab's words), and the person's pick stays through a selection that moves.
//  Below them stands the chosen one's part, the others hidden and disabled (a stacked panel - kEVEArrangeChildrenStacked; a hidden widget left enabled still takes a click):
//    QR Code  Find Next (the QR code after the one it found last - the model's KFCQRSearch::Next - landed on as an object row's click lands, KFCJump::LandOnQRCode; the top right of the face, as
//             InDesign's faces' buttons) and two lines for what it says.
//    Table    Find Table Style: ([Any Table Style] and the front document's table styles), With Overrides Only, Change to Table Style: ([Leave As It Is] and the styles), Clear Overrides. A
//             style is held by its full path, untranslated: a list's search and a row's Replace read it by name in each document (KFCTableSearch). The lists follow the front document and its
//             styles while the face is shown, and a style the front document does not have stays on the list while it is asked for.
//    Ruby     Look For: (Text With Ruby / Text Without Ruby / Ruby Marks in Text), Base Text:, Ruby:, Change Ruby To:, Remove Ruby. The fields are kept at every key stroke: the panel's Find may be
//             pressed with one still focused.
//    Kenten   Look For: (Text With Kenten / Text Without Kenten), Base Text:, Kenten: ([Any Kenten] and the kinds), Change Kenten To: ([Leave As It Is] and the kinds), Remove Kenten.
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

	/** Application shutdown: what the face keeps holds PMStrings - released while the session stands (the plug-in's other statics' rule). */
	void ShutdownCleanup();
}

#endif // __KFCKohakuTab_h__

// End, KFCKohakuTab.h.
