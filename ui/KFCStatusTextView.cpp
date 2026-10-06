//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
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
//  colour - KCM shows attribute changes, and KFC replaces text.
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
//  kept (KFCPanelTextDraw.h - a measure that disagrees with the draw is the drift that file exists to
//  stop); (4) the overflow rule breaks the text again for every amount of context it tries, so the
//  breaker could replace only the inner step of that search. (1)-(3) are what the header leaves open,
//  not anything measured. KCM's box, which this one came from, carries the same hand-written wrap.
//
//  HOW MANY LINES: as many as the box holds, worked out at draw time. KFCPanelMetrics makes the box
//  four of this font's lines tall (MessageBlockHeight - 72px on a Japanese UI); a hand-drawn box has
//  no line count of its own to disagree with that, so the height and the font are the whole answer -
//  and both come from the one place, KFCPanelMetrics' MessageFont / MessageLineMetrics.
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
//    messages stay cut to fit four lines (KFCPanelMetrics.cpp).
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
#include "IKFCStatusTextData.h"
#include "KFCUIID.h"
#include "KFCPanelTextDraw.h"	// the context's fade, the '&' flags and the bar - shared with the hit rows
#include "KFCPanelMetrics.h"	// the font and its line - the same answer the box's height is made of

// Std includes:
#include <vector>

namespace
{

/* One piece of the message as it arrives: a string, and whether it is drawn faded.
   Only two colours exist in this box, and the faded one means one thing only: CONTEXT - words carried
   along so the reader can place the change. Everything the message itself says (the heading, and the
   characters that matter) is drawn at the theme's text colour. */
struct KFCRun
{
	PMString	fText;
	bool16		fFaded;
	bool16		fIsCaret;	// a PLACE, drawn as a bar: its text is the one space that reserves the room

	KFCRun() : fFaded(kTrue), fIsCaret(kFalse) {}
	KFCRun(const PMString& text, bool16 faded, bool16 isCaret = kFalse)
		: fText(text), fFaded(faded), fIsCaret(isCaret) {}
};

/* One piece of the message as it will be drawn: a run, or the part of a run that fell on one line.
   A run that crosses a line boundary becomes two fragments of the SAME colour - which is why the colour
   travels on the fragment rather than on the line. */
struct KFCFrag
{
	PMString	fText;
	bool16		fFaded;
	bool16		fIsCaret;
	int32		fLine;
	PMReal		fX;

	KFCFrag() : fFaded(kTrue), fIsCaret(kFalse), fLine(0), fX(0.0) {}
	KFCFrag(const PMString& text, bool16 faded, bool16 isCaret, int32 line, const PMReal& x)
		: fText(text), fFaded(faded), fIsCaret(isCaret), fLine(line), fX(x) {}
};

/* The first n characters, and everything from the nth on. Written with Truncate / Remove rather than
   PMString::Substring, which returns a string the caller has to delete (PMString.h). */
PMString KFCHead(const PMString& s, int32 n)
{
	PMString out(s);
	const int32 total = out.CharCount();
	if (n < 0)
		n = 0;
	if (n < total)
		out.Truncate(total - n);
	return out;
}

PMString KFCTail(const PMString& s, int32 n)
{
	PMString out(s);
	if (n > 0)
		out.Remove(0, (n < out.CharCount()) ? n : out.CharCount());
	return out;
}

/* Move a cut position off the middle of a surrogate pair, so neither side of the cut holds half a
   character. It cuts BEFORE the pair, whichever side is kept (KFCHead keeps [0, pos), KFCTail keeps
   [pos, end)). KCM carries the same three lines for the same doubt: PMString counts characters, so
   this may never fire - but it is correct either way. */
int32 KFCSafeCut(const PMString& s, int32 pos)
{
	const int32 total = s.CharCount();
	if (pos <= 0 || pos >= total)
		return pos;
	const uchar16 c = s.GetChar(pos).GetValue();
	if (c >= 0xDC00 && c <= 0xDFFF)		// the second half of a pair: cut in front of the first half
		return pos - 1;
	return pos;
}

/* The cut mark - kTextChar_Ellipse, the one a hit row's cut segment carries (KFCSearchEngine.cpp).
   Taken from TextChar.h rather than written as a glyph, so this file stays plain ASCII. */
PMString KFCEllipsis()
{
	PMString s;
	s.AppendW(static_cast<UTF32TextChar>(kTextChar_Ellipse));
	s.SetTranslatable(kFalse);
	return s;
}

PMReal KFCWidth(IGraphicsContext* gc, const PMString& s, const InterfaceFontInfo& font)
{
	if (s.IsEmpty())
		return PMReal(0.0);
	return StringUtils::PMMeasureString(gc, s, font, kKFCDontConvertAmpersand).X();
}

/* How many characters from the front of s fit in `room` - measured as WHOLE PREFIXES, not summed per
   character: a sum ignores the spacing a font puts between glyphs, and the error accumulates along the
   line. A binary search asks the question the drawing will ask, about six times per line. */
int32 KFCFitCount(IGraphicsContext* gc, const InterfaceFontInfo& font, const PMString& s, const PMReal& room)
{
	const int32 total = s.CharCount();
	if (total <= 0 || room <= PMReal(0.0))
		return 0;
	if (KFCWidth(gc, s, font) <= room)
		return total;

	int32 lo = 0, hi = total;
	while (lo < hi)
	{
		const int32 mid = (lo + hi + 1) / 2;
		if (KFCWidth(gc, KFCHead(s, mid), font) <= room)
			lo = mid;
		else
			hi = mid - 1;
	}
	return lo;
}

/* Is there any text after this point that the layout has not placed? */
bool16 KFCAnythingLeft(const std::vector<KFCRun>& runs, size_t atRun, const PMString& rest)
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
     1. a line break in the text is always honoured;
     2. a line is broken at a SPACE when there is one to break at - otherwise between characters, which
        is the only thing Japanese offers and the only thing a long file name offers;
     3. a wrapped line never starts with the space it was broken at (a line the TEXT broke keeps its
        spaces: those are the author's). */
bool16 KFCLayoutRuns(IGraphicsContext* gc, const InterfaceFontInfo& font,
					 const std::vector<KFCRun>& runs, const PMReal& availWidth,
					 int32 maxLines, std::vector<KFCFrag>& out)
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
					return !KFCAnythingLeft(runs, r, rest);
				continue;
			}

			// (3) The space a wrap broke at does not start the next line.
			// EXCEPT THE BAR'S ROOM. The bar stands in one space (KFCCaretPlaceholder); thrown away as "the
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
			const PMString chunk = (breakAt < 0) ? rest : KFCHead(rest, breakAt);

			const PMReal room = availWidth - x;
			int32 fit = KFCFitCount(gc, font, chunk, room);

			if (fit >= chunk.CharCount())
			{
				// All of it goes on this line.
				if (!chunk.IsEmpty())
				{
					out.push_back(KFCFrag(chunk, faded, isCaret, line, x));
					x += KFCWidth(gc, chunk, font);
				}
				rest = (breakAt < 0) ? PMString() : KFCTail(rest, breakAt);	// leave the break itself
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
						return !KFCAnythingLeft(runs, r, rest);
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
				fit = (space > 0) ? space : KFCSafeCut(chunk, fit);
			}

			const PMString head = KFCHead(chunk, fit);
			if (!head.IsEmpty())
				out.push_back(KFCFrag(head, faded, isCaret, line, x));
			rest = KFCTail(rest, fit);
			++line;
			x = PMReal(0.0);
			justWrapped = kTrue;
			if (line >= maxLines)
				return !KFCAnythingLeft(runs, r, rest);
		}
	}
	return kTrue;
}

/* The message as the one run the layout takes - at the theme's full text colour. */
std::vector<KFCRun> KFCMakeRuns(const PMString& message)
{
	std::vector<KFCRun> runs;
	if (!message.IsEmpty())
		runs.push_back(KFCRun(message, kFalse));
	return runs;
}

}	// anonymous namespace

//----------------------------------------------------------------------------------------
// KFCStatusTextData - what the message area draws
//----------------------------------------------------------------------------------------

/** Non-persistent holder for the current message, aggregated on the widget's boss beside the view.
    Written by KFCResultTree (ShowStatus), read by Draw. */
class KFCStatusTextData : public CPMUnknown<IKFCStatusTextData>
{
public:
	KFCStatusTextData(IPMUnknown* boss) : CPMUnknown<IKFCStatusTextData>(boss) {}
	virtual ~KFCStatusTextData() {}

	virtual void SetText(const PMString& message)
	{
		// Not a translation key: messages are assembled sentences and document text, and a short common word left
		// translatable can come back from the string tables as something else.
		fText = message;
		fText.SetTranslatable(kFalse);
		// The same message on this boss's ITextControlData, where a reader that walks the widgets looks for a label
		// (KIDMCP's inspect_ui; the regression suite's PSTATUS). THE STOCK ONE, INHERITED FROM kGenericPanelWidgetBoss -
		// do not aggregate another (a non-persistent one of our own on the hit row's cell crashed InDesign the first
		// time a row was built - KFCColorTextView.cpp). Not doubled for '&': this box draws with convertAmpersand kFalse.
		InterfacePtr<ITextControlData> plain(this, UseDefaultIID());
		if (plain != nil)
			plain->SetString(fText, kFalse /*invalidate: the view draws from fText*/, kFalse /*notify: nothing observes it*/);
	}

	virtual void GetText(PMString& outMessage) const
	{
		outMessage = fText;
	}

private:
	PMString	fText;
};

CREATE_PMINTERFACE(KFCStatusTextData, kKFCStatusTextDataImpl)

//----------------------------------------------------------------------------------------
// KFCStatusTextView - the self-drawing message area
//----------------------------------------------------------------------------------------

/** Implements IControlView: wraps the message into the box and draws it. */
class KFCStatusTextView : public DVControlView
{
	typedef DVControlView inherited;
public:
	KFCStatusTextView(IPMUnknown* boss) : inherited(boss) {}
	virtual ~KFCStatusTextView() {}

	virtual void Draw(IViewPort* viewPort, SysRgn updateRgn);
};

CREATE_PERSIST_PMINTERFACE(KFCStatusTextView, kKFCStatusTextViewImpl)

void KFCStatusTextView::Draw(IViewPort* viewPort, SysRgn updateRgn)
{
	AGMGraphicsContext gc(viewPort, this, updateRgn);
	InterfacePtr<IGraphicsPort> gPort(gc.GetViewPort(), UseDefaultIID());
	if (gPort == nil)
		return;
	AutoGSave gSave(gPort);

	InterfacePtr<IKFCStatusTextData> data(this, UseDefaultIID());
	if (data == nil)
		return;

	PMString message;
	data->GetText(message);

	// NOTHING IS PAINTED BEHIND THE TEXT. The panel draws its own background; this box adds words on
	// top of it, as the stock widget did. An empty message is therefore a no-op, not a blank rectangle.
	if (message.IsEmpty())
		return;

	// The palette window's SYSTEM SCRIPT font - the hit rows' (KFCColorTextView.cpp says why: it is what
	// the shipping panels use for text that came out of a document). ! A hand-drawn widget has no font
	// field to read: its boss is a generic panel, which carries no IUIFontSpec.
	// ASKED OF KFCPanelMetrics, THE SAME PLACE THE BOX'S HEIGHT IS. The box is four of the very lines
	// drawn here - one question, one place.
	const InterfaceFontInfo* const fontPtr = KFCPanelMetrics::MessageFont();
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
	// a whole number of lines (KFCPanelMetrics::MessageBlockHeight). Measuring a string answers "how
	// tall is this ink", which is a different question from "how far to the next line".
	// The fallback is the measured string, so a font whose metrics are refused still draws something.
	PMReal lineHeight(0.0);
	PMReal ascent(0.0);
	if (!KFCPanelMetrics::MessageLineMetrics(lineHeight, ascent))
	{
		lineHeight = StringUtils::PMMeasureString(&gc, PMString("Ag"), fontInfo, kKFCDontConvertAmpersand).Y();
		if (lineHeight <= PMReal(0.0))
			return;
		ascent = Utils<IWidgetUtils>()->GetViewYPosition(&gc, fontInfo, lineHeight);
	}

	// A HAIR ADDED BEFORE THE CUT. This box is sized to a WHOLE number of lines, so the quotient is meant
	// to come out at exactly 4 - and a line height a rounding step over what the box was made of would
	// make it 3.9999, cut to 3, and the last line of every long message would go. KCM's box has 2px to
	// spare (74), which is why it never needed this. (The box is four of THIS line rounded up to a pixel
	// - KFCPanelMetrics::MessageBlockHeight - so the quotient is 4 or a hair over; the hair stays for a
	// frame that comes in a rounding step short.)
	int32 maxLines = static_cast<int32>(ToDouble(frame.Height() / lineHeight) + 0.01);
	if (maxLines < 1)
		maxLines = 1;		// a box too short for even one line still shows the beginning of it

	// The text colour, from the current theme. ! No selection pair, unlike the hit rows: a message
	// area is never selected.
	RealAGMColor fg(0.0, 0.0, 0.0);		// a sane fallback if the query fails
	InterfacePtr<IInterfaceColors> colors(GetExecutionContextSession(), UseDefaultIID());
	if (colors != nil)
		colors->GetRealAGMColor(kInterfaceTextColor, fg);
	const RealAGMColor kStrongColor = fg;

	// ---- lay the message out, cutting its tail if it does not fit ---------------------------------

	std::vector<KFCFrag> frags;
	if (!KFCLayoutRuns(&gc, fontInfo, KFCMakeRuns(message), availWidth, maxLines, frags))
	{
		// The message overflows the box: the most of its head that fits, with an ellipsis. frags already holds as much
		// of the whole message as the box could take - kept as the last resort, so nothing here has to succeed.
		// ! `keep` is "how much to KEEP", so bigger is better: a fit moves the search UP and overwrites `best`, which
		// is why the answer is the LAST fit rather than the first.
		std::vector<KFCFrag> best;
		bool16 found = kFalse;
		int32 lo = 0, hi = message.CharCount();
		while (lo <= hi)
		{
			const int32 keep = lo + (hi - lo) / 2;
			PMString cut;
			if (keep >= message.CharCount())
				cut = message;
			else
			{
				cut = KFCHead(message, KFCSafeCut(message, keep));
				cut.Append(KFCEllipsis());
				cut.SetTranslatable(kFalse);
			}
			std::vector<KFCFrag> trial;
			if (KFCLayoutRuns(&gc, fontInfo, KFCMakeRuns(cut), availWidth, maxLines, trial))
			{
				best.swap(trial);
				found = kTrue;
				lo = keep + 1;
			}
			else
				hi = keep - 1;
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
		const KFCFrag& f = frags[i];
		const PMPoint at(frame.Left() + f.fX, baseline0 + lineHeight * PMReal(f.fLine));

		StringUtils::PMDrawStringRGB(&gc, at, f.fText, fontInfo, kStrongColor,
									 kKFCDontConvertAmpersand, kKFCNoUnderline);
	}
}

// End, KFCStatusTextView.cpp.
