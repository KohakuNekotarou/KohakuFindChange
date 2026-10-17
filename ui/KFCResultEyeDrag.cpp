//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE ROW EYE TRACED (the author, 2026-10-17 night: "the eye on and off - by dragging, like the Layers panel"): a press on
//  a written row's eye that moves becomes a drag (KFCResultEyeEH), and every eye it passes over turns as the first one
//  does - on where the first was off, off where it was on; an eye already so is left as it is.
//  THE LAYERS PANEL'S OWN SHAPE (open/components/layerpanel: LayerEyeDragDropSource, LayerEyeDragDropTarget,
//  LayerEyeDEHandler, and kLayerEyeDEHandlerBoss in LayerPanelClass.fr): the eye is the drag's source and every eye its
//  target; the drag carries a flavor of the panel's own, through a data exchange handler holding the first eye's state;
//  and a target acts AS THE MOUSE ENTERS IT, not on the drop - "so user sees feedback while mouse is still down".
//  ONE DIFFERENCE, CHOSEN: the Layers panel turns the first row and the row entered; this turns every row BETWEEN them, in
//  the order the list draws them - as the Buttons panel's state eyes do (buttonui/appearancepanel/
//  StateEyeDragDropTarget.cpp, "based on LayerEyeDragDropTarget"): a mouse moved quickly passes rows without entering
//  them, and a trace that misses rows is not a trace. The rows between are the rows DRAWN - a closed document's are not
//  (TreeNodeTraverser with expandedNodesOnly, as Shift+arrow walks them - KFCResultTreeController's SelectRun).
//  ! THE EYE IS KFC'S OWN STATE, not the document's (the row kept when searched again - KFCResultModel::SetHitKept, a field
//    set and nothing more, so no row is rebuilt under the drag): a turn is no command, and Edit > Undo does not undo it,
//    as it does not undo a click's.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IBoolData.h"				// the handler's: the first eye's state, as the Layers panel's handler holds its layer's
#include "IControlView.h"
#include "IDataExchangeHandler.h"
#include "IDragDropController.h"
#include "IPanelControlData.h"		// a row's eye, found in its row widget
#include "IPMDataObject.h"			// AddDragItem - PromiseFlavor
#include "ISession.h"
#include "ITreeViewHierarchyAdapter.h"
#include "ITreeViewMgr.h"
#include "IWidgetParent.h"

// General includes:
#include "AppUIID.h"				// kAppUIPluginID - the pointing hand, the Layers panel's drag cursor
#include "CDataExchangeHandlerFor.h"
#include "CDragDropSource.h"
#include "CDragDropTarget.h"
#include "CTreeViewMgr.h"			// AboveOrBelow - which way the rows between go
#include "CursorDefs.h"				// kCrsrPointyHand
#include "CursorSpec.h"
#include "DataObjectIterator.h"
#include "PMFlavorTypes.h"			// RegisterExternalFlavor - a flavor of KFC's own
#include "TreeNodeTraverser.h"		// the rows in the order they are drawn

#include <vector>

// Project includes:
#include "KFCUIID.h"
#include "KFCModelAccess.h"			// KFCResults() - the rows' state
#include "KFCResultEye.h"
#include "KFCResultNodeID.h"
#include "KFCResultTree.h"			// ShowStatus, TakeKeyboard

namespace
{

/** THE DRAG'S FLAVOR. On Windows a PMFlavor is a clipboard format, registered by name - "Use this to register flavor
	formats (for clipboard and drag drop operations)" (PMFlavorTypes.h, RegisterExternalFlavor): a name of KFC's own, so
	only this list's eyes accept a trace, and nothing else in InDesign takes one as a drop. (The SDK's basicdragdrop
	names its flavor with four characters - PMFlavor('AOL1') - which the 16-bit format cuts to two.) kNoFlavor when the
	name could not be registered: then nothing drags, and a press is a click. */
PMFlavor EyeFlavor()
{
	static const PMFlavor flavor = RegisterExternalFlavor(const_cast<TCHAR*>(TEXT("Kohaku Find/Change row eye")));
	return flavor;
}

/** THE TRACE UNDER WAY: where it began, the first eye's state, and what it has turned. The row is held here and not in the
	handler - a row is two numbers (its chapter and hit), and the handler's data interfaces hold one (the Layers panel's
	holds its layer's UIDRef); the handler holds the first eye's state, as the Layers panel's does. One trace at a time:
	StartDrag returns only when it is over (IDragDropController.h), and DragEnded lets it go. Main thread only. */
struct Trace
{
	bool		fActive;
	int32		fChapter;
	int32		fHit;
	bool		fWasKept;		///< the first eye's state - every eye passed over that shows it is turned
	int32		fTurned;		///< eyes turned so far
	PMString	fLast;			///< the last one turned's locator - the status line names it
	Trace() : fActive(false), fChapter(-1), fHit(-1), fWasKept(false), fTurned(0) {}
};
Trace gTrace;

/** The list an eye is drawn in - its tree's manager, nil when it is not in one. */
ITreeViewMgr* QueryTreeOf(const IPMUnknown* eye)
{
	InterfacePtr<const IWidgetParent> parent(eye, UseDefaultIID());
	return (parent != nil) ? static_cast<ITreeViewMgr*>(parent->QueryParentFor(ITreeViewMgr::kDefaultIID)) : nil;
}

/** The eye of a row, nil when the row is not drawn now (scrolled out of the list - drawn right when it comes back:
	KFCResultListWidgetMgr reads the row's state). */
IControlView* EyeOf(const ITreeViewMgr* treeMgr, const NodeID& row)
{
	InterfacePtr<IControlView> rowView(treeMgr->QueryWidgetFromNode(row));
	InterfacePtr<IPanelControlData> rowData(rowView, UseDefaultIID());
	return (rowData != nil) ? rowData->FindWidget(kKFCResultEyeWidgetID) : nil;
}

/** One row of a trace: turned when it is a written live hit row whose eye shows what the first one showed. */
void TurnRow(const ITreeViewMgr* treeMgr, const NodeID& row)
{
	TreeNodePtr<KFCResultNodeID> id(row);
	if (id == nil || !id->IsHitRow())
		return;		// a story, document, book or tier row - or a kept tier's
	KFCResultModel::RowDisplay display;
	if (!KFCResults()->GetHitRow(id->GetChapter(), id->GetHit(), display) || !KFCResultEye::IsWritten(display))
		return;		// a row KFC has not written has no eye to turn
	if (display.kept != gTrace.fWasKept)
		return;		// turned already - by this trace, or as the first one was turned to
	if (!KFCResults()->SetHitKept(id->GetChapter(), id->GetHit(), !gTrace.fWasKept))
		return;
	KFCResultEye::Draw(EyeOf(treeMgr, row), !gTrace.fWasKept, true /*now: the button is still down*/);
	++gTrace.fTurned;
	gTrace.fLast = display.locator;
}

/** The rows from the trace's first to `here`, both counted, in the order the list draws them. Only the first and `here`
	when the walk does not meet `here` (a row of another tier: AboveOrBelow sends the walk one way, and a walk that missed
	its end would run on to the list's end) - the Layers panel's two. */
std::vector<NodeID> RowsTo(ITreeViewMgr* treeMgr, const NodeID& here)
{
	std::vector<NodeID> rows;
	const NodeID start(KFCResultNodeID::Create(gTrace.fChapter, gTrace.fHit));
	rows.push_back(start);
	if (here == start)
		return rows;
	InterfacePtr<ITreeViewHierarchyAdapter> adapter(treeMgr, UseDefaultIID());
	if (adapter != nil)
	{
		const int32 aboveOrBelow = CTreeViewMgr::AboveOrBelow(adapter, start, here);
		TreeNodeTraverser walk(start, treeMgr, kTrue /*expandedNodesOnly*/,
			aboveOrBelow < 0 ? TreeNodeTraverser::eForward : TreeNodeTraverser::eReverse);
		std::vector<NodeID> between;
		NodeID next;
		while (!walk.Completed())
		{
			next = walk.Next();
			if (!next.IsValid())
				continue;
			between.push_back(next);
			if (next == here)
			{
				rows.insert(rows.end(), between.begin(), between.end());
				return rows;
			}
		}
	}
	rows.push_back(here);
	return rows;
}

/** The status line while tracing and after: the last eye turned, as a click names it - or how many, once there are more. */
void SayTurned()
{
	PMString words;
	if (gTrace.fTurned == 1)
		words = gTrace.fLast;
	else
	{
		words.AppendNumber(gTrace.fTurned);
		words.Append(" rows:");
	}
	words.SetTranslatable(kFalse);
	words.Append(gTrace.fWasKept ? "  not kept when searching again" : "  kept when searching again");
	KFCResultTree::ShowStatus(words);
}

}	// anonymous namespace

//========================================================================================
// The drag's data exchange handler - kKFCResultEyeDEHandlerBoss (LayerEyeDEHandler)
//========================================================================================

class KFCResultEyeDEHandler : public CDataExchangeHandlerFor
{
public:
	KFCResultEyeDEHandler(IPMUnknown* boss) : CDataExchangeHandlerFor(boss) {}
	virtual ~KFCResultEyeDEHandler() {}

	virtual PMFlavor GetFlavor() const { return EyeFlavor(); }
};

CREATE_PMINTERFACE(KFCResultEyeDEHandler, kKFCResultEyeDEHandlerImpl)

//========================================================================================
// The eye as the drag's source (LayerEyeDragDropSource)
//========================================================================================

class KFCResultEyeDragSource : public CDragDropSource
{
public:
	KFCResultEyeDragSource(IPMUnknown* boss) : CDragDropSource(boss) {}
	virtual ~KFCResultEyeDragSource() {}

	/** A written live hit row's eye only - any other eye's press is the row's (KFCResultEyeEH). */
	virtual bool16 WillDrag(IEvent* e) const;
	/** No outline for an eye (the Layers panel's). */
	virtual SysWireframe DoMakeDragOutlineRegion() const { return nil; }
	/** The first eye's state into the handler, the trace begun, the flavor promised. */
	virtual bool16 DoAddDragContent(IDragDropController* controller);
	/** The pointing hand, the Layers panel's. */
	virtual CursorSpec GetDragCursor(DragDrop::eTargetResponse, bool16 isDragLocal) const;
};

CREATE_PMINTERFACE(KFCResultEyeDragSource, kKFCResultEyeDragSourceImpl)

bool16 KFCResultEyeDragSource::WillDrag(IEvent* /*e*/) const
{
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	return (EyeFlavor() != kNoFlavor && KFCResultEye::WrittenRow(this, chapter, hit, row)) ? kTrue : kFalse;
}

bool16 KFCResultEyeDragSource::DoAddDragContent(IDragDropController* controller)
{
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	if (controller == nil || !KFCResultEye::WrittenRow(this, chapter, hit, row))
		return kFalse;
	// A handler for the flavor, or nothing promised (LayerEyeDragDropSource).
	InterfacePtr<IDataExchangeHandler> handler(controller->QueryHandler(EyeFlavor()));
	if (handler == nil)
		return kFalse;
	InterfacePtr<IBoolData> wasKept(handler, IID_IBOOLDATA);
	if (wasKept != nil)
		wasKept->Set(row.kept);
	gTrace = Trace();
	gTrace.fActive  = true;
	gTrace.fChapter = chapter;
	gTrace.fHit     = hit;
	gTrace.fWasKept = row.kept;
	controller->SetSourceHandler(handler);
	// One item, one flavor - "the internal flavor and external flavor are the same" (LayerEyeDragDropSource).
	InterfacePtr<IPMDataObject> item(controller->AddDragItem(1));
	if (item == nil)
		return kFalse;
	const PMFlavorFlags flavorFlags = 0;
	item->PromiseFlavor(EyeFlavor(), flavorFlags);
	return kTrue;
}

CursorSpec KFCResultEyeDragSource::GetDragCursor(DragDrop::eTargetResponse, bool16 /*isDragLocal*/) const
{
	return CursorSpec(kAppUIPluginID, kCrsrPointyHand);
}

//========================================================================================
// Every eye as the drag's target (LayerEyeDragDropTarget)
//========================================================================================

class KFCResultEyeDropTarget : public CDragDropTarget
{
public:
	KFCResultEyeDropTarget(IPMUnknown* boss) : CDragDropTarget(boss) {}
	virtual ~KFCResultEyeDropTarget() {}

	/** A trace only - KFC's flavor - and always a copy "in the drag and drop sense" (the Layers panel's). */
	virtual DragDrop::TargetResponse CouldAcceptTypes(DataObjectIterator* dataIter, const IDragDropSource*,
													 const IDragDropController*) const;
	/** The turn, AS THE MOUSE ENTERS (the Layers panel's: "for the eyeball though, we send the command as soon as the mouse
		enters the control"). */
	virtual void DoDragEnter();
	/** No drop outline for an eye. */
	virtual SysWireframe DoMakeDropHiliteRegion() const { return nil; }
	/** The rows from the trace's first to this one turned (RowsTo, TurnRow) - on entering, and again on the drop, which
		turns nothing more: a row turned shows the other state now. */
	virtual ErrorCode ProcessDragDropCommand(IDragDropController* controller, DragDrop::eCommandType type);
};

CREATE_PMINTERFACE(KFCResultEyeDropTarget, kKFCResultEyeDropTargetImpl)

DragDrop::TargetResponse KFCResultEyeDropTarget::CouldAcceptTypes(DataObjectIterator* dataIter, const IDragDropSource*,
																 const IDragDropController*) const
{
	if (dataIter == nil || EyeFlavor() == kNoFlavor)
		return DragDrop::kWontAcceptTargetResponse;
	const DataExchangeResponse response = dataIter->FlavorExistsWithPriorityInAllObjects(EyeFlavor());
	if (!response.CanDo())
		return DragDrop::kWontAcceptTargetResponse;
	return DragDrop::TargetResponse(response, DragDrop::kDropWillCopy);
}

void KFCResultEyeDropTarget::DoDragEnter()
{
	InterfacePtr<IDragDropController> ddController(GetExecutionContextSession(), IID_IDRAGDROPCONTROLLER);
	if (ddController != nil)
		(void)this->ProcessDragDropCommand(ddController, DragDrop::kDrop);
}

ErrorCode KFCResultEyeDropTarget::ProcessDragDropCommand(IDragDropController* controller, DragDrop::eCommandType /*type*/)
{
	if (!gTrace.fActive || controller == nil)
		return kFailure;
	// The drag's flavor taken in, as the Layers panel's target does before it reads the handler.
	if (controller->InternalizeDrag(EyeFlavor(), EyeFlavor()) != kSuccess)
		return kFailure;
	int32 chapter = -1, hit = -1;
	NodeID here;
	if (!KFCResultEye::LiveHitRow(this, chapter, hit, &here))
		return kSuccess;		// a kept tier's row: nothing of a trace's to turn
	InterfacePtr<ITreeViewMgr> treeMgr(QueryTreeOf(this));
	if (treeMgr == nil)
		return kFailure;
	const int32 turnedBefore = gTrace.fTurned;
	const std::vector<NodeID> rows = RowsTo(treeMgr, here);
	for (size_t k = 0; k < rows.size(); ++k)
		TurnRow(treeMgr, rows[k]);
	if (gTrace.fTurned != turnedBefore)
		SayTurned();
	return kSuccess;
}

//========================================================================================
// The trace over (KFCResultEye.h)
//========================================================================================

void KFCResultEye::DragEnded(const IPMUnknown* eye)
{
	// A DRAG THAT NEVER BEGAN - DoAddDragContent refused it (no handler for the flavor), so no trace was started: the
	// press is still a click on the eye pressed, and is not lost.
	if (!gTrace.fActive)
	{
		int32 chapter = -1, hit = -1;
		KFCResultModel::RowDisplay row;
		if (KFCResultEye::WrittenRow(eye, chapter, hit, row))
		{
			gTrace.fActive  = true;
			gTrace.fChapter = chapter;
			gTrace.fHit     = hit;
			gTrace.fWasKept = row.kept;
		}
	}
	if (gTrace.fActive)
	{
		// A DRAG THAT ENTERED NO EYE - the press moved straight off the column: a click on the first eye, which turns it
		// (as KFCResultEyeEH's Turn would have).
		if (gTrace.fTurned == 0)
		{
			InterfacePtr<ITreeViewMgr> treeMgr(QueryTreeOf(eye));
			const NodeID first(KFCResultNodeID::Create(gTrace.fChapter, gTrace.fHit));
			if (treeMgr != nil)
				TurnRow(treeMgr, first);
		}
		if (gTrace.fTurned > 0)
			SayTurned();
	}
	gTrace = Trace();
	// The keyboard to the list, as after every click on it (KFCResultEyeEH's Turn).
	(void)KFCResultTree::TakeKeyboard();
}

// End, KFCResultEyeDrag.cpp.
