//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  SPECIAL OPERATIONS > LABEL'S DIALOGS (docs/superpowers/specs/2026-11-06-kfc-label-spelling-runscript-design.md LB1 - LB4 - the author, 11-06: "Add a key,
//  remove a key - not offered with none -, edit its context: three from a Label submenu, modal dialogs; Edit, a list of the keys with the input under it";
//  the names were left to me: Add Key..., Edit Value..., Remove Key...). Made of InDesign's own widgets, as the value dialogs are (KFCSpecialDialogs - the
//  author's 10-23 call): one dialog boss (kKFCLabelDialogBoss), a view each. The keys' list is the query dialog's flat list (KFCQueryList - its adapter tells
//  the lists apart by their widget ids). What a key is, how many of the rows' holders have it and its value come from the model (IKFCRuns::LabelKeysOfRows);
//  the write is the model's too (IKFCRuns::WriteLabelRows) - these ask, nothing more. The main thread only.
//  TEST BUILDS: the fault switch label-answer answers instead of the window - its lines key=, value=, pick=<key>, edit=<key>=<value> (one a key), cancel - and
//  the trace says what was asked (LABELDLG).
//
//========================================================================================

#ifndef __KFCLabelDialogs_h__
#define __KFCLabelDialogs_h__

#include "PMString.h"
#include "KFCModelTypes.h"		// KFCLabelKey, KFCLabelEdit

#include <vector>

namespace KFCLabelDialogs
{
	/** ADD KEY... (LB4): Key: and Value: asked - Value on several lines. True = OK: out.key and out.value are set, neither empty (the dialog says so and
	    stays). False = Cancel (outWhy empty), or the dialog could not be shown (outWhy says so). */
	bool AskAddKey(KFCLabelEdit& out, PMString& outWhy);

	/** EDIT VALUE...: the keys' list (each "<key>  (<n> of <holders>)") over Value:. A key picked shows its value - empty, with a note, when the holders'
	    values differ; a value typed is kept for its key while another is picked, and OK gives every key whose value was changed (an empty one takes the
	    key away - insertLabel's rule, and the Script Label panel's). True = OK with at least one key changed (out); false = Cancel, nothing changed, or the
	    dialog not shown (outWhy). */
	bool AskEditValues(const std::vector<KFCLabelKey>& keys, int32 holders, std::vector<KFCLabelEdit>& out, PMString& outWhy);

	/** REMOVE KEY...: the keys' list - the key picked (the dialog says so, and stays, when none is). True = OK (outKey); false = Cancel, or not shown (outWhy). */
	bool AskRemoveKey(const std::vector<KFCLabelKey>& keys, int32 holders, PMString& outKey, PMString& outWhy);

	/** THE KEYS' LIST'S ROWS while a dialog stands (KFCQueryList's adapter and row maker, KFCQueryRowTip - the list's widget kKFCLabelKeyListWidgetID): how
	    many, a row's words, and its tip (the key's value as it reads, or that the values differ). 0 rows / empty words with no dialog. */
	int32 KeyRowCount();
	PMString KeyRowText(int32 index);
	PMString KeyRowTip(int32 index);
}

#endif // __KFCLabelDialogs_h__

// End, KFCLabelDialogs.h.
