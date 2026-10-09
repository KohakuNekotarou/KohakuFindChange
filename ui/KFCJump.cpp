//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Jump-to-hit navigation implementation. The move-to-location helpers (scroll, the rectangle the
//  view is centred on, bring-document-frontmost with zoom carry) are ported from KESCL's
//  KESCLFindInDoc (KESCL left untouched); the driver JumpToHit is simplified to a static snapshot: it
//  reads the stored (docRef, file, story, range) for one hit and goes there, with no match-list
//  navigation, edit-repair or reverse mode. Whether a position is overset is asked of KFCSearchEngine
//  (through IKFCRuns), which resolved every hit's frame in the first place. Once a hit's database is
//  in hand, everything that follows happens inside a SaveRestoreModifiedState dirty guard -
//  composing, opening a window, changing spread - so a (possibly windowless) chapter never comes out
//  modified.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IControlView.h"
#include "IDocument.h"
#include "IDocumentPresentation.h"	// MakeActive; what the layout predicate is handed
#include "IDocumentUIUtils.h"		// FindPresentationForDocument
#include "IFrameList.h"
#include "IFrameListComposer.h"
#include "IHierarchy.h"				// the match's frame as a page item - which spread is it on?
#include "ILayoutCmdData.h"			// kSetSpreadCmdBoss carries the view it is addressing
#include "ILayoutControlData.h"		// kFitNone; GetSpreadRef - which spread the view is showing
#include "ILayoutUIUtils.h"
#include "IPasteboardUtils.h"		// QuerySpread - the spread containing a page item
#include "ISpread.h"
#include "IOpenLayoutCmdData.h"		// SetPerspective_ - the inherited zoom rides the open command
#include "IPanorama.h"
#include "ISelectionManager.h"		// DeselectAll / SelectionExists - clearing before selecting
#include "ISelectionUtils.h"		// GetActiveSelection - the active context's selection (the document the jump fronted)
#include "ITextModel.h"
#include "ITextSelectionSuite.h"	// SetTextSelection - the double-click's whole point
#include "ITool.h"					// IsToolOfType(kTextSelectionTool) - is a text tool already active?
#include "IToolBoxUtils.h"			// QueryActiveTool / QueryTool / SetActiveTool
#include "IWaxStrand.h"
#include "IWaxIterator.h"
#include "IWaxLine.h"
#include "IWaxRun.h"
#include "IWaxGlyphs.h"

// General includes:
#include "RangeData.h"				// the range handed to SetTextSelection
#include "TextEditorID.h"			// kIBeamToolBoss - the Type tool the double-click switches to
#include "TextID.h"					// kFrameListBoss, IID_IWAXSTRAND
#include "widgetid.h"				// IID_IPANORAMA
#include "LayoutUIID.h"				// kOpenLayoutCmdBoss
#include "SpreadID.h"				// kSetSpreadCmdBoss - put a spread in the layout view
#include "ErrorUtils.h"				// GlobalErrorStatePreserver / PMSetGlobalErrorCode - a window or a
									// spread change can raise an error state that is not this run's
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
#include "KFCJump.h"
#include "KFCHitMarkerView.h"		// the marker shown and taken down - the UI half of KFCHitMarker
#include "KFCModelAccess.h"		// the model half, through its session interfaces (the model/UI split)
#include "KFCBookPanelLookup.h"		// BringBookTabForward - a book row's tab
#include "KFCResultTree.h"			// RefreshRows / ShowStatus - telling the panel what was found here
#include "KFCDiag.h"				// KFC_DIAG_LOG / KFC_DIAG_FAULT - test builds only
#include <chrono>					// how soon after a click Search: changed (KeepSearchScopeAfterClick)
#include <vector>

namespace
{
	// The "Hide Previous Chapter" flyout toggle (session state only; starts ON - a book search
	// leaves the desk clean, showing only the chapter a jump landed in). The sweep it gates
	// (CloseDisplayedDocsIfClean) is stateless, so flipping it mid-session is safe.
	bool gHidePrevChapterOn = true;

	/** May the "close everything else" sweep run for the results now on the panel?

	    TWO conditions. The toggle says whether the user wants it; IsFromBook says whether it means
	    anything - the sweep is about CHAPTERS, and a document-scope result set has none.

	    Without the second test a document-scope jump would close every other clean document the user
	    had open, with no way to stop it: the menu item greys itself out in document scope
	    (KFCActionComponent::UpdateActionStates says so in as many words), so the toggle could not
	    even be reached to be turned off. The menu and the behaviour answer the same question.

	    The same lock-out has a second door: a menu asking the LIVE Book Scope toggle alone would sit
	    grey over book results with the scope since switched off, while the sweep ran on every jump.
	    So the menu asks IsFromBook too - the very question below - and wherever the sweep can run,
	    the toggle can be reached.

	    Asked of the RESULTS rather than of the live Book Scope toggle, for the reason the model
	    records that flag at all: flipping the scope after a search must not change how the results
	    already on screen behave. */
	bool ShouldHidePreviousChapter()
	{
		return gHidePrevChapterOn && KFCResults()->IsFromBook();
	}

	//------------------------------------------------------------------------------------
	// Move-to-location helpers (ported from KESCLFindInDoc)
	//------------------------------------------------------------------------------------

	// Scroll the GIVEN layout view so the given pasteboard point is centred. Does not select.
	//
	// THE VIEW IS PASSED IN, NOT LOOKED UP HERE: one view, looked up ONCE by the caller and handed to
	// both this and EnsureSpreadInView, because the two lookups are NOT THE SAME QUESTION:
	//
	//   QueryFrontView / GetFrontDocument -> "the frontmost LAYOUT presentation"  (ILayoutUIUtils.h:89-98)
	//   QueryFrontLayoutData              -> "the FRONT MOST presentation"'s layout part (:127-133)
	//
	// With a Story Editor window in front of its own document's layout window, the second one is nil
	// while the first still hands back the layout view behind it - so asked separately, the spread
	// would never be changed and this scroll would run anyway, landing on empty pasteboard for any hit
	// on another spread (measured on the running application: the active spread stayed put while the
	// jump reported nothing wrong).
	//
	// ScrollContentLocationToFrameCenter, not ScrollViewCenterTo: IPanorama.h:141-145 calls the
	// latter "an obsolete name" for this one and says new code should call this, "but this function
	// will go away in a future release".
	//
	// The two reach the same code: the NEW name is the inline, and it calls the OLD one, which is the
	// pure virtual (IPanorama.h:135-138 vs :145). Calling the new name is right because that is where
	// Adobe will keep the entry point, not because it is the one with an implementation behind it
	// today.
	void ScrollViewToPoint(IControlView* view, const PBPMPoint& pbPoint)
	{
		if (view == nil)
			return;
		InterfacePtr<IPanorama> pano(view, UseDefaultIID());
		if (pano == nil)
			return;
		pano->ScrollContentLocationToFrameCenter(pbPoint, kTrue /*forceRedraw*/);
	}

	/** Bring this story's composition up to date, so that what is read below is the CURRENT
	    composition rather than the one left over from before the last edit.

	    IT COVERS THE OVERSET TEST AS WELL AS THE GEOMETRY. Both are readings of the RESULT of
	    composition; a recompose inside the geometry helper alone would answer "is this position
	    overset" from the old composition and measure the rectangle from the new one, inside one jump -
	    and being overset is exactly what changes when text is recomposed.

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
		// CAREFUL with IWaxLineShape::GetSelectionLine here. It was tried and reverted. What the
		// header says, both sentences (IWaxLineShape.h:142-148):
		//   1. "Get the selection line (top/bottom) for this line" - so it DOES report a top and a
		//      bottom;
		//   2. "This is typically used to determine the constraints on the height of the highlight
		//      for this waxLine" - so constraining a highlight IS its stated typical use (the value
		//      also travels on as maxTopBottom - IWaxRunShape.h:124).
		// Neither sentence rules the function out here.
		//
		// What still stands, and is why the proportions below are kept: the returned PMLineSeg's
		// COORDINATE SPACE is nowhere stated, and the rectangle here is assembled in a wax RUN's local
		// space through that run's to-pasteboard matrix. There is no call site for GetSelectionLine
		// anywhere in the SDK to settle it from. Measure it on a real document before trusting it.
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
	// The frame is resolved through KFCSearchEngine::EditableFrameForMatch, which is the same answer
	// the hit's own locator was built from - an overset match names the frame carrying the "+".
	UID SpreadForMatch(const UIDRef& storyRef, TextIndex pos)
	{
		const UID frameUID = KFCRuns()->EditableFrameForMatch(storyRef, pos);
		if (frameUID == kInvalidUID)
			return kInvalidUID;
		InterfacePtr<IHierarchy> frameHier(storyRef.GetDataBase(), frameUID, UseDefaultIID());
		if (frameHier == nil)
			return kInvalidUID;
		InterfacePtr<ISpread> spread(Utils<IPasteboardUtils>()->QuerySpread(frameHier));
		return (spread != nil) ? ::GetUID(spread) : kInvalidUID;
	}

	/** Put the layout view on the SPREAD the match sits on, before anything is scrolled.

	    SCROLLING TO A POINT ASSUMES THE VIEW IS ALREADY ON THAT POINT'S SPREAD. The scroll below
	    moves the view to a pasteboard POINT, and a point taken from one spread means something else -
	    or nothing at all - to a view showing another. A MASTER spread is where this shows up plainly,
	    because it is not in the ordinary spreads' continuous pasteboard at all: measured with Include
	    Master Pages on and the scroll alone, the row read "PA master cat one" correctly and clicking
	    it left the window on EMPTY PASTEBOARD - no page, no text, no marker, nothing said - while the
	    body row beside it landed correctly in the same test run.

	    THE TEST IS "IS IT A DIFFERENT SPREAD", NOT "IS IT A MASTER". That is the rule Adobe's own code
	    follows: SnapTracker.cpp:224 compares ::GetUIDRef(spread) against
	    ILayoutControlData::GetSpreadRef() and issues the command whenever they differ, with no special
	    case for masters anywhere. That ordinary spread-to-spread jumps work by scrolling alone is a
	    reason to TEST the ordinary case, not a reason to keep a second rule of our own beside Adobe's.

	    !! AND THE GEOMETRY MUST BE COMPUTED AFTER THIS RUNS. SnapTracker.cpp:234-235 recalculates its
	    pasteboard point the moment the spread has changed ("Re-calculate the starting point"), which
	    is the same statement from the other side: a pasteboard coordinate taken before the change
	    cannot be trusted after it. Hence the call site - ahead of KFCFindOversetLocator and
	    GetFirstChunkPasteboardRect, both of which read their coordinates fresh.

	    kSetSpreadCmdBoss with ILayoutCmdData is the command (SnapTracker.cpp:390-413 is the worked
	    example; customdatalinkui, basicdragdrop and CPathCreationTracker do the same three steps).
	    That a layout view can show a master spread at all is stated by
	    ILayoutUIUtils::GetVisibleMasterSpreadUID (ILayoutUIUtils.h:220).

	    THE VIEW IS THE ONE THAT WILL BE SCROLLED. It is handed in rather than looked up: looked up
	    here it would be QueryFrontLayoutData beside the scroll's QueryFrontView - two different
	    questions (see ScrollViewToPoint above for the contract lines and for what a Story Editor
	    window does with the difference). Asking the view we are about to scroll is the only way the
	    two can never disagree.

	    Silent when it cannot do it: the scroll that follows is no worse off than before. */
	void EnsureSpreadInView(IControlView* view, const UIDRef& storyRef, TextIndex pos)
	{
		const UID targetSpread = SpreadForMatch(storyRef, pos);
		if (targetSpread == kInvalidUID)
			return;

		InterfacePtr<ILayoutControlData> layout(view, IID_ILAYOUTCONTROLDATA);
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

		// PRESERVE, THEN CLEAR - the caller's error state goes back the way it came. Clearing the
		// CALLER's error state is not this function's to decide, and every exit below - not only the
		// last - has to hand back what it found (a bare clear after the command would not). The pair
		// is Adobe's own (CDialogObserver.cpp:392-394; contract at
		// ErrorUtils.h:115-137). The clear is the other half: a standing error fails whatever is
		// attempted next, and what is attempted next is this very command.
		GlobalErrorStatePreserver spreadErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

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
		CmdUtils::ProcessCommand(setSpreadCmd);		// the scroll still runs either way
	}

	/** Accepts a presentation that HAS A LAYOUT IN IT, and no other.

	    A DOCUMENT'S WINDOWS ARE NOT ALL LAYOUT WINDOWS. A Story Editor window is a presentation of
	    the same document (kStoryEditorPresentationBoss, WritingModeUIID2.h:117), and a predicate that
	    accepts EVERYTHING lets a jump make that window active in a document being edited in one -
	    after which every single thing the jump does next (scroll, spread, marker) is addressed at a
	    LAYOUT view that was never brought forward.
	    Adobe's own worked examples for this search order their candidates with prefer-criteria such
	    as is_layout (DocumentPresFindCriteria.h:54-58); this asks the same question in the accept
	    half, where a "no" is the useful answer - no layout presentation means the else branch below
	    opens one, which is exactly right.

	    THE TEST IS THE SDK'S OWN PREDICATE FOR IT. ILayoutUIUtils::IsLayoutPresentation,
	    "Test to see if the given presentation contains ILayoutControlData" (ILayoutUIUtils.h:112-115)
	    - which is this function's whole question, under that name. (Not a ClassID comparison,
	    deliberately: naming a boss means naming all of them - kLayoutPresentationBoss,
	    kWasmLayoutPresentationBoss, whatever comes next.)

	    WHY NOT THE STOCK FindPresCriteria::is_layout (DocumentPresFindCriteria.h:86). Its
	    implementation is in WidgetBin, and the header (:40-46) tells a model plug-in to write a local
	    one of exactly this shape. This is the UI half, where the stock one links (KIDMCP's UI half
	    passes it, and this plug-in's any-presentation question uses the stock accept_all -
	    KFCUIServices). This one stays local because what it tests is stated - "contains ILayoutControlData" - and the stock
	    is_layout's test is not: its implementation is not in the SDK, and nothing says what it counts
	    as a layout. */
	bool KFCAcceptLayoutPresentation(IDocumentPresentation* p)
	{
		if (p == nil)
			return false;
		return Utils<ILayoutUIUtils>()->IsLayoutPresentation(p);
	}

	/** Is the window in front RIGHT NOW a layout window showing this document?

	    NOT ILayoutUIUtils::GetFrontDocument, WHICH ANSWERS A WEAKER QUESTION. That one returns "the
	    document associated with the frontmost LAYOUT presentation" (ILayoutUIUtils.h:95-98) - so with
	    a Story Editor window in front of its own document's layout window it still names that
	    document, and a jump asking it concludes the document is already frontmost and stops. Measured
	    on the running application: the Story Editor stayed in front, the layout never changed spread,
	    and the panel reported nothing wrong.

	    QueryFrontLayoutData is about "the FRONT MOST presentation" (:127-133) and hands back its
	    layout part, so it is nil exactly when the window in front is not a layout - which is the
	    question this function is named for.

	    ONE place asks it, and both the entry test and the did-it-take test below call here: they are
	    the same question, and written out twice they drift apart. */
	bool LayoutOfDocIsFrontmost(const UIDRef& docRef)
	{
		InterfacePtr<ILayoutControlData> frontLayout(Utils<ILayoutUIUtils>()->QueryFrontLayoutData());
		if (frontLayout == nil)
			return false;
		IDocument* const doc = frontLayout->GetDocument();
		return doc != nil && ::GetUIDRef(doc) == docRef;
	}

	// Preferred among a document's layout windows: one that is not minimised (a preference criterion for
	// IDocumentUIUtils::FindPresentationForDocument, IDocumentUIUtils.h:45-47).
	bool KFCPresentationNotMinimized(IDocumentPresentation* p)
	{
		return p != nil && !p->IsMinimized();
	}

	/** A MINIMISED WINDOW COMES BACK. A layout window
	    that floats can be minimised (IDocumentPresentation.h:88-96), and MakeActive does not bring it
	    back: measured, a click on a hit row in a document whose only layout window was floating and
	    minimised moved that window's view (centre y 0 -> -114.5 pt) and left it minimised - the scroll and
	    the marker happened where nobody could see them, and the panel said nothing. Minimize() is a TOGGLE,
	    so it is asked only of a window that reads minimised - the guard KIDMCP's own restore keeps
	    (KIDMCPUIWindows.cpp, Restore). */
	void KFCBringBackIfMinimized(IDocumentPresentation* p)
	{
		if (p != nil && p->IsMinimized())
			p->Minimize();
	}

	// Bring the given document's layout window to the front. A windowless chapter gets its first
	// window opened here; a background-tab window is activated; a minimised one is brought back
	// (KFCBringBackIfMinimized). The zoom the user was looking at
	// travels along (zoom first, scroll second). Returns false when no window could be produced or
	// the activation did not take (the caller then reports without scrolling).
	//
	// On success the chapter STOPS BEING HELD (KFCBookScope::ForgetHeldDoc). See the
	// note beside that call at the foot of this function.
	bool EnsureDocFrontmost(const UIDRef& docRef)
	{
#ifdef KFC_DIAG
		if (KFC_DIAG_FAULT("jump-no-front"))
		{
			KFC_DIAG_LOG("JUMPFRONT fault jump-no-front: the window is not fronted");
			return false;
		}
#endif
		IDataBase* db = docRef.GetDataBase();
		if (db == nil)
			return false;

		// LAYOUT presentations only - see KFCAcceptLayoutPresentation - and one on screen before a minimised
		// one. "None" here means this document has no layout window at all, which is what the open below is
		// for. Asked before the entry test, because a minimised window can be the one in front.
		FindPresentation_PreferCriteria onScreenFirst;
		onScreenFirst.push_back(KFCPresentationNotMinimized);
		IDocumentPresentation* pres = Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, KFCAcceptLayoutPresentation, onScreenFirst);

		if (LayoutOfDocIsFrontmost(docRef))
		{
			KFCBringBackIfMinimized(pres);			// in front, but minimised (see above)
			KFCChapters()->ForgetHeldDoc(docRef);	// already in front and visible - see below
			return true;
		}

		// PRESERVE, THEN CLEAR. Same pair, and for the same reason, as EnsureSpreadInView above (not a
		// bare clear on an exit or two): MakeActive() reports nothing at all, and a standing error would
		// then fail the zoom command below it and the spread command after it. The destructor at the
		// closing brace puts the caller's own state back untouched (ErrorUtils.h:115-137).
		GlobalErrorStatePreserver frontErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

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
		if (pres != nil)
		{
			KFCBringBackIfMinimized(pres);			// after the zoom was read from the window in front
			pres->MakeActive();
		}
		else
		{
			// No layout window yet: open the document's first one (which also makes it active).
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
				return false;
			openedWindow = true;
		}

		// Verify the switch took - the same question the entry test asked, asked in one place.
		if (!LayoutOfDocIsFrontmost(docRef))
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
						if (zoomCmd != nil)
							CmdUtils::ProcessCommand(zoomCmd);	// carried over as a courtesy; never fatal
					}
				}
			}
		}

		// THE CHAPTER IS THE USER'S FROM HERE ON: STOP HOLDING IT.
		//
		// A jump reaches a closed chapter by reopening it WINDOWLESS (EnsureChapterReachable ->
		// ReopenChapterDoc), which puts it on the held list - the list of chapters a run is entitled
		// to hand back by closing them, with the UI suppressed. It has a window now and the user is
		// looking at it, so that entitlement is over: the next run would otherwise close a window
		// they are working in, and take with it whatever they have typed or replaced into it since
		// (the user: "a document the user opened by jumping should not be closed, even if nothing was
		// replaced in it").
		//
		// KFCBookScope::ShowChapterWindow says this about the window IT opens after a replace. This is
		// the same statement about the window a JUMP opens - the one that reaches the user first.
		KFCChapters()->ForgetHeldDoc(docRef);
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
		// By file first; the docRef the results hold only for a chapter with no file - see
		// KFCBookScope::ReachChapterDoc, which the replace asks too.
		if (KFCChapters()->ReachChapterDoc(file, ioDocRef))
		{
			KFCResults()->RebindChapterDoc(chapterIdx, ioDocRef);
			return true;
		}

		// Nothing can be reached, so nothing moves - and that has to be SAID. A row that does
		// nothing at all when clicked reads as a broken panel: the file has been moved, deleted,
		// renamed, or is open in another application.
		PMString message("Cannot open that chapter - moved, deleted, or in use?");
		message.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(message);
		return false;
	}

} // anonymous namespace

//----------------------------------------------------------------------------------------
// Public entry points
//----------------------------------------------------------------------------------------

bool KFCJump::IsHidePreviousChapterOn()
{
	return gHidePrevChapterOn;
}

void KFCJump::ToggleHidePreviousChapter()
{
	gHidePrevChapterOn = !gHidePrevChapterOn;
}

void KFCJump::SetHidePreviousChapter(bool on)
{
	// For the saved settings (KFCPanelState.cpp), which has to write a REMEMBERED value rather
	// than flip whatever the flag happens to be. Toggling from a restore would come out inverted
	// whenever the default is not what was saved.
	gHidePrevChapterOn = on;
}

namespace
{

// One activation at a time, across BOTH public doors (a click and a keyboard walk share
// ActivateNode; the double click's selection is the other entry). A landing opens documents, and
// opening a document RUNS THE MESSAGE LOOP - so the next click, or the trailing half of a double
// click, can be dispatched while the previous landing is still inside its own open, and would then
// select or jump from a state that landing has not finished making. The keyboard walk guards itself
// this way as well (KFCResultTreeEH's gWalking, which also guards its own selection step and
// therefore stays). Kept HERE rather than in each event handler so the doors cannot drift apart and
// a future caller is covered on arrival.
bool gActivating = false;

class ActivationGuard
{
public:
	ActivationGuard() { gActivating = true; }
	~ActivationGuard() { gActivating = false; }
};

// Does a row name a place in its story? Every row the search makes does, and nothing in KFC takes one away -
// asked anyway, because what follows (the overset test, the spread, the wax rectangle) must never be handed -1.
bool RowHasPlace(TextIndex start, TextIndex end)
{
	return start != kInvalidTextIndex && end != kInvalidTextIndex && start >= 0 && end >= start;
}

void SayRowHasNoPlace()
{
	PMString message("This row has no place in its story to go to. Search again.");
	message.SetTranslatable(kFalse);
	KFCResultTree::ShowStatus(message);
}

// Is the text at this position still the text this row describes - and if not, where is the row now? The stored
// position is an offset into the story, so ANY edit earlier in that story moves it, and that is exactly the case
// where marking or selecting there would frame text the user never searched for. ONE QUESTION TO THE MODEL
// (KFCSearchEngine::LocateRow - docs/superpowers/specs/2026-10-08-kfc-text-focus-jump-design.md T2): the row's stored
// place and its text focus - the place InDesign has carried through the user's typing - are both read against the
// row (the stored HASH of the whole match, and the line around it), look-alikes are told apart by their count and
// order, and a row found nowhere is looked for again. A row found elsewhere is gone to for this jump only (T3); when
// a row moved or a Missing word went, the rows are repainted.
// Shared by JumpToHit, which still moves the view when it is not found, and SelectHitText, which refuses.
bool RowFoundOrFoundAgain(int32 chapterIdx, int32 hitIdx, const UIDRef& docRef, UID storyUID,
	TextIndex& start, TextIndex& end)
{
	(void)storyUID;		// the model reads the row's own story
	const KFCResultModel::RowLocation found = KFCRuns()->LocateRow(chapterIdx, hitIdx, docRef, start, end);
	if (found == KFCResultModel::kRowMoved || found == KFCResultModel::kRowElsewhere)
		KFCResultTree::RefreshRows();
	return found != KFCResultModel::kRowNotFound;
}

// A PLACE PAST THE END OF ITS STORY IS BROUGHT BACK INSIDE IT.
// A row that is no longer found keeps its stored place, and an edit made since the search can have cut the
// story short of it. Measured unclamped: a row at 40 in a story cut to 8 characters went on as 40 to the
// overset test, the spread and the wax lookup - InDesign answered "overset" for a place that is not in the
// story at all, and the double click's selection then refused with the overset reason instead of "not
// found". Every other door bounds it too: KFCSearchEngine's RowReadsAsFound, the clamp at the foot of
// SelectHitText, and the SDK's gotolasttextedit ("reset text index if it is out of range",
// GTTxtEdtUtils.cpp:107-109). The story's last character is the nearest place there is to where the row
// was, so the view still goes where the hit used to be. A story that has gone (its UID deleted) reads
// as nil here and below (IDataBase.h:152-156).
void ClampIntoStory(const UIDRef& storyRef, TextIndex& start, TextIndex& end)
{
	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return;
	const TextIndex total = textModel->TotalLength();
	if (total <= 0)
		return;
	if (start >= total)
		start = total - 1;
	if (end > total)
		end = total;
	if (end < start)
		end = start;
}

// Bring the chapter's window to the front - saying so when it cannot be done, because a click that
// moves nothing must not appear to do nothing - and then, with "Hide Previous Chapter" ON, close every
// other displayed clean chapter of the searched book (scheduled; the landed-in document is the exception;
// a document outside the book stays; book results only - see ShouldHidePreviousChapter). Shared by
// JumpToHit and ShowChapter.
bool FrontChapter(const UIDRef& docRef)
{
	if (!EnsureDocFrontmost(docRef))
	{
		PMString message("Cannot bring that chapter's window to the front.");
		message.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(message);
		return false;
	}
	if (ShouldHidePreviousChapter())
		KFCChapters()->CloseDisplayedDocsIfClean(docRef);
	return true;
}

// NOTHING SELECTED in the front document - DeselectAll on the active selection, when there is one (1.4.0: a hit row's
// click clears the way for its own selection; an object row's Replace takes away what InDesign's replace selected).
void ClearSelection()
{
	ISelectionManager* const selectionManager = Utils<ISelectionUtils>()->GetActiveSelection();
	if (selectionManager != nil && selectionManager->SelectionExists(kInvalidClass, ISelectionManager::kAnySelection))
		selectionManager->DeselectAll(nil);
}

// SEARCH: AS IT WAS BEFORE A CLICK (1.4.0 - the author's call of 2026-10-10: put Search: back as it was). Edit >
// Find/Change - open, or minimized - re-picks Search: when the selection or the front document changes, and a row's
// click can do both: measured 2026-10-10 with the dialog open and InDesign in front, about 0.2 s after a click that
// selected, Story, To End of Story and All Documents became Document (on another run, Story became Selection);
// Document stayed. A click that fronts ANOTHER document had it re-picked WHILE that document came forward (read after
// the fronting, the value was Document already - t1-cross-alldocs-r2), and a click that cannot select still takes the
// old selection away, which the dialog answers as well (All Documents -> Document - t1-locked-all_documents). The
// dialog's own Find Next leaves Search: alone. So every activation (ActivateNode) remembers the tab's Search: BEFORE it
// does anything, and a change to it within kScopeKeepMs is put back - KFCPanelTitle's observer hears the change and
// calls KFCJump::KeepSearchScopeAfterClick. Measured: written back, the open dialog shows the value again and keeps it.
// A change later than that is the user's own; one inside it is put back too - who made it cannot be told. A row's
// Replace leaves Search: as it is (measured: replace-keeps-scope), so nothing arms for it.
struct ScopeKeep
{
	bool	armed;
	int32	mode;
	int32	scope;
	std::chrono::steady_clock::time_point at;
	ScopeKeep() : armed(false), mode(-1), scope(-1) {}
};
ScopeKeep gScopeKeep;
const long long kScopeKeepMs = 2000;

// Arm for an activation about to happen. One INSIDE an armed window keeps the value already held and only renews the
// window: the arrow walk's next row can come between the dialog's write and ours, and would take the dialog's value
// as the one to keep.
void ArmScopeKeep()
{
	const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
	const int32 mode = KFCRuns()->CurrentSearchMode();
	if (gScopeKeep.armed && mode == gScopeKeep.mode
		&& std::chrono::duration_cast<std::chrono::milliseconds>(now - gScopeKeep.at).count() <= kScopeKeepMs)
	{
		gScopeKeep.at = now;
		KFC_DIAG_LOG("SCOPEKEEP renewed mode=%d scope=%d", (int)gScopeKeep.mode, (int)gScopeKeep.scope);
		return;
	}
	const int32 scope = KFCRuns()->CurrentSearchScope();
	gScopeKeep.armed = (mode >= 0 && scope >= 0);
	gScopeKeep.mode = mode;
	gScopeKeep.scope = scope;
	gScopeKeep.at = now;
	KFC_DIAG_LOG("SCOPEKEEP armed=%d mode=%d scope=%d", gScopeKeep.armed ? 1 : 0, (int)mode, (int)scope);
}

// The Type tool, because a selected match is an invitation to EDIT - the double click's (SelectHitText) and, since
// 1.4.0, the click's as well: Edit > Find/Change's Find Next selects what it found and puts the Type tool on, and the
// author asked for the same (2026-10-10). A text selection made while the Selection tool is active is not somewhere the
// user can start typing. ! This CHANGES THE USER'S ACTIVE TOOL - deliberately, and it is written down in How to Use for
// that reason.
//
// IsToolOfType(kTextSelectionTool), NOT IsTextTool(). ITool.h:178-183 says IsTextTool "could be
// more accurately called DoesToolDeactivateTextEditor" and that the Zoom, Gradient and Hand tools
// return kTrue from it as well - then names this call as the one to use "for traditional 'text'
// tools that select text". Measured on the running application with IsTextTool: with the Hand,
// Zoom or Gradient tool active, a double click left that tool in place and made the selection
// anyway - text highlighted in a window the user cannot type into, which is the
// one outcome the paragraph above says must not happen. (The Selection tool was the control
// group and switched correctly, before and after.)
// ! Every SDK sample uses IsTextTool here, gotolasttextedit included; the header is what they
//   are all not reading. There is no call to IsToolOfType anywhere in the SDK to copy from.
// False when the Type tool could not be put on.
bool PutTypeToolOn()
{
	InterfacePtr<ITool> activeTool(Utils<IToolBoxUtils>()->QueryActiveTool());
	if (activeTool == nil || !activeTool->IsToolOfType(ITool::kTextSelectionTool))
	{
		InterfacePtr<ITool> iBeamTool(Utils<IToolBoxUtils>()->QueryTool(kIBeamToolBoss));
		if (iBeamTool == nil)
			return false;
		if (!Utils<IToolBoxUtils>()->SetActiveTool(iBeamTool))
			return false;
	}
	return true;
}

// A CLICK'S SELECTION OF A TEXT ROW'S MATCH (1.4.0 - the author's call of 2026-10-09: the tree does what Edit >
// Find/Change's Find Next does, one result selected after another, so the selection is the pointer and no marker goes
// over it). Only a match that can be selected - found where the row says, not locked, not hidden, not zero width (an
// overset one never comes here). Refused WITHOUT A WORD: the marker JumpToHit raises instead is the answer, as on every
// click before 1.4.0; the reasons are the double click's to say (SelectHitText). The Type tool is put on, as Find Next
// does (PutTypeToolOn - the author's call of 2026-10-10); the keyboard stays on the list - the row's LButtonUp acquires
// it after the jump, the walk takes it back - so the arrows walk on and Return replaces. Handing the keyboard to the
// text is still the double click's. The document is in front and composed, inside JumpToHit's dirty guard, its
// selection cleared. True when the match is selected.
bool SelectMatchOnClick(int32 chapterIdx, int32 hitIdx, const UIDRef& storyRef, TextIndex start, TextIndex end, bool found)
{
	if (!found || start >= end)
		return false;
	bool locked = false, hidden = false;
	KFCResults()->GetHitReach(chapterIdx, hitIdx, locked, hidden);
	if (locked || hidden)
		return false;
	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return false;
	const TextIndex total = textModel->TotalLength();
	if (start >= total)
		return false;
	if (end > total)
		end = total;
	ISelectionManager* const selectionManager = Utils<ISelectionUtils>()->GetActiveSelection();
	if (selectionManager == nil)
		return false;
	InterfacePtr<ITextSelectionSuite> textSelectionSuite(selectionManager, UseDefaultIID());
	if (textSelectionSuite == nil)
		return false;
	// The Type tool after the selection was cleared (JumpToHit) and before the text is selected - the order
	// SelectHitText keeps, and the official recipes' (its note there).
	if (!PutTypeToolOn())
		return false;
	// ! RangeData's two-argument form is (start, END) - SelectHitText's note.
	return textSelectionSuite->SetTextSelection(storyRef, RangeData(start, end), Selection::kDontScrollSelection, nil) != kFalse;
}

/** Jump to hit 'hitIdx' of chapter 'chapterIdx': front its document, scroll to the match, and select
    it - or raise the marker on one that cannot be selected (SelectMatchOnClick, 1.4.0). An unreachable
    chapter (missing / locked file) reports through the status line. An overset match has no on-page
    location of its own, so the view scrolls to the frame's overset "+" instead and NO marker is raised
    - those pixels belong to the indicator, not to the text.
    The marker comes up at once, whichever door asked (see the note at the head of KFCJump.h).

    Reached only through ActivateNode, where the "one activation at a time" guard lives - which is why
    it is in here.
    @return true when the jump LANDED ON THE ROW - its document in front and the text at its place
          still the text the row describes (overset or not). False for every other end: a bad index,
          a row with no place, an unreachable chapter, a window that could not be fronted, a row whose
          text is no longer there (each of which has said why, or has nothing to say). */
bool JumpToHit(int32 chapterIdx, int32 hitIdx)
{
	UIDRef docRef;
	IDFile file;
	UID storyUID = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	// A JUMP THAT GOES NOWHERE TAKES THE OLD MARKER WITH IT. Every exit below that does not move the
	// view clears it, these two included - or the previous hit's marker would stand over a row that
	// had just refused to go anywhere. It expires by itself within the second either way; what is
	// being made consistent is what the panel is SAYING.
	if (!KFCResults()->GetHitLocation(chapterIdx, hitIdx, docRef, file, storyUID, start, end))
	{
		KFCHitMarkerView::Hide();
		return false;
	}

	// A ROW WITH NO PLACE GOES NOWHERE, AND SAYS WHY (RowHasPlace). Nothing below asks about that: the
	// overset test, the spread and the wax rectangle would all be handed -1.
	if (!RowHasPlace(start, end))
	{
		KFCHitMarkerView::Hide();
		SayRowHasNoPlace();
		return false;
	}

	// The chapter may have been closed since the search (the user can close a held window). Bring
	// it back windowless by file - see EnsureChapterReachable, which ShowChapter shares.
	if (!EnsureChapterReachable(chapterIdx, docRef, file))
	{
		KFCHitMarkerView::Hide();	// it has already said why through the status line
		return false;
	}

	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
	{
		KFCHitMarkerView::Hide();
		return false;
	}
	const UIDRef storyRef(db, storyUID);

	// Everything from here on happens inside the dirty guard. Recomposing text dirties a document,
	// and so - harmlessly but visibly - can opening a window on it or changing which spread it
	// shows; none of that is the user's edit, and a chapter this plug-in opened windowless must not
	// come out wanting to be saved. IDataBase.h:389-412: this does not "keep it clean", it puts back
	// the flag the document had on the way in.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// Compose first, then read. The overset test below, the overset locator and the wax rectangle are
	// all readings of the RESULT of composition, and being overset is precisely what recomposing
	// changes (see RecomposeIfDamaged).
	RecomposeIfDamaged(storyRef);

	// Is the text at this position still the text this row describes - or can the row be found again?
	// When neither, the view still moves (it shows where the hit used to be) and the row says so below.
	const bool sameOccurrence = RowFoundOrFoundAgain(chapterIdx, hitIdx, docRef, storyUID, start, end);
	if (!sameOccurrence)
		ClampIntoStory(storyRef, start, end);	// a place the story no longer reaches - see ClampIntoStory

	// Asked of the search engine, which is where every hit's frame was resolved in the first place
	// (KFCSearchEngine::IsPositionOverset -> the same position-to-parcel-to-frame walk BuildHit
	// used; a copy of that walk here would have to fold the same failures into "no frame of its own").
#ifdef KFC_DIAG
	{
		InterfacePtr<ITextModel> diagModel(storyRef, UseDefaultIID());
		const TextIndex diagTotal = (diagModel != nil) ? diagModel->TotalLength() : -1;
		KFC_DIAG_LOG("JUMPPOS jump row %d/%d start=%d end=%d total=%d same=%d %s", chapterIdx, hitIdx,
			(int)start, (int)end, (int)diagTotal, sameOccurrence ? 1 : 0,
			(start >= 0 && start < diagTotal) ? "inside" : "OUTSIDE");
	}
#endif
	const bool overset = KFCRuns()->IsPositionOverset(storyRef, start);

	// A match in another document needs that document's window in front before any scrolling; if no
	// window can be produced, the panel has said so and the view is left where it was. (With "Hide
	// Previous Chapter" ON the tour has moved, and every other displayed clean chapter of the book is closed.)
	if (!FrontChapter(docRef))
	{
		KFCHitMarkerView::Hide();
		return false;
	}

	// A CLICK LEAVES ITS ROW'S MATCH SELECTED, OR NOTHING (1.4.0 - the author's call: whatever Search: is, as Find Next
	// does). What was selected goes first; the match is selected below when it can be, and otherwise the marker alone
	// points.
	ClearSelection();

	// ONE VIEW, LOOKED UP ONCE, USED BY EVERYTHING BELOW - not one per user through two different
	// calls that do not mean the same thing (ILayoutUIUtils.h:89-98 vs :127-133 - see
	// ScrollViewToPoint). Taken here, after the document has been fronted and before any geometry is
	// read.
	InterfacePtr<IControlView> frontView(Utils<ILayoutUIUtils>()->QueryFrontView());

	// The window is the right one; make sure it is showing the right SPREAD before anything is
	// scrolled - every pasteboard coordinate read below is taken AFTER this, deliberately. See
	// EnsureSpreadInView, and the empty pasteboard a master-page row lands on without it.
	EnsureSpreadInView(frontView, storyRef, start);

	// A visible match scrolls to its first wax line AND gets the marker on its characters. An overset
	// match has no wax line, so it scrolls to the red "+" overset locator (KFCFindOversetLocator,
	// which also climbs out of a pushed-out table to the main frame's "+") but is NOT marked - there
	// are no drawn characters to put it on. If no geometry can be produced, just clear.
	//
	// THE RECTANGLE BELOW IS FOR SCROLLING ONLY. It covers the first line of the match, in
	// pasteboard coordinates - as a marker it would mark a match running over several lines on its
	// first alone. The marker is a global text adornment (KFCHitMarker), handed the story and the
	// whole range, and drawn on every line of it.
	if (overset)
	{
		const KFCOversetLoc loc = KFCRuns()->FindOversetLocator(storyRef, start);
		if (loc.found)
			ScrollViewToPoint(frontView, loc.outportPb);	// scroll only - no marker on the "+" locator
		KFCHitMarkerView::Hide();
	}
	else
	{
		PMRect pbRect;
		if (GetFirstChunkPasteboardRect(storyRef, start, end, pbRect))
		{
			ScrollViewToPoint(frontView, PBPMPoint(
				(pbRect.Left() + pbRect.Right()) / PMReal(2.0),
				(pbRect.Top() + pbRect.Bottom()) / PMReal(2.0)));
			// The marker goes up either way, and in the same colour (the author's call). On a row
			// whose text is missing it frames whatever stands at that position now rather than the
			// match - which is the useful thing: it shows WHERE the hit used to be. That it is not
			// there any more is said by the status line and by the word on the row itself.
			//
			// AT ONCE, from the mouse as from the keyboard - the beat KCM's Story-mode jump flash
			// keeps (the user's request). Not booked for the double-click interval so that a double
			// click never flashes one: that brings it up about half a second after the view has
			// moved. A double click shows the marker for that moment and SelectHitText's
			// KFCHitMarkerView::Hide takes it down when the selection is made - exactly what KCM does
			// ("THE MARK COMES DOWN").
			//
			// 1.4.0: a match that can be selected IS selected, and no marker goes over it - a selection under an
			// inversion cannot be read (JMP-17's reason). Every other row is marked as above.
			if (SelectMatchOnClick(chapterIdx, hitIdx, storyRef, start, end, sameOccurrence))
				KFCHitMarkerView::Hide();
			else
				KFCHitMarkerView::Show(db, storyUID, start, end);
		}
		else
		{
			KFCHitMarkerView::Hide();
		}
	}

	// A row whose text has changed underneath says so from here on, and its Replace goes grey, so
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
		bool replaced = false, locked = false;
		KFCResults()->GetHitFlags(chapterIdx, hitIdx, replaced, locked);

		PMString message;
		message.SetTranslatable(kFalse);
		if (replaced)
		{
			message.Append("The replacement is no longer here - undone, or edited since.");
		}
		else
		{
			KFCResults()->SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeMissing);
			KFCResultTree::RefreshRows();
			message.Append("Not found - the text is no longer where the search left it. Search again.");
		}
		KFCResultTree::ShowStatus(message);
	}
	return sameOccurrence;
}

/** Show chapter 'chapterIdx': bring its document to the front, reopening it windowless first if the
    user closed it since the search. Does NOT scroll and raises no marker - a chapter row names a
    document, not a place inside one. Honours "Hide Previous Chapter". No-op on a bad index; an
    unreachable chapter reports through the status line. Reached through ActivateNode, like
    JumpToHit. */
void ShowChapter(int32 chapterIdx)
{
	UIDRef docRef;
	IDFile file;
	if (!KFCResults()->GetChapterLocation(chapterIdx, docRef, file))
		return;

	if (!EnsureChapterReachable(chapterIdx, docRef, file))
		return;

	// The guard the file's header promises for everything after the database is in hand - opening a
	// window, zooming, making it active - and that JumpToHit and SelectHitText both keep. Without it
	// a chapter a search had closed and this row reopened could come out wanting to be saved for
	// having been LOOKED at, and then "Hide Previous Chapter" would not close it. It restores the flag
	// the document came in with, so it changes nothing when nothing was dirtied.
	IDataBase::SaveRestoreModifiedState dirtyGuard(docRef.GetDataBase());

	// Showing a chapter is NOT jumping to a match: the view is left exactly where the user had it
	// and no marker is raised. The row says "this document", so the answer is that document, not a
	// place inside it. (KESCL's document rows behave the same way.)
	//
	// AND THE STANDING MARKER IS NOT TAKEN DOWN, unlike every exit of JumpToHit. The
	// asymmetry is real and it is harmless, which is worth saying so that nobody "fixes" it: a
	// marker belongs to one database (KFCHitMarker's adornment draws in that one only), so bringing a
	// DIFFERENT chapter forward stops it being painted without anything being cleared, and bringing
	// forward the chapter it is already in leaves it pointing at the same place, since this does not
	// scroll. ShowBook is the same case again - it moves a panel tab, not a view. Either way it
	// expires within the second. JumpToHit clears on its dead ends for a different reason: there the
	// view HAS been asked to move and has not, so a marker left up would be describing a place the
	// panel has just refused to go to.
	//
	// The same landing a jump makes - its window in front, and the same "Hide Previous Chapter" sweep.
	(void)FrontChapter(docRef);
}

/** Activate the book the results came from: make it IBookManager's current active book AND bring its
    tab to the front in the book panel - two separate things that do not follow each other. A book that
    has been closed since the search is NOT reopened; the status line says so. No-op for a
    document-scope result, which has no book row. Reached through ActivateNode, like JumpToHit. */
void ShowBook()
{
	// Which book the results came from. The SEARCHED PATH, not the model's display name: that name
	// is the file name only, and two books in different folders can share one.
	PMString bookPath;
	if (!KFCChapters()->GetSearchedBookPath(bookPath) || bookPath.IsEmpty())
		return;		// a document-scope result has no book row to click in the first place

	// A book closed since the search is NOT reopened. The row records which book was SEARCHED; it
	// is not a request to open a file. Saying so beats a row that appears to do nothing.
	if (!KFCChapters()->MakeBookActive(bookPath))
	{
		PMString message("That book is no longer open.");
		message.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(message);
		return;
	}
	// ...and its tab to the front of the book panel: the active book and the front tab are separate states,
	// and the user who clicks a book row asks for both (the tab is user interface, the UI half's).
	KFCBookPanelLookup::BringBookTabForward(bookPath);
}

} // anonymous namespace

bool KFCJump::SelectHitText(int32 chapterIdx, int32 hitIdx)
{
	// A previous landing is still inside its own document-open (see gActivating above JumpToHit).
	// Selecting NOW would put a caret into a state that landing has not finished making - refused
	// instead, silently: the refusal leaves the click behaving as the single click whose jump is
	// still under way, which is also why no status line is written over that jump's own.
	if (gActivating)
		return false;
	ActivationGuard activationGuard;

	UIDRef docRef;
	IDFile file;
	UID storyUID = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	if (!KFCResults()->GetHitLocation(chapterIdx, hitIdx, docRef, file, storyUID, start, end))
		return false;

	// A row with no range at all (see JumpToHit) - said before the zero-width test below, which would
	// otherwise answer it with the wrong reason.
	if (!RowHasPlace(start, end))
	{
		SayRowHasNoPlace();
		return false;
	}

	// OUT OF THE USER'S REACH: move there and mark it, but do not select (the author's call).
	// A LOCKED match is on a locked layer or in a locked story - InDesign
	// can search locked content but offers no way to change it, so a selection would be an offer it
	// cannot keep. A HIDDEN match is on a switched-off layer: it is composed and can be jumped to,
	// but it draws nothing, so a selection over it would be invisible.
	//
	// Asked FIRST because it costs nothing - no database, no composition - and because there is no
	// point doing any of that work for a row that is going to be refused.
	//
	// THE MARKER STAYS UP. It is taken down only when a selection replaces it (see the
	// foot of this function). Here nothing replaces it, so it remains what it always was: the answer
	// to "where is it?" - which is the whole of what a double click on these rows can give.
	bool hitLocked = false, hitHidden = false;
	KFCResults()->GetHitReach(chapterIdx, hitIdx, hitLocked, hitHidden);
	if (hitLocked || hitHidden)
	{
		PMString message(hitLocked
			? "That match is locked - it cannot be selected."
			: "That match is on a hidden layer - it cannot be selected.");
		message.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(message);
		return false;
	}

	// A ZERO-WIDTH MATCH HAS NOTHING TO SELECT. GREP's ^, $ and the lookarounds match at a POSITION
	// rather than over characters, so such a row names a place, not text (measured: ^ returns one hit
	// per paragraph). The clamp further down refuses the empty range
	// anyway - but silently, and a double click that appears to do nothing is the one refusal this
	// function must not make when every other one says why. Asked up here with the other tests that
	// cost nothing, before any database work is done for a row that is going to be turned away.
	if (start == end)
	{
		PMString message("That match has no width (^, $ or a lookaround) - there is nothing to select.");
		message.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(message);
		return false;
	}

	// The jump that ran a moment ago already brought this chapter back if it had been closed, and
	// already fronted its window. Asked again anyway: this is a public function, and a caller that
	// reached it another way must not select into a database that is not there.
	if (!EnsureChapterReachable(chapterIdx, docRef, file))
		return false;

	// ...AND ITS WINDOW HAS TO BE THE ONE IN FRONT. The selection below goes through the ACTIVE
	// selection - the window in front - and the double click only promises that the jump was TRIED: it
	// can have failed to bring the window forward (it has said why), or have been dropped while an
	// earlier landing was still opening a chapter. Measured without this test, with the test build's
	// fault switch jump-no-front: the front document's own selection was cleared, and the hit's
	// text - in the document behind - was not selected. Refused without a word, as a click dropped by
	// gActivating is: the jump has already said why, or a later click will land.
	KFC_DIAG_LOG("SELECT row %d/%d: the hit's document is in front=%d", chapterIdx, hitIdx,
		LayoutOfDocIsFrontmost(docRef) ? 1 : 0);
	if (!LayoutOfDocIsFrontmost(docRef))
		return false;

	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
		return false;
	const UIDRef storyRef(db, storyUID);

	// Same guard the jump puts round everything it does: making a selection recomposes and can dirty
	// a document that this plug-in only opened to look at. IDataBase.h:389-412 - it restores the flag
	// the document came in with rather than forcing it clean.
	IDataBase::SaveRestoreModifiedState dirtyGuard(db);

	// Compose first, then read - the order JumpToHit keeps, for the reason it gives there: being
	// overset is a reading of the RESULT of composition. The first click's jump composed this story,
	// but nothing between the two clicks promises it is still undamaged, and this function does not
	// lean on its caller's history. A no-op when nothing is damaged.
	RecomposeIfDamaged(storyRef);

	// STALE: the same test the jump makes (RowFoundOrFoundAgain). There the answer only changes what
	// the panel SAYS - the view still moves, which is useful, because it shows where the hit used to
	// be. Here it changes what the user GETS: a selection over text they never searched for, ready to
	// be typed over. So this one refuses. The jump has already put its own message up in this case;
	// this adds nothing and would only overwrite it.
	//
	// ASKED BEFORE THE OVERSET TEST, IN THE ORDER JumpToHit ASKS THEM. The other way round, a stale
	// place is handed to the overset test first: measured, a row at 40 in a story cut to 8 characters
	// read as "overset", and the status line said "An overset match has no text on the page to select."
	// over the jump's "Not found". And a row found again here (moved) would be tested for being overset
	// at the place it had left.
	if (!RowFoundOrFoundAgain(chapterIdx, hitIdx, docRef, storyUID, start, end))
		return false;

	// OVERSET: move there, but do not select. (Same rule as locked and hidden above - the author's
	// call.) There is no on-page text to highlight. The jump has the same split and
	// scrolls to the "+" indicator instead.
	//
	// The marker is left exactly as the jump left it - which for an overset row means there is none
	// (JumpToHit clears it: those pixels belong to the "+" indicator, not to the text). Nothing is
	// done about it here either way; this function only takes the marker down when a SELECTION
	// replaces it.
#ifdef KFC_DIAG
	{
		InterfacePtr<ITextModel> diagModel(storyRef, UseDefaultIID());
		const TextIndex diagTotal = (diagModel != nil) ? diagModel->TotalLength() : -1;
		KFC_DIAG_LOG("JUMPPOS select row %d/%d start=%d end=%d total=%d %s", chapterIdx, hitIdx,
			(int)start, (int)end, (int)diagTotal, (start >= 0 && start < diagTotal) ? "inside" : "OUTSIDE");
	}
#endif
	if (KFCRuns()->IsPositionOverset(storyRef, start))
	{
		PMString message("An overset match has no text on the page to select.");
		message.SetTranslatable(kFalse);
		KFCResultTree::ShowStatus(message);
		return false;
	}

	// From here the shape is the official one: gotolasttextedit's GTTxtEdtUtils::ActivateStory
	// (:99-140) = clear the selection, make sure a text tool is active, then SetTextSelection.

	// A range past the end of the story cannot be selected. The story is the live one and the range
	// is the recorded one; the stale test above says the TEXT still matches, which makes this a
	// belt-and-braces clamp rather than a live case - but the official recipe clamps too, and a bad
	// RangeData is an assert rather than a refusal.
	InterfacePtr<ITextModel> textModel(storyRef, UseDefaultIID());
	if (textModel == nil)
		return false;
	const TextIndex total = textModel->TotalLength();
	if (start >= total)
		return false;
	if (end > total)
		end = total;
	if (end <= start)
		return false;

	ISelectionManager* selectionManager = Utils<ISelectionUtils>()->GetActiveSelection();
	if (selectionManager == nil)
		return false;

	// Clear whatever was selected first (a page-item selection left standing is a second selection in
	// a different CSB). BEFORE the tool switch, which is the order both official recipes keep:
	// gotolasttextedit deselects and then switches (GTTxtEdtUtils.cpp:113-128), typekitinspector
	// deselects and never switches (TKITreeWidgetObserver.cpp:136-140, which is where the
	// SelectionExists test comes from). Switching first hands the incoming tool a selection it will
	// convert, only for the next line to throw the result away.
	if (selectionManager->SelectionExists(kInvalidClass /*any CSB*/, ISelectionManager::kAnySelection))
		selectionManager->DeselectAll(nil);

	// The Type tool, because this is an invitation to EDIT - PutTypeToolOn, whose note says why it
	// is IsToolOfType and not IsTextTool. (Since 1.4.0 the click has already put it on, as Find Next
	// does; asked again because a caller may come here another way.)
	if (!PutTypeToolOn())
		return false;

	InterfacePtr<ITextSelectionSuite> textSelectionSuite(selectionManager, UseDefaultIID());
	if (textSelectionSuite == nil)
		return false;

	// ! RangeData's two-argument form is (start, END) - not (start, length). Getting that wrong
	//   selects from the match to a point measured from the start of the STORY.
	//
	// kDontScrollSelection: the jump has already centred the match with
	// IPanorama::ScrollContentLocationToFrameCenter, which puts it in the middle of the window.
	// kScrollIntoView would only guarantee it is somewhere on screen, and asking for it here would
	// undo the better answer that has just been given.
	if (textSelectionSuite->SetTextSelection(storyRef, RangeData(start, end),
			Selection::kDontScrollSelection, nil) == kFalse)
	{
		return false;
	}

	// TAKE THE JUMP'S MARKER BACK DOWN (the author's call).
	//
	// The first click of this double click may have raised the marker, which INVERTS the pixels under
	// the match (KFCHitMarker.h) - a pointer saying "it is here". (Since 1.4.0 it selects a match it
	// can select and raises no marker over it - JumpToHit; a marker is up here only when that click
	// could not select.) The selection now says the same thing, and it is what the user is about to
	// type over. Leaving both up puts an inversion on top of a highlight, so the text the user came
	// here to read is the one thing on screen that cannot be read.
	//
	// Only on SUCCESS. Every refusal above returns before this, and there the marker is the only
	// feedback the click produced - taking it down as well would leave a double click that appears
	// to do nothing.
	KFCHitMarkerView::Hide();
	return true;
}

void KFCJump::ActivateNode(int32 chapterIdx, int32 hitIdx)
{
	// One door for every row, so a click and a keyboard walk can never drift apart - the reason
	// this exists at all is that there are two callers.

	// A previous landing is still inside its own document-open - the click is dropped, exactly as
	// the keyboard walk drops its key (see gActivating above JumpToHit).
	if (gActivating)
		return;
	ActivationGuard activationGuard;

	// Search: as it is BEFORE this activation fronts a document or changes the selection - put back if Edit >
	// Find/Change re-picks it in answer (ScopeKeep, above JumpToHit).
	ArmScopeKeep();

	// A GREP row landed on: what its Replace would write, on the message area (KFCResultTree::ShowRowPreview).
	// Any other landing - a jump refused (its reason stands), a branch row - takes a previous row's preview
	// away, so it never stands beside a row it does not belong to.
	if (hitIdx >= 0)
	{
		if (JumpToHit(chapterIdx, hitIdx))
			(void)KFCResultTree::ShowRowPreview(chapterIdx, hitIdx);
		else
			KFCResultTree::DropRowPreview();
	}
	else
	{
		KFCResultTree::DropRowPreview();
		if (chapterIdx >= 0)
			ShowChapter(chapterIdx);
		else if (chapterIdx == -1)
			ShowBook();
	}
}

void KFCJump::KeepSearchScopeAfterClick()
{
	if (!gScopeKeep.armed)
		return;
	const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - gScopeKeep.at).count();
	// Too late to be the click's doing, or a setting of another tab: let go.
	if (ms > kScopeKeepMs || KFCRuns()->CurrentSearchMode() != gScopeKeep.mode)
	{
		gScopeKeep.armed = false;
		return;
	}
	// Still armed after putting it back, for the window's length: the dialog can write more than once for one click
	// (measured: a click that fronted another document had it written as the document came forward and again about
	// 0.35 s later, for the selection - click-keeps-scope-cross), and a double click selects again inside it
	// (SelectHitText), which the dialog answers too. Writing the same value back changes nothing, so the notification of
	// our own write ends here.
	//
	// ONLY A VALUE THE SELECTION NOW OFFERS goes back (KFCSearchEngine::SearchScopeForSelection: All Documents and
	// Document always; Story, To End of Story and Selection only with a selection that gives them). One it does not
	// offer - Story after a click that could not select, which leaves nothing selected - is one the dialog cannot show
	// either, and it stays as the dialog made it, as when the user takes a selection away themselves.
	const int32 now = KFCRuns()->CurrentSearchScope();
	const bool offered = (KFCRuns()->SearchScopeForSelection(gScopeKeep.scope) == gScopeKeep.scope);
	KFC_DIAG_LOG("SCOPEKEEP heard ms=%d scope now=%d kept=%d offered=%d", (int)ms, (int)now, (int)gScopeKeep.scope,
		offered ? 1 : 0);
	if (now >= 0 && now != gScopeKeep.scope && offered)
		KFCRuns()->RestoreSearchScope(gScopeKeep.mode, gScopeKeep.scope);
}

// End, KFCJump.cpp.
