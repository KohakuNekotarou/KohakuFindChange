//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Jump-to-hit navigation implementation. The move-to-location helpers (scroll, marker rectangle,
//  bring-document-frontmost with zoom carry) are ported from KESCL's KESCLFindInDoc (KESCL left
//  untouched); the driver JumpToHit is simplified to a static snapshot: it reads the stored
//  (docRef, file, story, range) for one hit and goes there, with no match-list navigation,
//  edit-repair or reverse mode. Whether a position is overset is asked of KBSSearchEngine, which
//  resolved every hit's frame in the first place. Once a hit's database is in hand, everything that
//  follows happens inside a SaveRestoreModifiedState dirty guard - composing, opening a window,
//  changing spread - so a (possibly windowless) chapter never comes out modified.
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
#include "IHierarchy.h"				// the match's frame as a page item - which spread is it on?
#include "ILayoutCmdData.h"			// kSetSpreadCmdBoss carries the view it is addressing
#include "ILayoutControlData.h"		// kFitNone; GetSpreadRef - which spread the view is showing
#include "ILayoutUIUtils.h"
#include "IPasteboardUtils.h"		// QuerySpread - the spread containing a page item
#include "ISpread.h"
#include "IOpenLayoutCmdData.h"		// SetPerspective_ - the inherited zoom rides the open command
#include "IPageList.h"
#include "IPanorama.h"
#include "ITextModel.h"
#include "IWaxStrand.h"
#include "IWaxIterator.h"
#include "IWaxLine.h"
#include "IWaxRun.h"
#include "IWaxGlyphs.h"
#include "ISession.h"

// General includes:
#include "TextID.h"					// kFrameListBoss, IID_IWAXSTRAND
#include "widgetid.h"				// IID_IPANORAMA
#include "LayoutUIID.h"				// kOpenLayoutCmdBoss
#include "SpreadID.h"				// kSetSpreadCmdBoss - put a spread in the layout view
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
#include "KBSSearchEngine.h"		// MatchIsSameOccurrence (the jump is its only caller since
									// 2026-08-05) / EditableFrameForMatch / IsPositionOverset
#include "KBSResultTree.h"			// RefreshRows / ShowStatus - telling the panel what was found here

namespace
{
	// The "Hide Previous Chapter" flyout toggle (session state only; starts ON - a book search
	// leaves the desk clean, showing only the chapter a jump landed in). The sweep it gates
	// (CloseDisplayedDocsIfClean) is stateless, so flipping it mid-session is safe.
	bool gHidePrevChapterOn = true;

	/** May the "close everything else" sweep run for the results now on the panel?

	    TWO conditions, and the second one used to be missing. The toggle says whether the user wants
	    it; IsFromBook says whether it means anything - the sweep is about CHAPTERS, and a
	    document-scope result set has none.

	    Without the second test a document-scope jump closed every other clean document the user had
	    open, and there was no way to stop it: the menu item greys itself out in document scope
	    (KBSActionComponent::UpdateActionStates says so in as many words), so the toggle could not
	    even be reached to be turned off (found 2026-08-03 in the defect audit). The menu and the
	    behaviour now answer the same question.

	    Asked of the RESULTS rather than of the live Book Scope toggle, for the reason the model
	    records that flag at all: flipping the scope after a search must not change how the results
	    already on screen behave. */
	bool ShouldHidePreviousChapter()
	{
		return gHidePrevChapterOn && KBSResultModel::IsFromBook();
	}

	//------------------------------------------------------------------------------------
	// Move-to-location helpers (ported from KESCLFindInDoc)
	//------------------------------------------------------------------------------------

	// Scroll the front layout view so the given pasteboard point is centred. Does not select.
	//
	// ScrollContentLocationToFrameCenter, not ScrollViewCenterTo: IPanorama.h:141-145 calls the
	// latter "an obsolete name" for this one and says new code should call this, "but this function
	// will go away in a future release". The old name is an inline that calls the new one
	// (IPanorama.h:135-138), so nothing about the behaviour changes.
	void ScrollFrontViewToPoint(const PBPMPoint& pbPoint)
	{
		InterfacePtr<IControlView> view(Utils<ILayoutUIUtils>()->QueryFrontView());
		if (view == nil)
			return;
		InterfacePtr<IPanorama> pano(view, UseDefaultIID());
		if (pano == nil)
			return;
		pano->ScrollContentLocationToFrameCenter(pbPoint, kTrue /*forceRedraw*/);
	}

	/** Bring this story's composition up to date, so that what is read below is the CURRENT
	    composition rather than the one left over from before the last edit.

	    ***** IT COVERS THE OVERSET TEST AS WELL AS THE GEOMETRY. ***** Both are readings of the
	    RESULT of composition, and the recompose used to sit inside the geometry helper alone - so
	    the question "is this position overset" was answered from the old composition and the
	    rectangle was measured from the new one, in that order, inside one jump. Being overset is
	    exactly what changes when text is recomposed, so that was the wrong way round.

	    The recipe is the SDK's: IFrameList::GetFirstDamagedFrameIndex() != -1 ->
	    IFrameListComposer::RecomposeThruLastFrame (SnpInspectTextModel.cpp:724-733). Called inside
	    the caller's SaveRestoreModifiedState guard, because composing dirties the document. */
	void RecomposeIfDamaged(const UIDRef& storyRef)
	{
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			return;

		InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)textModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
		if (waxStrand == nil)
			return;

		InterfacePtr<IFrameList> frameList(waxStrand, UseDefaultIID());
		if (frameList != nil && frameList->GetFirstDamagedFrameIndex() != -1)
		{
			InterfacePtr<IFrameListComposer> composer(frameList, UseDefaultIID());
			if (composer != nil)
				composer->RecomposeThruLastFrame();
		}
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
	//
	// Assumes the caller has already run RecomposeIfDamaged on this story - the wax read below is a
	// reading of the composition, and so is the overset test the caller made before choosing to come
	// here, so both have to be looking at the same one.
	bool GetFirstChunkPasteboardRect(const UIDRef& storyRef, TextIndex start, TextIndex end, PMRect& outRect)
	{
		InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
		if (textModel == nil)
			return false;

		InterfacePtr<IWaxStrand> waxStrand((IWaxStrand*)textModel->QueryStrand(kFrameListBoss, IID_IWAXSTRAND));
		if (waxStrand == nil)
			return false;

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

		// How tall to make the rectangle: proportions of the line height, measured from the baseline
		// (the wax run's local y origin), which is the space mLeft / mRight map from.
		//
		// WARNING: do NOT "improve" this with IWaxLineShape::GetSelectionLine. That was tried 2026-07-31
		// and reverted the same day: its documented job is to CONSTRAIN a highlight's height so
		// adjacent lines do not overlap (IWaxLineHilite.h:53-54 calls it maxTopBottom, "used to
		// prevent double XOR problems when waxLines are too close together"), not to report this
		// line's ascent and descent, and its coordinate space is nowhere stated. There is no call
		// site for it anywhere in the SDK. Measure it on a real document before trusting it.
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

	// The spread a match sits on: its frame -> the spread containing that frame. kInvalidUID when the
	// match has no frame at all (and for the query failures around it, which read the same).
	//
	// The frame is resolved through KBSSearchEngine::EditableFrameForMatch, which is the same answer
	// the hit's own locator was built from - an overset match names the frame carrying the "+".
	UID SpreadForMatch(const UIDRef& storyRef, TextIndex pos)
	{
		const UID frameUID = KBSSearchEngine::EditableFrameForMatch(storyRef, pos);
		if (frameUID == kInvalidUID)
			return kInvalidUID;
		InterfacePtr<IHierarchy> frameHier(storyRef.GetDataBase(), frameUID, UseDefaultIID());
		if (frameHier == nil)
			return kInvalidUID;
		InterfacePtr<ISpread> spread(Utils<IPasteboardUtils>()->QuerySpread(frameHier));
		return (spread != nil) ? ::GetUID(spread) : kInvalidUID;
	}

	/** Put the layout view on the SPREAD the match sits on, before anything is scrolled.

	    ***** SCROLLING TO A POINT ASSUMES THE VIEW IS ALREADY ON THAT POINT'S SPREAD. ***** The scroll
	    below moves the view to a pasteboard POINT, and a point taken from one spread means something
	    else - or nothing at all - to a view showing another. A MASTER spread is where this shows up
	    plainly, because it is not in the ordinary spreads' continuous pasteboard at all: measured
	    2026-08-05 with Include Master Pages on, the row read "PA master cat one" correctly and
	    clicking it left the window on EMPTY PASTEBOARD - no page, no text, no marker, nothing said -
	    while the body row beside it landed correctly in the same test run.

	    ***** THE TEST IS "IS IT A DIFFERENT SPREAD", NOT "IS IT A MASTER". ***** That is the rule
	    Adobe's own code follows: SnapTracker.cpp:224 compares ::GetUIDRef(spread) against
	    ILayoutControlData::GetSpreadRef() and issues the command whenever they differ, with no special
	    case for masters anywhere. This started out master-only, on the reasoning that ordinary
	    spread-to-spread jumps had worked by scrolling for as long as the panel had existed; that is a
	    reason to TEST the ordinary case, not a reason to keep a second rule of our own beside Adobe's.

	    !! AND THE GEOMETRY MUST BE COMPUTED AFTER THIS RUNS. SnapTracker.cpp:234-235 recalculates its
	    pasteboard point the moment the spread has changed ("Re-calculate the starting point"), which
	    is the same statement from the other side: a pasteboard coordinate taken before the change
	    cannot be trusted after it. Hence the call site - ahead of KBSFindOversetLocator and
	    GetFirstChunkPasteboardRect, both of which read their coordinates fresh.

	    kSetSpreadCmdBoss with ILayoutCmdData is the command (SnapTracker.cpp:390-413 is the worked
	    example; customdatalinkui, basicdragdrop and CPathCreationTracker do the same three steps).
	    That a layout view can show a master spread at all is stated by
	    ILayoutUIUtils::GetVisibleMasterSpreadUID (ILayoutUIUtils.h:220).

	    Silent when it cannot do it: the scroll that follows is no worse off than before. */
	void EnsureSpreadInView(const UIDRef& storyRef, TextIndex pos)
	{
		const UID targetSpread = SpreadForMatch(storyRef, pos);
		if (targetSpread == kInvalidUID)
			return;

		InterfacePtr<ILayoutControlData> layout(Utils<ILayoutUIUtils>()->QueryFrontLayoutData());
		if (layout == nil)
			return;
		if (layout->GetSpreadRef().GetUID() == targetSpread)
			return;			// already looking at it - the ordinary case, and the cheapest exit

		IDocument* const viewDoc = layout->GetDocument();
		if (viewDoc == nil)
			return;

		// The spread UID was read out of the STORY's database and is about to be handed to a command
		// addressed at the VIEW's. They are the same database on every path that reaches here - the
		// caller has just brought this hit's document to the front - but a UID means nothing outside
		// the database it came from, so the two are checked rather than assumed.
		if (::GetDataBase(viewDoc) != storyRef.GetDataBase())
			return;

		InterfacePtr<ICommand> setSpreadCmd(CmdUtils::CreateCommand(kSetSpreadCmdBoss));
		if (setSpreadCmd == nil)
			return;
		InterfacePtr<ILayoutCmdData> cmdData(setSpreadCmd, UseDefaultIID());
		if (cmdData == nil)
			return;
		// The view's OWN document, exactly as the worked example takes it - this command addresses a
		// view, and the document it is showing is the one that answers for it.
		cmdData->Set(::GetUIDRef(viewDoc), layout);
		setSpreadCmd->SetItemList(UIDList(::GetDataBase(viewDoc), targetSpread));
		if (CmdUtils::ProcessCommand(setSpreadCmd) != kSuccess)
			ErrorUtils::PMSetGlobalErrorCode(kSuccess);	// the scroll still runs; do not poison later commands
	}

	/** Accepts every presentation. The twin of KBSBookScope's predicate of the same name, kept
	    separate because each file's is private to it (both sit in an anonymous namespace).

	    ***** A LOCAL PREDICATE IS WHAT ADOBE ASKS FOR HERE. ***** The stock one exists and is named
	    FindPresCriteria::accept_all (DocumentPresFindCriteria.h:82), but that file's own preamble
	    (:40-46) says its implementations "are found in the WidgetBin shared library, so you cannot
	    use them from a model only plugin. Should the need arise you can create local
	    implementations" - and prints a two-line example of exactly this shape. So this is the
	    documented route, not a stand-in for one. (The reasoning was written into the BookScope copy
	    on 2026-08-08 and not into this one, which is the sort of split this comment now closes.) */
	bool KBSAcceptAnyPresentation(IDocumentPresentation* /*p*/)
	{
		return true;
	}

	// Bring the given document's layout window to the front. A windowless chapter gets its first
	// window opened here; a background-tab window is activated. The zoom the user was looking at
	// travels along (zoom first, scroll second). Returns false when no window could be produced or
	// the activation did not take (the caller then reports without scrolling).
	//
	// ***** On success the chapter STOPS BEING HELD (KBSBookScope::ForgetHeldDoc). ***** See the
	// note beside that call at the foot of this function.
	bool EnsureDocFrontmost(const UIDRef& docRef)
	{
		IDocument* front = Utils<ILayoutUIUtils>()->GetFrontDocument();
		if (front != nil && ::GetUIDRef(front) == docRef)
		{
			KBSBookScope::ForgetHeldDoc(docRef);	// already in front and visible - see below
			return true;
		}

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

		// ***** THE CHAPTER IS THE USER'S FROM HERE ON: STOP HOLDING IT. *****
		//
		// A jump reaches a closed chapter by reopening it WINDOWLESS (EnsureChapterReachable ->
		// ReopenChapterDoc), which puts it on the held list - the list of chapters a run is entitled
		// to hand back by closing them, with the UI suppressed. It has a window now and the user is
		// looking at it, so that entitlement is over: the next run would otherwise close a window
		// they are working in, and take with it whatever they have typed or replaced into it since
		// (user, 2026-08-03: "a document the user opened by jumping should not be closed, even if
		// nothing was replaced in it").
		//
		// KBSBookScope::ShowChapterWindow has always said this about the window IT opens after a
		// replace. This is the same statement about the window a JUMP opens - the case that was
		// missing, and the one that reaches the user first.
		KBSBookScope::ForgetHeldDoc(docRef);
		return true;
	}

	// Make a chapter reachable: reopen it windowless if the user closed it since the search, and
	// rebind the model so later work uses the live database. Shared by JumpToHit and ShowChapter -
	// the two differ in what they do AFTER this, not in how they get there.
	//
	// Returns false when the chapter cannot be reached at all; the caller has already been told
	// why through the status line, so it should just return.
	bool EnsureChapterReachable(int32 chapterIdx, UIDRef& ioDocRef, const IDFile& file)
	{
		// ***** BY FILE FIRST, never gated on IsDocStillOpen. ***** That question is asked of a
		// docRef whose document may have been closed since the search, and a UIDRef is only
		// (IDataBase*, UID) - once the address is reused by a document opened afterwards, and the
		// UID lands the same, it answers YES about a DIFFERENT DOCUMENT. ReopenChapterDoc asks by
		// FILE instead: it hands back the open document living in this chapter's .indd, or opens
		// it. See the longer note at the same change in KBSReplaceEngine's resolve pass.
		UIDRef reopened;
		if (KBSBookScope::ReopenChapterDoc(file, reopened))
		{
			ioDocRef = reopened;
			KBSResultModel::RebindChapterDoc(chapterIdx, reopened);
			return true;
		}

		// ***** TWO different failures, and only ONE of them may fall back. ***** No file to open BY
		// is normal - a DOCUMENT-scope row is the front document and carries none - and the old
		// question is safe for it: that docRef IS the live front document, with nothing closed
		// behind it. A file that would NOT open is the other case, and there the docRef is the one
		// the search left behind, whose document was closed when the search finished - the exact
		// thing the note above says must not be asked about. Say "cannot be reached" instead.
		if (!KBSBookScope::ChapterHasFile(file) && KBSBookScope::IsDocStillOpen(ioDocRef))
			return true;

		// Nothing can be reached, so nothing moves - and that has to be SAID. A row that does
		// nothing at all when clicked reads as a broken panel: the file has been moved, deleted,
		// renamed, or is open in another application.
		PMString message("Cannot open that chapter - moved, deleted, or in use?");
		message.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(message);
		return false;
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

void KBSJump::SetHidePreviousChapter(bool on)
{
	// Added for the saved settings (KBSPanelState.cpp), which has to write a REMEMBERED value rather
	// than flip whatever the flag happens to be. Toggling from a restore would come out inverted
	// whenever the default is not what was saved.
	gHidePrevChapterOn = on;
}

void KBSJump::JumpToHit(int32 chapterIdx, int32 hitIdx)
{
	UIDRef docRef;
	IDFile file;
	UID storyUID = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	// ***** A JUMP THAT GOES NOWHERE TAKES THE OLD MARKER WITH IT. ***** Every exit below that does
	// not move the view clears it, and these two used to be the exceptions - leaving the previous
	// hit's marker standing over a row that had just refused to go anywhere. It expires by itself
	// within the second either way; what is being made consistent is what the panel is SAYING.
	if (!KBSResultModel::GetHitLocation(chapterIdx, hitIdx, docRef, file, storyUID, start, end))
	{
		KBSDrawEventHandler::ClearMarker();
		return;
	}

	// The chapter may have been closed since the search (the user can close a held window). Bring
	// it back windowless by file - see EnsureChapterReachable, which ShowChapter shares.
	if (!EnsureChapterReachable(chapterIdx, docRef, file))
	{
		KBSDrawEventHandler::ClearMarker();	// it has already said why through the status line
		return;
	}

	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
	{
		KBSDrawEventHandler::ClearMarker();
		return;
	}
	const UIDRef storyRef(db, storyUID);

	// Everything from here on happens inside the dirty guard. Recomposing text dirties a document,
	// and so - harmlessly but visibly - can opening a window on it or changing which spread it
	// shows; none of that is the user's edit, and a chapter this plug-in opened windowless must not
	// come out wanting to be saved. IDataBase.h:389-412: this does not "keep it clean", it puts back
	// the flag the document had on the way in.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// Compose first, then read. The overset test below, the overset locator and the wax rectangle are
	// all readings of the RESULT of composition. The recompose used to live inside the geometry
	// helper alone, which put it AFTER the overset test had already been answered - so the two could
	// be looking at different compositions, and being overset is precisely what recomposing changes.
	RecomposeIfDamaged(storyRef);

	// Is the text at this position still the text this row describes? The stored position is an
	// offset into the story, so ANY edit earlier in that story moves it - and that is exactly the
	// case where scrolling here and drawing a marker would frame text the user never searched for.
	//
	// The story and position arms of the test are trivially satisfied here - we are asking ABOUT the
	// stored position. What does the work is the text.
	//
	// ***** Asked only of a row whose match IS story text. ***** An overset finding carries the
	// scan's own words there - "Frame (370)" - so the comparison could never agree, and every click
	// on one was answering a jump that had just landed correctly with "Not found - the text is no
	// longer where the search left it" and stamping the row 'missing' (2026-08-02).
	// The stored HASH, not the drawn text: the row's match string is capped at 500 characters, so
	// asking it about a long GREP match only ever compared the first 500 (2026-08-04). The story,
	// position and length arms are all trivially satisfied here - this side asks about the very
	// range the row recorded - so what does the work is the hash.
	UID expectStory = kInvalidUID;
	TextIndex expectStart = kInvalidTextIndex, expectEnd = kInvalidTextIndex;
	uint64 expectHash = 0;
	KBSResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, expectStory, expectStart, expectEnd,
		expectHash);
	const bool sameOccurrence = !KBSResultModel::MatchTextIsLiveText()
		|| KBSSearchEngine::MatchIsSameOccurrence(
			storyRef, start, end, storyUID, start, end, expectHash);

	// Asked of the search engine, which is where every hit's frame was resolved in the first place
	// (KBSSearchEngine::IsPositionOverset -> the same position-to-parcel-to-frame walk BuildHit
	// used). This file wrote that walk out by hand until the block 12 API audit, 2026-08-08, and its
	// copy answered "not overset" to the failures the original folds into "no frame of its own".
	const bool overset = KBSSearchEngine::IsPositionOverset(storyRef, start);

	// A match in another document needs that document's window in front before any scrolling; if no
	// window can be produced, report the match without moving the view.
	if (!EnsureDocFrontmost(docRef))
	{
		// Same rule as the failed reopen above: the view did not move, so the panel says why rather
		// than leaving a click that appears to do nothing.
		KBSDrawEventHandler::ClearMarker();
		PMString message("Cannot bring that chapter's window to the front.");
		message.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(message);
		return;
	}

	// The tour has moved: with "Hide Previous Chapter" ON, every other displayed clean document is
	// closed again (scheduled). The landed-in document is the exception. Book results only - see
	// ShouldHidePreviousChapter.
	if (ShouldHidePreviousChapter())
		KBSBookScope::CloseDisplayedDocsIfClean(docRef);

	// The window is the right one; make sure it is showing the right SPREAD before anything is
	// scrolled - every pasteboard coordinate read below is taken AFTER this, deliberately. See
	// EnsureSpreadInView, and the empty pasteboard a master-page row used to land on.
	EnsureSpreadInView(storyRef, start);

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
	else if (!KBSResultModel::MatchTextIsLiveText() && !overset)
	{
		// ***** An OVERSET row whose place is not overset any anymore. *****
		//
		// An overset finding is a statement about the document as it was AT SCAN TIME, and this is the
		// one row that can go silently stale: the test above cannot speak for it - its match segment
		// holds the scan's own words ("Frame (370)"), so the comparison is short-circuited - and the
		// jump itself works perfectly, scrolling to whatever now stands at that position. The result
		// was a click that moved the view and said nothing at all, over a row that no longer describes
		// anything (the text was made to fit, or an edit moved this position into placed text).
		//
		// The ROW is left alone on purpose. 'missing' means "the text is not where the search left
		// it", which is not what happened here, and there is nothing to fix on a row whose whole
		// content is a measurement - the answer is to scan again, which is what this says.
		PMString message("No longer overset here - this row is out of date. Run Find Overset again.");
		message.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(message);
	}
}

void KBSJump::ShowChapter(int32 chapterIdx)
{
	UIDRef docRef;
	IDFile file;
	if (!KBSResultModel::GetChapterLocation(chapterIdx, docRef, file))
		return;

	if (!EnsureChapterReachable(chapterIdx, docRef, file))
		return;

	// Showing a chapter is NOT jumping to a match: the view is left exactly where the user had it
	// and no marker is raised. The row says "this document", so the answer is that document, not a
	// place inside it. (KESCL's document rows behave the same way.)
	if (!EnsureDocFrontmost(docRef))
	{
		PMString message("Cannot bring that chapter's window to the front.");
		message.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(message);
		return;
	}

	// Same sweep a jump does: with "Hide Previous Chapter" ON the desk is left showing only where
	// we landed. The landed-in document is the exception. Book results only - see
	// ShouldHidePreviousChapter.
	if (ShouldHidePreviousChapter())
		KBSBookScope::CloseDisplayedDocsIfClean(docRef);
}

void KBSJump::ShowBook()
{
	// Which book the results came from. The SEARCHED PATH, not the model's display name: that name
	// is the file name only, and two books in different folders can share one.
	PMString bookPath;
	if (!KBSBookScope::GetSearchedBookPath(bookPath) || bookPath.IsEmpty())
		return;		// a document-scope result has no book row to click in the first place

	// A book closed since the search is NOT reopened. The row records which book was SEARCHED; it
	// is not a request to open a file. Saying so beats a row that appears to do nothing.
	if (!KBSBookScope::ActivateBook(bookPath))
	{
		PMString message("That book is no longer open.");
		message.SetTranslatable(kFalse);
		KBSResultTree::ShowStatus(message);
	}
}

void KBSJump::ActivateNode(int32 chapterIdx, int32 hitIdx)
{
	// One door for every row, so a click and a keyboard walk can never drift apart - the reason
	// this exists at all is that there are now two callers.
	if (hitIdx >= 0)
		JumpToHit(chapterIdx, hitIdx);
	else if (chapterIdx >= 0)
		ShowChapter(chapterIdx);
	else if (chapterIdx == -1)
		ShowBook();
}

// End, KBSJump.cpp.
