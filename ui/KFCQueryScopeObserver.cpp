//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE QUERY DIALOG'S Runs on: LINE, FOLLOWED WHILE THE DIALOG IS OPEN, THE KFC PANEL SHOWN OR NOT (the author's call):
//  what the line is made of - the front document, the selection (a Search: the selection does not offer reads
//  Document) and Edit > Find/Change's Search: - is heard here, and the line asked again (KFCQueryDialogRefreshScope).
//  The panel's observer (KFCPanelTitle.cpp) hears the same moments only while the panel is shown, and the dialog
//  stands while it is closed: the line then went on naming the document in front when the dialog opened, and Run
//  would have run on another (measured - spec map QRY-18).
//
//  THE SDK'S SHAPE FOR IT: an ActiveSelectionObserver under an IID of its own (IID_IKFCQUERYSCOPEOBSERVER - the
//  dialog boss's IID_IOBSERVER is the dialog's observer), attached and detached by the dialog's observer as the dialog
//  opens and closes, as the product's ConditionStaticTextView.cpp drives its selection observer from its own attach
//  and detach. (No IControlViewObservers would do it on its own: kDialogBoss aggregates none.) The Find/Change
//  settings arrive through their subject, as the panel's observer hears them
//  (KFCPanelTitle::AttachToFindChangeOptions). Asked on every caret step, as the panel's observer asks: the line is
//  written only when its words change (KFCQueryDialog.cpp's ShowRunScope), and the words are the model's quick
//  answer - no document is walked.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "TextWalkerServiceProviderID.h"	// IID_IFINDCHANGEOPTIONS - the protocol the settings' changes arrive on

// General includes:
#include "SelectionObserver.h"				// ActiveSelectionObserver - the front document's selection, followed

// Project includes:
#include "KFCDiag.h"						// the test build's QUERYSCOPE lines - attached, detached
#include "KFCPanelTitle.h"					// AttachToFindChangeOptions
#include "KFCQueryDialog.h"					// KFCQueryDialogRefreshScope
#include "KFCUIID.h"

/** On kKFCQueryDialogBoss, beside the dialog's observer, which attaches it as the dialog opens and detaches it as the
    dialog closes. */
class KFCQueryScopeObserver : public ActiveSelectionObserver
{
public:
	KFCQueryScopeObserver(IPMUnknown* boss) :
		ActiveSelectionObserver(boss, IID_IKFCQUERYSCOPEOBSERVER), fAttached(false)
	{
	}
	virtual ~KFCQueryScopeObserver() {}

	virtual void AutoAttach()
	{
		// ONCE, until it is detached: ActiveSelectionObserver's source is not in the SDK, so whether a second attach
		// puts this on the active context twice cannot be read - and one left on it would be called after the dialog is
		// gone.
		if (fAttached)
			return;
		fAttached = true;
		// The selection first - SelectionObserver.h asks for the inherited method before a subclass attaches to
		// anything of its own.
		ActiveSelectionObserver::AutoAttach();
		KFCPanelTitle::AttachToFindChangeOptions(this, true, IID_IKFCQUERYSCOPEOBSERVER);
		KFC_DIAG_LOG("QUERYSCOPE attached");
	}

	virtual void AutoDetach()
	{
		if (!fAttached)
			return;
		fAttached = false;
		KFCPanelTitle::AttachToFindChangeOptions(this, false, IID_IKFCQUERYSCOPEOBSERVER);
		// The selection last - the mirror of AutoAttach (SelectionObserver.h).
		ActiveSelectionObserver::AutoDetach();
		KFC_DIAG_LOG("QUERYSCOPE detached");
	}

protected:
	// A new selection - a front document changed brings one - or different items in it.
	virtual void HandleSelectionChanged(const ISelectionMessage* /*message*/)
	{
		KFCQueryDialogRefreshScope();
	}

	// Every caret step: a caret into a story or out of one is what Search: Story turns on.
	virtual void HandleFrequentSelectionChanged()
	{
		KFCQueryDialogRefreshScope();
	}

	// What is not the selection's: the Find/Change settings. The base first - it routes the selection's own messages to
	// the two above (SelectionObserver.h).
	virtual void HandleSelectionUpdate(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol,
		void* changedBy)
	{
		ActiveSelectionObserver::HandleSelectionUpdate(theChange, theSubject, protocol, changedBy);
		if (protocol == IID_IFINDCHANGEOPTIONS)
			KFCQueryDialogRefreshScope();
	}

private:
	bool fAttached;		// between this observer's AutoAttach and its AutoDetach
};

CREATE_PMINTERFACE(KFCQueryScopeObserver, kKFCQueryScopeObserverImpl)

// End, KFCQueryScopeObserver.cpp.
