//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  THE RUBY TAB, LISTED (docs/superpowers/specs/2026-10-31-kfc-ruby-tab-design.md - the Table tab's shape, KFCTableSearch.h). Ruby is no text: it rides a strand
//  beside the story (IRubyAttrStrand) and InDesign's Find/Change has no condition for it, nor a change format for its reading. While KFC's own tab in
//  Edit > Find/Change - "Ruby" - is the tab Find/Change is on (IsTabCurrent), the panel's Find lists the text with ruby (its base and its reading each "contains"), or
//  the occurrences of a text without ruby, or - Ruby Marks in Text - ruby a manuscript wrote out: a bar, a base, and its reading in book title marks, as Aozora Bunko writes it (the bar
//  full-width or ASCII; with none, the kanji just before the reading - KFCRubyMarkText.h), or - Words in a Ruby List - the words of a file, each with the readings its line gives it (KFCRubyList.h).
//  A row is a TEXT row on the ruby's run (on the mark); what it draws there is the base and the reading
//  (Hit::runChain - "base<reading>"; a mark's row draws its text - Hit::upper the reading above it).
//  Its Replace gives the row what the tab's Change boxes say (KFCRubyChangePlan.h - WritePlan), and takes its ruby off when they read nothing (Remove below - KFCReplaceEngine's own-tab rows). A reading a
//  person cannot see is no ruby: a footnote's marker inherits one. Text a condition hides is not searched. No setting of Find/Change's is read or touched. The main thread only.
//
//========================================================================================

#ifndef __KFCRubySearch_h__
#define __KFCRubySearch_h__

#include "PMString.h"
#include "UIDRef.h"
#include "WideString.h"

#include <map>
#include <set>
#include <vector>

#include "KFCBookScope.h"		// ChapterDoc, SkippedChapter - a run's targets
#include "KFCModelTypes.h"		// KFCRubyConditions
#include "KFCResultModel.h"		// Hit - a row
#include "KFCRubyChangePlan.h"	// what the Change boxes write over a row's text
#include "KFCSearchEngine.h"	// CollectTally - a walk's count

namespace KFCRubySearch
{
	/** IS THE RUBY TAB FIND/CHANGE'S TAB NOW, and what it asks (outConditions, when given - the tab's as it stands, whichever tab is shown: a row's Replace reads its
	    Change side). Asked of the UI half (IKFCUIServices::RubyTabConditions - ui/KFCKohakuTab.h); no UI half = not the Ruby tab. A TEST BUILD asks its fault switch
	    ruby-tab first: on = the Ruby tab, its file's lines kind=without, base=, ruby=, change=<a reading over the row's whole text>, piece=<base>=<reading> (a box, in order), remove=1 (no box reads
	    anything), scope=all|story|toend|selection, first=story|document|book, on=<icon>, off=<icon> (KFCOwnScopeDiagLine) the conditions. */
	bool IsTabCurrent(KFCRubyConditions* outConditions = nil);

	/** The Change boxes as KFCRubyChangePlan takes them. */
	std::vector<KFCRubyChangePlan::Piece> PlanPieces(const KFCRubyConditions& asked);

	/** What the boxes write over [start, end) of the story: the plan's answer, runs by code point from start. */
	KFCRubyChangePlan::Result PlanForRow(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Piece>& pieces, std::vector<KFCRubyChangePlan::Run>& outRuns, const KFCOwnSwitches& switches);

	/** WHAT A RUBY LIST WRITES OVER [start, end) of the story: its word's runs - as the list's search compared them (switches: Kana Sensitive, Width Sensitive) - or false: the text there is no word of the list's (a row edited since). */
	bool ListPlanForRow(const KFCRubyList::Words& words, const KFCOwnSwitches& switches, const UIDRef& story, TextIndex start, TextIndex end, std::vector<KFCRubyChangePlan::Run>& outRuns);

	/** Does [start, end) of the story read as the runs say - each its reading, nothing between them? Asked before a write: a row that does is not written. */
	bool ReadsAsPlanned(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Run>& runs);

	/** WRITES THE RUNS OVER [start, end) OF THE STORY inside the caller's step: ruby off the characters between them that have any (Remove), then each by Apply. kSuccess = done. */
	ErrorCode WritePlan(const UIDRef& story, TextIndex start, TextIndex end, const std::vector<KFCRubyChangePlan::Run>& runs);

	/** WHAT STANDS ABOVE A ROW ON [start, end): its readings, by characters from start. */
	KFCRowUpper UpperOn(const UIDRef& story, TextIndex start, TextIndex end);

	/** THE TAB'S WALK OVER A RUN'S TARGETS (KFCTableSearch::CollectTargets' shape). part (KFCSearchEngine::PartStretches): a run with ruby its stretches touch - the reading is over the whole of it; an
	    occurrence or a mark they hold whole, as a text search finds none a selection cuts. nil = the whole of each target. */
	void CollectTargets(std::vector<KFCBookScope::ChapterDoc>& targets, bool fromBook, std::vector<KFCBookScope::SkippedChapter>& unopenable,
		KFCSearchEngine::CollectTally& out, int32 reserved, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part = nil);

	/** One document's rows again (Search This Document Again). False = cancelled or nothing to walk with. */
	bool WalkOneDocument(const UIDRef& docRef, const PMString& name, size_t limit, const KFCRubyConditions& asked,
		std::vector<KFCResultModel::Hit>& outHits, std::map<UID, uint32>& outVersions, bool& outCapped, bool& outCancelled);

	/** Is there a reading a person can see anywhere on [start, end) of the story (no strand, the ruby off, or a reading of control characters = none)? */
	bool HasRubyOn(const UIDRef& story, TextIndex start, TextIndex end);

	/** What a row's words are for [start, end): each ruby's "base<reading>" (the book title marks U+300A and U+300B) and the text between them, each cut to what a row holds; empty with no ruby on it
	    (the row draws its own text). A reader of the widgets and Copy read these; the row draws Hit::upper over its text. */
	PMString RowWords(const UIDRef& story, TextIndex start, TextIndex end);

	/** Gives [start, end) the reading, inside the caller's step: its strand made first if the story has none; the flag and the string written, the ruby's look left alone. Several characters
	    are made a group ruby - a reading over the whole of them - whatever they were: a per-character ruby holding a reading with no space puts all of it on the first character, with no word from
	    InDesign (the spec's F19). One character's kind is left alone: over one, the two compose the same. kSuccess = done. */
	ErrorCode Apply(const UIDRef& story, TextIndex start, TextIndex end, const WideString& reading);

	/** IS [start, end) OF THE STORY ONE WHOLE MARK STILL (a row's door)? out* = what it holds, by its offset from start. */
	bool ReadMark(const UIDRef& story, TextIndex start, TextIndex end, int32& outBaseOffset, int32& outBaseLength, WideString& outReading);

	/** WRITES THE MARK AT `at` (its length characters), inside the caller's step: the reading's marks and the bar taken out of the text, and the reading put on the base as Apply puts it.
	    outBaseLength = what is left at `at`. kFailure with outStillThere: a deletion left the text where it was - Track Changes on in the story, which keeps deleted text. */
	ErrorCode WriteMark(const UIDRef& story, TextIndex at, int32 length, int32& outBaseLength, bool& outStillThere);

	/** A QUERY RUN'S WRITE OF ONE DOCUMENT (KFCKohakuQuery::WriteDocument): what the conditions list, given their Change side (ruby from marks: its mark's), with no rows. outCount = the places written. False = InDesign would
	    not write one - Track Changes keeps the marks a run would take out (WriteMark). ioBookSeen: a list's First in Book over a book's chapters - the query's one set over the run's documents (nil: each document's own). */
	bool WriteDocument(const UIDRef& docRef, const KFCRubyConditions& asked, const std::vector<KFCSearchEngine::ListedPlace>* part, int32& outCount, std::set<size_t>* ioBookSeen = nil);

	/** Takes the ruby off [start, end) inside the caller's step: every ruby attribute's override removed - the official snippet only turns the flag off, which leaves the
	    reading behind (KIDMCPRuby.cpp's ClearRubyOn, the same thirty). A style cannot give text a ruby - it has no flag and no reading (measured, 10-31), unlike a kenten's kind - so nothing is left to turn off; a style's look stays. kSuccess = done. */
	ErrorCode Remove(const UIDRef& story, TextIndex start, TextIndex end);
}

#endif // __KFCRubySearch_h__

// End, KFCRubySearch.h.
