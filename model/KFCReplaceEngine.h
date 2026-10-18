//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  Replace engine: replaces the rows the user asks for in the result panel - a row's right-click menu or Return, one
//  row (ReplaceHit) or the rows selected together (ReplaceHits; object rows are KFCObjectReplace's) - using the CHANGE
//  string of the official Find/Change dialog. The Replace never writes that string - it uses it
//  as the dialog holds it - so GREP back-references and escapes are interpreted by InDesign's own engine and are
//  never parsed here. (What KFC does write to the panel - a value restated, the direction turned - is listed at
//  the head of KFCSearchEngine.h.)
//  ALSO HERE, shared with the object rows (KFCObjectReplace): a write's undo step (WriteStep) and what its documents are
//  left as (DocsAfterWrite), the doors before it (RefuseChangedQuery, WhyGreyed) and IsReplacing, which KFCRunGuard
//  counts; and a GREP row's after-text (PreviewHit).
//
//  HOW IT WRITES: ONE MATCH AT A TIME, A STORY AT A TIME (the author's call). ReplaceInChapterOneByOne in
//  the .cpp walks each story that holds a row asked for (kFindTextCmdBoss, then kTWReplaceTextCmdBoss on
//  the match it made current) and writes only those rows, each recognised by its thread, its offset into
//  it and its length - so a match a replacement made that the search never listed is stepped over, and
//  its row is reported missing rather than written. A GREP query holding ^ is walked backward (the
//  direction is set before anything is written, outside the sequence). Track Changes is left as each
//  story has it: KFC records nothing of its own
//  (docs/superpowers/specs/_done/2026-10-06-kfc-no-track-change-all-design.md F1). (Not InDesign's Change All
//  over each story with the rows NOT asked for taken back afterwards: that was tried and dropped - git
//  history, c876bc7 and before. Change All itself is KFCChangeAll's.)
//
//  THE CHECK BEFORE THE WRITE. What is left of the old chapter-by-chapter re-walk (measured,
//  docs/superpowers/specs/_done/2026-07-25-kbs-replace-checked-design.md section 10.1) is the check
//  it ran before anything was written (ChapterMovedUnderRows in the .cpp): the row's story is walked as
//  the search walked it, and the row asked for must still begin where the search found it.
//
//========================================================================================

#ifndef __KFCReplaceEngine_h__
#define __KFCReplaceEngine_h__

#include "PMString.h"
#include "UIDRef.h"				// DocsAfterWrite
#include "KFCModelTypes.h"		// KFCRowsByChapter
#include "KFCID.h"				// kKFCReplaceStepKey - BeginWriteStep's own name by default

#include <vector>				// DocsAfterWrite

class ICommandSequence;		// WriteStep
class IAbortableCmdSeq;

namespace KFCReplaceEngine
{

	/** Do the current Find/Change settings still describe the search the panel's results came from?

	    Two questions, most specific first:
	      1. the TAB - clicking another one returns another set of matches;
	      2. the QUERY and every switch that decides which occurrences come back - the find string,
	         case / whole word / kana / width, the five scope switches, and FIND FORMAT (a paragraph
	         style, a font, a colour). See KFCSearchEngine::BuildWalkSignature.
	    (Not "is that tab one this panel walks at all": the tab on the results is one the search could
	    state, so that would never answer no.)

	    Why it has to be asked at all: the replace walks every story that holds a row it writes with the
	    LIVE IFindChangeOptions and writes the matches it meets at the rows' places - so a query edited
	    between the search and the replace walks a different set of matches from the one the rows
	    list. The verify walk looks for the rows' matches the same way, and the reason is the same for
	    it.

	    ONE OF TWO DOORS, and they divide the ways a write can be wrong between them.

	      - the QUERY changing between the search and the replace is caught HERE, before a chapter
	        is even opened, and the write is refused;
	      - the DOCUMENT moving between the two is caught by the verify walk (ChapterMovedUnderRows in
	        the .cpp), which is the only place the document itself can be asked.

	    Both refuse before a character is written. This one is first because it is far cheaper - the
	    dialog's own settings are readable and BuildWalkSignature turns them into something
	    comparable, where the other has to walk the row's story.

	    TWO SIDE EFFECTS, both deliberate. Once the tab is the searched one it STATES it
	    (KFCSearchEngine::CommitSearchMode - every walk after this needs that, and the query has to
	    be compared on the same side of that command as the search took it), and on answer 2 it
	    CLEARS THE RESULTS: they describe a query the dialog no longer holds, so keeping them up
	    would only invite another attempt. Answer 1 leaves them alone - a tab is one click to put
	    back - and so does a tab that could not be stated. A caller that gets true back should
	    redraw the tree.

	    Call it OUTSIDE any command sequence: it processes a command of its own.

	    @param outSummary OUT the reason, ready for the status line. Cleared first.
	    @return true when the write must NOT go ahead. */
	bool RefuseChangedQuery(PMString& outSummary);

	/** RefuseChangedQuery's question WITHOUT ITS CONSEQUENCES: true when the Find/Change settings
	    still describe the search the results came from. The tab is stated as RefuseChangedQuery
	    states it - a walk after this runs in it - and NOTHING IS CLEARED whatever the answer. For a
	    caller that only has to know (the jump's look for a row an Undo moved -
	    KFCSearchEngine::LocateRow - is one), where RefuseChangedQuery would clear the whole result
	    set in the middle of the caller's work on a changed query. Same rule as RefuseChangedQuery: outside
	    any command sequence. */
	bool QueryUnchangedSinceSearch();

	/** Replace on a hit row's right-click menu, or Return (the author's call): that one row, with no prompt, in ONE
	    undo step ("Replace"). The list stays a work list: the row shows its new text, every other row is
	    moved to where its text now stands. Refused - nothing changed, outStatus says why - when the query
	    changed since the search, the row's text is not the one the search found, the chapter cannot be
	    opened, or the row would not be replaced (locked since, missing, refused). */
	bool ReplaceHit(int32 chapterIdx, int32 hitIdx, PMString& outStatus);

	/** THE ROWS SELECTED TOGETHER (the author's calls), all of one kind, of one document or several, in ONE undo step:
	    object rows by KFCObjectReplace::ReplaceRows (row by row -
	    a row that cannot be written is left and counted); text rows whose Replace is greyed are left and counted, and the
	    others are written all or nothing, as ReplaceHit writes one ("Replaced 3 rows: ID:262 #1, ID:262 #3, ID:270 #1." /
	    "Replaced 2 of 3 rows: ... Left as they were: 1 locked." / "Replaced 3 rows in 2 documents: a.indd ID:262 #1;
	    b.indd ID:270 #1."; a refusal names "a selected row"). Several documents are one abortable step around every
	    document's write (WriteStep, below). One row = ReplaceHit. */
	bool ReplaceHits(const KFCRowsByChapter& rows, PMString& outStatus);

	/** RESTORE ORIGINAL (docs/superpowers/specs/2026-10-17-kfc-row-copy-restore-design.md): is the row one to offer it to -
	    a text row a Replace of the list wrote (replaced, no item) whose copy is held (kCopyKept - the menu greys a row
	    without one, the author's wish of 2026-10-17)? Whether its copy can be put back now - its text, the rest of its
	    paragraph, a lock - is RestoreHits' to say, on the status line (the spec's C7). */
	bool CanRestoreHit(int32 chapterIdx, int32 hitIdx);

	/** RESTORE ORIGINAL over text rows - the right-clicked row, or the rows selected together it is one of: the copy each
	    one's Replace took of the text it wrote over (KFCCopyStore - ReplaceHits takes it) pasted back over what it wrote,
	    in ONE undo step ("Restore from Copy" in the Edit menu - WriteStep) that the panel follows (KFCUndoFollow). The doors,
	    row by row (C8): a replaced text row with its copy kept; its document reached (a closed chapter reopened windowless,
	    as a Replace reaches it); its story followed (its rows take their foci's places - KFCSearchEngine::FollowStoryFoci -
	    and its own focus stands exactly on its place; a story with no foci as KFC left it); what its Replace wrote still
	    there; not locked. The rows that pass are pasted in text order, each checked again just before (its run-on rest
	    too); one that cannot be yet waits for another round. Each row put back reads as before its Replace - not replaced,
	    its eye as it was (KFCResultModel::MarkHitRestored); every other row is carried to where its text stands.
	    outStatus: "Restored ID:262 #3." - "Restored 2 of 3 rows: ID:262 #1, ID:262 #3. Left as they were: 1 edited
	    since." - "Restored 3 rows in 2 documents: a.indd ID:262 #1; b.indd ID:270 #1."; a refusal ("Restore from Copy:
	    ...") when none could be put back. True when something was written. Refused while a run of ours is up. */
	bool RestoreHits(const KFCRowsByChapter& rows, PMString& outStatus);

	/** A TEXT ROW'S SPECIAL OPERATIONS > CREATE OUTLINES (docs/superpowers/specs/2026-10-18-kfc-text-outlines-design.md X1-X9):
	    the rows' matched characters outlined as InDesign's Type > Create Outlines does them, the text deleted and the outlines
	    put in as inline objects (ICreateOutlinesUtils::CreateCOTextCmd, deleteOriginal kTrue) - one inline per line of the
	    match (M7); story by story in text order, each checked again just before it is written; every other row carried past
	    what each one turned into (the story's length before and after the command - M7: the number is not fixed). ONE undo
	    step ("Create Outlines"), KFC's mark in it: the panel follows its Ctrl+Z and Redo. Each written row Outlined
	    (KFCResultModel::MarkHitOutlined - the characters it outlined kept for the row to draw). Refused rows counted by why
	    (overset - InDesign refuses it -, locked, edited since, ...). True = something written; outStatus says what. */
	bool OutlineHits(const KFCRowsByChapter& rows, PMString& outStatus);

	/** Can a text row be outlined - a text row not outlined yet, not locked, not overset, not zero-width? Read off the row
	    alone (nothing opened): the menu greys Create Outlines by it (X9); what only the document can tell is said when it is
	    pressed. */
	bool CanOutlineHit(int32 chapterIdx, int32 hitIdx);

	/** ONE UNDO STEP FOR A WRITE FROM THE LIST ("Replace" - kKFCReplaceStepKey), the one rule for the text rows (ReplaceHit,
	    ReplaceHits) and the object rows (KFCObjectReplace):
	      - ONE DOCUMENT: a plain sequence, rolled back on a failure the SDK's own way - the error state raised, the
	        sequence ended, the state cleared (CmdUtils.h, SequenceContext). Plain, as the SDK asks: a plain sequence is
	        the type that meets most needs, and an abortable one is for where it is needed - it carries "a heavy
	        performance overhead" (ICommandSequence.h). (Not because an ended abortable step was once seen, in KCM, to
	        take the undo step below it away: that sighting was never told apart from InDesign itself dropping a
	        document's undo history at a script's boundary, and KFC's own abortable steps - Change All in Book, the query
	        run - were measured not to: docs/ai-notes/kfc-shipping-full-test-2026-10-09.md section 3.)
	      - SEVERAL DOCUMENTS: ONE abortable sequence around every document's write and none inside it, aborted on a
	        failure (measured when Change Checked wrote several documents the same way - git ee77485^,
	        KFCReplaceEngine.cpp: a sequence per document took the step out of the other documents' histories without
	        putting their text back; a sequence nested inside settled its document, so the outer abort could not take it
	        back; and a plain sequence's rollback by the error state did not reach across documents).
	    BeginWriteStep outside any other command sequence; false = InDesign would not start one (nothing to end - the
	    caller says so). EndWriteStep keeps the step when ok and takes it back whole otherwise, and leaves the error state
	    clear. Neither touches a document's "unsaved" flag: a rollback or an abort puts the text back and leaves the flag
	    up, so the caller puts it back on a document that was clean (DocsAfterWrite, below). */
	struct WriteStep
	{
		ICommandSequence*	plain;		// one document's
		IAbortableCmdSeq*	across;		// several documents'
		WriteStep() : plain(nil), across(nil) {}
	};
	// (stepKey = the Edit menu's name of the step - Replace, Create Outlines; internalName = the sequence's own, as
	//  CmdUtils tracks a lost one.)
	bool BeginWriteStep(bool acrossDocuments, WriteStep& outStep, const char* stepKey = kKFCReplaceStepKey,
		const char* internalName = "KFC Replace");
	void EndWriteStep(WriteStep& step, bool ok);

	/** WHAT EACH DOCUMENT OF A WRITE FROM THE LIST IS LEFT AS (the author's calls), once the write is over, whichever way
	    it went - the one rule for the text rows (ReplaceHit, ReplaceHits) and the object rows, one row's and rows selected
	    together (KFCObjectReplace's one write behind its two doors). A closed chapter was reopened windowless and is held
	    (KFCBookScope::ReopenChapterDoc).
	      - WRITTEN: a chapter of ours gets its window, so the write can be seen and saved
	        (KFCBookScope::ShowChapterWindow); a document that is not ours is left as it is - one the user keeps WITHOUT a
	        window (Search: = All Documents) stays hidden, and the status line says so. (Not a window for every write:
	        ShowChapterWindow asks no IsHeldDoc, so a hidden document of the user's would be shown - measured.) A document
	        another chapter of the write wrote is written: two chapters can be one document - a book listing one file
	        twice reaches the same open document for both - and the one whose rows were not written must not take that
	        document's "unsaved" flag down over the other's write, or any close would throw the write away without asking.
	      - NOT WRITTEN (refused, rolled back, aborted): nothing of the write is in it, so a document that was clean is made
	        clean again, and a chapter of ours is then handed back - flag first, since a held chapter that says "unsaved"
	        is not closed. A rollback or an abort puts the text back and leaves the flag up (measured: a saved document, its
	        write rolled back whole, came out modified=true with not a character changed, and asked to be saved on close);
	        so can a check's walk. It is the product's own shape after an abort - the flag read before, put back only where
	        it was clean (incopyfileactions FileActionsOpenDocResponder.cpp:113-114, 161-171) - and not
	        IDataBase::SaveRestoreModifiedState, the read-only walks' guard, which puts the flag back whatever happened
	        inside it, a write that went through included.
	    Built before the first document is reached and told of each one as it is (Reached) and of each written one
	    (Wrote); the rule runs as it goes out of scope, so no way out of a write can skip it. */
	class DocsAfterWrite
	{
	public:
		DocsAfterWrite() {}
		~DocsAfterWrite();
		/** A document the write reached - a chapter's. readFlag = the write has rows for it: its "unsaved" flag is read
		    now, before anything of the write touches it (a document whose flag was not read is never made clean). */
		void Reached(const UIDRef& doc, bool readFlag);
		/** The write went into this document - every chapter of it. */
		void Wrote(const UIDRef& doc);
	private:
		struct Doc
		{
			UIDRef	doc;
			bool	flagRead;
			bool	wasModified;
			bool	wrote;
		};
		std::vector<Doc>	fDocs;
		DocsAfterWrite(const DocsAfterWrite&);
		DocsAfterWrite& operator=(const DocsAfterWrite&);
	};

	/** Can the row be replaced from its menu: a Find/Change match not replaced, not locked, with no
	    outcome - WhyGreyed answering kGreyedNot. */
	bool CanReplaceHit(int32 chapterIdx, int32 hitIdx);

	/** IS A ROW'S REPLACE UNDER WAY - ReplaceHit or ReplaceHits, text rows or object rows, from the door to the last
	    chapter's hand-back? KFCRunGuard counts it. It has no bar, but reopening a closed chapter and giving a written one
	    its window RUN THE MESSAGE LOOP, and what that dispatches must not start inside it: a landing (its Hide Previous
	    Chapter sweep would schedule the close of a chapter this is about to write), a second Replace, a run (its commit
	    point would close the chapter this reopened), the book watcher's release, the Undo follow. */
	bool IsReplacing();

	/** WHY A ROW'S REPLACE IS GREYED - the one place the reasons are asked (CanReplaceHit is this answering kGreyedNot) -
	    as its row shows it - Changed, Missing, locked, refused -
	    for the rows left by a Replace of rows selected together, counted by reason (the author's call: "1 already
	    replaced, 1 locked" rather than one "already replaced or locked" for all of them, which named a row reading
	    Missing as one of those). One reason a row: replaced, then missing, then locked, then refused
	    (a locked row a jump found changed reads "Missing ... locked" - missing is what keeps it from being written).
	    kGreyedNot when the row can be replaced. Text and object rows alike (KFCObjectReplace counts its rows by it). */
	enum GreyedReason { kGreyedNot = 0, kGreyedReplaced, kGreyedMissing, kGreyedLocked, kGreyedRefused, kGreyedReasonCount };
	GreyedReason WhyGreyed(int32 chapterIdx, int32 hitIdx);

	/** A GREP row's AFTER-TEXT: what the row's Replace would write at its place, read from a write that is
	    then thrown away (an aborted sequence - no undo step, the text, the story's version, the rows and the modified
	    flag as they were). false (outAfter empty) wherever Return would not write it now - not a GREP search, the query
	    changed since it, the row replaced / locked / not a work row, its document closed, its story changed, a run
	    going - or the write did not land. The UI shows it on the message area when the row is selected. */
	bool PreviewHit(int32 chapterIdx, int32 hitIdx, PMString& outAfter);
}

#endif // __KFCReplaceEngine_h__
