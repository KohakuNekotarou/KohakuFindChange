//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Kohaku tab's face in InDesign's Find/Change dialog - see KFCKohakuTab.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IActiveContext.h"
#include "IApplication.h"				// the keyboard - a box that goes takes its focus with it
#include "ICallbackTimer.h"				// the base boxes settle at the next idle (kEndOfTime comes with it)
#include "IControlView.h"
#include "IDropDownListController.h"	// the face's lists' entries
#include "IEditBoxAttributes.h"			// SelectRange - a base box's caret, when what was typed goes on into it
#include "IEventHandler.h"				// ...the focus's handler
#include "ISaveFileDialog.h"			// (and DocumentID.h - kSaveFileDialogBoss) Save List...'s file
#include "IKeyBoard.h"
#include "IObserver.h"					// the selection observer, attached with the face
#include "IPanelMgr.h"					// Find in KFC: the panel shown first when it is closed
#include "IPanelControlData.h"
#include "ISelectableDialogSwitcher.h"	// the dialog's current face, asked of the face's own parent
#include "ISession.h"
#include "IStringListControlData.h"		// the style lists' strings, Search:'s entries
#include "IStyleGroupHierarchy.h"
#include "IStyleGroupManager.h"
#include "ISubject.h"
#include "ITextControlData.h"			// the QR part's lines, the Ruby part's fields
#include "ITriStateControlData.h"		// the boxes
#include "IWalkerScopeFactoryUtils.h"	// its Search: values
#include "IWidgetParent.h"
#include "IWorkspace.h"
#include "widgetid.h"					// IID_ISTRINGLISTCONTROLDATA / IID_ITRISTATECONTROLDATA / IID_IBOOLEANCONTROLDATA / IID_ITEXTCONTROLDATA and their messages

// General includes:
#include "AbstractDialogObserver.h"		// a selectable dialog face's observer (its header says so)
#include "CAlert.h"						// the Table part's Cannot find a match and Search is completed, InDesign's own words
#include "LocaleSetting.h"				// the UI language - Ruby is a Japanese InDesign's
#include "CDialogController.h"
#include "CPanelCreator.h"
#include "CreateObject.h"				// ::CreateObject2 - Save List...'s dialog, the base boxes' timer
#include "FileUtils.h"					// a ruby list's name
#include "PMRect.h"						// the Ruby part's boxes' frames
#include "SDKFileHelper.h"				// SDKFileOpenChooser - Choose List...
#include "SelectionObserver.h"			// ActiveSelectionObserver - the Table part's Change greys when the selection leaves what Find Next found
#include "TablesID.h"					// IID_ITABLESTYLEGROUPMANAGER
#include "UIDList.h"

#include <string>		// (test builds: the fault switch ruby-list-file's line)
#include <vector>

// (windows.h is in already: the precompiled header brings it - KFCPanelAlpha.cpp says how.)
#include <imm.h>		// InputMethodComposing - a base box leaves what an input method is still composing alone
#pragma comment(lib, "imm32.lib")

// Project includes:
#include "KFCKohakuTab.h"
#include "KFCUIID.h"
#include "KFCDiag.h"					// KFC_DIAG_LOG - the face's calls, in a test build's trace (QRTAB / TBLTAB / RUBYTAB lines)
#include "KFCHowTo.h"					// ShowRubyMarks, ShowKenten - the tab's How to Use...
#include "KFCPanelState.h"				// KFCWriteFileSafely - Save List...'s file
#include "KFCPanelTitle.h"				// Update - the panel's title names the kind and its Search:
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow - the Application Bar's field shows the tab's nothing
#include "KFCJump.h"					// LandOnQRCode, LandOnPlace - Find Next lands as a row's click does
#include "KFCBookPanelLookup.h"		// QueryPanelManager - Find in KFC shows the panel first
#include "KFCResultHistory.h"			// Find - Find in KFC is the panel's own Find
#include "KFCModelAccess.h"				// KFCRuns()->QRCodeNext - the QR code after the one found last (the model's)
#include "KFCKohakuQueryMenu.h"			// the face shown and hidden, and what it asks changed: Find/Change's own Query menu
#include "KFCResultTree.h"				// RefreshRows - a list's row of a table the Table part's Change wrote
#include "KFCRubyChangePlan.h"			// IsSpace - what the base boxes leave out, as the row's text is compared
#include "KFCTextCut.h"					// KFCIsAttachedMark - a base box takes a character with its marks

namespace
{

// Find:'s entries, in the face's order (KFCUI.fr).
const int32 kKindQRCode = 0;
const int32 kKindTable = 1;
const int32 kKindRuby = 2;
const int32 kKindKenten = 3;

// Search:'s entries: the .fr's two for every kind, the three after them for what stands in a story's text (ShowScopeEntries).
const IWalkerScopeFactoryUtils::WalkScopeType kScopeOfEntry[] =
{
	IWalkerScopeFactoryUtils::kAllDocumentScope,
	IWalkerScopeFactoryUtils::kDocumentScope,
	IWalkerScopeFactoryUtils::kStoryScope,
	IWalkerScopeFactoryUtils::kToEndOfStoryScope,
	IWalkerScopeFactoryUtils::kSelectionScope,
};
const int32 kEntriesForAnyKind = 2;
const int32 kEntriesInAStory = 5;

bool gShown = false;		// the face is shown - from its AutoAttach to its AutoDetach
bool gClosedOnIt = false;	// the dialog closed with the face shown - until the face is shown again
bool gYield = false;		// a saved query loaded since states its own tab (YieldToSavedQuery) - until the face is shown again, or Find: picked
bool gStated = false;		// a saved query of this tab's Find is under way (StateQuery) - until EndStatedFind
int32 gStatedScope = IWalkerScopeFactoryUtils::kDocumentScope;	// ...over this Search:
IPMUnknown* gShownFace = nil;	// the face, while shown - from its AutoAttach to its AutoDetach (not AddRef'd): a query stated with it shown writes its widgets
int32 gKind = kKindQRCode;	// its Find: - QR Code at launch, the tab InDesign's dialog first had
// WHAT EACH KIND LAST SAID ON THE FACE'S TWO LINES (docs/superpowers/specs/2026-11-02-kfc-kohaku-face-buttons-design.md L4 - the author, 11-02: "the same place as the other messages"): the first line, what a button or the list
// said; the second, why what was found could not be selected. One pair of lines for every kind - shown again as its kind is.
const int32 kKindCount = 4;
PMString gSaid[kKindCount][2];
int32 gScope = IWalkerScopeFactoryUtils::kDocumentScope;	// its Search: - Document at launch
KFCTableConditions gTable;	// what the Table part asks, kept from its widgets (its scope is gScope)
KFCRubyConditions gRuby;	// what the Ruby part asks
KFCKentenConditions gKenten;	// what the Kenten part asks

// THE RUBY PART'S BOXES (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md F14 - F16): what each holds, as gRuby holds it (KeepFromBoxes). The widgets' own text is the same - the face can be hidden and written (a saved query) with
// none to read. Find's Ruby: and Base Text: a column each, the Change side's reading over the same columns; its base text is Find's.
PMString gCellFindRuby[kKFCRubyBoxColumns];
PMString gCellBase[kKFCRubyBoxColumns];
PMString gCellChange[kKFCRubyBoxColumns];
bool gJoin[kKFCRubyBoxColumns];				// gJoin[k]: column k and the next are one reading - k's boxes, over both base boxes - in Find and Change alike
const int32 gCols = kKFCRubyBoxColumns;		// how many stand: all of them, always (the author, 11-02: "eight, fixed")

// THE TABLE PART'S FIND NEXT (F3 - F6): the table it found last and how it left things - let go with gLastFound, and for gLastFound's reason.
KFCTableFound gFoundTable;
bool gFoundSelected = false;				// the landing left it selected
bool gFoundWritten = false;					// Change or Change All has written since
UIDRef gLeftStory;							// the text selection the landing left (gNull: none) - it is "as left" while it still reads so
TextIndex gLeftStart = kInvalidTextIndex;
TextIndex gLeftEnd = kInvalidTextIndex;
void BoxesFromKept();

// THE FULL PATHS BEHIND THE STYLE LISTS' ENTRIES (entry n = gFindPaths[n - 1] - entry 0 asks for nothing), as FillStyles wrote them.
std::vector<PMString> gFindPaths;
std::vector<PMString> gChangePaths;

// THE QR CODE FIND NEXT FOUND LAST - where its next press goes on from (Q10: not the selection). Kept only while the face is shown - let go when it is shown again and when the dialog closes:
// a closed document's database address can be a document opened later (memory uidref-reuse-after-close), and a QR code of the same UID there would be taken for it. NOT let go when a document
// closes and another opens while the face stays shown - tried once, the address was not reused (docs/ai-notes/kfc-qr-recheck-2026-10-28.md O21-1).
UIDRef gLastFound;

// What Find Next found, on either part, let go (gLastFound's note says when).
void ForgetFound()
{
	gLastFound = UIDRef();
	gFoundTable = KFCTableFound();
	gFoundSelected = false;
	gFoundWritten = false;
	gLeftStory = UIDRef::gNull;
	gLeftStart = kInvalidTextIndex;
	gLeftEnd = kInvalidTextIndex;
}

// TEST BUILDS: the one-shot fault switch kohaku-tab-reset (KFCDiag.h) - what the tab keeps as at launch: a case's saved query left its conditions, as it should, and the next case's Find would ask them.
void TakeTestReset()
{
#ifdef KFC_DIAG
	if (KFC_DIAG_FAULT("kohaku-tab-reset"))
	{
		KFCDiagFaultOff("kohaku-tab-reset");
		gKind = kKindQRCode;
		gTable = KFCTableConditions();
		gRuby = KFCRubyConditions();
		gKenten = KFCKentenConditions();
		BoxesFromKept();
		for (int32 k = 0; k < kKindCount; ++k)
		{
			gSaid[k][0] = PMString();
			gSaid[k][1] = PMString();
		}
	}
#endif
}

bool FaceIsCurrent()
{
	TakeTestReset();
	return gStated || (!gYield && (gShown || gClosedOnIt));
}

// Search: as KFC's Find asks it now.
int32 ScopeAsked()
{
	return gStated ? gStatedScope : gScope;
}

// IS THE FACE THE DIALOG'S CURRENT ONE - asked of its own parent, the dialog's selectable switcher (no window is walked: the header's hazard). In the face's ClosingDialog the switcher still
// answers the face the dialog is closing on (measured).
bool SwitcherShowsFace(IPMUnknown* face)
{
	InterfacePtr<IWidgetParent> parent(face, UseDefaultIID());
	InterfacePtr<ISelectableDialogSwitcher> switcher((parent != nil)
		? static_cast<ISelectableDialogSwitcher*>(parent->QueryParentFor(IID_ISELECTABLEDIALOGSWITCHER)) : nil);
	if (switcher == nil)
		return false;
	const int32 index = switcher->GetCurrentPanelIndex();
	return index >= 0 && switcher->GetPanelWidgetID(index) == kKFCKohakuTabPanelWidgetID;
}

// One of the face's drop-downs (AddRef'd), or nil.
IDropDownListController* QueryList(IPMUnknown* face, const WidgetID& widget)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	return list.forget();
}

const char* KindWord();	// (below, with the face)

// ----- the face's two lines (docs/superpowers/specs/2026-11-02-kfc-kohaku-face-buttons-design.md L4)

// One of the face's lines, written.
void SetFaceLine(IPanelControlData* panel, const WidgetID& widget, const PMString& words)
{
	IControlView* const line = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITextControlData> text(line, UseDefaultIID());
	if (text != nil)
		text->SetString(words);
}

// WHAT A KIND SAYS: kept as that kind's, and on the face's two lines while the face shows that kind - the first, what a button or the list said; the second, why what was found could not be selected (empty when it
// was): a line each, so a long content cuts its own line and never the reason (KFCUI.fr).
void SayFor(int32 kind, const PMString& first, const PMString& second)
{
	if (kind < 0 || kind >= kKindCount)
		return;
	gSaid[kind][0] = first;
	gSaid[kind][0].SetTranslatable(kFalse);
	gSaid[kind][1] = second;
	gSaid[kind][1].SetTranslatable(kFalse);
	if (gShownFace == nil || kind != gKind)
		return;
	InterfacePtr<IPanelControlData> panel(gShownFace, UseDefaultIID());
	SetFaceLine(panel, kKFCQRTabStatusWidgetID, gSaid[kind][0]);
	SetFaceLine(panel, kKFCQRTabNoteWidgetID, gSaid[kind][1]);
}

// The lines as the kind shown now last left them (Find: switched, the face shown again).
void ShowSaid(IPanelControlData* panel)
{
	SetFaceLine(panel, kKFCQRTabStatusWidgetID, gSaid[gKind][0]);
	SetFaceLine(panel, kKFCQRTabNoteWidgetID, gSaid[gKind][1]);
}

// ----- the QR Code part

// FIND NEXT (the QR design's Q10 / D5): the QR code after the one found last, in the list's own order over the face's Search: (the model's KFCQRSearch::Next - read only), landed on as an object
// row's click lands (KFCJump::LandOnQRCode) - one it cannot select is named on the second line, with no alert (Q10); after the last, the first again, silently.
void QRFindNext()
{
	UIDRef next;
	int32 reach = 0;
	PMString words, note;
	words.SetTranslatable(kFalse);
	note.SetTranslatable(kFalse);
	// NOT WHILE A RUN OF THE MODEL'S IS UNDER WAY (KFCRunGuard - a row's click is turned away the same, KFCJump::ActivateNode): it can be standing inside its own message loop, with a chapter it
	// opened in hand.
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayFor(kKindQRCode, words, note);
		return;
	}
	if (KFCRuns()->QRCodeNext(gLastFound, next, reach, words))
	{
		gLastFound = next;
		(void)KFCJump::LandOnQRCode(next, reach, note);
	}
	// (Test builds: both lines in one - what the face shows, the reason after two spaces.)
	KFC_DIAG_LOG("QRTAB findnext %s%s%s", words.GetUTF8String().c_str(), note.IsEmpty() ? "" : "  ",
		note.GetUTF8String().c_str());
	SayFor(kKindQRCode, words, note);
}

// ----- the buttons beside Find Next: Table's (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md F1 - F9), and Ruby's and Kenten's the same way (2026-11-02-kfc-kohaku-face-buttons-design.md B1 - B5)

// IS THE TEXT SELECTION AS THE LAST LANDING LEFT IT? (A place, read now: a flag raised by the selection's own messages would have to tell the landing's from the person's.)
bool SelectionAsLeft()
{
	UIDRef story;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	(void)KFCJump::ReadTextSelection(story, start, end);
	return story == gLeftStory && start == gLeftStart && end == gLeftEnd;
}

// The model's mode for the text kinds' buttons (IKFCRuns::FaceTextNext and the rest).
int32 TextModeOfKind()
{
	return (gKind == kKindKenten) ? static_cast<int32>(kKFCKentenSearchMode) : static_cast<int32>(kKFCRubySearchMode);
}

// DOES THE KIND'S CHANGE SIDE ASK ANYTHING (B5)? Table: a style, or Clear Overrides. Ruby: with Text Without Ruby a reading in a Change Ruby To box; with the rest always - no reading takes the ruby off (the author,
// 11-01), a mark is made ruby, a list's word gets its reading. Kenten: Change Kenten To or Remove Kenten.
bool ChangeSideAsks()
{
	switch (gKind)
	{
		case kKindTable:
			return !gTable.changeStyle.IsEmpty() || gTable.clearOverrides;
		case kKindRuby:
			if (gRuby.kind != kKFCRubyFindWithout)
				return true;
			for (size_t p = 0; p < gRuby.change.size(); ++p)
				if (!gRuby.change[p].reading.IsEmpty())
					return true;
			return false;
		case kKindKenten:
			return gKenten.changeTo != 0 || gKenten.removeKenten;
		default:
			return false;
	}
}

// Change and Change/Find are for what Find Next found, while it is selected and has not been written; all of them want something on the Change side. Grey otherwise, as the Text tab's after the selection moves or a Change.
void ShowFaceButtons(IPanelControlData* panel)
{
	if (panel == nil || gKind == kKindQRCode)
		return;
	const bool asks = ChangeSideAsks();
	const bool onFound = asks && gFoundTable.doc != UIDRef::gNull && gFoundSelected && !gFoundWritten && SelectionAsLeft();
	IControlView* const change = panel->FindWidget(kKFCTableTabChangeWidgetID);
	if (change != nil)
		change->Enable(onFound ? kTrue : kFalse);
	IControlView* const changeFind = panel->FindWidget(kKFCTableTabChangeFindWidgetID);
	if (changeFind != nil)
		changeFind->Enable(onFound ? kTrue : kFalse);
	IControlView* const changeAll = panel->FindWidget(kKFCTableTabChangeAllWidgetID);
	if (changeAll != nil)
		changeAll->Enable(asks ? kTrue : kFalse);
}

// Where the model goes on from.
KFCTableLook FaceLookNow()
{
	KFCTableLook look;
	look.lastDoc = gFoundTable.doc;
	look.lastStory = gFoundTable.story;
	look.lastAt = gFoundTable.at;
	look.onLast = (gFoundTable.doc != UIDRef::gNull) && SelectionAsLeft();
	TextIndex start = kInvalidTextIndex;
	(void)KFCJump::ReadTextSelection(look.selStory, start, look.selEnd);
	return look;
}

// InDesign's own alert words - with the number and what it counts, when it says one.
void InformationAlertOf(const char* words, int32 count, const char* counted)
{
	PMString said(words);
	said.SetTranslatable(kFalse);
	if (count >= 0)
	{
		said.AppendNumber(count);
		said.Append(counted);
	}
	CAlert::InformationAlert(said);
}

// FIND NEXT ON TABLE, RUBY OR KENTEN: the next table, or the next place the kind finds, over Search: (KFCTableSearch::Next, KFCFaceText::Next say from where), landed on as its row's click lands. One it cannot
// select is named on the second line; none at all is InDesign's own alert.
void FaceFindNext()
{
	InterfacePtr<IPanelControlData> panel(gShownFace, UseDefaultIID());
	PMString words, note;
	words.SetTranslatable(kFalse);
	note.SetTranslatable(kFalse);
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayFor(gKind, words, note);
		return;
	}
	const bool table = (gKind == kKindTable);
	KFCTableFound found;
	const KFCTableLook look(FaceLookNow());
	if (!(table ? KFCRuns()->TableNext(look, found) : KFCRuns()->FaceTextNext(TextModeOfKind(), look, found)))
	{
		gFoundTable = KFCTableFound();
		gFoundSelected = false;
		gFoundWritten = false;
		SayFor(gKind, found.words, note);
		KFC_DIAG_LOG("%s findnext none %s", KindWord(), found.words.GetUTF8String().c_str());
		ShowFaceButtons(panel);
		if (found.words.IsEmpty())
			InformationAlertOf("Cannot find a match.", -1, "");
		return;
	}
	bool selected = false;
	KFCJump::LandOnPlace(found.story, found.at, found.end, found.reach, table ? "table" : "text", note, selected);
	gFoundTable = found;
	gFoundSelected = selected;
	gFoundWritten = false;
	(void)KFCJump::ReadTextSelection(gLeftStory, gLeftStart, gLeftEnd);
	// (Why it could not be selected first: that is what stops the person now. Over a Search: searched as Document.)
	if (note.IsEmpty())
		note = found.note;
	KFC_DIAG_LOG("%s findnext %s%s%s selected=%d", KindWord(), found.words.GetUTF8String().c_str(), note.IsEmpty() ? "" : "  ", note.GetUTF8String().c_str(), selected ? 1 : 0);
	SayFor(gKind, found.words, note);
	ShowFaceButtons(panel);
}

// CHANGE: the Change side on what Find Next found. thenNext (Change/Find): on to the next, written or not - the reason it was not on the second line.
void FaceChange(bool thenNext)
{
	InterfacePtr<IPanelControlData> panel(gShownFace, UseDefaultIID());
	PMString words, none;
	words.SetTranslatable(kFalse);
	none.SetTranslatable(kFalse);
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayFor(gKind, words, none);
		return;
	}
	const bool wrote = (gKind == kKindTable) ? KFCRuns()->TableChangeFound(gFoundTable, words) : KFCRuns()->FaceTextChangeFound(TextModeOfKind(), gFoundTable, words);
	KFC_DIAG_LOG("%s change wrote=%d %s", KindWord(), wrote ? 1 : 0, words.GetUTF8String().c_str());
	if (wrote)
	{
		gFoundWritten = true;
		KFCResultTree::RefreshRows();		// (a row of the list that stands for it reads as written)
	}
	if (thenNext)
	{
		FaceFindNext();
		if (!wrote)
			SayFor(gKind, gSaid[gKind][0], words);
		return;
	}
	SayFor(gKind, words, none);
	ShowFaceButtons(panel);
}

// CHANGE ALL: everything of Search: given the Change side, one Ctrl+Z, said as InDesign says it.
void FaceChangeAll()
{
	InterfacePtr<IPanelControlData> panel(gShownFace, UseDefaultIID());
	PMString words, none;
	words.SetTranslatable(kFalse);
	none.SetTranslatable(kFalse);
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayFor(gKind, words, none);
		return;
	}
	const bool table = (gKind == kKindTable);
	int32 changed = 0, matched = 0;
	const bool ran = table ? KFCRuns()->TableChangeAll(FaceLookNow(), changed, matched, words) : KFCRuns()->FaceTextChangeAll(TextModeOfKind(), FaceLookNow(), changed, matched, words);
	KFC_DIAG_LOG("%s changeall ran=%d changed=%d matched=%d %s", KindWord(), ran ? 1 : 0, static_cast<int>(changed), static_cast<int>(matched), words.GetUTF8String().c_str());
	if (ran && changed > 0)
	{
		gFoundWritten = true;
		KFCResultTree::RefreshRows();
	}
	SayFor(gKind, words, none);
	ShowFaceButtons(panel);
	if (!ran)
		return;
	if (matched == 0)
		InformationAlertOf("Cannot find a match.", -1, "");
	else
		InformationAlertOf("Search is completed. ", changed, table ? " table(s) changed." : " replacement(s) made.");
}

// FIND IN KFC (the author, 11-02: "Find For KFC" - Words in a Ruby List was searched from KFC's panel with nothing on the face to say so): the panel's own Find (KFCResultHistory::Find - the door the panel's Find and the
// query dialog's go through: the same search, bar and status line), the panel shown first when it is closed - the keyboard left where it is. What it said, on the face's line too. Not while a run or a landing is under
// way (the panel's Find's doors: KFCActionComponent's kKFCSearchBookActionID).
void FindInKFC()
{
	PMString words, none;
	words.SetTranslatable(kFalse);
	none.SetTranslatable(kFalse);
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayFor(gKind, words, none);
		return;
	}
	if (KFCJump::IsLanding())
		return;
	InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
	const bool opened = (panelMgr != nil && !panelMgr->IsPanelWithWidgetIDShown(kKFCPanelWidgetID));
	if (opened)
		panelMgr->ShowPanelByWidgetID(kKFCPanelWidgetID, kFalse);
	PMString summary;
	(void)KFCResultHistory::Find(KFCResultHistory::QueryLabel(), summary);
	KFC_DIAG_LOG("%s findinkfc opened=%d %s", KindWord(), opened ? 1 : 0, summary.GetUTF8String().c_str());
	SayFor(gKind, summary, none);
}
// ----- the Table part

// A style list as the drop-down shows it, kept (entry 0 asks for nothing; an entry past the paths leaves the kept one).
void KeepStyleOf(IDropDownListController* list, const std::vector<PMString>& paths, PMString& ioStyle)
{
	const int32 entry = (list != nil) ? list->GetSelected() : -1;
	if (entry == 0)
		ioStyle.Clear();
	else if (entry > 0 && entry <= static_cast<int32>(paths.size()))
		ioStyle = paths[static_cast<size_t>(entry - 1)];
	ioStyle.SetTranslatable(kFalse);
}

// A box of the face set from what is kept - not heard (notifyOfChange kFalse).
void SetBox(IPanelControlData* panel, const WidgetID& widget, bool on)
{
	IControlView* const view = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITriStateControlData> box(view, UseDefaultIID());
	if (box == nil)
		return;
	if (on)
		box->Select(kTrue, kFalse);
	else
		box->Deselect(kTrue, kFalse);
}

// A style list written: its first entry stays (the .fr's - it is translated there), the styles after it, and the style asked for selected - added at the end when this document has none of that
// name: it is still what the face asks (a list's search goes by the name, in every document).
void FillStyleList(IPMUnknown* face, const WidgetID& widget, const std::vector<PMString>& paths, const std::vector<PMString>& names,
	const PMString& asked, std::vector<PMString>& outPaths)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<IStringListControlData> strings(dropDown, IID_ISTRINGLISTCONTROLDATA);
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	if (strings == nil || list == nil)
		return;
	while (strings->Length() > 1)
		strings->RemoveString(strings->Length() - 1, kFalse, kFalse);
	outPaths = paths;
	for (size_t k = 0; k < names.size(); ++k)
		strings->AddString(names[k], IStringListControlData::kEnd, kFalse, kFalse);
	int32 entry = 0;
	if (!asked.IsEmpty())
	{
		size_t at = 0;
		while (at < outPaths.size() && outPaths[at] != asked)
			++at;
		if (at == outPaths.size())
		{
			outPaths.push_back(asked);
			strings->AddString(KFCStylePathWords(asked), IStringListControlData::kEnd, kFalse, kFalse);
		}
		entry = static_cast<int32>(at) + 1;
	}
	list->Select(entry, kTrue, kFalse);
}

// THE STYLE LISTS WRITTEN FROM WHAT IS KEPT AND THE FRONT DOCUMENT'S TABLE STYLES (the session's defaults with no document: the context's workspace). Its own widgets only. Not heard.
void FillStyles(IPMUnknown* face)
{
	std::vector<PMString> paths, names;
	ISession* const session = GetExecutionContextSession();
	IActiveContext* const context = (session != nil) ? session->GetActiveContext() : nil;
	IWorkspace* const workspace = (context != nil) ? context->GetContextWorkspace() : nil;
	InterfacePtr<IStyleGroupManager> styles(workspace, IID_ITABLESTYLEGROUPMANAGER);
	if (styles != nil)
	{
		IDataBase* const db = ::GetDataBase(styles);
		UIDList found(db);
		styles->GetRootHierarchy()->GetDescendents(&found, IID_ISTYLEINFO);
		for (int32 k = 0; k < found.Length(); ++k)
		{
			InterfacePtr<IStyleGroupHierarchy> style(db, found[k], UseDefaultIID());
			if (style == nil)
				continue;
			PMString path(style->GetFullPath(kFalse));
			path.SetTranslatable(kFalse);
			paths.push_back(path);
			names.push_back(KFCStylePathWords(style->GetFullPath(kTrue)));
		}
	}
	FillStyleList(face, kKFCTableTabFindStyleWidgetID, paths, names, gTable.findStyle, gFindPaths);
	FillStyleList(face, kKFCTableTabChangeStyleWidgetID, paths, names, gTable.changeStyle, gChangePaths);
	KFC_DIAG_LOG("TBLTAB filled styles=%d find=%s change=%s", static_cast<int>(names.size()),
		gTable.findStyle.GetUTF8String().c_str(), gTable.changeStyle.GetUTF8String().c_str());
}

// ----- the Ruby part

void SetField(IPanelControlData* panel, const WidgetID& widget, const PMString& words)
{
	IControlView* const view = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITextControlData> field(view, UseDefaultIID());
	if (field != nil)
		field->SetString(words, kTrue, kFalse);
}

// ----- the Ruby part's Change boxes (KFCUI.fr's grid; F14 - F16)

// The numbered rows of widgets.
WidgetID ReadingBoxID(int32 k)
{
	return WidgetID(kKFCRubyReadingBox0WidgetID + k);
}

WidgetID BaseBoxID(int32 k)
{
	return WidgetID(kKFCRubyBaseBox0WidgetID + k);
}

WidgetID JoinID(int32 k)
{
	return WidgetID(kKFCRubyJoin0WidgetID + k);
}

WidgetID ChangeBoxID(int32 k)
{
	return WidgetID(kKFCRubyChangeBox0WidgetID + k);
}

WidgetID ChangeBaseID(int32 k)
{
	return WidgetID(kKFCRubyChangeBase0WidgetID + k);
}

// Which of a numbered row this widget is, or -1.
int32 ColumnOf(const WidgetID& widget, const WidgetID& first, int32 count)
{
	const int32 n = static_cast<int32>(widget.Get()) - static_cast<int32>(first.Get());
	return (n >= 0 && n < count) ? n : -1;
}

// A text's characters as a base box takes them: a mark that belongs to a character stays with it, and a space is no character of it (the row's text is compared without them - KFCRubyChangePlan).
std::vector<PMString> CharsOf(const PMString& words)
{
	std::vector<PMString> out;
	const WideString wide(words);
	for (WideString::const_iterator at = wide.begin(); at != wide.end(); ++at)
	{
		const uint32 code = static_cast<uint32>(*at);
		if (KFCRubyChangePlan::IsSpace(static_cast<char32_t>(code)))
			continue;
		if (!out.empty() && KFCIsAttachedMark(UTF32TextChar(code)))
		{
			out.back().AppendW(UTF32TextChar(code));
			continue;
		}
		PMString one;
		one.SetTranslatable(kFalse);
		one.AppendW(UTF32TextChar(code));
		out.push_back(one);
	}
	return out;
}

// THE BASE BOXES FROM A BASE TEXT THAT CAME WHOLE (a saved query from before the boxes), a character a box - past the last box, all the rest in it.
void FillBaseFromKept()
{
	const std::vector<PMString> chars(CharsOf(gRuby.base));
	for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
	{
		gCellBase[k].Clear();
		gCellBase[k].SetTranslatable(kFalse);
	}
	for (size_t c = 0; c < chars.size(); ++c)
		gCellBase[(static_cast<int32>(c) < kKFCRubyBoxColumns) ? static_cast<int32>(c) : kKFCRubyBoxColumns - 1].Append(chars[c]);
}

// WHAT THE BOXES ASK, KEPT. A search reads base and reading, every box's words run together: Find's boxes say how the Change side's stand, and a mono ruby and a group ruby of the same words are found alike. A row's Replace reads the pieces: one for each joined run of
// columns, its Change reading over the base boxes of those columns. A column holding nothing asks nothing.
void KeepFromBoxes()
{
	gRuby.change.clear();
	gRuby.base.Clear();
	gRuby.base.SetTranslatable(kFalse);
	gRuby.reading.Clear();
	gRuby.reading.SetTranslatable(kFalse);
	for (int32 k = 0; k < gCols; ++k)
	{
		KFCRubyChangePiece piece;
		piece.reading = gCellChange[k];
		piece.reading.SetTranslatable(kFalse);
		piece.findReading = gCellFindRuby[k];
		piece.findReading.SetTranslatable(kFalse);
		piece.base = gCellBase[k];
		while (k + 1 < gCols && gJoin[k])
		{
			++k;
			piece.base.Append(gCellBase[k]);
		}
		piece.base.SetTranslatable(kFalse);
		gRuby.reading.Append(piece.findReading);
		gRuby.base.Append(piece.base);
		if (!piece.reading.IsEmpty() || !piece.findReading.IsEmpty() || !piece.base.IsEmpty())
			gRuby.change.push_back(piece);
	}
}

// THE BOXES FROM WHAT IS KEPT - a saved query's pieces, a character of base text a box and those of one piece joined (one reading over them). A file from before Find's boxes holds Ruby: whole: it comes back in the first box. One from before the boxes has no base text in its
// pieces: its readings in the first boxes and its base text spread over the base boxes - one reading over all of it, as it read.
void BoxesFromKept()
{
	for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
	{
		gCellFindRuby[k].Clear();
		gCellFindRuby[k].SetTranslatable(kFalse);
		gCellBase[k].Clear();
		gCellBase[k].SetTranslatable(kFalse);
		gCellChange[k].Clear();
		gCellChange[k].SetTranslatable(kFalse);
		gJoin[k] = false;
	}
	bool anyBase = false, anyFindReading = false;
	int32 readings = 0;
	for (size_t p = 0; p < gRuby.change.size(); ++p)
	{
		anyBase = anyBase || !CharsOf(gRuby.change[p].base).empty();
		anyFindReading = anyFindReading || !gRuby.change[p].findReading.IsEmpty();
		if (!gRuby.change[p].reading.IsEmpty())
			++readings;
	}
	if (!anyFindReading)
		gCellFindRuby[0] = gRuby.reading;
	int32 col = 0;
	if (!anyBase)
	{
		for (size_t p = 0; p < gRuby.change.size() && col < kKFCRubyBoxColumns; ++p)
		{
			gCellChange[col] = gRuby.change[p].reading;
			if (anyFindReading)
				gCellFindRuby[col] = gRuby.change[p].findReading;
			++col;
		}
		FillBaseFromKept();
		if (readings == 1)
			for (int32 k = 0; k + 1 < gCols; ++k)
				gJoin[k] = !gCellBase[k + 1].IsEmpty();
		return;
	}
	for (size_t p = 0; p < gRuby.change.size(); ++p)
	{
		const std::vector<PMString> chars(CharsOf(gRuby.change[p].base));
		if (col >= kKFCRubyBoxColumns)
		{
			for (size_t c = 0; c < chars.size(); ++c)
				gCellBase[kKFCRubyBoxColumns - 1].Append(chars[c]);
			continue;
		}
		gCellChange[col] = gRuby.change[p].reading;
		gCellChange[col].SetTranslatable(kFalse);
		if (anyFindReading)
		{
			gCellFindRuby[col] = gRuby.change[p].findReading;
			gCellFindRuby[col].SetTranslatable(kFalse);
		}
		for (size_t c = 0; c < chars.size(); ++c)
		{
			const int32 at = col + static_cast<int32>(c);
			if (at >= kKFCRubyBoxColumns)
			{
				gCellBase[kKFCRubyBoxColumns - 1].Append(chars[c]);
				continue;
			}
			gCellBase[at] = chars[c];
			if (c > 0)
				gJoin[at - 1] = true;
		}
		col += chars.empty() ? 1 : static_cast<int32>(chars.size());
		if (col > kKFCRubyBoxColumns)
			col = kKFCRubyBoxColumns;
	}
}

void ShowRubyButtons(IPanelControlData* panel);	// (below, with the face's buttons)

// WHERE A COLUMN STANDS IN THE GRID (KFCUI.fr): the labels to 110, the columns from 115 at the size twelve of them had between 115 and 422 (the author, 11-02: "from the start this size, eight, fixed" - the
// eight end at 319, inside the left column's 330).
const int32 kGridColumnsLeft = 115;
const int32 kGridColumnsRight = 422;
const int32 kGridBoxGap = 2;
const int32 kGridPitchColumns = 12;

int32 GridColumnLeft(int32 k)
{
	return kGridColumnsLeft + ((kGridColumnsRight - kGridColumnsLeft + kGridBoxGap) * k) / kGridPitchColumns;
}

// A widget of the grid put where it goes, or away: hidden is disabled too (ShowPart's rule).
void PlaceGridWidget(IPanelControlData* panel, const WidgetID& widget, bool shown, bool enabled, int32 left, int32 top, int32 right, int32 bottom)
{
	IControlView* const view = (panel != nil) ? panel->FindWidget(widget) : nil;
	if (view == nil)
		return;
	if (shown)
		view->SetFrame(PMRect(PMReal(left), PMReal(top), PMReal(right), PMReal(bottom)));
	view->Enable((shown && enabled) ? kTrue : kFalse);
	view->ShowView(shown ? kTrue : kFalse);
}

// THE GRID AS THE COLUMNS AND THE JOINS NOW SAY (the frames set here, no EVE layout: the window keeps its size - InDesign's LinkInfoPanelObserver places its widgets the same way). Each column's base box and its words on the Change side are its own; a reading box - Find's, and
// Change's, which follow it (the author, 11-01) - runs over the columns joined to it, and the boxes it runs over are away. GREY: Find's Ruby: with Text Without Ruby, whose search has no reading; and all of it with the part hidden.
void LayOutRubyGrid(IPanelControlData* panel)
{
	const bool part = (gKind == kKindRuby);
	// WORDS IN A RUBY LIST ASKS NOTHING OF THE BOXES: they go, and what it says of its file stands in their place.
	const bool listing = part && gRuby.kind == kKFCRubyFindList;
	// NOR DOES RUBY MARKS IN TEXT (the author, 11-02: "not needed? then hide them"): the reading is in the mark, and every mark is a row - a row's own Replace is how one is left out. They go, and what they hold asks nothing of it (KFCRubySearch.cpp's WalkMarks).
	const bool boxes = !listing && !(part && gRuby.kind == kKFCRubyFindMarks);
	const bool findReads = part && gRuby.kind != kKFCRubyFindWithout;
	PlaceGridWidget(panel, kKFCRubyGridLabelReadingWidgetID, boxes, part, 0, 0, 110, 21);
	PlaceGridWidget(panel, kKFCRubyGridLabelBaseWidgetID, boxes, part, 0, 41, 110, 62);
	PlaceGridWidget(panel, kKFCRubyGridLabelChangeWidgetID, boxes, part, 0, 72, 110, 93);
	PlaceGridWidget(panel, kKFCRubyGridLabelChangeBaseWidgetID, boxes, part, 0, 96, 110, 116);
	PlaceGridWidget(panel, kKFCRubyListLabelWidgetID, listing, listing, 0, 0, 110, 21);
	PlaceGridWidget(panel, kKFCRubyListNameWidgetID, listing, listing, 115, 0, 330, 21);
	PlaceGridWidget(panel, kKFCRubyListFirstLabelWidgetID, listing, listing, 0, 26, 110, 47);
	PlaceGridWidget(panel, kKFCRubyListFirstWidgetID, listing, listing, 115, 26, 285, 47);
	PlaceGridWidget(panel, kKFCRubyListOtherRubyWidgetID, listing, listing, 115, 52, 330, 71);
	ShowRubyButtons(panel);		// (Choose List... is usable with Words in a Ruby List alone)
	for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
	{
		const bool stands = boxes && (k < gCols);
		int32 last = k;
		while (stands && last + 1 < gCols && gJoin[last])
			++last;
		const bool overLeft = stands && k > 0 && gJoin[k - 1];
		const int32 left = stands ? GridColumnLeft(k) : 0;
		const int32 readingRight = stands ? GridColumnLeft(last + 1) - kGridBoxGap : 0;
		const int32 columnRight = stands ? GridColumnLeft(k + 1) - kGridBoxGap : 0;
		PlaceGridWidget(panel, ReadingBoxID(k), stands && !overLeft, findReads, left, 0, readingRight, 21);
		PlaceGridWidget(panel, BaseBoxID(k), stands, part, left, 41, columnRight, 62);
		PlaceGridWidget(panel, ChangeBoxID(k), stands && !overLeft, part, left, 72, readingRight, 93);
		PlaceGridWidget(panel, ChangeBaseID(k), stands, part, left, 96, columnRight, 116);
		if (k + 1 < kKFCRubyBoxColumns)
		{
			const bool between = boxes && (k + 1 < gCols);
			const int32 middle = between ? GridColumnLeft(k + 1) - (kGridBoxGap / 2) : 0;
			PlaceGridWidget(panel, JoinID(k), between, part, middle - 10, 21, middle + 10, 41);
		}
	}
}

// WHAT THE PART SAYS OF ITS LIST: the file's name beside Ruby List:, and on the face's lines how many words it holds or why it cannot be used (the author, 11-02: "the same place as the other messages") - read now, as a Find
// will read it.
void ShowRubyList(IPanelControlData* panel)
{
	if (panel == nil || gKind != kKindRuby || gRuby.kind != kKFCRubyFindList)
		return;
	PMString name;
	if (!gRuby.listPath.IsEmpty())
		FileUtils::GetFileName(FileUtils::PMStringToSysFile(gRuby.listPath), name);
	name.SetTranslatable(kFalse);
	int32 words = 0;
	PMString said;
	if (KFCRuns()->RubyListCheck(gRuby.listPath, words, said))
	{
		said.Clear();
		said.AppendNumber(words);
		said.Append((words == 1) ? " word." : " words.");
	}
	said.SetTranslatable(kFalse);
	SetFaceLine(panel, kKFCRubyListNameWidgetID, name);
	PMString none;
	none.SetTranslatable(kFalse);
	SayFor(kKindRuby, said, none);
	KFC_DIAG_LOG("RUBYTAB list name=%s said=%s", name.GetUTF8String().c_str(), said.GetUTF8String().c_str());
}

// CHOOSE LIST...: its file. Text files, and any file - a list may be called anything. (The SDK's own chooser, as Load Order... - KFCQueryOrderFile.cpp. TEST BUILDS - the fault switch ruby-list-file's first line is the file: Windows' dialogs cannot be pressed by a test.)
// False = none was chosen: nothing the face asks has changed.
bool ChooseRubyList(IPanelControlData* panel)
{
	bool chosen = false;
#ifdef KFC_DIAG
	std::string testPath;
	if (KFCDiagFaultText("ruby-list-file", testPath))
	{
		gRuby.listPath.SetUTF8String(testPath);
		chosen = true;
		KFC_DIAG_LOG("RUBYTAB list through the fault switch: %s", testPath.c_str());
	}
#endif
	if (!chosen)
	{
		SDKFileOpenChooser chooser;
		chooser.SetTitle(PMString(kKFCRubyListDialogTitleKey));		// "Choose Ruby List"
		chooser.AddFilter('TEXT', PMString("txt"), PMString(kKFCRubyListFileTypeKey));		// "Ruby List"
		chooser.AddFilter('TEXT', PMString("csv"), PMString(kKFCRubyListFileTypeKey));
		chooser.AddAllFiles();
		chooser.ShowDialog();
		if (!chooser.IsChosen())
			return false;
		gRuby.listPath = chooser.GetPath();
	}
	gRuby.listPath.SetTranslatable(kFalse);
	ShowRubyList(panel);
	return true;
}

// SAVE LIST... (the author, 11-02): the ruby of what Search: holds, read first - there may be nothing to save, and the book's or the document's name is the file's - then its file, asked of Windows' Save dialog: what Choose List... reads, in UTF-8 with its BOM (Notepad and Excel take it as UTF-8) -
// and what it came to said. Through the Save dialog of KFCChooseOrderFileToSave, whose reasons hold: the name always ends in .txt. TEST BUILDS - the fault switch ruby-list-save's first line is the file.
void SaveRubyList()
{
	PMString text, name, said;
	if (!KFCRuns()->RubyListText(text, name, said))
	{
		KFC_DIAG_LOG("RUBYTAB save refused: %s", said.GetUTF8String().c_str());
		CAlert::InformationAlert(said);
		return;
	}
	IDFile file;
	bool chosen = false;
#ifdef KFC_DIAG
	std::string testPath;
	if (KFCDiagFaultText("ruby-list-save", testPath))
	{
		PMString path;
		path.SetUTF8String(testPath);
		file = FileUtils::PMStringToSysFile(path);
		chosen = true;
	}
#endif
	if (!chosen)
	{
		InterfacePtr<ISaveFileDialog> dialog(::CreateObject2<ISaveFileDialog>(kSaveFileDialogBoss, IID_ISAVEFILEDIALOG));
		if (dialog == nil)
			return;
		PMString shownType(kKFCRubyListFileTypeKey);		// "Ruby List"
		shownType.Translate();
		dialog->AddFileTypeInfo(shownType, PMString("txt"));
		dialog->SetAdditionalFOSFlags(FOS_OVERWRITEPROMPT | FOS_NOREADONLYRETURN | FOS_STRICTFILETYPES);
		// (Its name with no extension of its own: a document's .indd, a book's .indb. A name with no folder: the dialog starts where Windows last saved.)
		PMString fileName(name);
		fileName.SetTranslatable(kFalse);
		const int32 dot = fileName.LastIndexOfWChar(UTF32TextChar('.'));
		if (dot > 0)
			fileName.Remove(dot, fileName.CharCount() - dot);
		fileName.Append(" Ruby List.txt");
		const IDFile defaultFile(FileUtils::PMStringToSysFile(fileName));
		PMString title(kKFCRubyListSaveTitleKey);		// "Save Ruby List"
		title.Translate();
		int32 selectedIndex = 0;
		if (dialog->DoDialog(&defaultFile, &file, &selectedIndex, kTrue /*the last used folder*/, kTrue /*the type menu*/, &title) == kFalse)
			return;
	}
	std::string bytes("\xEF\xBB\xBF");
	bytes.append(text.GetUTF8String());
	const char* const failure = KFCWriteFileSafely(file, bytes);
	PMString shownName;
	FileUtils::GetFileName(file, shownName);
	shownName.SetTranslatable(kFalse);
	PMString words;
	words.SetTranslatable(kFalse);
	if (failure != nil)
	{
		words.Append(shownName);
		words.Append(" could not be written (");
		words.Append(failure);
		words.Append(").");
	}
	else
	{
		words.Append("Saved to ");
		words.Append(shownName);
		words.Append(": ");
		words.Append(said);
	}
	words.SetTranslatable(kFalse);
	KFC_DIAG_LOG("RUBYTAB saved=%d name=%s said=%s", (failure == nil) ? 1 : 0, shownName.GetUTF8String().c_str(), words.GetUTF8String().c_str());
	CAlert::InformationAlert(words);
}

// EVERY BOX AND ITS WORDS WRITTEN FROM WHAT IS KEPT. Not heard (notifyOfChange kFalse).
void WriteBoxes(IPanelControlData* panel)
{
	for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
	{
		SetField(panel, ReadingBoxID(k), gCellFindRuby[k]);
		SetField(panel, BaseBoxID(k), gCellBase[k]);
		SetField(panel, ChangeBoxID(k), gCellChange[k]);
		SetField(panel, ChangeBaseID(k), gCellBase[k]);
		if (k + 1 < kKFCRubyBoxColumns)
			SetBox(panel, JoinID(k), gJoin[k]);
	}
}

// A BOX THAT IS GOING MUST NOT KEEP THE KEYBOARD (AddDeleteCaptionRowButtonObserver's rule - the keys would go on into a box no one sees): let go now, and the box to take it is answered - its row's, of the column that stands after this - or kInvalidWidgetID when no box is going.
WidgetID LetGoOfGoingBox()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> application((session != nil) ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyboard(application, UseDefaultIID());
	InterfacePtr<IControlView> focus((keyboard != nil) ? keyboard->GetKeyFocus() : nil, UseDefaultIID());
	if (focus == nil)
		return kInvalidWidgetID;
	const WidgetID held = focus->GetWidgetID();
	WidgetID (*boxOf)(int32) = ReadingBoxID;
	bool joins = true;	// (a reading box runs over the columns joined to it; a base box stands by itself)
	int32 col = ColumnOf(held, ReadingBoxID(0), kKFCRubyBoxColumns);
	if (col < 0)
	{
		boxOf = ChangeBoxID;
		col = ColumnOf(held, ChangeBoxID(0), kKFCRubyBoxColumns);
	}
	if (col < 0)
	{
		boxOf = BaseBoxID;
		joins = false;
		col = ColumnOf(held, BaseBoxID(0), kKFCRubyBoxColumns);
	}
	if (col < 0 || (col < gCols && !(joins && col > 0 && gJoin[col - 1])))
		return kInvalidWidgetID;
	keyboard->RelinquishKeyFocus();
	int32 to = (col < gCols) ? col : gCols - 1;
	while (joins && to > 0 && gJoin[to - 1])
		--to;
	return boxOf(to);
}

// THE BOXES ARE GOING, ALL OF THEM (Ruby Marks in Text, Words in a Ruby List): one holding the keyboard lets go - the keys would go on into a box no one sees (LetGoOfGoingBox's rule, below; it answers for the columns that stay).
void LetGoOfAnyBox()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> application((session != nil) ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyboard(application, UseDefaultIID());
	InterfacePtr<IControlView> focus((keyboard != nil) ? keyboard->GetKeyFocus() : nil, UseDefaultIID());
	if (focus == nil)
		return;
	const WidgetID held = focus->GetWidgetID();
	if (ColumnOf(held, ReadingBoxID(0), kKFCRubyBoxColumns) >= 0 || ColumnOf(held, BaseBoxID(0), kKFCRubyBoxColumns) >= 0 || ColumnOf(held, ChangeBoxID(0), kKFCRubyBoxColumns) >= 0)
		keyboard->RelinquishKeyFocus();
}

// IS AN INPUT METHOD STILL COMPOSING IN THE WINDOW WITH THE KEYBOARD? What a box holds then is not yet what the person typed, and moving it would break the composition. (IMM's own answer; an input method on the newer text services answers it through the same call - not measured here on one: only the author's hands can.)
bool InputMethodComposing()
{
	const HWND focus = ::GetFocus();
	const HIMC context = (focus != nullptr) ? ::ImmGetContext(focus) : nullptr;
	if (context == nullptr)
		return false;
	const LONG bytes = ::ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
	::ImmReleaseContext(focus, context);
	return bytes > 0;
}

// WHAT A BASE BOX WAS GIVEN, SPREAD (the author, 11-02: "more than one character typed goes on into the next box"): one character stays; more - typed, pasted, an input method's confirming - one a box from this one on, over what those hold, and
// past the last box all the rest in it (FillBaseFromKept's rule). A space is no character of it (CharsOf). THE KEYBOARD GOES WITH THEM if this box still has it - to the last box written, its caret after its character, so the next key goes on from there (a box hears a change when it loses
// the keyboard too: then it stays where the person put it). Only ever from SettleBaseBoxes - never while what was given is still arriving.
void SpreadBaseBox(IPanelControlData* panel, int32 k, const PMString& typed)
{
	gCellBase[k] = typed;
	if (panel == nil)
		return;
	const std::vector<PMString> chars(CharsOf(typed));
	if (chars.size() <= 1)
	{
		PMString one;
		one.SetTranslatable(kFalse);
		if (!chars.empty())
			one = chars[0];
		if (one != typed)
			SetField(panel, BaseBoxID(k), one);
		gCellBase[k] = one;
		SetField(panel, ChangeBaseID(k), one);
		return;
	}
	int32 last = k;
	for (size_t c = 0; c < chars.size(); ++c)
	{
		const int32 place = k + static_cast<int32>(c);
		if (place < kKFCRubyBoxColumns)
		{
			last = place;
			gCellBase[place] = chars[c];
		}
		else
			gCellBase[kKFCRubyBoxColumns - 1].Append(chars[c]);
	}
	for (int32 place = k; place <= last; ++place)
	{
		gCellBase[place].SetTranslatable(kFalse);
		SetField(panel, BaseBoxID(place), gCellBase[place]);
		SetField(panel, ChangeBaseID(place), gCellBase[place]);
	}
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> application((session != nil) ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyboard(application, UseDefaultIID());
	InterfacePtr<IControlView> focus((keyboard != nil) ? keyboard->GetKeyFocus() : nil, UseDefaultIID());
	if (focus == nil || focus->GetWidgetID() != BaseBoxID(k))
		return;
	panel->SetKeyboardFocus(BaseBoxID(last));
	InterfacePtr<IEditBoxAttributes> box(panel->FindWidget(BaseBoxID(last)), UseDefaultIID());
	if (box != nil)
		box->SelectRange(gCellBase[last].CharCount(), gCellBase[last].CharCount());
}

// ----- THE BASE BOXES SETTLE AT IDLE (the author's bug, 11-02: three kanji typed with the Japanese input method came out as the third, then the first). An input method hands a box what it confirms a character at a time, every one of them to that
// box, before the first is read - spread at the second, the third went into the first box at its start, where a write leaves the caret; and a box's notification comes while the person is still composing, before IMM says so
// (measured - docs/superpowers/specs/2026-11-02-kfc-kohaku-face-buttons-design.md I1). So a change is only kept where it is heard, and what a box was given is spread at the next idle, all of it at once - while the person is
// still composing, asked again a little later.
ICallbackTimer* gSettleTimer = nil;		// made on first use; given back by ShutdownCleanup (its callback is a raw pointer into this .pln)
uint32 gPendingBase = 0;				// bit k: base box k changed since it was last settled
const uint32 kSettleComposingMs = 100;	// while the person composes: asked again this much later (the callback's return re-arms it - measured on 21.0, memory icallbacktimer)
const uint32 kSettleArrivingMs = 30;	// while characters are still on their way: the same, sooner

PMString WordsOf(IControlView* view);	// (below, with the face's widgets)

// IS MORE OF WHAT IS BEING TYPED STILL ON ITS WAY - a key, a character, an input method's message queued for this thread? InDesign runs its idle tasks between them (measured: a burst of three posted characters was settled after
// each one), so the next idle is not yet the end of a burst. Looked at, not taken (PM_NOREMOVE).
bool InputStillArriving()
{
	MSG msg;
	return ::PeekMessageW(&msg, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE | PM_NOYIELD) != FALSE
		|| ::PeekMessageW(&msg, nullptr, WM_IME_STARTCOMPOSITION, WM_IME_KEYLAST, PM_NOREMOVE | PM_NOYIELD) != FALSE
		|| ::PeekMessageW(&msg, nullptr, WM_IME_CHAR, WM_IME_CHAR, PM_NOREMOVE | PM_NOYIELD) != FALSE;
}

uint32 SettleBaseBoxes(void* /*refPtr*/)
{
	if (gPendingBase == 0 || gShownFace == nil)
	{
		gPendingBase = 0;
		return IIdleTask::kEndOfTime;
	}
	if (InputStillArriving())
	{
		KFC_DIAG_LOG("RUBYTAB base arriving");
		return kSettleArrivingMs;
	}
	if (InputMethodComposing())
	{
		KFC_DIAG_LOG("RUBYTAB base composing");
		return kSettleComposingMs;
	}
	InterfacePtr<IPanelControlData> panel(gShownFace, UseDefaultIID());
	const uint32 pending = gPendingBase;
	gPendingBase = 0;
	for (int32 k = 0; k < kKFCRubyBoxColumns && panel != nil; ++k)
	{
		if ((pending & (1u << k)) == 0)
			continue;
		IControlView* const box = panel->FindWidget(BaseBoxID(k));
		if (box != nil)
			SpreadBaseBox(panel, k, WordsOf(box));
	}
	KeepFromBoxes();
	KFC_DIAG_LOG("RUBYTAB base=%s", gRuby.base.GetUTF8String().c_str());
	return IIdleTask::kEndOfTime;
}

// A BASE BOX HEARD: what it holds kept as it stands, and its column's words on the Change side - to be spread at the next idle (SettleBaseBoxes).
void NoteBaseBox(IPanelControlData* panel, int32 k, const PMString& typed)
{
	gCellBase[k] = typed;
	gCellBase[k].SetTranslatable(kFalse);
	SetField(panel, ChangeBaseID(k), gCellBase[k]);
	gPendingBase |= (1u << k);
	if (gSettleTimer == nil)
		gSettleTimer = ::CreateObject2<ICallbackTimer>(kCallbackTimerBoss, IID_ICALLBACKTIMER);
	if (gSettleTimer == nil)
	{
		(void)SettleBaseBoxes(nil);		// (no timer to be had: spread now, as before - composing, it stays as typed)
		return;
	}
	gSettleTimer->StopTimer();		// (never a booking over a live one - memory icallbacktimer)
	gSettleTimer->StartTimer(SettleBaseBoxes, 0, nil);
}

// (What each Look For: greys and takes away - LayOutRubyGrid - and what a list says of itself.)
void ShowRubyKind(IPanelControlData* panel)
{
	LayOutRubyGrid(panel);
	ShowRubyList(panel);
}

// The samples beside the Kenten part's lists draw what is kept now (KFCKohakuTab::KentenSampleKind).
void RedrawKentenSamples(IPanelControlData* panel)
{
	IControlView* const markSample = (panel != nil) ? panel->FindWidget(kKFCKentenMarkSampleWidgetID) : nil;
	if (markSample != nil)
		markSample->Invalidate();
	IControlView* const changeSample = (panel != nil) ? panel->FindWidget(kKFCKentenChangeSampleWidgetID) : nil;
	if (changeSample != nil)
		changeSample->Invalidate();
	KFC_DIAG_LOG("KENTAB samples mark=%d change=%d", static_cast<int>(KFCKohakuTab::KentenSampleKind(kKFCKentenMarkSampleWidgetID)), static_cast<int>(KFCKohakuTab::KentenSampleKind(kKFCKentenChangeSampleWidgetID)));
}

// Kenten: means nothing to Text Without Kenten: grey there.
void ShowKentenKind(IPanelControlData* panel)
{
	IControlView* const mark = (panel != nil) ? panel->FindWidget(kKFCKentenTabMarkWidgetID) : nil;
	if (mark != nil)
		mark->Enable(gKenten.kind == kKFCKentenFindWithout ? kFalse : kTrue);
	RedrawKentenSamples(panel);
}

// A text field's words as they stand now, not translated.
PMString WordsOf(IControlView* view)
{
	PMString words;
	InterfacePtr<ITextControlData> field(view, UseDefaultIID());
	if (field != nil)
		words = field->GetString();
	words.SetTranslatable(kFalse);
	return words;
}

// ----- the face

// IS THIS A JAPANESE INDESIGN - Ruby and Kenten are on Find: in it alone (the author's call, 10-31; the UI language, as How to Use asks it). TEST BUILDS - the fault switch not-japanese: as one in another language.
bool JapaneseKinds()
{
	const bool asAnother = KFC_DIAG_FAULT("not-japanese");
	return !asAnother && LocaleSetting::GetLocale().GetUserInterfaceId() == k_jaJP;
}

// FIND:'S ENTRIES: the .fr's two for every language, Ruby and Kenten after them in a Japanese InDesign - and the kind selected. Not heard (notifyOfChange kFalse).
void ShowKindEntries(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(kKFCKohakuTabKindWidgetID) : nil;
	InterfacePtr<IStringListControlData> strings(dropDown, IID_ISTRINGLISTCONTROLDATA);
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	if (strings == nil || list == nil)
		return;
	if (JapaneseKinds())
	{
		if (strings->Length() <= kKindRuby)
		{
			strings->AddString(PMString(kKFCKohakuTabRubyKey, PMString::kTranslateDuringCall), IStringListControlData::kEnd, kFalse, kFalse);
			strings->AddString(PMString(kKFCKohakuTabKentenKey, PMString::kTranslateDuringCall), IStringListControlData::kEnd, kFalse, kFalse);
		}
	}
	else
	{
		while (strings->Length() > kKindRuby)
			strings->RemoveString(strings->Length() - 1, kFalse, kFalse);
		if (gKind >= kKindRuby)
			gKind = kKindQRCode;
	}
	list->Select(gKind, kTrue, kFalse);
}

// A test build's trace names the kind (run.ps1's cases read them).
const char* KindWord()
{
	return (gKind == kKindTable) ? "TBLTAB" : (gKind == kKindRuby) ? "RUBYTAB" : (gKind == kKindKenten) ? "KENTAB" : "QRTAB";
}

// WHAT SEARCH: IS FOR THE KIND NOW: a QR code's frame stands in no story's text, so a part of a story picked for the others reads as Document on QR Code - and is there again when they come back
// (nothing is written here).
int32 ScopeForKind()
{
	if (gKind == kKindQRCode && gScope != IWalkerScopeFactoryUtils::kAllDocumentScope)
		return IWalkerScopeFactoryUtils::kDocumentScope;
	return gScope;
}

// SEARCH: FOR THE KIND: its entries - the .fr's two, and the three a story's text adds to them - and the value selected. Not heard (notifyOfChange kFalse).
void ShowScopeEntries(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	IControlView* const dropDown = (panel != nil) ? panel->FindWidget(kKFCKohakuTabScopeWidgetID) : nil;
	InterfacePtr<IStringListControlData> strings(dropDown, IID_ISTRINGLISTCONTROLDATA);
	InterfacePtr<IDropDownListController> list(dropDown, UseDefaultIID());
	if (strings == nil || list == nil)
		return;
	if (gKind == kKindQRCode)
	{
		while (strings->Length() > kEntriesForAnyKind)
			strings->RemoveString(strings->Length() - 1, kFalse, kFalse);
	}
	else if (strings->Length() < kEntriesInAStory)
	{
		strings->AddString(PMString(kKFCKohakuStoryEntryKey, PMString::kTranslateDuringCall), IStringListControlData::kEnd, kFalse, kFalse);
		strings->AddString(PMString(kKFCKohakuTabToEndKey, PMString::kTranslateDuringCall), IStringListControlData::kEnd, kFalse, kFalse);
		strings->AddString(PMString(kKFCKohakuSelectionKey, PMString::kTranslateDuringCall), IStringListControlData::kEnd, kFalse, kFalse);
	}
	const int32 scope = ScopeForKind();
	int32 entry = 1;	// (Document)
	for (int32 k = 0; k < kEntriesInAStory; ++k)
		if (kScopeOfEntry[k] == scope)
			entry = k;
	list->Select(entry, kTrue, kFalse);
}

// A part of the face shown or not: hidden is disabled too - a hidden widget stacked over another still takes a click (memory gotourl-and-stacked-widget-traps).
void ShowPart(IPanelControlData* panel, const WidgetID& widget, bool shown)
{
	IControlView* const view = (panel != nil) ? panel->FindWidget(widget) : nil;
	if (view == nil)
		return;
	view->Enable(shown ? kTrue : kFalse);
	view->ShowView(shown ? kTrue : kFalse);
}

// ----- THE PART'S OWN BUTTONS in the face's right column (the author, 11-02: "Choose, Save, How to, as the other tabs" - they stood left of the dialog frame's Done before): Choose List..., Save List..., How to Use... on Ruby,
// How to Use... on Kenten - each group hidden and disabled whole for another kind (an empty group left standing over the other would take its clicks), Choose List... grey but for Words in a Ruby List.
void ShowRubyButtons(IPanelControlData* panel)
{
	ShowPart(panel, kKFCRubyButtonsWidgetID, gKind == kKindRuby);
	ShowPart(panel, kKFCKentenButtonsWidgetID, gKind == kKindKenten);
	IControlView* const choose = (panel != nil) ? panel->FindWidget(kKFCRubyListChooseWidgetID) : nil;
	if (choose != nil)
		choose->Enable((gKind == kKindRuby && gRuby.kind == kKFCRubyFindList) ? kTrue : kFalse);
}

// THE BUTTONS OF THE RIGHT COLUMN BESIDE THE FOUR (heard while the face is shown; a press of one is no change of what a query holds): Find in KFC, and the parts' own.
const WidgetID kColumnButtons[] =
{
	kKFCKohakuFindInKFCWidgetID,
	kKFCRubyListChooseWidgetID,
	kKFCRubyListSaveWidgetID,
	kKFCKohakuHowToWidgetID,
	kKFCKentenHowToWidgetID,
};

bool IsColumnButton(const WidgetID& widget)
{
	for (size_t b = 0; b < sizeof(kColumnButtons) / sizeof(kColumnButtons[0]); ++b)
		if (widget == kColumnButtons[b])
			return true;
	return false;
}

// ----- The icons under Search: (the author, 11-02), each kind's own: nothing a kind asks has to do with what the other asks.

// The widget and which of KFCOwnSwitches it says.
struct SwitchIcon
{
	WidgetID			widget;
	bool KFCOwnSwitches::*	value;
};

const SwitchIcon kSwitchIcons[] =
{
	{ kKFCSwitchLockedLayersWidgetID, &KFCOwnSwitches::lockedLayers },
	{ kKFCSwitchLockedStoriesWidgetID, &KFCOwnSwitches::lockedStories },
	{ kKFCSwitchHiddenWidgetID, &KFCOwnSwitches::hiddenLayers },
	{ kKFCSwitchMasterWidgetID, &KFCOwnSwitches::masterPages },
	{ kKFCSwitchFootnotesWidgetID, &KFCOwnSwitches::footnotes },
	{ kKFCSwitchKanaWidgetID, &KFCOwnSwitches::kanaSensitive },
	{ kKFCSwitchWidthWidgetID, &KFCOwnSwitches::widthSensitive },
};
const int32 kSwitchIconCount = static_cast<int32>(sizeof(kSwitchIcons) / sizeof(kSwitchIcons[0]));

bool IsSwitchIcon(const WidgetID& widget)
{
	for (int32 k = 0; k < kSwitchIconCount; ++k)
		if (widget == kSwitchIcons[k].widget)
			return true;
	return false;
}

// The icons of the kind now: shown for Ruby and Kenten, each as that kind holds it. Not heard (notifyOfChange kFalse).
void ShowSwitches(IPanelControlData* panel)
{
	const bool shown = (gKind == kKindRuby || gKind == kKindKenten);
	const KFCOwnSwitches& kept = (gKind == kKindKenten) ? gKenten.switches : gRuby.switches;
	for (int32 k = 0; k < kSwitchIconCount; ++k)
	{
		ShowPart(panel, kSwitchIcons[k].widget, shown);
		if (shown)
			SetBox(panel, kSwitchIcons[k].widget, kept.*(kSwitchIcons[k].value));
	}
}

// THE CHOSEN KIND'S PART, the others away, its buttons, its lines, and Search: for it. Every kind has Find Next and Find in KFC; Table, Ruby and Kenten Change, Change All and Change/Find (QR Code writes nothing).
void ShowKind(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	const bool writes = (gKind != kKindQRCode);
	ShowPart(panel, kKFCQRTabFindNextWidgetID, true);
	ShowPart(panel, kKFCTableTabChangeWidgetID, writes);
	ShowPart(panel, kKFCTableTabChangeAllWidgetID, writes);
	ShowPart(panel, kKFCTableTabChangeFindWidgetID, writes);
	ShowPart(panel, kKFCKohakuFindInKFCWidgetID, true);
	ShowPart(panel, kKFCTableTabFindLabelWidgetID, gKind == kKindTable);
	ShowSwitches(panel);
	ShowPart(panel, kKFCKohakuTableSectionWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCKohakuRubySectionWidgetID, gKind == kKindRuby);
	ShowPart(panel, kKFCKohakuKentenSectionWidgetID, gKind == kKindKenten);
	ShowRubyKind(panel);		// (their own greys stand whatever their panels were told; Ruby's buttons with them)
	ShowRubyButtons(panel);
	ShowKentenKind(panel);
	ShowFaceButtons(panel);
	ShowSaid(panel);
	ShowScopeEntries(face);
}

// THE FACE WRITTEN FROM WHAT IS KEPT, its style lists aside (FillStyles). Its own widgets only - nothing is walked. Not heard (notifyOfChange kFalse).
void FillKept(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	ShowKindEntries(face);
	SetBox(panel, kKFCTableTabOverridesOnlyWidgetID, gTable.overridesOnly);
	SetBox(panel, kKFCTableTabClearOverridesWidgetID, gTable.clearOverrides);
	InterfacePtr<IDropDownListController> rubyKind(QueryList(face, kKFCRubyTabKindWidgetID));
	if (rubyKind != nil)
		rubyKind->Select(gRuby.kind, kTrue, kFalse);
	InterfacePtr<IDropDownListController> rubyFirst(QueryList(face, kKFCRubyListFirstWidgetID));
	if (rubyFirst != nil)
		rubyFirst->Select(gRuby.listFirst, kTrue, kFalse);
	WriteBoxes(panel);
	InterfacePtr<IDropDownListController> kentenKind(QueryList(face, kKFCKentenTabKindWidgetID));
	if (kentenKind != nil)
		kentenKind->Select(gKenten.kind, kTrue, kFalse);
	SetField(panel, kKFCKentenTabBaseWidgetID, gKenten.base);
	InterfacePtr<IDropDownListController> kentenMark(QueryList(face, kKFCKentenTabMarkWidgetID));
	if (kentenMark != nil)
		kentenMark->Select(gKenten.mark, kTrue, kFalse);
	InterfacePtr<IDropDownListController> kentenChange(QueryList(face, kKFCKentenTabChangeWidgetID));
	if (kentenChange != nil)
		kentenChange->Select(gKenten.changeTo, kTrue, kFalse);
	SetBox(panel, kKFCKentenTabRemoveWidgetID, gKenten.removeKenten);
	SetBox(panel, kKFCRubyListOtherRubyWidgetID, gRuby.listOtherRuby);
	ShowKind(face);
}

}	// anonymous namespace

bool KFCKohakuTab::IsQRCodeCurrent(int32& outScope)
{
	outScope = (ScopeAsked() == IWalkerScopeFactoryUtils::kAllDocumentScope) ? static_cast<int32>(IWalkerScopeFactoryUtils::kAllDocumentScope)
		: static_cast<int32>(IWalkerScopeFactoryUtils::kDocumentScope);
	return FaceIsCurrent() && gKind == kKindQRCode;
}

bool KFCKohakuTab::IsTableCurrent(KFCTableConditions& outConditions)
{
	outConditions = gTable;
	outConditions.scope = ScopeAsked();
	return FaceIsCurrent() && gKind == kKindTable;
}

bool KFCKohakuTab::IsRubyCurrent(KFCRubyConditions& outConditions)
{
	outConditions = gRuby;
	outConditions.scope = ScopeAsked();
	return FaceIsCurrent() && gKind == kKindRuby;
}

int32 KFCKohakuTab::KentenSampleKind(const WidgetID& sample)
{
	if (sample == kKFCKentenMarkSampleWidgetID)
		return (gKenten.kind == kKFCKentenFindWithout) ? 0 : gKenten.mark;
	if (sample == kKFCKentenChangeSampleWidgetID)
		return gKenten.removeKenten ? 0 : gKenten.changeTo;
	return 0;
}

bool KFCKohakuTab::IsKentenCurrent(KFCKentenConditions& outConditions)
{
	outConditions = gKenten;
	outConditions.scope = ScopeAsked();
	return FaceIsCurrent() && gKind == kKindKenten;
}

void KFCKohakuTab::YieldToSavedQuery()
{
	gYield = true;
	KFC_DIAG_LOG("%s yield (a saved query states its own tab)", KindWord());
}

bool KFCKohakuTab::OffersQuery(const KFCKohakuQueryData& query)
{
	return (query.mode != kKFCRubySearchMode && query.mode != kKFCKentenSearchMode) || JapaneseKinds();
}

void KFCKohakuTab::StateQuery(const KFCKohakuQueryData& query, bool forItsFind, int32 scope)
{
	if (query.mode == kKFCTableSearchMode)
	{
		gKind = kKindTable;
		gTable = query.table;
	}
	else if (query.mode == kKFCRubySearchMode)
	{
		gKind = kKindRuby;
		gRuby = query.ruby;
		BoxesFromKept();
		KFC_DIAG_LOG("RUBYTAB stated base=%s reading=%s", gRuby.base.GetUTF8String().c_str(), gRuby.reading.GetUTF8String().c_str());
	}
	else if (query.mode == kKFCKentenSearchMode)
	{
		gKind = kKindKenten;
		gKenten = query.kenten;
	}
	else
		gKind = kKindQRCode;
	gYield = false;
	ForgetFound();		// (Find Next starts from the first again)
	if (forItsFind)
	{
		gStated = true;
		gStatedScope = scope;
	}
	else if (!gShown)
		gClosedOnIt = true;		// (no dialog stands, or it is about to show the face, which ends this: KFC's Find takes what the query asks)
	if (gShownFace != nil)
	{
		FillStyles(gShownFace);
		FillKept(gShownFace);
	}
	KFC_DIAG_LOG("%s stated a saved query find=%d", KindWord(), forItsFind ? 1 : 0);
}

void KFCKohakuTab::EndStatedFind()
{
	gStated = false;
}

void KFCKohakuTab::QueryOfFace(KFCKohakuQueryData& out)
{
	out = KFCKohakuQueryData();
	out.mode = (gKind == kKindTable) ? static_cast<int32>(kKFCTableSearchMode) : (gKind == kKindRuby) ? static_cast<int32>(kKFCRubySearchMode)
		: (gKind == kKindKenten) ? static_cast<int32>(kKFCKentenSearchMode) : static_cast<int32>(kKFCQRCodeSearchMode);
	out.table = gTable;
	out.ruby = gRuby;
	out.kenten = gKenten;
}

void KFCKohakuTab::ShutdownCleanup()
{
	// THE BASE BOXES' TIMER, its booking stopped first: its callback is a raw pointer into this .pln (ICallbackTimer.h).
	if (gSettleTimer != nil)
	{
		gSettleTimer->StopTimer();
		gSettleTimer->Release();
		gSettleTimer = nil;
	}
	gPendingBase = 0;
	// Fresh values, so the storage goes too.
	gTable = KFCTableConditions();
	gRuby = KFCRubyConditions();
	gKenten = KFCKentenConditions();
	gFindPaths = std::vector<PMString>();
	gChangePaths = std::vector<PMString>();
	gFoundTable = KFCTableFound();
	for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
	{
		gCellFindRuby[k] = PMString();
		gCellBase[k] = PMString();
		gCellChange[k] = PMString();
	}
	for (int32 k = 0; k < kKindCount; ++k)
	{
		gSaid[k][0] = PMString();
		gSaid[k][1] = PMString();
	}
}

//========================================================================================
// The face's three implementations - the shape of InDesign's own faces (kFindMiniTabbedDialogTextPanelBoss and its siblings in the 20.5 dump; KT measured the same three).
//========================================================================================

/** Names the resource that describes the face: the IDList / IDListPair id, not the view's (KFCUI.fr). */
class KFCKohakuTabPanelCreator : public CPanelCreator
{
public:
	KFCKohakuTabPanelCreator(IPMUnknown* boss) : CPanelCreator(boss) {}
	virtual ~KFCKohakuTabPanelCreator() {}
	virtual RsrcID GetPanelRsrcID() const
	{
		return kKFCKohakuTabIDListRsrcID;
	}
};
CREATE_PMINTERFACE(KFCKohakuTabPanelCreator, kKFCKohakuTabPanelCreatorImpl)

/** The dialog's calls on the face: what is kept put in when it is first shown, nothing to check or apply (the base's own - KFC's Find reads what the face keeps), and - as the dialog closes -
    whether it closes on this face. */
class KFCKohakuTabController : public CDialogController
{
public:
	KFCKohakuTabController(IPMUnknown* boss) : CDialogController(boss) {}
	virtual ~KFCKohakuTabController() {}

	virtual void InitializeDialogFields(IActiveContext* context)
	{
		CDialogController::InitializeDialogFields(context);
		FillStyles(this);
		FillKept(this);
	}

	virtual void ClosingDialog(IActiveContext* context)
	{
		// ON EVERY CLOSE, on whatever face (measured): does it close on this one - shown now, or so its switcher says? The dialog opens on it again, so KFC's Find goes on looking for what its Find:
		// says. Nothing is walked here, and nothing of KFC's that walks is run from here (the header's hazard) - the panel's title already names it.
		gClosedOnIt = gShown || SwitcherShowsFace(this);
		ForgetFound();		// Find Next starts again at the next open (gLastFound's note)
		KFC_DIAG_LOG("%s closing on-it=%d", KindWord(), gClosedOnIt ? 1 : 0);
		CDialogController::ClosingDialog(context);
	}
};
CREATE_PMINTERFACE(KFCKohakuTabController, kKFCKohakuTabControllerImpl)

/** The face's widgets heard while it is shown (AbstractDialogObserver: a selectable dialog face's base, by its header), and the front document: its workspace's table styles fill the Table part's lists
    (the shape of Adobe's ConditionSetDropDownObserver - the active context, regular; the workspace's list, lazy). */
class KFCKohakuTabObserver : public AbstractDialogObserver
{
public:
	KFCKohakuTabObserver(IPMUnknown* boss) : AbstractDialogObserver(boss) {}
	virtual ~KFCKohakuTabObserver() {}

	virtual void AutoAttach()
	{
		// The base first, as the SDK's face on a dialog of InDesign's does (xdocbookworkflowui's XDocBkUIDialogObserver) - CObserver's, which does nothing (publiclib's CObserver.cpp).
		AbstractDialogObserver::AutoAttach();
		// THE FACE IS SHOWN: the Kohaku tab is Find/Change's now, whatever was kept - a close on it, or a saved query's own tab (YieldToSavedQuery) - until it is hidden.
		TakeTestReset();
		gShown = true;
		gShownFace = this;
		gClosedOnIt = false;
		gYield = false;
		ForgetFound();		// Find Next starts from the first again (gLastFound's note)
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel != nil)
		{
			AttachToWidget(kKFCKohakuTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCKohakuTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCQRTabFindNextWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCRubyListFirstWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			for (int32 k = 0; k < kSwitchIconCount; ++k)
				AttachToWidget(kSwitchIcons[k].widget, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeAllWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeFindWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			for (size_t b = 0; b < sizeof(kColumnButtons) / sizeof(kColumnButtons[0]); ++b)
				AttachToWidget(kColumnButtons[b], IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCRubyListOtherRubyWidgetID, IID_ITRISTATECONTROLDATA, panel);
			for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
			{
				AttachToWidget(ReadingBoxID(k), IID_ITEXTCONTROLDATA, panel);
				AttachToWidget(BaseBoxID(k), IID_ITEXTCONTROLDATA, panel);
				AttachToWidget(ChangeBoxID(k), IID_ITEXTCONTROLDATA, panel);
				if (k + 1 < kKFCRubyBoxColumns)
					AttachToWidget(JoinID(k), IID_ITRISTATECONTROLDATA, panel);
			}
			AttachToWidget(kKFCTableTabFindStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabOverridesOnlyWidgetID, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCTableTabClearOverridesWidgetID, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCKentenTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCKentenTabBaseWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCKentenTabMarkWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCKentenTabChangeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCKentenTabRemoveWidgetID, IID_ITRISTATECONTROLDATA, panel);
		}
		// The front document, followed while shown (Update): its workspace's styles fill the lists, now and as they change.
		ISession* const session = GetExecutionContextSession();
		IActiveContext* const context = (session != nil) ? session->GetActiveContext() : nil;
		InterfacePtr<ISubject> contextSubject(context, UseDefaultIID());
		if (contextSubject != nil && !contextSubject->IsAttached(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER))
			contextSubject->AttachObserver(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER);
		fWorkspace = UIDRef::gNull;
		FollowWorkspace(context);
		FillKept(this);
		// THE SELECTION, FOLLOWED WHILE SHOWN: Change greys as it leaves what Find Next found.
		InterfacePtr<IObserver> selectionObserver(this, IID_IKFCKOHAKUSELECTIONOBSERVER);
		if (selectionObserver != nil)
			selectionObserver->AutoAttach();
		KFC_DIAG_LOG("%s shown scope=%d", KindWord(), static_cast<int>(ScopeForKind()));
		KFCKohakuQueryMenu::FaceShown();
		// THE PANEL'S TITLE NAMES THE KIND NOW, AND THE APPLICATION BAR'S FIELD SHOWS ITS NOTHING (QR-22): no notification of Find/Change's settings comes with a switch to it (measured), so both are
		// written from here.
		KFCPanelTitle::Update();
		KFCMirrorAppBarFieldNow();
	}

	virtual void AutoDetach()
	{
		// HIDDEN - by another face, or by the dialog's close (after its ClosingDialog). Only let go of what is attached: nothing is walked here and nothing of KFC's is run from here (the header's
		// hazard). A switch to InDesign's own face writes the panel's title through Find/Change's own notifications.
		AbstractDialogObserver::AutoDetach();		// (the base first, as AutoAttach's)
		InterfacePtr<IObserver> selectionObserver(this, IID_IKFCKOHAKUSELECTIONOBSERVER);
		if (selectionObserver != nil)
			selectionObserver->AutoDetach();
		gShown = false;
		gShownFace = nil;
		KFC_DIAG_LOG("%s hidden", KindWord());
		KFCKohakuQueryMenu::FaceHidden();
		StopHearingWorkspace();
		// (The dialog can go with the application: no session, no context - nothing is attached to a context that has gone.)
		ISession* const session = GetExecutionContextSession();
		IActiveContext* const context = (session != nil) ? session->GetActiveContext() : nil;
		InterfacePtr<ISubject> contextSubject(context, UseDefaultIID());
		if (contextSubject != nil && contextSubject->IsAttached(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER))
			contextSubject->DetachObserver(ISubject::kRegularAttachment, this, IID_IACTIVECONTEXT, IID_IOBSERVER);
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel == nil)
			return;
		DetachFromWidget(kKFCKohakuTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCKohakuTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCQRTabFindNextWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyListFirstWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		for (int32 k = 0; k < kSwitchIconCount; ++k)
			DetachFromWidget(kSwitchIcons[k].widget, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeAllWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeFindWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		for (size_t b = 0; b < sizeof(kColumnButtons) / sizeof(kColumnButtons[0]); ++b)
			DetachFromWidget(kColumnButtons[b], IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyListOtherRubyWidgetID, IID_ITRISTATECONTROLDATA, panel);
		for (int32 k = 0; k < kKFCRubyBoxColumns; ++k)
		{
			DetachFromWidget(ReadingBoxID(k), IID_ITEXTCONTROLDATA, panel);
			DetachFromWidget(BaseBoxID(k), IID_ITEXTCONTROLDATA, panel);
			DetachFromWidget(ChangeBoxID(k), IID_ITEXTCONTROLDATA, panel);
			if (k + 1 < kKFCRubyBoxColumns)
				DetachFromWidget(JoinID(k), IID_ITRISTATECONTROLDATA, panel);
		}
		DetachFromWidget(kKFCTableTabFindStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabOverridesOnlyWidgetID, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabClearOverridesWidgetID, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCKentenTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCKentenTabBaseWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCKentenTabMarkWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCKentenTabChangeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCKentenTabRemoveWidgetID, IID_ITRISTATECONTROLDATA, panel);
	}

	virtual void Update(const ClassID& theChange, ISubject* theSubject, const PMIID& protocol, void* /*changedBy*/)
	{
		// THE FRONT DOCUMENT MAY BE ANOTHER: its styles on the lists.
		if (protocol == IID_IACTIVECONTEXT)
		{
			InterfacePtr<IActiveContext> context(theSubject, UseDefaultIID());
			FollowWorkspace(context);
			return;
		}
		InterfacePtr<IControlView> view(theSubject, UseDefaultIID());
		const WidgetID widget = (view != nil) ? view->GetWidgetID() : kInvalidWidgetID;
		if (widget == kKFCKohakuTabKindWidgetID)
		{
			// FIND: CHANGED - another part, and KFC's Find looks for it, whatever a saved query had stated: said in the panel's title and the Application Bar's field, as a switch of tab is.
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKindQRCode && entry <= kKindKenten)
				gKind = entry;
			gYield = false;
			ForgetFound();		// (Find Next starts from the first again)
			ShowKind(this);
			KFC_DIAG_LOG("%s shown scope=%d", KindWord(), static_cast<int>(ScopeForKind()));
			KFCPanelTitle::Update();
			KFCMirrorAppBarFieldNow();
		}
		else if (widget == kKFCKohakuTabScopeWidgetID)
		{
			// SEARCH: CHANGED - kept for KFC's Find and its menu's name, and said in the panel's title.
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= 0 && entry < kEntriesInAStory)
				gScope = kScopeOfEntry[entry];
			KFC_DIAG_LOG("%s scope=%d", KindWord(), static_cast<int>(gScope));
			KFCPanelTitle::Update();
		}
		else if (widget == kKFCQRTabFindNextWidgetID && theChange == kTrueStateMessage)
		{
			if (gKind == kKindQRCode)
				QRFindNext();
			else
				FaceFindNext();
		}
		else if (widget == kKFCKohakuFindInKFCWidgetID && theChange == kTrueStateMessage)
			FindInKFC();
		else if ((widget == kKFCKohakuHowToWidgetID || widget == kKFCKentenHowToWidgetID) && theChange == kTrueStateMessage)
		{
			KFC_DIAG_LOG("%s how to", KindWord());
			if (widget == kKFCKentenHowToWidgetID)
				KFCHowTo::ShowKenten();
			else
				KFCHowTo::ShowRubyMarks();
		}
		else if (widget == kKFCRubyListSaveWidgetID && theChange == kTrueStateMessage)
			SaveRubyList();
		else if (widget == kKFCRubyListChooseWidgetID && theChange == kTrueStateMessage)
		{
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			if (ChooseRubyList(panel))
				KFCKohakuQueryMenu::FaceChanged();		// (the list is what a query holds; its press alone is nothing - below)
		}
		else if (widget == kKFCTableTabChangeWidgetID && theChange == kTrueStateMessage)
			FaceChange(false);
		else if (widget == kKFCTableTabChangeFindWidgetID && theChange == kTrueStateMessage)
			FaceChange(true);
		else if (widget == kKFCTableTabChangeAllWidgetID && theChange == kTrueStateMessage)
			FaceChangeAll();
		else if (widget == kKFCTableTabFindStyleWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			KeepStyleOf(list, gFindPaths, gTable.findStyle);
			KFC_DIAG_LOG("TBLTAB find=%s", gTable.findStyle.GetUTF8String().c_str());
		}
		else if (widget == kKFCTableTabChangeStyleWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			KeepStyleOf(list, gChangePaths, gTable.changeStyle);
			KFC_DIAG_LOG("TBLTAB change=%s", gTable.changeStyle.GetUTF8String().c_str());
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowFaceButtons(panel);
		}
		else if (widget == kKFCTableTabOverridesOnlyWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			gTable.overridesOnly = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("TBLTAB overrides=%d", gTable.overridesOnly ? 1 : 0);
		}
		else if (widget == kKFCTableTabClearOverridesWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			gTable.clearOverrides = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("TBLTAB clear=%d", gTable.clearOverrides ? 1 : 0);
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowFaceButtons(panel);
		}
		else if (widget == kKFCRubyTabKindWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKFCRubyFindWith && entry <= kKFCRubyFindVaries)
				gRuby.kind = entry;
			if (gRuby.kind == kKFCRubyFindList || gRuby.kind == kKFCRubyFindMarks)
				LetGoOfAnyBox();
			ForgetFound();		// (what Find Next found was another Look For's: it starts from the first again)
			// (What the face's lines said was another Look For's too - a list's count under Inconsistent Ruby, the face's picture of 11-02: gone. A list says its count again as it
			// is shown - ShowRubyKind's ShowRubyList.)
			SayFor(kKindRuby, PMString(), PMString());
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowRubyKind(panel);
			ShowFaceButtons(panel);
			KFC_DIAG_LOG("RUBYTAB kind=%d", static_cast<int>(gRuby.kind));
		}
		else if (widget == kKFCRubyListOtherRubyWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			// INCLUDE TEXT WITH OTHER RUBY (the author's proposal 2): a list's word with ruby that reads otherwise is a row too.
			gRuby.listOtherRuby = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("RUBYTAB other=%d", gRuby.listOtherRuby ? 1 : 0);
		}
		else if (widget == kKFCRubyListFirstWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKFCRubyListAll && entry <= kKFCRubyListFirstInBook)
				gRuby.listFirst = entry;
			KFC_DIAG_LOG("RUBYTAB first=%d", static_cast<int>(gRuby.listFirst));
		}
		else if (protocol == IID_ITRISTATECONTROLDATA && (theChange == kTrueStateMessage || theChange == kFalseStateMessage) && IsSwitchIcon(widget))
		{
			// AN ICON, THE KIND'S OWN: InDesign's toggling icon button (the Text tab's own - kADBEIconToggleButtonWidgetBoss, in its class list): a press leaves it as it says.
			KFCOwnSwitches& kept = (gKind == kKindKenten) ? gKenten.switches : gRuby.switches;
			for (int32 k = 0; k < kSwitchIconCount; ++k)
				if (widget == kSwitchIcons[k].widget)
					kept.*(kSwitchIcons[k].value) = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("%s switches=%d", KindWord(), static_cast<int>(kept.Bits()));
		}
		else if (ColumnOf(widget, ReadingBoxID(0), kKFCRubyBoxColumns) >= 0)
		{
			// A BOX OF FIND'S RUBY: - the search reads them all run together (KeepFromBoxes).
			const int32 k = ColumnOf(widget, ReadingBoxID(0), kKFCRubyBoxColumns);
			gCellFindRuby[k] = WordsOf(view);
			KeepFromBoxes();
			KFC_DIAG_LOG("RUBYTAB reading=%s", gRuby.reading.GetUTF8String().c_str());
		}
		else if (ColumnOf(widget, BaseBoxID(0), kKFCRubyBoxColumns) >= 0)
		{
			// A BOX OF FIND'S BASE TEXT: - and the Change side's words under its column, which are this box's. Spread over the boxes at the next idle (SettleBaseBoxes).
			const int32 k = ColumnOf(widget, BaseBoxID(0), kKFCRubyBoxColumns);
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			NoteBaseBox(panel, k, WordsOf(view));
			KeepFromBoxes();
			KFC_DIAG_LOG("RUBYTAB base heard %d=%s", static_cast<int>(k), gCellBase[k].GetUTF8String().c_str());
		}
		else if (ColumnOf(widget, ChangeBoxID(0), kKFCRubyBoxColumns) >= 0)
		{
			const int32 k = ColumnOf(widget, ChangeBoxID(0), kKFCRubyBoxColumns);
			gCellChange[k] = WordsOf(view);
			KeepFromBoxes();
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowFaceButtons(panel);		// (Text Without Ruby's Change All asks for a reading)
			KFC_DIAG_LOG("RUBYTAB box r%d=%s", static_cast<int>(k), gCellChange[k].GetUTF8String().c_str());
		}
		else if (ColumnOf(widget, JoinID(0), kKFCRubyBoxColumns - 1) >= 0 && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			// A CHAIN PRESSED (F16). Joined: the right column's reading goes to the end of the left's - the box they now share - in Find and Change alike. Let go: the left's box keeps what it has, the right's stands empty.
			const int32 k = ColumnOf(widget, JoinID(0), kKFCRubyBoxColumns - 1);
			gJoin[k] = (theChange == kTrueStateMessage);
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			if (gJoin[k])
			{
				int32 first = k;
				while (first > 0 && gJoin[first - 1])
					--first;
				gCellFindRuby[first].Append(gCellFindRuby[k + 1]);
				gCellFindRuby[k + 1].Clear();
				SetField(panel, ReadingBoxID(first), gCellFindRuby[first]);
				SetField(panel, ReadingBoxID(k + 1), gCellFindRuby[k + 1]);
				gCellChange[first].Append(gCellChange[k + 1]);
				gCellChange[k + 1].Clear();
				SetField(panel, ChangeBoxID(first), gCellChange[first]);
				SetField(panel, ChangeBoxID(k + 1), gCellChange[k + 1]);
			}
			KeepFromBoxes();
			const WidgetID focusTo(LetGoOfGoingBox());
			LayOutRubyGrid(panel);
			if (focusTo != kInvalidWidgetID && panel != nil)
				panel->SetKeyboardFocus(focusTo);
			KFC_DIAG_LOG("RUBYTAB join %d=%d", static_cast<int>(k), gJoin[k] ? 1 : 0);
		}
		else if (widget == kKFCKentenTabKindWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKFCKentenFindWith && entry <= kKFCKentenFindWithout)
				gKenten.kind = entry;
			ForgetFound();		// (what Find Next found was another Look For's)
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowKentenKind(panel);
			ShowFaceButtons(panel);
			KFC_DIAG_LOG("KENTAB kind=%d", static_cast<int>(gKenten.kind));
		}
		else if (widget == kKFCKentenTabBaseWidgetID)
		{
			gKenten.base = WordsOf(view);
			KFC_DIAG_LOG("KENTAB base=%s", gKenten.base.GetUTF8String().c_str());
		}
		else if (widget == kKFCKentenTabMarkWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= 0)
				gKenten.mark = entry;		// (an entry is its IKentenStyle::KentenKind, 0 = any)
			KFC_DIAG_LOG("KENTAB mark=%d", static_cast<int>(gKenten.mark));
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			RedrawKentenSamples(panel);
		}
		else if (widget == kKFCKentenTabChangeWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= 0)
				gKenten.changeTo = entry;		// (an entry is its IKentenStyle::KentenKind, 0 = leave it)
			KFC_DIAG_LOG("KENTAB change=%d", static_cast<int>(gKenten.changeTo));
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			RedrawKentenSamples(panel);
			ShowFaceButtons(panel);
		}
		else if (widget == kKFCKentenTabRemoveWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			gKenten.removeKenten = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("KENTAB remove=%d", gKenten.removeKenten ? 1 : 0);
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			RedrawKentenSamples(panel);
			ShowFaceButtons(panel);
		}
		// WHAT A QUERY HOLDS WAS CHANGED BY HAND: Find/Change's Query menu says [Custom], as a change on InDesign's own tabs does. (Search: is no part of a query, as InDesign's own Search: is none of its menu's; no button
		// changes it - Find Next, Find in KFC and How to Use... change nothing, what Change and its kind write is the document, Save List... writes a file; Choose List... changes what a query holds, and says so itself.)
		if (widget != kKFCKohakuTabScopeWidgetID && widget != kKFCQRTabFindNextWidgetID && widget != kKFCTableTabChangeWidgetID && widget != kKFCTableTabChangeAllWidgetID
			&& widget != kKFCTableTabChangeFindWidgetID && !IsColumnButton(widget)
			&& (protocol != IID_ITRISTATECONTROLDATA || theChange == kTrueStateMessage || theChange == kFalseStateMessage))
			KFCKohakuQueryMenu::FaceChanged();
	}

	/** THE FRONT DOCUMENT'S TABLE STYLES CHANGED (the workspace's list - a style made, renamed, deleted, or an Undo of one). */
	virtual void LazyUpdate(ISubject* /*theSubject*/, const PMIID& protocol, const LazyNotificationData* /*data*/)
	{
		if (protocol == IID_ITABLESTYLEGROUPMANAGER)
			FillStyles(this);
	}

private:
	// The workspace the lists are from, and heard for its table styles (IDHAObserver's protocol, in the SDK's hostadapter sample).
	void FollowWorkspace(IActiveContext* context)
	{
		IWorkspace* const workspace = (context != nil) ? context->GetContextWorkspace() : nil;
		const UIDRef now = (workspace != nil) ? ::GetUIDRef(workspace) : UIDRef::gNull;
		if (now == fWorkspace)
			return;
		StopHearingWorkspace();
		fWorkspace = now;
		InterfacePtr<ISubject> subject(workspace, UseDefaultIID());
		if (subject != nil && !subject->IsAttached(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER))
			subject->AttachObserver(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER);
		FillStyles(this);
	}

	void StopHearingWorkspace()
	{
		if (fWorkspace == UIDRef::gNull)
			return;
		InterfacePtr<ISubject> subject(fWorkspace, UseDefaultIID());
		if (subject != nil && subject->IsAttached(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER))
			subject->DetachObserver(ISubject::kLazyAttachment, this, IID_ITABLESTYLEGROUPMANAGER, IID_IOBSERVER);
		fWorkspace = UIDRef::gNull;
	}

	UIDRef fWorkspace;
};
CREATE_PMINTERFACE(KFCKohakuTabObserver, kKFCKohakuTabObserverImpl)

/** The selection, while the face is shown (KFCQueryScopeObserver's shape: an ActiveSelectionObserver under an IID of its own, beside the face's observer of its widgets - the face's AutoAttach and AutoDetach drive it). It only greys: the face's Change and
    Change/Find - Table's, Ruby's and Kenten's - once the selection leaves what Find Next found (F6, B4). */
class KFCKohakuTabSelectionObserver : public ActiveSelectionObserver
{
public:
	KFCKohakuTabSelectionObserver(IPMUnknown* boss) : ActiveSelectionObserver(boss, IID_IKFCKOHAKUSELECTIONOBSERVER) {}
	virtual ~KFCKohakuTabSelectionObserver() {}

protected:
	virtual void HandleSelectionChanged(const ISelectionMessage* /*message*/)
	{
		Show();
	}

	// Every caret step: a caret moved off the table, or a person's text selection.
	virtual void HandleFrequentSelectionChanged()
	{
		Show();
	}

private:
	void Show()
	{
		InterfacePtr<IPanelControlData> panel(gShownFace, UseDefaultIID());
		ShowFaceButtons(panel);
	}
};
CREATE_PMINTERFACE(KFCKohakuTabSelectionObserver, kKFCKohakuTabSelectionObserverImpl)

// End, KFCKohakuTab.cpp.
