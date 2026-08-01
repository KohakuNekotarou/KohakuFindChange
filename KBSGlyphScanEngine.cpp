//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The missing-glyph scanner. See KBSGlyphScanEngine.h for the contract.
//
//  ***** MEASUREMENT BUILD (Task 2 of the plan) *****
//  IFindChangeUtils::SearchForGlyph has ZERO callers anywhere in the SDK, so before any real
//  scanning logic is built on top of it, this walks the front document and reports what the API
//  actually does. Already established by the earlier runs (see the trace file):
//    - the options argument is NOT optional despite the header saying "not used"
//    - a block's exclusive End must have 1 taken off before it is passed as findEnd
//    - consecutive notdefs come back one character at a time
//    - body text and footnote threads work perfectly
//    - a story holding a TABLE stopped dead
//
//  ***** WHAT THE MEASUREMENTS SETTLED (2026-08-01) *****
//  The earlier conclusion - "a story with a table cannot be searched, whatever range is used" - was
//  wrong, and so was its replacement. Both were fixed by holding the document constant and varying
//  only the range. What is actually true:
//
//    1. findEnd is EXCLUSIVE. A block whose notdef sits at 12 answers nothing to (12, 12) and
//       hands back 12 to (12, 13). The old note that "the ceiling is TotalLength() - 1" was reading
//       that backwards: the ceiling is real, but what it keeps out is the story's final,
//       uneditable carriage return.
//    2. A TABLE ANCHOR (kTextChar_Table, TextChar.h:58) inside the range is fatal - ONE index is
//       enough. "A lone anchor at 0..0 is safe" was an artefact of reading the end as inclusive:
//       with an exclusive end that range is empty and never looked at the anchor.
//    3. Everything else is fine: cell blocks, footnote blocks, ranges spanning thread boundaries,
//       and the footnote reference marker (0x0004).
//
//  So the scan collects the anchor indices first and searches only the stretches BETWEEN them.
//  A table occupies its primary story thread as a single anchor character, with the cells' text in
//  a thread block of its own, so skipping the anchor costs nothing - it is where a table hangs, not
//  a character that can come out as a box.
//
//  Every block's leading characters are still dumped to the trace before any searching starts, so
//  the character sitting at each index can be named rather than guessed at.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"	// CopyText - reads the characters an index actually holds
#include "IDocument.h"			// GetName
#include "IFindChangeOptions.h"	// SearchForGlyph needs a real one - see the call site
#include "IFindChangeUtils.h"	// SearchForGlyph / kAnyNotDefGlyphID
#include "IK2ServiceProvider.h"	// the text walker service - RunWithWalker
#include "IK2ServiceRegistry.h"
#include "ILayoutUIUtils.h"		// GetFrontDocument - the same way KBSSearchEngine resolves doc scope
#include "ISession.h"			// GetExecutionContextSession - where the service registry lives
#include "IStoryList.h"			// the document's stories
#include "ITextModel.h"			// TotalLength / GetPrimaryStoryThreadSpan
#include "ITextStoryThreadDict.h"		// GetThreadBlockTextRange - one thread's own range
#include "ITextStoryThreadDictHier.h"	// NextUID - walk the story's threads (tables) in order
#include "ITextWalker.h"				// RunWithWalker - also declares ITextWalkerClient
#include "ITextWalkerProgressMonitor.h"	// where the walk's RangeProgressBar is parked
#include "ITextWalkerScope.h"
#include "IWalkerScopeFactoryUtils.h"	// QueryDocumentWalkerScope

// General includes:
#include "CreateObject.h"		// ::CreateObject / ::CreateObject2 - the walker client is made
#include "IDataBase.h"
#include "PMString.h"
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "ProgressBar.h"		// RangeProgressBar - the walk's progress + cancel
#include "TextChar.h"			// kTextChar_Table / kTextChar_TableContinued - the table anchor
#include "TextWalkerServiceProviderID.h"	// kNonSession_FindChangeOptionsBoss / kTextWalkerService
#include "Utils.h"
#include "WalkerScopeOptions.h"	// what the walk is allowed to visit
#include "WideString.h"			// GrabUTF16Buffer - the same way KBSSearchEngine reads text

#include <vector>

// Project includes:
#include "KBSGlyphScanEngine.h"
#include "KBSGlyphWalkerClient.h"	// KBSGlyphWalkerData - what the walk saw
#include "KBSID.h"					// kKBSGlyphWalkerClientBoss
#include "KBSResultTree.h"		// ShowStatus - also what app.kbsStatus reads back
#include "KBSSearchEngine.h"	// SearchBook - the real scan runs through KBS's own search path

#include <cstdio>				// ***** TEMPORARY: crash tracing, removed once the cause is known
#include <cstring>				// ***** TEMPORARY: strcat_s, for the character dump

namespace
{

// ***** TEMPORARY DIAGNOSTIC - NOT FOR THE SHIPPED PLUG-IN *****
// The first run of this engine took InDesign down with it, and a crash leaves nothing behind:
// app.kbsStatus lives in memory, so the status line cannot say how far the run got. This writes
// each step to disk and flushes immediately, so the last line in the file is the step that killed
// it. Delete this and every Trace() call once the cause is understood.
// TEMP rather than the work folder: the first attempt wrote nothing there, and "the trace is
// empty" has two possible meanings (never reached / could not write). This removes one of them.
const char* const kTracePath = "C:\\Users\\user\\AppData\\Local\\Temp\\kbs-glyphscan-trace.txt";

void TraceOpen(const char* mode, const char* text)
{
	FILE* f = nil;
	if (::fopen_s(&f, kTracePath, mode) == 0 && f != nil)
	{
		::fprintf(f, "%s\n", text);
		::fflush(f);
		::fclose(f);
	}
}

void TraceReset()				{ TraceOpen("w", "--- run start ---"); }
void Trace(const char* text)	{ TraceOpen("a", text); }

void TraceNum(const char* label, int32 a, int32 b)
{
	char buf[256];
	::sprintf_s(buf, sizeof(buf), "%s %d %d", label, static_cast<int>(a), static_cast<int>(b));
	Trace(buf);
}

// How many hits to report per story. A measurement only needs the first few, and the status line
// is one line: a story full of boxes would otherwise produce an unreadable wall of numbers.
const int32 kMaxReportedPerStory = 12;

// How many stories to report. Same reason.
const int32 kMaxReportedStories = 6;

// ***** WHICH GLYPH ID TO SEARCH FOR (measurement, 2026-08-01 round 3) *****
// kAnyNotDefGlyphID (-2) is the "any notdef, whatever the font" sentinel, and it is the one that
// kills InDesign. That was proved twice over through the DOM, which reaches the same engine:
//
//   app.findGlyphPreferences.glyphID = -2;  doc.findGlyph();
//     - on a document holding a TABLE      -> process gone (2 of 2 attempts)
//     - on a document that is merely OVERSET, no table at all -> process gone (2 of 2)
//
// 0 is the notdef glyph id of very nearly every font. Through the DOM it SURVIVED both of those
// documents, and on the table document it returned exactly the 7 hits the official preflight
// profile reports - story and index included. IFindChangeUtils.h:59-60 explains the split: the
// sentinel (and a font's own notdef id) make the search read the WAX, anything else makes it read
// the TEXT. Only the wax path is broken.
//
// This build asks whether the same holds for the C++ entry point, which the DOM measurement cannot
// answer on its own.
const Text::GlyphID kScanGlyphID = 0;

// Whether to cut the table anchors out of the searched ranges.
//
// With -2 this was mandatory: one anchor index inside the range was fatal. With 0 the DOM needed no
// such care - a single findGlyph() call covered a whole document, table and footnotes and all - so
// this build deliberately searches each thread block in ONE call and finds out whether the C++ side
// agrees. If it does, the anchor-collecting machinery below can be deleted outright.
const bool kSplitAroundAnchors = false;

/** A half-open [start, end) range of text: either one of the story's thread blocks, or one run of
    consecutive notdef glyphs. */
struct GlyphRun
{
	TextIndex start;
	TextIndex end;
	GlyphRun() : start(kInvalidTextIndex), end(kInvalidTextIndex) {}
};

/** Collect the story's thread block ranges, in order. A story is a primary story thread followed
    by one block per table (ITextStoryThreadDictHier.h:31-39); the walk is the one
    SnpIterTableUseDictHier.cpp:145-199 shows. End is EXCLUSIVE - each block's End is the next
    block's Start, and the last one's End is TotalLength(). */
void CollectThreadRanges(ITextModel* model, std::vector<GlyphRun>& out)
{
	InterfacePtr<ITextStoryThreadDictHier> dictHier(model, UseDefaultIID());
	if (dictHier == nil)
	{
		// No hierarchy: treat the whole story as one block, minus the terminating carriage return.
		GlyphRun whole;
		whole.start = 0;
		whole.end = model->TotalLength();
		out.push_back(whole);
		return;
	}

	IDataBase* threadDB = ::GetDataBase(dictHier);
	UID nextUID = ::GetUIDRef(dictHier).GetUID();
	int32 guard = 0;
	while (nextUID != kInvalidUID && guard < 200)
	{
		InterfacePtr<ITextStoryThreadDict> dict(threadDB, nextUID, UseDefaultIID());
		if (dict == nil)
			break;
		const Text::StoryRange r = dict->GetThreadBlockTextRange();
		GlyphRun block;
		block.start = r.Start(nil);
		block.end = r.End();
		if (block.end > block.start)
			out.push_back(block);
		nextUID = dictHier->NextUID(nextUID);
		++guard;
	}
}

/** Dump the UTF-16 units at the head of [start, end) into the trace, so the character sitting AT an
    index can be named rather than guessed at. What this is looking for is kTextChar_Table
    (TextChar.h:58): a table occupies its primary story thread as ONE anchor character, and the
    cells' text lives in a thread block of its own further along. Nothing here can hang, so it is
    done for every block before any searching starts. */
void TraceRangeChars(ITextModel* model, TextIndex start, TextIndex end)
{
	const int32 kMaxDumped = 24;	// one trace line's worth - enough to name the head of a block

	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
	{
		Trace("    chars: no compose scanner");
		return;
	}

	int32 len = static_cast<int32>(end - start);
	if (len <= 0)
		return;
	if (len > kMaxDumped)
		len = kMaxDumped;

	WideString text;
	scanner->CopyText(start, len, &text);

	int32 n = 0;
	const UTF16TextChar* buf = text.GrabUTF16Buffer(&n);
	if (buf == nil || n <= 0)
	{
		Trace("    chars: empty");
		return;
	}

	char line[640];
	::strcpy_s(line, sizeof(line), "    chars ");
	for (int32 i = 0; i < n && i < kMaxDumped; ++i)
	{
		char one[32];
		::sprintf_s(one, sizeof(one), "%d:%04X ", static_cast<int>(start + i),
					static_cast<unsigned int>(buf[i]));
		::strcat_s(line, sizeof(line), one);
	}
	Trace(line);
}

/** Every index in [start, end) that holds a table anchor. */
void CollectTableAnchors(ITextModel* model, TextIndex start, TextIndex end,
						 std::vector<TextIndex>& out)
{
	const int32 kMaxScanned = 4000;		// this is a measurement, not the shipped scan

	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;

	int32 len = static_cast<int32>(end - start);
	if (len <= 0)
		return;
	if (len > kMaxScanned)
		len = kMaxScanned;

	WideString text;
	scanner->CopyText(start, len, &text);

	int32 n = 0;
	const UTF16TextChar* buf = text.GrabUTF16Buffer(&n);
	if (buf == nil)
		return;

	for (int32 i = 0; i < n; ++i)
	{
		if (buf[i] == kTextChar_Table || buf[i] == kTextChar_TableContinued)
			out.push_back(start + i);
	}
}

/** Run SearchForGlyph across one range and append every notdef it hands back to 'out'.

    findEnd is EXCLUSIVE (measured 2026-08-01 - see the note in ReportOneStory), so a thread block's
    End goes in unchanged. The caller must already have kept every table anchor out of the range.

    @return how many notdefs were found. */
int32 SearchRange(ITextModel* model, const char* label, TextIndex rangeStart,
				  TextIndex rangeEndExclusive, PMString& out)
{
	int32 hits = 0;
	TextIndex pos = rangeStart;
	while (pos < rangeEndExclusive)
	{
		char head[128];
		::sprintf_s(head, sizeof(head), "    %s SearchForGlyph %d..%d", label,
					static_cast<int>(pos), static_cast<int>(rangeEndExclusive));
		Trace(head);

		Utils<IFindChangeUtils> fcUtils;
		if (fcUtils == nil)
		{
			Trace("    !! Utils<IFindChangeUtils> is NIL");
			break;
		}

		// ***** The options argument is NOT optional, whatever the header says. *****
		// IFindChangeUtils.h:64 calls it "not used", and passing nil took InDesign down on the
		// first run (proved by this trace: the last line was "utils ok"). Every caller of the
		// sibling FindThisItem hands over a real object, and the product code shows where one
		// comes from - it is CREATED, not queried:
		//
		//   spellpanel/SpellChangeAllObserver.cpp:195-197
		//     ::CreateObject(kSpellCheckFindChangeDataBoss, IID_IFINDCHANGEOPTIONS)
		//
		// That boss is the spell panel's own. The general-purpose one is
		// kNonSession_FindChangeOptionsBoss (TextWalkerServiceProviderID.h:103) - "the Find/Change
		// options that do NOT belong to the session", i.e. exactly what a caller who must not
		// disturb the user's own Find/Change dialog needs. It is real: work/Boss.txt:50418 lists
		// it in TEXT WALKER.RPLN carrying IID_IFINDCHANGEOPTIONS.
		InterfacePtr<IFindChangeOptions> options(static_cast<IFindChangeOptions*>(
			::CreateObject(kNonSession_FindChangeOptionsBoss, IID_IFINDCHANGEOPTIONS)));
		if (options == nil)
		{
			Trace("    !! CreateObject(kNonSession_FindChangeOptionsBoss) returned NIL");
			break;
		}

		TextIndex foundStart = kInvalidTextIndex;
		TextIndex foundEnd = kInvalidTextIndex;
		const bool16 found = fcUtils->SearchForGlyph(
			model, options, kScanGlyphID, pos, rangeEndExclusive, foundStart, foundEnd);

		TraceNum("    returned found/start", found ? 1 : 0, foundStart);
		if (!found)
			break;

		if (hits < kMaxReportedPerStory)
		{
			out.Append("[");
			out.AppendNumber(foundStart);
			out.Append("-");
			out.AppendNumber(foundEnd);
			out.Append("]");
		}
		++hits;

		// The search must always move forward. A call that hands back a range ending where it began
		// would otherwise spin here forever - and since this API has no callers to copy, that is a
		// real possibility rather than a theoretical one.
		pos = (foundEnd > pos) ? foundEnd : (pos + 1);
	}
	return hits;
}

int32 ReportOneStory(ITextModel* model, PMString& out)
{
	std::vector<GlyphRun> blocks;
	CollectThreadRanges(model, blocks);
	TraceNum("  thread blocks / total", static_cast<int32>(blocks.size()), model->TotalLength());

	// Name every block's characters first. None of this can hang, so however the run ends, the
	// trace always says what was sitting at each index.
	for (size_t b = 0; b < blocks.size(); ++b)
	{
		TraceNum("  BLOCK start/end", blocks[b].start, blocks[b].end);
		TraceRangeChars(model, blocks[b].start, blocks[b].end);
	}

	std::vector<TextIndex> allAnchors;
	for (size_t b = 0; b < blocks.size(); ++b)
		CollectTableAnchors(model, blocks[b].start, blocks[b].end, allAnchors);
	TraceNum("  table anchors in story", static_cast<int32>(allAnchors.size()), 0);

	int32 hits = 0;

	// ***** Steps A and B: every block BACK TO FRONT, with the table anchors cut out. *****
	// Back to front because a table's cell block always follows the primary block it is anchored
	// in, and the primary block is where the previous run died - taking the cells first puts their
	// answer on disk before anything known to be risky is attempted.
	// Within a block the anchor indices are skipped, so what gets searched here is only text that
	// is certainly not a table anchor.
	for (size_t bi = blocks.size(); bi-- > 0; )
	{
		const TextIndex blockStart = blocks[bi].start;
		const TextIndex blockEnd = blocks[bi].end;		// EXCLUSIVE

		std::vector<TextIndex> anchors;
		CollectTableAnchors(model, blockStart, blockEnd, anchors);
		TraceNum("  == block / anchors in it", static_cast<int32>(bi),
				 static_cast<int32>(anchors.size()));

		if (!kSplitAroundAnchors)
		{
			// ONE call for the whole block, anchors included. This is the measurement: with glyph
			// id 0 the DOM covered an entire document - table anchor and all - in a single call.
			// If the anchor is still fatal here, the trace stops on this line and says so.
			TraceNum("  == WHOLE BLOCK in one call, anchors NOT skipped", blockStart, blockEnd);
			hits += SearchRange(model, "whole", blockStart, blockEnd, out);
		}
		else
		{
			// The stretches BETWEEN the anchors, each searched on its own. segEnd goes in unchanged:
			// findEnd is EXCLUSIVE (see the note above SearchRange).
			TextIndex segStart = blockStart;
			for (size_t a = 0; a <= anchors.size(); ++a)
			{
				const TextIndex segEnd = (a < anchors.size()) ? anchors[a] : blockEnd;	// exclusive
				if (segEnd > segStart)
					hits += SearchRange(model, "seg", segStart, segEnd, out);
				if (a < anchors.size())
					segStart = anchors[a] + 1;
			}
		}
	}

	// The anchors themselves are never searched. Two runs were spent proving they must not be:
	// searching one on its own looked safe at first (0..0), but that was an empty range under an
	// exclusive end - the moment a real anchor index was actually examined, InDesign died.
	// Nothing is lost by skipping them: an anchor is where a table hangs, not a character that can
	// come out as a box.

	return hits;
}

}

void KBSGlyphScanEngine::Run()
{
	TraceReset();

	PMString out;
	out.SetTranslatable(kFalse);

	Trace("about to GetFrontDocument");
	IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	Trace(doc != nil ? "front doc ok" : "front doc NIL");
	if (doc == nil)
	{
		out.Append("Glyph scan: no open document.");
		KBSResultTree::ShowStatus(out);
		return;
	}

	const UIDRef docRef = ::GetUIDRef(doc);
	IDataBase* db = docRef.GetDataBase();
	if (db == nil)
	{
		out.Append("Glyph scan: document has no database.");
		KBSResultTree::ShowStatus(out);
		return;
	}

	InterfacePtr<const IStoryList> storyList(db, db->GetRootUID(), UseDefaultIID());
	if (storyList == nil)
	{
		out.Append("Glyph scan: no story list.");
		KBSResultTree::ShowStatus(out);
		return;
	}

	// User accessible only. IStoryList.h:38-42 states that internal stories are not subject to
	// find/change or spell checking, so scanning them would report boxes in places the rest of the
	// panel - and InDesign itself - never looks at.
	Trace("story list ok");
	const int32 storyCount = storyList->GetUserAccessibleStoryCount();
	const int32 allCount = storyList->GetAllTextModelCount();
	TraceNum("counts user/all", storyCount, allCount);

	out.Append("stories ");
	out.AppendNumber(storyCount);
	out.Append("/");
	out.AppendNumber(allCount);
	out.Append(" : ");

	int32 grandTotal = 0;
	for (int32 i = 0; i < storyCount; ++i)
	{
		TraceNum("story index/count", i, storyCount);

		const UIDRef storyRef = storyList->GetNthUserAccessibleStoryUID(i);
		InterfacePtr<ITextModel> model(storyRef, UseDefaultIID());
		if (model == nil)
		{
			Trace("  model nil - skipped");
			continue;
		}
		TraceNum("  model ok, total/primary", model->TotalLength(), model->GetPrimaryStoryThreadSpan());

		const bool reportThis = (i < kMaxReportedStories);
		if (reportThis)
		{
			out.Append("S");
			out.AppendNumber(i);
			out.Append(" t=");
			out.AppendNumber(model->TotalLength());
			out.Append(" p=");
			out.AppendNumber(model->GetPrimaryStoryThreadSpan());
			out.Append(" ");
		}

		PMString storyHits;
		storyHits.SetTranslatable(kFalse);
		const int32 hits = ReportOneStory(model, storyHits);
		grandTotal += hits;

		if (reportThis)
		{
			out.Append(storyHits);
			out.Append("(n=");
			out.AppendNumber(hits);
			out.Append(") ");
		}
	}

	out.Append("| total ");
	out.AppendNumber(grandTotal);

	KBSResultTree::ShowStatus(out);
}

//========================================================================================
// The walker-driven version
//========================================================================================

namespace
{

// ***** ROUND 2 - kept only so the two can be compared. This is NOT what the menu runs now. *****
// A walk driven by OUR OWN client, which then called SearchForGlyph itself on each range the walker
// offered. That is a hybrid, and it is the wrong half that decides: the walker supplies the ranges,
// but the search is still the all-at-once entry point, so it died on the range holding a table
// anchor - the walker hands that range over unchanged.
void RunRound2WalkerScan()
{
	TraceReset();
	Trace("=== WALKER-DRIVEN SCAN ===");

	PMString out;
	out.SetTranslatable(kFalse);

	IDocument* doc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	if (doc == nil)
	{
		out.Append("Glyph scan (walker): no open document.");
		KBSResultTree::ShowStatus(out);
		return;
	}
	const UIDRef docRef = ::GetUIDRef(doc);

	// The walker comes out of the session's service registry - the same three steps
	// KBSSearchEngine takes for the find/change walk (KBSSearchEngine.cpp:666-684).
	InterfacePtr<IK2ServiceRegistry> registry(GetExecutionContextSession(), UseDefaultIID());
	InterfacePtr<IK2ServiceProvider> provider(
		(registry != nil)
			? registry->QueryServiceProviderByClassID(kTextWalkerService, kTextWalkerServiceProviderBoss)
			: nil);
	InterfacePtr<ITextWalker> walker(provider, UseDefaultIID());
	if (walker == nil)
	{
		Trace("!! no text walker service");
		out.Append("Glyph scan (walker): no text walker service.");
		KBSResultTree::ShowStatus(out);
		return;
	}

	// Always start a fresh walk from the top.
	if (walker->IsWalking())
		walker->Halt();

	// EVERY switch left at its default kTrue. The glyph scan deliberately ignores the five
	// Find/Change scope options and looks everywhere (design section 2), so nothing is read off the
	// dialog here - which is the one place this differs from
	// KBSSearchEngine::GetKBSWalkerScopeOptions.
	WalkerScopeOptions scopeOptions;

	InterfacePtr<ITextWalkerScope> scope(
		Utils<IWalkerScopeFactoryUtils>()->QueryDocumentWalkerScope(docRef, scopeOptions));
	if (scope == nil)
	{
		Trace("!! QueryDocumentWalkerScope returned NIL");
		out.Append("Glyph scan (walker): no walker scope for this document.");
		KBSResultTree::ShowStatus(out);
		return;
	}

	// Our own client instead of the stock kFindChangeClientBoss - see KBSGlyphWalkerClient.h.
	InterfacePtr<ITextWalkerClient> client(static_cast<ITextWalkerClient*>(
		::CreateObject2<ITextWalkerClient>(kKBSGlyphWalkerClientBoss)));
	if (client == nil)
	{
		Trace("!! CreateObject2(kKBSGlyphWalkerClientBoss) returned NIL");
		out.Append("Glyph scan (walker): could not create the walker client.");
		KBSResultTree::ShowStatus(out);
		return;
	}

	// nil find/change options. There is no query to run here - the client asks each range about its
	// glyphs - and LinguisticTestMenu.cpp:1221 initialises a walker with nil the same way. Handing
	// over the session's options instead would tie this scan to whichever tab the user's
	// Find/Change dialog happens to be on.
	walker->Initialize(client, scope, nil, nil);

	// !! Deliberately NO TextWalkerSelections_CriticalSection, unlike KBSSearchEngine.
	// What that section does is take the keyboard focus away and hand it back (spellpanel says so
	// outright - SpellCheckWalker.cpp:85-141 "same code as
	// TextWalkerSelectionUtils::EnterWalkerSelections_CriticalSection"). It is there because a
	// find/change walk MOVES THE SELECTION onto each match. This scan selects nothing and writes
	// nothing, and holding the section would cost the very thing the walk is here to gain: with it
	// held, UI events cannot be pumped, so the bar's Cancel could never be heard. HyphenateStoryCmd
	// (LinguisticTestMenu.cpp:1192-1225) drives a walk without it either.

	KBSGlyphWalkerData::Reset();

	PMString taskText("Scanning for missing glyphs...");
	taskText.SetTranslatable(kFalse);
	// showImmediate = kTrue so the bar is visibly there even on a small document; showCancel is
	// kTrue by default (ProgressBar.h:208).
	RangeProgressBar bar(taskText, 0, 100, kTrue);

	// The monitor is only a parking space - the CLIENT is what moves the bar (see the .h).
	InterfacePtr<ITextWalkerProgressMonitor> monitor(client, UseDefaultIID());
	if (monitor != nil)
		monitor->SetWalkerProgressMonitor(&bar);

	Trace("about to Walk()");
	const bool16 walked = walker->Walk();
	Trace(walked ? "Walk() returned kTrue" : "Walk() returned kFalse");

	// Unhook the bar BEFORE it goes out of scope: the walker holds on to the client, so a bar left
	// parked there would be a dangling pointer on an object that outlives this function.
	if (monitor != nil)
		monitor->SetWalkerProgressMonitor(nil);

	if (walker->IsWalking())
		walker->Halt();

	out.Append("walker: stories ");
	out.AppendNumber(KBSGlyphWalkerData::GetStoryCount());
	out.Append(", ranges ");
	out.AppendNumber(KBSGlyphWalkerData::GetRangeCount());
	out.Append(" (anchor inside: ");
	out.AppendNumber(KBSGlyphWalkerData::GetRangesWithAnchor());
	out.Append("), notdef ");
	out.AppendNumber(KBSGlyphWalkerData::GetHitCount());
	out.Append(" ");
	out.Append(KBSGlyphWalkerData::GetDigest());
	if (!walked)
		out.Append(" [Walk returned false]");

	KBSResultTree::ShowStatus(out);
}

// Flip to true to run the round-2 measurement above instead of the real scan.
const bool kUseRound2WalkerScan = false;

}	// anonymous namespace

//========================================================================================
// ROUND 3: the OFFICIAL find/change engine, reached through KBS's own search path
//========================================================================================

void KBSGlyphScanEngine::RunWithWalker()
{
	if (kUseRound2WalkerScan)
	{
		RunRound2WalkerScan();
		return;
	}

	// ***** Why this is the right way in, and the earlier two were not. *****
	//
	// The user settled it on 2026-08-01 by hand: with the Find/Change dialog on the Glyph tab and
	// -2 typed into ID: GID/CID, pressing Find Next repeatedly walked the WHOLE of the very document
	// that had taken InDesign down twice under measurement - table included, hits inside the table
	// cells found - without a stumble.
	//
	// So kAnyNotDefGlyphID is not broken. What is broken is handing the search a story's ENTIRE
	// range in one call:
	//
	//   IFindChangeUtils::SearchForGlyph over a whole story  -> dies on a table anchor
	//   DOM doc.findGlyph() (returns every match at once)    -> dies (2 of 2, table AND overset)
	//   the official engine walking under a TEXT WALKER      -> fine
	//
	// The walker cuts a story into thread blocks - body, footnotes, each table cell - so the range
	// carrying the anchor is never the range being searched. That is the same mechanism already
	// known to be why KBS's ordinary searches survive tables.
	//
	// KBS walks that way for every search it runs, so the scan needs no engine of its own: it is the
	// existing search with one value replaced. Scope resolution, chapter opening, the five options,
	// the progress bar, cancel, the result tree and the jumps all come along unchanged.
	PMString summary;
	summary.SetTranslatable(kFalse);
	KBSSearchEngine::SearchBook(summary, kAnyNotDefGlyphID);
	KBSResultTree::ShowStatus(summary);
}

// End, KBSGlyphScanEngine.cpp.
