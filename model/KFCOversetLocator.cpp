//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Implementation of the overset "+" locator shared by KFCSearchEngine and, through IKFCRuns, the
//  UI half's KFCJump. See KFCOversetLocator.h for the rationale.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITextModel.h"			// QueryTextParcelList
#include "ITextParcelList.h"
#include "IParcelList.h"		// GetLastParcelKey / GetParcelFrameUID / GetParcelBounds
#include "IGeometry.h"
#include "ITableUtils.h"		// InsideTable / TableToPrimaryTextIndex
#include "ITextStoryThread.h"	// GetDictUID - the dictionary a footnote's thread belongs to
#include "ITextStoryThreadDict.h"	// GetAnchorTextRange - where that footnote's reference stands

// General includes:
#include "ParcelKey.h"			// ParcelKey::IsValid
#include "TransformUtils.h"		// ::InnerToPasteboardMatrix
#include "Utils.h"
#include "PMMatrix.h"
#include "PMPoint.h"
#include "PMRect.h"

// Project includes:
#include "KFCOversetLocator.h"

namespace
{
	// The outport of the LAST placed parcel in the thread that 'pos' composes into: its frame UID
	// and its outport corner in pasteboard coordinates. Returns false when the thread has no placed
	// parcel at all (every parcel reports kInvalidUID = overset).
	//
	// VERTICAL TEXT NEEDS NO SPECIAL CASE (measured on the Japanese build, through KCM's copy of
	// this code): the corner is taken in the PARCEL's own coordinates, and
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

	// One step out of a thread that hangs from a character of its parent - a footnote, whose reference character
	// stands in the text it belongs to - to that character's position: where its dictionary is anchored
	// (ITextStoryThreadDict::GetAnchorTextRange, as SnpManipulateTextModel.cpp:648 asks it). pos itself when
	// the thread hangs from nothing: the primary thread's dictionary and an unanchored one say so through
	// wasAnchored (ITextStoryThreadDict.h:51-61), and then there is nowhere further out to go.
	TextIndex AnchorOfThread(ITextModel* textModel, IDataBase* db, TextIndex pos)
	{
		InterfacePtr<ITextStoryThread> thread(textModel->QueryStoryThread(pos, nil, nil));
		if (thread == nil)
			return pos;
		InterfacePtr<ITextStoryThreadDict> dict(db, thread->GetDictUID(), UseDefaultIID());
		if (dict == nil)
			return pos;
		bool16 anchored = kFalse;
		const Text::StoryRange anchor = dict->GetAnchorTextRange(&anchored);
		return anchored ? anchor.Start(nil) : pos;
	}
}

KFCOversetLoc KFCFindOversetLocator(const UIDRef& storyRef, TextIndex pos)
{
	KFCOversetLoc loc;	// found = false

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

	// Nothing placed in this thread: the "+" lives on an ancestor thread. Inside a table, the table (or
	// the row holding this cell) is pushed out of a parent frame, so the cell itself is gone; inside a
	// FOOTNOTE, its reference character is overset, so the footnote was never placed at all (climbing
	// out of tables alone leaves an overset footnote's match no "+" to name: no page on its row, no frame
	// for the lock test, so the replace could write where the body text beside it was refused). Climb
	// out - the table anchor, or the footnote's reference - until an ancestor has a placed parcel:
	// ultimately the main frame's "+". Guarded against non-progress / deep nesting.
	TextIndex cur = pos;
	for (int32 guard = 0; guard < 32; ++guard)
	{
		const TextIndex up = Utils<ITableUtils>()->InsideTable(textModel, cur)
			? Utils<ITableUtils>()->TableToPrimaryTextIndex(textModel, cur)
			: AnchorOfThread(textModel, db, cur);
		if (up == cur)
			break;	// no progress - nothing further out
		cur = up;
		if (LocateInThread(textModel, db, cur, loc.frameUID, loc.outportPb))
		{
			loc.found = true;
			return loc;
		}
	}
	return loc;
}

// End, KFCOversetLocator.cpp.
