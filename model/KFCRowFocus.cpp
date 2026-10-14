//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC) - a result row's text focus of KFC's own class. See KFCRowFocus.h.
//
//  THE RANGE IS A SNAPSHOT VIEW INTERFACE (CViewInterface.h, its ten steps): every change the focus manager makes
//  (Inserted / Deleted / Update) and every PreDirtyFocus marks it dirty FIRST - with PreDirtyViewInterface, the public
//  function PersistUtils.h gives for a view interface (CViewInterface's own PreDirty is noted "For internal use") - so
//  the database it targets keeps the range of before the change with that step: an Undo reads it back, a Redo the range
//  of after. Initialize and SetRange do not mark: making a focus is no change of the document, and the header leaves
//  the PreDirtyFocus before a SetRange to its caller (ITextFocus.h).
//
//  HOW A RANGE MOVES. InDesign's rules for its own focus are written nowhere; these are KT's, measured the same as
//  InDesign's on typing before, inside and at the start of a range and on a delete across its end, and closer than
//  InDesign's on a Change All (its focus took in a character written at its end - the spike note, S9):
//    - an insert before the range, or at its start, moves it on; inside it, it grows; at its end, nothing. A caret at
//      the insert point goes with the text it leans on (forward = the text after it).
//    - a delete: each end clipped to the deleted part (an end inside it goes to the part's start).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IPMStream.h"
#include "ITextFocus.h"
#include "ITextFocusManager.h"
#include "ITextModel.h"

// General includes:
#include "CViewInterface.h"
#include "PersistUtils.h"		// PreDirtyViewInterface, GetClass
#include "RangeData.h"

// Project includes:
#include "KFCID.h"
#include "KFCRowFocus.h"

/** A result row's text focus that the user's Undo and Redo put back. See KFCRowFocus.h.
*/
class KFCRowFocusImpl : public CViewInterface<ITextFocus>
{
public:
	KFCRowFocusImpl(IPMUnknown* boss);
	virtual ~KFCRowFocusImpl() {}

	virtual void		Initialize(ITextModel* model, const RangeData& range);
	virtual ITextModel*	QueryModel() const;
	virtual void		PreDirtyFocus();
	virtual void		SetRange(bool16 allowNotify, const RangeData& range, bool16 excludeCacheEnd = kTrue);
	virtual RangeData	GetCurrentRange() const;
	virtual TextIndex	GetStart(RangeData::Lean* lean) const;
	virtual TextIndex	GetEnd() const;
	virtual bool16		IsRange() const;
	virtual uint32		Length() const;
	virtual void		Inserted(TextIndex start, int32 count, bool16 allowNotify);
	virtual void		Deleted(TextIndex start, int32 count, bool16 allowNotify);
	virtual void		Update(const ITextFocusManager::FocusUpdateInfoList& updateInfoList, bool16 allowNotify);
	virtual void		NotifyChanged(bool16 excludeCacheEnd = kTrue);
	virtual void		DeRegisterFromModel(void);

	/** The snapshot: the range and its lean. Not the model (a focus never moves to another one), and not the lost mark
	    (an Undo must not take it down). */
	void SnapshotReadWrite(IPMStream* s, ImplementationID prop);

	/** An Undo or a Redo this focus holds no snapshot for (CViewInterface.h, step 9: "forcibly rebuild the data") - what
	    it would rebuild from is gone, so it is marked lost for good. */
	void ResetView(ImplementationID prop);

	bool IsLost() const { return fLost; }

private:
	void Take(const RangeData& range);
	void ApplyInsert(TextIndex at, int32 count);
	void ApplyDelete(TextIndex at, int32 count);

	ITextModel*	fModel;		// not AddRef'd: the model's focus manager holds this focus, not the other way round
	TextIndex	fStart;
	TextIndex	fEnd;
	int32		fLean;		// a RangeData::Lean
	bool		fLost;		// ResetView came - outside the snapshot
};

CREATE_VIEW_PMINTERFACE(KFCRowFocusImpl, kKFCRowFocusImpl)

KFCRowFocusImpl::KFCRowFocusImpl(IPMUnknown* boss)
	: CViewInterface<ITextFocus>(boss), fModel(nil), fStart(0), fEnd(0), fLean(RangeData::kLeanForward), fLost(false)
{
}

void KFCRowFocusImpl::Initialize(ITextModel* model, const RangeData& range)
{
	fModel = model;
	Take(range);
}

ITextModel* KFCRowFocusImpl::QueryModel() const
{
	if (fModel != nil)
		fModel->AddRef();
	return fModel;
}

void KFCRowFocusImpl::PreDirtyFocus()
{
	::PreDirtyViewInterface(this);
}

void KFCRowFocusImpl::SetRange(bool16 /*allowNotify*/, const RangeData& range, bool16 /*excludeCacheEnd*/)
{
	Take(range);
}

RangeData KFCRowFocusImpl::GetCurrentRange() const
{
	return RangeData(fStart, fEnd, static_cast<RangeData::Lean>(fLean));
}

TextIndex KFCRowFocusImpl::GetStart(RangeData::Lean* lean) const
{
	if (lean != nil)
		*lean = static_cast<RangeData::Lean>(fLean);
	return fStart;
}

TextIndex KFCRowFocusImpl::GetEnd() const
{
	return fEnd;
}

bool16 KFCRowFocusImpl::IsRange() const
{
	return fEnd > fStart ? kTrue : kFalse;
}

uint32 KFCRowFocusImpl::Length() const
{
	return static_cast<uint32>(fEnd - fStart);
}

void KFCRowFocusImpl::Inserted(TextIndex start, int32 count, bool16 /*allowNotify*/)
{
	::PreDirtyViewInterface(this);
	ApplyInsert(start, count);
}

void KFCRowFocusImpl::Deleted(TextIndex start, int32 count, bool16 /*allowNotify*/)
{
	::PreDirtyViewInterface(this);
	ApplyDelete(start, count);
}

void KFCRowFocusImpl::Update(const ITextFocusManager::FocusUpdateInfoList& updateInfoList, bool16 /*allowNotify*/)
{
	// A series of changes at one time - a Change All (ITextFocusManager.h) - taken in their order.
	::PreDirtyViewInterface(this);
	for (ITextFocusManager::FocusUpdateInfoList::const_iterator it = updateInfoList.begin(); it != updateInfoList.end(); ++it)
	{
		if (it->fUpdateType == ITextFocusManager::kInserted)
			ApplyInsert(it->fStart, it->fLen);
		else if (it->fUpdateType == ITextFocusManager::kDeleted)
			ApplyDelete(it->fStart, it->fLen);
	}
}

void KFCRowFocusImpl::NotifyChanged(bool16 /*excludeCacheEnd*/)
{
	// The focus cache of a selection is told here; a row's focus is drawn by no one and has no cache to tell.
}

void KFCRowFocusImpl::DeRegisterFromModel(void)
{
	fModel = nil;
}

void KFCRowFocusImpl::SnapshotReadWrite(IPMStream* s, ImplementationID /*prop*/)
{
	int32 start = fStart, end = fEnd, lean = fLean;
	s->XferInt32(start);
	s->XferInt32(end);
	s->XferInt32(lean);
	if (s->IsReading())
	{
		fStart = start;
		fEnd = end;
		fLean = lean;
	}
}

void KFCRowFocusImpl::ResetView(ImplementationID /*prop*/)
{
	fLost = true;
}

void KFCRowFocusImpl::Take(const RangeData& range)
{
	RangeData::Lean lean = RangeData::kLeanForward;
	fStart = range.Start(&lean);
	fEnd = range.End();
	fLean = static_cast<int32>(lean);
}

void KFCRowFocusImpl::ApplyInsert(TextIndex at, int32 count)
{
	if (count <= 0)
		return;
	const bool caret = (fStart == fEnd);
	const bool leansOn = (fLean == RangeData::kLeanForward);
	if (fStart > at || (fStart == at && (!caret || leansOn)))
		fStart += count;
	if (fEnd > at || (caret && fEnd == at && leansOn))
		fEnd += count;
}

void KFCRowFocusImpl::ApplyDelete(TextIndex at, int32 count)
{
	if (count <= 0)
		return;
	const TextIndex stop = at + count;
	fStart = (fStart <= at) ? fStart : (fStart >= stop ? fStart - count : at);
	fEnd = (fEnd <= at) ? fEnd : (fEnd >= stop ? fEnd - count : at);
}

//----------------------------------------------------------------------------------------

bool KFCRowFocus::IsLost(const ITextFocus* focus)
{
	// Our class's implementation, read only once the class says it is ours (the same plug-in's own class).
	if (focus == nil || ::GetClass(focus) != kKFCRowFocusBoss)
		return false;
	return static_cast<const KFCRowFocusImpl*>(focus)->IsLost();
}

// End, KFCRowFocus.cpp.
