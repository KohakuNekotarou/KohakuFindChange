//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC) - one text focus per result row. See KFCRowFoci.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IDocumentList.h"
#include "ITextFocus.h"
#include "ITextFocusManager.h"
#include "ITextModel.h"
#include "IViewObjectChanges.h"	// a focus's target database (KFCRowFocus.h)

// General includes:
#include "IDThreadingPrimitives.h"	// IsMainThreadDomain - every entry point's gate (the text-focus spec's T10)
#include "RangeData.h"

#include <map>
#include <new>			// std::nothrow - a held set's allocation
#include <utility>
#include <vector>

// Project includes:
#include "KFCRowFoci.h"
#include "KFCBookScope.h"		// QueryOpenDocumentList
#include "KFCID.h"				// kKFCRowFocusBoss
#include "KFCRowFocus.h"		// IsLost
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"	// ReadStoryVersion, ChapterDocNow

namespace
{
struct Kept
{
	ITextFocus*	focus;	// AddRef'd (NewFocus)
	IDataBase*	db;		// the document it was made in - compared, never read through before DocOpen says yes
	UID			story;
	Kept() : focus(nil), db(nil), story(kInvalidUID) {}
};
typedef std::pair<int32, int32> RowKey;		// (chapter, hit)
std::map<RowKey, Kept> gKept;
typedef std::pair<int32, UID> PartKey;		// (chapter, story) - a story's searched part (KFCResultModel::SearchedRange)
std::map<PartKey, Kept> gParts;

// Is this database an open document's? The document list's own lookup by database: the pointer is compared, never
// read through (KFCBookScope::IsDocStillOpen asks the same for a UIDRef).
bool DocOpen(IDataBase* db)
{
	if (db == nil)
		return false;
	InterfacePtr<IDocumentList> docs(KFCBookScope::QueryOpenDocumentList());
	return docs != nil && docs->FindDocByDataBase(db) != nil;
}

// Is f one of the foci this manager holds now? Its list walked, the pointer compared, nothing else read (KT's
// KTFocusProbe, where it was measured).
bool InManager(ITextFocusManager* mgr, const ITextFocus* f)
{
	if (mgr == nil || f == nil)
		return false;
	const int32 n = mgr->GetFocusCount();
	for (int32 i = 0; i < n; ++i)
	{
		InterfacePtr<ITextFocus> nth(mgr->QueryNthFocus(i));
		if (nth.get() == f)
			return true;
	}
	return false;
}

// Let one go (the text-focus spec's T9): its target database cleared, then out of its story's manager, then our
// reference - the last two only while it is still in that manager's list. Otherwise it is dropped untouched: a leak
// beats a call into what may be gone (the close responder lets every focus of a closing document go while the document
// still stands - DetachDocument). The target is cleared whenever the document stands, its story gone or not: a focus
// left behind must not keep pointing the view-interface records (KFCRowFocus.h) at a database that will close.
void Let(Kept& k)
{
	if (k.focus != nil && DocOpen(k.db))
	{
		InterfacePtr<IViewObjectChanges> view(k.focus, UseDefaultIID());
		if (view != nil)
			view->SetTargetDB(nil);
		if (k.db->IsValidUID(k.story))
		{
			InterfacePtr<ITextModel> model(k.db, k.story, UseDefaultIID());
			InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
			if (mgr != nil && InManager(mgr, k.focus))
			{
				mgr->RemoveFocus(k.focus);
				k.focus->Release();
			}
		}
	}
	k.focus = nil;
}

// The row's stored place - asked of the model each time, never kept here.
bool StoredPlace(int32 chapterIdx, int32 hitIdx, UID& outStory, TextIndex& outStart, TextIndex& outEnd)
{
	uint64 hash = 0;
	return KFCResultModel::GetHitMatchIdentity(chapterIdx, hitIdx, outStory, outStart, outEnd, hash)
		&& outStory != kInvalidUID && outStart >= 0 && outEnd >= outStart;
}

// The kept focus, still usable: its document open, its story there, the focus in its story's manager and on that
// story's model. Anything else lets it go (the caller drops it from its map) and answers nil.
ITextFocus* StillUsable(Kept& k)
{
	if (k.focus != nil && DocOpen(k.db) && k.db->IsValidUID(k.story))
	{
		InterfacePtr<ITextModel> model(k.db, k.story, UseDefaultIID());
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		InterfacePtr<ITextModel> focusModel(k.focus->QueryModel());
		if (mgr != nil && InManager(mgr, k.focus) && focusModel.get() == model.get())
			return k.focus;
	}
	Let(k);
	return nil;
}

ITextFocus* Usable(std::map<RowKey, Kept>::iterator it)
{
	ITextFocus* f = StillUsable(it->second);
	if (f == nil)
		gKept.erase(it);
	return f;
}

// A focus of KFC's own class on `range` of the manager's story, its target the document's database (KFCRowFocus.h:
// without a target its snapshots go nowhere, and an Undo leaves it behind as InDesign's own focus is left). AddRef'd;
// nil when none could be made. (Every range here is RangeData(start, end, kLeanForward): where start == end, a caret
// with that lean - the three-index form takes the lean for an empty range, RangeData.h.)
ITextFocus* MakeFocus(ITextFocusManager* mgr, IDataBase* db, const RangeData& range)
{
	ITextFocus* f = mgr->NewFocus(range, kKFCRowFocusBoss);	// AddRef'd
	if (f == nil)
		return nil;
	InterfacePtr<IViewObjectChanges> view(f, UseDefaultIID());
	if (view != nil)
		view->SetTargetDB(db);
	return f;
}

// The story's searched part given its focus in db, on its recorded place - made, moved, or left as it is; nothing
// with no db (the chapter's document is not open). inStep: a move inside a write's step is marked first
// (KFCRowFoci::MoveTo's rule).
void PlacePart(int32 chapterIdx, UID story, IDataBase* db, bool inStep)
{
	KFCResultModel::SearchedRange part;
	if (db == nil || !KFCResultModel::GetSearchedRange(chapterIdx, story, part) || !part.walkable || !db->IsValidUID(story))
		return;
	uint32 now = 0;
	if (!KFCSearchEngine::ReadStoryVersion(db, story, now) || now != part.version)
		return;		// the recorded place is not exact for the story as it stands (the text-focus spec's T5)
	InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
	InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
	if (model == nil || mgr == nil || part.start < 0 || part.end < part.start || part.end > model->TotalLength())
		return;
	const PartKey key(chapterIdx, story);
	std::map<PartKey, Kept>::iterator had = gParts.find(key);
	if (had != gParts.end())
	{
		ITextFocus* f = (had->second.db == db) ? StillUsable(had->second) : nil;
		if (f != nil)
		{
			const RangeData r = f->GetCurrentRange();
			if (r.Start(nil) != part.start || r.End() != part.end)
			{
				if (inStep)
					f->PreDirtyFocus();
				f->SetRange(kFalse, RangeData(part.start, part.end, RangeData::kLeanForward));
			}
			return;
		}
		Let(had->second);		// one from another database, or no longer usable
		gParts.erase(had);
	}
	ITextFocus* f = MakeFocus(mgr, db, RangeData(part.start, part.end, RangeData::kLeanForward));
	if (f == nil)
		return;
	Kept k;
	k.focus = f;
	k.db = db;
	k.story = story;
	gParts[key] = k;
}

// Every focus in `kept` - the rows' or the parts' (both keyed chapter first) - that `picks`, asked of its key and of
// what is kept, let go and dropped (DetachChapter, DetachDocument, DetachAll, LetDocumentOf).
template <class Key, class Picks> void LetWhere(std::map<Key, Kept>& kept, const Picks& picks)
{
	for (typename std::map<Key, Kept>::iterator it = kept.begin(); it != kept.end(); )
	{
		if (picks(it->first, it->second))
		{
			Let(it->second);
			it = kept.erase(it);
		}
		else
			++it;
	}
}
}	// anonymous namespace

void KFCRowFoci::AttachChapter(int32 chapterIdx)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	// THE CHAPTER'S DOCUMENT, BY ITS FILE (KFCSearchEngine::ChapterDocNow): asked by the binding the results hold, a
	// neighbour that has taken a closed chapter's address would get these rows' foci.
	IDataBase* const db = KFCSearchEngine::ChapterDocNow(chapterIdx).GetDataBase();
	if (db == nil)
		return;
	std::map<UID, bool> knownStory;		// per story: is it at the version KFC recorded?
	const int32 n = KFCResultModel::GetHitCount(chapterIdx);
	for (int32 i = 0; i < n; ++i)
	{
		const RowKey key(chapterIdx, i);
		std::map<RowKey, Kept>::iterator had = gKept.find(key);
		if (had != gKept.end())
		{
			if (had->second.db == db)
				continue;			// it has one in this document already
			Let(had->second);		// one from another database (the chapter closed and opened again)
			gKept.erase(had);
		}
		UID story = kInvalidUID;
		TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
		if (!StoredPlace(chapterIdx, i, story, start, end) || !db->IsValidUID(story))
			continue;
		std::map<UID, bool>::iterator known = knownStory.find(story);
		if (known == knownStory.end())
		{
			uint32 recorded = 0, now = 0;
			const bool same = KFCResultModel::GetStoryVersion(chapterIdx, story, recorded)
				&& KFCSearchEngine::ReadStoryVersion(db, story, now) && recorded == now;
			known = knownStory.insert(std::make_pair(story, same)).first;
		}
		if (!known->second)
			continue;	// KFC does not know this story as it stands - its stored places are not to be trusted
						// (the text-focus spec's T5)
		InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		if (model == nil || mgr == nil || end > model->TotalLength())
			continue;
		ITextFocus* f = MakeFocus(mgr, db, RangeData(start, end, RangeData::kLeanForward));
		if (f == nil)
			continue;
		Kept k;
		k.focus = f;
		k.db = db;
		k.story = story;
		gKept[key] = k;
	}
	// The searched part of each story, after a search over part of a story - on the same terms.
	std::vector<UID> parts;
	KFCResultModel::GetSearchedRangeStories(chapterIdx, parts);
	for (size_t i = 0; i < parts.size(); ++i)
		PlacePart(chapterIdx, parts[i], db, false);
}

void KFCRowFoci::AttachOpenChapters()
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	const int32 chapters = KFCResultModel::GetChapterCount();
	for (int32 c = 0; c < chapters; ++c)
		AttachChapter(c);
}

bool KFCRowFoci::Current(int32 chapterIdx, int32 hitIdx, IDataBase* db, TextIndex& outStart, TextIndex& outEnd)
{
	if (!IDThreading::IsMainThreadDomain())
		return false;
	std::map<RowKey, Kept>::iterator it = gKept.find(RowKey(chapterIdx, hitIdx));
	if (it == gKept.end() || it->second.db != db)
		return false;
	ITextFocus* f = Usable(it);
	if (f == nil)
		return false;
	const RangeData r = f->GetCurrentRange();
	outStart = r.Start(nil);
	outEnd = r.End();
	return true;
}

KFCRowFoci::FocusState KFCRowFoci::Where(int32 chapterIdx, int32 hitIdx, IDataBase* db, TextIndex& outStart, TextIndex& outEnd)
{
	if (!IDThreading::IsMainThreadDomain())
		return kNoFocus;
	std::map<RowKey, Kept>::iterator it = gKept.find(RowKey(chapterIdx, hitIdx));
	if (it == gKept.end() || it->second.db != db)
		return kNoFocus;
	ITextFocus* f = Usable(it);
	if (f == nil)
		return kNoFocus;
	const RangeData r = f->GetCurrentRange();
	outStart = r.Start(nil);
	outEnd = r.End();
	return KFCRowFocus::IsLost(f) ? kLostFocus : kFocused;
}

void KFCRowFoci::MoveTo(int32 chapterIdx, int32 hitIdx, TextIndex start, TextIndex end, bool inStep)
{
	if (!IDThreading::IsMainThreadDomain() || start < 0 || end < start)
		return;
	std::map<RowKey, Kept>::iterator it = gKept.find(RowKey(chapterIdx, hitIdx));
	if (it == gKept.end())
		return;
	ITextFocus* f = Usable(it);
	if (f == nil)
		return;
	InterfacePtr<ITextModel> model(f->QueryModel());
	if (model == nil || end > model->TotalLength())
		return;
	const RangeData r = f->GetCurrentRange();
	if (r.Start(nil) == start && r.End() == end)
		return;
	// Inside a write's step the move is marked first, so the step's snapshot holds it and the step's Undo and Redo put
	// the focus where the write left it (KFCRowFocus.h). Outside a step there is no step to hold it, and an Undo after it
	// reads back the range of before (the KT spike note's S7) - out there only a lost focus, or one already in place,
	// is moved.
	if (inStep)
		f->PreDirtyFocus();
	f->SetRange(kFalse, RangeData(start, end, RangeData::kLeanForward));
}

void KFCRowFoci::Reanchor(int32 chapterIdx, int32 hitIdx, bool inStep)
{
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	if (StoredPlace(chapterIdx, hitIdx, story, start, end))
		MoveTo(chapterIdx, hitIdx, start, end, inStep);
}

void KFCRowFoci::ReanchorStory(int32 chapterIdx, UID story, bool inStep)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	std::vector<int32> rows;
	for (std::map<RowKey, Kept>::const_iterator it = gKept.begin(); it != gKept.end(); ++it)
		if (it->first.first == chapterIdx && it->second.story == story)
			rows.push_back(it->first.second);
	for (size_t i = 0; i < rows.size(); ++i)
		Reanchor(chapterIdx, rows[i], inStep);
	// ...and the story's searched part, on its recorded place.
	PlacePart(chapterIdx, story, KFCSearchEngine::ChapterDocNow(chapterIdx).GetDataBase(), inStep);
}

bool KFCRowFoci::CurrentSearchedRange(int32 chapterIdx, UID story, IDataBase* db, TextIndex& outStart, TextIndex& outEnd)
{
	if (!IDThreading::IsMainThreadDomain())
		return false;
	std::map<PartKey, Kept>::iterator it = gParts.find(PartKey(chapterIdx, story));
	if (it == gParts.end() || it->second.db != db)
		return false;
	ITextFocus* f = StillUsable(it->second);
	if (f == nil)
	{
		gParts.erase(it);
		return false;
	}
	const RangeData r = f->GetCurrentRange();
	outStart = r.Start(nil);
	outEnd = r.End();
	return true;
}

void KFCRowFoci::DetachChapter(int32 chapterIdx)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	const auto ofChapter = [&](const auto& key, const Kept&) { return key.first == chapterIdx; };
	LetWhere(gKept, ofChapter);
	LetWhere(gParts, ofChapter);
}

void KFCRowFoci::DetachDocument(IDataBase* db)
{
	if (!IDThreading::IsMainThreadDomain() || db == nil)
		return;
	const auto onDoc = [&](const auto&, const Kept& k) { return k.db == db; };
	LetWhere(gKept, onDoc);
	LetWhere(gParts, onDoc);
}

void KFCRowFoci::DetachAll()
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	const auto every = [](const auto&, const Kept&) { return true; };
	LetWhere(gKept, every);
	LetWhere(gParts, every);
}

// A TIER'S FOCI, HELD (KFCRowFoci.h): the two maps the live list keeps, as they were when it was put aside - keyed by its
// own chapter and hit numbers, which its list keeps while it is grey (KFCTierStore - the list is a snapshot).
struct KFCRowFoci::Held
{
	std::map<RowKey, Kept>	rows;
	std::map<PartKey, Kept>	parts;

	~Held()
	{
		// The last holder gone: every focus let go - Let's own rule (only from a manager that still lists it; a leak
		// beats a call into a closed document). Never off the main thread (a leak then, as Let would refuse anyway).
		if (!IDThreading::IsMainThreadDomain())
			return;
		for (std::map<RowKey, Kept>::iterator it = rows.begin(); it != rows.end(); ++it)
			Let(it->second);
		for (std::map<PartKey, Kept>::iterator it = parts.begin(); it != parts.end(); ++it)
			Let(it->second);
	}
};

KFCRowFoci::HeldPtr KFCRowFoci::HandOver()
{
	if (!IDThreading::IsMainThreadDomain() || (gKept.empty() && gParts.empty()))
		return HeldPtr();
	HeldPtr held(new (std::nothrow) Held());
	if (held == nullptr)
		return HeldPtr();		// (no memory: the live list keeps them, and lets them go as it always has)
	held->rows.swap(gKept);
	held->parts.swap(gParts);
	return held;
}

void KFCRowFoci::TakeBack(HeldPtr& held)
{
	if (!IDThreading::IsMainThreadDomain() || held == nullptr)
		return;
	for (std::map<RowKey, Kept>::iterator it = held->rows.begin(); it != held->rows.end(); ++it)
	{
		std::map<RowKey, Kept>::iterator live = gKept.find(it->first);
		if (live != gKept.end())
			Let(live->second);		// one attached on the stored place when the list came back - the held one follows the edits
		gKept[it->first] = it->second;
	}
	for (std::map<PartKey, Kept>::iterator it = held->parts.begin(); it != held->parts.end(); ++it)
	{
		std::map<PartKey, Kept>::iterator live = gParts.find(it->first);
		if (live != gParts.end())
			Let(live->second);
		gParts[it->first] = it->second;
	}
	// The foci are the live list's now: emptied, so no holder lets them go.
	held->rows.clear();
	held->parts.clear();
	held.reset();
}

void KFCRowFoci::LetDocumentOf(const HeldPtr& held, IDataBase* db)
{
	if (!IDThreading::IsMainThreadDomain() || held == nullptr || db == nil)
		return;
	const auto onDoc = [&](const auto&, const Kept& k) { return k.db == db; };
	LetWhere(held->rows, onDoc);
	LetWhere(held->parts, onDoc);
}

// End, KFCRowFoci.cpp.
