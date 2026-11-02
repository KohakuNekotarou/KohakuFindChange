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
#include "IControlView.h"
#include "IDropDownListController.h"	// the face's lists' entries
#include "IEventHandler.h"				// ...the focus's handler
#include "IKeyBoard.h"
#include "IObserver.h"					// the selection observer, attached with the face
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
#include "CreateObject.h"				// ::CreateObject2 - How to Use... built into the row of the frame's Done
#include "FileUtils.h"					// a ruby list's name
#include "FindChangeID.h"				// kFindMinITabbedDialogOKButtonWidgetID - the frame's Done
#include "CoreResTypes.h"				// kViewRsrcType
#include "PMRect.h"						// the Ruby part's boxes' frames
#include "RsrcSpec.h"					// How to Use...'s resource
#include "SDKFileHelper.h"				// SDKFileOpenChooser - Choose List...
#include "SelectionObserver.h"			// ActiveSelectionObserver - the Table part's Change greys when the selection leaves what Find Next found
#include "TablesID.h"					// IID_ITABLESTYLEGROUPMANAGER
#include "UIDList.h"

#include <string>		// (test builds: the fault switch ruby-list-file's line)
#include <vector>

// Project includes:
#include "KFCKohakuTab.h"
#include "KFCUIID.h"
#include "KFCDiag.h"					// KFC_DIAG_LOG - the face's calls, in a test build's trace (QRTAB / TBLTAB / RUBYTAB lines)
#include "KFCHowTo.h"					// ShowRubyMarks, ShowKenten - the tab's How to Use...
#include "KFCPanelTitle.h"				// Update - the panel's title names the kind and its Search:
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow - the Application Bar's field shows the tab's nothing
#include "KFCJump.h"					// LandOnQRCode, LandOnTable - Find Next lands as a row's click does
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
IControlView* gHowTo = nil;		// How to Use... in the row of the frame's Done, while the face is shown (the face's observer holds the reference: QueryHowTo's note)
int32 gKind = kKindQRCode;	// its Find: - QR Code at launch, the tab InDesign's dialog first had
int32 gScope = IWalkerScopeFactoryUtils::kDocumentScope;	// its Search: - Document at launch
KFCTableConditions gTable;	// what the Table part asks, kept from its widgets (its scope is gScope)
KFCRubyConditions gRuby;	// what the Ruby part asks
KFCKentenConditions gKenten;	// what the Kenten part asks

// THE RUBY PART'S BOXES (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md F14 - F16): what each holds, as gRuby holds it (KeepFromBoxes). The widgets' own text is the same - the face can be hidden and written (a saved query) with
// none to read. Find's Ruby: and Base Text: a column each, the Change side's reading over the same columns; its base text is Find's.
PMString gCellFindRuby[kKFCRubyBoxColumnsMost];
PMString gCellBase[kKFCRubyBoxColumnsMost];
PMString gCellChange[kKFCRubyBoxColumnsMost];
bool gJoin[kKFCRubyBoxColumnsMost];				// gJoin[k]: column k and the next are one reading - k's boxes, over both base boxes - in Find and Change alike
int32 gCols = kKFCRubyBoxColumnsAtFirst;		// how many stand

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

// ----- the QR Code part

// One of the part's lines, written.
void SetFaceLine(IPanelControlData* panel, const WidgetID& widget, const PMString& words)
{
	IControlView* const line = (panel != nil) ? panel->FindWidget(widget) : nil;
	InterfacePtr<ITextControlData> text(line, UseDefaultIID());
	if (text != nil)
		text->SetString(words);
}

// The part's two lines - what Find Next found (or that there is none, or why it did not look), and why it could not select it (empty when it did): a line each, so a long content cuts its own
// line and never the reason (KFCUI.fr).
void SayOnFace(IPMUnknown* face, const PMString& found, const PMString& note)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	SetFaceLine(panel, kKFCQRTabStatusWidgetID, found);
	SetFaceLine(panel, kKFCQRTabNoteWidgetID, note);
}

// FIND NEXT (the QR design's Q10 / D5): the QR code after the one found last, in the list's own order over the face's Search: (the model's KFCQRSearch::Next - read only), landed on as an object
// row's click lands (KFCJump::LandOnQRCode) - one it cannot select is named on the second line, with no alert (Q10); after the last, the first again, silently.
void FindNext(IPMUnknown* face)
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
		SayOnFace(face, words, note);
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
	SayOnFace(face, words, note);
}

// ----- the Table part's buttons (docs/superpowers/specs/2026-11-01-kfc-kohaku-trial-fixes-design.md F1 - F9)

// IS THE TEXT SELECTION AS THE LAST LANDING LEFT IT? (A place, read now: a flag raised by the selection's own messages would have to tell the landing's from the person's.)
bool SelectionAsLeft()
{
	UIDRef story;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	(void)KFCJump::ReadTextSelection(story, start, end);
	return story == gLeftStory && start == gLeftStart && end == gLeftEnd;
}

// Change and Change/Find are for the table Find Next found, while it is selected and has not been written; all of them want something on the Change side. Grey otherwise, as the Text tab's after the selection moves or a Change.
void ShowTableButtons(IPanelControlData* panel)
{
	if (panel == nil || gKind != kKindTable)
		return;
	const bool asks = !gTable.changeStyle.IsEmpty() || gTable.clearOverrides;
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

// The part's two lines (the QR Code part's way).
void SayOnTablePart(IPMUnknown* face, const PMString& found, const PMString& note)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	SetFaceLine(panel, kKFCTableTabStatusWidgetID, found);
	SetFaceLine(panel, kKFCTableTabNoteWidgetID, note);
}

// Where the model goes on from.
KFCTableLook TableLookNow()
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

void InformationAlertOf(const char* words, int32 count)
{
	PMString said(words);
	said.SetTranslatable(kFalse);
	if (count >= 0)
	{
		said.AppendNumber(count);
		said.Append(" table(s) changed.");
	}
	CAlert::InformationAlert(said);
}

// FIND NEXT ON TABLE: the next table of Search: (KFCTableSearch::Next says from where), landed on as a table row's click lands. One it cannot select is named on the second line; none at all is InDesign's own alert.
void TableFindNext(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	PMString words, note;
	words.SetTranslatable(kFalse);
	note.SetTranslatable(kFalse);
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayOnTablePart(face, words, note);
		return;
	}
	KFCTableFound found;
	const KFCTableLook look(TableLookNow());
	if (!KFCRuns()->TableNext(look, found))
	{
		gFoundTable = KFCTableFound();
		gFoundSelected = false;
		gFoundWritten = false;
		SayOnTablePart(face, found.words, note);
		KFC_DIAG_LOG("TBLTAB findnext none %s", found.words.GetUTF8String().c_str());
		ShowTableButtons(panel);
		if (found.words.IsEmpty())
			InformationAlertOf("Cannot find a match.", -1);
		return;
	}
	bool selected = false;
	KFCJump::LandOnTable(found.story, found.at, found.end, found.reach, note, selected);
	gFoundTable = found;
	gFoundSelected = selected;
	gFoundWritten = false;
	(void)KFCJump::ReadTextSelection(gLeftStory, gLeftStart, gLeftEnd);
	// (Why it could not be selected first: that is what stops the person now. Over a Search: searched as Document.)
	if (note.IsEmpty())
		note = found.note;
	KFC_DIAG_LOG("TBLTAB findnext %s%s%s selected=%d", found.words.GetUTF8String().c_str(), note.IsEmpty() ? "" : "  ", note.GetUTF8String().c_str(), selected ? 1 : 0);
	SayOnTablePart(face, found.words, note);
	ShowTableButtons(panel);
}

// CHANGE: the Change side on the table Find Next found. thenNext (Change/Find): on to the next, written or not - the reason it was not on the second line.
void TableChange(IPMUnknown* face, bool thenNext)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	PMString words;
	words.SetTranslatable(kFalse);
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		PMString none;
		SayOnTablePart(face, words, none);
		return;
	}
	const bool wrote = KFCRuns()->TableChangeFound(gFoundTable, words);
	KFC_DIAG_LOG("TBLTAB change wrote=%d %s", wrote ? 1 : 0, words.GetUTF8String().c_str());
	if (wrote)
	{
		gFoundWritten = true;
		KFCResultTree::RefreshRows();		// (a row of the list that stands for it reads as written)
	}
	if (thenNext)
	{
		TableFindNext(face);
		if (!wrote)
			SetFaceLine(panel, kKFCTableTabNoteWidgetID, words);
		return;
	}
	PMString none;
	SayOnTablePart(face, words, none);
	ShowTableButtons(panel);
}

// CHANGE ALL: every table of Search: given the Change side, one Ctrl+Z, said as InDesign says it.
void TableChangeAll(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	PMString words;
	words.SetTranslatable(kFalse);
	PMString none;
	if (KFCRuns()->IsAnyRunning())
	{
		words.Append(KFCRuns()->BusyMessage());
		SayOnTablePart(face, words, none);
		return;
	}
	int32 changed = 0, matched = 0;
	const bool ran = KFCRuns()->TableChangeAll(TableLookNow(), changed, matched, words);
	KFC_DIAG_LOG("TBLTAB changeall ran=%d changed=%d matched=%d %s", ran ? 1 : 0, static_cast<int>(changed), static_cast<int>(matched), words.GetUTF8String().c_str());
	if (ran && changed > 0)
	{
		gFoundWritten = true;
		KFCResultTree::RefreshRows();
	}
	SayOnTablePart(face, words, none);
	ShowTableButtons(panel);
	if (!ran)
		return;
	if (matched == 0)
		InformationAlertOf("Cannot find a match.", -1);
	else
		InformationAlertOf("Search is completed. ", changed);
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

// THE BASE BOXES FROM A BASE TEXT THAT CAME WHOLE (a saved query from before the boxes), a character a box, the columns grown to hold them - past the last box, all the rest in it.
void FillBaseFromKept()
{
	const std::vector<PMString> chars(CharsOf(gRuby.base));
	for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
	{
		gCellBase[k].Clear();
		gCellBase[k].SetTranslatable(kFalse);
	}
	for (size_t c = 0; c < chars.size(); ++c)
		gCellBase[(static_cast<int32>(c) < kKFCRubyBoxColumnsMost) ? static_cast<int32>(c) : kKFCRubyBoxColumnsMost - 1].Append(chars[c]);
	if (static_cast<int32>(chars.size()) > gCols)
		gCols = (static_cast<int32>(chars.size()) < kKFCRubyBoxColumnsMost) ? static_cast<int32>(chars.size()) : kKFCRubyBoxColumnsMost;
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
	for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
	{
		gCellFindRuby[k].Clear();
		gCellFindRuby[k].SetTranslatable(kFalse);
		gCellBase[k].Clear();
		gCellBase[k].SetTranslatable(kFalse);
		gCellChange[k].Clear();
		gCellChange[k].SetTranslatable(kFalse);
		gJoin[k] = false;
	}
	gCols = kKFCRubyBoxColumnsAtFirst;
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
		for (size_t p = 0; p < gRuby.change.size() && col < kKFCRubyBoxColumnsMost; ++p)
		{
			gCellChange[col] = gRuby.change[p].reading;
			if (anyFindReading)
				gCellFindRuby[col] = gRuby.change[p].findReading;
			++col;
		}
		if (col > gCols)
			gCols = col;
		FillBaseFromKept();
		if (readings == 1)
			for (int32 k = 0; k + 1 < gCols; ++k)
				gJoin[k] = !gCellBase[k + 1].IsEmpty();
		return;
	}
	for (size_t p = 0; p < gRuby.change.size(); ++p)
	{
		const std::vector<PMString> chars(CharsOf(gRuby.change[p].base));
		if (col >= kKFCRubyBoxColumnsMost)
		{
			for (size_t c = 0; c < chars.size(); ++c)
				gCellBase[kKFCRubyBoxColumnsMost - 1].Append(chars[c]);
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
			if (at >= kKFCRubyBoxColumnsMost)
			{
				gCellBase[kKFCRubyBoxColumnsMost - 1].Append(chars[c]);
				continue;
			}
			gCellBase[at] = chars[c];
			if (c > 0)
				gJoin[at - 1] = true;
		}
		col += chars.empty() ? 1 : static_cast<int32>(chars.size());
		if (col > kKFCRubyBoxColumnsMost)
			col = kKFCRubyBoxColumnsMost;
	}
	if (col > gCols)
		gCols = col;
}

// WHERE A COLUMN STANDS IN THE GRID (KFCUI.fr): the labels to 110, the + and - from 428, the columns sharing what is between.
const int32 kGridColumnsLeft = 115;
const int32 kGridColumnsRight = 422;
const int32 kGridBoxGap = 2;

int32 GridColumnLeft(int32 k)
{
	return kGridColumnsLeft + ((kGridColumnsRight - kGridColumnsLeft + kGridBoxGap) * k) / gCols;
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
// Change's, which follow it (the author, 11-01) - runs over the columns joined to it, and the boxes it runs over are away. GREY: Find's Ruby: with Text Without Ruby, whose search has no reading; the Change side and the chains with Ruby Marks in Text - the reading is in the mark -
// and all of it with the part hidden.
void LayOutRubyGrid(IPanelControlData* panel)
{
	const bool part = (gKind == kKindRuby);
	// WORDS IN A RUBY LIST ASKS NOTHING OF THE BOXES: they go, and what it says of its file stands in their place.
	const bool listing = part && gRuby.kind == kKFCRubyFindList;
	const bool boxes = !listing;
	const bool findReads = part && gRuby.kind != kKFCRubyFindWithout;
	const bool changes = part && gRuby.kind != kKFCRubyFindMarks;
	PlaceGridWidget(panel, kKFCRubyGridLabelReadingWidgetID, boxes, part, 0, 0, 110, 21);
	PlaceGridWidget(panel, kKFCRubyGridLabelBaseWidgetID, boxes, part, 0, 41, 110, 62);
	PlaceGridWidget(panel, kKFCRubyGridLabelChangeWidgetID, boxes, part, 0, 72, 110, 93);
	PlaceGridWidget(panel, kKFCRubyGridLabelChangeBaseWidgetID, boxes, part, 0, 96, 110, 116);
	PlaceGridWidget(panel, kKFCRubyListLabelWidgetID, listing, listing, 0, 0, 110, 21);
	PlaceGridWidget(panel, kKFCRubyListNameWidgetID, listing, listing, 115, 0, 456, 21);
	PlaceGridWidget(panel, kKFCRubyListStatusWidgetID, listing, listing, 0, 26, 456, 47);
	// (Choose List... and How to Use... stand in the head's column.)
	IControlView* const choose = (panel != nil) ? panel->FindWidget(kKFCRubyListChooseWidgetID) : nil;
	if (choose != nil)
	{
		choose->Enable(listing ? kTrue : kFalse);
		choose->ShowView(listing ? kTrue : kFalse);
	}
	for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
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
		PlaceGridWidget(panel, ChangeBoxID(k), stands && !overLeft, changes, left, 72, readingRight, 93);
		PlaceGridWidget(panel, ChangeBaseID(k), stands, changes, left, 96, columnRight, 116);
		if (k + 1 < kKFCRubyBoxColumnsMost)
		{
			const bool between = boxes && (k + 1 < gCols);
			const int32 middle = between ? GridColumnLeft(k + 1) - (kGridBoxGap / 2) : 0;
			PlaceGridWidget(panel, JoinID(k), between, changes, middle - 10, 21, middle + 10, 41);
		}
	}
	IControlView* const plus = (panel != nil) ? panel->FindWidget(kKFCRubyGridPlusWidgetID) : nil;
	if (plus != nil)
	{
		plus->Enable((part && boxes && gCols < kKFCRubyBoxColumnsMost) ? kTrue : kFalse);
		plus->ShowView(boxes ? kTrue : kFalse);
	}
	IControlView* const minus = (panel != nil) ? panel->FindWidget(kKFCRubyGridMinusWidgetID) : nil;
	if (minus != nil)
	{
		minus->Enable((part && boxes && gCols > 1) ? kTrue : kFalse);
		minus->ShowView(boxes ? kTrue : kFalse);
	}
}

// WHAT THE PART SAYS OF ITS LIST: the file's name, and how many words it holds or why it cannot be used - read now, as a Find will read it.
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
	SetFaceLine(panel, kKFCRubyListStatusWidgetID, said);
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

// EVERY BOX AND ITS WORDS WRITTEN FROM WHAT IS KEPT. Not heard (notifyOfChange kFalse).
void WriteBoxes(IPanelControlData* panel)
{
	for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
	{
		SetField(panel, ReadingBoxID(k), gCellFindRuby[k]);
		SetField(panel, BaseBoxID(k), gCellBase[k]);
		SetField(panel, ChangeBoxID(k), gCellChange[k]);
		SetField(panel, ChangeBaseID(k), gCellBase[k]);
		if (k + 1 < kKFCRubyBoxColumnsMost)
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
	int32 col = ColumnOf(held, ReadingBoxID(0), kKFCRubyBoxColumnsMost);
	if (col < 0)
	{
		boxOf = ChangeBoxID;
		col = ColumnOf(held, ChangeBoxID(0), kKFCRubyBoxColumnsMost);
	}
	if (col < 0)
	{
		boxOf = BaseBoxID;
		joins = false;
		col = ColumnOf(held, BaseBoxID(0), kKFCRubyBoxColumnsMost);
	}
	if (col < 0 || (col < gCols && !(joins && col > 0 && gJoin[col - 1])))
		return kInvalidWidgetID;
	keyboard->RelinquishKeyFocus();
	int32 to = (col < gCols) ? col : gCols - 1;
	while (joins && to > 0 && gJoin[to - 1])
		--to;
	return boxOf(to);
}

// THE BOXES ARE GOING, ALL OF THEM (Words in a Ruby List): one holding the keyboard lets go - the keys would go on into a box no one sees (LetGoOfGoingBox's rule, below; it answers for the columns that stay).
void LetGoOfAnyBox()
{
	ISession* const session = GetExecutionContextSession();
	InterfacePtr<IApplication> application((session != nil) ? session->QueryApplication() : nil);
	InterfacePtr<IKeyBoard> keyboard(application, UseDefaultIID());
	InterfacePtr<IControlView> focus((keyboard != nil) ? keyboard->GetKeyFocus() : nil, UseDefaultIID());
	if (focus == nil)
		return;
	const WidgetID held = focus->GetWidgetID();
	if (ColumnOf(held, ReadingBoxID(0), kKFCRubyBoxColumnsMost) >= 0 || ColumnOf(held, BaseBoxID(0), kKFCRubyBoxColumnsMost) >= 0 || ColumnOf(held, ChangeBoxID(0), kKFCRubyBoxColumnsMost) >= 0)
		keyboard->RelinquishKeyFocus();
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

// ----- How to Use... (the author, 11-02: "left of the Done button", on Ruby and on Kenten)
//
// DONE IS THE DIALOG FRAME'S, in a row of the frame under the faces' tabs (measured: the row's first widget is on no face), so a child of the face cannot stand beside it. The button is built into that row - the recipe of Adobe's linksui
// (LinkInfoPanelObserver: ::CreateObject with a RsrcSpec, AddWidget, SetFrame), memory widget-injection-builtin-ui - and it is a plain button of InDesign's (KFCUI.fr). Found and built as the face is shown. HIDING WALKS NOTHING: the face's
// AutoDetach comes with the dialog's close too, and a face that walked the dialog's widgets there left Find/Change closed and yet standing, for good (the QR spec's D1, measured): the face holds the button for as long as it is shown.

// How far left of Done: the Object tab's Find Next and Change stand 5 apart in a column, and InDesign's dialogs part two buttons in a row by 10 (kEVELargeSpaceAfter).
const int32 kHowToGapToDone = 10;

// The first panel up from the face that holds the frame's Done: the dialog's own, for the frame is no face's. Nil with no such dialog. (The face is shown: see above.)
IControlView* FindFrameDone(IPMUnknown* face)
{
	InterfacePtr<IWidgetParent> at(face, UseDefaultIID());
	for (int32 up = 0; up < 32 && at != nil; ++up)
	{
		IPMUnknown* const parent = at->GetParent();
		InterfacePtr<IPanelControlData> panel(parent, UseDefaultIID());
		IControlView* const done = (panel != nil) ? panel->FindWidget(kFindMinITabbedDialogOKButtonWidgetID) : nil;
		if (done != nil)
			return done;
		InterfacePtr<IWidgetParent> next(parent, UseDefaultIID());
		at.reset(next.forget());
	}
	return nil;
}

// The button in Done's row (AddRef'd): the one built at an earlier show if the dialog has kept it, or built now, Done's height and binding, its own width left of Done. Nil where the dialog is not as measured: then there is simply no How to Use....
IControlView* QueryHowTo(IPMUnknown* face)
{
	IControlView* const done = FindFrameDone(face);
	InterfacePtr<IWidgetParent> doneParent(done, UseDefaultIID());
	InterfacePtr<IPanelControlData> row((doneParent != nil) ? doneParent->GetParent() : nil, UseDefaultIID());
	if (row == nil)
		return nil;
	IControlView* const kept = row->FindWidget(kKFCKohakuHowToWidgetID);
	if (kept != nil)
	{
		kept->AddRef();
		return kept;
	}
	InterfacePtr<IControlView> button(::CreateObject2<IControlView>(::GetDataBase(row), RsrcSpec(LocaleSetting::GetLocale(), kKFCUIPluginID, kViewRsrcType, kKFCKohakuHowToRsrcID)));
	if (button == nil)
		return nil;
	row->AddWidget(button);
	const PMRect doneFrame = done->GetFrame();
	const PMReal right = doneFrame.Left() - PMReal(kHowToGapToDone);
	button->SetFrame(PMRect(right - button->GetFrame().Width(), doneFrame.Top(), right, doneFrame.Bottom()));
	button->SetFrameBinding(done->GetFrameBinding());
	return button.forget();
}

// THE BUTTON FOR THE KIND NOW: Ruby's and Kenten's own, each with its page; a hidden one takes no click. Nothing while the face is hidden (gHowTo).
void ShowHowTo()
{
	if (gHowTo == nil)
		return;
	const bool shown = (gKind == kKindRuby || gKind == kKindKenten);
	gHowTo->Enable(shown ? kTrue : kFalse);
	gHowTo->ShowView(shown ? kTrue : kFalse);
}

// THE CHOSEN KIND'S PART, the others away, and Search: for it.
void ShowKind(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	ShowPart(panel, kKFCKohakuQRSectionWidgetID, gKind == kKindQRCode);
	ShowPart(panel, kKFCQRTabFindNextWidgetID, gKind == kKindQRCode || gKind == kKindTable);
	ShowHowTo();
	ShowPart(panel, kKFCTableTabChangeWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCTableTabChangeAllWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCTableTabChangeFindWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCTableTabFindLabelWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCKohakuTableSectionWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCKohakuRubySectionWidgetID, gKind == kKindRuby);
	ShowPart(panel, kKFCKohakuKentenSectionWidgetID, gKind == kKindKenten);
	ShowRubyKind(panel);		// (their own greys stand whatever their panels were told)
	ShowKentenKind(panel);
	ShowTableButtons(panel);
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
	// Fresh values, so the storage goes too.
	gTable = KFCTableConditions();
	gRuby = KFCRubyConditions();
	gKenten = KFCKentenConditions();
	gFindPaths = std::vector<PMString>();
	gChangePaths = std::vector<PMString>();
	gFoundTable = KFCTableFound();
	for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
	{
		gCellFindRuby[k] = PMString();
		gCellBase[k] = PMString();
		gCellChange[k] = PMString();
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
	KFCKohakuTabObserver(IPMUnknown* boss) : AbstractDialogObserver(boss), fHowTo(nil) {}
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
			AttachToWidget(kKFCRubyListChooseWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeAllWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeFindWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
			{
				AttachToWidget(ReadingBoxID(k), IID_ITEXTCONTROLDATA, panel);
				AttachToWidget(BaseBoxID(k), IID_ITEXTCONTROLDATA, panel);
				AttachToWidget(ChangeBoxID(k), IID_ITEXTCONTROLDATA, panel);
				if (k + 1 < kKFCRubyBoxColumnsMost)
					AttachToWidget(JoinID(k), IID_ITRISTATECONTROLDATA, panel);
			}
			AttachToWidget(kKFCRubyGridPlusWidgetID, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCRubyGridMinusWidgetID, IID_ITRISTATECONTROLDATA, panel);
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
		// HOW TO USE..., held and heard while shown, and shown by FillKept for the kind. (Heard as the face's own buttons are; AttachToWidget would want the row's panel.)
		fHowTo.reset(QueryHowTo(this));
		gHowTo = fHowTo;
		InterfacePtr<ISubject> howToSubject(fHowTo, UseDefaultIID());
		if (howToSubject != nil && !howToSubject->IsAttached(ISubject::kRegularAttachment, this, IID_IBOOLEANCONTROLDATA, IID_IOBSERVER))
			howToSubject->AttachObserver(ISubject::kRegularAttachment, this, IID_IBOOLEANCONTROLDATA, IID_IOBSERVER);
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
		// HOW TO USE... GOES WITH THE FACE - the button the face holds; no walk to it (the note above QueryHowTo).
		if (fHowTo != nil)
		{
			InterfacePtr<ISubject> howToSubject(fHowTo, UseDefaultIID());
			if (howToSubject != nil && howToSubject->IsAttached(ISubject::kRegularAttachment, this, IID_IBOOLEANCONTROLDATA, IID_IOBSERVER))
				howToSubject->DetachObserver(ISubject::kRegularAttachment, this, IID_IBOOLEANCONTROLDATA, IID_IOBSERVER);
			fHowTo->Disable();
			fHowTo->HideView();
		}
		gHowTo = nil;
		fHowTo.reset();
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
		DetachFromWidget(kKFCRubyListChooseWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeAllWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeFindWidgetID, IID_IBOOLEANCONTROLDATA, panel);
		for (int32 k = 0; k < kKFCRubyBoxColumnsMost; ++k)
		{
			DetachFromWidget(ReadingBoxID(k), IID_ITEXTCONTROLDATA, panel);
			DetachFromWidget(BaseBoxID(k), IID_ITEXTCONTROLDATA, panel);
			DetachFromWidget(ChangeBoxID(k), IID_ITEXTCONTROLDATA, panel);
			if (k + 1 < kKFCRubyBoxColumnsMost)
				DetachFromWidget(JoinID(k), IID_ITRISTATECONTROLDATA, panel);
		}
		DetachFromWidget(kKFCRubyGridPlusWidgetID, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCRubyGridMinusWidgetID, IID_ITRISTATECONTROLDATA, panel);
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
			if (gKind == kKindTable)
				TableFindNext(this);
			else
				FindNext(this);
		}
		else if (widget == kKFCKohakuHowToWidgetID && theChange == kTrueStateMessage)
		{
			KFC_DIAG_LOG("%s how to", KindWord());
			if (gKind == kKindKenten)
				KFCHowTo::ShowKenten();
			else
				KFCHowTo::ShowRubyMarks();
		}
		else if (widget == kKFCRubyListChooseWidgetID && theChange == kTrueStateMessage)
		{
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			if (ChooseRubyList(panel))
				KFCKohakuQueryMenu::FaceChanged();		// (the list is what a query holds; its press alone is nothing - below)
		}
		else if (widget == kKFCTableTabChangeWidgetID && theChange == kTrueStateMessage)
			TableChange(this, false);
		else if (widget == kKFCTableTabChangeFindWidgetID && theChange == kTrueStateMessage)
			TableChange(this, true);
		else if (widget == kKFCTableTabChangeAllWidgetID && theChange == kTrueStateMessage)
			TableChangeAll(this);
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
			ShowTableButtons(panel);
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
			ShowTableButtons(panel);
		}
		else if (widget == kKFCRubyTabKindWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKFCRubyFindWith && entry <= kKFCRubyFindList)
				gRuby.kind = entry;
			if (gRuby.kind == kKFCRubyFindList)
				LetGoOfAnyBox();
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowRubyKind(panel);
			KFC_DIAG_LOG("RUBYTAB kind=%d", static_cast<int>(gRuby.kind));
		}
		else if (ColumnOf(widget, ReadingBoxID(0), kKFCRubyBoxColumnsMost) >= 0)
		{
			// A BOX OF FIND'S RUBY: - the search reads them all run together (KeepFromBoxes).
			const int32 k = ColumnOf(widget, ReadingBoxID(0), kKFCRubyBoxColumnsMost);
			gCellFindRuby[k] = WordsOf(view);
			KeepFromBoxes();
			KFC_DIAG_LOG("RUBYTAB reading=%s", gRuby.reading.GetUTF8String().c_str());
		}
		else if (ColumnOf(widget, BaseBoxID(0), kKFCRubyBoxColumnsMost) >= 0)
		{
			// A BOX OF FIND'S BASE TEXT: - and the Change side's words under its column, which are this box's.
			const int32 k = ColumnOf(widget, BaseBoxID(0), kKFCRubyBoxColumnsMost);
			gCellBase[k] = WordsOf(view);
			KeepFromBoxes();
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			SetField(panel, ChangeBaseID(k), gCellBase[k]);
			KFC_DIAG_LOG("RUBYTAB base=%s", gRuby.base.GetUTF8String().c_str());
		}
		else if (ColumnOf(widget, ChangeBoxID(0), kKFCRubyBoxColumnsMost) >= 0)
		{
			const int32 k = ColumnOf(widget, ChangeBoxID(0), kKFCRubyBoxColumnsMost);
			gCellChange[k] = WordsOf(view);
			KeepFromBoxes();
			KFC_DIAG_LOG("RUBYTAB box r%d=%s", static_cast<int>(k), gCellChange[k].GetUTF8String().c_str());
		}
		else if (ColumnOf(widget, JoinID(0), kKFCRubyBoxColumnsMost - 1) >= 0 && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			// A CHAIN PRESSED (F16). Joined: the right column's reading goes to the end of the left's - the box they now share - in Find and Change alike. Let go: the left's box keeps what it has, the right's stands empty.
			const int32 k = ColumnOf(widget, JoinID(0), kKFCRubyBoxColumnsMost - 1);
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
		else if (widget == kKFCRubyGridPlusWidgetID && theChange == kTrueStateMessage)
		{
			if (gCols < kKFCRubyBoxColumnsMost)
				++gCols;
			KeepFromBoxes();
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			LayOutRubyGrid(panel);
			KFC_DIAG_LOG("RUBYTAB cols=%d", static_cast<int>(gCols));
		}
		else if (widget == kKFCRubyGridMinusWidgetID && theChange == kTrueStateMessage)
		{
			// THE LAST COLUMN GOES, what it held with it (the Captions dialog's - no asking), and the chain before it: the next + brings a clean one.
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			if (gCols > 1)
			{
				--gCols;
				gCellFindRuby[gCols].Clear();
				gCellBase[gCols].Clear();
				gCellChange[gCols].Clear();
				gJoin[gCols - 1] = false;
				SetField(panel, ReadingBoxID(gCols), gCellFindRuby[gCols]);
				SetField(panel, BaseBoxID(gCols), gCellBase[gCols]);
				SetField(panel, ChangeBoxID(gCols), gCellChange[gCols]);
				SetField(panel, ChangeBaseID(gCols), gCellBase[gCols]);
				SetBox(panel, JoinID(gCols - 1), false);
			}
			KeepFromBoxes();
			const WidgetID focusTo(LetGoOfGoingBox());
			LayOutRubyGrid(panel);
			if (focusTo != kInvalidWidgetID && panel != nil)
				panel->SetKeyboardFocus(focusTo);
			KFC_DIAG_LOG("RUBYTAB cols=%d", static_cast<int>(gCols));
		}
		else if (widget == kKFCKentenTabKindWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKFCKentenFindWith && entry <= kKFCKentenFindWithout)
				gKenten.kind = entry;
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowKentenKind(panel);
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
		}
		else if (widget == kKFCKentenTabRemoveWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			gKenten.removeKenten = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("KENTAB remove=%d", gKenten.removeKenten ? 1 : 0);
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			RedrawKentenSamples(panel);
		}
		// WHAT A QUERY HOLDS WAS CHANGED BY HAND: Find/Change's Query menu says [Custom], as a change on InDesign's own tabs does. (Search: is no part of a query, as InDesign's own Search: is none of its menu's; Find Next changes nothing, nor do the
		// Table part's other buttons - what they write is the document - nor a + - a box more holds nothing.)
		if (widget != kKFCKohakuTabScopeWidgetID && widget != kKFCQRTabFindNextWidgetID && widget != kKFCTableTabChangeWidgetID && widget != kKFCTableTabChangeAllWidgetID
			&& widget != kKFCTableTabChangeFindWidgetID && widget != kKFCRubyGridPlusWidgetID && widget != kKFCKohakuHowToWidgetID && widget != kKFCRubyListChooseWidgetID
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
	InterfacePtr<IControlView> fHowTo;	// How to Use..., while the face is shown (gHowTo)
};
CREATE_PMINTERFACE(KFCKohakuTabObserver, kKFCKohakuTabObserverImpl)

/** The selection, while the face is shown (KFCQueryScopeObserver's shape: an ActiveSelectionObserver under an IID of its own, beside the face's observer of its widgets - the face's AutoAttach and AutoDetach drive it). It only greys: the Table part's Change and
    Change/Find, once the selection leaves what Find Next found (F6). */
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
		ShowTableButtons(panel);
	}
};
CREATE_PMINTERFACE(KFCKohakuTabSelectionObserver, kKFCKohakuTabSelectionObserverImpl)

// End, KFCKohakuTab.cpp.
