//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RUBY TAB, LISTED (docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md - the Table tab's shape, KFCTableSearch.h). Ruby is no text: it rides a strand
//  beside the story (IRubyAttrStrand) and InDesign's Find/Change has no condition for it, nor a change format for its reading. While KFC's own tab in
//  Edit > Find/Change - "Ruby" - is the tab Find/Change is on (IsTabCurrent), the panel's Find lists the text with ruby (its base and its reading each "contains"), or
//  the occurrences of a text without ruby. A row is a TEXT row on the ruby's run; what it draws there is the base and the reading (Hit::runChain - "base<reading>").
//  Its Replace gives the run the reading the tab's Change side says, or takes the ruby off (KFCReplaceEngine's own-tab rows - Apply, Remove below). A reading a person
//  cannot see is no ruby: a footnote's marker inherits one. Text a condition hides is not searched. No setting of Find/Change's is read or touched. The main thread only.
//
//========================================================================================

#ifndef __KFCRubySearch_h__
#define __KFCRubySearch_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WideString.h"

#include <map>
#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCRubyConditions
#include "KFCResultModel.h"		// Hit - a row
#include "KFCSearchEngine.h"	// CollectTally - a walk's count

namespace KFCRubySearch
{
	/** IS THE RUBY TAB FIND/CHANGE'S TAB NOW, and what it asks (outConditions, when given - the tab's as it stands, whichever tab is shown: a row's Replace reads its
	    Change side). Asked of the UI half (IKFCUIServices::RubyTabConditions - ui/KFCRubyTab.h); no UI half = not the Ruby tab. A TEST BUILD asks its fault switch
	    ruby-tab first: on = the Ruby tab, its file's lines kind=without, base=, ruby=, change=, remove=1, scope=all the conditions. */
	bool IsTabCurrent(KFCRubyConditions* outConditions = nil);

	/** THE TAB'S WALK OVER A RUN'S TARGETS (KFCTableSearch::CollectTargets' shape). */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCRubyConditions& asked);

	/** One document's rows again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCRubyConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** The reading a person can see on [start, end) of the story - empty for none (no strand, the ruby off, or a reading of control characters). */
	WideString ReadingOn(const UIDRef& story, TextIndex start, TextIndex end);

	/** What a row draws for a run: "base<reading>" (the book title marks U+300A and U+300B), cut to what a row holds; empty with no reading (the row draws its own text). */
	PMString RowWords(const UIDRef& story, TextIndex start, TextIndex end, const WideString& reading);

	/** Gives [start, end) the reading, inside the caller's step: its strand made first if the story has none; the flag and the string written, the ruby's look left alone. A run
	    that had no ruby and has several characters is made a group ruby (its reading over the whole of it); one that had keeps what it was. kSuccess = done. */
	ErrorCode Apply(const UIDRef& story, TextIndex start, TextIndex end, const WideString& reading);

	/** Takes the ruby off [start, end) inside the caller's step: every ruby attribute's override removed - the official snippet only turns the flag off, which leaves the
	    reading behind (KIDMCPRuby.cpp's ClearRubyOn, the same thirty). kSuccess = done. */
	ErrorCode Remove(const UIDRef& story, TextIndex start, TextIndex end);
}

#endif // __KFCRubySearch_h__

// End, KFCRubySearch.h.
