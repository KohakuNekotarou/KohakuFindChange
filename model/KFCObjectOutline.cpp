//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  An object row's Special Operations > Create Outlines - see KFCObjectOutline.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "ICommand.h"
#include "ICreateOutlinesUtils.h"	// CreateCOFrameCmd, FilterUIDList - InDesign's own Type > Create Outlines, over frames
#include "IDataBase.h"
#include "IHierarchy.h"

// General includes:
#include "CmdUtils.h"
#include "ErrorUtils.h"
#include "UIDList.h"
#include "Utils.h"

#include <utility>
#include <vector>

// Project includes:
#include "KFCBookScope.h"
#include "KFCDiag.h"
#include "KFCDiagWrittenDocs.h"	// a test build's written-document check (nothing in a shipping one)
#include "KFCID.h"				// kKFCCreateOutlinesStepKey - the Edit menu's name of the step
#include "KFCObjectOutline.h"
#include "KFCObjectSearch.h"	// IsOnSpread, Fingerprint
#include "KFCPageItemFacts.h"	// IsPageItemLockedForEdit, IsFrameOnLockedLayer, ItemIsThere
#include "KFCReplaceEngine.h"	// the write's undo step (WriteStep) and what its documents are left as (DocsAfterWrite)
#include "KFCResultModel.h"
#include "KFCRunGuard.h"
#include "KFCUndoFollow.h"

namespace
{
// WHY A ROW IS NOT OUTLINED - said in full for one row (RowSentence), counted for several (RefusalWords).
enum Refusal
{
	kRefusedNone = 0,
	kRefusedNotObject,		// not an object row (never from the panel - the menu is the object rows')
	kRefusedOutlined,		// already outlined
	kRefusedNoDoc,			// its document could not be found
	kRefusedNotOpened,		// ...or not opened
	kRefusedMissing,		// the item is gone - the row now reads Missing
	kRefusedOverset,		// no spread holds it - in overset text (KFCObjectSearch::IsOnSpread)
	kRefusedLocked,			// locked now
	kRefusedNotText,		// not a text frame (InDesign's own filter - ICreateOutlinesUtils::FilterUIDList)
	kRefusedNotWritten,		// InDesign would not make the command for it
	kRefusalCount
};

// One row's refusal, after "Create Outlines: ".
const char* RowSentence(Refusal why)
{
	switch (why)
	{
		case kRefusedNotObject:		return "this row is not an object row.";
		case kRefusedOutlined:		return "the object of this row has already been outlined.";
		case kRefusedNoDoc:			return "the document of this row could not be found.";
		case kRefusedNotOpened:		return "the document of this row could not be opened.";
		case kRefusedMissing:		return "the object is no longer in the document - search again.";
		case kRefusedOverset:		return "the object is in overset text - left as it is.";
		case kRefusedLocked:		return "the object is locked now (a locked layer, or Object > Lock) - left as it is.";
		case kRefusedNotText:		return "the object is not a text frame - only text frames can be outlined.";
		case kRefusedNotWritten:	return "InDesign would not create outlines for this object - nothing was changed.";
		default:					return "";
	}
}

// The same reasons, a few words each, for a count of rows: "2 not a text frame".
const char* RefusalWords(Refusal why)
{
	switch (why)
	{
		case kRefusedNotObject:		return "not an object row";
		case kRefusedOutlined:		return "already outlined";
		case kRefusedNoDoc:			return "document not found";
		case kRefusedNotOpened:		return "document not opened";
		case kRefusedMissing:		return "no longer in the document";
		case kRefusedOverset:		return "in overset text";
		case kRefusedLocked:		return "locked";
		case kRefusedNotText:		return "not a text frame";
		case kRefusedNotWritten:	return "not outlined by InDesign";
		default:					return "";
	}
}

// "1 locked, 2 not a text frame" - the reasons counted, in the order of the enum.
void AppendRefusals(PMString& out, const int32 (&counts)[kRefusalCount])
{
	bool first = true;
	for (int32 why = kRefusedNotObject; why < kRefusalCount; ++why)
	{
		if (counts[why] == 0)
			continue;
		if (!first)
			out.Append(", ");
		out.AppendNumber(counts[why]);
		out.Append(" ");
		out.Append(RefusalWords(static_cast<Refusal>(why)));
		first = false;
	}
}

// A refusal begins with the command's name (a Replace's rule - KFCObjectReplace.cpp).
void Refuse(PMString& out, const char* why)
{
	out = "Create Outlines: ";
	out.Append(why);
	out.SetTranslatable(kFalse);
}

// THE DOORS FOR ONE ROW (the spec's D6) - the object Replace's CheckItem (KFCObjectReplace.cpp) without the query's and
// the fingerprint's: outlines go by the item the row names. outDoc / outItem: the row's document (reached - a closed
// chapter reopened windowless, held) and its item.
Refusal CheckItem(int32 chapterIdx, int32 hitIdx, UIDRef& outDoc, UID& outItem)
{
	outItem = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	if (outItem == kInvalidUID)
		return kRefusedNotObject;
	KFCResultModel::RowDisplay row;
	if (KFCResultModel::GetHitRow(chapterIdx, hitIdx, row) && row.outlined)
		return kRefusedOutlined;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, outDoc, file))
		return kRefusedNoDoc;
	if (!KFCBookScope::ReachChapterDoc(file, outDoc))
		return kRefusedNotOpened;
	KFCResultModel::RebindChapterDoc(chapterIdx, outDoc);
	IDataBase* const db = outDoc.GetDataBase();
	bool there = false;
	if (db != nil && db->IsValidUID(outItem))
	{
		InterfacePtr<IHierarchy> hier(db, outItem, UseDefaultIID());
		there = (hier != nil);
	}
	if (!there)
	{
		KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeMissing);
		return kRefusedMissing;
	}
	if (!KFCObjectSearch::IsOnSpread(UIDRef(db, outItem)))
		return kRefusedOverset;
	if (KFCPageItemFacts::IsPageItemLockedForEdit(db, outItem) || KFCPageItemFacts::IsFrameOnLockedLayer(db, outItem))
		return kRefusedLocked;
	// A TEXT FRAME, AS INDESIGN TELLS ONE: the filter its own command runs (ICreateOutlinesUtils.h - "called by
	// CreateCOFrameCmd"), asked here so a row it would pass by is counted by its reason rather than written as nothing.
	Utils<ICreateOutlinesUtils> outlines;
	if (!outlines || outlines->FilterUIDList(UIDList(db, outItem)).Length() == 0)
		return kRefusedNotText;
	return kRefusedNone;
}

// A ROW THE WRITE OUTLINED: the row, its frame, and the outlines InDesign made of it (the command's item list once
// processed - codesnippets/SnpInspectPathInfo.cpp's way; kInvalidUID when it named none).
struct Outlined
{
	int32	row;
	UID		frame;
	UID		made;
	Outlined(int32 r, UID f, UID m) : row(r), frame(f), made(m) {}
};

// ONE DOCUMENT'S ROWS in the write: those that passed the doors, with their items, and what the write made of them.
struct OutlineChapter
{
	int32								chapterIdx;
	UIDRef								docRef;			// reached by CheckItem (a closed chapter reopened windowless, held)
	std::vector<std::pair<int32, UID> >	todo;			// (the row, its item)
	std::vector<Outlined>				written;
	int32								refusedByInDesign;	// no command made for it (CreateCOFrameCmd answered nil)
	OutlineChapter() : chapterIdx(-1), refusedByInDesign(0) {}
};
}	// anonymous namespace

bool KFCObjectOutline::CanOutlineHit(int32 chapterIdx, int32 hitIdx)
{
	// CheckItem's doors without what they do: no chapter reached (FindOpenChapterDoc only finds an open one), no row
	// rebound or marked Missing - the menu asks this every time it opens.
	const UID item = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	if (item == kInvalidUID)
		return false;
	KFCResultModel::RowDisplay row;
	if (KFCResultModel::GetHitRow(chapterIdx, hitIdx, row) && row.outlined)
		return false;
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file))
		return false;
	if (!KFCBookScope::FindOpenChapterDoc(file, docRef))
		return true;		// not open now: pressing reaches it, and its doors say why when it cannot be outlined
	IDataBase* const db = docRef.GetDataBase();
	if (!KFCPageItemFacts::ItemIsThere(db, item) || !KFCObjectSearch::IsOnSpread(UIDRef(db, item)))
		return false;
	if (KFCPageItemFacts::IsPageItemLockedForEdit(db, item) || KFCPageItemFacts::IsFrameOnLockedLayer(db, item))
		return false;
	Utils<ICreateOutlinesUtils> outlines;
	return outlines && outlines->FilterUIDList(UIDList(db, item)).Length() > 0;
}

bool KFCObjectOutline::OutlineRows(const KFCRowsByChapter& rows, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("object-outline-rows");	// (test builds: when it is over - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	if (KFCRunGuard::IsAnyRunning())
	{
		outStatus.Append(KFCRunGuard::BusyMessage());
		return false;
	}
	const size_t total = KFCRowCount(rows);
	if (total == 0)
		return false;
	Utils<ICreateOutlinesUtils> outlines;
	if (!outlines)
	{
		Refuse(outStatus, "InDesign's Create Outlines could not be reached - nothing was changed.");
		return false;
	}

	// THE DOORS, ROW BY ROW: the rows that pass are written, the rest counted by their reason.
	int32 counts[kRefusalCount] = {};
	Refusal lastWhy = kRefusedNone;
	std::vector<OutlineChapter> chapters(rows.size());
	KFCReplaceEngine::DocsAfterWrite docsAfter;		// what each document is left as, on every way out from here
	size_t toWrite = 0;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		OutlineChapter& chapter = chapters[c];
		chapter.chapterIdx = rows[c].chapter;
		for (size_t k = 0; k < rows[c].hits.size(); ++k)
		{
			UID item = kInvalidUID;
			const Refusal why = CheckItem(chapter.chapterIdx, rows[c].hits[k], chapter.docRef, item);
			if (why != kRefusedNone)
			{
				++counts[why];
				lastWhy = why;
				continue;
			}
			chapter.todo.push_back(std::make_pair(rows[c].hits[k], item));
		}
		toWrite += chapter.todo.size();
		// Its "unsaved" flag read now when it has rows to write (a document whose flag was not read is never made clean).
		if (chapter.docRef.GetDataBase() != nil)
			docsAfter.Reached(chapter.docRef, !chapter.todo.empty());
	}
	if (toWrite == 0)
	{
		if (total == 1)
			Refuse(outStatus, RowSentence(lastWhy));
		else
		{
			outStatus = "Create Outlines: none of the ";
			outStatus.AppendNumber(static_cast<int32>(total));
			outStatus.Append(" selected rows can be outlined - ");
			AppendRefusals(outStatus, counts);
			outStatus.Append(".");
			outStatus.SetTranslatable(kFalse);
		}
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last row is checked (the object Replace's rule - KFCObjectReplace.cpp).
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (!chapters[c].todo.empty() && !KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			Refuse(outStatus, "a document of the selected rows has been closed - nothing was changed.");
			return false;
		}
	}

	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (D7): one step, every frame read before the write - its Undo brings the
	// frames back, its Redo takes them away again (KFCUndoFollow: an item the write took away).
	size_t documents = 0;
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (chapters[c].todo.empty())
			continue;
		++documents;
		chapterIdxs.push_back(chapters[c].chapterIdx);
	}
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	recorder.SetName("Create Outlines");
	for (size_t c = 0; c < chapters.size(); ++c)
		for (size_t k = 0; k < chapters[c].todo.size(); ++k)
			recorder.RecordItem(chapters[c].chapterIdx, chapters[c].todo[k].second);
	KFCReplaceEngine::WriteStep step;
	if (!KFCReplaceEngine::BeginWriteStep(documents > 1, step, kKFCCreateOutlinesStepKey, "KFC Create Outlines"))
	{
		Refuse(outStatus, "InDesign would not start a command sequence - nothing was changed.");
		return false;
	}
	// THE WRITE: each row's frame outlined by InDesign's own command - the frame replaced by its outlines (D4) - one
	// command per frame, so each row learns its own outlines from the command's item list. An error standing ends the
	// step by it: the whole step goes back, and no row is marked over a write InDesign takes back.
	bool errorStands = false;
	size_t writtenCount = 0;
	for (size_t c = 0; c < chapters.size() && !errorStands; ++c)
	{
		OutlineChapter& chapter = chapters[c];
		IDataBase* const db = chapter.docRef.GetDataBase();
		for (size_t k = 0; k < chapter.todo.size(); ++k)
		{
			InterfacePtr<ICommand> cmd(outlines->CreateCOFrameCmd(UIDList(db, chapter.todo[k].second), kTrue));
			if (cmd == nil)
			{
				++chapter.refusedByInDesign;
				continue;
			}
			const ErrorCode done = CmdUtils::ProcessCommand(cmd);
			if (done != kSuccess || ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
			{
				errorStands = true;
				break;
			}
			const UIDList& made = cmd->GetItemListReference();
			chapter.written.push_back(Outlined(chapter.todo[k].first, chapter.todo[k].second,
				made.Length() > 0 ? made[0] : kInvalidUID));
		}
		writtenCount += chapter.written.size();
	}
	const bool ok = writtenCount > 0 && !errorStands;
	if (ok)
		for (size_t c = 0; c < chapters.size(); ++c)
			if (!chapters[c].written.empty())
				KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());		// in this step, so its Undo / Redo is heard
	// Kept, or taken back whole when nothing was written or an error stands; the "unsaved" flags are DocsAfterWrite's.
	KFCReplaceEngine::EndWriteStep(step, ok);
	for (size_t c = 0; c < chapters.size(); ++c)
		counts[kRefusedNotWritten] += chapters[c].refusedByInDesign;
	if (!ok)
	{
		KFCResultModel::RollBackRows();
		if (errorStands)
			Refuse(outStatus, "InDesign's Create Outlines stopped with an error - nothing was changed.");
		else
		{
			outStatus = "Create Outlines: nothing was changed - ";
			AppendRefusals(outStatus, counts);
			outStatus.Append(".");
			outStatus.SetTranslatable(kFalse);
		}
		return false;
	}
	// D8: each written row Outlined, naming its outlines (their fingerprint with them), its eye on; the documents told.
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const OutlineChapter& chapter = chapters[c];
		if (chapter.written.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		for (size_t k = 0; k < chapter.written.size(); ++k)
		{
			const Outlined& o = chapter.written[k];
			uint64 print = 0;
			uint32 length = 0;
			if (o.made != kInvalidUID)
				(void)KFCObjectSearch::Fingerprint(UIDRef(db, o.made), print, length);
			KFCResultModel::MarkItemOutlined(chapter.chapterIdx, o.row, o.made, print, length);
		}
		KFC_DIAG_NOTE_WRITE(chapter.docRef);
		docsAfter.Wrote(chapter.docRef);		// a chapter of ours has to be seen and saved
	}
	recorder.Keep();

	// D10: "Outlined 3 objects: ID:259, ID:260, ID:261." - the frames' IDs, up to kNamedIDs; several documents named before
	// their IDs; the rows left as they were counted after.
	const size_t kNamedIDs = 8;
	outStatus = "Outlined ";
	outStatus.AppendNumber(static_cast<int32>(writtenCount));
	if (writtenCount < total)
	{
		outStatus.Append(" of ");
		outStatus.AppendNumber(static_cast<int32>(total));
	}
	outStatus.Append(writtenCount == 1 && total == 1 ? " object" : " objects");
	size_t writtenDocuments = 0;
	for (size_t c = 0; c < chapters.size(); ++c)
		if (!chapters[c].written.empty())
			++writtenDocuments;
	if (writtenDocuments > 1)
	{
		outStatus.Append(" in ");
		outStatus.AppendNumber(static_cast<int32>(writtenDocuments));
		outStatus.Append(" documents");
	}
	outStatus.Append(": ");
	size_t named = 0;
	bool firstDocument = true;
	for (size_t c = 0; c < chapters.size() && named < kNamedIDs; ++c)
	{
		const OutlineChapter& chapter = chapters[c];
		if (chapter.written.empty())
			continue;
		if (writtenDocuments > 1)
		{
			if (!firstDocument)
				outStatus.Append("; ");
			PMString docName;
			int32 rowsNow = 0;
			if (KFCResultModel::GetChapterDisplay(chapter.chapterIdx, docName, rowsNow))
			{
				outStatus.Append(docName);
				outStatus.Append(" ");
			}
		}
		firstDocument = false;
		for (size_t k = 0; k < chapter.written.size() && named < kNamedIDs; ++k)
		{
			if (k > 0 || (writtenDocuments <= 1 && named > 0))
				outStatus.Append(", ");
			outStatus.Append("ID:");
			outStatus.AppendNumber(static_cast<int32>(chapter.written[k].frame.Get()));
			++named;
		}
	}
	if (writtenCount > kNamedIDs)
		outStatus.Append(", ...");
	outStatus.Append(".");
	if (writtenCount < total)
	{
		outStatus.Append(" Left as they were: ");
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
	}
	outStatus.SetTranslatable(kFalse);
	return true;
}

// End, KFCObjectOutline.cpp.
