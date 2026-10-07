//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  NodeID class for the result tree. A node is (chapter index, font group, hit index):
//
//    (-2, -1, -1)        the hidden root
//    (-1, -1, -1)        the BOOK row       -> present only while the results came from a book search
//    (chap, -1, -1)      a document row     -> index into KFCResultModel's chapters
//    (chap, font, -1)    a STORY row        -> 'font' indexes the chapter's fontGroups (one per story)
//    (chap, font, hit)   a hit row          -> hit indexes that CHAPTER's hits
//
//  The book row is what tells the user WHICH book was searched, permanently and in the panel
//  itself rather than in a status line that the next message overwrites. A document-scope search
//  has no book row, so its tree is one level shallower.
//
//  The level under a document holds STORIES - a Find/Change result is grouped by story, the way KCM's
//  Story mode lists them. !It is named "font" / FontGroup because it once held fonts (for the Find
//  Missing Glyphs scan, since removed); the names were kept. A story row has no menu of its own - only a
//  hit row has one (KFCResultNodeEH::RButtonDn).
//
//  hit stays the CHAPTER-wide index, not a position inside the font group. Everything that asks
//  the model about a hit - the row's drawing, the jump, the replace - names it that
//  way, and this level is a way of DISPLAYING those hits, not a renumbering of them.
//
//  The root sits at -2 rather than -1 precisely so the book row can have -1: anything that means
//  "the root" must go through CreateRoot(). A place still saying Create(-1) would now be naming
//  the book row, and a parent that is its own child is an infinite descent.
//
//  The node's data (book name, chapter name / count, font name, hit text segments) is looked up
//  from KFCResultModel by these indices when needed, so nodes stay tiny and a rebuild after a new
//  search is just ClearTree + ChangeRoot (+ re-expanding). Ported from KESCL's KESCLResultNodeID -
//  itself modelled on the paneltreeview sample's PnlTrvFileNodeID.
//
//========================================================================================

#ifndef __KFCResultNodeID_h__
#define __KFCResultNodeID_h__

#include "NodeID.h"
#include "IPMStream.h"
#include "PMString.h"
#include "KFCUIID.h"
#include "KFCModelAccess.h"		// the model half, through its session interfaces

/** One node of the result tree: (chapter index, font group, hit index). See the file comment for
    the five shapes a node can take. */
class KFCResultNodeID : public NodeIDClass
{
public:
	enum { kNodeType = kKFCResultListWidgetBoss };

	/** The generic node the tree-view framework asks for (GetGenericNodeID) - the root's shape. */
	static NodeID_rv Create() { return new KFCResultNodeID(); }

	/** The hidden root. Use this rather than Create(-1), which now names the book row. */
	static NodeID_rv CreateRoot() { return new KFCResultNodeID(-2, -1, -1); }

	/** The book row. Only ever asked for while KFCResultModel::IsFromBook() is true. */
	static NodeID_rv CreateBook() { return new KFCResultNodeID(-1, -1, -1); }

	/** A document row ('chapter' = 0-based chapter index). */
	static NodeID_rv Create(int32 chapter) { return new KFCResultNodeID(chapter, -1, -1); }

	/** A FONT row under chapter 'chapter' ('font' indexes that chapter's fontGroups). */
	static NodeID_rv CreateFont(int32 chapter, int32 font) { return new KFCResultNodeID(chapter, font, -1); }

	/** A hit row under chapter 'chapter' ('hit' is the index into that CHAPTER's hits).

	    The font group is looked up here rather than passed in, and that is the whole point of
	    having it on the node at all: identity runs through Compare, which sees every field, so two
	    nodes naming the same hit MUST carry the same font. One place to derive it is one place to
	    get it right - a caller filling it in itself is a caller that can fill it in wrong, and a
	    tree holding two identities for one row loses selections and expansion state in ways that
	    look random.

	    The lookup is bounds-checked at the model end and answers -1 for anything it cannot resolve,
	    which is what lets nodes be made while the model is empty (during ClearTree, or straight after
	    Clear). */
	static NodeID_rv Create(int32 chapter, int32 hit)
	{
		const int32 font = KFCResults()->GetHitFontGroup(chapter, hit);
		return new KFCResultNodeID(chapter, font, hit);
	}

	virtual ~KFCResultNodeID() {}

	virtual NodeType GetNodeType() const { return kNodeType; }

	virtual int32 Compare(const NodeIDClass* nodeID) const
	{
		const KFCResultNodeID* other = static_cast<const KFCResultNodeID*>(nodeID);
		// Nothing hands this a nil - a NodeID owns its NodeIDClass and clones it on every copy
		// (NodeID.h:135, 193) - and the two official implementations do not guard at all
		// (paneltreeview's asserts and dereferences anyway; widgetbin's IntNodeID just
		// dereferences). The guard stays because an assert is not a guard in a release build, but
		// it answers "not equal" rather than the 0 it used to: 0 is the one answer that would make
		// the tree treat an unusable node as THIS row, and equality is the last thing a missing
		// node should be able to claim. Which side it falls on does not matter - only that it is
		// not the same side as this.
		if (other == nil)
			return 1;
		if (fChapter < other->fChapter)	return -1;
		if (fChapter > other->fChapter)	return 1;
		if (fFont < other->fFont)	return -1;
		if (fFont > other->fFont)	return 1;
		if (fHit < other->fHit)	return -1;
		if (fHit > other->fHit)	return 1;
		return 0;
	}

	virtual NodeIDClass* Clone() const { return new KFCResultNodeID(fChapter, fFont, fHit); }

	virtual void Read(IPMStream* stream)
	{
		stream->XferInt32(fChapter);
		stream->XferInt32(fFont);
		stream->XferInt32(fHit);
	}

	virtual void Write(IPMStream* stream) const
	{
		stream->XferInt32(const_cast<KFCResultNodeID*>(this)->fChapter);
		stream->XferInt32(const_cast<KFCResultNodeID*>(this)->fFont);
		stream->XferInt32(const_cast<KFCResultNodeID*>(this)->fHit);
	}

	/** The chapter's 0-based index into KFCResultModel (negative = root or book row). */
	int32 GetChapter() const { return fChapter; }

	/** The font (story) group this row belongs to, or -1 on a row above the story level (and on a hit
	    the model could not resolve). */
	int32 GetFont() const { return fFont; }

	/** The hit index within that chapter (-1 = this is NOT a hit row). */
	int32 GetHit() const { return fHit; }

	/** Is this a hit row (a leaf)? */
	bool16 IsHitRow() const { return fHit >= 0; }

	/** Is this a FONT row - the STORY row (the level's old name - see the file comment)? */
	bool16 IsFontRow() const { return fChapter >= 0 && fFont >= 0 && fHit < 0; }

	/** Is this the book row - the one that names the book the results came from? */
	bool16 IsBookRow() const { return fChapter == -1 && fHit < 0; }

	/** Is this the hidden root? */
	bool16 IsRoot() const { return fChapter <= -2; }

	/** Debug aid, like the samples: makes tree-view asserts name the node. */
	virtual PMString GetDescription() const
	{
		PMString s("KFCResultRow ");
		s.AppendNumber(fChapter);
		if (fFont >= 0)
		{
			s.Append("/f");
			s.AppendNumber(fFont);
		}
		if (fHit >= 0)
		{
			s.Append(":");
			s.AppendNumber(fHit);
		}
		s.SetTranslatable(kFalse);
		return s;
	}

private:
	// Private constructors force the factory methods, PnlTrvFileNodeID-style.
	KFCResultNodeID() : fChapter(-2), fFont(-1), fHit(-1) {}
	KFCResultNodeID(int32 chapter, int32 font, int32 hit)
		: fChapter(chapter), fFont(font), fHit(hit) {}

	int32 fChapter;
	int32 fFont;
	int32 fHit;
};

#endif // __KFCResultNodeID_h__
