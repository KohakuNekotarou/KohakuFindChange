//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The Object tab's writes - see KFCObjectReplace.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IFindChangeOptions.h"		// kObjectSearch, kAllFrames - a Frames list's Replace states the Object tab's find side
#include "IObjectWalker.h"

// General includes:
#include "ErrorUtils.h"

#include <set>
#include <utility>
#include <vector>

// Project includes:
#include "KFCBookScope.h"
#include "KFCDiag.h"
#include "KFCDiagWrittenDocs.h"	// a test build's written-document check (nothing in a shipping one)
#include "KFCObjectReplace.h"
#include "KFCObjectSearch.h"
#include "KFCPageItemFacts.h"
#include "KFCModelNotify.h"	// KFCNotifyStatus - Find/Change not put back after a Frames list's write
#include "KFCQueryCopy.h"		// a Frames list's Replace: the Object tab read, stated for the write, put back
#include "KFCQueryScope.h"		// KFCFindChangeNotPutBack - the sentence every failed put-back says
#include "KFCReplaceEngine.h"	// RefuseChangedQuery, CanReplaceHit, WhyGreyed; the write's undo step (WriteStep) and
								// what its documents are left as (DocsAfterWrite)
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"	// KFCForwardSearchScope
#include "KFCUndoFollow.h"

namespace
{
// A refusal begins with the command's name (the text Replace's rule).
void Refuse(PMString& out, const char* why)
{
	out = "Replace: ";
	out.Append(why);
	out.SetTranslatable(kFalse);
}

// WHY A ROW IS NOT WRITTEN - said in full for one row (RowSentence), counted for several (RefusalWords).
enum Refusal
{
	kRefusedNone = 0,
	kRefusedRow,			// already replaced, locked when found, or not a match of Find/Change (CanReplaceHit) - one row's
							// sentence; rows selected together are counted by the row's own reason (GreyedRefusal)
	kRefusedReplaced,		// already replaced (rows selected together)
	kRefusedNoDoc,			// its document could not be found
	kRefusedNotOpened,		// ...or not opened
	kRefusedMissing,		// the item is gone - the row now reads Missing
	kRefusedOverset,		// no spread holds it - in overset text (KFCObjectSearch::IsOnSpread)
	kRefusedLocked,			// locked now
	kRefusedEdited,			// changed since the search (its fingerprint)
	kRefusedNotReached,		// InDesign's walk never came to it - no longer a match
	kRefusedWalkFailed,		// InDesign's walk broke off with an error before it came to it - nobody looked
	kRefusedNotWritten,		// InDesign's replace would not run on it
	kRefusalCount
};

// One row's refusal, after "Replace: ".
const char* RowSentence(Refusal why)
{
	switch (why)
	{
		case kRefusedRow:			return "this row cannot be replaced (already replaced, locked, or not a match of Find/Change).";
		case kRefusedReplaced:		return "this row has already been replaced.";
		case kRefusedNoDoc:			return "the document of this row could not be found.";
		case kRefusedNotOpened:		return "the document of this row could not be opened.";
		case kRefusedMissing:		return "the object is no longer in the document - search again.";
		case kRefusedOverset:		return "the object is in overset text - left as it is.";
		case kRefusedLocked:		return "the object is locked now (a locked layer, or Object > Lock) - left as it is.";
		case kRefusedEdited:		return "the object has changed since the search (moved, restyled or edited) - search again.";
		case kRefusedNotReached:	return "the object no longer matches the Find/Change settings - search again.";
		case kRefusedWalkFailed:	return "InDesign's search stopped with an error before it reached the object - search again.";
		case kRefusedNotWritten:	return "InDesign's replace would not run on this object - nothing was changed.";
		default:					return "";
	}
}

// The same reasons, a few words each, for a count of rows: "2 locked".
const char* RefusalWords(Refusal why)
{
	switch (why)
	{
		case kRefusedRow:			return "not an object row";		// (never - rows of one kind are selected together)
		case kRefusedReplaced:		return "already replaced";
		case kRefusedNoDoc:			return "document not found";
		case kRefusedNotOpened:		return "document not opened";
		case kRefusedMissing:		return "no longer in the document";
		case kRefusedOverset:		return "in overset text";
		case kRefusedLocked:		return "locked";
		case kRefusedEdited:		return "changed since the search";
		case kRefusedNotReached:	return "no longer matching";
		case kRefusedWalkFailed:	return "not reached - InDesign's search stopped with an error";
		case kRefusedNotWritten:	return "not replaced by InDesign";
		default:					return "";
	}
}

// "1 locked, 2 changed since the search" - the reasons counted, in the order of the enum.
void AppendRefusals(PMString& out, const int32 (&counts)[kRefusalCount])
{
	bool first = true;
	for (int32 why = kRefusedRow; why < kRefusalCount; ++why)
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

// "Replace: none of the 5 selected rows can be replaced - 2 locked, 3 changed since the search." - rows selected together
// of which none would be written (CheckRowsNow asks, ReplaceRows finds).
void SayNoneReplaceable(PMString& out, size_t total, const int32 (&counts)[kRefusalCount])
{
	out = "Replace: none of the ";
	out.AppendNumber(static_cast<int32>(total));
	out.Append(" selected rows can be replaced - ");
	AppendRefusals(out, counts);
	out.Append(".");
	out.SetTranslatable(kFalse);
}

// O10 steps 1-2, asked once for a Replace: the settings the search ran with (a changed query CLEARS the results - the
// author's rule for text, kept), and something to change to. false = refused, outStatus says why.
// A FRAMES OF THESE MATCHES LIST (KFCResultModel::ListIsFrames - the narrow-down spec's N6): its rows were found by a text
// search, so no object query of the search is asked; what is asked is the Object tab's change side (HasChangeObjectFormat
// reads that tab whichever one Find/Change shows), which the write uses (FramesObjectQuery).
bool CheckSettings(PMString& outStatus)
{
	if (KFCResultModel::ListIsFrames())
	{
		if (!KFCObjectSearch::HasChangeObjectFormat())
		{
			Refuse(outStatus, "nothing to change to - set Change Object Format or an object style on the Object tab of Edit > Find/Change.");
			return false;
		}
		return true;
	}
	PMString refusal;
	if (KFCReplaceEngine::RefuseChangedQuery(refusal))
	{
		outStatus = "Replace: ";
		outStatus.Append(refusal);
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	if (!KFCObjectSearch::HasChangeObjectFormat())
	{
		Refuse(outStatus, "nothing to change to - set Change Object Format or an object style in Edit > Find/Change.");
		return false;
	}
	return true;
}

// O10 steps 3-5 for one row whose Replace is not greyed. outDoc / outItem: the row's document (reached - a closed chapter
// reopened windowless) and its item.
Refusal CheckItem(int32 chapterIdx, int32 hitIdx, UIDRef& outDoc, UID& outItem)
{
	outItem = KFCResultModel::GetHitItem(chapterIdx, hitIdx);
	// 3. The item, still in its document - by the chapter's file first (KFCBookScope::ReachChapterDoc).
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, outDoc, file))
		return kRefusedNoDoc;
	if (!KFCBookScope::ReachChapterDoc(file, outDoc))
		return kRefusedNotOpened;
	KFCResultModel::RebindChapterDoc(chapterIdx, outDoc);
	IDataBase* const db = outDoc.GetDataBase();
	if (!KFCResultModel::RowItemIsThere(chapterIdx, hitIdx, db))
	{
		KFCResultModel::SetHitOutcome(chapterIdx, hitIdx, KFCResultModel::kOutcomeMissing);
		return kRefusedMissing;
	}
	// 3b. On a spread: an item in overset text is not written. A row's Replace brings the view to its
	// item and leaves it selected (KFCJump::ReplaceObjectRow), and nothing of an overset one is laid out to show or
	// select (KFCObjectSearch::IsOnSpread).
	if (!KFCObjectSearch::IsOnSpread(UIDRef(db, outItem)))
		return kRefusedOverset;
	// 4. Not locked (Object > Lock, an insert lock, a locked layer).
	if (KFCPageItemFacts::IsPageItemLockedForEdit(db, outItem) || KFCPageItemFacts::IsFrameOnLockedLayer(db, outItem))
		return kRefusedLocked;
	// 5. As the search found it - or as KFC last wrote it (O12): any change its fingerprint shows refuses.
	uint64 was = 0, now = 0;
	uint32 wasLength = 0, nowLength = 0;
	if (!KFCResultModel::GetHitItemPrint(chapterIdx, hitIdx, was, wasLength)
		|| !KFCObjectSearch::Fingerprint(UIDRef(db, outItem), now, nowLength) || was != now || wasLength != nowLength)
		return kRefusedEdited;
	return kRefusedNone;
}

// Is the row's Replace greyed (O10's door before step 1 - the text rows' CanReplaceHit)?
bool RowIsReplaceable(int32 chapterIdx, int32 hitIdx)
{
	return KFCResultModel::GetHitItem(chapterIdx, hitIdx) != kInvalidUID
		&& KFCReplaceEngine::CanReplaceHit(chapterIdx, hitIdx);
}

// A greyed row of several selected together, counted by the reason its row shows (the author's call, as the text rows
// are: KFCReplaceEngine::WhyGreyed). An item that went missing reads as one "no longer in the document", one
// locked as "locked" - the words a row found so by the write itself is counted with.
Refusal GreyedRefusal(int32 chapterIdx, int32 hitIdx)
{
	if (KFCResultModel::GetHitItem(chapterIdx, hitIdx) == kInvalidUID)
		return kRefusedRow;
	switch (KFCReplaceEngine::WhyGreyed(chapterIdx, hitIdx))
	{
		case KFCReplaceEngine::kGreyedReplaced:	return kRefusedReplaced;
		case KFCReplaceEngine::kGreyedMissing:	return kRefusedMissing;
		case KFCReplaceEngine::kGreyedLocked:	return kRefusedLocked;
		case KFCReplaceEngine::kGreyedRefused:	return kRefusedNotWritten;
		default:								return kRefusedRow;
	}
}

// O10 steps 1-5 for one row, in the order a row's Replace has always asked them.
bool CheckRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus, UIDRef& outDoc, UID& outItem)
{
	outItem = kInvalidUID;
	if (!RowIsReplaceable(chapterIdx, hitIdx))
	{
		Refuse(outStatus, RowSentence(kRefusedRow));
		return false;
	}
	if (!CheckSettings(outStatus))
		return false;
	const Refusal why = CheckItem(chapterIdx, hitIdx, outDoc, outItem);
	if (why != kRefusedNone)
	{
		Refuse(outStatus, RowSentence(why));
		return false;
	}
	return true;
}

}	// anonymous namespace

// THE ROWS A WRITE OF KFC'S OWN MOVES BESIDES ITS OWN (spec OBJ-12; the author's call). A fingerprint holds more
// than its item (KFCObjectSearch::WriteReach), so a write moved the print of rows it did not write - the text frame an inline
// written hangs in, the group holding an item written - and their Replace then refused them as "changed since the
// search", the change being KFC's own. BEFORE the write: the rows of the chapter the write can reach that read now as their
// row holds them - the rows to be written among them, which CheckItem has just read so. AFTER it, each of them not written
// takes the print the write left (FollowRowsTheWriteMoved). A row that read otherwise before the write was changed by
// someone else, and its Replace refuses it still. (In the namespace: a special operation's write follows them too -
// KFCObjectSpecial.)
void KFCObjectReplace::RowsTheWriteCanMove(int32 chapterIdx, IDataBase* db, const std::set<UID>& toWrite, RowItems& out)
{
	out.clear();
	if (db == nil)
		return;
	KFCObjectSearch::WriteReach reach;
	KFCObjectSearch::ReachOfWrite(db, toWrite, reach);
	const int32 rows = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 h = 0; h < rows; ++h)
	{
		const UID item = KFCResultModel::GetHitItem(chapterIdx, h);
		if (item == kInvalidUID || !RowIsReplaceable(chapterIdx, h))
			continue;
		if (toWrite.count(item) > 0)
		{
			out.push_back(std::make_pair(h, item));		// as its row holds it: CheckItem has just asked
			continue;
		}
		if (!KFCObjectSearch::WriteReaches(db, reach, item))
			continue;
		uint64 was = 0, now = 0;
		uint32 wasLength = 0, nowLength = 0;
		// (A test build's fault switch: every row the write can reach taken, changed or not - an edit of someone else's
		//  would then be given the write's print and written over - case obj-f2-edited-refused-armed.)
		if (KFC_DIAG_FAULT("object-prints-blind")
			|| (KFCResultModel::GetHitItemPrint(chapterIdx, h, was, wasLength)
				&& KFCObjectSearch::Fingerprint(UIDRef(db, item), now, nowLength) && was == now && wasLength == nowLength))
			out.push_back(std::make_pair(h, item));
	}
	KFC_DIAG_LOG("OBJPRINTS before chapter=%d writing=%u holders=%u stories=%u watched=%u", chapterIdx,
		(unsigned)toWrite.size(), (unsigned)reach.items.size(), (unsigned)reach.stories.size(), (unsigned)out.size());
}

void KFCObjectReplace::FollowRowsTheWriteMoved(int32 chapterIdx, IDataBase* db, const RowItems& rows,
	const std::set<UID>& written)
{
	// (A test build's fault switch: the other rows left as they were, the old way - case obj-f2-inline-armed.)
	if (db == nil || KFC_DIAG_FAULT("object-prints-stale"))
		return;
	int32 followed = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (written.count(rows[k].second) > 0)
			continue;		// Changed, with its own new print (MarkItemReplaced)
		uint64 was = 0, now = 0;
		uint32 wasLength = 0, nowLength = 0;
		// Nothing of the item can be read now: the row keeps its print, and its Replace refuses it as before.
		if (!KFCResultModel::GetHitItemPrint(chapterIdx, rows[k].first, was, wasLength)
			|| !KFCObjectSearch::Fingerprint(UIDRef(db, rows[k].second), now, nowLength))
			continue;
		if (now == was && nowLength == wasLength)
			continue;
		KFCResultModel::SetHitItemPrint(chapterIdx, rows[k].first, now, nowLength);
		++followed;
	}
	KFC_DIAG_LOG("OBJPRINTS after chapter=%d followed=%d", chapterIdx, followed);
}

// WHAT A WRITE OF OBJECT ROWS DID (O10's message - the object search spec). (In the namespace: Create Outlines' and the
// other Special Operations' writes say it too - KFCObjectOutline, KFCObjectSpecial.)
void KFCObjectReplace::SayItemsWritten(PMString& out, const PMString& head, size_t count, size_t total,
	const ItemsWritten& written, const PMString& afterCount, const char* beforeIDs, const PMString& left)
{
	const size_t kNamedIDs = 8;
	const bool severalDocuments = written.size() > 1;
	out = head;
	out.AppendNumber(static_cast<int32>(count));
	if (count < total)
	{
		out.Append(" of ");
		out.AppendNumber(static_cast<int32>(total));
	}
	out.Append(count == 1 && total == 1 ? " object" : " objects");
	if (severalDocuments)
	{
		out.Append(" in ");
		out.AppendNumber(static_cast<int32>(written.size()));
		out.Append(" documents");
	}
	out.Append(afterCount);
	out.Append(beforeIDs);
	size_t named = 0;
	for (size_t c = 0; c < written.size() && named < kNamedIDs; ++c)
	{
		if (severalDocuments)
		{
			if (c > 0)
				out.Append("; ");
			PMString docName;
			int32 rowsNow = 0;
			if (KFCResultModel::GetChapterDisplay(written[c].first, docName, rowsNow))
			{
				out.Append(docName);
				out.Append(" ");
			}
		}
		const std::vector<UID>& items = written[c].second;
		for (size_t k = 0; k < items.size() && named < kNamedIDs; ++k)
		{
			if (k > 0 || (!severalDocuments && named > 0))
				out.Append(", ");
			out.Append("ID:");
			out.AppendNumber(static_cast<int32>(items[k].Get()));
			++named;
		}
	}
	if (count > kNamedIDs)
		out.Append(", ...");
	out.Append(".");
	if (count < total)
	{
		out.Append(" Left as they were: ");
		out.Append(left);
		out.Append(".");
	}
	out.SetTranslatable(kFalse);
}

namespace
{
using KFCObjectReplace::RowItems;
using KFCObjectReplace::RowsTheWriteCanMove;
using KFCObjectReplace::FollowRowsTheWriteMoved;

// ONE STEP OF A WRITE'S WALK (steps 7-8 - the one write, WriteObjectRows): InDesign's own object search moved on to its next
// match in the walker's document - true with that item in outItem. False when the walk is over by itself, as the search's
// ends (spec O5) - InDesign has no more, an item comes round again, or the walker stands in another document - or when it
// broke off (outWalkFailed): KFCObjectSearch::ReadWalkStep reads any answer with the error state raised so, and the error
// is left standing for the write's step to end by - nothing is written over it.
// hiddenSpreads: the document holds a hidden spread - the step is made under the Answering (OBJ-23), so a match there
// before the rows' items stops nothing.
bool NextItemOfWalk(IFindChangeService* svc, IObjectWalker* shared, IDataBase* db, bool hiddenSpreads,
	std::set<UID>& seen, UID& outItem, bool& outWalkFailed)
{
	// (The match is the shared walker's current item - the call's OUT argument stays empty, measured.)
	const KFCObjectSearch::WalkStep at = KFCObjectSearch::ReadWalkStep(
		KFCObjectSearch::CallSearchObject(svc, db, hiddenSpreads, kFalse));
	if (at != KFCObjectSearch::kWalkStepOn)
	{
		outWalkFailed = (at == KFCObjectSearch::kWalkStepBroke);
		return false;
	}
	const UIDRef current(shared->GetCurrentItem());
	if (current.GetDataBase() != db || !seen.insert(current.GetUID()).second)
		return false;
	outItem = current.GetUID();
	return true;
}

// A chapter of ours that a check reopened goes back when the check refuses (KFCBookScope::HandBackIfHeld - the UI half's
// landings hand back the same way). The writes are reached only through the UI half's landing (KFCJump::ReplaceObjectRow,
// ReplaceObjectRows), which has brought the row's chapter forward first - for rows selected together, the chapter of the
// row selected last - and which hands back on its own way out whatever an exit of the write's leaves
// (HandBackChapterOnExit in KFCJump.cpp). The chapter brought forward is then the user's, no
// longer held; what every chapter a write reaches is left as - the one brought forward included, of which only its
// "unsaved" flag can change - is DocsAfterWrite's (both doors - WriteObjectRows).
using KFCBookScope::HandBackIfHeld;

// ONE DOCUMENT'S OBJECT ROWS in a Replace - one row's (ReplaceRow) or rows selected together (CheckRowsNow / ReplaceRows):
// its rows that pass the checks, with their items, and what the write did to them. (What the document is left as
// afterwards is the text rows' rule - KFCReplaceEngine::DocsAfterWrite.)
struct ObjectChapterWrite
{
	int32								chapterIdx;
	UIDRef								docRef;			// reached by CheckItem (a closed chapter reopened windowless, held)
	std::vector<std::pair<int32, UID> >	todo;			// (the row, its item)
	std::set<UID>						wanted;			// todo's items (WriteObjectRows)
	std::set<UID>						met;			// todo's items the walk stood on - InDesign's replace run on each,
														// written or not
	std::set<UID>						written;
	std::set<UID>						refusedByInDesign;	// met, and InDesign's replace would not run on it
	bool								walkFailed;		// the walk broke off before every item was met (ReadWalkStep)
	RowItems							watched;		// the rows its write can move, read before it (RowsTheWriteCanMove)
	ObjectChapterWrite() : chapterIdx(-1), walkFailed(false) {}
};

// WHAT A WRITE OF OBJECT ROWS CAME TO (WriteObjectRows) - for its door to say in its own words.
struct ObjectWriteResult
{
	bool	started;		// false = it never began: InDesign's object search not reached, or no command sequence (the write
							// said why)
	bool	ok;				// kept: something written, and no error standing
	bool	errorStands;	// InDesign left the error state raised - the step went back whole
	bool	searchError;	// ...raised by the walk's search rather than by a replace
	size_t	writtenCount;
	ObjectWriteResult() : started(false), ok(false), errorStands(false), searchError(false), writtenCount(0) {}
};

// A FRAMES OF THESE MATCHES LIST'S WRITE, AIMED (the narrow-down spec's N6 - M5): the walk reaches a row's item by InDesign's
// own object matching, which goes by the Object tab's find side; a Frames list's frames were found by a text search - so for
// the length of the write the Object tab is stated with nothing on its find side (no Find Object Format, no object style),
// Type All Frames, and the Include switches of the list's own query (the walk then reaches every frame the text search could),
// its change side as the person left it; then the Object tab put back as it was, and the tab Find/Change showed (KFCQueryCopier
// - KFCQueryScope's way: Apply, then Matches; what cannot be put back is said). A list of any other kind: nothing is stated.
class FramesObjectQuery
{
public:
	FramesObjectQuery() : fStated(false), fRefused(false)
	{
		fWhy.SetTranslatable(kFalse);
		if (!KFCResultModel::ListIsFrames())
			return;
		KFCQueryCopier::Capture(fBeforeNow);
		KFCQueryCopier::CaptureMode(fBeforeObject, IFindChangeOptions::kObjectSearch);
		if (!fBeforeNow.valid || !fBeforeObject.valid)
		{
			fRefused = true;
			fWhy = "the Find/Change settings could not be read - nothing was changed.";
			return;
		}
		KFCQueryCopy frames(fBeforeObject);
		frames.findAttrs.reset();
		frames.objectFindStyle = kInvalidUID;
		frames.objectType = IFindChangeOptions::kAllFrames;
		const std::shared_ptr<const KFCQueryCopy> list(KFCResultModel::GetQueryCopy());
		if (list != nullptr && list->valid)
		{
			frames.includeLockedStories = list->includeLockedStories;
			frames.includeLockedLayers = list->includeLockedLayers;
			frames.includeHiddenLayers = list->includeHiddenLayers;
			frames.includeMasterPages = list->includeMasterPages;
			frames.includeFootnotes = list->includeFootnotes;
		}
		fStated = true;		// (whatever Apply did before a failure is put back by the destructor)
		PMString what;
		if (!KFCQueryCopier::Apply(frames, what) || !KFCQueryCopier::Matches(frames, what))
		{
			fRefused = true;
			fWhy = "the Object tab could not be set for the frames (";
			fWhy.Append(what);
			fWhy.Append(") - nothing was changed.");
		}
		KFC_DIAG_LOG("FRAMESREPLACE stated=%d refused=%d beforeMode=%d", fStated ? 1 : 0, fRefused ? 1 : 0, (int)fBeforeNow.mode);
	}
	~FramesObjectQuery()
	{
		if (!fStated)
			return;
		PMString what;
		bool back = KFCQueryCopier::Apply(fBeforeObject, what) && KFCQueryCopier::Matches(fBeforeObject, what);
		if (back && fBeforeNow.mode != fBeforeObject.mode)
			back = KFCQueryCopier::Apply(fBeforeNow, what) && KFCQueryCopier::Matches(fBeforeNow, what);
		KFC_DIAG_LOG("FRAMESREPLACE put back=%d (%s)", back ? 1 : 0, what.GetUTF8String().c_str());
		if (!back)
			KFCNotifyStatus(KFCFindChangeNotPutBack(what));
	}
	bool Ready(PMString& outWhy) const
	{
		if (!fRefused)
			return true;
		outWhy = "Replace: ";
		outWhy.Append(fWhy);
		outWhy.SetTranslatable(kFalse);
		return false;
	}
private:
	bool			fStated;
	bool			fRefused;
	PMString		fWhy;
	KFCQueryCopy	fBeforeNow;
	KFCQueryCopy	fBeforeObject;
	FramesObjectQuery(const FramesObjectQuery&);
	FramesObjectQuery& operator=(const FramesObjectQuery&);
};

// THE WRITE (steps 6-8, 10, 11) - ONE for one row's Replace and for rows selected together, behind two doors that check
// their rows first: the text rows' shape (KFCReplaceEngine's ReplaceHit and ReplaceHits, both writing through
// ReplaceRowsNow). The doors have reached each row's document and listed the rows that pass (each chapter's todo); what
// each document is left as is docsAfter's, told here of each one written. ONE undo step for every document written, with
// KFC's undo mark in it - the text rows' rule (KFCReplaceEngine.h, WriteStep: one document's a plain sequence, several
// documents' one abortable sequence around them all); every row's item walked to by InDesign's own matching - one walk of
// each document from its first match - and replaced as the walk stands on it, its own Change/Find item after item; the
// step kept only when something was written and no error stands - each written row then Changed with its item's new
// fingerprint, the rows the write moved besides given the prints it left (spec OBJ-12), and the step kept for the panel's
// following of Undo (O11). The words are the doors': outStatus is written here only when the write never began.
ObjectWriteResult WriteObjectRows(std::vector<ObjectChapterWrite>& chapters, KFCReplaceEngine::DocsAfterWrite& docsAfter,
	PMString& outStatus)
{
	ObjectWriteResult result;
	// WHERE THE VIEWS STOOD, put back as the write ends (OBJ-06 - KFCObjectSearch::ViewKeeper): the walk scrolls past
	// every match before a row's item. Taken after the door put its row's item in view - so the view stays on it - or,
	// on a hidden spread, left where it was.
	const KFCObjectSearch::ViewKeeper views;
	// A Frames list's write: the Object tab aimed at its frames for the length of it (FramesObjectQuery - before any
	// command sequence below begins, put back after the last one ends).
	const FramesObjectQuery framesQuery;
	if (!framesQuery.Ready(outStatus))
		return result;
	size_t documents = 0;
	for (size_t c = 0; c < chapters.size(); ++c)
		if (!chapters[c].todo.empty())
			++documents;
	InterfacePtr<IFindChangeService> svc(KFCObjectSearch::CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(KFCObjectSearch::QuerySharedWalker());
	if (svc == nil || shared == nil)
	{
		Refuse(outStatus, "InDesign's object search could not be reached - nothing was changed.");
		return result;
	}
	// The rows each document's write can move besides its own, read before it (RowsTheWriteCanMove).
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		ObjectChapterWrite& chapter = chapters[c];
		chapter.wanted.clear();
		for (size_t k = 0; k < chapter.todo.size(); ++k)
			chapter.wanted.insert(chapter.todo[k].second);
		if (!chapter.wanted.empty())
			RowsTheWriteCanMove(chapter.chapterIdx, chapter.docRef.GetDataBase(), chapter.wanted, chapter.watched);
	}
	// RECORDED FOR THE PANEL'S FOLLOWING OF UNDO (O11): one step, every item's fingerprint read before the write - of
	// every document it writes.
	std::vector<int32> chapterIdxs;
	for (size_t c = 0; c < chapters.size(); ++c)
		if (!chapters[c].todo.empty())
			chapterIdxs.push_back(chapters[c].chapterIdx);
	KFCUndoFollow::StepRecorder recorder(chapterIdxs);
	for (size_t c = 0; c < chapters.size(); ++c)
		for (size_t k = 0; k < chapters[c].todo.size(); ++k)
			recorder.RecordItem(chapters[c].chapterIdx, chapters[c].todo[k].second);
	KFCReplaceEngine::WriteStep step;
	if (!KFCReplaceEngine::BeginWriteStep(documents > 1, step))
	{
		Refuse(outStatus, "InDesign would not start a command sequence - nothing was changed.");
		return result;
	}
	result.started = true;
	const bool walkMisses = KFC_DIAG_FAULT("object-walk-miss");	// (a test build's: no item reached - oca-walk-miss)
	for (size_t c = 0; c < chapters.size() && !result.errorStands; ++c)
	{
		ObjectChapterWrite& chapter = chapters[c];
		if (chapter.todo.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		// 7-8 FOR EVERY ROW IN ONE WALK of its document: InDesign's own matching from the document's first match, and its
		// replace on each row's item as the walk stands on it - its own Change/Find, item after item. The walk ends when
		// every item has been met, or by itself (NextItemOfWalk).
		// A HIDDEN SPREAD IN THE DOCUMENT (OBJ-23): every call made under the Answering - a match there, a row's item
		// there too, hands its item back and leaves the spread hidden. Asked once: the walk hides or shows no spread.
		shared->Initialize(KFCObjectSearch::WalkerOptionsFor(chapter.docRef, nil));
		const bool hiddenSpreads = KFCPageItemFacts::HasHiddenSpread(chapter.docRef);
		std::set<UID> seen;
		UID at = kInvalidUID;
		while (!walkMisses && NextItemOfWalk(svc, shared, db, hiddenSpreads, seen, at, chapter.walkFailed))
		{
			if (chapter.wanted.count(at) == 0)
				continue;
			chapter.met.insert(at);
			// That item alone (ReplaceObject answers kSuccess when it wrote - measured, the object search plan's Task 1 M8,
			// docs/superpowers/plans/2026-10-09-kfc-object-search.md) - and the error state too: the step ends by it
			// (EndWriteStep, below - rolled back when one stands), so with one standing the whole step goes back, no row is
			// marked Changed over a write InDesign takes back, and nothing more is written into it. The text Replace asks
			// both (KFCReplaceEngine's walks: the command's ErrorCode and its result).
			const bool replacedIt =
				(KFCObjectSearch::CallReplaceObject(svc, db, hiddenSpreads) == IFindChangeService::kSuccess);
			if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
			{
				result.errorStands = true;
				break;
			}
			if (replacedIt)
				chapter.written.insert(at);
			else
				chapter.refusedByInDesign.insert(at);
			if (chapter.written.size() + chapter.refusedByInDesign.size() == chapter.wanted.size())
				break;
		}
		// ...AND AFTER THE WALK: a search step that broke off with the error state raised, after rows were written, ends the
		// step by that error just the same - the whole step goes back, so the rows are not marked Changed, and no mark is
		// put over the error (MarkWrite).
		if (!result.errorStands && ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		{
			result.errorStands = true;
			result.searchError = true;
		}
		result.writtenCount += chapter.written.size();
	}
	result.ok = result.writtenCount > 0 && !result.errorStands;
	if (result.ok)
		for (size_t c = 0; c < chapters.size(); ++c)
			if (!chapters[c].written.empty())
				KFCUndoFollow::MarkWrite(chapters[c].docRef.GetDataBase());		// in this step, so its Undo / Redo is heard
	// Kept, or taken back whole when nothing was written or an error stands (EndWriteStep); the "unsaved" flags are
	// DocsAfterWrite's (a document is written only when the step is kept - docsAfter.Wrote, below).
	KFCReplaceEngine::EndWriteStep(step, result.ok);
	KFCObjectSearch::AimSharedWalkerAtFront();		// 11 (O7)
	if (!result.ok)
	{
		KFCResultModel::RollBackRows();
		return result;
	}
	// 10. Each written row Changed, with its item as the write left it, and the rows the write moved besides.
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const ObjectChapterWrite& chapter = chapters[c];
		if (chapter.written.empty())
			continue;
		IDataBase* const db = chapter.docRef.GetDataBase();
		for (size_t k = 0; k < chapter.todo.size(); ++k)
		{
			if (chapter.written.count(chapter.todo[k].second) == 0)
				continue;
			uint64 print = 0;
			uint32 length = 0;
			(void)KFCObjectSearch::Fingerprint(UIDRef(db, chapter.todo[k].second), print, length);
			KFCResultModel::MarkItemReplaced(chapter.chapterIdx, chapter.todo[k].first, print, length);
		}
		FollowRowsTheWriteMoved(chapter.chapterIdx, db, chapter.watched, chapter.written);
		KFC_DIAG_NOTE_WRITE(chapter.docRef);
		docsAfter.Wrote(chapter.docRef);		// a chapter of ours has to be seen and saved
	}
	recorder.Keep();
	return result;
}
}	// anonymous namespace

bool KFCObjectReplace::CheckRowNow(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	UIDRef docRef;
	UID item = kInvalidUID;
	if (CheckRow(chapterIdx, hitIdx, outStatus, docRef, item))
		return true;
	HandBackIfHeld(docRef);
	return false;
}

bool KFCObjectReplace::ReplaceRow(int32 chapterIdx, int32 hitIdx, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("object-replace-row");	// (test builds: when it is over - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	// Forward, as the search was - outside the sequence (KFCForwardSearchScope's contract).
	KFCForwardSearchScope forward;
	std::vector<ObjectChapterWrite> chapters(1);
	ObjectChapterWrite& chapter = chapters[0];
	chapter.chapterIdx = chapterIdx;
	UID item = kInvalidUID;
	if (!CheckRow(chapterIdx, hitIdx, outStatus, chapter.docRef, item))
	{
		HandBackIfHeld(chapter.docRef);
		return false;
	}
	chapter.todo.push_back(std::make_pair(hitIdx, item));
	// What the document is left as, once the write is over - the rule rows selected together and the text rows keep
	// (DocsAfterWrite): its "unsaved" flag read now and put back when nothing was written (a rollback puts the item back
	// and leaves the flag up). Its window and hand-back are its landing's, as said above HandBackIfHeld.
	KFCReplaceEngine::DocsAfterWrite docsAfter;
	docsAfter.Reached(chapter.docRef, true);
	// THE WRITE - the one rows selected together take too (WriteObjectRows).
	const ObjectWriteResult wrote = WriteObjectRows(chapters, docsAfter, outStatus);
	if (!wrote.started)
		return false;
	if (!wrote.ok)
	{
		// One row's sentence: InDesign's replace would not run on the item the walk stood on; or the walk never came to it
		// - broken off by an error, or the item no longer a match.
		const bool reached = chapter.met.count(item) > 0;
		Refuse(outStatus, RowSentence(reached ? kRefusedNotWritten : (chapter.walkFailed ? kRefusedWalkFailed : kRefusedNotReached)));
		return false;
	}
	// The message names the item (REP-22's form).
	outStatus = "Replaced ID:";
	outStatus.AppendNumber(static_cast<int32>(item.Get()));
	outStatus.Append(".");
	outStatus.SetTranslatable(kFalse);
	return true;
}

bool KFCObjectReplace::CheckRowsNow(const KFCRowsByChapter& rows, PMString& outStatus)
{
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	const size_t total = KFCRowCount(rows);
	int32 oneChapter = -1, oneHit = -1;
	if (total == 1 && KFCFirstRow(rows, oneChapter, oneHit))
		return CheckRowNow(oneChapter, oneHit, outStatus);
	if (!CheckSettings(outStatus))
		return false;
	// Yes at the first row that would be written; the rest are asked again by the write itself (ReplaceRows), which
	// hands back what it reached and did not write.
	int32 counts[kRefusalCount] = {};
	std::vector<UIDRef> reached;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		const int32 chapterIdx = rows[c].chapter;
		const std::vector<int32>& hitIdxs = rows[c].hits;
		UIDRef docRef;
		for (size_t k = 0; k < hitIdxs.size(); ++k)
		{
			if (!RowIsReplaceable(chapterIdx, hitIdxs[k]))
			{
				++counts[GreyedRefusal(chapterIdx, hitIdxs[k])];
				continue;
			}
			UID item = kInvalidUID;
			const Refusal why = CheckItem(chapterIdx, hitIdxs[k], docRef, item);
			if (why == kRefusedNone)
				return true;
			++counts[why];
		}
		reached.push_back(docRef);
	}
	for (size_t k = 0; k < reached.size(); ++k)
		HandBackIfHeld(reached[k]);
	SayNoneReplaceable(outStatus, total, counts);
	return false;
}

bool KFCObjectReplace::ReplaceRows(const KFCRowsByChapter& rows, PMString& outStatus)
{
	KFC_DIAG_WRITTEN_DOCS_CHECK("object-replace-rows");	// (test builds: when it is over - KFCDiagWrittenDocs.h)
	outStatus.Clear();
	outStatus.SetTranslatable(kFalse);
	const size_t total = KFCRowCount(rows);
	// ONE ROW is the row's own Replace, word for word.
	int32 oneChapter = -1, oneHit = -1;
	if (total == 1 && KFCFirstRow(rows, oneChapter, oneHit))
		return ReplaceRow(oneChapter, oneHit, outStatus);
	if (total == 0)
		return false;
	// Forward, as the search was - outside the sequence (KFCForwardSearchScope's contract).
	KFCForwardSearchScope forward;
	if (!CheckSettings(outStatus))
		return false;

	// O10 steps 3-5, row by row: the rows that pass are written, the rest are counted by their reason.
	int32 counts[kRefusalCount] = {};
	std::vector<ObjectChapterWrite> chapters(rows.size());
	KFCReplaceEngine::DocsAfterWrite docsAfter;		// what each document is left as, on every way out from here
	size_t toWrite = 0;
	for (size_t c = 0; c < rows.size(); ++c)
	{
		ObjectChapterWrite& chapter = chapters[c];
		chapter.chapterIdx = rows[c].chapter;
		const std::vector<int32>& hitIdxs = rows[c].hits;
		for (size_t k = 0; k < hitIdxs.size(); ++k)
		{
			if (!RowIsReplaceable(chapter.chapterIdx, hitIdxs[k]))
			{
				++counts[GreyedRefusal(chapter.chapterIdx, hitIdxs[k])];
				continue;
			}
			UID item = kInvalidUID;
			const Refusal why = CheckItem(chapter.chapterIdx, hitIdxs[k], chapter.docRef, item);
			if (why != kRefusedNone)
			{
				++counts[why];
				continue;
			}
			chapter.todo.push_back(std::make_pair(hitIdxs[k], item));
		}
		toWrite += chapter.todo.size();
		// Its "unsaved" flag read now when it has rows to write (a document whose flag was not read is never made clean).
		if (chapter.docRef.GetDataBase() != nil)
			docsAfter.Reached(chapter.docRef, !chapter.todo.empty());
	}
	if (toWrite == 0)
	{
		SayNoneReplaceable(outStatus, total, counts);
		return false;
	}
	// EVERY DOCUMENT TO WRITE STILL OPEN once the last row is checked - the text rows' rule (KFCReplaceEngine.cpp,
	// ReplaceRowsNow): checking a row of a closed chapter reopens it, and a close the message loop dispatches there would
	// leave a document checked before it behind its docRef (not measured). Asked by comparing alone.
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		if (!chapters[c].todo.empty() && !KFCBookScope::IsDocStillOpen(chapters[c].docRef))
		{
			Refuse(outStatus, "a document of the selected rows has been closed - nothing was changed.");
			return false;
		}
	}
	// THE WRITE - the same one a row's own Replace takes (WriteObjectRows).
	const ObjectWriteResult wrote = WriteObjectRows(chapters, docsAfter, outStatus);
	if (!wrote.started)
		return false;
	// The walk's own refusals, counted with the checks': the items InDesign's replace would not run on, and the rows the walk
	// never came to - broken off by an error, or no longer a match.
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const ObjectChapterWrite& chapter = chapters[c];
		counts[kRefusedNotWritten] += static_cast<int32>(chapter.refusedByInDesign.size());
		counts[chapter.walkFailed ? kRefusedWalkFailed : kRefusedNotReached]
			+= static_cast<int32>(chapter.wanted.size() - chapter.written.size() - chapter.refusedByInDesign.size());
	}
	if (!wrote.ok)
	{
		if (wrote.errorStands)
		{
			Refuse(outStatus, wrote.searchError ? "InDesign's search stopped with an error - nothing was changed."
				: "InDesign's replace stopped with an error - nothing was changed.");
			return false;
		}
		outStatus = "Replace: nothing was changed - ";
		AppendRefusals(outStatus, counts);
		outStatus.Append(".");
		outStatus.SetTranslatable(kFalse);
		return false;
	}
	// "Replaced 3 objects: ID:259, ID:260, ID:261." - and, when some were left, "Replaced 3 of 5 objects: ... Left as they
	// were: 1 locked, 1 changed since the search." / "Replaced 3 objects in 2 documents: a.indd ID:259, ID:260; b.indd
	// ID:12." (SayItemsWritten) - each document's items written, in its rows' order.
	KFCObjectReplace::ItemsWritten items;
	for (size_t c = 0; c < chapters.size(); ++c)
	{
		const ObjectChapterWrite& chapter = chapters[c];
		if (chapter.written.empty())
			continue;
		std::vector<UID> ids;
		for (size_t k = 0; k < chapter.todo.size(); ++k)
			if (chapter.written.count(chapter.todo[k].second) > 0)
				ids.push_back(chapter.todo[k].second);
		items.push_back(std::make_pair(chapter.chapterIdx, ids));
	}
	PMString left;
	AppendRefusals(left, counts);
	KFCObjectReplace::SayItemsWritten(outStatus, PMString("Replaced "), wrote.writtenCount, total, items, PMString(),
		": ", left);
	return true;
}

bool KFCObjectReplace::ReplaceAllInDoc(const UIDRef& docRef, int32& outReplaced, int32& outPartially)
{
	outReplaced = 0;
	outPartially = 0;
	// Nothing processed over an error already standing (KFCReplaceEngine's RunWalkerCmd says why): a failure - the caller's
	// step goes back by it.
	if (ErrorUtils::PMGetGlobalErrorCode() != kSuccess)
		return false;
	InterfacePtr<IFindChangeService> svc(KFCObjectSearch::CreateFindChangeService());
	InterfacePtr<IObjectWalker> shared(KFCObjectSearch::QuerySharedWalker());
	if (svc == nil || shared == nil || docRef.GetDataBase() == nil)
		return false;
	shared->Initialize(KFCObjectSearch::WalkerOptionsFor(docRef, nil));
	int32 found = 0, fully = 0, partially = 0;
	const IFindChangeService::FindChangeResult result = svc->ReplaceAllObject(&found, &fully, &partially, kFalse);
	// FAILED = its answer is kFailure, OR it left the error state raised - the text side's Change All asks both of its
	// command (WriteDocument). The state is cleared here either way, and a failure is the caller's to act on: it rolls
	// its whole step back (KFCChangeAll::Run, KFCQuerySequence.cpp's Run).
	const bool errorStood = (ErrorUtils::PMGetGlobalErrorCode() != kSuccess);
	ErrorUtils::PMSetGlobalErrorCode(kSuccess);
	if (result == IFindChangeService::kFailure || errorStood)
		return false;
	outReplaced = fully + partially;
	outPartially = partially;
	return true;
}

// End, KFCObjectReplace.cpp.
