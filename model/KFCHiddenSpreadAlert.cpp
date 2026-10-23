//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCHiddenSpreadAlert.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IObjectWalker.h"			// GetCurrentItem - the item a walk stands on

// General includes:
#include "CAlert.h"					// GetShowAlerts / SetShowAlerts; kOKString
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - every entry point's gate

// Project includes:
#include "KFCDiag.h"				// KFC_DIAG_LOG - the HSALERT trace, test builds only
#include "KFCHiddenSpreadAlert.h"
#include "KFCModelTypes.h"			// kKFCAlertShowIt / kKFCAlertLeaveToDefault
#include "KFCObjectSearch.h"		// QuerySharedWalker
#include "KFCPageItemFacts.h"		// SpreadOf / IsSpreadHidden

namespace
{
// THE ANSWERING STANDING NOW - one at a time, on the main thread (each entry point's gate). Its database is compared
// with the walker's item's, never read through.
bool		gArmed = false;
IDataBase*	gArmedDB = nil;
bool		gShowAlertsWas = true;		// CAlert::GetShowAlerts as the Answering found it - put back as it goes

// THE ALERT, BY ITS KEY: InDesign's "Cannot show the object: the item is on a hidden spread" comes to a handler as
// kObjectOnHiddenSpread, its buttons as Unhide Spread (the default, 1) and OK (2) - measured, the test build's HSALERT
// trace. The handler is handed the alert's strings untranslated (IAlertHandler.h - "All of the strings are
// non-translated strings"): each compared as a key, as PMString's operator== (ConstCString) compares one. A key
// InDesign renames one day matches nothing here, and its alert then comes on screen as before - never answered blind.
const char* const kHiddenSpreadAlertKey = "kObjectOnHiddenSpread";

bool IsKey(const PMString& s, const char* key)
{
	return !s.IsEmpty() && s == key;
}
}	// anonymous namespace

KFCHiddenSpreadAlert::Answering::Answering(IDataBase* db, bool docHasHidden) : fArmed(false)
{
	if (!docHasHidden || db == nil || gArmed || !IDThreading::IsMainThreadDomain())
		return;
	gShowAlertsWas = CAlert::GetShowAlerts() != kFalse;
	gArmed = true;
	gArmedDB = db;
	fArmed = true;
	CAlert::SetShowAlerts(kFalse);
}

KFCHiddenSpreadAlert::Answering::~Answering()
{
	if (!fArmed)
		return;
	CAlert::SetShowAlerts(gShowAlertsWas ? kTrue : kFalse);
	gArmed = false;
	gArmedDB = nil;
}

bool KFCHiddenSpreadAlert::IsArmed()
{
	return gArmed && IDThreading::IsMainThreadDomain();
}

int16 KFCHiddenSpreadAlert::AnswerAlert(const PMString& message, const PMString& button1, const PMString& button2,
	const PMString& button3, int16 defaultButton)
{
	if (!IsArmed())
		return kKFCAlertLeaveToDefault;
	// THE ITEM THE WALK STANDS ON - the one InDesign is showing: SearchObject has moved the shared walker onto its
	// find, and ReplaceObject writes the walker's current item.
	UID item = kInvalidUID;
	bool onHidden = false;
	InterfacePtr<IObjectWalker> shared(KFCObjectSearch::QuerySharedWalker());
	const UIDRef current = (shared != nil) ? shared->GetCurrentItem() : UIDRef();
	if (current.GetDataBase() == gArmedDB && current.GetUID() != kInvalidUID && gArmedDB->IsValidUID(current.GetUID()))
	{
		item = current.GetUID();
		onHidden = KFCPageItemFacts::IsSpreadHidden(gArmedDB, KFCPageItemFacts::SpreadOf(current));
	}
	int16 ok = 0;
	if (IsKey(button1, kOKString))
		ok = 1;
	else if (IsKey(button2, kOKString))
		ok = 2;
	else if (IsKey(button3, kOKString))
		ok = 3;
	// OK only to that alert, about that item: the alert by its key, the item on a hidden spread, an OK to press.
	const bool theAlert = IsKey(message, kHiddenSpreadAlertKey);
	const int16 answer = (theAlert && onHidden && ok > 0) ? ok
		: static_cast<int16>(gShowAlertsWas ? kKFCAlertShowIt : kKFCAlertLeaveToDefault);
	KFC_DIAG_LOG("HSALERT item=%u hidden=%d buttons=[%s|%s|%s] default=%d answer=%d message=[%s]",
		static_cast<unsigned>(item.Get()), onHidden ? 1 : 0, button1.GetUTF8String().c_str(),
		button2.GetUTF8String().c_str(), button3.GetUTF8String().c_str(), static_cast<int>(defaultButton),
		static_cast<int>(answer), message.GetUTF8String().c_str());
	return answer;
}

// End, KFCHiddenSpreadAlert.cpp.
