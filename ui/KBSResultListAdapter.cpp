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
//  Under a document, a STORY node per group (2026-09-27), each holding that story's hits - the code calls
//  the level FONT, the name it had when it held the fonts of Find Missing Glyphs (removed the same day).
//  Every chapter's hits are grouped; the branches below that hang hits off the document directly are the
//  safe answer for a node that names no group, not a shape any list has now.
//
//  ***** A RUN LEVEL between the document and its stories (2026-09-29, Show Changes by KohakuFindChange). *****
//  A list rebuilt from the Track Changes records groups a document's rows by the replace that wrote them
//  first: document -> run -> story -> row. Decided per chapter again, from its own runs (none on every
//  other list).
//
//  See KBSResultNodeID.h for the six node shapes and for why the root sits at -2. Ported from
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
#include "KFCUIID.h"
#include "KBSResultNodeID.h"
#include "KBSModelAccess.h"		// the model half, through its session interfaces (2026-10-01, the model/UI split)

/** The hierarchy over KBSResultModel: hidden root -> the BOOK row when the results came from a book
    -> one document node per chapter with hits -> (a RUN node per replace, on a list rebuilt from the
    records) -> a STORY ("font") node per group -> one hit node per match. Without a book the document
    nodes hang off the root itself. */
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
			// A hit hangs off its story ("font") row - and off the document row only when it names no
			// group, which no list has since 2026-09-27.
			const int32 font = nodeID->GetFont();
			if (font >= 0)
				return KBSResultNodeID::CreateFont(nodeID->GetChapter(), font);
			return KBSResultNodeID::Create(nodeID->GetChapter());
		}
		if (nodeID->IsFontRow())
		{
			// a story row -> its run row when the list has runs, its document row when it has not
			if (nodeID->GetRun() >= 0)
				return KBSResultNodeID::CreateRun(nodeID->GetChapter(), nodeID->GetRun());
			return KBSResultNodeID::Create(nodeID->GetChapter());
		}
		if (nodeID->IsRunRow())
			return KBSResultNodeID::Create(nodeID->GetChapter());	// run -> its document row
		if (nodeID->IsBookRow())
			return KBSResultNodeID::CreateRoot();
		// A document row hangs off the book row when the results came from a book, and off the root
		// when they came from a single document - which is the two-level tree KBS has always had.
		return KBSResults()->IsFromBook() ? KBSResultNodeID::CreateBook() : KBSResultNodeID::CreateRoot();
	}

	virtual int32 GetNumChildren(const NodeID& node) const
	{
		TreeNodePtr<KBSResultNodeID> nodeID(node);
		if (nodeID == nil || nodeID->IsHitRow())
			return 0;	// hit rows are the leaves
		if (nodeID->IsRoot())
			return KBSResults()->IsFromBook() ? 1 : KBSResults()->GetDisplayChapterCount();
		if (nodeID->IsBookRow())
			return KBSResults()->GetDisplayChapterCount();
		if (nodeID->IsFontRow())
			return KBSResults()->GetDisplayFontHitCount(nodeID->GetChapter(), nodeID->GetFont());
		if (nodeID->IsRunRow())
			return KBSResults()->GetDisplayRunGroupCount(nodeID->GetChapter(), nodeID->GetRun());

		// A document row: its RUN rows when the list has runs (2026-09-29); else its story ("font") rows
		// (its hits directly only for a chapter with no groups - see the head of the file).
		const int32 runs = KBSResults()->GetDisplayRunCount(nodeID->GetChapter());
		if (runs > 0)
			return runs;
		const int32 fonts = KBSResults()->GetDisplayFontCount(nodeID->GetChapter());
		if (fonts > 0)
			return fonts;
		return KBSResults()->GetDisplayHitCount(nodeID->GetChapter());
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
			if (KBSResults()->IsFromBook())
				return (nth == 0) ? KBSResultNodeID::CreateBook() : kInvalidNodeID;
			// The nth SHOWN chapter, which is chapter nth unless a closed document's chapter was emptied in
			// place before it (2026-09-29, All Documents - KBSResultModel::CloseChapter).
			const int32 chapter = KBSResults()->GetShownChapter(nth);
			return (chapter >= 0) ? KBSResultNodeID::Create(chapter) : kInvalidNodeID;
		}
		if (nodeID->IsBookRow())
		{
			const int32 chapter = KBSResults()->GetShownChapter(nth);
			return (chapter >= 0) ? KBSResultNodeID::Create(chapter) : kInvalidNodeID;
		}
		if (nodeID->IsFontRow())
		{
			// The group hands back a CHAPTER-wide hit index - which is what a node names.
			const int32 hit = KBSResults()->GetFontGroupHit(nodeID->GetChapter(), nodeID->GetFont(), nth);
			if (hit < 0)
				return kInvalidNodeID;
			return KBSResultNodeID::Create(nodeID->GetChapter(), hit);
		}
		if (nodeID->IsRunRow())
		{
			// The run hands back a CHAPTER-wide group index; the cap wipes out a run's LAST groups, so the
			// nth displayed one is the nth.
			if (nth < 0 || nth >= KBSResults()->GetDisplayRunGroupCount(nodeID->GetChapter(), nodeID->GetRun()))
				return kInvalidNodeID;
			const int32 group = KBSResults()->GetRunGroup(nodeID->GetChapter(), nodeID->GetRun(), nth);
			if (group < 0)
				return kInvalidNodeID;
			return KBSResultNodeID::CreateFont(nodeID->GetChapter(), group);
		}

		// A document row. The groups the display cap wipes out are the LAST ones (they are in
		// first-appearance order and the cap keeps a prefix of the chapter's hits), so the nth
		// displayed group is simply the nth group - and the same holds for the runs.
		const int32 runs = KBSResults()->GetDisplayRunCount(nodeID->GetChapter());
		if (runs > 0)
		{
			if (nth < 0 || nth >= runs)
				return kInvalidNodeID;
			return KBSResultNodeID::CreateRun(nodeID->GetChapter(), nth);
		}
		const int32 fonts = KBSResults()->GetDisplayFontCount(nodeID->GetChapter());
		if (fonts > 0)
		{
			if (nth < 0 || nth >= fonts)
				return kInvalidNodeID;
			return KBSResultNodeID::CreateFont(nodeID->GetChapter(), nth);
		}
		if (nth < 0 || nth >= KBSResults()->GetDisplayHitCount(nodeID->GetChapter()))
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
			// Its place under its story ("font") row (its place in the chapter for a hit with no group).
			const int32 pos = KBSResults()->GetHitFontGroupPos(childID->GetChapter(), childID->GetHit());
			return (pos >= 0) ? pos : childID->GetHit();
		}
		if (childID->IsFontRow())
		{
			// under a run row: its place among the run's groups; under a document row: the group itself
			if (childID->GetRun() >= 0)
				return KBSResults()->GetGroupPosInRun(childID->GetChapter(), childID->GetFont());
			return childID->GetFont();
		}
		if (childID->IsRunRow())
			return childID->GetRun();
		if (childID->IsBookRow())
			return 0;		// the root's only child
		return KBSResults()->GetShownChapterPos(childID->GetChapter());	// GetNthChild's reverse
	}

	virtual NodeID_rv GetGenericNodeID() const
	{
		return KBSResultNodeID::Create();
	}

	virtual bool16 ShouldAddNthChild(const NodeID& node, const int32& nth) const { return kTrue; }
};

CREATE_PMINTERFACE(KBSResultListAdapter, kKBSResultListAdapterImpl)

// End, KBSResultListAdapter.cpp.
