//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The tips of the Ruby and Kenten parts' icons (kKFCSwitchIconWidgetBoss - KFCOwnSwitches): InDesign's own words for its own Text tab's icons, by its own string keys, so they read as the tab beside them does
//  in whatever language it runs (the keys measured with app.findKeyStrings and app.translateKeyString - work/note-scripts/2026-11-02-kfc-ruby-list/measure_switches.jsx, measure_switches2.jsx). The tip's base is every product tip's: KFCIconTip.cpp.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "AbstractTip.h"
#include "IControlView.h"

// General includes:
#include "PMString.h"

// Project includes:
#include "KFCUIID.h"

class KFCSwitchIconTip : public AbstractTip
{
public:
	KFCSwitchIconTip(IPMUnknown* boss) : AbstractTip(boss) {}
	virtual ~KFCSwitchIconTip() {}

	virtual PMString GetTipText(const PMPoint& /*mouseLocation*/)
	{
		InterfacePtr<IControlView> view(this, UseDefaultIID());
		const WidgetID widget = (view != nil) ? view->GetWidgetID() : kInvalidWidgetID;
		const char* const key = (widget == kKFCSwitchLockedLayersWidgetID) ? "kFCToolTipLockedString"
			: (widget == kKFCSwitchLockedStoriesWidgetID) ? "Include Locked Stories (Find Only)"
			: (widget == kKFCSwitchHiddenWidgetID) ? "kFCToolTipHiddenString"
			: (widget == kKFCSwitchMasterWidgetID) ? "Include Master Pages"
			: (widget == kKFCSwitchFootnotesWidgetID) ? "Include Footnotes"
			: (widget == kKFCSwitchKanaWidgetID) ? "Kana Sensitive"
			: (widget == kKFCSwitchWidthWidgetID) ? "Full-Width/Half-Width Sensitive"
			: "";
		return PMString(key, PMString::kTranslateDuringCall);
	}
};

CREATE_PMINTERFACE(KFCSwitchIconTip, kKFCSwitchIconTipImpl)

// End, KFCSwitchIcon.cpp.
