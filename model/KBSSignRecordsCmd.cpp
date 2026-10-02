//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  kKBSSignRecordsCmdBoss (2026-09-28): the tracked changes one replace made, signed "KohakuFindChange"
//  at the row's time - KBSTrackChange::SignRecordsNow does the work; this wraps it in a command so the
//  change to the model goes through a command, inside the run's sequence (one Ctrl+Z takes the replace
//  and its signature back together, and a Redo brings both - measured on the spike, 2026-09-28).
//  KBSInt64Data: IInt64Data (IID_IINT64DATA, CommandID.h) ships with only a PERSISTENT implementation,
//  kPersistInt64DataImpl (ShuksanID.h:1145), which no boss of the product's carries (the 20.5 boss dump has
//  no IID_IINT64DATA at all). A command's data lives outside the database, and for 32 bits the SDK gives
//  commands a plain one beside the persistent one - kIntDataImpl (CommandID.h:164) / kPersistIntDataImpl
//  (ShuksanID.h:828) - so this is that plain one, for 64. (Until 2026-10-02 this said the SDK had "no
//  stock implementation" - it has the persistent one.)
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IInt64Data.h"
#include "IRangeData.h"

// General includes:
#include "CPMUnknown.h"
#include "Command.h"
#include "ErrorUtils.h"
#include "UIDList.h"

// Project includes:
#include "KBSID.h"
#include "KBSTrackChange.h"

class KBSSignRecordsCmd : public Command
{
public:
	KBSSignRecordsCmd(IPMUnknown* boss) : Command(boss) {}
protected:
	virtual void Do();
	virtual PMString* CreateName();
};

CREATE_PMINTERFACE(KBSSignRecordsCmd, kKBSSignRecordsCmdImpl)

void KBSSignRecordsCmd::Do()
{
	bool ok = false;
	const UIDList& items = this->GetItemListReference();
	InterfacePtr<IRangeData> range(this, UseDefaultIID());
	InterfacePtr<IInt64Data> stamp(this, UseDefaultIID());
	if (items.Length() == 1 && range != nil && stamp != nil)
		ok = KBSTrackChange::SignRecordsNow(items.GetRef(0), range->GetStart(nil), range->GetEnd(),
			static_cast<uint64>(stamp->Get()));
	if (!ok)
		ErrorUtils::PMSetGlobalErrorCode(kFailure);
}

PMString* KBSSignRecordsCmd::CreateName()
{
	PMString* name = new PMString("Sign Tracked Changes");
	name->SetTranslatable(kFalse);
	return name;
}

class KBSInt64Data : public CPMUnknown<IInt64Data>
{
public:
	KBSInt64Data(IPMUnknown* boss) : CPMUnknown<IInt64Data>(boss), fValue(0) {}
	virtual void Set(ValueType i) { fValue = i; }
	virtual ValueType GetInt() const { return fValue; }
private:
	ValueType fValue;
};

CREATE_PMINTERFACE(KBSInt64Data, kKBSInt64DataImpl)
