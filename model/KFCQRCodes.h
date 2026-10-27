//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A DOCUMENT'S QR CODES, READ (docs/superpowers/specs/2026-10-27-kfc-qr-code-search-design.md - the QR Code tab).
//  A QR code is an EPS item (kEPSItem) whose IID_IQRCODEDATAUID (IPersistUIDData) names the boss its content is kept on -
//  one of five kinds, told by the interface a Query of that boss answers (IQRHyperLinkData, IQRPlainTextData, IQRSMSData,
//  IQREmailData, IQRvCardData: EPSID.h, the graphics headers; no facade reads one - IBarcodeFacade only makes them). The
//  frame the person sees and selects holds the EPS item as its one child.
//  THEY ARE FOUND BY AN OBJECT WALKER OF KFC'S OWN (kObjectWalkerBoss), every Include on, the Type All Frames: not the
//  session's shared walker and not the find/change service, so a walk selects nothing, scrolls nothing, raises no alert (a
//  hidden spread's) and leaves the Find/Change settings alone - and it reaches what InDesign's own object search reaches:
//  parent pages, the pasteboard, groups, inline and anchored items, a table's graphic cells, overset text, hidden and locked
//  layers, hidden spreads (measured in KT - docs/ai-notes/kfc-qr-tab-measure-2026-10-27.md M8, sixteen QR codes in sixteen
//  places).
//  Read only. The main thread only.
//
//========================================================================================

#ifndef __KFCQRCodes_h__
#define __KFCQRCodes_h__

#include "PMString.h"
#include "UIDRef.h"

#include <vector>

namespace KFCQRCodes
{
	/** The five kinds of QR code InDesign makes (Object > Generate QR Code), in its dialog's order. */
	enum Kind { kKindNone = -1, kKindWeb = 0, kKindText, kKindSMS, kKindEmail, kKindVCard };

	/** A kind's word on a row: "Web", "Text", "SMS", "Email", "vCard" - "" for kKindNone. */
	const char* KindWord(Kind kind);

	/** THE QR CODE A FRAME HOLDS (or the EPS item itself): true with its kind and its content as ONE line - the kind's own
	    fields (the design's Q14 / D6: InDesign's hover text is the raw format with line breaks for an Email and a business
	    card): Web = the URL; Text = the text; SMS = "<number>: <message>"; Email = "<address>  <subject>"; business card =
	    "<first> <last>" (InDesign's own FN order) and " / <organisation>" when there is one. A line break is a space.
	    false = not a QR code (an item no longer there, no IID_IQRCODEDATAUID, or its data of no kind known here); out
	    values as they were. */
	bool Read(const UIDRef& frameOrItem, Kind& outKind, PMString& outContent);

	/** EVERY QR CODE OF A DOCUMENT, as its frame's UID, in the walk's order (parent spreads first, then the spreads in
	    order, a spread's pasteboard with it). false = the document could not be walked (no walker); outFrames is cleared
	    first either way. */
	bool CollectFrames(const UIDRef& docRef, std::vector<UID>& outFrames);

#ifdef KFC_DIAG
	/** TEST BUILDS ONLY - the fault switch qr-probe (plan Task 2, regression case qr-probe-kinds): the active document's QR
	    codes read into the trace, one line each - QRPROBE <frame's UID> <kind> <content> - and outSummary
	    "QR probe: <n>". Searches nothing. Returns 0. */
	int32 DiagProbe(PMString& outSummary);
#endif
}

#endif // __KFCQRCodes_h__

// End, KFCQRCodes.h.
