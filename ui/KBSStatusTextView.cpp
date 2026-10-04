//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuBookSearch (KBS)
//
//  The panel's MESSAGE AREA, drawn by hand so that it can show more than one colour: when a replaced
//  row is selected, this box shows the text as it was BEFORE the replace, with the characters the
//  replace took at the theme's text colour and the words around them faded - the treatment a hit row
//  already has (the author's request: "show it in the panel's top part, the way KCM does").
//
//  BROUGHT OVER FROM KCM's KCMStatusTextView.cpp, which replaced the same stock widget for the same
//  reason. What came across: the wrapping, the line count taken from the box's
//  height and the font, the rule that the CONTEXT gives way before the change does, the heading on a
//  line of its own, and the bar for a change with no characters. What did not: the reading over the
//  characters (ruby), the emphasis marks (kenten), the warichu / tate-chu-yoko layers and the warning
//  colour - KCM shows attribute changes, and KBS replaces text.
//
//  WHAT A STOCK MULTI-LINE STATIC TEXT GIVES, AND WHAT IT COSTS. It holds one string,
//  wraps it, and draws the lot in ONE colour. The wrapping is the part worth keeping - the panel's
//  messages are sentences that fill the box, and several name files - so it is written out below.
//  The one colour is the part that had to go.
//
//  THE STOCK WRAPPING IS PUBLIC, AND IS NOT USED HERE. A
//  multi-line static text breaks its string through IStaticTextLineBreaker (public/interfaces/ui -
//  CreateLineBreaks / GetNthLine / GetNumLines; the stock implementations are kStaticTextLineBreakerImpl
//  and kDV_StaticTextLineBreakerImpl), and where the app wants its own rules it writes another one on
//  that interface (the Preflight panel's info box: kPreflightUIInfoBoxLineBreakerImpl, beside an
//  IID_IPREFLIGHTUIINFOBOXLINEBREAKDATA - no source in the SDK). It is not used here because what
//  this box needs is more than where the lines fall: (1) the colours change INSIDE a line, so every
//  line has to map back to exact character offsets in the pieces, and GetNthLine hands back a string -
//  the header does not say whether a break drops the space it fell on; (2) the bar's room is that same
//  one space; (3) CreateLineBreaks takes no ampersand flag, while this box measures and draws with '&'
//  kept (KBSPanelTextDraw.h - a measure that disagrees with the draw is the drift that file exists to
//  stop); (4) the overflow rule breaks the text again for every amount of context it tries, so the
//  breaker could replace only the inner step of that search. (1)-(3) are what the header leaves open,
//  not anything measured. KCM's box, which this one came from, carries the same hand-written wrap.
//
//  HOW MANY LINES: as many as the box holds, worked out at draw time. KBSPanelMetrics makes the box
//  four of this font's lines tall (MessageBlockHeight - 72px on a Japanese UI); a hand-drawn box has
//  no line count of its own to disagree with that, so the height and the font are the whole answer -
//  and both come from the one place, KBSPanelMetrics' MessageFont / MessageLineMetrics.
//  ! The stock widget drew in kPaletteWindowFontId; this draws in the SYSTEM SCRIPT variant, the one
//    the hit rows use, because it shows the document's own text as well as the panel's sentences.
//    On a Japanese UI the two answer the same 18px line (KCM measured ascent 12.7 + descent 5.3 for
//    this font). A Roman UI has not been measured.
//
//  WHEN IT DOES NOT FIT, THE CONTEXT GIVES WAY. The replaced text itself is cut only when it
//  alone overflows the box, and then an ellipsis says so.
//  ! An ordinary message has no context, so a long one ends in an ellipsis. That is a CHANGE from the
//    stock widget, which cut silently at the last line it had room for. Truncation that shows is
//    better than truncation that does not - but it is not a licence for long messages: a number cut in
//    half still reads as a different number (memory ellipsis-in-status-line-breaks-numbers), and the
//    messages stay cut to fit four lines (KBSPanelMetrics.cpp).
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IGraphicsPort.h"
#include "IInterfaceColors.h"	// RealAGMColor, InterfaceColor indices
#include "ITextControlData.h"	// the message as plain text, for a reader that walks the widgets

// General includes:
#include "AGMGraphicsContext.h"
#include "AutoGSave.h"
#include "CPMUnknown.h"
#include "DVControlView.h"
#include "DrawStringUtils.h"	// StringUtils::PMDrawStringRGB / PMMeasureString
#include "ISession.h"			// GetExecutionContextSession
#include "IWidgetUtils.h"		// GetViewYPosition - only for the fallback when the metrics are refused
#include "TextChar.h"			// kTextChar_Ellipse - the mark a cut piece carries
#include "Utils.h"				// Utils<IWidgetUtils>()

// Project includes:
#include "IKBSStatusTextData.h"
#include "KFCUIID.h"
#include "KBSPanelTextDraw.h"	// the context's fade, the '&' flags and the bar - shared with the hit rows
#include "KBSPanelMetrics.h"	// the font and its line - the same answer the box's height is made of

// Std includes:
#include <vector>

namespace
{

/* One piece of the message as it arrives: a string, and whether it is drawn faded.
   Only two colours exist in this box, and the faded one means one thing only: CONTEXT - words carried
   along so the reader can place the change. Everything the message itself says (the heading, and the
   characters that matter) is drawn at the theme's text colour. */
struct KBSRun
{
	PMString	fText;
	bool16		fFaded;
	bool16		fIsCaret;	// a PLACE, drawn as a bar: its text is the one space that reserves the room

	KBSRun() : fFaded(kTrue), fIsCaret(kFalse) {}
	KBSRun(const PMString& text, bool16 faded, bool16 isCaret = kFalse)
		: fText(text), fFaded(faded), fIsCaret(isCaret) {}
};

/* One piece of the message as it will be drawn: a run, or the part of a run that fell on one line.
   A run that crosses a line boundary becomes two fragments of the SAME colour - which is why the colour
   travels on the fragment rather than on the line. */
struct KBSFrag
{
	PMString	fText;
	bool16		fFaded;
	bool16		fIsCaret;
	int32		fLine;
	PMReal		fX;

	KBSFrag() : fFaded(kTrue), fIsCaret(kFalse), fLine(0), fX(0.0) {}
	KBSFrag(const PMString& text, bool16 faded, bool16 isCaret, int32 line, const PMReal& x)
		: fText(text), fFaded(faded), fIsCaret(isCaret), fLine(line), fX(x) {}
};

/* The first n characters, and everything from the nth on. Written with Truncate / Remove rather than
   PMString::Substring, which returns a string the caller has to delete (PMString.h). */
PMString KBSHead(const PMString& s, int32 n)
{
	PMString out(s);
	const int32 total = out.CharCount();
	if (n < 0)
		n = 0;
	if (n < total)
		out.Truncate(total - n);
	return out;
}

PMString KBSTail(const PMString& s, int32 n)
{
	PMString out(s);
	if (n > 0)
		out.Remove(0, (n < out.CharCount()) ? n : out.CharCount());
	return out;
}

/* Move a cut position off the middle of a surrogate pair, so neither side of the cut holds half a
   character. It cuts BEFORE the pair, whichever side is kept (KBSHead keeps [0, pos), KBSTail keeps
   [pos, end)). KCM carries the same three lines for the same doubt: PMString counts characters, so
   this may never fire - but it is correct either way. */
int32 KBSSafeCut(const PMString& s, int32 pos)
{
	const int32 total = s.CharCount();
	if (pos <= 0 || pos >= total)
		return pos;
	const uchar16 c = s.GetChar(pos).GetValue();
	if (c >= 0xDC00 && c <= 0xDFFF)		// the second half of a pair: cut in front of the first half
		return pos - 1;
	return pos;
}

/* The cut mark - kTextChar_Ellipse, the one a hit row's cut segment carries (KBSSearchEngine.cpp).
   Taken from TextChar.h rather than written as a glyph, so this file stays plain ASCII. */
PMString KBSEllipsis()
{
	PMString s;
	s.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	s.SetTranslatable(kFalse);
	return s;
}

PMReal KBSWidth(IGraphicsContext* gc, const PMString& s, const InterfaceFontInfo& font)
{
	if (s.IsEmpty())
		return PMReal(0.0);
	return StringUtils::PMMeasureString(gc, s, font, kKBSDontConvertAmpersand).X();
}

/* How many characters from the front of s fit in `room` - measured as WHOLE PREFIXES, not summed per
   character: a sum ignores the spacing a font puts between glyphs, and the error accumulates along the
   line. A binary search asks the question the drawing will ask, about six times per line. */
int32 KBSFitCount(IGraphicsContext* gc, const InterfaceFontInfo& font, const PMString& s, const PMReal& room)
{
	const int32 total = s.CharCount();
	if (total <= 0 || room <= PMReal(0.0))
		return 0;
	if (KBSWidth(gc, s, font) <= room)
		return total;

	int32 lo = 0, hi = total;
	while (lo < hi)
	{
		const int32 mid = (lo + hi + 1) / 2;
		if (KBSWidth(gc, KBSHead(s, mid), font) <= room)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

/* Is there any text after this point that the layout has not placed? */
bool16 KBSAnythingLeft(const std::vector<KBSRun>& runs, size_t atRun, const PMString& rest)
{
	if (!rest.IsEmpty())
		return kTrue;
	for (size_t i = atRun + 1; i < runs.size(); ++i)
		if (!runs[i].fText.IsEmpty())
			return kTrue;
	return kFalse;
}

/* Wrap the runs into lines and record where each fragment goes.

   @param availWidth the width of one line. @param maxLines how many lines the box holds.
   @param out [out] every fragment that FITS, in drawing order - filled even when the answer is kFalse,
          so a caller with nothing better can still draw what fits.
   @return kTrue when everything was placed; kFalse when the box ran out of lines first.

   Breaking rules, in order:
     1. a line break in the text is always honoured (only the heading's own - the pieces of a
        "Source Text:" arrive with their breaks already turned into marks, KBSResultModel::MarkUpBreaksForDisplay);
     2. a line is broken at a SPACE when there is one to break at - otherwise between characters, which
        is the only thing Japanese offers and the only thing a long file name offers;
     3. a wrapped line never starts with the space it was broken at (a line the TEXT broke keeps its
        spaces: those are the author's). */
bool16 KBSLayoutRuns(IGraphicsContext* gc, const InterfaceFontInfo& font,
					 const std::vector<KBSRun>& runs, const PMReal& availWidth,
					 int32 maxLines, std::vector<KBSFrag>& out)
{
	out.clear();
	if (maxLines <= 0 || availWidth <= PMReal(0.0))
		return runs.empty();

	int32 line = 0;
	PMReal x(0.0);
	bool16 justWrapped = kFalse;

	for (size_t r = 0; r < runs.size(); ++r)
	{
		PMString rest = runs[r].fText;
		const bool16 faded = runs[r].fFaded;
		const bool16 isCaret = runs[r].fIsCaret;

		while (!rest.IsEmpty())
		{
			// (1) A break the text asked for.
			if (rest.GetChar(0).IsLineBreakChar())
			{
				const bool16 wasCR = rest.GetChar(0).IsChar('\r');
				rest.Remove(0, 1);
				if (wasCR && !rest.IsEmpty() && rest.GetChar(0).IsChar('\n'))
					rest.Remove(0, 1);		// CRLF is one break, not two
				++line;
				x = PMReal(0.0);
				justWrapped = kFalse;		// the author's spaces on the new line are the author's
				if (line >= maxLines)
					return !KBSAnythingLeft(runs, r, rest);
				continue;
			}

			// (3) The space a wrap broke at does not start the next line.
			// EXCEPT THE BAR'S ROOM. The bar stands in one space (KBSCaretPlaceholder); thrown away as "the
			// space the wrap broke at", a bar at the head of a wrapped line would vanish with the place it
			// marks. (KCMStatusTextView.cpp, where this came from, keeps the same exception.)
			if (justWrapped && !isCaret && rest.GetChar(0).IsSpace())
			{
				rest.Remove(0, 1);
				continue;
			}

			// As much of this run as may go on one line: up to the next break it asks for.
			int32 breakAt = -1;
			const int32 restLen = rest.CharCount();
			for (int32 i = 0; i < restLen; ++i)
			{
				if (rest.GetChar(i).IsLineBreakChar())
				{
					breakAt = i;
					break;
				}
			}
			const PMString chunk = (breakAt < 0) ? rest : KBSHead(rest, breakAt);

			const PMReal room = availWidth - x;
			int32 fit = KBSFitCount(gc, font, chunk, room);

			if (fit >= chunk.CharCount())
			{
				// All of it goes on this line.
				if (!chunk.IsEmpty())
				{
					out.push_back(KBSFrag(chunk, faded, isCaret, line, x));
					x += KBSWidth(gc, chunk, font);
				}
				rest = (breakAt < 0) ? PMString() : KBSTail(rest, breakAt);	// leave the break itself
				justWrapped = kFalse;
				continue;
			}

			if (fit <= 0)
			{
				// Nothing fits in what is left of this line. Start a fresh one and try again - unless this
				// line was already fresh, in which case one character has to go down anyway or the loop
				// would never move.
				if (x > PMReal(0.0))
				{
					++line;
					x = PMReal(0.0);
					justWrapped = kTrue;
					if (line >= maxLines)
						return !KBSAnythingLeft(runs, r, rest);
					continue;
				}
				fit = 1;
			}
			else
			{
				// (2) Break at a space if this chunk offers one.
				int32 space = -1;
				for (int32 i = fit; i > 0; --i)
				{
					if (chunk.GetChar(i - 1).IsSpace())
					{
						space = i;
						break;
					}
				}
				fit = (space > 0) ? space : KBSSafeCut(chunk, fit);
			}

			const PMString head = KBSHead(chunk, fit);
			if (!head.IsEmpty())
				out.push_back(KBSFrag(head, faded, isCaret, line, x));
			rest = KBSTail(rest, fit);
			++line;
			x = PMReal(0.0);
			justWrapped = kTrue;
			if (line >= maxLines)
				return !KBSAnythingLeft(runs, r, rest);
		}
	}
	return kTrue;
}

/* The pieces as runs, with the heading on a line of its own.
   @param wantCaret draw the change as a BAR rather than as characters - only while mid is empty. */
std::vector<KBSRun> KBSMakeRuns(const PMString& label, const PMString& pre,
								const PMString& mid, const PMString& post, bool16 wantCaret)
{
	std::vector<KBSRun> runs;
	if (!label.IsEmpty())
	{
		PMString heading(label);
		heading.Append("\n");		// the heading owns its break: nothing may share its line
		// NOT faded: faded means context, and the heading is not context - it says what the words
		// below are (KCM's author made the same call there).
		runs.push_back(KBSRun(heading, kFalse));
	}
	if (!pre.IsEmpty())
		runs.push_back(KBSRun(pre, kTrue));
	if (!mid.IsEmpty())
		runs.push_back(KBSRun(mid, kFalse));
	else if (wantCaret)
		runs.push_back(KBSRun(KBSCaretPlaceholder(), kFalse, kTrue /*a place, drawn as a bar*/));
	if (!post.IsEmpty())
		runs.push_back(KBSRun(post, kTrue));
	return runs;
}

/* The leading context cut down to its last `keep` characters, with an ellipsis for what went. It loses
   its HEAD - the end facing AWAY from the change. */
PMString KBSTrimLeadingContext(const PMString& pre, int32 keep)
{
	const int32 total = pre.CharCount();
	if (pre.IsEmpty() || keep >= total)
		return pre;
	PMString out = KBSEllipsis();
	out.Append(KBSTail(pre, KBSSafeCut(pre, total - keep)));
	out.SetTranslatable(kFalse);
	return out;
}

/* The trailing context cut down to its first `keep` characters. It loses its TAIL. */
PMString KBSTrimTrailingContext(const PMString& post, int32 keep)
{
	const int32 total = post.CharCount();
	if (post.IsEmpty() || keep >= total)
		return post;
	PMString out = KBSHead(post, KBSSafeCut(post, keep));
	out.Append(KBSEllipsis());
	out.SetTranslatable(kFalse);
	return out;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KBSStatusTextData - what the message area draws
//----------------------------------------------------------------------------------------

/** Non-persistent holder for the current message, aggregated on the widget's boss beside the view.
    Written by KBSResultTree (ShowStatus and the "Source Text:" of a replaced row), read by Draw. */
class KBSStatusTextData : public CPMUnknown<IKBSStatusTextData>
{
public:
	KBSStatusTextData(IPMUnknown* boss) : CPMUnknown<IKBSStatusTextData>(boss), fWantCaret(kFalse) {}
	virtual ~KBSStatusTextData() {}

	virtual void SetSegments(const PMString& label, const PMString& pre, const PMString& mid,
		const PMString& post, bool16 wantCaret)
	{
		// Not translation keys: messages are assembled sentences and document text, and a short common
		// word left translatable can come back from the string tables as something else.
		fLabel = label;	fLabel.SetTranslatable(kFalse);
		fPre = pre;		fPre.SetTranslatable(kFalse);
		fMid = mid;		fMid.SetTranslatable(kFalse);
		fPost = post;	fPost.SetTranslatable(kFalse);
		fWantCaret = wantCaret;

		// The same message as one line of plain text on this boss's ITextControlData, where a reader
		// that walks the widgets looks for a label (KIDMCP's inspect_ui; the regression suite's
		// PSTATUS). Written here, the one place every message arrives, so the two cannot drift.
		//
		// THE STOCK ONE, INHERITED FROM kGenericPanelWidgetBoss - DO NOT AGGREGATE ANOTHER. It is
		// persistent; a non-persistent one of our own on the hit row's cell crashed InDesign the first
		// time a row was built (KBSColorTextView.cpp). Nothing draws it: this box paints itself.
		// ! Not doubled for '&' any more: the stock widget took a lone '&' as an accelerator, so the
		//   message was written "A&&B.indd" and a reader saw that. This box draws with
		//   convertAmpersand kFalse, so the text is the text.
		InterfacePtr<ITextControlData> plain(this, UseDefaultIID());
		if (plain != nil)
		{
			PMString line;
			if (fLabel.IsEmpty() && fPre.IsEmpty() && fPost.IsEmpty() && !fWantCaret)
				line = fMid;		// an ordinary message: the sentence itself
			else
			{
				// the way a hit row reads (KBSRowData::SetSegments): the part that matters in [ ]
				line = fLabel;
				if (!line.IsEmpty())
					line.Append("  ");
				line.Append(fPre);
				line.Append("[");
				line.Append(fMid);
				line.Append("]");
				line.Append(fPost);
			}
			line.SetTranslatable(kFalse);
			plain->SetString(line, kFalse /*invalidate: the view draws from the pieces*/,
				kFalse /*notify: nothing observes it*/);
		}
	}

	virtual void GetSegments(PMString& outLabel, PMString& outPre, PMString& outMid,
		PMString& outPost, bool16& outWantCaret) const
	{
		outLabel = fLabel;
		outPre = fPre;
		outMid = fMid;
		outPost = fPost;
		outWantCaret = fWantCaret;
	}

private:
	PMString	fLabel;
	PMString	fPre;
	PMString	fMid;
	PMString	fPost;
	bool16		fWantCaret;
};

CREATE_PMINTERFACE(KBSStatusTextData, kKBSStatusTextDataImpl)

//----------------------------------------------------------------------------------------
// KBSStatusTextView - the self-drawing message area
//----------------------------------------------------------------------------------------

/** Implements IControlView: wraps the message into the box and draws it in up to two colours. */
class KBSStatusTextView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KBSStatusTextView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KBSStatusTextView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KBSStatusTextView, kKBSStatusTextViewImpl)

void KBSStatusTextView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	InterfacePtr<IKBSStatusTextData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString label, pre, mid, post;
	bool16 wantCaret = kFalse;
	data->GetSegments(label, pre, mid, post, wantCaret);
	if (!mid.IsEmpty())
		wantCaret = kFalse;		// a bar stands only where there are no characters

	// NOTHING IS PAINTED BEHIND THE TEXT. The panel draws its own background; this box adds words on
	// top of it, as the stock widget did. An empty message is therefore a no-op, not a blank rectangle.
	if (label.IsEmpty() && pre.IsEmpty() && mid.IsEmpty() && post.IsEmpty() && !wantCaret)
		return;

	// The palette window's SYSTEM SCRIPT font - the hit rows' (KBSColorTextView.cpp says why: it is what
	// the shipping panels use for text that came out of a document). ! A hand-drawn widget has no font
	// field to read: its boss is a generic panel, which carries no IUIFontSpec.
	// ASKED OF KBSPanelMetrics, THE SAME PLACE THE BOX'S HEIGHT IS. The box is four of the very lines
	// drawn here - one question, one place.
	const InterfaceFontInfo* const fontPtr = KBSPanelMetrics::MessageFont();
	if (fontPtr == nil)
		return;
	const InterfaceFontInfo& fontInfo = *fontPtr;

	const PMRect frame = this->GetInnerContentFrame();
	const PMReal availWidth = frame.Width();
	if (availWidth <= PMReal(0.0))
		return;

	// ONE LINE'S ADVANCE COMES FROM THE FONT, NOT FROM A MEASURED STRING. KCM measured it (a diagnostic
	// build on a Japanese UI): ascent + descent + leading answers 18.0 where
	// PMMeasureString("Ag").Y() answers 19.0 - and that one pixel costs a whole line in a box sized to
	// a whole number of lines (KBSPanelMetrics::MessageBlockHeight). Measuring a string answers "how
	// tall is this ink", which is a different question from "how far to the next line".
	// The fallback is the measured string, so a font whose metrics are refused still draws something.
	PMReal lineHeight(0.0);
	PMReal ascent(0.0);
	if (!KBSPanelMetrics::MessageLineMetrics(lineHeight, ascent))
	{
		lineHeight = StringUtils::PMMeasureString(&gc, PMString("Ag"), fontInfo, kKBSDontConvertAmpersand).Y();
		if (lineHeight <= PMReal(0.0))
			return;
		ascent = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, lineHeight);
	}

	// A HAIR ADDED BEFORE THE CUT. This box is sized to a WHOLE number of lines, so the quotient is meant
	// to come out at exactly 4 - and a line height a rounding step over what the box was made of would
	// make it 3.9999, cut to 3, and the last line of every long message would go. KCM's box has 2px to
	// spare (74), which is why it never needed this. (The box is four of THIS line rounded up to a pixel
	// - KBSPanelMetrics::MessageBlockHeight - so the quotient is 4 or a hair over; the hair stays for a
	// frame that comes in a rounding step short.)
	int32 maxLines = static_cast<int32>(ToDouble(frame.Height() / lineHeight) + 0.01);
	if (maxLines < 1)
		maxLines = 1;		// a box too short for even one line still shows the beginning of it

	// Colours, entirely from the current theme. ! No selection pair, unlike the hit rows: a message
	// area is never selected, so the panel's fill is always what stands behind it.
	RealAGMColor bg(0.5, 0.5, 0.5), fg(0.0, 0.0, 0.0);		// sane fallbacks if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
	{
		colors->GetRealAGMColor(kInterfacePaletteFill, bg);
		colors->GetRealAGMColor(kInterfaceTextColor, fg);
	}
	const RealAGMColor kStrongColor = fg;
	const RealAGMColor kContextColor = KBSBlendColor(bg, fg, PMReal(kKBSContextTextWeight));

	// ---- lay the message out, giving the context away first if it does not fit ------------------

	std::vector<KBSFrag> frags;
	if (!KBSLayoutRuns(&gc, fontInfo, KBSMakeRuns(label, pre, mid, post, wantCaret), availWidth, maxLines, frags))
	{
		// frags holds as much of the whole message as the box could take - kept as the last resort
		// below, so nothing here has to succeed for something to be drawn.
		const int32 preLen = pre.CharCount();
		const int32 postLen = post.CharCount();
		const int32 maxKeep = (preLen > postLen) ? preLen : postLen;

		std::vector<KBSFrag> best;
		bool16 found = kFalse;

		// The search is written once; the two attempts below differ only in what they build out of
		// `keep`. ! `keep` is "how much to KEEP", so bigger is better: a fit moves the search UP and
		// overwrites `best`, which is why the answer is the LAST fit rather than the first.
		auto largestFitting = [&](int32 hi, auto build) -> bool16
		{
			bool16 any = kFalse;
			int32 lo = 0;
			while (lo <= hi)
			{
				const int32 keep = lo + (hi - lo) / 2;
				std::vector<KBSFrag> trial;
				if (KBSLayoutRuns(&gc, fontInfo, build(keep), availWidth, maxLines, trial))
				{
					best.swap(trial);
					any = kTrue;
					lo = keep + 1;
				}
				else
					hi = keep - 1;
			}
			return any;
		};

		// The largest amount of context that still fits, the same amount on each side.
		if (maxKeep > 0)
		{
			found = largestFitting(maxKeep, [&](int32 keep)
			{
				return KBSMakeRuns(label, KBSTrimLeadingContext(pre, keep), mid,
								   KBSTrimTrailingContext(post, keep), wantCaret);
			});
		}

		// Still no room: the change alone overflows the box. Cut its tail and say so. This is also the
		// branch an ordinary long message takes - it is all "change" and has no context to give away.
		if (!found)
		{
			const PMString kNothing;
			found = largestFitting(mid.CharCount(), [&](int32 keep)
			{
				PMString midCut;
				if (keep >= mid.CharCount())
					midCut = mid;
				else
				{
					midCut = KBSHead(mid, KBSSafeCut(mid, keep));
					midCut.Append(KBSEllipsis());
					midCut.SetTranslatable(kFalse);
				}
				return KBSMakeRuns(label, kNothing, midCut, kNothing, wantCaret);
			});
		}

		if (found)
			frags.swap(best);
	}

	// ---- draw -------------------------------------------------------------------------------------

	// Each fragment is drawn at an explicit BASELINE rather than centred in a line-high box: the advance
	// (18) is a pixel less than the ink the renderer reports (19), so centring would let the last line's
	// descenders drift below the box. The first baseline sits one ascent below the top.
	const PMReal baseline0 = frame.Top() + ascent;

	for (size_t i = 0; i < frags.size(); ++i)
	{
		const KBSFrag& f = frags[i];
		const PMPoint at(frame.Left() + f.fX, baseline0 + lineHeight * PMReal(f.fLine));

		// A PLACE WITH NOTHING IN IT IS DRAWN, NOT WRITTEN: the space reserved the room, the bar goes over
		// it, and the space itself never is.
		if (f.fIsCaret)
		{
			KBSDrawCaret(gPort, kStrongColor, at.X(), KBSWidth(&gc, f.fText, fontInfo),
						 at.Y() - ascent, lineHeight);
			continue;
		}

		StringUtils::PMDrawStringRGB(&gc, at, f.fText, fontInfo,
									 f.fFaded ? kContextColor : kStrongColor,
									 kKBSDontConvertAmpersand, kKBSNoUnderline);
	}
}

// End, KBSStatusTextView.cpp.
