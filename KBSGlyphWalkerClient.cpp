//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The missing-glyph scan's text-walker client. See KBSGlyphWalkerClient.h for why it exists
//  and which product code it is modelled on.
//
//  ***** MEASUREMENT BUILD - what this is trying to find out *****
//  A raw IFindChangeUtils::SearchForGlyph call takes InDesign down when the range it is handed
//  contains a table anchor (kTextChar_Table, 0x0016) and carries on past it. Measured
//  2026-08-01, same document, range the only variable:
//      cell block 2..7 ok / 1..1 ok / anchor alone 0..0 ok / whole story 0..7 DEAD.
//
//  The open question is whether a WALKER ever offers such a range. The walker is what breaks a
//  story into thread blocks, takes footnotes in and out of scope and skips master pages, so it
//  may well never hand over a range that straddles an anchor - in which case a walker-driven
//  scan is safe by construction and no splitting is needed anywhere.
//
//  So this deliberately does NOT split around anchors. It hands the walker's range to
//  SearchForGlyph exactly as given, and writes the range - together with how many anchors are
//  inside it - to the trace BEFORE the call. If InDesign dies, the last line of the trace names
//  the range that did it and says whether an anchor was in there.
//
//  !! The tracing here is diagnostic and does not ship. See KBSGlyphScanEngine.cpp.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IComposeScanner.h"			// CopyText - to count the table anchors in a range
#include "IFindChangeOptions.h"			// SearchForGlyph will not take nil for this
#include "IFindChangeUtils.h"			// SearchForGlyph / kAnyNotDefGlyphID
#include "ITextModel.h"
#include "ITextWalker.h"				// also declares ITextWalkerClient
#include "ITextWalkerProgressMonitor.h"	// where the RangeProgressBar is parked

// General includes:
#include "CreateObject.h"				// ::CreateObject - the options object is made, not queried
#include "HelperInterface.h"			// DECLARE_HELPER_METHODS / DEFINE_HELPER_METHODS
#include "PMString.h"
#include "ProgressBar.h"				// RangeProgressBar
#include "TextChar.h"					// kTextChar_Table / kTextChar_TableContinued
#include "TextWalkerServiceProviderID.h"	// kNonSession_FindChangeOptionsBoss
#include "Utils.h"
#include "WideString.h"					// GrabUTF16Buffer

#include <cstdio>						// ***** TEMPORARY: crash tracing
#include <vector>

// Project includes:
#include "KBSGlyphWalkerClient.h"
#include "KBSID.h"						// kKBSGlyphWalkerClientImpl - CREATE_PMINTERFACE needs it

namespace
{

// ***** TEMPORARY DIAGNOSTIC - NOT FOR THE SHIPPED PLUG-IN *****
// Appends to the same file KBSGlyphScanEngine writes, so one run reads as one story. Flushed on
// every line: a crash leaves nothing in memory behind, so the last line on disk is the step that
// killed it.
const char* const kTracePath = "C:\\Users\\user\\AppData\\Local\\Temp\\kbs-glyphscan-trace.txt";

void Trace(const char* text)
{
	FILE* f = nil;
	if (::fopen_s(&f, kTracePath, "a") == 0 && f != nil)
	{
		::fprintf(f, "%s\n", text);
		::fflush(f);
		::fclose(f);
	}
}

// ----- what the walk saw (see KBSGlyphWalkerClient.h for why this is module state) -----
int32 gHitCount = 0;
int32 gRangeCount = 0;
int32 gRangesWithAnchor = 0;
int32 gStoryCount = 0;
PMString gDigest;

// The status line is one line, and a story full of boxes would fill it with numbers.
const int32 kMaxDigestEntries = 10;

// Runaway guard. The walker is being driven by hand here, and a client that fails to move itself
// forward would spin for ever - with no callers in the SDK to copy, that is a real possibility
// rather than a theoretical one.
const int32 kMaxRanges = 20000;

// Cap on how much text is read to count anchors in one range. This is a measurement, and the
// answer that matters ("was there an anchor in there at all") comes from the front of the range.
const int32 kMaxScannedForAnchors = 4000;

/** Every index in [start, end) holding a table anchor.

    These have to be kept OUT of any range handed to SearchForGlyph. Measured 2026-08-01: one
    anchor index inside the range is enough to take InDesign down, and the walker does NOT leave
    them out by itself - it offered range=0..1 on a table story, whose index 0 is the anchor, and
    that call was fatal. (An earlier reading said a lone anchor was safe. It was not: findEnd is
    exclusive, so the "0..0" that survived was an empty range that never looked at anything.) */
void CollectTableAnchors(ITextModel* model, TextIndex start, TextIndex end,
						 std::vector<TextIndex>& out)
{
	InterfacePtr<IComposeScanner> scanner(model, UseDefaultIID());
	if (scanner == nil)
		return;

	int32 len = static_cast<int32>(end - start);
	if (len <= 0)
		return;
	if (len > kMaxScannedForAnchors)
		len = kMaxScannedForAnchors;

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

/** Collect every notdef in [start, end) - findEnd is EXCLUSIVE, see the call site - appending each
    to the digest. The caller has already made sure no table anchor is inside.
    @return how many were found. */
int32 SearchSegment(ITextModel* model, TextIndex start, TextIndex end)
{
	int32 found = 0;
	TextIndex pos = start;
	while (pos < end)
	{
		char head[160];
		::sprintf_s(head, sizeof(head), "    seg SearchForGlyph %d..%d",
					static_cast<int>(pos), static_cast<int>(end));
		Trace(head);

		// Not optional, whatever IFindChangeUtils.h:64 says: nil here took InDesign down. The
		// general-purpose object is the one that does NOT belong to the session, so running this
		// scan cannot disturb the user's own Find/Change dialog.
		InterfacePtr<IFindChangeOptions> options(static_cast<IFindChangeOptions*>(
			::CreateObject(kNonSession_FindChangeOptionsBoss, IID_IFINDCHANGEOPTIONS)));
		if (options == nil)
		{
			Trace("    !! CreateObject(kNonSession_FindChangeOptionsBoss) returned NIL");
			break;
		}

		TextIndex foundStart = kInvalidTextIndex;
		TextIndex foundEnd = kInvalidTextIndex;
		const bool16 hit = Utils<IFindChangeUtils>()->SearchForGlyph(
			model, options, kAnyNotDefGlyphID, pos, end, foundStart, foundEnd);

		char line[160];
		::sprintf_s(line, sizeof(line), "      returned found=%d start=%d end=%d",
					hit ? 1 : 0, static_cast<int>(foundStart), static_cast<int>(foundEnd));
		Trace(line);

		if (!hit)
			break;

		++found;
		++gHitCount;
		if (gHitCount <= kMaxDigestEntries)
		{
			gDigest.Append("[");
			gDigest.AppendNumber(foundStart);
			gDigest.Append("-");
			gDigest.AppendNumber(foundEnd);
			gDigest.Append("]");
		}

		// Always forward. A hit whose end is where it began would otherwise spin here for ever.
		pos = (foundEnd > pos) ? foundEnd : (pos + 1);
	}
	return found;
}

} // anonymous namespace

//========================================================================================
// CLASS KBSGlyphWalkerClient
//========================================================================================

/** Collects every notdef glyph the walker's ranges contain. Shaped after
    spellpanel/SpellReplaceWalker.cpp's client (product code): the same set of overrides, the same
    CREATE_PMINTERFACE + DECLARE_HELPER_METHODS pair. */
class KBSGlyphWalkerClient : public ITextWalkerClient
{
public:
						KBSGlyphWalkerClient(IPMUnknown* boss);
	virtual				~KBSGlyphWalkerClient();

	virtual	bool16		OnStart(ITextWalker* pWalker);
	virtual	bool16		OnEnd(ITextWalker* pWalker);
	virtual	bool16		OnResume(ITextWalker* pWalker)	{ return kTrue; }
	virtual	bool16		OnSuspend(ITextWalker* pWalker)	{ return kTrue; }

	virtual	bool16		OnNextPosition(ITextWalker* pWalker, ITextModel* pModel, TextIndex nPosition,
									   TextIndex nStartRange, TextIndex nEndRange,
									   int32 startRangePrcnt, int32 endRangePrcnt,
									   bool16 rangeAdjustable);

	virtual	bool16		OnStoryStart(ITextWalker* pWalker, UID storyUID);
	virtual	void		OnStoryEnd(ITextWalker* pWalker)						{ }
	virtual	bool16		OnDocumentStart(ITextWalker* pWalker, const UIDRef& doc)	{ return kTrue; }
	virtual	void		OnDocumentEnd(ITextWalker* pWalker)						{ }

	// Nothing here replaces anything, so there is no count to keep. The stock find/change client
	// does not update these either (measured) - they belong to kFCClient_ReturnsReplacementCount_Boss.
	virtual	void		SetReplacementCount(int32 nCount)	{ }
	virtual	int32		GetReplacementCount()				{ return 0; }

	/** kTrue = "move on to the next range". kFalse would make the walker offer the SAME range
	    again, which is for clients that want several passes over one stretch of text. */
	virtual	bool16		UpdateTextRange()					{ return kTrue; }

DECLARE_HELPER_METHODS()
};

CREATE_PMINTERFACE(KBSGlyphWalkerClient, kKBSGlyphWalkerClientImpl)
DEFINE_HELPER_METHODS(KBSGlyphWalkerClient)

KBSGlyphWalkerClient::KBSGlyphWalkerClient(IPMUnknown* boss) :
	HELPER_METHODS_INIT(boss)
{
}

KBSGlyphWalkerClient::~KBSGlyphWalkerClient()
{
}

bool16 KBSGlyphWalkerClient::OnStart(ITextWalker* pWalker)
{
	Trace("  WALK start");
	return kTrue;
}

bool16 KBSGlyphWalkerClient::OnEnd(ITextWalker* pWalker)
{
	Trace("  WALK end");
	return kTrue;
}

bool16 KBSGlyphWalkerClient::OnStoryStart(ITextWalker* pWalker, UID storyUID)
{
	++gStoryCount;

	char line[128];
	::sprintf_s(line, sizeof(line), "  WALK story #%d uid=%d",
				static_cast<int>(gStoryCount), static_cast<int>(storyUID.Get()));
	Trace(line);
	return kTrue;
}

bool16 KBSGlyphWalkerClient::OnNextPosition(ITextWalker* pWalker, ITextModel* pModel,
											TextIndex nPosition,
											TextIndex nStartRange, TextIndex nEndRange,
											int32 startRangePrcnt, int32 endRangePrcnt,
											bool16 /* rangeAdjustable */)
{
	if (pWalker == nil || pModel == nil)
		return kTrue;

	++gRangeCount;
	if (gRangeCount > kMaxRanges)
	{
		Trace("  !! range cap reached - halting the walk");
		pWalker->Halt();
		return kTrue;
	}

	// ----- progress and cancel -----
	// The monitor is only a parking space (ITextWalkerProgressMonitor.h has just a get and a set),
	// so moving the bar is this client's job. Same shape as SpellReplaceWalker.cpp:461-497, minus
	// its Change-All-specific averaging: the walker already says what percentage of the whole walk
	// this range spans, so a position inside the range interpolates between the two.
	InterfacePtr<ITextWalkerProgressMonitor> monitor(this, IID_ITEXTWALKERPROGRESSMONITOR);
	RangeProgressBar* bar = (monitor != nil) ? monitor->GetWalkerProgressMonitor() : nil;
	if (bar != nil)
	{
		const int32 span = static_cast<int32>(nEndRange - nStartRange);
		const int32 pct = (span > 0)
			? (startRangePrcnt + static_cast<int32>((nPosition - nStartRange) * (endRangePrcnt - startRangePrcnt) / span))
			: endRangePrcnt;
		bar->SetPosition(pct);

		if (bar->WasCancelled())
		{
			Trace("  WALK cancelled by the user");
			pWalker->Halt();
			return kTrue;
		}
	}

	// ----- what the walker is offering, written down BEFORE any of it is searched -----
	std::vector<TextIndex> anchors;
	CollectTableAnchors(pModel, nPosition, nEndRange, anchors);
	if (!anchors.empty())
		++gRangesWithAnchor;

	char line[256];
	::sprintf_s(line, sizeof(line), "  WALK pos=%d range=%d..%d pct=%d..%d anchors=%d",
				static_cast<int>(nPosition), static_cast<int>(nStartRange),
				static_cast<int>(nEndRange), static_cast<int>(startRangePrcnt),
				static_cast<int>(endRangePrcnt), static_cast<int>(anchors.size()));
	Trace(line);

	// ***** findEnd is EXCLUSIVE, and every table anchor has to be kept out of the range. *****
	//
	// Both halves of that were measured on 2026-08-01, and they are what the two earlier readings
	// of this API got wrong between them:
	//
	//   EXCLUSIVE. The footnote block is 11..13 with a notdef at 12.
	//       SearchForGlyph(12, 12) found nothing    <- an EMPTY range, not a miss
	//       SearchForGlyph(12, 13) found it at 12
	//     Taking one off nEndRange first therefore came up short by one hit per block.
	//
	//   ANCHORS ARE FATAL, and the walker does NOT remove them. It offered range=0..1 on a table
	//     story whose index 0 is the anchor, and searching that killed InDesign. The earlier note
	//     that "a lone anchor at 0..0 is safe" was an artefact of the inclusive misreading: with an
	//     exclusive end, 0..0 is empty and never looked at the anchor at all.
	//     (It also explains the older "findEnd's ceiling is TotalLength() - 1": the ceiling is real,
	//     but what it keeps out is the story's final uneditable carriage return.)
	//
	// So the range is cut into the stretches BETWEEN its anchors, and each is searched on its own.
	// Nothing is lost by skipping the anchor positions themselves: an anchor is where a table hangs,
	// not a character that can come out as a box.
	TextIndex segStart = nPosition;
	for (size_t a = 0; a <= anchors.size(); ++a)
	{
		const TextIndex segEnd = (a < anchors.size()) ? anchors[a] : nEndRange;	// exclusive
		if (segEnd > segStart)
			SearchSegment(pModel, segStart, segEnd);
		if (a < anchors.size())
			segStart = anchors[a] + 1;
	}

	// The whole range has been dealt with in one go, so step past it and let the walker offer the
	// next one. Moving the walker on is the client's job in a batch walk - HyphenateStoryWalker
	// does the same, one word at a time (LinguisticTestMenu.cpp:1482).
	pWalker->MoveTo(nEndRange);
	return kTrue;
}

//========================================================================================
// KBSGlyphWalkerData - what the engine reads back after the walk
//========================================================================================

void KBSGlyphWalkerData::Reset()
{
	gHitCount = 0;
	gRangeCount = 0;
	gRangesWithAnchor = 0;
	gStoryCount = 0;
	gDigest.Clear();
	gDigest.SetTranslatable(kFalse);
}

int32 KBSGlyphWalkerData::GetHitCount()			{ return gHitCount; }
int32 KBSGlyphWalkerData::GetRangeCount()		{ return gRangeCount; }
int32 KBSGlyphWalkerData::GetRangesWithAnchor()	{ return gRangesWithAnchor; }
int32 KBSGlyphWalkerData::GetStoryCount()		{ return gStoryCount; }

const PMString& KBSGlyphWalkerData::GetDigest()	{ return gDigest; }

// End, KBSGlyphWalkerClient.cpp.
