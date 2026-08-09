//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Implementation of the overset "+" locator shared by KBSJump and KBSSearchEngine. See
//  KBSOversetLocator.h for the rationale.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITextModel.h"			// QueryTextParcelList
#include "ITextParcelList.h"
#include "IParcelList.h"		// GetLastParcelKey / GetParcelFrameUID / GetParcelBounds
#include "IGeometry.h"
#include "ITableUtils.h"		// InsideTable / TableToPrimaryTextIndex

// General includes:
#include "ParcelKey.h"			// ParcelKey::IsValid
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "Utils.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"

// Project includes:
#include "KBSOversetLocator.h"

namespace
{
	// The outport of the LAST placed parcel in the thread that 'pos' composes into: its frame UID
	// and its outport corner in pasteboard coordinates. Returns false when the thread has no placed
	// parcel at all (every parcel reports kInvalidUID = overset).
	//
	// VERTICAL TEXT NEEDS NO SPECIAL CASE (measured 2026-08-06, on the Japanese build, through
	// KESCM's copy of this code): the corner is taken in the PARCEL's own coordinates, and
	// GetParcelToFrameMatrix carries the writing direction, so the transformed point lands where
	// InDesign actually draws the "+" - bottom LEFT for vertical text. Do not add a branch on
	// writing direction here.
	bool LocateInThread(ITextModel* textModel, IDataBase* db, TextIndex pos, UID& outFrame, PBPMPoint& outPb)
	{
		InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
		if (tpl == nil)
			return false;
		InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
		if (pl == nil)
			return false;

		for (ParcelKey k = pl->GetLastParcelKey(); k.IsValid(); k = pl->GetPreviousParcelKey(k))
		{
			const UID frameUID = pl->GetParcelFrameUID(k);
			if (frameUID == kInvalidUID)
				continue;	// this fragment is itself overset - keep walking toward the placed part

			InterfacePtr<IGeometry> frameGeo(db, frameUID, UseDefaultIID());
			if (frameGeo == nil)
				continue;

			const PMRect  parcelBounds  = pl->GetParcelBounds(k);			// parcel-local
			const PMMatrix toFrame      = pl->GetParcelToFrameMatrix(k);		// parcel -> text-frame inner
			const PMMatrix toPasteboard = ::InnerToPasteboardMatrix(frameGeo);	// frame inner -> pasteboard

			PMPoint corner(parcelBounds.Right(), parcelBounds.Bottom());		// outport corner, in parcel coordinates
			toFrame.Transform(&corner);
			toPasteboard.Transform(&corner);

			outFrame = frameUID;
			outPb    = PBPMPoint(corner.X(), corner.Y());
			return true;
		}
		return false;
	}
}

KBSOversetLoc KBSFindOversetLocator(const UIDRef& storyRef, TextIndex pos)
{
	KBSOversetLoc loc;	// found = false

	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return loc;
	IDataBase* db = storyRef.GetDataBase();
	if (db == nil)
		return loc;

	// The position's own thread first (a plain frame, or a placed cell whose own content overflows).
	if (LocateInThread(textModel, db, pos, loc.frameUID, loc.outportPb))
	{
		loc.found = true;
		return loc;
	}

	// Nothing placed in this thread: if it is inside a table, the table (or the row holding this
	// cell) is pushed out of a parent frame, so the cell itself is gone and the "+" lives on an
	// ancestor thread. Climb the table-anchor chain out of any nested tables until an ancestor has a
	// placed parcel - ultimately the main frame's "+". Guarded against non-progress / deep nesting.
	TextIndex cur = pos;
	for (int32 guard = 0; guard < 32; ++guard)
	{
		if (!Utils<ITableUtils>()->InsideTable(textModel, cur))
			break;
		const TextIndex up = Utils<ITableUtils>()->TableToPrimaryTextIndex(textModel, cur);
		if (up == cur)
			break;	// no progress
		cur = up;
		if (LocateInThread(textModel, db, cur, loc.frameUID, loc.outportPb))
		{
			loc.found = true;
			return loc;
		}
	}
	return loc;
}

// End, KBSOversetLocator.cpp.
