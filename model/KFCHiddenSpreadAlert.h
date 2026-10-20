//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  InDesign's HIDDEN-SPREAD ALERT, ANSWERED OK INSIDE A WALK OF OURS (the spec map's OBJ-23 - the author's call: an
//  object search lists the items of a hidden spread, its rows marked "hidden spread").
//
//  InDesign's object search SHOWS what it finds (IFindChangeService::SearchObject - and ReplaceObject the item it
//  writes). An item on a hidden spread cannot be shown, and InDesign says so inside the call: "Cannot show the object:
//  the item is on a hidden spread", with two buttons - Unhide Spread, the default, and OK (measured). The default shows
//  the spread: a search would change the document. With alerts switched off (CAlert::SetShowAlerts) InDesign takes the
//  default by itself - the same. OK changes nothing, and the item is handed back as found: the walk goes on (measured).
//  Change All (ReplaceAllObject) shows nothing, and replaces a hidden spread's items with no alert at all (measured).
//
//  So each such call over a document that holds a hidden spread (KFCPageItemFacts::HasHiddenSpread) is made under an
//  Answering: alerts switched off for the call, and the UI half's alert handler (KFCAlertAnswer.cpp - IAlertHandler,
//  the SDK's door for a plug-in to choose an alert's button: "If alerts are set to not show ... we ask the service
//  registry for a list of IAlertHandlers, and ask them to handle the alert instead") asks here which button: OK when
//  the alert is that one (by its key - the .cpp says which, measured), the item the shared object walker stands on is
//  on a hidden spread, and the alert offers OK. Any other alert is put on screen as it would have been - or, when
//  alerts were already off (a script's run), left to its default as before.
//
//  Main thread only: a walk of ours runs there, and so does the alert it raises.
//
//========================================================================================

#ifndef __KFCHiddenSpreadAlert_h__
#define __KFCHiddenSpreadAlert_h__

#include "PMString.h"

class IDataBase;

namespace KFCHiddenSpreadAlert
{
	/** One call of InDesign's object search (SearchObject / ReplaceObject) over `db`, made on the stack around that
	    call alone: armed when docHasHidden - the document holds a hidden spread - with alerts switched off for its
	    length; nothing at all otherwise, or off the main thread. Answerings do not nest: an inner one does nothing. */
	class Answering
	{
	public:
		Answering(IDataBase* db, bool docHasHidden);
		~Answering();
	private:
		bool	fArmed;
		bool	fShowAlertsWas;
		Answering(const Answering&);
		Answering& operator=(const Answering&);
	};

	/** Is an Answering standing now - the UI half's WantToOverrideAlert (IKFCRuns::IsAnsweringAlerts). */
	bool IsArmed();

	/** The button for an alert raised while one stands - the UI half's HandleAlert (IKFCRuns::AnswerAlert): OK's number
	    (1-3) when the alert is InDesign's hidden-spread one, the item the shared object walker stands on is on a hidden
	    spread of the armed document and the alert offers OK; otherwise kKFCAlertShowIt - alerts were on before the
	    Answering, so the alert goes on screen as it would have - or kKFCAlertLeaveToDefault - they were off, or none
	    stands (KFCModelTypes.h). */
	int16 AnswerAlert(const PMString& message, const PMString& button1, const PMString& button2,
		const PMString& button3, int16 defaultButton);
}

#endif // __KFCHiddenSpreadAlert_h__
