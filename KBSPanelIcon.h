//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The panel's illustration - which of the stacked pictures is showing. They occupy ONE frame in
//  KBS.fr, and exactly one of them is visible and enabled at a time.
//
//  There are three: before anything has been run, once something HAS been run, and once a replace
//  has written something. Adding a fourth is three lines - an id and a resource in KBSID.h, a
//  widget in KBS.fr, and a row in this file's table - plus the test that picks it, which goes in
//  Choose() with the more specific state FIRST. Nothing else in the plug-in names the pictures:
//  the panel's observer asks IsIconWidget rather than testing an id of its own.
//
//========================================================================================

#ifndef __KBSPanelIcon_h__
#define __KBSPanelIcon_h__

#include "widgetid.h"		// WidgetID

class IPanelControlData;

namespace KBSPanelIcon
{
	/** Show the picture that matches what the panel is currently reporting, and hide the rest.
	    Safe to call when the panel is closed (does nothing then).

	    Called when the panel appears - its widgets are built fresh on every show, so the state has
	    to be written on then, not assumed - and after anything that changes the panel's status.

	    *Finds the panel through Utils<IPalettePanelUtils>()->QueryPanelByWidgetID, which is the
	     product's own way in (layerpanel, conditionaltextui, msopanel, dynamicdocumentsui all reach
	     their panels that way) and hands back nil for a panel that is not on screen - which is
	     exactly the "does nothing then" above. */
	void Update();

	/** The same, for a caller that ALREADY HAS the panel - the panel's own observer, which is
	    aggregated onto the panel boss and can therefore ask itself
	    (InterfacePtr<IPanelControlData>(this, UseDefaultIID()), the shape the product uses in
	    ConditionalTextUIPanelDetailController.cpp:162 and LayerPanelView.cpp:63).

	    Nothing is looked up here, so nothing can be looked up differently from the caller's other
	    work on the same panel in the same moment. Does nothing when panelData is nil. */
	void Update(IPanelControlData* panelData);

	/** Is this one of the illustrations? The panel's observer asks before treating a click as
	    "open the home page", so another picture needs no change on that side. */
	bool IsIconWidget(const WidgetID& widgetID);

	/** How many illustrations there are, and the nth one's WidgetID. Used by the panel's observer
	    to subscribe to every one of them. */
	int32 Count();
	WidgetID NthWidgetID(int32 n);
}

#endif // __KBSPanelIcon_h__
