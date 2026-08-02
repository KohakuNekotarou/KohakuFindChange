//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The panel's illustration - which of the stacked pictures is showing. They occupy ONE frame in
//  KBS.fr, and exactly one of them is visible and enabled at a time.
//
//  Adding another picture (one for the aftermath of a replace is expected) is three lines: an id
//  and a resource in KBSID.h, a widget in KBS.fr, and a row in this file's table. Nothing else in
//  the plug-in names the pictures - the panel's observer asks IsIconWidget rather than testing an
//  id of its own.
//
//========================================================================================

#ifndef __KBSPanelIcon_h__
#define __KBSPanelIcon_h__

#include "widgetid.h"		// WidgetID

namespace KBSPanelIcon
{
	/** Show the picture that matches what the panel is currently reporting, and hide the rest.
	    Safe to call when the panel is closed (does nothing then).

	    Called when the panel appears - its widgets are built fresh on every show, so the state has
	    to be written on then, not assumed - and after anything that changes the panel's status. */
	void Update();

	/** Is this one of the illustrations? The panel's observer asks before treating a click as
	    "open the home page", so another picture needs no change on that side. */
	bool IsIconWidget(const WidgetID& widgetID);

	/** How many illustrations there are, and the nth one's WidgetID. Used by the panel's observer
	    to subscribe to every one of them. */
	int32 Count();
	WidgetID NthWidgetID(int32 n);
}

#endif // __KBSPanelIcon_h__
