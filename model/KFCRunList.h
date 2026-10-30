//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE LIST A SAVED QUERY RUN LEAVES (docs/superpowers/specs/2026-10-30-kfc-run-result-list-design.md): one row a place it wrote,
//  reading what the place has been (cat -> dog -> kog, the author's D1-D4) and the queries that wrote it. InDesign's Change
//  All says how many it wrote and nothing of where, so before each one the same walk selects the same matches (the design's
//  M2), and after it they are placed in the text it left (KFCRunListAlign) and laid over the places so far (KFCRunListChain).
//  NOTHING IS GUESSED (R4): a document whose find and Change All do not agree, or whose text cannot be laid, is left out of
//  the list and said - a row pointing at the wrong place is worse than no row. The run itself goes on either way: nothing
//  here stops, slows the decision of, or changes what a run writes.
//  The main thread only, inside the run (KFCQuerySequence.cpp).
//
//========================================================================================

#ifndef __KFCRunList_h__
#define __KFCRunList_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WalkerScopeOptions.h"
#include "KFCBookScope.h"		// ChapterDoc - the run's documents
#include "KFCModelTypes.h"		// KFCResultModel::SearchScopeKind
#include "KFCRunListChain.h"

#include <map>
#include <string>
#include <vector>

namespace KFCRunList
{
	class Recorder
	{
	public:
		/** queryNames = the run's queries, in its order, named as the dialog names them; targets = its documents, opened. */
		Recorder(const std::vector<PMString>& queryNames, const std::vector<KFCBookScope::ChapterDoc>& targets);

		/** Just before query `query` is written in document `target`, inside the run's sequence: the walk that write takes
		    (KFCChangeAll::InitWalk), every match selected and kept, and the text of each story holding one. Find/Change holds
		    that query now. Leaves no error standing, and a failure here is the list's alone: the document is left out. */
		void RecordBefore(size_t target, int32 query, int32 writeScope, const WalkerScopeOptions& options);

		/** Just after it: count = what its Change All says it wrote. The two agree, and each story's text is laid - or the
		    document is left out, by name and with the reason. */
		void RecordAfter(size_t target, int32 query, int32 count);

		/** An Object query ran: written and counted, not listed (R11) - said. */
		void NoteObjectQuery();

		/** The run stands, its documents left as they are left (LeaveRunDocuments): each place read against the text as it
		    stands, a row made of it and filed into the model - which the caller has just cleared - under a header naming the
		    list a query run's (kKFCRunListSearchMode, RunNames), up to the rows a list holds (R9). Returns the rows filed;
		    none: no header, the panel stays empty. outWords = what the message line says of it, each sentence with a space
		    in front ("" when there is nothing to say). */
		int32 FileList(bool fromBook, bool allDocuments, KFCResultModel::SearchScopeKind scope, const PMString& bookName,
			PMString& outWords);

	private:
		/** One story's places, and what RecordBefore kept of it for RecordAfter. */
		struct Story
		{
			std::vector<KFCRunListChain::Place>		places;
			std::vector<KFCRunListAlign::Match>		matches;	// this query's, in place order
			std::u32string							before;		// its text before this query's write
		};
		/** One document of the run: a book listing one file twice is one document here (its writes move one text). */
		struct Doc
		{
			size_t					target;		// its first place in targets - what its rows are filed under
			bool					out;		// left out of the list
			PMString				why;		// ...and what the message line says of it
			int32					found;		// this query's matches
			bool					walked;		// RecordBefore kept them
			KFCRunListAlign::Writes	writes;		// what this query writes
			std::map<UID, Story>	stories;
			Doc() : target(0), out(false), found(0), walked(false), writes(KFCRunListAlign::kAnyLength) {}
		};
		Doc& DocOf(size_t target);
		void LeaveOut(Doc& doc, const PMString& why);

		std::vector<PMString>						fNames;
		std::vector<KFCBookScope::ChapterDoc>		fTargets;
		std::vector<Doc>							fDocs;
		std::vector<size_t>							fDocOfTarget;
		bool										fObjectQuery;

		Recorder(const Recorder&);
		Recorder& operator=(const Recorder&);
	};
}

#endif // __KFCRunList_h__

// End, KFCRunList.h.
