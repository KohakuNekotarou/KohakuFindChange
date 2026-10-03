//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSHitMarker.h. Two things live here: where the marker is, and the adornment that draws it.
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

// General includes:
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "FileUtils.h"				// IsEqual - the marker's document confirmed by its file
#include "GraphicsData.h"
#include "GraphicTypes.h"			// kPMBlendDifference
#include "IDFile.h"
#include "PMRect.h"
#include "TextDrawPriority.h"		// kTAPassPriForeground

#include <boost/thread/recursive_mutex.hpp>

// Project includes:
#include "KBSID.h"
#include "KBSHitMarker.h"
#include "KBSDiag.h"		// KBS_DIAG_LOG - test builds only

namespace
{

// ---- where the marker is -------------------------------------------------------------------
//
// ***** WRITTEN ON THE MAIN THREAD, AND THE ADORNMENT MAY BE ASKED ON ANOTHER. ***** A global text
// adornment is a service, which the registry resolves in every execution context - the
// asynchronous PDF export's background thread included (KCM measured it, KCM.fr says so). This
// marker never draws there (kPrinting is refused before anything is read), but GetCouldDraw and
// GetInkBounds are not told why they are being asked, so every read of the state below takes the
// lock - except gHasMark, which is read first and alone so that each run of every page drawn while
// NO marker is up costs one test and no lock (the order KCMStoryMarker uses).
boost::recursive_mutex gLock;
typedef boost::recursive_mutex::scoped_lock Lock;

volatile bool16 gHasMark  = kFalse;	// set kTrue only after the rest is filled in; kFalse before it is emptied
IDataBase*      gDB       = nil;	// an ADDRESS, never read through: see KBSHitMarkerSameDoc
IDFile          gDocFile;			// the file gDB's document lived in when the marker went up...
bool16          gDocHasFile = kFalse;	// ...if it had one (kFalse = never saved)
UID             gStory    = kInvalidUID;
TextIndex       gStart    = 0;
TextIndex       gEnd      = 0;
bool16          gShutdown = kFalse;

// How far the inversion reaches above and below the baseline, as a fraction of the type size, and
// how wide the bar for a zero-width hit is. The same figures as KCM's marker (KCMStoryMarker.cpp),
// so the two plug-ins mark a line the same way.
const double kAscentFraction     = 0.85;
const double kDescentFraction    = 0.10;
const double kCaretWidthFraction = 0.25;

// ***** SAME ADDRESS IS NOT SAME DOCUMENT. ***** A closed document's address can be handed to the
// next document opened, so the address is confirmed by the file (the rule the Draw Event marker kept
// from 2026-08-04 - memory uidref-reuse-after-close). Neither ever saved: the address stands alone.
// gDB is compared, never read; drawnDB is a wax run's, being drawn, so it is certainly alive.
// ***** THE FILES ARE COMPARED, NOT THEIR PATH STRINGS (API re-audit, 2026-10-02). ***** FileUtils::IsEqual,
// the rule KBSBookScope's KBSDocumentLivesInFile keeps (and KCM's KCMIsSameDoc) - this compared the two
// paths' strings until then. Caller holds gLock.
bool KBSHitMarkerSameDoc(IDataBase* drawnDB)
{
	if (drawnDB == nil || drawnDB != gDB)
		return false;
	const IDFile* drawnFile = drawnDB->GetSysFile();
	if (drawnFile == nil || !gDocHasFile)
		return drawnFile == nil && !gDocHasFile;
	return FileUtils::IsEqual(*drawnFile, gDocFile) != kFalse;
}

// Forget where the marker is - gHasMark first, the flag every unlocked reader tests. The one place
// ClearMarker, ForgetDoc and ShutdownCleanup empty the state (each wrote the four lines out until
// 2026-10-01). Caller holds gLock.
void KBSHitMarkerForget()
{
	gHasMark = kFalse;
	gDB = nil;
	gDocFile = IDFile();
	gDocHasFile = kFalse;
	gStory = kInvalidUID;
}

// (KBSHitMarkerRepaint - repaint a document so the marker appears or disappears now - stood here until
//  2026-10-01. Repainting views is the UI half's: KBSHitMarkerView.cpp, with its notes.)

// The part of this run the marker covers, as character offsets into the run. False when none of it
// does. Caller holds gLock and has checked gHasMark.
bool KBSHitMarkerRunPart(const IWaxRun* waxRun, int32& outCharStart, int32& outCharCount)
{
	if (waxRun == nil)
		return false;
	const int32 runCount = waxRun->GetCharCount();
	if (runCount <= 0)
		return false;
	const TextIndex runStart = waxRun->TextOrigin();
	const TextIndex runEnd = runStart + runCount;

	// Cheapest first: does the run touch the marked characters at all? A zero-width marker sits at
	// gStart, which belongs to the run that holds that character.
	if (gEnd > gStart)
	{
		if (runEnd <= gStart || runStart >= gEnd)
			return false;
	}
	else if (gStart < runStart || gStart >= runEnd)
		return false;

	// Then which story and which document - the Query this costs is only paid by runs that passed.
	const IWaxLine* waxLine = waxRun->GetWaxLine();
	if (waxLine == nil)
		return false;
	InterfacePtr<ITextModel> model(waxLine->QueryTextModel());
	if (model == nil)
		return false;
	const UIDRef modelRef = ::GetUIDRef(model);
	if (modelRef.GetUID() != gStory || !KBSHitMarkerSameDoc(modelRef.GetDataBase()))
		return false;

	const TextIndex from = (gStart > runStart) ? gStart : runStart;
	const TextIndex to = (gEnd < runEnd) ? gEnd : runEnd;
	outCharStart = static_cast<int32>(from - runStart);
	outCharCount = (to > from) ? static_cast<int32>(to - from) : 0;
	return true;
}

// The rectangle to invert in this run, in the run's own coordinates (what Draw is handed). False when
// the run cannot be measured - an inline graphic has neither glyphs nor render data.
// ***** CHARACTERS ARE NOT GLYPHS. ***** The range is mapped with MapCharsToGlyphs before any width
// is added up - the call the product's spelling squiggle makes (DynamicSpellCheckAdornment.cpp), and
// KCM's GetMarkBoxes after it.
bool KBSHitMarkerBox(const IWaxRun* waxRun, const IWaxRenderData* renderData,
	const IWaxGlyphs* waxGlyphs, PMRect& outBox)
{
	if (waxRun == nil || renderData == nil || waxGlyphs == nil)
		return false;

	int32 charStart = 0, charCount = 0;
	{
		Lock lock(gLock);
		if (!gHasMark || !KBSHitMarkerRunPart(waxRun, charStart, charCount))
			return false;
	}

	const int32 glyphCount = waxGlyphs->GetGlyphCount();
	if (glyphCount <= 0)
		return false;

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

	const PMReal size = renderData->GetFontMatrix().GetYScale();
	if (charCount == 0 || width <= 0.0)
		width = size * PMReal(kCaretWidthFraction);	// a zero-width hit still has a place

	const PMReal x = waxRun->GetXPosition() + offset;
	const PMReal y = waxRun->GetYPosition();		// the baseline
	outBox.Left(x);
	outBox.Right(x + width);
	outBox.Top(y - size * PMReal(kAscentFraction));
	outBox.Bottom(y + size * PMReal(kDescentFraction));
	return true;
}

} // anonymous namespace

//----------------------------------------------------------------------------------------
// The adornment
//----------------------------------------------------------------------------------------

class KBSHitMarkerAdornment : public CPMUnknown<IGlobalTextAdornment>
{
public:
	KBSHitMarkerAdornment(IPMUnknown* boss) : CPMUnknown<IGlobalTextAdornment>(boss) {}
	~KBSHitMarkerAdornment() {}

	// FOREGROUND, after the glyphs: an inversion has to cover the characters for them to invert with
	// their ground. kPassForeground is 16384 (DrawPassInfo.h:94); the product's own foreground
	// adornments sit at +0.50 (invisibles) and +0.52 (the spelling squiggle) -
	// IGlobalTextAdornment.h:180-181. A positive fraction under 1 keeps this in the same pass.
	virtual Text::DrawPriority GetDrawPriority()
		{ return Text::DrawPriority(Text::kTAPassPriForeground + 0.55); }

	virtual bool16 GetCheckIsActive() { return kTrue; }
	virtual bool16 GetIsActive(const IParcelShape*, const ITextOptions*, int32 iShapeFlags)
	{
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
		return (gHasMark && KBSHitMarkerRunPart(waxRun, charStart, charCount)) ? kTrue : kFalse;
	}

	// The inversion reaches from ascent to descent, which is more than a glyph's own ink - declared,
	// or it would be clipped to the glyphs.
	virtual bool16 GetHasInkBounds() { return kTrue; }
	virtual void GetInkBounds(PMRect* inkBounds, const IWaxRun* waxRun,
		const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
	{
		PMRect box;
		if (inkBounds != nil && KBSHitMarkerBox(waxRun, renderData, waxGlyphs, box))
			*inkBounds = box;
	}

	virtual void Draw(GraphicsData* gd, int32 iShapeFlags, const IWaxRun* waxRun,
		const IWaxRenderData* renderData, const IWaxGlyphs* waxGlyphs)
	{
		if (gd == nil || !gHasMark)
			return;
		if ((iShapeFlags & IShape::kPrinting) != 0)
			return;
		// ***** EVERY SCREEN MODE, OVERPRINT PREVIEW INCLUDED (user's call, 2026-09-26). ***** The
		// Draw Event marker hid itself under Overprint Preview (kSepPrvOPPEnabledVPAttr) on the reading
		// that the preview simulates print; the user asked for the marker after a jump to show in
		// whatever mode the window is in. Only paper and exports (kPrinting, above) go without it...
		// ***** ...AND A PAGE DRAWN AS A PICTURE: kPreviewMode WITH NO VIEW (2026-10-03, M-1). ***** The
		// screen's preview modes draw with kPreviewMode too, so the flag alone cannot tell them apart; the
		// view can (the Pages panel's thumbnail test KCM and KIDMCP use). See the header.
		if ((iShapeFlags & IShape::kPreviewMode) != 0 && gd->GetView() == nil)
		{
			KBS_DIAG_LOG("MARKER refused flags=0x%x view=0", (unsigned)iShapeFlags);
			return;
		}

		PMRect box;
		if (!KBSHitMarkerBox(waxRun, renderData, waxGlyphs, box))
			return;
		KBS_DIAG_LOG("MARKER draw flags=0x%x view=%d", (unsigned)iShapeFlags, gd->GetView() != nil ? 1 : 0);

		IGraphicsPort* gPort = gd->GetGraphicsPort();
		if (gPort == nil)
			return;

		// White over Difference = (1 - backdrop): a full inversion, visible on any ground, with the
		// glyphs inverting along with it so the text stays readable. The blending mode is part of the
		// graphics state, so AutoGSave puts it back. (Unchanged from the Draw Event marker.)
		AutoGSave ag(gPort);
		gPort->setblendingmode(kPMBlendDifference);
		gPort->setrgbcolor(PMReal(1.0), PMReal(1.0), PMReal(1.0));
		gPort->rectfill(box.Left(), box.Top(), box.Width(), box.Height());
		gPort->newpath();
	}

	virtual void StartOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}
	virtual void EndOfParcelDraw(GraphicsData*, int32, const IParcelShape*) {}
};

CREATE_PMINTERFACE(KBSHitMarkerAdornment, kKBSHitMarkerAdornmentImpl)

//----------------------------------------------------------------------------------------
// The public face
//----------------------------------------------------------------------------------------

bool KBSHitMarker::SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end, IDataBase*& outPreviousDB)
{
	outPreviousDB = nil;
	if (gShutdown || db == nil || storyUID == kInvalidUID)
		return false;
	if (end < start)
		end = start;

	IDataBase* previousDB = nil;
	const IDFile* const sysFile = db->GetSysFile();	// read now, while the document is certainly alive
	{
		Lock lock(gLock);
		previousDB = gHasMark ? gDB : nil;
		gHasMark = kFalse;
		gDB = db;
		gDocHasFile = (sysFile != nil) ? kTrue : kFalse;
		gDocFile = (sysFile != nil) ? *sysFile : IDFile();
		gStory = storyUID;
		gStart = start;
		gEnd = end;
		gHasMark = kTrue;
	}
	if (previousDB != nil && previousDB != db)
		outPreviousDB = previousDB;
	// The repaint of both, and the countdown, are the caller's (KBSHitMarkerView - the UI half).
	return true;
}

bool KBSHitMarker::ClearMarker(IDataBase*& outDB)
{
	outDB = nil;
	if (gShutdown)
		return false;
	// (The countdown is stopped by the caller - KBSHitMarkerView, the UI half - since 2026-10-01.)

	IDataBase* db = nil;
	{
		Lock lock(gLock);
		db = gHasMark ? gDB : nil;
		KBSHitMarkerForget();
	}
	outDB = db;		// repainted by the caller
	return true;
}

void KBSHitMarker::ForgetDoc(IDataBase* db)
{
	if (db == nil)
		return;
	Lock lock(gLock);
	if (gDB != db)
		return;			// compared, never read
	KBSHitMarkerForget();
}

void KBSHitMarker::ShutdownCleanup()
{
	gShutdown = kTrue;
	Lock lock(gLock);
	KBSHitMarkerForget();	// gDocFile too: a static path must not outlive the DLL's teardown
}

// End, KBSHitMarker.cpp.
