//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSEditStamp.h for what is recorded and why it is every story rather than only the ones
//  a hit landed in - and why, since 2026-08-09, the stamp also carries the walk gates (hidden /
//  locked-layer per frame, insert lock per story) the search's scope switches watch.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IFrameList.h"			// the story's frames, in flow order - the walk gates are per frame
#include "IItemLockData.h"		// the story's insert lock - what "Include Locked Stories" is about
#include "IStoryList.h"
#include "ITextModel.h"

// General includes:
#include "UIDRef.h"

// Project includes:
#include "KBSEditStamp.h"
#include "KBSSearchEngine.h"	// GetFrameWalkGates - the same two answers every hit is built with

#include <utility>
#include <vector>

namespace
{

// One frame's walk gates, in the story's flow order. EFFECTIVE states, not raw ones: "hidden"
// folds the layer's eye and the item's own Object > Hide together, "lockedLayer" is the layer the
// frame RESOLVES to - so a frame moved onto a hidden or locked layer changes its entry, while one
// moved between two ordinary layers does not (no layer list could say either of those things).
struct FrameStamp
{
	bool	hidden;
	bool	lockedLayer;

	FrameStamp() : hidden(false), lockedLayer(false) {}
};

// One story's record.
struct StoryStamp
{
	UID						uid;
	uint32					changeCount;
	bool					insertLocked;	// read - and compared - only under watchLockedStories
	std::vector<FrameStamp>	frames;			// filled only under watchHiddenContent / watchLockedLayers

	StoryStamp() : uid(kInvalidUID), changeCount(0), insertLocked(false) {}
};

// One chapter's record. `stamped` is not the same question as "is `stories` empty": a document
// that could not be read is left unstamped and must never be reported as edited, whereas a
// document that genuinely holds no readable story is stamped with an empty list and WOULD be
// reported if one appeared later.
struct ChapterStamp
{
	bool	stamped;

	// Which gates the search's scope switches made part of the universe (KBSEditStamp.h). Stored
	// in the stamp so the compare asks exactly what the capture answered - the switches cannot
	// have changed in between (the walk signature refuses first), but the stamp must not depend
	// on anyone re-reading them.
	bool	watchHiddenContent;
	bool	watchLockedLayers;
	bool	watchLockedStories;

	std::vector<StoryStamp>	stories;

	ChapterStamp()
		: stamped(false),
		  watchHiddenContent(false), watchLockedLayers(false), watchLockedStories(false) {}
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

// Read one story's record - the counter always, the gates per the watch flags. ONE function for
// the capture and the compare (the compare builds a fresh reading and compares records), because
// two spellings of "what does this story look like" is how a capture and a compare drift apart.
void ReadStory(const UIDRef& storyRef, ITextModel* model, const ChapterStamp& watches,
	StoryStamp& outEntry)
{
	outEntry.uid = storyRef.GetUID();
	outEntry.changeCount = model->GetChangeCount();

	if (watches.watchLockedStories)
	{
		// The lock "Include Locked Stories" is about, read the way the product reads it before
		// changing text (spellpanel, SpellReplaceWalker.cpp:435-436). Absent interface = not
		// locked, on both sides alike.
		InterfacePtr<IItemLockData> lock(model, IID_IITEMLOCKDATA);
		outEntry.insertLocked = (lock != nil && lock->GetInsertLock() != kFalse);
	}

	if (watches.watchHiddenContent || watches.watchLockedLayers)
	{
		// The story's frames in flow order. A story placed in no frame (internal, or fully
		// overset) simply records none - stable on both sides. The gates come from
		// KBSSearchEngine so a frame reads here exactly as it reads under a hit.
		InterfacePtr<IFrameList> frameList(model->QueryFrameList());
		if (frameList != nil)
		{
			IDataBase* const db = storyRef.GetDataBase();
			const int32 frameCount = frameList->GetFrameCount();
			for (int32 f = 0; f < frameCount; ++f)
			{
				bool hidden = false, lockedLayer = false;
				KBSSearchEngine::GetFrameWalkGates(db, frameList->GetNthFrameUID(f),
					hidden, lockedLayer);

				FrameStamp gate;
				// An unwatched gate is recorded as its default, never read - so it cannot differ.
				if (watches.watchHiddenContent)
					gate.hidden = hidden;
				if (watches.watchLockedLayers)
					gate.lockedLayer = lockedLayer;
				outEntry.frames.push_back(gate);
			}
		}
	}
}

} // anonymous namespace

/* CapturePending
*/
void KBSEditStamp::CapturePending(const UIDRef& docRef, const WalkerScopeOptions& scopeOptions)
{
	ChapterStamp& slot = gPending;
	slot.stamped = false;
	slot.stories.clear();

	// An OFF switch excludes a state from the walk, which makes that state part of the universe
	// the stored walk orders were numbered in; an ON one walks it regardless. Only what can move
	// the numbering is watched - see the header.
	slot.watchHiddenContent = scopeOptions.GetIncludeHiddenLayers() == kFalse;
	slot.watchLockedLayers  = scopeOptions.GetIncludeLockedLayers() == kFalse;
	slot.watchLockedStories = scopeOptions.GetIncludeLockedStories() == kFalse;

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

		StoryStamp entry;
		ReadStory(storyRef, model, slot, entry);
		slot.stories.push_back(entry);
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

/* QueryChapterChange
*/
KBSEditStamp::ChapterChange KBSEditStamp::QueryChapterChange(int32 chapterIdx, const UIDRef& docRef)
{
	if (chapterIdx < 0 || static_cast<size_t>(chapterIdx) >= gStamps.size())
		return kUnchanged;	// never stamped - see the header for why silence is the right answer

	const ChapterStamp& slot = gStamps[static_cast<size_t>(chapterIdx)];
	if (!slot.stamped)
		return kUnchanged;

	InterfacePtr<IStoryList> storyList(docRef, UseDefaultIID());
	if (storyList == nil)
		return kTextEdited;	// it WAS readable when stamped, so something has changed since

	// Walk both lists in step. Any of three things means the text has moved on: a counter that
	// no longer matches, a story that is not the one that stood in that position, or the two
	// lists not ending together. The third is what catches an added or deleted story - a new one
	// is inserted part-way through the enumeration rather than appended (measured 2026-08-08), so
	// the position check usually fires first, but the ending check is what makes it certain.
	//
	// A GATE that moved is noted and NOT returned yet: a text edit found further down the same
	// walk is the more specific answer (KBSEditStamp.h), so the text arms get the whole list
	// before the gates are allowed to speak.
	bool gateMoved = false;
	const int32 storyCount = storyList->GetAllTextModelCount();
	size_t at = 0;
	for (int32 i = 0; i < storyCount; ++i)
	{
		const UIDRef storyRef = storyList->GetNthTextModelUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
			continue;	// mirror the capture

		if (at >= slot.stories.size())
			return kTextEdited;

		const StoryStamp& was = slot.stories[at];
		if (was.uid != storyRef.GetUID() || was.changeCount != model->GetChangeCount())
			return kTextEdited;

		if (!gateMoved
			&& (slot.watchHiddenContent || slot.watchLockedLayers || slot.watchLockedStories))
		{
			// The same reading the capture took, compared record against record. A frame count
			// that differs is a gate change too: with the counter above unmoved, the text cannot
			// have driven it, so the frames themselves were rearranged.
			StoryStamp now;
			ReadStory(storyRef, model, slot, now);
			if (now.insertLocked != was.insertLocked
				|| now.frames.size() != was.frames.size())
				gateMoved = true;
			else
			{
				for (size_t f = 0; f < now.frames.size(); ++f)
				{
					if (now.frames[f].hidden != was.frames[f].hidden
						|| now.frames[f].lockedLayer != was.frames[f].lockedLayer)
					{
						gateMoved = true;
						break;
					}
				}
			}
		}
		++at;
	}
	if (at != slot.stories.size())
		return kTextEdited;

	return gateMoved ? kScopeStateChanged : kUnchanged;
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

	// ***** THIS FILE KEEPS TWO STATICS, NOT ONE. ***** The pending reading holds a vector of its
	// own, and neither Forget() nor CommitPending() does more than clear() it - which is exactly the
	// case the line above exists for. Any book search leaves it holding its buffer, so it reached
	// the unload still owning storage until this was added (2026-08-09, the file's first API audit).
	// The caller that invokes this function describes it as "a static vector of its own", singular
	// (KBSResultListWidgetMgr.cpp), which is how the second one went uncounted.
	gPending.stamped = false;
	std::vector<StoryStamp>().swap(gPending.stories);
}

// End, KBSEditStamp.cpp.
