//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The tooltip on the panel's illustration (kKBSIconWidgetBoss). It shows the very URL that
//  clicking the picture opens, so the icon is not a button whose destination is a mystery - the
//  same job KESCM's KESCMIconTip does for its panel.
//
//  AbstractTip (source/public/libs/widgetbin/includes/AbstractTip.h) is deliberately NOT used: its
//  implementation lives in DV_WidgetBin.lib, and an ITip written straight onto CPMUnknown needs no
//  library at all. There is nothing to gain here from the base class - the tip text is one constant.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ITip.h"

// General includes:
#include "CPMUnknown.h"
#include "PMString.h"

// Project includes:
#include "KBSID.h"

/** The panel illustration's tooltip: the plug-in's own home page, which is where a click goes. */
class KBSIconTip : public CPMUnknown<ITip>
{
public:
	KBSIconTip(IPMUnknown* boss) : CPMUnknown<ITip>(boss) {}

	virtual PMString GetTipText(const PMPoint& mouseLocation);
	virtual bool16 UpdateToolTipOnMouseMove();
	virtual void SetTipText(const PMString tipText);
};

CREATE_PMINTERFACE(KBSIconTip, kKBSIconTipImpl)

PMString KBSIconTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	// A URL is not a phrase to translate, and it is not in any string table for the same reason.
	PMString tip(kKBSRepoURL);
	tip.SetTranslatable(kFalse);
	return tip;
}

bool16 KBSIconTip::UpdateToolTipOnMouseMove()
{
	return kFalse;		// one widget, one answer - moving inside it changes nothing
}

void KBSIconTip::SetTipText(const PMString /*tipText*/)
{
	// ITip.h says the general case leaves this empty: the text is decided here, not pushed in.
}

// End, KBSIconTip.cpp.
