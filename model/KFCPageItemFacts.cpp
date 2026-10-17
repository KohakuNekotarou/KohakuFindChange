//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCPageItemFacts.h. (The bodies were KFCSearchEngine.cpp's - their history before the move is in its log.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"
#include "IDataBase.h"
#include "IDocumentLayer.h"
#include "IHierarchy.h"
#include "IItemLockData.h"
#include "ILayerList.h"				// FindByName - a document's layer of a name (DocLayerNamed)
#include "ILayerUtils.h"
#include "ILayoutUtils.h"
#include "ILockPosition.h"
#include "IOwnedItem.h"				// GetFrameRef - the frame an inline's anchor is composed into (AnchorLayerUID)
#include "IPageItemVisibilityFacade.h"
#include "IPageList.h"
#include "ISpreadLayer.h"
#include "ITextModel.h"
#include "ITextStoryThread.h"
#include "ITextUtils.h"

// General includes:
#include "InCopySharedID.h"
#include "PersistUtils.h"
#include "TextChar.h"
#include "TextID.h"					// kInlineBoss
#include "UnicodeClass.h"
#include "Utils.h"
#include "WideString.h"

// Project includes:
#include "KFCDiag.h"			// the fault switch inline-layer-off (AnchorLayerUID)
#include "KFCPageItemFacts.h"
#include "KFCResultModel.h"
#include "KFCTextCut.h"			// KFCIsAttachedMark - a mark goes with its character (StoryLeadText)

namespace KFCPageItemFacts
{

// A frame UID -> its page, named the way the Pages panel names it (section prefix and all). Shared
// by the visible-match path (the match's own frame) and the overset path (the "+" indicator's
// frame). false only when neither a page nor a spread can be resolved. outPageIndex is the page's
// plain document order (for sorting); the STRING follows the section, so it can read "iv" or "A-1",
// and "PB" for a frame sitting on the pasteboard.
bool GetFramePageString(const UIDRef& docRef, UID frameUID, PMString& outPage, int32& outPageIndex)
{
	outPage.Clear();
	outPage.SetTranslatable(kFalse);
	outPageIndex = -1;
	if (frameUID == kInvalidUID)
		return false;

	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;

	// ITextUtils::GetPageUIDRef is the purpose-built lookup - its contract is "the page the given
	// textFrame is on" - and everything that reaches this function IS a text frame, since it comes
	// from IParcelList::GetParcelFrameUID.
	UID pageUID = Utils<ITextUtils>()->GetPageUIDRef(UIDRef(db, frameUID)).GetUID();

	// Fall back to the general page-item lookup, which answers for anything in the hierarchy.
	//
	// That fallback is also what keeps a PASTEBOARD hit readable, and it is deliberate: a frame that
	// sits on no page makes GetOwnerPageUID return the SPREAD's UID, and GetPageString takes a
	// spread UID too and spells it "PB" (IPageList.h:131, "Given a page UID (or spread UID)"). A
	// match out on the pasteboard should say where it is rather than drop out of the list, so this
	// does NOT check that the UID is a kPageBoss before using it. (Code that wants real pages only
	// has to verify with db->GetClass - see KCM's KCMFramePageUID (KCMCore.cpp), which does exactly that.)
	if (pageUID == kInvalidUID)
	{
		InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
		if (frameHier == nil)
			return false;
		pageUID = Utils<ILayoutUtils>()->GetOwnerPageUID(frameHier);
	}
	if (pageUID == kInvalidUID)
		return false;

	InterfacePtr<IPageList> pageList(docRef, UseDefaultIID());
	if (pageList == nil)
		return false;

	// bUseIntegerStyle = kFalse, so the page is spelled the way the Pages panel spells it - in the
	// style its SECTION uses (iv, A-1) - instead of being forced to arabic numerals. The default is
	// kTrue (IPageList.h:136: "kTrue == Use arabic numerals in string; kFalse == use the style of
	// this section (eg iv for 4)"), so taking the default printed "4" for a page the panel calls
	// "iv". Sorting is unaffected: that uses outPageIndex, which is the plain document order.
	pageList->GetPageString(pageUID, &outPage, kTrue /*bIncludeSectionName*/, kFalse /*bUseIntegerStyle*/);
	outPage.SetTranslatable(kFalse);
	outPageIndex = pageList->GetPageIndex(pageUID);
	return !outPage.IsEmpty();
}

// Is this frame switched off? Such a match is composed and has a page like any other - only its
// drawing is suppressed - so it can be listed and jumped to; the row just has to say so, the way the
// Find/Change dialog says "Hidden Item".
//
// TWO independent switches, and both have to be asked:
//   * its LAYER is hidden. The layer an item sits on is a SPREAD layer (one per spread); the
//     visibility switch lives on the DOCUMENT layer it points at, which is the row in the Layers
//     panel.
//   * the ITEM ITSELF is hidden (Object > Hide), which no layer says anything about.
//
// Without the second, a match inside an individually hidden frame comes up with no "hidden" mark at
// all. Adobe asks both: spellpanel's DetermineIfTextIsHidden does the layer
// test and then IPageItemVisibilityFacade::IsHidden, and states in its own comment that it is the
// same code as FindChangeClient.cpp - i.e. this is what the Find/Change dialog itself reports.
//
// THE LAYER OF AN ITEM PLACED IN TEXT - an inline or anchored frame, and the text frame column inside one - IS ITS
// ANCHOR'S. The guide (sol-02, "Acquiring a reference to a spread layer"): an inline frame's hierarchy (kInlineBoss)
// answers kInvalidUID for its layer - find the page item in the layout hierarchy first. Found here by calls already
// measured: up the item's hierarchy to the kInlineBoss it hangs from, whose owned item (IOwnedItem - kInlineBoss
// carries it) names the frame column its anchor is composed into (GetFrameRef) - the frame it is drawn with, whose
// layer this is; out again while that frame is itself in text. kInvalidUID when the item does not hang in text, or
// its anchor stands in overset text (composed into no frame) - as for any item on no layer. (Not the guide's own
// IPageItemUtils::QueryInlineParentPageItem: no caller in the SDK, and a utility asked about an item of a kind it was
// not written for has brought InDesign down before - memory facade-not-safe-for-every-item.)
UID AnchorLayerUID(IDataBase* db, UID item)
{
	const int32 kClimbAtMost = 16;		// steps up one hierarchy, and inlines inside inlines - far more than layouts use
	UID at = item;
	for (int32 nest = 0; nest < kClimbAtMost && at != kInvalidUID; ++nest)
	{
		UID inlineUID = kInvalidUID;
		UID up = at;
		for (int32 step = 0; step < kClimbAtMost && up != kInvalidUID && db->IsValidUID(up); ++step)
		{
			if (db->GetClass(up) == kInlineBoss)
			{
				inlineUID = up;
				break;
			}
			InterfacePtr<IHierarchy> h(db, up, UseDefaultIID());
			up = (h != nil) ? h->GetParentUID() : kInvalidUID;
		}
		if (inlineUID == kInvalidUID)
			return kInvalidUID;		// not in text
		InterfacePtr<IOwnedItem> owned(db, inlineUID, UseDefaultIID());
		const UIDRef anchorFrame = (owned != nil) ? owned->GetFrameRef() : UIDRef::gNull;
		if (anchorFrame.GetDataBase() != db || anchorFrame.GetUID() == kInvalidUID)
			return kInvalidUID;		// the anchor stands in overset text
		InterfacePtr<IHierarchy> anchorHier(anchorFrame, UseDefaultIID());
		const UID layer = (anchorHier != nil) ? Utils<ILayerUtils>()->GetLayerUID(anchorHier) : kInvalidUID;
		if (layer != kInvalidUID)
			return layer;
		at = anchorFrame.GetUID();	// that frame hangs in text too: out again
	}
	return kInvalidUID;
}

// The layer is found the one way both tests below need it: the frame's SPREAD layer (one per
// spread) points at the DOCUMENT layer that carries the switches - the Layers panel's row. nil when
// any step cannot be resolved.
IDocumentLayer* QueryDocLayerOf(IDataBase* db, IHierarchy* frameHier)
{
	UID spreadLayerUID = Utils<ILayerUtils>()->GetLayerUID(frameHier);
	// (Fault switch inline-layer-off, a test build's only - KFCDiag.h: an item in text reads as on no layer, the old way.)
	if (spreadLayerUID == kInvalidUID && !KFC_DIAG_FAULT("inline-layer-off"))
		spreadLayerUID = AnchorLayerUID(db, ::GetUID(frameHier));
	if (spreadLayerUID == kInvalidUID)
		return nil;
	InterfacePtr<ISpreadLayer> spreadLayer(db, spreadLayerUID, UseDefaultIID());
	if (spreadLayer == nil)
		return nil;
	InterfacePtr<IDocumentLayer> docLayer(db, spreadLayer->GetDocLayerUID(), UseDefaultIID());
	return docLayer.forget();
}

bool IsFrameHidden(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;

	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, frameHier));
	if (docLayer != nil && !docLayer->IsVisible())
		return true;

	// The item's own switch. Asked second because it is the rarer of the two, and asked even when the
	// layer could not be resolved - "cannot tell about the layer" is not an answer about the item.
	return Utils<Facade::IPageItemVisibilityFacade>()->IsHidden(UIDRef(db, frameUID)) != kFalse;
}

// Is this frame on a LOCKED layer? Same two-step as the hidden test above (spread layer -> the
// document layer that carries the switch), asked so the replace can leave such a match alone.
// A frame that resolves to no layer reads as unlocked - see IsFrameEditable in KFCSearchEngine.h on why
// "cannot tell" must not turn into a refusal.
bool IsFrameOnLockedLayer(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<IHierarchy> frameHier(db, frameUID, UseDefaultIID());
	if (frameHier == nil)
		return false;

	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, frameHier));
	return docLayer != nil && docLayer->IsLocked();
}

// Do this page item's own lock flags refuse an edit? A page item can carry TWO, independently
// (kSplineItemBoss holds both, per a live object-model dump):
//
//   ILockPosition::IsPageItemLocked - Object > Lock (Ctrl+L), the one users reach for. selecting =
//       kFalse asks for the lock itself, not "would a click be refused"; the Prevent Selecting
//       Locked Items preference has no bearing on whether text may be rewritten.
//   IItemLockData::GetInsertLock    - "the content cannot be edited", the insert lock InCopy sets
//       on a managed frame. ILockPosition folds this in for managed frames (ILockPosition.h:53-56)
//       but it is asked outright as well, so the answer does not depend on that folding.
//
// EACH IS ASKED AT TWO LEVELS. The UID this gets is IParcelList::GetParcelFrameUID - the item the
// text is composed INTO, which is not the page item the lock lives on. Asking it alone found
// nothing at all (measured on the running application: locking a text frame left its hits fully
// selectable, while locking the LAYER worked). Nothing had depended on the distinction before,
// because the two existing users of this UID - GetOwnerPageUID and ILayerUtils::GetLayerUID - both
// climb the hierarchy themselves. Adobe climbs for lock interfaces too (CGraphicPlaceBehavior uses
// QueryOutermostParentFor with IID_IITEMLOCKDATA). Self first, then the outermost ancestor, so a
// frame locked on its own and a frame inside a locked GROUP both answer.
bool IsPageItemLockedForEdit(IDataBase* db, UID frameUID)
{
	if (db == nil || frameUID == kInvalidUID)
		return false;

	InterfacePtr<ILockPosition> lockPos(db, frameUID, UseDefaultIID());
	if (lockPos != nil && lockPos->IsPageItemLocked(kFalse))
		return true;
	InterfacePtr<IItemLockData> lockData(db, frameUID, UseDefaultIID());
	if (lockData != nil && lockData->GetInsertLock())
		return true;

	InterfacePtr<IHierarchy> hier(db, frameUID, UseDefaultIID());
	if (hier == nil)
		return false;

	// Two separate climbs: QueryOutermostParentFor finds the outermost ancestor supporting THAT
	// interface, and the two need not land on the same item.
	InterfacePtr<ILockPosition> outerLockPos(static_cast<ILockPosition*>(
		Utils<ILayoutUtils>()->QueryOutermostParentFor(hier, IID_ILOCKPOSITION)));
	if (outerLockPos != nil && outerLockPos->IsPageItemLocked(kFalse))
		return true;

	InterfacePtr<IItemLockData> outerLockData(static_cast<IItemLockData*>(
		Utils<ILayoutUtils>()->QueryOutermostParentFor(hier, IID_IITEMLOCKDATA)));
	if (outerLockData != nil && outerLockData->GetInsertLock())
		return true;

	return false;
}

// The first words of a story, for its row in the tree: up to 24 characters
// that show, each run of white space read as one space, InDesign's own marker characters (a table's
// anchor, a footnote's reference, an anchored object, a zero-width mark...) left out, and the cut mark
// when the story goes on. Read from the first 200 characters, which is plenty for 24 that show.
// A mark that belongs to the character before it (a combining voiced mark, an accent) goes with that
// character and is not one of the 24, so the cut never falls between the two (the rule in KFCTextCut.h).
//
// IN THE OFFICIAL TERMS. Read through
// IComposeScanner::CopyText, like every other read in this file (and codesnippets/
// SnpCreateCrossReference.cpp, which names stories from their text the same way). A marker is what
// InDesign itself counts as neither white space nor a character, UnicodeClass::IsIgnoredCharacter
// with the spell checker's set (kIgnoreSpellingIgnorable: zero-width marks, discretionary hyphens,
// page numbers and other computed text, table characters, inline graphics, special glyphs, variation
// selectors), plus the rest of the control range and the object placeholder. A gap is one of the
// three breaks InDesign keeps in the control range, or UnicodeClass::IsWhiteSpace (which KCM's story
// list, KCMStoryList.cpp, asks too) - with the two corrections the loop below names. The cut is
// kTextChar_Ellipse, as a hit row's is. (Not a hand-made table of code points: one dropped the whole
// private-use area, so a story opening with gaiji lost them from its row - the regression case's row
// read "a-b-c" with the gaiji gone.)
//
// THE BREAKS ARE KEPT (the author: "show the paragraph mark on the story rows, and the forced line
// break too" - the marks KCM's story list draws). A paragraph's end (CR) and a
// forced line break (LF) stay in the text as the characters they are, and the tree draws them as the
// pilcrow and the return arrow a hit row uses (KFCResultModel::MarkUpBreaksForDisplay, applied where the
// row's text is built). Each counts as one of the 24. Only a break BETWEEN visible characters is kept:
// the story's own last CR, and the empty paragraphs at its end, would only say "the story ends here" -
// which a hit row does not say either (its last paragraph draws no pilcrow) - and the breaks at its
// HEAD (an empty first paragraph, or the one a table or an anchored object stands in) would put a
// pilcrow before the first word, where the row's name is read; leading white space was never shown
// either. White space next to a break is not shown: the break is the separator, and what follows it is
// an indent.
//
// THE STORY'S BODY - ITS PRIMARY THREAD - AND NOTHING ELSE (the author: "the story row shows the text
// with the tracked changes in it - the text from before the change"). A text model holds more than the
// body: the text Track Changes keeps for a deletion (a thread of its own, kDeletedTextBoss), a table's
// cells, footnotes, notes - every thread after the body's last CR. Read from the whole text model, a
// short body is followed on its row by whatever comes next: "kittenkitten dog catcat" named a story
// whose page reads "kittenkitten dog" (the "catcat" was the deletion a replace left).
// ITextModel::GetPrimaryStoryThreadSpan is the body's length (ITextModel.h: "does not include any
// characters that are part of story threads for table cells").
// Only when the body has no WORDS - a frame holding a table and nothing else - are the other threads
// read, in order, and then never a deletion's (so the row still names the story by its cells: "<sign>
// cell text", as KCM's story list reads it). A table's anchor shows as its sign wherever it stands.
PMString StoryLeadText(const UIDRef& storyRef)
{
	PMString out;
	out.SetTranslatable(kFalse);
	InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (model == nil || scanner == nil)
		return out;
	const int32 kShownAtMost = 24;		// what a story row shows (ROW-09)
	const int32 kReadAtMost = 200;		// plenty for 24 that show
	WideString lead;
	WideString pendingBreaks;	// breaks read since the last visible character - kept once one follows
	int32 shown = 0;			// what the row shows - the 24
	int32 words = 0;			// of those, the ones that are text: a table's sign is not (see below)
	bool pendingSpace = false;
	bool more = false;
	auto scan = [&](const WideString& raw)
	{
		for (int32 i = 0; i < raw.CharCount() && !more; ++i)
		{
			const UTF32TextChar c = raw.GetChar(i);
			const uint32 v = c.GetValue();
			// A TABLE'S ANCHOR IS SHOWN, AS ITS SIGN (the author: "like KCM, a table mark").
			// Kept as the character itself - the row's text is marked up where it is built, and
			// KFCResultModel::MarkUpBreaksForDisplay turns it into U+25A6, as it turns the breaks into their
			// marks. It is one of the 24 but not a WORD (KCM's rule, KCMStoryList.cpp): a body holding a
			// table and nothing else still goes on to the cells below. Its per-row continuations
			// (kTextChar_TableContinued) are dropped with the other control characters: one table, one sign.
			const bool isTableSign = (v == kTextChar_Table);
			// IN THIS ORDER, AND WITH TWO NAMED CHARACTERS - BOTH MEASURED (the story-lead-chars
			// regression case). The breaks first: IsIgnoredCharacter counts CR / LF
			// as markers too. Then the markers, BEFORE white space, because IsWhiteSpace answered TRUE for
			// the zero-width space (U+200B), which then showed as a space between two letters. Then white
			// space - with the ideographic space named, because IsWhiteSpace answered FALSE for it and a
			// Japanese paragraph's indent came through as a character at the head of the row.
			if (v == kTextChar_CR || v == kTextChar_LF)
			{
				if (shown > 0)
					pendingBreaks.Append(c);	// (before the first visible character: not shown - see above)
				pendingSpace = false;		// white space before a break is not shown
				continue;
			}
			if (v == kTextChar_Tab)
			{
				pendingSpace = (shown > 0 && pendingBreaks.CharCount() == 0);
				continue;
			}
			// The marks every row leaves out (KFCResultModel::IsMarkerNotShown - an index marker among them), and
			// for a story row's first words all control characters and IsIgnoredCharacter's (variation selectors, special
			// glyphs), which a hit row keeps.
			if (!isTableSign && (v < kTextChar_Space || KFCResultModel::IsMarkerNotShown(c)
				|| UnicodeClass::IsIgnoredCharacter(c, UnicodeClass::kIgnoreSpellingIgnorable)))
				continue;
			if (UnicodeClass::IsWhiteSpace(c) || v == kTextChar_IdeographicSpace)
			{
				pendingSpace = (shown > 0 && pendingBreaks.CharCount() == 0);	// after a break: an indent
				continue;
			}
			// A MARK THAT BELONGS TO THE CHARACTER JUST SHOWN goes on with it, uncounted (KFCIsAttachedMark): the 24
			// counts what shows, and a cut between the two would draw the bare character - a different glyph. Only
			// the combining marks get here: IsIgnoredCharacter's set drops the variation selectors above, by design
			// (kIgnoreUnicodeVariation), and keeps the combining marks (measured against Public.dll outside
			// InDesign: yes to the five selectors tried, IVS among them; no to the seven marks, U+3099 among them).
			if (shown > 0 && pendingBreaks.CharCount() == 0 && !pendingSpace && KFCIsAttachedMark(c))
			{
				lead.Append(c);
				continue;
			}
			// A visible character: the breaks before it go in first, each one of the 24 - and when the 24 are full, the
			// character (with any break still waiting) does not fit: the row goes on past them.
			for (int32 b = 0; b < pendingBreaks.CharCount() && shown < kShownAtMost; ++b)
			{
				lead.Append(pendingBreaks.GetChar(b));
				++shown;
			}
			pendingBreaks.Clear();
			if (shown >= kShownAtMost)
			{
				more = true;
				break;
			}
			if (pendingSpace)
			{
				lead.Append(UTF32TextChar(kTextChar_Space));
				pendingSpace = false;
			}
			lead.Append(c);
			++shown;
			if (!isTableSign)
				++words;
		}
	};
	// Where one thread ends and the next is read: its last break is the thread's own end, not a paragraph
	// mark anybody typed (the story's end, a cell's end) - so it is never shown, and the next thread's words
	// are set off by a gap, the one a tab leaves (KCM reads `<sign> c` the same way).
	auto endThread = [&]()
	{
		pendingBreaks.Clear();
		pendingSpace = (shown > 0);
	};

	// The body first.
	const int32 total = model->TotalLength();
	int32 body = model->GetPrimaryStoryThreadSpan();
	if (body > total)
		body = total;
	WideString raw;
	if (body > 0)
		scanner->CopyText(0, (body < kReadAtMost) ? body : kReadAtMost, &raw);
	scan(raw);
	endThread();

	// No words in it (a table alone - then its sign leads the row - or an anchored object alone): the
	// threads after it, in text order - a deletion's never (what the page does not show must not name the
	// story).
	int32 budget = kReadAtMost;
	for (TextIndex at = body; words == 0 && !more && at < total && budget > 0; )
	{
		TextIndex threadStart = kInvalidTextIndex;
		int32 threadLen = 0;
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at, &threadStart, &threadLen));
		if (thread == nil || threadStart < 0 || threadLen <= 0 || threadStart + threadLen <= at)
			break;		// no thread to go on with, or one that would not move the walk on
		if (::GetClass(thread) != kDeletedTextBoss)
		{
			const int32 len = (threadLen < budget) ? threadLen : budget;
			WideString part;
			scanner->CopyText(threadStart, len, &part);
			scan(part);
			endThread();
			budget -= len;
		}
		at = threadStart + threadLen;
	}

	out = PMString(lead);
	if (more)
		out.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	out.SetTranslatable(kFalse);
	return out;
}

int32 StackIndex(IDataBase* db, UID item)
{
	// Its parent's own list of children, in which the order IS the stacking order (IHierarchy.h - GetChildIndex): what
	// Object > Arrange moves.
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return -1;
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	if (hier == nil)
		return -1;
	InterfacePtr<IHierarchy> parent(hier->QueryParent());
	return (parent != nil) ? parent->GetChildIndex(hier) : -1;
}

UID DocLayerOf(IDataBase* db, UID item)
{
	if (db == nil || item == kInvalidUID || !db->IsValidUID(item))
		return kInvalidUID;
	InterfacePtr<IHierarchy> hier(db, item, UseDefaultIID());
	if (hier == nil)
		return kInvalidUID;
	InterfacePtr<IDocumentLayer> docLayer(QueryDocLayerOf(db, hier));
	return (docLayer != nil) ? ::GetUID(docLayer) : kInvalidUID;
}

UID DocLayerNamed(const UIDRef& docRef, const PMString& name)
{
	if (name.IsEmpty() || docRef.GetDataBase() == nil)
		return kInvalidUID;
	InterfacePtr<ILayerList> layers(docRef, UseDefaultIID());
	return (layers != nil) ? layers->FindByName(name) : kInvalidUID;
}


}	// namespace KFCPageItemFacts

// End, KFCPageItemFacts.cpp.
