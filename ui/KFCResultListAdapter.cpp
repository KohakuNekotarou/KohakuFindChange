//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  ITreeViewHierarchyAdapter for the result tree: adapts KFCResultModel's chapters and hits to
//  the tree-view framework. Under a hidden root: one BOOK node when the results came from a book
//  search - it is then the root's ONLY child and the chapters hang off it - then one DOCUMENT node
//  per chapter shown (in book order - see the class's note). A document-scope search has no book row, so its
//  chapters hang off the root directly, which is the two-level tree KFC started with.
//
//  Under a document, a STORY node per group, each holding that story's hits - the code calls the level
//  FONT, the name it had when it held the fonts of the Find Missing Glyphs scan (since removed).
//  A text search's hits are all grouped; an OBJECT search's rows have no story and hang off their document row
//  directly - the branches below for a node that names no group (KFCResultModel's BuildFontGroups).
//
//  WHILE THE RESULTS ARE KEPT IN TIERS (KFCResultHistory - docs/superpowers/specs/2026-10-14-kfc-stacked-results-design.md
//  section 3-3) the root's children are the TIERS: the kept ones, oldest first, each over its copy of the rows it had, and
//  the live one last, over the list above - its book row, or its documents. The live list's own shape is the same either
//  way, kept in LiveRowCount / LiveRow below, which a tier's copy walks as well.
//
//  See KFCResultNodeID.h for the node shapes and for why the root sits at -2. Ported from
//  KESCL's KESCLResultListAdapter, dropping its filtered-view indirection (KFC has no filters) - itself
//  modelled on paneltreeview's adapter.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITreeViewHierarchyAdapter.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KFCUIID.h"
#include "KFCResultNodeID.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces
#include "KFCResultHistory.h"	// the tiers kept above the live list
#include "KFCResultTree.h"		// LiveRowCount / LiveRow - defined below

namespace
{
// THE LIVE LIST'S TOP: the hidden root while the list is drawn as it always was, the live tier's row while it is drawn
// in tiers - where the book row, or the documents of a list with none, hang.
NodeID_rv LiveTop()
{
	if (KFCResultHistory::IsStacking())
		return KFCResultNodeID::CreateLiveTier();
	return KFCResultNodeID::CreateRoot();
}
}	// anonymous namespace

/** The hierarchy over KFCResultModel: hidden root -> the BOOK row when the results came from a book
    -> one document node per chapter shown (with hits, or searched again with none - Chapter::shownEmpty)
    -> a STORY ("font") node per group -> one hit node per match. Without a book the document
    nodes hang off the root itself. While the results are kept in tiers, the tiers' rows come between the root and all of
    that (the file comment). */
class KFCResultListAdapter : public CPMUnknown<ITreeViewHierarchyAdapter>
{
public:
	KFCResultListAdapter(IPMUnknown* boss) : CPMUnknown<ITreeViewHierarchyAdapter>(boss) {}
	virtual ~KFCResultListAdapter() {}

	virtual NodeID_rv GetRootNode() const
	{
		return KFCResultNodeID::CreateRoot();
	}

	virtual NodeID_rv GetParentNode(const NodeID& node) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsRoot())
			return kInvalidNodeID;	// the root has no parent
		// A TIER'S row hangs off the root; a KEPT row off the row above it in its tier's copy - or the tier's own row.
		if (nodeID->IsTierRow())
			return KFCResultNodeID::CreateRoot();
		if (nodeID->IsKeptRow())
		{
			const int32 up = KFCResultHistory::KeptParent(nodeID->GetTier(), nodeID->GetKeptRow());
			if (up >= 0)
				return KFCResultNodeID::CreateKept(nodeID->GetTier(), up);
			return KFCResultNodeID::CreateTier(nodeID->GetTier());
		}
		if (nodeID->IsHitRow())
		{
			// A hit hangs off its story ("font") row - and off the document row when it names no group: an
			// object row, which has no story (KFCResultModel's BuildFontGroups).
			const int32 font = nodeID->GetFont();
			if (font >= 0)
				return KFCResultNodeID::CreateFont(nodeID->GetChapter(), font);
			return KFCResultNodeID::Create(nodeID->GetChapter());
		}
		if (nodeID->IsFontRow())
			return KFCResultNodeID::Create(nodeID->GetChapter());	// a story row -> its document row
		if (nodeID->IsBookRow())
			return LiveTop();
		// A document row hangs off the book row when the results came from a book, and off the list's top
		// when they came from a single document - which is the two-level tree KFC has always had.
		if (KFCResults()->IsFromBook())
			return KFCResultNodeID::CreateBook();
		return LiveTop();
	}

	virtual int32 GetNumChildren(const NodeID& node) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return 0;	// hit rows are the leaves
		// THE ROOT WHILE THE RESULTS ARE IN TIERS: the kept tiers, then the live one - while there is a live list.
		if (nodeID->IsRoot() && KFCResultHistory::IsStacking())
			return KFCResultHistory::KeptTierCount() + (KFCResultHistory::HasLiveList() ? 1 : 0);
		if (nodeID->IsTierRow() && nodeID->GetTier() >= 0)
			return KFCResultHistory::KeptChildCount(nodeID->GetTier(), -1);
		if (nodeID->IsKeptRow())
			return KFCResultHistory::KeptChildCount(nodeID->GetTier(), nodeID->GetKeptRow());
		return KFCResultTree::LiveRowCount(node);
	}

	virtual NodeID_rv GetNthChild(const NodeID& node, const int32& nth) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return kInvalidNodeID;
		if (nodeID->IsRoot() && KFCResultHistory::IsStacking())
		{
			// IN SEARCH ORDER (v2 R4 - the tiers do not move): the kept tiers as the model keeps them, the live tier at its
			// place among them.
			const int32 kept = KFCResultHistory::KeptTierCount();
			int32 tier = nth;
			if (KFCResultHistory::HasLiveList())
			{
				const int32 live = KFCResultHistory::LiveTierPosition();
				if (nth == live)
					return KFCResultNodeID::CreateLiveTier();
				if (nth > live)
					tier = nth - 1;
			}
			if (tier >= 0 && tier < kept)
				return KFCResultNodeID::CreateTier(tier);
			return kInvalidNodeID;
		}
		if (nodeID->IsTierRow() && nodeID->GetTier() >= 0)
		{
			const int32 row = KFCResultHistory::KeptChild(nodeID->GetTier(), -1, nth);
			if (row < 0)
				return kInvalidNodeID;
			return KFCResultNodeID::CreateKept(nodeID->GetTier(), row);
		}
		if (nodeID->IsKeptRow())
		{
			const int32 row = KFCResultHistory::KeptChild(nodeID->GetTier(), nodeID->GetKeptRow(), nth);
			if (row < 0)
				return kInvalidNodeID;
			return KFCResultNodeID::CreateKept(nodeID->GetTier(), row);
		}
		return KFCResultTree::LiveRow(node, nth);
	}

	virtual int32 GetChildIndex(const NodeID& parent, const NodeID& child) const
	{
		TreeNodePtr<KFCResultNodeID> childID(child);
		if (childID == nil || childID->IsRoot())
			return -1;
		// A tier's row: its place among the tiers - in search order, the live one among them (GetNthChild's reverse).
		if (childID->IsTierRow())
		{
			const int32 tier = childID->GetTier();
			if (!KFCResultHistory::HasLiveList())
				return tier;
			const int32 live = KFCResultHistory::LiveTierPosition();
			if (tier < 0)
				return live;
			return (tier < live) ? tier : tier + 1;
		}
		if (childID->IsKeptRow())
			return KFCResultHistory::KeptChildPos(childID->GetTier(), childID->GetKeptRow());
		if (childID->IsHitRow())
		{
			// Its place under its story ("font") row (its place in the chapter for a hit with no group).
			const int32 pos = KFCResults()->GetHitFontGroupPos(childID->GetChapter(), childID->GetHit());
			return (pos >= 0) ? pos : childID->GetHit();
		}
		if (childID->IsFontRow())
			return childID->GetFont();		// under its document row: the group itself
		if (childID->IsBookRow())
			return 0;		// the only child of the list's top
		return KFCResults()->GetShownChapterPos(childID->GetChapter());	// GetNthChild's reverse
	}

	virtual NodeID_rv GetGenericNodeID() const
	{
		return KFCResultNodeID::Create();
	}

	virtual bool16 ShouldAddNthChild(const NodeID& node, const int32& nth) const { return kTrue; }
};

CREATE_PMINTERFACE(KFCResultListAdapter, kKFCResultListAdapterImpl)

int32 KFCResultTree::LiveRowCount(const NodeID& node)
{
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	// Hit rows are the leaves; a kept tier's rows are KFCResultHistory's.
	if (nodeID == nil || nodeID->IsHitRow() || nodeID->IsKeptRow() || (nodeID->IsTierRow() && nodeID->GetTier() >= 0))
		return 0;
	// The list's top - the root, or the live tier's row: the book row alone while the results came from a book, the
	// documents shown otherwise.
	if (nodeID->IsRoot() || nodeID->IsTierRow())
		return KFCResults()->IsFromBook() ? 1 : KFCResults()->GetDisplayChapterCount();
	if (nodeID->IsBookRow())
		return KFCResults()->GetDisplayChapterCount();
	if (nodeID->IsFontRow())
		return KFCResults()->GetDisplayFontHitCount(nodeID->GetChapter(), nodeID->GetFont());

	// A document row: its story ("font") rows - its hits directly for a chapter with no groups, an object search's
	// (see GetParentNode).
	const int32 fonts = KFCResults()->GetDisplayFontCount(nodeID->GetChapter());
	if (fonts > 0)
		return fonts;
	return KFCResults()->GetDisplayHitCount(nodeID->GetChapter());
}

NodeID_rv KFCResultTree::LiveRow(const NodeID& node, int32 nth)
{
	TreeNodePtr<KFCResultNodeID> nodeID(node);
	if (nodeID == nil || nodeID->IsHitRow() || nodeID->IsKeptRow() || (nodeID->IsTierRow() && nodeID->GetTier() >= 0))
		return kInvalidNodeID;
	if (nodeID->IsRoot() || nodeID->IsTierRow())
	{
		// The book row is the top's only child while the results came from a book. Without one
		// the documents hang off the top directly, exactly as they always have.
		if (KFCResults()->IsFromBook())
			return (nth == 0) ? KFCResultNodeID::CreateBook() : kInvalidNodeID;
		// The nth SHOWN chapter, which is chapter nth unless a closed document's chapter was emptied in
		// place before it (All Documents - KFCResultModel::CloseChapter).
		const int32 chapter = KFCResults()->GetShownChapter(nth);
		return (chapter >= 0) ? KFCResultNodeID::Create(chapter) : kInvalidNodeID;
	}
	if (nodeID->IsBookRow())
	{
		const int32 chapter = KFCResults()->GetShownChapter(nth);
		return (chapter >= 0) ? KFCResultNodeID::Create(chapter) : kInvalidNodeID;
	}
	if (nodeID->IsFontRow())
	{
		// The group hands back a CHAPTER-wide hit index - which is what a node names.
		const int32 hit = KFCResults()->GetFontGroupHit(nodeID->GetChapter(), nodeID->GetFont(), nth);
		if (hit < 0)
			return kInvalidNodeID;
		return KFCResultNodeID::Create(nodeID->GetChapter(), hit);
	}

	// A document row. Every story group shows (GetDisplayFontCount), so the nth displayed group
	// is simply the nth group.
	const int32 fonts = KFCResults()->GetDisplayFontCount(nodeID->GetChapter());
	if (fonts > 0)
	{
		if (nth < 0 || nth >= fonts)
			return kInvalidNodeID;
		return KFCResultNodeID::CreateFont(nodeID->GetChapter(), nth);
	}
	if (nth < 0 || nth >= KFCResults()->GetDisplayHitCount(nodeID->GetChapter()))
		return kInvalidNodeID;
	return KFCResultNodeID::Create(nodeID->GetChapter(), nth);
}

// End, KFCResultListAdapter.cpp.
