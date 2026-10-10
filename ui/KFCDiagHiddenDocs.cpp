//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCDiagHiddenDocs.h. Test builds only: without KFC_DIAG this file compiles to nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

#ifdef KFC_DIAG

// Interface includes:
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IDocumentUIUtils.h"			// FindPresentationForDocument - the has-a-window test KFCUIServices asks
#include "ISession.h"

// General includes:
#include "DocumentPresFindCriteria.h"	// FindPresCriteria::accept_all (WidgetBin - this is the UI half)
#include "PMString.h"
#include "Utils.h"

#include <string>

// Project includes:
#include "KFCDiag.h"					// KFC_DIAG_LOG
#include "KFCDiagHiddenDocs.h"
#include "KFCModelAccess.h"				// KFCChapters()->IsHeldDoc, KFCRuns()->IsAnyRunning

namespace
{
	// The checks standing now: an operation dispatched inside another (through a run's progress bar) is checked with the
	// outer one, when that ends.
	int32 gDepth = 0;
}

KFCDiagHiddenDocsCheck::KFCDiagHiddenDocsCheck(const char* where)
	: fWhere(where)
{
	++gDepth;
}

KFCDiagHiddenDocsCheck::~KFCDiagHiddenDocsCheck()
{
	if (--gDepth > 0)
		return;
	// A run of ours standing holds the chapter it is walking - nothing to say about that until it is over.
	if (KFCRuns()->IsAnyRunning())
		return;
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> app(session != nil ? session->QueryApplication() : nil);
	InterfacePtr<IDocumentList> docList(app != nil ? app->QueryDocumentList() : nil);
	if (docList == nil)
		return;
	int32 hidden = 0, held = 0;
	std::string named;
	const int32 count = docList->GetDocCount();
	for (int32 i = 0; i < count; ++i)
	{
		IDocument* const doc = docList->GetNthDoc(i);
		IDataBase* const db = (doc != nil) ? ::GetDataBase(doc) : nil;
		if (db == nil)
			continue;
		// Any presentation at all - the question KFCUIServices::DocHasAnyWindow asks for the model half.
		FindPresentation_PreferCriteria noPreference;
		if (Utils<IDocumentUIUtils>()->FindPresentationForDocument(db, &FindPresCriteria::accept_all, noPreference) != nil)
			continue;
		++hidden;
		const bool isHeld = KFCChapters()->IsHeldDoc(::GetUIDRef(doc));
		if (isHeld)
			++held;
		PMString name;
		doc->GetName(name);
		named += " [";
		named += name.GetUTF8String();
		named += isHeld ? "|held" : "|not-held";
		named += (db->IsModified() != kFalse) ? "|modified]" : "|clean]";
	}
	KFC_DIAG_LOG("HIDDENDOCS %s held=%d hidden=%d%s", fWhere, static_cast<int>(held), static_cast<int>(hidden), named.c_str());
}

#endif // KFC_DIAG

// End, KFCDiagHiddenDocs.cpp.
