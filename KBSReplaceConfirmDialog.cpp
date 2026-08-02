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
#include "ITriStateControlData.h"	// reading the "save after replace" box on OK

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
#include "KBSReplaceConfirmDialog.h"

KBSReplaceConfirmDialog::Side	KBSReplaceConfirmDialog::sFind;
KBSReplaceConfirmDialog::Side	KBSReplaceConfirmDialog::sChange;
bool						KBSReplaceConfirmDialog::sAccepted = false;
int32						KBSReplaceConfirmDialog::sCheckedCount = 0;
int32						KBSReplaceConfirmDialog::sChapterCount = 0;
PMString					KBSReplaceConfirmDialog::sMessage;
bool						KBSReplaceConfirmDialog::sSaveAfterReplace = false;

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

	// The Unicode is a bonus, not a requirement: an ALTERNATE form has none to give - Adobe's
	// own note about it is in SnpInsertGlyph.cpp:291-299 - and writing U+0000 in its place
	// would be a lie. An empty string here means "do not show that line at all".
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

/* SetSaveAfterReplace
*/
void KBSReplaceConfirmDialog::SetSaveAfterReplace(bool save)
{
	sSaveAfterReplace = save;
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

/* GetChapterCount
*/
int32 KBSReplaceConfirmDialog::GetChapterCount()
{
	return sChapterCount;
}

/* GetMessage
*/
const PMString& KBSReplaceConfirmDialog::GetMessage()
{
	return sMessage;
}

/* Ask
*/
bool KBSReplaceConfirmDialog::Ask(int32 checkedCount, int32 chapterCount, const PMString& message,
	bool& outSaveAfterReplace)
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
	sChapterCount = chapterCount;
	// Already assembled AND translated by the caller: what arrives here is finished text, and a
	// key only translates while it is the WHOLE string anyway.
	sMessage = message;
	sMessage.SetTranslatable(kFalse);
	sAccepted = false;
	// OFF every time. Nothing writes this to the widget on open and nothing remembers it between
	// runs: the box overwrites the user's files, so it is asked per run rather than configured
	// once (user's call, 2026-08-02).
	sSaveAfterReplace = false;

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

	// Both answers come back the same way: only ApplyDialogFields - which Cancel never reaches -
	// writes either of them.
	outSaveAfterReplace = sSaveAfterReplace;
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
	InterfacePtr<IPanelControlData> panelData(this, UseDefaultIID());
	if (panelData == nil)
		return;
	IControlView* view = panelData->FindWidget(widgetID);
	if (view == nil)
		return;

	if (text.IsEmpty())
	{
		// Hidden AND disabled: a widget that is only hidden still takes clicks (the lesson from the
		// replaced rows' check boxes). Nothing here is clickable, but the pair is the house rule.
		view->ShowView(kFalse);
		view->Disable();
		return;
	}
	this->SetTextControlData(widgetID, text);
	view->ShowView(kTrue);
	view->Enable();
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

	// Hidden AND disabled, always as a pair - see SetOptionalLine.
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

	this->ShowOrHide(kKBSReplaceConfirmMessageWidgetID, textLayout);
	this->ShowOrHide(kKBSReplaceConfirmGlyphBlockWidgetID, !textLayout);
	this->ShowOrHide(kKBSReplaceConfirmCountWidgetID, !textLayout);
	this->ShowOrHide(kKBSReplaceConfirmUnsavedWidgetID, !textLayout);

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
	// The opening sentence, from the same string-table entries the message above is built from - the
	// same question, laid out differently. Each key is translated BEFORE the count goes in: a key only
	// translates while it is the WHOLE string, and the count is real data, so it is marked
	// untranslatable first.
	PMString countStr;
	countStr.AppendNumber(KBSReplaceConfirmDialog::GetCheckedCount());
	countStr.SetTranslatable(kFalse);
	PMString countLine(KBSReplaceConfirmDialog::GetCheckedCount() == 1
		? kKBSConfirmReplaceOneKey : kKBSConfirmReplaceManyKey);
	countLine.Translate();
	::ReplaceStringParameters(&countLine, countStr);
	countLine.SetTranslatable(kFalse);
	this->SetTextControlData(kKBSReplaceConfirmCountWidgetID, countLine);

	// The closing sentence, the same way.
	PMString chapterStr;
	chapterStr.AppendNumber(KBSReplaceConfirmDialog::GetChapterCount());
	chapterStr.SetTranslatable(kFalse);
	PMString unsaved(KBSReplaceConfirmDialog::GetChapterCount() <= 1
		? kKBSConfirmUnsavedOneKey : kKBSConfirmUnsavedManyKey);
	unsaved.Translate();
	::ReplaceStringParameters(&unsaved, chapterStr);
	unsaved.SetTranslatable(kFalse);
	this->SetTextControlData(kKBSReplaceConfirmUnsavedWidgetID, unsaved);

	// The four lines under the two frames. All optional, and all for the same reason: an empty
	// Change To box has no font to name and no Unicode to give, and an ALTERNATE form has no
	// Unicode either (SnpInsertGlyph.cpp:291-299 records Adobe's own note about that). An empty
	// line under one frame but not the other reads as a fault, so the line is hidden rather than
	// blanked. Already marked untranslatable where they were built - they are data.
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
	// answer defaults to "no" and only OK turns it into "yes". The check box is read on the same
	// terms: a run that was not approved cannot have asked for a save either.
	KBSReplaceConfirmDialog::SetAccepted(true);
	KBSReplaceConfirmDialog::SetSaveAfterReplace(
		this->GetTriStateControlData(kKBSReplaceConfirmSaveWidgetID) == ITriStateControlData::kSelected);
}

// End, KBSReplaceConfirmDialog.cpp.
