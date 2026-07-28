//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Jump-to-hit navigation implementation. The move-to-location helpers (scroll, overset test,
//  marker rectangle, bring-document-frontmost with zoom carry) are ported from KESCL's
//  KESCLFindInDoc (KESCL left untouched); the driver JumpToHit is simplified to a static snapshot:
//  it reads the stored (docRef, file, story, range) for one hit and goes there, with no match-list
//  navigation, edit-repair or reverse mode. Read-only geometry work sits inside a
//  SaveRestoreModifiedState dirty guard so a (possibly windowless) chapter never comes out
//  modified.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IDocument.h"
#include "IDocumentPresentation.h"	// MakeActive; the accept-all predicate typedef
#include "IDocumentUIUtils.h"		// FindPresentationForDocument
#include "IFrameList.h"
#include "IFrameListComposer.h"
#include "IGeometry.h"
#include "ILayoutControlData.h"		// kFitNone
#include "ILayoutUIUtils.h"
#include "IOpenLayoutCmdData.h"		// SetPerspective_ - the inherited zoom rides the open command
#include "IPageList.h"
#include "IPanorama.h"
#include "IParcelList.h"			// GetParcelFrameUID
#include "ITextModel.h"
#include "ITextParcelList.h"		// GetParcelContaining
#include "IWaxStrand.h"
#include "IWaxIterator.h"
#include "IWaxLine.h"
#include "IWaxRun.h"
#include "IWaxGlyphs.h"
#include "ISession.h"

// General includes:
#include "ParcelKey.h"				// ParcelKey::IsValid
#include "TextID.h"					// kFrameListBoss, IID_IWAXSTRAND
#include "widgetid.h"				// IID_IPANORAMA
#include "LayoutUIID.h"				// kOpenLayoutCmdBoss
#include "ErrorUtils.h"				// PMSetGlobalErrorCode
#include "CmdUtils.h"
#include "PersistUtils.h"			// ::GetUIDRef
#include "IDataBase.h"				// SaveRestoreModifiedState
#include "UIDList.h"
#include "Utils.h"
#include "K2SmartPtr.h"				// K2::scoped_ptr
#include "PMPoint.h"
#include "PMRect.h"
#include "PMMatrix.h"

// Project includes:
#include "KBSJump.h"
#include "KBSDrawEventHandler.h"
#include "KBSBookScope.h"
#include "KBSResultModel.h"
#include "KBSOversetLocator.h"		// KBSFindOversetLocator - the shared overset "+" locator
#include "KBSSearchEngine.h"		// MatchIsSameOccurrence - the one test the replace uses too
#include "KBSResultTree.h"			// RefreshRows / ShowStatus - telling the panel what was found here

namespace
{
	// The "Hide Previous Chapter" flyout toggle (session state only; starts ON - a book search
	// leaves the desk clean, showing only the chapter a jump landed in). The sweep it gates
	// (CloseDisplayedDocsIfClean) is stateless, so flipping it mid-session is safe.
	bool gHidePrevChapterOn = true;

	//------------------------------------------------------------------------------------
	// Move-to-location helpers (ported from KESCLFindInDoc)
	//------------------------------------------------------------------------------------

	// Scroll the front layout view so the given pasteboard point is centred. Does not select.
	void ScrollFrontViewToPoint(const PBPMPoint& pbPoint)
	{
		InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
		if (view == nil)
			return;
		InterfacePtr<IPanorama> pano(view, UseDefaultIID());
		if (pano == nil)
			return;
		pano->ScrollViewCenterTo(pbPoint, kTrue /*forceRedraw*/);
	}

	// True if text position 'pos' in storyRef is overset (composed but not placed in any frame).
	bool IsTextIndexOverset(const UIDRef& storyRef, TextIndex pos)
	{
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			return false;
		InterfacePtr<ITextParcelList> tpl(textModel->QueryTextParcelList(pos));
		if (tpl == nil)
			return true;
		const ParcelKey key = tpl->GetParcelContaining(pos);
		if (!key.IsValid())
			return true;
		InterfacePtr<IParcelList> pl(tpl, UseDefaultIID());
		if (pl == nil)
			return false;
		return (pl->GetParcelFrameUID(key) == kInvalidUID);
	}

	// x (in the wax run's local coords) and the run's to-pasteboard matrix for a text offset within
	// a wax line: locate the run by text offset, read the glyph escapement up to the offset, hand
	// back the run matrix so the caller can transform a whole rectangle.
	bool RunXAndMatrix(IWaxLine* waxLine, int32 offsetInLine, PMReal& xOut, PMMatrix& mOut)
	{
		if (waxLine == nil)
			return false;
		int32 glyphOffset = -1;
		InterfacePtr<IWaxRun> waxRun(waxLine->QueryRunByTextOffset(offsetInLine, &glyphOffset));
		if (waxRun == nil)
			return false;
		PMReal x = 0;
		if (glyphOffset > 0)
		{
			InterfacePtr<IWaxGlyphs> waxGlyphs(waxRun, UseDefaultIID());
			if (waxGlyphs != nil)
				x = waxGlyphs->GetEscapementAt(glyphOffset - 1);
		}
		xOut = x;
		mOut = waxRun->GetToPasteboardMatrix();
		return true;
	}

	// Pasteboard rectangle around the FIRST chunk of the match [start, end): the part on the wax
	// line containing 'start'. Returns false if the position is overset or geometry is unavailable.
	bool GetFirstChunkPasteboardRect(const UIDRef& storyRef, TextIndex start, TextIndex end, PMRect& outRect)
	{
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			return false;

		InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)textModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
		if (waxStrand == nil)
			return false;

		InterfacePtr<IFrameList> frameList(waxStrand, UseDefaultIID());
		if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
		{
			InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}

		K2::scoped_ptr<IWaxIterator> waxIter(waxStrand->NewWaxIterator());
		if (waxIter == nil)
			return false;

		int32 offStart = 0;
		IWaxLine* waxLine = waxIter->GetFirstWaxLine(start, &offStart);
		if (waxLine == nil)
			return false;	// overset / not placed

		const TextIndex lineOrigin = start - offStart;
		const int32     lineSpan   = waxLine->GetTextSpan();
		const TextIndex lineEnd    = lineOrigin + lineSpan;
		TextIndex chunkEnd = end;
		if (chunkEnd > lineEnd) chunkEnd = lineEnd;
		if (chunkEnd <= start)  chunkEnd = start + 1;
		const int32 offEnd = static_cast<int32>(chunkEnd - lineOrigin);

		PMReal xLeft = 0, xRight = 0;
		PMMatrix mLeft, mRight;
		if (!RunXAndMatrix(waxLine, offStart, xLeft, mLeft))
			return false;
		if (!RunXAndMatrix(waxLine, offEnd, xRight, mRight))
		{
			mRight = mLeft;
			xRight = xLeft + waxLine->GetLineHeight() * PMReal(0.5);
		}

		const PMReal h       = waxLine->GetLineHeight();
		const PMReal ascent  = h * PMReal(0.95);
		const PMReal descent = h * PMReal(0.2);

		PMPoint c[4];
		c[0] = PMPoint(xLeft,  -ascent);  mLeft.Transform(&c[0]);
		c[1] = PMPoint(xLeft,   descent); mLeft.Transform(&c[1]);
		c[2] = PMPoint(xRight, -ascent);  mRight.Transform(&c[2]);
		c[3] = PMPoint(xRight,  descent); mRight.Transform(&c[3]);

		PMReal minX = c[0].X(), maxX = c[0].X(), minY = c[0].Y(), maxY = c[0].Y();
		for (int32 i = 1; i < 4; ++i)
		{
			if (c[i].X() < minX) minX = c[i].X();
			if (c[i].X() > maxX) maxX = c[i].X();
			if (c[i].Y() < minY) minY = c[i].Y();
			if (c[i].Y() > maxY) maxY = c[i].Y();
		}
		outRect = PMRect(minX, minY, maxX, maxY);
		return true;
	}

	// Accepts every presentation (a local accept-all predicate).
	bool KBSAcceptAnyPresentation(IDocumentPresentation* /*p*/)
	{
		return true;
	}

	// Bring the given document's layout window to the front. A windowless chapter gets its first
	// window opened here; a background-tab window is activated. The zoom the user was looking at
	// travels along (zoom first, scroll second). Returns false when no window could be produced or
	// the activation did not take (the caller then reports without scrolling).
	bool EnsureDocFrontmost(const UIDRef& docRef)
	{
		IDocument* front = Utils<ILayoutUIUtils>()->GetFrontDocument();
		if (front != nil && ::GetUIDRef(front) == docRef)
			return true;

		IDataBase* db = docRef.GetDataBase();
		if (db == nil)
			return false;

		// The zoom to inherit, read BEFORE the switch (monitor-PPI corrected effective scale).
		PMReal srcZoom(-1.0);
		{
			InterfacePtr<IControlView> srcView(Utils<ILayoutUIUtils>()->QueryFrontView());
			if (srcView != nil)
			{
				InterfacePtr<IPanorama> srcPano(srcView, UseDefaultIID());
				if (srcPano != nil)
					srcZoom = srcPano->GetXScaleFactor(kTrue);
			}
		}

		bool openedWindow = false;
		FindPresentation_PreferCriteria noPreference;
		IDocumentPresentation* pres = Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, KBSAcceptAnyPresentation, noPreference);
		if (pres != nil)
		{
			pres->MakeActive();
		}
		else
		{
			// No window yet: open the document's first layout window (which also makes it active).
			InterfacePtr<ICommand> openWinCmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
			if (openWinCmd == nil)
				return false;
			openWinCmd->SetItemList(UIDList(docRef));
			if (srcZoom > 0.0)
			{
				InterfacePtr<IOpenLayoutPresentationCmdData> openData(openWinCmd, UseDefaultIID());
				if (openData != nil)
					openData->SetPerspective_(srcZoom, srcZoom, PMPoint(0, 0), ILayoutControlData::kFitNone);
			}
			if (CmdUtils::ProcessCommand(openWinCmd) != kSuccess)
			{
				ErrorUtils::PMSetGlobalErrorCode(kSuccess);
				return false;
			}
			openedWindow = true;
		}

		// Verify the switch took.
		front = Utils<ILayoutUIUtils>()->GetFrontDocument();
		if (front == nil || ::GetUIDRef(front) != docRef)
			return false;

		// Hand an ALREADY-OPEN incoming view the outgoing view's zoom (a freshly opened window got
		// it in the open command above). MakeZoomCmd works on any document's view.
		if (srcZoom > 0.0 && !openedWindow)
		{
			InterfacePtr<IControlView> destView(Utils<ILayoutUIUtils>()->QueryFrontView());
			if (destView != nil)
			{
				InterfacePtr<IPanorama> destPano(destView, UseDefaultIID());
				if (destPano != nil)
				{
					PMReal diff = destPano->GetXScaleFactor(kTrue) - srcZoom;
					if (diff < PMReal(0.0))
						diff = -diff;
					if (diff > PMReal(0.001))
					{
						InterfacePtr<ICommand> zoomCmd(Utils<ILayoutUIUtils>()->MakeZoomCmd(destView, srcZoom));
						if (zoomCmd != nil && CmdUtils::ProcessCommand(zoomCmd) != kSuccess)
							ErrorUtils::PMSetGlobalErrorCode(kSuccess);
					}
				}
			}
		}
		return true;
	}

} // anonymous namespace

//----------------------------------------------------------------------------------------
// Public entry points
//----------------------------------------------------------------------------------------

bool KBSJump::IsHidePreviousChapterOn()
{
	return gHidePrevChapterOn;
}

void KBSJump::ToggleHidePreviousChapter()
{
	gHidePrevChapterOn = !gHidePrevChapterOn;
}

void KBSJump::JumpToHit(int32 chapterIdx, int32 hitIdx)
{
	UIDRef docRef;
	IDFile file;
	UID storyUID = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	if (!KBSResultModel::GetHitLocation(chapterIdx, hitIdx, docRef, file, storyUID, start, end))
		return;

	// The chapter may have been closed since the search (the user can close a held window). Bring
	// it back windowless by file - the stored story UID / positions are persistent in the file, so
	// the reopened document answers them - and rebind the model so a later jump uses the live DB.
	if (!KBSBookScope::IsDocStillOpen(docRef))
	{
		UIDRef reopened;
		if (!KBSBookScope::ReopenChapterDoc(file, reopened))
			return;	// missing / locked: report nothing, do not move
		docRef = reopened;
		KBSResultModel::RebindChapterDoc(chapterIdx, reopened);
	}

	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return;
	const UIDRef storyRef(db, storyUID);

	// The overset test and marker geometry recompose text on demand - read-only work that must not
	// leave a (possibly windowless) chapter modified, or closing it later would want a save.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// Is the text at this position still the text this row describes? The stored position is an
	// offset into the story, so ANY edit earlier in that story moves it - and that is exactly the
	// case where scrolling here and drawing a marker would frame text the user never searched for.
	//
	// The story and position arms of the test are trivially satisfied here (we are asking ABOUT the
	// stored position, and this side has replaced nothing, so the delta is zero). What does the work
	// is the text.
	PMString storedLocator, storedPre, storedMatch, storedPost;
	KBSResultModel::GetHitDisplay(chapterIdx, hitIdx, storedLocator, storedPre, storedMatch, storedPost);
	const bool sameOccurrence = KBSSearchEngine::MatchIsSameOccurrence(
		storyRef, start, end, storyUID, start, storedMatch, 0);

	const bool overset = IsTextIndexOverset(storyRef, start);

	// A match in another document needs that document's window in front before any scrolling; if no
	// window can be produced, report the match without moving the view.
	if (!EnsureDocFrontmost(docRef))
	{
		KBSDrawEventHandler::ClearMarker();
		return;
	}

	// The tour has moved: with "Hide Previous Chapter" ON, every other displayed clean document is
	// closed again (scheduled). The landed-in document is the exception.
	if (gHidePrevChapterOn)
		KBSBookScope::CloseDisplayedDocsIfClean(docRef);

	// A visible match scrolls to its wax rectangle AND gets a red marker rectangle. An overset match
	// has no wax line, so it scrolls to the red "+" overset locator (KBSFindOversetLocator, which
	// also climbs out of a pushed-out table to the main frame's "+") but is NOT marked - the pixels
	// there belong to the "+" indicator, not the text, so a rectangle would only clutter it. If no
	// geometry can be produced, just clear.
	if (overset)
	{
		const KBSOversetLoc loc = KBSFindOversetLocator(storyRef, start);
		if (loc.found)
			ScrollFrontViewToPoint(loc.outportPb);	// scroll only - no marker on the "+" locator
		KBSDrawEventHandler::ClearMarker();
	}
	else
	{
		PMRect pbRect;
		if (GetFirstChunkPasteboardRect(storyRef, start, end, pbRect))
		{
			ScrollFrontViewToPoint(PBPMPoint(
				(pbRect.Left() + pbRect.Right()) / PMReal(2.0),
				(pbRect.Top() + pbRect.Bottom()) / PMReal(2.0)));
			// The marker goes up either way, and in the same colour (user call, 2026-07-28). On a row
			// whose text is missing it frames whatever stands at that position now rather than the
			// match - which is the useful thing: it shows WHERE the hit used to be. That it is not
			// there any more is said by the status line and by the word on the row itself.
			KBSDrawEventHandler::SetMarker(db, pbRect);
		}
		else
		{
			KBSDrawEventHandler::ClearMarker();
		}
	}

	// A row whose text has changed underneath says so from here on, and loses its check box, so
	// the panel stops offering a replacement that would be refused anyway. The tree's SHAPE is
	// untouched - same chapters, same rows - so the rows are repainted rather than rebuilt.
	if (!sameOccurrence)
	{
		// A REPLACED row is a different case, and one the row itself cannot show: SetHitOutcome
		// turns those away (a row that was replaced had nothing go wrong with it), so marking it
		// would change nothing on screen while the status line announced a problem - the panel
		// saying two things at once. What the mismatch means there is also different: the row's
		// match text is what the REPLACE wrote, so finding something else in its place means the
		// replacement is gone, undone or edited away, not that the search's text has moved.
		bool checked = false, replaced = false, locked = false;
		KBSResultModel::GetHitFlags(chapterIdx, hitIdx, checked, replaced, locked);

		PMString message;
		message.SetTranslatable(kFalse);
		if (replaced)
		{
			message.Append("The replacement is no longer here - undone, or edited since.");
		}
		else
		{
			KBSResultModel::SetHitOutcome(chapterIdx, hitIdx, KBSResultModel::kOutcomeMissing);
			KBSResultTree::RefreshRows();
			message.Append("Not found - the text is no longer where the search left it. Search again.");
		}
		KBSResultTree::ShowStatus(message);
	}
}

// End, KBSJump.cpp.
