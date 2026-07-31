//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  See KBSGlyphConfirmDialog.h. This file holds the resolution half - reading the two glyphs
//  and their fonts out of the Find/Change settings. The dialog half arrives with the dialog.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IFindChangeOptions.h"
#include "IFontFamily.h"
#include "IGlyphUtils.h"
#include "IPMFont.h"
#include "ITextAttrFont.h"		// the font STYLE name  (kTextAttrFontStyleBoss)
#include "ITextAttrUID.h"		// the font FAMILY uid  (kTextAttrFontUIDBoss)

// General includes:
#include "AttributeBossList.h"
#include "TextAttrID.h"			// kTextAttrFontUIDBoss / kTextAttrFontStyleBoss
#include "Utils.h"

#include <stdio.h>				// snprintf - see the U+ formatting below

// Project includes:
#include "KBSGlyphConfirmDialog.h"

KBSGlyphConfirmDialog::Side	KBSGlyphConfirmDialog::sFind;
KBSGlyphConfirmDialog::Side	KBSGlyphConfirmDialog::sChange;
bool						KBSGlyphConfirmDialog::sAccepted = false;
int32						KBSGlyphConfirmDialog::sCheckedCount = 0;
int32						KBSGlyphConfirmDialog::sChapterCount = 0;

/* ResolveSide
*/
bool KBSGlyphConfirmDialog::ResolveSide(Text::GlyphID glyphID, const AttributeBossList* list,
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
bool KBSGlyphConfirmDialog::Resolve(IFindChangeOptions* opts)
{
	KBSGlyphConfirmDialog::ReleaseSides();

	if (opts == nil)
		return false;

	IDataBase* const db = opts->GetUIDAttrDB();
	const bool okFind = KBSGlyphConfirmDialog::ResolveSide(opts->GetFindGlyphID(),
		opts->GetFindAttributeBossList(db, IFindChangeOptions::kGlyphSearch), db, sFind);
	const bool okChange = KBSGlyphConfirmDialog::ResolveSide(opts->GetReplaceGlyphID(),
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
		KBSGlyphConfirmDialog::ReleaseSides();
		return false;
	}
	return true;
}

/* ReleaseSides
*/
void KBSGlyphConfirmDialog::ReleaseSides()
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
const KBSGlyphConfirmDialog::Side& KBSGlyphConfirmDialog::GetFindSide()
{
	return sFind;
}

/* GetChangeSide
*/
const KBSGlyphConfirmDialog::Side& KBSGlyphConfirmDialog::GetChangeSide()
{
	return sChange;
}

// End, KBSGlyphConfirmDialog.cpp.
