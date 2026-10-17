//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE ROW EYE'S CLICK (docs/superpowers/specs/2026-10-17-kfc-row-eye-keep-design.md 2-1 - the author's call of
//  2026-10-17, D-13): a hit row's eye turned on or off - the row kept, or not, when its story or document is searched
//  again. The SDK's own shape of a tree row's icon (sdksamples/customconditionaltextui/CusCondTxtUIIconEventHandler.cpp):
//  the press handled by the icon itself - the row found through its parent (ITreeNodeIDData), the model told, the icon
//  drawn anew. The press, its release and a double click's second press are all taken here, so a click on the eye
//  neither selects the row nor jumps (KFCResultNodeEH does both on the release); every click turns the eye once (a
//  double click twice, as every click of this list is one click). A kept tier's row (grey) is not a hit row: its eye is
//  disabled and its clicks go to the row, which switches to its tier (ROW-42). AN EYE IS A WRITTEN ROW'S - one KFC has
//  replaced or outlined (the author, 2026-10-17 evening: an eye on a row not written only kept a match since deleted as
//  Missing): on any other row the column is empty and its clicks are the row's too (selected, jumped to).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "ITreeNodeIDData.h"		// the row's NodeID, on the row widget this icon sits in
#include "IWidgetParent.h"			// the icon -> its row

// General includes:
#include "CEventHandler.h"
#include "IconRsrcDefs.h"			// kEyeballIcon, kNoIcon
#include "SysControlIds.h"			// kApplicationRsrcPluginID - the eyeball is the application's icon

// Project includes:
#include "KFCUIID.h"
#include "KFCModelAccess.h"			// KFCResults() - the model half's door
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
	/** The live hit row this icon sits in - false for a kept tier's row, or none. */
	bool LiveHitRow(int32& outChapter, int32& outHit) const;
	/** ...one KFC has written (replaced or outlined): the only rows with an eye to turn. outRow = its display. */
	bool WrittenRow(int32& outChapter, int32& outHit, KFCResultModel::RowDisplay& outRow) const;
	/** The eye turned: the model told, the icon drawn, the status line, the keyboard to the list. kFalse = not a written
	    live hit row's eye - the click is the row's. */
	bool16 Turn();
};

CREATE_PMINTERFACE(KFCResultEyeEH, kKFCResultEyeEHImpl)

bool KFCResultEyeEH::LiveHitRow(int32& outChapter, int32& outHit) const
{
	outChapter = outHit = -1;
	InterfacePtr<const IWidgetParent> parent(this, UseDefaultIID());
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
	return true;
}

bool KFCResultEyeEH::WrittenRow(int32& outChapter, int32& outHit, KFCResultModel::RowDisplay& outRow) const
{
	return LiveHitRow(outChapter, outHit) && KFCResults()->GetHitRow(outChapter, outHit, outRow) && outRow.replaced;
}

bool16 KFCResultEyeEH::Turn()
{
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	if (!WrittenRow(chapter, hit, row))
		return kFalse;		// a kept tier's row (ROW-42), or one KFC has not written: its click is the row's
	const bool kept = !row.kept;
	if (!KFCResults()->SetHitKept(chapter, hit, kept))
		return kTrue;
	InterfacePtr<IControlView> view(this, UseDefaultIID());
	if (view != nil)
	{
		view->SetRsrcID(kept ? kEyeballIcon : kNoIcon);
		view->SetRsrcPluginID(kApplicationRsrcPluginID);
		view->Invalidate();
	}
	// "P1(2)  kept when searching again" - the row named by its locator, as the list reads it.
	PMString words(row.locator);
	words.SetTranslatable(kFalse);
	words.Append(kept ? "  kept when searching again" : "  not kept when searching again");
	KFCResultTree::ShowStatus(words);
	// The keyboard to the list, as after every click on it (KFCResultNodeEH) - Return and the arrows go on from here.
	(void)KFCResultTree::TakeKeyboard();
	return kTrue;
}

bool16 KFCResultEyeEH::LButtonDn(IEvent* /*e*/)
{
	return Turn();
}

bool16 KFCResultEyeEH::LButtonUp(IEvent* /*e*/)
{
	// The release of a press taken above: taken too, or the row's handler would select the row and jump. A row not
	// written gave its press to the row: its release goes there too.
	int32 chapter = -1, hit = -1;
	KFCResultModel::RowDisplay row;
	return WrittenRow(chapter, hit, row) ? kTrue : kFalse;
}

bool16 KFCResultEyeEH::ButtonDblClk(IEvent* /*e*/)
{
	// A double click's second press: a click like the first (every click of this list is one click).
	return Turn();
}

// End, KFCResultEyeEH.cpp.
