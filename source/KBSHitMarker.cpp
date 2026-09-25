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
#include "IApplication.h"
#include "IDataBase.h"
#include "IDocument.h"
#include "IDocumentList.h"
#include "IGlobalTextAdornment.h"
#include "IGraphicsPort.h"
#include "IShape.h"					// kPrinting
#include "ITextModel.h"				// which story a wax run belongs to
#include "IWaxGlyphs.h"
#include "IWaxLine.h"
#include "IWaxRenderData.h"
#include "IWaxRun.h"
#include "ISession.h"

// General includes:
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "GraphicsData.h"
#include "GraphicTypes.h"			// kPMBlendDifference
#include "ILayoutUIUtils.h"
#include "ILayoutUtils.h"			// InvalidateViews
#include "PMRect.h"
#include "PMString.h"
#include "SDKFileHelper.h"
#include "TextDrawPriority.h"		// kTAPassPriForeground
#include "Utils.h"

#include <boost/thread/recursive_mutex.hpp>

// Project includes:
#include "KBSID.h"
#include "KBSHitMarker.h"
#include "KBSMarkerExpiryIdleTask.h"

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
PMString        gDocPath;			// the file gDB's document lived in when the marker went up ("" = never saved)
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

// The file a database's document lives in - "" when it has none. Only ever called on a database that
// is certainly alive: the jump's (it has just been in it) and a wax run's (it is being drawn).
PMString KBSHitMarkerDocPath(IDataBase* db)
{
	PMString path;
	path.SetTranslatable(kFalse);
	if (db == nil)
		return path;
	const IDFile* sysFile = db->GetSysFile();
	if (sysFile == nil)
		return path;
	SDKFileHelper helper(*sysFile);
	path = helper.GetPath();
	path.SetTranslatable(kFalse);
	return path;
}

// ***** SAME ADDRESS IS NOT SAME DOCUMENT. ***** A closed document's address can be handed to the
// next document opened, so the address is confirmed by the file (the rule the Draw Event marker kept
// from 2026-08-04 - memory uidref-reuse-after-close). Two empty paths mean neither was ever saved,
// and the address stands alone. gDB is compared, never read. Caller holds gLock.
bool KBSHitMarkerSameDoc(IDataBase* drawnDB)
{
	if (drawnDB == nil || drawnDB != gDB)
		return false;
	const PMString drawnPath(KBSHitMarkerDocPath(drawnDB));
	if (drawnPath.IsEmpty() && gDocPath.IsEmpty())
		return true;
	return drawnPath == gDocPath;
}

// Repaint a document so the marker appears or disappears now - an adornment is only consulted while
// text is being drawn. The address is resolved through the document list first, so a document that
// has closed in the meantime is never touched (moved here from KBSDrawEventHandler unchanged).
void KBSHitMarkerRepaint(IDataBase* db)
{
	if (db != nil)
	{
		InterfacePtr<IApplication> app(GetExecutionContextSession()->QueryApplication());
		InterfacePtr<IDocumentList> docList(app ? app->QueryDocumentList() : nil);
		IDocument* doc = (docList != nil) ? docList->FindDocByDataBase(db) : nil;
		if (doc != nil)
		{
			Utils<ILayoutUtils>()->InvalidateViews(doc);
			return;
		}
	}
	IDocument* fdoc = Utils<ILayoutUIUtils>()->GetFrontDocument();
	if (fdoc != nil)
		Utils<ILayoutUtils>()->InvalidateViews(fdoc);
}

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
		// Never on paper or in an export. kPreviewMode is deliberately NOT refused - see the header.
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
		// whatever mode the window is in. Only paper and exports (kPrinting, above) go without it.

		PMRect box;
		if (!KBSHitMarkerBox(waxRun, renderData, waxGlyphs, box))
			return;

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

void KBSHitMarker::SetMarker(IDataBase* db, UID storyUID, TextIndex start, TextIndex end)
{
	if (gShutdown || db == nil || storyUID == kInvalidUID)
		return;
	if (end < start)
		end = start;

	IDataBase* previousDB = nil;
	const PMString path(KBSHitMarkerDocPath(db));	// taken now, while the document is certainly alive
	{
		Lock lock(gLock);
		previousDB = gHasMark ? gDB : nil;
		gHasMark = kFalse;
		gDB = db;
		gDocPath = path;
		gStory = storyUID;
		gStart = start;
		gEnd = end;
		gHasMark = kTrue;
	}
	if (previousDB != nil && previousDB != db)
		KBSHitMarkerRepaint(previousDB);
	KBSHitMarkerRepaint(db);

	// A pointer, not a highlight: the countdown takes it away again (restarted by every jump).
	KBSMarkerExpiryIdleTask::Start();
}

void KBSHitMarker::ClearMarker()
{
	if (gShutdown)
		return;
	KBSMarkerExpiryIdleTask::Stop();

	IDataBase* db = nil;
	{
		Lock lock(gLock);
		db = gHasMark ? gDB : nil;
		gHasMark = kFalse;
		gDB = nil;
		gDocPath.Clear();
		gStory = kInvalidUID;
	}
	if (db != nil)
		KBSHitMarkerRepaint(db);
}

void KBSHitMarker::ForgetDoc(IDataBase* db)
{
	if (db == nil)
		return;
	Lock lock(gLock);
	if (gDB != db)
		return;			// compared, never read
	gHasMark = kFalse;
	gDB = nil;
	gDocPath.Clear();
	gStory = kInvalidUID;
}

void KBSHitMarker::ShutdownCleanup()
{
	gShutdown = kTrue;
	Lock lock(gLock);
	gHasMark = kFalse;
	gDB = nil;
	gDocPath.Clear();	// a static PMString must not outlive the DLL's teardown
	gStory = kInvalidUID;
}

// End, KBSHitMarker.cpp.
