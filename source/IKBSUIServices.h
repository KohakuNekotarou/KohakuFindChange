//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ***** WHAT THE MODEL HALF ASKS THE UI HALF FOR, AND DOES WITHOUT WHEN IT IS NOT THERE *****
//  (2026-10-01, the model/UI split - docs/superpowers/specs/2026-10-01-kbs-model-ui-split-design.md 4.3)
//
//  A model plug-in may not show a progress bar, an alert or a window, nor read the Book panel: those are
//  user-interface components (guide vol1-06, "UI component content"), and a model plug-in must not depend
//  on the plug-ins that provide them (vol1-06, "Problems when mixing model and UI"). Yet a search has a
//  bar to show, a replace an alert to raise and a chapter to give a window. So the model asks THIS
//  interface - pure virtual, declared here on the model's side - and the UI half implements it
//  (KBSUIServices.cpp) and puts it on kSessionBoss from its own resource (guide vol1-07, Object-Model
//  Rule 2: a UI implementation reaches a model boss only through an AddIn in a UI plug-in's resource).
//
//  ***** NIL IS AN ANSWER. ***** On a background thread, and under InDesign Server, the UI plug-in is not
//  there and the Query comes back nil - "the system behaves as if the plug-in were missing and returns a
//  nil pointer. It is critical that you write model code that expects to be able to receive nil pointers"
//  (vol1-07, "Rules for thread safety"). Every caller in the model half reads nil as "no UI": no bar, no
//  window, no Book panel, no alert.
//
//  What the model DECIDES stays in the model - the rules, the wording, the order. What crosses is only
//  the showing.
//
//========================================================================================

#ifndef __IKBSUIServices_h__
#define __IKBSUIServices_h__

#include "IPMUnknown.h"
#include "IDFile.h"
#include "PMString.h"
#include "UIDRef.h"

#include "KBSBoundaryID.h"	// IID_IKBSUISERVICES

/** One progress bar the UI half has put up for the model half - RangeProgressBar's four calls that the
    engines make, and nothing else. Deleted by the model half when its run is over (KBSProgressBar). */
class KBSProgressBarUI
{
public:
	virtual ~KBSProgressBarUI() {}
	virtual void	SetTaskText(const PMString& text, bool16 forceRedraw) = 0;
	virtual void	SetPosition(int32 newPosition) = 0;
	virtual bool16	WasCancelled(bool8 setGlobalErrorState) = 0;
	virtual void	DisableChildProgressBars(bool16 disable) = 0;
};

class IKBSUIServices : public IPMUnknown
{
public:
	enum { kDefaultIID = IID_IKBSUISERVICES };

	/** A modal RangeProgressBar with these arguments (ProgressBar.h), or nil when one could not be made.
	    The caller owns it and deletes it when the run is over - the bar comes down with it. */
	virtual KBSProgressBarUI*	NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
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

	/** A modal alert, the message plus a warning icon (CAlert::WarningAlert). The message is finished
	    text, already marked untranslatable by the caller. */
	virtual void				WarningAlert(const PMString& message) = 0;
};

#endif // __IKBSUIServices_h__
