//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  SEARCH TEXT IN THESE OBJECTS - THE TEXT (docs/superpowers/specs/2026-10-21-kfc-text-in-object-results-design.md T3):
//  the text a tier of object rows holds, made into the stretches its walk takes (KFCSearchEngine's CollectObjectTexts) -
//  each found frame's own part of its story, and the overset after a story's last frame - with the frames those
//  stretches were taken from, which every row the walk builds is checked against (Keeps).
//
//========================================================================================

#ifndef __KFCObjectText_h__
#define __KFCObjectText_h__

#include "UIDRef.h"
#include "KFCResultModel.h"		// Hit

#include <set>
#include <vector>

class IDataBase;

namespace KFCObjectText
{
	/** One stretch of a story to walk: [start, end) of its primary thread - a found frame's own text, or the overset after
	    its story's last frame. The tables and footnotes anchored in it are walked with it (the walk's scope takes them in -
	    the spec's M1, M5). */
	struct Stretch
	{
		UID			story;
		TextIndex	start;
		TextIndex	end;
		bool		overset;
	};

	/** WHAT ONE DOCUMENT'S OBJECTS HOLD AS TEXT. */
	struct Ranges
	{
		std::vector<Stretch>	stretches;		// by story and start, a story's joined where they touch or overlap
		std::set<UID>			frames;			// the items whose frames' text was taken - a row is kept when its match is
												// drawn in one of them (Keeps)
		std::set<UID>			oversetStories;	// the stories whose overset was taken - a row with no frame is kept for these
		int32					objects = 0;	// the objects still in the document, text or not
		int32					noText = 0;		// ...of which none gave a stretch (a rectangle, an image's frame, an empty frame)
		int32					gone = 0;		// the objects no longer in the document (deleted since the object search)
	};

	/** The text of `items` - page items of one document - as stretches to walk. A text frame: its own text, the official
	    form (SnpManipulateTextFrame::GetTextFrameTextRange: its frame list composed through it first when the list has
	    damage - IMultiColumnTextFrame::RecomposeThru, never RecomposeFully, which adds frames - then TextStart for
	    TextSpan characters, the story's last character left out); and, when it is its story's last frame, the overset after
	    it. Composing dirties a document: called under its SaveRestoreModifiedState. */
	void Collect(IDataBase* db, const std::vector<UID>& items, Ranges& out);

	/** Is this row - one the walk of r.stretches built - drawn in a frame whose text was taken? Its match's frame's item
	    (KFCNarrow::FrameOwner) one of r.frames; a match with no frame (overset text) in a story whose overset was taken. */
	bool Keeps(const Ranges& r, IDataBase* db, const KFCResultModel::Hit& hit);
}

#endif // __KFCObjectText_h__
