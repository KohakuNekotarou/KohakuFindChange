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
//  ***** WHY IT DERIVES FROM AbstractTip *****
//
//  AbstractTip (source/public/libs/widgetbin/includes/AbstractTip.h) is what every tooltip in the
//  product code derives from - linksui, layerpanel, buttonui, dynamicdocumentsui, conditionaltextui
//  - and customconditionaltextui/CusCondTxtUIIconTip.cpp:42 shows an external plug-in doing the
//  same. Nothing in the SDK implements ITip on CPMUnknown directly.
//
//  It supplies the two members this class has no opinion about, so only GetTipText is left to
//  write:
//    UpdateToolTipOnMouseMove - which ITip.h:44-50 wraps in ID_DEPRECATED
//    SetTipText               - which ITip.h:51-53 says is "not implemented in general case"
//
//  It lives in DV_WidgetBin.lib. That was once the reason NOT to use it; the plug-in now links that
//  library in all four configurations anyway, for its own drawn views (KBSColorTextView,
//  KBSGlyphView), so the base class costs nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "AbstractTip.h"		// the base every product tooltip uses

// General includes:
#include "PMString.h"

// Project includes:
#include "KBSID.h"

/** The panel illustration's tooltip: the plug-in's own home page, which is where a click goes. */
class KBSIconTip : public AbstractTip
{
public:
	KBSIconTip(IPMUnknown* boss);
	virtual ~KBSIconTip();

	virtual PMString GetTipText(const PMPoint& mouseLocation);
};

CREATE_PMINTERFACE(KBSIconTip, kKBSIconTipImpl)

KBSIconTip::KBSIconTip(IPMUnknown* boss) : AbstractTip(boss)
{
}

KBSIconTip::~KBSIconTip()
{
}

PMString KBSIconTip::GetTipText(const PMPoint& /*mouseLocation*/)
{
	// A URL is not a phrase to translate, and it is not in any string table for the same reason.
	PMString tip(kKBSRepoURL);
	tip.SetTranslatable(kFalse);
	return tip;
}

// End, KBSIconTip.cpp.
