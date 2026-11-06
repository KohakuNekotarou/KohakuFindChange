//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Special Operations > Label's dialogs - see KFCLabelDialogs.h. Asked as the value dialogs ask (KFCSpecialDialogs.cpp - snippetrunner's
//  SnipRunParameterUtils.cpp): the dialog made and opened, Open() waiting until it is dismissed (IDialog.h: "Open by default now calls
//  WaitForDialog"), the answer then read from what the controller kept on OK. One dialog boss for the three (kKFCLabelDialogBoss - KFCUI.fr), each its
//  own view; which one stands is the question being asked (gAsked - modal, so one at a time), and everything it reads and writes lives in the asking
//  function's frame - no container outlives a dialog.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IControlView.h"			// GetWidgetID - which widget a change came from
#include "IDialog.h"
#include "IDialogController.h"		// the field helpers, asked by the observer of the controller beside it
#include "IDialogMgr.h"
#include "IPanelControlData.h"		// FindWidget - the list and the fields
#include "ISession.h"
#include "ISubject.h"				// the observer's Update hears one
#include "widgetid.h"				// IID_ITREEVIEWCONTROLLER / kListSelectionChangedMessage

// General includes:
#include "CAlert.h"					// WarningAlert - an answer refused
#include "CDialogController.h"
#include "CDialogObserver.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "LocaleSetting.h"
#include "PMString.h"
#include "RsrcSpec.h"

#include <string>
#include <vector>

// Project includes:
#include "KFCLabelDialogs.h"
#include "KFCQueryList.h"			// the keys' list - the query dialog's flat list, told apart by its widget id
#include "KFCDiag.h"				// the test build's fault switch label-answer, the LABELDLG trace
#include "KFCUIID.h"

namespace
{
// WHICH QUESTION STANDS.
enum LabelAsking { kLabelAskingAdd = 1, kLabelAskingEdit, kLabelAskingRemove };

// EVERYTHING A DIALOG READS AND WRITES - in the asking function's frame (gAsked points at it while the dialog stands): the keys the rows' holders have, how
// many holders were read, what was typed for each key (Edit Value), the key the field shows, and the answer.
struct Asked
{
	LabelAsking						asking;
	const std::vector<KFCLabelKey>*	keys;
	int32							holders;
	std::vector<PMString>			typed;		// Edit Value: a key's value as typed...
	std::vector<bool>				changed;	// ...when it differs from what the key had
	int32							shown;		// Edit Value: the key whose value the field shows (-1 none)
	bool							answered;	// OK went through
	KFCLabelEdit*					add;		// Add Key's answer
	PMString*						remove;		// Remove Key's answer
	Asked() : asking(kLabelAskingAdd), keys(nil), holders(0), shown(-1), answered(false), add(nil), remove(nil) {}
};
Asked* gAsked = nil;

// A STRING'S LINE BREAKS AS ONE KIND - LF (the field gives CR LF for each, whatever it was given - memory multiline-text-widget-linebreaks): what a value is
// compared by.
std::string Bare(const PMString& s)
{
	std::string text(s.GetUTF8String());
	std::string out;
	out.reserve(text.size());
	for (size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\r')
		{
			out += '\n';
			if (i + 1 < text.size() && text[i + 1] == '\n')
				++i;
		}
		else
			out += text[i];
	}
	return out;
}

// WHAT THE FIELD GAVE, ITS LINE BREAKS AS THE VALUE IT REPLACES HAD THEM - CR alone, or LF alone; as the field gives them (CR LF) when the value had none,
// or both kinds, or there was no value (InDesign's own Script Label panel is a Windows edit box too).
PMString AsBefore(const PMString& typed, const PMString& before)
{
	const std::string old(before.GetUTF8String());
	const bool cr = old.find('\r') != std::string::npos, lf = old.find('\n') != std::string::npos, crlf = old.find("\r\n") != std::string::npos;
	if (crlf || cr == lf)
		return typed;
	std::string text(Bare(typed));
	if (cr)
		for (size_t i = 0; i < text.size(); ++i)
			if (text[i] == '\n')
				text[i] = '\r';
	PMString out;
	out.SetUTF8String(text);
	out.SetTranslatable(kFalse);
	return out;
}

// What a key had - its value when every holder that has it has the same one, else nothing.
PMString KeyValueBefore(int32 index)
{
	PMString none;
	none.SetTranslatable(kFalse);
	if (gAsked == nil || gAsked->keys == nil || index < 0 || index >= static_cast<int32>(gAsked->keys->size()))
		return none;
	const KFCLabelKey& key = (*gAsked->keys)[static_cast<size_t>(index)];
	return key.same ? key.value : none;
}

// EDIT VALUE: what the field holds now kept for the key it shows, when it reads otherwise than the key had (its line breaks aside).
void CommitShown(IDialogController* controller)
{
	if (gAsked == nil || gAsked->asking != kLabelAskingEdit || gAsked->shown < 0 || controller == nil)
		return;
	const size_t at = static_cast<size_t>(gAsked->shown);
	PMString now(controller->GetTextControlData(kKFCLabelValueFieldWidgetID));
	now.SetTranslatable(kFalse);
	const PMString before(gAsked->changed[at] ? gAsked->typed[at] : KeyValueBefore(gAsked->shown));
	if (Bare(now) == Bare(before))
		return;
	gAsked->typed[at] = AsBefore(now, KeyValueBefore(gAsked->shown));
	gAsked->changed[at] = true;
}

// EDIT VALUE: the key picked shown - its value as typed, or as it is; the note says when the holders' values differ, or not every holder has it.
void ShowKey(IDialogController* controller, int32 index)
{
	if (gAsked == nil || gAsked->keys == nil || controller == nil || index < 0 || index >= static_cast<int32>(gAsked->keys->size()) || index == gAsked->shown)
		return;
	gAsked->shown = index;
	const size_t at = static_cast<size_t>(index);
	const KFCLabelKey& key = (*gAsked->keys)[at];
	PMString value(gAsked->changed[at] ? gAsked->typed[at] : KeyValueBefore(index));
	value.SetTranslatable(kFalse);
	controller->SetTextControlData(kKFCLabelValueFieldWidgetID, value, nil, kTrue, kFalse);
	PMString note;
	if (!key.same && !gAsked->changed[at])
		note = PMString(kKFCLabelValuesDifferKey, PMString::kTranslateDuringCall);
	else if (key.count < gAsked->holders)
	{
		note = PMString(kKFCLabelSomeHaveKey, PMString::kTranslateDuringCall);
		PMString n, of;
		n.AppendNumber(key.count);
		of.AppendNumber(gAsked->holders);
		note.Append(" (");
		note.Append(n);
		note.Append(" / ");
		note.Append(of);
		note.Append(")");
	}
	note.SetTranslatable(kFalse);
	controller->SetTextControlData(kKFCLabelNoteWidgetID, note, nil, kTrue, kFalse);
}

// The keys a test build's trace names, as LABELDLG reads them: "<key>:<count>:<same|varies>" each - the Script Label panel's as "[panel]".
std::string KeysWords(const std::vector<KFCLabelKey>& keys)
{
	std::string words;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		words += (i > 0) ? " " : "";
		words += keys[i].panel ? std::string("[panel]") : std::string(keys[i].key.GetUTF8String());
		words += ":" + std::to_string(keys[i].count) + (keys[i].same ? ":same" : ":varies");
	}
	return words;
}
}	// anonymous namespace

/** The Label dialogs' controller: fills the dialog that stands, checks it on OK and keeps the answer. */
class KFCLabelDialogController : public CDialogController
{
public:
	KFCLabelDialogController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCLabelDialogController() {}

protected:
	// Filled at every open (a cached dialog too), without a word to the observer (notifyOfChange kFalse); the first field or the list takes the keyboard.
	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		if (gAsked == nil)
			return;
		PMString empty;
		empty.SetTranslatable(kFalse);
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		switch (gAsked->asking)
		{
			case kLabelAskingAdd:
				SetTextControlData(kKFCLabelKeyFieldWidgetID, empty, nil, kTrue, kFalse);
				SetTextControlData(kKFCLabelValueFieldWidgetID, empty, nil, kTrue, kFalse);
				SelectDialogWidget(kKFCLabelKeyFieldWidgetID);
				break;
			case kLabelAskingEdit:
				KFCQueryListRebuild(panel, kKFCLabelKeyListWidgetID);
				gAsked->shown = -1;
				ShowKey(this, 0);
				KFCQueryListSelect(panel, kKFCLabelKeyListWidgetID, 0);
				SelectDialogWidget(kKFCLabelValueFieldWidgetID);
				break;
			case kLabelAskingRemove:
				KFCQueryListRebuild(panel, kKFCLabelKeyListWidgetID);
				KFCQueryListSelect(panel, kKFCLabelKeyListWidgetID, 0);
				SelectDialogWidget(kKFCLabelKeyListWidgetID);
				break;
		}
	}

	// ON OK. Add Key: a key and a value both (InDesign keeps no key with an empty value - insertLabel's rule). Remove Key: a key picked. The field
	// returned is the one the framework selects (CDialogObserver.cpp).
	virtual WidgetID ValidateDialogFields(IActiveContext* /*context*/)
	{
		if (gAsked == nil)
			return kNoInvalidWidgets;
		if (gAsked->asking == kLabelAskingAdd)
		{
			if (GetTextControlData(kKFCLabelKeyFieldWidgetID).IsEmpty())
			{
				CAlert::WarningAlert(PMString(kKFCLabelNoKeyKey));
				return kKFCLabelKeyFieldWidgetID;
			}
			if (GetTextControlData(kKFCLabelValueFieldWidgetID).IsEmpty())
			{
				CAlert::WarningAlert(PMString(kKFCLabelNoValueKey));
				return kKFCLabelValueFieldWidgetID;
			}
		}
		else if (gAsked->asking == kLabelAskingRemove)
		{
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			if (KFCQueryListSelectedIndex(panel, kKFCLabelKeyListWidgetID) < 0)
			{
				CAlert::WarningAlert(PMString(kKFCLabelPickKeyKey));
				return kKFCLabelKeyListWidgetID;
			}
		}
		return kNoInvalidWidgets;
	}

	// ON OK, CHECKED: the answer kept for the asking function.
	virtual void ApplyDialogFields(IActiveContext* /*context*/, const WidgetID& /*widgetId*/)
	{
		if (gAsked == nil)
			return;
		switch (gAsked->asking)
		{
			case kLabelAskingAdd:
				if (gAsked->add != nil)
				{
					gAsked->add->key = GetTextControlData(kKFCLabelKeyFieldWidgetID);
					gAsked->add->key.SetTranslatable(kFalse);
					gAsked->add->value = GetTextControlData(kKFCLabelValueFieldWidgetID);
					gAsked->add->value.SetTranslatable(kFalse);
				}
				break;
			case kLabelAskingEdit:
				CommitShown(this);
				break;
			case kLabelAskingRemove:
			{
				InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
				const int32 picked = KFCQueryListSelectedIndex(panel, kKFCLabelKeyListWidgetID);
				if (gAsked->remove != nil && gAsked->keys != nil && picked >= 0 && picked < static_cast<int32>(gAsked->keys->size()))
					*gAsked->remove = (*gAsked->keys)[static_cast<size_t>(picked)].key;
				break;
			}
		}
		gAsked->answered = true;
	}
};

CREATE_PMINTERFACE(KFCLabelDialogController, kKFCLabelDialogControllerImpl)

/** The Label dialogs' observer. It stands in for the stock CDialogObserver kDialogBoss carries and calls it first in AutoAttach and Update, last in
    AutoDetach (KFCValueDialogObserver's shape) - OK and Cancel are the base's. Its own: Edit Value's list - a key picked keeps what was typed for the one
    shown, and shows its own. */
class KFCLabelDialogObserver : public CDialogObserver
{
public:
	KFCLabelDialogObserver(IPMUnknown* boss) : CDialogObserver(boss) {}
	virtual ~KFCLabelDialogObserver() {}

	virtual void AutoAttach()
	{
		CDialogObserver::AutoAttach();
		Hear(true);
	}

	virtual void AutoDetach()
	{
		Hear(false);
		CDialogObserver::AutoDetach();
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* changedBy)
	{
		CDialogObserver::Update(theChange, theSubject, protocol, changedBy);
		if (gAsked == nil || gAsked->asking != kLabelAskingEdit || protocol != IID_ITREEVIEWCONTROLLER || theChange != kListSelectionChangedMessage)
			return;
		InterfacePtr<IDialogController> controller(this, UseDefaultIID());
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		const int32 picked = KFCQueryListSelectedIndex(panel, kKFCLabelKeyListWidgetID);
		if (controller == nil || picked < 0)
			return;
		CommitShown(controller);
		ShowKey(controller, picked);
	}

private:
	// The keys' list on IID_ITREEVIEWCONTROLLER, where the dialog has one (the query dialog's way - KFCQueryDialog.cpp).
	void Hear(bool attach)
	{
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil || panel->FindWidget(kKFCLabelKeyListWidgetID) == nil)
			return;
		if (attach)
			this->AttachToWidget(kKFCLabelKeyListWidgetID, IID_ITREEVIEWCONTROLLER, panel);
		else
			this->DetachFromWidget(kKFCLabelKeyListWidgetID, IID_ITREEVIEWCONTROLLER, panel);
	}
};

CREATE_PMINTERFACE(KFCLabelDialogObserver, kKFCLabelDialogObserverImpl)

namespace
{
// ASKED: the question's dialog made and opened - Open() waits until it is dismissed - and whether OK kept its answer. kNotShown = no dialog (no dialog
// manager, the view not made, or a Label dialog standing already).
enum AskResult { kAnswered, kCancelled, kNotShown };

AskResult Ask(Asked& asked, RsrcID view)
{
	if (gAsked != nil)
		return kNotShown;
	ISession* session = GetExecutionContextSession();
	InterfacePtr<IApplication> application((session != nil) ? session->QueryApplication() : nil);
	InterfacePtr<IDialogMgr> dialogMgr(application, UseDefaultIID());
	if (dialogMgr == nil)
		return kNotShown;
	RsrcSpec dialogSpec
	(
		LocaleSetting::GetLocale(),		// Locale index
		kKFCUIPluginID,					// This plug-in
		kViewRsrcType,
		view,							// The question's view
		kTrue							// Initially visible
	);
	gAsked = &asked;					// before the dialog is made: the controller fills it by the question
	// Modal and movable, as InDesign's own. The pointer is not held: the dialog takes care of itself as it closes.
	IDialog* dialog = dialogMgr->CreateNewDialog(dialogSpec, IDialog::kMovableModal);
	if (dialog != nil)
		dialog->Open();
	gAsked = nil;
	if (dialog == nil)
		return kNotShown;
	return asked.answered ? kAnswered : kCancelled;
}

// A TEST BUILD'S ANSWER (the fault switch label-answer): its lines instead of the window - key=, value=, pick=<key>, edit=<key>=<value>, cancel. True = the
// switch is on (outLines its lines).
bool TestAnswer(std::vector<std::string>& outLines)
{
	outLines.clear();
#ifdef KFC_DIAG
	if (KFC_DIAG_FAULT("label-answer"))
	{
		(void)KFCDiagFaultLines("label-answer", outLines);
		return true;
	}
#endif
	return false;
}

// A test answer's line "<name>=<rest>" - true, outRest set.
bool LineOf(const std::string& line, const char* name, std::string& outRest)
{
	const std::string head(std::string(name) + "=");
	if (line.compare(0, head.size(), head) != 0)
		return false;
	outRest = line.substr(head.size());
	return true;
}

PMString Words(const std::string& utf8)
{
	PMString out;
	out.SetUTF8String(utf8);
	out.SetTranslatable(kFalse);
	return out;
}

void SayNotShown(PMString& outWhy, const char* what)
{
	outWhy.Append(what);
	outWhy.Append(": the dialog could not be shown - nothing was changed.");
}
}	// anonymous namespace

bool KFCLabelDialogs::AskAddKey(KFCLabelEdit& out, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::vector<std::string> lines;
	if (TestAnswer(lines))
	{
		KFC_DIAG_LOG("LABELDLG asked=add answer=fault");
		std::string rest;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			if (lines[i] == "cancel")
				return false;
			if (LineOf(lines[i], "key", rest))
				out.key = Words(rest);
			else if (LineOf(lines[i], "value", rest))
				out.value = Words(rest);
		}
		return !out.key.IsEmpty() && !out.value.IsEmpty();
	}
	Asked asked;
	asked.asking = kLabelAskingAdd;
	asked.add = &out;
	KFC_DIAG_LOG("LABELDLG asked=add shown=1");
	const AskResult result = Ask(asked, kKFCAddKeyDialogRsrcID);
	if (result == kNotShown)
		SayNotShown(outWhy, "Add Key");
	return result == kAnswered && !out.key.IsEmpty() && !out.value.IsEmpty();
}

bool KFCLabelDialogs::AskEditValues(const std::vector<KFCLabelKey>& keys, int32 holders, std::vector<KFCLabelEdit>& out, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	out.clear();
	std::vector<std::string> lines;
	if (TestAnswer(lines))
	{
		KFC_DIAG_LOG("LABELDLG asked=edit holders=%d keys=%s answer=fault", static_cast<int>(holders), KeysWords(keys).c_str());
		std::string rest;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			if (lines[i] == "cancel")
			{
				out.clear();
				return false;
			}
			if (!LineOf(lines[i], "edit", rest))
				continue;
			const size_t eq = rest.find('=');
			const std::string name(rest.substr(0, eq));
			KFCLabelEdit edit;
			edit.key = Words(name);
			edit.value = Words((eq == std::string::npos) ? std::string() : rest.substr(eq + 1));
			for (size_t k = 0; k < keys.size(); ++k)
				if (keys[k].key == edit.key || (name == "[panel]" && keys[k].panel))		// ([panel]: the Script Label panel's own key, whatever it is called)
				{
					edit.key = keys[k].key;
					out.push_back(edit);
					break;
				}
		}
		return !out.empty();
	}
	Asked asked;
	asked.asking = kLabelAskingEdit;
	asked.keys = &keys;
	asked.holders = holders;
	asked.typed.resize(keys.size());
	asked.changed.resize(keys.size(), false);
	KFC_DIAG_LOG("LABELDLG asked=edit holders=%d keys=%s shown=1", static_cast<int>(holders), KeysWords(keys).c_str());
	const AskResult result = Ask(asked, kKFCEditValueDialogRsrcID);
	if (result == kNotShown)
	{
		SayNotShown(outWhy, "Edit Value");
		return false;
	}
	if (result != kAnswered)
		return false;
	for (size_t k = 0; k < keys.size(); ++k)
	{
		if (!asked.changed[k])
			continue;
		KFCLabelEdit edit;
		edit.key = keys[k].key;
		edit.value = asked.typed[k];
		edit.value.SetTranslatable(kFalse);
		out.push_back(edit);
	}
	if (out.empty())
		outWhy.Append("Edit Value: no value was changed - nothing was written.");
	return !out.empty();
}

bool KFCLabelDialogs::AskRemoveKey(const std::vector<KFCLabelKey>& keys, int32 holders, PMString& outKey, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	outKey.Clear();
	outKey.SetTranslatable(kFalse);
	std::vector<std::string> lines;
	if (TestAnswer(lines))
	{
		KFC_DIAG_LOG("LABELDLG asked=remove holders=%d keys=%s answer=fault", static_cast<int>(holders), KeysWords(keys).c_str());
		std::string rest;
		for (size_t i = 0; i < lines.size(); ++i)
		{
			if (lines[i] == "cancel")
				return false;
			if (!LineOf(lines[i], "pick", rest))
				continue;
			const PMString picked(Words(rest));
			for (size_t k = 0; k < keys.size(); ++k)
				if (keys[k].key == picked || (rest == "[panel]" && keys[k].panel))
				{
					outKey = keys[k].key;
					return true;
				}
		}
		return false;
	}
	Asked asked;
	asked.asking = kLabelAskingRemove;
	asked.keys = &keys;
	asked.holders = holders;
	asked.remove = &outKey;
	KFC_DIAG_LOG("LABELDLG asked=remove holders=%d keys=%s shown=1", static_cast<int>(holders), KeysWords(keys).c_str());
	const AskResult result = Ask(asked, kKFCRemoveKeyDialogRsrcID);
	if (result == kNotShown)
		SayNotShown(outWhy, "Remove Key");
	return result == kAnswered && !outKey.IsEmpty();
}

int32 KFCLabelDialogs::KeyRowCount()
{
	return (gAsked != nil && gAsked->keys != nil) ? static_cast<int32>(gAsked->keys->size()) : 0;
}

PMString KFCLabelDialogs::KeyRowText(int32 index)
{
	// "<key>  (<n> of <holders>)" - the Script Label panel's own key named as the panel.
	PMString text;
	text.SetTranslatable(kFalse);
	if (index < 0 || index >= KeyRowCount())
		return text;
	const KFCLabelKey& key = (*gAsked->keys)[static_cast<size_t>(index)];
	if (key.panel)
		text = PMString(kKFCLabelPanelKeyKey, PMString::kTranslateDuringCall);
	else
		text = key.key;
	text.Append("  (");
	text.AppendNumber(key.count);
	text.Append(" of ");
	text.AppendNumber(gAsked->holders);
	text.Append(")");
	text.SetTranslatable(kFalse);
	return text;
}

PMString KFCLabelDialogs::KeyRowTip(int32 index)
{
	// The key's value as it reads - several lines as they stand - or that the holders' values differ.
	PMString tip;
	tip.SetTranslatable(kFalse);
	if (index < 0 || index >= KeyRowCount())
		return tip;
	const KFCLabelKey& key = (*gAsked->keys)[static_cast<size_t>(index)];
	tip = key.same ? key.value : PMString(kKFCLabelValuesDifferKey, PMString::kTranslateDuringCall);
	tip.SetTranslatable(kFalse);
	return tip;
}

// End, KFCLabelDialogs.cpp.
