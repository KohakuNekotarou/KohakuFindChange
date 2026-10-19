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
#include "ITextDataValidation.h"	// RangeError - what IPageList answers a page range with
#include "KFCModelTypes.h"		// KFCNarrowStep
#include "KFCResultModel.h"		// Hit

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace KFCNarrow
{
	/** WHERE A MATCH'S TEXT STANDS (In Tables / In Footnotes / In Endnotes): read off the story thread holding `at` -
	    the owner of its thread dictionary (ITextStoryThread::GetDictUID: a table's cells are threads of
	    kTableModelBoss, a footnote's text one of kFootnoteReferenceBoss - the iid-boss dictionary's owners of
	    IID_ITEXTSTORYTHREADDICT) - and whether the story is an endnote's (Facade::IEndnoteFacade::IsEndnoteStory - a
	    story of kEndnoteStoryBoss). Written onto the row; nothing for a position it cannot read. */
	void ReadTextPlace(const UIDRef& storyRef, TextIndex at, KFCResultModel::Hit& io);

	/** PAGES... - A PAGE RANGE READ IN ONE DOCUMENT (the spec's N5): as the Print dialog reads its Range - "1-5, 8", a section's
	    name ("Sec1:3"), an absolute number ("+3") - through IPageList::PageRangeStringToUIDList (SnpPrintDocument.cpp's call),
	    the document's own numbering (kDefaultPageType: as the preferences show page numbers - a book's chapter by its own
	    Pages panel). outPages = the pages named (empty unless kNoError). */
	ITextDataValidation::RangeError ReadPageRange(IDataBase* db, const PMString& pages, std::set<UID>& outPages);

	/** FRAMES OF THESE MATCHES - THE FRAME THE PERSON SELECTS for a match's own frame (the parcel's frame - a text frame's
	    column, a frame on a path), by the official two steps: the path itself for text on a path
	    (ITOPFrameData::GetMainSplineItemUID), else the spline of the column's multi-column item
	    (ITextUtils::QuerySplineFromTextFrame - a text frame's spline, an inline's frame). kInvalidUID for none. */
	UID FrameOwner(IDataBase* db, UID matchFrame);

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
		/** FRAMES OF THESE MATCHES' BOOKKEEPING over one search: a frame already made a row (one row a frame, whatever its
		    matches - N5) - FirstFrame answers true once a frame, in its document; a match that has no frame (overset text) is
		    counted (NoteNoFrame) for the status line. */
		bool FirstFrame(IDataBase* db, UID frame) const;
		void NoteNoFrame() const { ++fNoFrame; }
		int32 NoFrameCount() const { return fNoFrame; }

		/** A TEXT ROW, built (its page, its flags, its place) in document `db`: does it meet every condition taken before a
		    Frames step - every condition, when there is none? */
		bool KeepText(const KFCResultModel::Hit& hit, IDataBase* db) const;
		/** AN OBJECT ROW in document `db` - an Object tab search's, or a frame a Frames step made: does it meet every condition
		    that asks of an object (those after a Frames step, or every one of an Object tab's list)? */
		bool KeepObject(const KFCResultModel::Hit& hit, IDataBase* db) const;

		/** ON ACTIVE LAYER'S LAYER: the name of the list's first On Active Layer step (empty for none) - and the documents that
		    had a row to ask and no layer of that name (their names, in the order met: the status line says them). */
		PMString LayerName() const;
		const std::vector<PMString>& NoLayerDocs() const { return fNoLayerDocs; }

	private:
		bool Meets(size_t k, const KFCResultModel::Hit& hit, bool objectRow, IDataBase* db) const;
		/** Step k's page range as document db reads it - read once a document (ReadPageRange): a document that cannot read
		    it lists nothing for it. */
		const std::set<UID>& PagesOf(size_t k, IDataBase* db) const;
		/** Step k's layer in document db - its layer of the step's name, looked up once a document; kInvalidUID when it has none
		    (its rows are not kept, and its name is noted - NoLayerDocs). */
		UID LayerOf(size_t k, IDataBase* db) const;
		// THE CACHES ARE KEYED BY A DOCUMENT'S IDENTITY, NOT ITS IDataBase* (2026-10-18): a book's run opens its chapters one at
		//  a time and closes each before the next with kProcess (KFCBookScope::HandBackHeldDocNow), so a chapter opened after
		//  another was closed can be given the closed one's address - and chapters made alike share page and frame UIDs (three
		//  of them measured: page 243, frame 272 in each). Identity is the document's file (memory uidref-reuse-after-close);
		//  a document with no file (never saved - never one of a run's chapters, so never closed under a run) by its address.
		static std::string DocKey(IDataBase* db);
		std::vector<KFCNarrowStep>	fSteps;
		size_t						fFramesAt;		// the index of the Frames step - fSteps.size() when there is none
		mutable std::map<std::pair<size_t, std::string>, std::set<UID> >	fPages;		// PagesOf's reading, by step and document
		mutable std::map<std::pair<size_t, std::string>, UID>			fLayers;	// LayerOf's reading, by step and document
		mutable std::set<std::pair<std::string, UID> >	fFrames;	// the frames made rows (FirstFrame)
		mutable int32									fNoFrame;	// the matches with no frame (NoteNoFrame)
		mutable std::vector<PMString>					fNoLayerDocs;	// the documents with no layer of the name (LayerOf)
	};

	/** Is this a condition only a text row can meet (In Tables / In Footnotes / In Endnotes / Frames of These Matches)? */
	bool IsTextOnly(int32 kind);
	/** A step's words, as the menu names it ("In Tables") - the status line's refusals. */
	PMString StepWords(const KFCNarrowStep& step);
}

#endif // __KFCNarrow_h__

// End, KFCNarrow.h.
