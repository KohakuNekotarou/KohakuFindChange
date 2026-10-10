//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  ITreeViewHierarchyAdapter for the result tree: adapts KFCResultModel's chapters and hits to
//  the tree-view framework. Under a hidden root: one BOOK node when the results came from a book
//  search - it is then the root's ONLY child and the chapters hang off it - then one DOCUMENT node
//  per chapter that holds matches (in book order). A document-scope search has no book row, so its
//  chapters hang off the root directly, which is the two-level tree KFC started with.
//
//  Under a document, a STORY node per group, each holding that story's hits - the code calls the level
//  FONT, the name it had when it held the fonts of the Find Missing Glyphs scan (since removed).
//  Every chapter's hits are grouped; the branches below that hang hits off the document directly are the
//  safe answer for a node that names no group, not a shape any list has now.
//
//  See KFCResultNodeID.h for the five node shapes and for why the root sits at -2. Ported from
//  KESCL's KESCLResultListAdapter, dropping its filtered-view indirection (KFC shows every chapter
//  that has hits, no filters) - itself modelled on paneltreeview's adapter.
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

/** The hierarchy over KFCResultModel: hidden root -> the BOOK row when the results came from a book
    -> one document node per chapter shown (with hits, or searched again with none - Chapter::shownEmpty)
    -> a STORY ("font") node per group -> one hit node per match. Without a book the document
    nodes hang off the root itself. */
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
		if (nodeID->IsHitRow())
		{
			// A hit hangs off its story ("font") row - and off the document row when it names no group: an
			// object row (1.4.0), which has no story (KFCResultModel's BuildFontGroups).
			const int32 font = nodeID->GetFont();
			if (font >= 0)
				return KFCResultNodeID::CreateFont(nodeID->GetChapter(), font);
			return KFCResultNodeID::Create(nodeID->GetChapter());
		}
		if (nodeID->IsFontRow())
			return KFCResultNodeID::Create(nodeID->GetChapter());	// a story row -> its document row
		if (nodeID->IsBookRow())
			return KFCResultNodeID::CreateRoot();
		// A document row hangs off the book row when the results came from a book, and off the root
		// when they came from a single document - which is the two-level tree KFC has always had.
		return KFCResults()->IsFromBook() ? KFCResultNodeID::CreateBook() : KFCResultNodeID::CreateRoot();
	}

	virtual int32 GetNumChildren(const NodeID& node) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return 0;	// hit rows are the leaves
		if (nodeID->IsRoot())
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

	virtual NodeID_rv GetNthChild(const NodeID& node, const int32& nth) const
	{
		TreeNodePtr<KFCResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return kInvalidNodeID;
		if (nodeID->IsRoot())
		{
			// The book row is the root's only child while the results came from a book. Without one
			// the documents hang off the root directly, exactly as they always have.
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

		// A document row. The groups the display cap wipes out are the LAST ones (they are in
		// first-appearance order and the cap keeps a prefix of the chapter's hits), so the nth
		// displayed group is simply the nth group.
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

	virtual int32 GetChildIndex(const NodeID& parent, const NodeID& child) const
	{
		TreeNodePtr<KFCResultNodeID> childID(child);
		if (childID == nil || childID->IsRoot())
			return -1;
		if (childID->IsHitRow())
		{
			// Its place under its story ("font") row (its place in the chapter for a hit with no group).
			const int32 pos = KFCResults()->GetHitFontGroupPos(childID->GetChapter(), childID->GetHit());
			return (pos >= 0) ? pos : childID->GetHit();
		}
		if (childID->IsFontRow())
			return childID->GetFont();		// under its document row: the group itself
		if (childID->IsBookRow())
			return 0;		// the root's only child
		return KFCResults()->GetShownChapterPos(childID->GetChapter());	// GetNthChild's reverse
	}

	virtual NodeID_rv GetGenericNodeID() const
	{
		return KFCResultNodeID::Create();
	}

	virtual bool16 ShouldAddNthChild(const NodeID& node, const int32& nth) const { return kTrue; }
};

CREATE_PMINTERFACE(KFCResultListAdapter, kKFCResultListAdapterImpl)

// End, KFCResultListAdapter.cpp.
