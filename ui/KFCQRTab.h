//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QR CODE TAB'S FACE IN INDESIGN'S FIND/CHANGE DIALOG (docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md -
//  measured in KT: docs/ai-notes/kfc-qr-tab-measure-2026-10-27.md in the SDK folder). A face of KFC's own among the dialog's
//  tabs - "QR Code" - offered through kFindChangeMiniTabbedDialogService (KFCUI.fr's IDList / IDListPair): Search: (All
//  Documents / Document - Q4; Document at launch - Q19), Find Next (the QR code after the one it found last, in the list's
//  own order - the model's KFCQRSearch::Next - landed on as an object row's click lands, KFCJump::LandOnQRCode) and a line
//  for what Find Next says. The frame's
//  Done closes the dialog as on every tab (Q11 - the frame's own). While the face is the dialog's tab - or was when the
//  dialog closed, the face it opens on again - KFC's Find lists QR codes (KFCQRSearch in the model half, which asks
//  IsCurrent through IKFCUIServices::IsQRCodeTabCurrent).
//  WHAT THE TAB IS IS KEPT FROM THE FACE'S OWN CALLS - NO WINDOW IS WALKED (the design's D1, and the measurement's hazard: a
//  face that walked the dialog's windows and widgets while the dialog closed left Find/Change stuck - drawn, but gone).
//  Shown = from its AutoAttach to its AutoDetach; closed on it = its ClosingDialog came while it was shown (or its own parent,
//  the dialog's switcher, said it was the current face), until it is shown again. Switching to it sends no notification of
//  Find/Change's settings (measured), so the face writes the panel's title (and the Application Bar's linked field - Q17)
//  again itself - from its AutoAttach and from its Search: - and from nothing that runs while the dialog closes. The main
//  thread only.
//
//========================================================================================

#ifndef __KFCQRTab_h__
#define __KFCQRTab_h__

#include "BaseType.h"		// int32

namespace KFCQRTab
{
	/** IS THE QR CODE TAB FIND/CHANGE'S TAB NOW (IKFCUIServices::IsQRCodeTabCurrent): its face shown, or the dialog closed
	    with it shown last - but not while a saved query loaded since stands (YieldToSavedQuery). outScope = its Search:
	    either way (IWalkerScopeFactoryUtils::kDocumentScope - at launch - or kAllDocumentScope). */
	bool IsCurrent(int32& outScope);

	/** A SAVED QUERY STATES ITS OWN TAB (the query dialog's Find - KFCQueryDialog, once the query is loaded): the QR Code tab
	    steps aside until the person shows its face again, so the Find searches what the query says. */
	void YieldToSavedQuery();
}

#endif // __KFCQRTab_h__

// End, KFCQRTab.h.
