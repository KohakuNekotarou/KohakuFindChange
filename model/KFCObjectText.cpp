//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  SEARCH TEXT IN THESE OBJECTS - THE TEXT. See KFCObjectText.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IFrameList.h"
#include "IGraphicFrameData.h"
#include "IHierarchy.h"
#include "IMainItemTOPData.h"		// the text on an item's path
#include "IMultiColumnTextFrame.h"
#include "ITextModel.h"
#include "ITextUtils.h"			// CollectOwnedItems - the frames anchored in a frame's text

// General includes:
#include "OwnedItemDataList.h"
#include "PersistUtils.h"			// ::GetUID
#include "TextID.h"				// kInlineBoss - an inline or anchored frame's anchor
#include "Utils.h"

#include <algorithm>				// std::sort - the stretches put in story order

// Project includes:
#include "KFCObjectText.h"
#include "KFCNarrow.h"				// FrameOwner - the item a row's match is drawn in
#include "KFCPageItemFacts.h"		// ItemIsThere - an object deleted since the object search
#include "KFCSearchEngine.h"		// FrameAt - the frame an anchored frame's anchor is drawn in

namespace
{
// ONE FRAME'S OWN TEXT AS A STRETCH (KFCObjectText::Collect): SnpManipulateTextFrame's form (see the header) - measured
// to walk as InDesign's own Search: Selection over the frame's text selected (the spec's M1) and to read right in a
// windowless document (M2). The overset after it when its last column is its frame list's last: the multi-column item's
// last child (measured to agree with IMultiColumnTextFrame::GetUIDInFrameList, which is Adobe's internal use only -
// M3), the story's last character left out as the frame's is. `item` = the item whose frame it is. True = a stretch was
// taken.
bool FrameText(IMultiColumnTextFrame* mcf, UID item, KFCObjectText::Ranges& out)
{
	InterfacePtr<IFrameList> frameList(mcf->QueryFrameList());
	InterfacePtr<ITextModel> model(mcf->QueryTextModel());
	if (frameList == nil || model == nil)
		return false;
	if (frameList->GetFirstDamagedFrameIndex() != -1)
		mcf->RecomposeThru();
	const TextIndex start = mcf->TextStart();
	const int32 span = mcf->TextSpan();
	const int32 primary = model->GetPrimaryStoryThreadSpan();
	const UID story = ::GetUID(model);
	out.frames.insert(item);
	bool taken = false;
	TextIndex end = start + span;
	if (end >= primary)
		end = primary - 1;
	if (end > start)
	{
		out.stretches.push_back(KFCObjectText::Stretch{ story, start, end, false });
		taken = true;
	}
	InterfacePtr<IHierarchy> columns(mcf, UseDefaultIID());
	const int32 count = (columns != nil) ? columns->GetChildCount() : 0;
	const bool last = count > 0
		&& frameList->GetFrameIndex(columns->GetChildUID(count - 1)) == frameList->GetFrameCount() - 1;
	if (last && start + span < primary - 1)
	{
		out.stretches.push_back(KFCObjectText::Stretch{ story, start + span, primary - 1, true });
		out.oversetStories.insert(story);
		taken = true;
	}
	return taken;
}

// How deep frames anchored in frames anchored in a found frame are followed.
const int32 kMaxAnchorDepth = 8;

// AN ITEM'S TEXT (KFCObjectText::Collect - the spec's T3): a text frame's own text (FrameText); the text on its path - the
// path's own multi-column frame (IMainItemTOPData), a row of it drawn in the path itself (KFCNarrow::FrameOwner); and the
// frames anchored in the text taken - inline and anchored alike (ITextUtils::CollectOwnedItems, nested: those in its tables'
// cells and its footnotes too - measured, the spec's M4) - by the same rules, each one ANCHORED IN THIS ITEM'S FRAME: its
// anchor drawn there (KFCSearchEngine::FrameAt - a table row or a footnote drawn in the next frame takes its anchored frames
// with it, as the frame check takes its matches). True = some text was taken.
bool ItemText(IDataBase* db, UID item, KFCObjectText::Ranges& out, int32 depth)
{
	bool taken = false;
	const size_t from = out.stretches.size();
	InterfacePtr<IGraphicFrameData> frame(db, item, UseDefaultIID());
	const UID content = (frame != nil) ? frame->GetTextContentUID() : kInvalidUID;
	if (content != kInvalidUID)
	{
		InterfacePtr<IMultiColumnTextFrame> mcf(db, content, UseDefaultIID());
		if (mcf != nil && FrameText(mcf, item, out))
			taken = true;
	}
	InterfacePtr<IMainItemTOPData> onPath(db, item, UseDefaultIID());
	if (onPath != nil && onPath->GetTOPSplineItemUID() != kInvalidUID)
	{
		InterfacePtr<IMultiColumnTextFrame> pathFrame(onPath->QueryTOPMCTextFrame());
		if (pathFrame != nil && FrameText(pathFrame, item, out))
			taken = true;
	}
	if (depth >= kMaxAnchorDepth)
		return taken;
	const size_t to = out.stretches.size();
	for (size_t s = from; s < to; ++s)
	{
		const KFCObjectText::Stretch one = out.stretches[s];		// a copy: the frames anchored in it add stretches
		if (one.overset)
			continue;		// overset text draws nothing anchored in it
		InterfacePtr<ITextModel> model(db, one.story, UseDefaultIID());
		if (model == nil)
			continue;
		OwnedItemDataList owned;
		Utils<ITextUtils>()->CollectOwnedItems(model, one.start, one.end - one.start, &owned, kTrue);
		for (size_t o = 0; o < owned.size(); ++o)
		{
			if (owned[o].fClassID != kInlineBoss)
				continue;
			InterfacePtr<IHierarchy> anchor(db, owned[o].fUID, UseDefaultIID());
			if (anchor == nil || anchor->GetChildCount() < 1)
				continue;
			const UID drawnIn = KFCNarrow::FrameOwner(db, KFCSearchEngine::FrameAt(UIDRef(db, one.story), owned[o].fAt));
			if (drawnIn != item)
				continue;
			if (ItemText(db, anchor->GetChildUID(0), out, depth + 1))
				taken = true;
		}
	}
	return taken;
}
}	// anonymous namespace

void KFCObjectText::Collect(IDataBase* db, const std::vector<UID>& items, Ranges& out)
{
	out = Ranges();
	if (db == nil)
		return;
	for (size_t i = 0; i < items.size(); ++i)
	{
		const UID item = items[i];
		if (!KFCPageItemFacts::ItemIsThere(db, item))
		{
			++out.gone;
			continue;
		}
		++out.objects;
		if (!ItemText(db, item, out, 0))
			++out.noText;
	}
	// JOINED: each story's stretches in order, one where they touch or overlap - two found frames threaded one after the
	// other - so no match is counted twice, and a match running from one found frame into the next is found (the spec's T3;
	// M1: the two joined find it, each alone does not, as InDesign's Selection over one does not).
	std::sort(out.stretches.begin(), out.stretches.end(), [](const Stretch& a, const Stretch& b)
		{ return (a.story != b.story) ? (a.story.Get() < b.story.Get()) : (a.start < b.start); });
	std::vector<Stretch> joined;
	for (size_t i = 0; i < out.stretches.size(); ++i)
	{
		const Stretch& s = out.stretches[i];
		if (!joined.empty() && joined.back().story == s.story && s.start <= joined.back().end)
		{
			if (s.end > joined.back().end)
				joined.back().end = s.end;
			joined.back().overset = joined.back().overset || s.overset;
		}
		else
			joined.push_back(s);
	}
	out.stretches.swap(joined);
}

bool KFCObjectText::Keeps(const Ranges& r, IDataBase* db, const KFCResultModel::Hit& hit)
{
	// NO FRAME = overset text (FrameUIDForPosition's kInvalidUID) - not Hit::isOverset, which a match whose frame sits on no
	// page carries too.
	if (hit.matchFrame == kInvalidUID)
		return r.oversetStories.count(hit.storyUID) != 0;
	return r.frames.count(KFCNarrow::FrameOwner(db, hit.matchFrame)) != 0;
}

// End, KFCObjectText.cpp.
