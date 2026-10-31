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
#include "IControlView.h"
#include "IDropDownListController.h"	// the face's lists' entries
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
#include "LocaleSetting.h"				// the UI language - Ruby is a Japanese InDesign's
#include "CDialogController.h"
#include "CPanelCreator.h"
#include "TablesID.h"					// IID_ITABLESTYLEGROUPMANAGER
#include "UIDList.h"

#include <vector>

// Project includes:
#include "KFCKohakuTab.h"
#include "KFCUIID.h"
#include "KFCDiag.h"					// KFC_DIAG_LOG - the face's calls, in a test build's trace (QRTAB / TBLTAB / RUBYTAB lines)
#include "KFCPanelTitle.h"				// Update - the panel's title names the kind and its Search:
#include "KFCAppBarSearchEnter.h"		// KFCMirrorAppBarFieldNow - the Application Bar's field shows the tab's nothing
#include "KFCJump.h"					// LandOnQRCode - Find Next lands as an object row's click does
#include "KFCModelAccess.h"				// KFCRuns()->QRCodeNext - the QR code after the one found last (the model's)
#include "KFCKohakuQueryMenu.h"			// the face shown and hidden, and what it asks changed: Find/Change's own Query menu

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
int32 gScope = IWalkerScopeFactoryUtils::kDocumentScope;	// its Search: - Document at launch
KFCTableConditions gTable;	// what the Table part asks, kept from its widgets (its scope is gScope)
KFCRubyConditions gRuby;	// what the Ruby part asks
KFCKentenConditions gKenten;	// what the Kenten part asks

// THE FULL PATHS BEHIND THE STYLE LISTS' ENTRIES (entry n = gFindPaths[n - 1] - entry 0 asks for nothing), as FillStyles wrote them.
std::vector<PMString> gFindPaths;
std::vector<PMString> gChangePaths;

// THE QR CODE FIND NEXT FOUND LAST - where its next press goes on from (Q10: not the selection). Kept only while the face is shown - let go when it is shown again and when the dialog closes:
// a closed document's database address can be a document opened later (memory uidref-reuse-after-close), and a QR code of the same UID there would be taken for it. NOT let go when a document
// closes and another opens while the face stays shown - tried once, the address was not reused (docs/ai-notes/kfc-qr-recheck-2026-10-28.md O21-1).
UIDRef gLastFound;

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

// Ruby: means nothing to Text Without Ruby, nor the Change side to ruby from marks - the reading is in the mark: grey there.
void ShowRubyKind(IPanelControlData* panel)
{
	IControlView* const reading = (panel != nil) ? panel->FindWidget(kKFCRubyTabReadingWidgetID) : nil;
	if (reading != nil)
		reading->Enable(gRuby.kind == kKFCRubyFindWithout ? kFalse : kTrue);
	const bool changes = (gRuby.kind != kKFCRubyFindMarks);
	IControlView* const change = (panel != nil) ? panel->FindWidget(kKFCRubyTabChangeWidgetID) : nil;
	if (change != nil)
		change->Enable(changes ? kTrue : kFalse);
	IControlView* const remove = (panel != nil) ? panel->FindWidget(kKFCRubyTabRemoveWidgetID) : nil;
	if (remove != nil)
		remove->Enable(changes ? kTrue : kFalse);
}

// Kenten: means nothing to Text Without Kenten: grey there.
void ShowKentenKind(IPanelControlData* panel)
{
	IControlView* const mark = (panel != nil) ? panel->FindWidget(kKFCKentenTabMarkWidgetID) : nil;
	if (mark != nil)
		mark->Enable(gKenten.kind == kKFCKentenFindWithout ? kFalse : kTrue);
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

// THE CHOSEN KIND'S PART, the others away, and Search: for it.
void ShowKind(IPMUnknown* face)
{
	InterfacePtr<IPanelControlData> panel(face, UseDefaultIID());
	ShowPart(panel, kKFCKohakuQRSectionWidgetID, gKind == kKindQRCode);
	ShowPart(panel, kKFCQRTabFindNextWidgetID, gKind == kKindQRCode);
	ShowPart(panel, kKFCKohakuTableSectionWidgetID, gKind == kKindTable);
	ShowPart(panel, kKFCKohakuRubySectionWidgetID, gKind == kKindRuby);
	ShowPart(panel, kKFCKohakuKentenSectionWidgetID, gKind == kKindKenten);
	ShowRubyKind(panel);		// (their own greys stand whatever their panels were told)
	ShowKentenKind(panel);
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
	SetField(panel, kKFCRubyTabBaseWidgetID, gRuby.base);
	SetField(panel, kKFCRubyTabReadingWidgetID, gRuby.reading);
	SetField(panel, kKFCRubyTabChangeWidgetID, gRuby.changeTo);
	SetBox(panel, kKFCRubyTabRemoveWidgetID, gRuby.removeRuby);
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
	}
	else if (query.mode == kKFCKentenSearchMode)
	{
		gKind = kKindKenten;
		gKenten = query.kenten;
	}
	else
		gKind = kKindQRCode;
	gYield = false;
	gLastFound = UIDRef();		// (Find Next starts from the first again)
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
		gLastFound = UIDRef();		// Find Next starts again at the next open (gLastFound's note)
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
		gLastFound = UIDRef();		// Find Next starts from the first again (gLastFound's note)
		InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
		if (panel != nil)
		{
			AttachToWidget(kKFCKohakuTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCKohakuTabScopeWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCQRTabFindNextWidgetID, IID_IBOOLEANCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabFindStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabChangeStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCTableTabOverridesOnlyWidgetID, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCTableTabClearOverridesWidgetID, IID_ITRISTATECONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabBaseWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabReadingWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabChangeWidgetID, IID_ITEXTCONTROLDATA, panel);
			AttachToWidget(kKFCRubyTabRemoveWidgetID, IID_ITRISTATECONTROLDATA, panel);
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
		DetachFromWidget(kKFCTableTabFindStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabChangeStyleWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabOverridesOnlyWidgetID, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCTableTabClearOverridesWidgetID, IID_ITRISTATECONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabKindWidgetID, IID_ISTRINGLISTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabBaseWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabReadingWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabChangeWidgetID, IID_ITEXTCONTROLDATA, panel);
		DetachFromWidget(kKFCRubyTabRemoveWidgetID, IID_ITRISTATECONTROLDATA, panel);
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
			gLastFound = UIDRef();		// (Find Next starts from the first again)
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
			FindNext(this);
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
		}
		else if (widget == kKFCRubyTabKindWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= kKFCRubyFindWith && entry <= kKFCRubyFindMarks)
				gRuby.kind = entry;
			InterfacePtr<IPanelControlData> panel(this, UseDefaultIID());
			ShowRubyKind(panel);
			KFC_DIAG_LOG("RUBYTAB kind=%d", static_cast<int>(gRuby.kind));
		}
		else if (widget == kKFCRubyTabBaseWidgetID)
		{
			gRuby.base = WordsOf(view);
			KFC_DIAG_LOG("RUBYTAB base=%s", gRuby.base.GetUTF8String().c_str());
		}
		else if (widget == kKFCRubyTabReadingWidgetID)
		{
			gRuby.reading = WordsOf(view);
			KFC_DIAG_LOG("RUBYTAB reading=%s", gRuby.reading.GetUTF8String().c_str());
		}
		else if (widget == kKFCRubyTabChangeWidgetID)
		{
			gRuby.changeTo = WordsOf(view);
			KFC_DIAG_LOG("RUBYTAB change=%s", gRuby.changeTo.GetUTF8String().c_str());
		}
		else if (widget == kKFCRubyTabRemoveWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			gRuby.removeRuby = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("RUBYTAB remove=%d", gRuby.removeRuby ? 1 : 0);
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
		}
		else if (widget == kKFCKentenTabChangeWidgetID)
		{
			InterfacePtr<IDropDownListController> list(view, UseDefaultIID());
			const int32 entry = (list != nil) ? list->GetSelected() : -1;
			if (entry >= 0)
				gKenten.changeTo = entry;		// (an entry is its IKentenStyle::KentenKind, 0 = leave it)
			KFC_DIAG_LOG("KENTAB change=%d", static_cast<int>(gKenten.changeTo));
		}
		else if (widget == kKFCKentenTabRemoveWidgetID && (theChange == kTrueStateMessage || theChange == kFalseStateMessage))
		{
			gKenten.removeKenten = (theChange == kTrueStateMessage);
			KFC_DIAG_LOG("KENTAB remove=%d", gKenten.removeKenten ? 1 : 0);
		}
		// WHAT A QUERY HOLDS WAS CHANGED BY HAND: Find/Change's Query menu says [Custom], as a change on InDesign's own tabs does. (Search: is no part of a query, as InDesign's own Search: is none of its menu's; Find Next changes nothing.)
		if (widget != kKFCKohakuTabScopeWidgetID && widget != kKFCQRTabFindNextWidgetID
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

// End, KFCKohakuTab.cpp.
