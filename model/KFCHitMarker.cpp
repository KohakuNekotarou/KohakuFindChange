//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  See KFCHitMarker.h. Two things live here: where the marker is, and the adornment that draws it.
//  They are one question - "is anything marked, and where" - so they are kept together.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IDataBase.h"
#include "IGlobalTextAdornment.h"
#include "IGraphicsPort.h"
#include "IShape.h"					// kPrinting
#include "ITextModel.h"				// which story a wax run belongs to
#include "IWaxGlyphs.h"
#include "IWaxLine.h"
#include "IWaxRenderData.h"
#include "IWaxRun.h"
#include "IWaxRunShape.h"			// GetWaxBounds - text on a path, a quadrilateral a character

// General includes:
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "FileUtils.h"				// IsEqual - the marker's document confirmed by its file
#include "GraphicsData.h"
#include "GraphicTypes.h"			// kPMBlendDifference
#include "IDFile.h"
#include "K2Vector.h"
#include "PMPolygon4.h"
#include "PMRect.h"
#include "TextDrawPriority.h"		// kTAPassPriForeground

#include <boost/thread/recursive_mutex.hpp>
#include <atomic>					// gHasMark - read off the main thread without the lock
#include <cmath>					// std::sqrt - a bar's direction on a path

// Project includes:
#include "KFCID.h"
#include "KFCHitMarker.h"
#include "KFCDiag.h"		// KFC_DIAG_LOG - test builds only

#ifdef KFC_DIAG
#include "IDThreadingPrimitives.h"	// IDThreading::IsMainThreadDomain - a test build counts the asks from another thread
#endif

namespace
{

// ---- where the marker is -------------------------------------------------------------------
//
// WRITTEN ON THE MAIN THREAD, AND THE ADORNMENT MAY BE ASKED ON ANOTHER. A global text
// adornment is a service, which the registry resolves in every execution context - the
// asynchronous PDF export's background thread included (KCM measured it, KCM.fr says so). This
// marker never draws there (kPrinting is refused before anything is read), but GetCouldDraw and
// GetInkBounds are not told why they are being asked, so every read of the state below takes the
// lock - except gHasMark, which is read first and alone so that each run of every page drawn while
// NO marker is up costs one test and no lock (the order KCMStoryMarker uses).
boost::recursive_mutex gLock;
typedef boost::recursive_mutex::scoped_lock Lock;

// Where the marker is, as ONE value - put up and emptied whole under gLock, so neither can leave a field behind.
struct Mark
{
	IDataBase*	db;			// an ADDRESS, never read through: see KFCHitMarkerSameDoc
	IDFile		docFile;	// the file db's document lived in when the marker went up...
	bool		docHasFile;	// ...if it had one (false = never saved)
	UID			story;
	TextIndex	start;
	TextIndex	end;
	Mark() : db(nil), docHasFile(false), story(kInvalidUID), start(0), end(0) {}
};
Mark gMark;

// Set true only after gMark is filled in; false before it is emptied. Read first and alone, without the lock (above) -
// std::atomic, so that read is the language's and not the compiler's reading of volatile.
std::atomic<bool> gHasMark(false);
bool gShutdown = false;

#ifdef KFC_DIAG
// A TEST BUILD SAYS WHEN THE ADORNMENT IS ASKED OFF THE MAIN THREAD (the thread test - an export's background thread draws
// the text too, and the state above is read there). Asked once per parcel, so not every ask is written: the 1st, 10th and
// 100th of the session, then every 250th (so a later run in the same session still writes some), with whether a marker was
// up and whether the ask was for paper or an export.
void KFCHitMarkerNoteOffMain(int32 iShapeFlags)
{
	if (IDThreading::IsMainThreadDomain())
		return;
	static std::atomic<int> asks(0);
	const int n = ++asks;
	if (n == 1 || n == 10 || n == 100 || n % 250 == 0)
		KFC_DIAG_LOG("MARKER asked off the main thread n=%d mark=%d printing=%d", n, gHasMark ? 1 : 0,
			(iShapeFlags & IShape::kPrinting) != 0 ? 1 : 0);
}
#endif

// How far the inversion reaches above and below the baseline, as a fraction of the type size, and
// how wide the bar for a zero-width hit is. The same figures as KCM's marker (KCMStoryMarker.cpp),
// so the two plug-ins mark a line the same way.
const double kAscentFraction     = 0.85;
const double kDescentFraction    = 0.10;
const double kCaretWidthFraction = 0.25;

// SAME ADDRESS IS NOT SAME DOCUMENT. A closed document's address can be handed to the next document
// opened, so the address is confirmed by the file (memory uidref-reuse-after-close). Neither ever
// saved: the address stands alone. The mark's db is compared, never read; drawnDB is a wax run's, being drawn,
// so it is certainly alive.
// THE FILES ARE COMPARED, NOT THEIR PATH STRINGS. FileUtils::IsEqual, the rule KFCBookScope's
// KFCDocumentLivesInFile keeps (and KCM's KCMIsSameDoc). Caller holds gLock.
bool KFCHitMarkerSameDoc(IDataBase* drawnDB)
{
	if (drawnDB == nil || drawnDB != gMark.db)
		return false;
	const IDFile* drawnFile = drawnDB->GetSysFile();
	if (drawnFile == nil || !gMark.docHasFile)
		return drawnFile == nil && !gMark.docHasFile;
	return FileUtils::IsEqual(*drawnFile, gMark.docFile) != kFalse;
}

// Forget where the marker is - gHasMark first, the flag every unlocked reader tests. The one place
// ClearMarker, ForgetDoc and ShutdownCleanup empty the state. Caller holds gLock.
void KFCHitMarkerForget()
{
	gHasMark = false;
	gMark = Mark();
}

// (Repainting a document so the marker appears or disappears now is the UI half's:
//  KFCHitMarkerView.cpp.)

// The part of this run the marker covers, as character offsets into the run. False when none of it
// does. Caller holds gLock and has checked gHasMark.
bool KFCHitMarkerRunPart(const IWaxRun* waxRun, int32& outCharStart, int32& outCharCount)
{
	if (waxRun == nil)
		return false;
	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return false;
	const TextIndex runStart = waxRun->TextOrigin();
	const TextIndex runEnd = runStart + runCount;

	// Cheapest first: does the run touch the marked characters at all? A zero-width marker sits at
	// its start, which belongs to the run that holds that character.
	const TextIndex markStart = gMark.start, markEnd = gMark.end;
	if (markEnd > markStart)
	{
		if (runEnd <= markStart || runStart >= markEnd)
			return false;
	}
	else if (markStart < runStart || markStart >= runEnd)
		return false;

	// Then which story and which document - the Query this costs is only paid by runs that passed.
	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return false;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return false;
	const UIDRef modelRef = ::GetUIDRef(model);
	if (modelRef.GetUID() != gMark.story || !KFCHitMarkerSameDoc(modelRef.GetDataBase()))
		return false;

	const TextIndex from = (markStart > runStart) ? markStart : runStart;
	const TextIndex to = (markEnd < runEnd) ? markEnd : runEnd;
	outCharStart = static_cast<int32>(from - runStart);
	outCharCount = (to > from) ? static_cast<int32>(to - from) : 0;
	return true;
}

// What the marker fills in one run: its rectangle - or, for text on a path, one quadrilateral a character instead
// (KFCHitMarkerShapeOnPath), the rectangle then what they reach. The ink bounds read the rectangle either way.
struct KFCMarkShape
{
	PMRect					box;
	K2Vector<PMPolygon4>	quads;
};

// A ZERO-WIDTH MARKER ON A PATH: a bar as tall as the character's quadrilateral and leaning the way it leans,
// standing at the edge the place is at and running on in the reading direction - what the bar is on a straight
// line, turned to the path. KCM's KCMStoryMarkerBarOf, where it was measured: the corners run with the text
// whichever way up it stands - 0 start and top, 1 end and top, 2 end and bottom, 3 start and bottom.
// after = at the character's far edge rather than its near one.
PMPolygon4 KFCHitMarkerBarOf(const PMPolygon4& quad, bool after, const PMReal& width)
{
	const PMPoint top = quad[after ? 1 : 0];
	const PMPoint bottom = quad[after ? 2 : 3];
	// A character with no width has no top edge to read the direction off: square to its side instead.
	double dx = ::ToDouble(quad[1].X() - quad[0].X());
	double dy = ::ToDouble(quad[1].Y() - quad[0].Y());
	if (dx == 0.0 && dy == 0.0)
	{
		dx = ::ToDouble(quad[3].Y() - quad[0].Y());
		dy = ::ToDouble(quad[0].X() - quad[3].X());
	}
	const double length = std::sqrt(dx * dx + dy * dy);
	const double scale = (length > 0.0) ? ::ToDouble(width) / length : 0.0;
	const PMReal alongX = dx * scale;
	const PMReal alongY = dy * scale;
	PMPolygon4 bar;
	bar[0] = top;
	bar[1] = PMPoint(top.X() + alongX, top.Y() + alongY);
	bar[2] = PMPoint(bottom.X() + alongX, bottom.Y() + alongY);
	bar[3] = bottom;
	return bar;
}

// TEXT ON A PATH (IWaxRun::IsTOPRun): the marked characters' own quadrilaterals.
// ***** A STRAIGHT BOX CANNOT DO IT *****: such a run stands at the path's own origin and each glyph carries its
// own matrix, so the run's x and the glyphs' widths are not where the characters are. Measured on 2026-11-07 (the
// block 12 re-inspection - work/note-scripts/2026-11-07-kfc-block12, jump_mark_probe.cpp and live_slot.ps1): for
// "cat" on an oval's text path the box came out at 0,-9.6 to 13.2,-0.2 and the characters' quadrilaterals at
// 136,190 to 150,200 - on screen the marker went up on the oval's top-left corner, the words at its bottom.
// IWaxRunShape::GetWaxBounds answers in the space Draw is handed ("Used mostly for selection and highlighting" -
// IWaxRunShape.h); KCM's marker took this road first (KCMStoryMarkerBoxOnPath - KCM's M10 re-inspection: 4% of
// its wash near the ink before, 99% after). A zero-width marker is a bar at its character's near edge - or at the
// far edge of the one before it, when its own character gives no shape. False when the run gives no shape there.
bool KFCHitMarkerShapeOnPath(const IWaxRun* waxRun, int32 charStart, int32 charCount, const PMReal& barWidth,
	KFCMarkShape& out)
{
	InterfacePtr<const IWaxRunShape> shape(waxRun, UseDefaultIID());
	if (shape == nil)
		return false;
	K2Vector<PMPolygon4> quads;
	bool after = false;
	shape->GetWaxBounds(charStart, (charCount > 0) ? charCount : 1, &quads);
	if (quads.empty() && charCount == 0 && charStart > 0)
	{
		shape->GetWaxBounds(charStart - 1, 1, &quads);
		after = true;
	}
	if (quads.empty())
		return false;
	if (charCount == 0)
	{
		const PMPolygon4 bar = KFCHitMarkerBarOf(after ? quads.back() : quads.front(), after, barWidth);
		quads.clear();
		quads.push_back(bar);
	}
	PMRect box = quads.front().GetContainingRect();
	for (K2Vector<PMPolygon4>::const_iterator q = quads.begin(); q != quads.end(); ++q)
		box.Union(q->GetContainingRect());
	out.box = box;
	out.quads.swap(quads);
	return true;
}

// The shape to invert in this run, in the run's own coordinates (what Draw is handed): a rectangle, or - on a
// path - one quadrilateral a character (KFCHitMarkerShapeOnPath). False when the run gives nothing to measure (no
// render data, no glyphs). AN INLINE MATCH GETS NO BOX, ON PURPOSE (the author's call): its landing selects it -
// the anchor and the object with it, as Find Next does - and the marker is for a match that cannot be selected.
// Not for want of a way to measure one: IInlineData::GetGlyphBoundingBox does.
// NOR DOES A TABLE'S CHARACTER, BY ITS NATURE: it has no glyph to lay the inversion on. Measured on 2026-11-07
// (the same re-inspection): the run that holds a table's two characters gives no glyph for them
// (QueryRunByTextOffset's glyph offset -1), and a locked table's landing drew nothing (no "MARKER draw" in the test
// build's trace) - the view shows the table and the row's note, or the face's line, says why (the spec map's
// TBL-20, TBL-63; KCM's SMK-26 records the same of its marks).
// CHARACTERS ARE NOT GLYPHS. The range is mapped with MapCharsToGlyphs before any width
// is added up - the call the product's spelling squiggle makes (DynamicSpellCheckAdornment.cpp), and
// KCM's GetMarkBoxes after it.
bool KFCHitMarkerBox(const IWaxRun* waxRun, const IWaxRenderData* renderData,
	const IWaxGlyphs* waxGlyphs, KFCMarkShape& out)
{
	if (waxRun == nil || renderData == nil || waxGlyphs == nil)
		return false;

	int32 charStart = 0, charCount = 0;
	{
		Lock lock(gLock);
		if (!gHasMark || !KFCHitMarkerRunPart(waxRun, charStart, charCount))
			return false;
	}

	const int32 glyphCount = waxGlyphs->GetGlyphCount();
	if (glyphCount <= 0)
		return false;

	const PMReal size = renderData->GetFontMatrix().GetYScale();
	if (waxRun->IsTOPRun())
		return KFCHitMarkerShapeOnPath(waxRun, charStart, charCount, size * PMReal(kCaretWidthFraction), out);

	int32 glyphIndex = -1, glyphLength = 0;
	waxGlyphs->MapCharsToGlyphs(charStart, (charCount > 0) ? charCount : 1, &glyphIndex, &glyphLength);
	if (glyphIndex < 0 || glyphIndex >= glyphCount)
		return false;
	if (glyphLength <= 0)
		glyphLength = 1;
	if (glyphIndex + glyphLength > glyphCount)
		glyphLength = glyphCount - glyphIndex;

	PMReal offset(0.0), width(0.0);
	for (int32 i = 0; i < glyphIndex; ++i)
		offset += waxGlyphs->GetWidthAt(i);
	for (int32 i = glyphIndex; i < glyphIndex + glyphLength; ++i)
		width += waxGlyphs->GetWidthAt(i);

	if (charCount == 0 || width <= 0.0)
		width = size * PMReal(kCaretWidthFraction);	// a zero-width hit still has a place

	const PMReal x = waxRun->GetXPosition() + offset;
	const PMReal y = waxRun->GetYPosition();		// the baseline
	out.quads.clear();
	out.box.Left(x);
	out.box.Right(x + width);
	out.box.Top(y - size * PMReal(kAscentFraction));
	out.box.Bottom(y + size * PMReal(kDescentFraction));
	return true;
}

} // anonymous namespace

//----------------------------------------------------------------------------------------
// The adornment
//----------------------------------------------------------------------------------------

class KFCHitMarkerAdornment : public CPMUnknown<IGlobalTextAdornment>
{
public:
	KFCHitMarkerAdornment(IPMUnknown* boss) : CPMUnknown<IGlobalTextAdornment>(boss) {}
	~KFCHitMarkerAdornment() {}

	// FOREGROUND, after the glyphs: an inversion has to cover the characters for them to invert with
	// their ground. kPassForeground is 16384 (DrawPassInfo.h:94); the product's own foreground
	// adornments sit at +0.50 (invisibles) and +0.52 (the spelling squiggle) -
	// IGlobalTextAdornment.h:180-181. A positive fraction under 1 keeps this in the same pass.
	virtual Text::DrawPriority GetDrawPriority()
		{ return Text::DrawPriority(Text::kTAPassPriForeground + 0.55); }

	virtual bool16 GetCheckIsActive() { return kTrue; }
	virtual bool16 GetIsActive(const IParcelShape*, const ITextOptions*, int32 iShapeFlags)
	{
#ifdef KFC_DIAG
		KFCHitMarkerNoteOffMain(iShapeFlags);
#endif
		if (!gHasMark)
			return kFalse;
		// Never on paper or in an export. kPreviewMode is NOT refused here - the screen's preview modes
		// draw with it - and Draw refuses it only where there is no view (see the header).
		return ((iShapeFlags & IShape::kPrinting) != 0) ? kFalse : kTrue;
	}

	// kTrue: the marker covers a handful of characters in one story, so saying no to every other run
	// is what keeps the text engine from calling Draw for the whole document while it is up.
	virtual bool16 GetCheckCouldDraw() { return kTrue; }
	virtual bool16 GetCouldDraw(const IWaxRun* waxRun, const IWaxRenderData*, const IWaxGlyphs*)
	{
		if (!gHasMark)
			return kFalse;
		Lock lock(gLock);
		int32 charStart = 0, charCount = 0;
		return (gHasMark && KFCHitMarkerRunPart(waxRun, charStart, charCount)) ? kTrue : kFalse;
	}

	// The inversion reaches from ascent to descent, which is more than a glyph's own ink - declared,
	// or it would be clipped to the glyphs.
	virtual bool16 GetHasInkBounds() { return kTrue; }
	virtual void GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
		const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
	{
		KFCMarkShape shape;
		if (inkBounds != nil && KFCHitMarkerBox(waxRun, renderData, waxGlyphs, shape))
			*inkBounds = shape.box;
	}

	virtual void Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
		const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
	{
		if (gd == nil || !gHasMark)
			return;
		if ((iShapeFlags & IShape::kPrinting) != 0)
			return;
		// EVERY SCREEN MODE, OVERPRINT PREVIEW INCLUDED (the author's call). Not hidden under Overprint
		// Preview (kSepPrvOPPEnabledVPAttr) on the reading that the preview simulates print: the marker
		// after a jump shows in whatever mode the window is in. Only paper and exports (kPrinting, above)
		// go without it...
		// ...AND A PAGE DRAWN AS A PICTURE: kPreviewMode WITH NO VIEW. The screen's preview modes draw
		// with kPreviewMode too, so the flag alone cannot tell them apart; the
		// view can (the Pages panel's thumbnail test KCM and KIDMCP use). See the header.
		if ((iShapeFlags & IShape::kPreviewMode) != 0 && gd->GetView() == nil)
		{
			KFC_DIAG_LOG("MARKER refused flags=0x%x view=0", (unsigned)iShapeFlags);
			return;
		}

		KFCMarkShape shape;
		if (!KFCHitMarkerBox(waxRun, renderData, waxGlyphs, shape))
			return;
		KFC_DIAG_LOG("MARKER draw flags=0x%x view=%d quads=%d box=%.1f,%.1f %.1f,%.1f", (unsigned)iShapeFlags,
			gd->GetView() != nil ? 1 : 0, (int)shape.quads.size(),
			::ToDouble(shape.box.Left()), ::ToDouble(shape.box.Top()),
			::ToDouble(shape.box.Right()), ::ToDouble(shape.box.Bottom()));

		IGraphicsPort* gPort = gd->GetGraphicsPort();
		if (gPort == nil)
			return;

		// White over Difference = (1 - backdrop): a full inversion, visible on any ground, with the
		// glyphs inverting along with it so the text stays readable. The blending mode is part of the
		// graphics state, so AutoGSave puts it back.
		AutoGSave ag(gPort);
		gPort->setblendingmode(kPMBlendDifference);
		gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
		if (shape.quads.empty())
			gPort->rectfill(shape.box.Left(), shape.box.Top(), shape.box.Width(), shape.box.Height());
		else
		{
			// TEXT ON A PATH - ONE PATH, ONE FILL. The quadrilaterals overlap where the path bends, and an
			// inversion laid twice is no inversion: filled one by one (KCM's wash fills them so - opaque, it
			// shows no overlap) the overlaps would come back un-inverted. Filled as one path under the
			// non-zero rule (fill), every point inside is inverted once - the quadrilaterals all turn the
			// same way, the text's.
			gPort->newpath();
			for (K2Vector<PMPolygon4>::const_iterator q = shape.quads.begin(); q != shape.quads.end(); ++q)
			{
				gPort->moveto((*q)[0].X(), (*q)[0].Y());
				gPort->lineto((*q)[1].X(), (*q)[1].Y());
				gPort->lineto((*q)[2].X(), (*q)[2].Y());
				gPort->lineto((*q)[3].X(), (*q)[3].Y());
				gPort->closepath();
			}
			gPort->fill();
		}
		gPort->newpath();
	}

	virtual void StartOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}
	virtual void EndOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}
};

CREATE_PMINTERFACE(KFCHitMarkerAdornment, kKFCHitMarkerAdornmentImpl)

//----------------------------------------------------------------------------------------
// The public face
//----------------------------------------------------------------------------------------

bool KFCHitMarker::SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB)
{
	outPreviousDB = nil;
	if (gShutdown || db == nil || storyUID == kInvalidUID)
		return false;

	Mark mark;
	mark.db = db;
	const IDFile* const sysFile = db->GetSysFile();	// read now, while the document is certainly alive
	mark.docHasFile = (sysFile != nil);
	if (sysFile != nil)
		mark.docFile = *sysFile;
	mark.story = storyUID;
	mark.start = start;
	mark.end = (end < start) ? start : end;

	IDataBase* previousDB = nil;
	{
		Lock lock(gLock);
		previousDB = gHasMark ? gMark.db : nil;
		gHasMark = false;
		gMark = mark;
		gHasMark = true;
	}
	if (previousDB != nil && previousDB != db)
		outPreviousDB = previousDB;
	// The repaint of both, and the countdown, are the caller's (KFCHitMarkerView - the UI half).
	return true;
}

bool KFCHitMarker::ClearMarker(IDataBase*& outDB)
{
	outDB = nil;
	if (gShutdown)
		return false;
	// (The countdown is stopped by the caller - KFCHitMarkerView, the UI half.)

	IDataBase* db = nil;
	{
		Lock lock(gLock);
		db = gHasMark ? gMark.db : nil;
		KFCHitMarkerForget();
	}
	outDB = db;		// repainted by the caller
	return true;
}

void KFCHitMarker::ForgetDoc(IDataBase* db)
{
	if (db == nil)
		return;
	Lock lock(gLock);
	if (gMark.db != db)
		return;			// compared, never read
	KFCHitMarkerForget();
}

void KFCHitMarker::ShutdownCleanup()
{
	gShutdown = true;
	Lock lock(gLock);
	KFCHitMarkerForget();	// the mark's file too: a static path must not outlive the DLL's teardown
}

// End, KFCHitMarker.cpp.
