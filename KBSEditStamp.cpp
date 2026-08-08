//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSEditStamp.h for what is recorded and why it is every story rather than only the ones
//  a hit landed in.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IStoryList.h"
#include "ITextModel.h"

// General includes:
#include "UIDRef.h"

// Project includes:
#include "KBSEditStamp.h"

#include <utility>
#include <vector>

namespace
{

// One chapter's record. `stamped` is not the same question as "is `stories` empty": a document
// that could not be read is left unstamped and must never be reported as edited, whereas a
// document that genuinely holds no readable story is stamped with an empty list and WOULD be
// reported if one appeared later.
struct ChapterStamp
{
	bool									stamped;
	std::vector<std::pair<UID, uint32> >	stories;

	ChapterStamp() : stamped(false) {}
};

// Indexed by the chapter's position in KBSResultModel. Emptied from inside that model's Clear(),
// so nothing here has to be remembered at the nine call sites that clear the results.
std::vector<ChapterStamp> gStamps;

// What the last CapturePending read, waiting for the index it belongs to. One is enough: the
// search walks one chapter at a time and commits before moving on.
ChapterStamp gPending;

void EnsureRoom(int32 chapterIdx)
{
	const size_t wanted = static_cast<size_t>(chapterIdx) + 1;
	if (gStamps.size() < wanted)
		gStamps.resize(wanted);
}

} // anonymous namespace

/* CapturePending
*/
void KBSEditStamp::CapturePending(const UIDRef& docRef)
{
	ChapterStamp& slot = gPending;
	slot.stamped = false;
	slot.stories.clear();

	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return;		// left UNSTAMPED, which reads as "not known to have changed"

	// GetAllTextModelCount, not GetUserAccessibleStoryCount: the walk the replace re-runs covers
	// the chapter, and a story the user never sees still holds text that can match.
	const int32 storyCount = storyList->GetAllTextModelCount();
	for (int32 i = 0; i < storyCount; ++i)
	{
		const UIDRef storyRef = storyList->GetNthTextModelUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;	// skipped here AND skipped in the compare, so the two stay in step

		slot.stories.push_back(std::make_pair(storyRef.GetUID(), model->GetChangeCount()));
	}

	slot.stamped = true;
}

/* CommitPending
*/
void KBSEditStamp::CommitPending(int32 chapterIdx)
{
	if (chapterIdx >= 0)
	{
		EnsureRoom(chapterIdx);
		gStamps[static_cast<size_t>(chapterIdx)] = gPending;
	}

	// Cleared whether or not it was filed. A reading left standing would be committed against the
	// NEXT chapter's index if that one could not be read at all.
	gPending.stamped = false;
	gPending.stories.clear();
}

/* IsChapterCurrent
*/
bool KBSEditStamp::IsChapterCurrent(int32 chapterIdx, const UIDRef& docRef)
{
	if (chapterIdx < 0 || static_cast<size_t>(chapterIdx) >= gStamps.size())
		return true;	// never stamped - see the header for why silence is the right answer

	const ChapterStamp& slot = gStamps[static_cast<size_t>(chapterIdx)];
	if (!slot.stamped)
		return true;

	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return false;	// it WAS readable when stamped, so something has changed since

	// Walk both lists in step. Any of three things means the chapter has moved on: a counter that
	// no longer matches, a story that is not the one that stood in that position, or the two lists
	// not ending together. The third is what catches an added or deleted story - a new one is
	// inserted part-way through the enumeration rather than appended (measured 2026-08-08), so the
	// position check usually fires first, but the ending check is what makes it certain.
	const int32 storyCount = storyList->GetAllTextModelCount();
	size_t at = 0;
	for (int32 i = 0; i < storyCount; ++i)
	{
		const UIDRef storyRef = storyList->GetNthTextModelUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;	// mirror the capture

		if (at >= slot.stories.size()
			|| slot.stories[at].first != storyRef.GetUID()
			|| slot.stories[at].second != model->GetChangeCount())
			return false;
		++at;
	}

	return at == slot.stories.size();
}

/* Forget
*/
void KBSEditStamp::Forget()
{
	gStamps.clear();
	// The pending reading belongs to the run that is being discarded too. A chapter that turned out
	// to hold no hits leaves one standing, and it must not survive into the next search.
	gPending.stamped = false;
	gPending.stories.clear();
}

/* ShutdownCleanup
*/
void KBSEditStamp::ShutdownCleanup()
{
	// clear() would leave the vector holding its buffer. Swapping with an empty one hands the
	// storage back before the .pln unloads, which is what the model's own ShutdownCleanup is for.
	std::vector<ChapterStamp>().swap(gStamps);
}

// End, KBSEditStamp.cpp.
