//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSReplaceConfirmDialog.h. This file holds the resolution half - reading the two glyphs
//  and their fonts out of the Find/Change settings. The dialog half arrives with the dialog.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IApplication.h"		// the dialog manager hangs off the application
#include "IControlView.h"		// showing / hiding the two optional lines
#include "IDialog.h"
#include "IDialogMgr.h"
#include "IEVEUtils.h"			// ApplyEveLayout - re-flow once the strings are in
#include "IFindChangeOptions.h"
#include "IFontFamily.h"
#include "IGlyphUtils.h"
#include "IPanelControlData.h"	// FindWidget - the check box and the optional lines
#include "IPMFont.h"
#include "ISession.h"			// GetExecutionContextSession
#include "ITextAttrFont.h"		// the font STYLE name  (kTextAttrFontStyleBoss)
#include "ITextAttrUID.h"		// the font FAMILY uid  (kTextAttrFontUIDBoss)

// General includes:
#include "AttributeBossList.h"
#include "CDialogController.h"
#include "CoreResTypes.h"		// kViewRsrcType - the dialog's RsrcSpec
#include "LocaleSetting.h"		// the dialog's RsrcSpec locale
#include "RsrcSpec.h"
#include "StringUtils.h"		// ::ReplaceStringParameters - fills the ^1 in a translated string
#include "TextAttrID.h"			// kTextAttrFontUIDBoss / kTextAttrFontStyleBoss
#include "Utils.h"

#include <stdio.h>				// snprintf - see the U+ formatting below

// Project includes:
#include "KBSID.h"
#include "KBSLoc.h"			// runtime Japanese - the jaJP string table is gone (2026-08-05)
#include "KBSReplaceConfirmDialog.h"
#include "KBSResultModel.h"		// which chapters are ticked, and what they are called
#include "KBSEditStamp.h"		// ...and whether any of them has been edited since the search
#include "IDocument.h"			// the chapter, found by file rather than by a stale UIDRef
#include "IDocumentList.h"		// FindDoc(IDFile) - is this chapter open RIGHT NOW?

KBSReplaceConfirmDialog::Side	KBSReplaceConfirmDialog::sFind;
KBSReplaceConfirmDialog::Side	KBSReplaceConfirmDialog::sChange;
bool						KBSReplaceConfirmDialog::sAccepted = false;
int32						KBSReplaceConfirmDialog::sCheckedCount = 0;
PMString					KBSReplaceConfirmDialog::sMessage;

/* ResolveSide
*/
bool KBSReplaceConfirmDialog::ResolveSide(Text::GlyphID glyphID, const AttributeBossList* list,
	IDataBase* db, Side& outSide)
{
	outSide = Side();
	outSide.fGlyphID = glyphID;

	// No glyph on this side. On the change side that is a legitimate request - an empty Change To
	// box deletes every match - and there is simply nothing to draw or to look up: "what character
	// is glyph -1" is not a question with an answer, and asking the font anyway is how a crash gets
	// written. The caller decides what an absent side means.
	if (glyphID == kInvalidGlyphID)
		return false;

	if (list == nil || db == nil)
		return false;

	// The font is two attributes, not one: the FAMILY is a UID into the attribute database and
	// the STYLE is a name. QueryByClassID hands back a const pointer, so the query is taken
	// through a const InterfacePtr - the shape typekitinspector uses on this same attribute
	// (TKIPanelWidgetObserver.cpp:452).
	InterfacePtr<const ITextAttrUID> familyAttr(static_cast<const ITextAttrUID*>(
		list->QueryByClassID(kTextAttrFontUIDBoss, ITextAttrUID::kDefaultIID)));
	if (familyAttr == nil)
		return false;			// a ROS-group query carries no font - the caller falls back

	const UID familyUID = familyAttr->Get();
	if (familyUID == kInvalidUID)
		return false;

	PMString styleName;
	InterfacePtr<const ITextAttrFont> styleAttr(static_cast<const ITextAttrFont*>(
		list->QueryByClassID(kTextAttrFontStyleBoss, ITextAttrFont::kDefaultIID)));
	if (styleAttr != nil)
		styleName = styleAttr->GetFontName();

	InterfacePtr<IFontFamily> family(db, familyUID, UseDefaultIID());
	if (family == nil)
		return false;

	// IFontFamily.h:201 asks for BOTH checks: a face can come back non-nil and still not be
	// installed, and drawing with one of those would put nothing on screen. (That comment names
	// the method FontStatus(); the method is GetFontStatus() and FontStatus is the enum - the
	// header's own declaration is what this follows.)
	IPMFont* font = family->QueryFace(styleName);
	if (font == nil)
		return false;
	if (font->GetFontStatus() != IPMFont::kFontInstalled)
	{
		font->Release();
		return false;
	}
	outSide.fFont = font;		// kept; ReleaseSides() drops it

	outSide.fFontLabel = family->GetFamilyName();
	if (!styleName.empty())
	{
		outSide.fFontLabel.Append("  ");
		outSide.fFontLabel.Append(styleName);
	}
	outSide.fFontLabel.SetTranslatable(kFalse);

	// The Unicode is a bonus, not a requirement: the header says plainly that this call "May
	// return 0" (IGlyphUtils.h:281), and writing U+0000 in its place would be a lie. An empty
	// string here means "do not show that line at all".
	//
	// ***** AND THIS IS THE UPPER OF THE TWO CALLS, WHICH IS WHY AN ALTERNATE FORM DOES ANSWER.
	// ***** GlyphToCharacter (:275) is the lower one, and Adobe's own note against IT is that an
	// alternate form of a Unicode character does not come back (SnpInsertGlyph.cpp:291-299).
	// GetUnicodeForGlyphID is built on that call and goes further - "Uses GlyphToCharacter to get
	// its work done. Will also get a unicode value for glyphs in OpenType features"
	// (IGlyphUtils.h:277-278) - which is the whole reason it is the one asked here.
	// (Corrected 2026-08-08: the note here used to cite that BUG? comment as though it described
	// THIS call, so it explained the empty line by the very case this API is chosen to answer.)
	//
	// Formatted with snprintf because PMString::AppendNumber only writes decimal, and the
	// number has to read the way the Find/Change dialog's own ID field reads it.
	const UTF32TextChar ch = Utils<IGlyphUtils>()->GetUnicodeForGlyphID(font, glyphID);
	if (ch.GetValue() != 0)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned int>(ch.GetValue()));
		outSide.fUnicode = buf;
		outSide.fUnicode.SetTranslatable(kFalse);
	}

	return true;
}

/* Resolve
*/
bool KBSReplaceConfirmDialog::Resolve(IFindChangeOptions* opts)
{
	KBSReplaceConfirmDialog::ReleaseSides();

	if (opts == nil)
		return false;

	IDataBase* const db = opts->GetUIDAttrDB();
	const bool okFind = KBSReplaceConfirmDialog::ResolveSide(opts->GetFindGlyphID(),
		opts->GetFindAttributeBossList(db, IFindChangeOptions::kGlyphSearch), db, sFind);
	const bool okChange = KBSReplaceConfirmDialog::ResolveSide(opts->GetReplaceGlyphID(),
		opts->GetChangeAttributeBossList(db, IFindChangeOptions::kGlyphSearch), db, sChange);

	// The FIND side must resolve - without it there is nothing to show at all.
	//
	// The change side is allowed to be absent, but only when the Change To box is genuinely empty:
	// that is the "delete every match" request, and a deletion has no glyph to draw. A change side
	// that IS set but would not resolve is a different thing - drawing one side and describing the
	// other in numbers is worse than the plain alert, which at least treats the two the same way -
	// so that case still falls back.
	const bool changeIsEmpty = (opts->GetReplaceGlyphID() == kInvalidGlyphID);
	if (!okFind || (!okChange && !changeIsEmpty))
	{
		KBSReplaceConfirmDialog::ReleaseSides();
		return false;
	}
	return true;
}

/* ReleaseSides
*/
void KBSReplaceConfirmDialog::ReleaseSides()
{
	if (sFind.fFont != nil)
		sFind.fFont->Release();
	if (sChange.fFont != nil)
		sChange.fFont->Release();
	sFind = Side();
	sChange = Side();
}

/* GetFindSide
*/
const KBSReplaceConfirmDialog::Side& KBSReplaceConfirmDialog::GetFindSide()
{
	return sFind;
}

/* GetChangeSide
*/
const KBSReplaceConfirmDialog::Side& KBSReplaceConfirmDialog::GetChangeSide()
{
	return sChange;
}

/* GetSideForWidget
*/
const KBSReplaceConfirmDialog::Side* KBSReplaceConfirmDialog::GetSideForWidget(const WidgetID& widgetID)
{
	// Two frames, told apart by their id. An absent font means the frame draws empty, which is the
	// right picture for an empty Change To box - the deletion has no glyph to show.
	if (widgetID == kKBSGlyphConfirmFindGlyphWidgetID)
		return sFind.fFont != nil ? &sFind : nil;
	if (widgetID == kKBSGlyphConfirmChangeGlyphWidgetID)
		return sChange.fFont != nil ? &sChange : nil;
	return nil;
}

/* BuildCountLine
*/
PMString KBSReplaceConfirmDialog::BuildCountLine(int32 checkedCount)
{
	// The key is translated BEFORE the count goes in - a key only translates while it is the WHOLE
	// string - and the count itself is data, so it is marked untranslatable first: a number that
	// happened to match a table entry would otherwise come back as somebody else's translation.
	PMString countStr;
	countStr.AppendNumber(checkedCount);
	countStr.SetTranslatable(kFalse);

	PMString line(checkedCount == 1
		? KBSLoc::Text(kKBSConfirmReplaceOneKey, KBSJa::kConfirmReplaceOne)
		: KBSLoc::Text(kKBSConfirmReplaceManyKey, KBSJa::kConfirmReplaceMany));
	::ReplaceStringParameters(&line, countStr);
	line.SetTranslatable(kFalse);
	return line;
}

/* BuildEditedSinceLine
*/
PMString KBSReplaceConfirmDialog::BuildEditedSinceLine()
{
	// Only the chapters this run is about to WRITE to. A chapter with nothing ticked is not at
	// risk from an edit, and a warning that fires for one would teach the user to read past this
	// line - which is the one line here that can save their text.
	int32 editedCount = 0;
	PMString firstName;
	const int32 chapterCount = KBSResultModel::GetChapterCount();
	for (int32 c = 0; c < chapterCount; ++c)
	{
		if (KBSResultModel::GetChapterCheckedCount(c) <= 0)
			continue;

		UIDRef docRef;
		IDFile file;
		if (!KBSResultModel::GetChapterLocation(c, docRef, file))
			continue;

		// ***** THE CHAPTER IS FOUND BY ITS FILE, NOT BY THE UIDRef THE SEARCH RECORDED. *****
		// A book search closes each chapter as it finishes with it, so by the time this runs that
		// UIDRef's database is very likely gone - and reading it would be undefined behaviour, not
		// a nil check (KBSBookScope::IsDocStillOpen exists to say so without dereferencing).
		//
		// Testing it with IsDocStillOpen is not enough either: when the user REOPENS a chapter to
		// edit it - the one case this whole feature is about - the reopened document has a new
		// UIDRef, and the recorded one still reads as closed. Measured 2026-08-08: the warning
		// never appeared on the book path until this asked by file instead.
		//
		// The stamp itself does not care: it holds story UIDs and counters, which are the same
		// values in the reopened document (measured 2026-08-08).
		InterfacePtr<IDocumentList> docList(GetExecutionContextSession()->QueryDocumentList());
		if (docList == nil)
			continue;

		// "Search to see if one (whatFile) is already open. If so, return it" - IDocumentList.h:64-69.
		// nil means the chapter is not open now, and a chapter nobody can see cannot have been
		// edited, so it is passed over rather than reported.
		IDocument* liveDoc = docList->FindDoc(file);
		if (liveDoc == nil)
			continue;

		if (KBSEditStamp::IsChapterCurrent(c, ::GetUIDRef(liveDoc)))
			continue;

		// The name is only needed when exactly one chapter turns out to be edited, but it has to
		// be taken here, while its index is in hand.
		if (editedCount == 0)
		{
			int32 hitCount = 0;
			KBSResultModel::GetChapterDisplay(c, firstName, hitCount);
			firstName.SetTranslatable(kFalse);
		}
		++editedCount;
	}

	// ***** Nothing to say, so say NOTHING. ***** Empty means the line is hidden rather than drawn
	// blank - SetOptionalLine does that for the glyph layout, and the Text / GREP caller leaves out
	// its separators. A standing disclaimer stood here until 2026-08-08 and was shown on every
	// prompt, because nothing knew whether the text had been edited. KBSEditStamp knows.
	if (editedCount == 0)
		return PMString();

	// The opening names WHAT was edited; the ending is the same in all three cases. Each key is
	// translated before anything is put into it, and what goes in is marked untranslatable first -
	// the reasoning BuildCountLine spells out above applies unchanged.
	PMString line;
	if (!KBSResultModel::IsFromBook())
	{
		line = KBSLoc::Text(kKBSConfirmEditedDocKey, KBSJa::kConfirmEditedDoc);
	}
	else if (editedCount == 1)
	{
		line = KBSLoc::Text(kKBSConfirmEditedOneKey, KBSJa::kConfirmEditedOne);
		::ReplaceStringParameters(&line, firstName);
	}
	else
	{
		PMString countStr;
		countStr.AppendNumber(editedCount);
		countStr.SetTranslatable(kFalse);
		line = KBSLoc::Text(kKBSConfirmEditedManyKey, KBSJa::kConfirmEditedMany);
		::ReplaceStringParameters(&line, countStr);
	}

	// A break rather than a space: English wants one between the two sentences and Japanese does
	// not, and a line end is right in both.
	line.Append(kLineSeparatorString);
	line.Append(KBSLoc::Text(kKBSConfirmEditedTailKey, KBSJa::kConfirmEditedTail));
	line.SetTranslatable(kFalse);
	return line;
}

/* BuildUnsavedLine
*/
PMString KBSReplaceConfirmDialog::BuildUnsavedLine()
{
	// No count and no parameter: the sentence states what a book-wide replace leaves behind rather
	// than numbering the chapters, so one string serves every run (2026-08-07, user's wording).
	PMString line(KBSLoc::Text(kKBSConfirmUnsavedKey, KBSJa::kConfirmUnsaved));
	line.SetTranslatable(kFalse);
	return line;
}

/* BuildCareLine
*/
PMString KBSReplaceConfirmDialog::BuildCareLine()
{
	PMString line(KBSLoc::Text(kKBSConfirmCareKey, KBSJa::kConfirmCare));
	line.SetTranslatable(kFalse);
	return line;
}

/* SetAccepted
*/
void KBSReplaceConfirmDialog::SetAccepted(bool accepted)
{
	sAccepted = accepted;
}

/* GetCheckedCount
*/
int32 KBSReplaceConfirmDialog::GetCheckedCount()
{
	return sCheckedCount;
}

/* GetMessage
*/
const PMString& KBSReplaceConfirmDialog::GetMessage()
{
	return sMessage;
}

/* Ask
*/
bool KBSReplaceConfirmDialog::Ask(int32 checkedCount, const PMString& message)
{
	// The glyph layout needs a resolved find side to draw - an empty CHANGE side is the deletion
	// request and draws as an empty frame, but with nothing on the find side there is no prompt.
	// The Text / GREP layout needs nothing but its string, which is also what the caller falls
	// back to when the fonts could not be resolved.
	if (message.IsEmpty() && sFind.fFont == nil)
		return false;

	// There is no way to switch this prompt off any more (2026-08-01). It used to share the plain
	// alert's suppression flag, but a confirmation in front of a destructive rewrite is not worth
	// having if a single tick can remove it for good.
	sCheckedCount = checkedCount;
	// Already assembled AND translated by the caller: what arrives here is finished text, and a
	// key only translates while it is the WHOLE string anyway.
	sMessage = message;
	sMessage.SetTranslatable(kFalse);
	sAccepted = false;

	InterfacePtr<IApplication> application(GetExecutionContextSession()->QueryApplication());
	if (application == nil)
		return false;
	InterfacePtr<IDialogMgr> dialogMgr(application, UseDefaultIID());
	if (dialogMgr == nil)
		return false;

	// Built fresh every time (kDontCacheDialog): the glyphs, the fonts and the counts differ on
	// every run, so a cached dialog would have to be rewritten from scratch anyway. Not resizable -
	// two glyph frames and a button row have nothing to gain from it.
	RsrcSpec dialogSpec(LocaleSetting::GetLocale(), kKBSPluginID, kViewRsrcType,
		kSDKDefDialogResourceID, kTrue /*initially visible*/);
	IDialog* dialog = dialogMgr->CreateNewDialog(dialogSpec, IDialog::kMovableModal,
		IDialogMgr::kAllowMultipleCopies, IDialogMgr::kDontCacheDialog,
		IDialogMgr::kDontAllowUserResize);
	if (dialog == nil)
		return false;

	// Open() waits for the dialog by default, so control returns here only once it is dismissed -
	// by which time the controller has recorded the answer.
	dialog->Open();

	// Only ApplyDialogFields writes the answer, and Cancel never reaches it - so a dismissed prompt
	// leaves it at the false it was set to above.
	return sAccepted;
}

//----------------------------------------------------------------------------------------
// KBSReplaceConfirmDialogController - the dialog's own behaviour
//----------------------------------------------------------------------------------------

/** Implements IDialogController for the Glyph tab's replace confirmation: fills the fields on
	open, and records the answer on OK. There is nothing to validate - the dialog has no input.
*/
class KBSReplaceConfirmDialogController : public CDialogController
{
	typedef CDialogController inherited;
public:
	/** Constructor.
		@param boss interface ptr from boss object on which this interface is aggregated.
	*/
	KBSReplaceConfirmDialogController(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KBSReplaceConfirmDialogController() {}

	/** Write the counts, the font names and the Unicode values into the dialog.
		@param dlgContext active context.
	*/
	virtual void InitializeDialogFields(IActiveContext* dlgContext);

	/** Record that the rewrite was approved (OK only - Cancel never reaches here).
		@param myContext active context.
		@param widgetId identifies the widget on which to act.
	*/
	virtual void ApplyDialogFields(IActiveContext* myContext, const WidgetID& widgetId);

private:
	/** Show a line when there is something to put on it, hide it when there is not.
		@param widgetID the line to fill or hide.
		@param text what it should say; empty means hide.
	*/
	void SetOptionalLine(const WidgetID& widgetID, const PMString& text);

	/** Show or hide one of the two layouts' widgets, disabling whatever it hides.
		@param widgetID the widget to show or hide.
		@param show true to show it.
	*/
	void ShowOrHide(const WidgetID& widgetID, bool show);

	/** Fill the glyph layout: the two counts and the four optional lines under the frames. Only
		called when there is no message to show instead. */
	void FillGlyphLayout();
};

CREATE_PMINTERFACE(KBSReplaceConfirmDialogController, kKBSReplaceConfirmDialogControllerImpl)

/* SetOptionalLine
*/
void KBSReplaceConfirmDialogController::SetOptionalLine(const WidgetID& widgetID, const PMString& text)
{
	// Empty means "do not show this line at all": an ALTERNATE form has no Unicode to give and an
	// empty Change To box has no font to name, and a blank line under one frame but not the other
	// reads as a fault. Whether a widget is shown is ShowOrHide's question and is asked there -
	// this used to carry a second copy of the same show/hide/enable/disable steps.
	if (text.IsEmpty())
	{
		this->ShowOrHide(widgetID, false);
		return;
	}
	// SetTextControlData looks the widget up itself and does nothing when there is none, so a line
	// that is not in this resource costs a lookup and no more.
	this->SetTextControlData(widgetID, text);
	this->ShowOrHide(widgetID, true);
}

/* ShowOrHide
*/
void KBSReplaceConfirmDialogController::ShowOrHide(const WidgetID& widgetID, bool show)
{
	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	if (panelData == nil)
		return;
	IControlView* view = panelData->FindWidget(widgetID);
	if (view == nil)
		return;

	// ***** HIDDEN AND DISABLED, ALWAYS AS A PAIR. ***** A widget that is only hidden still takes
	// clicks (the lesson from the replaced rows' check boxes - see gotourl-and-stacked-widget-traps).
	// Nothing on this prompt is clickable, but the pair is the house rule and this is the one place
	// that applies it, for the layout that is not showing and for the optional lines alike.
	view->ShowView(show ? kTrue : kFalse);
	if (show)
		view->Enable();
	else
		view->Disable();
}

/* InitializeDialogFields
*/
void KBSReplaceConfirmDialogController::InitializeDialogFields(IActiveContext* /*dlgContext*/)
{
	// ONE resource, two layouts. Which one is showing is decided here and nowhere else: a message
	// means Text or GREP (the whole prompt in one wrapped block), no message means Glyph (the two
	// frames). Whichever is not showing is hidden AND disabled - see ShowOrHide.
	const PMString& message = KBSReplaceConfirmDialog::GetMessage();
	const bool textLayout = !message.IsEmpty();

	// (A "Don't show again" label was stamped here until 2026-08-06. The box itself came off both
	// layouts on 2026-08-01, so kKBSReplaceConfirmDontShowWidgetID names nothing in KBS.fr any
	// more and the call found no widget to write to - CDialogController::SetTextControlData
	// checks FindWidget first, so it did nothing at all. Its comment claimed the opposite
	// ("present in both layouts"), which is the only reason it survived a reading. The id and the
	// string key stay reserved in KBSID.h so the box can come back as a resource change.)

	this->ShowOrHide(kKBSReplaceConfirmMessageWidgetID, textLayout);
	this->ShowOrHide(kKBSReplaceConfirmGlyphBlockWidgetID, !textLayout);
	this->ShowOrHide(kKBSReplaceConfirmCountWidgetID, !textLayout);
	this->ShowOrHide(kKBSReplaceConfirmUnsavedWidgetID, !textLayout);
	this->ShowOrHide(kKBSReplaceConfirmCareWidgetID, !textLayout);
	this->ShowOrHide(kKBSReplaceConfirmEditedWidgetID, !textLayout);

	if (textLayout)
	{
		// The whole prompt, assembled by the caller. Nothing to parameterise or translate here: it
		// arrived finished, which is what keeps the wording identical to what the plain alert drew.
		this->SetTextControlData(kKBSReplaceConfirmMessageWidgetID, message);
	}
	else
		this->FillGlyphLayout();

	// The strings just written are parameterized and translated, so they can be longer than the
	// resource allowed for, and widgets have just been hidden. Re-flow once, at the end - the way
	// linksui's own dialogs do it.
	InterfacePtr<IControlView> dialogView(this, UseDefaultIID());
	if (dialogView != nil)
		Utils<IEVEUtils>()->ApplyEveLayout(dialogView);
}

/* FillGlyphLayout
*/
void KBSReplaceConfirmDialogController::FillGlyphLayout()
{
	// The three fixed labels first - "Find", the arrow, "Change to". The .fr resource writes
	// their enUS strings; this restamps them through the language switch, the same way every
	// other Japanese string is produced since the jaJP table went (2026-08-05).
	this->SetTextControlData(kKBSGlyphConfirmFindLabelWidgetID,
		KBSLoc::Text(kKBSGlyphConfirmFindLabelKey, KBSJa::kGlyphFindLabel));
	this->SetTextControlData(kKBSGlyphConfirmArrowWidgetID,
		KBSLoc::Text(kKBSGlyphConfirmArrowKey, KBSJa::kGlyphArrow));
	this->SetTextControlData(kKBSGlyphConfirmChangeLabelWidgetID,
		KBSLoc::Text(kKBSGlyphConfirmChangeLabelKey, KBSJa::kGlyphChangeLabel));

	// ***** THE FOUR SENTENCES THE OTHER LAYOUT SAYS TOO, FROM THE ONE PLACE THAT SPELLS THEM. *****
	// All this layout decides is which widget each one lands on; the keys, the singular/plural and
	// the parameter go through Build*Line (see the header). Until 2026-08-07 they were written out
	// here as well as in KBSActionComponent::ConfirmReplace - the same four sentences, twice.
	this->SetTextControlData(kKBSReplaceConfirmCountWidgetID,
		KBSReplaceConfirmDialog::BuildCountLine(KBSReplaceConfirmDialog::GetCheckedCount()));

	// Whether the text was actually edited since the search - between the glyphs and the closing
	// lines, the same place the Text / GREP layout puts it. SetOptionalLine, not
	// SetTextControlData: the line is empty when nothing was edited, and an empty line drawn on
	// the prompt reads as a fault (2026-08-08, when this stopped being a standing disclaimer).
	this->SetOptionalLine(kKBSReplaceConfirmEditedWidgetID,
		KBSReplaceConfirmDialog::BuildEditedSinceLine());

	// The closing sentence.
	this->SetTextControlData(kKBSReplaceConfirmUnsavedWidgetID,
		KBSReplaceConfirmDialog::BuildUnsavedLine());

	// ...and the warning under it, WHATEVER the count (user, 2026-08-05).
	this->SetTextControlData(kKBSReplaceConfirmCareWidgetID,
		KBSReplaceConfirmDialog::BuildCareLine());

	// The four lines under the two frames. All optional: an empty Change To box has no font to name
	// and no Unicode to give, and GetUnicodeForGlyphID "May return 0" for a glyph of any kind
	// (IGlyphUtils.h:281). ***** NOT the alternate-form case this used to name ***** - that is
	// Adobe's note against the LOWER call, GlyphToCharacter, and answering for alternate forms is
	// exactly what the call ResolveSide makes is chosen for (see it). An empty line under one frame
	// but not the other reads as a fault, so the line is hidden rather than blanked. Already marked
	// untranslatable where they were built - they are data.
	const KBSReplaceConfirmDialog::Side& findSide = KBSReplaceConfirmDialog::GetFindSide();
	const KBSReplaceConfirmDialog::Side& changeSide = KBSReplaceConfirmDialog::GetChangeSide();
	this->SetOptionalLine(kKBSGlyphConfirmFindFontWidgetID, findSide.fFontLabel);
	this->SetOptionalLine(kKBSGlyphConfirmFindUnicodeWidgetID, findSide.fUnicode);
	this->SetOptionalLine(kKBSGlyphConfirmChangeFontWidgetID, changeSide.fFontLabel);
	this->SetOptionalLine(kKBSGlyphConfirmChangeUnicodeWidgetID, changeSide.fUnicode);
}

/* ApplyDialogFields
*/
void KBSReplaceConfirmDialogController::ApplyDialogFields(IActiveContext* /*myContext*/,
	const WidgetID& /*widgetId*/)
{
	// Only reached on OK - Cancel never gets here, which is exactly the behaviour wanted: the
	// answer defaults to "no" and only OK turns it into "yes".
	KBSReplaceConfirmDialog::SetAccepted(true);
}

// End, KBSReplaceConfirmDialog.cpp.
