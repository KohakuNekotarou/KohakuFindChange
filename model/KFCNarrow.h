//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  A TIER'S NARROW DOWN - THE CONDITIONS (docs/superpowers/specs/2026-10-18-kfc-narrow-down-design.md N3, N5 - the author,
//  2026-10-18): what a narrowed list's walks ask of each match or item before they list it, so the list's limit counts the
//  rows that pass (the author's call: "the latter" - the tier searched again, the rest dropped on the way). The steps are
//  the list's (KFCResultModel::ListHeader::narrow); the search engines hand each row they build to a Filter made of them.
//  And the fact a text row carries for them that only they ask, read as the row is built: where its match's text stands (a
//  table cell, a footnote, an endnote). (Its frame out on the pasteboard is read with its page - KFCSearchEngine's FrameFacts.)
//
//========================================================================================

#ifndef __KFCNarrow_h__
#define __KFCNarrow_h__

#include "PMString.h"
#include "UIDRef.h"
#include "KFCModelTypes.h"		// KFCNarrowStep
#include "KFCResultModel.h"		// Hit

#include <set>
#include <vector>

namespace KFCNarrow
{
	/** WHERE A MATCH'S TEXT STANDS (In Tables / In Footnotes / In Endnotes): read off the story thread holding `at` - the
	    owner of its thread dictionary (ITextStoryThread::GetDictUID: a table's cells are threads of kTableModelBoss, a footnote's
	    text one of kFootnoteReferenceBoss - the iid-boss dictionary's owners of IID_ITEXTSTORYTHREADDICT) - and the story's own
	    class (an endnote's text is a story of kEndnoteStoryBoss). Written onto the row; nothing for a position it cannot read. */
	void ReadTextPlace(const UIDRef& storyRef, TextIndex at, KFCResultModel::Hit& io);

	/** THE CONDITIONS OF ONE NARROWED LIST, asked of each row a walk builds. Made of the list's steps; an empty Filter keeps
	    every row (a list no Narrow Down made walks as it always has). */
	class Filter
	{
	public:
		explicit Filter(const std::vector<KFCNarrowStep>& steps);

		/** No condition at all. */
		bool Empty() const { return fSteps.empty(); }
		/** Does a text row need its place read (ReadTextPlace) for these conditions? */
		bool NeedsTextPlace() const;
		/** Do the conditions turn the text rows into the frames holding them (Frames of These Matches)? */
		bool MakesFrames() const;

		/** A TEXT ROW, built (its page, its flags, its place): does it meet every condition taken before a Frames step - every
		    condition, when there is none? */
		bool KeepText(const KFCResultModel::Hit& hit) const;
		/** AN OBJECT ROW - an Object tab search's, or a frame a Frames step made: does it meet every condition that asks of an
		    object (those after a Frames step, or every one of an Object tab's list)? */
		bool KeepObject(const KFCResultModel::Hit& hit) const;

	private:
		bool Meets(const KFCNarrowStep& step, const KFCResultModel::Hit& hit, bool objectRow) const;
		std::vector<KFCNarrowStep>	fSteps;
		size_t						fFramesAt;		// the index of the Frames step - fSteps.size() when there is none
	};

	/** Is this a condition only a text row can meet (In Tables / In Footnotes / In Endnotes / Frames of These Matches)? */
	bool IsTextOnly(int32 kind);
	/** A step's words, as the menu names it ("In Tables") - the status line's refusals. */
	PMString StepWords(const KFCNarrowStep& step);
}

#endif // __KFCNarrow_h__

// End, KFCNarrow.h.
