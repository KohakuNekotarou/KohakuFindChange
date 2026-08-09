//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ITreeViewHierarchyAdapter for the result tree: adapts KBSResultModel's chapters and hits to
//  the tree-view framework. Under a hidden root: one BOOK node when the results came from a book
//  search - it is then the root's ONLY child and the chapters hang off it - then one DOCUMENT node
//  per chapter that holds matches (in book order). A document-scope search has no book row, so its
//  chapters hang off the root directly, which is the two-level tree KBS started with.
//
//  Under a document, one of two things (2026-08-02):
//    * a FONT node per group when this chapter's hits name fonts - a missing-glyph scan, where the
//      font is what the finding is ABOUT - each holding that font's hits;
//    * the hits themselves when they do not, which is every Find/Change result.
//  The choice is made per CHAPTER, from the chapter's own groups, so the two can never disagree.
//
//  See KBSResultNodeID.h for the five node shapes and for why the root sits at -2. Ported from
//  KESCL's KESCLResultListAdapter, dropping its filtered-view indirection (KBS shows every chapter
//  that has hits, no filters) - itself modelled on paneltreeview's adapter.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITreeViewHierarchyAdapter.h"

// General includes:
#include "CPMUnknown.h"

// Project includes:
#include "KBSID.h"
#include "KBSResultNodeID.h"
#include "KBSResultModel.h"

/** The hierarchy over KBSResultModel: hidden root -> the BOOK row when the results came from a book
    -> one document node per chapter with hits -> a FONT node per group when the chapter has groups
    -> one hit node per match. Without a book the document nodes hang off the root itself; without
    groups the hit nodes hang off their document. */
class KBSResultListAdapter : public CPMUnknown<ITreeViewHierarchyAdapter>
{
public:
	KBSResultListAdapter(IPMUnknown* boss) : CPMUnknown<ITreeViewHierarchyAdapter>(boss) {}
	virtual ~KBSResultListAdapter() {}

	virtual NodeID_rv GetRootNode() const
	{
		return KBSResultNodeID::CreateRoot();
	}

	virtual NodeID_rv GetParentNode(const NodeID& node) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsRoot())
			return kInvalidNodeID;	// the root has no parent
		if (nodeID->IsHitRow())
		{
			// A hit hangs off its FONT row when its chapter has groups, and off the document row
			// itself when it has not - which is the tree a Find/Change result has always had.
			const int32 font = nodeID->GetFont();
			if (font >= 0)
				return KBSResultNodeID::CreateFont(nodeID->GetChapter(), font);
			return KBSResultNodeID::Create(nodeID->GetChapter());
		}
		if (nodeID->IsFontRow())
			return KBSResultNodeID::Create(nodeID->GetChapter());	// font -> its document row
		if (nodeID->IsBookRow())
			return KBSResultNodeID::CreateRoot();
		// A document row hangs off the book row when the results came from a book, and off the root
		// when they came from a single document - which is the two-level tree KBS has always had.
		return KBSResultModel::IsFromBook() ? KBSResultNodeID::CreateBook() : KBSResultNodeID::CreateRoot();
	}

	virtual int32 GetNumChildren(const NodeID& node) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return 0;	// hit rows are the leaves
		if (nodeID->IsRoot())
			return KBSResultModel::IsFromBook() ? 1 : KBSResultModel::GetDisplayChapterCount();
		if (nodeID->IsBookRow())
			return KBSResultModel::GetDisplayChapterCount();
		if (nodeID->IsFontRow())
			return KBSResultModel::GetDisplayFontHitCount(nodeID->GetChapter(), nodeID->GetFont());

		// A document row: its FONT rows when this chapter's hits name fonts, its hits directly when
		// they do not.
		const int32 fonts = KBSResultModel::GetDisplayFontCount(nodeID->GetChapter());
		if (fonts > 0)
			return fonts;
		return KBSResultModel::GetDisplayHitCount(nodeID->GetChapter());
	}

	virtual NodeID_rv GetNthChild(const NodeID& node, const int32& nth) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return kInvalidNodeID;
		if (nodeID->IsRoot())
		{
			// The book row is the root's only child while the results came from a book. Without one
			// the documents hang off the root directly, exactly as they always have.
			if (KBSResultModel::IsFromBook())
				return (nth == 0) ? KBSResultNodeID::CreateBook() : kInvalidNodeID;
			if (nth < 0 || nth >= KBSResultModel::GetDisplayChapterCount())
				return kInvalidNodeID;
			return KBSResultNodeID::Create(nth);
		}
		if (nodeID->IsBookRow())
		{
			if (nth < 0 || nth >= KBSResultModel::GetDisplayChapterCount())
				return kInvalidNodeID;
			return KBSResultNodeID::Create(nth);
		}
		if (nodeID->IsFontRow())
		{
			// The group hands back a CHAPTER-wide hit index - which is what a node names.
			const int32 hit = KBSResultModel::GetFontGroupHit(nodeID->GetChapter(), nodeID->GetFont(), nth);
			if (hit < 0)
				return kInvalidNodeID;
			return KBSResultNodeID::Create(nodeID->GetChapter(), hit);
		}

		// A document row. The groups the display cap wipes out are the LAST ones (they are in
		// first-appearance order and the cap keeps a prefix of the chapter's hits), so the nth
		// displayed group is simply the nth group.
		const int32 fonts = KBSResultModel::GetDisplayFontCount(nodeID->GetChapter());
		if (fonts > 0)
		{
			if (nth < 0 || nth >= fonts)
				return kInvalidNodeID;
			return KBSResultNodeID::CreateFont(nodeID->GetChapter(), nth);
		}
		if (nth < 0 || nth >= KBSResultModel::GetDisplayHitCount(nodeID->GetChapter()))
			return kInvalidNodeID;
		return KBSResultNodeID::Create(nodeID->GetChapter(), nth);
	}

	virtual int32 GetChildIndex(const NodeID& parent, const NodeID& child) const
	{
		TreeNodePtr<KBSResultNodeID> childID(child);
		if (childID == nil || childID->IsRoot())
			return -1;
		if (childID->IsHitRow())
		{
			// Its place under its FONT row when it has one, and its place in the chapter when it
			// does not.
			const int32 pos = KBSResultModel::GetHitFontGroupPos(childID->GetChapter(), childID->GetHit());
			return (pos >= 0) ? pos : childID->GetHit();
		}
		if (childID->IsFontRow())
			return childID->GetFont();
		if (childID->IsBookRow())
			return 0;		// the root's only child
		return childID->GetChapter();
	}

	virtual NodeID_rv GetGenericNodeID() const
	{
		return KBSResultNodeID::Create();
	}

	virtual bool16 ShouldAddNthChild(const NodeID& node, const int32& nth) const { return kTrue; }
};

CREATE_PMINTERFACE(KBSResultListAdapter, kKBSResultListAdapterImpl)

// End, KBSResultListAdapter.cpp.
