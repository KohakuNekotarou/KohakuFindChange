//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  WHAT THE MODEL HALF ASKS THE UI HALF FOR, AND DOES WITHOUT WHEN IT IS NOT THERE
//  (the model/UI split - docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md 4.3)
//
//  Two reasons keep these out of the model half, and they are not the same reason:
//    - A WINDOW and the BOOK PANEL come from UI plug-ins - kOpenLayoutCmdBoss is LayoutUI's, and
//      IDocumentUIUtils' predicates and PaletteRefUtils are WidgetBin's - and a model plug-in must not
//      depend on those (vol1-06, "Problems when mixing model and UI").
//    - The PROGRESS BAR does not: RangeProgressBar is PUBLIC_DECL (ProgressBar.h), in the library every
//      plug-in links, as CAlert is, and Adobe's own model plug-in incopyfileactions (InCopy's) calls CAlert
//      itself (InCopyDocUtils.cpp). It is here because the guide lists DIALOGS as user-interface components
//      (vol1-06, "UI component content") and KFC reads a modal bar as a dialog - the author's call (the
//      design's section 0.3: follow the guide strictly).
//  Yet a run has a bar to show and a chapter to give a window. So the
//  model asks THIS interface - pure virtual, declared here on the model's side - and the UI half
//  implements it (KFCUIServices.cpp) and puts it on kSessionBoss from its own resource (guide vol1-07,
//  Object-Model Rule 2: a UI implementation reaches a model boss only through an AddIn in a UI plug-in's
//  resource).
//
//  kSessionBoss, NOT kUtilsBoss - although Adobe's own precedent for this shape is on kUtilsBoss.
//  InCopy's model plug-in incopyfileactions asks a UI plug-in's IBGTasksUIUtils
//  the same way - Utils<IBGTasksUIUtils>, nil read as "no UI" (InCopyDocUtils.cpp:3048-3049). It is kept
//  on the session because the guide promises a session per thread ("a separate execution context ... for
//  each thread", "Threads do not share object-model instances" - vol1-07, "Rules for thread safety"),
//  while Utils.h:44-46 calls kUtilsBoss "the single kUtilsBoss object" and says nothing of threads; and
//  Adobe's call is made while closing a document, behind uiFlags != kSuppressUI - not from a background
//  export, which is where KFC's nil has to hold. Both answer nil without the UI on the main thread;
//  nothing would be gained by moving.
//
//  NIL IS AN ANSWER. On a background thread, and under InDesign Server, the UI plug-in is not
//  there and the Query comes back nil - "the system behaves as if the plug-in were missing and returns a
//  nil pointer. It is critical that you write model code that expects to be able to receive nil pointers"
//  (vol1-07, "Rules for thread safety"). Every caller in the model half reads nil as "no UI" and does
//  without. The model half asks through KFCQueryUIServices, which answers nil for a UI half of another
//  build as well.
//
//  What the model DECIDES stays in the model - the rules, the wording, the order. What crosses is only
//  the showing.
//
//========================================================================================

#ifndef __IKFCUIServices_h__
#define __IKFCUIServices_h__

#include "IPMUnknown.h"
#include "ISession.h"			// KFCQueryUIServices
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"
#include "UIDList.h"

#include <vector>

#include "IKFCBoundary.h"		// KFCOtherHalfIsThisBuild - KFCQueryUIServices
#include "KFCBoundaryID.h"		// IID_IKFCUISERVICES

/** One progress bar the UI half has put up for the model half - the RangeProgressBar calls the engines make,
    and nothing else. Deleted by the model half when its run is over (KFCProgressBar). Its slots cross the boundary
    as a door's do: a change to them moves kKFCBoundaryRevision up by one (KFCBoundaryID.h). */
class KFCProgressBarUI
{
public:
	virtual ~KFCProgressBarUI() {}
	virtual void	SetTaskText(const PMString& text, bool16 forceRedraw) = 0;
	virtual void	SetPosition(int32 newPosition) = 0;
	virtual bool16	WasCancelled(bool8 setGlobalErrorState) = 0;
	virtual void	DisableChildProgressBars(bool16 disable) = 0;
};

class IKFCUIServices : public IPMUnknown
{
public:
	// No kDefaultIID, on purpose: the model half asks through KFCQueryUIServices and nowhere else.

	/** A modal RangeProgressBar with these arguments (ProgressBar.h), or nil when one could not be made.
	    The caller owns it and deletes it when the run is over - the bar comes down with it. A bar the manager
	    turned away (another bar's DisableChildProgressBars) comes back all the same and does nothing - its
	    WasCancelled is false (KFCUIServices.cpp). */
	virtual KFCProgressBarUI*	NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
									bool8 showImmediate, bool8 showCancel) = 0;

	/** Does this document have a WINDOW anywhere - front, or behind another tab? (The all-presentations
	    search: GetFrontmostPresentationForDocument answers nil for a document behind another tab.) */
	virtual bool				DocHasAnyWindow(const UIDRef& docRef) = 0;

	/** Give this open, windowless document a layout window. True when a window came out of it - "the
	    command succeeded" and "there is a window" are two different statements, so the window is asked
	    for. The caller has checked the document is open and has no window. */
	virtual bool				OpenLayoutWindow(const UIDRef& docRef) = 0;

	/** The file of the book whose tab is FRONTMOST in the Book panel. False when no book panel is
	    frontmost (iconised, closed, no book open) - the caller then falls back to the active book. */
	virtual bool				GetPanelBookFile(IDFile& outFile) = 0;
	// Added by hand from here on - a new method goes below the last one, never between (a vtable slot is a
	// promise to every built caller). One taken out moves every slot below it, so both halves are built
	// together then (nothing outside KFC calls this door). Either way kKFCBoundaryRevision goes up by one
	// (KFCBoundaryID.h): a model half that finds another build's UI half then asks this door nothing
	// (KFCQueryUIServices).
	/** The documents selected in the Book panel that shows this book (Find/Change Selected Documents
	    (Book)). true only for a PART of the book - none or all selected answers false, the product's rule
	    (AcquireCurrentBook::AllOrNoneSelected) - and false too when no panel shows this book. outContents = the
	    selected BookContent UIDs (the book's database); outTotal = the panel's rows. */
	virtual bool				GetPanelBookSelection(const IDFile& bookFile, std::vector<UID>& outContents, int32& outTotal) = 0;
	/** The page items selected in the front layout view, standoffs stripped (the Object tab's Search: = Selection
	    searches these). false (outItems emptied) when there is no layout selection or no item in it. */
	virtual bool				GetSelectedPageItems(UIDList& outItems) = 0;
	/** Is there a TEXT selection in the front view - a range, or a caret (the author's call: on the Object tab,
	    Search: = Selection with the cursor in text searches the frame the cursor stands in, as Find/Change's own Find
	    does; KFCObjectSearch then lets InDesign aim its walk from Search:). */
	virtual bool				HasTextSelection() = 0;
	/** Is anything selected in the front view - a page item, text, cells. The selection snapshot gives "nothing" back
	    as nothing to do, so the object search asks this before its walk and clears what the walk selected when the
	    answer was no - without it an object search left an object selected (the user's report). */
	virtual bool				HasAnySelection() = 0;
	/** Nothing selected in the front view - DeselectAll on the active selection, when there is one
	    (KFCJump::ClearFrontSelection, the SDK's way in SnpSelectShape.cpp). */
	virtual void				ClearSelection() = 0;
	/** IS A ROW'S LANDING UNDER WAY in the panel (KFCJump::IsLanding - a click's, an arrow's, an object row's Replace's)?
	    A landing opens documents and windows and runs the message loop while it does, and the model's own deferred work
	    (KFCBookWatch's retire - KFCRunGuard::IsLandingUnderWay) must not empty the results or hand chapters back under it.
	    The model cannot see the UI half's flag; this is how it asks. */
	virtual bool				IsLanding() = 0;
	/** DOES THE QUERY FIND/CHANGE HOLDS NOW READ AS LIST `resultSet`'s NAME - the live list's or a kept tier's - word for
	    word, in the words the person reads? The third check of a list's query stated for a row operation (KFCQueryScope -
	    docs/superpowers/specs/2026-10-14-kfc-tier-switch-v2-design.md section 3-2), after the field-by-field one and the
	    walk signature: the names are the UI half's (it names each list after its query when the search is made -
	    KFCResultHistory), so this is where the model asks. outName = that list's name, for the words said. False = it does
	    not read so, or there is no named list of that set to read it against - the operation is refused either way. */
	virtual bool				QueryReadsAsTier(uint32 resultSet, PMString& outName) = 0;
	/** THE LIVE LIST `resultSet`'s QUERY COPY WAS TAKEN AGAIN (v2 R3 - KFCQueryScope::Finish): a replace went through with
	    Find/Change as it is, its Change to (or another change-side setting) edited since the search, and the list now
	    remembers what was used. The UI half names the live list again from the query Find/Change holds (its place in the
	    query dialog's run order kept), so its heading says what a later replace under it would write - and the third check
	    above reads that name. Nothing for a set that is not the live list's. */
	virtual void				LiveQueryRecopied(uint32 resultSet) = 0;
	/** A NOTICE ON THE PANEL'S MESSAGE LINE where the operation has no words of its own to carry it - Find/Change could not
	    be put back as it was after a list's query was stated for a GREP row's after-text, or for an operation that left
	    early (KFCQueryScope). Said at once (KFCResultTree::ShowStatus), so the person checks Edit > Find/Change. */
	virtual void				ShowNotice(const PMString& words) = 0;
	/** THE SELECTIONS OF THE DOCUMENTS BEHIND THE FRONT ONE, AROUND AN OBJECT SEARCH (the object search's O6 - the selection,
	    given back): its walks select what they find in every document with a window they walk (IFindChangeService::
	    SearchObject - measured 2026-10-18: an All Documents search with nothing selected anywhere left the last item it
	    found selected in the document behind the front one), and the selection snapshot gives the front view's back only.
	    Snapshot = every other document's layout view read now - the page items selected, nothing, or something else (text,
	    a table's cells); Restore = put back once the walks are over: the items selected again, nothing selected where
	    nothing was, and where text or cells were (which this cannot read back) the items a walk left selected taken away.
	    Kept by the UI half between the two calls; a document closed since is passed over. */
	virtual void				SnapshotBackgroundSelections() = 0;
	virtual void				RestoreBackgroundSelections() = 0;
};

/** HOW THE MODEL HALF ASKS FOR IKFCUIServices - the only way. IKFCUIServices has no kDefaultIID, so a Query of its
    own by UseDefaultIID() does not compile, and IID_IKFCUISERVICES is spelled here alone. The UI half that is loaded
    has to be this build's (KFCOtherHalfIsThisBuild, IKFCBoundary.h), or the services are taken for absent and nil
    comes back, as with no UI half at all - another build's slots would put each call in another method
    (KFCBoundaryID.h). Not only a run asks: the book watcher asks IsLanding on every book close of the session
    (KFCRunGuard::IsLandingUnderWay). Asked every time, not kept - a background thread's session has no UI half, the
    main thread's does. AddRef'd, or nil. (The test build's boundary-mismatch switch, KFCDiag.h, makes the stamps
    differ - set it before InDesign starts, as the UI half reads it once.) */
inline IKFCUIServices* KFCQueryUIServices()
{
	ISession* const session = GetExecutionContextSession();
	if (session == nil || !KFCOtherHalfIsThisBuild(session, IID_IKFCUIBOUNDARY))
		return nil;
	InterfacePtr<IKFCUIServices> ui(session, IID_IKFCUISERVICES);
	return ui.forget();
}

#endif // __IKFCUIServices_h__
