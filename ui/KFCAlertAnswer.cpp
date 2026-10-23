//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  INDESIGN'S HIDDEN-SPREAD ALERT, ANSWERED OK INSIDE A WALK OF OURS (the spec map's OBJ-23): the UI half's alert
//  handler. Why and when is the model half's (model/KFCHiddenSpreadAlert.h): a walk of InDesign's object search over a
//  document that holds a hidden spread switches alerts off around each call, and InDesign then asks the alert handlers
//  in the service registry instead of showing its alert - "If alerts are set to not show, rather than showing an alert
//  we ask the service registry for a list of IAlertHandlers, and ask them to handle the alert instead. They can then
//  choose which button to press in the alert." (IAlertHandler.h). Which button is the model half's answer too
//  (IKFCRuns::AnswerAlert): OK for InDesign's "Cannot show the object: the item is on a hidden spread", whose default -
//  Unhide Spread - would change the document; any other alert put on screen as it would have been.
//
//  No worked example in the SDK - the header and one #include in PublicTestHeader.h are the whole of it. The shape:
//  the handler on a boss of its own, whose service provider registers it under kAlertHandlerService - KIDMCP's
//  alert handler had it, measured there (retired since: its git keeps it at d7e1666, ui/KIDMCPUIAlertHandler.cpp).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IAlertHandler.h"			// the interface the alert machinery looks for in the registry
#include "IK2ServiceProvider.h"		// InstancePerX

// General includes:
#include "CAlert.h"					// SetShowAlerts / ModalAlert - another alert put on screen as it would have been
#include "CPMUnknown.h"
#include "CServiceProvider.h"
#include "PMString.h"
#include "widgetid.h"				// kAlertHandlerService - the ID this boss registers under

// Project includes:
#include "KFCUIID.h"				// kKFCAlertAnswerImpl / kKFCAlertAnswerServiceImpl
#include "KFCModelAccess.h"			// KFCRuns() - IsAnsweringAlerts / AnswerAlert; kKFCAlertShowIt (KFCModelTypes.h)

namespace
{
/** The registry's own switch for THIS handler (IAlertHandler::SetOverrideAlert), apart from whether a walk of ours is
    answering - kept apart: whatever turns the handler off need not know why it was on. */
bool gRegistryWantsUs = true;
}	// anonymous namespace

/** Answers InDesign's hidden-spread alert OK while a walk of ours stands in InDesign's object search.
	@ingroup KFC
*/
class KFCAlertAnswer : public CPMUnknown<IAlertHandler>
{
public:
	KFCAlertAnswer(IPMUnknown* boss) : CPMUnknown<IAlertHandler>(boss) {}
	virtual ~KFCAlertAnswer() {}

	/** kFalse leaves the alert to any other handler, as if this one were not installed - every alert but one raised
	    inside a walk of ours (IKFCRuns::IsAnsweringAlerts). */
	virtual bool16 WantToOverrideAlert()
	{
		return (gRegistryWantsUs && KFCRuns()->IsAnsweringAlerts()) ? kTrue : kFalse;
	}

	virtual void SetOverrideAlert(bool16 override) { gRegistryWantsUs = (override != kFalse); }

	/** The model half's answer (IKFCRuns::AnswerAlert - asked again here: KIDMCP measured a HandleAlert that no
	    WantToOverrideAlert answering kTrue had come before): a button, or the alert put on screen as it would have
	    been, or its default. */
	virtual int16 HandleAlert(const PMString& message, const PMString& button1String, const PMString& button2String,
		const PMString& button3String, int16 defaultButton)
	{
		const int16 answer = KFCRuns()->AnswerAlert(message, button1String, button2String, button3String,
			defaultButton);
		if (answer > 0)
			return answer;
		if (answer != kKFCAlertShowIt)
			return defaultButton;
		// ANOTHER ALERT, WHERE ALERTS WERE ON before the walk switched them off: shown as it would have been - for this
		// call alone, then off again for the rest of the walk's call. (The icon is not handed to a handler; a warning's
		// is the one shown.)
		CAlert::SetShowAlerts(kTrue);
		const int16 pressed = CAlert::ModalAlert(message, button1String, button2String, button3String, defaultButton,
			CAlert::eWarningIcon);
		CAlert::SetShowAlerts(kFalse);
		return pressed;
	}
};

CREATE_PMINTERFACE(KFCAlertAnswer, kKFCAlertAnswerImpl)

/** Puts the handler above into the service registry, under kAlertHandlerService.
	@ingroup KFC
*/
class KFCAlertAnswerService : public CServiceProvider
{
public:
	KFCAlertAnswerService(IPMUnknown* boss) : CServiceProvider(boss) {}
	virtual ~KFCAlertAnswerService() {}

	virtual ServiceID GetServiceID() { return kAlertHandlerService; }
	virtual bool16 IsDefaultServiceProvider() { return kFalse; }
	virtual InstancePerX GetInstantiationPolicy() { return IK2ServiceProvider::kInstancePerSession; }
	// (An internal name, no string-table key - the encoding form, as KFCBookPanelServiceProvider says.)
	virtual void GetName(PMString* pName) { pName->SetCString("KFCAlertAnswerService", PMString::kEncodingASCII); }
	// (No GetThreadingPolicy: CServiceProvider answers by the plug-in's kind - kMainThreadOnly for this UI half.)
};

CREATE_PMINTERFACE(KFCAlertAnswerService, kKFCAlertAnswerServiceImpl)

// End, KFCAlertAnswer.cpp.
