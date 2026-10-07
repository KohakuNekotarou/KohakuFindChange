//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The UI half's side of IKFCUIServices.h (the model/UI split): the progress bar, the windows, the Book
//  panel and the alert the model half asks for. AddIn'd on kSessionBoss from the UI half's resource, so a
//  background thread - which loads no UI plug-in - finds nothing there and the model does without. Each
//  answer is what KFCBookScope / the engines did themselves before the split, carried over unchanged; the
//  notes are theirs.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"			// SetItemList - kOpenLayoutCmdBoss takes the document as its item
#include "IDataBase.h"
#include "IDocumentUIUtils.h"	// FindPresentationForDocument (has-a-window test)
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear?
#include "IWindow.h"			// the window kOpenLayoutCmdBoss is supposed to have produced

// General includes:
#include "CAlert.h"
#include "CmdUtils.h"
#include "CPMUnknown.h"
#include "DocumentPresFindCriteria.h"	// FindPresCriteria::accept_all (WidgetBin - this is the UI half)
#include "ErrorUtils.h"			// GlobalErrorStatePreserver / PMSetGlobalErrorCode
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss
#include "ProgressBar.h"		// RangeProgressBar
#include "UIDList.h"
#include "Utils.h"

#include <new>					// std::nothrow

// Project includes:
#include "KFCUIID.h"				// kKFCUIServicesImpl
#include "IKFCUIServices.h"
#include "KFCBookPanelLookup.h"

namespace
{
	/** One RangeProgressBar, held for the model half. Its four calls are the bar's own. */
	class KFCUIProgressBar : public KFCProgressBarUI
	{
	public:
		KFCUIProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel)
			: fBar(title, startRange, endRange, showImmediate, showCancel) {}
		virtual ~KFCUIProgressBar() {}

		virtual void	SetTaskText(const PMString& text, bool16 forceRedraw)	{ fBar.SetTaskText(text, forceRedraw); }
		virtual void	SetPosition(int32 newPosition)							{ fBar.SetPosition(newPosition); }
		virtual bool16	WasCancelled(bool8 setGlobalErrorState)					{ return fBar.WasCancelled(setGlobalErrorState); }
		virtual void	DisableChildProgressBars(bool16 disable)				{ fBar.DisableChildProgressBars(disable); }

	private:
		RangeProgressBar	fBar;
	};
}

class KFCUIServices : public CPMUnknown<IKFCUIServices>
{
public:
	KFCUIServices(IPMUnknown* boss) : CPMUnknown<IKFCUIServices>(boss) {}
	virtual ~KFCUIServices() {}

	virtual KFCProgressBarUI* NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
		bool8 showImmediate, bool8 showCancel)
	{
		return new (std::nothrow) KFCUIProgressBar(title, startRange, endRange, showImmediate, showCancel);
	}

	/** Does this document have a WINDOW anywhere - front, or behind another tab? The
	    all-presentations search, because GetFrontmostPresentationForDocument answers nil for a
	    document sitting behind another tab (ShowChapterWindow asks it this way too).

	    Asked by the held-chapter releases: a window makes a chapter the USER'S,
	    whoever raised the window. ShowChapterWindow and the jump take a chapter off the held list
	    when they raise one themselves, but a window can be raised behind this module's back - the
	    book panel lists every chapter, and double-clicking one there windows the very document being
	    held. A release that closed it then would take a window the user is looking at; and once they
	    had saved their work, not even the unsaved-work door would stand in the way.

	    The SDK's own "any presentation" predicate, FindPresCriteria::accept_all, as KCM's UI half uses
	    it (KCMStoryJump.cpp). This test cannot move back to the model half: the predicate's
	    implementation is in WidgetBin (DocumentPresFindCriteria.h:40-46). */
	virtual bool DocHasAnyWindow(const UIDRef& docRef)
	{
		IDataBase* db = docRef.GetDataBase();
		if (db == nil)
			return false;
		FindPresentation_PreferCriteria noPreference;
		return Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, &FindPresCriteria::accept_all, noPreference) != nil;
	}

	/** The window half of KFCBookScope::ShowChapterWindow, carried over as it stood. */
	virtual bool OpenLayoutWindow(const UIDRef& docRef)
	{
		// THE WINDOW IS ALLOWED NOT TO APPEAR - this function's false says so - so its error
		// state stays in here. Preserve, then clear, exactly as the open in ReopenChapterDoc does
		// (see that note for the SDK's own shape and the contract at ErrorUtils.h:115-117). Placed
		// ahead of the command rather than inside the failure branch so that all THREE ways this can
		// end without a window are covered: the command that would not build, the one that failed, and
		// the one that reported success without producing a presentation.
		GlobalErrorStatePreserver windowErrorState;
		ErrorUtils::PMSetGlobalErrorCode(kSuccess);

		InterfacePtr<ICommand> cmd(CmdUtils::CreateCommand(kOpenLayoutCmdBoss));
		if (cmd == nil)
			return false;
		cmd->SetItemList(UIDList(docRef));

		// The command's data interface, taken BEFORE processing so the result can be read back off it
		// afterwards. Nothing is set on it - the defaults are what a chapter window should get.
		//
		// NO DATA INTERFACE = FAILURE, and the command is not run at all. The recipe this follows breaks
		// off here too (SDKLayoutHelper.cpp:268-272). Running the command anyway would have to skip the
		// window test below, and return true without having established the one thing this function's
		// true means - that the chapter now has a window.
		InterfacePtr<IOpenLayoutPresentationCmdData> openData(cmd, IID_IOPENLAYOUTCMDDATA);
		if (openData == nil)
			return false;

		if (CmdUtils::ProcessCommand(cmd) != kSuccess)
			return false;		// whatever it raised goes back with the preserver above

		// Did a window actually appear? SDKLayoutHelper::OpenLayoutWindow (the SDK's own recipe for this
		// command) does not stop at the return code: it reads GetResultingPresentation() and checks an
		// IWindow comes out of it, because "the command succeeded" and "there is a window" are two
		// different statements. Saying so here matters - the caller reports this chapter as shown.
		InterfacePtr<IWindow> window(openData->GetResultingPresentation(), UseDefaultIID());
		return window != nil;
	}

	virtual bool GetPanelBookFile(IDFile& outFile)
	{
		return KFCBookPanelLookup::GetPanelBookFile(outFile);
	}

	/** CAlert::WarningAlert - THE OFFICIAL CALL FOR EXACTLY THIS: a message and a warning icon
	    (CAlert.h:75-79). The replace's "the results went stale" alert, worded by the model half
	    (KFCReplaceEngine.cpp, TellResultsWentStale - its notes say why this call and not another). */
	virtual void WarningAlert(const PMString& message)
	{
		CAlert::WarningAlert(message);
	}

	virtual bool GetPanelBookSelection(const IDFile& bookFile, std::vector<UID>& outContents, int32& outTotal)
	{
		return KFCBookPanelLookup::GetPanelBookSelection(bookFile, outContents, outTotal);
	}
};

CREATE_PMINTERFACE(KFCUIServices, kKFCUIServicesImpl)

// End, KFCUIServices.cpp.
