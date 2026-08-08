//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  Scripting: app.kfcStatus - the last message the panel put on its status line, read-only.
//
//  WHY THIS EXISTS
//
//  The panel answers in one line: how many hits, in how many chapters, which chapters could not be
//  opened, how many rows a replace left alone and why. That line is the plug-in's whole account of
//  what it just did - and until now the only way to read it was to look at the screen. Verifying a
//  change therefore meant a person taking a screenshot, which is slow, and impossible to automate.
//
//  A script property turns that into one line of JavaScript, which means it is also reachable over
//  COM from PowerShell:
//
//      $app = New-Object -ComObject "InDesign.Application.2026"
//      $app.DoScript("app.kfcStatus", 1246973031)
//
//  READ-ONLY on purpose. Setting it would let a script write something the panel never said, which
//  is exactly the property this is useful for not having.
//
//  It answers even when the panel is CLOSED. KBSResultTree::ShowStatus keeps the string in the
//  module whatever happens to the panel widget, so a search run from a script and read back from a
//  script needs no panel on screen at all.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IScript.h"
#include "IScriptErrorUtils.h"	// SetReadOnlyPropertyErrorData - how the base class refuses a put
#include "IScriptRequestData.h"

// General includes:
#include "CScriptProvider.h"
#include "ScriptData.h"
#include "WideString.h"

// Project includes:
#include "KBSID.h"
#include "KBSScriptingDefs.h"
#include "KBSResultTree.h"
#include "KBSResultModel.h"		// DescribeAllRows - the rows behind the status line

/** Serves this plug-in's scripting additions. One property, on the application object. */
class KBSScriptProvider : public CScriptProvider
{
public:
	KBSScriptProvider(IPMUnknown* boss) : CScriptProvider(boss) {}
	virtual ~KBSScriptProvider() {}

	/** Read (or refuse to write) one of our properties. Anything not ours goes to the base class,
	    which is what keeps the rest of the application's scripting working on this object. */
	virtual ErrorCode AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script);
};

CREATE_PMINTERFACE(KBSScriptProvider, kKBSScriptProviderImpl)

ErrorCode KBSScriptProvider::AccessProperty(ScriptID propID, IScriptRequestData* data, IScript* script)
{
	if (propID.Get() != p_KBSStatus && propID.Get() != p_KBSResults)
		return CScriptProvider::AccessProperty(propID, data, script);

	if (data == nil)
		return kFailure;

	// Read-only. The declaration in KBS.fr says kReadOnly, so the engine should refuse an assignment
	// before it ever reaches here; this is the backstop, and it refuses rather than quietly accepting
	// a value that would then not be there on the next read.
	//
	// Refused the way the base class refuses a put on the read-only properties IT owns:
	// CScriptProvider.cpp:369 (parent), :1096 (object), :1116 (id) and :1135 (index) all end with this
	// same call. A bare kFailure reaches the script as a failure that says nothing about why; this
	// names the property and the reason (IScriptErrorUtils.h:67-73).
	if (data->IsPropertyPut())
		return Utils<IScriptErrorUtils>()->SetReadOnlyPropertyErrorData(data, propID);

	if (!data->IsPropertyGet())
		return CScriptProvider::AccessProperty(propID, data, script);

	// The status line is one sentence about the last run; the result block is every row behind it.
	// Both are read the same way and neither can be written, so they share this one handler.
	PMString value;
	if (propID.Get() == p_KBSStatus)
		KBSResultTree::GetLastStatus(value);
	else
		KBSResultModel::DescribeAllRows(value);

	ScriptData outputData;
	outputData.SetWideString(WideString(value));
	data->AppendReturnData(script, propID, outputData);
	return kSuccess;
}

// End, KBSScriptProvider.cpp.
