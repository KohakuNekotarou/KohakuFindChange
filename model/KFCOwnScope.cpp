//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  What a Ruby or Kenten search takes in - see KFCOwnScope.h.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IItemLockData.h"			// a story's lock
#include "ITextModel.h"
#include "ITextStoryThread.h"

// General includes:
#include "MasterPageID.h"			// kMasterPagesBoss - a parent spread
#include "TextID.h"					// kFootnoteReferenceBoss

// Project includes:
#include "KFCOwnScope.h"
#include "KFCPageItemFacts.h"
#include "KFCSearchEngine.h"		// FrameAt - the frame a position is composed into, as a row's

bool KFCOwnScope::TakesStory(const UIDRef& story) const
{
	if (fSwitches.lockedStories)
		return true;
	// (The same question KFCSearchEngine::IsFrameEditable asks of a story: its checkParent - an inline in a locked story - with it.)
	InterfacePtr<IItemLockData> lock(story, UseDefaultIID());
	return lock == nil || !lock->GetInsertLock();
}

bool KFCOwnScope::Takes(const UIDRef& story, ITextModel* model, TextIndex at)
{
	IDataBase* const db = story.GetDataBase();
	if (db == nil || model == nil)
		return true;
	// A FOOTNOTE'S TEXT: its thread's owner is its reference (KFCNarrow::ReadTextPlace's reading).
	if (!fSwitches.footnotes)
	{
		InterfacePtr<ITextStoryThread> thread(model->QueryStoryThread(at));
		const UID dict = (thread != nil) ? thread->GetDictUID() : kInvalidUID;
		if (dict != kInvalidUID && db->IsValidUID(dict) && db->GetClass(dict) == kFootnoteReferenceBoss)
			return false;
	}
	if (fSwitches.masterPages && fSwitches.hiddenLayers && fSwitches.lockedLayers)
		return true;
	const UID frame = KFCSearchEngine::FrameAt(story, at);
	if (frame == kInvalidUID)
		return true;
	std::map<UID, FrameAnswers>::const_iterator known = fFrames.find(frame);
	if (known == fFrames.end())
	{
		FrameAnswers answers;
		const UID spread = KFCPageItemFacts::SpreadOf(UIDRef(db, frame));
		answers.onMaster = (spread != kInvalidUID && db->GetClass(spread) == kMasterPagesBoss);
		answers.hidden = KFCPageItemFacts::IsFrameHidden(db, frame);
		answers.locked = KFCPageItemFacts::IsPageItemLockedForEdit(db, frame) || KFCPageItemFacts::IsFrameOnLockedLayer(db, frame);
		known = fFrames.insert(std::make_pair(frame, answers)).first;
	}
	return (fSwitches.masterPages || !known->second.onMaster) && (fSwitches.hiddenLayers || !known->second.hidden) && (fSwitches.lockedLayers || !known->second.locked);
}

// End, KFCOwnScope.cpp.
