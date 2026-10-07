//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCBookPanelLookup.h. UI side.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// QueryPanelManager - the panel walk starts here
#include "IBook.h"
#include "IBookContent.h"		// GetShortName - the test build's selection names its chapters
#include "IBookContentMgr.h"
#include "IBookManager.h"
#include "IBookUIUtils.h"		// GetBookFileFromBookPanel (panel vs active book)
#include "IControlView.h"		// a panel IS a control view - what GetNthPanelInfo's UID resolves to
#include "IPanelMgr.h"			// GetPanelCount / GetNthPanelInfo - one book panel per open book
#include "IPanelControlData.h"	// what QueryActiveBookPanel hands over - the book panel's class is read off it
#include "ISession.h"

// General includes:
#include "FileUtils.h"			// IsEqual - a panel's book and the searched book, compared as files
#include "PaletteRefUtils.h"	// IsPaletteVisible - the front tab is decided on the container
#include "PersistUtils.h"		// ::GetClass / ::GetDataBase
#include "SDKFileHelper.h"
#include "UIDList.h"			// GetSelectedBookContents' answer
#include "Utils.h"
#include "WideString.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Project includes:
#include "KFCBookPanelLookup.h"
#include "KFCDiag.h"			// KFC_DIAG_FAULT - the test build's book-selection (compiled out of a shipping one)

namespace
{
	/** The ClassID of InDesign's book panel as THIS build numbers it, learned from a live one - or
	    kInvalidClass while none could be asked yet.

	    Why it is learned rather than written down. kBookPanelBoss lives in BOOK PANEL.APLN and is
	    declared in no public header, and the two other ways to recognise the panel both fail:
	      * a number - 0x10101, read off a 20.5 DEBUG build's object-model dump
	        (docs/ai-notes/book-panel-active-tab.md) - is one build's numbering, and nothing promises
	        the release build of another version keeps it;
	      * matching the panel's NAME against the open books' titles can pick the WRONG PANEL: a panel
	        whose name merely STARTS with a book's title makes a book called "Book" turn the Bookmarks
	        panel into a book panel, and a book called "Info" the Info panel (the user asked "how do
	        you tell them apart - can it not get it wrong?", and it could).
	    A wrong answer would move and resize somebody else's palette (Remember Book Panel Placement).
	    Neither is used - and no hard-coded number as a fallback either, at the author's word.
	    IBookUIUtils::QueryActiveBookPanel hands over the active book's own panel (IBookUIUtils.h:83-87),
	    so its class is the book panel's class by construction, whatever the build numbers it.
	    Learned once and kept: a class does not change within a session. Nothing is asked while no
	    book is open - there is no book panel to learn from, and no book panel to find either. */
	ClassID gLearnedBookPanelClass = kInvalidClass;

	ClassID LearnedBookPanelClass()
	{
		if (gLearnedBookPanelClass != kInvalidClass)
			return gLearnedBookPanelClass;

		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		if (bookMgr == nil || bookMgr->GetBookCount() == 0)
			return kInvalidClass;
		if (!Utils<IBookUIUtils>().Exists())
			return kInvalidClass;

		InterfacePtr<IPanelControlData> activeBookPanel(Utils<IBookUIUtils>()->QueryActiveBookPanel());
		if (activeBookPanel == nil)
			return kInvalidClass;	// no active book right now - asked again next time

		gLearnedBookPanelClass = ::GetClass(activeBookPanel);
		return gLearnedBookPanelClass;
	}

	/** Is this registered panel one of InDesign's book panels?

	    ONE PLACE. Every walk of the panel list in this plug-in asks it here (ForEachBookPanel,
	    and KFCBookPanelPlacement through IsBookPanel), so how a book panel is recognised is decided once.
	    The class learned from a live book panel decides, and nothing else (see LearnedBookPanelClass).
	    Before one could be asked, the answer is "no": the callers here then fall back to the active
	    book, and KFCBookPanelPlacement finds no book panel to measure or move. With no book open that
	    is simply right. With books open but NONE ACTIVE - QueryActiveBookPanel then has nothing to
	    hand over - their panels go unrecognised until one is; whether InDesign lets that state last
	    is not measured. */
	bool IsBookPanelView(IControlView* panelView)
	{
		if (panelView == nil)
			return false;
		const ClassID learned = LearnedBookPanelClass();
		if (learned == kInvalidClass)
			return false;
		return ::GetClass(panelView) == learned;
	}

	/** The book file THIS panel is showing; false when the panel could not be resolved.

	    Handing the panel itself in is the whole point: with a real widget IBookUIUtils resolves that
	    panel's book, where a nil widget falls through to QueryActiveBookPanel - the active book, which
	    is precisely the value both callers exist to avoid.

	    An empty result is refused here rather than passed on, because further up it would read as
	    "no book at all" instead of "this panel could not be asked". */
	bool GetBookFileFromPanelView(IControlView* panelView, IDFile& outFile)
	{
		IDFile panelBookFile;
		Utils<IBookUIUtils>()->GetBookFileFromBookPanel(panelBookFile, panelView);

		SDKFileHelper panelFileHelper(panelBookFile);
		if (panelFileHelper.GetPath().empty())
			return false;

		outFile = panelBookFile;
		return true;
	}
}

IPanelMgr* KFCBookPanelLookup::QueryPanelManager()
{
	ISession* session = GetExecutionContextSession();
	if (session == nil)
		return nil;
	InterfacePtr<IApplication> app(session->QueryApplication());
	if (app == nil)
		return nil;
	return app->QueryPanelManager();
}

void KFCBookPanelLookup::ForEachBookPanel(IPanelMgr* panelMgr,
	const std::function<bool(IControlView* panelView, const WidgetID& panelWidgetID)>& visit)
{
	if (panelMgr == nil)
		return;
	IDataBase* panelDB = ::GetDataBase(panelMgr);
	if (panelDB == nil)
		return;

	const uint32 panelCount = panelMgr->GetPanelCount();
	for (uint32 i = 0; i < panelCount; ++i)
	{
		UID panelUID;
		WidgetID panelWidgetID;
		if (!panelMgr->GetNthPanelInfo(i, panelUID, nil, &panelWidgetID))
			continue;

		InterfacePtr<IControlView> panelView(panelDB, panelUID, UseDefaultIID());
		if (!IsBookPanelView(panelView))
			continue;
		if (visit(panelView, panelWidgetID))
			return;
	}
}

bool KFCBookPanelLookup::GetPanelBookFile(IDFile& outFile)
{
	if (!Utils<IBookUIUtils>().Exists())
		return false;

	// Walk every registered panel instead of asking for "the" book panel. The two ways of asking
	// fail and are not worth repeating (measured):
	//   - GetBookPanelWidget() returns nil for us. It is fed by the book panel's OWN actions
	//     (SetBookPanelWidget), so a command from another panel's flyout finds nothing stored.
	//   - GetBookFileFromBookPanel(file, nil) falls through to QueryActiveBookPanel(), i.e. the
	//     ACTIVE book - which is exactly the value we are trying not to use.
	// The walk works because InDesign creates one book panel per open book (tab count == panel
	// count) and registers each with IPanelMgr. Details: docs/ai-notes/book-panel-active-tab.md.
	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	bool found = false;
	ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID&) -> bool
	{
		// The front tab is decided on the CONTAINER, never on the panel. A book panel sitting
		// behind another tab still reports itself visible - all three panels came back
		// "Visible state 1" in the measurement - while only the front tab's kTabPanelContainerType
		// is visible. Asking panelView->IsVisible() here would match every book panel and pick
		// whichever came first.
		const PaletteRef container = panelMgr->GetPaletteRefContainingPanel(panelView);
		if (!container.IsValid() || !PaletteRefUtils::IsPaletteVisible(container))
			return false;

		// Keep looking when this panel could not be asked, rather than handing back a blank file.
		found = GetBookFileFromPanelView(panelView, outFile);
		return found;
	});

	// None: no visible book panel - it is iconised, its palette is closed, or no book is open at all.
	// The caller falls back to the active book, which is what the user expects in that state.
	return found;
}

bool KFCBookPanelLookup::IsBookPanel(IControlView* panelView)
{
	// A door onto the anonymous-namespace test, not a second copy of it: a copy here would be a
	// second place where a book panel is recognised.
	return IsBookPanelView(panelView);
}

void KFCBookPanelLookup::BringBookTabForward(const PMString& bookPath)
{
	// (Step 1, making the book the active one, is the model's half - KFCBookScope::MakeBookActive - and
	//  the caller asks it before this.)
	// 2. The tab the user can SEE. One panel per open book, each registered with IPanelMgr, so the
	//    panel is found by walking the list and asking each candidate which book it belongs to -
	//    the WidgetID is numbered per book at runtime, which is why no name can be used here.
	if (!Utils<IBookUIUtils>().Exists())
		return;	// the active book was still set; the tab is the part we could not do

	// THE BOOKS ARE COMPARED AS FILES, NOT AS PATH STRINGS.
	// FileUtils::IsEqual - the rule KFCBookScope's KFCDocumentLivesInFile keeps, and the model half's
	// MakeBookActive asks IBookManager::FindOpenBookByName with the same IDFile.
	const IDFile wanted(SDKFileHelper(bookPath).GetIDFile());

	InterfacePtr<IPanelMgr> panelMgr(QueryPanelManager());
	ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID& panelWidgetID) -> bool
	{
		// Unlike GetPanelBookFile, visibility is NOT a filter here: the tab we are looking for is
		// precisely the one that is NOT in front yet.
		IDFile panelBookFile;
		if (!GetBookFileFromPanelView(panelView, panelBookFile))
			return false;

		if (FileUtils::IsEqual(panelBookFile, wanted) == kFalse)
			return false;

		// kFalse = do not take the key focus. The keyboard walk calls this while the user is
		// holding an arrow key on the result tree; handing the focus to the book panel would end
		// the walk at the first book row.
		panelMgr->ShowPanelByWidgetID(panelWidgetID, kFalse);
		return true;
	});
}

namespace
{
	/** The Book panel's own selection for this book, as it stands: the selected rows' BookContent UIDs and the panel's
	    row count - AcquireCurrentBook's read, nothing judged yet. false = no panel shows this book, or it could not be
	    read (the whole book, either way). */
	bool ReadPanelSelection(const IDFile& bookFile, std::vector<UID>& outContents, int32& outTotal)
	{
		if (!Utils<IBookUIUtils>().Exists())
			return false;
		InterfacePtr<IPanelMgr> panelMgr(KFCBookPanelLookup::QueryPanelManager());
		bool read = false;
		KFCBookPanelLookup::ForEachBookPanel(panelMgr, [&](IControlView* panelView, const WidgetID&) -> bool
		{
			// This book's panel - in front or not (the selection belongs to the panel, and the run to the book).
			IDFile panelBookFile;
			if (!GetBookFileFromPanelView(panelView, panelBookFile) || FileUtils::IsEqual(panelBookFile, bookFile) == kFalse)
				return false;
			InterfacePtr<IPanelControlData> panelData(Utils<IBookUIUtils>()->QueryBookPanelData(panelView));
			if (panelData == nil)
				return true;		// this book's panel, but not readable: the whole book
			K2Vector<int32> rows;
			UIDList* selected = nil;
			(void)Utils<IBookUIUtils>()->GetSelectedBookContents(rows, selected, panelData);
			outTotal = Utils<IBookUIUtils>()->GetListItems(panelData);
			if (selected != nil)
				for (int32 i = 0; i < selected->Length(); ++i)
					outContents.push_back(selected->At(i));
			delete selected;		// made by the callee, deleted by the caller (AcquireCurrentBook's destructor)
			read = true;
			return true;
		});
		return read;
	}

#ifdef KFC_DIAG
	/** THE TEST BUILD'S SELECTION (the fault switch book-selection - KFCDiag.h): the file's first line
	    "book=<the book's file name>", then one chapter's short name a line (IBookContent::GetShortName - "x-ch1.indd").
	    For that book, the chapters named stand for the panel's selected rows; for any other book nothing is
	    selected. Judged afterwards by the same rule as a real selection (GetPanelBookSelection). */
	bool ReadDiagSelection(const IDFile& bookFile, std::vector<UID>& outContents, int32& outTotal)
	{
		std::vector<std::string> lines;
		char* temp = nullptr;
		size_t len = 0;
		if (_dupenv_s(&temp, &len, "TEMP") != 0 || temp == nullptr)
			return false;
		const std::string path = std::string(temp) + "\\kbs-diag-fault-book-selection";
		free(temp);
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr)
			return false;
		std::string text;
		char buffer[512];
		size_t got = 0;
		while ((got = fread(buffer, 1, sizeof(buffer), f)) > 0 && text.size() < 65536)
			text.append(buffer, got);
		fclose(f);
		std::string line;
		text.push_back('\n');
		for (size_t i = 0; i < text.size(); ++i)
		{
			if (text[i] == '\r')
				continue;
			if (text[i] != '\n')
			{
				line += text[i];
				continue;
			}
			if (!line.empty())
				lines.push_back(line);
			line.clear();
		}
		if (lines.empty() || lines[0].compare(0, 5, "book=") != 0)
			return false;
		PMString wantedBook;
		wantedBook.SetUTF8String(lines[0].substr(5));
		PMString bookName;
		FileUtils::GetFileName(bookFile, bookName);
		if (!bookName.IsEqual(wantedBook, kFalse))
			return false;		// another book: nothing selected in it

		InterfacePtr<IBookManager> bookMgr(GetExecutionContextSession(), UseDefaultIID());
		IBook* book = (bookMgr != nil) ? bookMgr->FindOpenBookByName(bookFile) : nil;	// non-owning
		InterfacePtr<IBookContentMgr> contentMgr(book, UseDefaultIID());
		IDataBase* bookDB = (book != nil) ? ::GetDataBase(book) : nil;
		if (contentMgr == nil || bookDB == nil)
			return false;
		outTotal = contentMgr->GetContentCount();
		for (int32 i = 0; i < outTotal; ++i)
		{
			const UID contentUID = contentMgr->GetNthContent(i);
			InterfacePtr<IBookContent> content(bookDB, contentUID, UseDefaultIID());
			if (content == nil)
				continue;
			PMString shortName;
			WideString wide = content->GetShortName();
			const UTF16TextChar* buf = wide.GrabUTF16Buffer(nil);
			if (buf != nil)
				shortName.AppendW(buf);
			for (size_t n = 1; n < lines.size(); ++n)
			{
				PMString wanted;
				wanted.SetUTF8String(lines[n]);
				if (shortName.IsEqual(wanted, kFalse))
				{
					outContents.push_back(contentUID);
					break;
				}
			}
		}
		return true;
	}
#endif
}

bool KFCBookPanelLookup::GetPanelBookSelection(const IDFile& bookFile, std::vector<UID>& outContents, int32& outTotal)
{
	outContents.clear();
	outTotal = 0;
	bool read = false;
#ifdef KFC_DIAG
	if (KFC_DIAG_FAULT("book-selection"))
		read = ReadDiagSelection(bookFile, outContents, outTotal);
	else
#endif
		read = ReadPanelSelection(bookFile, outContents, outTotal);

	// THE PRODUCT'S RULE (AcquireCurrentBook::AllOrNoneSelected - the Book panel's own "selected documents" commands):
	// none or all of it selected is the whole book.
	const int32 selected = static_cast<int32>(outContents.size());
	if (!read || selected == 0 || selected >= outTotal)
	{
		outContents.clear();
		return false;
	}
	return true;
}

// End, KFCBookPanelLookup.cpp.
