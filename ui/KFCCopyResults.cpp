//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Copy Results - see KFCCopyResults.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IClipboardController.h"
#include "ICommand.h"
#include "IDataExchangeHandler.h"
#include "ITextModelCmds.h"
#include "ITextScrapData.h"

// General includes:
#include "CmdUtils.h"
#include "PMFlavorTypes.h"
#include "PMString.h"
#include "TextChar.h"
#include "WideString.h"

// Project includes:
#include "KFCCopyResults.h"
#include "KFCResultHistory.h"
#include "KFCResultNodeID.h"
#include "KFCResultTree.h"

namespace
{
	// The columns (English, as every word of the panel is - the spec map's GEN-35).
	const char* const kHeaderLine = "Document\tPage\tStatus\tText";

	struct Lines
	{
		PMString	text;
		int32		rows;

		Lines() : rows(0)
		{
			text.SetTranslatable(kFalse);
			text.Append(kHeaderLine);
		}
	};

	// One cell: the text as the list draws it, with what would break a line or a column made harmless - a tab as InDesign
	// draws one with Show Hidden Characters (U+00BB), a line break (none is left in a row: the model marks them, but a cell
	// must not end a line) as a space.
	void AppendCell(PMString& out, const PMString& cell)
	{
		const int32 count = cell.CharCount();
		for (int32 i = 0; i < count; ++i)
		{
			const UTF32TextChar c = cell.GetWChar(i);
			const uint32 v = c.GetValue();
			if (v == kTextChar_Tab)
				out.AppendW(UTF32TextChar(0x00BB));
			else if (v == kTextChar_CR || v == kTextChar_LF)
				out.Append(" ");
			else
				out.AppendW(c);
		}
	}

	// Does `s` start with `word` and a space? (the words are ASCII)
	bool StartsWithWord(const PMString& s, const char* word)
	{
		int32 n = 0;
		while (word[n] != 0)
			++n;
		if (s.CharCount() < n + 1)
			return false;
		for (int32 i = 0; i < n; ++i)
			if (s.GetWChar(i).GetValue() != static_cast<uint32>(static_cast<unsigned char>(word[i])))
				return false;
		return s.GetWChar(n).GetValue() == ' ';
	}

	// A HIT ROW'S LOCATOR, SPLIT: the words in front of its place - what the list wrote on the row ("Changed", "Outlined",
	// a special operation's word: KFCResultTree::GetRowLook) and "Missing" (the model's BuildHitLocator) - go to Status;
	// the place and the words after it ("P3(2) overset hidden locked") are the Page.
	void SplitLocator(const PMString& locator, PMString& outStatus, PMString& outPage)
	{
		outStatus.Clear();
		outStatus.SetTranslatable(kFalse);
		outPage = locator;
		outPage.SetTranslatable(kFalse);
		for (bool more = true; more; )
		{
			more = false;
			const char* word = nil;
			if (StartsWithWord(outPage, "Changed"))
				word = "Changed";
			else if (StartsWithWord(outPage, "Outlined"))
				word = "Outlined";
			else if (StartsWithWord(outPage, "Missing"))
				word = "Missing";
			else
				for (int32 op = kKFCSpecialNone + 1; op < kKFCSpecialOpCount && word == nil; ++op)
				{
					const char* special = KFCResultTree::SpecialRowWord(op);
					if (special != nil && special[0] != 0 && StartsWithWord(outPage, special))
						word = special;
				}
			if (word == nil)
				break;
			int32 n = 0;
			while (word[n] != 0)
				++n;
			outPage.Remove(0, n + 1);
			if (!outStatus.IsEmpty())
				outStatus.Append(" ");
			outStatus.Append(word);
			more = true;
		}
	}

	// A BRANCH ROW'S NAME: its label without the counts KFCResultTree::AppendCounts puts after it ("ch1.indd  (1/3)").
	PMString NameOf(const PMString& label)
	{
		PMString name(label);
		name.SetTranslatable(kFalse);
		const int32 count = name.CharCount();
		if (count > 0 && name.GetWChar(count - 1).GetValue() == ')')
			for (int32 i = count - 2; i >= 2; --i)
				if (name.GetWChar(i).GetValue() == '(' && name.GetWChar(i - 1).GetValue() == ' '
					&& name.GetWChar(i - 2).GetValue() == ' ')
				{
					name.Remove(i - 2, count - (i - 2));
					break;
				}
		return name;
	}

	// One hit row's line - Document, Page, Status (the row's words, and its accent word: "refused"), Text as the row reads:
	// before[match]after (an outlined text row's match is the characters it outlined - GetRowLook draws them there; an
	// object row's is the object's name).
	void AppendRow(Lines& out, const PMString& document, const KFCResultTree::RowLook& look)
	{
		PMString status, page;
		SplitLocator(look.locator, status, page);
		if (!look.flag.IsEmpty())
		{
			if (!status.IsEmpty())
				status.Append(" ");
			status.Append(look.flag);
		}
		out.text.Append("\r");
		AppendCell(out.text, document);
		out.text.Append("\t");
		AppendCell(out.text, page);
		out.text.Append("\t");
		AppendCell(out.text, status);
		out.text.Append("\t");
		AppendCell(out.text, look.pre);
		out.text.Append("[");
		AppendCell(out.text, look.match);
		out.text.Append("]");
		AppendCell(out.text, look.post);
		++out.rows;
	}

	// THE LIVE LIST WALKED AS THE TREE DRAWS IT (KFCResultTree::LiveRowCount / LiveRow - the adapter's own walk): the book
	// row, its documents, their stories, the rows - a row taken off the list is not among them. `only`: the rows to copy
	// (nil = all). `limit`: stop after that many rows (0 = all) - the menus' question asks for one.
	void WalkLive(const NodeID& node, const PMString& document, const KFCRowsByChapter* only, int32 limit, Lines& out)
	{
		const int32 count = KFCResultTree::LiveRowCount(node);
		for (int32 nth = 0; nth < count && (limit == 0 || out.rows < limit); ++nth)
		{
			const NodeID child(KFCResultTree::LiveRow(node, nth));
			TreeNodePtr<KFCResultNodeID> row(child);
			if (row == nil)
				continue;
			KFCResultTree::RowLook look;
			if (row->IsHitRow())
			{
				if ((only == nil || KFCResultTree::RowsHold(*only, row->GetChapter(), row->GetHit()))
					&& KFCResultTree::GetRowLook(child, look))
					AppendRow(out, document, look);
				continue;
			}
			// a document row names the rows under it; the book row and a story row pass their document on
			PMString name(document);
			if (!row->IsBookRow() && !row->IsFontRow() && KFCResultTree::GetRowLook(child, look))
				name = NameOf(look.label);
			WalkLive(child, name, only, limit, out);
		}
	}

	// Is a kept row's label a DOCUMENT row's - not a story row's ("ID<uid>: <first words>") and not the book row's
	// ("<name>.indb  (R/N)")?
	bool IsDocumentLabel(const PMString& label)
	{
		const int32 count = label.CharCount();
		if (count >= 3 && label.GetWChar(0).GetValue() == 'I' && label.GetWChar(1).GetValue() == 'D')
		{
			int32 i = 2;
			while (i < count && label.GetWChar(i).GetValue() >= '0' && label.GetWChar(i).GetValue() <= '9')
				++i;
			if (i > 2 && i < count && label.GetWChar(i).GetValue() == ':')
				return false;
		}
		const PMString name(NameOf(label));
		const int32 n = name.CharCount();
		if (n >= 5)
		{
			const char* tail = ".indb";
			bool book = true;
			for (int32 k = 0; k < 5 && book; ++k)
			{
				uint32 v = name.GetWChar(n - 5 + k).GetValue();
				if (v >= 'A' && v <= 'Z')
					v += 'a' - 'A';
				book = v == static_cast<uint32>(tail[k]);
			}
			if (book)
				return false;
		}
		return true;
	}

	// A GREY TIER'S COPY WALKED AS IT IS DRAWN (KFCResultHistory::KeptChildCount / KeptChild from its own row, -1).
	void WalkKept(int32 tier, int32 row, const PMString& document, int32 limit, Lines& out)
	{
		const int32 count = KFCResultHistory::KeptChildCount(tier, row);
		for (int32 nth = 0; nth < count && (limit == 0 || out.rows < limit); ++nth)
		{
			const int32 child = KFCResultHistory::KeptChild(tier, row, nth);
			KFCResultTree::RowLook look;
			if (child < 0 || !KFCResultHistory::GetKeptLook(tier, child, look))
				continue;
			if (KFCResultHistory::IsKeptHit(tier, child))
			{
				AppendRow(out, document, look);
				continue;
			}
			WalkKept(tier, child, IsDocumentLabel(look.label) ? NameOf(look.label) : document, limit, out);
		}
	}

	// THE TEXT ON THE CLIPBOARD - InDesign's own way (LinksUIPanelMenuComponent::CopyStringToScrap): the clipboard made ready
	// for a copy, its text flavor's handler made the active one, the text put into that handler's scrap story with one
	// insert command (the scrap's own database - no document, no undo step of the user's), the copy completed.
	bool PutOnClipboard(const PMString& text)
	{
		InterfacePtr<IClipboardController> clip(GetExecutionContextSession(), UseDefaultIID());
		if (clip == nil || !clip->IsValid())
			return false;
		clip->PrepareForCopy();
		bool put = false;
		InterfacePtr<IDataExchangeHandler> textHandler(clip->QueryHandler(kPMTextFlavor));
		if (textHandler != nil)
		{
			clip->SetActiveHandler(textHandler);
			InterfacePtr<ITextScrapData> scrapData(textHandler, UseDefaultIID());
			if (scrapData != nil)
			{
				scrapData->Clear();
				InterfacePtr<ITextModelCmds> modelCmds(scrapData->GetStoryRef(), UseDefaultIID());
				if (modelCmds != nil)
				{
					boost::shared_ptr<WideString> stringData(new WideString(text));
					InterfacePtr<ICommand> insertCmd(modelCmds->InsertCmd(0, stringData));
					put = insertCmd != nil && CmdUtils::ProcessCommand(insertCmd) == kSuccess;
				}
			}
		}
		clip->CopyHasCompleted();
		return put;
	}

	bool Finish(const Lines& lines, const char* lead, PMString& outStatus)
	{
		outStatus.Clear();
		outStatus.SetTranslatable(kFalse);
		if (lines.rows <= 0)
		{
			outStatus.Append(lead);
			outStatus.Append(": no rows to copy.");
			return false;
		}
		if (!PutOnClipboard(lines.text))
		{
			outStatus.Append(lead);
			outStatus.Append(": the clipboard could not be written - nothing was copied.");
			return false;
		}
		outStatus.Append("Copied ");
		outStatus.AppendNumber(lines.rows);
		outStatus.Append(lines.rows == 1 ? " row to the clipboard." : " rows to the clipboard.");
		return true;
	}
}

bool KFCCopyResults::CopyLiveList(PMString& outStatus)
{
	Lines lines;
	WalkLive(KFCResultNodeID::CreateRoot(), PMString(), nil, 0, lines);
	return Finish(lines, "Copy Results", outStatus);
}

bool KFCCopyResults::CopyLiveRows(const KFCRowsByChapter& rows, PMString& outStatus)
{
	Lines lines;
	WalkLive(KFCResultNodeID::CreateRoot(), PMString(), &rows, 0, lines);
	return Finish(lines, "Copy Rows", outStatus);
}

bool KFCCopyResults::CopyTier(int32 tier, PMString& outStatus)
{
	if (tier < 0)
		return CopyLiveList(outStatus);
	Lines lines;
	WalkKept(tier, -1, PMString(), 0, lines);
	return Finish(lines, "Copy These Results", outStatus);
}

bool KFCCopyResults::TierHasRows(int32 tier)
{
	Lines lines;
	if (tier < 0)
		WalkLive(KFCResultNodeID::CreateRoot(), PMString(), nil, 1, lines);
	else
		WalkKept(tier, -1, PMString(), 1, lines);
	return lines.rows > 0;
}

// End, KFCCopyResults.cpp.
