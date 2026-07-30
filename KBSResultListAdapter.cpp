//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ITreeViewHierarchyAdapter for the result tree: adapts KBSResultModel's chapters and hits to
//  the tree-view framework. Under a hidden root: one BOOK node when the results came from a book
//  search - it is then the root's ONLY child and the chapters hang off it - then one DOCUMENT node
//  per chapter that holds matches (in book order), each with one HIT node per match. A
//  document-scope search has no book row, so its chapters hang off the root directly, which is the
//  two-level tree KBS started with. See KBSResultNodeID.h for the four node shapes and for why the
//  root sits at -2. Ported from KESCL's KESCLResultListAdapter, dropping its filtered-view
//  indirection (KBS shows every chapter that has hits, no filters) - itself modelled on
//  paneltreeview's adapter.
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

/** The hierarchy over KBSResultModel: hidden root (-2, -1) -> the BOOK row (-1, -1) when the results
    came from a book -> one document node per chapter with hits -> one hit node per match. Without a
    book the document nodes hang off the root itself. */
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
			return KBSResultNodeID::Create(nodeID->GetChapter());	// hit -> its document row
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
			return childID->GetHit();
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
