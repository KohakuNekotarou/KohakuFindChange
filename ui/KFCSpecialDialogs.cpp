//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The values an object row's transform asks first, and Narrow Down's page range - see KFCSpecialDialogs.h.
//  MADE OF INDESIGN'S OWN WIDGETS (the author's call: the dialogs are to be InDesign's own - the design is
//  docs/superpowers/specs/2026-10-23-kfc-native-value-dialogs-design.md): the measure, angle and percentage fields
//  InDesign's dialogs are made of (EveInfo.fh), so a value reads and shows in the front document's ruler units, in
//  every unit and way of writing one InDesign knows (a cicero, 1p6), and a value out of a field's range is refused in
//  InDesign's own words; the chain is InDesign's own button (kChainButtonBoss - it toggles itself and draws its own
//  pictures). Laid out as InDesign's Move, Scale, Rotate and Shear dialogs - LayoutUIID.h's kMoveDlg... /
//  kScaleDlg... / kRotate... / kShearDlg... widgets, and photographs of them in
//  work/note-scripts/2026-10-17-kfc-object-special/official-*.png: the fields on the left, OK and Cancel stacked on the
//  right. What InDesign's have and these have not: Copy (a transformed copy - a row would have nothing to follow) and
//  Preview (it would write the document while the dialog stands).
//
//  One dialog boss for the five (kKFCValueDialogBoss - KFCUI.fr), each its own view; which one stands is the question
//  being asked (gAsking - modal, so one at a time). Asked as snippetrunner asks a value (SnipRunParameterUtils.cpp):
//  the dialog made and opened, Open() waiting until it is dismissed ("Open by default now calls WaitForDialog" -
//  IDialog.h), the answer then read from what the controller kept on OK.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"
#include "IControlView.h"			// GetWidgetID - which field a change came from
#include "IDialog.h"
#include "IDialogController.h"		// the field helpers, asked by the observer of the controller beside it
#include "IDialogMgr.h"
#include "IPanelControlData.h"		// FindWidget - the fields a dialog has
#include "ISession.h"
#include "ISubject.h"				// the observer's Update hears one
#include "ITextDataValidation.h"	// IsEntryLegal / GetErrorString - a field's own range check, and its words
#include "ITriStateControlData.h"	// the chain, the axis buttons
#include "widgetid.h"				// IID_ITEXTCONTROLDATA / IID_ITRISTATECONTROLDATA and their messages

// General includes:
#include "CAlert.h"					// WarningAlert - a value refused
#include "CDialogController.h"
#include "CDialogObserver.h"
#include "CoreResTypes.h"			// kViewRsrcType
#include "LocaleSetting.h"
#include "PMReal.h"					// kPMPi
#include "PMString.h"
#include "RsrcSpec.h"

#include <cmath>					// std::sqrt, std::atan2, std::cos, std::sin, std::fabs, std::isfinite

// Project includes:
#include "KFCSpecialDialogs.h"
#include "KFCUIID.h"

namespace
{
// WHAT WAS ANSWERED LAST, offered again for the session (the special operations spec's E12) - kept on OK alone. Plain
// values: nothing to tear down when the plug-in goes. A distance is kept in points and shown in the ruler units of the
// document in front when the dialog opens.
double gMoveX = 0.0;				// points, right positive
double gMoveY = 0.0;				// points, down positive
double gScaleX = 100.0;				// percent
double gScaleY = 100.0;
bool gScaleConstrain = true;		// the chain linked
double gRotate = 0.0;				// degrees, counter-clockwise positive
double gShear = 0.0;				// degrees
bool gShearVertical = false;		// the axis
// Narrow Down's Pages...: the range as typed, UTF-16 (a section's name can be any text) - a longer one is not kept.
const int32 kTypedPages = 256;
UTF16TextChar gPages[kTypedPages] = { 0 };
int32 gPagesLength = 0;

// THE QUESTION BEING ASKED - which dialog stands (modal: one at a time), so the one controller and the one observer
// know which fields they fill, follow and read.
enum Asking { kAskingNone = 0, kAskingMove, kAskingScale, kAskingRotate, kAskingShear, kAskingPages };
Asking gAsking = kAskingNone;
bool gAnswered = false;				// OK pressed, and what it answered kept (ApplyDialogFields)
PMString* gPagesAnswer = nil;		// AskPageRange's answer while its dialog stands - the range, whatever its length
// MOVE: which pair was typed last - Distance and Angle (true) or Horizontal and Vertical. Followed on every key stroke,
// so the pair typed is the one answered even when OK is pressed before the field is committed (Enter typed in it).
bool gMoveByDistance = false;

// THE VALUE FIELDS THAT ANSWER, checked on OK in the order they are read (the first refused is the one selected).
// Move's are the pair it answers by: the other pair is worked out, and a distance worked out from two values in range
// can stand beyond the Distance field's own range without being typed there.
const WidgetID kMoveXYFields[] = { kKFCValueXWidgetID, kKFCValueYWidgetID };
const WidgetID kMoveDistanceFields[] = { kKFCValueDistanceWidgetID, kKFCValueAngleWidgetID };
const WidgetID kScaleFields[] = { kKFCValueXWidgetID, kKFCValueYWidgetID };
const WidgetID kAngleFields[] = { kKFCValueAngleWidgetID };

// The dialog's view, by the question.
RsrcID ViewOf(Asking asking)
{
	switch (asking)
	{
		case kAskingMove:	return kKFCMoveDialogRsrcID;
		case kAskingScale:	return kKFCScaleDialogRsrcID;
		case kAskingRotate:	return kKFCRotateDialogRsrcID;
		case kAskingShear:	return kKFCShearDialogRsrcID;
		default:			return kKFCPagesDialogRsrcID;
	}
}

// A value next to nothing written as nothing: the arithmetic below leaves a trace (the cosine of 90 degrees) that a
// field would show as "-0 mm".
double Clean(double v)
{
	return (std::fabs(v) < 1e-9) ? 0.0 : v;
}

// MOVE'S TWO PAIRS, each worked out from the other as InDesign's Move works them: the angle counter-clockwise from the
// right, down positive (InDesign's pasteboard coordinates).
void PolarOf(double x, double y, double& outDistance, double& outAngle)
{
	outDistance = Clean(std::sqrt(x * x + y * y));
	outAngle = (x != 0.0 || y != 0.0) ? Clean(std::atan2(-y, x) * 180.0 / kPMPi) : 0.0;
}

void CartesianOf(double distance, double angle, double& outX, double& outY)
{
	const double r = angle * kPMPi / 180.0;
	outX = Clean(distance * std::cos(r));
	outY = Clean(-distance * std::sin(r));
}

// A button's state, from whether it is on.
ITriStateControlData::TriState StateOf(bool on)
{
	return on ? ITriStateControlData::kSelected : ITriStateControlData::kUnselected;
}

// A field of the dialog the boss is, by its ID - nil when this dialog has none.
IControlView* FieldOf(IPMUnknown* boss, const WidgetID& id)
{
	InterfacePtr<IPanelControlData> panel(boss, UseDefaultIID());
	return (panel != nil) ? panel->FindWidget(id) : nil;
}

// A FIELD'S VALUE WHEN IT READS: a number its own check accepts (in range, not blank) - false otherwise, out left as it
// was. In the field's base unit: points for a measure, percent, degrees (ITextValue.h).
bool ReadValue(IDialogController* controller, const WidgetID& id, double& out)
{
	InterfacePtr<ITextDataValidation> check(FieldOf(controller, id), UseDefaultIID());
	if (check == nil || !check->IsEntryLegal())
		return false;
	bool16 notANumber = kFalse, blank = kFalse;
	const PMReal value = controller->GetTextValue(id, nil, &notANumber, &blank);
	if (notANumber || blank)
		return false;
	out = ::ToDouble(value);
	return true;
}

// One field's value written into another as that field writes it ("50" becomes "50%") - without a word back to the
// observer (notifyOfChange kFalse), so no field answers a field.
void Mirror(IDialogController* controller, const WidgetID& from, const WidgetID& to)
{
	double v = 0.0;
	if (ReadValue(controller, from, v))
		controller->SetTextValue(to, v, nil, kTrue, kFalse);
}
}	// anonymous namespace

/** The value dialogs' controller: fills the dialog that stands with what was answered last, checks it on OK - each
    field by its own check, in InDesign's words - and keeps what it answered. */
class KFCValueDialogController : public CDialogController
{
public:
	KFCValueDialogController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCValueDialogController() {}

protected:
	// Filled at every open (a cached dialog too) - written without a word to the observer (notifyOfChange kFalse):
	// nothing is typed yet. The first field takes the keyboard, as in InDesign's own (the product's Layer Options
	// dialog the same - LayerOptionsDlgController.cpp).
	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		gMoveByDistance = false;
		switch (gAsking)
		{
			case kAskingMove:
			{
				double distance = 0.0, angle = 0.0;
				PolarOf(gMoveX, gMoveY, distance, angle);
				SetTextValue(kKFCValueXWidgetID, gMoveX, nil, kTrue, kFalse);
				SetTextValue(kKFCValueYWidgetID, gMoveY, nil, kTrue, kFalse);
				SetTextValue(kKFCValueDistanceWidgetID, distance, nil, kTrue, kFalse);
				SetTextValue(kKFCValueAngleWidgetID, angle, nil, kTrue, kFalse);
				SelectDialogWidget(kKFCValueXWidgetID);
				break;
			}
			case kAskingScale:
				SetTextValue(kKFCValueXWidgetID, gScaleX, nil, kTrue, kFalse);
				SetTextValue(kKFCValueYWidgetID, gScaleY, nil, kTrue, kFalse);
				SetTriStateControlData(kKFCValueChainWidgetID, StateOf(gScaleConstrain), nil, kTrue, kFalse);
				SelectDialogWidget(kKFCValueXWidgetID);
				break;
			case kAskingRotate:
				SetTextValue(kKFCValueAngleWidgetID, gRotate, nil, kTrue, kFalse);
				SelectDialogWidget(kKFCValueAngleWidgetID);
				break;
			case kAskingShear:
				SetTextValue(kKFCValueAngleWidgetID, gShear, nil, kTrue, kFalse);
				// Both buttons, each by hand: written without a word, the cluster does not unset the other.
				SetTriStateControlData(kKFCValueAxisHorizontalWidgetID, StateOf(!gShearVertical), nil, kTrue, kFalse);
				SetTriStateControlData(kKFCValueAxisVerticalWidgetID, StateOf(gShearVertical), nil, kTrue, kFalse);
				SelectDialogWidget(kKFCValueAngleWidgetID);
				break;
			case kAskingPages:
			{
				PMString pages;
				pages.SetXString(gPages, gPagesLength);
				pages.SetTranslatable(kFalse);
				SetTextControlData(kKFCValuePagesWidgetID, pages, nil, kTrue, kFalse);
				SelectDialogWidget(kKFCValuePagesWidgetID);
				break;
			}
			default:
				break;
		}
	}

	// ON OK. The base checks nothing (CDialogController.cpp), so the fields that answer are asked here, as the product
	// asks its fields (DynamicDocumentsUIUtils.cpp): each by its own check - the range in KFCUI.fr, a value that is no
	// number - and, when it refuses, in its own words. Then what KFC refuses itself: a scale of 0% (no object can be
	// scaled to nothing) and a page range of nothing. The field returned is the one the framework selects
	// (CDialogObserver.cpp).
	virtual WidgetID ValidateDialogFields(IActiveContext* /*context*/)
	{
		const WidgetID* fields = nil;
		size_t count = 0;
		switch (gAsking)
		{
			case kAskingMove:
				fields = gMoveByDistance ? kMoveDistanceFields : kMoveXYFields;
				count = 2;
				break;
			case kAskingScale:
				fields = kScaleFields;
				count = sizeof(kScaleFields) / sizeof(kScaleFields[0]);
				break;
			case kAskingRotate:
			case kAskingShear:
				fields = kAngleFields;
				count = sizeof(kAngleFields) / sizeof(kAngleFields[0]);
				break;
			default:
				break;
		}
		for (size_t i = 0; i < count; ++i)
		{
			InterfacePtr<ITextDataValidation> check(FieldOf(this, fields[i]), UseDefaultIID());
			if (check != nil && !check->IsEntryLegal())
			{
				CAlert::WarningAlert(check->GetErrorString());
				return fields[i];
			}
		}
		if (gAsking == kAskingScale)
		{
			const bool linked = GetTriStateControlData(kKFCValueChainWidgetID) == ITriStateControlData::kSelected;
			WidgetID zero = kNoInvalidWidgets;
			if (::ToDouble(GetTextValue(kKFCValueXWidgetID)) == 0.0)
				zero = kKFCValueXWidgetID;
			else if (!linked && ::ToDouble(GetTextValue(kKFCValueYWidgetID)) == 0.0)
				zero = kKFCValueYWidgetID;
			if (zero != kNoInvalidWidgets)
			{
				CAlert::WarningAlert(PMString(kKFCValueNotZeroKey));
				return zero;
			}
		}
		if (gAsking == kAskingPages)
		{
			PMString pages(GetTextControlData(kKFCValuePagesWidgetID));
			pages.StripWhiteSpace(PMString::kLeadingAndTrailingWhiteSpace);
			if (pages.IsEmpty())
			{
				CAlert::WarningAlert(PMString(kKFCValueNoPagesKey));
				return kKFCValuePagesWidgetID;
			}
		}
		return kNoInvalidWidgets;
	}

	// ON OK, THE VALUES CHECKED: kept for the next open and for the caller to read. Move answers by the pair typed last
	// (gMoveByDistance); Scale with the chain linked answers Scale X for both.
	virtual void ApplyDialogFields(IActiveContext* /*context*/, const WidgetID& /*widgetId*/)
	{
		switch (gAsking)
		{
			case kAskingMove:
				if (gMoveByDistance)
					CartesianOf(::ToDouble(GetTextValue(kKFCValueDistanceWidgetID)),
						::ToDouble(GetTextValue(kKFCValueAngleWidgetID)), gMoveX, gMoveY);
				else
				{
					gMoveX = ::ToDouble(GetTextValue(kKFCValueXWidgetID));
					gMoveY = ::ToDouble(GetTextValue(kKFCValueYWidgetID));
				}
				break;
			case kAskingScale:
				gScaleConstrain = GetTriStateControlData(kKFCValueChainWidgetID) == ITriStateControlData::kSelected;
				gScaleX = ::ToDouble(GetTextValue(kKFCValueXWidgetID));
				gScaleY = gScaleConstrain ? gScaleX : ::ToDouble(GetTextValue(kKFCValueYWidgetID));
				break;
			case kAskingRotate:
				gRotate = ::ToDouble(GetTextValue(kKFCValueAngleWidgetID));
				break;
			case kAskingShear:
				gShear = ::ToDouble(GetTextValue(kKFCValueAngleWidgetID));
				gShearVertical = (GetTriStateControlData(kKFCValueAxisVerticalWidgetID)
					== ITriStateControlData::kSelected);
				break;
			case kAskingPages:
			{
				PMString pages(GetTextControlData(kKFCValuePagesWidgetID));
				pages.StripWhiteSpace(PMString::kLeadingAndTrailingWhiteSpace);
				pages.SetTranslatable(kFalse);
				if (gPagesAnswer != nil)
					*gPagesAnswer = pages;
				int32 n = 0;
				const UTF16TextChar* typed = pages.GrabUTF16Buffer(&n);
				if (typed != nil && n < kTypedPages)
				{
					for (int32 i = 0; i < n; ++i)
						gPages[i] = typed[i];
					gPagesLength = n;
				}
				break;
			}
			default:
				break;
		}
		gAnswered = true;
	}
};

CREATE_PMINTERFACE(KFCValueDialogController, kKFCValueDialogControllerImpl)

/** The value dialogs' observer. It stands in for the stock CDialogObserver kDialogBoss carries and calls it first in
    AutoAttach and Update, last in AutoDetach (KFCQueryDialogObserver's shape) - OK and Cancel are the base's: OK asks
    the controller to check the values and keep them. Its own: Move's two pairs, each worked out from the other as one
    is committed, and which pair was typed last; Scale's chain - the two values one while it is linked. It hears the
    fields its dialog has - the same IDs in every dialog, so a field the question does not follow is heard and let
    be. */
class KFCValueDialogObserver : public CDialogObserver
{
public:
	KFCValueDialogObserver(IPMUnknown* boss) : CDialogObserver(boss) {}
	virtual ~KFCValueDialogObserver() {}

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
		InterfacePtr<IControlView> view(theSubject, UseDefaultIID());
		InterfacePtr<IDialogController> controller(this, UseDefaultIID());
		if (view == nil || controller == nil)
			return;
		const WidgetID id = view->GetWidgetID();
		if (protocol == IID_ITEXTCONTROLDATA
			&& (theChange == kEditBoxKeyStrokeMessage || theChange == kTextChangeStateMessage))
		{
			const bool committed = (theChange == kTextChangeStateMessage);
			if (gAsking == kAskingMove)
				FollowMove(controller, id, committed);
			else if (gAsking == kAskingScale)
				FollowScale(controller, id, committed);
		}
		else if (protocol == IID_ITRISTATECONTROLDATA && id == kKFCValueChainWidgetID && theChange == kTrueStateMessage)
			Mirror(controller, kKFCValueXWidgetID, kKFCValueYWidgetID);		// linked: Scale Y takes Scale X
	}

private:
	// The value fields on IID_ITEXTCONTROLDATA - a key stroke (kEditBoxKeyStrokeMessage: KFCUI.fr sets "notify every
	// key stroke" on Move's and Scale's) and a commit (kTextChangeStateMessage) - and the chain on
	// IID_ITRISTATECONTROLDATA, those this dialog has.
	void Hear(bool attach)
	{
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil)
			return;
		const WidgetID kFields[] = { kKFCValueXWidgetID, kKFCValueYWidgetID, kKFCValueDistanceWidgetID,
			kKFCValueAngleWidgetID };
		for (size_t i = 0; i < sizeof(kFields) / sizeof(kFields[0]); ++i)
		{
			if (panel->FindWidget(kFields[i]) == nil)
				continue;
			if (attach)
				this->AttachToWidget(kFields[i], IID_ITEXTCONTROLDATA, panel);
			else
				this->DetachFromWidget(kFields[i], IID_ITEXTCONTROLDATA, panel);
		}
		if (panel->FindWidget(kKFCValueChainWidgetID) == nil)
			return;
		if (attach)
			this->AttachToWidget(kKFCValueChainWidgetID, IID_ITRISTATECONTROLDATA, panel);
		else
			this->DetachFromWidget(kKFCValueChainWidgetID, IID_ITRISTATECONTROLDATA, panel);
	}

	// MOVE: the pair typed is the pair OK answers by, and once it is committed, the other pair is worked out from it. A
	// pair that does not read (a value refused, a field left blank) leaves the other as it stands.
	void FollowMove(IDialogController* controller, const WidgetID& id, bool committed)
	{
		const bool byDistance = (id == kKFCValueDistanceWidgetID || id == kKFCValueAngleWidgetID);
		if (!byDistance && id != kKFCValueXWidgetID && id != kKFCValueYWidgetID)
			return;
		gMoveByDistance = byDistance;
		if (!committed)
			return;
		double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
		if (byDistance)
		{
			if (!ReadValue(controller, kKFCValueDistanceWidgetID, a)
				|| !ReadValue(controller, kKFCValueAngleWidgetID, b))
				return;
			CartesianOf(a, b, c, d);
			controller->SetTextValue(kKFCValueXWidgetID, c, nil, kTrue, kFalse);
			controller->SetTextValue(kKFCValueYWidgetID, d, nil, kTrue, kFalse);
		}
		else
		{
			if (!ReadValue(controller, kKFCValueXWidgetID, a) || !ReadValue(controller, kKFCValueYWidgetID, b))
				return;
			PolarOf(a, b, c, d);
			controller->SetTextValue(kKFCValueDistanceWidgetID, c, nil, kTrue, kFalse);
			controller->SetTextValue(kKFCValueAngleWidgetID, d, nil, kTrue, kFalse);
		}
	}

	// SCALE, WHILE THE CHAIN IS LINKED: the two values one - each key stroke's text copied to the other field as it is
	// typed, and once committed, the value written there as that field writes it.
	void FollowScale(IDialogController* controller, const WidgetID& id, bool committed)
	{
		if ((id != kKFCValueXWidgetID && id != kKFCValueYWidgetID)
			|| controller->GetTriStateControlData(kKFCValueChainWidgetID) != ITriStateControlData::kSelected)
			return;
		const WidgetID other = (id == kKFCValueXWidgetID) ? kKFCValueYWidgetID : kKFCValueXWidgetID;
		if (committed)
		{
			Mirror(controller, id, other);
			return;
		}
		PMString typed(controller->GetTextControlData(id));
		typed.SetTranslatable(kFalse);
		controller->SetTextControlData(other, typed, nil, kTrue, kFalse);
	}
};

CREATE_PMINTERFACE(KFCValueDialogObserver, kKFCValueDialogObserverImpl)

namespace
{
// ASKED: the question's dialog made and opened - Open() waits until it is dismissed - and whether OK kept its values.
// kNotShown = no dialog (no dialog manager, the view not made, or a value dialog standing already).
enum AskResult { kAnswered, kCancelled, kNotShown };

AskResult Ask(Asking asking, PMString* pagesAnswer)
{
	if (gAsking != kAskingNone)
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
		ViewOf(asking),					// The question's view
		kTrue							// Initially visible
	);
	gAsking = asking;					// before the dialog is made: the controller fills it by the question
	gAnswered = false;
	gPagesAnswer = pagesAnswer;
	// Modal and movable, as InDesign's own. The pointer is not held: the dialog takes care of itself as it closes.
	IDialog* dialog = dialogMgr->CreateNewDialog(dialogSpec, IDialog::kMovableModal);
	if (dialog != nil)
		dialog->Open();
	const bool answered = gAnswered;
	gAsking = kAskingNone;
	gPagesAnswer = nil;
	if (dialog == nil)
		return kNotShown;
	return answered ? kAnswered : kCancelled;
}
}	// anonymous namespace

const char* KFCSpecialDialogs::OpName(int32 op)
{
	switch (op)
	{
		case kKFCSpecialMove:	return "Move";
		case kKFCSpecialScale:	return "Scale";
		case kKFCSpecialRotate:	return "Rotate";
		case kKFCSpecialShear:	return "Shear";
		default:				return "";
	}
}

bool KFCSpecialDialogs::NothingToDo(int32 op, const KFCSpecialParams& params)
{
	const double kTiny = 1e-9;
	switch (op)
	{
		case kKFCSpecialMove:	return std::fabs(params.x) < kTiny && std::fabs(params.y) < kTiny;
		case kKFCSpecialScale:	return std::fabs(params.x - 100.0) < kTiny && std::fabs(params.y - 100.0) < kTiny;
		case kKFCSpecialRotate:
		case kKFCSpecialShear:	return std::fabs(params.x) < kTiny;		// a shear of 0 is nothing on either axis
		default:				return false;
	}
}

bool KFCSpecialDialogs::AskPageRange(PMString& io, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	PMString answer;
	const AskResult asked = Ask(kAskingPages, &answer);
	if (asked == kNotShown)
	{
		outWhy.Append("Narrow Down: the dialog could not be shown - nothing was searched.");
		return false;
	}
	if (asked == kCancelled)
		return false;		// Cancel: nothing searched, nothing said
	io = answer;
	io.SetTranslatable(kFalse);
	return !io.IsEmpty();
}

bool KFCSpecialDialogs::AskTransformValues(int32 op, KFCSpecialParams& io, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	Asking asking = kAskingNone;
	switch (op)
	{
		case kKFCSpecialMove:	asking = kAskingMove; break;
		case kKFCSpecialScale:	asking = kAskingScale; break;
		case kKFCSpecialRotate:	asking = kAskingRotate; break;
		case kKFCSpecialShear:	asking = kAskingShear; break;
		default:				return false;
	}
	const AskResult asked = Ask(asking, nil);
	if (asked == kNotShown)
	{
		outWhy = OpName(op);
		outWhy.Append(": the dialog could not be shown - nothing was changed.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	if (asked == kCancelled)
		return false;		// Cancel: nothing written, nothing said
	switch (asking)
	{
		case kAskingMove:	io.x = gMoveX; io.y = gMoveY; break;
		case kAskingScale:	io.x = gScaleX; io.y = gScaleY; break;
		case kAskingRotate:	io.x = gRotate; io.y = 0.0; break;
		case kAskingShear:	io.x = gShear; io.y = gShearVertical ? 1.0 : 0.0; break;
		default:			break;
	}
	// NOT FINITE - refused: no transform can be made of it (KFCObjectSpecial's doors measure what a finite one does).
	// The fields' ranges (KFCUI.fr) keep every value finite: this says so where it is relied on.
	if (!std::isfinite(io.x) || !std::isfinite(io.y))
	{
		outWhy = OpName(op);
		outWhy.Append(": the values are too large - nothing was changed.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	return true;
}

// End, KFCSpecialDialogs.cpp.
