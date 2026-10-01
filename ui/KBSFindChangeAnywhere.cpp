//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  ***** EDIT > FIND/CHANGE WITH NO DOCUMENT OPEN (2026-10-01, the user's request). *****
//  InDesign greys Edit > Find/Change - and with it Ctrl+F - while no document is open. With only a
//  book open (the Book panel and nothing else) the query cannot be set, so Find in Book has nothing
//  to search with. The dialog itself does not need a document: opened over one and left standing, it
//  stays when the last document closes (the user's observation, 2026-10-01).
//
//  So the action's own enabling is what stands in the way, and the SDK's way to change it is an
//  action filter (IActionFilter.h): every IActionManager::AddAction calls the filters, which may
//  change anything about the action being added - here only the enabling bits that grey it out with
//  no document (ActionDefs.h: kDisableIfNoFrontDocument, kDisableIfNoFrontLayoutView). The action
//  keeps its component, its name, its shortcut and every other enabling bit. The boss is the three
//  lines IActionFilter.h prints, as customactionfilter (CstAFlt.fr) has them.
//
//  User interface: it changes a menu item. It goes to the UI plug-in with the model/UI split.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActionFilter.h"

// General includes:
#include "ActionDefs.h"			// kDisableIfNoFrontDocument / kDisableIfNoFrontLayoutView
#include "CPMUnknown.h"
#include "FindChangeID.h"		// kFindDialogActionID - Edit > Find/Change

// Project includes:
#include "KFCUIID.h"

class KBSFindChangeAnywhere : public CPMUnknown<IActionFilter>
{
public:
	KBSFindChangeAnywhere(IPMUnknown* boss) : CPMUnknown<IActionFilter>(boss) {}
	virtual ~KBSFindChangeAnywhere() {}

	virtual void FilterAction(ClassID* componentClass, ActionID* actionID, PMString* actionName,
		PMString* actionArea, int16* actionType, uint32* enablingType, bool16* userEditable)
	{
		// Called for every action InDesign adds - thousands at startup - so the one question that
		// decides is asked first and alone (the sample asks it last; memory action-filter-hijack-recipe).
		if (actionID == nil || *actionID != kFindDialogActionID || enablingType == nil)
			return;

		// Measured 2026-10-01: InDesign adds the action with kDisableIfNoFrontDocument | kDisableIfLowMem (0x84)
		// and no custom enabling, so taking the first bit off is the whole change - with no document open the
		// menu item is enabled, the dialog opens from it and from Ctrl+F, and Find in Book runs on what is
		// typed there. The filter runs only when the action is added (a first launch, or after the plug-in set
		// changes); on later launches the action comes back from the saved data with the bit already off.
		*enablingType &= ~static_cast<uint32>(kDisableIfNoFrontDocument | kDisableIfNoFrontLayoutView);
	}
};

CREATE_PMINTERFACE(KBSFindChangeAnywhere, kKBSFindChangeAnywhereImpl)

// End, KBSFindChangeAnywhere.cpp.
