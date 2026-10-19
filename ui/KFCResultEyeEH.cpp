//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE ROW EYE'S CLICK (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-1 - the author's call): a hit
//  row's eye turned on or off - the row kept, or not, when its story or document is searched again. The SDK's own shape
//  of a tree row's icon (sdksamples/customconditionaltextui/CusCondTxtUIIconEventHandler.cpp): the press handled by the
//  icon itself - the row found through its parent (ITreeNodeIDData), the model told, the icon drawn anew. The press,
//  its release and a double click's second press are all taken here, so a click on the eye neither selects the row nor
//  jumps (KFCResultNodeEH does both on the release); every click turns the eye once (a double click twice, as every
//  click of this list is one click). A kept tier's row (grey) is not a hit row: its eye is disabled and its clicks go
//  to the row, which switches to its tier (ROW-42). AN EYE IS A WRITTEN ROW'S - one KFC has replaced, outlined, or
//  given a special operation (the author's call: an eye on a row not written only kept a match since deleted as
//  Missing): on any other row nothing is drawn there, and its clicks are the row's too (selected, jumped to).
//  A PRESS THAT BECOMES A DRAG TRACES THE COLUMN (the author: "like the Layers panel"): the Layers
//  panel's own eye (open/components/layerpanel/LayerEyeDragDropSourceEH.cpp) - IsUserStartingDrag (IEventUtils) tells a
//  press that moves from one that is let go, and the first starts a drag the eyes it passes over answer
//  (KFCResultEyeDrag.cpp).
//  The row's facts, asked by the drag too, are KFCResultEye's (KFCResultEye.h).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IDragDropController.h"	// StartDrag - the trace
#include "IDragDropSource.h"		// WillDrag - only a written row's eye starts one (KFCResultEyeDrag.cpp)
#include "IEvent.h"
#include "IEventUtils.h"			// IsUserStartingDrag - a press that moves, told from one let go (EventUtilities.h's
									// own, inside ID_DEPRECATED, says to use this instead)
#include "ITreeNodeIDData.h"		// the row's NodeID, on the row widget this icon sits in
#include "IWidgetParent.h"			// the icon -> its row
#include "ISession.h"

// General includes:
#include "CEventHandler.h"
#include "IconRsrcDefs.h"			// kEyeballIcon, kNoIcon
#include "SysControlIds.h"			// kApplicationRsrcPluginID - the eyeball is the application's icon
#include "Utils.h"					// Utils<IEventUtils>

// Project includes:
#include "KFCUIID.h"
#include "KFCModelAccess.h"			// KFCResults() - the model half's door
#include "KFCResultEye.h"
#include "KFCResultNodeID.h"
#include "KFCResultTree.h"			// ShowStatus, TakeKeyboard

class KFCResultEyeEH : public CEventHandler
{
public:
	KFCResultEyeEH(IPMUnknown* boss) : CEventHandler(boss) {}
	virtual ~KFCResultEyeEH() {}

	virtual bool16 LButtonDn(IEvent* e);
	virtual bool16 LButtonUp(IEvent* e);
	virtual bool16 ButtonDblClk(IEvent* e);

private:
	/** The eye turned: the model told, the icon drawn, the status line, the keyboard to the list. kFalse = not a written
	    live hit row's eye - the click is the row's. */
	bool16 Turn();
};

CREATE_PMINTERFACE(KFCResultEyeEH, kKFCResultEyeEHImpl)

//========================================================================================
// The row's facts (KFCResultEye.h) - asked by this click and by the drag
//========================================================================================

bool KFCResultEye::LiveHitRow(const IPMUnknown* eye, int32& outChapter, int32& outHit, NodeID* outNode)
{
	outChapter = outHit = -1;
	InterfacePtr<const IWidgetParent> parent(eye, UseDefaultIID());
	if (parent == nil)
		return false;
	InterfacePtr<ITreeNodeIDData> row(static_cast<ITreeNodeIDData*>(parent->QueryParentFor(IID_ITREENODEIDDATA)));
	if (row == nil)
		return false;
	TreeNodePtr<KFCResultNodeID> node(row->Get());
	if (node == nil || !node->IsHitRow())
		return false;
	outChapter = node->GetChapter();
	outHit = node->GetHit();
	if (outNode != nil)
		*outNode = row->Get();
	return true;
}

bool KFCResultEye::IsWritten(const KFCResultModel::RowDisplay& row)
{
	return row.replaced || row.special != kKFCSpecialNone;
}

bool KFCResultEye::WrittenRow(const IPMUnknown* eye, int32& outChapter, int32& outHit, KFCResultModel::RowDisplay& outRow,
							  NodeID* outNode)
{
	return LiveHitRow(eye, outChapter, outHit, outNode) && KFCResults()->GetHitRow(outChapter, outHit, outRow)
		&& IsWritten(outRow);
}

void KFCResultEye::SayKept(const PMString& what, bool kept)
{
	PMString words(what);
	words.SetTranslatable(kFalse);
	words.Append(kept ? "  kept when searching again" : "  not kept when searching again");
	KFCResultTree::ShowStatus(words);
}

void KFCResultEye::Draw(IControlView* eye, bool kept, bool now)
{
	if (eye == nil)
		return;
	eye->SetRsrcID(kept ? kEyeballIcon : kNoIcon);
	eye->SetRsrcPluginID(kApplicationRsrcPluginID);
	if (now)
		eye->ForceRedraw();
	else
		eye->Invalidate();
}

//========================================================================================
// The click
//========================================================================================

bool16 KFCResultEyeEH::Turn()
{
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	if (!KFCResultEye::WrittenRow(this, chapter, hit, row))
		return kFalse;		// a kept tier's row (ROW-42), or one KFC has not written: its click is the row's
	const bool kept = !row.kept;
	if (!KFCResults()->SetHitKept(chapter, hit, kept))
		return kTrue;
	InterfacePtr<IControlView> view(this, UseDefaultIID());
	KFCResultEye::Draw(view, kept, false);
	// "P1(2)  kept when searching again" - the row named by its locator, as the list reads it.
	KFCResultEye::SayKept(row.locator, kept);
	// The keyboard to the list, as after every click on it (KFCResultNodeEH) - Return and the arrows go on from here.
	(void)KFCResultTree::TakeKeyboard();
	return kTrue;
}

bool16 KFCResultEyeEH::LButtonDn(IEvent* e)
{
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	if (!KFCResultEye::WrittenRow(this, chapter, hit, row))
		return kFalse;		// the row's click (Turn says why)
	// A PRESS THAT MOVES IS A TRACE (LayerEyeDragDropSourceEH::LButtonDn): the drag the eyes it passes over answer, the
	// first of them this one (KFCResultEyeDrag.cpp). IsUserStartingDrag waits for the mouse to move or the button to come
	// up - a press let go where it was is a click, and turns this eye alone, as before. (The Layers panel calls the
	// deprecated global of the same name; IEventUtils is the one its header points to.)
	InterfacePtr<IDragDropSource> dragSource(this, IID_IDRAGDROPSOURCE);
	Utils<IEventUtils> eventUtils;		// asked before it is used: QueryUtilInterface has no nil guard (memory utils-boss-facade-access)
	bool16 isPatientUser = kFalse;
	if (dragSource != nil && eventUtils && dragSource->WillDrag(e) && eventUtils->IsUserStartingDrag(e, isPatientUser))
	{
		InterfacePtr<IDragDropController> ddController(GetExecutionContextSession(), IID_IDRAGDROPCONTROLLER);
		if (ddController != nil)
		{
			// Returns with the drag over ("upon return, the drag will have completed" - IDragDropController.h); it takes
			// the release, so LButtonUp does not come for this press (LayerEyeDragDropSourceEH: "StartDrag will swallow
			// the mouse up").
			(void)ddController->StartDrag(dragSource, e);
			KFCResultEye::DragEnded(this);
			return kTrue;
		}
	}
	return Turn();
}

bool16 KFCResultEyeEH::LButtonUp(IEvent* /*e*/)
{
	// The release of a press taken above: taken too, or the row's handler would select the row and jump. A row not
	// written gave its press to the row: its release goes there too.
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	return KFCResultEye::WrittenRow(this, chapter, hit, row) ? kTrue : kFalse;
}

bool16 KFCResultEyeEH::ButtonDblClk(IEvent* /*e*/)
{
	// A double click's second press: a click like the first (every click of this list is one click).
	return Turn();
}

// End, KFCResultEyeEH.cpp.
