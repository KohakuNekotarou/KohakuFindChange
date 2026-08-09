//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The replace confirmation. On the Glyph tab the query is a GLYPH, not a string, so the
//  prompt has to show the glyph itself - drawn in the font that defines it, because a
//  glyph id means nothing on its own: an alternate form shares its Unicode with the standard
//  form, a GID is specific to one font file, and a CID means a different character under a
//  different ROS. Adobe says as much in the shape of its own API - IGlyphUtils takes the font
//  as a required argument to say what a glyph id means.
//
//  Two halves:
//    - Resolve() reads the font, the glyph id and the Unicode for both sides out of the
//      Find/Change settings and holds them for the life of one prompt;
//    - Ask() puts them on screen and answers whether the user approved the rewrite.
//
//  When the fonts cannot be resolved - a ROS-group query carries no font at all - Resolve()
//  returns false and the caller asks with the Text / GREP layout instead, which needs nothing
//  but its string. A confirmation that cannot be DRAWN must never become a reason the replace
//  cannot RUN.
//
//  Text and GREP asked through CAlert::ModalAlert until 2026-08-02. That alert cannot carry a
//  check box with a label of our own - the SDK offers "Don't show again" and "Apply to All",
//  both fixed wording (CAlert.h:185,209) - and "save after replace" had to go somewhere. That box
//  was removed on 2026-08-05, but the move stays: one prompt for all three tabs, and a Cancel that
//  can be the default button (an alert carrying a box cannot be given one).
//
//========================================================================================

#ifndef __KBSReplaceConfirmDialog_h__
#define __KBSReplaceConfirmDialog_h__

#include "CTextEnum.h"		// Text::GlyphID, kInvalidGlyphID
#include "PMString.h"
#include "widgetid.h"		// WidgetID

class AttributeBossList;
class IDataBase;
class IFindChangeOptions;
class IPMFont;

/** The replace confirmation - see the file header. */
class KBSReplaceConfirmDialog
{
public:
	/** One side of the prompt: what is searched for, or what will be written.

		The font pointer is BORROWED. The module holds the only reference, taken in Resolve()
		and dropped in ReleaseSides(), so nothing copied out of here may outlive the prompt.
	*/
	class Side
	{
	public:
		Side() : fGlyphID(kInvalidGlyphID), fFont(nil) {}

		Text::GlyphID	fGlyphID;	/**< the GID/CID the search and the replace actually run on */
		IPMFont*		fFont;		/**< nil when the font could not be resolved */
		PMString		fFontLabel;	/**< family and style, e.g. "Kozuka Mincho Pr6N  R" */
		PMString		fUnicode;	/**< "U+7425"; empty when the glyph has no Unicode */
	};

	/** Read both sides out of the Find/Change settings and hold them for one prompt.
		@param opts the session's Find/Change settings (read only - KBS never writes to them).
		@return true when BOTH sides resolved to an installed font, which is the caller's
		        signal that the glyph prompt can be shown; false means fall back to the alert.
	*/
	static bool Resolve(IFindChangeOptions* opts);

	/** Drop the fonts taken in Resolve(). Safe when Resolve() failed or never ran. */
	static void ReleaseSides();

	/** Empty every non-POD static this module keeps (the fonts via ReleaseSides, and the last
	    prompt's message text), so nothing is left for a static destructor at DLL unload. Called
	    from KBSStartupShutdown, alongside every other module's ShutdownCleanup. */
	static void ShutdownCleanup();

	/** The side being searched for. Meaningful between Resolve() and ReleaseSides(). */
	static const Side& GetFindSide();

	/** The side that will be written. Meaningful between Resolve() and ReleaseSides(). */
	static const Side& GetChangeSide();

	/** Which side a glyph frame draws, from its own WidgetID. The prompt has exactly two
		frames, so this is all the routing KBSGlyphView needs.
		@param widgetID the frame's id.
		@return the side, or nil when the id is not a glyph frame or nothing is resolved.
	*/
	static const Side* GetSideForWidget(const WidgetID& widgetID);

	/** Put the prompt on screen and wait for the answer.

		TWO LAYOUTS, ONE RESOURCE. Pass an EMPTY message to get the glyph layout - call Resolve()
		first, and check that it succeeded. Pass the assembled prompt to get the Text / GREP one,
		where that string IS the prompt: it arrives finished, and nothing here adds to it.

		There is no way to switch this prompt off. It used to share the plain alert's suppression
		flag, but a confirmation in front of a destructive rewrite is not worth having if a single
		tick can remove it for good.

		(A chapter COUNT was passed here as well until 2026-08-07, for a closing sentence that named
		it. That sentence now states the case instead of counting - see BuildUnsavedLine - so the
		number had no reader left.)

		@param checkedCount how many hits will be rewritten (the opening sentence, glyph layout).
		@param message      the whole prompt, for the Text / GREP layout. Empty = glyph layout.
		@return true when the user approved the rewrite.
	*/
	static bool Ask(int32 checkedCount, const PMString& message);

	/** ***** THE FOUR SENTENCES BOTH LAYOUTS SAY, SPELLED IN ONE PLACE. *****

		The prompt has two layouts and they differ only in where a sentence GOES: the Text / GREP
		one assembles a single wrapped block (KBSActionComponent::ConfirmReplace), the Glyph one
		puts a widget on each line (FillGlyphLayout, in this file's .cpp). Until 2026-08-07 each of
		them also chose its own string-table key, made its own singular/plural decision and ran its
		own ::ReplaceStringParameters - four sentences written twice. Nothing had drifted, but the
		wording of one prompt was two edits away from disagreeing with the other, about a rewrite
		the user is being asked to authorise. What a sentence SAYS belongs here; where it goes
		belongs to the caller.

		Each comes back FINISHED - translated for the UI language and with its number already in -
		and marked untranslatable, so nothing downstream can take it for a key.

		@param checkedCount how many hits will be rewritten (decides singular / plural).
		@return the opening sentence.
	*/
	static PMString BuildCountLine(int32 checkedCount);

	// ***** A BuildEditedSinceLine STOOD HERE FOR ONE AFTERNOON ON 2026-08-08, AND THE QUESTION IT
	// ***** ASKED COULD NOT BE ASKED FROM HERE.
	//
	// It reported whether the text had been edited since the search (KBSEditStamp) as a line on this
	// prompt. The counters live in the document and need it OPEN to be read - and a book search
	// closes every chapter as it finishes with it - so from here it could only ever ask about the
	// chapters that happened to be open when the prompt was drawn. A chapter the user had opened,
	// edited, SAVED and closed was silently passed over, which is the one case the whole feature is
	// about.
	//
	// On the user's decision the question moved to the only point where every chapter can be asked:
	// the moment the replace opens each one (KBSReplaceEngine, AskEditedChapter and the resolve
	// pass). It is a modal alert per chapter now, and Cancel there stops the whole run - which this
	// prompt could not offer either, its own Cancel having already been answered by then.

	/** What the run leaves behind - documents open and unsaved. @see BuildCountLine

		Took a chapter count until 2026-08-07 and chose between a singular and a plural string with
		it. The wording states the case now ("when a replace covers several documents in a book..."),
		so there is no number in it, one string does for every run, and nothing here inflects.
	*/
	static PMString BuildUnsavedLine();

	/** The warning under it, shown whatever the count. @see BuildCountLine */
	static PMString BuildCareLine();

	/** The controller's way back: OK sets it, Ask() reads it. */
	static void SetAccepted(bool accepted);

	/** What the controller was told to show. Read by InitializeDialogFields. */
	static int32 GetCheckedCount();

	/** The Text / GREP prompt, or EMPTY for the glyph layout - which is also how the controller
		tells the two apart. @see GetCheckedCount */
	static const PMString& GetMessage();

private:
	static Side		sFind;
	static Side		sChange;
	static bool		sAccepted;
	static int32	sCheckedCount;
	static PMString	sMessage;

	/** Fill one side from one attribute list.
		@param glyphID the glyph the caller already read off the options.
		@param list the find or change attribute list (may be nil).
		@param db the database those attributes live in.
		@param outSide receives the resolved font and its labels.
		@return true when an installed font was found.
	*/
	static bool ResolveSide(Text::GlyphID glyphID, const AttributeBossList* list,
		IDataBase* db, Side& outSide);
};

#endif // __KBSReplaceConfirmDialog_h__

// End, KBSReplaceConfirmDialog.h.
