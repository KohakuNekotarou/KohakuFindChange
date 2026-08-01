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
//  actually does onto the status line. What is being measured:
//    1. does a run of consecutive notdefs come back as ONE hit, or one hit per character?
//    2. does passing nil for the "not used" options argument work?
//    3. does 0..TotalLength() really reach table-cell and footnote story threads?
//       (TotalLength vs GetPrimaryStoryThreadSpan is printed per story so the answer is visible)
//    4. are the returned indices character counts or UTF-16 units?
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDocument.h"			// GetName
#include "IFindChangeOptions.h"	// SearchForGlyph needs a real one - see the call site
#include "IFindChangeUtils.h"	// SearchForGlyph / kAnyNotDefGlyphID
#include "ILayoutUIUtils.h"		// GetFrontDocument - the same way KBSSearchEngine resolves doc scope
#include "IStoryList.h"			// the document's stories
#include "ITextModel.h"			// TotalLength / GetPrimaryStoryThreadSpan
#include "ITextStoryThreadDict.h"		// GetThreadBlockTextRange - one thread's own range
#include "ITextStoryThreadDictHier.h"	// NextUID - walk the story's threads (tables) in order

// General includes:
#include "CreateObject.h"		// ::CreateObject - the options object is made, not queried
#include "IDataBase.h"
#include "PMString.h"
#include "PersistUtils.h"		// ::GetUIDRef / ::GetDataBase
#include "TextWalkerServiceProviderID.h"	// kNonSession_FindChangeOptionsBoss
#include "Utils.h"

#include <vector>

// Project includes:
#include "KBSGlyphScanEngine.h"
#include "KBSResultTree.h"		// ShowStatus - also what app.kbsStatus reads back

#include <cstdio>				// ***** TEMPORARY: crash tracing, removed once the cause is known

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

/** Append "[start-end]" for every notdef SearchForGlyph hands back in this story, up to the cap.
    @return the total number of hits (which can exceed what was appended). */
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

int32 ReportOneStory(ITextModel* model, PMString& out)
{
	const TextIndex total = model->TotalLength();
	const TextIndex primary = model->GetPrimaryStoryThreadSpan();

	// ***** ONE THREAD BLOCK AT A TIME - never across a boundary. *****
	// Measured 2026-08-01: a story of body text + footnote (blocks 0-20 and 20-27) tolerated a
	// single 0..26 call, but a story holding only a table (blocks 0-2 and 2-13) hung even at 0..1.
	// The walker-based search never had this problem because the walker hands the searcher one
	// focus at a time; passing a raw 0..TotalLength() range is asking SearchForGlyph to do work
	// the walker normally does for it.
	std::vector<GlyphRun> blocks;
	CollectThreadRanges(model, blocks);
	TraceNum("  thread blocks / total", static_cast<int32>(blocks.size()), total);

	int32 hits = 0;
	for (size_t b = 0; b < blocks.size(); ++b)
	{
	const TextIndex blockStart = blocks[b].start;
	const TextIndex blockEnd = blocks[b].end;
	TraceNum("  BLOCK start/end", blockStart, blockEnd);

	TextIndex pos = blockStart;
	while (pos < blockEnd)
	{
		TextIndex foundStart = kInvalidTextIndex;
		TextIndex foundEnd = kInvalidTextIndex;

		TraceNum("  about to SearchForGlyph pos/end", pos, blockEnd);

		Utils<IFindChangeUtils> fcUtils;
		if (fcUtils == nil)
		{
			Trace("  !! Utils<IFindChangeUtils> is NIL");
			break;
		}
		Trace("  utils ok");

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
			Trace("  !! CreateObject(kNonSession_FindChangeOptionsBoss) returned NIL");
			break;
		}
		Trace("  options created");

		// The block's End is EXCLUSIVE (it equals the next block's Start, and the last block's End
		// equals TotalLength()), so the last index this call may look at is End-1. Passing End
		// itself is what took InDesign down on the very first run.
		const TextIndex searchEnd = (blockEnd > blockStart) ? (blockEnd - 1) : blockStart;
		const bool16 found = fcUtils->SearchForGlyph(
			model, options, kAnyNotDefGlyphID, pos, searchEnd, foundStart, foundEnd);

		TraceNum("  returned, found/start", found ? 1 : 0, foundStart);
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
	}	// next thread block
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

// End, KBSGlyphScanEngine.cpp.
