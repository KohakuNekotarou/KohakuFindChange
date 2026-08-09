//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  NodeID class for the result tree. A node is the triple (chapter index, font group, hit index):
//
//    (-2, -1, -1)        the hidden root
//    (-1, -1, -1)        the BOOK row       -> present only while the results came from a book search
//    (chap, -1, -1)      a document row     -> index into KBSResultModel's chapters
//    (chap, font, -1)    a FONT row         -> only under a chapter whose hits name a font
//    (chap, font, hit)   a hit row          -> hit indexes that CHAPTER's hits; font is -1 when the
//                                              chapter has no font groups
//
//  The book row is what tells the user WHICH book was searched, permanently and in the panel
//  itself rather than in a status line that the next message overwrites. A document-scope search
//  has no book row, so its tree is one level shallower.
//
//  The FONT level (2026-08-02) exists because a box means "this font has no glyph for this
//  character": the font is the unit a fix applies to, so it is the unit the panel groups by and
//  the unit Check All reaches. Only a missing-glyph scan names fonts - a Find/Change result names
//  none, its chapters have no groups, and its tree is the three levels it has always had.
//
//  ***** hit stays the CHAPTER-wide index, not a position inside the font group. Everything that asks
//  the model about a hit - the row's drawing, the jump, the check box, the replace - names it that
//  way, and this level is a way of DISPLAYING those hits, not a renumbering of them.
//
//  The root sits at -2 rather than -1 precisely so the book row can have -1: anything that means
//  "the root" must go through CreateRoot(). A place still saying Create(-1) would now be naming
//  the book row, and a parent that is its own child is an infinite descent.
//
//  The node's data (book name, chapter name / count, font name, hit text segments) is looked up
//  from KBSResultModel by these indices when needed, so nodes stay tiny and a rebuild after a new
//  search is just ClearTree + ChangeRoot (+ re-expanding). Ported from KESCL's KESCLResultNodeID -
//  itself modelled on the paneltreeview sample's PnlTrvFileNodeID.
//
//========================================================================================

#ifndef __KBSResultNodeID_h__
#define __KBSResultNodeID_h__

#include "NodeID.h"
#include "IPMStream.h"
#include "PMString.h"
#include "KBSID.h"
#include "KBSResultModel.h"		// GetHitFontGroup - a hit row derives its font group from the model

/** One node of the result tree: (chapter index, font group, hit index). See the file comment for
    the five shapes a node can take. */
class KBSResultNodeID : public NodeIDClass
{
public:
	enum { kNodeType = kKBSResultListWidgetBoss };

	/** The generic node the tree-view framework asks for (GetGenericNodeID) - the root's shape. */
	static NodeID_rv Create() { return new KBSResultNodeID(); }

	/** The hidden root. Use this rather than Create(-1), which now names the book row. */
	static NodeID_rv CreateRoot() { return new KBSResultNodeID(-2, -1, -1); }

	/** The book row. Only ever asked for while KBSResultModel::IsFromBook() is true. */
	static NodeID_rv CreateBook() { return new KBSResultNodeID(-1, -1, -1); }

	/** A document row ('chapter' = 0-based chapter index). */
	static NodeID_rv Create(int32 chapter) { return new KBSResultNodeID(chapter, -1, -1); }

	/** A FONT row under chapter 'chapter' ('font' indexes that chapter's fontGroups). */
	static NodeID_rv CreateFont(int32 chapter, int32 font)
	{
		return new KBSResultNodeID(chapter, font, -1);
	}

	/** A hit row under chapter 'chapter' ('hit' is the index into that CHAPTER's hits).

	    The font group is looked up here rather than passed in, and that is the whole point of
	    having it on the node at all: identity runs through Compare, which sees every field, so two
	    nodes naming the same hit MUST carry the same font. One place to derive it is one place to
	    get it right - a caller filling it in itself is a caller that can fill it in wrong, and a
	    tree holding two identities for one row loses selections and expansion state in ways that
	    look random.

	    A chapter with no groups answers -1, which is exactly the value this node carried before the
	    font level existed, so every existing caller keeps working unchanged. The lookup is
	    bounds-checked at the model end and answers -1 for anything it cannot resolve, which is what
	    lets nodes be made while the model is empty (during ClearTree, or straight after Clear). */
	static NodeID_rv Create(int32 chapter, int32 hit)
	{
		return new KBSResultNodeID(chapter, KBSResultModel::GetHitFontGroup(chapter, hit), hit);
	}

	virtual ~KBSResultNodeID() {}

	virtual NodeType GetNodeType() const { return kNodeType; }

	virtual int32 Compare(const NodeIDClass* nodeID) const
	{
		const KBSResultNodeID* other = static_cast<const KBSResultNodeID*>(nodeID);
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

	virtual NodeIDClass* Clone() const { return new KBSResultNodeID(fChapter, fFont, fHit); }

	virtual void Read(IPMStream* stream)
	{
		stream->XferInt32(fChapter);
		stream->XferInt32(fFont);
		stream->XferInt32(fHit);
	}

	virtual void Write(IPMStream* stream) const
	{
		stream->XferInt32(const_cast<KBSResultNodeID*>(this)->fChapter);
		stream->XferInt32(const_cast<KBSResultNodeID*>(this)->fFont);
		stream->XferInt32(const_cast<KBSResultNodeID*>(this)->fHit);
	}

	/** The chapter's 0-based index into KBSResultModel (negative = root or book row). */
	int32 GetChapter() const { return fChapter; }

	/** The font group this row belongs to, or -1 when its chapter has no groups. */
	int32 GetFont() const { return fFont; }

	/** The hit index within that chapter (-1 = this is NOT a hit row). */
	int32 GetHit() const { return fHit; }

	/** Is this a hit row (a leaf)? */
	bool16 IsHitRow() const { return fHit >= 0; }

	/** Is this a FONT row - the level that names which font had no glyph? */
	bool16 IsFontRow() const { return fChapter >= 0 && fFont >= 0 && fHit < 0; }

	/** Is this the book row - the one that names the book the results came from? */
	bool16 IsBookRow() const { return fChapter == -1 && fHit < 0; }

	/** Is this the hidden root? */
	bool16 IsRoot() const { return fChapter <= -2; }

	/** Debug aid, like the samples: makes tree-view asserts name the node. */
	virtual PMString GetDescription() const
	{
		PMString s("KBSResultRow ");
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
	KBSResultNodeID() : fChapter(-2), fFont(-1), fHit(-1) {}
	KBSResultNodeID(int32 chapter, int32 font, int32 hit) : fChapter(chapter), fFont(font), fHit(hit) {}

	int32 fChapter;
	int32 fFont;
	int32 fHit;
};

#endif // __KBSResultNodeID_h__
