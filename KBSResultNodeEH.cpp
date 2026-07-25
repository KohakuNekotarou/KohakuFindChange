//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Tree row event handler (Task 3): a click on a HIT row jumps to that occurrence. Replaces
//  IID_IEVENTHANDLER on the result tree's node boss (kKBSResultNodeWidgetBoss). Derives from the
//  stock TreeNodeEventHandler so ordinary tree behaviour (select, expand/collapse, drag) is kept;
//  only LButtonDn is extended - after the base does its selection, a hit row fires the jump. Every
//  click jumps (fresh or a re-click on the already-selected row); chapter rows just select /
//  expand. Simplified from KESCL (which split fresh clicks onto a selection observer).
//
//  The row's "replace me" check box is a real widget of its own (kKBSResultCheckWidgetBoss) that
//  swallows its own clicks, so ticking a hit never arrives here and never jumps. Its observer is
//  KBSResultCheckObserver.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITreeNodeIDData.h"	// this node's NodeID

// General includes:
#include "TreeNodeEventHandler.h"	// stock base (source/open/includes/widgets; on the CPP.rsp path)

// Project includes:
#include "KBSID.h"
#include "KBSResultNodeID.h"
#include "KBSJump.h"

class KBSResultNodeEH : public TreeNodeEventHandler
{
public:
	KBSResultNodeEH(IPMUnknown* boss) : TreeNodeEventHandler(boss) {}
	virtual ~KBSResultNodeEH() {}

	virtual bool16 LButtonDn(IEvent* e);
};

CREATE_PMINTERFACE(KBSResultNodeEH, kKBSResultNodeEHImpl)

bool16 KBSResultNodeEH::LButtonDn(IEvent* e)
{
	// Let the stock handler select the row / start a drag first.
	const bool16 result = TreeNodeEventHandler::LButtonDn(e);

	// Then, for a hit row, jump to that occurrence. The node's NodeID lives on this boss's
	// ITreeNodeIDData (every TreeNode widget carries it).
	InterfacePtr<ITreeNodeIDData> nodeData(this, UseDefaultIID());
	if (nodeData != nil)
	{
		TreeNodePtr<KBSResultNodeID> nodeID(nodeData->Get());
		if (nodeID != nil && nodeID->IsHitRow())
			KBSJump::JumpToHit(nodeID->GetChapter(), nodeID->GetHit());
	}
	return result;
}

// End, KBSResultNodeEH.cpp.
