//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  NodeID class for the result tree. A node is the pair (chapter index, hit index):
//
//    (-2, -1)        the hidden root
//    (-1, -1)        the BOOK row       -> present only while the results came from a book search
//    (chap, -1)      a document row     -> index into KBSResultModel's chapters
//    (chap, hit)     a hit row          -> hit indexes that chapter's hits
//
//  The book row is what tells the user WHICH book was searched, permanently and in the panel
//  itself rather than in a status line that the next message overwrites. A document-scope search
//  has no book row, so its tree is the two levels it has always been.
//
//  The root sits at -2 rather than -1 precisely so the book row can have -1: anything that means
//  "the root" must go through CreateRoot(). A place still saying Create(-1) would now be naming
//  the book row, and a parent that is its own child is an infinite descent.
//
//  The node's data (book name, chapter name / count, hit text segments) is looked up from
//  KBSResultModel by these indices when needed, so nodes stay tiny and a rebuild after a new
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

/** One node of the result tree: (chapter index, hit index). See the file comment for the four
    shapes a node can take. */
class KBSResultNodeID : public NodeIDClass
{
public:
	enum { kNodeType = kKBSResultListWidgetBoss };

	/** The generic node the tree-view framework asks for (GetGenericNodeID) - the root's shape. */
	static NodeID_rv Create() { return new KBSResultNodeID(); }

	/** The hidden root. Use this rather than Create(-1), which now names the book row. */
	static NodeID_rv CreateRoot() { return new KBSResultNodeID(-2, -1); }

	/** The book row. Only ever asked for while KBSResultModel::IsFromBook() is true. */
	static NodeID_rv CreateBook() { return new KBSResultNodeID(-1, -1); }

	/** A document row ('chapter' = 0-based chapter index). */
	static NodeID_rv Create(int32 chapter) { return new KBSResultNodeID(chapter, -1); }

	/** A hit row under chapter 'chapter'. */
	static NodeID_rv Create(int32 chapter, int32 hit) { return new KBSResultNodeID(chapter, hit); }

	virtual ~KBSResultNodeID() {}

	virtual NodeType GetNodeType() const { return kNodeType; }

	virtual int32 Compare(const NodeIDClass* nodeID) const
	{
		const KBSResultNodeID* other = static_cast<const KBSResultNodeID*>(nodeID);
		if (other == nil)
			return 0;
		if (fChapter < other->fChapter)	return -1;
		if (fChapter > other->fChapter)	return 1;
		if (fHit < other->fHit)	return -1;
		if (fHit > other->fHit)	return 1;
		return 0;
	}

	virtual NodeIDClass* Clone() const { return new KBSResultNodeID(fChapter, fHit); }

	virtual void Read(IPMStream* stream)
	{
		stream->XferInt32(fChapter);
		stream->XferInt32(fHit);
	}

	virtual void Write(IPMStream* stream) const
	{
		stream->XferInt32(const_cast<KBSResultNodeID*>(this)->fChapter);
		stream->XferInt32(const_cast<KBSResultNodeID*>(this)->fHit);
	}

	/** The chapter's 0-based index into KBSResultModel (negative = root or book row). */
	int32 GetChapter() const { return fChapter; }

	/** The hit index within that chapter (-1 = this is NOT a hit row). */
	int32 GetHit() const { return fHit; }

	/** Is this a hit row (a leaf)? */
	bool16 IsHitRow() const { return fHit >= 0; }

	/** Is this the book row - the one that names the book the results came from? */
	bool16 IsBookRow() const { return fChapter == -1 && fHit < 0; }

	/** Is this the hidden root? */
	bool16 IsRoot() const { return fChapter <= -2; }

	/** Debug aid, like the samples: makes tree-view asserts name the node. */
	virtual PMString GetDescription() const
	{
		PMString s("KBSResultRow ");
		s.AppendNumber(fChapter);
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
	KBSResultNodeID() : fChapter(-2), fHit(-1) {}
	KBSResultNodeID(int32 chapter, int32 hit) : fChapter(chapter), fHit(hit) {}

	int32 fChapter;
	int32 fHit;
};

#endif // __KBSResultNodeID_h__
