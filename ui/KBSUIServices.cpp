//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The UI half's side of IKBSUIServices.h (2026-10-01, the model/UI split): the progress bar, the
//  windows, the Book panel and the alert the model half asks for. AddIn'd on kSessionBoss from the UI
//  half's resource, so a background thread - which loads no UI plug-in - finds nothing there and the
//  model does without. Each answer is what KBSBookScope / the engines did themselves until that day,
//  carried over unchanged; the notes are theirs.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"			// SetItemList - kOpenLayoutCmdBoss takes the document as its item
#include "IDataBase.h"
#include "IDocumentPresentation.h"	// the predicate typedef / presentation handle
#include "IDocumentUIUtils.h"	// FindPresentationForDocument (has-a-window test)
#include "IOpenLayoutCmdData.h"	// GetResultingPresentation - did the window actually appear?
#include "IWindow.h"			// the window kOpenLayoutCmdBoss is supposed to have produced

// General includes:
#include "CAlert.h"
#include "CmdUtils.h"
#include "CPMUnknown.h"
#include "ErrorUtils.h"			// GlobalErrorStatePreserver / PMSetGlobalErrorCode
#include "LayoutUIID.h"			// kOpenLayoutCmdBoss
#include "ProgressBar.h"		// RangeProgressBar
#include "UIDList.h"
#include "Utils.h"

#include <new>					// std::nothrow

// Project includes:
#include "KFCUIID.h"				// kKBSUIServicesImpl
#include "IKBSUIServices.h"
#include "KBSBookPanelLookup.h"

namespace
{
	// (Carried from KBSBookScope.cpp on 2026-10-01 with the window test. In the UI half the stock
	//  FindPresCriteria::accept_all would be reachable; the local predicate is kept as it was.)
	/** Accepts every presentation.

	    ***** A LOCAL PREDICATE IS WHAT ADOBE ASKS FOR HERE. ***** The stock one exists and is named
	    FindPresCriteria::accept_all (DocumentPresFindCriteria.h:82), but that file's own preamble
	    (:40-46) says its implementations "are found in the WidgetBin shared library, so you cannot
	    use them from a model only plugin. Should the need arise you can create local
	    implementations" - and prints a two-line example of exactly this shape. So this is the
	    documented route, not a stand-in for one. (Until 2026-08-08 the note here said we kept our own
	    because we did not know where the stock objects live, which was no longer true and read like
	    an avoidable dependency.) KESCL carries the same predicate for the same reason. */
	bool KBSAcceptAnyPresentation(IDocumentPresentation* /*p*/)
	{
		return true;
	}

	/** One RangeProgressBar, held for the model half. Its four calls are the bar's own. */
	class KBSUIProgressBar : public KBSProgressBarUI
	{
	public:
		KBSUIProgressBar(const PMString& title, int32 startRange, int32 endRange, bool8 showImmediate, bool8 showCancel)
			: fBar(title, startRange, endRange, showImmediate, showCancel) {}
		virtual ~KBSUIProgressBar() {}

		virtual void	SetTaskText(const PMString& text, bool16 forceRedraw)	{ fBar.SetTaskText(text, forceRedraw); }
		virtual void	SetPosition(int32 newPosition)							{ fBar.SetPosition(newPosition); }
		virtual bool16	WasCancelled(bool8 setGlobalErrorState)					{ return fBar.WasCancelled(setGlobalErrorState); }
		virtual void	DisableChildProgressBars(bool16 disable)				{ fBar.DisableChildProgressBars(disable); }

	private:
		RangeProgressBar	fBar;
	};
}

class KBSUIServices : public CPMUnknown<IKBSUIServices>
{
public:
	KBSUIServices(IPMUnknown* boss) : CPMUnknown<IKBSUIServices>(boss) {}
	virtual ~KBSUIServices() {}

	virtual KBSProgressBarUI* NewProgressBar(const PMString& title, int32 startRange, int32 endRange,
		bool8 showImmediate, bool8 showCancel)
	{
		return new (std::nothrow) KBSUIProgressBar(title, startRange, endRange, showImmediate, showCancel);
	}

	/** Does this document have a WINDOW anywhere - front, or behind another tab? The
	    all-presentations search, because GetFrontmostPresentationForDocument answers nil for a
	    document sitting behind another tab (ShowChapterWindow has always asked it this way).

	    Asked by the held-chapter releases since 2026-08-05: a window makes a chapter the USER'S,
	    whoever raised the window. ShowChapterWindow and the jump take a chapter off the held list
	    when they raise one themselves, but a window can be raised behind this module's back - the
	    book panel lists every chapter, and double-clicking one there windows the very document being
	    held. A release that closed it then would take a window the user is looking at; and once they
	    had saved their work, not even the unsaved-work door would stand in the way. */
	virtual bool DocHasAnyWindow(const UIDRef& docRef)
	{
		IDataBase* db = docRef.GetDataBase();
		if (db == nil)
			return false;
		FindPresentation_PreferCriteria noPreference;
		return Utils<IDocumentUIUtils>()->FindPresentationForDocument(
			db, KBSAcceptAnyPresentation, noPreference) != nil;
	}

	/** The window half of KBSBookScope::ShowChapterWindow, carried over as it stood. */
	virtual bool OpenLayoutWindow(const UIDRef& docRef)
	{
		// ***** THE WINDOW IS ALLOWED NOT TO APPEAR - this function's false says so - so its error
		// state stays in here. ***** Preserve, then clear, exactly as the open in ReopenChapterDoc does
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
		// ***** NO DATA INTERFACE = FAILURE, and the command is not run at all. ***** The recipe this
		// follows breaks off here too (SDKLayoutHelper.cpp:268-272). It used to run the command anyway
		// and skip the window test when this was nil, which returned TRUE without having established the
		// one thing this function's true means - that the chapter now has a window (block 11 API audit,
		// 2026-08-08).
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
		return KBSBookPanelLookup::GetPanelBookFile(outFile);
	}

	/** CAlert::WarningAlert - THE OFFICIAL CALL FOR EXACTLY THIS: a message and a warning icon
	    (CAlert.h:75-79). The replace's "the results went stale" alert, worded by the model half
	    (KBSReplaceEngine.cpp, TellResultsWentStale - its notes say why this call and not another). */
	virtual void WarningAlert(const PMString& message)
	{
		CAlert::WarningAlert(message);
	}
};

CREATE_PMINTERFACE(KBSUIServices, kKBSUIServicesImpl)

// End, KBSUIServices.cpp.
