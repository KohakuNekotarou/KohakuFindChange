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

// General includes:
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - every entry point's gate (spec T10)
#include "RangeData.h"

#include <map>
#include <utility>
#include <vector>

// Project includes:
#include "KFCRowFoci.h"
#include "KFCBookScope.h"		// FindOpenChapterDoc, QueryOpenDocumentList
#include "KFCResultModel.h"
#include "KFCSearchEngine.h"	// ReadStoryVersion

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

// Let one go (spec T9): out of its story's manager, then our reference - only while it is still in that manager's
// list. Otherwise it is dropped untouched: a leak beats a call into what may be gone (the close responder lets every
// focus of a closing document go while the document still stands - DetachDocument).
void Let(Kept& k)
{
	if (k.focus != nil && DocOpen(k.db) && k.db->IsValidUID(k.story))
	{
		InterfacePtr<ITextModel> model(k.db, k.story, UseDefaultIID());
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		if (mgr != nil && InManager(mgr, k.focus))
		{
			mgr->RemoveFocus(k.focus);
			k.focus->Release();
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
// story's model. Anything else lets it go and answers nil.
ITextFocus* Usable(std::map<RowKey, Kept>::iterator it)
{
	Kept& k = it->second;
	if (k.focus != nil && DocOpen(k.db) && k.db->IsValidUID(k.story))
	{
		InterfacePtr<ITextModel> model(k.db, k.story, UseDefaultIID());
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		InterfacePtr<ITextModel> focusModel(k.focus->QueryModel());
		if (mgr != nil && InManager(mgr, k.focus) && focusModel.get() == model.get())
			return k.focus;
	}
	Let(k);
	gKept.erase(it);
	return nil;
}
}	// anonymous namespace

void KFCRowFoci::AttachChapter(int32 chapterIdx)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	// THE CHAPTER'S DOCUMENT, BY ITS FILE (FindOpenChapterDoc - a chapter with no file by its UIDRef). A book chapter's
	// binding outlives its document - book results stay when a chapter closes (KFCCloseDocResponder) - and a closed
	// document's address can be the next document's: asked by that UIDRef, a neighbour would get these rows' foci (the
	// address-reuse fault KFCBookScope::ReachChapterDoc records).
	UIDRef docRef;
	IDFile file;
	if (!KFCResultModel::GetChapterLocation(chapterIdx, docRef, file) || !KFCBookScope::FindOpenChapterDoc(file, docRef))
		return;
	IDataBase* const db = docRef.GetDataBase();
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
			continue;	// KFC does not know this story as it stands - its stored places are not to be trusted (T5)
		InterfacePtr<ITextModel> model(db, story, UseDefaultIID());
		InterfacePtr<ITextFocusManager> mgr(model, UseDefaultIID());
		if (model == nil || mgr == nil || end > model->TotalLength())
			continue;
		ITextFocus* f = mgr->NewFocus(RangeData(start, end, RangeData::kLeanForward), kInvalidClass);	// AddRef'd
		if (f == nil)
			continue;
		Kept k;
		k.focus = f;
		k.db = db;
		k.story = story;
		gKept[key] = k;
	}
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

void KFCRowFoci::MoveTo(int32 chapterIdx, int32 hitIdx, TextIndex start, TextIndex end)
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
	f->SetRange(kFalse, RangeData(start, end, RangeData::kLeanForward));
}

void KFCRowFoci::Reanchor(int32 chapterIdx, int32 hitIdx)
{
	UID story = kInvalidUID;
	TextIndex start = kInvalidTextIndex, end = kInvalidTextIndex;
	if (StoredPlace(chapterIdx, hitIdx, story, start, end))
		MoveTo(chapterIdx, hitIdx, start, end);
}

void KFCRowFoci::ReanchorStory(int32 chapterIdx, UID story)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	std::vector<int32> rows;
	for (std::map<RowKey, Kept>::const_iterator it = gKept.begin(); it != gKept.end(); ++it)
		if (it->first.first == chapterIdx && it->second.story == story)
			rows.push_back(it->first.second);
	for (size_t i = 0; i < rows.size(); ++i)
		Reanchor(chapterIdx, rows[i]);
}

void KFCRowFoci::DetachChapter(int32 chapterIdx)
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	for (std::map<RowKey, Kept>::iterator it = gKept.begin(); it != gKept.end(); )
	{
		if (it->first.first == chapterIdx)
		{
			Let(it->second);
			it = gKept.erase(it);
		}
		else
			++it;
	}
}

void KFCRowFoci::DetachDocument(IDataBase* db)
{
	if (!IDThreading::IsMainThreadDomain() || db == nil)
		return;
	for (std::map<RowKey, Kept>::iterator it = gKept.begin(); it != gKept.end(); )
	{
		if (it->second.db == db)
		{
			Let(it->second);
			it = gKept.erase(it);
		}
		else
			++it;
	}
}

void KFCRowFoci::DetachAll()
{
	if (!IDThreading::IsMainThreadDomain())
		return;
	for (std::map<RowKey, Kept>::iterator it = gKept.begin(); it != gKept.end(); ++it)
		Let(it->second);
	gKept.clear();
}

// End, KFCRowFoci.cpp.
