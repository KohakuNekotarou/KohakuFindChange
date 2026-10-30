//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RUBY TAB'S FACE IN INDESIGN'S FIND/CHANGE DIALOG (docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md - the Table tab's shape, KFCTableTab.h, and its hazard: no window
//  is walked while the dialog closes). Search: (All Documents / Document), Find: (Text With Ruby / Text Without Ruby), Base Text:, Ruby:, Change Ruby To:, Remove Ruby, and
//  two lines that say where the finding and the replacing are done. The text fields are kept at every key stroke (the panel's Find may be pressed with one still focused).
//  While the face is the dialog's tab - or was when the dialog closed - KFC's Find lists ruby (KFCRubySearch in the model half, which asks through
//  IKFCUIServices::RubyTabConditions). The main thread only.
//
//========================================================================================

#ifndef __KFCRubyTab_h__
#define __KFCRubyTab_h__

#include "KFCModelTypes.h"		// KFCRubyConditions

namespace KFCRubyTab
{
	/** IS THE RUBY TAB FIND/CHANGE'S TAB NOW (IKFCUIServices::RubyTabConditions): its face shown, or the dialog closed with it shown last - but not while a saved query loaded
	    since stands (YieldToSavedQuery). outConditions = the face's as they stand, either way. */
	bool IsCurrent(KFCRubyConditions& outConditions);

	/** A SAVED QUERY STATES ITS OWN TAB (the query dialog's Find): the Ruby tab steps aside until the person shows its face again. */
	void YieldToSavedQuery();

	/** Application shutdown: what the face keeps holds PMStrings - released while the session stands (the plug-in's other statics' rule). */
	void ShutdownCleanup();
}

#endif // __KFCRubyTab_h__

// End, KFCRubyTab.h.
