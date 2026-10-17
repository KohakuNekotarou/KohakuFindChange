//========================================================================================
//
//  Owner: KohakuNekotarou
//
//  KohakuFindChange (KFC)
//
//  The values an object row's transform asks first - see KFCSpecialDialogs.h.
//  LAID OUT AS INDESIGN'S OWN (the author, 2026-10-17 night: "photograph the official Move, Rotate and Scale dialogs and
//  make KFC's look like them" - work/note-scripts/2026-10-17-kfc-object-special/official-*.png): the fields on the left,
//  each label before its box; OK and Cancel stacked on the right; a value shown with its unit ("0 mm", "100%", "0" and the
//  degree sign); Move's Horizontal and Vertical in a Position panel, with Distance and Angle under a line, each pair
//  worked out from the other; Scale's chain between its two fields; Shear's Axis. What InDesign's has and these have not:
//  Copy (a transformed copy - a row would have nothing to follow) and Preview. The words that explained KFC's own ways (a
//  number alone in ruler units, each object about its own centre) went into the fields' tips, where InDesign's dialogs
//  say nothing.
//
//========================================================================================

#include "VCPlugInHeaders.h"

// Interface includes:
#include "IExtendScriptUtils.h"		// RunScriptInEngine - the dialog, and its answer read back

// General includes:
#include "PMString.h"
#include "ScriptData.h"				// ScriptData / ScriptRecordData - the script handed over, its answer
#include "Utils.h"

#include <cmath>					// std::fabs
#include <cstdio>					// std::snprintf
#include <cstdlib>					// std::strtod
#include <string>
#include <vector>

// Project includes:
#include "KFCSpecialDialogs.h"

namespace
{
// WHAT WAS TYPED LAST, offered again for the session (E12) - a distance as typed, so it keeps its unit; a percentage or an
// angle as the number it was read as. Fixed buffers: nothing to tear down when the plug-in goes (an ASCII text the script
// handed back, or the default).
const size_t kTyped = 40;
char gMoveH[kTyped] = "0";
char gMoveV[kTyped] = "0";
char gScaleX[kTyped] = "100";
char gScaleY[kTyped] = "100";
bool gScaleConstrain = true;
char gRotate[kTyped] = "0";
char gShear[kTyped] = "0";
bool gShearVertical = false;

void Keep(char (&slot)[kTyped], const std::string& typed)
{
	if (!typed.empty())
		std::snprintf(slot, kTyped, "%s", typed.c_str());
}

// A JavaScript string literal's body: printable ASCII as it is, the quote and the backslash escaped - the script is pure
// ASCII, whatever came back from the last one (KFCHowTo's rule).
std::string JsLiteral(const char* text)
{
	std::string out;
	for (const char* p = text; *p != 0; ++p)
	{
		const unsigned char ch = static_cast<unsigned char>(*p);
		if (ch == '"' || ch == '\\')
		{
			out += '\\';
			out += static_cast<char>(ch);
		}
		else if (ch >= 0x20 && ch <= 0x7E)
			out += static_cast<char>(ch);
	}
	return out;
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to)
{
	for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
		s.replace(at, from.size(), to);
}

// THE DIALOG'S SCRIPT, one per operation, ASCII (the degree sign is the escape \u00B0). The shared head: the units, the
// readers and the writers of a value, ASCII-only echoes of what was typed, a field and the button column as InDesign's
// dialogs lay them out; then the operation's own window. Each answers "ok;<x>;<y>;<typed>..." or "cancel".
// ! ExtendScript is ES3: no nested ?: (memory extendscript-nested-ternary-is-left-associative) - none is written here.
const char* const kHead = R"JS(
(function () {
  var U = { pt: 1, mm: 72 / 25.4, cm: 72 / 2.54, "in": 72, pc: 12, px: 1, q: 72 / 25.4 / 4, h: 72 / 25.4 / 4 };
  var SHOWN = { pt: "pt", mm: "mm", cm: "cm", "in": "in", pc: "pc", px: "px", q: "Q", h: "H" };
  var DEG = "\u00B0";
  function unitOf(mu) {
    try {
      if (mu == MeasurementUnits.MILLIMETERS) return "mm";
      if (mu == MeasurementUnits.CENTIMETERS) return "cm";
      if (mu == MeasurementUnits.INCHES || mu == MeasurementUnits.INCHES_DECIMAL) return "in";
      if (mu == MeasurementUnits.PICAS) return "pc";
      if (mu == MeasurementUnits.PIXELS) return "px";
      if (mu == MeasurementUnits.Q) return "q";
      if (mu == MeasurementUnits.HA) return "h";
    } catch (e) {}
    return "pt";
  }
  var hu = "pt", vu = "pt";
  try { var vp = app.activeDocument.viewPreferences; hu = unitOf(vp.horizontalMeasurementUnits); vu = unitOf(vp.verticalMeasurementUnits); } catch (e) {}
  function trim(s) { return String(s).replace(/^\s+|\s+$/g, ""); }
  // A distance in points: a number alone in the ruler's unit, or a number and its unit.
  function len(text, unit) {
    var m = trim(text).toLowerCase().match(/^([-+]?(?:\d+\.?\d*|\.\d+))\s*(pt|mm|cm|in|pc|px|q|h)?$/);
    if (!m) return null;
    return parseFloat(m[1]) * U[m[2] || unit];
  }
  // A number, with the suffix allowed after it (a regular expression's text).
  function num(text, suffix) {
    var m = trim(text).toLowerCase().match(new RegExp("^([-+]?(?:\\d+\\.?\\d*|\\.\\d+))\\s*" + suffix + "$"));
    if (!m) return null;
    return parseFloat(m[1]);
  }
  function angle(text) { return num(text, "(?:" + DEG + "|deg)?"); }
  // An angle left is written again with the degree sign ("30" becomes "30" and the sign), as InDesign's fields write it.
  function degrees(e) { var a = angle(e.text); if (a !== null) e.text = fmt(a) + DEG; }
  // A number written as InDesign's fields write one: three places at most, no trailing zeros.
  function fmt(n) { var r = Math.round(n * 1000) / 1000; if (Math.abs(r) < 0.0005) r = 0; return String(r); }
  function lenText(pts, unit) { return fmt(pts / U[unit]) + " " + SHOWN[unit]; }
  // What was typed last, shown as InDesign shows a value: a number alone with the ruler's unit after it.
  function shownLen(typed, unit) {
    var t = trim(typed);
    if (/^[-+]?(?:\d+\.?\d*|\.\d+)$/.test(t)) return t + " " + SHOWN[unit];
    return t;
  }
  function ascii(s) { s = trim(s); return /^[\x20-\x3a\x3c-\x7e]*$/.test(s) ? s : ""; }
  // A field row as InDesign's dialogs have it: the label, then the box - rows right-aligned, so the boxes line up.
  function field(parent, label, text, tip) {
    var g = parent.add("group");
    g.add("statictext", undefined, label);
    var e = g.add("edittext", undefined, text);
    e.characters = 9;
    if (tip) { e.helpTip = tip; }
    return e;
  }
  // The window: fields on the left, OK and Cancel stacked on the right. Answers the OK button.
  function frame(title) {
    var w = new Window("dialog", title);
    w.orientation = "row";
    w.alignChildren = "top";
    w.spacing = 18;
    w.margins = [18, 16, 18, 16];
    return w;
  }
  function buttons(w) {
    var b = w.add("group");
    b.orientation = "column";
    b.alignChildren = "fill";
    b.spacing = 8;
    var ok = b.add("button", undefined, "OK", { name: "ok" });
    b.add("button", undefined, "Cancel", { name: "cancel" });
    return ok;
  }
  var answer = "cancel";
)JS";

const char* const kMove = R"JS(
  var w = frame("Kohaku Find/Change - Move");
  var pos = w.add("panel", undefined, "Position");
  pos.orientation = "column";
  pos.alignChildren = "right";
  pos.margins = [14, 18, 14, 12];
  var tip = "A number alone is in the document's ruler units (" + SHOWN[hu] + "), or type pt, mm, cm, in, pc, px, Q or H. Right and down are positive; every object moves by the same distance.";
  var eh = field(pos, "Horizontal:", shownLen("@@H@@", hu), tip);
  var ev = field(pos, "Vertical:", shownLen("@@V@@", vu), tip);
  var line = pos.add("panel");
  line.alignment = "fill";
  line.minimumSize.height = 1;
  line.maximumSize.height = 1;
  var ed = field(pos, "Distance:", "", "How far each object moves - Horizontal and Vertical worked out from it and the Angle.");
  var ea = field(pos, "Angle:", "", "The direction, counter-clockwise from the right - as InDesign's own Move.");
  // Each pair worked out from the other when it is left, as InDesign's Move does - and what was typed written again with
  // its unit ("10" becomes "10 mm"), as InDesign's fields write it.
  function fromXY() {
    var x = len(eh.text, hu), y = len(ev.text, vu);
    if (x === null || y === null) return;
    eh.text = lenText(x, hu);
    ev.text = lenText(y, vu);
    ed.text = lenText(Math.sqrt(x * x + y * y), hu);
    var a = 0;
    if (x !== 0 || y !== 0) a = Math.atan2(-y, x) * 180 / Math.PI;
    ea.text = fmt(a) + DEG;
  }
  function fromDA() {
    var d = len(ed.text, hu), a = angle(ea.text);
    if (d === null || a === null) return;
    var r = a * Math.PI / 180;
    ed.text = lenText(d, hu);
    ea.text = fmt(a) + DEG;
    eh.text = lenText(d * Math.cos(r), hu);
    ev.text = lenText(-d * Math.sin(r), vu);
  }
  eh.onChange = fromXY; ev.onChange = fromXY; ed.onChange = fromDA; ea.onChange = fromDA;
  fromXY();
  var ok = buttons(w);
  ok.onClick = function () {
    var x = len(eh.text, hu), y = len(ev.text, vu);
    if (x === null || y === null) { alert("Type a distance - a number, or a number and one of pt, mm, cm, in, pc, px, Q, H."); return; }
    answer = "ok;" + x + ";" + y + ";" + ascii(eh.text) + ";" + ascii(ev.text);
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

const char* const kScale = R"JS(
  var w = frame("Kohaku Find/Change - Scale");
  var g = w.add("group");
  g.orientation = "row";
  g.alignChildren = "center";
  g.spacing = 6;
  var col = g.add("group");
  col.orientation = "column";
  col.alignChildren = "right";
  var tip = "In percent. Each object is scaled about its own centre.";
  var ex = field(col, "Scale X:", "@@X@@%", tip);
  var ey = field(col, "Scale Y:", "@@Y@@%", tip);
  // THE CHAIN, as InDesign's: pressed, the two values are one; let go, each its own. Drawn here - a link closed or open.
  var chain = g.add("iconbutton", undefined, undefined, { style: "toolbutton", toggle: true });
  chain.preferredSize = [20, 34];
  chain.value = @@C@@;
  chain.helpTip = "Constrain proportions for scaling";
  chain.onDraw = function () {
    try {
      var gr = this.graphics, W = this.size[0], H = this.size[1];
      var dark = true;
      try { dark = app.generalPreferences.uiBrightnessPreference < 0.5; } catch (e1) {}
      var inkColour = [0.25, 0.25, 0.25, 1], fillColour = [0.78, 0.78, 0.78, 1];
      if (dark) { inkColour = [0.85, 0.85, 0.85, 1]; fillColour = [0.36, 0.36, 0.36, 1]; }
      if (this.value) {
        gr.newPath(); gr.rectPath(0, 0, W, H); gr.fillPath(gr.newBrush(gr.BrushType.SOLID_COLOR, fillColour));
      }
      var pen = gr.newPen(gr.PenType.SOLID_COLOR, inkColour, 1.5);
      var lw = 7, lh = 11, x = (W - lw) / 2;
      var gap = 3;
      if (this.value) gap = -3;
      var top = (H - (2 * lh + gap)) / 2;
      gr.newPath(); gr.ellipsePath(x, top, lw, lh); gr.strokePath(pen);
      gr.newPath(); gr.ellipsePath(x, top + lh + gap, lw, lh); gr.strokePath(pen);
    } catch (e) {}
  };
  ex.onChanging = function () { if (chain.value) ey.text = ex.text; };
  ey.onChanging = function () { if (chain.value) ex.text = ey.text; };
  chain.onClick = function () { if (chain.value) ey.text = ex.text; };
  // A value left is written again with its sign ("50" becomes "50%"), as InDesign's fields write it.
  function percent(e) { var v = num(e.text, "%?"); if (v !== null) e.text = fmt(v) + "%"; }
  ex.onChange = function () { percent(ex); if (chain.value) ey.text = ex.text; };
  ey.onChange = function () { percent(ey); if (chain.value) ex.text = ey.text; };
  var ok = buttons(w);
  ok.onClick = function () {
    var x = num(ex.text, "%?"), y = num(ey.text, "%?");
    if (chain.value) y = x;
    if (x === null || y === null || x === 0 || y === 0) { alert("Type a percentage other than 0."); return; }
    answer = "ok;" + x + ";" + y + ";" + fmt(x) + ";" + fmt(y) + ";" + (chain.value ? "1" : "0");
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

const char* const kRotate = R"JS(
  var w = frame("Kohaku Find/Change - Rotate");
  var col = w.add("group");
  col.orientation = "column";
  col.alignChildren = "right";
  var ea = field(col, "Angle:", "@@A@@" + DEG, "Counter-clockwise is positive, as in InDesign's own Rotate. Each object turns about its own centre.");
  ea.onChange = function () { degrees(ea); };
  var ok = buttons(w);
  ok.onClick = function () {
    var a = angle(ea.text);
    if (a === null) { alert("Type an angle in degrees."); return; }
    answer = "ok;" + a + ";0;" + fmt(a);
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

const char* const kShear = R"JS(
  var w = frame("Kohaku Find/Change - Shear");
  var col = w.add("group");
  col.orientation = "column";
  col.alignChildren = "left";
  col.spacing = 10;
  var ea = field(col, "Shear Angle:", "@@A@@" + DEG, "Between -90 and 90 degrees. Each object is sheared about its own centre.");
  ea.onChange = function () { degrees(ea); };
  var axis = col.add("panel", undefined, "Axis");
  axis.orientation = "column";
  axis.alignChildren = "left";
  axis.alignment = "fill";
  axis.margins = [14, 18, 14, 10];
  var rh = axis.add("radiobutton", undefined, "Horizontal");
  var rv = axis.add("radiobutton", undefined, "Vertical");
  rv.value = @@V@@;
  rh.value = !rv.value;
  var ok = buttons(w);
  ok.onClick = function () {
    var a = angle(ea.text);
    if (a === null || a <= -90 || a >= 90) { alert("Type an angle between -90 and 90 degrees."); return; }
    answer = "ok;" + a + ";" + (rv.value ? "1" : "0") + ";" + fmt(a);
    w.close(1);
  };
  w.show();
  return answer;
})();
)JS";

// The answer's fields, split at ';'.
std::vector<std::string> Fields(const std::string& answer)
{
	std::vector<std::string> out;
	size_t from = 0;
	for (size_t at = answer.find(';'); ; at = answer.find(';', from))
	{
		out.push_back(answer.substr(from, at == std::string::npos ? std::string::npos : at - from));
		if (at == std::string::npos)
			break;
		from = at + 1;
	}
	return out;
}
}	// anonymous namespace

const char* KFCSpecialDialogs::OpName(int32 op)
{
	switch (op)
	{
		case kKFCSpecialMove:	return "Move";
		case kKFCSpecialScale:	return "Scale";
		case kKFCSpecialRotate:	return "Rotate";
		case kKFCSpecialShear:	return "Shear";
		default:				return "";
	}
}

bool KFCSpecialDialogs::NothingToDo(int32 op, const KFCSpecialParams& params)
{
	const double kTiny = 1e-9;
	switch (op)
	{
		case kKFCSpecialMove:	return std::fabs(params.x) < kTiny && std::fabs(params.y) < kTiny;
		case kKFCSpecialScale:	return std::fabs(params.x - 100.0) < kTiny && std::fabs(params.y - 100.0) < kTiny;
		case kKFCSpecialRotate:
		case kKFCSpecialShear:	return std::fabs(params.x) < kTiny;		// a shear of 0 is nothing on either axis
		default:				return false;
	}
}

bool KFCSpecialDialogs::AskTransformValues(int32 op, KFCSpecialParams& io, PMString& outWhy)
{
	outWhy.Clear();
	outWhy.SetTranslatable(kFalse);
	std::string js(kHead);
	switch (op)
	{
		case kKFCSpecialMove:
		{
			std::string body(kMove);
			ReplaceAll(body, "@@H@@", JsLiteral(gMoveH));
			ReplaceAll(body, "@@V@@", JsLiteral(gMoveV));
			js += body;
			break;
		}
		case kKFCSpecialScale:
		{
			std::string body(kScale);
			ReplaceAll(body, "@@X@@", JsLiteral(gScaleX));
			ReplaceAll(body, "@@Y@@", JsLiteral(gScaleY));
			ReplaceAll(body, "@@C@@", gScaleConstrain ? "true" : "false");
			js += body;
			break;
		}
		case kKFCSpecialRotate:
		{
			std::string body(kRotate);
			ReplaceAll(body, "@@A@@", JsLiteral(gRotate));
			js += body;
			break;
		}
		case kKFCSpecialShear:
		{
			std::string body(kShear);
			ReplaceAll(body, "@@A@@", JsLiteral(gShear));
			ReplaceAll(body, "@@V@@", gShearVertical ? "true" : "false");
			js += body;
			break;
		}
		default:
			return false;
	}

	// RUN THROUGH THE PLUG-IN'S OWN ENGINE (KFCHowTo's - "KFC", out of the user's #targetengine sessions), the answer read
	// back (KIDMCP's RunOnce is the shape: a script handed as ScriptData, no arguments, the result and an error string; no
	// alert of the runner's own - an error is said here).
	Utils<IExtendScriptUtils> esUtils;
	if (!esUtils.Exists())
	{
		outWhy = OpName(op);
		outWhy.Append(": the dialog could not be shown (no script engine) - nothing was changed.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	PMString scriptText;
	scriptText.SetTranslatable(kFalse);
	scriptText.SetUTF8String(js);
	ScriptData script;
	script.SetPMString(scriptText);
	ScriptRecordData arguments;
	ScriptData result;
	PMString errorString;
	const ErrorCode ran = esUtils->RunScriptInEngine("KFC", script, arguments, result, errorString, kFalse /*showErrorAlert*/,
		kFalse /*invokeDebugger*/);
	PMString answerText;
	if (ran != kSuccess || !errorString.IsEmpty() || result.GetType() != ScriptData::s_string
		|| result.GetPMString(answerText) != kSuccess)
	{
		outWhy = OpName(op);
		outWhy.Append(": the dialog could not be shown - nothing was changed.");
		outWhy.SetTranslatable(kFalse);
		return false;
	}
	const std::vector<std::string> f = Fields(answerText.GetUTF8String());
	if (f.size() < 3 || f[0] != "ok")
		return false;		// Cancel: nothing written, nothing said
	io.x = std::strtod(f[1].c_str(), nil);
	io.y = std::strtod(f[2].c_str(), nil);
	// What was typed, kept for the next one (an empty echo - not ASCII - keeps the last).
	switch (op)
	{
		case kKFCSpecialMove:
			if (f.size() >= 5) { Keep(gMoveH, f[3]); Keep(gMoveV, f[4]); }
			break;
		case kKFCSpecialScale:
			if (f.size() >= 6) { Keep(gScaleX, f[3]); Keep(gScaleY, f[4]); gScaleConstrain = (f[5] == "1"); }
			break;
		case kKFCSpecialRotate:
			if (f.size() >= 4) Keep(gRotate, f[3]);
			break;
		case kKFCSpecialShear:
			if (f.size() >= 4) Keep(gShear, f[3]);
			gShearVertical = (io.y > 0.5);
			break;
		default:
			break;
	}
	return true;
}

// End, KFCSpecialDialogs.cpp.
